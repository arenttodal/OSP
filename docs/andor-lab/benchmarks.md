# ANDOR Lab — CPU and memory measurements

**Hardware: a virtualised Linux container** (4 vCPU "Intel Xeon Processor @ 2.80GHz", 15 GB
RAM, GCC 13.3 `-O3` Release, no other load during the matrix). This is not a Mac and not a
musician's machine: absolute numbers will differ, and single-block spikes (worst cases of
~3–8 ms) come from the virtualisation as much as from the code. **No CPU-efficiency claim is
made from these numbers**; they show scaling and headroom, and they are the method to repeat
on reference hardware:

```sh
research-renderer --andor-bench --seconds 3 --output-json bench.json                    # typical patches
research-renderer --andor-bench --engine grain --preset stress --output-json stress.json # dense grains
```

Each cell renders 3 s with the given number of voices held (one voice re-triggered every
0.5 s), timing every `render()` call. "Load" = mean callback time ÷ block duration; "overruns"
= callbacks longer than their block (out of ~1000–4500 per cell).

## Typical patches

Grain: SIZE 150 ms, DENSITY 30/s, KEY DNA variation 50 %, OSC 25 %, LFO → POSITION, SPACE
30 %, ECHO 20 %, HIGH quality, the vocal phrase as a Note Atlas. Phrase: the vocal phrase,
EXTENDED fill, SUSTAIN, TONE 80 %, SPACE 30 %, ECHO 20 %.

Mean load in % of the block, for 1 / 4 / 8 / 12 / 16 voices (`data/bench-typical.txt`):

| Rate | Block | Grain | Phrase |
| --- | --- | --- | --- |
| 44.1 kHz | 128 | 2.6 / 4.2 / 5.5 / 7.0 / 9.4 | 3.1 / 4.3 / 6.9 / 9.6 / 14.9 |
| 48 kHz | 64 | 3.9 / 5.0 / 6.2 / 8.8 / 12.9 | 2.3 / 3.3 / 6.3 / 8.2 / 13.8 |
| 48 kHz | 128 | 3.0 / 5.6 / 8.6 / 7.6 / 10.4 | 2.5 / 3.6 / 6.5 / 7.9 / 9.7 |
| 48 kHz | 512 | 2.9 / 4.1 / 8.2 / 15.3 / 11.3 | 2.2 / 4.5 / 9.8 / 9.5 / 10.9 |
| 96 kHz | 64 | 5.8 / 10.9 / 12.4 / 15.7 / 18.4 | 4.7 / 7.4 / 11.9 / 13.5 / 18.1 |
| 96 kHz | 128 | 6.3 / 8.3 / 12.4 / 16.5 / 23.1 | 5.7 / 7.8 / 9.9 / 14.8 / 17.1 |

Highest mean anywhere: Grain 23 %, Phrase 18 %. Overruns: 19 of 82 645 Grain callbacks and
27 of 82 645 Phrase callbacks, scattered single blocks without a pattern by size or voices
(virtualisation jitter).

## Dense grains (stress)

SIZE 600 ms, DENSITY 120/s, PITCH SCATTER 2 st, the rest as above (HIGH budget 384 grains;
`data/bench-grain-stress.txt`):

| Rate | Block | 1 / 4 / 8 / 12 / 16 voices | Overruns |
| --- | --- | --- | --- |
| 48 kHz | 128 | 7.4 / 16.8 / 18.9 / 21.6 / 31.5 % | 3 / 5625 |
| 48 kHz | 512 | 7.0 / 10.8 / 19.5 / 25.4 / 25.1 % | 0 / 1405 |
| 96 kHz | 128 | 19.1 / 31.9 / 56.1 / 58.7 / 62.0 % | 133 / 11 250 |
| 96 kHz | 512 | 20.8 / 38.4 / 39.5 / 50.2 / 62.6 % | 10 / 2810 |

The budget holds (peak 228 of 384 grains at 16 voices; the fair share caps each voice), so
the cost stays bounded however extreme the settings. At 96 kHz with 12–16 dense voices the
container runs at 50–60 % mean with overruns: **recommendation for now — STANDARD (12
voices, 192 grains) at 96 kHz until measured on the Mac**. The quality limits (8 / 12 / 16
voices, 96 / 192 / 384 grains) remain provisional.

## Where the time goes (profile-free reasoning, to confirm with a profiler on the Mac)

- Grain: per grain and sample, one window lookup and two 4-point Hermite reads; per voice, a
  stereo SVF and the oscillator when mixed in. Cost ∝ active grains, which the budget bounds.
- Phrase: per voice and sample, a Kaiser-windowed sinc read (32 taps at unity, up to ~128 at
  4× upward transposition via precomputed stretch tables), doubled inside loop and release
  cross-fades; the bloom adds up to 8 grains.
- No SIMD yet (spec: profile first). Obvious next steps if a Mac profile asks for them:
  SIMD sinc dot products (shared with OSP's "CPU step 2"), and grain reads in blocks.

## Memory

| Case | Peak resident memory (whole process) |
| --- | --- |
| Phrase analysis CLI, 10 s phrase | 13 MB |
| Phrase analysis CLI, 30 s phrase | 28 MB |

Per instance: one decoded copy of the source (float, ≤ 2 channels) plus one padded playback
copy (shared by every voice; for a hybrid, shared by both engines — tested), the atlas, and
fixed pools (Phrase 32 voices with a 4.4 KB sinc kernel each; Grain 20 voices and 512 grains).
A 30 s stereo 48 kHz source costs about 2 × 11.5 MB.
