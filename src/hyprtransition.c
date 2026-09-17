// hyprtransition — purely visual workspace transitions for Hyprland.
//
// Screenshots one output, covers it with a click-through overlay, plays a GLSL
// effect over the screenshot on a transparent background, exits. Wherever the
// effect outputs alpha 0, whatever the compositor shows underneath comes through.
// See effects/_template.glsl for the effect contract.

#define _GNU_SOURCE
#include <getopt.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#ifndef DATADIR
#define DATADIR "/usr/share/hyprtransition"
#endif
#ifndef VERSION
#define VERSION "dev"
#endif

#define LAYER_NAMESPACE "hyprtransition"
#define DEFAULT_EFFECT "tear"
#define DEFAULT_DURATION_MS 700
#define MAX_OUTPUTS 16

struct options {
    const char *effect, *output, *then;
    double duration_ms;
    float seed;
    bool cursor, loop;
};

struct output {
    struct wl_output *handle;
    char name[64];
    int width, height, transform;
};

struct wayland {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wp_viewporter *viewporter;
    struct output outputs[MAX_OUTPUTS];
    int output_count;
    struct output *output;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer;
    struct wp_viewport *viewport;
    int logical_width, logical_height;
    bool configured, closed;
};

struct gl {
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    struct wl_egl_window *window;
    GLuint program, texture;
    GLint u_time, u_seed, u_aspect, u_resolution;
};

static struct {
    struct options opt;
    struct wayland wl;
    struct gl gl;
    char *effect_path;
    int width, height;
    double started_at;
    bool tracing;
} app;

// ---------------------------------------------------------------- helpers

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static _Noreturn void fail(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fputs("hyprtransition: ", stderr);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
    exit(1);
}

static void warn(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fputs("hyprtransition: ", stderr);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
}

static void trace(const char *step) {
    static double origin;
    if (!origin) origin = now_ms();
    if (app.tracing) fprintf(stderr, "hyprtransition: %6.1f ms  %s\n", now_ms() - origin, step);
}

static char *format(const char *fmt, ...) {
    char *out;
    va_list args;
    va_start(args, fmt);
    if (vasprintf(&out, fmt, args) < 0) fail("out of memory");
    va_end(args);
    return out;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *text = malloc(size + 1);
    size_t got = fread(text, 1, size, f);
    fclose(f);
    text[got] = 0;
    return text;
}

// $VAR if set, else $HOME/<fallback>; NULL when neither is known.
static char *xdg_dir(const char *var, const char *fallback) {
    const char *set = getenv(var), *home = getenv("HOME");
    if (set) return strdup(set);
    return home ? format("%s/%s", home, fallback) : NULL;
}

static char *config_dir(void) {
    char *base = xdg_dir("XDG_CONFIG_HOME", ".config");
    return base ? format("%s/hyprtransition", base) : NULL;
}

static char *exe_dir(void) {
    static char path[4096];
    ssize_t len = readlink("/proc/self/exe", path, sizeof path - 1);
    if (len < 0) return NULL;
    path[len] = 0;
    return dirname(path);
}

// Double fork so the child is reparented to init and never needs reaping.
static void run_detached(const char *command) {
    pid_t pid = fork();
    if (pid == 0) {
        if (fork() == 0) execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(0);
    }
    waitpid(pid, NULL, 0);
}

// ---------------------------------------------------------------- options

static _Noreturn void usage(int status) {
    fputs("usage: hyprtransition [-e EFFECT] [-o OUTPUT] [-d MS] [-s SEED] [-c] [--loop] [--then CMD]\n"
          "\n"
          "  -e, --effect NAME     effect name or path to a .glsl file (default: tear)\n"
          "  -o, --output NAME     monitor to play on (default: the focused one)\n"
          "  -d, --duration MS     override the effect's own duration\n"
          "  -s, --seed N          fix the randomness\n"
          "  -c, --cursor          include the mouse cursor in the screenshot\n"
          "      --loop            replay forever, re-reading the effect file each cycle\n"
          "      --then CMD        run CMD once the overlay is on screen\n"
          "  -v, --version\n"
          "\n"
          "Defaults for -e, -d and -c come from $XDG_CONFIG_HOME/hyprtransition/config.lua.\n",
          status ? stderr : stdout);
    exit(status);
}

