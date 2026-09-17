// duration: 600
// tiles — the screen breaks into tiles that spin and shrink away, rippling out from a random point.

const vec2 GRID = vec2(16.0, 9.0);

vec4 effect(vec2 uv, float t) {
    vec2 cell = floor(uv * GRID);
    vec2 center = (cell + 0.5) / GRID;

    // each tile starts later the further it is from the origin, plus a little jitter
    vec2 origin = vec2(hash(11.0), hash(12.0));
    float dist = length((center - origin) * vec2(u_aspect, 1.0)) / length(vec2(u_aspect, 1.0));
    float delay = dist * 0.55 + hash2(cell) * 0.12;
    float p = ease_in(phase(delay, delay + 0.3, t));   // this tile's own progress
    float scale = 1.0 - p;
    if (scale < 0.001) return vec4(0.0);

    // undo the tile's spin + shrink to find the source pixel
    float cellAspect = u_aspect * GRID.y / GRID.x;
    vec2 local = (uv - center) * GRID * vec2(cellAspect, 1.0);   // square tile space
    float a = (hash2(cell + 3.0) - 0.5) * 1.5 * p;
    local = vec2(cos(a) * local.x + sin(a) * local.y, -sin(a) * local.x + cos(a) * local.y) / scale;
    if (abs(local.x) > 0.5 * cellAspect || abs(local.y) > 0.5) return vec4(0.0);

    vec2 src = center + local / vec2(cellAspect, 1.0) / GRID;
    vec3 col = texture2D(u_tex, src).rgb * (1.0 - 0.5 * p);      // darken as it goes
    return vec4(col, 1.0);
}
