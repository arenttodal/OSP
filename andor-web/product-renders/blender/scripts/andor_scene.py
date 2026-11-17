"""ANDOR OSP product-render scene (pass 3.5 + checkpoint 2; headless Blender 4.2 / bpy, Cycles).

The instrument is built in layers from the genuine editor (textures/geometry.json, written by
prep_textures.py from the OSP test captures):
  A  housing     teak: a back rail and two side cheeks (14.4 mm, mitred at the back). Checkpoint 2
                 (the trimmed editor): the cheeks run to the front edge and end square; the ceramic
                 panel and a shallow ivory front return are one piece (no fascia), over a recessed
                 dark base. The pass 3.5 teak fascia is rebuilt when geometry.json has no "apron"
                 (the old capture), for like-for-like comparisons. A ceramic tray inside, a plinth below;
  B  ceramic     the panel, seated 1 mm below the timber, and the raised cards (three layer cards,
                 the macro/envelope panel), each with real bevels and thickness;
  C  displays    wells cut into the cards (Boolean), dark bezel walls, the screen on the well floor:
                 the editor's pixels, emitted under a clear coat;
  D  controls    25 knobs (macro, layer, envelope) modelled from the editor's own centres, radii
                 and pointer angles, on a clean capture with no baked caps; buttons, the preset bar,
                 ARP/Advanced, the key bed and 36 black keys raised at their measured bounds;
  E  artwork     everything else (type, scales, arcs, ticks, waveforms) is the capture, projected
                 straight down the face normal onto the tops of B and D. Nothing is redrawn.

Usage (from andor-web/, Python 3.11 venv with bpy==4.2.0):
  python product-renders/blender/scripts/andor_scene.py -- --camera B --stage neutral \
      --res 2000 1500 --samples 128 --out out.png
  --camera A | B | macros | display | corner | corner_fl | front_edge | keys34 | hero_A | hero_B | ...
  --stage neutral | dark
  --variant shaded | clay | alpha      --save-blend PATH (images packed)
"""
import argparse
import json
import math
import os
import sys

import bpy  # first: bmesh and mathutils come with the bpy module
import bmesh
from mathutils import Euler, Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
p = argparse.ArgumentParser()
p.add_argument("--camera", default="B", choices=["A", "B", "macros", "display", "corner", "hero_A", "hero_B", "knob", "knob_low", "small", "module", "corner_back", "corner_fl", "corner_br", "front_edge", "keys34", "keys_macro", "wheels", "strips", "strips34", "strips_top"])
p.add_argument("--stage", default="neutral", choices=["neutral", "dark", "gallery"])
p.add_argument("--variant", default="shaded", choices=["shaded", "clay", "alpha"])
p.add_argument("--res", type=int, nargs=2, default=None, help="default 2000 x 1500, or the --shot's own")
p.add_argument("--setup", choices=["ANDOR_GALLERY", "ANDOR_FLOATING", "ANDOR_MATERIAL"],
               help="Gate C: a campaign setup from config/campaign_setups.json (stage, lights, float, exposure); needs --shot")
p.add_argument("--shot", help="Gate C: the setup's shot (camera, product rotation, resolution), e.g. A1, B2, C1")
p.add_argument("--noise", type=float, default=0.012, help="adaptive-sampling noise threshold (masters: 0.005 keeps fine GUI text out of the denoiser's reach)")
p.add_argument("--bezel-height", type=float, default=0.6, help="--wheels physical: the ivory bezel's height above the panel (mm; C2 test 0.6, C2.5 studies 0.4 / 0.3)")
p.add_argument("--wheel-finish", default="satin", choices=["c2", "satin"],
               help="--wheels physical: c2 = the C2 test's soft-touch charcoal; satin = a slightly lighter, lower-roughness crown that catches a highlight, over a darker, rougher slot")
p.add_argument("--strip-crown", type=float, default=0.02, help="--wheels strip: convex crown across the strip's width, as a fraction of the width (A 0, B 0.02, C 0.04)")
p.add_argument("--wheels", default="flat", choices=["flat", "physical", "strip"],
               help="C2 test only: 'physical' models PITCH/MOD from the GUI (ivory bezel, slot, wheel, PITCH's orange band); default flat artwork")
p.add_argument("--float", type=float, help="override the setup's float height (m), for floating-height studies")
p.add_argument("--dist-scale", type=float, default=1.0, help="multiply the shot's camera distance (product-scale studies: 1/1.15, 1/1.25)")
p.add_argument("--scale", type=float, default=1.0)
p.add_argument("--samples", type=int, default=128)
p.add_argument("--out")
p.add_argument("--textures", default="product-renders/blender/textures")
p.add_argument("--save-blend")
p.add_argument("--view", default="AgX")
p.add_argument("--look", default="AgX - Punchy")
p.add_argument("--exposure", type=float, default=None)
p.add_argument("--border", type=float, nargs=4)
p.add_argument("--knob", default="hd", choices=["pass2", "shallow", "shallow_fluted", "mid", "approved", "hd"],
               help="knob geometry: pass 2's, the approved profile shallow (0.15 D) plain or finely fluted, or the approved spec (11 mm on 28 mm)")
p.add_argument("--hd", type=float, default=0.30,
               help="--knob hd: the official profile with only its total height changed, as H/D (D = 28 mm); pass 3.5 lock: 0.30")
p.add_argument("--flute-depth", type=float, default=0.252, help="--knob hd: flute depth in mm (official 0.42; round 1 E 0.252)")
p.add_argument("--frame-h", type=float, default=14.4, help="timber height in mm (pass 3.5 lock: 14.4; round 1 test: 16, 14.4, 12.8)")
# Keyboard + front body round (trimmed editor only). Heights in mm, scene scale.
p.add_argument("--lower-shell", type=float, default=5.5, help="ivory lower shell under the cheeks (the body's extra mass)")
p.add_argument("--feet", type=float, default=0.8, help="height of the four dark feet")
p.add_argument("--undercut", type=float, default=3.0, help="radius of the lower shell's tuck-under edge")
p.add_argument("--white-drop", type=float, default=0.6, help="white key tops below the shell top")
p.add_argument("--white-key", type=float, default=3.4, help="white key body height (its front face)")
p.add_argument("--black-rise", type=float, default=2.0, help="black key tops above the white key tops")
p.add_argument("--ledge-drop", type=float, default=3.6, help="the front return's top below the shell top")
p.add_argument("--ledge-round", type=float, default=1.5, help="radius of the front return's nose")
p.add_argument("--lip", type=float, default=5.0, help="height of the front return's face (the lip) below its top")
p.add_argument("--front-setback", type=float, default=1.2, help="how far the body under the lip (and the lower shell's front) sits behind it")
# Gate B (look development): materials and lighting, each switchable so comparisons isolate them.
p.add_argument("--finish", default="refined", choices=["r2", "refined"], help="r2: the keyboard + front body round's materials; refined: Gate B's ceramic family, keys, screens, natural teak")
p.add_argument("--teak", default="warm", choices=["current", "neutral", "warm"], help="--finish refined: the teak finish (current = r2's tone with the natural-grain fixes)")
p.add_argument("--light", default="sculpt", choices=["neutral", "sculpt"], help="neutral: the construction-review rig; sculpt: Gate B's studio rig")
p.add_argument("--report", help="write a JSON geometry report of the knobs and frame here")
args = p.parse_args(argv)

# Gate C: a campaign setup replaces the stage, lights, camera and output, never the product.
SETUP = SHOT = None
if args.setup:
    _cfg = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "config", "campaign_setups.json")))
    SETUP = _cfg["setups"][args.setup]
    SHOT = SETUP["shots"][args.shot]
    # C2.5: a shot may refine its setup without touching it ("overrides": floor, world, exposure,
    # product_light_ev, per-light changes by name, extra lights), so earlier shots render as they were.
    _ov = SHOT.get("overrides")
    if _ov:
        import copy
        SETUP = copy.deepcopy(SETUP)
        for _k in ("floor", "world", "exposure", "product_light_ev"):
            if _k in _ov:
                SETUP[_k] = _ov[_k]
        for _L in SETUP["lights"]:
            _L.update(_ov.get("lights", {}).get(_L["name"], {}))
        SETUP["lights"] = [L for L in SETUP["lights"] if not L.get("off")] + _ov.get("add_lights", [])
    args.stage = SETUP["stage"]
    if args.res is None:
        args.res = SHOT["res"]
if args.res is None:
    args.res = [2000, 1500]

TEX = os.path.abspath(args.textures)
GEO = json.load(open(os.path.join(TEX, "geometry.json")))
EW, EH = GEO["editor_px"]
APRON = GEO.get("apron")
# C2.5: the current editor trims its three source cards (EngineCard::footTrim), so everything under
# them sits higher. KY carries that to every editor y below the cards that is written as a number here
# (0 for every earlier capture: their scenes rebuild unchanged).
KY = GEO["keyboard"]["bed"][1] - 913.0          # checkpoint 2 (the trimmed editor); absent: the pass 3.5 footer + teak fascia
# World origin: the pass 3.5 face centre (editor 1448 x 1086), kept for the trimmed editor too, so the
# old and new instruments share every camera, light and pixel above the keyboard.
CY = 1086 / 2

# --- dimensions (visualisation scale: the face is 400 mm wide) -------------------------------
W = 0.400
PX = W / EW
H = EH * PX
MM = 0.001
# Pass 3: heights follow OSP's own approved design (design-lab direction 09, controls.json), which
# is drawn at 0.31 mm per editor px. DMM converts its millimetres to this scene's scale.
DMM = (PX / 0.00031) * MM
if APRON:
    # Keyboard + front body round: the cheeks sit on an ivory lower shell (on four small feet), so the
    # body gains mass in ivory, not in wood; the timber stays 14.4 mm.
    FEET_H = args.feet * MM
    PLINTH_H = FEET_H + args.lower_shell * MM   # the lower shell's top: the cheeks and the front return sit on it
else:
    PLINTH_H = 2.0 * MM                  # pass 3.5: dark base, inset 3 mm
FRAME_TOP = PLINTH_H + args.frame_h * MM   # timber height (pass 2: 16 mm)
PANEL_TOP = FRAME_TOP - 1.5 * DMM    # the housing: teak stands 1.5 mm proud of it (trim.teak)
TRAY_TOP = PANEL_TOP - 1.2 * MM      # layer A: the tray band between panel and timber
CARD_H, CARD_BEVEL = 0.9 * DMM, 0.42 * DMM      # cards (spec 0.9 / 0.55; bevel crisper, brief 3.2)
MPANEL_H, MPANEL_BEVEL = 0.6 * DMM, 0.36 * DMM  # macro panel D3 (spec 0.6 / 0.5)
CARD_TOP = PANEL_TOP + CARD_H
WELL, WELL_LIP, WELL_GROW = 0.6 * DMM, 0.32 * DMM, 1.6   # bezel pockets (geometry.bezel)
KEYBED_DEPTH = 1.2 * DMM             # the keybed is a pocket in the housing (geometry.keybed)
KEYBED_TOP = PANEL_TOP - KEYBED_DEPTH
BLACK_TOP = KEYBED_TOP + 2.2 * MM    # black keys: as pass 2 above the white keys
KNOB_H = {"macro": 8.0 * MM, "layer": 5.5 * MM, "mini": 4.5 * MM}   # pass 2 knob only
FLOAT = {"neutral": 0.055, "dark": 0.28, "gallery": 0.0}[args.stage]
if SETUP:
    FLOAT = args.float if args.float is not None else SETUP["float_m"]


def lin(c):
    return tuple(((x / 255) / 12.92) if x / 255 <= 0.04045 else (((x / 255) + 0.055) / 1.055) ** 2.4 for x in c)


def ex(x):
    return (x - EW / 2) * PX


def ey(y):
    return (CY - y) * PX


# --- scene -----------------------------------------------------------------------------------
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = "CYCLES"
cy = scene.cycles
cy.device = "CPU"
cy.samples = args.samples
cy.use_adaptive_sampling = True
cy.adaptive_threshold = args.noise
cy.use_denoising = True
cy.denoiser = "OPENIMAGEDENOISE"
cy.max_bounces = 10
cy.glossy_bounces = 4
cy.caustics_reflective = False
cy.caustics_refractive = False
cy.blur_glossy = 0.4
scene.render.resolution_x, scene.render.resolution_y = args.res
scene.render.resolution_percentage = int(round(args.scale * 100))
scene.render.filter_size = 1.2
scene.view_settings.view_transform = args.view
scene.view_settings.look = args.look
scene.view_settings.exposure = args.exposure if args.exposure is not None else (
    SETUP["exposure"] if SETUP else {"neutral": -0.55, "dark": 0.3, "gallery": -0.42}[args.stage])
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA" if args.variant == "alpha" else "RGB"
scene.render.image_settings.color_depth = "16"
scene.render.film_transparent = args.variant == "alpha"

PRODUCT = bpy.data.collections.new("OSP product")
scene.collection.children.link(PRODUCT)
STAGE = bpy.data.collections.new("Stage")
scene.collection.children.link(STAGE)
root = bpy.data.objects.new("OSP", None)
PRODUCT.objects.link(root)


def obj(name, me, coll=PRODUCT, parent=True):
    o = bpy.data.objects.new(name, me)
    coll.objects.link(o)
    if parent:
        o.parent = root
    return o


def img(path, non_color=False):
    i = bpy.data.images.load(os.path.join(TEX, path))
    if non_color:
        i.colorspace_settings.name = "Non-Color"
    return i


FACE_IMG = img("osp_face.png")
TEAK_IMG = img("teak_strip.png")


