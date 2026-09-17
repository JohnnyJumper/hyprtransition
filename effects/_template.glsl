// duration: 500
// _template — the simplest possible effect (a plain fade), annotated.
// Copy this file to <name>.glsl in an effects dir, then run:  hyprtransition -e <name> --loop
// (--loop replays forever and re-reads the file every cycle, so just keep editing.)
//
// ── The contract ────────────────────────────────────────────────────────────
//
//   vec4 effect(vec2 uv, float t)
//
//   You get:   uv  — this pixel, 0..1, (0,0) = top-left, (1,1) = bottom-right
//              t   — progress, 0 at the start, 1 at the end
//   You return: the colour of this pixel and its alpha (straight, not premultiplied).
//              alpha 1 = show this, alpha 0 = show the new workspace underneath.
//
//   At t = 0 you should return the untouched screenshot (so nothing jumps), and
//   by t = 1 everything should be alpha 0 (so the new workspace is fully visible).
//
// ── What's available ─────────────────────────────────────────────────────────
//
//   sampler2D u_tex           the screenshot; texture2D(u_tex, uv).rgb
//   float     u_seed          random per run, so effects can vary each time
//   float     u_aspect        width / height of the screen
//   vec2      u_resolution    size in pixels
//
//   vec4  screen(uv)                   screenshot at uv, or transparent when uv is off-screen
//   bool  inside(uv)                   is uv within 0..1 on both axes
//   vec2  rotate_about(uv, pivot, a)   rotate uv around pivot by a radians (aspect-correct)
//   float phase(from, to, t)           0..1 as t goes from `from` to `to`, clamped
//   float ease_in(x) / ease_out(x) / ease_in_out(x)
//   float hash(n) / hash2(p)           deterministic randomness (seeded per run)
//   float vnoise(x) / vnoise2(p) / fbm(p)   value noise, 1D / 2D / layered
//
//   The first line "// duration: MS" sets the default length; -d overrides it.
//
// ── Tips ─────────────────────────────────────────────────────────────────────
//
//   Moving pieces around: don't move pixels, move the *lookup*. For each output
//   pixel, undo the piece's motion to find where it came from in the screenshot
//   (see tear.glsl: rotate_about(uv - offset, pivot, -angle)), then sample there.
//   Return vec4(0.0) for pixels no piece covers.

vec4 effect(vec2 uv, float t) {
    vec3 col = texture2D(u_tex, uv).rgb;
    return vec4(col, 1.0 - ease_in_out(t));
}
