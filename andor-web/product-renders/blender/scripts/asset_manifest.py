"""Final asset manifest for the approved commercial stills (Gate C3, Phase 4).

  python3 product-renders/blender/scripts/asset_manifest.py <masters dir> <derivatives.json> <batch script> <out dir>

For every master: identifier, file, dimensions, bit depth, colour space, alpha, the setup / shot /
pass and camera it was rendered with (read back from config/campaign_setups.json, overrides
included), samples, intended use, the optimized derivatives that exist, the source files and
their commits, and SHA-256 hashes of the master and of the scene's input textures (which are
git-ignored), so every approved render can be reproduced later. Writes asset_manifest.json and
asset_manifest.md.
"""
import hashlib
import json
import os
import re
import shlex
import struct
import subprocess
import sys

masters, deriv_path, batch, out = sys.argv[1:5]
HERE = os.path.dirname(os.path.abspath(__file__))
BLENDER = os.path.dirname(HERE)
CFG = json.load(open(os.path.join(BLENDER, "config", "campaign_setups.json")))
deriv = json.load(open(deriv_path))


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def git(*a):
    return subprocess.run(["git", *a], cwd=BLENDER, capture_output=True, text=True).stdout.strip()


def last_commit(rel):
    return git("log", "-1", "--format=%h", "--", rel)


def png_header(path):
    d = open(path, "rb").read(33)
    w, h = struct.unpack(">II", d[16:24])
    return w, h, d[24], d[25]  # width, height, bit depth, colour type (2 RGB, 6 RGBA)


# The batch lines: r <name> --setup X --shot Y [--pass P] [--res W H] --samples N --noise T
renders = {}
for line in open(batch):
    line = line.strip().replace("${B1SHOT}", "B1_R4").replace("$B1SHOT", "B1_R4")   # the batch's default B1 shot
    m = re.match(r"r (\S+) (.*)", line)
    if m:
        renders[m.group(1)] = shlex.split(m.group(2))
    if line.startswith("for m in M01"):
        for i in range(1, 7):
            for v in ("sharp", "cine"):
                renders[f"C3_M0{i}_{v}"] = ["--setup", "ANDOR_MATERIAL", "--shot", f"M0{i}_{v}", "--samples", "80", "--noise", "0.008"]
# Phase 4: M06 re-rendered from its R1 shots (Edge strip +22 deg; sharp at f/22).
for v in ("sharp", "cine"):
    renders[f"C3_M06_{v}"] = ["--setup", "ANDOR_MATERIAL", "--shot", f"M06_R1_{v}", "--samples", "80", "--noise", "0.008"]


def opt(args, k, n=1):
    if k in args:
        i = args.index(k)
        return args[i + 1:i + 1 + n] if n > 1 else args[i + 1]
    return None


USAGE = {
    "A1_R1": "Website desktop hero (live type left of the product)",
    "A1_R1_W": "Website desktop hero, wide-headline layout (product about 6 % further right)",
    "A1M_R1": "Website mobile hero (19.5:9; crops to 9:16 top and bottom)",
    "A2": "Gallery / product photography",
    "B1_R4": "Dark campaign, 4:5 feed",
    "B2_115_R4": "Dark campaign, 9:16 vertical (stories, reels)",
    "product": "Transparent product for compositing over any background",
    "product_shadow": "Transparent product with its floor shadow baked in",
    "shadow": "Shadow-only layer (black, alpha) to pair with the transparent product",
    "background": "Clean background plate without the product",
    "beauty": "Combined beauty pass at the transparent set's camera",
    "M01": "Detail: macros", "M02": "Detail: source controls", "M03": "Detail: display",
    "M04": "Detail: keyboard", "M05": "Detail: front-left corner (timber, ceramic)", "M06": "Detail: PITCH / MOD touch strips",
}

texdir = os.path.join(BLENDER, "textures_current")
textures = {f: sha(os.path.join(texdir, f)) for f in sorted(os.listdir(texdir))} if os.path.isdir(texdir) else {}
sources = {
    "scene": ("scripts/andor_scene.py", last_commit("scripts/andor_scene.py")),
    "config": ("config/campaign_setups.json", last_commit("config/campaign_setups.json")),
    "textures_prep": ("scripts/prep_textures.py", last_commit("scripts/prep_textures.py")),
    "derivatives": ("scripts/web_derivatives.py", last_commit("scripts/web_derivatives.py")),
}