# --- node helpers ----------------------------------------------------------------------------
class NT:
    def __init__(self, name):
        self.m = bpy.data.materials.new(name)
        self.m.use_nodes = True
        self.t = self.m.node_tree
        for n in list(self.t.nodes):
            self.t.nodes.remove(n)
        self.out = self.t.nodes.new("ShaderNodeOutputMaterial")

    def n(self, kind, **inputs):
        node = self.t.nodes.new(kind)
        for k, v in inputs.items():
            node.inputs[k].default_value = v
        return node

    def link(self, a, b):
        self.t.links.new(a, b)

    def math(self, op, a, b=None, c=None):
        m = self.t.nodes.new("ShaderNodeMath")
        m.operation = op
        for i, v in enumerate((a, b, c)):
            if v is None:
                continue
            if isinstance(v, (int, float)):
                m.inputs[i].default_value = v
            else:
                self.link(v, m.inputs[i])
        return m.outputs[0]

    def principled(self):
        return self.n("ShaderNodeBsdfPrincipled")

    def finish(self, shader_out):
        self.link(shader_out, self.out.inputs[0])
        return self.m


def face_uv(nt):
    """Editor UV from the product root's object space: the capture projected down the normal."""
    tc = nt.n("ShaderNodeTexCoord")
    tc.object = root
    sep = nt.n("ShaderNodeSeparateXYZ")
    nt.link(tc.outputs["Object"], sep.inputs[0])
    u = nt.math("MULTIPLY_ADD", sep.outputs[0], 1 / W, 0.5)
    v = nt.math("MULTIPLY_ADD", sep.outputs[1], 1 / H, (EH - CY) / EH)
    uv = nt.n("ShaderNodeCombineXYZ")
    nt.link(u, uv.inputs[0])
    nt.link(v, uv.inputs[1])
    return tc, uv.outputs[0]


def mat_face(name="OSP Face (capture)", rough=0.48, spec=0.22, coat=0.05, tint=None):
    """Lit ceramic carrying the interface artwork. No emission: the light gives it form. `tint`
    (linear RGB multipliers, within a percent or two) separates related parts without colour blocks."""
    nt = NT(name)
    tc, uv = face_uv(nt)
    tex = nt.n("ShaderNodeTexImage")
    tex.image, tex.interpolation, tex.extension = FACE_IMG, "Cubic", "EXTEND"
    nt.link(uv, tex.inputs[0])
    b = nt.principled()
    if tint:
        mix = nt.n("ShaderNodeMix")
        mix.data_type, mix.blend_type = "RGBA", "MULTIPLY"
        mix.inputs[0].default_value = 1.0
        nt.link(tex.outputs["Color"], mix.inputs[6])
        mix.inputs[7].default_value = (*tint, 1)
        nt.link(mix.outputs[2], b.inputs["Base Color"])
    else:
        nt.link(tex.outputs["Color"], b.inputs["Base Color"])
    b.inputs["Roughness"].default_value = rough
    b.inputs["Specular IOR Level"].default_value = spec
    b.inputs["Coat Weight"].default_value = coat          # a thin glaze
    b.inputs["Coat Roughness"].default_value = 0.22
    noise = nt.n("ShaderNodeTexNoise", Scale=2600.0, Detail=3.0)
    nt.link(tc.outputs["Object"], noise.inputs["Vector"])
    bump = nt.n("ShaderNodeBump", Strength=0.012, Distance=0.0002)
    nt.link(noise.outputs["Fac"], bump.inputs["Height"])
    nt.link(bump.outputs[0], b.inputs["Normal"])
    return nt.finish(b.outputs[0])


def mat_display():
    """A screen: the editor's pixels emitted under a clear coat; the dark glass barely diffuses."""
    nt = NT("OSP Display (capture)")
    _, uv = face_uv(nt)
    tex = nt.n("ShaderNodeTexImage")
    tex.image, tex.interpolation, tex.extension = FACE_IMG, "Cubic", "EXTEND"
    nt.link(uv, tex.inputs[0])
    b = nt.principled()
    b.inputs["Base Color"].default_value = (0.004, 0.004, 0.004, 1)
    b.inputs["Roughness"].default_value = 0.25
    b.inputs["Specular IOR Level"].default_value = 0.0
    b.inputs["Coat Weight"].default_value = 0.45 if args.finish == "r2" else 0.34    # Gate B: less glare
    b.inputs["Coat Roughness"].default_value = 0.03 if args.finish == "r2" else 0.045
    nt.link(tex.outputs["Color"], b.inputs["Emission Color"])
    b.inputs["Emission Strength"].default_value = 1.7
    return nt.finish(b.outputs[0])


def mat_solid(name, rgb, rough, spec=0.4, coat=0.0, metal=0.0, bump=0.01):
    nt = NT(name)
    b = nt.principled()
    b.inputs["Base Color"].default_value = (*lin(rgb), 1)
    b.inputs["Specular IOR Level"].default_value = spec
    b.inputs["Metallic"].default_value = metal
    b.inputs["Coat Weight"].default_value = coat
    b.inputs["Coat Roughness"].default_value = 0.2
    tc = nt.n("ShaderNodeTexCoord")
    noise = nt.n("ShaderNodeTexNoise", Scale=90.0, Detail=5.0)
    nt.link(tc.outputs["Object"], noise.inputs["Vector"])
    r = nt.n("ShaderNodeMapRange")
    r.inputs["To Min"].default_value, r.inputs["To Max"].default_value = rough - 0.03, rough + 0.03
    nt.link(noise.outputs["Fac"], r.inputs["Value"])
    nt.link(r.outputs[0], b.inputs["Roughness"])
    if bump:
        fine = nt.n("ShaderNodeTexNoise", Scale=2400.0, Detail=3.0)
        nt.link(tc.outputs["Object"], fine.inputs["Vector"])
        bp = nt.n("ShaderNodeBump", Strength=bump, Distance=0.0002)
        nt.link(fine.outputs["Fac"], bp.inputs["Height"])
        nt.link(bp.outputs[0], b.inputs["Normal"])
    return nt.finish(b.outputs[0])


STRIP_W = 13.5 * PX            # teak_strip.png: 13.5 editor px across ...
STRIP_L = TEAK_IMG.size[1] / GEO["capture_scale"] * PX   # ... and both cheeks along the grain (pass 3.5: 2 x 1055 px)


def mat_teak(axis, offset):
    """The editor's teak. Long grain runs along the part (`axis` x or y) on its top and long faces;
    faces across the grain (the ends of the rails) show darker, rougher end grain."""
    nt = NT(f"ANDOR Teak ({axis})")
    tc = nt.n("ShaderNodeTexCoord")
    tc.object = root
    pos = nt.n("ShaderNodeSeparateXYZ")
    nt.link(tc.outputs["Object"], pos.inputs[0])
    nrm = nt.n("ShaderNodeSeparateXYZ")
    nt.link(tc.outputs["Normal"], nrm.inputs[0])
    long_i, across_i = (0, 1) if axis == "x" else (1, 0)
    nz = nt.math("ABSOLUTE", nrm.outputs[2])
    is_top = nt.math("GREATER_THAN", nz, 0.6)
    n_long = nt.math("ABSOLUTE", nrm.outputs[long_i])
    is_end = nt.math("GREATER_THAN", n_long, 0.7)
    # across coordinate: the other plan axis on the top, height on the long faces
    across = nt.n("ShaderNodeMix")
    across.data_type = "FLOAT"
    nt.link(is_top, across.inputs[0])
    nt.link(pos.outputs[2], across.inputs[2])
    nt.link(pos.outputs[across_i], across.inputs[3])
    u = nt.math("DIVIDE", across.outputs[0], STRIP_W)
    refined = args.finish == "refined"
    if refined:
        # Gate B: natural teak. The fibres wander a little (no ruler-straight lines), so the strip's
        # stacked rows never read as a repeat; tone and sheen drift gently over the length.
        wobble = nt.n("ShaderNodeTexNoise", Scale=32.0, Detail=2.0, Roughness=0.4)
        nt.link(tc.outputs["Object"], wobble.inputs["Vector"])
        u = nt.math("ADD", u, nt.math("MULTIPLY", nt.math("SUBTRACT", wobble.outputs["Fac"], 0.5), 0.45))
    v = nt.math("MULTIPLY_ADD", pos.outputs[long_i], 1 / STRIP_L, offset)
    uv = nt.n("ShaderNodeCombineXYZ")
    nt.link(u, uv.inputs[0])
    nt.link(v, uv.inputs[1])
    tex = nt.n("ShaderNodeTexImage")
    tex.image, tex.extension, tex.interpolation = TEAK_IMG, "MIRROR", "Cubic"
    nt.link(uv.outputs[0], tex.inputs[0])
    # End grain: the same wood seen across its fibres. Quarter-sawn stock: the growth rings cross the
    # end as fine, near-vertical lines (their centre far to the side), darker and more matte than
    # the long grain. Low contrast, so it reads as end grain, not as plywood layers.
    centre = abs(ex(12.5)) if axis == "y" else abs(ey(6.5))
    ra = nt.math("SUBTRACT", nt.math("ABSOLUTE", pos.outputs[across_i]), centre)
    rv = nt.n("ShaderNodeCombineXYZ")
    nt.link(nt.math("ADD", ra, 0.04), rv.inputs[0])
    nt.link(nt.math("SUBTRACT", pos.outputs[2], 0.012), rv.inputs[1])
    rings = nt.n("ShaderNodeTexWave", Scale=520.0, Distortion=1.6, Detail=2.0)
    rings.wave_type = "RINGS"
    rings.rings_direction = "SPHERICAL"           # the default (X) flattens rings into horizontal bands
    nt.link(rv.outputs[0], rings.inputs["Vector"])
    rmap = nt.n("ShaderNodeMapRange")
    rmap.inputs["To Min"].default_value, rmap.inputs["To Max"].default_value = 0.54, 0.64
    nt.link(rings.outputs["Fac"], rmap.inputs["Value"])
    # the end's base colour: the strip's mid-width, so no long-grain bands run across the end
    euv = nt.n("ShaderNodeCombineXYZ")
    euv.inputs[0].default_value = 0.5
    nt.link(nt.math("MULTIPLY_ADD", pos.outputs[long_i], 1 / STRIP_L, offset), euv.inputs[1])
    etex = nt.n("ShaderNodeTexImage")
    etex.image, etex.extension, etex.interpolation = TEAK_IMG, "MIRROR", "Cubic"
    nt.link(euv.outputs[0], etex.inputs[0])
    edark = nt.n("ShaderNodeHueSaturation")
    edark.inputs["Saturation"].default_value = 1.0
    nt.link(rmap.outputs[0], edark.inputs["Value"])
    nt.link(etex.outputs["Color"], edark.inputs["Color"])
    col = nt.n("ShaderNodeMix")
    col.data_type = "RGBA"
    nt.link(is_end, col.inputs[0])
    nt.link(tex.outputs["Color"], col.inputs[6])
    nt.link(edark.outputs["Color"], col.inputs[7])
    # colour-matched to the editor's teak under AgX (see documentation/materials.md)
    hsv = nt.n("ShaderNodeHueSaturation")
    # keyboard + front body round: deeper and less orange than pass 3.5 (1.12 / 0.74)
    # r2: hue 0.512, saturation 0.80, value 0.57 (a touch toward golden brown, less pink under AgX).
    # Gate B finishes: current (r2's tone), neutral (less saturated, quieter), warm (deeper satin).
    hue, sat, val, coat, coat_r = {"current": (0.512, 0.80, 0.57, 0.08, 0.35), "neutral": (0.514, 0.72, 0.58, 0.08, 0.35),
                                   "warm": (0.507, 0.92, 0.47, 0.16, 0.28)}[args.teak if refined else "current"]
    hsv.inputs["Hue"].default_value = hue
    hsv.inputs["Saturation"].default_value = sat
    hsv.inputs["Value"].default_value = val
    nt.link(col.outputs[2], hsv.inputs["Color"])
    if refined:
        drift = nt.n("ShaderNodeTexNoise", Scale=5.0, Detail=1.0)
        nt.link(tc.outputs["Object"], drift.inputs["Vector"])
        dmap = nt.n("ShaderNodeMapRange")
        dmap.inputs["To Min"].default_value, dmap.inputs["To Max"].default_value = 0.94, 1.05
        nt.link(drift.outputs["Fac"], dmap.inputs["Value"])
        tone = nt.n("ShaderNodeMix")
        tone.data_type, tone.blend_type = "RGBA", "MULTIPLY"
        tone.inputs[0].default_value = 1.0
        nt.link(hsv.outputs["Color"], tone.inputs[6])
        dcol = nt.n("ShaderNodeCombineColor")
        for k in range(3):
            nt.link(dmap.outputs[0], dcol.inputs[k])
        nt.link(dcol.outputs[0], tone.inputs[7])
        hsv_out = tone.outputs[2]
    else:
        hsv_out = hsv.outputs["Color"]
    b = nt.principled()
    nt.link(hsv_out, b.inputs["Base Color"])
    rough = nt.n("ShaderNodeMapRange")
    rough.inputs["To Min"].default_value, rough.inputs["To Max"].default_value = 0.62, 0.48
    nt.link(tex.outputs["Color"], rough.inputs["Value"])
    r_long = rough.outputs[0]
    if refined:   # sheen varies along the board, as oiled timber does
        rn = nt.n("ShaderNodeTexNoise", Scale=18.0, Detail=2.0)
        nt.link(tc.outputs["Object"], rn.inputs["Vector"])
        r_long = nt.math("ADD", r_long, nt.math("MULTIPLY", nt.math("SUBTRACT", rn.outputs["Fac"], 0.5), 0.12))
    r_end = nt.math("MAXIMUM", r_long, nt.math("MULTIPLY", is_end, 0.78))
    nt.link(r_end, b.inputs["Roughness"])
    b.inputs["Specular IOR Level"].default_value = 0.36
    b.inputs["Coat Weight"].default_value = coat     # oiled, not lacquered
    b.inputs["Coat Roughness"].default_value = coat_r
    bump = nt.n("ShaderNodeBump", Strength=0.10, Distance=0.00015)
    nt.link(col.outputs[2], bump.inputs["Height"])
    nt.link(bump.outputs[0], b.inputs["Normal"])
    return nt.finish(b.outputs[0])


