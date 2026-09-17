# hyprtransition

[![CI](https://github.com/JohnnyJumper/hyprtransition/actions/workflows/ci.yml/badge.svg)](https://github.com/JohnnyJumper/hyprtransition/actions/workflows/ci.yml)

Shader-driven workspace transitions for [Hyprland](https://hyprland.org).
When you switch workspaces, the old screen tears in half and falls away,
burns up like paper, gets sucked into a black hole, turns like a book page,
squashes like a cartoon — or does whatever you write in a ~30-line GLSL file.

Purely cosmetic and fully decoupled: it knows nothing about your keys, your
workspace layout or your plugins. It reacts to the workspace *changing* —
however that happened: keys, scroll, waybar, `hyprctl`, split-monitor-workspaces,
anything — and turning it off is deleting one line.

## How it works

`hyprtransition` is a small standalone program. On a workspace switch it:

1. screenshots the focused monitor (via `grim`)
2. opens a click-through, fullscreen overlay showing that screenshot
3. plays an **effect** — a GLSL fragment shader — over it, on a transparent
   background. Wherever the effect outputs alpha 0, the new workspace shows through
4. exits

The Lua module hooks Hyprland's `workspace.active` event, which fires
synchronously inside the switch before anything is drawn. Right there it steps
back to the old workspace (invisible — no frame has been rendered), starts the
overlay, and the moment Hyprland reports the overlay as opened it switches
forward underneath it. The switch is hidden under a pixel-exact copy of the
old screen until the effect reveals it. No flash, no key configuration, no
compositor patching, nothing that breaks on Hyprland updates.

## Requirements

- Hyprland ≥ 0.56 with the Lua config (`hyprland.lua`) for the key integration.
  The binary itself works with the classic `hyprland.conf` too, see below.
- `grim`, `lua` (5.4 or 5.5; used to read `config.lua`)
- build: a C compiler, `pkg-config`, `wayland-protocols`, and dev headers for
  `wayland-client`, `wayland-egl`, `egl`, `glesv2`, `lua`

## Install

```sh
git clone https://github.com/JohnnyJumper/hyprtransition
cd hyprtransition
sudo make install        # system-wide, PREFIX=/usr/local by default
# Arch, from the AUR:
yay -S hyprtransition    # or hyprtransition-git for the latest main
# or, without root:
make install-user        # ~/.local/bin + ~/.local/share/hyprtransition/ — nothing touches ~/.config
```

Then add **one line** anywhere in your `hyprland.lua`:

```lua
-- system install: the module is on Hyprland's Lua path
require("hyprtransition").setup()

-- user install: Hyprland only searches ~/.config/hypr and system dirs for modules,
-- and we don't put files in your Hyprland config, so load it by path instead
dofile(os.getenv("HOME") .. "/.local/share/hyprtransition/hyprtransition.lua").setup()
```

Reload, switch workspaces however you normally do. Delete the line to turn it off.

## Configuration

Everything of yours lives in `~/.config/hyprtransition/` (or `$XDG_CONFIG_HOME`).
hyprtransition only reads it, never creates it:

```
~/.config/hyprtransition/
├── config.lua    your settings  (optional; every field has a default)
└── effects/      your own effects; a file with a bundled effect's name overrides it
```

Start from the example: `mkdir -p ~/.config/hyprtransition && cp ~/.local/share/hyprtransition/config.example.lua ~/.config/hyprtransition/config.lua`
(`/usr/share/hyprtransition/config.example.lua` for a system install).

```lua
return {
    effect = "tear",                 -- or a list: { "tear", "burn", "blackhole" } → one at random per switch
    duration = nil,                  -- ms; nil = each effect file's own "// duration:" line
    cursor = false,                  -- include the mouse cursor in the captured screen

    -- Lua module only
    fallback_ms = 400,               -- if the overlay never appears, switch anyway after this
    disable_workspace_animation = true,  -- the effect replaces Hyprland's slide
    bin = "hyprtransition",          -- if it isn't on your PATH
}
```

It's plain Lua, so it can compute: `effect = os.getenv("HOSTNAME") == "laptop" and "fade" or "tear"`.
The binary evaluates it too (`effect`, `duration`, `cursor`), so `hyprland.conf`
users and terminal runs get the same defaults. Precedence: built-in defaults <
`config.lua` < `setup({ ... })` arguments / command-line flags. A broken file is
reported (as a Hyprland notification from the module, on stderr from the
binary) and ignored, so it can never break your Hyprland config.

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
| `blackhole` | the screen twists and is sucked into a singularity, revealing the new workspace from the edges in |
| `page` | the old workspace turns like a book page, its back folding over, casting a shadow on the new one |
| `squash` | cartoon squash & stretch: a little hop, then the screen pancakes to the floor and pops |
| `glitch` | displaced scanlines with RGB split, then blocks drop out until nothing is left |
| `melt` | the screen sags and drips down like hot wax, uncovering the new workspace from the top |

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
and it's usable as `effect = <name>`. A file there with the same name as a
bundled effect overrides it. Start from [`effects/_template.glsl`](effects/_template.glsl),
which lists every uniform and helper available (noise, easing, `rotate_about`,
`screen(uv)`, …) and the one trick for moving pieces around: undo the motion and
move the *lookup*, not the pixels.

Iterate on it live:

```sh
hyprtransition -e <name> --loop    # replays forever, re-reads the file every cycle
```

Save, and the next cycle uses it. Compile errors are printed with your file's
line numbers, and the last good version keeps playing. `make check` (or
`scripts/check-effects.sh path/to/yours.glsl`) validates effects with
glslang without a compositor, which is what CI does for the bundled ones.

Effects are searched, in order, in `$HYPRTRANSITION_EFFECTS`,
`~/.config/hyprtransition/effects` (yours), `~/.local/share/hyprtransition/effects`
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
- Every workspace change on a monitor plays the effect, including ones you
  didn't type (e.g. focusing an urgent window on another workspace). Special
  workspaces and moving the mouse between monitors do not.
- Workspace-swipe gestures: the swipe already animated the change visually, so
  the effect will replay it. Set `HyprTransition.enabled = false` around
  gesture use if that bothers you.

## Releasing

1. Bump `VERSION` in the `Makefile`, commit, tag `vX.Y.Z`, push the tag. CI
   checks the binary reports the same version and creates the GitHub release.
2. In `packaging/aur/hyprtransition/PKGBUILD` set `pkgver`, reset `pkgrel=1`,
   and update `sha256sums` with the tarball's hash:
   `curl -sL https://github.com/JohnnyJumper/hyprtransition/archive/refs/tags/vX.Y.Z.tar.gz | sha256sum`
3. `packaging/aur/publish.sh hyprtransition` — regenerates `.SRCINFO` and pushes
   to the AUR. The `-git` package only needs publishing when its PKGBUILD changes.

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
