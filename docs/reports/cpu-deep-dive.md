# CPU deep dive: what OSP costs, where it goes, how to make it cheaper

This is a measurement-based audit of the whole plugin: the audio engine, the editor and loading. All
numbers are from this container: a 2.1 GHz Xeon cloud VM, 48 kHz, 128-sample blocks, the JUCE
software renderer.

- **Mac estimate.** The same engine measured about 2× faster on the macOS arm64 CI runner, and a
  real Apple Silicon Mac is faster again. Treat "÷ 2–4" as the Mac estimate.
- **Harnesses.** `osp_plugin_tests "[cpu-profile]"` (the plugin's own `processBlock`, a new patch,
  held notes, one thing changed at a time), `"[ui-cpu]"` (paint cost per region) and callgrind
  profiles of the first.

## 1. How much it costs now

### Audio (the plugin's processBlock, 16 held notes = "reference")

| Case | CPU (% of one core, VM) | vs reference |
|---|---|---|
| Idle: a sound loaded, nothing playing | 1.0 % | |
| 1 / 8 / 16 / 24 notes | 1.7 / 5.6 / **12.2** / 18.9 % | ≈ 0.75 % per voice |
| 16 notes, 96 kHz | 19.3 % | 1.6× |
| 16 notes, **+24 semitones** | **56.5 %** | **4.6×** |
| 16 notes, −24 semitones | 9.7 % | 0.8× |
| 16 notes, block 32 / 1024 | 13.3 / 10.8 % | small per-block overhead |
| CHARACTER Tilt instead of LP24 | 9.8 % | the ladder costs about 2.4 points |
| MOVEMENT 0 / chorus / tape / shaper | 9.7 / 12.1 / 13.9 / 9.9 % | the default DRIFT costs about 2.5 points |
| SPACE 0 / 100 | ≈ the same (± noise) | the reverb is about 5 % of the total |
| REIMAGINED 50 / 100 (KALEIDOSCOPE) | 20.4 / **28.7 %** | 1.7× / 2.4× |
| TAPE FRAME 100 / MOSAIC 100 | 23.7 / 14.7 % | |
| Granular | 9.6 % | |
| 2 layers / 3 layers | 17.8 / **34.0 %** | 1.5× / 2.8× |
| 3 layers, every REIMAGINED 100 | **79 %** | 6.5× |

Worst single blocks on the VM spike to several hundred percent. That is the VM's scheduler, not
the code: the medians are stable. It still has to be checked on a real Mac.

### Editor (paint time, JUCE software renderer)

| What | 1× | 2× (Retina) |
|---|---|---|
| Whole editor (open, resize) | 331 ms | 733 ms |
| The editor's own background (housing, panels) inside that | ~250 ms | ~600 ms |
| **One waveform display, one frame while notes play (30 fps)** | **44 ms** | **75 ms** |
| A 3 px strip around one read head | 3 ms | 3 ms |
| One macro knob moving (automation) | 6.8 ms | 18.7 ms |
| The keyboard (all of it) | 27 ms | 124 ms |

On a Mac (CoreGraphics) these are several times faster, but the proportions hold.

- **Idle.** The editor is cheap: no timer repaints while nothing plays.
- **Playing.** Repainting each sounding card's whole display 30 times a second, through the
  expensive background, is the single largest CPU user in the whole plugin.

### Loading (worker thread, never the audio thread)

- **Analysis.** A few tens of ms per sample. The REIMAGINED data adds about 1.4 ms per 3 s
  sample.
- **Register anchors.** The Natural pitch character builds four offline pitch-shifted copies of
  every loaded sample (Signalsmith Stretch). It is the heaviest part of loading and is done even
  when Natural is never used.

## 2. What draws the most

From callgrind, inside `processBlock` only. These are shares of the audio work.

**Reference (default patch, 16 notes):**

| Function | Share | What it is |
|---|---|---|
| `InstrumentVoice::readFrame` + `SincInterpolator::computeKernel*` | **43 %** | the band-limited read: a 32-tap kernel looked up and applied, per sample, per voice, per channel |
| `CharacterFilter::ladder` + `process` | **23 %** | the LP24 ladder, per voice, per channel, even when nearly open |
| `InstrumentVoice::render` (inlined) | 15 % | shelves, envelope, gains, crossfades |
| `SpaceReverb::process` + its `std::sin` | ~9 % | **one `std::sin` per FDN line per sample** (≈ 48 M calls in 1.5 s) |
| libm `log2` / `pow` (control rate) | 3 % | pitch and filter coefficients every 32 samples, per voice |
| `Adsr::next` | 2 % | |

