# Final-quality 1920 review renders: the flicker fix for every approved shot (21 Nov)

**Why.** The owner saw flicker in the R4 drafts (R03 first, then R04 and the others). The cause is the draft
setting: 768 px at 6 samples, where the denoiser boils frame to frame on the screens and highlights
(`r03_screen_flicker.md`). The fix is final quality: no change to the scene, the light or the camera.

**Settings:** 1920 × 1080, 24 fps, 96 samples, adaptive noise 0.01, OpenImageDenoise on the GPU, a fixed seed.
Every frame was rendered on the frozen interface (`interface_frozen: true`, commit `32e0bee`), which is recorded in
`final_1920/meta_*.json`. The masters are H.264 at CRF 14, each exactly duration × 24 frames.

| Shot | Frames | GPU | Seconds per frame |
|---|---|---|---|
| R04 Window Light | 384 (16.0 s) | RTX 4090 | 2.92 |
| D2 Macro Row | 288 (12.0 s) | L40S | 3.99 |
| R03 Immersive Glide, source take; the 15.5 s edit (372) is cut from it | 408 (17.0 s) | RTX 4090 | 3.71 |
| R01 close (R4) | 132 (5.5 s) | RTX 4090 | 3.51 |
| R01 portrait (R4), plus its matte | 156 (6.5 s) | RTX 4090 | 3.61 (matte 2.16) |
| R02 R4 loop (peak −0.3 EV) | 264 (11.0 s) | RTX 4090 | 3.32 |
| R02 R3 loop | 264 (11.0 s) | RTX 4090 | 3.34 |

**Assembled:**
- The R01 T1 cut is 288 frames: the close, then the portrait with T1. T1 is composited through the portrait's matte with `compose_style_type.py` and layout `SF_A2/T1`. It covers 11.8–12.3 % of the letters, the same as the draft.
- R02 R3 against R4, side by side.
- Keyframe sheets for every shot.

**Job script:** `benchmark/final_review_job.sh` runs on the pod (driven by `pod_runner.py`).

## Flicker, draft against final

Measured by `benchmark/flicker_draft_vs_final.py`, with results in `final_1920_flicker_metrics.json`:
- 48 consecutive frames per shot, scaled to 768 px, taken from the delivered H.264 files.
- Each pair of frames is aligned by phase correlation, so the camera move cancels.
- The residual left over is change that is not motion.
- The 99.5th percentile is the measure that picks up local boiling.

| Shot | Draft | Final | Change |
|---|---|---|---|
| D2 | 30.2 | 8.7 | −71 % |
| R01 close | 19.4 | 5.0 | −74 % |
| R02 R4 | 4.1 | 3.0 | −26 % |
| R01 portrait | 5.3 | 4.1 | −23 % |
| R04 | 9.3 | 7.4 | −21 % |
| R03 | 23.5 | 23.2 | −1 % |

**Limits of the measure:**
- **R03.** The R03 figure is dominated by parallax. In the glide, near and far edges move at different speeds, which one global shift cannot cancel.
- **R03 on its own.** Measured on a display crop from the 16-bit frames, the residual fell from 2.59 to 2.03 levels (`r03_screen_flicker.md`).
- **Not a sign-off.** The measure is a guard-rail, not the judgement. Signing off on flicker needs viewing on the target screens.

## What went wrong, and its cost

**The first run lost five shots.** Two pods rendered all eight shots, but the encode step encoded only the first shot on each pod:
- `ffmpeg`, run inside a `while read` loop, read the rest of the job list from stdin.
- The other frames were lost when the pods were terminated.

**The fix:** `-nostdin` on every `ffmpeg` call, and Blender's stdin taken from `/dev/null`. The script was dry-run with stub tools before the re-render: every job was rendered and encoded.

**GPU spend for these renders:**
- The re-run cost $1.21.
- With the benchmark, the R03 flicker test and the first run, the session total is **$4.65 of the owner's $5 cap** (balance $10.00 → $5.35).
- No pods remain.

**Not done:** the 4K batch. It still waits for the owner's approval.
