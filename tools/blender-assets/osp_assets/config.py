"""Configuration: one JSON per control, named presets for materials, lights and cameras.

A control's config names its material, lighting and camera; those are looked up in the
preset folders (materials/, lighting/, scenes/), so every control shares one rig and one
set of finishes. Plain Python (no bpy): the post-processing tools use it too.
"""

import copy
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCHEMA_VERSION = 1


def _load_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def _presets(folder, key="presets"):
    """Every preset in every JSON file of a folder, by name (sorted file order: stable)."""
    found = {}
    directory = os.path.join(ROOT, folder)
    for name in sorted(os.listdir(directory)):
        if name.endswith(".json"):
            data = _load_json(os.path.join(directory, name))
            for preset, value in data.get(key, {}).items():
                if preset in found:
                    raise ValueError(f"preset '{preset}' defined twice in {folder}/")
                found[preset] = value
    return found


def materials():
    return _presets("materials")


def inks():
    return _presets("materials", "inks")


def lighting(name):
    rigs = _presets("lighting")
    if name not in rigs:
        raise KeyError(f"unknown lighting preset '{name}' (have: {', '.join(sorted(rigs))})")
    return rigs[name]


def camera(name):
    cams = _presets("scenes")
    if name not in cams:
        raise KeyError(f"unknown camera preset '{name}' (have: {', '.join(sorted(cams))})")
    return cams[name]


def material(name):
    mats = materials()
    if name not in mats:
        raise KeyError(f"unknown material preset '{name}' (have: {', '.join(sorted(mats))})")
    return mats[name]


def _parse_value(text):
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return text


def apply_override(config, assignment):
    """'a.b.c=value' (value as JSON when it parses, else a string)."""
    path, _, value = assignment.partition("=")
    if not path or not _:
        raise ValueError(f"override '{assignment}' is not key.path=value")
    node = config
    keys = path.split(".")
    for key in keys[:-1]:
        node = node.setdefault(key, {})
    node[keys[-1]] = _parse_value(value)


def load(path, overrides=()):
    config = _load_json(path)
    if config.get("schemaVersion") != SCHEMA_VERSION:
        raise ValueError(f"{path}: schemaVersion {config.get('schemaVersion')} (this generator reads {SCHEMA_VERSION})")
    config = copy.deepcopy(config)
    for assignment in overrides:
        apply_override(config, assignment)
    return config


def srgb_to_linear(hex_colour):
    """'#rrggbb' -> linear RGB floats (the sRGB transfer curve)."""
    h = hex_colour.lstrip("#")
    out = []
    for i in (0, 2, 4):
        c = int(h[i:i + 2], 16) / 255.0
        out.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
    return tuple(out)


def srgb_bytes(hex_colour):
    h = hex_colour.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
