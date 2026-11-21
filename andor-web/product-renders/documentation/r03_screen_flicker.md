# R03: screen flicker, draft against final quality (21 Nov)

**The problem:** the R3 draft of R03 shows visible flicker on the three displays, which makes it feel low quality.

## The test

**Footage:** R03's display section, frames 169–216, which is 2 s and 48 consecutive frames. It was rendered on a Runpod RTX 4090 on the frozen interface (`32e0bee`), at the production settings: 96 samples, noise 0.01, OptiX, denoised on the GPU, a fixed seed.

**Versions compared:**

| Version | Seconds per frame |
|---|---|
| The R3 draft (768 px, 6 samples) | (already rendered) |
| Final quality at 1920 | 3.8 |
| Final quality at 3840, downscaled to 1920 | 11.2 |

**Cost:** about $0.40 of GPU time across two pods. The first pod was terminated before its 4K pass finished, so the job was run again.

## What causes it

At 6 samples per pixel, the denoiser has very little to work with on the displays' fine detail: the waveform particles, the hairlines and the small text. It reconstructs that detail differently in every frame, so the particles smear into blotches that change shape from frame to frame. As the camera glides, this reads as boiling on the screens.

At final quality the same particles are crisp, round and **identical from frame to frame**, and the hairlines and text hold steady. `R03_screens_consecutive-frames.jpg` shows three consecutive frames of each version, enlarged.

## Measurement

**Method:** each pair of consecutive frames around display B was aligned by phase correlation, which cancels the camera's 4.1 px-per-frame glide. Anything still changing between the aligned frames is either flicker or parallax: the screen, the bezel and the glass reflections sit at different depths, and a single shift can't align them all. Measured at 768 px for all three versions.

| Version | Mean change left after alignment (levels of 255) | 99th percentile | Pixels changing by more than 6 levels |
|---|---|---|---|
| R3 draft, 6 samples | 2.59 | 21.0 | 11.9 % |
| Final, 1920, 96 samples | **2.03** | 18.7 | **9.1 %** |
| Final, 4K downscaled | 2.06 | 19.0 | 9.4 % |

- **Final quality removes the draft's speckle:** the change left after alignment falls by about a fifth.
- **What remains in the finals is parallax and moving reflections:** it lies along edges and the glass, not on the screen faces. It's the same at 1920 and from 4K, so rendering at 4K and downscaling gives no further gain on this content.

**An earlier unaligned measure** (the temporal second difference) did not separate the versions. At 4 px of movement per frame, sharp detail does not cancel over three frames, so that measure was dominated by the motion itself. It was not used.

## Conclusion

- **The flicker is a draft artefact.** R03 needs no change of shot, light or materials.
- **Final renders** use the production settings above. Direct 1920 renders are enough for web and 1080p; 4K is only needed where a 4K master is delivered.
- **Review copies:** clips that will be judged for screen quality should come from a final-quality render, not the 6-sample draft. At 3.8 s per frame on an RTX 4090, the whole 17 s R03 at 1920 takes about 26 minutes (about $0.40).
