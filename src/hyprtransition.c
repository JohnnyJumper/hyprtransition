// hyprtransition — purely visual workspace transitions for Hyprland.
//
// 1. screenshots one output (via grim)
// 2. opens a click-through, fullscreen overlay layer surface on that output
// 3. plays an effect over the screenshot: a GLSL file from effects/ that
//    decides, per pixel and per moment, what to show. Wherever it outputs
//    alpha 0, whatever the compositor is showing underneath comes through
//    (i.e. the workspace you switched to)
// 4. exits
//
// It never touches workspaces itself. Optionally it runs a command once the
// overlay is on screen (--then), which is where a workspace switch belongs.
//
//   hyprtransition [-e EFFECT] [-o OUTPUT] [-d MS] [-s SEED] [-c] [--loop] [--then CMD]
//
//   -e EFFECT   effect name or a path to a .glsl file. Default: tear. Names are
//               looked up in $HYPRTRANSITION_EFFECTS, ~/.config/hypr/hyprtransition/effects,
//               <dir of binary>/effects, <dir of binary>/../effects, DATADIR/effects
//   -o OUTPUT   output name (e.g. DP-3). Default: the focused monitor (via hyprctl)
//   -d MS       total duration in ms. Default: the effect's "// duration:" line, else 700
//   -s SEED     randomness seed (default: random)
//   -c          include the cursor in the screenshot
//   --loop      replay forever, re-reading the effect file each time (for authoring)
//   --then CMD  run CMD (via sh -c) right after the first frame is committed
//
// The effect contract is documented in effects/_template.glsl.

#define _GNU_SOURCE
#include <errno.h>
#include <libgen.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define NAMESPACE "hyprtransition"
#define DEFAULT_EFFECT "tear"
#define DEFAULT_DURATION 700
#ifndef DATADIR
#define DATADIR "/usr/share/hyprtransition"
#endif

struct output {
    struct wl_output *wl;
    char name[64];
    int32_t mode_w, mode_h;
    int32_t transform;
};

struct state {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wp_viewporter *viewporter;

    struct output outputs[16];
    int n_outputs;
    struct output *out;

    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer;
    struct wp_viewport *viewport;
    struct wl_egl_window *egl_window;
    int32_t lw, lh; // logical size (from configure)
    int32_t bw, bh; // buffer size (physical pixels)
    bool configured, done;

    EGLDisplay egl;
    EGLContext ctx;
    EGLSurface esurf;
    GLuint prog, tex;
    GLint u_time, u_seed, u_aspect, u_resolution;

    double t0, duration_ms;
    float seed;
    bool cursor, loop;
    const char *out_name, *then, *effect;
    char effect_path[4096];
};

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static bool debug;
static double t_start;
static void mark(const char *what) {
    if (debug) fprintf(stderr, "hyprtransition: %6.1f ms  %s\n", now_ms() - t_start, what);
}

static void die(const char *msg) {
    fprintf(stderr, "hyprtransition: %s\n", msg);
    exit(1);
}

// ---------------------------------------------------------------- wl_output

static void out_geometry(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t pw, int32_t ph, int32_t sub,
                         const char *make, const char *model, int32_t transform) {
    (void)o; (void)x; (void)y; (void)pw; (void)ph; (void)sub; (void)make; (void)model;
    ((struct output *)d)->transform = transform;
}
static void out_mode(void *d, struct wl_output *o, uint32_t flags, int32_t w, int32_t h, int32_t refresh) {
    (void)o; (void)refresh;
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        ((struct output *)d)->mode_w = w;
        ((struct output *)d)->mode_h = h;
    }
}
static void out_done(void *d, struct wl_output *o) { (void)d; (void)o; }
static void out_scale(void *d, struct wl_output *o, int32_t s) { (void)d; (void)o; (void)s; }
static void out_name(void *d, struct wl_output *o, const char *name) {
    (void)o;
    snprintf(((struct output *)d)->name, sizeof(((struct output *)d)->name), "%s", name);
}
static void out_desc(void *d, struct wl_output *o, const char *desc) { (void)d; (void)o; (void)desc; }

static const struct wl_output_listener output_listener = {
    .geometry = out_geometry, .mode = out_mode, .done = out_done,
    .scale = out_scale, .name = out_name, .description = out_desc,
};

// ---------------------------------------------------------------- registry

