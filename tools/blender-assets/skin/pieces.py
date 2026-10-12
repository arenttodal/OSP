"""The ceramic skin's pieces: what is rendered, where, and how the plugin stretches it.

Pure data (no bpy), shared by render.py (Blender) and pack.py (Python 3 + Pillow).

Every piece is rendered at its real place in the GUI's design space (1448 x 1086 px), so
the studio light falls on it exactly as it falls in the approved full-GUI render
(design-lab direction 09-warm-ceramic-teak). The plugin draws a piece as a nine-slice:
the corners keep their size, the edges stretch along their length, the middle both ways.
Drawn at the size it was rendered at, a piece is the render itself.

Kinds:
- "opaque": the whole scene (the window's background).
- "slab": one raised part with its contact shadow, alpha over what it stands on (the
  surface below is a shadow catcher of that surface's material, so it still bounces light).
- "pocket": a recess cut into a surface, rendered with and without the recess; pack.py
  turns the difference into an overlay that darkens and lights whatever surface it is
  drawn on.

Units: rects in design px (x, y, w, h); heights and bevels in mm; `margin` (design px) is
the room around the part for its shadow; `insets` (design px from the part's edges) are
the slice lines.
"""

DIRECTION = "09-warm-ceramic-teak"
FULL = (0, 0, 1448, 1086)
HOUSING = (25, 19, 1398, 1053)
HOUSING_TOP = 6.0          # mm: the housing slab's height (controls.json geometry.housing)
CARD_TOP = HOUSING_TOP + 0.9
PANEL_TOP = HOUSING_TOP + 0.6

SAMPLES_LARGE = 48
SAMPLES_SMALL = 64

# A1's emphasis by role (controls.json "emphasis"): the key's height, so the hierarchy reads
# in depth.
EMPHASIS = {"modifier": 1.0, "mode": 0.8, "eq": 0.8, "layerState": 0.6, "advanced": 0.8, "arp": 0.9, "preset": 0.7, "generic": 0.8}
KEY_STATES = ("normal", "hover", "pressed", "active")

MACROS = {"life": 62, "drive": 208, "character": 354, "motion": 500, "echo": 646, "space": 792}   # slider x (y 752, 132 px)
MACRO_COLOURS = {"life": "#be7552", "drive": "#b5843a", "character": "#7b8d69", "motion": "#73899c", "echo": "#5c9490", "space": "#96839d"}
BADGES = {"A": "#c4561f", "B": "#4d6176", "C": "#64806b"}


def _slab(name, rect, radius, height, bevel, material, base, margin, scale, insets=None, samples=SAMPLES_SMALL, **extra):
    inset = insets if insets is not None else radius + 4
    return dict(name=name, kind="slab", rect=rect, radius=radius, height=height, bevel=bevel, material=material,
                base=base, margin=margin, scale=scale, insets=[inset] * 4 if isinstance(inset, (int, float)) else list(inset),
                samples=samples, **extra)


