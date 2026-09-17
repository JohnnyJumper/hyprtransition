// duration: 700
// squash — cartoon squash & stretch: a little hop up, then the screen pancakes to the floor and pops.

vec4 effect(vec2 uv, float t) {
    // anticipation: stretch up, then squash down onto the bottom edge, then pop away
    float up = ease_out(phase(0.0, 0.18, t));
    float down = ease_in_out(phase(0.18, 0.62, t));
    float pop = ease_in(phase(0.72, 1.0, t));

    float sy = mix(1.0, 1.10, up);
    sy = mix(sy, 0.06, down);
    float sx = 1.0 / sqrt(max(sy, 0.06));             // keep "volume": wider as it flattens
    sx = min(sx, 2.6);
    sx *= 1.0 - pop;                                   // pop: the pancake shrinks to nothing sideways
    float jiggle = 0.02 * sin(t * 40.0) * (1.0 - t);   // a bit of jelly
    sx *= 1.0 + jiggle;
    sy *= 1.0 - jiggle;
    if (sx <= 0.001) return vec4(0.0);

    // anchored to the bottom edge, centred horizontally
    vec2 src = vec2(0.5 + (uv.x - 0.5) / sx, 1.0 - (1.0 - uv.y) / sy);
    if (!inside(src)) return vec4(0.0);

    vec3 col = texture2D(u_tex, src).rgb;
    col *= 1.0 - 0.3 * down;                           // a little darker when squished
    return vec4(col, 1.0);
}