CLAY = mat_solid("Clay", (150, 150, 148), 0.6, bump=0) if args.variant == "clay" else None
M_FACE = mat_face()
M_FACE_CARD = M_FACE_MPANEL = M_FACE_BUTTON = M_FACE
if args.finish == "refined":
    # Gate B: one ceramic family, each part its own response: roughness, sheen and colour
    # temperature differ by a few percent only, so the parts separate without colour blocks.
    M_FACE = mat_face("Ceramic housing (capture)", 0.50, 0.20, 0.04)
    M_FACE_CARD = mat_face("Ceramic source cards (capture)", 0.43, 0.26, 0.08, tint=(1.006, 1.0, 0.988))
    M_FACE_MPANEL = mat_face("Ceramic macro panel (capture)", 0.46, 0.23, 0.06, tint=(1.0, 1.0, 0.996))
    M_FACE_BUTTON = mat_face("Ceramic buttons (capture)", 0.38, 0.30, 0.10, tint=(0.994, 1.0, 1.008))
M_DISPLAY = mat_display()
C = GEO["colours"]
M_TRAY = mat_solid("Ceramic tray (housing)", tuple(C["tray"]), 0.46)
M_PANEL_SIDE = mat_solid("Ceramic ivory (edges)", tuple(C["panel"]), 0.40, coat=0.1)
# checkpoint 2: the front return's edge and face, the same ceramic in the tone the editor gives the
# apron's front (sampled at 1040 px), so it falls off from the printed apron as the GUI does
M_SHELL_LOWER = mat_solid("Ceramic ivory (lower shell)", tuple(C["panel"]), 0.44, coat=0.06)
M_RETURN = mat_solid("Ceramic ivory (front return)", tuple(GEO["apron"]["rgb_front"]), 0.44 if args.finish == "refined" else 0.40, coat=0.1) if GEO.get("apron") else None
M_CARD_SIDE = mat_solid("Ceramic card (edges)", tuple(C["card"]), 0.38, coat=0.1)
M_BEZEL = mat_solid("Display bezel", (20, 20, 21), 0.32, spec=0.5, bump=0) if args.finish == "r2" else \
    mat_solid("Display bezel", (20, 20, 21), 0.26, spec=0.5, coat=0.25, bump=0)    # Gate B: a fine highlight on the bezel lip
M_PLINTH = mat_solid("Plinth", (28, 25, 23), 0.5, bump=0)
M_KNOB = mat_solid("Ceramic knob", (245, 241, 233), 0.30, spec=0.45, coat=0.25)
M_COLLAR = mat_solid("Knob collar", (52, 47, 42), 0.5, bump=0)
M_POINTER = mat_solid("Knob indicator", (40, 38, 36), 0.45, bump=0)
M_KEY_SIDE = mat_solid("Black key (satin charcoal)", (30, 29, 28), 0.52, spec=0.30, coat=0.04, bump=0) if args.finish == "r2" else \
    mat_solid("Black key (satin charcoal)", (29, 28, 27), 0.46, spec=0.32, coat=0.10, bump=0)   # Gate B: edge highlights, still satin
M_KEY_TOP = mat_face("White key tops (capture, key plastic)", 0.30, 0.42, 0.12) if args.finish == "r2" else \
    mat_face("White keycaps (capture, satin key plastic)", 0.26, 0.48, 0.18, tint=(1.004, 1.0, 0.994))
M_KEY_WHITE = mat_solid("White key (plastic)", tuple(GEO["colours"].get("white_key", (244, 239, 229))), 0.30, spec=0.45, coat=0.1, bump=0)
M_KEYBED = mat_solid("Keybed floor (felt)", (36, 33, 30), 0.9, spec=0.15, bump=0)
M_TEAK_X = mat_teak("x", 0.13)
M_TEAK_Y = mat_teak("y", 0.61)
M_TEAK_Y2 = mat_teak("y", 0.29)


# --- geometry helpers ------------------------------------------------------------------------
def rr_ring(bm, x0, y0, x1, y1, r, z, seg):
    pts = []
    r = max(r, 1e-5)
    for cx, cy, a0 in [(x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180), (x1 - r, y0 + r, 270)]:
        for i in range(seg + 1):
            a = math.radians(a0 + 90 * i / seg)
            pts.append(bm.verts.new((cx + r * math.cos(a), cy + r * math.sin(a), z)))
    return pts


def slab(name, x0, y0, x1, y1, r, z0, z1, mats, bevel=0.0, seg=8, coll=PRODUCT, cutters=(), bevel_segments=4):
    """Rounded-rectangle slab (metres, product space). `mats` = [top, side, ...]."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    lo = rr_ring(bm, x0, y0, x1, y1, r, z0, seg)
    hi = rr_ring(bm, x0, y0, x1, y1, r, z1, seg)
    bm.faces.new(list(reversed(lo)))
    bm.faces.new(hi)
    n = len(lo)
    for i in range(n):
        bm.faces.new((lo[i], lo[(i + 1) % n], hi[(i + 1) % n], hi[i]))
    bm.to_mesh(me)
    bm.free()
    o = obj(name, me, coll, parent=coll is PRODUCT)
    for m in mats:
        me.materials.append(m)
    for c in cutters:
        b = o.modifiers.new("Well", "BOOLEAN")
        b.operation = "DIFFERENCE"
        b.solver = "EXACT"
        b.object = c
    if bevel > 0:
        b = o.modifiers.new("Bevel", "BEVEL")
        b.width = bevel
        b.segments = bevel_segments
        b.limit_method = "ANGLE"
        b.angle_limit = math.radians(40)
        b.harden_normals = True
        o.modifiers.new("Normals", "WEIGHTED_NORMAL").keep_sharp = True
    return o


def prism_poly(name, pts, z0, z1, mats, bevel=0.0, coll=PRODUCT):
    """A straight prism over a convex polygon (metres, product space, counter-clockwise)."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    lo = [bm.verts.new((x, y, z0)) for x, y in pts]
    hi = [bm.verts.new((x, y, z1)) for x, y in pts]
    bm.faces.new(list(reversed(lo)))
    bm.faces.new(hi)
    n = len(pts)
    for i in range(n):
        bm.faces.new((lo[i], lo[(i + 1) % n], hi[(i + 1) % n], hi[i]))
    bm.to_mesh(me)
    bm.free()
    o = obj(name, me, coll, parent=coll is PRODUCT)
    for m in mats:
        me.materials.append(m)
    if bevel > 0:
        b = o.modifiers.new("Bevel", "BEVEL")
        b.width, b.segments, b.limit_method = bevel, 4, "ANGLE"
        b.angle_limit = math.radians(30)
        b.harden_normals = True
        o.modifiers.new("Normals", "WEIGHTED_NORMAL").keep_sharp = True
    return o


def bake(o, classify):
    """Apply modifiers, then give every polygon a material index from classify(centre, normal)."""
    deps = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(o.evaluated_get(deps), preserve_all_data_layers=True, depsgraph=deps)
    for poly in me.polygons:
        poly.material_index = classify(poly.center, poly.normal)
        poly.use_smooth = poly.normal.z < 0.999
    old = o.data
    o.modifiers.clear()
    o.data = me
    bpy.data.meshes.remove(old)
    return o


def top_side(top_z, eps=0.0002):
    return lambda c, n: 0 if (n.z > 0.9 and c.z > top_z - eps) else 1


# --- layer A: housing ----------------------------------------------------------------------
cx0, cx1 = GEO["frame"]["cheek_x"]
by, fy = GEO["frame"]["back_y"], GEO["frame"]["front_y"]
WOOD_BEVEL = (1.0 if APRON else 0.7) * MM   # Gate B: the cheeks' edges match the ivory's softer radii
# Pass 3: one construction at all four corners: mitred joints (each rail runs to the diagonal
# from the outer corner to the inner corner), so the grain of every rail runs along it to the
# joint and no end grain shows. A fine bevel on every edge gives the joint its hairline.
O = [(ex(0), ey(0)), (ex(EW), ey(0)), (ex(EW), ey(EH)), (ex(0), ey(EH))]          # outer: TL TR BR BL
I = [(ex(cx0), ey(by)), (ex(cx1), ey(by)), (ex(cx1), ey(fy)), (ex(cx0), ey(fy))]  # inner
back = prism_poly("Teak back rail", [I[0], I[1], O[1], O[0]], PLINTH_H, FRAME_TOP, [M_TEAK_X], bevel=WOOD_BEVEL)
if APRON:
    # No front fascia (checkpoint 2). The side cheeks run to the front edge (the editor's 1046 px) and
    # end square there: mitred at the back, end grain at the front, flush with the ivory front return.
    # They stand on the ivory lower shell (front_body), not on a dark plinth.
    left = prism_poly("Teak side rail L", [O[0], O[3], I[3], I[0]], PLINTH_H, FRAME_TOP, [M_TEAK_Y], bevel=WOOD_BEVEL)
    right = prism_poly("Teak side rail R", [I[1], I[2], O[2], O[1]], PLINTH_H, FRAME_TOP, [M_TEAK_Y2], bevel=WOOD_BEVEL)
    RAILS = (back, left, right)
else:
    front = prism_poly("Teak front fascia", [O[3], O[2], I[2], I[3]], PLINTH_H, FRAME_TOP, [M_TEAK_X], bevel=WOOD_BEVEL)
    left = prism_poly("Teak side rail L", [O[0], O[3], I[3], I[0]], PLINTH_H, FRAME_TOP, [M_TEAK_Y], bevel=WOOD_BEVEL)
    right = prism_poly("Teak side rail R", [I[1], I[2], O[2], O[1]], PLINTH_H, FRAME_TOP, [M_TEAK_Y2], bevel=WOOD_BEVEL)
    RAILS = (front, back, left, right)
for o in RAILS:
    bake(o, lambda c, n: 0)
if not APRON:
    tray = slab("Ceramic tray", ex(cx0) - 0.5 * MM, ey(fy) - 0.5 * MM, ex(cx1) + 0.5 * MM, ey(by) + 0.5 * MM, 0, PLINTH_H + 1 * MM, TRAY_TOP, [M_TRAY])
    plinth = slab("Plinth", ex(0) + 3 * MM, ey(EH) + 3 * MM, ex(EW) - 3 * MM, ey(0) - 3 * MM, 4 * MM, 0.0, PLINTH_H + 0.2 * MM, [M_PLINTH], bevel=0.5 * MM)
    bake(plinth, lambda c, n: 0)

