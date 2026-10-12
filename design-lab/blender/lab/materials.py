"""PBR materials for the design lab, from configs/materials.json.

Every material has the same parameters: base colour, roughness, metallic, specular, coat,
sheen, anisotropy and a procedural microtexture ("powder": isotropic grain; "brushed":
grain stretched along X with the anisotropy's tangent along X). Object coordinates are mm.
"""

import json
import os

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG = os.path.join(os.path.dirname(HERE), "configs", "materials.json")


def srgb_to_linear(hex_colour):
    h = hex_colour.lstrip("#")
    out = []
    for i in (0, 2, 4):
        c = int(h[i:i + 2], 16) / 255.0
        out.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
    return tuple(out)


def presets():
    with open(CONFIG, "r", encoding="utf-8") as f:
        return json.load(f)["materials"]


def _math(nodes, links, op, a, b=None):
    n = nodes.new("ShaderNodeMath")
    n.operation = op
    for i, v in enumerate((a, b)):
        if v is None:
            continue
        if isinstance(v, (int, float)):
            n.inputs[i].default_value = v
        else:
            links.new(v, n.inputs[i])
    return n.outputs[0]


def make(name, spec, tint=None, emission=None):
    """spec: a materials.json entry (or a dict like one). tint: '#rrggbb' mixed into the base
    (active states); emission: (hex, strength) for a lit element."""
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    nodes.clear()
    out = nodes.new("ShaderNodeOutputMaterial")
    bsdf = nodes.new("ShaderNodeBsdfPrincipled")
    links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    base = srgb_to_linear(spec["baseColor"])
    if tint:
        t = srgb_to_linear(tint[0])
        base = tuple(b * (1.0 - tint[1]) + c * tint[1] for b, c in zip(base, t))
    bsdf.inputs["Base Color"].default_value = (*base, 1.0)
    bsdf.inputs["Roughness"].default_value = spec.get("roughness", 0.5)
    bsdf.inputs["Metallic"].default_value = spec.get("metallic", 0.0)
    bsdf.inputs["Specular IOR Level"].default_value = spec.get("specular", 0.5)
    bsdf.inputs["Coat Weight"].default_value = spec.get("coat", 0.0)
    bsdf.inputs["Coat Roughness"].default_value = spec.get("coatRoughness", 0.25)
    bsdf.inputs["Sheen Weight"].default_value = spec.get("sheen", 0.0)
    bsdf.inputs["Sheen Roughness"].default_value = 0.6
    aniso = spec.get("anisotropic", 0.0)
    if aniso > 0.0:
        bsdf.inputs["Anisotropic"].default_value = aniso
        tangent = nodes.new("ShaderNodeCombineXYZ")
        tangent.inputs["X"].default_value = 1.0
        links.new(tangent.outputs["Vector"], bsdf.inputs["Tangent"])
    if emission:
        bsdf.inputs["Emission Color"].default_value = (*srgb_to_linear(emission[0]), 1.0)
        bsdf.inputs["Emission Strength"].default_value = emission[1]

    wood = spec.get("wood")
    if wood:
        _wood(nodes, links, bsdf, wood)
        return mat

    grain = spec.get("grain", {})
    if grain.get("height", 0.0) > 0.0:
        coords = nodes.new("ShaderNodeTexCoord")
        noise = nodes.new("ShaderNodeTexNoise")
        noise.inputs["Detail"].default_value = 3.0
        noise.inputs["Roughness"].default_value = 0.55
        size = max(1e-4, grain.get("size", 0.05))
        if grain.get("kind") == "brushed":
            # Grain stretched along X: fine across, long along the brushing.
            mapping = nodes.new("ShaderNodeMapping")
            links.new(coords.outputs["Object"], mapping.inputs["Vector"])
            stretch = grain.get("stretch", 60.0)
            mapping.inputs["Scale"].default_value = (1.0 / (size * stretch), 1.0 / size, 1.0 / size)
            links.new(mapping.outputs["Vector"], noise.inputs["Vector"])
            noise.inputs["Scale"].default_value = 1.0
        else:
            links.new(coords.outputs["Object"], noise.inputs["Vector"])
            noise.inputs["Scale"].default_value = 1.0 / size
        height = _math(nodes, links, "MULTIPLY", _math(nodes, links, "SUBTRACT", noise.outputs["Fac"], 0.5), 2.0 * grain["height"])
        # The grain is also a very low-contrast streak or speckle in the colour: how a
        # brushed or powder finish reads at small sizes (bump alone vanishes when viewed
        # straight on under soft light).
        var = spec.get("albedoVariation", 0.0)
        if var > 0.0:
            gain = _math(nodes, links, "MULTIPLY_ADD", _math(nodes, links, "SUBTRACT", noise.outputs["Fac"], 0.5), 2.0 * var)
            gain.node.inputs[2].default_value = 1.0
            scale = nodes.new("ShaderNodeVectorMath")
            scale.operation = "SCALE"
            scale.inputs[0].default_value = base
            links.new(gain, scale.inputs["Scale"])
            links.new(scale.outputs["Vector"], bsdf.inputs["Base Color"])
        bump = nodes.new("ShaderNodeBump")
        bump.inputs["Strength"].default_value = 1.0
        bump.inputs["Distance"].default_value = 1.0   # scene units are mm, as the heights
        links.new(height, bump.inputs["Height"])
        links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
        # The grain also varies the roughness a little (never perfectly even).
        rough = _math(nodes, links, "MULTIPLY_ADD", _math(nodes, links, "SUBTRACT", noise.outputs["Fac"], 0.5), 0.06)
        rough.node.inputs[2].default_value = spec.get("roughness", 0.5)
        links.new(rough, bsdf.inputs["Roughness"])
    return mat


