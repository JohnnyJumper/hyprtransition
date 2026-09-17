// duration: 650
// burn — the screen burns away like paper, embers glowing along the front.

vec4 effect(vec2 uv, float t) {
    // a noise field decides the order pixels burn in; the front sweeps through its values
    vec2 p = uv * vec2(u_aspect, 1.0) * 4.0 + vec2(hash(1.0), hash(2.0)) * 10.0;
    float n = fbm(p);
    float front = ease_in_out(t) * 1.25 - 0.1;
    float d = n - front;                                // > 0: not burned yet
    if (d < 0.0) return vec4(0.0);

    vec3 col = texture2D(u_tex, uv).rgb;
    col = mix(col, vec3(0.05, 0.02, 0.0), smoothstep(0.08, 0.0, d) * 0.9);  // charred rim
    col += vec3(1.0, 0.45, 0.1) * exp(-d * 40.0) * 1.2                      // embers
         + vec3(1.0, 0.9, 0.6) * exp(-d * 150.0);                           // white-hot edge
    return vec4(col, 1.0);
}