def mesh_obj(name, prisms, mats, mark=None, nose=0.0, nose_seg=6, edge=0.0, edge_seg=3, cutters=()):
    """One object from straight prisms over simple polygons (world metres, either winding). Edges
    picked by mark(a, b) (their two vertex positions) get the `nose` round; then every remaining
    sharp edge gets `edge`. Booleans run between the two, so cut edges get the fine round only."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    for pts, z0, z1 in prisms:
        pts = [q for i, q in enumerate(pts) if i == 0 or (abs(q[0] - pts[i - 1][0]) > 1e-9 or abs(q[1] - pts[i - 1][1]) > 1e-9)]
        area = sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1] for i in range(len(pts)))
        if area < 0:
            pts = pts[::-1]
        lo = [bm.verts.new((x, y, z0)) for x, y in pts]
        hi = [bm.verts.new((x, y, z1)) for x, y in pts]
        bm.faces.new(list(reversed(lo)))
        bm.faces.new(hi)
        n = len(pts)
        for i in range(n):
            bm.faces.new((lo[i], lo[(i + 1) % n], hi[(i + 1) % n], hi[i]))
    if mark:
        bw = bm.edges.layers.float.new("bevel_weight_edge")
        for e in bm.edges:
            if mark(e.verts[0].co, e.verts[1].co):
                e[bw] = 1.0
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    o = obj(name, me)
    for m in mats:
        me.materials.append(m)
    if nose > 0:
        b = o.modifiers.new("Nose", "BEVEL")
        b.width, b.segments, b.limit_method, b.profile = nose, nose_seg, "WEIGHT", 0.5
    for c in cutters:
        b = o.modifiers.new("Cut", "BOOLEAN")
        b.operation, b.solver, b.object = "DIFFERENCE", "EXACT", c
    if edge > 0:
        b = o.modifiers.new("Edges", "BEVEL")
        b.width, b.segments, b.limit_method = edge, edge_seg, "ANGLE"
        b.angle_limit = math.radians(40)
        b.harden_normals = True
    o.modifiers.new("Normals", "WEIGHTED_NORMAL").keep_sharp = True
    return o


def rect_px(u0, v0, u1, v1):
    return [(ex(u0), ey(v0)), (ex(u1), ey(v0)), (ex(u1), ey(v1)), (ex(u0), ey(v1))]


SECTION = {}


def front_body():
    """Keyboard + front body round (the trimmed editor). The front, from the top down:
      1 the ivory top shell (the panel): beside the keyboard it runs at full height to the front
        edge (end blocks under PITCH/MOD and ARP, 1.5 mm nose); the keybed opening (146..1300 px)
        is open to the front, where the front drops to the lip;
      2 52 white keys, real solids (3.4 mm bodies, a 0.5 mm nose, 0.24 mm gaps, cut-outs around the
        black keys), tops 0.6 mm below the shell; 36 black keys, tapered, sloped fronts, 2.4 mm higher;
      3 the key fronts over a dark keybed floor: a real shadow slot (1014..1017 px) under them;
      4 the ivory front return in the keyboard section: a ledge 3.6 mm below the shell top, printed with the GUI's apron,
        a 1.5 mm nose and a 5 mm lip; under it the ivory body steps back 2 mm, so the lip casts a
        real shadow (no dark band);
      5 the ivory lower shell the cheeks stand on (5.5 mm), set back with the body at the front, its
        bottom edge tucked under (3 mm radius), on four small dark feet.
    Plan positions are the approved editor's; only heights are designed here."""
    P = PANEL_TOP
    zW = P - args.white_drop * MM
    zWb = zW - args.white_key * MM
    zL = P - args.ledge_drop * MM
    zF = zWb - 0.6 * MM
    zB = zW + args.black_rise * MM
    ov = 0.6 * MM
    WX0, WX1 = ex(cx0) - ov, ex(cx1) + ov
    YF = ey(EH)
    SECTION.update({"shell_top": P, "white_top": zW, "white_bottom": zWb, "ledge_top": zL, "keybed_floor": zF,
                    "black_top": zB, "part_line": PLINTH_H, "feet": FEET_H, "cheek_top": FRAME_TOP})
    zR = zL - args.lip * MM
    SECTION["lip_bottom"] = zR
    # the keybed opening (geometry.keybed 146..1300 from 910) and, in front of it, the keyboard
    # section of the front lowered to the lip
    cut = slab("Keybed cutter", ex(146), ey(1017.5 + KY), ex(1300), ey(910 + KY), 6 * PX, zF - 1 * MM, P + 2 * MM, [M_BEZEL], seg=6)
    notch = slab("Front notch cutter", ex(146), ey(EH + 4), ex(1300), ey(1016 + KY), 0, zR - 1 * MM, P + 2 * MM, [M_BEZEL])
    for c_ in (cut, notch):
        c_.hide_render = c_.hide_viewport = True

    # 1 top shell: rounded back corners (the GUI's 7 px); from the apron's top (1024 px) it widens
    # cheek to cheek and runs to the front edge at full height: the end blocks under PITCH/MOD and
    # ARP, with the front's 1.5 mm nose. The keyboard section is cut down to the lip (the notch).
    r, seg = GEO["panel"]["radius"] * PX, 8
    X0, Y0, X1, YT = ex(x0), ey(y0), ex(x1), ey(APRON["top"])
    pts = [(X0, YT), (WX0, YT), (WX0, YF), (WX1, YF), (WX1, YT), (X1, YT)]
    pts += [(X1 - r + r * math.cos(math.radians(90 * i / seg)), Y0 - r + r * math.sin(math.radians(90 * i / seg))) for i in range(seg + 1)]
    pts += [(X0 + r + r * math.cos(math.radians(90 + 90 * i / seg)), Y0 - r + r * math.sin(math.radians(90 + 90 * i / seg))) for i in range(seg + 1)]
    # the front's top edge and (Gate B) its underside both rounded: the lip reads as the shell's own
    # moulded return, not a plate laid under the keys
    front_edge = lambda p_, q_: (abs(p_.y - YF) < 1e-7 and abs(q_.y - YF) < 1e-7 and
                                 ((p_.z > P - 1e-7 and q_.z > P - 1e-7) or (abs(p_.z - zR) < 1e-7 and abs(q_.z - zR) < 1e-7)))
    panel = mesh_obj("Ceramic panel", [(pts, zR, P)], [M_FACE, M_PANEL_SIDE, M_RETURN], mark=front_edge,
                     nose=args.ledge_round * MM, nose_seg=8, edge=0.6 * MM, edge_seg=3, cutters=[cut, notch])
    bake(panel, lambda c, n: 0 if n.z > 0.9 else (2 if c.y < YT - 1e-4 else 1))
    # the tray band at the sides and back (under the shell's edge)
    tray = slab("Ceramic tray", ex(cx0) - 0.5 * MM, ey(APRON["top"] - 0.1), ex(cx1) + 0.5 * MM, ey(by) + 0.5 * MM, 0,
                PLINTH_H, TRAY_TOP, [M_TRAY], bevel=0.3 * MM, bevel_segments=2, cutters=[cut, notch])
    bake(tray, lambda c, n: 0)

    # 3 keybed floor (dark), under the keys and the slot
    floor = slab("Keybed floor", ex(144), ey(1019 + KY), ex(1302), ey(903 + KY), 0, zF - 0.8 * MM, zF, [M_KEYBED])
    bake(floor, lambda c, n: 0)

    # 2 keys
    KB_ = GEO["keyboard"]["bed"]
    n_white = round((KB_[2] - KB_[0]) / 22.1)
    pitch = (KB_[2] - KB_[0]) / n_white
    g, bg = 0.12 * MM / PX, 0.30 * MM / PX
    blacks = [(k[0], k[2]) for k in GEO["keyboard"]["black"]]
    yc, yb, yf = 975.5 + KY + bg, 906.0 + KY, KB_[3]      # cut-out depth (behind the black keys), key backs (under the shell), fronts
    prisms = []
    for i in range(n_white):
        a, b = KB_[0] + i * pitch + g, KB_[0] + (i + 1) * pitch - g
        lb, rb = a, b
        for bx0, bx1 in blacks:
            if bx0 < a < bx1:
                lb = bx1 + bg
            if bx0 < b < bx1:
                rb = bx0 - bg
        ptsk = [(a, yf), (b, yf), (b, yc), (rb, yc), (rb, yb), (lb, yb), (lb, yc), (a, yc)]
        prisms.append(([(ex(u), ey(v)) for u, v in ptsk], zWb, zW))
    yfw = ey(yf)
    whites = mesh_obj("White keys", prisms, [M_KEY_TOP, M_KEY_WHITE],
                      mark=lambda p_, q_: abs(p_.y - yfw) < 1e-7 and abs(q_.y - yfw) < 1e-7 and p_.z > zW - 1e-7 and q_.z > zW - 1e-7,
                      nose=0.5 * MM, nose_seg=4, edge=0.15 * MM, edge_seg=2)
    bake(whites, lambda c, n: 0 if n.z > 0.9 else 1)
    me = bpy.data.meshes.new("Black keys")
    bm = bmesh.new()
    ti, fi = 0.22 * MM, 1.1 * MM
    for bx0, bx1 in blacks:
        X0b, X1b, Yback, Yfront = ex(bx0), ex(bx1), ey(yb), ey(975.5 + KY)
        lo = [bm.verts.new(v) for v in ((X0b, Yfront, zF), (X1b, Yfront, zF), (X1b, Yback, zF), (X0b, Yback, zF))]
        hi = [bm.verts.new(v) for v in ((X0b + ti, Yfront + fi, zB), (X1b - ti, Yfront + fi, zB), (X1b - ti, Yback, zB), (X0b + ti, Yback, zB))]
        bm.faces.new(list(reversed(lo)))
        bm.faces.new(hi)
        for i in range(4):
            bm.faces.new((lo[i], lo[(i + 1) % 4], hi[(i + 1) % 4], hi[i]))
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    bk = obj("Black keys", me)
    me.materials.append(M_KEY_SIDE)
    bv = bk.modifiers.new("Edges", "BEVEL")
    bv.width, bv.segments, bv.limit_method, bv.harden_normals = 0.32 * MM, 4, "ANGLE", True
    bv.angle_limit = math.radians(30)
    bk.modifiers.new("Normals", "WEIGHTED_NORMAL").keep_sharp = True
    bake(bk, lambda c, n: 0)

    # 4 the front return: cheek to cheek, from the slot to the front edge, printed with the apron
    ledge = mesh_obj("Front return", [(rect_px(146, 1017 + KY, 1300, EH), zR, zL)], [M_FACE, M_RETURN],
                     mark=lambda p_, q_: (abs(p_.y - YF) < 1e-7 and abs(q_.y - YF) < 1e-7 and
                                          ((p_.z > zL - 1e-7 and q_.z > zL - 1e-7) or (abs(p_.z - zR) < 1e-7 and abs(q_.z - zR) < 1e-7))),
                     nose=args.ledge_round * MM, nose_seg=8, edge=0.4 * MM, edge_seg=3)
    # run 0.3 mm into the end blocks (hidden), so the joint is a fine line, never a gap
    for v in ledge.data.vertices:
        if abs(v.co.x - ex(146)) < 1e-7:
            v.co.x -= 0.3 * MM
        elif abs(v.co.x - ex(1300)) < 1e-7:
            v.co.x += 0.3 * MM
    bake(ledge, lambda c, n: 0 if n.z > 0.9 else 1)

    # under the lip, the body steps back (`--front-setback`): the lip's shadow is real construction
    sb = args.front_setback * MM / PX
    body = slab("Front body", WX0, ey(EH - sb - 0.02 * MM / PX), WX1, ey(990 + KY), 0, PLINTH_H - 0.3 * MM, zR + 0.3 * MM, [M_SHELL_LOWER],
                bevel=0.3 * MM, bevel_segments=2)
    bake(body, lambda c, n: 0)

    # 5 lower shell: the full footprint, 0.4 mm inside the cheeks' faces (a fine shadow under the wood)
    # and stepped back at the front with the body; its bottom edge tucked under; four feet
    zS = FEET_H
    shell = mesh_obj("Lower shell", [(rect_px(0 + 0.4 * MM / PX, 0 + 0.4 * MM / PX, EW - 0.4 * MM / PX, EH - sb), zS, PLINTH_H)], [M_SHELL_LOWER],
                     mark=lambda p_, q_: abs(p_.z - zS) < 1e-7 and abs(q_.z - zS) < 1e-7,
                     nose=args.undercut * MM, nose_seg=10, edge=0.4 * MM, edge_seg=3)
    bake(shell, lambda c, n: 0)
    fr = 18
    for j, (u, v) in enumerate(((70, 70), (EW - 70, 70), (70, EH - 70), (EW - 70, EH - 70))):
        f = slab(f"Foot {j}", ex(u - fr), ey(v + fr), ex(u + fr), ey(v - fr), fr * PX, 0.0, zS + 0.4 * MM, [M_PLINTH], seg=8)
        bake(f, lambda c, n: 0)
    bpy.data.objects.remove(cut)
    bpy.data.objects.remove(notch)
    return panel


# --- layer B: ceramic panel and cards; layer C: display wells -------------------------------
x0, y0, x1, y1 = GEO["panel"]["rect"]
KB = GEO["keyboard"]["bed"]
if APRON:
    panel = front_body()
else:
    KB_POCKET = (146, 910, 1300, 1016)       # geometry.keybed (146, 910, 1154, 106), radius 6
    kb_cut = slab("Keybed cutter", ex(KB_POCKET[0]), ey(KB_POCKET[3]), ex(KB_POCKET[2]), ey(KB_POCKET[1]), 6 * PX,
                  KEYBED_TOP, PANEL_TOP + 2 * MM, [M_BEZEL], seg=6)
    kb_cut.hide_render = kb_cut.hide_viewport = True
    panel = slab("Ceramic panel", ex(x0), ey(y1), ex(x1), ey(y0), GEO["panel"]["radius"] * PX, TRAY_TOP - 0.5 * MM, PANEL_TOP,
                 [M_FACE, M_PANEL_SIDE], bevel=0.45 * MM, seg=8, cutters=[kb_cut], bevel_segments=3)
    # the pocket's floor carries the white keys (the capture); its walls are ceramic
    bake(panel, lambda c, n: 0 if n.z > 0.9 else 1)
    bpy.data.objects.remove(kb_cut)


def inside(x, y, rect, pad=0.0):
    return rect[0] - pad <= x <= rect[2] + pad and rect[1] - pad <= y <= rect[3] + pad


DISPLAYS = GEO["displays"]
for i, cd in enumerate(GEO["cards"]):
    a0, b0, a1, b1 = cd["rect"]
    macro_panel = (b1 - b0) < 300
    height, bevel, radius = (MPANEL_H, MPANEL_BEVEL, 13) if macro_panel else (CARD_H, CARD_BEVEL, 16)
    top_z = PANEL_TOP + height
    wells = [d for d in DISPLAYS if inside(d[0], d[1], cd["rect"]) and inside(d[2], d[3], cd["rect"])]
    depth = min(WELL, height - 0.08 * MM)    # the floor always stays above the housing
    cutters = []
    for j, d in enumerate(wells):
        g = WELL_GROW
        wr = 12 if macro_panel else 9          # bezel-r12 (envelope), bezel-r9 (sources)
        c = slab(f"Well cutter {i}.{j}", ex(d[0] - g), ey(d[3] + g), ex(d[2] + g), ey(d[1] - g), (wr + g) * PX,
                 top_z - depth, top_z + 2 * MM, [M_BEZEL], seg=8)
        c.hide_render = True
        c.hide_viewport = True
        cutters.append(c)
    card = slab(f"Ceramic card {i}", ex(a0), ey(b1), ex(a1), ey(b0), radius * PX, PANEL_TOP - 0.3 * MM, top_z,
                [M_FACE_MPANEL if macro_panel else M_FACE_CARD, M_CARD_SIDE, M_DISPLAY, M_BEZEL], bevel=min(bevel, WELL_LIP), seg=10, cutters=cutters, bevel_segments=3)

    def classify(c, n, wells=wells, top_z=top_z):
        u, v = c.x / PX + EW / 2, CY - c.y / PX
        in_well = any(inside(u, v, d, WELL_GROW + 0.5) for d in wells)
        if n.z > 0.9:
            if c.z > top_z - 0.0002:
                return 0
            return 2 if in_well else 0
        return 3 if in_well else 1
    bake(card, classify)
    for c in cutters:
        bpy.data.objects.remove(c)


def surface_top(u, v):
    for cd in GEO["cards"]:
        if inside(u, v, cd["rect"]):
            return PANEL_TOP + (MPANEL_H if (cd["rect"][3] - cd["rect"][1]) < 300 else CARD_H)
    return PANEL_TOP


# --- layer D: buttons, keys, knobs -----------------------------------------------------------
# A1 keys (controls.json buttons.A1, emphasis): height 0.8 mm x the role's emphasis; corner
# radius per role (skin/pieces.py). Bevel kept to a third of the height so the top stays planar.
EMPH = {"ModifierButton": 1.0, "ModeSelector": 0.8, "EqButton": 0.8, "RecButton": 0.8, "LayerStateButton": 0.6,
        "AdvancedCardButton": 0.8, "ArpControl": 0.9, "PresetBar": 0.7, "SavePresetButton": 0.7}
