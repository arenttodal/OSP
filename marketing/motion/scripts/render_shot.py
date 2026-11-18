"""ANDOR Motion Library: render one shot from its config (marketing/motion/configs/<id>.json).

Run with Blender's Python (bpy), from the repository root:

  <bpy python> marketing/motion/scripts/render_shot.py --config marketing/motion/configs/D01_gallery_reveal.json \
      [--media marketing/motion/media] [--only frames|stills|reframes|encode] [--frames 0:209] [--res W H] [--samples N]

The scene is the approved master, built by andor-web/product-renders/blender/scripts/andor_scene.py
from the shot's campaign setup and base shot (product, materials, lights, floor, colour management:
unchanged). Only the camera moves. Its parameters are the campaign shots' own: yaw, pitch, distance,
lens, target (editor px, height in mm), f-stop and shift. They are interpolated between the
config's keys on an eased timeline. The product never moves, so its shadow is the scene's own,
path-traced in every frame.

Writes (media root, not in Git): <id>/<version>/frames/<id>_<nnnn>.png (resumable: frames already
on disk are kept), <id>_draft.mp4 (H.264, yuv420p), stills (open/middle/close frames, the
high-quality still, each reframe's stills) and the manifest (also copied to marketing/motion/manifests/).
"""
import argparse
import datetime
import hashlib
import json
import math
import os
import runpy
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
WEB = os.path.join(ROOT, "andor-web")
SCENE = os.path.join(WEB, "product-renders", "blender", "scripts", "andor_scene.py")
SETUPS = os.path.join(WEB, "product-renders", "blender", "config", "campaign_setups.json")

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
ap = argparse.ArgumentParser()
ap.add_argument("--config", required=True)
ap.add_argument("--media", default=os.path.join(ROOT, "marketing", "motion", "media"))
ap.add_argument("--only", choices=["frames", "stills", "reframes", "encode"], default=None)
ap.add_argument("--frames", default=None, help="a:b (inclusive) to render part of the range")
ap.add_argument("--res", type=int, nargs=2, default=None, help="override the draft resolution")
ap.add_argument("--samples", type=int, default=None, help="override the draft samples")
args = ap.parse_args(argv)

cfg = json.load(open(args.config))
SHOT_ID, VERSION = cfg["id"], cfg["version"]
FPS = cfg["fps"]
N = int(round(cfg["seconds"] * FPS))
OUT = os.path.join(os.path.abspath(args.media), SHOT_ID, VERSION)
os.makedirs(os.path.join(OUT, "frames"), exist_ok=True)
os.makedirs(os.path.join(OUT, "stills"), exist_ok=True)
draft = dict(cfg["draft"])
if args.res:
    draft["res"] = args.res
if args.samples:
    draft["samples"] = args.samples

EASES = {
    "linear": lambda t: t,
    "smoothstep": lambda t: t * t * (3.0 - 2.0 * t),
    "smootherstep": lambda t: t * t * t * (t * (6.0 * t - 15.0) + 10.0),   # zero velocity and acceleration at both ends
}
ease = EASES[cfg.get("ease", "smootherstep")]


def git(*a):
    try:
        return subprocess.run(["git", "-C", ROOT, *a], capture_output=True, text=True, check=True).stdout.strip()
    except Exception:
        return ""


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()[:16]


def build(shot, res, samples, noise):
    """The approved master for `shot` (andor_scene.py, run in this Blender); returns its globals."""
    cwd = os.getcwd()
    os.chdir(WEB)   # (andor_scene's default paths are relative to andor-web/)
    sys.argv = ["andor_scene.py", "--", "--setup", cfg["setup"], "--shot", shot, "--res", str(res[0]), str(res[1]),
                "--samples", str(samples), "--noise", str(noise)]
    g = runpy.run_path(SCENE, run_name="__main__")
    os.chdir(cwd)
    return g