def _wood(nodes, links, bsdf, wood):
    """Oiled straight-grained wood (teak): bands along Y (the cheeks' long axis), warped by
    noise, between a light and a dark tone; the same pattern as a faint bump (open pores)."""
    coords = nodes.new("ShaderNodeTexCoord")
    mapping = nodes.new("ShaderNodeMapping")
    links.new(coords.outputs["Object"], mapping.inputs["Vector"])
    # Grain runs along Y: compress X (many bands across), stretch Y.
    mapping.inputs["Scale"].default_value = (1.0, wood.get("stretch", 0.08), 1.0)
    wave = nodes.new("ShaderNodeTexWave")
    wave.wave_type = "BANDS"
    wave.bands_direction = "X"
    wave.inputs["Scale"].default_value = wood.get("scale", 0.55)
    wave.inputs["Distortion"].default_value = wood.get("distortion", 6.0)
    wave.inputs["Detail"].default_value = 4.0
    wave.inputs["Detail Scale"].default_value = 1.5
    links.new(mapping.outputs["Vector"], wave.inputs["Vector"])
    noise = nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 0.35
    noise.inputs["Detail"].default_value = 3.0
    links.new(mapping.outputs["Vector"], noise.inputs["Vector"])
    # Grain value: mostly the bands, some broad figure from the noise.
    mixf = nodes.new("ShaderNodeMix")
    mixf.data_type = "FLOAT"
    mixf.inputs[0].default_value = 0.35
    links.new(wave.outputs["Fac"], mixf.inputs[2])
    links.new(noise.outputs["Fac"], mixf.inputs[3])
    ramp = nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.25
    ramp.color_ramp.elements[0].color = (*srgb_to_linear(wood["light"]), 1.0)
    ramp.color_ramp.elements[1].position = 0.85
    ramp.color_ramp.elements[1].color = (*srgb_to_linear(wood["dark"]), 1.0)
    links.new(mixf.outputs[0], ramp.inputs["Fac"])
    links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])
    bump = nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.25
    bump.inputs["Distance"].default_value = wood.get("pore", 0.004)
    links.new(wave.outputs["Fac"], bump.inputs["Height"])
    links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])


def by_name(name, **kwargs):
    """A fresh material from a preset (the scene is rebuilt for every render)."""
    return make(name, presets()[name], **kwargs)


def resolve(ref):
    """A preset name, or {"preset": name, ...overrides} (a preset retuned for a role: the
    housing a shade darker than a card, a button a shade lighter)."""
    if isinstance(ref, str):
        return ref, dict(presets()[ref])
    spec = dict(presets()[ref["preset"]])
    spec.update({k: v for k, v in ref.items() if k not in ("preset", "tint", "emission")})
    if "grain" in ref:
        spec["grain"] = dict(presets()[ref["preset"]].get("grain", {}), **ref["grain"])
    return ref["preset"], spec


def material(ref, name=None, tint=None, emission=None):
    preset, spec = resolve(ref)
    if isinstance(ref, dict):
        tint = tint or ref.get("tint")
        emission = emission or ref.get("emission")
    return make(name or preset, spec, tint=tint, emission=emission)
