"""The lab's scenes: the whole ANDOR/OSP chassis as real geometry, in the GUI's own space.

One builder makes every render. It reads the production layout from the layer export
(references/current-gui/export2x/layers.json: every component's bounds), the component
families from configs/controls.json and a direction (configs/directions.json) or a family
study, and builds the housing, cards, display bezels, macro panel, buttons, tabs, fader,
keybed and details as slabs with pockets (lab/forms.py). The plugin's own drawing (text,
icons, waveforms, knobs, halos, keyboard) is never rendered here: the compositor
(scripts/composite_gui.py) lays it over these surfaces.

    python generate.py scene direction:06-hybrid-premium --quality final
    python generate.py scene family:buttons --quality final
"""

import copy
import json
import os

from lab import forms, geometry, studio
from lab.materials import material
from studies import CROPS, studies

HERE = os.path.dirname(os.path.abspath(__file__))
LAB = os.path.dirname(HERE)
EXPORT = os.path.join(LAB, "references", "current-gui", "export2x", "layers.json")
FULL = (0, 0, 1448, 1086)
HOUSING = (25, 19, 1398, 1053)
MACRO_PANEL = (35, 682, 1378, 216)


def load(name):
    with open(os.path.join(HERE, "configs", name), "r", encoding="utf-8") as f:
        return json.load(f)


CTRL = load("controls.json")
DIRECTIONS = load("directions.json")["directions"]


def gui():
    """The production layout, from the layer export (design px)."""
    with open(EXPORT, "r", encoding="utf-8") as f:
        layers = json.load(f)["layers"]
    g = {"cards": [], "screens": [], "modifiers": [], "modes": [], "eqs": [], "layerStates": [], "macros": [], "dials": [], "minis": []}
    card_of = {}
    for l in layers:
        cls = l["class"].split("::")[-1]
        r = (l["x"], l["y"], l["w"], l["h"])
        if cls == "EngineCard":
            card_of[l["index"]] = len(g["cards"])
            g["cards"].append(r)
        elif cls == "SourceDisplay":
            g["screens"].append((card_of[l["parent"]], r))
        elif cls == "ModifierButton":
            g["modifiers"].append((card_of[l["parent"]], r, l["index"]))
        elif cls == "ModeSelector":
            g["modes"].append((card_of[l["parent"]], r))
        elif cls == "EqButton":
            g["eqs"].append((card_of[l["parent"]], r))
        elif cls == "LayerStateButton":
            g["layerStates"].append((card_of[l["parent"]], r))
        elif cls == "Slider" and l["w"] == 132:
            g["macros"].append((l.get("paramId", ""), r, l.get("sliderPos", 0.0)))
        elif cls == "Slider" and l["w"] < 60:
            g["minis"].append((r, l.get("sliderPos", 0.0)))
        elif cls == "Dial":
            g["dials"].append((r, l.get("sliderPos", 0.0)))
        elif cls == "AdvancedCardButton":
            g["advanced"] = r
        elif cls == "ArpControl":
            g["arp"] = r
        elif cls == "PresetBar":
            g["preset"] = r
    return g


# Modifier buttons on in the exported state (the reference screenshot): loop and REIMAGINED
# on A, REIMAGINED on B, loop and REIMAGINED on C (layer indices in the export).
ACTIVE_MODIFIERS = {24, 25, 46, 66, 67}
BADGES = ("#c4561f", "#4d6176", "#64806b")


class Bag:
    """Materials of one object, by role: index 0 is the body."""

    def __init__(self, body):
        self.mats = [body]
        self.keys = {}

    def index(self, key, make):
        if key not in self.keys:
            self.keys[key] = len(self.mats)
            self.mats.append(make())
        return self.keys[key]


class Surface:
    """A slab waiting for its pockets (they are known only once its children are planned)."""

    def __init__(self, name, rect, radius, z0, height, bevel, body, chamfer=False):
        self.name, self.rect, self.radius = name, rect, radius
        self.z0, self.height, self.bevel, self.chamfer = z0, height, bevel, chamfer
        self.bag = Bag(body)
        self.pockets = []

    @property
    def top(self):
        return self.z0 + self.height

    def pocket(self, rect, radius, depth, lip, floor=None, floor_key=None):
        p = {"rect": rect, "radius": radius, "depth": depth, "lip": lip}
        if floor is not None:
            p["floor"] = self.bag.index(floor_key or id(floor), lambda: material(floor))
        self.pockets.append(p)

    def build(self):
        return forms.slab(self.name, self.rect, self.radius, self.z0, self.height, self.bevel, self.bag.mats[0],
                          chamfer=self.chamfer, pockets=self.pockets, extra_materials=self.bag.mats[1:])


