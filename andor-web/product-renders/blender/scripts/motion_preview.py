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
p.add_argument("--plan", required=True, choices=["D1", "D2", "D3", "G2"])
p.add_argument("--out", required=True)
p.add_argument("--res", type=int, nargs=2, default=[640, 360])
p.add_argument("--fps", type=int, default=12)
p.add_argument("--samples", type=int, default=16)
p.add_argument("--cuts", default=None, help="comma-separated cut names to render (default: all)")
p.add_argument("--handles", type=float, default=0.0, help="seconds of hold added before and after each cut (copies of its rest frames)")
p.add_argument("--range", type=int, nargs=2, default=None, help="render only frames [a, b) of each cut (quality tests)")
p.add_argument("--width", type=int, default=None, help="G2: draft width; each cut keeps its own shot's aspect (or its res)")
args = p.parse_args(argv)

# Gate D2 (Phase 5): the motion library's shots D01-D08 as individual shots with handles, each in
# its native composition (no automatic crops). D08 Interface is rendered by performance_anim.py
# (--camera interface), where the panel plays the real captured editor frames.
G2_CUTS = [
    # D01 Gallery reveal: the approved move, a subtle half-yaw variant, a slightly larger
    # instrument, the dedicated W composition and the native mobile one. The W and mobile cuts
    # end exactly on their stills (yaw / pitch / dolly 0 / 0 / 1), so hold frame and poster register.
    {"name": "D01_approved", "setup": "ANDOR_GALLERY", "shot": "A1_R1", "seconds": 6.0, "yaw": [-9.0, 3.0], "pitch": [4.0, 0.0], "dolly": [1.10, 1.0]},
    {"name": "D01_subtle", "setup": "ANDOR_GALLERY", "shot": "A1_R1", "seconds": 6.0, "yaw": [-4.5, 1.5], "pitch": [2.0, 0.0], "dolly": [1.05, 1.0]},
    {"name": "D01_larger", "setup": "ANDOR_GALLERY", "shot": "A1_R1", "seconds": 6.0, "yaw": [-4.5, 1.5], "pitch": [2.0, 0.0], "dolly": [0.97, 0.92]},
    {"name": "D01_W", "setup": "ANDOR_GALLERY", "shot": "A1_R1_W", "seconds": 6.0, "yaw": [-6.0, 0.0], "pitch": [3.0, 0.0], "dolly": [1.08, 1.0]},
    {"name": "D01_mobile", "setup": "ANDOR_GALLERY", "shot": "A1M_R1", "seconds": 6.0, "yaw": [-6.0, 0.0], "pitch": [3.0, 0.0], "dolly": [1.06, 1.0]},
    # D02 Dark orbit: native 4:5 (B1-R4) and 9:16 (B2_115_R4); the floating shadow renders every frame.
    {"name": "D02_orbit_4x5", "setup": "ANDOR_FLOATING", "shot": "B1_R4", "seconds": 6.0, "yaw": [-7.0, 5.0], "pitch": [-2.0, 1.0], "dolly": [1.04, 1.0]},
    {"name": "D02_orbit_9x16", "setup": "ANDOR_FLOATING", "shot": "B2_115_R4", "seconds": 6.0, "yaw": [-7.0, 5.0], "pitch": [-2.0, 1.0], "dolly": [1.04, 1.0]},
    # D03-D07 and the source controls: the six material subjects, 6 s each, a controlled glide.
    {"name": "D03_macro_glide", "setup": "ANDOR_MATERIAL", "shot": "M01_cine", "seconds": 5.0, "yaw": [5.0, -5.0], "pitch": [0, 0], "dolly": [1.0, 0.96]},
    {"name": "D04_inside_the_sound", "setup": "ANDOR_MATERIAL", "shot": "M03_cine", "seconds": 5.0, "yaw": [-4.0, 4.0], "pitch": [2.0, 0], "dolly": [1.02, 1.0]},
    {"name": "D05_keybed", "setup": "ANDOR_MATERIAL", "shot": "M04_cine", "seconds": 5.0, "yaw": [6.0, -2.0], "pitch": [0, 0], "dolly": [1.0, 0.97]},
    {"name": "D06_material", "setup": "ANDOR_MATERIAL", "shot": "M05_cine", "seconds": 5.0, "yaw": [-6.0, 4.0], "pitch": [0, 0], "dolly": [1.04, 0.98]},
    {"name": "D06b_source_controls", "setup": "ANDOR_MATERIAL", "shot": "M02_cine", "seconds": 5.0, "yaw": [-5.0, 3.0], "pitch": [1.0, 0], "dolly": [1.03, 1.0]},
    {"name": "D07_expression", "setup": "ANDOR_MATERIAL", "shot": "M06_R2_cine", "seconds": 5.0, "yaw": [-5.0, 3.0], "pitch": [0, 0], "dolly": [1.03, 1.0]},
]

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
    "G2": G2_CUTS,
}