def pieces():
    out = []

    # The window: surround, housing (7 px corners against the teak) and the teak cheeks.
    out.append(dict(name="housing", kind="opaque", rect=FULL, margin=0, scale=2, insets=[60, 60, 60, 60], samples=SAMPLES_LARGE))

    # Cards: one per layout width (three, two or one layer), satin ceramic.
    for w in (451, 683, 1379):
        out.append(_slab(f"card-{w}", (35, 127, w, 525), 16, 0.9, 0.55, "card", "housing", 16, 2, insets=40, samples=SAMPLES_LARGE))

    # The macro panel (D3) and the arpeggiator's inline panel (the same satin).
    out.append(_slab("panel-macro", (35, 682, 1378, 216), 13, 0.6, 0.5, "panel", "housing", 16, 2, insets=40, samples=SAMPLES_LARGE))
    out.append(_slab("panel-arp", (35, 908, 1378, 177), 13, 0.6, 0.5, "panel", "housing", 16, 2, insets=40, samples=SAMPLES_LARGE))

    # Smaller satin panels at the radii the GUI uses (library, popups, the ARP card's own
    # drawing when it is not a key).
    for r in (7, 9, 10, 12):
        out.append(_slab(f"raised-r{r}", (620, 300, 128, 84), r, 0.6, 0.5, "panel", "housing", 12, 4))

    # A1 keys at their real places and sizes, in every state.
    keys = [
        ("modifier", (51, 586, 98, 45), 7, "card"),
        ("layerState", (112, 137, 27, 21), 4, "card"),
        ("mode", (296, 139, 128, 41), 7, "card"),
        ("eq", (250, 143, 38, 33), 6, "card"),
        ("advanced", (1308, 983, 105, 31), 7, "housing"),
        ("preset", (501, 45, 446, 48), 12, "housing"),
        ("arp", (1308, 913, 105, 64), 10, "housing"),
    ]
    for r in (4, 5, 7, 9):
        keys.append((f"generic-r{r}", (700, 400, 84, 36), r, "card"))
    for role, rect, radius, base in keys:
        e = EMPHASIS.get(role, EMPHASIS["generic"])
        for state in KEY_STATES:
            pressed = state in ("pressed", "active")
            h = (0.42 if pressed else 0.8) * e
            out.append(_slab(f"key-{role}-{state}", rect, radius, h, min(0.55, 0.9 * h), "key", base, 10, 4,
                             insets=min(radius + 4, int(0.5 * min(rect[2], rect[3])) - 1), role=role, state=state))

    # The layer badges (A orange, B blue, C green), on the card at the badge's place.
    for letter, colour in BADGES.items():
        out.append(_slab(f"badge-{letter}", (56, 137, 47, 43), 7, 0.7, 0.4, {"badge": colour}, "card", 10, 4, insets=11))

    # C1 ivory fader cap (three grip grooves), at 75 % of the travel; the T3 metal rail.
    for state in ("normal", "hover", "drag"):
        out.append(dict(name=f"fader-{state}", kind="fader", rect=(1292 - 7, 80 - 11, 14, 22), state=state, base="housing",
                        margin=8, scale=4, insets=[7, 11, 7, 11], samples=SAMPLES_SMALL))
    out.append(_slab("rail", (1157 - 1.6, 80 - 1.6, 180 + 3.2, 3.2), 1.6, 0.6, 0.25, "rail", "housing", 6, 4, insets=[8, 1.6, 8, 1.6]))

    # Lens LEDs: the six macros (lit and unlit) on the panel, the ARP light on its key.
    for name, x in MACROS.items():
        for lit in (False, True):
            out.append(dict(name=f"led-{name}-{'lit' if lit else 'unlit'}", kind="lens", centre=(x + 66, 883), diameter=9, height=0.75,
                            rect=(x + 66 - 4.5, 883 - 4.5, 9, 9), insets=[4.5] * 4,
                            colour=MACRO_COLOURS[name], lit=lit, base="panel", margin=6, scale=4, samples=SAMPLES_SMALL))
    for lit in (False, True):
        out.append(dict(name=f"led-arp-{'on' if lit else 'off'}", kind="lens", centre=(1325, 931), diameter=11, height=0.7,
                        rect=(1325 - 5.5, 931 - 5.5, 11, 11), insets=[5.5] * 4,
                        colour="#e8692a" if lit else "#5a5752", lit=lit, graphite=not lit, base="key-arp", margin=6, scale=4,
                        samples=SAMPLES_SMALL))

    # Pockets: the displays' bezels (a 1.6 px lip around each dark well) and the keybed.
    bezels = [
        ("bezel-r9", (51, 190, 419, 258), 9, "card", (35, 127, 451, 525)),
        ("bezel-r12", (967, 714, 426, 90), 12, "panel", (35, 682, 1378, 216)),
        ("bezel-r8", (600, 300, 160, 90), 8, "card", (560, 260, 240, 170)),
        ("bezel-r10", (600, 300, 160, 90), 10, "card", (560, 260, 240, 170)),
    ]
    for name, well, radius, parent, parent_rect in bezels:
        grow = 1.6
        rect = (well[0] - grow, well[1] - grow, well[2] + 2 * grow, well[3] + 2 * grow)
        out.append(dict(name=name, kind="pocket", rect=rect, radius=radius + grow, depth=0.6, lip=0.32, floor="screenFloor",
                        parent=parent, parentRect=parent_rect, grow=grow, margin=6, scale=4 if well[2] < 300 else 2,
                        insets=[radius + grow + 6] * 4, samples=SAMPLES_SMALL))
    out.append(dict(name="keybed", kind="pocket", rect=(146, 910, 1154, 106), radius=6, depth=1.2, lip=0.4, floor="keybed",
                    parent="housing", parentRect=HOUSING, grow=2, margin=8, scale=2, insets=[20, 20, 20, 20], samples=SAMPLES_SMALL))
    return out
