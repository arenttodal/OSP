"""A warm room that only has to read out of focus: the background plate behind a flat GUI hero.

Runs with the bpy module (no Blender UI), on the CPU:
  python room_bokeh.py --out <file.png> [--res 2560x1440] [--samples 128] [--fstop 1.4]
         [--focus 1.3] [--light evening|day] [--portrait]

Nothing in frame is in focus: the camera focuses in front of the room, so the wall, shelf, lamp
and window melt into shapes and the plugin (placed over it in HTML) is the only sharp thing.
The left third stays dark (a plant against a dim window) for the headline; the light falls on the
right, where the plugin sits: sun through leaves onto a plaster wall, a lit lamp, books, a frame.
Every random placement comes from a fixed seed, so a re-render is identical.
"""
import argparse, math, random, sys
import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
ap = argparse.ArgumentParser()
ap.add_argument("--out", required=True)
ap.add_argument("--res", default="2560x1440")
ap.add_argument("--samples", type=int, default=128)
ap.add_argument("--fstop", type=float, default=0.9)
ap.add_argument("--focus", type=float, default=0.85, help="focus distance in metres (the wall is at about 3.6 m)")
ap.add_argument("--light", default="evening")
ap.add_argument("--portrait", action="store_true", help="aim at the lit right half for a phone plate")
a = ap.parse_args(argv)
rng = random.Random(7)

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
W, H = (int(v) for v in a.res.lower().split("x"))
scene.render.resolution_x, scene.render.resolution_y = W, H
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = a.samples
scene.cycles.use_denoising = True
scene.cycles.max_bounces = 6
scene.view_settings.view_transform = "AgX"
scene.view_settings.look = "AgX - Medium High Contrast"
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGB"
scene.render.filepath = a.out

EVENING = a.light == "evening"
SUN_COL = (1.0, 0.62, 0.34) if EVENING else (1.0, 0.86, 0.70)
SUN_W = 5.5 if EVENING else 7.0
WORLD = (0.012, 0.009, 0.007) if EVENING else (0.05, 0.045, 0.04)

world = bpy.data.worlds.new("w"); scene.world = world; world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (*WORLD, 1)


