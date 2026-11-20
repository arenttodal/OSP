#!/usr/bin/env python3
"""Three software <-> object transitions from a Virtual Studio project (system Python + Pillow).

  python3 vs_transitions.py <project> --fps 24 [--cut 7.0]

Inputs:
- renders/ortho_plain/<fps>fps/frames: the registered frontal ortho camera;
- renders/display_push/<fps>fps/frames and render_meta.json: the push into card A's display;
- renders/ortho_sweep/<fps>fps/frames and render_meta.json: the ortho camera with the light sweep;
- captures/<fps>fps/editor/NNNN.png: the real editor, 1x.

Outputs go to renders/transitions/<fps>fps/: T1_match_cut.mp4, T2_into_display.mp4, T3_light_sweep.mp4
(muxed with the audio of record), plus key-frame sheets.

The software is only ever scaled uniformly (aspect kept) and the physical frames are never warped:
- **T1:** cuts on a note onset, from the registered object to the editor.
- **T2:** ends the Blender push with card A's display filling the frame, then continues from the same
  display in the real UI, at the same scale, and pulls back to the whole editor.
- **T3:** reveals the UI behind the light's edge. The mask edge sits at the light's recorded position:
  ortho, so editor x maps linearly to image x.
"""
import argparse
import json
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vstudio import project  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("project")
ap.add_argument("--fps", type=int, default=24)
ap.add_argument("--cut", type=float, default=7.0, help="T1: the note onset (seconds) the cut lands on")
ap.add_argument("--master-geometry", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "product-renders",
                                                          "blender", "textures_0.9.0-beta.1", "geometry.json"))
a = ap.parse_args()
P, F = a.project, a.fps
R = lambda name: os.path.join(P, "renders", name, f"{F}fps")
ed = lambda f: os.path.join(P, "captures", f"{F}fps", "editor", f"{f:04d}.png")
out = os.path.join(P, "renders", "transitions", f"{F}fps")
os.makedirs(out, exist_ok=True)
geo = json.load(open(a.master_geometry))
EW, EH = geo["editor_px"]


def frames_of(name):
    d = os.path.join(R(name), "frames")
    return {int(f[:4]): os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith(".png") and project.png_ok(os.path.join(d, f))}


def editor_frame(f, size):
    """The real editor at frame f, cropped to the master's layout (top 1448 x 982) and scaled to size."""
    im = Image.open(ed(f)).convert("RGB")
    im = im.crop((0, 0, EW, min(im.height, EH)))
    return im.resize(size, Image.LANCZOS)


def editor_region(f, box, size):
    """A region of the real editor (editor px, aspect matching size) scaled uniformly to size."""
    im = Image.open(ed(f)).convert("RGB")
    return im.crop(tuple(int(round(v)) for v in box)).resize(size, Image.LANCZOS)


def encode(name, seq_dir, first):
    audio = os.path.join(P, "audio", "daw_bounce.wav")
    audio = audio if os.path.exists(audio) else os.path.join(P, "audio", "headless.wav")
    mp4 = os.path.join(out, name + ".mp4")
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(F), "-start_number", str(first),
                    "-i", os.path.join(seq_dir, "%04d.png"), "-ss", f"{(first - 1) / F:.6f}", "-i", audio,
                    "-map", "0:v", "-map", "1:a", "-shortest", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18",
                    "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", mp4], check=True)
    return mp4


def sheet(name, picks):
    ims = [Image.open(p).convert("RGB") for _, p in picks]
    w = 480
    th = [im.resize((w, round(im.height * w / im.width))) for im in ims]
    S = Image.new("RGB", (len(th) * (w + 10), th[0].height + 22), "white")
    d = ImageDraw.Draw(S)
    for i, (lab, _) in enumerate(picks):
        S.paste(th[i], (i * (w + 10), 22))
        d.text((i * (w + 10) + 4, 5), lab, fill="black")
    S.save(os.path.join(out, name + "_sheet.jpg"), quality=88)


