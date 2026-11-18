# Performance-driven animation · common input format (v1, Phase 5 proof of concept)

**What it is:** one directory per performance. A real plugin session is captured into it, and `blender/scripts/performance_anim.py` turns it into frames of the approved Blender master.

**Rule:** nothing in the animation is invented.
- Keys move only for recorded notes.
- Knobs turn only to the angles the editor itself computed.
- Displays show only captured editor frames.
- The film carries the plugin's own audio.

```
<performance>/
  performance.json      the record below
  audio.wav             the plugin's output (sync reference: sample 0 = frame 1)
  face/NNNN.png         per frame: the editor without knob caps (2x)  -> the Blender panel face
  editor/NNNN.png       per frame: the editor as seen (1x)            -> editorial cuts / the E4 transition
  knobs/NNNN.json       per frame: every rotary's centre, pointer angle and parameter id
```

## `performance.json`

| Field | Meaning |
|---|---|
| `schemaVersion` | 1 |
| `id` | Episode / take id, e.g. `001-nyckelharpa-take3` (`poc-001-hero-phrase` for the proof of concept) |
| `plugin`, `build` | Product and exact build (commit), so a capture can be reproduced |
| `preset` | The preset or state the session used (file name or description) |
| `sampleRate`, `fps`, `frames`, `seconds` | Timing. Video frame `n` (1-based) covers `[(n−1)/fps, n/fps)`; one audio block per frame |
| `audio` | Audio file name (`audio.wav`) |
| `editor` | Editor size in px when captured. It must match the Blender master's capture layout (1448 × the hero height) for the face texture to register |
| `notes[]` | `{note, on, off, velocity}`; seconds, MIDI note number, 1–127 |
| `automation{param: [[t, value], …]}` | Parameter envelopes as played (values in the parameter's own units, per frame) |
| `pitchBend[[t, −1…1]]`, `modWheel[[t, 0…1]]` | Wheels / touch strips as played |
| `captures{face, faceScale, editor, knobs}` | Patterns of the per-frame captures |

## Capturing a performance

**The proof of concept** is local scaffolding, deliberately not committed: `tests/plugin/LocalPerformanceCapture.inc` (`[perf-capture]`, `OSP_PERFORMANCE_DIR`). It loads a state, plays a note list and envelopes through `processBlock`, frame by frame, and writes all of the above.

**For a real episode** (playbook §5):
1. Record the performance in the DAW: MIDI plus automation of the macros and wheels.
2. Export the MIDI file and the automation lanes.
3. Replay them headless through the same build and preset with the capture test. A small importer from Standard MIDI File and automation to this JSON is the only new piece needed.
4. Use the DAW's own bounce of the same take as the audio of record. The headless render is sample-identical for deterministic presets, which is OSP Rule 2.

**Display captures** are the plugin's own drawing at that frame: waveforms, read heads, grain activity, arcs, key colours, the strips' lit state. They are never animated in Blender.

## Driving the master

```bash
python3 product-renders/blender/scripts/performance_prep.py <performance> <out>
<bpy python> product-renders/blender/scripts/performance_anim.py -- --performance <performance> --out <out> --camera perf
```

| Prototype | What drives it |
|---|---|
| **E1, keyboard** | The master's measured key solids are split into 88 keys (A0–C8). Each pivots at its back edge and travels about 3.5° at a note-on: one frame, or 1.6 frames for soft velocities. It returns over 2.5 frames at the note-off |
| **E2, macro** | Each macro knob turns to the editor's recorded pointer angle for that frame, relative to the master's capture angle. The value arc is in the face texture of the same frame, so knob and arc can't disagree |
| **E3, living display** | The panel's face texture is the captured sequence, with the same crop as the master's `osp_face.png` |
| **E4, transition** | The `--camera ortho` frontal orthographic view registers with `editor/NNNN.png` for a cut or dissolve between software and object |

**Output:** `<out>/frames/NNNN.png` and `performance_<camera>.mp4`, muxed with `audio.wav`.

## Limits of the proof of concept

- **Sound:** the sources are the master's own test signals (synthetic), used for the look only. Flagship films use the owner's recordings and performances.
- **Keys:** the travel is a physical estimate, not measured from a real keybed. Black keys share the white keys' pivot rule.
- **Layout:** the face texture must come from a capture of the same editor layout as the master (same build line and window). A different layout needs a new master capture first (`prep_textures.py`).
