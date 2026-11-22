"""ANDOR screen mockup studio: a real GUI capture on a minimal display, at an angle, in a soft studio.

Runs with the bpy module (no Blender UI), on the CPU:
  python screen_mockup.py --image <screen content PNG> --preset <name> --out <file.png>
         [--res 1920x1080] [--samples 96] [--tone light|dark|warm] [--focus u,v] [--zoom 1.0]

The screen content is any image (a GUI capture, or a desktop composition with a DAW behind the
plugin); it is fitted to the screen at its own aspect ratio (no stretching). Presets:
  float        front-on, slightly turned, floating over the floor with a soft contact shadow
  three        three-quarter view from the left, a little above; focus on the near edge
  low          low camera, screen leaning back: monumental
  macro        close and raking along the screen onto one region (--focus u,v in 0..1), shallow focus
  curved       a wide curved display, centred, slightly above
  desk         a display on a stand, seen across a surface
The screen is lit by its own emission and shaded with a glass coat, so the GUI's colours stay true
(Standard view transform) while the glass still catches the studio's highlights.
"""
import argparse, math, sys
import bpy
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
ap = argparse.ArgumentParser()
ap.add_argument("--image", required=True)
ap.add_argument("--preset", default="three")
ap.add_argument("--out", required=True)
ap.add_argument("--res", default="1920x1080")
ap.add_argument("--samples", type=int, default=96)
ap.add_argument("--tone", default="light")
ap.add_argument("--focus", default="0.5,0.5")
ap.add_argument("--zoom", type=float, default=1.0)
ap.add_argument("--screen-aspect", type=float, default=0.0, help="screen width/height; 0 = the image's")
ap.add_argument("--transparent", action="store_true")
ap.add_argument("--orbs", action="store_true", help="frosted glass spheres around the display")
a = ap.parse_args(argv)

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
W, H = (int(v) for v in a.res.lower().split("x"))
scene.render.resolution_x, scene.render.resolution_y = W, H
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = a.samples
scene.cycles.use_denoising = True
scene.cycles.max_bounces = 6
scene.render.film_transparent = a.transparent
scene.view_settings.view_transform = "Standard"
scene.view_settings.look = "None"
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA" if a.transparent else "RGB"
scene.render.filepath = a.out

TONES = {  # floor/backdrop colour, world colour, key strength
    "light": ((0.62, 0.61, 0.59), (0.55, 0.54, 0.52), 260),
    "dark": ((0.010, 0.010, 0.011), (0.004, 0.004, 0.005), 160),
    "warm": ((0.62, 0.40, 0.24), (0.55, 0.35, 0.21), 240),
}
floor_col, world_col, key_w = TONES[a.tone]


def mat(name, **p):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    for k, v in p.items():
        b.inputs[k].default_value = v
    return m


# world
world = bpy.data.worlds.new("w"); scene.world = world; world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (*world_col, 1)
world.node_tree.nodes["Background"].inputs[1].default_value = 0.25 if a.tone != "dark" else 0.05

# seamless backdrop: a floor that curves up into a wall 2.6 m behind the device, one surface
YW, R, N = 2.6, 1.6, 24
prof = [(-30.0, 0.0)]
for i in range(N + 1):
    t = i / N * (math.pi / 2)
    prof.append((YW - R + R * math.sin(t), R - R * math.cos(t)))
prof.append((YW, 30.0))
verts, faces = [], []
for x in (-40.0, 40.0):
    for y, z in prof:
        verts.append((x, y, z))
n = len(prof)
for i in range(n - 1):
    faces.append((i, n + i, n + i + 1, i + 1))
me = bpy.data.meshes.new("cyc"); me.from_pydata(verts, [], faces); me.update()
cyc = bpy.data.objects.new("cyc", me); scene.collection.objects.link(cyc)
for poly in me.polygons:
    poly.use_smooth = True
cyc.data.materials.append(mat("floor", **{"Base Color": (*floor_col, 1), "Roughness": 0.85}))

# the screen content
img = bpy.data.images.load(a.image)
iw, ih = img.size
aspect = a.screen_aspect or iw / ih
curved = a.preset == "curved"
SW = 1.9 if curved else 1.0          # screen width in m
SH = SW / aspect
BZ = 0.012                           # bezel
DEPTH = 0.028

