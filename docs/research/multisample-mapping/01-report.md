# Automatic multi-sample mapping — report (MS2–MS6)

_13 Nov 2026. Branch `claude/multisample-mapping`, from `0a418b9`. Not merged._

The audit and the measured reproduction are in [`00-audit.md`](00-audit.md). This report covers
the minimal fixes, the regression testing and the violin pizzicato demonstration.

## Already present vs new

| Behaviour | Before (`0a418b9`) | Now |
| --- | --- | --- |
| One recording on every key 0–127 | already present | unchanged |
| Nearest root, a tie to the lower root, edges extend to 0 and 127 | already present | unchanged |
| C3, C4 and C5 kept as three roots (no pitch-class folding) | already present | unchanged |
| Plucked one-shots rooted at their own pitch (acoustic detection) | already present | unchanged |
| Explicit pp / mf / ff (and other dynamics words) → velocity layers | already present | unchanged |
| A filled key plays its root's takes | already present (random) | now cycles them |
| Note Atlas FILL / max-transpose limits | already present | unchanged (file sets keep an unlimited fill, as before) |
| A folder dropped → one multi-sample sound | already present | unchanged |
| **Several loose files dropped** → one multi-sample sound | **new**: before, one layer per file (empty / ADD), or only the first file replaced the card (REPLACE) |
| **Drop on card A, B or C** → only that card becomes the set | **new** |
| **"Add layer…" with several files** → one multi-sample layer (as the drop) | **new**: before, one layer per file |
| **Deterministic order**: natural file-name order, whatever the drop order | **new** |
| **Real round robins**: a strict cycle in file order per (source, root, velocity layer), from the first take | **new**: before, a seeded random take that was never the previous one |
| **Each root's cycle independent** of other notes | **new**: before, any note moved the random pick |
| **Unlabeled louder / softer takes stay round robins** | **new**: before, a ≥ 4.5 dB gap made velocity layers, so one velocity played only one take |
| Sessions saved before keep their sound (random pick, loudness layers) | — | **new**: the policy is stored on the set; absent = the original rules |
| Filename pitch hints ("C4" in the name) | absent | **still absent**: not needed, the acoustic root was right on every fixture (the spec asks only for what is proven missing) |

## What changed (MS2–MS3)

1. **Drop / chooser**: `apps/plugin/Source/PluginEditor.cpp` (`filesDropped`, `chooseFile`).
   Loose files and the files of any dropped folders form one collection. One file keeps the
   old path (`addLayers`, one sound). Several files become one set: `addLayerSet` on an empty
   instrument or the ADD place, `replaceLayer` on a card (only that card changes). "Add layer…"
   with several files does the same. Its dialog title lost "(each becomes a layer)". No other
   visible change: no new control, no new modal, no layout change.
2. **Order**: `OspAudioProcessor::loadFiles` sorts files imported together by natural file name
   (`juce::String::compareNatural`, then the full path) before the `SetLoadRequest` is made.
   Recall keeps the saved member order.
3. **Policy** (`src/model/InstrumentSet.h`): `cycleTakes` (default false) and `loudnessLayers`
   (default true), carried by `SetInferenceOptions`, which `inferSampleSet`,
   `instrument::buildSet` and `SetLoadRequest::policy` all take. New imports use
   `SetInferenceOptions::automatic()`:
   - takes cycle, numbered in member (file) order;
   - only dynamics words make velocity layers.

   A Samples-inspector correction (`reassignInstrumentSet`) keeps the set's policy. Saved as
   optional `cycleTakes` / `loudnessLayers` properties on the session's `Set` tree, written only
   when they differ from the original rules (`docs/analysis-schema.md`). Old sessions save and
   load byte for byte as before.
4. **Engine** (`InstrumentEngine::memberFor`): with `cycleTakes`, the candidate whose take is the
   smallest above the previous take of that (source, group, layer), else the smallest one. It
   reuses the existing per-slot `take` state (no new state, no allocation, audio-thread safe).
   A newly installed cycling set starts at its first take. The original random path is
   untouched.
5. **Headless harness** (CLAUDE.md Rule 4):
   - `research-renderer --multisample <dir>`: every audio file in a folder as one instrument,
     mapped as the plugin maps it. It prints the mapping; `--print-members` prints the file
     each note plays; `--legacy-mapping` uses the original rules.
   - Render configs accept `"mode": "imprint"`, `"imprint"` and `"motion"` in a layer's
     `"synth"` block (absent = ANALOG, as before).

## Regression (MS4)

