"""ANDOR OSP product-render scene, pass 2 (headless Blender 4.2 / bpy, Cycles).

The instrument is built in layers from the genuine editor (textures/geometry.json, written by
prep_textures.py from the OSP test captures):
  A  housing     a continuous teak frame (front fascia and back rail along the width, side rails
                 between them, butt joints), a ceramic tray inside it, a narrow dark plinth below;
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
  --camera A | B | macros | display | corner | hero_A | hero_B     --stage neutral | dark
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
p.add_argument("--camera", default="B", choices=["A", "B", "macros", "display", "corner", "hero_A", "hero_B"])
p.add_argument("--stage", default="neutral", choices=["neutral", "dark"])
p.add_argument("--variant", default="shaded", choices=["shaded", "clay", "alpha"])
p.add_argument("--res", type=int, nargs=2, default=[2000, 1500])
p.add_argument("--scale", type=float, default=1.0)
p.add_argument("--samples", type=int, default=128)
p.add_argument("--out")
p.add_argument("--textures", default="product-renders/blender/textures")
p.add_argument("--save-blend")
p.add_argument("--view", default="AgX")
p.add_argument("--look", default="AgX - Punchy")
p.add_argument("--exposure", type=float, default=None)
p.add_argument("--border", type=float, nargs=4)
args = p.parse_args(argv)

TEX = os.path.abspath(args.textures)
GEO = json.load(open(os.path.join(TEX, "geometry.json")))
EW, EH = GEO["editor_px"]

# --- dimensions (visualisation scale: the face is 400 mm wide) -------------------------------
W = 0.400
PX = W / EW
H = EH * PX
MM = 0.001
PLINTH_H = 2.0 * MM          # dark base, inset 3 mm
FRAME_TOP = PLINTH_H + 16.0 * MM   # timber: 16 mm tall (5 % of the 300 mm depth of the face)
TRAY_TOP = FRAME_TOP - 2.4 * MM    # layer A: the ceramic tray inside the frame
PANEL_TOP = FRAME_TOP - 1.0 * MM   # layer B: the panel, seated 1 mm below the timber
CARD_TOP = PANEL_TOP + 1.2 * MM    # raised cards (thick enough to hold the display wells)
WELL = 0.85 * MM                   # display recess (floor stays above the panel)
KEYBED_TOP = PANEL_TOP + 1.0 * MM
BLACK_TOP = KEYBED_TOP + 2.2 * MM
KNOB_H = {"macro": 8.0 * MM, "layer": 5.5 * MM, "mini": 4.5 * MM}
FLOAT = {"neutral": 0.055, "dark": 0.28}[args.stage]


def lin(c):
    return tuple(((x / 255) / 12.92) if x / 255 <= 0.04045 else (((x / 255) + 0.055) / 1.055) ** 2.4 for x in c)


def ex(x):
    return (x - EW / 2) * PX


def ey(y):
    return (EH / 2 - y) * PX


# --- scene -----------------------------------------------------------------------------------
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = "CYCLES"
cy = scene.cycles
cy.device = "CPU"
cy.samples = args.samples
cy.use_adaptive_sampling = True
cy.adaptive_threshold = 0.012
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
scene.view_settings.exposure = args.exposure if args.exposure is not None else {"neutral": -0.55, "dark": 0.3}[args.stage]
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
    v = nt.math("MULTIPLY_ADD", sep.outputs[1], 1 / H, 0.5)
    uv = nt.n("ShaderNodeCombineXYZ")
    nt.link(u, uv.inputs[0])
    nt.link(v, uv.inputs[1])
    return tc, uv.outputs[0]


def mat_face():
    """Lit ceramic carrying the interface artwork. No emission: the light gives it form."""
    nt = NT("OSP Face (capture)")
    tc, uv = face_uv(nt)
    tex = nt.n("ShaderNodeTexImage")
    tex.image, tex.interpolation, tex.extension = FACE_IMG, "Cubic", "EXTEND"
    nt.link(uv, tex.inputs[0])
    b = nt.principled()
    nt.link(tex.outputs["Color"], b.inputs["Base Color"])
    b.inputs["Roughness"].default_value = 0.48
    b.inputs["Specular IOR Level"].default_value = 0.22
    b.inputs["Coat Weight"].default_value = 0.05          # a thin glaze
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
    b.inputs["Coat Weight"].default_value = 0.45
    b.inputs["Coat Roughness"].default_value = 0.03
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
STRIP_L = 2 * 1055 * PX        # ... and two cheeks (583 mm) along the grain


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
    v = nt.math("MULTIPLY_ADD", pos.outputs[long_i], 1 / STRIP_L, offset)
    uv = nt.n("ShaderNodeCombineXYZ")
    nt.link(u, uv.inputs[0])
    nt.link(v, uv.inputs[1])
    tex = nt.n("ShaderNodeTexImage")
    tex.image, tex.extension, tex.interpolation = TEAK_IMG, "MIRROR", "Cubic"
    nt.link(uv.outputs[0], tex.inputs[0])
    # End grain: the same wood, seen across its fibres: fine pores, darker, more matte.
    eu = nt.math("DIVIDE", pos.outputs[2], STRIP_W * 0.35)
    ev = nt.math("DIVIDE", pos.outputs[across_i], STRIP_W * 0.6)
    euv = nt.n("ShaderNodeCombineXYZ")
    nt.link(eu, euv.inputs[0])
    nt.link(ev, euv.inputs[1])
    etex = nt.n("ShaderNodeTexImage")
    etex.image, etex.extension = TEAK_IMG, "MIRROR"
    nt.link(euv.outputs[0], etex.inputs[0])
    edark = nt.n("ShaderNodeHueSaturation")
    edark.inputs["Value"].default_value = 0.62
    edark.inputs["Saturation"].default_value = 1.1
    nt.link(etex.outputs["Color"], edark.inputs["Color"])
    col = nt.n("ShaderNodeMix")
    col.data_type = "RGBA"
    nt.link(is_end, col.inputs[0])
    nt.link(tex.outputs["Color"], col.inputs[6])
    nt.link(edark.outputs["Color"], col.inputs[7])
    # colour-matched to the editor's teak under AgX (see documentation/materials.md)
    hsv = nt.n("ShaderNodeHueSaturation")
    hsv.inputs["Saturation"].default_value = 1.12
    hsv.inputs["Value"].default_value = 0.74
    nt.link(col.outputs[2], hsv.inputs["Color"])
    b = nt.principled()
    nt.link(hsv.outputs["Color"], b.inputs["Base Color"])
    rough = nt.n("ShaderNodeMapRange")
    rough.inputs["To Min"].default_value, rough.inputs["To Max"].default_value = 0.62, 0.48
    nt.link(tex.outputs["Color"], rough.inputs["Value"])
    r_end = nt.math("MAXIMUM", rough.outputs[0], nt.math("MULTIPLY", is_end, 0.78))
    nt.link(r_end, b.inputs["Roughness"])
    b.inputs["Specular IOR Level"].default_value = 0.36
    b.inputs["Coat Weight"].default_value = 0.08     # oiled, not lacquered
    b.inputs["Coat Roughness"].default_value = 0.35
    bump = nt.n("ShaderNodeBump", Strength=0.10, Distance=0.00015)
    nt.link(col.outputs[2], bump.inputs["Height"])
    nt.link(bump.outputs[0], b.inputs["Normal"])
    return nt.finish(b.outputs[0])


CLAY = mat_solid("Clay", (150, 150, 148), 0.6, bump=0) if args.variant == "clay" else None
M_FACE = mat_face()
M_DISPLAY = mat_display()
C = GEO["colours"]
M_TRAY = mat_solid("Ceramic tray (housing)", tuple(C["tray"]), 0.46)
M_PANEL_SIDE = mat_solid("Ceramic ivory (edges)", tuple(C["panel"]), 0.40, coat=0.1)
M_CARD_SIDE = mat_solid("Ceramic card (edges)", tuple(C["card"]), 0.38, coat=0.1)
M_BEZEL = mat_solid("Display bezel", (20, 20, 21), 0.32, spec=0.5, bump=0)
M_PLINTH = mat_solid("Plinth", (28, 25, 23), 0.5, bump=0)
M_KNOB = mat_solid("Ceramic knob", (245, 241, 233), 0.30, spec=0.45, coat=0.25)
M_COLLAR = mat_solid("Knob collar", (52, 47, 42), 0.5, bump=0)
M_POINTER = mat_solid("Knob indicator", (40, 38, 36), 0.45, bump=0)
M_KEY_SIDE = mat_solid("Black key", (34, 33, 32), 0.32, spec=0.5, bump=0)
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
WOOD_BEVEL = 0.7 * MM
front = slab("Teak front fascia", ex(0), ey(EH), ex(EW), ey(fy), 0, PLINTH_H, FRAME_TOP, [M_TEAK_X], bevel=WOOD_BEVEL, seg=1)
back = slab("Teak back rail", ex(0), ey(by), ex(EW), ey(0), 0, PLINTH_H, FRAME_TOP, [M_TEAK_X], bevel=WOOD_BEVEL, seg=1)
left = slab("Teak side rail L", ex(0), ey(fy), ex(cx0), ey(by), 0, PLINTH_H, FRAME_TOP, [M_TEAK_Y], bevel=WOOD_BEVEL, seg=1)
right = slab("Teak side rail R", ex(cx1), ey(fy), ex(EW), ey(by), 0, PLINTH_H, FRAME_TOP, [M_TEAK_Y2], bevel=WOOD_BEVEL, seg=1)
for o in (front, back, left, right):
    bake(o, lambda c, n: 0)
tray = slab("Ceramic tray", ex(cx0) - 0.5 * MM, ey(fy) - 0.5 * MM, ex(cx1) + 0.5 * MM, ey(by) + 0.5 * MM, 0, PLINTH_H + 1 * MM, TRAY_TOP, [M_TRAY])
plinth = slab("Plinth", ex(0) + 3 * MM, ey(EH) + 3 * MM, ex(EW) - 3 * MM, ey(0) - 3 * MM, 4 * MM, 0.0, PLINTH_H + 0.2 * MM, [M_PLINTH], bevel=0.5 * MM)
bake(plinth, lambda c, n: 0)

# --- layer B: ceramic panel and cards; layer C: display wells -------------------------------
x0, y0, x1, y1 = GEO["panel"]["rect"]
panel = slab("Ceramic panel", ex(x0), ey(y1), ex(x1), ey(y0), GEO["panel"]["radius"] * PX, TRAY_TOP - 0.5 * MM, PANEL_TOP,
             [M_FACE, M_PANEL_SIDE], bevel=0.45 * MM, seg=8)
bake(panel, top_side(PANEL_TOP))


def inside(x, y, rect, pad=0.0):
    return rect[0] - pad <= x <= rect[2] + pad and rect[1] - pad <= y <= rect[3] + pad


DISPLAYS = GEO["displays"]
for i, cd in enumerate(GEO["cards"]):
    a0, b0, a1, b1 = cd["rect"]
    wells = [d for d in DISPLAYS if inside(d[0], d[1], cd["rect"]) and inside(d[2], d[3], cd["rect"])]
    cutters = []
    for j, d in enumerate(wells):
        c = slab(f"Well cutter {i}.{j}", ex(d[0]), ey(d[3]), ex(d[2]), ey(d[1]), 8 * PX, CARD_TOP - WELL, CARD_TOP + 2 * MM, [M_BEZEL], seg=8)
        c.hide_render = True
        c.hide_viewport = True
        cutters.append(c)
    card = slab(f"Ceramic card {i}", ex(a0), ey(b1), ex(a1), ey(b0), cd["radius"] * PX, PANEL_TOP - 0.3 * MM, CARD_TOP,
                [M_FACE, M_CARD_SIDE, M_DISPLAY, M_BEZEL], bevel=0.4 * MM, seg=8, cutters=cutters, bevel_segments=3)

    def classify(c, n, wells=wells):
        u, v = c.x / PX + EW / 2, EH / 2 - c.y / PX
        in_well = any(inside(u, v, d, 1.5) for d in wells)
        if n.z > 0.9:
            if c.z > CARD_TOP - 0.0002:
                return 0
            return 2 if in_well else 0
        return 3 if in_well else 1
    bake(card, classify)
    for c in cutters:
        bpy.data.objects.remove(c)


def surface_top(u, v):
    for cd in GEO["cards"]:
        if inside(u, v, cd["rect"]):
            return CARD_TOP
    return PANEL_TOP


# --- layer D: buttons, keys, knobs -----------------------------------------------------------
for i, bt in enumerate(GEO["buttons"]):
    a0, b0, a1, b1 = bt["rect"]
    base = surface_top((a0 + a1) / 2, (b0 + b1) / 2)
    top = base + bt["lift_mm"] * MM
    side = mat_solid(f"Button side {i}", tuple(bt["side_rgb"]), 0.4)
    o = slab(f"Button {bt['kind']} {i}", ex(a0), ey(b1), ex(a1), ey(b0), bt["radius"] * PX, base - 0.3 * MM, top,
             [M_FACE, side], bevel=0.35 * MM, seg=6)
    bake(o, top_side(top))

kb = GEO["keyboard"]
a0, b0, a1, b1 = kb["bed"]
o = slab("Key bed", ex(a0), ey(b1), ex(a1), ey(b0), 3 * PX, PANEL_TOP - 0.3 * MM, KEYBED_TOP, [M_FACE, M_CARD_SIDE], bevel=0.3 * MM, seg=4)
bake(o, top_side(KEYBED_TOP))
for i, k in enumerate(kb["black"]):
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


for i, k in enumerate(GEO["knobs"]):
    knob(f"Knob {k['kind']} {i:02d}", k["c"][0], k["c"][1], k["r"], k["angle"], k["kind"])

if CLAY:
    for o in PRODUCT.objects:
        if o.type == "MESH":
            o.data.materials.clear()
            o.data.materials.append(CLAY)

# --- pose ------------------------------------------------------------------------------------
ROT = {"A": -9.0, "hero_A": -9.0}.get(args.camera, -11.0)
root.location = (0, 0, FLOAT)
root.rotation_euler = (0, 0, math.radians(ROT))
TARGET = Vector((0, 0, FLOAT + PANEL_TOP))


def prod(u, v, z=PANEL_TOP):
    """Editor px -> world point on the instrument."""
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
if args.stage == "neutral":
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
    froot.rotation_euler = (0, 0, math.radians(-78 if args.camera in ("A", "hero_A") else -95))
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
# 1 key: a large soft box, upper left, slightly toward the camera side
area("Key softbox", T + Vector((-0.95, 0.02, 0.88)), T, 1.15, 0.85, 26 if args.stage == "neutral" else 18, (1.0, 0.97, 0.93), only=link)
# 2 fill: opposite, weaker
area("Fill", T + Vector((1.05, -0.85, 0.45)), T, 1.6, 1.2, 4.5, (0.94, 0.96, 1.0), only=link)
# 3 rim strip: behind and above, narrow, for the timber edges and the silhouette
area("Rim strip", T + Vector((-0.62, 0.92, 0.16)), T, 1.6, 0.10, 30, (1.0, 0.93, 0.86), spread=60, only=link)
# 3b front kicker: a low narrow strip for a highlight along the front fascia
area("Fascia kicker", T + Vector((-0.10, -1.10, -0.02)), T, 1.4, 0.06, 7, (1.0, 0.95, 0.9), spread=50, only=link)
# 4 reflection card for the display glass: seen only in reflections
# Placed just off the mirror direction of camera B so it lays one soft band along the top of the
# screens rather than a veil over the waveforms.
area("Display card", T + Vector((0.62, 0.95, 0.62)), T, 0.7, 0.12, 0.25, (1.0, 0.98, 0.95), glossy_only=True, only=link)
if args.stage == "dark":
    area("Floor overhead", (-0.55, 0.45, 1.5), (0.08, -0.08, 0.0), 1.0, 0.9, 34, (1.0, 0.95, 0.88), only=FLOORC)
    area("Floor rake", (-1.6, 0.9, 0.05), (0.4, -0.2, 0.0), 2.8, 0.18, 26, (1.0, 0.84, 0.68), spread=50, only=FLOORC)

# 5 black flag (negative fill): a matte black card where the screens' reflection looks, as a
# photographer would set one, so the glass reflects black instead of the bright studio wall.
# Seen only in reflections; it neither blocks light nor appears in frame.
if args.stage == "neutral":
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
    "macros": dict(yaw=-18, pitch=50, dist=0.64, lens=85, target=prod(493, 812, CARD_TOP + 4 * MM), fstop=11),
    "display": dict(yaw=-20, pitch=42, dist=0.30, lens=100, target=prod(723, 330, CARD_TOP), fstop=8),
    "corner": dict(yaw=38, pitch=66, dist=0.26, lens=100, target=prod(1390, 1050, FRAME_TOP * 0.6), fstop=8),
}
scene.camera = camera("Camera " + args.camera, **CAMS[args.camera])

if args.border:
    scene.render.use_border = True
    scene.render.use_crop_to_border = True
    scene.render.border_min_x, scene.render.border_max_x, scene.render.border_min_y, scene.render.border_max_y = args.border

if args.save_blend:
    bpy.ops.file.pack_all()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(args.save_blend))

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
