"""Gate D motion preproduction: low-resolution camera-move previews from the approved master.

  <bpy python> product-renders/blender/scripts/motion_preview.py --plan D1 --out <dir> [--res 640 360] [--fps 12] [--samples 16]

Each plan is a list of cuts. A cut starts from an approved campaign shot (its setup, light, product
rotation and camera, unchanged) and moves only the camera, slowly, around the shot's own focus
point: orbit (yaw about the vertical), tilt (pitch), dolly (distance factor) and a focus pull.
The product never moves, so its shadow is always the scene's own (no shadow pass is translated).
The scene is rebuilt per cut by andor_scene.py; frames render with Cycles at preview quality.
No typography is added. Output: frames/<cut>_<n>.png, opening and closing frames, and an MP4
per plan (ffmpeg), plus plan.json with the camera path for the final production.
"""
import argparse
import json
import math
import os
import runpy
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
p = argparse.ArgumentParser()
p.add_argument("--plan", required=True, choices=["D1", "D2", "D3"])
p.add_argument("--out", required=True)
p.add_argument("--res", type=int, nargs=2, default=[640, 360])
p.add_argument("--fps", type=int, default=12)
p.add_argument("--samples", type=int, default=16)
p.add_argument("--cuts", default=None, help="comma-separated cut names to render (default: all)")
args = p.parse_args(argv)

# A cut: setup, shot, seconds, and the camera move as start -> end deltas (eased).
#   yaw (deg about the vertical through the focus point), pitch (deg), dolly (distance factor),
#   fstop (None: the shot's own), res override (for vertical plans).
PLANS = {
    # D1, The Instrument Reveal (light hero): one continuous, slow move on A1-R1's warm gallery.
    "D1": [
        {"name": "reveal", "setup": "ANDOR_GALLERY", "shot": "A1_R1", "seconds": 9.0,
         "yaw": [-9.0, 3.0], "pitch": [4.0, 0.0], "dolly": [1.10, 1.0]},
    ],
    # D2, The Material Sequence: five slow detail moves on the MATERIAL light, then the full instrument.
    "D2": [
        {"name": "1_timber", "setup": "ANDOR_MATERIAL", "shot": "M05_cine", "seconds": 3.0, "yaw": [-6.0, 4.0], "pitch": [0, 0], "dolly": [1.04, 0.98]},
        {"name": "2_macros", "setup": "ANDOR_MATERIAL", "shot": "M01_cine", "seconds": 3.0, "yaw": [5.0, -5.0], "pitch": [0, 0], "dolly": [1.0, 0.96]},
        {"name": "3_display", "setup": "ANDOR_MATERIAL", "shot": "M03_cine", "seconds": 3.0, "yaw": [-4.0, 4.0], "pitch": [2.0, 0], "dolly": [1.02, 1.0]},
        {"name": "4_keyboard", "setup": "ANDOR_MATERIAL", "shot": "M04_cine", "seconds": 3.0, "yaw": [6.0, -2.0], "pitch": [0, 0], "dolly": [1.0, 0.97]},
        {"name": "5_strips", "setup": "ANDOR_MATERIAL", "shot": "M06_R1_cine", "seconds": 3.0, "yaw": [-5.0, 3.0], "pitch": [0, 0], "dolly": [1.03, 1.0]},
        {"name": "6_full", "setup": "ANDOR_GALLERY", "shot": "A2", "seconds": 4.0, "yaw": [-4.0, 2.0], "pitch": [2.0, 0], "dolly": [1.06, 1.0]},
    ],
    # D3, Floating Campaign: the dark architectural composition, a slow arc; the shadow is rendered every frame.
    "D3": [
        {"name": "float", "setup": "ANDOR_FLOATING", "shot": "B1_R4", "seconds": 8.0,
         "yaw": [-7.0, 5.0], "pitch": [-2.0, 1.0], "dolly": [1.04, 1.0]},
    ],
}


def ease(t):
    return t * t * (3.0 - 2.0 * t)   # smoothstep: starts and ends at rest


def lerp(a, b, t):
    return a + (b - a) * t


os.makedirs(os.path.join(args.out, "frames"), exist_ok=True)
plan = [c for c in PLANS[args.plan] if args.cuts is None or c["name"] in args.cuts.split(",")]
info = {"plan": args.plan, "fps": args.fps, "res": args.res, "samples": args.samples, "cuts": []}
for cut in plan:
    sys.argv = ["andor_scene.py", "--", "--setup", cut["setup"], "--shot", cut["shot"],
                "--res", str(args.res[0]), str(args.res[1]), "--samples", str(args.samples), "--noise", "0.05"]
    g = runpy.run_path(os.path.join(HERE, "andor_scene.py"), run_name="__main__")
    bpy, Vector, Euler = g["bpy"], g["Vector"], g["Euler"]
    scene = bpy.context.scene
    cam = scene.camera
    # The shot's focus point: along the camera's view at its focus distance (from its own
    # rotation: matrix_world is not evaluated yet when the scene script returns).
    bpy.context.view_layer.update()
    fwd = cam.rotation_euler.to_quaternion() @ Vector((0, 0, -1))
    dist0 = cam.data.dof.focus_distance if cam.data.dof.use_dof else 1.0
    target = cam.location + fwd * dist0
    rel0 = cam.location - target
    n = max(2, round(cut["seconds"] * args.fps))
    cdesc = {"name": cut["name"], "setup": cut["setup"], "shot": cut["shot"], "seconds": cut["seconds"], "frames": n,
             "move": {k: cut[k] for k in ("yaw", "pitch", "dolly")}, "target_m": list(target)}
    for i in range(n):
        t = ease(i / (n - 1))
        yaw, pitch, dolly = (lerp(*cut[k], t) for k in ("yaw", "pitch", "dolly"))
        r = rel0.copy()
        # pitch: tilt about the horizontal axis perpendicular to the view, then yaw about world Z
        axis = Vector((r.x, r.y, 0.0)).cross(Vector((0, 0, 1)))
        if axis.length > 1e-6 and pitch:
            from mathutils import Matrix
            r = Matrix.Rotation(math.radians(pitch), 3, axis.normalized()) @ r
        r.rotate(Euler((0, 0, math.radians(yaw))))
        cam.location = target + r * dolly
        cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
        if cam.data.dof.use_dof:
            cam.data.dof.focus_distance = (target - cam.location).length
        scene.render.filepath = os.path.join(args.out, "frames", f"{cut['name']}_{i:04d}.png")
        bpy.ops.render.render(write_still=True)
    info["cuts"].append(cdesc)
    print("cut", cut["name"], n, "frames")

json.dump(info, open(os.path.join(args.out, "plan.json"), "w"), indent=1)
# One MP4 for the plan (cuts in order, hard cuts), plus its opening and closing frames.
frames_dir = os.path.join(args.out, "frames")
lst = os.path.join(args.out, "frames.txt")
names = sorted(f for f in os.listdir(frames_dir) if f.endswith(".png"))
with open(lst, "w") as f:
    for nme in names:
        f.write(f"file '{os.path.join(frames_dir, nme)}'\nduration {1.0 / args.fps:.6f}\n")
subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", lst, "-r", str(args.fps),
                "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "20", os.path.join(args.out, f"{args.plan}_preview.mp4")], check=False)
for tag, nme in (("open", names[0]), ("close", names[-1])):
    subprocess.run(["cp", os.path.join(frames_dir, nme), os.path.join(args.out, f"{args.plan}_{tag}.png")])
print("done", args.plan)
