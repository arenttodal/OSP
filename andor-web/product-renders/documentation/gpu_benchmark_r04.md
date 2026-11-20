# GPU final-quality benchmark: R04 Window Light (20 Nov)

**What was measured:** R04 Window Light (16 s, 384 frames) at the production settings:
- 96 samples, adaptive noise 0.01, OpenImageDenoise, a fixed seed;
- 16-bit PNG, at the final 3840 × 2160;
- on the frozen interface (`textures_0.9.0-beta.1`, approved commit `32e0bee`), confirmed in every frame's metadata.

**Where:** a rented Runpod RTX 4090 (Secure Cloud, US, $0.89/h) running Blender 4.2.0, the same version as the build machine.
- Driven from this session over Runpod's API and the pod's HTTPS port proxy, with `benchmark/pod_runner.py` running on the pod.
- Approved by the owner with a $5 cap. **Spent: $0.99 in total** (balance $10.00 → $9.01). Both pods were terminated, and the account has no pods left.

## Results

| | Run 1: OptiX, denoised on the CPU | Run 2: OptiX, denoised on the GPU |
|---|---|---|
| Seconds per 4K frame (6 frames) | 53.4–56.3, **mean 54.5** | 11.9–12.1, **mean 12.0** |
| R04, 384 frames | 5.8 h | **1.3 h** |
| Seconds per 1920 frame (parity pass) | 15.4–16.1 | (not timed separately) |

**Why run 1 was slow.** The GPU rendered each 4K frame in about 8 s. Cycles then wrote the tiled frame to disk, read it back, and denoised it on the pod's CPU, which took about 43 s. In run 2:
- the denoiser runs on the GPU (`denoising_use_gpu`);
- the frame is rendered without tiles: 2.2 GB peak on a 24 GB card.

`cine_rig.py` now does both whenever it renders on a GPU, and records `denoise_gpu` in the metadata.

## Checks

**Colour and material parity, GPU against CPU.** Frame 25 at 1920 px, compared with the build machine's CPU reference (`benchmark/compare_reference.py`):

| | Mean difference | 99.9th percentile | Colour shift (levels of 255) |
|---|---|---|---|
| Run 1 | 0.01 | 1.0 | 0.00 / 0.00 / 0.00 |
| Run 2 | 0.05 | 1.0 | +0.04 / −0.01 / +0.02 |

**PARITY OK** for both: the same view transform, the same materials and no colour shift.

**GPU denoising against CPU denoising.** The same 4K frame (217) from the two runs differs by at most **1 level** at any pixel (mean 0.05). GPU denoising is a pure speed-up.

**Frame-to-frame stability.** Three consecutive 4K frames (217–219) were compared.
- The two steps between them are equal: mean 0.66 and 0.66 in run 1, 0.68 and 0.68 in run 2.
- The temporal second difference is the same in both runs: 0.52 and 0.56, with the 99.9th percentile at 26. Smooth changes in the light cancel in that measure; flicker does not. The larger values sit at the moving window light's edges.
- So the denoiser adds no flicker of its own. A full judgement needs a longer run of consecutive frames. The first final shot will provide that.

## Production estimate (for approval: nothing is rendered)

**Scope:** the approved shots at 4K, final quality, each with its 1 s handles. The rate is run 2's measured 12.0 s per frame, applied to every shot.

| Shot | Body frames | With handles |
|---|---|---|
| R04 Window Light (16 s) | 384 | 432 |
| D2 Macro Row (12 s) | 288 | 336 |
| R03 Immersive Glide, full source take (17 s); the 15.5 s edit is cut from it | 408 | 456 |
| R02 Silhouette (11 s, a loop: handles optional) | 264 | 312 |
| R01 close (5.5 s) | 132 | 180 |
| R01 portrait (6.5 s) | 156 | 204 |
| H01 dolly back (10 s; only after the still is approved) | 240 | 288 |
| **Total** | **1,872** | **2,208** |

| | Estimate |
|---|---|
| GPU time | 2,208 frames × 12 s ≈ **7.4 GPU-hours**, plus about 0.3 h for the R01 and H01 mattes (8 samples) |
| Cost on Runpod Secure Cloud (RTX 4090, $0.89/h) | **about $7**, or about **$8–9** with 20 % for retries and setup |
| Cost on Runpod Community Cloud ($0.34/h) | about $3, less reliable hosts |
| Wall time | about 8 h on one GPU, or about 2 h across four pods in parallel at the same total cost |
| Output | 16-bit 4K PNG at about 35 MB per frame: **about 75 GB** |

**Caveats:**
- R04 is a mid-weight shot. The close shots with depth of field (R01 close, D2) and the dark R02 may run slower or faster per frame. Allow **±50 %**, so **$5–14 on Secure Cloud**.
- The first final shot gives the real number per shot.

**What production needs before it starts** (this benchmark only needed the build machine):
1. **Storage.** 75 GB can't come back through this session. The pods should write to a Runpod network volume or to the owner's Cloudflare R2 bucket, and encode the review and delivery files (ProRes 4444 or H.265 masters, plus web derivatives) next to the frames.
2. **Autonomy.** The pod should start rendering on boot and terminate itself when done, using a Runpod key limited to that job. Run 2's pod sat idle for 53 minutes, about $0.80, while this session restarted: a pod that waits for its driver costs money whenever the driver is away.
3. **The frozen interface.** Already enforced: every final-quality render checks `FROZEN.json` before it starts.

**Do not launch the full 4K batch without the owner's approval** (the R3 review).

## Reproduce

`benchmark/make_bundle.sh <out>` builds `andor_render_benchmark.zip`: the scene, the rig, R04 and the frozen textures, about 6 MB. Its layout mirrors the repository. Then, on any GPU machine with Blender 4.2:

```bash
./run_benchmark.sh OPTIX /path/to/blender
```

**Timing data:** `benchmark_r04/run1_optix_cpu-denoise_timings.json`, `run2_optix_gpu-denoise_timings.json` and `run2_nvidia_smi.txt`.