def mat(name, color, rough=0.6, emit=None, strength=0.0, noise=0.0, metal=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; b = nt.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if emit:
        b.inputs["Emission Color"].default_value = (*emit, 1)
        b.inputs["Emission Strength"].default_value = strength
    if noise:   # plaster / grain: a little value variation and bump
        tex = nt.nodes.new("ShaderNodeTexNoise"); tex.inputs["Scale"].default_value = 9.0; tex.inputs["Detail"].default_value = 8
        ramp = nt.nodes.new("ShaderNodeMixRGB"); ramp.blend_type = "MULTIPLY"; ramp.inputs[0].default_value = noise
        ramp.inputs[1].default_value = (*color, 1)
        nt.links.new(tex.outputs["Fac"], ramp.inputs[2]); nt.links.new(ramp.outputs[0], b.inputs["Base Color"])
        bump = nt.nodes.new("ShaderNodeBump"); bump.inputs["Strength"].default_value = 0.25
        nt.links.new(tex.outputs["Fac"], bump.inputs["Height"]); nt.links.new(bump.outputs["Normal"], b.inputs["Normal"])
    return m


def box(name, size, loc, m, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc, rotation=rot)
    o = bpy.context.object; o.name = name; o.scale = size; o.data.materials.append(m); return o


def cyl(name, r, h, loc, m, verts=48):
    bpy.ops.mesh.primitive_cylinder_add(radius=r, depth=h, location=loc, vertices=verts)
    o = bpy.context.object; o.name = name; o.data.materials.append(m)
    bpy.ops.object.shade_smooth(); return o


plaster = mat("plaster", (0.50, 0.38, 0.27), 0.9, noise=0.35)
walnut = mat("walnut", (0.10, 0.05, 0.025), 0.32, noise=0.5)
shelfwood = mat("shelf", (0.16, 0.09, 0.05), 0.6)
dark = mat("dark", (0.02, 0.018, 0.016), 0.7)
leaf = mat("leaf", (0.018, 0.03, 0.012), 0.55)

# room: back wall, a side wall on the left with a window, the desk across the foreground
box("wall", (9, 0.1, 4), (0, 3.65, 2), plaster)
box("lwall", (0.1, 6, 4), (-2.2, 1.6, 2), plaster)
box("ceiling", (9, 6, 0.1), (0, 1.6, 3.3), dark)
box("desk", (6, 2.2, 0.06), (0, 1.55, 0.72), walnut)
win = mat("window", (0.0, 0.0, 0.0), 1.0, emit=(0.78, 0.62, 0.50) if EVENING else (0.92, 0.94, 1.0), strength=0.13 if EVENING else 1.2)
box("window", (0.9, 0.02, 1.6), (-1.25, 3.59, 1.85), win)     # on the back wall, at the left edge of frame
box("mullion", (0.04, 0.04, 1.6), (-1.25, 3.57, 1.85), dark)   # it reads as a window even blurred
box("mullionh", (0.9, 0.04, 0.04), (-1.25, 3.57, 2.05), dark)

# shelf with books on the right
box("shelf1", (0.9, 0.28, 0.03), (1.75, 3.45, 1.55), shelfwood)
box("shelf2", (0.9, 0.28, 0.03), (1.75, 3.45, 1.95), shelfwood)
spines = [(0.28, 0.12, 0.07), (0.45, 0.36, 0.22), (0.12, 0.13, 0.10), (0.36, 0.20, 0.10), (0.55, 0.48, 0.36), (0.18, 0.09, 0.06)]
for zs in (1.565, 1.965):
    x = 1.36
    while x < 2.15:
        w = rng.uniform(0.025, 0.05); h = rng.uniform(0.2, 0.3)
        box("book", (w, 0.2, h), (x + w / 2, 3.45, zs + h / 2), mat("b", rng.choice(spines), 0.7))
        x += w + 0.004

# a frame on the wall, centre right, catching the sun
box("frame", (0.62, 0.03, 0.80), (0.95, 3.59, 1.95), dark)
box("print", (0.52, 0.01, 0.68), (0.95, 3.57, 1.95), mat("print", (0.22, 0.20, 0.17), 0.8, noise=0.6))

# the lamp: a woven shade glowing on the desk at the right edge, a bulb inside
shade = mat("shade", (0.8, 0.55, 0.3), 0.8, emit=(1.0, 0.50, 0.20), strength=1.6, noise=0.7)
cyl("shade", 0.19, 0.34, (1.22, 2.35, 1.17), shade)
cyl("lampbase", 0.05, 0.28, (1.22, 2.35, 0.89), mat("brass", (0.55, 0.38, 0.18), 0.3, metal=1.0))
bpy.ops.object.light_add(type="POINT", location=(1.22, 2.35, 1.17))
bulb = bpy.context.object; bulb.data.energy = 45; bulb.data.color = (1.0, 0.6, 0.3); bulb.data.shadow_soft_size = 0.12

# a vase with dry branches by the lamp
cyl("vase", 0.08, 0.22, (0.85, 2.1, 0.86), mat("vase", (0.10, 0.07, 0.04), 0.35))
for i in range(9):
    ang = rng.uniform(-0.6, 0.6); tilt = rng.uniform(0.1, 0.45); L = rng.uniform(0.35, 0.6)
    d = Vector((math.sin(ang) * math.sin(tilt), -0.1 * math.sin(tilt), math.cos(tilt)))
    base = Vector((0.85, 2.1, 0.97)); mid = base + d * L / 2
    bpy.ops.mesh.primitive_cylinder_add(radius=0.004, depth=L, location=mid)
    st = bpy.context.object; st.rotation_mode = "QUATERNION"; st.rotation_quaternion = d.to_track_quat("Z", "Y"); st.data.materials.append(dark)
    for k in range(5):
        p = base + d * L * rng.uniform(0.5, 1.0) + Vector((rng.uniform(-.04, .04), 0, rng.uniform(-.03, .03)))
        bpy.ops.mesh.primitive_uv_sphere_add(radius=0.012, location=p, segments=8, ring_count=6)
        bpy.context.object.data.materials.append(mat("bud", (0.45, 0.30, 0.18), 0.7))

# a plant in the left foreground, close to the lens: dark leaves against the dim window
def leafy(cx, cy, cz, n, spread, size, m):
    for i in range(n):
        p = (cx + rng.gauss(0, spread), cy + rng.gauss(0, spread * 0.6), cz + abs(rng.gauss(0, spread * 1.2)))
        bpy.ops.mesh.primitive_uv_sphere_add(radius=1, location=p, segments=16, ring_count=8)
        o = bpy.context.object; s = size * rng.uniform(0.7, 1.3)
        o.scale = (s, s * 0.08, s * 0.45); o.rotation_euler = (rng.uniform(-0.6, 0.6), rng.uniform(0, math.pi), rng.uniform(-0.8, 0.8))
        o.data.materials.append(m)

leafy(-0.55, -0.05, 0.95, 60, 0.18, 0.05, leaf)
leafy(-1.55, 2.6, 1.2, 50, 0.32, 0.09, leaf)

# the sun through leaves and a window frame: an occluder out of frame on the left
sun_dir = Vector((1.0, 0.85, -0.42)).normalized()
bpy.ops.object.light_add(type="SUN")
sun = bpy.context.object; sun.data.energy = SUN_W; sun.data.color = SUN_COL; sun.data.angle = math.radians(1.2)
sun.rotation_mode = "QUATERNION"; sun.rotation_quaternion = (-sun_dir).to_track_quat("Z", "Y")
# the side wall must not block the sun: open a hole by making it a holdout for the light path
bpy.data.objects["lwall"].visible_shadow = False
bpy.data.objects["ceiling"].visible_shadow = False
# an outer wall with one opening, so the sun lands as a patch (centre right of the back wall), not a wash
for size, loc in (((0.05, 7, 3.0), (-2.6, 1.5, 0.9)), ((0.05, 7, 2.0), (-2.6, 1.5, 4.8)),
                  ((0.05, 2.2, 1.6), (-2.6, -0.9, 3.1)), ((0.05, 3.6, 1.6), (-2.6, 3.4, 3.1))):
    box("outer", size, loc, dark)
box("mull_v", (0.05, 0.05, 1.4), (-2.6, 0.9, 3.1), dark)
box("mull_h", (0.05, 1.4, 0.05), (-2.6, 0.9, 3.1), dark)
leafy(-3.1, 0.9, 2.9, 55, 0.4, 0.16, leaf)       # outside, between the sun and the opening
for o in bpy.data.objects:
    if o.name.startswith(("outer", "mull_", "Sphere")) and o.location.x < -2.5:
        o.visible_camera = False                       # it only shapes the light

# a low warm fill from the front right, so the shadowed wall is not black
bpy.ops.object.light_add(type="AREA", location=(1.5, -1.2, 1.6))
fill = bpy.context.object; fill.data.energy = 22 if EVENING else 120; fill.data.size = 2.0; fill.data.color = (1.0, 0.8, 0.62)
fill.rotation_mode = "QUATERNION"; fill.rotation_quaternion = (Vector((0.2, 3.6, 1.6)) - fill.location).to_track_quat("-Z", "Y")

# camera: 40 mm, a little above the desk, focused in front of everything
bpy.ops.object.camera_add(location=(0.15 if not a.portrait else 0.75, -0.55, 1.22))
cam = bpy.context.object; scene.camera = cam
cam.data.lens = 40 if not a.portrait else 30
target = Vector((0.15 if not a.portrait else 0.85, 3.6, 1.3))
cam.rotation_mode = "QUATERNION"; cam.rotation_quaternion = (target - cam.location).to_track_quat("-Z", "Y")
cam.data.dof.use_dof = True
cam.data.dof.focus_distance = a.focus
cam.data.dof.aperture_fstop = a.fstop
cam.data.dof.aperture_blades = 0

bpy.ops.render.render(write_still=True)
print("wrote", a.out)
