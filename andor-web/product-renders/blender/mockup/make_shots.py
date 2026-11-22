#!/usr/bin/env python3
"""Feature and hero shots from the mockup studio: real editor captures, composed over the DAW
session (screen_content/daw.html), then rendered on the display (screen_mockup.py).

  make_shots.py <captures dir> <out dir> [--python <bpy python>] [--res 1920x1080] [--samples 64] [--only name,...]

<captures dir> holds the [web-capture] PNGs and their .json bounds (tests/plugin/WebCaptureTests.inc).
For a close-up, the camera's focus is the centre of the part the capture's bounds name, mapped
through the plugin window's position on the screen, so each shot lands on its feature exactly.
"""
import json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
CAP, OUT = sys.argv[1], sys.argv[2]
opt = lambda k, d: sys.argv[sys.argv.index(k) + 1] if k in sys.argv else d
PY = opt("--python", sys.executable)
RES = opt("--res", "1920x1080")
SAMPLES = opt("--samples", "64")
ONLY = set(opt("--only", "").split(",")) - {""}
os.makedirs(OUT, exist_ok=True)

# name: (capture, bounds key or None, preset, tone, window scale, window px, py, extra args)
SHOTS = {
    "hero-three-dark":   ("web-01-hero-3L@2x.png", None, "three", "dark", 1080, 0.62, 0.55, []),
    "hero-curved-dark":  ("web-01-hero-3L@2x.png", None, "curved", "dark", 1000, 0.6, 0.5, []),
    "hero-desk-light":   ("web-01-hero-3L@2x.png", None, "desk", "light", 1080, 0.62, 0.55, []),
    "hero-float-light":  ("web-01-hero-3L@2x.png", None, "float", "light", 1080, 0.62, 0.55, []),
    "hero-low-dark":     ("web-01-hero-3L@2x.png", None, "low", "dark", 1080, 0.62, 0.55, []),
    "feat-reimagined":   ("web-06-reimagined-2-tape-frame@3x.png", "popup", "macro", "dark", 1240, 0.55, 0.5, ["--zoom", "1.1"]),
    "feat-space":        ("web-06-space@3x.png", "popup", "macro", "dark", 1240, 0.55, 0.5, ["--zoom", "1.1"]),
    "feat-life":         ("web-06-life@3x.png", "popup", "macro", "dark", 1240, 0.62, 0.5, ["--zoom", "1.1"]),
    "feat-keydna":       ("web-09-keydna@2x.png", "keydna", "three", "dark", 1240, 0.55, 0.5, []),
    "feat-capture":      ("web-09-capture@2x.png", "card", "macro", "dark", 1240, 0.55, 0.5, ["--zoom", "0.55"]),
    "feat-notemap":      ("web-09-notemap@2x.png", "card", "macro", "dark", 1240, 0.55, 0.5, ["--zoom", "0.55"]),
    "feat-layers-1":     ("web-01-hero-1L@2x.png", None, "three", "dark", 1080, 0.62, 0.55, []),
    "feat-layers-2":     ("web-01-hero-2L@2x.png", None, "three", "dark", 1080, 0.62, 0.55, []),
    # solo: the editor alone fills the display (scale 0: no DAW, no desktop), for the page's main images
    "solo-three":        ("web-01-hero-3L@2x.png", None, "three", "dark", 0, 0, 0, ["--zoom", "0.82"]),
    "solo-desk":         ("web-01-hero-3L@2x.png", None, "desk", "light", 0, 0, 0, ["--zoom", "0.85"]),
    "solo-space":        ("web-06-space@3x.png", "popup", "macro", "dark", 0, 0, 0, ["--zoom", "1.25"]),
    "solo-capture":      ("web-09-capture@2x.png", "card", "macro", "dark", 0, 0, 0, ["--zoom", "0.42"]),
    "solo-notemap":      ("web-09-notemap@2x.png", "card", "macro", "dark", 0, 0, 0, ["--zoom", "0.42"]),
    "solo-keydna":       ("web-09-keydna@2x.png", "keydna", "three", "dark", 0, 0, 0, ["--zoom", "0.82"]),
}

for name, (cap, key, preset, tone, scale, px, py, extra) in SHOTS.items():
    if ONLY and name not in ONLY:
        continue
    img = os.path.join(CAP, cap)
    if scale:
        content = os.path.join(OUT, f"content-{name}.png")
        q = f"plugin=file://{img}&layout=window&scale={scale}&px={px}&py={py}"
        subprocess.run(["node", os.path.join(HERE, "screen_content", "render_content.cjs"), content, q], check=True)
    else:
        content = img          # the capture itself is the screen, at its own aspect
    args = ["--image", content, "--preset", preset, "--tone", tone, "--out", os.path.join(OUT, f"{name}.png"), "--res", RES, "--samples", SAMPLES] + extra
    if key:
        from PIL import Image
        iw, ih = Image.open(img).size
        b = json.load(open(os.path.join(CAP, cap.split("@")[0] + ".json")))[key]
        if scale:
            k = scale / 1448.0                    # editor px -> css px (the editor is 1448 wide)
            h = scale * ih / iw + 30
            left, top = (1920 - scale) * px, (1080 - h) * py + 30
            cx, cy = left + (b["x"] + b["w"] / 2) * k, top + (b["y"] + b["h"] / 2) * k
            args += ["--focus", f"{cx / 1920:.4f},{cy / 1080:.4f}"]
        else:
            eh = 1448.0 * ih / iw                 # editor height in editor px
            args += ["--focus", f"{(b['x'] + b['w'] / 2) / 1448:.4f},{(b['y'] + b['h'] / 2) / eh:.4f}"]
    subprocess.run([PY, os.path.join(HERE, "screen_mockup.py"), "--"] + args, check=True, stdout=subprocess.DEVNULL)
    print("shot", name)