report = {}
# ---- T1: match cut on a note onset.
plain = frames_of("ortho_plain")
if plain:
    size = Image.open(next(iter(plain.values()))).size
    cut_f = 1 + int(round(a.cut * F))
    seq = os.path.join(out, "T1_frames")
    os.makedirs(seq, exist_ok=True)
    fs = sorted(plain)
    for f in fs:
        (Image.open(plain[f]).convert("RGB") if f < cut_f else editor_frame(f, size)).save(os.path.join(seq, f"{f:04d}.png"))
    report["T1"] = {"frames": [fs[0], fs[-1]], "cut_frame": cut_f, "cut_time_s": (cut_f - 1) / F, "mp4": encode("T1_match_cut", seq, fs[0])}
    sheet("T1_match_cut", [("object, frame before the cut", os.path.join(seq, f"{cut_f - 1:04d}.png")),
                           ("editor, the cut frame (note onset)", os.path.join(seq, f"{cut_f:04d}.png"))])

# ---- T2: into the display, out of the real UI.
push = frames_of("display_push")
if push:
    meta = json.load(open(os.path.join(R("display_push"), "render_meta.json")))
    size = Image.open(next(iter(push.values()))).size
    aspect = size[1] / size[0]
    fs = sorted(push)
    last = fs[-1]
    pf = meta["per_frame"][str(last)]
    cx, cy = pf["target_px"]
    w_end = pf["visible_width_px"]   # the display's width, filling the frame
    seq = os.path.join(out, "T2_frames")
    os.makedirs(seq, exist_ok=True)
    for f in fs:
        Image.open(push[f]).convert("RGB").save(os.path.join(seq, f"{f:04d}.png"))
    n_ui = len(fs)   # the pull back lasts as long as the push
    for k in range(1, n_ui + 1):
        f = last + k
        u = k / n_ui
        u = u * u * (3 - 2 * u)
        w = w_end + (EW - w_end) * u
        ccx = cx + (EW / 2 - cx) * u
        ccy = cy + (EH / 2 - cy) * u
        h = w * aspect
        box = (ccx - w / 2, ccy - h / 2, ccx + w / 2, ccy + h / 2)
        editor_region(f, box, size).save(os.path.join(seq, f"{f:04d}.png"))
    report["T2"] = {"push_frames": [fs[0], last], "ui_frames": [last + 1, last + n_ui], "seam_display_width_px": w_end,
                    "mp4": encode("T2_into_display", seq, fs[0])}
    sheet("T2_into_display", [("object: the push ends on display A", os.path.join(seq, f"{last:04d}.png")),
                              ("real UI: the same display, next frame", os.path.join(seq, f"{last + 1:04d}.png")),
                              ("real UI: pulled back", os.path.join(seq, f"{last + n_ui:04d}.png"))])

# ---- T3: light-assisted reveal.
sw = frames_of("ortho_sweep")
if sw:
    meta = json.load(open(os.path.join(R("ortho_sweep"), "render_meta.json")))
    size = Image.open(next(iter(sw.values()))).size
    seq = os.path.join(out, "T3_frames")
    os.makedirs(seq, exist_ok=True)
    fs = sorted(sw)
    feather = max(2, round(size[0] * 0.012))
    for f in fs:
        obj = Image.open(sw[f]).convert("RGB")
        ui = editor_frame(f, size)
        x = meta["per_frame"][str(f)]["sweep_x_px"] / EW * size[0]
        mask = Image.new("L", size, 0)
        ImageDraw.Draw(mask).rectangle((0, 0, int(round(x)), size[1]), fill=255)
        mask = mask.filter(ImageFilter.GaussianBlur(feather))
        Image.composite(ui, obj, mask).save(os.path.join(seq, f"{f:04d}.png"))
    mid = fs[len(fs) // 2]
    report["T3"] = {"frames": [fs[0], fs[-1]], "mp4": encode("T3_light_sweep", seq, fs[0])}
    sheet("T3_light_sweep", [("start", os.path.join(seq, f"{fs[0]:04d}.png")), ("mid-sweep", os.path.join(seq, f"{mid:04d}.png")),
                             ("end", os.path.join(seq, f"{fs[-1]:04d}.png"))])

project.write_json(os.path.join(out, "transitions_meta.json"), report)
print(json.dumps(report, indent=1))