RADIUS = {"ModifierButton": 7, "ModeSelector": 7, "EqButton": 6, "RecButton": 6, "LayerStateButton": 4,
          "AdvancedCardButton": 7, "ArpControl": 10, "PresetBar": 12, "SavePresetButton": 10}
SIDE_MATS = {}
for i, bt in enumerate(GEO["buttons"]):
    a0, b0, a1, b1 = bt["rect"]
    base = surface_top((a0 + a1) / 2, (b0 + b1) / 2)
    h = 0.8 * EMPH.get(bt["kind"], 0.8) * DMM
    top = base + h
    key = tuple(bt["side_rgb"])
    if key not in SIDE_MATS:
        SIDE_MATS[key] = mat_solid(f"Key side {len(SIDE_MATS)}", key, 0.36)
    o = slab(f"Key {bt['kind']} {i}", ex(a0), ey(b1), ex(a1), ey(b0), RADIUS.get(bt["kind"], bt["radius"]) * PX, base - 0.2 * MM, top,
             [M_FACE_BUTTON, SIDE_MATS[key]], bevel=0.34 * h, seg=8, bevel_segments=3)
    bake(o, top_side(top))

# Layer badges A/B/C (skin/pieces.py): 47 x 43 px, radius 7, 0.7 mm, on each card.
BADGE = {"A": (196, 86, 31), "B": (77, 97, 118), "C": (100, 128, 107)}
for j, (letter, rgb) in enumerate(BADGE.items()):
    a0, b0 = 56 + 463 * j, 137
    h = 0.7 * DMM
    top = PANEL_TOP + CARD_H + h
    o = slab(f"Badge {letter}", ex(a0), ey(b0 + 43), ex(a0 + 47), ey(b0), 7 * PX, PANEL_TOP + CARD_H - 0.2 * MM, top,
             [M_FACE_BUTTON, mat_solid(f"Badge {letter} side", rgb, 0.4)], bevel=0.34 * h, seg=6, bevel_segments=3)
    bake(o, top_side(top))

# Lens LEDs under the macros (geometry.macroLedY 883, diameter 9 px, 0.75 mm): CHARACTER lit.
MACRO_LED = [(128, (190, 117, 82)), (274, (181, 132, 58)), (420, (123, 141, 105)), (566, (115, 137, 156)), (712, (92, 148, 144)), (858, (150, 131, 157))]
for j, (x, rgb) in enumerate(MACRO_LED):
    nt = NT(f"Lens LED {j}")
    b = nt.principled()
    b.inputs["Base Color"].default_value = (*lin(rgb), 1)
    b.inputs["Roughness"].default_value = 0.12
    b.inputs["Coat Weight"].default_value = 1.0
    b.inputs["Coat Roughness"].default_value = 0.02
    b.inputs["Subsurface Weight"].default_value = 0.3
    if j == 2:
        b.inputs["Emission Color"].default_value = (*lin(rgb), 1)
        b.inputs["Emission Strength"].default_value = 1.2
    m = nt.finish(b.outputs[0])
    r = 4.5 * PX
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=1)
    led = bpy.context.active_object
    bpy.context.scene.collection.objects.unlink(led)
    PRODUCT.objects.link(led)
    led.name = f"Lens LED {j}"
    led.parent = root
    led.scale = (r, r, 0.75 * DMM)
    led.location = (ex(x), ey(883 + KY), PANEL_TOP + MPANEL_H - 0.75 * DMM * 0.15)
    led.data.materials.append(m)
    for poly in led.data.polygons:
        poly.use_smooth = True

# C1 ivory fader cap (14 x 22 px, radius 4, 2.0 mm, three grip grooves) on the T3 metal guide.
rail_m = mat_solid("T3 guide (warm brushed aluminium)", (149, 140, 126), 0.4, spec=0.5, metal=1.0, bump=0)
o = slab("T3 guide", ex(1155.4), ey(81.6), ex(1338.6), ey(78.4), 1.6 * PX, PANEL_TOP - 0.2 * MM, PANEL_TOP + 0.6 * DMM, [rail_m], bevel=0.2 * DMM, seg=4)
bake(o, lambda c, n: 0)
fh = 2.0 * DMM
o = slab("C1 fader cap", ex(1285), ey(91), ex(1299), ey(69), 4 * PX, PANEL_TOP, PANEL_TOP + fh, [M_FACE, M_KNOB], bevel=0.3 * fh, seg=6, bevel_segments=3)
bake(o, top_side(PANEL_TOP + fh))

kb = GEO["keyboard"]
for i, k in enumerate([] if APRON else kb["black"]):   # the trimmed editor's keys are built in front_body()
    o = slab(f"Black key {i:02d}", ex(k[0]), ey(k[3]), ex(k[2]), ey(k[1]), 1.6 * PX, KEYBED_TOP - 0.2 * MM, BLACK_TOP,
             [M_FACE, M_KEY_SIDE], bevel=0.45 * MM, seg=3)
    bake(o, top_side(BLACK_TOP))


def knob(name, u, v, r_px, angle, kind):
    """A ceramic knob revolved from a profile: a dark collar (base clearance), a cylindrical skirt
    (knurled on the macros, as the editor draws them), a rounded rim, a shallow domed top, and an
    inlaid charcoal indicator at the editor's own pointer angle."""
    r = r_px * PX
    h = KNOB_H[kind]
    z0 = surface_top(u, v)
    dome = 0.55 * MM if kind == "macro" else 0.35 * MM
    prof = [  # (radius, height, material, knurl)
        (0.80 * r, 0.0, 1, False), (0.80 * r, 0.45 * MM, 1, False),
        (0.965 * r, 0.45 * MM, 0, False), (r, 0.9 * MM, 0, True), (r, h - 1.5 * MM, 0, True),
        (0.988 * r, h - 0.75 * MM, 0, True), (0.955 * r, h - 0.25 * MM, 0, False), (0.915 * r, h, 0, False)]
    if kind != "macro":   # the drawn groove between rim and cap
        prof += [(0.885 * r, h - 0.22 * MM, 0, False), (0.86 * r, h - 0.04 * MM, 0, False)]
    prof += [(0.62 * r, h + dome * 0.55, 0, False), (0.32 * r, h + dome * 0.9, 0, False), (0.0, h + dome, 0, False)]
    seg = 192 if kind == "macro" else 112
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    rings = []
    for j in range(seg):
        a = 2 * math.pi * j / seg
        k = 1.0 + 0.011 * max(-0.55, math.cos(48 * a)) if kind == "macro" else 1.0
        ring = []
        for (pr, pz, _, knurl) in prof[:-1]:
            kk = k if knurl else 1.0
            ring.append(bm.verts.new((pr * kk * math.cos(a), pr * kk * math.sin(a), pz)))
        rings.append(ring)
    apex = bm.verts.new((0, 0, prof[-1][1]))
    faces_mat = []
    for j in range(seg):
        ra, rb = rings[j], rings[(j + 1) % seg]
        for i in range(len(prof) - 2):
            f = bm.faces.new((ra[i], rb[i], rb[i + 1], ra[i + 1]))
            f.material_index = prof[i][2]
        f = bm.faces.new((ra[-1], rb[-1], apex))
        f.material_index = 0
    bm.faces.new([rings[j][0] for j in reversed(range(seg))]).material_index = 1
    bm.to_mesh(me)
    bm.free()
    for poly in me.polygons:
        poly.use_smooth = True
    o = obj(name, me)
    o.location = (ex(u), ey(v), z0)
    for m in (M_KNOB, M_COLLAR):
        me.materials.append(m)
    # Indicator: a strip following the dome, from 0.33 r to 0.80 r, at the editor's angle
    # (clockwise from 12 o'clock on screen = from +y towards +x here).
    def dome_z(rad):
        top = [(pr, pz) for (pr, pz, _, _) in prof if pz >= h - 0.3 * MM]
        top.sort()
        for (ra_, za), (rb_, zb) in zip(top, top[1:]):
            if ra_ <= rad <= rb_:
                return za + (zb - za) * (rad - ra_) / max(rb_ - ra_, 1e-9)
        return h
    w = (0.085 if kind == "macro" else 0.095) * r
    dx, dy = math.sin(angle), math.cos(angle)
    px_, py_ = dy, -dx
    pm = bpy.data.meshes.new(name + " indicator")
    bm = bmesh.new()
    steps = 8
    tops, bots = [], []
    for s in range(steps + 1):
        rad = r * (0.33 + (0.80 - 0.33) * s / steps)
        z = dome_z(rad) + 0.035 * MM
        for side in (-1, 1):
            x, y = dx * rad + px_ * side * w / 2, dy * rad + py_ * side * w / 2
            tops.append(bm.verts.new((x, y, z)))
            bots.append(bm.verts.new((x, y, z - 0.2 * MM)))
    for s in range(steps):
        a, b2, c2, d = 2 * s, 2 * s + 1, 2 * s + 3, 2 * s + 2
        bm.faces.new((tops[a], tops[b2], tops[c2], tops[d]))
        bm.faces.new((bots[d], bots[c2], bots[b2], bots[a]))
        bm.faces.new((bots[a], tops[a], tops[d], bots[d]))
        bm.faces.new((bots[c2], tops[c2], tops[b2], bots[b2]))
    bm.faces.new((bots[0], bots[1], tops[1], tops[0]))
    bm.faces.new((tops[-2], tops[-1], bots[-1], bots[-2]))
    bm.to_mesh(pm)
    bm.free()
    po = obj(name + " indicator", pm)
    po.location = o.location
    pm.materials.append(M_POINTER)
    return o


# --- the approved knob (tools/blender-assets: config/macro-knob.json, osp_assets/geometry.py) -
# Pass 3 builds every knob from OSP's own generator, the same profile the editor's knob sprites
# are rendered from (all knobs in the editor use that one cap, scaled). Variants for Test 01:
#   approved        the spec: 28 mm, 11 mm high (0.39 D), 7 % taper, 1.5 mm bevel, 0.55 mm crown,
#                   48 flutes 0.42 mm deep, 1.4 mm base ring behind a 0.35 mm groove
#   shallow         the same profile at 0.15 D (4.2 mm), 0.7 mm bevel, 0.25 mm crown, no flutes
#   shallow_fluted  as shallow, with 48 fine flutes 0.15 mm deep (0.005 D)
#   mid             the approved profile at 0.25 D (7 mm), 0.9 mm bevel, flutes 0.26 mm (0.009 D)
import copy
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../../tools/blender-assets")))
from osp_assets import geometry as knobgeo   # noqa: E402

APPROVED = json.load(open(os.path.join(os.path.dirname(__file__), "../../../../tools/blender-assets/config/macro-knob.json")))
KG = copy.deepcopy(APPROVED["geometry"])
if args.knob in ("shallow", "shallow_fluted"):
    KG.update(height=4.2, bevelWidth=0.7, topCurvature=0.25, bevelSegments=8)
    KG["skirt"] = dict(KG["skirt"], height=0.9, gap=0.3)
    KG["flutes"] = dict(KG["flutes"], depth=0.15, fadeIn=0.6, topFade=0.4) if args.knob == "shallow_fluted" else dict(KG["flutes"], count=0, depth=0.0)
elif args.knob == "hd":
    # Pass 3.5: the official profile, regenerated with only the total height changed (mounting
    # plane to the crown's top, base ring included: the generator's own convention), and the
    # finer flutes of round 1 E. Diameter, taper, crown, bevel, edge round, base ring, groove
    # and the indicator stay official.
    KG["height"] = args.hd * KG["diameter"]
    KG["flutes"] = dict(KG["flutes"], depth=args.flute_depth)
elif args.knob == "mid":
    # The approved profile at 0.25 D (7 mm): between the spec (0.39 D) and the brief (0.11-0.17 D);
    # a crisper 0.9 mm bevel, a 0.4 mm crown, flutes 0.009 D.
    KG.update(height=7.0, bevelWidth=0.9, topCurvature=0.4, bevelSegments=8)
    KG["skirt"] = dict(KG["skirt"], height=1.1, gap=0.3)
    KG["flutes"] = dict(KG["flutes"], depth=0.26, fadeIn=0.9, topFade=0.45)
IND = APPROVED["indicator"]
_, KINFO = knobgeo.knob_profile(KG)


def mat_knob():
    """Satin Ivory (materials/ivory.json), with the engraved charcoal indicator drawn exactly on
    the cap in its own space (mm; +Y is the pointer), so it is flush, crisp and never floats."""
    nt = NT("Knob satin ivory (approved)")
    tc = nt.n("ShaderNodeTexCoord")
    p = nt.n("ShaderNodeSeparateXYZ")
    nt.link(tc.outputs["Object"], p.inputs[0])
    halfw = 0.5 * IND["width"]
    r0, r1 = IND["from"] * KINFO["rt"], KINFO["rt"] - 0.65 * KG["bevelWidth"]
    inx = nt.math("LESS_THAN", nt.math("ABSOLUTE", p.outputs[0]), halfw)
    iny = nt.math("MULTIPLY", nt.math("GREATER_THAN", p.outputs[1], r0), nt.math("LESS_THAN", p.outputs[1], r1))
    onz = nt.math("GREATER_THAN", p.outputs[2], KINFO["edgeZ"] - 0.2)
    mask = nt.math("MULTIPLY", nt.math("MULTIPLY", inx, iny), onz)
    b = nt.principled()
    col = nt.n("ShaderNodeMix")
    col.data_type = "RGBA"
    nt.link(mask, col.inputs[0])
    col.inputs[6].default_value = (*lin((236, 232, 224)), 1)     # #ece8e0 satin-ivory
    col.inputs[7].default_value = (*lin((38, 35, 31)), 1)        # #26231f charcoal ink
    nt.link(col.outputs[2], b.inputs["Base Color"])
    rough = nt.math("MULTIPLY_ADD", mask, 0.72 - 0.34, 0.34)
    nt.link(rough, b.inputs["Roughness"])
    b.inputs["Specular IOR Level"].default_value = 0.5
    coat = nt.math("MULTIPLY_ADD", mask, -0.18, 0.18)
    nt.link(coat, b.inputs["Coat Weight"])
    b.inputs["Coat Roughness"].default_value = 0.22
    # engraved: the line sits a hair below the surface (bump only; depth 0.22 mm in the spec)
    noise = nt.n("ShaderNodeTexNoise", Scale=900.0, Detail=2.0)
    nt.link(tc.outputs["Object"], noise.inputs["Vector"])
    h = nt.math("MULTIPLY_ADD", noise.outputs["Fac"], 0.02, nt.math("MULTIPLY", mask, -1.0))
    bump = nt.n("ShaderNodeBump", Strength=0.6, Distance=0.0002)
    nt.link(h, bump.inputs["Height"])
    nt.link(bump.outputs[0], b.inputs["Normal"])
    return nt.finish(b.outputs[0])


