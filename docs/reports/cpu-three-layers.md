# CPU with three layers: where it goes, and the options

This report measures one real patch ("Keys - Dreamy Mangled Keys 3", saved at state 10).
- **Sounds.** The patch's own sounds are not here, so three stand-ins are shaped like them:
  - A: a 0.75 s piano-like pluck at D5;
  - B: a 10 s flute-like tone at A5, in Granular, REVERSE;
  - C: a 3 s harp-like pluck at C5, with EQ.
- **Patch.** REIMAGINED is 30 % on A and 100 % on B and C, all KALEIDOSCOPE, with the legacy
  shared stage. The patch also uses LIFE 40, DRIFT 40, SPACE 67 (HALL), ECHO 34, the CHARACTER
  ladder and five routes.
- **Playing.** 5-note chords every 1.5 s, held 1.2 s. Optionally the sustain pedal is held
  throughout.
- **Machine.** This container: a 2.1 GHz Xeon VM, 48 kHz, 256-sample blocks (5.3 ms). A Mac
  is roughly 2–4× faster.
- **Harness.** `OSP_CPU_PRESET=<file> [OSP_CPU_SET=id=v,...] [OSP_CPU_PEDAL=1] [OSP_CPU_UI=1]
  ./osp_plugin_tests "[cpu-preset]"`. It times every block: the mean, the median, the 99th
  percentile and the worst, with the note-on blocks apart. Callgrind was run on the same
  harness.

## 1. What it costs

| Playing | Mean | Median | p99 | Worst |
|---|---|---|---|---|
| 5-note chords (up to 10 notes sounding) | 44 % | 43 % | 80 % | 114 % |
| the same, pedal held (notes pile up to 38) | **123 %** | 136 % | **252 %** | 311 % |
| pedal, every REIMAGINED at 0 | 55 % | 52 % | 99 % | |

- **Note-ons are not the cause.** Blocks with note-ons are cheaper than the median. The load
  is steady, and it grows with the number of sounding voices.
- **Stutters.** The pedal case is over budget on this VM. On a Mac, the 99th-percentile blocks
  of a dense pedalled passage reach about 60–120 % of the budget. That matches stutters at
  256.

### What costs what (chords, no pedal; one thing changed at a time)

| Changed | Mean | Saving |
|---|---|---|
| as saved | 44 % | |
| **REIMAGINED 0 on all three** | **22 %** | **half** |
| B's REIMAGINED 0 | 33 % | 11 points |
| C's REIMAGINED 0 | 37 % | 7 points |
| B muted | 26.5 % | B costs about 18 points |
| C muted | 28 % | C costs about 16 points |
| A muted | 43 % | A costs about 1 point (it plays below its root: cheap reads) |
| SPACE 0 / ECHO 0 / MOVEMENT 0 / LIFE 0 / CHARACTER Tilt | 42–48 % | within the noise: the effects are not the problem |
| B in One Shot instead of Granular | 56 % | Granular is the cheaper mode here |

### Where the instructions go (callgrind, inside processBlock)

| Function | Share | What it is |
|---|---|---|
| `GranularSource::read` + `render` | **36 %** | B's grains: about 8 overlapping per note (density 25.6/s × size 0.32 s). REIMAGINED's doubling plays a second cloud: 70–86 grains for 5 notes. Each grain does a 4-point Hermite read per channel per sample. |
| `readFrame` + `computeKernel*` | 18 % | the band-limited (sinc) reads of A and C |
| `InstrumentVoice::render` | 11 % | the per-sample voice chain |
| `CharacterFilter::ladder` + `process` | 10 % | the LP24 per voice, both channels |
| `readHermite` | 6.5 % | KALEIDOSCOPE's grains on C |
| `tanhf` / `expm1f` | 4.8 % | KALEIDOSCOPE's saturation (libm per sample) |
| `SpaceReverb` | 3.3 % | |
| `sin` | 2.2 % | the doubling head's modulation, per sample |

### The editor (message thread)

