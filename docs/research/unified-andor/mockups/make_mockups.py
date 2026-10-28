"""Annotated wireframes for docs/research/unified-andor/08-gui-proposals.md.

Research only: every image is a REAL screenshot of the current plugin (ceramic skin, rendered
headless from build-plugin at db136e5) with proposal overlays drawn on top. Overlays use one
deliberately foreign annotation colour (cyan, dashed) so nobody mistakes them for finished
design: they show WHERE something would live and HOW BIG it may be, not how it looks.
Geometry comes from apps/plugin/Source/Design.h and PluginEditor.cpp (see 08-gui-proposals.md).

    python3 -I make_mockups.py <screenshot-dir> <out-dir>
"""
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

SHOTS, OUT = Path(sys.argv[1]), Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)
NOTE = (0, 170, 255)          # annotation colour (not a design colour)
NOTE_FILL = (0, 170, 255, 40)
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def font(size, bold=False):
    return ImageFont.truetype(BOLD if bold else FONT, size)


# Keyboard geometry (PluginEditor.cpp:1547, :466-474): 148,913 1150x101, MIDI 21..108.
KB_X, KB_Y, KB_W, KB_H, KB_LO, KB_HI = 148, 913, 1150, 101, 21, 108
WHITE = [m for m in range(KB_LO, KB_HI + 1) if m % 12 in (0, 2, 4, 5, 7, 9, 11)]
WKEY = KB_W / len(WHITE)


def key_x(m):
    """Centre x of a key on the real keyboard."""
    if m % 12 in (0, 2, 4, 5, 7, 9, 11):
        return KB_X + (WHITE.index(m) + 0.5) * WKEY
    return KB_X + (WHITE.index(m - 1) + 1.0) * WKEY   # black key: on the boundary


