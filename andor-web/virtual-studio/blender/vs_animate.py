"""ANDOR Virtual Studio: a performance project drives the approved Blender master.

  <bpy python> virtual-studio/blender/vs_animate.py -- --project <dir> --fps 24 --camera perf
      [--setup ANDOR_GALLERY --shot A2] [--res 960 540] [--samples 16] [--noise 0.05]
      [--frames A B] [--controls all|macro|layer|mini]

Reads <project>/performance/performance.json (schema 2) and captures/<fps>fps/ (face_crop, knobs;
run vs_prep.py first). Writes <project>/renders/<camera>/<fps>fps/frames/NNNN.png, render_meta.json
and <camera>.mp4 muxed with the audio of record (audio/daw_bounce.wav, else audio/headless.wav).

Nothing is invented:
- **Keys** move only for the musician's input notes ("source": "input"). They never move for notes the
  arpeggiator generates; those appear only in the captured interface.
- **Each key's angle** is evaluated exactly at every frame time from the note list: a
  velocity-dependent press, a spring release, and a retrigger that starts from wherever the key is.
  So no note is ever reconstructed from frames, at 24 or 30 fps.
- **Key travel** follows the model's proportions: the front of every key travels 6.7 % of the median
  white key's length (about 10 mm on a full-size key), so each key's angle follows its own length
  from the shared back pivot.
- **Knobs** turn to the editor's recorded pointer angles for the frame (macro, source and ADSR minis),
  matched to the model by position. The value arc and text are in the same frame's face texture,
  so knob, arc and value cannot disagree.
- **Displays and the PITCH/MOD indication** are the captured editor frames (the strips' light layer
  samples the face texture).
"""
import argparse
import glob
import json
import math
import os
import runpy
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.abspath(os.path.join(HERE, "..", "..", "product-renders", "blender", "scripts"))
sys.path.insert(0, os.path.join(HERE, ".."))
from vstudio import project as vproject  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
ap = argparse.ArgumentParser()
ap.add_argument("--project", required=True)
ap.add_argument("--fps", type=int, default=24)
ap.add_argument("--camera", default="perf")
ap.add_argument("--name", default=None, help="output folder under renders/ (default: the camera's name)")
ap.add_argument("--setup", default="ANDOR_GALLERY")
ap.add_argument("--shot", default="A2")
ap.add_argument("--res", type=int, nargs=2, default=[960, 540])
ap.add_argument("--samples", type=int, default=16)
ap.add_argument("--noise", type=float, default=0.05)
ap.add_argument("--frames", type=int, nargs=2, default=None)
ap.add_argument("--controls", default="all", choices=["all", "macro", "layer", "mini"])
ap.add_argument("--travel", type=float, default=0.0667, help="key front travel / median white key length")
ap.add_argument("--track", type=float, nargs=2, default=[300.0, 1150.0], help="keys_track: editor x from .. to (px)")
ap.add_argument("--light-sweep", type=int, nargs=2, default=None, help="a narrow light crossing the panel over frames A..B")
args = ap.parse_args(argv)

P = args.project
perf = json.load(open(os.path.join(P, "performance", "performance.json")))
assert perf["schemaVersion"] == 2, "performance.json schema 2 expected (vs.py ingest)"
cap = os.path.join(P, "captures", f"{args.fps}fps")
meta = json.load(open(os.path.join(cap, "capture_meta.json")))
assert abs(meta["fps"] - args.fps) < 1e-6, "the capture's frame rate must be the render's"
face_dir = os.path.join(cap, "face_crop")
n_frames = meta["frames"]
assert len(glob.glob(os.path.join(face_dir, "*.png"))) == n_frames, "run vs_prep.py first"
out = os.path.join(P, "renders", args.name or args.camera, f"{args.fps}fps")
os.makedirs(os.path.join(out, "frames"), exist_ok=True)

# ---- the approved master (no render): the shot's light and setup; the camera is set below.
sys.argv = ["andor_scene.py", "--", "--setup", args.setup, "--shot", args.shot, "--res", str(args.res[0]), str(args.res[1]),
            "--samples", str(args.samples), "--noise", str(args.noise)]
g = runpy.run_path(os.path.join(SCRIPTS, "andor_scene.py"), run_name="__main__")
bpy, Vector, Euler = g["bpy"], g["Vector"], g["Euler"]
scene = bpy.context.scene
scene.render.fps, scene.render.fps_base = args.fps, 1.0
scene.cycles.seed, scene.cycles.use_animated_seed = 0, False
prod, PX, KY, GEO = g["prod"], g["PX"], g["KY"], g["GEO"]