**REIMAGINED (KALEIDOSCOPE) at 100 % adds:**

| Function | Share | What it is |
|---|---|---|
| `readHermite` | 15 % | up to 8 grains × 2 channels read per sample |
| a second `readFrame` | | the doubling head |
| `tanhf` / `expm1f` | **7.6 %** | the saturation calls libm's tanh per sample per channel |
| `std::sin` | 4 % | the doubling head's modulation, per sample |

**Upward transposition** stretches the kernel: at +24 semitones, 4× the taps (the 4.6× above).

## 3. Compared with other plugins

These are rough, typical figures from general experience: they were not measured here. They are
per voice, on an Apple Silicon Mac at 48 kHz:

| Kind | Typical CPU per voice |
|---|---|
| Plain sample playback (a sampler, no per-voice filter) | ~0.02–0.1 % |
| Samplers with a per-voice filter and modulation | ~0.1–0.3 % |
| Wavetable / VA synths (the lighter ones) | ~0.3–1 % |
| High-fidelity analog models (zero-delay filters, oversampling) | ~2–5 % |

- **OSP's default patch.** About 0.75 % per voice on this VM. That is an estimated ~0.2–0.4 % on
  Apple Silicon, so 16 notes is about 3–6 % of a core. This is in the upper range of rich
  samplers, far below heavy analog models.
- **Heavy patches.** Three layers with REIMAGINED, or playing two octaves up, move it into
  wavetable-synth territory, up to roughly 15–30 % for 16 notes on a Mac.
- **The editor.** It is heavier than typical commercial plugins while notes play. Those cache
  their static artwork and repaint only what moved, typically under 1–2 ms per frame; OSP
  repaints whole displays through live-rendered shadows.

## Done so far (A1–A6, B1, B2, B4)

Measured on the same VM, same harnesses, before → after.

| What | Before | After |
|---|---|---|
| Whole editor paint, 1× / 2× | 331 / 733 ms | **38 / 415 ms** |
| One waveform display, one frame while playing (1×) | 44 ms (whole display) | **≈ 2 ms per read head** (two 4 px strips) |
| One macro knob moving, 1× / 2× | 6.8 / 18.7 ms | **1.7 / 5.3 ms** |
| The whole keyboard, 1× | 27 ms | **2.6 ms** (and JUCE already repaints only the key that changed) |
| Audio, idle (sound loaded, nothing playing) | 1.0 % | **0.15 %** |
| Audio, 16 notes (reference), instructions per second of audio (callgrind) | 999 M | **946 M (−5.3 %)** |

- **A1 + A5 (shadows).** Nearly all of the background's cost was `juce::DropShadow`, which blurs
  a fresh mask on every paint. `design::CachedShadow` keeps each blurred mask, keyed by the
  path's exact shape and sub-pixel position, and only draws it. The pixels are the ones
  `DropShadow` makes. With that, a separate cached background image was not worth its memory.
  34 of the 42 canonical screenshots are pixel-identical. The other 8 differ by the same amount
  between two runs of one build: they are popovers animated by the clock.
- **A2 (dirty strips).** The source display repaints each read head's old and new 4 px strip, and
  the grains' horizontal span, instead of the whole display.
- **A3 (opaque) not done.** The displays and the keyboard sit in rounded wells drawn by their
  parents. With the shadows cached, the parents' share of a strip repaint is small.
- **A4 (keyboard).** JUCE's `MidiKeyboardComponent` already repaints only changed keys; the
  shadow cache made each key cheap.
- **A6.** The REIMAGINED popover's MIRAGE picture is still. It is now redrawn only when its
  settings or analysis change; the other modes still animate.
- **B1.** SPACE's modulation comes from a rotating phasor that is re-synced to `std::sin`/`cos`
  every 256 samples. The output differs from before by ≤ 2.5e-7 (−132 dBFS). Every `std::sin` is
  gone from the profile.
