"""ANDOR OSP product-render scene (headless Blender 4.2 / bpy, Cycles).

Builds the instrument as a layered physical object from the genuine editor capture, lights it with
a four-light studio rig, places it over a dark architectural floor and renders one camera.

Nothing about the interface is redrawn. Every visible interface pixel is the capture, projected
straight down the face normal onto the parts that carry it:
  * the tray (the editor's outer frame) and the raised ceramic panel,
  * the tops of the 25 knob caps (macro, layer and envelope knobs, at their measured centres/radii),
  * the teak cheeks use the cheek's own pixels (texture prep: teak_strip.png).
The knobs' drawn drop shadows stay in the face texture and ARE the knobs' shadows: the 3D caps do
not cast a second one. The key light comes from the upper left, the same side as the drawn light.

Usage (from andor-web/, Python 3.11 venv with bpy==4.2.0):
  python product-renders/blender/scripts/andor_scene.py -- --camera hero_9x16 --variant combined \
      --res 1080 1920 --samples 128 --out product-renders/renders/instagram_9x16/osp_hero_9x16.png
Variants: combined (backdrop), alpha (product only on transparency, plus a shadow pass), clay.
--save-blend PATH writes the scene (images packed) for editing in Blender.
"""
import argparse
import json
import math
import os
import sys

import bpy  # first: bmesh and mathutils come with the bpy module
import bmesh
from mathutils import Euler, Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
p = argparse.ArgumentParser()
p.add_argument("--camera", default="hero_9x16", choices=["hero_9x16", "gallery", "closeup"])
p.add_argument("--variant", default="combined", choices=["combined", "alpha", "clay"])
p.add_argument("--res", type=int, nargs=2, default=[1080, 1920])
p.add_argument("--scale", type=float, default=1.0, help="resolution percentage / 100 (drafts)")
p.add_argument("--samples", type=int, default=128)
p.add_argument("--out", required=False)
p.add_argument("--textures", default="product-renders/blender/textures")
p.add_argument("--save-blend")
p.add_argument("--threads", type=int, default=0)
p.add_argument("--view", default="AgX")
p.add_argument("--look", default="AgX - Punchy")
p.add_argument("--exposure", type=float, default=0.3)
p.add_argument("--border", type=float, nargs=4, help="render region xmin xmax ymin ymax (0-1), for inspection")
args = p.parse_args(argv)

TEX = os.path.abspath(args.textures)
GEO = json.load(open(os.path.join(TEX, "geometry.json")))
EW, EH = GEO["editor_px"]

# ---------------------------------------------------------------------------------------------
# Scale: the face is 400 mm wide (visualisation scale, not an official hardware dimension).
W = 0.400
PX = W / EW                  # metres per editor pixel (0.276 mm)
H = EH * PX                  # 300 mm
T = 0.024                    # enclosure depth
PANEL_RISE = 0.0012          # the ceramic panel stands 1.2 mm proud of the tray (drawn as a lip)
CHEEK_RISE = 0.0050          # the teak cheeks stand 5 mm proud of the face
FLOAT = 0.28                 # height of the instrument's underside above the floor
KNOB_H = {"macro": 0.0072, "layer": 0.0046, "mini": 0.0036}


def lin(c):
    """sRGB 0-255 -> linear 0-1."""
    return tuple(((x / 255) / 12.92) if x / 255 <= 0.04045 else (((x / 255) + 0.055) / 1.055) ** 2.4 for x in c)


def ex(x_px):
    return (x_px - EW / 2) * PX


def ey(y_px):
    return (EH / 2 - y_px) * PX


# ---------------------------------------------------------------------------------------------
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.unit_settings.system = "METRIC"
scene.render.engine = "CYCLES"
cy = scene.cycles
cy.device = "CPU"
cy.samples = args.samples
cy.use_adaptive_sampling = True
cy.adaptive_threshold = 0.015
cy.use_denoising = True
cy.denoiser = "OPENIMAGEDENOISE"
cy.max_bounces = 8
cy.glossy_bounces = 4
cy.transparent_max_bounces = 8
cy.caustics_reflective = False
cy.caustics_refractive = False
cy.blur_glossy = 0.5
if args.threads:
    scene.render.threads_mode = "FIXED"
    scene.render.threads = args.threads
