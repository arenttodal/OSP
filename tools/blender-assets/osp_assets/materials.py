"""Materials from presets: the ivory finishes, the indicator inlay or print, the panel.

The indicator is part of the knob's own shader (an object-space mask), not geometry: its
edges stay exact at any render size, it turns with the knob, and the engraved style cuts a
shallow rounded groove (bump) with a matte charcoal inlay in it.

Object coordinates are millimetres (the mesh is built in mm and scaled to metres), so every
size in the presets is in mm too.
"""

import bpy

from . import config as cfg


def _node(nodes, kind, location, **inputs):
    n = nodes.new(kind)
    n.location = location
    for key, value in inputs.items():
        n.inputs[key].default_value = value
    return n


def _math(nodes, links, op, a, b=None, location=(0, 0), clamp=False):
    n = nodes.new("ShaderNodeMath")
    n.operation = op
    n.use_clamp = clamp
    n.location = location
    for i, v in enumerate((a, b)):
        if v is None:
            continue
        if isinstance(v, (int, float)):
            n.inputs[i].default_value = v
        else:
            links.new(v, n.inputs[i])
    return n.outputs[0]


def _smooth(nodes, links, value, from_min, from_max, to_min, to_max, location=(0, 0)):
    n = nodes.new("ShaderNodeMapRange")
    n.interpolation_type = "SMOOTHSTEP"
    n.clamp = True
    n.location = location
    links.new(value, n.inputs["Value"])
    n.inputs["From Min"].default_value = from_min
    n.inputs["From Max"].default_value = from_max
    n.inputs["To Min"].default_value = to_min
    n.inputs["To Max"].default_value = to_max
    return n.outputs["Result"]


def _rgba(hex_colour):
    r, g, b = cfg.srgb_to_linear(hex_colour)
    return (r, g, b, 1.0)


