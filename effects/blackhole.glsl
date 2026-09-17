// duration: 850
// blackhole — the screen twists and is sucked into a singularity, revealing the new workspace from the edges in.

vec4 effect(vec2 uv, float t) {
    vec2 center = vec2(0.35 + hash(1.0) * 0.3, 0.35 + hash(2.0) * 0.3);
    vec2 d = (uv - center) * vec2(u_aspect, 1.0);         // aspect-correct offset from the hole
    float r = length(d);
    float a = atan(d.y, d.x);

    float pull = ease_in(t);                              // gravity ramps up
    float shrink = 1.0 - pull * 0.995;                    // image radius left
    float rs = r / shrink;                                // undo the contraction to find the source radius
    float swirl = 6.0 * pull / (rs + 0.15);               // inner parts twist harder
    float as = a + swirl;

    vec2 src = center + vec2(cos(as), sin(as)) * rs / vec2(u_aspect, 1.0);
    if (!inside(src)) return vec4(0.0);
    vec3 col = texture2D(u_tex, src).rgb;

    float horizon = 0.02 + 0.06 * pull;                   // the black disc grows
    col *= smoothstep(horizon, horizon + 0.05 + 0.1 * pull, r);
    col += vec3(0.4, 0.6, 1.0) * exp(-abs(r - horizon - 0.03) * 60.0) * pull; // blue-white photon ring
    col *= 1.0 - 0.4 * pull;                              // light fades as it falls in

    float alpha = 1.0 - smoothstep(0.85, 1.0, t);
    return vec4(col, alpha);
}
