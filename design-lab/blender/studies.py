"""The family studies: which variant, which state, which crop of the GUI (design px).

Pure data (no bpy): the renderer (scenes.py) renders each study's chassis, the compositor
(scripts/composite_gui.py) lays the plugin's drawing over it. A study whose look differs
only in software (a focus ring, a disabled wash, a B1 hover) reuses another study's chassis
("chassis") instead of rendering the same geometry again.
"""

CROPS = {
    "engine-buttons": (43, 578, 435, 61),
    "card-header": (43, 129, 435, 56),
    "arp-advanced": (1300, 905, 121, 117),
    "tabs": (950, 682, 460, 34),
    "fader": (1140, 40, 214, 58),
    "macro-panel": (25, 672, 1398, 236),
    "enclosure": (25, 19, 940, 640),
    "details-leds": (45, 860, 900, 40),
    "details-divider": (900, 690, 120, 200),
    "details-arp": (1300, 905, 121, 117),
}

# Render scale per crop (pixels per design px): 4x for small controls (judged up close and
# downsampled for actual size), 2x for the large surfaces.
SCALE = {"macro-panel": 2.0, "enclosure": 2.0}

BASE = {"enclosure": "E1", "panel": "D1", "buttons": "A4", "tabs": "B1", "fader": "original", "track": "original", "details": {}}


def study(out, crop, state=None, chassis=None, **spec):
    return {"out": out, "spec": dict(BASE, **spec), "crop": crop, "state": state or {}, "chassis": chassis or out,
            "scale": SCALE.get(crop, 4.0)}


def studies():
    out = []
    for a in ("A1", "A2", "A3", "A4"):
        for st in ("normal", "hover", "pressed", "active"):
            out.append(study(f"buttons/{a}-{st}", "engine-buttons", {"modifier": st}, buttons=a))
        out.append(study(f"buttons/{a}-disabled", "engine-buttons", {"modifier": "disabled"}, chassis=f"buttons/{a}-normal", buttons=a))
        out.append(study(f"buttons/{a}-card-header", "card-header", buttons=a))
        out.append(study(f"buttons/{a}-arp-advanced", "arp-advanced", buttons=a))
    for b in ("B1", "B2", "B3", "B4"):
        out.append(study(f"tabs/{b}-selected", "tabs", {"tab": 0}, tabs=b))
        out.append(study(f"tabs/{b}-hover", "tabs", {"tab": 0, "tabHover": 2}, chassis=None if b != "B1" else f"tabs/{b}-selected", tabs=b))
        out.append(study(f"tabs/{b}-focused", "tabs", {"tab": 0, "tabFocus": True}, chassis=f"tabs/{b}-selected", tabs=b))
    for c in ("C1", "C2", "C3"):
        for t in ("T1", "T2", "T3"):
            for p in (0.0, 0.25, 0.5, 0.75, 1.0):
                out.append(study(f"sliders/{c}-{t}-{int(p * 100):03d}", "fader", {"faderPos": p}, fader=c, track=t))
            out.append(study(f"sliders/{c}-{t}-hover", "fader", {"faderPos": 0.75, "fader": "hover"}, fader=c, track=t))
            out.append(study(f"sliders/{c}-{t}-drag", "fader", {"faderPos": 0.75, "fader": "drag"}, fader=c, track=t))
    for d in ("D1", "D2", "D3", "D4", "D5", "D6"):
        # D5's dark inset cannot carry the flat production LEDs: lens LEDs there.
        out.append(study(f"panels/{d}", "macro-panel", panel=d, details={"leds": d == "D5"}))
    for e in ("E1", "E2", "E3", "E4"):
        out.append(study(f"backgrounds/{e}", "enclosure", enclosure=e))
    out.append(study("details/leds-off", "details-leds"))
    out.append(study("details/leds-on", "details-leds", details={"leds": True}))
    out.append(study("details/divider-flat", "details-divider"))
    out.append(study("details/divider-groove", "details-divider", details={"dividerGroove": True}))
    out.append(study("details/seam", "details-divider", details={"seams": True}))
    out.append(study("details/arp-led-flat", "details-arp"))
    out.append(study("details/arp-led-lens", "details-arp", details={"leds": True}))
    out.append(study("details/button-wells", "engine-buttons", buttons="A2"))
    out.append(study("details/recessed-track", "fader", fader="C2", track="T2"))
    return out