| Card | Shows | One full display frame, 1× / 2× | One head's strip, 1× / 2× |
|---|---|---|---|
| A | 7–8 read heads | 3.6–4.4 / 13–14 ms | 0.50 / 1.22 ms |
| B | 70–86 grains | 4.0–4.7 / 13–17 ms | 0.61 / 0.94 ms |
| C | 10 read heads | 3.5 / 12.6 ms | 0.49 / 0.78 ms |

- **One head per voice.** Every voice that is playing has a head, releasing voices included,
  up to 64 per card. Each frame (30 fps) repaints two 4 px strips per head: where it was and
  where it is.
- **Past about 8 heads, the strips cost more than one full repaint.** C's 10 heads at 2× are
  about 16 ms per frame.
- **Total.** The three displays together are about 40 ms per frame at 2× here, more than one
  VM core at 30 fps. On a Mac (CoreGraphics) it is several times less, but still a real
  share of a core.
- **The audio meter.** The editor runs on the message thread, not the audio thread, so it does
  not show in Ableton's audio meter. On a laptop it still competes with the audio threads for
  cores.

## 2. Options

Each estimate is against the chord case above (mean 44 %). "Bit-exact" means the output is
sample-identical. "Inaudible" means float rounding only, about −140 dBFS or below, with one
golden update explained in the commit.

### Audio, group 1: the same sound, bit-exact

| # | Change | Est. saving | Repercussions |
|---|---|---|---|
| A1 | Granular inner loop: one index and one set of Hermite weights for both channels; walk only the playing grains | −7 to −10 % | none |
| A2 | The same fused read for KALEIDOSCOPE's grains | −2 % | none |
| A3 | The sinc stretch level cached per control period (now one `log2` per sample) | −1 to −2 % | none |

### Audio, group 2: inaudible numerics (one golden update)

| # | Change | Est. saving | Repercussions |
|---|---|---|---|
| A4 | SIMD sinc reads: both channels share the weights, 4–8 lanes | −10 % here (more on patches without Granular) | renders differ at about −140 dBFS; goldens and the REIMAGINED reference hashes updated once |
| A5 | SIMD ladder, left and right in one pass | −4 % | the same |
| A6 | KALEIDOSCOPE: a rational `tanh` (error < 1e-6) and a phasor in place of `sin` | −6 % | the same |
| A7 | Granular grains in SIMD lanes (on top of A1) | −5 % | the same |

### Audio, group 3: voice management (caps the worst case)

| # | Change | Est. saving | Repercussions |
|---|---|---|---|
| V1 | **Retire inaudible voices.** A released voice whose output (envelope × layer level × mix) stays below about −80 dBFS for 50 ms ends with a 5 ms fade. B (−24.8 dB) and C (−18.4 dB) tails end sooner. | large in the pedal case (the tails are most of the 38 voices); 0 when notes are short | nothing audible in practice. SPACE and ECHO tails are on the bus and unaffected. Not bit-exact for tails below −80 dBFS. |
| V2 | **Same pitch, at most two voices per layer.** A key struck again under the pedal fades its oldest voice. This is how acoustic pianos behave. | moderate under the pedal with repeated notes | slightly less build-up when one key is hammered with the pedal down |
| V3 | **One voice budget for the instrument** (for example 48 voices across A + B + C, now 24 × 3 = 72). The quietest released voice is stolen first, with a fade. | bounds the worst case: at most two-thirds of today's worst | the oldest tails of very long pedal washes are cut (faded) early |

### Audio, group 4: REIMAGINED, the largest single cost

| # | Change | Est. saving | Repercussions |
|---|---|---|---|
| R1 | **Polyphony-aware detail.** Above 8 sounding notes, each note's KALEIDOSCOPE grains and its Granular doubling cloud thin out (crossfaded), so the cost grows more slowly than the voice count. | pedal case about −30 %; 0 in light playing | dense chords get slightly less per-note shimmer, which the chord largely masks. Not bit-exact for existing patches when many notes sound; needs a listening check. |
| R2 | **REIMAGINED's doubling and saturation per layer** instead of per note | −30 to −40 % | **the sound changes**: saturating the chord intermodulates, and the doubling is shared. Only as an optional "light" mode. |

