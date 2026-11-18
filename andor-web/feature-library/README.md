# OSP website feature library (authentic editor imagery, Phase 4)

**Source:** every image and recording is the real ANDOR OSP editor, rendered headless by the plugin's own code.
- **Build:** the OSP default branch `56bab47` plus the approved D2 macro knobs (PR #26 head `3354690`).
- **Sounds:** the built-in tutorial examples (bowed note, voice, pluck).
- **Window:** 1448 px wide, captured at 2× (2896 px).
- **Clean:** no Learn notes, rings or simulated behaviour appear in any frame. The audio ran through the instrument for each state, so meters, read heads and held keys are real.

**Not shown:** the `layer-ivory` source knobs, which are in review and not yet shipped. The source knobs here are the current shipping vector knobs.

| Feature | Screen (`screens/`) | Detail (`details/`) | Verified against |
|---|---|---|---|
| Load a single sound | `01-empty-drop-a-sound`, `02-one-sound-loaded` | — | Empty instrument's DROP A SOUND; one layer loaded |
| Play it as an instrument | `03-one-sound-played-as-instrument` (chord held) | `card-one-sound` | — |
| Three-layer sound design | `04-three-layer-sound-design` (A bowed, B voice, C pluck) | `card-layer-c-pluck` | Three layers loaded |
| One-shot and granular | `05b-one-shot-mode`, `05c-granular-mode` | `granular-display` | `layerA.sourceMode` (One Shot / Granular) |
| REIMAGINED | `05-reimagined` (60 %, modes popover) | `reimagined-modes-popover` | Modes: Kaleidoscope, Tape Frame, Toybox, Mosaic, Mirage |
| SYNTH and IMPRINT | `06-synth-and-imprint` (layer C: SYNTH 45 %, mode Imprint, imprint 80 %) | `card-synth-imprint` | `synth.mode` = Analog / Imprint (shipping: SYNTHESIZE S1, S2) |
| MOVEMENT | `07-movement` (its rack) | `movement-rack` | — |
| Per-key variation, KEY DNA | `08-key-dna` (its editor, on) | `key-dna-editor` | KEY DNA in the Granulator (Unified Engine Phase 2) |
| Arpeggiator | `09-arpeggiator` (on, panel open) | `arpeggiator-panel` | — |
| ECHO, SPACE | `10-echo`, `11-space` (their racks) | `echo-rack`, `space-rack` | — |
| Modulation | `12-modulation` (LFO 1 → FILTER at +50 %) | `modulation-routes`, `modulation-panel` | — |
| FX routing | `13-fx-routing` | `fx-routing` | FX ROUTING view (shared with ANALOG) |

## Recordings (`recordings/`, 1448 px, 24 fps, about 4 s, H.264, muted)

| File | What happens |
|---|---|
| `osp_reimagined-sweep.mp4` | REIMAGINED automated from 0 to 100 % on a held chord (bowed note). The knob and its spectral arc move, and the display responds |
| `osp_movement-turn.mp4` | MOVEMENT turned from 0 to 80 % while a chord plays over the three layers |
| `osp_arpeggiator.mp4` | The arpeggiator switched on over a held chord (pluck). The read heads in the display follow the steps |

These are frame-accurate renders of the plugin's own drawing; audio was processed but not recorded.

**Before launch**, record a screen capture of a real session with sound (the standalone on a Retina Mac) for any clip that needs audio.

## Caveats (do not advertise)

- **Not shown:** the development switches (SYNTH's right-click blend-law audition, the knob-skin toggle) and research tools (polyphonic lab, Memo Lab) are not shown and must not be presented as features.
- **KEY DNA:** shown as enabled with its default (flat) curve. A drawn variation would make a better hero detail and can be captured once a factory KEY DNA preset exists.

**Capture code:** local, not committed (`tests/plugin/LocalFeatureShots.inc`, tag `[feature-shots]`, `OSP_FEATURE_SHOTS=<dir>`). It reuses the Learn test rig and the tutorial examples.