static void parse_args(int argc, char **argv) {
    static const struct option long_options[] = {
        { "effect", required_argument, NULL, 'e' }, { "output", required_argument, NULL, 'o' },
        { "duration", required_argument, NULL, 'd' }, { "seed", required_argument, NULL, 's' },
        { "cursor", no_argument, NULL, 'c' },         { "loop", no_argument, NULL, 'L' },
        { "then", required_argument, NULL, 'T' },     { "help", no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'v' },        { 0 },
    };
    struct options *opt = &app.opt;
    int c;
    while ((c = getopt_long(argc, argv, "e:o:d:s:chv", long_options, NULL)) != -1) {
        switch (c) {
        case 'e': opt->effect = optarg; break;
        case 'o': opt->output = optarg; break;
        case 'd': opt->duration_ms = atof(optarg); break;
        case 's': opt->seed = atof(optarg); break;
        case 'c': opt->cursor = true; break;
        case 'L': opt->loop = true; break;
        case 'T': opt->then = optarg; break;
        case 'h': usage(0);
        case 'v': puts("hyprtransition " VERSION); exit(0);
        default: usage(2);
        }
    }
}

// ---------------------------------------------------------------- config.lua

static char *lua_string_field(lua_State *L, const char *key) {
    lua_getfield(L, 1, key);
    bool list = lua_istable(L, -1) && lua_rawlen(L, -1) > 0;
    if (list) lua_rawgeti(L, -1, 1 + rand() % (int)lua_rawlen(L, -1));
    const char *value = lua_tostring(L, -1);
    char *copy = value ? strdup(value) : NULL;
    lua_settop(L, 1);
    return copy;
}

static double lua_number_field(lua_State *L, const char *key) {
    lua_getfield(L, 1, key);
    double value = lua_tonumber(L, -1);
    lua_settop(L, 1);
    return value;
}

static bool lua_bool_field(lua_State *L, const char *key) {
    lua_getfield(L, 1, key);
    bool value = lua_toboolean(L, -1);
    lua_settop(L, 1);
    return value;
}

// Fills in whatever the command line left unset. Same file the Lua module reads.
static void apply_config_file(void) {
    char *dir = config_dir();
    if (!dir) return;
    char *path = format("%s/config.lua", dir);
    if (access(path, R_OK) != 0) return;

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    if (luaL_dofile(L, path) != LUA_OK) {
        warn("%s", lua_tostring(L, -1));
    } else if (!lua_istable(L, 1)) {
        warn("%s must `return { ... }`", path);
    } else {
        struct options *opt = &app.opt;
        if (!opt->effect) opt->effect = lua_string_field(L, "effect");
        if (opt->duration_ms <= 0) opt->duration_ms = lua_number_field(L, "duration");
        opt->cursor = opt->cursor || lua_bool_field(L, "cursor");
    }
    lua_close(L);
}

// ---------------------------------------------------------------- effects

// Earlier directories shadow later ones, so a user's tear.glsl beats the bundled one.
static char **effect_dirs(void) {
    static char *dirs[8];
    int n = 0;
    char *env = getenv("HYPRTRANSITION_EFFECTS"), *config = config_dir(), *data = xdg_dir("XDG_DATA_HOME", ".local/share"), *exe = exe_dir();
    if (env) dirs[n++] = strdup(env);
    if (config) dirs[n++] = format("%s/effects", config);
    if (data) dirs[n++] = format("%s/hyprtransition/effects", data);
    if (exe) dirs[n++] = format("%s/effects", exe);
    if (exe) dirs[n++] = format("%s/../effects", exe);
    dirs[n++] = DATADIR "/effects";
    dirs[n] = NULL;
    return dirs;
}

static char *find_effect(const char *name) {
    if (strchr(name, '/')) return strdup(name);
    char **dirs = effect_dirs();
    for (int i = 0; dirs[i]; i++) {
        char *path = format("%s/%s.glsl", dirs[i], name);
        if (access(path, R_OK) == 0) return path;
        free(path);
    }
    warn("no effect named '%s' in:", name);
    for (int i = 0; dirs[i]; i++) fprintf(stderr, "  %s\n", dirs[i]);
    exit(1);
}

static double declared_duration(const char *source) {
    const char *tag = "// duration:";
    const char *found = strstr(source, tag);
    return found ? atof(found + strlen(tag)) : 0;
}

// ---------------------------------------------------------------- hyprctl

static char *focused_output_name(void) {
    FILE *pipe = popen("hyprctl activeworkspace -j 2>/dev/null", "r");
    if (!pipe) return NULL;
    char line[512], name[64] = "";
    while (fgets(line, sizeof line, pipe)) {
        char *field = strstr(line, "\"monitor\": \"");
        if (field) sscanf(field + 12, "%63[^\"]", name);
    }
    pclose(pipe);
    return name[0] ? strdup(name) : NULL;
}