static void reg_global(void *d, struct wl_registry *reg, uint32_t id, const char *iface, uint32_t ver) {
    struct state *st = d;
    if (!strcmp(iface, wl_compositor_interface.name)) {
        st->compositor = wl_registry_bind(reg, id, &wl_compositor_interface, 4);
    } else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name)) {
        st->layer_shell = wl_registry_bind(reg, id, &zwlr_layer_shell_v1_interface, ver < 4 ? ver : 4);
    } else if (!strcmp(iface, wp_viewporter_interface.name)) {
        st->viewporter = wl_registry_bind(reg, id, &wp_viewporter_interface, 1);
    } else if (!strcmp(iface, wl_output_interface.name) && st->n_outputs < 16) {
        if (ver < 4) return; // need the name event
        struct output *o = &st->outputs[st->n_outputs++];
        o->wl = wl_registry_bind(reg, id, &wl_output_interface, 4);
        wl_output_add_listener(o->wl, &output_listener, o);
    }
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t id) { (void)d; (void)r; (void)id; }
static const struct wl_registry_listener registry_listener = { reg_global, reg_remove };

// ---------------------------------------------------------------- layer surface

static void layer_configure(void *d, struct zwlr_layer_surface_v1 *ls, uint32_t serial, uint32_t w, uint32_t h) {
    struct state *st = d;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    st->lw = (int32_t)w;
    st->lh = (int32_t)h;
    st->configured = true;
}
static void layer_closed(void *d, struct zwlr_layer_surface_v1 *ls) {
    (void)ls;
    ((struct state *)d)->done = true;
}
static const struct zwlr_layer_surface_v1_listener layer_listener = { layer_configure, layer_closed };

// ---------------------------------------------------------------- effect files

static const char *VS =
    "attribute vec2 a_pos;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  v_uv = vec2(a_pos.x * 0.5 + 0.5, 0.5 - a_pos.y * 0.5);\n" // (0,0) = top-left, like the screenshot
    "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

// Everything an effect file can rely on. Keep in sync with effects/_template.glsl.
static const char *PRELUDE =
    "precision highp float;\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "uniform float u_time;\n"
    "uniform float u_seed;\n"
    "uniform float u_aspect;\n"
    "uniform vec2 u_resolution;\n"
    "\n"
    "float hash(float n) { return fract(sin(n * 127.1 + u_seed * 311.7) * 43758.5453); }\n"
    "float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7)) + u_seed * 74.7) * 43758.5453); }\n"
    "float vnoise(float x) { float i = floor(x); return mix(hash(i), hash(i + 1.0), fract(x)); }\n"
    "float vnoise2(vec2 p) {\n"
    "  vec2 i = floor(p), f = fract(p);\n"
    "  f = f * f * (3.0 - 2.0 * f);\n"
    "  return mix(mix(hash2(i), hash2(i + vec2(1, 0)), f.x), mix(hash2(i + vec2(0, 1)), hash2(i + vec2(1, 1)), f.x), f.y);\n"
    "}\n"
    "float fbm(vec2 p) { return vnoise2(p) * 0.5 + vnoise2(p * 2.03) * 0.25 + vnoise2(p * 4.11) * 0.125 + vnoise2(p * 8.3) * 0.0625; }\n"
    "float ease_in(float x) { return x * x; }\n"
    "float ease_out(float x) { return 1.0 - (1.0 - x) * (1.0 - x); }\n"
    "float ease_in_out(float x) { return x < 0.5 ? 2.0 * x * x : 1.0 - pow(-2.0 * x + 2.0, 2.0) / 2.0; }\n"
    "float phase(float from, float to, float t) { return clamp((t - from) / (to - from), 0.0, 1.0); }\n"
    "bool inside(vec2 uv) { return uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0; }\n"
    "vec4 screen(vec2 uv) { return inside(uv) ? vec4(texture2D(u_tex, uv).rgb, 1.0) : vec4(0.0); }\n"
    "vec2 rotate_about(vec2 uv, vec2 pivot, float angle) {\n"
    "  vec2 p = (uv - pivot) * vec2(u_aspect, 1.0);\n"
    "  float c = cos(angle), s = sin(angle);\n"
    "  p = vec2(c * p.x - s * p.y, s * p.x + c * p.y);\n"
    "  return p / vec2(u_aspect, 1.0) + pivot;\n"
    "}\n"
    "#line 1\n";