def name(m):
    return ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][m % 12] + str(m // 12 - 1)


def dashed_rect(d, box, colour=NOTE, width=2, dash=8):
    x0, y0, x1, y1 = box
    for (a, b, c, e) in ((x0, y0, x1, y0), (x1, y0, x1, y1), (x1, y1, x0, y1), (x0, y1, x0, y0)):
        length = math.hypot(c - a, e - b)
        n = max(1, int(length // (2 * dash)))
        for i in range(n):
            t0, t1 = (2 * i) / (2 * n), (2 * i + 1) / (2 * n)
            d.line((a + (c - a) * t0, b + (e - b) * t0, a + (c - a) * t1, b + (e - b) * t1), fill=colour, width=width)


def callout(d, xy, number, text, anchor_right=False):
    x, y = xy
    d.ellipse((x - 13, y - 13, x + 13, y + 13), fill=NOTE)
    d.text((x, y), str(number), fill=(255, 255, 255), font=font(15, True), anchor="mm")
    if text:
        f = font(14, True)
        w = d.textlength(text, font=f)
        tx = x - 20 - w if anchor_right else x + 20
        d.rounded_rectangle((tx - 6, y - 12, tx + w + 6, y + 12), 4, fill=(255, 255, 255, 230), outline=NOTE)
        d.text((tx, y), text, fill=(0, 90, 140), font=f, anchor="lm")


def with_legend(img, title, lines):
    """The screenshot plus a notes strip below (the instrument itself is not resized)."""
    pad = 46 + 22 * len(lines)
    out = Image.new("RGBA", (img.width, img.height + pad), (250, 250, 250, 255))
    out.paste(img, (0, 0))
    d = ImageDraw.Draw(out)
    d.rectangle((0, img.height, img.width, img.height + pad), fill=(236, 244, 250))
    d.text((24, img.height + 12), title, fill=(0, 70, 110), font=font(18, True))
    for i, line in enumerate(lines):
        d.text((24, img.height + 40 + 22 * i), line, fill=(20, 40, 60), font=font(14))
    return out


def overlay(base):
    layer = Image.new("RGBA", base.size, (0, 0, 0, 0))
    return layer, ImageDraw.Draw(layer)


def regions(d, box, notes, colour, labels=True, grains=False, seed=3):
    """Note regions across a display's plot (proposal: passive atlas view)."""
    x0, y0, x1, y1 = box
    total = sum(n[1] for n in notes)
    x = x0 + 20
    span = (x1 - x0 - 40)
    import random
    rnd = random.Random(seed)
    for midi, length, cents in notes:
        w = span * length / total
        d.rounded_rectangle((x + 2, y0 + 30, x + w - 2, y1 - 26), 5, outline=colour + (255,), width=2, fill=colour + (34,))
        sx = x + w * 0.25
        d.rectangle((sx, y1 - 34, x + w * 0.8, y1 - 28), fill=colour + (150,))   # sustain band
        if labels:
            d.text((x + 8, y0 + 36), f"{name(midi)} {cents:+d}", fill=(240, 236, 228, 255), font=font(13, True))
        if grains:
            for _ in range(10):
                gx = rnd.uniform(sx, x + w * 0.8)
                gy = rnd.uniform(y0 + 70, y1 - 50)
                d.ellipse((gx - 3, gy - 3, gx + 3, gy + 3), fill=(250, 210, 170, 220))
        x += w


def key_tints(d, mapped, filled, unplayable, colour):
    """Proposal: OspKeyboard tints the lower 38 % of each white key (PluginEditor.cpp:138-196)."""
    for m in range(KB_LO, KB_HI + 1):
        if m % 12 not in (0, 2, 4, 5, 7, 9, 11):
            continue
        x = KB_X + WHITE.index(m) * WKEY
        y0, y1 = KB_Y + KB_H * 0.64, KB_Y + KB_H - 4
        if m in mapped:
            d.rectangle((x + 3, y0, x + WKEY - 3, y1), fill=colour + (190,))
            if mapped[m] > 1:
                d.text((x + WKEY / 2, y0 + 9), str(mapped[m]), fill=(255, 255, 255, 255), font=font(12, True), anchor="mm")
        elif m in filled:
            d.rectangle((x + 3, y0, x + WKEY - 3, y1), fill=colour + (70,))
        elif m in unplayable:
            for k in range(0, int(WKEY), 5):
                d.line((x + k, y1, x + k + 8, y0), fill=(150, 150, 150, 120), width=1)


def mockup_1():
    img = Image.open(SHOTS / "gui-ceramic-1-layer@1x.png").convert("RGBA")
    layer, d = overlay(img)
    callout(d, (1170, 160), 1, "unchanged: the drop of a one-shot works exactly as today", anchor_right=True)
    out = with_legend(Image.alpha_composite(img, layer), "1  Conventional one-shot - NOTHING changes",
                      ["No new control is visible until a feature is used: no NOTE MAP, no KEY DNA, no GRAIN MIX key.",
                       "Existing presets load with every new parameter at its neutral default (bit-identical audio)."])
    out.convert("RGB").save(OUT / "01-one-shot-unchanged.jpg", quality=86, optimize=True)


def granular_strip(d, display, extra=True):
    """The existing hover strip (EngineCard.cpp:1719-1727): 5 MiniKnobs in a 74 px band."""
    x0, y0, x1, y1 = display
    d.rectangle((x0 + 1, y1 - 80, x1 - 1, y1 - 1), fill=(10, 14, 16, 150))
    names = ["POS", "SIZE", "DENS", "TUNE", "SPREAD"]
    usable = (x1 - x0 - 24) - (110 if extra else 0)
    for i, n in enumerate(names):
        cx = x0 + 12 + usable * (i + 0.5) / len(names)
        d.ellipse((cx - 17, y1 - 66, cx + 17, y1 - 32), outline=(230, 225, 215, 255), width=2)
        d.text((cx, y1 - 20), n, fill=(230, 225, 215, 255), font=font(11, True), anchor="mm")
    if extra:
        bx = x1 - 112
        dashed_rect(d, (bx, y1 - 66, bx + 96, y1 - 34))
        d.text((bx + 48, y1 - 50), "KEY DNA", fill=NOTE + (255,), font=font(13, True), anchor="mm")
        return (bx + 48, y1 - 50)
    return None


def mockup_2():
    img = Image.open(SHOTS / "gui-ceramic-1-layer@1x.png").convert("RGBA")
    layer, d = overlay(img)
    display = (51, 190, 1398, 508)
    granular_strip(d, display, extra=False)
    d.rectangle((1196, 141, 1351, 179), fill=(255, 255, 255, 0))
    callout(d, (1270, 120), 1, "mode menu reads 'Granular' (existing ModeSelector)", anchor_right=True)
    callout(d, (700, 405), 2, "existing hover strip, unchanged values and ranges")
    out = with_legend(Image.alpha_composite(img, layer), "2  One-shot through the existing Granulator - unchanged",
                      ["Same five controls, same ranges, same sound (KEY DNA amount 0 = today's per-voice clouds).",
                       "Strip drawn as a wireframe: the real one is the existing MiniKnob strip that fades in on hover."])
    out.convert("RGB").save(OUT / "02-granular-unchanged.jpg", quality=86, optimize=True)


def mockup_3():
    img = Image.open(SHOTS / "gui-ceramic-1-layer@1x.png").convert("RGBA")
    layer, d = overlay(img)
    # KEY DNA editor: a takeover of the source band (35,127 1379x525), keyboard stays live.
    band = (35, 127, 1414, 652)
    d.rounded_rectangle(band, 12, fill=(24, 30, 32, 236))
    dashed_rect(d, band)
    d.text((60, 150), "A  -  KEY DNA", fill=(240, 236, 228, 255), font=font(18, True))
    chips = ["POS", "SIZE", "DENS", "SPREAD", "TUNE", "SCATTER*", "PAN*"]
    x = 260
    for i, c in enumerate(chips):
        w = d.textlength(c, font=font(13, True)) + 22
        d.rounded_rectangle((x, 140, x + w, 166), 6, outline=(230, 225, 215, 255), width=1,
                            fill=(224, 122, 80, 200) if i == 1 else None)
        d.text((x + w / 2, 153), c, fill=(240, 236, 228, 255), font=font(13, True), anchor="mm")
        x += w + 8
    for i, k in enumerate(["CURVE", "VARIATION", "INDIVIDUAL"]):
        cx = 1030 + i * 120
        d.ellipse((cx - 16, 138, cx + 16, 170), outline=(230, 225, 215, 255), width=2)
        d.text((cx, 182), k, fill=(200, 195, 185, 255), font=font(11, True), anchor="mm")
    plot = (KB_X, 205, KB_X + KB_W, 600)
    d.rectangle(plot, outline=(70, 80, 84, 255))
    mid = (plot[1] + plot[3]) / 2
    d.line((plot[0], mid, plot[2], mid), fill=(90, 100, 104, 255))
    import random
    rnd = random.Random(5)
    pts = [(21, 0.55), (40, 0.35), (60, 0.0), (80, -0.3), (108, -0.6)]
    def curve(m):
        for (a, va), (b, vb) in zip(pts, pts[1:]):
            if a <= m <= b:
                return va + (vb - va) * (m - a) / (b - a)
        return 0.0
    for m in range(KB_LO, KB_HI + 1):
        v = curve(m) + 0.25 * rnd.uniform(-1, 1)
        x = key_x(m)
        y = mid - v * (plot[3] - plot[1]) / 2.4
        d.line((x, mid, x, y), fill=(120, 130, 134, 200), width=3)
    last = None
    for m in range(KB_LO, KB_HI + 1):
        p = (key_x(m), mid - curve(m) * (plot[3] - plot[1]) / 2.4)
        if last:
            d.line((last, p), fill=(224, 122, 80, 255), width=3)
        last = p
    for m, v in pts:
        x, y = key_x(m), mid - v * (plot[3] - plot[1]) / 2.4
        d.ellipse((x - 6, y - 6, x + 6, y + 6), fill=(240, 236, 228, 255))
    for m in (48, 55, 64):   # sounding keys' effective values
        x = key_x(m)
        d.ellipse((x - 5, 610, x + 5, 620), fill=(250, 210, 120, 255))
    d.text((KB_X, 628), "aligned to the real keyboard below: x = the key's own position (MIDI 21-108)", fill=(200, 195, 185, 255), font=font(12))
    for m in (36, 60, 84):
        dashed_rect(d, (key_x(m) - 1, 600, key_x(m) + 1, KB_Y), width=1, dash=4)
    callout(d, (1395, 120), 1, "takeover of the source band only; macros, mod panel and keyboard stay", anchor_right=True)
    callout(d, (1360, 420), 2, "")
    callout(d, (260, 120), 3, "parameter chips (* = new in Phase 2)")
    out = with_legend(Image.alpha_composite(img, layer), "3  One-shot with KEY DNA (advanced, opened from the strip's KEY DNA key)",
                      ["(2) Curve per parameter = shared key-curve editor (mod::Curve, extracted from DampenEditor/ModCurveView);",
                       "grey bars = seeded per-key individuality (KeyDnaTable), dots = sounding notes' effective values.",
                       "The editor closes back to the card; nothing about the card, macros or keyboard geometry changes."])
    out.convert("RGB").save(OUT / "03-key-dna-editor.jpg", quality=86, optimize=True)


PHRASE = [(64, 3, 12), (67, 2, -6), (69, 4, 3), (67, 2, -2), (64, 3, 8), (62, 2, -14), (60, 5, 0), (62, 2, 5), (64, 4, -3)]


def mapped_keys():
    counts = {}
    for m, _, _ in PHRASE:
        counts[m] = counts.get(m, 0) + 1
    filled = {m for m in range(55, 75)} - set(counts)
    unplayable = {m for m in range(KB_LO, KB_HI + 1)} - set(counts) - filled
    return counts, filled, unplayable


def mockup_4():
    img = Image.open(SHOTS / "gui-ceramic-1-layer@1x.png").convert("RGBA")
    layer, d = overlay(img)
    display = (51, 190, 1398, 508)
    d.rectangle((52, 191, 1397, 507), fill=(26, 31, 33, 200))
    regions(d, display, PHRASE, (224, 150, 100))
    dashed_rect(d, (1062, 141, 1134, 179))
    d.text((1098, 160), "NOTES 9", fill=NOTE + (255,), font=font(13, True), anchor="mm")
    counts, filled, unplayable = mapped_keys()
    key_tints(d, counts, filled, unplayable, (224, 122, 80))
    callout(d, (1098, 120), 1, "NOTES key (like EQ): opens the NOTE MAP page", anchor_right=True)
    callout(d, (100, 230), 2, "detected notes as regions (passive view)")
    callout(d, (110, 975), 3, "")
    out = with_legend(Image.alpha_composite(img, layer), "4  Imported phrase with detected notes (Note Atlas source)",
                      ["(3) OspKeyboard tints: strong = recorded key (badge = variations), pale = filled within FILL range, hatched = silent.",
                       "The waveform is the source as today; regions are a paint pass over the cached display (SourceDisplay).",
                       "Editing (markers, split/merge, pitch, PLAYS ON) lives in the NOTE MAP page, an EQ-style in-display overlay."])
    out.convert("RGB").save(OUT / "04-phrase-note-map.jpg", quality=86, optimize=True)


def mockup_4b():
    img = Image.open(SHOTS / "gui-ceramic-1-layer@1x.png").convert("RGBA")
    layer, d = overlay(img)
    display = (51, 190, 1398, 508)
    d.rounded_rectangle(display, 8, fill=(22, 27, 29, 245))
    dashed_rect(d, display)
    d.text((70, 202), "EQ   DAMPEN   NOTES", fill=(220, 215, 205, 255), font=font(14, True))
    d.line((205, 222, 262, 222), fill=(224, 122, 80, 255), width=3)
    plot = (51, 230, 1010, 500)
    regions(d, plot, PHRASE, (224, 150, 100))
    # markers on the selected note
    sel_x0 = 51 + 20 + (959 - 40) * (3 + 2) / 27
    for i, t in enumerate((0.05, 0.25, 0.8, 0.98)):
        x = sel_x0 + t * (959 - 40) * 4 / 27
        d.line((x, 250, x, 480), fill=(250, 230, 200, 255), width=2)
    panel = (1025, 238, 1385, 495)
    d.rounded_rectangle(panel, 8, outline=(90, 100, 104, 255), width=1)
    rows = ["A4  +3 c   conf 0.94", "PITCH  -  +     OCT  -  +", "PLAYS ON  A4   AUTO", "INCLUDED   AUDITION", "SPLIT   MERGE >   RE-ANALYSE", "FILL  BALANCED (5 st)", "VARIATIONS  sequential"]
    for i, r in enumerate(rows):
        d.text((1040, 255 + 33 * i), r, fill=(230, 225, 215, 255), font=font(13, True if i == 0 else False))
    counts, filled, unplayable = mapped_keys()
    key_tints(d, counts, filled, unplayable, (224, 122, 80))
    callout(d, (240, 172), 1, "third page beside EQ | DAMPEN (EqEditor pattern)")
    callout(d, (1205, 510), 2, "note inspector: the Phrase NotePanel verbs, in OSP controls")
    out = with_legend(Image.alpha_composite(img, layer), "4b  NOTE MAP page open (hero card; dual/triple use the same page, zoomed)",
                      ["Space: the display rectangle exactly (1347x319 hero, 651x319 dual, 419x258 triple) - no window growth.",
                       "Triple density is tight (367x190 of graph): editing there zooms the selected note; the keyboard map stays.",
                       "Undo per edit (one UndoableAction per committed change), as Phrase does."])
    out.convert("RGB").save(OUT / "04b-note-map-page.jpg", quality=86, optimize=True)


def mockup_5():
    img = Image.open(SHOTS / "gui-ceramic-1-layer@1x.png").convert("RGBA")
    layer, d = overlay(img)
    display = (51, 190, 1398, 508)
    d.rectangle((52, 191, 1397, 507), fill=(26, 31, 33, 200))
    regions(d, display, PHRASE, (224, 150, 100), grains=True)
    granular_strip(d, display, extra=True)
    counts, filled, unplayable = mapped_keys()
    key_tints(d, counts, filled, unplayable, (224, 122, 80))
    callout(d, (1300, 120), 1, "mode menu: Granular (grains stay inside each note's region)", anchor_right=True)
    callout(d, (1340, 415), 2, "", anchor_right=True)
    out = with_legend(Image.alpha_composite(img, layer), "5  Phrase with granular playback",
                      ["Each key's grains come from its own note's sustain region (an atlas member is the voice's source buffer).",
                       "(2) KEY DNA key: per-key identity on top; POS/SIZE/DENS stay the base values for every note."])
    out.convert("RGB").save(OUT / "05-phrase-granular.jpg", quality=86, optimize=True)


def mockup_6():
    img = Image.open(SHOTS / "gui-ceramic@1x.png").convert("RGBA")
    layer, d = overlay(img)
    # triple: cards at x 35, 498, 961 (width 451), displays at +16,+63 (419x258)
    a = (51, 190, 470, 448)
    c = (977, 190, 1396, 448)
    d.rectangle((52, 191, 469, 447), fill=(26, 31, 33, 210))
    regions(d, a, PHRASE[:6], (224, 150, 100), labels=False)
    dashed_rect(d, (190, 141, 246, 177))
    d.text((218, 159), "NOTES", fill=NOTE + (255,), font=font(11, True), anchor="mm")
    for i in range(40):
        import random
        r = random.Random(i)
        gx, gy = r.uniform(1050, 1350), r.uniform(240, 420)
        d.ellipse((gx - 3, gy - 3, gx + 3, gy + 3), fill=(190, 230, 200, 230))
    granular_strip(d, c, extra=True)
    counts, filled, unplayable = mapped_keys()
    key_tints(d, counts, filled, unplayable, (224, 122, 80))
    callout(d, (260, 120), 1, "A: Note Atlas (phrase)")
    callout(d, (723, 120), 2, "B: one-shot, untouched")
    callout(d, (1186, 120), 3, "C: granular + KEY DNA", anchor_right=True)
    callout(d, (110, 975), 4, "")
    out = with_legend(Image.alpha_composite(img, layer), "6  A/B/C holding different source types at once",
                      ["One note-on still starts one voice per occupied layer; each layer resolves its own region (A), plain read (B), DNA cloud (C).",
                       "(The screenshot's C menu shows the default 'One Shot'; with grains it would read 'Granular'.)",
                       "(4) The keyboard shows the EDITED layer's map only (the layer whose card was last clicked), so three maps never overlap."])
    out.convert("RGB").save(OUT / "06-abc-mixed.jpg", quality=86, optimize=True)


for f in (mockup_1, mockup_2, mockup_3, mockup_4, mockup_4b, mockup_5, mockup_6):
    f()
print("ok")