- **B2.** After 0.25 s of exact digital silence in, with the tail below 1e-6 (−120 dBFS), the
  reverb sleeps: the output is the (silent) input and only the modulation phase advances. Any
  input or a type crossfade wakes it. After waking it matches a reverb that never slept within
  1.4e-7 (test `post: SPACE sleeps through silence and wakes without a trace`).
- **B4.** The CHARACTER filter recomputes `tan`, the resonance `pow`s and the drive `pow`s only
  when their own input changed. The voice takes the CHARACTER range's `log2` only when the range
  moves. This is bit-exact: the 18 REIMAGINED reference scenes null to the same 7e-9 as SPACE
  alone.
- **Still open, found on the way.** The sinc kernel's stretch-level choice takes a `log2` per
  output sample, about 1.7 % of the reference. It could be cached per control period
  (bit-exact).
- **2× editor on Linux.** The remaining 2× cost is the scaled draw of the cached masks in JUCE's
  software renderer (the 36 black-key shadows alone take ~85 ms of a full keyboard repaint). On
  macOS and Windows the platform renderer draws these images. Making them cheaper here would
  need a re-rendered (sharper) mask, so they are kept identical.

## 4. Every way to save, ranked

✓ = done (see above). The quality column says exactly what changes for the listener or viewer. "Bit-exact" means the
output is sample-identical to now: goldens, the 18 REIMAGINED reference scenes and session
recall stay identical.

### A. The editor (largest absolute saving while playing; no visual change)