// ---------------------------------------------------------------- wayland

static void on_output_geometry(void *data, struct wl_output *o, int32_t x, int32_t y, int32_t pw, int32_t ph,
                               int32_t subpixel, const char *make, const char *model, int32_t transform) {
    (void)o; (void)x; (void)y; (void)pw; (void)ph; (void)subpixel; (void)make; (void)model;
    ((struct output *)data)->transform = transform;
}

static void on_output_mode(void *data, struct wl_output *o, uint32_t flags, int32_t w, int32_t h, int32_t refresh) {
    (void)o; (void)refresh;
    struct output *out = data;
    if (flags & WL_OUTPUT_MODE_CURRENT) out->width = w, out->height = h;
}

static void on_output_name(void *data, struct wl_output *o, const char *name) {
    (void)o;
    snprintf(((struct output *)data)->name, sizeof ((struct output *)data)->name, "%s", name);
}

static void on_output_ignore(void *data, struct wl_output *o) { (void)data; (void)o; }
static void on_output_scale(void *data, struct wl_output *o, int32_t s) { (void)data; (void)o; (void)s; }
static void on_output_description(void *data, struct wl_output *o, const char *d) { (void)data; (void)o; (void)d; }

static const struct wl_output_listener output_listener = {
    .geometry = on_output_geometry, .mode = on_output_mode, .done = on_output_ignore,
    .scale = on_output_scale, .name = on_output_name, .description = on_output_description,
};

static void on_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
    (void)data;
    struct wayland *wl = &app.wl;
    if (!strcmp(interface, wl_compositor_interface.name))
        wl->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    if (!strcmp(interface, zwlr_layer_shell_v1_interface.name))
        wl->layer_shell = wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, version < 4 ? version : 4);
    if (!strcmp(interface, wp_viewporter_interface.name))
        wl->viewporter = wl_registry_bind(registry, id, &wp_viewporter_interface, 1);
    bool named_output = !strcmp(interface, wl_output_interface.name) && version >= 4;
    if (named_output && wl->output_count < MAX_OUTPUTS) {
        struct output *out = &wl->outputs[wl->output_count++];
        out->handle = wl_registry_bind(registry, id, &wl_output_interface, 4);
        wl_output_add_listener(out->handle, &output_listener, out);
    }
}

static void on_global_remove(void *data, struct wl_registry *registry, uint32_t id) { (void)data; (void)registry; (void)id; }
static const struct wl_registry_listener registry_listener = { on_global, on_global_remove };

static void on_layer_configure(void *data, struct zwlr_layer_surface_v1 *layer, uint32_t serial, uint32_t w, uint32_t h) {
    (void)data;
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    app.wl.logical_width = w;
    app.wl.logical_height = h;
    app.wl.configured = true;
}

static void on_layer_closed(void *data, struct zwlr_layer_surface_v1 *layer) {
    (void)data; (void)layer;
    app.wl.closed = true;
}

static const struct zwlr_layer_surface_v1_listener layer_listener = { on_layer_configure, on_layer_closed };

static void connect_wayland(void) {
    struct wayland *wl = &app.wl;
    wl->display = wl_display_connect(NULL);
    if (!wl->display) fail("cannot connect to the Wayland display");
    wl_registry_add_listener(wl_display_get_registry(wl->display), &registry_listener, NULL);
    wl_display_roundtrip(wl->display); // globals
    wl_display_roundtrip(wl->display); // output events
    if (!wl->compositor || !wl->layer_shell || !wl->viewporter) fail("compositor lacks wl_compositor, layer-shell or viewporter");
    if (wl->output_count == 0) fail("no outputs");
}

static struct output *find_output(const char *name) {
    struct wayland *wl = &app.wl;
    if (!name) return &wl->outputs[0];
    for (int i = 0; i < wl->output_count; i++)
        if (!strcmp(wl->outputs[i].name, name)) return &wl->outputs[i];
    fail("no output named '%s'", name);
    return NULL;
}