scene.render.resolution_x, scene.render.resolution_y = args.res
scene.render.resolution_percentage = int(round(args.scale * 100))
scene.render.filter_size = 1.2
scene.view_settings.view_transform = args.view
scene.view_settings.look = args.look
scene.view_settings.exposure = args.exposure
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA" if args.variant == "alpha" else "RGB"
scene.render.image_settings.color_depth = "16"
scene.render.film_transparent = args.variant == "alpha"

coll = scene.collection


def new_obj(name, mesh, target=None):
    o = bpy.data.objects.new(name, mesh)
    (target or coll).objects.link(o)
    return o


def image(name, path, non_color=False, extension="EXTEND"):
    img = bpy.data.images.load(os.path.join(TEX, path))
    img.name = name
    if non_color:
        img.colorspace_settings.name = "Non-Color"
    return img


# ---------------------------------------------------------------------------------------------
# Materials
PRODUCT = bpy.data.collections.new("OSP product")
coll.children.link(PRODUCT)
root = bpy.data.objects.new("OSP", None)  # product root: face projection is relative to it
PRODUCT.objects.link(root)

face_img = image("osp_face", "osp_face.png")
mask_img = image("osp_display_mask", "osp_display_mask.png", non_color=True)
teak_img = image("teak_strip", "teak_strip.png")


def nodes_of(mat):
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    return nt, out


def face_material():
    """The capture, projected down the face normal: lit ceramic, glowing glass where the editor
    draws a display."""
    m = bpy.data.materials.new("OSP Face (capture)")
    nt, out = nodes_of(m)
    n, L = nt.nodes, nt.links
    tc = n.new("ShaderNodeTexCoord")
    tc.object = root
    sep = n.new("ShaderNodeSeparateXYZ")
    L.new(tc.outputs["Object"], sep.inputs[0])
    u = n.new("ShaderNodeMath"); u.operation = "MULTIPLY_ADD"; u.inputs[1].default_value = 1 / W; u.inputs[2].default_value = 0.5
    v = n.new("ShaderNodeMath"); v.operation = "MULTIPLY_ADD"; v.inputs[1].default_value = 1 / H; v.inputs[2].default_value = 0.5
    L.new(sep.outputs[0], u.inputs[0]); L.new(sep.outputs[1], v.inputs[0])
    uv = n.new("ShaderNodeCombineXYZ")
    L.new(u.outputs[0], uv.inputs[0]); L.new(v.outputs[0], uv.inputs[1])
    tex = n.new("ShaderNodeTexImage"); tex.image = face_img; tex.interpolation = "Cubic"; tex.extension = "EXTEND"
    msk = n.new("ShaderNodeTexImage"); msk.image = mask_img; msk.interpolation = "Linear"; msk.extension = "EXTEND"
    L.new(uv.outputs[0], tex.inputs[0]); L.new(uv.outputs[0], msk.inputs[0])

    bsdf = n.new("ShaderNodeBsdfPrincipled")
    # A screen is light under glass, not paint: on displays the pixels emit and barely reflect.
    dim = n.new("ShaderNodeMix"); dim.data_type = "RGBA"; dim.blend_type = "MIX"
    L.new(msk.outputs["Color"], dim.inputs[0])
    L.new(tex.outputs["Color"], dim.inputs[6])
    dim.inputs[7].default_value = (0.004, 0.004, 0.004, 1)
    L.new(dim.outputs[2], bsdf.inputs["Base Color"])
    # Ceramic: satin (0.42); display glass: smooth with a clear coat.
    rough = n.new("ShaderNodeMapRange")
    L.new(msk.outputs["Color"], rough.inputs["Value"])
    rough.inputs["To Min"].default_value = 0.5
    rough.inputs["To Max"].default_value = 0.22
    L.new(rough.outputs[0], bsdf.inputs["Roughness"])
    # Displays: the coat carries the reflections; the base layer's own specular is dropped so the
    # glass stays as deep as the editor draws it.
    spec = n.new("ShaderNodeMapRange")
    L.new(msk.outputs["Color"], spec.inputs["Value"])
    spec.inputs["To Min"].default_value = 0.3
    spec.inputs["To Max"].default_value = 0.0
    L.new(spec.outputs[0], bsdf.inputs["Specular IOR Level"])
    coat = n.new("ShaderNodeMath"); coat.operation = "MULTIPLY"; coat.inputs[1].default_value = 0.7
    L.new(msk.outputs["Color"], coat.inputs[0])
    L.new(coat.outputs[0], bsdf.inputs["Coat Weight"])
    bsdf.inputs["Coat Roughness"].default_value = 0.035
    bsdf.inputs["Coat IOR"].default_value = 1.5
    # Screens emit their own light; ceramic only a whisper (keeps labels legible in shade).
    em = n.new("ShaderNodeMapRange")
    L.new(msk.outputs["Color"], em.inputs["Value"])
    em.inputs["To Min"].default_value = 0.03
    em.inputs["To Max"].default_value = 1.7
    L.new(tex.outputs["Color"], bsdf.inputs["Emission Color"])
    L.new(em.outputs[0], bsdf.inputs["Emission Strength"])
    # Fine ceramic imperfection: a very low bump from noise (not on the glass).
    noise = n.new("ShaderNodeTexNoise"); noise.inputs["Scale"].default_value = 900; noise.inputs["Detail"].default_value = 4
    L.new(tc.outputs["Object"], noise.inputs["Vector"])
    bstr = n.new("ShaderNodeMapRange"); bstr.inputs["To Min"].default_value = 0.025; bstr.inputs["To Max"].default_value = 0.0
    L.new(msk.outputs["Color"], bstr.inputs["Value"])
    bump = n.new("ShaderNodeBump"); bump.inputs["Distance"].default_value = 0.0004
    L.new(bstr.outputs[0], bump.inputs["Strength"]); L.new(noise.outputs["Fac"], bump.inputs["Height"])
    L.new(bump.outputs[0], bsdf.inputs["Normal"])
    L.new(bsdf.outputs[0], out.inputs[0])
    return m


