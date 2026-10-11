"""The lab's studio: the approved knob's light rig, cameras and render settings.

The knob was lit by area lights a short distance from a 28 mm object. A whole interface
(450 x 340 mm at the knob's scale) under the same lamps would be lit unevenly, with hot
spots. Here every lamp becomes a sun from the same direction, with the same angular size
(2 atan(size / 2 / distance)), so a panel is lit like the knob everywhere: same direction,
same softness, no fall-off. The world ambient is the same.

Colour management is the knob's: Standard view transform, no look, gamma 1, sRGB display,
8-bit RGBA PNG, straight alpha, film transparent; exposure from one calibration.
"""

import json
import math
import os

import bpy
from mathutils import Vector

from . import geometry
from .materials import srgb_to_linear

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
SOFTBOX_WATTS = 260000.0   # scene units are mm: radiance ~0.25 over a 1 x 0.34 m panel
KNOB_LIGHTING = os.path.join(REPO, "tools", "blender-assets", "lighting", "studio-soft.json")


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    return bpy.context.scene


def rig(scene, strength=1.0):
    with open(KNOB_LIGHTING, "r", encoding="utf-8") as f:
        knob = json.load(f)["presets"]["studio-soft"]
    world = bpy.data.worlds.new("world")
    world.use_nodes = True
    nodes, links = world.node_tree.nodes, world.node_tree.links
    bg = nodes["Background"]
    bg.inputs["Color"].default_value = (*srgb_to_linear(knob["world"]["color"]), 1.0)
    bg.inputs["Strength"].default_value = knob["world"]["strength"]
    # A studio, not a uniform sky: brighter overhead, falling towards the horizon (what
    # satin and metal reflect).
    coords = nodes.new("ShaderNodeTexCoord")
    sep = nodes.new("ShaderNodeSeparateXYZ")
    links.new(coords.outputs["Generated"], sep.inputs["Vector"])
    ramp = nodes.new("ShaderNodeMapRange")
    ramp.inputs["From Min"].default_value = -0.2
    ramp.inputs["From Max"].default_value = 1.0
    ramp.inputs["To Min"].default_value = 0.35 * knob["world"]["strength"]
    ramp.inputs["To Max"].default_value = 1.6 * knob["world"]["strength"]
    links.new(sep.outputs["Z"], ramp.inputs["Value"])
    links.new(ramp.outputs["Result"], bg.inputs["Strength"])
    scene.world = world
    # The overhead softbox, fixed in the interface's own space (every lab render sits in
    # GUI coordinates, so a button is lit exactly as it would be in the whole window): a
    # wide panel above the top edge, aimed at the middle, giving the gentle top-to-bottom
    # fall-off the GUI's gradients imitate.
    box = bpy.data.lights.new("softbox", "AREA")
    box.shape = "RECTANGLE"
    box.size = 1448 * geometry.PX * 1.4
    box.size_y = 1086 * geometry.PX * 0.5
    box.energy = strength * SOFTBOX_WATTS
    box.color = srgb_to_linear("#ffffff")   # neutral: the warmth is in the materials
    obj = bpy.data.objects.new("softbox", box)
    obj.location = (724 * geometry.PX, 120 * geometry.PX, 900 * geometry.PX)
    target = Vector((724 * geometry.PX, -620 * geometry.PX, 0.0))
    obj.rotation_euler = (target - obj.location).to_track_quat("-Z", "Y").to_euler()
    scene.collection.objects.link(obj)
    scrim(scene, strength)
    for spec in knob["lights"]:
        light = bpy.data.lights.new(spec["name"], "SUN")
        light.angle = 2.0 * math.atan(0.5 * spec["size"] / spec["distance"])
        # The knob's lamps keep their balance (power over distance squared); the calibration
        # sets the overall level.
        light.energy = strength * spec["energy"] / (math.pi * (spec["distance"] * 0.001) ** 2)
        light.color = srgb_to_linear(spec.get("color", "#ffffff"))
        obj = bpy.data.objects.new(spec["name"], light)
        a, e = math.radians(spec["azimuth"]), math.radians(spec["elevation"])
        direction = Vector((math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e)))
        obj.location = direction * 1.0
        obj.rotation_euler = (-direction).to_track_quat("-Z", "Y").to_euler()
        scene.collection.objects.link(obj)


SCRIM_LOW, SCRIM_HIGH = 0.45, 1.25   # radiance at the bottom / top of the window