static void create_overlay(void) {
    struct wayland *wl = &app.wl;
    wl->surface = wl_compositor_create_surface(wl->compositor);
    struct wl_region *nothing = wl_compositor_create_region(wl->compositor);
    wl_surface_set_input_region(wl->surface, nothing); // clicks pass through
    wl_region_destroy(nothing);
    wl->viewport = wp_viewporter_get_viewport(wl->viewporter, wl->surface);

    wl->layer = zwlr_layer_shell_v1_get_layer_surface(wl->layer_shell, wl->surface, wl->output->handle,
                                                      ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, LAYER_NAMESPACE);
    zwlr_layer_surface_v1_add_listener(wl->layer, &layer_listener, NULL);
    zwlr_layer_surface_v1_set_anchor(wl->layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                                    ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(wl->layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(wl->layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    zwlr_layer_surface_v1_set_size(wl->layer, 0, 0);
    wl_surface_commit(wl->surface);
    while (!wl->configured && wl_display_dispatch(wl->display) != -1) {}
    if (wl->logical_width <= 0 || wl->logical_height <= 0) fail("compositor gave the overlay no size");

    // Render at physical resolution so the screenshot stays pixel-exact under
    // fractional scaling; the viewport maps the buffer onto the logical size.
    struct output *out = wl->output;
    bool rotated = out->transform & 1;
    app.width = rotated ? out->height : out->width;
    app.height = rotated ? out->width : out->height;
    if (app.width <= 0 || app.height <= 0) app.width = wl->logical_width, app.height = wl->logical_height;
    wp_viewport_set_destination(wl->viewport, wl->logical_width, wl->logical_height);
}

// ---------------------------------------------------------------- shaders

#include "shaders.h" // VERTEX_SHADER, EFFECT_PRELUDE, EFFECT_POSTLUDE from src/*.glsl

static GLuint compile_shader(GLenum type, const char *source, const char *label) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok) return shader;
    char log[4096];
    glGetShaderInfoLog(shader, sizeof log, NULL, log);
    warn("%s failed to compile:\n%s", label, log);
    glDeleteShader(shader);
    return 0;
}

static GLuint link_program(GLuint vertex, GLuint fragment) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glBindAttribLocation(program, 0, "a_pos");
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok) return program;
    char log[4096];
    glGetProgramInfoLog(program, sizeof log, NULL, log);
    warn("%s failed to link:\n%s", app.effect_path, log);
    glDeleteProgram(program);
    return 0;
}

static void use_program(GLuint program) {
    struct gl *gl = &app.gl;
    if (gl->program) glDeleteProgram(gl->program);
    gl->program = program;
    glUseProgram(program);
    gl->u_time = glGetUniformLocation(program, "u_time");
    gl->u_seed = glGetUniformLocation(program, "u_seed");
    gl->u_aspect = glGetUniformLocation(program, "u_aspect");
    gl->u_resolution = glGetUniformLocation(program, "u_resolution");
    glUniform1i(glGetUniformLocation(program, "u_tex"), 0);
    glUniform1f(gl->u_seed, app.opt.seed);
    glUniform1f(gl->u_aspect, (float)app.wl.logical_width / app.wl.logical_height);
    glUniform2f(gl->u_resolution, app.width, app.height);
}

// Keeps the current program when the file is missing or broken.
static bool load_effect(void) {
    char *body = read_file(app.effect_path);
    if (!body) {
        warn("cannot read %s", app.effect_path);
        return false;
    }
    if (app.opt.duration_ms <= 0) app.opt.duration_ms = declared_duration(body);
    if (app.opt.duration_ms <= 0) app.opt.duration_ms = DEFAULT_DURATION_MS;

    char *source = format("%s%s%s", EFFECT_PRELUDE, body, EFFECT_POSTLUDE);
    free(body);
    GLuint vertex = compile_shader(GL_VERTEX_SHADER, VERTEX_SHADER, "vertex shader");
    GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, source, app.effect_path);
    free(source);
    if (!vertex || !fragment) return false;

    GLuint program = link_program(vertex, fragment);
    if (!program) return false;
    use_program(program);
    return true;
}

// ---------------------------------------------------------------- gl

static void init_gl(void) {
    struct gl *gl = &app.gl;
    gl->display = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, app.wl.display, NULL);
    if (gl->display == EGL_NO_DISPLAY || !eglInitialize(gl->display, NULL, NULL)) fail("eglInitialize failed");
    eglBindAPI(EGL_OPENGL_ES_API);

    static const EGLint config_attribs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                                             EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
    static const EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLConfig config;
    EGLint count;
    if (!eglChooseConfig(gl->display, config_attribs, &config, 1, &count) || count < 1) fail("no EGL config with alpha");
    gl->context = eglCreateContext(gl->display, config, EGL_NO_CONTEXT, context_attribs);
    if (gl->context == EGL_NO_CONTEXT) fail("eglCreateContext failed");

    gl->window = wl_egl_window_create(app.wl.surface, app.width, app.height);
    gl->surface = eglCreateWindowSurface(gl->display, config, (EGLNativeWindowType)gl->window, NULL);
    if (gl->surface == EGL_NO_SURFACE) fail("eglCreateWindowSurface failed");
    eglMakeCurrent(gl->display, gl->surface, gl->surface, gl->context);
    eglSwapInterval(gl->display, 0); // frame callbacks pace us instead

    static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    GLuint buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glGenTextures(1, &gl->texture);
    glBindTexture(GL_TEXTURE_2D, gl->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glDisable(GL_BLEND); // effects output premultiplied alpha in a single pass
    glViewport(0, 0, app.width, app.height);
    glClearColor(0, 0, 0, 0);
}