def ceramic_material(name, rgb, rough=0.44, knurl=False):
    m = bpy.data.materials.new(name)
    nt, out = nodes_of(m)
    n, L = nt.nodes, nt.links
    bsdf = n.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Base Color"].default_value = (*lin(rgb), 1)
    bsdf.inputs["Specular IOR Level"].default_value = 0.42
    tc = n.new("ShaderNodeTexCoord")
    noise = n.new("ShaderNodeTexNoise"); noise.inputs["Scale"].default_value = 60; noise.inputs["Detail"].default_value = 6
    L.new(tc.outputs["Object"], noise.inputs["Vector"])
    r = n.new("ShaderNodeMapRange"); r.inputs["To Min"].default_value = rough - 0.05; r.inputs["To Max"].default_value = rough + 0.05
    L.new(noise.outputs["Fac"], r.inputs["Value"]); L.new(r.outputs[0], bsdf.inputs["Roughness"])
    fine = n.new("ShaderNodeTexNoise"); fine.inputs["Scale"].default_value = 1400; fine.inputs["Detail"].default_value = 3
    L.new(tc.outputs["Object"], fine.inputs["Vector"])
    bump = n.new("ShaderNodeBump"); bump.inputs["Strength"].default_value = 0.03; bump.inputs["Distance"].default_value = 0.0003
    L.new(fine.outputs["Fac"], bump.inputs["Height"]); L.new(bump.outputs[0], bsdf.inputs["Normal"])
    L.new(bsdf.outputs[0], out.inputs[0])
    return m


