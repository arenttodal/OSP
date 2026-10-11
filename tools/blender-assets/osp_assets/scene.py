"""The studio: an empty scene with the control, a shadow-catching panel, a fixed light rig
and an orthographic camera. Built from presets; nothing here depends on the control type
except the measurements it is handed.
"""

import math

import bpy
from mathutils import Vector

from . import config as cfg
from . import geometry
from . import materials

MM = 0.001   # the scene is in metres; the presets are in millimetres


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    for obj in list(scene.collection.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    return scene


def _direction(azimuth_deg, elevation_deg):
    a, e = math.radians(azimuth_deg), math.radians(elevation_deg)
    return Vector((math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e)))


def _aim(obj, target):
    direction = target - obj.location
    obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


def add_knob(scene, g, finish, indicator, ink):
    verts, faces, info = geometry.build_knob_mesh(g)
    mesh = bpy.data.meshes.new("knob")
    mesh.from_pydata(verts, [], faces)
    mesh.validate(clean_customdata=False)
    mesh.polygons.foreach_set("use_smooth", [True] * len(mesh.polygons))
    mesh.update()
    obj = bpy.data.objects.new("knob", mesh)
    obj.scale = (MM, MM, MM)
    scene.collection.objects.link(obj)
    mesh.materials.append(materials.knob_material("knob", finish, indicator, info, ink))
    return obj, info


def add_panel(scene, panel):
    mesh = bpy.data.meshes.new("panel")
    s = 0.4
    mesh.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    obj = bpy.data.objects.new("panel", mesh)
    obj.is_shadow_catcher = True          # only its shadows reach the image (alpha); it still bounces light
    scene.collection.objects.link(obj)
    mesh.materials.append(materials.panel_material("panel", panel))
    return obj


def add_lights(scene, rig, target):
    world = bpy.data.worlds.new("world")
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    r, g, b = cfg.srgb_to_linear(rig["world"]["color"])
    bg.inputs["Color"].default_value = (r, g, b, 1.0)
    bg.inputs["Strength"].default_value = rig["world"]["strength"]
    scene.world = world
    lights = []
    for spec in rig["lights"]:
        light = bpy.data.lights.new(spec["name"], "AREA")
        light.shape = "DISK"
        light.size = spec["size"] * MM
        light.energy = spec["energy"]
        light.color = cfg.srgb_to_linear(spec.get("color", "#ffffff"))
        obj = bpy.data.objects.new(spec["name"], light)
        obj.location = target + _direction(spec["azimuth"], spec["elevation"]) * spec["distance"] * MM
        _aim(obj, target)
        scene.collection.objects.link(obj)
        lights.append(obj)
    return lights


def add_camera(scene, preset, outer_radius_mm, target):
    cam = bpy.data.cameras.new("camera")
    cam.type = "ORTHO"
    cam.ortho_scale = 2.0 * outer_radius_mm * preset["margin"] * MM
    cam.clip_start = 0.001
    cam.clip_end = 2.0
    obj = bpy.data.objects.new("camera", cam)
    # Straight down at 90 (the camera's own up is +Y: the indicator's 12 o'clock); lower
    # elevations tip the view about X, leaning towards the player (from the screen bottom).
    tilt = math.radians(90.0 - preset["elevation"])
    obj.rotation_euler = (tilt, 0.0, 0.0)
    view = Vector((0.0, math.sin(tilt), -math.cos(tilt)))
    obj.location = target - view * 0.5
    scene.collection.objects.link(obj)
    scene.camera = obj
    return obj


def configure_render(scene, render, resolution, samples, exposure=0.0):
    scene.render.engine = render.get("engine", "CYCLES")
    cy = scene.cycles
    cy.device = "CPU"
    cy.samples = samples
    cy.use_adaptive_sampling = False      # every pixel the same number of samples: no frame-to-frame shimmer
    cy.seed = render.get("seed", 0)
    cy.use_animated_seed = False
    cy.use_denoising = bool(render.get("denoise", True))
    cy.denoiser = "OPENIMAGEDENOISE"
    cy.filter_width = render.get("filterWidth", 1.5)
    # A matte object on a matte panel: a few bounces carry all the light there is.
    bounces = render.get("bounces", {})
    cy.max_bounces = bounces.get("total", 6)
    cy.diffuse_bounces = bounces.get("diffuse", 3)
    cy.glossy_bounces = bounces.get("glossy", 2)
    cy.transmission_bounces = 0
    cy.volume_bounces = 0
    cy.transparent_max_bounces = 2
    cy.caustics_reflective = False
    cy.caustics_refractive = False
    scene.render.film_transparent = True
    scene.render.resolution_x = resolution
    scene.render.resolution_y = resolution
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "8"
    scene.render.image_settings.compression = 15
    scene.view_settings.view_transform = render.get("viewTransform", "Standard")
    scene.view_settings.look = "None"
    scene.view_settings.exposure = exposure
    scene.view_settings.gamma = 1.0
    scene.display_settings.display_device = "sRGB"


def build(config, material_name=None, indicator_style=None, camera_name=None):
    """The whole studio for one control. Returns (scene, knob object, geometry info)."""
    scene = reset()
    g = config["geometry"]
    finish = cfg.material(material_name or config["material"])
    indicator = dict(config["indicator"])
    if indicator_style:
        indicator["style"] = indicator_style
    inks = cfg.inks()
    ink = inks["charcoal-print"] if indicator["style"] == "printed" else {"color": indicator["color"], "roughness": indicator["roughness"]}
    knob, info = add_knob(scene, g, finish, indicator, ink)
    rig = cfg.lighting(config["lighting"])
    add_panel(scene, rig["panel"])
    target = Vector((0.0, 0.0, 0.5 * info["height"] * MM))
    add_lights(scene, rig, target)
    add_camera(scene, cfg.camera(camera_name or config["camera"]), info["outer"], target)
    return scene, knob, info


def set_angle(knob, juce_angle):
    """JUCE's rotary angle (radians clockwise from 12 o'clock, seen from above) -> the
    object's turn about +Z (counter-clockwise from above), the indicator built along +Y."""
    knob.rotation_euler = (0.0, 0.0, -juce_angle)


def render_to(scene, path):
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def measure_patch(path, centre_xy, radius_px):
    """Mean linear RGB of a disc of pixels in a rendered PNG (centre in pixels, y down)."""
    image = bpy.data.images.load(path, check_existing=False)
    w, h = image.size
    pixels = image.pixels[:]
    bpy.data.images.remove(image)
    cx, cy = centre_xy
    total = [0.0, 0.0, 0.0]
    count = 0
    for py in range(int(cy - radius_px), int(cy + radius_px) + 1):
        for px in range(int(cx - radius_px), int(cx + radius_px) + 1):
            if (px - cx) ** 2 + (py - cy) ** 2 > radius_px ** 2:
                continue
            row = h - 1 - py                     # Blender's pixels start at the bottom row
            i = 4 * (row * w + px)
            for c in range(3):
                v = pixels[i + c]
                total[c] += v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
            count += 1
    return [t / max(1, count) for t in total]


def luminance(rgb):
    return 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]
