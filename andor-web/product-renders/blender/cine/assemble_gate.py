#!/usr/bin/env python3
"""Assembles the creative gate's review package from the rig's renders (system Python + Pillow, ffmpeg).

  python3 assemble_gate.py --renders <gate dir> --out <package dir> [--version 1]

Per shot (<renders>/<id>/beauty, keyframes from <renders>/<id>/keyframes_beauty when present,
otherwise taken from the draft frames at 0, 1/4, 1/2, 3/4 and 1 of the move):
- the full-motion draft, H.264, 24 fps, silent (no music, no transitions):
  andor-osp_<R0n>_<variant>_16x9_<w>w_24fps_draft_v<NN>.mp4
- the five key frames (opening, quarter, mid, three-quarter, closing) and a sheet of them;
- R01: the typography preview (wordmark behind the product through the matte) next to the clean
  master, and the desktop composition with the headline; the matte as a grey-scale preview.
Names follow Launch Kit v2: a draft never carries a final-implying name.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
TYPO = os.path.join(HERE, "..", "..", "..", "launch-kit", "typography", "compose_wordmark.py")
ap = argparse.ArgumentParser()
ap.add_argument("--renders", required=True)
ap.add_argument("--out", required=True)
ap.add_argument("--version", type=int, default=1)
a = ap.parse_args()
V = f"v{a.version:02d}"
LABELS = ["0_open", "1_quarter", "2_mid", "3_three_quarter", "4_close"]


def name(shot, kind, w, ext, fps=True):
    fam, var = shot.split("_", 1)
    return f"andor-osp_{fam}_{var.replace('_', '-')}{'-' + kind if kind else ''}_16x9_{w}w{'_24fps' if fps else ''}_draft_{V}.{ext}"


def encode(frames_dir, mp4, first):
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", "24", "-start_number", str(first), "-i",
                    os.path.join(frames_dir, "%04d.png"), "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "17",
                    "-preset", "slow", "-movflags", "+faststart", "-an", mp4], check=True)


def sheet(paths, out, labels):
    ims = [Image.open(p).convert("RGB") for p in paths]
    w = 384
    th = [im.resize((w, round(im.height * w / im.width)), Image.LANCZOS) for im in ims]
    S = Image.new("RGB", (len(th) * (w + 8) - 8, th[0].height + 22), (250, 248, 244))
    d = ImageDraw.Draw(S)
    for i, (t, lab) in enumerate(zip(th, labels)):
        S.paste(t, (i * (w + 8), 22))
        d.text((i * (w + 8) + 2, 5), lab, fill=(40, 36, 32))
    S.save(out, quality=90)


os.makedirs(a.out, exist_ok=True)
index = []
for shot in sorted(os.listdir(a.renders)):
    bdir = os.path.join(a.renders, shot, "beauty")
    if not os.path.isdir(bdir):
        continue
    frames = sorted(int(f[:4]) for f in os.listdir(bdir) if f.endswith(".png"))
    spec = json.load(open(os.path.join(HERE, "shots", shot + ".json")))
    expected = int(round(spec["seconds"] * 24)) + 1
    if len(frames) < expected:
        print("incomplete", shot, len(frames), "/", expected)
        continue
    w = Image.open(os.path.join(bdir, f"{frames[0]:04d}.png")).width
    d = os.path.join(a.out, spec["family"].split()[0], shot)
    os.makedirs(d, exist_ok=True)
    rec = {"shot": shot, "family": spec["family"], "variant": spec["variant"], "seconds": spec["seconds"], "frames": len(frames)}
    mp4 = os.path.join(d, name(shot, "", w, "mp4"))
    encode(bdir, mp4, frames[0])
    rec["draft"] = os.path.relpath(mp4, a.out)
    picks = [frames[round(u * (len(frames) - 1))] for u in (0, 0.25, 0.5, 0.75, 1.0)]
    kdir = os.path.join(d, "keyframes")
    os.makedirs(kdir, exist_ok=True)
    kp = []
    for lab, f in zip(LABELS, picks):
        dst = os.path.join(kdir, name(shot, lab.replace("_", "-"), w, "png", fps=False))
        shutil.copyfile(os.path.join(bdir, f"{f:04d}.png"), dst)
        kp.append(dst)
    sheet(kp, os.path.join(d, name(shot, "keyframes", w, "jpg", fps=False)), ["opening", "quarter", "mid", "three-quarter", "closing"])
    mdir = os.path.join(a.renders, shot, "matte")
    if shot.startswith("R01") and os.path.isdir(mdir) and len(os.listdir(mdir)) >= len(frames):
        tdir = os.path.join(a.renders, shot, "typo")
        layout = "desktop" if shot.endswith("desktop") else "centre"
        ty = spec.get("typography", {})
        extra = []
        if "reveal" in ty:
            extra += ["--reveal"] + [str(x) for x in ty["reveal"]]
        if "blur_from" in ty:
            extra += ["--blur-from", str(ty["blur_from"])]
        subprocess.run([sys.executable, TYPO, "--beauty", bdir, "--matte", mdir, "--out", tdir, "--layout", layout] + extra, check=True)
        tmp4 = os.path.join(d, name(shot, "typography", w, "mp4"))
        encode(tdir, tmp4, frames[0])
        rec["typography_preview"] = os.path.relpath(tmp4, a.out)
        rec["typography_meta"] = json.load(open(os.path.join(tdir, "typography_meta.json")))
        tp = [os.path.join(tdir, f"{f:04d}.png") for f in picks]
        sheet(tp, os.path.join(d, name(shot, "typography-keyframes", w, "jpg", fps=False)), ["opening", "quarter", "mid", "three-quarter", "closing"])
        # the matte as a preview (grey = alpha), proving the occlusion source
        mp = os.path.join(d, name(shot, "matte-close", w, "png", fps=False))
        Image.open(os.path.join(mdir, f"{picks[-1]:04d}.png")).getchannel("A").save(mp)
    cdir = os.path.join(a.renders, shot, "clean")
    if os.path.isdir(cdir) and len(os.listdir(cdir)) >= len(frames):
        cmp4 = os.path.join(d, name(shot, "background-pass", w, "mp4"))
        encode(cdir, cmp4, frames[0])
        rec["background_pass"] = os.path.relpath(cmp4, a.out)
    index.append(rec)
    print("assembled", shot)
stills = os.path.join(a.renders, "stills")
if os.path.isdir(stills):
    sd = os.path.join(a.out, "stills")
    os.makedirs(sd, exist_ok=True)
    for f in sorted(os.listdir(stills)):
        if f.endswith(".png"):
            shot = f.split("_still")[0]
            im = Image.open(os.path.join(stills, f))
            im.convert("RGB").save(os.path.join(sd, name(shot, "still", im.width, "jpg", fps=False)), quality=94)
            shutil.copyfile(os.path.join(stills, f), os.path.join(sd, name(shot, "still", im.width, "png", fps=False)))
json.dump(index, open(os.path.join(a.out, "gate_index.json"), "w"), indent=1)
print(len(index), "shots ->", a.out)
