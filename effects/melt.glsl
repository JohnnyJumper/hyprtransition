// duration: 850
// melt — the screen sags and drips down like hot wax, uncovering the new workspace from the top.

vec4 effect(vec2 uv, float t) {
    float p = ease_in(t);
    // each column sinks at its own speed: broad smooth waves plus finer drips (vnoise2 is smooth; vnoise is jagged)
    float speed = 0.5 + 1.0 * vnoise2(vec2(uv.x * 4.0, 0.5)) + 0.35 * vnoise2(vec2(uv.x * 22.0, 7.5));
    float sink = p * 1.6 * speed;

    // pixels moved down by `sink`, and the wax stretches as it flows
    float stretch = 1.0 + sink * 0.6;
    float sy = (uv.y - sink) / stretch;
    if (sy < 0.0) return vec4(0.0);
    vec2 src = vec2(uv.x, sy);
    if (!inside(src)) return vec4(0.0);

    vec3 col = texture2D(u_tex, src).rgb;
    col *= 1.0 - 0.35 * clamp(sink, 0.0, 1.0);             // darkens as it runs
    col += vec3(0.08) * smoothstep(0.02, 0.0, sy) * p;      // wet highlight on the sagging top edge
    return vec4(col, 1.0);
}