def knob_material(name, finish, indicator, info, ink):
    """finish: a materials/ preset; indicator: the config's indicator block (style
    'engraved' or 'printed'); info: the geometry's measurements; ink: an inks/ preset."""
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    nodes.clear()
    out = _node(nodes, "ShaderNodeOutputMaterial", (1400, 0))
    bsdf = _node(nodes, "ShaderNodeBsdfPrincipled", (1100, 0))
    links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])

    coords = _node(nodes, "ShaderNodeTexCoord", (-1400, 0))
    sep = _node(nodes, "ShaderNodeSeparateXYZ", (-1200, 0))
    links.new(coords.outputs["Object"], sep.inputs["Vector"])
    x, y, z = sep.outputs["X"], sep.outputs["Y"], sep.outputs["Z"]

    # --- The indicator mask (1 on the line, 0 elsewhere), along +Y.
    half = 0.5 * indicator["width"]
    soft = min(0.1, 0.15 * indicator["width"])
    ax = _math(nodes, links, "ABSOLUTE", x, location=(-1000, 200))
    mx = _smooth(nodes, links, ax, half - soft, half, 1.0, 0.0, (-800, 300))
    r0 = indicator["from"] * info["rt"]
    my = _smooth(nodes, links, y, r0 - soft, r0, 0.0, 1.0, (-800, 150))
    mask = _math(nodes, links, "MULTIPLY", mx, my, (-600, 250))
    if indicator.get("to", 1.0) < 1.0:
        r1 = indicator["to"] * info["rt"]
        my2 = _smooth(nodes, links, y, r1 - soft, r1, 1.0, 0.0, (-800, 0))
        mask = _math(nodes, links, "MULTIPLY", mask, my2, (-600, 100))
        z_from = info["edgeZ"] - 0.2
    else:
        # Down the sidewall for this fraction of its height (as on the classic fluted cap).
        down = max(0.0, min(1.0, indicator.get("downSide", 0.0)))
        z_from = info["edgeZ"] - down * (info["edgeZ"] - info["zb"]) - 0.2 * (1.0 - down)
    mz = _smooth(nodes, links, z, z_from - soft, z_from, 0.0, 1.0, (-800, -150))
    mask = _math(nodes, links, "MULTIPLY", mask, mz, (-450, 200))

    # --- Microtexture: a fine grain (heights in mm).
    grain = finish["grain"]
    size = max(1e-4, grain["size"])
    noise = nodes.new("ShaderNodeTexNoise")
    noise.location = (-800, -400)
    noise.noise_dimensions = "3D"
    noise.inputs["Detail"].default_value = grain.get("detail", 2.0)
    noise.inputs["Roughness"].default_value = 0.55
    if grain["kind"] == "brushed":
        # Circular brushing, seamless: (radius, direction) stretched along the turn.
        xy = _node(nodes, "ShaderNodeCombineXYZ", (-1200, -500))
        links.new(x, xy.inputs["X"])
        links.new(y, xy.inputs["Y"])
        rho_node = nodes.new("ShaderNodeVectorMath")
        rho_node.operation = "LENGTH"
        rho_node.location = (-1050, -500)
        links.new(xy.outputs["Vector"], rho_node.inputs[0])
        rho = rho_node.outputs["Value"]
        safe = _math(nodes, links, "MAXIMUM", rho, 0.05, (-950, -600))
        cx = _math(nodes, links, "DIVIDE", x, safe, (-900, -650))
        cy = _math(nodes, links, "DIVIDE", y, safe, (-900, -700))
        stretch = 3.0                       # angular frequency: a few streaks per turn, not 1000s
        radial = _math(nodes, links, "ADD", rho, _math(nodes, links, "MULTIPLY", z, 0.3, location=(-1000, -800)), (-900, -550))
        v = _node(nodes, "ShaderNodeCombineXYZ", (-800, -600))
        links.new(_math(nodes, links, "DIVIDE", radial, size, (-850, -550)), v.inputs["X"])
        links.new(_math(nodes, links, "MULTIPLY", cx, stretch, location=(-850, -650)), v.inputs["Y"])
        links.new(_math(nodes, links, "MULTIPLY", cy, stretch, location=(-850, -700)), v.inputs["Z"])
        links.new(v.outputs["Vector"], noise.inputs["Vector"])
        noise.inputs["Scale"].default_value = 1.0
    else:
        links.new(coords.outputs["Object"], noise.inputs["Vector"])
        noise.inputs["Scale"].default_value = 1.0 / size
    grain_h = _math(nodes, links, "MULTIPLY", _math(nodes, links, "SUBTRACT", noise.outputs["Fac"], 0.5, location=(-600, -400)),
                    2.0 * grain["height"], (-450, -400))

    # Roughness varies a little over larger blotches (a molded part is never perfectly even).
    blotch = nodes.new("ShaderNodeTexNoise")
    blotch.location = (-800, -150)
    blotch.inputs["Scale"].default_value = 1.0 / (size * 60.0)
    blotch.inputs["Detail"].default_value = 1.0
    links.new(coords.outputs["Object"], blotch.inputs["Vector"])
    rough = _math(nodes, links, "MULTIPLY_ADD", _math(nodes, links, "SUBTRACT", blotch.outputs["Fac"], 0.5, location=(-600, -150)),
                  2.0 * finish.get("roughnessVariation", 0.0), (-450, -150))
    rough.node.inputs[2].default_value = finish["roughness"]

    # --- The engraved groove: rounded, deepest on the line's centre.
    height = grain_h
    if indicator["style"] == "engraved" and indicator.get("depth", 0.0) > 0.0:
        profile = _smooth(nodes, links, ax, 0.35 * half, half, 1.0, 0.0, (-800, 450))
        groove = _math(nodes, links, "MULTIPLY", _math(nodes, links, "MULTIPLY", profile, my, (-600, 450)), mz, (-450, 450))
        height = _math(nodes, links, "MULTIPLY_ADD", groove, -indicator["depth"], (-300, 0))
        height.node.inputs[2].default_value = 0.0
        links.new(grain_h, height.node.inputs[2])
    bump = _node(nodes, "ShaderNodeBump", (850, -300), Strength=1.0, Distance=0.001)
    links.new(height, bump.inputs["Height"])
    links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])

    # --- Ivory where there is no line, the ink where there is.
    colour = _node(nodes, "ShaderNodeMix", (700, 300))
    colour.data_type = "RGBA"
    # ShaderNodeMix carries float, vector and colour sockets of the same names: by index.
    colour.inputs[6].default_value = _rgba(finish["baseColor"])
    colour.inputs[7].default_value = _rgba(ink["color"])
    links.new(mask, colour.inputs[0])
    links.new(colour.outputs[2], bsdf.inputs["Base Color"])

    rmix = _node(nodes, "ShaderNodeMix", (700, 0))
    rmix.data_type = "FLOAT"
    links.new(rough, rmix.inputs[2])
    rmix.inputs[3].default_value = ink["roughness"]
    links.new(mask, rmix.inputs[0])
    links.new(rmix.outputs[0], bsdf.inputs["Roughness"])

    keep = _math(nodes, links, "SUBTRACT", 1.0, mask, (500, -100))
    links.new(_math(nodes, links, "MULTIPLY", keep, finish.get("sheen", 0.0), (650, -150)), bsdf.inputs["Sheen Weight"])
    links.new(_math(nodes, links, "MULTIPLY", keep, finish.get("coat", 0.0), (650, -200)), bsdf.inputs["Coat Weight"])
    bsdf.inputs["Coat Roughness"].default_value = finish.get("coatRoughness", 0.3)
    bsdf.inputs["Specular IOR Level"].default_value = finish.get("specular", 0.5)
    bsdf.inputs["Subsurface Weight"].default_value = finish.get("subsurface", 0.0)
    bsdf.inputs["Subsurface Radius"].default_value = (1.0, 0.75, 0.55)
    bsdf.inputs["Subsurface Scale"].default_value = 0.0006   # metres: a ceramic-like softness, no translucency
    bsdf.inputs["Sheen Roughness"].default_value = 0.6
    return mat


def panel_material(name, panel):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = _rgba(panel["color"])
    bsdf.inputs["Roughness"].default_value = panel.get("roughness", 0.8)
    return mat