def camera_at(base, keys, t):
    """The camera parameters at timeline position t (0..1): the base shot's, overridden by the keys,
    each numeric value interpolated between the keys on either side (in eased time)."""
    e = ease(min(1.0, max(0.0, t)))

    def value(k, key):
        c = dict(base)
        c.update({kk: vv for kk, vv in key["camera"].items() if kk != "distScale"})
        if "distScale" in key["camera"]:
            c["dist"] = base["dist"] * key["camera"]["distScale"]
        return c[k]

    lo = max([k for k in keys if k["t"] <= e], key=lambda k: k["t"], default=keys[0])
    hi = min([k for k in keys if k["t"] >= e], key=lambda k: k["t"], default=keys[-1])
    u = 0.0 if hi["t"] == lo["t"] else (e - lo["t"]) / (hi["t"] - lo["t"])
    out = {}
    for k in ("yaw", "pitch", "dist", "lens", "fstop"):
        a, b = value(k, lo), value(k, hi)
        out[k] = None if a is None or b is None else a + (b - a) * u
    for k in ("target", "shift"):
        a, b = value(k, lo), value(k, hi)
        out[k] = [x + (y - x) * u for x, y in zip(a, b)]
    out["fit"] = base.get("fit", "AUTO")
    return out


def apply(g, cam, c):
    """Places `cam` exactly as andor_scene.camera() places a campaign camera (same formula)."""
    Vector, Euler = g["Vector"], g["Euler"]
    u, v, z = c["target"]
    v += g["KY"] if v > 600 else 0.0
    target = Vector(g["prod"](u, v, g["REF_PANEL_TOP"] + z * g["MM"]))
    d = Vector((0, -math.sin(math.radians(c["pitch"])), math.cos(math.radians(c["pitch"]))))
    d.rotate(Euler((0, 0, math.radians(c["yaw"]))))
    cam.location = target + d * c["dist"]
    cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
    cd = cam.data
    cd.lens, cd.sensor_fit, cd.sensor_width = c["lens"], c["fit"], 36
    cd.shift_x, cd.shift_y = c["shift"]
    if c["fstop"]:
        cd.dof.use_dof = True
        cd.dof.focus_distance = (target - cam.location).length
        cd.dof.aperture_fstop = c["fstop"]
        cd.dof.aperture_blades = 9
    else:
        cd.dof.use_dof = False
    return {"location_m": [round(x, 5) for x in cam.location], "target_m": [round(x, 5) for x in target],
            "focus_m": round((target - cam.location).length, 5)}


def render_to(g, path, res=None, samples=None, noise=None):
    scene = g["bpy"].context.scene
    old = (scene.render.resolution_x, scene.render.resolution_y, scene.cycles.samples, scene.cycles.adaptive_threshold)
    if res:
        scene.render.resolution_x, scene.render.resolution_y = res
    if samples:
        scene.cycles.samples = samples
    if noise:
        scene.cycles.adaptive_threshold = noise
    scene.render.filepath = path
    g["bpy"].ops.render.render(write_still=True)
    scene.render.resolution_x, scene.render.resolution_y, scene.cycles.samples, scene.cycles.adaptive_threshold = old


def base_camera(shot):
    return dict(json.load(open(SETUPS))["setups"][cfg["setup"]]["shots"][shot]["camera"])


manifest_path = os.path.join(OUT, "manifest.json")
manifest = json.load(open(manifest_path)) if os.path.exists(manifest_path) else {}
base = base_camera(cfg["baseShot"])
first, last = 0, N - 1
if args.frames:
    a, b = args.frames.split(":")
    first, last = int(a), int(b)
do = lambda part: args.only is None or args.only == part

curve = []
if do("frames") or do("stills"):
    g = build(cfg["baseShot"], draft["res"], draft["samples"], draft["noise"])
    scene = g["bpy"].context.scene
    cam = scene.camera
    scene.render.fps = FPS
    scene.render.use_persistent_data = True   # (only the camera changes between frames: no re-sync)
    for i in range(N):
        c = camera_at(base, cfg["keys"], i / (N - 1))
        placed = apply(g, cam, c)
        curve.append({"frame": i, **{k: (round(v, 5) if isinstance(v, float) else v) for k, v in c.items()}, **placed})
        path = os.path.join(OUT, "frames", f"{SHOT_ID}_{i:04d}.png")
        if do("frames") and first <= i <= last and not os.path.exists(path):
            render_to(g, path)
            print(f"{SHOT_ID} frame {i + 1}/{N}", flush=True)
    if do("stills"):
        hq = cfg["stills"]["hq"]
        c = camera_at(base, cfg["keys"], hq["t"])
        apply(g, cam, c)
        path = os.path.join(OUT, "stills", f"{SHOT_ID}_hq_{hq['res'][0]}x{hq['res'][1]}.png")
        if not os.path.exists(path):
            render_to(g, path, hq["res"], hq["samples"], hq["noise"])
    json.dump(curve, open(os.path.join(OUT, "camera_curve.json"), "w"), indent=1)

