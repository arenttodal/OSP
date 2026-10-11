"""Lay the production GUI's own drawing over a rendered chassis.

The plugin exported every component's own paint as a transparent layer (tests/plugin/
DesignLabExport.inc -> references/current-gui/export<scale>x). The chassis (Blender, same
layout, same space) replaces the *surfaces*; everything the plugin draws on them is kept:

* content layers (displays and waveforms, knobs and halos, the keyboard, labels, the
  envelope editor) are laid on unchanged, alpha over, in the plugin's z-order;
* surface layers (the housing, cards, buttons, preset bar, ARP card) carry ink - text,
  icons, dividers - on a surface the chassis now provides. The ink is what differs from the
  layer's own local surface (a grey-level closing removes strokes narrower than ~20 design
  px and keeps the surface); it is transferred as a ratio in linear light, so dark text
  stays as dark on a new ivory and an orange icon keeps its colour. Edges, bevels and
  shadows of the old drawing (bands along each outline) are never transferred: the
  chassis has real ones. On dark surfaces (the graphite inset) ink is recoloured light.

Nothing is painted that the plugin or the chassis does not provide, except where a family
is vector by design (B1 tabs' underline, a fader's value fill), documented below.

    python scripts/composite_gui.py direction 06-hybrid-premium --scale 2
"""

import argparse
import json
import os

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CFG = os.path.join(LAB, "blender", "configs")
TEXT = np.array([0x23, 0x21, 0x1e]) / 255.0
LIGHT_INK = np.array([0xec, 0xe5, 0xd8]) / 255.0
ACCENT = (0xe8, 0x69, 0x2a)
ACTIVE_MODIFIERS = {24, 25, 46, 66, 67}


def load_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


CTRL = load_json(os.path.join(CFG, "controls.json"))
DIRECTIONS = load_json(os.path.join(CFG, "directions.json"))["directions"]


def lin(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def srgb(l):
    l = np.clip(l, 0.0, 1.0)
    return np.where(l <= 0.0031308, 12.92 * l, 1.055 * np.power(l, 1 / 2.4) - 0.055)


def lum(rgb):
    return rgb[..., 0] * 0.2126 + rgb[..., 1] * 0.7152 + rgb[..., 2] * 0.0722


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def rounded_rect_mask(h, w, rect, radius, inset, aa=1.0):
    """Coverage of a rounded rectangle (canvas px) shrunk by inset (negative grows)."""
    x, y, rw, rh = rect
    x, y, rw, rh = x + inset, y + inset, rw - 2 * inset, rh - 2 * inset
    r = max(0.0, min(radius - inset, 0.5 * min(rw, rh)))
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32) + 0.5
    cx, cy = x + 0.5 * rw, y + 0.5 * rh
    dx = np.abs(xx - cx) - (0.5 * rw - r)
    dy = np.abs(yy - cy) - (0.5 * rh - r)
    d = np.hypot(np.maximum(dx, 0), np.maximum(dy, 0)) + np.minimum(np.maximum(dx, dy), 0) - r
    return np.clip(0.5 - d / aa, 0.0, 1.0)


def band_mask(h, w, rect, radius, half):
    """1 within `half` px of a rounded rectangle's outline."""
    return np.clip(rounded_rect_mask(h, w, rect, radius, -half) - rounded_rect_mask(h, w, rect, radius, half), 0.0, 1.0)


def disc(h, w, centre, r):
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32) + 0.5
    return np.clip(r + 0.5 - np.hypot(xx - centre[0], yy - centre[1]), 0.0, 1.0)


class Layers:
    def __init__(self, scale):
        name = {1.0: "export1x", 1.5: "export1.5x", 2.0: "export2x", 4.0: "export4x"}[float(scale)]
        self.dir = os.path.join(LAB, "references", "current-gui", name)
        self.doc = load_json(os.path.join(self.dir, "layers.json"))
        self.scale = float(scale)

    def image(self, l, box=None):
        """The layer's pixels; `box` (layer px) limits decoding work to the part needed."""
        im = Image.open(os.path.join(self.dir, "layers", l["file"]))
        if box is not None:
            im = im.crop(box)
        return np.asarray(im.convert("RGBA")).astype(np.float32) / 255.0


