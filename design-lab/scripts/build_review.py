"""Build the review package (design-lab/review/) from the composites.

    python scripts/build_review.py

Inputs: mockups/full-gui/<direction>@{1,2}x.png and mockups/components/** (composite_gui.py),
the production export (references/current-gui/). Outputs:

  review/00-original.png, 01..06-*.png       full GUI, 2x (2896 x 2172), identical framing
  review/actual-size/<name>@{1,1.5,2}x.png   a fixed crop at 100 / 150 / 200 % GUI scale
  review/component-comparisons/*.png         labelled family sheets (actual size + enlarged)
  review/closeups/*.png                      the same region in all seven GUIs
  review/contact-sheets/overview.png         all seven at a glance
  review/web/*.jpg                           light copies for the gallery (index.html)
"""

import json
import os
import shutil
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(LAB)
REVIEW = os.path.join(LAB, "review")
FONTS = os.path.join(REPO, "apps", "plugin", "Resources", "fonts")
sys.path.insert(0, os.path.join(LAB, "scripts"))
sys.path.insert(0, os.path.join(LAB, "blender"))

with open(os.path.join(LAB, "blender", "configs", "directions.json"), "r", encoding="utf-8") as f:
    DIRECTIONS = json.load(f)["directions"]
with open(os.path.join(LAB, "blender", "configs", "controls.json"), "r", encoding="utf-8") as f:
    CTRL = json.load(f)

NAMES = ["00-original"] + list(DIRECTIONS)
LABELS = {"00-original": "00 Original (production today)", **{k: v["label"] for k, v in DIRECTIONS.items()}}
BG = (246, 243, 237)
INK = (35, 33, 30)
MUTED = (110, 104, 96)


def font(size, weight="Regular"):
    return ImageFont.truetype(os.path.join(FONTS, f"Inter-{weight}.ttf"), size)


def full(name, scale):
    if name == "00-original":
        d = {1: "export1x", 1.5: "export1.5x", 2: "export2x"}[scale]
        return Image.open(os.path.join(LAB, "references", "current-gui", d, f"full@{scale:g}x.png")).convert("RGB")
    return Image.open(os.path.join(LAB, "mockups", "full-gui", f"{name}@{scale:g}x.png")).convert("RGB")


def crop(img, rect, scale):
    x, y, w, h = rect
    return img.crop((int(round(x * scale)), int(round(y * scale)), int(round((x + w) * scale)), int(round((y + h) * scale))))


def comp(name):
    return Image.open(os.path.join(LAB, "mockups", "components", name + ".png")).convert("RGB")


def shrink(img, factor):
    return img.resize((max(1, round(img.width / factor)), max(1, round(img.height / factor))), Image.BOX if factor == int(factor) else Image.LANCZOS)


class Sheet:
    """A vertical stack of labelled rows."""

    def __init__(self, title, subtitle, width):
        self.width = width
        self.rows = []
        self.title, self.subtitle = title, subtitle

    def add(self, label, note, images, captions=None, gap=24):
        self.rows.append((label, note, images, captions or [""] * len(images), gap))

    def render(self, path):
        pad, head = 48, 132
        heights = []
        for label, note, images, captions, gap in self.rows:
            heights.append(64 + max(im.height for im in images) + 34)
        img = Image.new("RGB", (self.width, head + sum(heights) + pad), BG)
        d = ImageDraw.Draw(img)
        d.text((pad, 36), self.title, font=font(40, "SemiBold"), fill=INK)
        d.text((pad, 88), self.subtitle, font=font(20), fill=MUTED)
        y = head
        for (label, note, images, captions, gap), h in zip(self.rows, heights):
            d.text((pad, y + 6), label, font=font(24, "SemiBold"), fill=INK)
            d.text((pad, y + 38), note, font=font(17), fill=MUTED)
            x = pad
            for im, cap in zip(images, captions):
                img.paste(im, (x, y + 64))
                if cap:
                    d.text((x, y + 64 + im.height + 6), cap, font=font(15, "Medium"), fill=MUTED)
                x += max(im.width, int(d.textlength(cap, font=font(15, "Medium")))) + gap
            y += h
        os.makedirs(os.path.dirname(path), exist_ok=True)
        img.save(path, optimize=True)
        return img