- **New tests**: `[multisample]`, 9 core cases and 5 plugin cases, all passing. Before the fix,
  4 of 8 core cases and 2 of 3 plugin cases failed (`00-audit.md`). They cover:
  - every key mapped, nearest root with ties to the lower, octaves apart;
  - the strict cycle from take 01, independent roots, filled keys, all eight of eight takes
    used;
  - named layers kept, unlabeled takes kept as takes;
  - pluck roots;
  - the original rules still in force for a set without a policy;
  - the drop on an empty instrument and on each of cards A, B and C (the other cards keep the
    same instrument object), and a single file;
  - natural name order whatever the drop order;
  - save → recall of the policy and order, an old session with no policy recalled with the
    original rules, and a Samples correction keeping the policy.
- **Full suites**: all green.
  - Core `ctest`: 5 of 5 (unit, integration, regression, performance smoke, bootstrap tool).
  - Plugin tree `ctest`: 10 of 10 (the OSP plugin tests, the shell's visual reference at
    0 pixels, memo lab, the example products and their visual tests, unit, integration,
    regression, performance smoke, bootstrap tool).
- **Bit-exact renders**: the 55-render engine-C matrix (5 sources × 4 fixtures × 2 block sizes,
  plus the unified baseline scenes) is byte-identical to the reference. The legacy pick, the
  inference without a policy and single-file playback are unchanged.
- **Granular, KEY DNA, SYNTH / IMPRINT**: a set's member is chosen before the voice starts.
  Every source mode plays the chosen member's own model. The demo renders Granular and
  SYNTH / IMPRINT from the cycling set. The existing Granular / KEY DNA / Imprint tests are part
  of the full suites.

## Demonstration (MS5): synthetic violin pizzicato

No recorded or private audio. [`make_demo.py`](make_demo.py) writes:

- 12 Karplus-Strong pizzicato one-shots with a small violin-like body, named like a sample
  library (`Violin_Pizz_G3_01.wav` …). The roots are G3 ×2, C4 ×3 (0, −5 and −9 dB: unlabeled
  softer performances), G4 ×3, D5 ×2 and A5 ×2.
- The MIDI phrases.

```
python3 docs/research/multisample-mapping/make_demo.py demo
research-renderer --multisample demo/violin-pizz --midi demo/midi/repeated-C4.mid --output 02.wav --print-members
```

The mapping as printed (automatic):

```
multisample violin-pizz: 12 files, 5 roots (takes cycle, named layers only)
  root 55 (G3) keys 0-57,   1 layer:  G3_01 T0, G3_02 T1
  root 60 (C4) keys 58-63,  1 layer:  C4_01 T0, C4_02 T1, C4_03 T2
  root 67 (G4) keys 64-70,  1 layer:  G4_01 T0, G4_02 T1, G4_03 T2
  root 74 (D5) keys 71-77,  1 layer:  D5_01 T0, D5_02 T1
  root 81 (A5) keys 78-127, 1 layer:  A5_01 T0, A5_02 T1
```

| Render | What it shows | Members played |
| --- | --- | --- |
| `01-chromatic-up-down` | G3 → A5 → G3 in eighths: each key from its nearest root | each root's takes in turn (e.g. A#3 B3 C4 C#4 → C4_01 02 03 01) |
| `02-repeated-C4` | nine C4s | C4_01 02 03 01 02 03 01 02 03 |
| `03-repeated-D4-filled` | nine D4s (filled from C4) | C4_01 02 03 … (the same cycle) |
| `04-chords` | four chords, G3 … G5 | each chord tone from its root, the roots' cycles independent |
| `05-granular-melody`, `05b-granular-chords` | Granular source mode on the set | as One Shot (the member is chosen first) |
| `06-synth-imprint-melody` | SYNTHESIZE 0.7, SYNTH page IMPRINT 0.85, MOTION 0.5 | as One Shot |
| `07-legacy-rules-repeated-C4` | the same set under the original rules | C4 split into 2 loudness layers; at velocity 100 **C4_01 nine times** (the defect) |

All renders are free of NaN and Inf and do not clip. The harness renders at engine level
(peaks around −25 to −35 dBFS, as single-source engine-C renders). The listening copies are
peak-normalised to −1 dBFS.

## Not done, on purpose

- No filename pitch parsing (not proven necessary).
- No GUI redesign, new control or modal. No change to the Note Atlas, to ANDOR ANALOG's loading
  or to the shell products. The shared loader only carries the policy. Its default is the
  original rules, so every other caller is unchanged.
- Not merged.