screen_mat = bpy.data.materials.new("screen"); screen_mat.use_nodes = True
nt = screen_mat.node_tree; nodes = nt.nodes; links = nt.links
nodes.clear()
out = nodes.new("ShaderNodeOutputMaterial")
tex = nodes.new("ShaderNodeTexImage"); tex.image = img; tex.interpolation = "Cubic"; tex.extension = "CLIP"
emis = nodes.new("ShaderNodeEmission"); emis.inputs[1].default_value = 1.0
glass = nodes.new("ShaderNodeBsdfGlossy"); glass.inputs[1].default_value = 0.06
glass.inputs[0].default_value = (1, 1, 1, 1)
fres = nodes.new("ShaderNodeLayerWeight"); fres.inputs[0].default_value = 0.08
add = nodes.new("ShaderNodeMixShader")
links.new(tex.outputs[0], emis.inputs[0])
links.new(fres.outputs[0], add.inputs[0])   # Fresnel facing: more reflection at grazing angles
links.new(emis.outputs[0], add.inputs[1]); links.new(glass.outputs[0], add.inputs[2])
links.new(add.outputs[0], out.inputs[0])

bezel_mat = mat("bezel", **{"Base Color": (0.006, 0.006, 0.007, 1), "Roughness": 0.18})
alu_mat = mat("alu", **{"Base Color": (0.80, 0.80, 0.81, 1), "Metallic": 1.0, "Roughness": 0.32})

dev = bpy.data.objects.new("device", None); scene.collection.objects.link(dev)

# shell: rounded slab (aluminium back, black glass front comes from the bezel plane)
bpy.ops.mesh.primitive_cube_add(size=1)
shell = bpy.context.object; shell.name = "shell"
shell.scale = ((SW + 2 * BZ) / 1, DEPTH, (SH + 2 * BZ) / 1)
bpy.ops.object.transform_apply(scale=True)
bev = shell.modifiers.new("bev", "BEVEL"); bev.width = 0.010; bev.segments = 6
shell.location = (0, DEPTH / 2 + 0.0005, 0)
shell.data.materials.append(alu_mat)
shell.parent = dev

# front black glass (the bezel) and the screen inset just in front of it
def plane(name, w, h, y, m, segments=1):
    bpy.ops.mesh.primitive_grid_add(x_subdivisions=segments, y_subdivisions=1, size=1)
    o = bpy.context.object; o.name = name
    o.rotation_euler = (math.pi / 2, 0, 0)
    o.scale = (w, h, 1)
    bpy.ops.object.transform_apply(rotation=True, scale=True)
    o.location = (0, y, 0)
    o.data.materials.append(m); o.parent = dev
    return o

front = plane("front", SW + 2 * BZ - 0.004, SH + 2 * BZ - 0.004, -0.0002, bezel_mat, 64 if curved else 1)
scr = plane("screen", SW, SH, -0.0006, screen_mat, 64 if curved else 1)
# uv: fit the image (the grid's default uv already spans 0..1 over the plane)
if curved:
    for o in (shell, front, scr):
        if o is shell:
            sub = o.modifiers.new("sub", "SUBSURF"); sub.levels = 0
            o.modifiers.remove(sub)
        d = o.modifiers.new("bend", "SIMPLE_DEFORM"); d.deform_method = "BEND"; d.deform_axis = "Z"
        d.angle = math.radians(-38)
    # the shell needs geometry along x to bend
    shell.modifiers.clear()
    bpy.context.view_layer.objects.active = shell
    bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.subdivide(number_cuts=48); bpy.ops.object.mode_set(mode="OBJECT")
    for o in (shell,):
        bev = o.modifiers.new("bev", "BEVEL"); bev.width = 0.008; bev.segments = 4; bev.limit_method = "ANGLE"
        d = o.modifiers.new("bend", "SIMPLE_DEFORM"); d.deform_method = "BEND"; d.deform_axis = "Z"; d.angle = math.radians(-38)

for o in (shell, front, scr):
    for p in o.data.polygons:
        p.use_smooth = True

# stand (desk preset): an aluminium plate from a foot on the floor up to the display's back
def stand(device_z):
    top = device_z
    bpy.ops.mesh.primitive_cube_add(size=1); arm = bpy.context.object; arm.name = "arm"
    arm.scale = (0.18, 0.014, top); bpy.ops.object.transform_apply(scale=True)
    arm.location = (0, DEPTH + 0.045, top / 2); arm.rotation_euler = (math.radians(6), 0, 0)
    arm.modifiers.new("b", "BEVEL").width = 0.005
    arm.data.materials.append(alu_mat)
    bpy.ops.mesh.primitive_cube_add(size=1); foot = bpy.context.object; foot.name = "foot"
    foot.scale = (0.26, 0.26, 0.008); bpy.ops.object.transform_apply(scale=True)
    foot.location = (0, DEPTH + 0.13, 0.004)
    foot.modifiers.new("b", "BEVEL").width = 0.003
    foot.data.materials.append(alu_mat)
    return arm, foot