# ----------------------------------------------------------------------------- packages
def full_guis():
    for n in NAMES:
        full(n, 2).save(os.path.join(REVIEW, f"{n}.png"), optimize=True)


ACTUAL_RECT = (25, 560, 960, 360)   # engine buttons, macro panel, tabs: the densest strip


def actual_size():
    from composite_gui import compose_direction
    out = os.path.join(REVIEW, "actual-size")
    os.makedirs(out, exist_ok=True)
    for n in NAMES:
        for s in (1, 1.5, 2):
            if n == "00-original":
                img = full(n, s)
            elif s == 1.5:
                img = compose_direction(n, 1.5)
            else:
                img = full(n, s)
            crop(img, ACTUAL_RECT, s).save(os.path.join(out, f"{n}@{s:g}x.png"), optimize=True)


# Close-ups compare today, the round-1 favourite and round 2 (the round-1 sheets stay as built).
CLOSEUP_NAMES = ["00-original", "02-warm-ceramic", "09-warm-ceramic-teak", "10-celadon-teak", "11-studio-keys", "12-studio-keys-approved-macros"]
CLOSEUPS = {
    "macro-panel": (30, 676, 1388, 228),
    "modulation-tabs": (948, 680, 466, 132),
    "engine-buttons": (35, 570, 451, 82),
    "volume-slider": (1140, 38, 214, 60),
    "teak-edge": (0, 0, 260, 400),
}


