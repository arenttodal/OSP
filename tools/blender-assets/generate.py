"""Render a control from its config: prototypes, the full frame sequence.

Runs headless, two ways:

    blender -b --factory-startup -P generate.py -- --config config/macro-knob.json --stage frames
    python generate.py --config config/macro-knob.json --stage frames      # with the `bpy` module

Stages (each writes under output/<name>/):
    calibrate   probe render: the exposure that puts the top face on the config's target
    variants    one still per material variant (same geometry, camera, light, exposure)
    indicators  engraved inlay vs printed line
    cameras     the camera presets side by side
    frames      the whole rotation: frame 0 = minimum, the last = maximum
    all         calibrate, variants, indicators, cameras, frames

Then run postprocess.py (plain Python + Pillow) for the comparison sheets, runtime atlases
and manifest.
"""

import argparse
import hashlib
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy  # noqa: E402

from osp_assets import config as cfg  # noqa: E402
from osp_assets import scene as studio  # noqa: E402


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--config", required=True)
    p.add_argument("--stage", default="all", choices=["calibrate", "variants", "indicators", "cameras", "frames", "all"])
    p.add_argument("--out", default=None, help="output folder (default output/<name>)")
    p.add_argument("--material", default=None, help="material preset for indicators/cameras/frames")
    p.add_argument("--frames", type=int, default=None, help="frame count (default: the config's)")
    p.add_argument("--only", default=None, help="comma-separated frame indices (others are left as they are)")
    p.add_argument("--samples", type=int, default=None)
    p.add_argument("--resolution", type=int, default=None)
    p.add_argument("--force", action="store_true", help="re-render frames that already exist")
    p.add_argument("--set", action="append", default=[], metavar="KEY=VALUE", help="override a config value (JSON)")
    return p.parse_args(argv)


def config_hash(config):
    return hashlib.sha256(json.dumps(config, sort_keys=True).encode()).hexdigest()[:16]


def angle_of(config, value):
    rot = config["rotation"]
    return rot["start"] + value * (rot["end"] - rot["start"])


def write_json(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, sort_keys=True)
        f.write("\n")


def calibrate(config, out, material):
    """The exposure that puts the top face (a patch below the centre, clear of the line) on
    the target colour's luminance. Deterministic: fixed seed, settings and patch."""
    c = config["render"].get("calibrate", {})
    path = os.path.join(out, "calibration.json")
    if not c.get("enabled", False):
        return 0.0
    scene, knob, info = studio.build(config, material)
    res, spp = c.get("probeResolution", 160), c.get("probeSamples", 96)
    studio.configure_render(scene, config["render"], res, spp, 0.0)
    studio.set_angle(knob, angle_of(config, 0.5))
    probe = os.path.join(out, "probe.png")
    margin = cfg.camera(config["camera"])["margin"]
    px_per_mm = res / (2.0 * info["outer"] * margin)
    centre = (0.5 * res, 0.5 * res + 0.4 * info["rt"] * px_per_mm)   # below the centre (y down)
    target = studio.luminance(cfg.srgb_to_linear(c["target"]))
    import math
    exposure = 0.0
    for attempt in range(8):
        # A clipped probe says nothing about how far over it is: step down a whole stop.
        scene.view_settings.exposure = exposure
        studio.render_to(scene, probe)
        measured = studio.luminance(studio.measure_patch(probe, centre, 1.6 * px_per_mm))
        if measured >= 0.985:
            exposure -= 1.0
            continue
        step = math.log2(target / measured) if measured > 0 else 1.0
        exposure += step
        if abs(step) < 0.01:
            break
    exposure = round(exposure, 4)
    write_json(path, {"material": material, "measuredLuminance": measured, "targetLuminance": target,
                      "target": c["target"], "exposure": exposure, "resolution": res, "samples": spp})
    print(f"calibrate: measured {measured:.4f}, target {target:.4f} -> exposure {exposure:+.3f} EV")
    return exposure


def exposure_for(config, out, material):
    path = os.path.join(out, "calibration.json")
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)["exposure"]
    return calibrate(config, out, material)


def still(config, out_path, material, value, exposure, indicator_style=None, camera_name=None, resolution=None, samples=None):
    scene, knob, _ = studio.build(config, material, indicator_style, camera_name)
    studio.configure_render(scene, config["render"], resolution or config["render"]["resolution"],
                            samples or config["render"]["samples"], exposure)
    studio.set_angle(knob, angle_of(config, value))
    t = time.time()
    studio.render_to(scene, out_path)
    print(f"  {os.path.relpath(out_path)}  ({time.time() - t:.1f} s)")


def frames(config, out, material, exposure, count, only, force, resolution, samples):
    scene, knob, _ = studio.build(config, material)
    res = resolution or config["render"]["resolution"]
    spp = samples or config["render"]["samples"]
    studio.configure_render(scene, config["render"], res, spp, exposure)
    folder = os.path.join(out, "master")
    os.makedirs(folder, exist_ok=True)
    indices = range(count) if only is None else only
    entries = []
    t_all = time.time()
    for i in range(count):
        value = i / (count - 1)
        entries.append({"index": i, "value": value, "angle": angle_of(config, value)})
    for i in indices:
        path = os.path.join(folder, f"frame_{i:03d}.png")
        if os.path.exists(path) and not force:
            continue
        studio.set_angle(knob, entries[i]["angle"])
        t = time.time()
        studio.render_to(scene, path)
        print(f"  frame {i + 1}/{count} ({time.time() - t:.1f} s)", flush=True)
    write_json(os.path.join(out, "render-info.json"), {
        "schemaVersion": 1,
        "name": config["name"],
        "material": material,
        "exposure": exposure,
        "frameCount": count,
        "resolution": res,
        "samples": spp,
        "blender": bpy.app.version_string,
        "configHash": config_hash(config),
        "config": config,
        "frames": entries,
        "seconds": round(time.time() - t_all, 1),
    })


def main():
    args = parse_args()
    config = cfg.load(args.config, args.set)
    out = args.out or os.path.join(cfg.ROOT, "output", config["name"])
    os.makedirs(out, exist_ok=True)
    material = args.material or config["material"]
    stages = ["calibrate", "variants", "indicators", "cameras", "frames"] if args.stage == "all" else [args.stage]
    exposure = None
    for stage in stages:
        print(f"== {stage}", flush=True)
        if stage == "calibrate":
            exposure = calibrate(config, out, config["material"])
            continue
        if exposure is None:
            exposure = exposure_for(config, out, config["material"])
        if stage == "variants":
            for name in config["variants"]:
                still(config, os.path.join(out, "variants", f"{name}.png"), name, 0.5, exposure,
                      resolution=args.resolution, samples=args.samples)
        elif stage == "indicators":
            for style in ("engraved", "printed"):
                still(config, os.path.join(out, "indicators", f"{style}.png"), material, 0.62, exposure, indicator_style=style,
                      resolution=args.resolution, samples=args.samples)
        elif stage == "cameras":
            with open(os.path.join(cfg.ROOT, "scenes", "cameras.json"), "r", encoding="utf-8") as f:
                names = sorted(json.load(f)["presets"])
            for name in names:
                still(config, os.path.join(out, "cameras", f"{name}.png"), material, 0.5, exposure, camera_name=name,
                      resolution=args.resolution, samples=args.samples)
        elif stage == "frames":
            count = args.frames or config["frames"]["count"]
            only = [int(i) for i in args.only.split(",")] if args.only else None
            frames(config, out, material, exposure, count, only, args.force, args.resolution, args.samples)


if __name__ == "__main__":
    main()
