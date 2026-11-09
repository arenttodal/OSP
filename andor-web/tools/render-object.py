"""Studio render of the genuine OSP capture as a physical object (headless Blender 4.2 / bpy).

The face of the slab IS the capture (emission, Standard view transform: the interface's colours
are reproduced exactly, no relighting of the screen content). The slab's sides use colours sampled
from the capture's own edges (teak cheeks left/right, ceramic top/bottom). Nothing is invented: no
controls, ports, screws or labels are added. A shadow catcher gives a transparent PNG with a real
contact shadow for compositing on the site's paper background.

Usage (Python 3.11 + `pip install bpy==4.2.0`):
  python tools/render-object.py --image assets/source/captures/web-01-hero-1L@3x.png \
      --out assets/source/renders/object-1l.png --res 2400 --samples 96 [--view desk|stand]
"""
import argparse
import math
import sys

import bpy
from mathutils import Vector

p = argparse.ArgumentParser()
p.add_argument("--image", required=True)
p.add_argument("--out", required=True)
p.add_argument("--res", type=int, default=2400)
p.add_argument("--samples", type=int, default=96)
p.add_argument("--view", default="desk", choices=["desk", "stand"])
p.add_argument("--aspect", type=float, default=1448 / 1086)
args = p.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:])

TEAK = (0xBD / 255, 0x89 / 255, 0x66 / 255)
CERAMIC = (0xE2 / 255, 0xD9 / 255, 0xCB / 255)


def srgb_to_linear(c):
    return tuple(x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c)


bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = args.samples
scene.cycles.use_denoising = True
scene.render.film_transparent = True
scene.view_settings.view_transform = "Standard"  # exact screen colours (no filmic curve)
scene.view_settings.look = "None"
scene.render.resolution_x = args.res
scene.render.resolution_y = int(args.res * 0.78)
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA"
scene.render.image_settings.color_depth = "16"

# --- the object: 290 mm wide, proportional to the editor, 16 mm thick ------------------------
W = 0.29
H = W / args.aspect
T = 0.016
bpy.ops.mesh.primitive_cube_add(size=1)
body = bpy.context.active_object
body.name = "Body"
body.scale = (W, H, T)
body.location = (0, 0, T / 2)
bpy.ops.object.transform_apply(scale=True)
bev = body.modifiers.new("Bevel", "BEVEL")
bev.width = 0.0022
bev.segments = 6
bev.limit_method = "ANGLE"

side = bpy.data.materials.new("Sides")
side.use_nodes = True
nt = side.node_tree
bsdf = nt.nodes["Principled BSDF"]
bsdf.inputs["Roughness"].default_value = 0.42
# Choose teak for the left/right faces, ceramic elsewhere, by the face normal.
geo = nt.nodes.new("ShaderNodeNewGeometry")
sep = nt.nodes.new("ShaderNodeSeparateXYZ")
absx = nt.nodes.new("ShaderNodeMath"); absx.operation = "ABSOLUTE"
ramp = nt.nodes.new("ShaderNodeMath"); ramp.operation = "GREATER_THAN"; ramp.inputs[1].default_value = 0.6
mix = nt.nodes.new("ShaderNodeMix"); mix.data_type = "RGBA"
mix.inputs[6].default_value = (*srgb_to_linear(CERAMIC), 1)
mix.inputs[7].default_value = (*srgb_to_linear(TEAK), 1)
nt.links.new(geo.outputs["Normal"], sep.inputs[0])
nt.links.new(sep.outputs["X"], absx.inputs[0])
nt.links.new(absx.outputs[0], ramp.inputs[0])
nt.links.new(ramp.outputs[0], mix.inputs[0])
# Teak faces sample the capture's own teak cheek (x 6-20 px of 1448) along their length, so the
# grain on the slab's sides is the interface's real wood, not an invented texture.
tc = nt.nodes.new("ShaderNodeTexCoord")
sx = nt.nodes.new("ShaderNodeSeparateXYZ")
nt.links.new(tc.outputs["Object"], sx.inputs[0])
uz = nt.nodes.new("ShaderNodeMapRange")
uz.inputs["From Min"].default_value = -T / 2; uz.inputs["From Max"].default_value = T / 2
uz.inputs["To Min"].default_value = 6 / 1448; uz.inputs["To Max"].default_value = 20 / 1448
vy = nt.nodes.new("ShaderNodeMapRange")
vy.inputs["From Min"].default_value = -H / 2; vy.inputs["From Max"].default_value = H / 2
vy.inputs["To Min"].default_value = 0.02; vy.inputs["To Max"].default_value = 0.98
nt.links.new(sx.outputs["Z"], uz.inputs["Value"])
nt.links.new(sx.outputs["Y"], vy.inputs["Value"])
cmb = nt.nodes.new("ShaderNodeCombineXYZ")
nt.links.new(uz.outputs[0], cmb.inputs[0])
nt.links.new(vy.outputs[0], cmb.inputs[1])
grain = nt.nodes.new("ShaderNodeTexImage")
grain.image = bpy.data.images.load(args.image)
grain.interpolation = "Cubic"
nt.links.new(cmb.outputs[0], grain.inputs["Vector"])
nt.links.new(grain.outputs["Color"], mix.inputs[7])
nt.links.new(mix.outputs[2], bsdf.inputs["Base Color"])
body.data.materials.append(side)