static const char *POSTLUDE =
    "\nvoid main() {\n"
    "  vec4 c = effect(v_uv, u_time);\n"
    "  gl_FragColor = vec4(c.rgb * c.a, c.a);\n" // straight alpha in, premultiplied out
    "}\n";

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(n + 1);
    if (fread(buf, 1, n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    buf[n] = 0;
    fclose(f);
    return buf;
}

// "-e some/path.glsl" is used as is; "-e NAME" is searched for as NAME.glsl in:
// $HYPRTRANSITION_EFFECTS, ~/.config/hypr/hyprtransition/effects, next to the
// binary (effects/ and ../effects/, for running from a checkout), then DATADIR.
static void resolve_effect(struct state *st) {
    if (strchr(st->effect, '/')) {
        snprintf(st->effect_path, sizeof st->effect_path, "%s", st->effect);
        return;
    }
    char exe[4096], dirs[6][1024];
    int n_dirs = 0;
    const char *env = getenv("HYPRTRANSITION_EFFECTS"), *home = getenv("HOME");
    if (env) snprintf(dirs[n_dirs++], 1024, "%s", env);
    if (home) snprintf(dirs[n_dirs++], 1024, "%s/.config/hypr/hyprtransition/effects", home);
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n >= 0) {
        exe[n] = 0;
        const char *d = dirname(exe);
        snprintf(dirs[n_dirs++], 1024, "%s/effects", d);
        snprintf(dirs[n_dirs++], 1024, "%s/../effects", d);
    }
    snprintf(dirs[n_dirs++], 1024, "%s/effects", DATADIR);

    for (int i = 0; i < n_dirs; i++) {
        snprintf(st->effect_path, sizeof st->effect_path, "%.1023s/%.255s.glsl", dirs[i], st->effect);
        if (access(st->effect_path, R_OK) == 0) return;
    }
    fprintf(stderr, "hyprtransition: no effect named '%s' in:\n", st->effect);
    for (int i = 0; i < n_dirs; i++) fprintf(stderr, "  %s\n", dirs[i]);
    exit(1);
}

// Asks hyprctl which monitor is focused. Returns false if that fails for any reason.
static bool focused_output(char *out, size_t n) {
    FILE *f = popen("hyprctl monitors -j 2>/dev/null", "r");
    if (!f) return false;
    char line[512], name[64] = "";
    bool found = false;
    // hyprctl prints one key per line; "name" precedes "focused" within each monitor
    while (fgets(line, sizeof line, f)) {
        char *p;
        if ((p = strstr(line, "\"name\": \""))) {
            p += 9;
            char *e = strchr(p, '"');
            if (e) { *e = 0; snprintf(name, sizeof name, "%s", p); }
        } else if (strstr(line, "\"focused\": true") && name[0]) {
            snprintf(out, n, "%s", name);
            found = true;
        }
    }
    pclose(f);
    return found;
}

// Reads an optional "// duration: MS" line from the effect source.
static double effect_duration(const char *src) {
    const char *p = strstr(src, "// duration:");
    return p ? atof(p + strlen("// duration:")) : 0;
}

static GLuint compile(GLenum type, const char *src, const char *what) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "hyprtransition: %s failed to compile:\n%s", what, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

// Builds the program from the effect file. Returns false (and keeps the old
// program, if any) when the file is missing or does not compile.
static bool load_effect(struct state *st) {
    char *src = read_file(st->effect_path);
    if (!src) {
        fprintf(stderr, "hyprtransition: cannot read effect %s\n", st->effect_path);
        return false;
    }
    if (st->duration_ms <= 0) {
        double d = effect_duration(src);
        st->duration_ms = d > 0 ? d : DEFAULT_DURATION;
    }

    size_t n = strlen(PRELUDE) + strlen(src) + strlen(POSTLUDE) + 1;
    char *full = malloc(n);
    snprintf(full, n, "%s%s%s", PRELUDE, src, POSTLUDE);
    free(src);

    GLuint vs = compile(GL_VERTEX_SHADER, VS, "vertex shader");
    GLuint fs = compile(GL_FRAGMENT_SHADER, full, st->effect_path);
    free(full);
    if (!vs || !fs) return false;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "a_pos");
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(prog, sizeof log, NULL, log);
        fprintf(stderr, "hyprtransition: %s failed to link:\n%s", st->effect_path, log);
        glDeleteProgram(prog);
        return false;
    }

    if (st->prog) glDeleteProgram(st->prog);
    st->prog = prog;
    glUseProgram(prog);
    st->u_time = glGetUniformLocation(prog, "u_time");
    st->u_seed = glGetUniformLocation(prog, "u_seed");
    st->u_aspect = glGetUniformLocation(prog, "u_aspect");
    st->u_resolution = glGetUniformLocation(prog, "u_resolution");
    glUniform1i(glGetUniformLocation(prog, "u_tex"), 0);
    glUniform1f(st->u_seed, st->seed);
    glUniform1f(st->u_aspect, (float)st->lw / (float)st->lh);
    glUniform2f(st->u_resolution, (float)st->bw, (float)st->bh);
    return true;
}

