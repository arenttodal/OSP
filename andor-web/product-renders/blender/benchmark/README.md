# ANDOR production render benchmark (portable bundle)

**What it is:** the production scene and one shot, runnable unchanged on any machine with Blender 4.2 LTS: a GPU workstation, a rented GPU instance or a render service.

**What it measures:**
- the time per frame at final settings, on whatever device the machine has;
- the frames themselves, for the colour and material parity check against the CPU reference.

**No account, upload or external spend is made by anything in this bundle.**

## Contents (`make_bundle.sh` builds `andor_render_benchmark.zip`, about 6 MB)

| Path | What it is |
|---|---|
| `product-renders/blender/scripts/andor_scene.py`, `motion_preview.py` | The scene builder (geometry, materials, light) and the shot driver |
| `product-renders/blender/config/campaign_setups.json` | Setups, shots, lights |
| `product-renders/blender/textures_0.9.0-beta.1/` | The face texture, geometry and teak strip (the release interface; regenerate with `prep_textures.py` from `assets/source/captures/osp-0.9.0-beta.1/`) |
| `tools/blender-assets/config/macro-knob.json` | The approved macro knob definition |
| `reference/` | CPU reference frames and timings from the build machine |

The scene is built from scripts and textures, with no `.blend` file and no add-ons. It needs Cycles and OpenImageDenoise, both bundled with Blender.

## Run (from the bundle's root)

```bash
blender -b --python product-renders/blender/scripts/motion_preview.py -- \
  --plan G2 --cuts D02_orbit_9x16 --width 1080 --fps 24 --samples 96 --noise 0.01 \
  --range 60 66 --device OPTIX --timings bench_OPTIX.json --out out_OPTIX
```

**`--device`:**
- **CUDA or OPTIX:** NVIDIA;
- **HIP:** AMD;
- **METAL:** Apple Silicon;
- **ONEAPI:** Intel Arc;
- **CPU:** any machine.

If the requested device isn't found, the run falls back to CPU and says so in `bench_*.json`.

**Results:**
- `bench_<device>.json`: the Blender version, devices, samples and seconds per frame;
- `out_<device>/frames/`: the six frames (16-bit PNG).

## Parity check (colour management and materials)

```bash
python3 compare_reference.py reference/frames out_OPTIX/frames
```

**What it reports:** for each frame against the CPU reference:
- the mean and 99.9th-percentile difference;
- the mean colour shift per channel.

**Passes when** the mean colour shift is under 0.5 levels of 255 and no structural difference is visible.

CPU and GPU Cycles converge to the same image but with different noise, so per-pixel noise differences are expected. A colour shift is not expected: one would point to a different view transform, OCIO config or Blender version.

## Production settings (Phase 6)

| | |
|---|---|
| Blender | 4.2 LTS (4.2.0 on the build machine) |
| Engine | Cycles, OpenImageDenoise, a fixed seed (0, not animated) |
| Samples / adaptive threshold | 96 / 0.01. See the Phase 6 flicker study for why 0.01, not the drafts' 0.05 |
| Colour | View transform AgX, look "AgX - Punchy" (`andor_scene.py` defaults), exposure per setup from `campaign_setups.json`; 16-bit PNG out |
| Frame rate | 24 fps |