# The face: a plane exactly on the top surface (inset by the bevel), carrying the capture.
inset = bev.width
bpy.ops.mesh.primitive_plane_add(size=1)
face = bpy.context.active_object
face.name = "Face"
face.scale = (W - 2 * inset * 0.35, H - 2 * inset * 0.35, 1)
face.location = (0, 0, T + 0.00005)
bpy.ops.object.transform_apply(scale=True)
fm = bpy.data.materials.new("Capture")
fm.use_nodes = True
fnt = fm.node_tree
for n in list(fnt.nodes):
    fnt.nodes.remove(n)
out = fnt.nodes.new("ShaderNodeOutputMaterial")
tex = fnt.nodes.new("ShaderNodeTexImage")
tex.image = bpy.data.images.load(args.image)
tex.interpolation = "Cubic"
emit = fnt.nodes.new("ShaderNodeEmission")
emit.inputs["Strength"].default_value = 1.0
# A thin clear coat so the face catches the softbox like glazed ceramic, without recolouring it.
gloss = fnt.nodes.new("ShaderNodeBsdfGlossy")
gloss.inputs["Roughness"].default_value = 0.22
gloss.inputs["Color"].default_value = (1, 1, 1, 1)
fres = fnt.nodes.new("ShaderNodeFresnel")
fres.inputs["IOR"].default_value = 1.38
add = fnt.nodes.new("ShaderNodeMixShader")
fnt.links.new(tex.outputs["Color"], emit.inputs["Color"])
coat = fnt.nodes.new("ShaderNodeMath"); coat.operation = "MULTIPLY"; coat.inputs[1].default_value = 0.16
fnt.links.new(fres.outputs[0], coat.inputs[0])
fnt.links.new(coat.outputs[0], add.inputs[0])
fnt.links.new(emit.outputs[0], add.inputs[1])
fnt.links.new(gloss.outputs[0], add.inputs[2])
fnt.links.new(add.outputs[0], out.inputs["Surface"])
face.data.materials.append(fm)

# Group under an empty for posing.
bpy.ops.object.empty_add(location=(0, 0, 0))
rig = bpy.context.active_object
body.parent = rig
face.parent = rig

# --- floor (shadow catcher) ---------------------------------------------------------------
bpy.ops.mesh.primitive_plane_add(size=6, location=(0, 0, 0))
floor = bpy.context.active_object
floor.is_shadow_catcher = True

# --- light: overhead softbox, warm key, cool-neutral fill --------------------------------
def area(name, loc, size, energy, color):
    bpy.ops.object.light_add(type="AREA", location=loc)
    l = bpy.context.active_object
    l.name = name
    l.data.shape = "RECTANGLE"
    l.data.size = size[0]
    l.data.size_y = size[1]
    l.data.energy = energy
    l.data.color = color
    l.rotation_euler = (Vector((0, 0, 0)) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    return l

area("Softbox", (-0.25, 0.35, 0.9), (0.9, 0.6), 20, (1.0, 0.96, 0.9))
area("Fill", (0.8, -0.6, 0.45), (0.8, 0.8), 6, (0.95, 0.97, 1.0))
area("Rim", (0.5, 0.9, 0.35), (0.5, 0.3), 5, (1.0, 0.92, 0.85))
world = bpy.data.worlds.new("World")
scene.world = world
world.use_nodes = True
world.node_tree.nodes["Background"].inputs["Color"].default_value = (*srgb_to_linear((0.97, 0.96, 0.94)), 1)
world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.22

# --- pose + camera -------------------------------------------------------------------------
bpy.ops.object.camera_add()
cam = bpy.context.active_object
scene.camera = cam
cam.data.lens = 85
if args.view == "desk":
    # Lying on a desk, seen from the player's side: front edge and right side visible.
    rig.rotation_euler = (0, 0, math.radians(-16))
    cam.location = (0.42, -0.98, 0.64)
    target = Vector((0.0, 0.0, 0.0))
else:
    # Standing, leaned back on its stand like the EP-133 shot: strong depth, face legible.
    rig.rotation_euler = (math.radians(72), 0, math.radians(-24))
    rig.location = (0, 0.03, H * 0.47)
    cam.location = (0.50, -0.80, 0.27)
    target = Vector((0.012, 0, H * 0.43))
cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
# Frame the object.
bpy.context.view_layer.update()
cam.data.sensor_fit = "HORIZONTAL"
cam.data.lens = 88 if args.view == "desk" else 92

scene.render.filepath = args.out
bpy.ops.render.render(write_still=True)
print("wrote", args.out)