class Compositor:
    def __init__(self, spec, chassis, scale, crop=None, state=None):
        self.spec = spec
        self.state = state or {}
        self.s = float(scale)
        self.crop = crop or (0, 0, 1448, 1086)
        self.L = Layers(scale)
        base = Image.open(chassis).convert("RGB")
        want = (int(round(self.crop[2] * self.s)), int(round(self.crop[3] * self.s)))
        if base.size != want:
            base = base.resize(want, Image.LANCZOS)
        self.canvas = np.asarray(base).astype(np.float32) / 255.0   # sRGB, as JUCE blends
        self.H, self.W = self.canvas.shape[:2]
        self.k = int(round(20 * self.s)) | 1
        details = spec.get("details", {})
        self.leds = details.get("leds", False)
        self.groove = details.get("dividerGroove", False)
        self.panel = CTRL["panels"][spec["panel"]]

    # ------------------------------------------------------------ geometry
    def px(self, rect):
        """Design rect -> canvas rect (floats)."""
        return ((rect[0] - self.crop[0]) * self.s, (rect[1] - self.crop[1]) * self.s, rect[2] * self.s, rect[3] * self.s)

    def place(self, l, img):
        """The layer's slice that falls on the canvas: (canvas slices, image slices)."""
        x0 = int(round((l["x"] - self.crop[0]) * self.s))
        y0 = int(round((l["y"] - self.crop[1]) * self.s))
        h, w = img.shape[:2]
        cx0, cy0 = max(0, x0), max(0, y0)
        cx1, cy1 = min(self.W, x0 + w), min(self.H, y0 + h)
        if cx1 <= cx0 or cy1 <= cy0:
            return None
        return (slice(cy0, cy1), slice(cx0, cx1)), (slice(cy0 - y0, cy1 - y0), slice(cx0 - x0, cx1 - x0)), (x0, y0)

    def local_mask(self, l, img, rect_design, radius, inset):
        """A mask in the layer's own pixels for a design-space rounded rectangle."""
        h, w = img.shape[:2]
        r = ((rect_design[0] - l["x"]) * self.s, (rect_design[1] - l["y"]) * self.s, rect_design[2] * self.s, rect_design[3] * self.s)
        return rounded_rect_mask(h, w, r, radius * self.s, inset * self.s)

    def local_band(self, l, img, rect_design, radius, half):
        h, w = img.shape[:2]
        r = ((rect_design[0] - l["x"]) * self.s, (rect_design[1] - l["y"]) * self.s, rect_design[2] * self.s, rect_design[3] * self.s)
        return band_mask(h, w, r, radius * self.s, half * self.s)

    def local_disc(self, l, img, centre, r):
        h, w = img.shape[:2]
        return disc(h, w, ((centre[0] - l["x"]) * self.s, (centre[1] - l["y"]) * self.s), r * self.s)

    # ------------------------------------------------------------ operations
    def over(self, l, img, alpha_scale=1.0, colour=None):
        p = self.place(l, img)
        if p is None:
            return
        cs, ls, _ = p
        a = img[ls + (3,)][..., None] * alpha_scale
        rgb = img[ls + (slice(0, 3),)] if colour is None else np.broadcast_to(colour, img[ls + (slice(0, 3),)].shape)
        self.canvas[cs] = self.canvas[cs] * (1 - a) + rgb * a

    def ink(self, l, img, body, exclude=None, recolour=None, colour=None, strength=1.0, stroke=6.0, light=None):
        """Transfer the layer's ink (see the module doc) inside `body` (layer px mask).
        stroke: the widest ink stroke in design px (the closing removes features narrower
        than this; broader shading - an old pressed gradient, a bevel - is not ink)."""
        a = img[..., 3]
        q = img[..., :3] * a[..., None] + (1 - a[..., None])
        k = int(round(stroke * self.s)) | 1
        # Closing removes thin dark ink (text, icons); inside `light` regions (a badge's
        # letter) an opening first removes thin light ink too. (Opening everywhere would fill
        # the light gaps between dense dark strokes.)
        bg = np.stack([ndimage.grey_closing(q[..., c], size=(k, k)) for c in range(3)], -1)
        if light is not None:
            asf = np.stack([ndimage.grey_closing(ndimage.grey_opening(q[..., c], size=(k, k)), size=(k, k)) for c in range(3)], -1)
            bg = bg + (asf - bg) * light[..., None]
        lq, lb = lin(q), np.maximum(lin(bg), 1e-4)
        ratio = np.clip(lq / lb, 0.0, 6.0)
        dev = np.abs(ratio - 1.0).max(-1)
        w = smoothstep(0.012, 0.045, dev) * body * (a > 0.985)
        if exclude is not None:
            w = w * (1 - exclude)
        w = w * strength
        p = self.place(l, img)
        if p is None:
            return
        cs, ls, _ = p
        w, ratio = w[ls], ratio[ls]
        dst = lin(self.canvas[cs])
        if colour is not None or recolour is not None:
            inkalpha = np.clip((lum(lb) - lum(lq)) / np.maximum(lum(lb) - lum(lin(TEXT)), 1e-3), 0, 1)[ls]
        if colour is not None:
            tgt = dst + (lin(np.asarray(colour, np.float32)) - dst) * (inkalpha * w)[..., None]
        else:
            tgt = dst + (dst * ratio - dst) * w[..., None]
        if recolour is not None:
            rc = recolour[ls] * w
            tgt = tgt + (lin(LIGHT_INK) - tgt) * 0.0   # keep shape
            tgt = np.where(rc[..., None] > 0, dst + (lin(LIGHT_INK) - dst) * (inkalpha * rc)[..., None], tgt)
        self.canvas[cs] = srgb(tgt)

    def vector(self, draw_fn):
        """Vector drawing (supersampled 4x) laid over the canvas: families that are flat by
        design (B1 underline, value fills)."""
        ss = 4
        img = Image.new("RGBA", (self.W * ss, self.H * ss), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        draw_fn(d, lambda v: v * self.s * ss, lambda x: (x - self.crop[0]) * self.s * ss, lambda y: (y - self.crop[1]) * self.s * ss)
        img = img.resize((self.W, self.H), Image.BOX)
        arr = np.asarray(img).astype(np.float32) / 255.0
        a = arr[..., 3:4]
        self.canvas = self.canvas * (1 - a) + arr[..., :3] * a

    # ------------------------------------------------------------ the GUI
    def run(self):
        layers = self.L.doc["layers"]
        margin = 24.0   # design px around the crop: wider than any ink filter (16 px)
        cx0, cy0 = self.crop[0] - margin, self.crop[1] - margin
        cx1, cy1 = self.crop[0] + self.crop[2] + margin, self.crop[1] + self.crop[3] + margin
        for l in layers:
            cls = l["class"].split("::")[-1]
            if l["index"] == 0 or cls in ("ModulationPanel",):
                continue
            # Only the part of the layer near the crop is decoded and processed: `l` gets the
            # image's origin (x, y), its component bounds stay in bx, by, bw, bh.
            ix0, iy0 = max(l["x"], cx0), max(l["y"], cy0)
            ix1, iy1 = min(l["x"] + l["w"], cx1), min(l["y"] + l["h"], cy1)
            if ix1 <= ix0 or iy1 <= iy0:
                continue
            box = tuple(int(round(v)) for v in ((ix0 - l["x"]) * self.s, (iy0 - l["y"]) * self.s, (ix1 - l["x"]) * self.s, (iy1 - l["y"]) * self.s))
            img = self.L.image(l, box)
            l = dict(l, bx=l["x"], by=l["y"], bw=l["w"], bh=l["h"], x=l["x"] + box[0] / self.s, y=l["y"] + box[1] / self.s)
            handler = getattr(self, "do_" + cls, None)
            if handler is not None:
                handler(l, img)
            else:
                self.over(l, img)
        self.after()
        return self.canvas

    def body(self, l, img, radius, band):
        return self.local_mask(l, img, (l["bx"], l["by"], l["bw"], l["bh"]), radius, band)

    def do_Content(self, l, img):
        body = self.local_mask(l, img, (25, 19, 1398, 1053), 26, 8)
        excl = self.local_band(l, img, (35, 682, 1378, 216), 13, 7) + self.local_band(l, img, (146, 910, 1154, 106), 6, 6)
        if self.groove:
            excl += self.local_mask(l, img, (941, 690, 8, 200), 1, 0)
        dots = np.zeros(img.shape[:2], np.float32)
        for x in (128, 274, 420, 566, 712, 858):
            excl += self.local_disc(l, img, (x, 883), 10)
            dots += self.local_disc(l, img, (x, 883), 6.5)
        recolour = None
        if self.panel["kind"] == "graphiteInset":
            recolour = self.local_mask(l, img, tuple(self.panel["inset"]), self.panel["insetRadius"], 2)
        self.ink(l, img, body, np.clip(excl, 0, 1), recolour=recolour, stroke=16.0)
        if not self.leds:
            # The production LEDs are small discs, not ink: laid on as drawn.
            part = img.copy()
            part[..., 3] *= np.clip(dots, 0, 1)
            self.over(l, part)

    def do_EngineCard(self, l, img):
        badge = (l["bx"] + 21, l["by"] + 11, 47, 42)
        excl = self.local_band(l, img, badge, 7, 4.5)
        light = self.local_mask(l, img, badge, 7, 0)
        self.ink(l, img, self.body(l, img, 16, 3.5), excl, stroke=7.0, light=light)

    def button_ink(self, l, img, radius, band=2.5, exclude=None, kind="secondary"):
        state = self.state.get("modifier" if kind == "modifier" else "secondary", "auto")
        colour, strength = None, 1.0
        if kind == "modifier" and state not in ("auto",):
            colour = (ACCENT_F if state == "active" else IDLE_ICON)
            if state == "disabled":
                strength = 0.45
        self.ink(l, img, self.body(l, img, radius, band), exclude, colour=colour, strength=strength, stroke=5.0)
        if state == "disabled":
            p = self.place(l, img)
            if p is not None:
                cs, _, _ = p
                self.canvas[cs] = self.canvas[cs] + (DISABLED_WASH - self.canvas[cs]) * 0.35

    def do_ModifierButton(self, l, img):
        self.button_ink(l, img, 7, kind="modifier")

    def do_ModeSelector(self, l, img):
        self.button_ink(l, img, 7)

    def do_EqButton(self, l, img):
        self.button_ink(l, img, 6)

    def do_LayerStateButton(self, l, img):
        self.button_ink(l, img, 4, band=2.0)

    def do_AdvancedCardButton(self, l, img):
        self.button_ink(l, img, 7)

    def do_PresetBar(self, l, img):
        self.button_ink(l, img, 12, band=3.0)

    def do_ArpControl(self, l, img):
        pill = tuple(CTRL["geometry"]["arpPill"])
        excl = self.local_band(l, img, (pill[0] - 1, pill[1] - 1, pill[2] + 2, pill[3] + 2), 9, 2.5)
        ax, ay, _ = CTRL["geometry"]["arpLed"]
        excl = excl + self.local_disc(l, img, (ax, ay), 9)
        self.button_ink(l, img, 10, exclude=np.clip(excl, 0, 1))
        if not self.leds:
            part = img.copy()
            part[..., 3] *= self.local_disc(l, img, (ax, ay), 7.5)
            self.over(l, part)

    def do_MacroLabel(self, l, img):
        if self.panel["kind"] == "graphiteInset":
            self.over(l, img, colour=LIGHT_INK)
        else:
            self.over(l, img)

    def do_VolumeSlider(self, l, img):
        if self.spec.get("fader", "original") == "original":
            self.over(l, img)
            return
        h = img.shape[0]
        rows = np.zeros(img.shape[:2], np.float32)
        rows[: max(0, int(round((l["by"] + 21 - l["y"]) * self.s)))] = 1.0
        part = img.copy()
        part[..., 3] *= rows
        self.over(l, part)
        self.fill_value()

    def fill_value(self):
        """The fader's value fill: software-drawn (never baked into the cap), in the slot up
        to the cap's edge."""
        v = CTRL["geometry"]["volume"]
        track = self.spec.get("track", "T1")
        cap = CTRL["faders"][self.spec["fader"]]
        pos = self.state.get("faderPos", 0.75)
        x0, x1, y = v["left"], v["left"] + pos * (v["right"] - v["left"]) - 0.5 * cap["w"] - 1.0, v["trackY"]
        if x1 <= x0 + 1:
            return
        width = {"T1": 1.6, "T2": 3.0, "T3": 1.2}.get(track, 1.6)
        alpha = 230 if self.state.get("fader") == "drag" else 205

        def draw(d, S, X, Y):
            r = S(0.5 * width)
            d.rounded_rectangle((X(x0), Y(y) - r, X(x1), Y(y) + r), radius=r, fill=ACCENT + (alpha,))
        self.vector(draw)

    def do_Strip(self, l, img):
        """Tabs. The production underline is removed (rows below the words); B1 redraws it
        crisper, the physical families show selection with geometry instead."""
        fam = CTRL["tabs"][self.spec.get("tabs", "B1")]
        cut = img.copy()
        cut[max(0, int(round((l["by"] + 17.5 - l["y"]) * self.s))):, :, 3] = 0.0
        sel = self.state.get("tab", 0)
        hover = self.state.get("tabHover")
        tabs = CTRL["geometry"]["tabs"]
        if fam["kind"] == "flat":
            # Selected word at full text colour, idle words a touch quieter.
            words = np.zeros(cut.shape[:2], np.float32)
            t = tabs[sel]
            x0, x1 = int((t[0] - l["x"]) * self.s), int((t[0] + t[2] - l["x"]) * self.s)
            words[:, max(0, x0):max(0, x1)] = 1.0
            sel_part = cut.copy()
            sel_part[..., 3] *= words
            idle = cut.copy()
            idle[..., 3] *= (1 - words) * 0.88
            self.over(l, idle)
            self.over(l, sel_part, colour=TEXT)
            spans = {0: (971, 998.5), 1: (1028.5, 1051.5), 2: (1109.5, 1132.5), 3: (1190.5, 1213), 4: (1270.5, 1293)}

            def draw(d, S, X, Y):
                a, b = spans[sel]
                yb = l["by"] + 19.6
                d.rounded_rectangle((X(a), Y(yb) - S(1.0), X(b), Y(yb) + S(1.0)), radius=S(1.0), fill=ACCENT + (255,))
                if hover is not None and hover != sel:
                    a, b = spans[hover]
                    d.rounded_rectangle((X(a), Y(yb) - S(0.5), X(b), Y(yb) + S(0.5)), radius=S(0.5), fill=(0x8a, 0x84, 0x7b, 120))
                if self.state.get("tabFocus"):
                    t = tabs[sel]
                    d.rounded_rectangle((X(t[0]), Y(t[1]), X(t[0] + t[2]), Y(t[1] + t[3])), radius=S(5), outline=ACCENT + (150,), width=max(1, int(S(1.0))))
            self.vector(draw)
        else:
            self.over(l, cut)

    def after(self):
        pass


ACCENT_F = np.array(ACCENT) / 255.0
IDLE_ICON = np.array([0x8a, 0x84, 0x7b]) / 255.0
DISABLED_WASH = np.array([0xec, 0xe6, 0xdc]) / 255.0


def compose_direction(name, scale):
    spec = DIRECTIONS[name]
    render_scale = 2 if scale <= 2 else 4
    chassis = os.path.join(LAB, "renders", "chassis", f"{name}@{render_scale}x.png")
    if not os.path.exists(chassis):
        chassis = os.path.join(LAB, "renders", "chassis", f"{name}@1x.png")
    out = Compositor(spec, chassis, scale).run()
    return Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8))


def compose_studies(which):
    import sys
    sys.path.insert(0, os.path.join(LAB, "blender"))
    from studies import CROPS, studies
    for st in studies():
        if which not in ("all", st["out"].split("/")[0], st["out"]):
            continue
        chassis = os.path.join(LAB, "renders", st["chassis"] + ".png")
        if not os.path.exists(chassis):
            print("missing", chassis)
            continue
        out = Compositor(st["spec"], chassis, st["scale"], CROPS[st["crop"]], st["state"]).run()
        path = os.path.join(LAB, "mockups", "components", st["out"] + ".png")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8)).save(path)
        print("wrote", path, flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("what", choices=["direction", "family"])
    ap.add_argument("name")
    ap.add_argument("--scale", type=float, default=2.0)
    ap.add_argument("--out")
    a = ap.parse_args()
    if a.what == "family":
        compose_studies(a.name)
        return
    img = compose_direction(a.name, a.scale)
    out = a.out or os.path.join(LAB, "mockups", "full-gui", f"{a.name}@{a.scale:g}x.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    img.save(out)
    print("wrote", out)


if __name__ == "__main__":
    main()