// ---------------------------------------------------------------- GL

static void gl_init(struct state *st) {
    st->egl = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, st->display, NULL);
    if (st->egl == EGL_NO_DISPLAY) st->egl = eglGetDisplay((EGLNativeDisplayType)st->display);
    if (st->egl == EGL_NO_DISPLAY || !eglInitialize(st->egl, NULL, NULL)) die("eglInitialize failed");
    eglBindAPI(EGL_OPENGL_ES_API);
    mark("  eglInitialize");

    EGLint attr[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
    EGLConfig cfg;
    EGLint n;
    if (!eglChooseConfig(st->egl, attr, &cfg, 1, &n) || n < 1) die("no EGL config with alpha");

    EGLint cattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    st->ctx = eglCreateContext(st->egl, cfg, EGL_NO_CONTEXT, cattr);
    if (st->ctx == EGL_NO_CONTEXT) die("eglCreateContext failed");

    st->egl_window = wl_egl_window_create(st->surface, st->bw, st->bh);
    st->esurf = eglCreateWindowSurface(st->egl, cfg, (EGLNativeWindowType)st->egl_window, NULL);
    if (st->esurf == EGL_NO_SURFACE) die("eglCreateWindowSurface failed");
    eglMakeCurrent(st->egl, st->esurf, st->esurf, st->ctx);
    eglSwapInterval(st->egl, 0); // paced by frame callbacks instead
    mark("  context + surface");

    static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glGenTextures(1, &st->tex);
    glBindTexture(GL_TEXTURE_2D, st->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glDisable(GL_BLEND); // effect output is premultiplied and drawn once
    glViewport(0, 0, st->bw, st->bh);
    glClearColor(0, 0, 0, 0);

    if (!load_effect(st)) die("no usable effect");
    mark("  effect compiled");
}

// Reads a binary PPM (P6) from grim's stdout straight into the texture.
static void load_screenshot(FILE *f) {
    int w, h, maxv;
    if (fscanf(f, "P6 %d %d %d", &w, &h, &maxv) != 3 || maxv != 255) die("grim did not produce a P6 image");
    fgetc(f); // single whitespace after maxval
    size_t n = (size_t)w * h * 3;
    unsigned char *px = malloc(n);
    if (!px || fread(px, 1, n, f) != n) die("short read from grim");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
    free(px);
}

// ---------------------------------------------------------------- frame loop

static void frame_done(void *d, struct wl_callback *cb, uint32_t time);
static const struct wl_callback_listener frame_listener = { frame_done };

static void draw(struct state *st) {
    float t = st->t0 == 0 ? 0.f : (float)((now_ms() - st->t0) / st->duration_ms);
    if (t >= 1.f) {
        if (!st->loop) {
            st->done = true;
            return;
        }
        // authoring mode: pick up edits, keep the old program if the new one is broken
        st->duration_ms = 0;
        if (!load_effect(st)) st->duration_ms = DEFAULT_DURATION;
        st->t0 = now_ms();
        t = 0.f;
    }
    glClear(GL_COLOR_BUFFER_BIT);
    glUniform1f(st->u_time, t);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    struct wl_callback *cb = wl_surface_frame(st->surface);
    wl_callback_add_listener(cb, &frame_listener, st);
    eglSwapBuffers(st->egl, st->esurf);
}

static void frame_done(void *d, struct wl_callback *cb, uint32_t time) {
    (void)time;
    struct state *st = d;
    wl_callback_destroy(cb);
    if (st->t0 == 0) { st->t0 = now_ms(); mark("first frame presented"); } // clock starts here
    draw(st);
}

// ---------------------------------------------------------------- main

// Double-fork so the command is reparented to init and we never have to reap it.
static void run_then(const char *cmd) {
    if (!cmd) return;
    pid_t pid = fork();
    if (pid == 0) {
        if (fork() == 0) {
            execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0) waitpid(pid, NULL, 0);
}

static void usage(void) {
    fprintf(stderr, "usage: hyprtransition [-e EFFECT] [-o OUTPUT] [-d MS] [-s SEED] [-c] [--loop] [--then CMD]\n");
    exit(2);
}

int main(int argc, char **argv) {
    struct state st = { .seed = -1, .effect = DEFAULT_EFFECT };
    t_start = now_ms();
    debug = getenv("HYPRTRANSITION_DEBUG") != NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-e") && i + 1 < argc) st.effect = argv[++i];
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) st.out_name = argv[++i];
        else if (!strcmp(argv[i], "-d") && i + 1 < argc) st.duration_ms = atof(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) st.seed = atof(argv[++i]);
        else if (!strcmp(argv[i], "-c")) st.cursor = true;
        else if (!strcmp(argv[i], "--loop")) st.loop = true;
        else if (!strcmp(argv[i], "--then") && i + 1 < argc) st.then = argv[++i];
        else usage();
    }
    if (st.seed < 0) {
        srand((unsigned)(now_ms() * 1000) ^ (unsigned)getpid());
        st.seed = (float)(rand() % 1000);
    }
    resolve_effect(&st);
    char focused[64];
    if (!st.out_name && focused_output(focused, sizeof focused)) st.out_name = focused;

    st.display = wl_display_connect(NULL);
    if (!st.display) die("cannot connect to Wayland display");
    struct wl_registry *reg = wl_display_get_registry(st.display);
    wl_registry_add_listener(reg, &registry_listener, &st);
    wl_display_roundtrip(st.display); // globals
    wl_display_roundtrip(st.display); // output events
    mark("wayland globals");
    if (!st.compositor || !st.layer_shell || !st.viewporter) die("compositor lacks wl_compositor/layer-shell/viewporter");
    if (st.n_outputs == 0) die("no outputs");

    st.out = &st.outputs[0];
    if (st.out_name) {
        st.out = NULL;
        for (int i = 0; i < st.n_outputs; i++)
            if (!strcmp(st.outputs[i].name, st.out_name)) st.out = &st.outputs[i];
        if (!st.out) die("no such output");
    }

    // Kick off the screenshot first; it runs while we set up the overlay.
    char cmd[256];
    snprintf(cmd, sizeof cmd, "grim %s -o '%s' -t ppm -", st.cursor ? "-c" : "", st.out->name);
    FILE *shot = popen(cmd, "r");
    if (!shot) die("cannot run grim");

    st.surface = wl_compositor_create_surface(st.compositor);
    struct wl_region *empty = wl_compositor_create_region(st.compositor);
    wl_surface_set_input_region(st.surface, empty); // click-through
    wl_region_destroy(empty);
    st.viewport = wp_viewporter_get_viewport(st.viewporter, st.surface);

    st.layer = zwlr_layer_shell_v1_get_layer_surface(st.layer_shell, st.surface, st.out->wl,
                                                     ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, NAMESPACE);
    zwlr_layer_surface_v1_add_listener(st.layer, &layer_listener, &st);
    zwlr_layer_surface_v1_set_anchor(st.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                                   ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(st.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(st.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    zwlr_layer_surface_v1_set_size(st.layer, 0, 0);
    wl_surface_commit(st.surface);
    while (!st.configured && wl_display_dispatch(st.display) != -1) {}
    if (st.lw <= 0 || st.lh <= 0) die("compositor gave us no size");
    mark("layer surface configured");

    // Render at the output's physical resolution so the screenshot is pixel-exact
    // under fractional scaling; the viewport maps it onto the logical size.
    bool rotated = st.out->transform & 1;
    st.bw = rotated ? st.out->mode_h : st.out->mode_w;
    st.bh = rotated ? st.out->mode_w : st.out->mode_h;
    if (st.bw <= 0 || st.bh <= 0) { st.bw = st.lw; st.bh = st.lh; }
    wp_viewport_set_destination(st.viewport, st.lw, st.lh);

    gl_init(&st);
    mark("egl/gl ready");
    load_screenshot(shot);
    if (pclose(shot) != 0) die("grim failed");
    mark("screenshot uploaded");

    draw(&st); // first frame at t=0, overlay is now mapped
    wl_display_flush(st.display);
    mark("first frame committed");
    run_then(st.then);

    while (!st.done && wl_display_dispatch(st.display) != -1) {}

    eglMakeCurrent(st.egl, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(st.egl, st.esurf);
    eglDestroyContext(st.egl, st.ctx);
    wl_egl_window_destroy(st.egl_window);
    zwlr_layer_surface_v1_destroy(st.layer);
    wl_surface_destroy(st.surface);
    wl_display_flush(st.display);
    wl_display_disconnect(st.display);
    return 0;
}