# ---- displays: the face texture becomes the captured sequence (same crop and UVs as the master).
face_img = next((im for im in bpy.data.images if im.filepath and os.path.basename(im.filepath) == "osp_face.png"), None)
assert face_img is not None, "the master's face image was not found"
face_img.source = "SEQUENCE"
face_img.filepath = os.path.join(face_dir, "0001.png")
for mat in bpy.data.materials:
    if mat.node_tree:
        for nd in mat.node_tree.nodes:
            if nd.type == "TEX_IMAGE" and nd.image == face_img:
                nd.image_user.frame_duration, nd.image_user.frame_start, nd.image_user.frame_offset = n_frames, 1, 0
                nd.image_user.use_auto_refresh, nd.image_user.use_cyclic = True, False


# ---- keys: split into individual keys, each pivoted at its back.
def separate(name):
    o = bpy.data.objects.get(name)
    if o is None:
        return []
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = o
    o.select_set(True)
    for m in list(o.modifiers):
        bpy.ops.object.modifier_apply(modifier=m.name)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.separate(type="LOOSE")
    bpy.ops.object.mode_set(mode="OBJECT")
    return list(bpy.context.selected_objects)


cx = lambda o: sum(v.co.x for v in o.data.vertices) / max(1, len(o.data.vertices))
whites, blacks = sorted(separate("White keys"), key=cx), sorted(separate("Black keys"), key=cx)
WHITE_PC = {0, 2, 4, 5, 7, 9, 11}
white_notes = [n for n in range(21, 109) if n % 12 in WHITE_PC]
black_notes = [n for n in range(21, 109) if n % 12 not in WHITE_PC]
assert (len(whites), len(blacks)) == (52, 36), f"keys: {len(whites)} white, {len(blacks)} black"
back_y = g["ey"](906.0 + KY)
keys = {}
for objs, notes in ((whites, white_notes), (blacks, black_notes)):
    for o, n in zip(objs, notes):
        ys = [v.co.y for v in o.data.vertices]
        front_is_low = abs(min(ys) - back_y) > abs(max(ys) - back_y)
        back, front = (max(ys), min(ys)) if front_is_low else (min(ys), max(ys))
        top = max(v.co.z for v in o.data.vertices)
        pivot = Vector((cx(o), back, top))
        for v in o.data.vertices:
            v.co -= pivot
        o.location = o.location + pivot
        o.rotation_mode = "XYZ"
        keys[n] = {"obj": o, "sign": -1.0 if front_is_low else 1.0, "length": abs(front - back)}