# lights: one big soft key, a fill, a rim from behind (glints on the shell's edge)
def area(name, loc, size, power, color=(1, 1, 1), target=(0, 0, 0)):
    l = bpy.data.lights.new(name, "AREA"); l.size = size; l.energy = power; l.color = color
    o = bpy.data.objects.new(name, l); scene.collection.objects.link(o); o.location = loc
    d = Vector(target) - Vector(loc); o.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
    return o

area("key", (-3.2, -3.0, 3.4), 3.5, key_w)
area("fill", (3.5, -2.0, 1.2), 4.0, key_w * 0.28)
area("rim", (1.5, 3.0, 2.5), 1.5, key_w * 0.6)
area("top", (0, 0, 5.0), 5.0, key_w * 0.35)

# camera presets: device transform, camera position, look-at, lens, f-stop
cam_data = bpy.data.cameras.new("cam"); cam = bpy.data.objects.new("cam", cam_data)
scene.collection.objects.link(cam); scene.camera = cam
fu, fv = (float(v) for v in a.focus.split(","))
focus_pt = Vector(((fu - 0.5) * SW, -0.001, (0.5 - fv) * SH))
P = {
    "float": dict(rot=(0, 0, math.radians(-14)), z=SH / 2 + 0.28, cam=(0.2, -2.25, 0.66), look=(0, 0, SH / 2 + 0.28), lens=62, f=8.0),
    "three": dict(rot=(math.radians(-4), 0, math.radians(-32)), z=SH / 2 + 0.2, cam=(-0.86, -2.12, 0.98), look=(0.04, 0, SH / 2 + 0.2), lens=55, f=4.0),
    "low": dict(rot=(math.radians(-12), 0, math.radians(18)), z=SH / 2 + 0.06, cam=(0.42, -1.55, 0.16), look=(0, 0, SH / 2 + 0.08), lens=40, f=5.6),
    "macro": dict(rot=(0, 0, math.radians(-38)), z=SH / 2 + 0.25, cam=None, look=None, lens=85, f=2.0),
    "curved": dict(rot=(0, 0, 0), z=SH / 2 + 0.3, cam=(0, -2.75, 0.92), look=(0, 0, SH / 2 + 0.3), lens=36, f=8.0),
    "desk": dict(rot=(0, 0, math.radians(-24)), z=SH / 2 + 0.38, cam=(-1.0, -3.0, 0.9), look=(0, 0, SH / 2 + 0.3), lens=46, f=4.0),
}[a.preset]
dev.rotation_euler = P["rot"]; dev.location = (0, 0, P["z"])
if a.preset == "desk":
    for o in stand(P["z"]):
        o.rotation_euler.z = P["rot"][2]
        o.location = Matrix.Rotation(P["rot"][2], 4, "Z") @ o.location
if a.orbs:
    glassm = bpy.data.materials.new("orb"); glassm.use_nodes = True
    bb = glassm.node_tree.nodes["Principled BSDF"]
    bb.inputs["Base Color"].default_value = (0.95, 0.96, 0.98, 1); bb.inputs["Roughness"].default_value = 0.22
    bb.inputs["Transmission Weight"].default_value = 1.0; bb.inputs["IOR"].default_value = 1.35
    for (x, y, z, r) in [(-0.78, 0.35, 1.18, 0.22), (0.86, 0.25, 0.30, 0.2), (-0.62, -0.45, 0.11, 0.11), (0.7, 0.6, 1.28, 0.3), (0.18, -0.7, 0.05, 0.05)]:
        bpy.ops.mesh.primitive_uv_sphere_add(radius=r, location=(x, y, z), segments=64, ring_count=32)
        o = bpy.context.object; bpy.ops.object.shade_smooth(); o.data.materials.append(glassm)
bpy.context.view_layer.update()
world_focus = dev.matrix_world @ focus_pt
if a.preset == "macro":
    # raking: the camera sits off to the side of the focus point, close, looking across the glass
    nrm = (dev.matrix_world.to_3x3() @ Vector((0, -1, 0))).normalized()
    side = (dev.matrix_world.to_3x3() @ Vector((1, 0, 0))).normalized()
    cam.location = world_focus + nrm * (0.55 / a.zoom) - side * (0.42 / a.zoom) + Vector((0, 0, 0.08 / a.zoom))
    look = world_focus
else:
    cam.location = Vector(P["cam"]) / a.zoom + Vector(P["look"]) * (1 - 1 / a.zoom)
    look = Vector(P["look"])
d = look - cam.location; cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
cam_data.lens = P["lens"]
cam_data.dof.use_dof = True
cam_data.dof.focus_distance = (world_focus - cam.location).length if a.preset in ("macro", "three") else (Vector(look) - cam.location).length
cam_data.dof.aperture_fstop = P["f"]
cam_data.sensor_width = 36

bpy.ops.render.render(write_still=True)
print("wrote", a.out)