### Audio, group 5: quality modes and threads

| # | Change | Est. saving | Repercussions |
|---|---|---|---|
| Q1 | **Live quality Eco / Full** (Advanced). Eco uses the 8-zero-crossing sinc and lighter REIMAGINED detail; a bounce (offline render) is always Full. | −25 to −35 % live | what you hear live differs slightly from the bounce, mostly at extreme transpositions. A familiar trade (some synths do this) that people either like or distrust. |
| Q2 | **The layers on several cores**: A, B and C rendered in parallel on a real-time pool joined to the host's audio workgroup (macOS) | up to about 2.5× more headroom for one instance; the same total CPU | bit-exact, but a large, delicate piece of engineering (thread wake-ups, priorities). In a big session Ableton already spreads tracks over the cores, so this mainly helps one heavy instance. |

### The editor

| # | Change | Saving | Repercussions |
|---|---|---|---|
| U1 | **Many heads: one repaint of the display** instead of two strips per head (from about 8 heads) | caps a frame at one display repaint | none: identical pixels |
| U2 | **The waveform and its well cached as an image** per display and scale; a frame is a copy plus the lines | about 12 ms → 1–2 ms per frame at 2× (estimate) | none visible; 1–4 MB of memory per display |
| U3 | **At most 6–8 heads per card**: the held notes first, released ones fading out of the picture | bounded frames even with the pedal | dense playing shows fewer lines. The lines show where each voice reads; past a few, they are noise rather than information. |
| U4 | **Heads at 20 fps** when many are moving | −33 % of head frames | slightly less fluid motion |
| U5 | **Granular: at most about 40 grains drawn** (the loudest) | about half of B's frame | a thinner-looking cloud in dense playing |

## 3. Recommendation

1. **First:**
   - group 1 (A1–A3);
   - V1 (inaudible-tail retirement) and V2 (two voices per pitch);
   - on the editor, U1 + U2, and U3 at 8 heads.
   No audible change, low risk. The chord case should drop by about a sixth, the pedal case by
   about half. The editor's frames should get many times cheaper.
2. **Then group 2** (A4–A7): about another −20 %, with one golden update. Together with step 1,
   the chord case is about 44 → 25–28 % on this VM.
3. **Only if still needed:** R1 (with a listening check) or V3 at a generous budget.
4. **Not recommended for now:**
   - R2: it changes the sound.
   - Q1 as the default: live and bounce would differ. It could come later as an opt-in.
   - Q2: large; worth it only if one instance must carry very dense three-layer playing.

## 4. Step 1: done

Chosen: A1–A3, V1, V2, U1–U3. U2 was already there (the display cached its waveform); the
real cost turned out to be elsewhere, see below.

| What | Change | Result |
|---|---|---|
| A1 | Granular: one index and one set of weights for both channels; the level correction computed when the settings change instead of every sample (a `sqrt` per sample) | bit-exact; granular −13 % |
| A2 | KALEIDOSCOPE grains: the same fused read | bit-exact; that read −36 % |
| A3 | The sinc stretch level cached per voice (it depends only on the increment) | bit-exact |
| V1 | A voice whose key is up, quieter than −90 dBFS after its layer's mix for 0.25 s, ends with the 5 ms steal fade. The level margin allows for EQ and DRIVE after it. Granular voices still making grains under the pedal, and REIMAGINED mode engines, are never retired (their output has natural gaps). | inaudible; ends tails nobody hears |
| V2 | At most two voices of one key per layer: a key struck again fades its oldest | the pedal case: 38 → 30 voices |
| U1 + U3 | At most 8 read heads per card (the loudest); past 12 moving strips, one repaint of the plot instead | bounded frames |
| U2' | **The display is opaque.** It paints its parents' picture (taken once per position, size, scale, skin and focus) behind its rounded corners, so a moving head repaints only the display, never the housing and card behind it. The overlay skips halos outside the repaint. | pixel-identical (worst difference 0 of 255 at 1× and 2×); **a full display frame 13–15 → 3–4 ms at 2×** |

