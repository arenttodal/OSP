"""Performance-driven animation (Phase 5 proof of concept, E1-E3): a recorded performance drives the
approved Blender master.

  <bpy python> product-renders/blender/scripts/performance_anim.py -- --performance <dir> --out <dir>
      [--res 960 540] [--samples 24] [--frames A B] [--camera perf|keys|macros|display]

Input: a performance directory in the common format (performance.json; see
documentation/performance_format.md), written by capturing a real plugin session:
  notes        timestamped note-on / note-off with velocity           -> E1: each key travels and returns
  automation   parameter envelopes (e.g. "character" = FILTER)       -> E2: knob angles come from the
  captures     per frame: the editor without knob caps (face), the       editor's own per-frame values
               editor as seen, every rotary's pointer angle (knobs)  -> E3: the panel face plays the real
  audio        the plugin's own output                                   editor frames (waveforms, read
                                                                         heads, arcs, key colours)
Nothing is invented: keys move only for recorded notes, knobs only to the editor's recorded angles,
the displays show only the captured frames, and the MP4 carries the captured audio.

The scene is built by andor_scene.py from the approved C3 master (geometry, materials, light); only
the keys are split into individual objects (as the master already measures them) and pivoted at
their backs, and the knobs pivoted at their own centres.
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
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
ap = argparse.ArgumentParser()
ap.add_argument("--performance", required=True)
ap.add_argument("--out", required=True)
ap.add_argument("--res", type=int, nargs=2, default=[960, 540])
ap.add_argument("--samples", type=int, default=24)
ap.add_argument("--frames", type=int, nargs=2, default=None, help="first and last frame (1-based, inclusive)")
ap.add_argument("--camera", default="perf", choices=["perf", "keys", "macros", "display", "ortho", "interface"])
ap.add_argument("--key-angle", type=float, default=3.5, help="key travel at the front, degrees about the key's back")
args = ap.parse_args(argv)

perf = json.load(open(os.path.join(args.performance, "performance.json")))
fps, frames = perf["fps"], perf["frames"]
os.makedirs(os.path.join(args.out, "frames"), exist_ok=True)

# ---- E3 input: the captured face frames, cropped exactly as prep_textures.py crops the master's
# (system Python + Pillow, before Blender: performance_prep.py).
seq_dir = os.path.join(args.out, "face_seq")
face_files = sorted(glob.glob(os.path.join(seq_dir, "*.png")))
if not face_files:
    raise SystemExit("run performance_prep.py first (it writes <out>/face_seq)")

# ---- The approved master (no render): A2's light and setup; the camera is set below.
sys.argv = ["andor_scene.py", "--", "--setup", "ANDOR_GALLERY", "--shot", "A2", "--res", str(args.res[0]), str(args.res[1]),
            "--samples", str(args.samples), "--noise", "0.03"]
g = runpy.run_path(os.path.join(HERE, "andor_scene.py"), run_name="__main__")
bpy, Vector, Euler = g["bpy"], g["Vector"], g["Euler"]
scene = bpy.context.scene
scene.render.fps = int(fps)
ex, ey, prod, PX, MM, KY = g["ex"], g["ey"], g["prod"], g["PX"], g["MM"], g["KY"]
GEO = g["GEO"]

# E3: the face texture becomes the captured sequence (same crop, same UVs).
face_img = None
for im in bpy.data.images:
    if im.filepath and os.path.basename(im.filepath) == "osp_face.png":
        face_img = im
if face_img is not None:
    face_img.source = "SEQUENCE"
    face_img.filepath = os.path.join(seq_dir, "0001.png")
    for mat in bpy.data.materials:
        if mat.node_tree is None:
            continue
        for n in mat.node_tree.nodes:
            if n.type == "TEX_IMAGE" and n.image == face_img:
                n.image_user.frame_duration = len(face_files)
                n.image_user.frame_start = 1
                n.image_user.frame_offset = 0
                n.image_user.use_auto_refresh = True
                n.image_user.use_cyclic = False
print("face texture sequence:", face_img is not None, len(face_files), "frames")


# ---- E1: individual keys, pivoted at their backs.
def separate(name):
    o = bpy.data.objects.get(name)
    if o is None:
        return []
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = o
    o.select_set(True)
    for m in list(o.modifiers):   # bake the bevel so each key keeps its edges once split
        bpy.ops.object.modifier_apply(modifier=m.name)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.separate(type="LOOSE")
    bpy.ops.object.mode_set(mode="OBJECT")
    return [x for x in bpy.context.selected_objects]


whites = separate("White keys")
blacks = separate("Black keys")
whites.sort(key=lambda o: sum((v.co.x for v in o.data.vertices)) / max(1, len(o.data.vertices)))
blacks.sort(key=lambda o: sum((v.co.x for v in o.data.vertices)) / max(1, len(o.data.vertices)))
WHITE_PC = {0, 2, 4, 5, 7, 9, 11}
white_notes = [n for n in range(21, 109) if n % 12 in WHITE_PC]
black_notes = [n for n in range(21, 109) if n % 12 not in WHITE_PC]
print("keys:", len(whites), "white,", len(blacks), "black (expected 52 / 36)")
key_of = {}
back_y_world = ey(906.0 + KY)
for objs, notes in ((whites, white_notes), (blacks, black_notes)):
    for o, n in zip(objs, notes):
        vs = [v.co for v in o.data.vertices]
        # the key's back edge: the end nearest the panel (the vertices with the largest |y - front|)
        ys = [v.y for v in vs]
        front_is_low = abs(min(ys) - back_y_world) > abs(max(ys) - back_y_world)
        back = max(ys) if front_is_low else min(ys)
        top = max(v.z for v in vs)
        pivot = Vector((sum(v.x for v in vs) / len(vs), back, top))
        for v in o.data.vertices:
            v.co -= pivot
        o.location = o.location + pivot
        o["front_sign"] = -1.0 if front_is_low else 1.0   # rotation sign that lowers the front
        key_of[n] = o

ang = math.radians(args.key_angle)
for n, o in key_of.items():
    o.rotation_mode = "XYZ"
    o.rotation_euler = (0, 0, 0)
    o.keyframe_insert("rotation_euler", index=0, frame=1)
for note in perf["notes"]:
    o = key_of.get(note["note"])
    if o is None:
        continue
    down = o["front_sign"] * -ang
    f_on = 1 + note["on"] * fps
    f_off = 1 + note["off"] * fps
    press = 1.0 if note["velocity"] > 80 else 1.6     # a softer note takes the key down a little slower
    for f, a in ((f_on - 0.01, 0.0), (f_on + press, down), (f_off, down), (f_off + 2.5, 0.0)):
        o.rotation_euler[0] = a
        o.keyframe_insert("rotation_euler", index=0, frame=f)
for o in key_of.values():
    if o.animation_data and o.animation_data.action:
        for fc in o.animation_data.action.fcurves:
            for kp in fc.keyframe_points:
                kp.interpolation = "SINE"

# ---- E2: macro knobs turn to the editor's recorded angles (frame by frame).
knob_objs = {}
for i, k in enumerate(GEO["knobs"]):
    o = bpy.data.objects.get(f"Knob {k['kind']} {i:02d}")
    if o is not None and k["kind"] == "macro":
        knob_objs[i] = (o, k)   # (each knob's origin is already its own centre)
MACRO_PARAMS = ("life", "drive", "character", "motion", "echo", "space")
captured = [json.load(open(f)) for f in sorted(glob.glob(os.path.join(args.performance, "knobs", "*.json")))]
for i, (o, k) in knob_objs.items():
    # the captured rotary nearest this knob's centre (editor px)
    def nearest(rows):   # the six macros have distinct x positions
        return min((r for r in rows if r["param"] in MACRO_PARAMS), key=lambda r: abs(r["x"] - k["c"][0]))
    base = k["angle"]
    o.rotation_mode = "XYZ"
    rest = o.rotation_euler[2]   # the master's own turn (the captured angle `base`): moves are relative to it
    for f, rows in enumerate(captured):
        r = nearest(rows)
        o.rotation_euler[2] = rest - (r["angle"] - base)   # editor clockwise -> a clockwise turn seen from above
        o.keyframe_insert("rotation_euler", index=2, frame=f + 1)
    print("knob", i, "->", nearest(captured[0])["param"])

# ---- Cameras (no product motion; the performance is the motion).
cams = {
    "perf": dict(yaw=-20, pitch=50, dist=0.74, lens=85, target=prod(640, 700, g["REF_PANEL_TOP"]), fstop=11),
    "keys": dict(yaw=-24, pitch=40, dist=0.34, lens=100, target=prod(560, 960 + KY, g["REF_PANEL_TOP"]), fstop=11),
    "macros": dict(yaw=-16, pitch=52, dist=0.30, lens=100, target=prod(420, 812 + KY, g["REF_PANEL_TOP"]), fstop=11),
    "display": dict(yaw=-14, pitch=46, dist=0.30, lens=100, target=prod(260, 330, g["REF_PANEL_TOP"]), fstop=11),
}
FACE_TARGET = prod(724, 500, g["REF_PANEL_TOP"])
if args.camera in ("ortho", "interface") and args.res == [960, 540]:
    args.res = [960, 2 * round(960 * perf["editor"][1] / perf["editor"][0] / 2)]   # the editor's own aspect
    scene.render.resolution_x, scene.render.resolution_y = args.res
if args.camera == "ortho":
    # E4: the frontal orthographic view; it registers with editor/NNNN.png (1448 px = the panel's width).
    cam = g["camera"]("Perf ortho", yaw=0, pitch=0, dist=0.8, lens=85, target=FACE_TARGET)
    cam.data.type = "ORTHO"
    cam.data.sensor_fit = "HORIZONTAL"
    cam.data.ortho_scale = 1448 * PX
elif args.camera == "interface":
    # D08 Interface: a near-frontal perspective that glides into the frontal view and ends framed
    # exactly as the orthographic camera (the panel's 1448 px across the frame's width), so the
    # cut to the real editor capture matches its landmarks. A long lens keeps perspective low.
    INTERFACE_LENS = 135.0
    end_dist = INTERFACE_LENS / 36.0 * 1448 * PX
    cam = g["camera"]("Perf interface", yaw=-10, pitch=22, dist=end_dist * 1.12, lens=INTERFACE_LENS, target=FACE_TARGET, fit="HORIZONTAL")
    cam.data.dof.use_dof = False   # the whole face sharp, as the editor is

    def interface_pose(f, f_first, f_last):
        t = (f - f_first) / max(1, f_last - f_first)
        t = t * t * (3.0 - 2.0 * t)
        yaw, pitch, dist = -10.0 * (1 - t), 22.0 * (1 - t), end_dist * (1.12 + (1.0 - 1.12) * t)
        d = Vector((0, -math.sin(math.radians(pitch)), math.cos(math.radians(pitch))))
        d.rotate(Euler((0, 0, math.radians(yaw))))
        cam.location = Vector(FACE_TARGET) + d * dist
        cam.rotation_euler = (Vector(FACE_TARGET) - cam.location).to_track_quat("-Z", "Y").to_euler()
else:
    cam = g["camera"]("Perf " + args.camera, **cams[args.camera])
scene.camera = cam

f0, f1 = args.frames if args.frames else (1, frames)
scene.frame_start, scene.frame_end = f0, f1
for f in range(f0, f1 + 1):
    path = os.path.join(args.out, "frames", f"{f:04d}.png")
    if os.path.exists(path):
        continue
    scene.frame_set(f)
    if args.camera == "interface":
        interface_pose(f, f0, f1)
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)

# ---- A playable MP4 with the captured audio (offset to the first frame rendered).
mp4 = os.path.join(args.out, f"performance_{args.camera}.mp4")
subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(fps), "-start_number", str(f0),
                "-i", os.path.join(args.out, "frames", "%04d.png"), "-ss", f"{(f0 - 1) / fps:.4f}",
                "-i", os.path.join(args.performance, perf["audio"]), "-map", "0:v", "-map", "1:a", "-shortest",
                "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", "-c:a", "aac", "-b:a", "192k", mp4], check=False)
print("wrote", mp4)