static void destroy_gl(void) {
    struct gl *gl = &app.gl;
    eglMakeCurrent(gl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(gl->display, gl->surface);
    eglDestroyContext(gl->display, gl->context);
    wl_egl_window_destroy(gl->window);
}

// ---------------------------------------------------------------- screenshot

static FILE *start_screenshot(void) {
    char *command = format("grim %s -o '%s' -t ppm -", app.opt.cursor ? "-c" : "", app.wl.output->name);
    FILE *pipe = popen(command, "r");
    free(command);
    if (!pipe) fail("cannot run grim");
    return pipe;
}

// Binary PPM straight from grim's stdout into the texture.
static void upload_screenshot(FILE *pipe) {
    int width, height, maxval;
    if (fscanf(pipe, "P6 %d %d %d", &width, &height, &maxval) != 3 || maxval != 255) fail("grim did not produce a P6 image");
    fgetc(pipe);
    size_t size = (size_t)width * height * 3;
    unsigned char *pixels = malloc(size);
    if (!pixels || fread(pixels, 1, size, pipe) != size) fail("short read from grim");
    if (pclose(pipe) != 0) fail("grim failed");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    free(pixels);
}

// ---------------------------------------------------------------- frames

static void on_frame(void *data, struct wl_callback *callback, uint32_t time);
static const struct wl_callback_listener frame_listener = { on_frame };

static float progress(void) {
    if (!app.started_at) return 0;
    return (now_ms() - app.started_at) / app.opt.duration_ms;
}

static void restart_with_fresh_effect(void) {
    app.opt.duration_ms = 0;
    if (!load_effect()) app.opt.duration_ms = DEFAULT_DURATION_MS;
    app.started_at = now_ms();
}

static void render(float t) {
    glClear(GL_COLOR_BUFFER_BIT);
    glUniform1f(app.gl.u_time, t);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    wl_callback_add_listener(wl_surface_frame(app.wl.surface), &frame_listener, NULL);
    eglSwapBuffers(app.gl.display, app.gl.surface);
}

static void on_frame(void *data, struct wl_callback *callback, uint32_t time) {
    (void)data; (void)time;
    wl_callback_destroy(callback);
    if (!app.started_at) app.started_at = now_ms(), trace("first frame presented");

    float t = progress();
    bool finished = t >= 1;
    if (finished && !app.opt.loop) {
        app.wl.closed = true;
        return;
    }
    if (finished) restart_with_fresh_effect(), t = 0;
    render(t);
}

// ---------------------------------------------------------------- main

int main(int argc, char **argv) {
    app.tracing = getenv("HYPRTRANSITION_DEBUG") != NULL;
    app.opt.seed = -1;
    parse_args(argc, argv);
    srand((unsigned)(now_ms() * 1000) ^ (unsigned)getpid());
    apply_config_file();
    if (app.opt.seed < 0) app.opt.seed = rand() % 1000;
    if (!app.opt.output) app.opt.output = focused_output_name();
    app.effect_path = find_effect(app.opt.effect ? app.opt.effect : DEFAULT_EFFECT);

    connect_wayland();
    app.wl.output = find_output(app.opt.output);
    FILE *screenshot = start_screenshot(); // grim runs while the overlay is set up
    create_overlay();
    trace("overlay configured");

    init_gl();
    if (!load_effect()) fail("no usable effect");
    trace("gl ready");
    upload_screenshot(screenshot);
    trace("screenshot uploaded");

    render(0);
    wl_display_flush(app.wl.display);
    trace("first frame committed");
    if (app.opt.then) run_detached(app.opt.then);

    while (!app.wl.closed && wl_display_dispatch(app.wl.display) != -1) {}

    destroy_gl();
    zwlr_layer_surface_v1_destroy(app.wl.layer);
    wl_surface_destroy(app.wl.surface);
    wl_display_disconnect(app.wl.display);
    return 0;
}