reframes = []
if do("reframes"):
    for rf in cfg.get("reframes", []):
        rbase = base_camera(rf["baseShot"])
        g = build(rf["baseShot"], rf["res"], rf["samples"], rf["noise"])
        cam = g["bpy"].context.scene.camera
        files = []
        for t in rf["stillsOnly"]:
            apply(g, cam, camera_at(rbase, rf["keys"], t))
            path = os.path.join(OUT, "stills", f"{rf['id']}_{int(round(t * 100)):03d}.png")
            if not os.path.exists(path):
                render_to(g, path)
            files.append(os.path.relpath(path, ROOT))
        reframes.append({"id": rf["id"], "purpose": rf["purpose"], "baseShot": rf["baseShot"], "res": rf["res"], "stills": files})

if do("encode") or args.only is None:
    frames = sorted(f for f in os.listdir(os.path.join(OUT, "frames")) if f.endswith(".png"))
    if len(frames) == N:
        mp4 = os.path.join(OUT, f"{SHOT_ID}_draft.mp4")
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(FPS), "-i", os.path.join(OUT, "frames", f"{SHOT_ID}_%04d.png"),
                        "-c:v", "libx264", "-preset", "slow", "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart", mp4], check=True)
        for tag, t in cfg["stills"]["frames"].items():
            src = os.path.join(OUT, "frames", f"{SHOT_ID}_{int(round(t * (N - 1))):04d}.png")
            subprocess.run(["cp", src, os.path.join(OUT, "stills", f"{SHOT_ID}_{tag}.png")], check=True)
    else:
        print(f"encode: {len(frames)} of {N} frames on disk; not encoding yet")

# The manifest (Gate D4's fields), kept with the media and copied beside the configs.
setup = json.load(open(SETUPS))["setups"][cfg["setup"]]
manifest.update({
    "schemaVersion": 1,
    "id": SHOT_ID, "title": cfg["title"], "version": VERSION, "purpose": cfg["purpose"],
    "approval": manifest.get("approval", "awaiting creative review (Gate D1)"),
    "scene": {"builder": os.path.relpath(SCENE, ROOT), "builderSha": sha(SCENE), "setup": cfg["setup"], "baseShot": cfg["baseShot"],
              "setupsSha": sha(SETUPS), "textures": "andor-web/product-renders/blender/textures_current (prep_textures.py from the current editor capture)"},
    "productBuild": {"commit": git("rev-parse", "--short", "HEAD"), "branch": git("rev-parse", "--abbrev-ref", "HEAD")},
    "camera": {"keys": cfg["keys"], "ease": cfg.get("ease", "smootherstep"), "base": base, "curve": os.path.relpath(os.path.join(OUT, "camera_curve.json"), ROOT)},
    "lens_mm": sorted({round(c["lens"], 2) for c in curve}) if curve else manifest.get("camera", {}).get("lens_mm"),
    "focus": "focus distance follows the shot's target every frame (dof on, f-stop per the keys)",
    "frameRange": [0, N - 1], "fps": FPS, "seconds": cfg["seconds"],
    "lightRig": {"setup": cfg["setup"], "lights": setup.get("lights"), "world": setup.get("world"), "exposure": setup.get("exposure")},
    "output": {"draft": draft, "hq": cfg["stills"]["hq"], "colour": "AgX, look AgX - Punchy (andor_scene defaults), sRGB PNG / H.264 yuv420p",
               "alpha": False},
    "render": {"engine": "Cycles (CPU)", "denoise": "andor_scene default", "samples": draft["samples"], "adaptiveThreshold": draft["noise"]},
    "files": {"frames": os.path.relpath(os.path.join(OUT, "frames"), ROOT), "mp4": os.path.relpath(os.path.join(OUT, f"{SHOT_ID}_draft.mp4"), ROOT),
              "stills": os.path.relpath(os.path.join(OUT, "stills"), ROOT)},
    "notes": cfg.get("notes", ""),
    "updated": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
})
if reframes:
    manifest["reframes"] = reframes
json.dump(manifest, open(manifest_path, "w"), indent=1)
os.makedirs(os.path.join(ROOT, "marketing", "motion", "manifests"), exist_ok=True)
json.dump(manifest, open(os.path.join(ROOT, "marketing", "motion", "manifests", f"{SHOT_ID}.json"), "w"), indent=1)
print("done", SHOT_ID, VERSION, OUT)
