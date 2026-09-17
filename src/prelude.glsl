#version 100
// Everything an effect file may use. Documented in effects/_template.glsl.
precision highp float;
varying vec2 v_uv;
uniform sampler2D u_tex;
uniform float u_time;
uniform float u_seed;
uniform float u_aspect;
uniform vec2 u_resolution;

float hash(float n) { return fract(sin(n * 127.1 + u_seed * 311.7) * 43758.5453); }
float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7)) + u_seed * 74.7) * 43758.5453); }
float vnoise(float x) { float i = floor(x); return mix(hash(i), hash(i + 1.0), fract(x)); }
float vnoise2(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1, 0)), f.x), mix(hash2(i + vec2(0, 1)), hash2(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) { return vnoise2(p) * 0.5 + vnoise2(p * 2.03) * 0.25 + vnoise2(p * 4.11) * 0.125 + vnoise2(p * 8.3) * 0.0625; }
float ease_in(float x) { return x * x; }
float ease_out(float x) { return 1.0 - (1.0 - x) * (1.0 - x); }
float ease_in_out(float x) { return x < 0.5 ? 2.0 * x * x : 1.0 - pow(-2.0 * x + 2.0, 2.0) / 2.0; }
float phase(float from, float to, float t) { return clamp((t - from) / (to - from), 0.0, 1.0); }
bool inside(vec2 uv) { return uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0; }
vec4 screen(vec2 uv) { return inside(uv) ? vec4(texture2D(u_tex, uv).rgb, 1.0) : vec4(0.0); }
vec2 rotate_about(vec2 uv, vec2 pivot, float angle) {
    vec2 p = (uv - pivot) * vec2(u_aspect, 1.0);
    float c = cos(angle), s = sin(angle);
    p = vec2(c * p.x - s * p.y, s * p.x + c * p.y);
    return p / vec2(u_aspect, 1.0) + pivot;
}
#line 1
