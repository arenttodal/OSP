# ANDOR Drums M8: integration, real-time safety and measurements

Milestone 8 and spec Part 11 (performance and real-time safety), and the state and
real-time test lists of Part 13.

## Real-time safety: proven, and one real fix

`andor_drums_plugin_tests "[realtime]"` replaces the global `operator new` in the test
binary. It counts every allocation made on the calling thread while `processBlock` runs;
loader threads allocate freely at the same time. The test drives one kit for 600 blocks of
dense playing:
- six notes per block on all nine pads, with note-offs and the sustain pedal;
- a pad with four round-robin layers;
- every effect on.

Meanwhile, on the message thread:
- PAD ↔ EXPANDED switches, a layer edit (a new bundle), pad and layer auditions;
- a sample loaded while playing, and a break imported and analysed while playing, with its
  slice preview;
- a layer mode change, a clear, an undo, a duplicate.

It then repeats at 44.1 and 96 kHz with 16-, 64- and 1024-sample blocks.

**It found a real bug.** `pullSettings` built every per-pad parameter ID as a `juce::String`
and looked each one up, on every block: about 405 heap allocations per callback (CLAUDE.md
rule 1). The parameters' atomic values are now looked up once in the constructor. The test
now requires and gets **0 allocations** in all cases.

Also tested:

- **Two instances** in one process. Each keeps its own kit, names and sounds. Instance A
  renders bit-identically whether or not a busy instance B is processing in between. They
  share the sample store by content hash.
- **Host automation.** Every parameter (1 + 8 + 9 × 11 = 108) is automatable and named.
  Automating a pad's gain during a held note ramps it out over 10 ms, with no step.
- **The rest of the Part 13 list,** covered by earlier milestones:
  - state recall, bit-identical;
  - missing samples, kit files, portable kits;
  - the OSP session ignored, independent plugin identities (M0–M2);
  - output independent of the host's block size (M1).

## Measurements (spec 11.5): `andor_drums_plugin_tests "[perf]"`

48 kHz, 256-sample blocks (5.33 ms). A shared Linux container CPU, so these numbers give the
shape of the load, not a Mac's. Eight seconds per scenario. The voice counts are the most
sounding at once.

| Scenario | Mean | Worst | Mean of the block | Voices |
|---|---|---|---|---|
| Idle, 9 pads loaded | 15 µs | 168 µs | 0.3 % | 0 |
| 8 pads, 16ths at 120 (8 hits/s) | 115 µs | 1.0 ms | 2.2 % | 5 |
| 9 pads, 32nds (16 hits/s) | 189 µs | 0.6 ms | 3.5 % | 9 |
| 9 pads, 32nds, global effects | 354 µs | 0.7 ms | 6.6 % | 9 |
| Round-robin pad (4 layers), 32 hits/s | 425 µs | 3.5 ms | 8.0 % | 14 |
| High-polyphony retrigger, 100 hits/s | 1.23 ms | 5.1 ms | 23 % | 43 |
| Loading samples while playing | 431 µs | 2.4 ms | 8.1 % | 12 |
| Break analysis while playing | 495 µs | 6.7 ms | 9.3 % | 12 |
| Two instances (both timed) | 439 µs | 2.1 ms | 8.2 % | 12 |

- **Worst cases.** The worst cases while loading or analysing come from the loader thread
  taking the container's few cores from the audio thread. On a real machine they run on
  other cores. The worst-case numbers still need a quiet Mac.
- **The cost of one sound.** It is the OSP voice, about 25–30 µs per block, so the cost
  grows with the voices sounding, not with the number of pads.
- **The shared effects** add about 165 µs. They run once for the kit, never nine times
  (spec 11.3).

**Memory.** The measured bottleneck was voice memory. Each pad's OSP engine preallocated 208
voice slots (OSP's three layers × 64 voices), about 4 MB each and 36 MB per kit, before any
sound. `InstrumentEngine` now has an inert constructor that takes a slot count; OSP keeps
its default. Drums gives each pad 48 slots: its polyphony of 12 (capped at 24), plus room
for stacked layers and the fade-outs of fast retriggers.

- Two instances with a kit loaded: **275 MB → 222 MB**.
- The idle kit's callback: **60 → 15 µs**, with fewer slots to scan.
- The rest of the memory is the loaded sounds: OSP's models, with their analysis and
  playback buffers.

## Undo, MIDI Learn, portable kits, compatibility

Done in earlier milestones and tested there:

- **Undo:** every map, pad, layer, kit, break and import edit.
- **MIDI Learn** (V1.1).
- **Portable kits and relinking** (M2).
- **Compatibility:**
  - sessions with states 1–3 open;
  - kits with their own schema;
  - OSP files are refused;
  - newer kits are refused rather than misread.

## Installer and build validation

CI (`.github/workflows/ci.yml`, macOS 14) builds everything and runs every suite. It
validates the AU with `auval -v aumu Adrm Ospx` and packages `ANDOR-Drums-macOS-test`, with
an `install.command`. Every milestone on this branch passed it.

## Still for a real Mac (needs the user)

- **Worst-case callback timing** at 32/64-sample buffers on Apple Silicon, with the DAW's
  own load.
- **Hosts:** Logic, Ableton Live, REAPER. That covers automation lanes, state in projects,
  multiple instances and offline bounce.