def closeups():
    out = os.path.join(REVIEW, "closeups")
    os.makedirs(out, exist_ok=True)
    names = [n for n in NAMES if n in CLOSEUP_NAMES]
    imgs = {n: full(n, 2) for n in names}
    for key, rect in CLOSEUPS.items():
        tiles = [crop(imgs[n], rect, 2) for n in names]
        if key in ("engine-buttons", "volume-slider", "modulation-tabs", "teak-edge"):
            cols = 2 if key not in ("volume-slider", "teak-edge") else 3
        else:
            cols = 1
        w, h = tiles[0].size
        rows = (len(tiles) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * (w + 24) + 72, rows * (h + 46) + 120), BG)
        d = ImageDraw.Draw(sheet)
        d.text((48, 30), f"Close-up: {key.replace('-', ' ')} (2x, identical crop)", font=font(30, "SemiBold"), fill=INK)
        for i, (n, t) in enumerate(zip(names, tiles)):
            x, y = 48 + (i % cols) * (w + 24), 90 + (i // cols) * (h + 46)
            d.text((x, y), LABELS[n], font=font(17, "Medium"), fill=MUTED)
            sheet.paste(t, (x, y + 24))
        sheet.save(os.path.join(out, f"{key}.png"), optimize=True)


def contact_sheet():
    out = os.path.join(REVIEW, "contact-sheets")
    os.makedirs(out, exist_ok=True)
    tiles = [shrink(full(n, 1), 2) for n in NAMES]
    w, h = tiles[0].size
    cols = 4
    rows = (len(tiles) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (w + 20) + 60, rows * (h + 44) + 110), BG)
    d = ImageDraw.Draw(sheet)
    d.text((40, 28), "ANDOR/OSP design lab: the original, round 1 (01-06) and round 2 (09-12); same layout and values", font=font(26, "SemiBold"), fill=INK)
    for i, (n, t) in enumerate(zip(NAMES, tiles)):
        x, y = 40 + (i % cols) * (w + 20), 80 + (i // cols) * (h + 44)
        d.text((x, y), LABELS[n], font=font(16, "Medium"), fill=MUTED)
        sheet.paste(t, (x, y + 22))
    sheet.save(os.path.join(out, "overview.png"), optimize=True)


def sheets():
    out = os.path.join(REVIEW, "component-comparisons")
    # Buttons: actual size (1x) for every state, then 2x detail of normal and active.
    s = Sheet("Button families A1-A4", "Engine modifier row of card A (link, reverse, loop, REIMAGINED) in every state; same light, same icons. "
              "Top: actual size (100 %). Below: the card header and ARP / Advanced at 100 %, then 2x detail.", 2400)
    for a in ("A1", "A2", "A3", "A4", "A5", "A6"):
        fam = CTRL["buttons"][a]
        states = [shrink(comp(f"buttons/{a}-{st}"), 4) for st in ("normal", "hover", "pressed", "active", "disabled")]
        s.add(fam["label"] + " - states at 100 %", fam["rationale"], states, ["normal", "hover", "pressed", "active", "disabled"])
        head = shrink(comp(f"buttons/{a}-card-header"), 4)
        arp = shrink(comp(f"buttons/{a}-arp-advanced"), 4)
        detail = [shrink(comp(f"buttons/{a}-normal"), 2).crop((0, 0, 440, 122)), shrink(comp(f"buttons/{a}-active"), 2).crop((428, 0, 870, 122))]
        s.add(fam["label"] + " - secondary keys and 2x detail", "Mode selector, EQ, M/S (secondary weight); ARP card and Advanced; 2x close-up of normal and active keys.",
              [head, arp] + detail, ["card header 100 %", "ARP + Advanced 100 %", "normal 2x", "active 2x"])
    s.render(os.path.join(out, "buttons.png"))

    s = Sheet("Modulation tab families B1-B4", "The production words and sockets (AMP, ENV 1, ENV 2, LFO 1, LFO 2), unchanged; only the treatment differs. "
              "100 % then 2x. Six-tab layouts (MOD-FINAL) work with every family: each tab is sized by its word.", 2400)
    for b in ("B1", "B2", "B3", "B4"):
        fam = CTRL["tabs"][b]
        ims = [shrink(comp(f"tabs/{b}-{st}"), 4) for st in ("selected", "hover", "focused")]
        big = [shrink(comp(f"tabs/{b}-{st}"), 2) for st in ("selected", "hover")]
        s.add(fam["label"], fam["rationale"], ims + big, ["selected 100 %", "hover ENV 2 100 %", "focused 100 %", "selected 2x", "hover 2x"])
    s.render(os.path.join(out, "tabs.png"))

    s = Sheet("Volume fader caps C1-C3 x tracks T1-T3", "Cap = rendered object (never carries the value); fill and labels stay software. "
              "Each row: minimum, 25, 50, 75 %, maximum at 100 %, then hover and drag at 2x.", 2400)
    for c, t in [(c, t) for c in ("C1", "C2", "C3") for t in ("T1", "T2", "T3")] + [("C4", "T2"), ("C5", "T2")]:
        if True:
            pos = [shrink(comp(f"sliders/{c}-{t}-{p:03d}"), 4) for p in (0, 25, 50, 75, 100)]
            st = [shrink(comp(f"sliders/{c}-{t}-{x}"), 2) for x in ("hover", "drag")]
            s.add(f"{CTRL['faders'][c]['label']} on {CTRL['tracks'][t]['label'].lower()} ({t})", CTRL["faders"][c]["rationale"], pos + st,
                  ["min", "25 %", "50 %", "75 %", "max", "hover 2x", "drag 2x"], gap=16)
    s.render(os.path.join(out, "sliders.png"))

    s = Sheet("Macro panel surfaces D1-D6", "Same approved knobs, labels, values, halos, lighting and framing. 100 % (left) and the left half at 2x.", 2900)
    for dd in ("D1", "D2", "D3", "D4", "D5", "D6"):
        fam = CTRL["panels"][dd]
        im = comp(f"panels/{dd}")
        s.add(fam["label"], fam["rationale"], [shrink(im, 2), im.crop((0, 0, 1100, im.height))], ["100 %", "2x detail"])
    s.render(os.path.join(out, "macro-panels.png"))

    s = Sheet("Main background and source panels E1-E4", "Header and two engine cards; buttons A4 and panel D1 held constant. 100 % and 2x detail.", 2400)
    for e in ("E1", "E2", "E3", "E4"):
        fam = CTRL["enclosures"][e]
        im = comp(f"backgrounds/{e}")
        s.add(fam["label"], fam["rationale"], [shrink(im, 2), im.crop((0, 0, 1000, 600))], ["100 %", "2x detail"])
    s.render(os.path.join(out, "backgrounds.png"))

    s = Sheet("Small hardware details (F)", "Optional finishing details, each against the production drawing. 2x unless noted.", 2400)
    s.add("Macro LEDs: production bead vs rendered lens", "Lens LEDs sit in the panel; CHARACTER is lit (modulated).",
          [shrink(comp("details/leds-off"), 4), shrink(comp("details/leds-on"), 4), shrink(comp("details/leds-off"), 2).crop((0, 0, 900, 80)), shrink(comp("details/leds-on"), 2).crop((0, 0, 900, 80))],
          ["production 100 %", "lens 100 %", "production 2x", "lens 2x"])
    s.add("Divider: painted line / recessed groove / metal seam", "The macro-envelope divider as a 0.25 mm groove; the seam is a 1 px brushed inlay around the panel.",
          [shrink(comp("details/divider-flat"), 2), shrink(comp("details/divider-groove"), 2), shrink(comp("details/seam"), 2)], ["painted", "groove", "seam"])
    s.add("ARP light, button wells, recessed track", "Lens ARP LED; A2 wells (the inset keys' grooves); T2 recessed slot.",
          [shrink(comp("details/arp-led-flat"), 2), shrink(comp("details/arp-led-lens"), 2), shrink(comp("details/button-wells"), 2), shrink(comp("details/recessed-track"), 2)],
          ["ARP production", "ARP lens", "button wells", "recessed track"])
    s.render(os.path.join(out, "hardware-details.png"))

    # Round 2: the teak edge and the cream knob caps, each against production.
    s = Sheet("Round 2: teak edges and studio knobs", "Left: today. Right: round 2. Same light, values, rings and halos (the rings are the plugin's own).", 3000)
    s.add("Teak side cheeks", CTRL["trim"]["teak"]["rationale"],
          [comp("details/trim-none"), comp("details/trim-teak"), shrink(comp("details/trim-none"), 2), shrink(comp("details/trim-teak"), 2)],
          ["today 2x", "teak 2x", "today 100 %", "teak 100 %"])
    s.add("Layer knobs: production vs K2 cream caps", CTRL["knobs"]["K2"]["rationale"],
          [shrink(comp("knobs/production-layer"), 2), shrink(comp("knobs/K2-layer"), 2), shrink(comp("knobs/production-layer"), 4), shrink(comp("knobs/K2-layer"), 4)],
          ["production 2x", "K2 2x", "production 100 %", "K2 100 %"])
    s.add("Macro knobs: approved Blender knob vs K2 cream caps", "Direction 11 uses K2 on the macros too; 12 keeps the approved knob.",
          [comp("knobs/approved-macro"), comp("knobs/K2-macro")], ["approved 2x", "K2 2x"])
    s.render(os.path.join(out, "round2-details.png"))


def web():
    """JPEG copies for the gallery (the PNGs stay the masters)."""
    out = os.path.join(REVIEW, "web")
    os.makedirs(out, exist_ok=True)
    for root, _, files in os.walk(REVIEW):
        if root.startswith(out):
            continue
        for f in files:
            if not f.endswith(".png"):
                continue
            src = os.path.join(root, f)
            rel = os.path.relpath(src, REVIEW).replace(os.sep, "__")[:-4] + ".jpg"
            Image.open(src).convert("RGB").save(os.path.join(out, rel), quality=90, optimize=True, progressive=True)


def main():
    steps = sys.argv[1:] or ["full", "actual", "closeups", "contact", "sheets", "web"]
    os.makedirs(REVIEW, exist_ok=True)
    for st in steps:
        {"full": full_guis, "actual": actual_size, "closeups": closeups, "contact": contact_sheet, "sheets": sheets, "web": web}[st]()
        print("built", st, flush=True)


if __name__ == "__main__":
    main()