rows = []
for name in sorted(os.listdir(masters)):
    if not name.endswith(".png"):
        continue
    base = name[:-4]
    path = os.path.join(masters, name)
    w, h, depth, ctype = png_header(path)
    args = renders.get(base, [])
    setup, shot, pss = opt(args, "--setup"), opt(args, "--shot"), opt(args, "--pass") or "beauty"
    S = CFG["setups"].get(setup, {}) if setup else {}
    sh = S.get("shots", {}).get(shot, {}) if shot else {}
    ov = sh.get("overrides", {})
    if pss != "beauty":
        key = pss
    elif "combined" in base:
        key = "beauty"
    else:
        key = shot.split("_")[0] if shot and shot.startswith("M0") else shot
    rows.append({
        "id": base.replace("C3_", "").rsplit("_", 1)[0] if re.search(r"_\d+x\d+$", base) else base.replace("C3_", ""),
        "master": name,
        "width": w, "height": h, "bit_depth": depth,
        "colour": "sRGB display; Blender AgX view transform, look 'AgX - Punchy', exposure %s EV (baked)" % ov.get("exposure", S.get("exposure")),
        "alpha": ctype == 6,
        "setup": setup, "shot": shot, "pass": pss,
        "camera": sh.get("camera"), "rot_deg": sh.get("rot"),
        "lighting": {"rig": setup, "lights": [L["name"] for L in S.get("lights", [])],
                     "overrides": {k: v for k, v in ov.items() if k != "why"} or None},
        "samples": int(opt(args, "--samples")) if opt(args, "--samples") else None,
        "noise_threshold": float(opt(args, "--noise")) if opt(args, "--noise") else None,
        "usage": USAGE.get(key, ""),
        "derivatives": sorted({f"{d['format']} {d['width']}" for d in deriv if d["master"] == name},
                              key=lambda s: (s.split()[0], int(s.split()[1]))),
        "command": "andor_scene.py -- " + " ".join(args + ["--out", name]),
        "sha256": sha(path),
    })

doc = {"schemaVersion": 1, "about": "ANDOR commercial stills, Gate C3 (approved) with the Phase 4 M06 correction",
       "repository": "arenttodal/OSP", "branch": git("rev-parse", "--abbrev-ref", "HEAD"), "head": git("rev-parse", "--short", "HEAD"),
       "blender": "4.2 (bpy), Cycles CPU", "sources": {k: {"file": v[0], "commit": v[1]} for k, v in sources.items()},
       "textures_current_sha256": textures, "assets": rows}
os.makedirs(out, exist_ok=True)
json.dump(doc, open(os.path.join(out, "asset_manifest.json"), "w"), indent=1)

L = ["# ANDOR commercial stills · final asset manifest", "",
     f"Repository `{doc['repository']}`, branch `{doc['branch']}`. Blender {doc['blender']}.", "",
     "**Sources:** " + ", ".join(f"`{v['file']}` @ `{v['commit']}`" for v in doc["sources"].values()) + ".",
     "**Scene textures** (`blender/textures_current/`, git-ignored, archived with Package A): " +
     ", ".join(f"`{k}` sha256 `{v[:12]}…`" for k, v in textures.items()) + ".", "",
     "Every master is a 16-bit PNG in sRGB (AgX view, 'AgX - Punchy' look, baked). The derivatives are 8-bit sRGB AVIF and WebP. "
     "The full camera, light overrides, command line and SHA-256 hash of each master are in `asset_manifest.json`.", "",
     "| Asset | Master | Size | Alpha | Setup · shot · pass | Camera | Samples | Use | Derivatives |",
     "|---|---|---|---|---|---|---|---|---|"]
for r in rows:
    c = r["camera"] or {}
    cam = f"yaw {c.get('yaw')}, pitch {c.get('pitch')}, {c.get('lens')} mm, f/{c.get('fstop')}" if c else ""
    widths = sorted({int(d.split()[1]) for d in r["derivatives"]})
    L.append(f"| {r['id']} | `{r['master']}` | {r['width']} × {r['height']} | {'yes' if r['alpha'] else 'no'} | "
             f"{r['setup']} · {r['shot']} · {r['pass']} | {cam} | {r['samples']} | {r['usage']} | "
             f"AVIF + WebP at {', '.join(map(str, widths))} |")
open(os.path.join(out, "asset_manifest.md"), "w").write("\n".join(L) + "\n")
print(len(rows), "assets")