def approved_knob(name, u, v, r_px, angle, samples):
    g = dict(KG, angularSamples=samples)
    verts, faces, info = knobgeo.build_knob_mesh(g)
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    for poly in me.polygons:
        poly.use_smooth = True
    o = obj(name, me)
    s = (r_px * PX) / info["outer"]            # the editor's body radius = the base ring's outer radius
    o.scale = (s, s, s)
    o.location = (ex(u), ey(v), surface_top(u, v))
    o.rotation_euler = (0, 0, -angle)          # editor angle: clockwise from 12 o'clock; +Y is the pointer
    me.materials.append(M_KNOB_APPROVED)
    KNOB_LOG.append((o, u, v, r_px, angle, verts, info, s))
    return o


KNOB_LOG = []


if args.knob == "pass2":
    for i, k in enumerate(GEO["knobs"]):
        knob(f"Knob {k['kind']} {i:02d}", k["c"][0], k["c"][1], k["r"], k["angle"], k["kind"])
else:
    M_KNOB_APPROVED = mat_knob()
    for i, k in enumerate(GEO["knobs"]):
        approved_knob(f"Knob {k['kind']} {i:02d}", k["c"][0], k["c"][1], k["r"], k["angle"], 1440 if k["kind"] == "macro" else 480)

def flute_count(verts, info):
    """Ridges counted on the ring of the profile nearest mid-sidewall (local maxima of radius)."""
    zmid = info["zb"] + 0.5 * (info["edgeZ"] - info["zb"])
    zs = sorted({round(v[2], 6) for v in verts})
    z = min(zs, key=lambda q: abs(q - zmid))
    ring = [v for v in verts if abs(v[2] - z) < 1e-6]
    ring.sort(key=lambda v: math.atan2(v[1], v[0]))
    rad = [math.hypot(v[0], v[1]) for v in ring]
    n = len(rad)
    return sum(1 for i in range(n) if rad[i] > rad[i - 1] and rad[i] >= rad[(i + 1) % n]) if n else 0


def geometry_report(path):
    knobs = []
    macro_centres = [(k[1], k[2]) for k in KNOB_LOG if k[3] > 40]
    for o, u, v, r_px, angle, verts, info, s in KNOB_LOG:
        zmax = max(q[2] for q in verts)
        rmax = max(math.hypot(q[0], q[1]) for q in verts)
        kind = "macro" if r_px > 40 else ("layer" if r_px > 25 else "mini")
        mount = surface_top(u, v)
        entry = {
            "kind": kind, "centre_px": [u, v], "pointer_angle_rad": angle,
            "design_mm": {"cap_diameter_D": 2 * info["rb"], "overall_height": round(zmax, 4), "H_over_D": round(zmax / (2 * info["rb"]), 4),
                          "upper_face_radius": round(info["rt"], 4), "base_ring_diameter": round(2 * rmax, 4),
                          "bevel_width": KG["bevelWidth"], "crown": KG["topCurvature"], "edge_round": KG.get("edgeRound"),
                          "groove_top_z": round(info["zb"], 4), "flute_depth": KG["flutes"]["depth"]},
            "flutes_counted": flute_count(verts, info) if KG["flutes"]["count"] else 0,
            "scale_scene_per_design": round(s, 6),
            "scene_mm": {"cap_diameter": round(2 * info["rb"] * s * 1000, 3), "overall_height": round(zmax * s * 1000, 3),
                         "base_ring_diameter": round(2 * rmax * s * 1000, 3)},
            "mounting_plane_m": round(mount, 6), "object_base_m": round(o.location.z, 6),
            "base_gap_mm": round((o.location.z - mount) * 1000, 4),
            "bbox_product_mm": {"x": [round((ex(u) - rmax * s) * 1000, 2), round((ex(u) + rmax * s) * 1000, 2)],
                                "y": [round((ey(v) - rmax * s) * 1000, 2), round((ey(v) + rmax * s) * 1000, 2)],
                                "z": [round(mount * 1000, 3), round((mount + zmax * s) * 1000, 3)]},
        }
        if kind == "macro":
            ring_px = rmax * s / PX
            others = [c for c in macro_centres if c != (u, v)]
            entry["clearance_px"] = {
                "to_neighbour_knob": round(min(math.hypot(c[0] - u, c[1] - v) for c in others) - 2 * ring_px, 2),
                "to_lens_led": round(abs(883 - v) - ring_px - 4.5, 2),
                "to_macro_panel_bottom_edge": round(896.5 - (v + ring_px), 2),
            }
        knobs.append(entry)
    rails = {}
    for o in RAILS:
        name = o.name
        zs = [vv.co.z for vv in o.data.vertices]
        rails[name] = {"z_min_mm": round(min(zs) * 1000, 3), "z_max_mm": round(max(zs) * 1000, 3)}
    macro = [k for k in knobs if k["kind"] == "macro"]
    checks = {
        "six_macros": len(macro) == 6,
        "macro_diameter_identical": len({k["design_mm"]["cap_diameter_D"] for k in macro}) == 1,
        "macro_height_identical": len({k["design_mm"]["overall_height"] for k in macro}) == 1,
        "all_knobs_seated_on_their_surface": all(abs(k["base_gap_mm"]) < 1e-6 for k in knobs),
        "flutes_48": all(k["flutes_counted"] in (0, 48) for k in knobs),
        "rails_same_height": len({(r["z_min_mm"], r["z_max_mm"]) for r in rails.values()}) == 1,
        "macro_clearances_positive": all(min(k["clearance_px"].values()) > 0 for k in macro),
    }
    if SECTION:
        checks["keys_above_ledge"] = SECTION["white_top"] > SECTION["ledge_top"] > SECTION["white_bottom"] > SECTION["keybed_floor"]
        checks["black_above_white"] = SECTION["black_top"] > SECTION["white_top"]
    rep_ = {"variant": args.knob, "section_mm": {k: round(v * 1000, 3) for k, v in SECTION.items()}, "hd": args.hd if args.knob == "hd" else None, "frame_h_mm": args.frame_h,
            "knob_config_design_mm": KG, "checks": checks, "rails": rails, "knobs": knobs}
    json.dump(rep_, open(path, "w"), indent=1)
    print("report", path, checks)


if args.report and args.knob != "pass2":
    geometry_report(args.report)

if CLAY:
    for o in PRODUCT.objects:
        if o.type == "MESH":
            o.data.materials.clear()
            o.data.materials.append(CLAY)

# --- C2 test: physical PITCH / MOD wheels (from the approved GUI) ----------------------------
def wheel_solid(name, u0, u1, v_c, zc, R, a0, a1, mat, seg=72):
    """A wheel (or an arc of one) turning about the x axis: outer surface plus side caps.
    u0/u1 editor px across, v_c editor px of the axle, zc the axle height, angles from +z toward the front."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    full = abs((a1 - a0) - 2 * math.pi) < 1e-6
    n = seg if full else max(4, int(seg * (a1 - a0) / (2 * math.pi)) + 1)
    rings = []
    for x in (ex(u0), ex(u1)):
        ring = []
        for i in range(n):
            a = a0 + (a1 - a0) * i / (n if full else n - 1)
            ring.append(bm.verts.new((x, ey(v_c) - R * math.sin(a), zc + R * math.cos(a))))
        rings.append(ring)
    L, Rr = rings
    m = n if full else n - 1
    for i in range(m):
        j = (i + 1) % n
        bm.faces.new((L[i], L[j], Rr[j], Rr[i]))
    if full:
        bm.faces.new(list(reversed(L)))
        bm.faces.new(Rr)
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    o = obj(name, me)
    me.materials.append(mat)
    for poly in me.polygons:
        poly.use_smooth = True
    return o


def strip_material(k):
    """The touch surface: satin charcoal (a capacitive sensor under a satin polymer / etched-glass
    finish), toned from the GUI's own unlit strip: warm charcoal for PITCH, slate for MOD."""
    rgb = (40, 38, 36) if k == 0 else (33, 38, 43)
    nt = NT(f"Touch strip {('PITCH', 'MOD')[k]} (satin charcoal)")
    b = nt.principled()
    b.inputs["Base Color"].default_value = (*lin(rgb), 1)
    b.inputs["Metallic"].default_value = 0.0
    b.inputs["Specular IOR Level"].default_value = 0.42
    b.inputs["Coat Weight"].default_value = 0.18
    b.inputs["Coat Roughness"].default_value = 0.26
    tc = nt.n("ShaderNodeTexCoord")
    noise = nt.n("ShaderNodeTexNoise", Scale=3000.0, Detail=2.0)
    nt.link(tc.outputs["Object"], noise.inputs["Vector"])
    r = nt.n("ShaderNodeMapRange")
    r.inputs["To Min"].default_value, r.inputs["To Max"].default_value = 0.36, 0.40      # very fine micro-roughness
    nt.link(noise.outputs["Fac"], r.inputs["Value"])
    nt.link(r.outputs[0], b.inputs["Roughness"])
    return nt.finish(b.outputs[0])


def strip_light_material(k):
    """The value indication, lit within the surface: the GUI's own pixels where they are orange
    (PITCH) or blue (MOD), added over the transparent strip so the surface's highlight stays. The
    shape, width, gradient, rounded ends and value are the capture's; nothing is drawn here."""
    nt = NT(f"Touch strip light {('PITCH', 'MOD')[k]}")
    _, uv = face_uv(nt)
    tex = nt.n("ShaderNodeTexImage")
    tex.image, tex.interpolation, tex.extension = FACE_IMG, "Cubic", "EXTEND"
    nt.link(uv, tex.inputs[0])
    sep = nt.n("ShaderNodeSeparateColor")
    nt.link(tex.outputs["Color"], sep.inputs[0])
    warm = nt.math("SUBTRACT", sep.outputs[0], sep.outputs[2])
    cool = nt.math("SUBTRACT", sep.outputs[2], sep.outputs[0])
    m1 = nt.math("MULTIPLY_ADD", warm, 4.0, -0.6)
    m2 = nt.math("MULTIPLY_ADD", cool, 4.0, -0.6)
    mask = nt.n("ShaderNodeClamp")
    nt.link(nt.math("MAXIMUM", m1, m2), mask.inputs[0])
    em = nt.n("ShaderNodeEmission")
    nt.link(tex.outputs["Color"], em.inputs["Color"])
    nt.link(nt.math("MULTIPLY", mask.outputs[0], 1.6), em.inputs["Strength"])
    tr = nt.n("ShaderNodeBsdfTransparent")
    add = nt.n("ShaderNodeAddShader")
    nt.link(tr.outputs[0], add.inputs[0])
    nt.link(em.outputs[0], add.inputs[1])
    return nt.finish(add.outputs[0])


