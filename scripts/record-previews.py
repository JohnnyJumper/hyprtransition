"""Records each effect on a real monitor and turns it into docs/previews/<effect>.gif.

    scripts/record-previews.py tear burn ...      (no args = all bundled effects)

Assumes this machine: monitor DP-3, split-monitor-workspaces with workspace 1 free
for a staged scene (two kitty windows: bat + fastfetch) and workspace 2 empty,
wf-recorder, ffmpeg, gifski and Pillow. Pauses the Lua module while recording so
the --then switch is not transitioned twice.
"""
import subprocess, time, sys, os, glob
from PIL import Image, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = '/tmp/hyprtransition-previews'
OUT = f'{ROOT}/docs/previews'
os.makedirs(S, exist_ok=True); os.makedirs(OUT, exist_ok=True)
FPS, WIDTH = 30, 640
HOLD_BEFORE, HOLD_AFTER = 12, 20   # frames of stillness kept around the effect

def hypr(lua): subprocess.run(['hyprctl', 'dispatch', lua], capture_output=True)
def module_enabled(on): hypr(f'(function() if HyprTransition then HyprTransition.enabled = {str(on).lower()} end return hl.dsp.no_op() end)()')
def workspace(i): hypr(f'(function() hl.plugin.split_monitor_workspaces.workspace({i}) return hl.dsp.no_op() end)()')

def record(effect):
    mp4 = f'{S}/{effect}.mp4'
    if os.path.exists(mp4): os.remove(mp4)
    rec = subprocess.Popen(['wf-recorder', '-o', 'DP-3', '-r', '60', '-f', mp4], stderr=subprocess.DEVNULL)
    time.sleep(1.5)                                      # recorder warm-up
    subprocess.run(['hyprtransition', '-e', effect, '-o', 'DP-3', '-s', '7', '--then',
                    "hyprctl dispatch '(function() hl.plugin.split_monitor_workspaces.workspace(2) return hl.dsp.no_op() end)()'"])
    time.sleep(1.0)
    rec.send_signal(2); rec.wait()
    workspace(1)
    time.sleep(0.4)
    return mp4

def frames_of(mp4, effect):
    d = f'{S}/frames_{effect}'
    subprocess.run(['rm', '-rf', d]); os.makedirs(d)
    subprocess.run(['ffmpeg', '-v', 'error', '-i', mp4, '-vf', f'fps={FPS},scale={WIDTH}:-1:flags=lanczos', f'{d}/f%04d.png'])
    return sorted(glob.glob(f'{d}/f*.png'))

def below_bar(path):
    img = Image.open(path).convert('L')
    return img.crop((0, 14, img.width, img.height))   # waybar's clock ticks; ignore it

def differs(a, b):
    changed = ImageChops.difference(below_bar(a), below_bar(b)).point(lambda v: 255 if v > 60 else 0)
    return changed.histogram()[255] > 300   # a blinking cursor is ~15 px; an effect is thousands

def trim(frames):
    first = next(i for i, f in enumerate(frames) if differs(frames[0], f))
    last = len(frames) - 1 - next(i for i, f in enumerate(reversed(frames)) if differs(frames[-1], f))
    return frames[max(0, first - HOLD_BEFORE): min(len(frames), last + HOLD_AFTER)]

def gif(effect):
    frames = trim(frames_of(record(effect), effect))
    out = f'{OUT}/{effect}.gif'
    subprocess.run(['gifski', '-q', '--fps', str(FPS), '--width', str(WIDTH), '--quality', '80', '-o', out] + frames)
    print(f'{effect:10s} {len(frames)} frames  {os.path.getsize(out) // 1024} KB')

def stage_scene():
    hypr('hl.dsp.focus({ monitor = "DP-3" })')
    workspace(1)
    effects = f'{ROOT}/effects'
    hypr(f'hl.dsp.exec_cmd("kitty --class hypr-demo sh -c \'bat --style=numbers --paging=never {effects}/tear.glsl; printf \\033[?25l; sleep 3600\'")')
    time.sleep(0.8)
    hypr('hl.dsp.exec_cmd("kitty --class hypr-demo sh -c \'fastfetch; printf \\033[?25l; sleep 3600\'")')
    time.sleep(1.5)

def clear_scene():
    subprocess.run(['pkill', '-f', '[k]itty --class hypr-demo'])
    time.sleep(0.3)
    workspace(1)
    hypr('hl.dsp.focus({ monitor = "DP-2" })')

module_enabled(False)
stage_scene()
try:
    names = sys.argv[1:] or sorted(os.path.basename(f)[:-5] for f in glob.glob(f'{ROOT}/effects/*.glsl'))
    for effect in names:
        gif(effect)
finally:
    clear_scene()
    module_enabled(True)