white_len = sorted(keys[n]["length"] for n in white_notes)[len(white_notes) // 2]
travel = args.travel * white_len
for k in keys.values():
    k["angle"] = math.atan2(travel, k["length"])   # the same front travel for every key

PRESS_SOFT, PRESS_HARD, RELEASE = 0.040, 0.008, 0.070   # seconds: velocity 1 .. 127; spring return


def depth_at(notes, t):
    """0 (rest) .. 1 (bottom) at time t for one key's input notes (sorted by onset)."""
    a, last = 0.0, -1e9   # depth and the time it was last known
    state = ("rest", 0.0, 0.0, 0.0)   # (phase, t0, a0, duration)

    def value(st, tt):
        ph, t0, a0, d = st
        if ph == "rest":
            return 0.0
        if ph == "press":
            u = min(1.0, (tt - t0) / d)
            return a0 + (1.0 - a0) * u * u            # the finger accelerates the key down
        if ph == "held":
            return 1.0
        u = min(1.0, (tt - t0) / d)                    # release: the spring returns it, settling
        return a0 * (1.0 - u) ** 2

    for nt in notes:
        if nt["on"] > t:
            break
        a_on = value(state, nt["on"])
        pd = PRESS_SOFT + (PRESS_HARD - PRESS_SOFT) * (nt["velocity"] - 1) / 126.0
        state = ("press", nt["on"], a_on, pd)
        if nt["off"] <= t:
            a_off = value(state, nt["off"]) if nt["off"] < nt["on"] + pd else 1.0
            state = ("release", nt["off"], a_off, RELEASE)
        elif t >= nt["on"] + pd:
            state = ("held", 0, 0, 0)
    return value(state, t)


by_key = {}
for nt in perf["notes"]:
    if nt.get("source", "input") == "input" and nt["note"] in keys:
        by_key.setdefault(nt["note"], []).append(nt)
for v in by_key.values():
    v.sort(key=lambda x: x["on"])

# ---- controls: every captured rotary matched to the model's knob by position (editor px).
captured = [json.load(open(os.path.join(cap, "knobs", f"{i:04d}.json"))) for i in range(1, n_frames + 1)]
knobs = []
for i, k in enumerate(GEO["knobs"]):
    if args.controls != "all" and k["kind"] != args.controls:
        continue
    o = bpy.data.objects.get(f"Knob {k['kind']} {i:02d}")
    if o is None:
        continue
    rows = captured[0]
    best = min(rows, key=lambda r: math.hypot(r["x"] - k["c"][0], r["y"] - k["c"][1]), default=None)
    if best is None or math.hypot(best["x"] - k["c"][0], best["y"] - k["c"][1]) > 12:
        print("no captured control for", o.name)
        continue
    o.rotation_mode = "XYZ"
    knobs.append({"obj": o, "base": k["angle"], "rest": o.rotation_euler[2], "param": best["param"], "c": k["c"]})
    print(f"{o.name} <- {best['param'] or '(unnamed)'}")


def capture_row(rows, kb):
    return min(rows, key=lambda r: math.hypot(r["x"] - kb["c"][0], r["y"] - kb["c"][1]))


# ---- keyframes at every output frame (frame f shows t = (f - 1) / fps, the capture's instant).
f0, f1 = args.frames if args.frames else (1, n_frames)
for f in range(1, n_frames + 1):
    t = (f - 1) / args.fps
    for n, k in keys.items():
        k["obj"].rotation_euler[0] = k["sign"] * -k["angle"] * depth_at(by_key.get(n, []), t)
        k["obj"].keyframe_insert("rotation_euler", index=0, frame=f)
    for kb in knobs:
        r = capture_row(captured[f - 1], kb)
        kb["obj"].rotation_euler[2] = kb["rest"] - (r["angle"] - kb["base"])
        kb["obj"].keyframe_insert("rotation_euler", index=2, frame=f)
for o in [k["obj"] for k in keys.values()] + [kb["obj"] for kb in knobs]:
    if o.animation_data and o.animation_data.action:
        for fc in o.animation_data.action.fcurves:
            for kp in fc.keyframe_points:
                kp.interpolation = "CONSTANT"   # values are exact per frame; nothing is interpolated

# ---- cameras (scene/cameras.json of the project overrides these; none moves the product).
CAMS = {
    "perf": dict(yaw=-20, pitch=50, dist=0.74, lens=85, target=prod(640, 700, g["REF_PANEL_TOP"]), fstop=11),
    "keys": dict(yaw=-24, pitch=40, dist=0.34, lens=100, target=prod(560, 960 + KY, g["REF_PANEL_TOP"]), fstop=11),
    "macros": dict(yaw=-16, pitch=52, dist=0.30, lens=100, target=prod(420, 812 + KY, g["REF_PANEL_TOP"]), fstop=11),
    "display": dict(yaw=-14, pitch=46, dist=0.30, lens=100, target=prod(260, 330, g["REF_PANEL_TOP"]), fstop=11),
}
cj = os.path.join(P, "scene", "cameras.json")
if os.path.exists(cj):
    for name, c in json.load(open(cj)).items():
        CAMS[name] = dict(c, target=prod(*c["target"], g["REF_PANEL_TOP"]))
EW_M, EH_M = GEO["editor_px"]
FACE_T = prod(EW_M / 2, EH_M / 2, g["REF_PANEL_TOP"])
ROT_RAD = math.radians(g["ROT"])
PANEL_UP = Vector((0, 1, 0))
PANEL_UP.rotate(Euler((0, 0, ROT_RAD)))


def look(o, target, up):
    """Aims a camera at target with a given world up (no roll from Blender's track axis)."""
    from mathutils import Matrix
    z = (o.location - Vector(target)).normalized()
    x = up.cross(z).normalized()
    o.rotation_euler = Matrix((x, z.cross(x), z)).transposed().to_euler()


def ease(u):
    u = min(1.0, max(0.0, u))
    return u * u * (3.0 - 2.0 * u)


pose = None   # per-frame camera function for moving cameras
if args.camera in ("ortho", "display_push"):
    if args.res == [960, 540]:   # the editor's own aspect: registers with the UI captures
        args.res = [960, 2 * round(960 * EH_M / EW_M / 2)]
    scene.render.resolution_x, scene.render.resolution_y = args.res
if args.camera == "ortho":
    cam = g["camera"]("VS ortho", yaw=0, pitch=0, dist=0.8, lens=85, target=FACE_T)
    cam.location = Vector(FACE_T) + Vector((0, 0, 0.8))
    cam.rotation_euler = (0.0, 0.0, ROT_RAD)
    cam.data.type, cam.data.sensor_fit, cam.data.ortho_scale = "ORTHO", "HORIZONTAL", EW_M * PX
    cam.data.dof.use_dof = False
elif args.camera == "display_push":
    # Straight down onto the panel (as the ortho view), a long lens, descending from the whole
    # panel into card A's display until the display fills the frame's width.
    LENS = 135.0
    da = GEO["displays"][0]
    disp_c = prod((da[0] + da[2]) / 2, (da[1] + da[3]) / 2, g["REF_PANEL_TOP"])
    d_full, d_disp = LENS / 36.0 * EW_M * PX, LENS / 36.0 * (da[2] - da[0]) * PX
    cam = g["camera"]("VS display push", yaw=0, pitch=0, dist=d_full, lens=LENS, target=FACE_T, fit="HORIZONTAL")
    cam.data.dof.use_dof = False

    def pose(f):
        u = ease((f - f0) / max(1, f1 - f0))
        tgt = Vector(FACE_T).lerp(Vector(disp_c), u)
        cam.location = tgt + Vector((0, 0, d_full + (d_disp - d_full) * u))
        cam.rotation_euler = (0.0, 0.0, ROT_RAD)
        return {"target_px": [EW_M / 2 + ((da[0] + da[2]) / 2 - EW_M / 2) * u, EH_M / 2 + ((da[1] + da[3]) / 2 - EH_M / 2) * u],
                "visible_width_px": EW_M + ((da[2] - da[0]) - EW_M) * u}
elif args.camera == "keys_track":
    base = dict(yaw=-24, pitch=40, dist=0.34, lens=100, fstop=11)
    v_keys = 960 + KY
    cam = g["camera"]("VS keys track", target=prod(args.track[0], v_keys, g["REF_PANEL_TOP"]), **base)
    rel = cam.location - Vector(prod(args.track[0], v_keys, g["REF_PANEL_TOP"]))

    def pose(f):
        u = ease((f - f0) / max(1, f1 - f0))
        tgt = Vector(prod(args.track[0] + (args.track[1] - args.track[0]) * u, v_keys, g["REF_PANEL_TOP"]))
        cam.location = tgt + rel
        look(cam, tgt, Vector((0, 0, 1)))
        cam.data.dof.focus_distance = rel.length
        return {"target_x_px": args.track[0] + (args.track[1] - args.track[0]) * u}
else:
    cam = g["camera"]("VS " + args.camera, **CAMS[args.camera])
scene.camera = cam
scene.frame_start, scene.frame_end = f0, f1

sweep = None
if args.light_sweep:
    # A narrow soft light crossing the panel left to right (editor x), above it, aimed down.
    ld = bpy.data.lights.new("VS sweep", "AREA")
    ld.shape, ld.size, ld.size_y = "RECTANGLE", 6 * PX, EH_M * PX * 1.1
    ld.energy = 40.0
    sweep = bpy.data.objects.new("VS sweep", ld)
    scene.collection.objects.link(sweep)
    sweep.rotation_euler = (0.0, 0.0, ROT_RAD)
    s0, s1 = args.light_sweep

    def sweep_x(f):
        return -0.05 * EW_M + 1.10 * EW_M * ease((f - s0) / max(1, s1 - s0))

for f in range(f0, f1 + 1):
    path = os.path.join(out, "frames", f"{f:04d}.png")
    if vproject.png_ok(path):   # resumable: only complete frames count
        continue
    scene.frame_set(f)
    if pose:
        pose(f)
    if sweep:
        sweep.location = Vector(prod(sweep_x(f), EH_M / 2, g["REF_PANEL_TOP"])) + Vector((0, 0, 0.04))
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
path_log = {}
for f in range(f0, f1 + 1):
    rec = {}
    if pose:
        rec.update(pose(f))
    if sweep:
        rec["sweep_x_px"] = sweep_x(f)
    if rec:
        path_log[f] = rec

audio = os.path.join(P, "audio", "daw_bounce.wav")
audio = audio if os.path.exists(audio) else os.path.join(P, "audio", "headless.wav")
mp4 = os.path.join(out, f"{args.name or args.camera}.mp4")
subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(args.fps), "-start_number", str(f0),
                "-i", os.path.join(out, "frames", "%04d.png"), "-ss", f"{(f0 - 1) / args.fps:.6f}", "-i", audio,
                "-map", "0:v", "-map", "1:a", "-shortest", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18",
                "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", mp4], check=False)
vproject.write_json(os.path.join(out, "render_meta.json"), {
    "camera": args.camera, "camera_settings": {k: (list(v) if hasattr(v, "__len__") and not isinstance(v, str) else v) for k, v in CAMS.get(args.camera, {}).items()},
    "setup": args.setup, "shot": args.shot, "fps": args.fps, "frames": [f0, f1], "res": args.res,
    "samples": args.samples, "noise_threshold": args.noise, "seed": 0, "blender": bpy.app.version_string,
    "capture_build": meta.get("build"), "capture_state": meta.get("state"), "audio": os.path.relpath(audio, P),
    "key_travel_front_m": travel, "controls": [kb["param"] for kb in knobs], "per_frame": path_log})
print("wrote", mp4)