def crowned_bar(name, X0, X1, Y0, Y1, z_bot, z_edge, crown, nx=48):
    """A bar whose top is a gentle convex arc across its width, the same along its whole length."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    w = X1 - X0
    top0, top1, bot0, bot1 = [], [], [], []
    for i in range(nx + 1):
        x = X0 + w * i / nx
        z = z_edge + crown * (1.0 - (2.0 * i / nx - 1.0) ** 2)
        top0.append(bm.verts.new((x, Y0, z))); top1.append(bm.verts.new((x, Y1, z)))
        bot0.append(bm.verts.new((x, Y0, z_bot))); bot1.append(bm.verts.new((x, Y1, z_bot)))
    for i in range(nx):
        bm.faces.new((top0[i], top0[i + 1], top1[i + 1], top1[i]))
        bm.faces.new((bot1[i], bot1[i + 1], bot0[i + 1], bot0[i]))
        bm.faces.new((bot0[i], bot0[i + 1], top0[i + 1], top0[i]))
        bm.faces.new((top1[i], top1[i + 1], bot1[i + 1], bot1[i]))
    bm.faces.new((bot0[0], top0[0], top1[0], bot1[0]))
    bm.faces.new((bot1[-1], top1[-1], top0[-1], bot0[-1]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    PRODUCT.objects.link(o)
    o.parent = root
    return o


def touch_strip(k, sl, P_, BZ_H, depth):
    """C2.5 touch strip: one continuous, full-height sensor filling the bezel's opening (the slot),
    rounded in plan like the GUI's strip, with a convex crown across its width along its whole length
    (args.strip_crown x width; 0 = flat). Its crown sits just under the bezel's top. Three separate
    objects, as the GUI's future layers: the bezel (static, built by the caller), the surface
    (static) and the illumination (dynamic: the capture's value)."""
    inset = 0.03 * MM                         # a hairline seam to the bezel's inner wall
    X0, X1 = ex(sl[0]) + inset, ex(sl[2]) - inset
    Y0, Y1 = sorted((ey(sl[1]), ey(sl[3])))
    Y0, Y1 = Y0 + inset, Y1 - inset
    crown = args.strip_crown * (X1 - X0)
    peak = P_ + BZ_H - 0.06 * MM
    edge = peak - crown
    zb = P_ - depth + 0.02 * MM
    names = ("PITCH", "MOD")
    bar = crowned_bar(f"Touch strip {names[k]}", X0, X1, Y0, Y1, zb, edge, crown)
    plan = slab(f"Touch strip plan {k}", X0, Y0, X1, Y1, 5 * PX - inset, zb - 1 * MM, peak + 1 * MM, [M_SLOT], seg=10)
    plan.hide_render = plan.hide_viewport = True
    bo = bar.modifiers.new("Plan", "BOOLEAN")
    bo.operation, bo.solver, bo.object = "INTERSECT", "EXACT", plan
    bv = bar.modifiers.new("Soft edge", "BEVEL")
    bv.limit_method, bv.angle_limit, bv.width, bv.segments = "ANGLE", math.radians(30), 0.14 * MM, 3
    bar.data.materials.append(strip_material(k))
    bake(bar, lambda c, n: 0)
    for poly in bar.data.polygons:
        poly.use_smooth = True
    # the illumination: the surface's top, 5 microns above it, carrying only the lit pixels
    light = crowned_bar(f"Touch strip light {names[k]}", X0 + 0.2 * MM, X1 - 0.2 * MM, Y0 + 0.2 * MM, Y1 - 0.2 * MM,
                        edge - 0.2 * MM, edge + 0.005 * MM, crown, nx=48)
    lo = light.modifiers.new("Plan", "BOOLEAN")
    lo.operation, lo.solver, lo.object = "INTERSECT", "EXACT", plan
    light.data.materials.append(strip_light_material(k))
    bake(light, lambda c, n: 0)
    for poly in light.data.polygons:
        poly.use_smooth = True
    light.visible_shadow = False
    bpy.data.objects.remove(plan)


if args.wheels in ("physical", "strip") and APRON:
    P_ = PANEL_TOP
    if args.wheel_finish == "satin":
        # C2.5: the crown reads as a turned part: a touch lighter and satin, so its upper edge
        # catches the key; the slot darker and rougher, so wheel and slot separate.
        M_WHEEL = mat_solid("Wheel (charcoal, satin)", (50, 48, 45), 0.36, spec=0.45, coat=0.08, bump=0)
        M_SLOT = mat_solid("Wheel slot (charcoal)", (24, 23, 21), 0.86, spec=0.15, bump=0)
    else:
        M_WHEEL = mat_solid("Wheel (charcoal, soft-touch)", (36, 34, 32), 0.58, spec=0.3, bump=0)
        M_SLOT = mat_solid("Wheel slot (charcoal)", (30, 28, 26), 0.75, spec=0.2, bump=0)
    M_FLANGE = mat_solid("Wheel flange", (78, 74, 68), 0.45, spec=0.35, bump=0)
    M_PITCH = mat_solid("PITCH band (orange satin)", (214, 104, 42), 0.40, spec=0.4, coat=0.1, bump=0)
    BZ_H = args.bezel_height * MM
    SLOT_DEPTH, R_W = 5.0 * MM, 8.0 * MM
    WHEELS = [  # bezel (editor px), slot, PITCH band (None for MOD), from web-10-hero-3L-nocaps@4x
        ((42.5, 913.75 + KY, 77.5, 1001.0 + KY), (46.0, 918.75 + KY, 74.0, 997.0 + KY), (52.5, 937.0 + KY, 65.0, 990.0 + KY)),
        ((93.75, 913.75 + KY, 128.75, 1001.0 + KY), (97.5, 918.75 + KY, 125.0, 997.0 + KY), None),
    ]
    panel_o = bpy.data.objects["Ceramic panel"]
    panel_o.data.materials.append(M_SLOT)
    tray_o = bpy.data.objects["Ceramic tray"]
    tray_o.data.materials.append(M_SLOT)
    for k, (bz, sl, band) in enumerate(WHEELS):
        cut = slab(f"Wheel slot cutter {k}", ex(sl[0]), ey(sl[3]), ex(sl[2]), ey(sl[1]), 5 * PX, P_ - SLOT_DEPTH, P_ + 3 * MM, [M_SLOT], seg=6)
        cut.hide_render = cut.hide_viewport = True
        b_ = panel_o.modifiers.new(f"Slot {k}", "BOOLEAN")
        b_.operation, b_.solver, b_.object = "DIFFERENCE", "EXACT", cut
        # C2.5: under the wheels the panel is a 1.2 mm skin over the housing tray, so the slot goes
        # on into the tray (C2 stopped at the tray's ivory top: the light floor seen in the slots)
        t_ = tray_o.modifiers.new(f"Slot {k}", "BOOLEAN")
        t_.operation, t_.solver, t_.object = "DIFFERENCE", "EXACT", cut
        bezel = slab(f"Wheel bezel {k}", ex(bz[0]), ey(bz[3]), ex(bz[2]), ey(bz[1]), 7 * PX, P_ - 0.2 * MM, P_ + BZ_H,
                     [M_FACE, M_PANEL_SIDE], bevel=min(0.22 * MM, 0.45 * BZ_H), seg=8, cutters=[cut], bevel_segments=3)
        bake(bezel, top_side(P_ + BZ_H))
        vc = (sl[1] + sl[3]) / 2
        zc = P_ + BZ_H - 0.25 * MM - R_W      # the crown 0.25 mm under the bezel's top, whatever its height
        if args.wheels == "strip":
            touch_strip(k, sl, P_, BZ_H, SLOT_DEPTH)
            continue
        inset = 0.6 * MM / PX
        wheel_solid(f"Wheel {k}", sl[0] + inset + 1.2, sl[2] - inset - 1.2, vc, zc, R_W, 0, 2 * math.pi, M_WHEEL)
        for f0, f1 in ((sl[0] + inset, sl[0] + inset + 1.2), (sl[2] - inset - 1.2, sl[2] - inset)):
            wheel_solid(f"Wheel flange {k}", f0, f1, vc, zc, R_W + 0.08 * MM, 0, 2 * math.pi, M_FLANGE)
        if band:
            a_lo = math.asin(max(-0.999, min(0.999, (ey(band[3]) - ey(vc)) / R_W)))
            a_hi = math.asin(max(-0.999, min(0.999, (ey(band[1]) - ey(vc)) / R_W)))
            wheel_solid("PITCH band", band[0], band[2], vc, zc, R_W + 0.06 * MM, -a_hi, -a_lo, M_PITCH, seg=240)
    YT_ = ey(APRON["top"])
    # Charcoal only on the slots' own walls and floor. (C2's rule took every face below the panel
    # top left of x 135 px, which included the panel's outer side wall along the left cheek: the
    # dark sliver at the timber junction.)
    TOL = 0.05 * MM
    SLOTS = [(min(ex(sl[0]), ex(sl[2])) - TOL, max(ex(sl[0]), ex(sl[2])) + TOL, min(ey(sl[1]), ey(sl[3])) - TOL, max(ey(sl[1]), ey(sl[3])) + TOL)
             for _, sl, _ in WHEELS]

    def in_slot(c):
        return c.z < P_ - 0.05 * MM and any(x0 <= c.x <= x1 and y0 <= c.y <= y1 for x0, x1, y0, y1 in SLOTS)

    bake(tray_o, lambda c, n: len(tray_o.data.materials) - 1 if c.z < TRAY_TOP - 0.05 * MM and any(x0 <= c.x <= x1 and y0 <= c.y <= y1 for x0, x1, y0, y1 in SLOTS) else 0)
    bake(panel_o, lambda c, n: 3 if in_slot(c)
         else (0 if n.z > 0.9 and c.z > P_ - 0.05 * MM else (2 if c.y < YT_ - 1e-4 else 1)))
    for o_ in [o for o in bpy.data.objects if o.name.startswith("Wheel slot cutter")]:
        bpy.data.objects.remove(o_)

# --- pose ------------------------------------------------------------------------------------
ROT = {"A": -9.0, "hero_A": -9.0, "front_edge": 0.0, "strips_top": 0.0}.get(args.camera, -11.0)
if SHOT:
    ROT = SHOT["rot"]
root.location = (0, 0, FLOAT)
root.rotation_euler = (0, 0, math.radians(ROT))
# Camera targets use the 16 mm timber's heights, so the fascia-height test never moves a camera.
REF_FRAME_TOP = 2.0 * MM + 16.0 * MM    # fixed (pass 3.5's base), so a taller body never moves a camera
REF_PANEL_TOP = REF_FRAME_TOP - 1.5 * DMM
TARGET = Vector((0, 0, FLOAT + REF_PANEL_TOP))


def prod(u, v, z=None):
    """Editor px -> world point on the instrument."""
    z = REF_PANEL_TOP if z is None else z
    pnt = Vector((ex(u), ey(v), 0))
    pnt.rotate(Euler((0, 0, math.radians(ROT))))
    return Vector((pnt.x, pnt.y, FLOAT + z))


# --- stage -----------------------------------------------------------------------------------
def floor_mat(name, rgb, rough, groove=0.0, metal=0.0):
    nt = NT(name)
    b = nt.principled()
    b.inputs["Base Color"].default_value = (*rgb, 1)
    b.inputs["Metallic"].default_value = metal
    b.inputs["Specular IOR Level"].default_value = 0.3
    tc = nt.n("ShaderNodeTexCoord")
    noise = nt.n("ShaderNodeTexNoise", Scale=4.0, Detail=8.0)
    nt.link(tc.outputs["Object"], noise.inputs["Vector"])
    r = nt.n("ShaderNodeMapRange")
    r.inputs["To Min"].default_value, r.inputs["To Max"].default_value = rough - 0.06, rough + 0.08
    nt.link(noise.outputs["Fac"], r.inputs["Value"])
    nt.link(r.outputs[0], b.inputs["Roughness"])
    if groove:
        wave = nt.n("ShaderNodeTexWave", Scale=groove, Distortion=0.5, Detail=2.0)
        wave.wave_type, wave.bands_direction = "BANDS", "Y"
        nt.link(tc.outputs["Object"], wave.inputs["Vector"])
        bp = nt.n("ShaderNodeBump", Strength=0.3, Distance=0.0008)
        nt.link(wave.outputs["Fac"], bp.inputs["Height"])
        nt.link(bp.outputs[0], b.inputs["Normal"])
    cv = nt.n("ShaderNodeMix")
    cv.data_type, cv.blend_type = "RGBA", "MULTIPLY"
    cv.inputs[0].default_value = 0.3
    cv.inputs[6].default_value = (*rgb, 1)
    nt.link(noise.outputs["Color"], cv.inputs[7])
    nt.link(cv.outputs[2], b.inputs["Base Color"])
    return nt.finish(b.outputs[0])


floor_objs = []
if args.stage == "gallery":
    # Gate C gallery: warm pale stone, never white; its tone comes from the lights' falloff alone.
    fl = (SETUP or {}).get("floor", {"rgb": [0.62, 0.595, 0.555], "roughness": 0.78})
    m = floor_mat("Gallery stone", tuple(fl["rgb"]), fl["roughness"])
    sweep = slab("Gallery floor", -15, -15, 15, 15, 0.01, -0.02, 0.0, [m], coll=STAGE)   # wide: tall frames never see its edge
    floor_objs.append(sweep)
elif args.stage == "neutral":
    # A warm light-grey studio floor (no wall: the camera looks down, and a lit wall would only
    # veil the glaze and the glass with its reflection).
    m = floor_mat("Studio floor", (0.56, 0.53, 0.485), 0.72)
    sweep = slab("Studio floor", -4, -4, 4, 4, 0.01, -0.02, 0.0, [m], coll=STAGE)
    floor_objs.append(sweep)
else:
    FLOOR = [
        (0.32, (0.020, 0.016, 0.013), 0.48, 140, 0.000, 0.0),
        (0.006, (0.12, 0.075, 0.040), 0.30, 400, 0.0015, 1.0),
        (0.26, (0.045, 0.029, 0.019), 0.38, 220, 0.004, 0.0),
        (0.18, (0.026, 0.020, 0.016), 0.52, 120, 0.006, 0.0),
        (0.38, (0.082, 0.052, 0.033), 0.30, 260, 0.002, 0.0),
        (0.006, (0.12, 0.075, 0.040), 0.30, 400, 0.0035, 1.0),
        (0.30, (0.040, 0.027, 0.019), 0.40, 200, 0.001, 0.0),
        (0.40, (0.022, 0.018, 0.015), 0.50, 150, 0.005, 0.0),
    ]
    froot = bpy.data.objects.new("Backdrop", None)
    STAGE.objects.link(froot)
    # plank direction, chosen per camera so the lines fall steeply left-to-right in frame
    froot.rotation_euler = (0, 0, math.radians(SETUP.get("plank_rot", -95) if SETUP else (-78 if args.camera in ("A", "hero_A") else -95)))
    total = sum(f[0] + 0.003 for f in FLOOR)
    yy = -total / 2
    for i, (w, rgb, rough, gs, dz, metal) in enumerate(FLOOR):
        m = floor_mat(f"Plank {i}", rgb, rough, gs, metal)
        o = slab(f"Plank {i}", -2.0, yy, 2.0, yy + w, 0.0005, -0.03 + dz, dz, [m], bevel=0.0012 if w > 0.01 else 0.0, seg=1, coll=STAGE)
        o.parent = froot
        floor_objs.append(o)
        yy += w + 0.003
    g = slab("Grooves", -2.0, -total / 2 - 0.02, 2.0, total / 2 + 0.02, 0.0005, -0.06, -0.02, [floor_mat("Groove", (0.006, 0.005, 0.004), 0.7)], seg=1, coll=STAGE)
    g.parent = froot
    floor_objs.append(g)

if args.variant == "alpha":
    for o in floor_objs:
        o.hide_render = True
    catcher = slab("Shadow catcher", -3, -3, 3, 3, 0.01, -0.01, 0.0, [M_TRAY], coll=STAGE)
    catcher.is_shadow_catcher = True
    bpy.context.view_layer.cycles.use_pass_shadow_catcher = True

# --- lights ----------------------------------------------------------------------------------
FLOORC = STAGE


def area(name, loc, target, size, size_y, power, rgb=(1, 1, 1), spread=180, only=None, glossy_only=False):
    d = bpy.data.lights.new(name, "AREA")
    d.shape, d.size, d.size_y, d.energy, d.color = "RECTANGLE", size, size_y, power, rgb
    d.spread = math.radians(spread)
    o = bpy.data.objects.new(name, d)
    scene.collection.objects.link(o)
    o.location = loc
    o.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    o.visible_camera = False
    if glossy_only:
        o.visible_diffuse = False
        o.visible_transmission = False
        o.visible_volume_scatter = False
    if only is not None:
        o.light_linking.receiver_collection = only
    return o


T = TARGET
link = PRODUCT if args.stage == "dark" else None
if SETUP:
    # Gate C: the setup's own rig (config/campaign_setups.json): real area lights only.
    for L in SETUP["lights"]:
        if "world" in L:
            loc, aim = Vector(L["world"]), Vector(L["aim"])
        else:
            loc, aim = T + Vector(L["offset"]), T
        only = {"product": PRODUCT, "floor": STAGE}.get(L.get("link"))
        # C2: the setup may trim only the product's lights (e.g. B1: -0.3 EV), never the floor's
        power = L["power"] * (2.0 ** SETUP.get("product_light_ev", 0.0) if L.get("link") == "product" else 1.0)
        area(L["name"], loc, aim, L["size"][0], L["size"][1], power, tuple(L["rgb"]), spread=L.get("spread", 180), only=only)
elif args.light == "neutral":
    # The construction-review rig (passes 2-3.5, checkpoint 2, keyboard + front body round).
    # 1 key: a large soft box, upper left, slightly toward the camera side
    area("Key softbox", T + Vector((-0.95, 0.02, 0.88)), T, 1.15, 0.85, 26 if args.stage == "neutral" else 18, (1.0, 0.97, 0.93), only=link)
    # 2 fill: opposite, weaker
    area("Fill", T + Vector((1.05, -0.85, 0.45)), T, 1.6, 1.2, 4.5, (0.94, 0.96, 1.0), only=link)
    # 3 rim strip: behind and above, narrow, for the timber edges and the silhouette
    area("Rim strip", T + Vector((-0.62, 0.92, 0.16)), T, 1.6, 0.10, 30, (1.0, 0.93, 0.86), spread=60, only=link)
    # 3b front kicker: a low narrow strip for a highlight along the front
    area("Fascia kicker", T + Vector((-0.10, -1.10, -0.02)), T, 1.4, 0.06, 7, (1.0, 0.95, 0.9), spread=50, only=link)
else:
    # Gate B studio rig ("sculpt"): real lights only, no glows or overlays.
    # 1 key: smaller and raking from upper left-behind, so knobs, flutes, cards and key fronts model
    #   and every raised part throws a short, soft shadow toward the viewer's right
    area("Key softbox", T + Vector((-0.80, 0.38, 0.72)), T, 0.75, 0.55, 25 if args.stage == "neutral" else 17, (1.0, 0.965, 0.92), only=link)
    # 2 overhead: broad and weak, lifts the white-on-white planes without flattening them
    area("Overhead", T + Vector((0.05, 0.10, 1.45)), T, 1.8, 1.2, 3.5, (1.0, 0.985, 0.97), only=link)
    # 3 fill: opposite the key, cooler and lower than before, so the shadow sides keep their depth
    area("Fill", T + Vector((1.05, -0.85, 0.45)), T, 1.6, 1.2, 2.4, (0.93, 0.96, 1.0), only=link)
    # 4 rim strip: behind, for the timber edges and the silhouette (as before)
    area("Rim strip", T + Vector((-0.62, 0.92, 0.16)), T, 1.6, 0.10, 30, (1.0, 0.93, 0.86), spread=60, only=link)
    # 5 front kicker: low and narrow, draws a line along every key front, the lip and the cheek ends
    area("Front kicker", T + Vector((-0.10, -1.10, 0.02)), T, 1.4, 0.06, 6, (1.0, 0.95, 0.9), spread=50, only=link)
# 4 reflection card for the display glass: seen only in reflections
# Placed just off the mirror direction of camera B so it lays one soft band along the top of the
# screens rather than a veil over the waveforms.
area("Display card", T + Vector((0.62, 0.95, 0.62)), T, 0.7, 0.12, 0.25, (1.0, 0.98, 0.95), glossy_only=True, only=link)
if args.stage == "dark" and not SETUP:
    area("Floor overhead", (-0.55, 0.45, 1.5), (0.08, -0.08, 0.0), 1.0, 0.9, 34, (1.0, 0.95, 0.88), only=FLOORC)
    area("Floor rake", (-1.6, 0.9, 0.05), (0.4, -0.2, 0.0), 2.8, 0.18, 26, (1.0, 0.84, 0.68), spread=50, only=FLOORC)

# 5 black flag (negative fill): a matte black card where the screens' reflection looks, as a
# photographer would set one, so the glass reflects black instead of the bright studio wall.
# Seen only in reflections; it neither blocks light nor appears in frame.
if args.stage in ("neutral", "gallery"):
    fm = mat_solid("Black flag", (6, 6, 6), 0.9, spec=0.0, bump=0)
    me = bpy.data.meshes.new("Black flag")
    bm = bmesh.new()
    vs = [bm.verts.new(v) for v in ((-0.9, 0, -0.6), (0.9, 0, -0.6), (0.9, 0, 0.6), (-0.9, 0, 0.6))]
    bm.faces.new(vs)
    bm.to_mesh(me)
    bm.free()
    flag = obj("Black flag", me, STAGE, parent=False)
    me.materials.append(fm)
    flag.location = T + Vector((0.42, 0.95, 0.80))
    flag.rotation_euler = (T - flag.location).to_track_quat("Y", "Z").to_euler()
    flag.visible_camera = False
    flag.visible_shadow = False
    flag.visible_diffuse = False
    flag.visible_transmission = False

world = bpy.data.worlds.new("Studio")
scene.world = world
world.use_nodes = True
bg = world.node_tree.nodes["Background"]
bg.inputs["Color"].default_value = (0.32, 0.30, 0.28, 1) if args.stage == "neutral" else (0.020, 0.018, 0.016, 1)
bg.inputs["Strength"].default_value = 0.10 if args.stage == "neutral" else 0.12
if SETUP:
    bg.inputs["Color"].default_value = (*SETUP["world"]["rgb"], 1)
    bg.inputs["Strength"].default_value = SETUP["world"]["strength"]


# --- cameras ---------------------------------------------------------------------------------
def camera(name, yaw, pitch, dist, lens, target, fstop=None, shift=(0.0, 0.0), fit="AUTO"):
    cd = bpy.data.cameras.new(name)
    cd.lens, cd.sensor_fit, cd.sensor_width = lens, fit, 36
    cd.shift_x, cd.shift_y = shift
    o = bpy.data.objects.new(name, cd)
    scene.collection.objects.link(o)
    d = Vector((0, -math.sin(math.radians(pitch)), math.cos(math.radians(pitch))))
    d.rotate(Euler((0, 0, math.radians(yaw))))
    o.location = Vector(target) + d * dist
    o.rotation_euler = (Vector(target) - o.location).to_track_quat("-Z", "Y").to_euler()
    if fstop:
        cd.dof.use_dof = True
        cd.dof.focus_distance = (Vector(target) - o.location).length
        cd.dof.aperture_fstop = fstop
        cd.dof.aperture_blades = 9
    return o


# pitch: from the face normal toward the front edge; yaw: around the normal.
CAMS = {
    "A": dict(yaw=-24, pitch=27, dist=1.16, lens=75, target=T, fstop=11),               # pass 1's angle
    "B": dict(yaw=-24, pitch=54, dist=1.30, lens=85, target=T, fstop=11),               # elevated three-quarter
    "hero_A": dict(yaw=-24, pitch=27, dist=1.12, lens=75, target=T, fstop=9, shift=(0, 0.07), fit="HORIZONTAL"),   # = pass 1
    "hero_B": dict(yaw=-24, pitch=54, dist=1.16, lens=85, target=T, fstop=9, shift=(-0.035, 0.05), fit="HORIZONTAL"),
    "macros": dict(yaw=-18, pitch=50, dist=0.64, lens=85, target=prod(493, 812 + KY, REF_PANEL_TOP + CARD_H + 4 * MM), fstop=11),
    "display": dict(yaw=-20, pitch=42, dist=0.30, lens=100, target=prod(723, 330, REF_PANEL_TOP + CARD_H), fstop=8),
    "corner": dict(yaw=38, pitch=66, dist=0.26, lens=100, target=prod(1390, 1050 + KY, REF_FRAME_TOP * 0.6), fstop=8),
    "corner_back": dict(yaw=-150, pitch=62, dist=0.26, lens=100, target=prod(40, 30, REF_FRAME_TOP * 0.6), fstop=8),
    "corner_fl": dict(yaw=-38, pitch=66, dist=0.26, lens=100, target=prod(58, 1050 + KY, REF_FRAME_TOP * 0.6), fstop=8),
    "corner_br": dict(yaw=150, pitch=62, dist=0.26, lens=100, target=prod(1408, 30, REF_FRAME_TOP * 0.6), fstop=8),
    "knob": dict(yaw=-20, pitch=52, dist=0.21, lens=100, target=prod(493, 818 + KY, REF_PANEL_TOP + 4 * MM), fstop=11),
    # pass 3.5: the same knobs from a lower elevation (22 deg above the panel) to read the sidewall
    "knob_low": dict(yaw=-20, pitch=68, dist=0.21, lens=100, target=prod(493, 818 + KY, REF_PANEL_TOP + 4 * MM), fstop=11),
    "small": dict(yaw=-20, pitch=50, dist=0.17, lens=100, target=prod(723, 520, REF_PANEL_TOP + CARD_H + 2 * MM), fstop=11),
    "module": dict(yaw=-24, pitch=55, dist=0.20, lens=100, target=prod(60, 640 + KY, REF_PANEL_TOP + CARD_H), fstop=11),
    # checkpoint 2: the front edge straight on (product square to the camera, 8 deg above the floor)
    # and a three-quarter close-up of the key fronts and the return
    "front_edge": dict(yaw=0, pitch=82, dist=1.70, lens=135, target=prod(724, 1046 + KY, REF_FRAME_TOP * 0.55), fstop=16),
    "keys_macro": dict(yaw=-28, pitch=64, dist=0.17, lens=100, target=prod(600, 985 + KY, REF_PANEL_TOP - 3 * MM), fstop=9),
    "strips": dict(yaw=-22, pitch=48, dist=0.15, lens=100, target=prod(86, 957 + KY, REF_PANEL_TOP), fstop=None),   # C2.5: no depth of field (geometry review)
    "strips34": dict(yaw=-48, pitch=60, dist=0.12, lens=100, target=prod(86, 957 + KY, REF_PANEL_TOP), fstop=None),
    "strips_top": dict(yaw=0, pitch=0.01, dist=0.30, lens=100, target=prod(86, 957 + KY, REF_PANEL_TOP), fstop=None),
    "wheels": dict(yaw=-22, pitch=48, dist=0.15, lens=100, target=prod(86, 957 + KY, REF_PANEL_TOP), fstop=11),   # C2.5: PITCH/MOD in focus
    "keys34": dict(yaw=-32, pitch=62, dist=0.27, lens=100, target=prod(520, 985 + KY, REF_PANEL_TOP - 3 * MM), fstop=11),
}
if SHOT:
    sc = dict(SHOT["camera"])
    u_, v_, z_ = sc.pop("target")
    v_ += KY if v_ > 600 else 0.0      # a target under the cards follows the current editor's trim (0 for older captures)
    sc["target"] = prod(u_, v_, REF_PANEL_TOP + z_ * MM)
    sc["dist"] *= args.dist_scale
    sc["shift"] = tuple(sc["shift"])
    scene.camera = camera(f"Camera {args.setup} {args.shot}", **sc)
else:
    scene.camera = camera("Camera " + args.camera, **CAMS[args.camera])
    if args.camera == "strips_top":   # C2.5: orthographic, straight down, for a like-for-like check against the GUI
        scene.camera.data.type = "ORTHO"
        scene.camera.data.ortho_scale = 150 * PX       # editor x 11..161 at the output's width

if args.border:
    scene.render.use_border = True
    scene.render.use_crop_to_border = True
    scene.render.border_min_x, scene.render.border_max_x, scene.render.border_min_y, scene.render.border_max_y = args.border

if args.save_blend:
    bpy.ops.file.pack_all()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(args.save_blend), compress=True)

if args.out:
    out = os.path.abspath(args.out)
    if args.variant == "alpha":
        scene.use_nodes = True
        ct = scene.node_tree
        for n_ in list(ct.nodes):
            ct.nodes.remove(n_)
        rl = ct.nodes.new("CompositorNodeRLayers")
        comp = ct.nodes.new("CompositorNodeComposite")
        ct.links.new(rl.outputs["Image"], comp.inputs["Image"])
        ct.links.new(rl.outputs["Alpha"], comp.inputs["Alpha"])
        fo = ct.nodes.new("CompositorNodeOutputFile")
        fo.base_path = os.path.dirname(out)
        fo.format.file_format, fo.format.color_mode, fo.format.color_depth = "PNG", "RGB", "16"
        fo.format.color_management = "OVERRIDE"
        fo.format.view_settings.view_transform = "Standard"
        fo.format.view_settings.look = "None"
        fo.file_slots[0].path = os.path.splitext(os.path.basename(out))[0] + "_shadow_"
        ct.links.new(rl.outputs["Shadow Catcher"], fo.inputs[0])
    scene.render.filepath = out
    bpy.ops.render.render(write_still=True)
    print("wrote", out)
