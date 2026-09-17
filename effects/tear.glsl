// duration: 700
// tear — a glowing crack races down the middle, then the two halves tip outward and fall away.

// x position of the crack for a given row; jagged on purpose (linear noise = straight segments)
float tearX(float y) {
    return 0.5 + (vnoise(y * 5.0) - 0.5) * 0.12
               + (vnoise(y * 21.0 + 7.0) - 0.5) * 0.035
               + (vnoise(y * 90.0 + 3.0) - 0.5) * 0.008;
}

// One half of the screen. side = -1 (left) or +1 (right).
vec4 piece(vec2 uv, float side, float crack, float fall, float glow, float shake) {
    vec2 pivot = vec2(side > 0.0 ? 1.0 : 0.0, 1.0);                // bottom outer corner
    vec2 offset = vec2(side * 0.125 * fall + shake, 0.95 * fall);  // drift outward + drop
    vec2 src = rotate_about(uv - offset, pivot, -side * 0.35 * fall);

    float d = (src.x - tearX(src.y)) * side;                        // > 0 on this piece's side
    if (!inside(src) || d < 0.0) return vec4(0.0);

    vec3 col = texture2D(u_tex, src).rgb;
    float dpx = d * u_aspect;                                       // distance from the crack
    float reached = 1.0 - smoothstep(crack - 0.02, crack, src.y);   // has the crack got here yet
    col *= 1.0 - 0.7 * exp(-dpx * 350.0) * reached;                 // scorched edge
    col += (vec3(1.0, 0.95, 0.85) * exp(-dpx * 600.0) * 1.6         // hot core
          + vec3(1.0, 0.55, 0.2) * exp(-dpx * 90.0) * 0.6)          // orange halo
          * reached * glow;
    return vec4(col, 1.0);
}

vec4 effect(vec2 uv, float t) {
    float crack = smoothstep(0.0, 0.22, t) * 1.04;   // phase 1: crack draws top -> bottom
    float s = phase(0.22, 1.0, t);                   // phase 2: fall
    float fall = ease_in(s);                         // gravity
    float alpha = 1.0 - smoothstep(0.55, 1.0, s);
    float glow = 1.0 - smoothstep(0.0, 0.6, s);
    float shake = t < 0.22 ? 0.00125 * sin(t * 420.0) * (1.0 - crack) : 0.0;

    vec4 c = piece(uv, -1.0, crack, fall, glow, shake);
    if (c.a == 0.0) c = piece(uv, 1.0, crack, fall, glow, shake);
    c.a *= alpha;
    return c;
}
