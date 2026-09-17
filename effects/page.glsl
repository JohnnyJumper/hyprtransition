// duration: 750
// page — the old workspace turns like a book page, its back folding over, casting a shadow on the new one.

const float R = 0.12;      // curl radius (fraction of screen width)
const float PI = 3.14159265;

vec4 effect(vec2 uv, float t) {
    bool flip = hash(3.0) > 0.5;                  // turn from the right or from the left
    float x = flip ? 1.0 - uv.x : uv.x;
    float A = 1.0 - ease_in_out(t) * (1.0 + R);   // curl axis sweeps across the page
    vec2 src;

    if (x < A) {
        // flat page — unless the folded-over back has already landed on top of it
        float srcx = 2.0 * A + PI * R - x;
        if (srcx <= 1.0) {
            src = vec2(flip ? 1.0 - srcx : srcx, uv.y);
            vec3 col = mix(vec3(0.93, 0.91, 0.88), texture2D(u_tex, src).rgb, 0.18); // paper back, faint show-through
            return vec4(col, 1.0);
        }
        float shade = 1.0 - 0.45 * (1.0 - smoothstep(0.0, 0.1, A - x));      // shadow under the curl
        return vec4(texture2D(u_tex, vec2(uv.x, uv.y)).rgb * shade, 1.0);
    }

    if (x < A + R) {
        // the roll: the back side is on top; below it (past the page's end) the front face shows
        float theta = asin(clamp((x - A) / R, 0.0, 1.0));
        float sBack = R * (PI - theta), sFront = R * theta;
        float light = 0.55 + 0.45 * cos(theta);                                 // cylinder shading
        if (A + sBack <= 1.0) {
            float srcx = A + sBack;
            src = vec2(flip ? 1.0 - srcx : srcx, uv.y);
            vec3 col = mix(vec3(0.93, 0.91, 0.88), texture2D(u_tex, src).rgb, 0.18) * light;
            return vec4(col, 1.0);
        }
        if (A + sFront <= 1.0) {
            float srcx = A + sFront;
            src = vec2(flip ? 1.0 - srcx : srcx, uv.y);
            return vec4(texture2D(u_tex, src).rgb * light, 1.0);
        }
    }

    // revealed area: only a soft shadow the lifted page casts onto the new workspace
    float shadow = 0.45 * (1.0 - smoothstep(A + R, A + R + 0.08, x)) * (1.0 - smoothstep(0.9, 1.0, t));
    return vec4(0.0, 0.0, 0.0, shadow);
}