def teak_material():
    """The editor's own teak, mapped so the grain runs along the cheek's length on every face."""
    m = bpy.data.materials.new("ANDOR Teak (capture)")
    nt, out = nodes_of(m)
    n, L = nt.nodes, nt.links
    tc = n.new("ShaderNodeTexCoord"); tc.object = root
    geo = n.new("ShaderNodeNewGeometry")
    sep = n.new("ShaderNodeSeparateXYZ"); L.new(tc.outputs["Object"], sep.inputs[0])
    nsep = n.new("ShaderNodeSeparateXYZ"); L.new(geo.outputs["Normal"], nsep.inputs[0])
    # Faces pointing along z (top) take u from x; side faces take u from z. v always from y.
    absn = n.new("ShaderNodeMath"); absn.operation = "ABSOLUTE"; L.new(nsep.outputs[2], absn.inputs[0])
    is_top = n.new("ShaderNodeMath"); is_top.operation = "GREATER_THAN"; is_top.inputs[1].default_value = 0.6
    L.new(absn.outputs[0], is_top.inputs[0])
    mixu = n.new("ShaderNodeMix"); mixu.data_type = "FLOAT"
    L.new(is_top.outputs[0], mixu.inputs["Factor"])
    L.new(sep.outputs[2], mixu.inputs["A"]); L.new(sep.outputs[0], mixu.inputs["B"])
    strip_w = 20 * PX  # teak_strip.png spans 20 editor px across, 1055 along
    u = n.new("ShaderNodeMath"); u.operation = "DIVIDE"; u.inputs[1].default_value = strip_w
    L.new(mixu.outputs[0], u.inputs[0])
    v = n.new("ShaderNodeMath"); v.operation = "MULTIPLY_ADD"; v.inputs[1].default_value = 1 / (1055 * PX); v.inputs[2].default_value = 0.5
    L.new(sep.outputs[1], v.inputs[0])
    uv = n.new("ShaderNodeCombineXYZ"); L.new(u.outputs[0], uv.inputs[0]); L.new(v.outputs[0], uv.inputs[1])
    tex = n.new("ShaderNodeTexImage"); tex.image = teak_img; tex.extension = "MIRROR"; tex.interpolation = "Cubic"
    L.new(uv.outputs[0], tex.inputs[0])
    bsdf = n.new("ShaderNodeBsdfPrincipled")
    # Colour-matched so the rendered teak reads like the editor's teak under AgX (verified by
    # sampling the render against the capture; see documentation/materials.md).
    hsv = n.new("ShaderNodeHueSaturation")
    hsv.inputs["Saturation"].default_value = 1.18
    hsv.inputs["Value"].default_value = 0.72
    L.new(tex.outputs["Color"], hsv.inputs["Color"])
    L.new(hsv.outputs["Color"], bsdf.inputs["Base Color"])
    # Oiled wood: satin; grain felt through a bump from the texture's own luminance.
    rr = n.new("ShaderNodeMapRange"); rr.inputs["To Min"].default_value = 0.72; rr.inputs["To Max"].default_value = 0.55
    L.new(tex.outputs["Color"], rr.inputs["Value"]); L.new(rr.outputs[0], bsdf.inputs["Roughness"])
    bsdf.inputs["Specular IOR Level"].default_value = 0.35
    bump = n.new("ShaderNodeBump"); bump.inputs["Strength"].default_value = 0.12; bump.inputs["Distance"].default_value = 0.0002
    L.new(tex.outputs["Color"], bump.inputs["Height"]); L.new(bump.outputs[0], bsdf.inputs["Normal"])
    L.new(bsdf.outputs[0], out.inputs[0])
    return m


def clay_material():
    m = bpy.data.materials.new("Clay")
    nt, out = nodes_of(m)
    b = nt.nodes.new("ShaderNodeBsdfPrincipled")
    b.inputs["Base Color"].default_value = (0.55, 0.55, 0.55, 1)
    b.inputs["Roughness"].default_value = 0.6
    nt.links.new(b.outputs[0], out.inputs[0])
    return m


MAT_FACE = face_material()
MAT_TRAY = ceramic_material("Ceramic Tray", (214, 203, 189), 0.46)     # the editor's outer frame
MAT_PANEL = ceramic_material("Ceramic Ivory", (232, 224, 211), 0.40)   # the raised panel's edge
MAT_KNOB = ceramic_material("Ceramic Knob", (236, 230, 219), 0.34)     # knob skirts
MAT_TEAK = teak_material()
CLAY = clay_material() if args.variant == "clay" else None


# ---------------------------------------------------------------------------------------------
# Geometry helpers
def rounded_rect(bm, x0, y0, x1, y1, r, z, seg=10):
    """A rounded-rectangle ring of vertices (counter-clockwise) at height z."""
    pts = []
    corners = [(x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180), (x1 - r, y0 + r, 270)]
    for cx, cy, a0 in corners:
        for i in range(seg + 1):
            a = math.radians(a0 + 90 * i / seg)
            pts.append(bm.verts.new((cx + r * math.cos(a), cy + r * math.sin(a), z)))
    return pts