def scrim(scene, strength=1.0):
    """A reflection-only scrim above the interface. Seen straight down, a flat surface
    mirrors what is directly above it; the lamps are off to the side, so without this
    metal and satin reflect only the even sky and read as flat paint. The scrim is seen by
    glossy rays alone (not the camera, not diffuse light, no shadows): it adds a soft
    top-to-bottom gradient and two broad soft bands that bevels and brushed metal catch."""
    cx, cy, w, h = geometry.design_rect_mm((-200, -200, 1848, 1486))
    mesh = bpy.data.meshes.new("scrim")
    z = 700.0
    mesh.from_pydata([(cx - w / 2, cy - h / 2, z), (cx + w / 2, cy - h / 2, z), (cx + w / 2, cy + h / 2, z), (cx - w / 2, cy + h / 2, z)], [], [(0, 3, 2, 1)])
    mesh.uv_layers.new()
    obj = bpy.data.objects.new("scrim", mesh)
    mat = bpy.data.materials.new("scrim")
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    nodes.clear()
    out = nodes.new("ShaderNodeOutputMaterial")
    emit = nodes.new("ShaderNodeEmission")
    emit.inputs["Color"].default_value = (*srgb_to_linear("#ffffff"), 1.0)
    coords = nodes.new("ShaderNodeTexCoord")
    sep = nodes.new("ShaderNodeSeparateXYZ")
    links.new(coords.outputs["Generated"], sep.inputs["Vector"])
    # Gradient: y = 0 at the bottom of the plane, 1 at the top.
    grad = nodes.new("ShaderNodeMapRange")
    grad.inputs["To Min"].default_value = SCRIM_LOW * strength
    grad.inputs["To Max"].default_value = SCRIM_HIGH * strength
    links.new(sep.outputs["Y"], grad.inputs["Value"])
    # Two soft bands (a studio's strip lights, seen in reflections only): 1 + 0.35 cos.
    wave = nodes.new("ShaderNodeMath")
    wave.operation = "MULTIPLY"
    wave.inputs[1].default_value = 2.0 * math.pi * 3.0
    links.new(sep.outputs["Y"], wave.inputs[0])
    cos = nodes.new("ShaderNodeMath")
    cos.operation = "COSINE"
    links.new(wave.outputs[0], cos.inputs[0])
    band = nodes.new("ShaderNodeMath")
    band.operation = "MULTIPLY_ADD"
    band.inputs[1].default_value = 0.3
    band.inputs[2].default_value = 1.0
    links.new(cos.outputs[0], band.inputs[0])
    total = nodes.new("ShaderNodeMath")
    total.operation = "MULTIPLY"
    links.new(grad.outputs["Result"], total.inputs[0])
    links.new(band.outputs[0], total.inputs[1])
    links.new(total.outputs[0], emit.inputs["Strength"])
    links.new(emit.outputs["Emission"], out.inputs["Surface"])
    mesh.materials.append(mat)
    obj.visible_camera = False
    obj.visible_diffuse = False
    obj.visible_shadow = False
    obj.visible_transmission = False
    obj.visible_volume_scatter = False
    scene.collection.objects.link(obj)
    return obj


def camera(scene, rect, scale):
    """Orthographic, straight down, covering `rect` (design px) at `scale` pixels per px."""
    cx, cy, w, h = geometry.design_rect_mm(rect)
    cam = bpy.data.cameras.new("camera")
    cam.type = "ORTHO"
    cam.ortho_scale = max(w, h)
    cam.sensor_fit = "AUTO"
    cam.clip_start = 0.1
    cam.clip_end = 2000.0
    obj = bpy.data.objects.new("camera", cam)
    obj.location = (cx, cy, 500.0)
    scene.collection.objects.link(obj)
    scene.camera = obj
    scene.render.resolution_x = max(1, int(round(rect[2] * scale)))
    scene.render.resolution_y = max(1, int(round(rect[3] * scale)))
    scene.render.resolution_percentage = 100
    return obj


def settings(scene, samples=64, exposure=0.0, transparent=True, denoise=True, seed=7):
    scene.render.engine = "CYCLES"
    c = scene.cycles
    c.device = "CPU"
    c.samples = samples
    c.use_adaptive_sampling = False
    c.seed = seed
    c.use_denoising = denoise
    c.denoiser = "OPENIMAGEDENOISE"
    c.max_bounces = 6
    c.diffuse_bounces = 3
    c.glossy_bounces = 3
    c.transmission_bounces = 0
    c.volume_bounces = 0
    c.caustics_reflective = False
    c.caustics_refractive = False
    c.filter_width = 1.5
    scene.render.film_transparent = transparent
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "8"
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = exposure
    scene.view_settings.gamma = 1.0
    scene.display_settings.display_device = "sRGB"


def shadow_catcher(scene, rect, z=0.0):
    """A plane that keeps only shadows (in the alpha); it still bounces the panel's light."""
    from .materials import by_name
    obj = geometry.plane("catcher", rect, by_name("M1-warm-matte-ivory"), z)
    obj.is_shadow_catcher = True
    return obj


def render(scene, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