def state_tint(ref, tint):
    ref = {"preset": ref} if isinstance(ref, str) else dict(ref)
    if tint:
        ref["tint"] = tint
    return ref


HOVER_TINT = ["#ffffff", 0.16]


class Builder:
    def __init__(self, spec, state=None):
        self.spec = spec
        self.state = state or {}
        self.g = gui()
        self.geo = CTRL["geometry"]
        self.roles = CTRL["roles"]
        self.later = []   # objects built after the surfaces (they sit on them)

    # ------------------------------------------------------------------ helpers
    def family(self, kind, key):
        fam = copy.deepcopy(CTRL[kind][key])
        if kind == "buttons":
            fam.update(self.spec.get("buttonOverrides", {}))
        return fam

    def button(self, parent, rect, radius, role, state, window=None):
        """Plan one button of the direction's family on `parent` (a Surface)."""
        key = self.spec.get("buttons", "A4")
        if key == "original":
            return
        fam = self.family("buttons", key)
        e = CTRL["emphasis"].get(role, 1.0)
        pressed = state in ("pressed", "active")
        tint = fam.get("activeTint") if state == "active" else (HOVER_TINT if state == "hover" else None)
        body = state_tint(fam.get("roleMaterial", {}).get(role, fam["material"]), tint)
        if fam["kind"] == "bank":
            fam = dict(fam, kind="raised", **fam["single"])
        if fam["kind"] == "inset":
            depth = fam["depth"] * (0.7 + 0.3 * e)
            glow = state == "active" and fam.get("activeGlow")
            groove = state_tint(fam["groove"], None)
            if glow:
                groove["emission"] = fam["activeGlow"]
            parent.pocket(forms.expand(rect, fam["gap"]), radius + fam["gap"], depth, fam["lip"], floor=groove,
                          floor_key="groove-glow" if glow else "groove")
            drop = fam["pressedDrop"] if pressed else fam["faceDrop"]
            z0 = parent.top - depth
            pockets, extra = self.window(window, fam)
            self.later.append(lambda: forms.slab(f"btn-{role}", rect, radius, z0, depth - drop, fam["faceBevel"], material(body),
                                                 pockets=pockets, extra_materials=extra))
        else:
            h = (fam["pressedHeight"] if pressed else fam["height"]) * e
            z0 = parent.top
            pockets, extra = self.window(window, fam)
            self.later.append(lambda: forms.slab(f"btn-{role}", rect, radius, z0, h, min(fam["bevel"], 0.9 * h), material(body),
                                                 chamfer=fam.get("chamfer", False), pockets=pockets, extra_materials=extra))
            if state == "active" and fam.get("activeRing"):
                ring = {"preset": "M1-warm-matte-ivory", "baseColor": "#f6b38a", "emission": fam["activeRing"]}
                w = fam.get("ringWidth", 1.0)
                self.later.append(lambda: forms.ring(f"btn-{role}-ring", forms.expand(rect, w + 0.6), radius + w + 0.6, z0, 0.05, w, material(ring)))
        return fam

    def bank(self, parent, keys, radius, role):
        """A row of keys in one black recessed bank (A5): the pocket spans the group, each key
        rises from the bank's floor and stands `proud` above the panel."""
        if not keys:
            return
        fam = self.family("buttons", self.spec["buttons"])
        x0 = min(r[0] for r, _ in keys)
        y0 = min(r[1] for r, _ in keys)
        x1 = max(r[0] + r[2] for r, _ in keys)
        y1 = max(r[1] + r[3] for r, _ in keys)
        g = fam["bankGrow"]
        e = CTRL["emphasis"].get(role, 1.0)
        depth = fam["depth"]
        parent.pocket((x0 - g, y0 - g, x1 - x0 + 2 * g, y1 - y0 + 2 * g), fam["bankRadius"], depth, 0.05,
                      floor=fam["bankFloor"], floor_key="bank")
        z0 = parent.top - depth
        for r, st in keys:
            pressed = st in ("pressed", "active")
            tint = fam.get("activeTint") if st == "active" else (HOVER_TINT if st == "hover" else None)
            body = state_tint(fam["material"], tint)
            h = depth + (fam["pressedProud"] if pressed else fam["proud"]) * e
            self.later.append(lambda r=r, body=body, h=h: forms.slab(f"key-{role}", r, radius, z0, h, fam["bevel"], material(body)))

    def plan_knobs(self, cards, deck):
        """Restyled knob caps (K2): cylinders with a dark line at the knob's real angle, under the
        production rings and halos (the compositor removes the production cap only)."""
        key = self.spec.get("knobs")
        if key not in CTRL.get("knobs", {}):
            return
        fam = CTRL["knobs"][key]
        sizes = fam["sizes"]
        jobs = []
        for r, pos in self.g["dials"]:
            card = next(c for c in cards if c.rect[0] <= r[0] < c.rect[0] + c.rect[2])
            jobs.append(((r[0] + 0.5 * r[2], r[1] + 0.5 * r[3]), pos, sizes["layer"], card.top))
        for r, pos in self.g["minis"]:
            jobs.append(((r[0] + 0.5 * r[2], r[1] + 0.5 * r[3]), pos, sizes["mini"], deck.top))
        if self.spec.get("knobScope", "all") == "all":
            for _, r, pos in self.g["macros"]:
                jobs.append(((r[0] + 0.5 * r[2], r[1] + 0.5 * r[3]), pos, sizes["macro"], deck.top))
        for centre, pos, size, z0 in jobs:
            self.later.append(lambda centre=centre, pos=pos, size=size, z0=z0: self.knob_cap(fam, centre, pos, size, z0))

    @staticmethod
    def knob_cap(fam, centre, pos, size, z0):
        import math
        from mathutils import Matrix
        cx, cy = centre
        r = size["radius"]
        forms.slab("knob", (cx - r, cy - r, 2 * r, 2 * r), r, z0, size["height"], size["bevel"], material(fam["material"]), segments=6)
        # The line, drawn pointing up, then turned to the knob's angle (225 deg + 270 deg x pos,
        # clockwise from 12 o'clock, as OspLookAndFeel).
        w = max(1.6, 0.075 * r)
        line = forms.slab("knob-line", (cx - 0.5 * w, cy - 0.88 * r, w, 0.72 * r), 0.5 * w, z0 + size["height"], 0.02, 0.0, material(fam["line"]))
        angle = math.radians(225.0 + 270.0 * pos)
        c = (cx * geometry.PX, -cy * geometry.PX, 0.0)
        line.matrix_world = Matrix.Translation(c) @ Matrix.Rotation(-angle, 4, "Z") @ Matrix.Translation((-c[0], -c[1], 0.0))

    def window(self, window, fam):
        """A shallow display window in a button's face (the ARP card's pattern and rate)."""
        if window is None:
            return [], ()
        floor = state_tint(fam["material"], ["#d6ccbe", 0.45])
        return [{"rect": window[0], "radius": window[1], "depth": 0.12, "lip": 0.05, "floor": 1}], (material(floor),)

    # ------------------------------------------------------------------ scene
    def build(self):
        g, geo, spec = self.g, self.geo, self.spec
        enc = CTRL["enclosures"][spec["enclosure"]]
        panel = CTRL["panels"][spec["panel"]]
        details = spec.get("details", {})
        mstate = self.state.get("modifier", "auto")

        geometry.plane("backdrop", (-200, -200, 1848, 1486), material(self.roles["backdrop"]))
        hg = geo["housing"]
        trim = CTRL["trim"].get(spec.get("trim", ""))
        housing = Surface("housing", HOUSING, trim["housingRadius"] if trim else 26, 0.0, hg["height"], hg["bevel"], material(enc["housing"]))
        if trim:
            for side in ("left", "right"):
                rect = tuple(trim[side])
                self.later.append(lambda rect=rect: forms.slab("trim", rect, trim["radius"], 0.0, trim["height"], trim["bevel"],
                                                               material(self.roles[trim["material"]])))
        kb = geo["keybed"]
        housing.pocket(tuple(kb["rect"]), kb["radius"], kb["depth"], kb["lip"], floor=enc.get("keybed", self.roles["keybed"]), floor_key="keybed")

        # Cards with their display bezels.
        cg, bz = geo["card"], geo["bezel"]
        card_mat = spec.get("cardMaterial", enc["card"])
        cards = [Surface(f"card-{i}", r, 16, housing.top, cg["height"], cg["bevel"], material(card_mat)) for i, r in enumerate(g["cards"])]
        for ci, r in g["screens"]:
            cards[ci].pocket(forms.expand(r, bz["grow"]), 9 + bz["grow"], bz["depth"], bz["lip"], floor=self.roles["screenFloor"], floor_key="screen")

        # The layer badges (A orange, B blue, C green: the identity colours' mid tones); the
        # letter is the plugin's own drawing, laid on by the compositor.
        for i, r in enumerate(g["cards"]):
            badge = {"preset": "M2-satin-ceramic-ivory", "baseColor": BADGES[i % 3], "roughness": 0.4, "coat": 0.12}
            rect = (r[0] + 21, r[1] + 11, 47, 42)
            top = cards[i].top
            self.later.append(lambda rect=rect, badge=badge, top=top: forms.slab("badge", rect, 7, top, 0.7, 0.4, material(badge)))

        # The macro panel (and, for D6, the recess it sits in).
        surfaces = [housing] + cards
        if panel["kind"] == "recessed":
            housing.pocket(MACRO_PANEL, 13, panel["depth"], panel["lip"])
            deck = Surface("macro-floor", MACRO_PANEL, 13, housing.top - panel["depth"] - 0.3, 0.3, 0.0, material(panel["material"]))
        else:
            deck = Surface("macro-panel", MACRO_PANEL, 13, housing.top, panel["height"], panel["bevel"], material(panel["material"]),
                           chamfer=panel.get("chamfer", False))
            if panel["kind"] == "graphiteInset":
                deck.pocket(tuple(panel["inset"]), panel["insetRadius"], panel["depth"], panel["lip"], floor=panel["floor"], floor_key="inset")
        surfaces.append(deck)
        ew = geo["envelopeWell"]
        deck.pocket(forms.expand(tuple(ew["rect"]), bz["grow"]), ew["radius"] + bz["grow"], bz["depth"], bz["lip"], floor=self.roles["screenFloor"], floor_key="screen")
        if details.get("dividerGroove"):
            dv = geo["divider"]
            deck.pocket(tuple(dv["rect"]), 0.75, dv["depth"], dv["lip"], floor=state_tint(panel["material"], ["#8f8578", 0.35]), floor_key="divider")
        if details.get("seams"):
            seam = {"preset": "M4-warm-brushed-aluminum", "baseColor": "#bdb5a7"}
            self.later.append(lambda: forms.ring("seam", forms.expand(MACRO_PANEL, 3.0), 16.0, housing.top, 0.04, 1.0, material(seam)))

        # Buttons (planned now: an inset family cuts pockets into its parent).
        sec = self.state.get("secondary", "normal")
        mods = [(ci, r, mstate if mstate != "auto" else ("active" if index in ACTIVE_MODIFIERS else "normal")) for ci, r, index in g["modifiers"]]
        if self.spec.get("buttons") in CTRL["buttons"] and CTRL["buttons"][self.spec["buttons"]]["kind"] == "bank":
            # Grouped keys share one recessed bank per card: the modifiers, and M/S.
            for ci in range(len(cards)):
                self.bank(cards[ci], [(r, st) for c, r, st in mods if c == ci], 7, "modifier")
                self.bank(cards[ci], [(r, sec) for c, r in g["layerStates"] if c == ci], 4, "layerState")
        else:
            for ci, r, st in mods:
                self.button(cards[ci], r, 7, "modifier", st)
            for ci, r in g["layerStates"]:
                self.button(cards[ci], r, 4, "layerState", sec)
        for ci, r in g["modes"]:
            self.button(cards[ci], r, 7, "mode", sec)
        for ci, r in g["eqs"]:
            self.button(cards[ci], r, 6, "eq", sec)
        self.button(housing, g["advanced"], 7, "advanced", sec)
        self.button(housing, g["preset"], 12, "preset", sec)
        self.button(housing, g["arp"], 10, "arp", sec, window=(tuple(self.geo["arpPill"]), 9))

        self.plan_tabs(deck)
        self.plan_fader(housing)
        self.plan_knobs(cards, deck)
        if details.get("leds"):
            self.plan_leds(deck, panel)

        for s in surfaces:
            s.build()
        for make in self.later:
            make()

    def plan_tabs(self, deck):
        key = self.spec.get("tabs", "B1")
        fam = self.family("tabs", key)
        if fam["kind"] == "flat":
            return
        sel = self.state.get("tab", 0)
        hover = self.state.get("tabHover")
        strip = tuple(self.geo["tabStrip"]["rect"])
        tabs = [tuple(t) for t in self.geo["tabs"]]
        top = deck.top
        if fam["kind"] == "inset":
            deck.pocket(strip, 6, fam["depth"], fam["lip"], floor=fam["floor"], floor_key="tabfloor")
            for i, t in enumerate(tabs):
                if i == sel or i == hover:
                    body = state_tint(fam["pad"], HOVER_TINT if i == hover and i != sel else None)
                    h = fam["padHeight"] * (1.0 if i == sel else 0.6)
                    self.later.append(lambda t=t, body=body, h=h: forms.slab("tab-pad", forms.expand(t, -1.5), 5, top - fam["depth"], h, 0.12, material(body)))
        elif fam["kind"] == "strip":
            self.later.append(lambda: forms.slab("tab-strip", strip, 6, top, fam["height"], fam["bevel"], material(fam["material"])))
            t = tabs[sel]
            ind = {"preset": "M1-warm-matte-ivory", "baseColor": "#f6b38a", "emission": fam["indicator"]}
            self.later.append(lambda: forms.slab("tab-indicator", (t[0] + 12, t[1] + 19.2, t[2] - 24, 1.6), 0.8, top + fam["height"], 0.03, 0.0, material(ind)))
            if hover is not None and hover != sel:
                th = tabs[hover]
                self.later.append(lambda: forms.slab("tab-hover", forms.expand(th, -2), 5, top + fam["height"], 0.03, 0.0,
                                                     material(state_tint(fam["material"], HOVER_TINT))))
        elif fam["kind"] == "keys":
            for i, t in enumerate(tabs):
                on = i == sel
                tint = fam["activeTint"] if on else (HOVER_TINT if i == hover else None)
                h = fam["pressedHeight"] if on else fam["height"]
                body = state_tint(fam["material"], tint)
                self.later.append(lambda t=t, h=h, body=body: forms.slab("tab-key", forms.expand(t, -1.0), 5, top, h, min(fam["bevel"], 0.9 * h), material(body), chamfer=True))

    def plan_fader(self, housing):
        cap, track = self.spec.get("fader", "original"), self.spec.get("track", "original")
        v = self.geo["volume"]
        left, right, cy = v["left"], v["right"], v["trackY"]
        if track in CTRL["tracks"]:
            t = CTRL["tracks"][track]
            w = t["w"]
            rect = (left - 0.5 * w, cy - 0.5 * w, right - left + w, w)
            if "depth" in t:
                housing.pocket(rect, 0.5 * w, t["depth"], t["lip"], floor=self.roles["slot"], floor_key="slot")
            else:
                self.later.append(lambda rect=rect: forms.slab("rail", rect, 0.5 * w, housing.top, t["height"], t["bevel"], material(t["material"])))
        if cap not in CTRL["faders"]:
            return
        c = CTRL["faders"][cap]
        pos = self.state.get("faderPos", 0.75)
        fstate = self.state.get("fader", "normal")
        cx = left + pos * (right - left)
        rect = (cx - 0.5 * c["w"], cy - 0.5 * c["h"], c["w"], c["h"])
        body = state_tint(c["material"], HOVER_TINT if fstate in ("hover", "drag") else None)
        z0 = housing.top
        lift = 0.25 if fstate == "drag" else 0.0
        top = z0 + lift + c["height"]
        pockets = []
        extra = []
        if c.get("grip"):
            for k in range(c["grip"]):
                x = cx + (k - 0.5 * (c["grip"] - 1)) * 3.4
                pockets.append({"rect": (x - 0.55, cy - 0.32 * c["h"], 1.1, 0.64 * c["h"]), "radius": 0.55, "depth": 0.16, "lip": 0.05})
        if c.get("line") == "engraved":
            extra = [material(self.roles["slot"])]
            pockets.append({"rect": (cx - 0.6, cy - 0.36 * c["h"], 1.2, 0.72 * c["h"]), "radius": 0.6, "depth": 0.14, "lip": 0.04, "floor": 1})
        self.later.append(lambda: forms.slab("fader-cap", rect, c["radius"], z0 + lift, c["height"], c["bevel"], material(body),
                                             chamfer=c.get("chamfer", False), pockets=pockets, extra_materials=tuple(extra)))
        if c.get("line") == "inlay":
            ivory = c.get("lineMaterial", "knob-satin-ivory")
            if fstate == "drag":
                ivory = {"preset": "knob-satin-ivory", "emission": ["#ff9a52", 0.6]}
            self.later.append(lambda: forms.slab("fader-line", (cx - 0.6, cy - 0.36 * c["h"], 1.2, 0.72 * c["h"]), 0.6, top, 0.03, 0.0, material(ivory)))
        if fstate == "drag" and c.get("line") == "engraved":
            glow = {"preset": "M1-warm-matte-ivory", "baseColor": "#f6b38a", "emission": ["#ff8a3c", 0.9]}
            self.later.append(lambda: forms.slab("fader-glow", (cx - 0.6, cy - 0.36 * c["h"], 1.2, 0.72 * c["h"]), 0.6, top - 0.13, 0.02, 0.0, material(glow)))

    def plan_leds(self, deck, panel):
        """Lens LEDs under the macros (the macro's colour; lit while it is modulated) and the
        ARP light."""
        y, d = self.geo["macroLedY"], self.geo["macroLedDiameter"]
        colours = {"life": "#be7552", "drive": "#b5843a", "character": "#7b8d69", "motion": "#73899c", "echo": "#5c9490", "space": "#96839d"}
        z0 = deck.top - (panel["depth"] if panel["kind"] == "graphiteInset" else 0.0)
        for pid, r, _ in self.g["macros"]:
            lit = pid in self.geo["litMacros"]
            col = colours.get(pid, "#999999")
            lens = {"preset": "M2-satin-ceramic-ivory", "baseColor": col, "roughness": 0.22, "coat": 0.6,
                    "emission": [col, 1.4 if lit else 0.12]}
            if not lit:
                lens["tint"] = ["#ece5d8", 0.3]
            cx = r[0] + 0.5 * r[2]
            self.later.append(lambda cx=cx, lens=lens: forms.dome("led", (cx, y), d, z0, 0.75, material(lens)))
        ax, ay, ad = self.geo["arpLed"]
        off = {"preset": "M5-graphite-powder-coat", "baseColor": "#5a5752", "roughness": 0.3, "coat": 0.5}
        fam = self.spec.get("buttons", "A4")
        e = CTRL["emphasis"]["arp"]
        b = CTRL["buttons"].get(fam, {})
        top = 6.0 + (b.get("height", 0.0) * e if b.get("kind") == "raised" else -b.get("faceDrop", 0.0))
        self.later.append(lambda: forms.dome("arp-led", (ax, ay), ad, top, 0.7, material(off)))


def build(spec, state=None):
    Builder(spec, state).build()


def render(spec, rect, scale, samples, exposure, path, state=None):
    scene = studio.reset()
    studio.rig(scene)
    build(spec, state)
    studio.camera(scene, rect, scale)
    studio.settings(scene, samples=samples, exposure=exposure)
    studio.render(scene, path)


def run(name, quality, exposure):
    kind, _, what = name.partition(":")
    if kind == "direction":
        spec = DIRECTIONS[what]
        render(spec, FULL, quality["scale"], quality["samples"], exposure,
               os.path.join(LAB, "renders", "chassis", f"{what}@{int(quality['scale'])}x.png"))
    elif kind == "family":
        for st in studies():
            if what not in ("all", st["out"].split("/")[0], st["out"]) or st["chassis"] != st["out"]:
                continue
            path = os.path.join(LAB, "renders", st["out"] + ".png")
            if os.path.exists(path) and os.environ.get("LAB_SKIP_EXISTING"):
                continue
            render(st["spec"], CROPS[st["crop"]], st["scale"], quality["samples"], exposure, path, st["state"])
            print("rendered", st["out"], flush=True)
    else:
        raise SystemExit(f"unknown scene: {name}")