**Audio, by instruction counts** (callgrind, the same 4 s): 10.90 G → 9.53 G, **−12.6 %**. By
time on this VM (noisier):

| Playing | Mean, before → after | p99, before → after |
|---|---|---|
| chords | 44 → 37–41 % | 80 → 70–76 % |
| pedal | 123 → 114–117 % | 252 → 221–228 % |

**The pedal case stays heavy.** The notes the pedal holds are still clearly audible, so V1
rightly leaves them alone. What bounds that case is step 2, or V3 / R1: REIMAGINED at 100 %
on B and C is still half the cost.

Tests: [voices] (two per key, and the limit off; an inaudible release retired, an audible
one, and retiring off); the display's cap and opacity; the goldens unchanged.

## 5. Step 2: done

Chosen: A4–A7. They are the inaudible group: no setting, no voice and no stage changes what it
does; each loop computes the same thing in a cheaper order.

| What | Change | Its own difference (unit tests) |
|---|---|---|
| A4 | The sinc read (`SincInterpolator::readFast`): the table weights blended and applied to both channels in one pass, never stored; the taps summed in four SIMD lanes. KALEIDOSCOPE's and the REIMAGINED modes' sinc reads use it too. | ≤ 4e-7 on full-scale noise (−128 dB) |
| A5 | The LP24 ladder: left and right in two lanes of one pass; its per-sample constants (powers of G, the feedback reciprocal, the saturator's bias offset) computed with the coefficients | −136 dB or less below its output |
| A6 | KALEIDOSCOPE's saturation: a rational `tanh` (the minimax fit Eigen uses) instead of libm's, which goes through `expm1f`; the doubling's slow sway by rotation instead of a `sin` per sample | tanh ≤ 4e-7; the sway ≤ 2e-9 after ten minutes |
| A7 | Granular and KALEIDOSCOPE's grains: the Hermite read as four tap weights on one 4-sample load, the window and pan folded into the weights, the grains summed in lanes | ≤ 7e-7 on full-scale noise |

The SIMD is SSE2 on Intel and NEON on Apple Silicon, both part of those processors' baselines,
so the universal binary needs no runtime dispatch (`core/Simd.h`; a plain fallback elsewhere).
The baselines A and B keep their own reads, so the goldens are unchanged and needed no update.

**Audio, by instruction counts** (callgrind, the same 4 s): 9.53 G → **5.86 G, −38.5 %**
(10.90 G → 5.86 G, −46 %, with step 1).

| Function | Step 1 | Step 2 |
|---|---|---|
| Granular (B's grains) | 3.37 G | 1.79 G |
| The voice loop, with KALEIDOSCOPE's grains, saturation and sway | 2.20 G | 1.32 G |
| The sinc reads | 1.74 G | 0.97 G |
| The LP24 ladder | 1.00 G | 0.59 G |

By time on this VM. It ran faster this day than for step 1, so before and after were measured
the same day:

| Playing | Mean, before → after | p99, before → after |
|---|---|---|
| chords | 31 → **18 %** | 57–60 → **28–29 %** |
| pedal | 100 → **57 %** | 189 → **103 %** |

**What changed in the sound.** The same seeded performance of the preset (20 s, chords, with
and without the pedal) rendered with both builds. The difference is −120 dBFS RMS, peaking at
−103 dBFS, about 90 dB below the music. That is below the noise floor of a 16-bit file.

It is the floor for any change here. Moving every one of the old reads by a single float step
(1 ulp, about 1e-7) gives the same difference: −91 dB below the music. The voice chain carries
rounding up to that level whichever way it arises.

Two renders of one build are bit-identical: still seed → exact output.

Tests: `tests/unit/FastNumericsTests.cpp` keeps each fast form against the one it replaced.
Each fast form is written out there as it was.
