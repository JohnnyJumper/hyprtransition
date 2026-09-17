# hyprtransition

Shader-driven workspace transitions for [Hyprland](https://hyprland.org).
When you switch workspaces, the old screen tears in half and falls away,
burns up like paper, shatters into spinning tiles — or does whatever you
write in a ~30-line GLSL file.

Purely cosmetic and fully decoupled: it never touches workspaces itself, it
works with or without plugins like split-monitor-workspaces, and turning it
off is deleting one line.

## How it works

`hyprtransition` is a small standalone program. On a workspace switch it:

1. screenshots the focused monitor (via `grim`)
2. opens a click-through, fullscreen overlay showing that screenshot
3. plays an **effect** — a GLSL fragment shader — over it, on a transparent
   background. Wherever the effect outputs alpha 0, the new workspace shows through
4. exits

The Lua module dispatches the real workspace switch at the exact moment
Hyprland reports the overlay as opened, so the switch is hidden under a
pixel-exact copy of the old screen until the effect reveals it. No flash, no
compositor patching, nothing that breaks on Hyprland updates.

## Requirements

- Hyprland ≥ 0.56 with the Lua config (`hyprland.lua`) for the key integration.
  The binary itself works with the classic `hyprland.conf` too, see below.
- `grim`
- build: a C compiler, `pkg-config`, `wayland-protocols`, and dev headers for
  `wayland-client`, `wayland-egl`, `egl`, `glesv2`

## Install

```sh
git clone https://github.com/jeyhunt/hyprtransition
cd hyprtransition
sudo make install        # system-wide, PREFIX=/usr/local by default
# Arch: makepkg -si      # uses the included PKGBUILD
# or, without root:
make install-user        # ~/.local/bin + ~/.local/share/hyprtransition/ — nothing touches ~/.config
```

Then add **one line** at the end of your `hyprland.lua` (after your keybinds —
it takes over the workspace keys):

```lua
-- system install: the module is on Hyprland's Lua path
require("hyprtransition").setup({ mod = "ALT", effect = "tear" })

-- user install: Hyprland only searches ~/.config/hypr and system dirs for modules,
-- and we don't put files in your config, so load it by path instead
dofile(os.getenv("HOME") .. "/.local/share/hyprtransition/hyprtransition.lua").setup({ mod = "ALT", effect = "tear" })
```

Reload, press `mod + 2`. Delete the line to turn it off. hyprtransition never
creates or modifies anything in your config directory; the only thing it reads
there is `~/.config/hyprtransition/effects/`, if you make it.

All options and their defaults:

```lua
require("hyprtransition").setup({
    mod = "ALT",
    effect = "tear",                 -- name in an effects dir, or a list to pick from at random
    duration = nil,                  -- ms; nil = the effect file's own "// duration:" line
    workspaces = 5,                  -- binds mod+1..workspaces (and mod+0 / mod+9 = next / prev)
    bin = "hyprtransition",          -- the binary; anything your shell can find
    bind_keys = true,                -- false: keep your binds, call HyprTransition.go(i) yourself
    fallback_ms = 400,               -- if the overlay never appears, switch anyway after this
    disable_workspace_animation = true,  -- the effect replaces Hyprland's slide
})
```

Change things at runtime, from a bind or a terminal:

```sh
hyprctl dispatch '(function() HyprTransition.enabled = not HyprTransition.enabled return hl.dsp.no_op() end)()'
hyprctl dispatch '(function() HyprTransition.effect = "burn" return hl.dsp.no_op() end)()'
```

### Classic `hyprland.conf`

The binary needs no Lua. `--then CMD` runs a command once the overlay is up:

```ini
layerrule = noanim, hyprtransition
animation = workspaces, 0
bind = ALT, 1, exec, hyprtransition -e tear --then "hyprctl dispatch workspace 1"
bind = ALT, 2, exec, hyprtransition -e tear --then "hyprctl dispatch workspace 2"
```

## Effects

| name    | what happens |
|---------|--------------|
| `tear`  | a glowing crack races down the middle, the halves tip outward and fall away |
| `burn`  | the screen burns away like paper, embers glowing along the front |
| `tiles` | the screen breaks into tiles that spin and shrink away, rippling out from a random point |

Try any of them without configuring anything:

```sh
hyprtransition -e burn                  # on the focused monitor, nothing switches
hyprtransition -e tiles -d 3000 -s 7    # slow motion, fixed seed
```

If you haven't set the layer rule yet, tell Hyprland once per session not to
animate the overlay in: `hyprctl dispatch '(function() hl.layer_rule({ match = { namespace = "^hyprtransition$" }, no_anim = true }) return hl.dsp.no_op() end)()'`

## Writing your own effect

An effect is a file `<name>.glsl` with one function:

```glsl
// duration: 500
vec4 effect(vec2 uv, float t) {
    // uv: this pixel (0,0 top-left → 1,1 bottom-right).  t: 0 → 1 over the animation.
    // Return colour + alpha. Alpha 0 = show the new workspace underneath.
    return vec4(texture2D(u_tex, uv).rgb, 1.0 - t);
}
```

That is a complete effect (a fade). Drop it in `~/.config/hyprtransition/effects/`
(create it — we never do) and it's usable as `effect = "<name>"`. A file there
with the same name as a bundled effect overrides it. Start from [`effects/_template.glsl`](effects/_template.glsl),
which lists every uniform and helper available (noise, easing, `rotate_about`,
`screen(uv)`, …) and the one trick for moving pieces around: undo the motion and
move the *lookup*, not the pixels.

Iterate on it live:

```sh
hyprtransition -e <name> --loop    # replays forever, re-reads the file every cycle
```

Save, and the next cycle uses it. Compile errors are printed with your file's
line numbers, and the last good version keeps playing.

Effects are searched, in order, in `$HYPRTRANSITION_EFFECTS`,
`$XDG_CONFIG_HOME/hyprtransition/effects` (yours), `$XDG_DATA_HOME/hyprtransition/effects`
(user install), next to the binary (a checkout), then `/usr/share/hyprtransition/effects`
(system install). `-e` also takes a path.

## CLI

```
hyprtransition [-e EFFECT] [-o OUTPUT] [-d MS] [-s SEED] [-c] [--loop] [--then CMD]

  -e EFFECT   effect name or path        (default: tear)
  -o OUTPUT   monitor, e.g. DP-3         (default: the focused one)
  -d MS       duration                   (default: the effect's // duration: line)
  -s SEED     randomness seed            (default: random)
  -c          include the cursor in the screenshot
  --loop      replay forever, re-reading the effect file each cycle
  --then CMD  run CMD (sh -c) once the overlay is on screen
```

`HYPRTRANSITION_DEBUG=1` prints startup timings to stderr.

## Notes

- Startup cost is creating one EGL context per switch: ~60 ms on the NVIDIA
  driver in testing (other drivers are usually quicker). The effect starts
  after that; the switch itself is never delayed by more than the overlay setup.
- Fractional scaling and rotated monitors are handled (the overlay renders at
  the physical resolution and is pixel-exact).
- With split-monitor-workspaces, `workspaces` must match the plugin's `count`.

## Why isn't this a hyprpm plugin?

It could be, and a native version would be better in two real ways: no
per-switch startup cost, and effects could receive the *new* workspace as a
texture too (cube rotations, page curls, crossfades).

It isn't, for now, because a hyprpm plugin is a C++ `.so` compiled against your
exact Hyprland commit and loaded into the compositor. A transition plugin has
to render workspaces offscreen and inject raw GL into the render pass — the
least stable part of the plugin API — and as of 0.56 Hyprland is mid-refactor
toward an abstract renderer with a Vulkan backend (`IHyprRenderer::RT_VK`).
Anything written against the GL render pass today would be written twice, and
a plugin crash takes the whole session with it.

This standalone version uses only stable Wayland protocols and survives every
Hyprland update untouched. The effect file format is designed so a native
backend can adopt it later (same `effect(uv, t)` contract, plus a second
texture) once the renderer settles.

## License

MIT
