# ANDOR Virtual Studio

**An internal cinematography toolkit, not a product feature.**

**What it does:** a recorded musical performance drives the approved Blender instrument, and the instrument's displays show the real plugin's frames of that same performance.

**Rule:** nothing is invented.
- Keys move only for the notes you played.
- Knobs turn only to the values the plugin had.
- Displays show only what the plugin drew.
- The film carries your audio.

```
 your take                     plugin capture (C++)                 Blender (Cycles)
 ─────────                     ────────────────────                 ────────────────
 MIDI, automation,  vs.py      performance.json ──► VS capture ──► captures/<fps>fps ──► vs_prep ──► vs_animate ──► renders/
 bounce, sources,  ingest ──►  (one sample clock)   (the build)    face, editor, knobs                (keys, knobs,   <shot>.mp4
 preset, build id                                                                                     displays)       + your bounce
```

## Components

| Path | What it is | Needs |
|---|---|---|
| `vs.py` | Projects: `init`, `ingest` (MIDI + automation + audio + preset + build), `check`, `preflight` (storage), `inventory` (SHA-256 of every file), `verify` (a copy against its inventory) | Python 3 standard library only |
| `vstudio/smf.py` | Standard MIDI File reader: tempo map, running status, SMPTE. Verified against `mido` on 400 random events across three tempo changes: identical times | – |
| `vstudio/timeline.py` | The timeline: seconds on the session's sample clock; frame `n` starts at sample `round(n·sr/fps)` | – |
| `vstudio/project.py` | Layout, manifest, PNG (chunk CRC + IEND) / WAV / JSON integrity, inventory, verify, preflight | – |
| `capture/VirtualStudioCapture.inc` | The plugin capture: the test harness `[vs-capture]`, included in the plugin's test translation unit | A plugin build with the capture hook |
| `capture/capture_hook.patch` | `design::draw::captureWithoutCaps`: the editor drawn without knob caps for the Blender panel texture (4 lines in `Design.cpp`). Applies cleanly to the default branch | OSP repository |
| `vs_prep.py` | Crops the captured faces to the master texture's aspect; refuses a mismatched layout | Pillow |
| `blender/vs_animate.py` | Drives the master: keys, knobs (macro, source, ADSR minis), the display sequence, cameras (`perf`, `keys`, `macros`, `display`, `ortho`, `display_push`, `keys_track`), the light sweep; resumable; render metadata | Blender 4.2 (bpy) |
| `vs_transitions.py` | The three software ↔ object transitions (match cut, into the display, light-assisted) | Pillow, ffmpeg |
| `examples/make_test_performance.py` | The technical test performance (synthetic, never flagship) | – |
| `docs/ableton_workflow.md` | Recording and export in Ableton Live: what is verified, what to check | – |
| `tests/test_vstudio.py` | MIDI timing, the retrigger rule, frame boundaries, integrity and verify | `python3 -m unittest discover -s tests` |

## Timing (B2)

**One continuous time base:**
- Every event is in seconds on the session's sample clock.
- **Capture:** frame `n` shows the state at `t = n / fps`, after samples `[0, round(n·sr/fps))`. Exact for 24 and 30 fps at 48 kHz; cumulative rounding otherwise, so it never drifts.
- **MIDI** goes into the plugin at its exact sample offsets. **Automation** is applied in 64-sample steps, as a host delivers it.
- **24 fps cinematic and 30 fps delivery** are two captures and two renders of the same performance. Nothing is interpolated.
- **In Blender**, each key's angle is evaluated analytically at every frame time from the note list, so a note is never reconstructed from frames.
- **Excerpts:** `--start` / `--end` at ingest select any length; the excerpt is re-based to 0.

## Keys (B3)

- **Physical keys follow your input notes only** (`"source": "input"`). Notes the arpeggiator generates are never in the input, so they never move a key. They appear where the plugin shows them: the displays and the editor's keyboard, which follows the incoming MIDI.
- **Travel:** the front of every key travels 6.7 % of the median white key's length (about 10 mm on a full-size key), pivoting at the shared back rail. Each key's angle follows its own length (black keys are shorter, so they tilt more for the same front travel).
- **Press:** from 40 ms (velocity 1) to 8 ms (velocity 127), with the finger accelerating the key.
- **Release:** a 70 ms spring return.
- **Retriggers:** a repeated note starts its press from wherever the key is.
- **Sustain pedal:** it holds the sound, not the keys, so keys rise at note-off.
- **Tested by the technical test performance:** soft and hard velocities, fast repeats (1/16 at 120 BPM), legato, sustained chords, bend and mod, and an arpeggio over a held chord.

## Controls (B4)

- **Matching:** every captured rotary is matched to the model's knob by position (6 macros, 15 source knobs, 4 ADSR minis).
- **Rotation:** each knob turns to `rest − (angle − base)`: the editor's recorded pointer angle relative to the master's capture angle.
- **Agreement:** the arc and the value text are in the same frame's face texture, so pointer, arc and value cannot disagree.
- **PITCH / MOD:** the strip's light layer samples the face texture, so the strips show the plugin's own lit pixels.
- **Measured: the editor's knob does not follow a macro performed by MIDI CC 20–27.** In the test performance, CC22 sweeps CHARACTER from 31 % to 87 % (9.1–9.9 s), and the knob stays at 88 %, its last automated value. The same knob tracks host automation exactly (50.4 % at 8.29 s on the 35 → 88 % ramp). The engine follows the CC; the host parameter, and so the knob and its arc, do not.
  - **Rule for filmed takes:** record macro moves as automation lanes, not CC 20–27, so the knobs move on screen.
- **Capture fix:** the capture recorded no rotaries headless, because `isShowing()` is false without a window. It now checks visibility up to the editor: 28 rotaries per frame (6 macros, 15 source knobs, 3 layer reimagined, 4 ADSR minis).

## Project layout and storage (B7)

See `vstudio/project.py`:
- **Inputs** (`performance/`, `audio/`, `sources/`, `preset/`, `scene/`) are the record.
- **`captures/` and `renders/`** are derived.
- **Storage:** projects live in production storage, not Git. `vs.py inventory` writes the SHA-256 and validity of every file, and `vs.py verify` proves a copy (or a restore on another machine) is complete.
- **Resumable:** captures and renders resume, and count a frame only when the PNG is complete.
- **Preflight:** `vs.py preflight` refuses a plan that won't fit on the disk.

## Rebuild on another machine

1. **The code:** this folder and `product-renders/blender` (scene, config, textures), plus `tools/blender-assets/config/macro-knob.json`, from Git.
2. **The plugin build:** check out the commit in `capture_meta.json`'s `build`, apply `capture/capture_hook.patch` if the branch lacks it, include `VirtualStudioCapture.inc` in the plugin tests, and build `osp_plugin_tests`.
3. **The project:** copy it from production storage, then run `python3 vs.py verify <project>`.
4. **Re-derive the captures:**

```bash
VS_PROJECT=<project> VS_FPS=24 osp_plugin_tests "[vs-capture]"
```
```bash
python3 vs_prep.py <project> --fps 24
```

5. **Render:**

```bash
blender -b --python blender/vs_animate.py -- --project <project> --fps 24 --camera perf
```
