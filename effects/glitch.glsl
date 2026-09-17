// duration: 550
// glitch — the picture tears into displaced scanlines with RGB split, then blocks drop out until nothing is left.

vec4 effect(vec2 uv, float t) {
    float frame = floor(t * 24.0);                      // discrete jumps look more digital than smooth motion
    float amount = ease_in(t) * 0.4 + 0.02;

    // horizontal bands, each shoved sideways by its own random amount
    float band = floor(uv.y * 28.0);
    float shove = (hash2(vec2(band, frame)) - 0.5) * amount * step(0.35, hash2(vec2(band, frame + 7.0)));
    vec2 src = vec2(uv.x + shove, uv.y);

    // RGB split grows with time
    float split = amount * 0.15;
    float r = texture2D(u_tex, src + vec2(split, 0.0)).r;
    float g = texture2D(u_tex, src).g;
    float b = texture2D(u_tex, src - vec2(split, 0.0)).b;
    vec3 col = vec3(r, g, b);

    // blocks drop out progressively; each has its own threshold
    vec2 block = floor(uv * vec2(24.0, 14.0));
    float gone = hash2(block + 11.0) * 0.9 + 0.05;
    if (t > gone) return vec4(0.0);

    // some blocks flash bright or go dark before dying
    float f = hash2(block + frame);
    if (f > 0.93) col = vec3(1.0);
    else if (f < 0.04) col *= 0.2;

    // scanlines
    col *= 0.92 + 0.08 * step(0.5, fract(uv.y * u_resolution.y * 0.5));
    return vec4(col, 1.0);
}