| # | Change | Saving | Quality | Drawbacks |
|---|---|---|---|---|
| A1 ✓ | **Cache the static background**: the housing, its two DropShadows, the panels and each card's raised body, rendered once into images at the display's pixel scale; repaint them only on resize, scale or layout changes | the ~250 ms full paint becomes a blit; the per-frame region cost drops by most of 44 → a few ms | identical pixels | memory: one ARGB image of the window (≈ 6 MB at 2×); rebuild on scale or layout change |
| A2 ✓ | **Repaint only what moved**: each read head's old and new 3 px strip (and the grains' pane in Granular) instead of the whole display | 44 ms → ~3 ms per frame here (measured), and less with A1 | identical | slightly more bookkeeping in `SourceDisplay` |
| A3 | `setOpaque (true)` where a component really covers its area (the displays inside their well, the keyboard bed), so JUCE stops repainting the parents behind them | further cuts every frame | identical, if the opaque parts are drawn by the component itself (the well's corners) | needs care at the rounded corners |
| A4 ✓ | Keyboard: cache the key bed and keys; repaint only the keys whose state changed | 27–124 ms → under 1 ms per note change | identical | |
| A5 ✓ | Pre-render `DropShadow`s (buttons, knobs, cards) once per size instead of blurring on every paint | knobs and buttons repaint several times faster | identical | a small image per size |
| A6 ✓ | Popover pictures (REIMAGINED's always animate at 30 fps): animate only while something moves; draw at 20–30 fps | only while a popover is open | same look | |
| A7 | Optional: a GPU renderer (`juce::OpenGLContext`) | moves compositing off the CPU | same | OpenGL is deprecated on macOS; text rendering differs slightly; some hosts have trouble with GL views. Only after A1–A5. |

A1 + A2 together should make the editor's cost while playing a small fraction of today's. Nothing
looks different.

### B. Audio: no change in sound at all (bit-exact)

| # | Change | Saving (of the audio work) | Notes |
|---|---|---|---|
| B1 ✓ | **SPACE's modulation from a rotating phasor** (or a small sine table) instead of `std::sin` per line per sample | ~4–5 % of the total | Bit-exact with a table of the same values per sample; or sub-1e-7 differences with a phasor (inaudible; then not bit-exact). |
| B2 ✓ | **Reverb and MOVEMENT tail gate**: when the input has been silent and the tail has decayed below −120 dBFS, skip SPACE and the bus until sound returns | idle 1 % → ~0 %, and every silent instance in a big session | Differences only below −120 dBFS (formally not bit-exact; inaudible). |
| B3 | **Mono fast path**: a voice whose source is mono and nothing has widened it yet runs the shelves and the CHARACTER ladder once, not twice | up to ~15 % on mono sources | Bit-exact. Rarely helps: 76 of 79 of your recordings are true stereo. |
| B4 ✓ | Control-rate work only when inputs move: pitch ratio, shelf and ladder coefficients recomputed only when their inputs changed beyond a hair | ~2–4 % | Bit-exact when the skip threshold is "exactly equal". |
| B5 | Natural pitch character: build the register anchors only when Natural is selected (or on first use) | faster loads; saves four full-length copies of every sample in memory | A short moment of plain-Tape pitch when switching to Natural the first time. |

### C. Audio: inaudible numerical changes (not bit-exact)

These change the output by float rounding only (around −140 dBFS or below). Old sessions would
no longer null to the sample against earlier renders: goldens and the REIMAGINED reference
hashes need a one-time update, explained in the commit, as CLAUDE.md requires.

| # | Change | Saving | Quality |
|---|---|---|---|
| C1 | **SIMD sinc reads**: apply the 32-tap kernel with SSE/NEON (four or eight lanes), both channels in one pass sharing the weights; vectorise the kernel's polyphase interpolation | the largest block of work, 43 %, made about 2–3× faster: **~25 % of the total** | identical filter; only the summation order changes |
| C2 | **SIMD ladder**: left and right in one 2- or 4-lane pass (same equations) | ~8–10 % on stereo sources | same filter; rounding only |
| C3 | KALEIDOSCOPE saturation: a high-accuracy rational `tanh` (error < 1e-6) instead of libm's | ~6 % at high REIMAGINED | indistinguishable (well below the noise of the source) |
| C4 | KALEIDOSCOPE doubling head: modulation from a rotating phasor instead of `std::sin` per sample | ~3 % at high REIMAGINED | indistinguishable |
| C5 | KALEIDOSCOPE grains: read both channels in one pass and skip grains whose window is still ~0 | ~5–8 % at high REIMAGINED | identical grains, rounding only |

### D. Audio: structural (large savings in the worst cases, a small, controlled change)

| # | Change | Saving | Quality | Drawbacks |
|---|---|---|---|---|
| D1 | **Mip-mapped sources for upward transposition**: half- and quarter-rate copies of each sample made on the loader thread with a steep half-band filter; a note above the root reads the level whose rate is closest, so the kernel never stretches beyond ~1.4× | **+24 st: 56 % → about 13–15 %**; +12 st about halves | as clean or cleaner (the decimation filter can be steeper than the stretched kernel's); a tiny difference in the top octave's rolloff | about +100 % memory per source (1/2 + 1/4 + …); a regression and golden update for transposed renders |
| D2 | A voice's CHARACTER ladder bypassed only when it is provably transparent (fully open, no drive, no resonance, no envelope) | ~20 % in such patches | none in that state; any setting that colours sound keeps the filter | the default patch (CHARACTER 90 %) is not transparent, so this helps only "open" patches |
| D3 | Lower internal rate for very high host rates (run voices at 48 / 44.1 kHz when the host is at 88.2 / 96, then upsample) | 96 kHz: 19 % → ~11 % | for this instrument almost nothing audible, but it removes content above 22 kHz | adds latency or filtering at the output; purists may object. **Not recommended** unless 96 kHz sessions are common. |

### What not to do (it would cost quality)

- **Fewer sinc taps** (16 → 8 zero-crossings), cheaper interpolation, or a smaller polyphase
  table: audible aliasing and dulling at extreme transpositions.
- **Lower default polyphony or aggressive voice stealing**: it changes how releases and pedal
  tails sound.
- **Dropping the per-voice ladder for one shared filter**: CHARACTER's per-note envelope and
  velocity response would be lost.
- **Running control updates less often than every 32 samples**: zipper noise on fast filter
  envelopes and glides.

## 5. Recommended order

1. **A1 + A2 (editor).** Biggest real-world saving while playing, zero visual change, no audio
   risk.
2. **B1, B2, B4 (audio, bit-exact).** About 5–10 % of the audio work, idle near zero, tests stay
   identical.
3. **C1 (SIMD sinc).** About 25 % of all audio work. It needs the one-time golden and reference
   update.
4. **D1 (mip-maps).** It removes the 4.6× cost of high-register playing. A memory trade-off, and a
   transposed-render regression update.
5. **C2–C5.** For heavy REIMAGINED patches.
6. **B5** for loading time and memory.

Steps 1–4 together should bring the default patch to roughly 60–70 % of today's audio cost, the
worst high-register and three-layer cases to well under half, and the editor while playing to a
small fraction of today's. Each step is measurable with the harnesses above and can be checked
against the existing null tests.