def ease(t):
    return t * t * (3.0 - 2.0 * t)   # smoothstep: starts and ends at rest


def lerp(a, b, t):
    return a + (b - a) * t


os.makedirs(os.path.join(args.out, "frames"), exist_ok=True)


def complete_png(path):
    """A frame counts as rendered only if the PNG is whole (ends with its IEND chunk): a write
    that failed (a full disk) leaves an empty or truncated file that must render again."""
    try:
        with open(path, "rb") as f:
            f.seek(0, 2)
            if f.tell() < 64:
                return False
            f.seek(-12, 2)
            return f.read()[4:8] == b"IEND"
    except OSError:
        return False
plan = [c for c in PLANS[args.plan] if args.cuts is None or c["name"] in args.cuts.split(",")]
info = {"plan": args.plan, "fps": args.fps, "res": args.res, "samples": args.samples, "cuts": []}
SETUPS = json.load(open(os.path.join(HERE, "..", "config", "campaign_setups.json")))["setups"]
for cut in plan:
    res = list(args.res)
    if args.width:   # the shot's own aspect at the draft width (even sizes for H.264)
        sw, sh = SETUPS[cut["setup"]]["shots"][cut["shot"]]["res"]
        res = [args.width, 2 * round(args.width * sh / sw / 2)]
    sys.argv = ["andor_scene.py", "--", "--setup", cut["setup"], "--shot", cut["shot"],
                "--res", str(res[0]), str(res[1]), "--samples", str(args.samples), "--noise", "0.05"]
    g = runpy.run_path(os.path.join(HERE, "andor_scene.py"), run_name="__main__")
    bpy, Vector, Euler = g["bpy"], g["Vector"], g["Euler"]
    scene = bpy.context.scene
    scene.cycles.seed = 0                  # a fixed seed: the noise pattern does not crawl between frames
    scene.cycles.use_animated_seed = False
    scene.render.fps = args.fps
    cam = scene.camera
    # The shot's focus point: along the camera's view at its focus distance (from its own
    # rotation: matrix_world is not evaluated yet when the scene script returns).
    bpy.context.view_layer.update()
    fwd = cam.rotation_euler.to_quaternion() @ Vector((0, 0, -1))
    dist0 = cam.data.dof.focus_distance if cam.data.dof.use_dof else 1.0
    target = cam.location + fwd * dist0
    rel0 = cam.location - target
    n = max(2, round(cut["seconds"] * args.fps))
    cdesc = {"name": cut["name"], "setup": cut["setup"], "shot": cut["shot"], "seconds": cut["seconds"], "frames": n, "res": res,
             "move": {k: cut[k] for k in ("yaw", "pitch", "dolly")}, "target_m": list(target)}
    for i in (range(*args.range) if args.range else range(n)):
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
        if not complete_png(scene.render.filepath):   # resumable
            bpy.ops.render.render(write_still=True)
    info["cuts"].append(cdesc)
    if args.plan == "G2" and not args.range:
        # Per shot: handles (copies of the rest frames: the move starts and ends at rest), an
        # editing master (PNG sequence), a web draft MP4, the poster (its closing frame), a manifest.
        import shutil
        sd = os.path.join(args.out, cut["name"])
        os.makedirs(os.path.join(sd, "master"), exist_ok=True)
        h = round(args.handles * args.fps)
        src = [os.path.join(args.out, "frames", f"{cut['name']}_{i:04d}.png") for i in range(n)]
        seq = [src[0]] * h + src + [src[-1]] * h
        for k, f in enumerate(seq):
            # Hard links, not copies: a master is the rendered frames plus repeated rest frames, so
            # copying would double the disk use of every 16-bit sequence (and filled the disk once).
            dst = os.path.join(sd, "master", f"{k + 1:04d}.png")
            if os.path.exists(dst):
                os.remove(dst)
            try:
                os.link(f, dst)
            except OSError:
                shutil.copyfile(f, dst)
        shutil.copyfile(src[-1], os.path.join(sd, "poster.png"))
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(args.fps), "-i", os.path.join(sd, "master", "%04d.png"),
                        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "20", "-movflags", "+faststart", "-an",
                        os.path.join(sd, f"{cut['name']}_draft.mp4")], check=False)
        cdesc.update({"handles_frames": h, "total_frames": len(seq), "fps": args.fps, "samples": args.samples,
                      "seed": 0, "music": None, "typography": None, "transitions": None,
                      "poster": "poster.png (= the last frame of the move)", "master": "master/%04d.png",
                      "web": f"{cut['name']}_draft.mp4", "status": "draft for review"})
        json.dump(cdesc, open(os.path.join(sd, "manifest.json"), "w"), indent=1)
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