def prism(name, x0, y0, x1, y1, r, z0, z1, mat_top, mat_side, bevel=0.0, seg=10, product=True):
    """A rounded-rect slab; top faces get mat_top, everything else mat_side."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    lo = rounded_rect(bm, x0, y0, x1, y1, r, z0, seg)
    hi = rounded_rect(bm, x0, y0, x1, y1, r, z1, seg)
    bm.faces.new(list(reversed(lo)))
    bm.faces.new(hi)
    n = len(lo)
    for i in range(n):
        bm.faces.new((lo[i], lo[(i + 1) % n], hi[(i + 1) % n], hi[i]))
    bm.to_mesh(me)
    bm.free()
    o = new_obj(name, me, PRODUCT if product else FLOORC)
    if product:
        o.parent = root
    me.materials.append(mat_top)
    me.materials.append(mat_side)
    if bevel > 0:
        b = o.modifiers.new("Bevel", "BEVEL")
        b.width = bevel
        b.segments = 5
        b.limit_method = "ANGLE"
        b.angle_limit = math.radians(50)
        b.harden_normals = True
        o.modifiers.new("Normals", "WEIGHTED_NORMAL").keep_sharp = True
    return o


def assign_by_normal(o, top_index=0, side_index=1, thresh=0.7):
    """After modifiers: top-facing polygons take material 0, the rest material 1."""
    deps = bpy.context.evaluated_depsgraph_get()
    ev = o.evaluated_get(deps)
    me = bpy.data.meshes.new_from_object(ev)
    for poly in me.polygons:
        poly.material_index = top_index if poly.normal.z > thresh else side_index
    me.shade_smooth()
    old = o.data
    o.modifiers.clear()
    o.data = me
    bpy.data.meshes.remove(old)
    # keep flat tops flat: mark edges between top and side faces sharp
    bm = bmesh.new()
    bm.from_mesh(me)
    for e in bm.edges:
        if len(e.link_faces) == 2:
            a, b = e.link_faces
            if a.normal.dot(b.normal) < math.cos(math.radians(35)):
                e.smooth = False
    for f in bm.faces:
        if f.normal.z > 0.999:
            f.smooth = False
    bm.to_mesh(me)
    bm.free()


def knob(name, cx, cy, r, h, z0, knurl=0):
    """A ceramic knob cap revolved from a profile; knurled skirt for the macro knobs."""
    seg = 192 if knurl else 128
    prof = [  # (radius, height) from the base up to the edge of the flat top
        (r * 0.985, 0.0), (r, 0.0004), (r, h * 0.78), (r * 0.995, h * 0.90),
        (r * 0.975, h * 0.965), (r * 0.95, h)]
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    rings = []
    for j in range(seg):
        a = 2 * math.pi * j / seg
        k = 1.0
        if knurl:
            k = 1.0 + 0.012 * max(-0.6, math.cos(knurl * a))
        ring = []
        for i, (pr, pz) in enumerate(prof):
            kk = k if 0 < i < 3 else 1.0
            ring.append(bm.verts.new((cx + pr * kk * math.cos(a), cy + pr * kk * math.sin(a), z0 + pz)))
        rings.append(ring)
    for j in range(seg):
        a, b = rings[j], rings[(j + 1) % seg]
        for i in range(len(prof) - 1):
            bm.faces.new((a[i], b[i], b[i + 1], a[i + 1]))
    top = bm.faces.new([rings[j][-1] for j in range(seg)])
    bm.faces.new([rings[j][0] for j in reversed(range(seg))])
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    o = new_obj(name, me, PRODUCT)
    o.parent = root
    me.materials.append(MAT_FACE)
    me.materials.append(MAT_KNOB)
    for poly in me.polygons:
        poly.material_index = 0 if poly.normal.z > 0.995 else 1
        poly.use_smooth = poly.material_index == 1
    # The face texture already carries each knob's drawn shadow: no second one from the cap.
    o.visible_shadow = False
    return o


# ---------------------------------------------------------------------------------------------
# The instrument
TRAY_R = 6 * PX
body = prism("Body", -W / 2, -H / 2, W / 2, H / 2, 2.2 * TRAY_R, 0.0, T, MAT_FACE, MAT_TRAY, bevel=0.0016)
assign_by_normal(body)

px0, py0, px1, py1 = GEO["panel"]["rect"]
panel = prism("Ceramic Panel", ex(px0), ey(py1), ex(px1), ey(py0), GEO["panel"]["radius"] * PX,
              T - 0.001, T + PANEL_RISE, MAT_FACE, MAT_PANEL, bevel=0.0005, seg=8)
assign_by_normal(panel)

for i, ch in enumerate(GEO["cheeks"]):
    x0, y0, x1, y1 = ch["rect"]
    # Full-depth end cheeks: flush with the body's outer edge, as wide and long as drawn.
    x0e, x1e = (ex(0), ex(x1)) if i == 0 else (ex(x0), ex(EW))
    c = prism(f"Teak Cheek {'LR'[i]}", x0e, ey(y1), x1e, ey(y0), 7 * PX, -0.0002, T + CHEEK_RISE,
              MAT_TEAK, MAT_TEAK, bevel=0.0018, seg=8)
    assign_by_normal(c)

for i, k in enumerate(GEO["knobs"]):
    kx, ky = k["c"]
    knob(f"Knob {k['kind']} {i:02d}", ex(kx), ey(ky), k["r"] * PX, KNOB_H[k["kind"]], T + PANEL_RISE - 0.0002,
         knurl=48 if k["kind"] == "macro" else 0)

if CLAY:
    for o in root.children:
        o.data.materials.clear()
        o.data.materials.append(CLAY)

# Pose: floating, turned a little in its own plane.
POSE = {"hero_9x16": (FLOAT, -9.0), "gallery": (FLOAT, 0.0), "closeup": (FLOAT, -9.0)}[args.camera]
root.location = (0, 0, POSE[0])
root.rotation_euler = (0, 0, math.radians(POSE[1]))

# ---------------------------------------------------------------------------------------------
# Backdrop: a dark architectural floor of long diagonal planks with fine grooves and one dark
# bronze inlay, each plank at a slightly different height and finish.
def floor_material(name, rgb, rough, groove_scale, metal=0.0):
    m = bpy.data.materials.new(name)
    nt, out = nodes_of(m)
    n, L = nt.nodes, nt.links
    bsdf = n.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Base Color"].default_value = (*rgb, 1)
    bsdf.inputs["Metallic"].default_value = metal
    bsdf.inputs["Specular IOR Level"].default_value = 0.3
    tc = n.new("ShaderNodeTexCoord")
    # grooves run along the plank (local x)
    wave = n.new("ShaderNodeTexWave"); wave.wave_type = "BANDS"; wave.bands_direction = "Y"
    wave.inputs["Scale"].default_value = groove_scale; wave.inputs["Distortion"].default_value = 0.6
    wave.inputs["Detail"].default_value = 2
    L.new(tc.outputs["Object"], wave.inputs["Vector"])
    noise = n.new("ShaderNodeTexNoise"); noise.inputs["Scale"].default_value = 4; noise.inputs["Detail"].default_value = 8
    L.new(tc.outputs["Object"], noise.inputs["Vector"])
    r = n.new("ShaderNodeMapRange"); r.inputs["To Min"].default_value = rough - 0.08; r.inputs["To Max"].default_value = rough + 0.1
    L.new(noise.outputs["Fac"], r.inputs["Value"]); L.new(r.outputs[0], bsdf.inputs["Roughness"])
    bump = n.new("ShaderNodeBump"); bump.inputs["Strength"].default_value = 0.35; bump.inputs["Distance"].default_value = 0.0008
    L.new(wave.outputs["Fac"], bump.inputs["Height"]); L.new(bump.outputs[0], bsdf.inputs["Normal"])
    # subtle tonal variation along the plank
    cv = n.new("ShaderNodeMix"); cv.data_type = "RGBA"; cv.blend_type = "MULTIPLY"
    cv.inputs[0].default_value = 0.35          # Factor
    cv.inputs[6].default_value = (*rgb, 1)     # A (colour)
    L.new(noise.outputs["Color"], cv.inputs[7])  # B (colour)
    L.new(cv.outputs[2], bsdf.inputs["Base Color"])
    L.new(bsdf.outputs[0], out.inputs[0])
    return m


FLOOR = [  # width (m), colour (linear), roughness, groove scale, height offset, metal
    (0.32, (0.020, 0.016, 0.013), 0.48, 140, 0.000, 0.0),   # peat charcoal
    (0.006, (0.12, 0.075, 0.040), 0.30, 400, 0.0015, 1.0),  # dark bronze inlay
    (0.26, (0.045, 0.029, 0.019), 0.38, 220, 0.004, 0.0),   # deep warm brown
    (0.18, (0.026, 0.020, 0.016), 0.52, 120, 0.006, 0.0),   # charcoal
    (0.38, (0.082, 0.052, 0.033), 0.30, 260, 0.002, 0.0),   # muted walnut: the lighter band behind the instrument
    (0.006, (0.12, 0.075, 0.040), 0.30, 400, 0.0035, 1.0),
    (0.30, (0.040, 0.027, 0.019), 0.40, 200, 0.001, 0.0),   # brown
    (0.40, (0.022, 0.018, 0.015), 0.50, 150, 0.005, 0.0),   # charcoal
]
FLOOR_ANGLE = -78.0  # plank direction in the floor plane (degrees)
FLOORC = bpy.data.collections.new("Backdrop")
coll.children.link(FLOORC)
floor_root = bpy.data.objects.new("Backdrop", None)
FLOORC.objects.link(floor_root)
floor_root.rotation_euler = (0, 0, math.radians(FLOOR_ANGLE))
total = sum(f[0] + 0.003 for f in FLOOR)
yy = -total / 2
floor_objs = []
for i, (w, rgb, rough, gs, dz, metal) in enumerate(FLOOR):
    m = floor_material(f"Backdrop {i}", rgb, rough, gs, metal)
    o = prism(f"Plank {i}", -2.0, yy, 2.0, yy + w, 0.001, -0.03 + dz, 0.0 + dz, m, m, bevel=0.0012 if w > 0.01 else 0.0, seg=1, product=False)
    o.parent = floor_root
    floor_objs.append(o)
    yy += w + 0.003
groove = prism("Plank grooves", -2.0, -total / 2 - 0.02, 2.0, total / 2 + 0.02, 0.001, -0.06, -0.02,
               floor_material("Groove", (0.006, 0.005, 0.004), 0.7, 50), floor_material("Groove", (0.006, 0.005, 0.004), 0.7, 50), product=False)
groove.parent = floor_root
floor_objs.append(groove)

if args.variant == "alpha":
    for o in floor_objs:
        o.hide_render = True
    catcher = prism("Shadow catcher", -2, -2, 2, 2, 0.01, -0.01, 0.0, MAT_TRAY, MAT_TRAY, product=False)
    catcher.is_shadow_catcher = True
    bpy.context.view_layer.cycles.use_pass_shadow_catcher = True

# ---------------------------------------------------------------------------------------------
# Lights: key softbox upper left, broad fill opposite, narrow rim behind/above, glass card.
def area(name, loc, target, size, power, shape="RECTANGLE", size_y=None, temp_rgb=(1, 1, 1), spread=180, only=None):
    d = bpy.data.lights.new(name, "AREA")
    d.shape = shape
    d.size = size
    if size_y is not None:
        d.size_y = size_y
    d.energy = power
    d.color = temp_rgb
    d.spread = math.radians(spread)
    o = bpy.data.objects.new(name, d)
    coll.objects.link(o)
    o.location = loc
    direction = Vector(target) - Vector(loc)
    o.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    if only is not None:  # light linking: this light illuminates only `only`
        o.light_linking.receiver_collection = only
    return o


tgt = (0, 0, FLOAT + T)
# Product lights are linked to the instrument; the floor has its own overhead and raking lights,
# in which the instrument still casts its shadow (light linking, not shadow linking).
area("Key softbox", (-0.85, 0.55, FLOAT + 1.05), tgt, 0.8, 11, size_y=0.55, temp_rgb=(1.0, 0.97, 0.93), only=PRODUCT)
area("Fill", (0.75, -1.05, FLOAT + 0.30), tgt, 1.6, 5.5, size_y=1.2, temp_rgb=(0.93, 0.95, 1.0), only=PRODUCT)
area("Rim", (0.15, 1.05, FLOAT + 0.26), tgt, 1.4, 40, size_y=0.12, temp_rgb=(1.0, 0.93, 0.85), spread=70, only=PRODUCT)
area("Floor overhead", (-0.55, 0.45, 1.5), (0.08, -0.08, 0.0), 1.0, 34, size_y=0.9, temp_rgb=(1.0, 0.95, 0.88), only=FLOORC)
area("Floor rake", (-1.6, 0.9, 0.05), (0.4, -0.2, 0.0), 2.8, 26, size_y=0.18, temp_rgb=(1.0, 0.84, 0.68), spread=50, only=FLOORC)

world = bpy.data.worlds.new("Studio")
scene.world = world
world.use_nodes = True
bg = world.node_tree.nodes["Background"]
bg.inputs["Color"].default_value = (0.020, 0.018, 0.016, 1)
bg.inputs["Strength"].default_value = 0.12

# ---------------------------------------------------------------------------------------------
# Cameras
def camera(name, yaw, pitch, dist, lens, target, fstop=None, shift=(0.0, 0.0), roll=0.0):
    cd = bpy.data.cameras.new(name)
    cd.lens = lens
    cd.sensor_fit = "HORIZONTAL"
    cd.sensor_width = 36
    cd.shift_x, cd.shift_y = shift
    o = bpy.data.objects.new(name, cd)
    coll.objects.link(o)
    # direction from target to camera: tilted `pitch` from the face normal toward the viewer
    # (−y), then turned `yaw` around the normal.
    d = Vector((0, -math.sin(math.radians(pitch)), math.cos(math.radians(pitch))))
    d.rotate(Euler((0, 0, math.radians(yaw))))
    o.location = Vector(target) + d * dist
    q = (Vector(target) - o.location).to_track_quat("-Z", "Y")
    o.rotation_euler = q.to_euler()
    o.rotation_euler.rotate_axis("Z", math.radians(roll))
    if fstop:
        cd.dof.use_dof = True
        cd.dof.focus_distance = (Vector(target) - o.location).length
        cd.dof.aperture_fstop = fstop
        cd.dof.aperture_blades = 9
    return o


cams = {
    "hero_9x16": camera("Camera 01 Hero", yaw=-24, pitch=27, dist=1.12, lens=75, target=(0, 0.005, FLOAT + T), fstop=9, shift=(0, 0.07)),
    "gallery": camera("Camera 02 Gallery", yaw=0, pitch=3, dist=2.6, lens=135, target=(0, 0, FLOAT + T)),
    "closeup": camera("Camera 03 Close-up", yaw=-24, pitch=38, dist=0.42, lens=90, target=(ex(274) , ey(818), FLOAT + T), fstop=4),
}
scene.camera = cams[args.camera]

if args.save_blend:
    bpy.ops.file.pack_all()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(args.save_blend))

if args.border:
    scene.render.use_border = True
    scene.render.use_crop_to_border = True
    scene.render.border_min_x, scene.render.border_max_x, scene.render.border_min_y, scene.render.border_max_y = args.border

if args.out:
    out = os.path.abspath(args.out)
    if args.variant == "alpha":
        # Combined (product, no catcher shadow) plus the shadow pass, written by the compositor.
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
        fo.format.file_format = "PNG"
        fo.format.color_mode = "RGB"
        fo.format.color_depth = "16"
        # The shadow pass is a multiplier (1 = no shadow): write it without the view transform.
        fo.format.color_management = "OVERRIDE"
        fo.format.view_settings.view_transform = "Standard"
        fo.format.view_settings.look = "None"
        fo.format.view_settings.exposure = 0.0
        fo.file_slots[0].path = os.path.splitext(os.path.basename(out))[0] + "_shadow_"
        ct.links.new(rl.outputs["Shadow Catcher"], fo.inputs[0])
    scene.render.filepath = out
    bpy.ops.render.render(write_still=True)
    print("wrote", out)
