# Gate D2 · Motion library drafts D01–D08 and the performance-driven PoC (Phase 5)

**Basis:** the approved C3 master. Geometry, materials, light and compositions are unchanged.

**What moves:**
- Only the camera moves (smoothstep: every move starts and ends at rest).
- The product never moves, so every shadow is the scene's own.
- **No music, typography or transitions** are baked into any shot.
- Displays show only the master's still face texture, or real captured editor frames (D08 and the PoC).

**Status: drafts for review.** Final production at full resolution starts only after approval.

## 1. Settings shared by every draft

| | |
|---|---|
| Frame rate | **24 fps for every shot** (the final plan's rate, so drafts and finals cut together) |
| Size | 960 px wide in the shot's **own** aspect: 16:9, 4:5, 9:16, 19.5:9 or 3:2. Separate compositions, never automatic crops |
| Render | Cycles, 6 samples, adaptive threshold 0.05, OpenImageDenoise (draft quality, to fit about 1,700 frames on this 4-core machine; noise at final settings is judged by the quality test in section 3) |
| Seed | **Fixed (0, not animated):** the residual noise does not crawl from frame to frame |
| Colour | The master's view transform and exposure per setup, unchanged |
| Handles | 0.5 s (12 frames) of rest at both ends: copies of the first and last frames, which are at rest |
| Poster | The closing frame of the move, so poster, hold frame and reduced-motion still are the same image |

**Per shot** (`renders/motion_gate_d2/<shot>/`):
- `master/NNNN.png`: the editing master, a lossless PNG sequence;
- `<shot>_draft.mp4`: the web draft (H.264 CRF 20, faststart, no audio track);
- `poster.png`;
- `manifest.json`: setup, shot, camera move, frames, handles, fps, samples, seed, and `null` for music, typography and transitions.

**Editing masters and web derivatives are kept apart.** The masters (PNG) go to the archive in the Package D zip. Only the MP4 drafts and posters sit next to the review sheets.

## 2. The shots

| Shot | Base | Length | Move (start → end) | Notes |
|---|---|---|---|---|
| D01 approved | A1-R1 | 6 s | yaw −9° → +3°, tilt 4° → 0°, dolly 1.10 → 1.00 | The Gate D move |
| D01 subtle | A1-R1 | 6 s | yaw −4.5° → +1.5° (half), tilt 2° → 0°, dolly 1.05 → 1.00 | Stiller |
| D01 larger | A1-R1 | 6 s | the subtle move, dolly 0.97 → 0.92 | The instrument about 8 % larger in frame |
| D01 W | A1-R1 W | 6 s | yaw −6° → **0°**, tilt 3° → **0°**, dolly 1.08 → **1.00** | Ends exactly on the W still: hold and poster register |
| D01 mobile | A1M-R1 (19.5:9) | 6 s | yaw −6° → 0°, tilt 3° → 0°, dolly 1.06 → 1.00 | Native vertical composition; ends on the A1M-R1 still |
| D02 orbit 4:5 | B1-R4 | 6 s | yaw −7° → +5°, tilt −2° → +1°, dolly 1.04 → 1.00 | Floating shadow rendered every frame |
| D02 orbit 9:16 | B2_115_R4 | 6 s | the same | Native vertical; copy space above as in B2 |
| D03 macro glide | M01 cinematic | 5 s | yaw +5° → −5°, dolly 1.00 → 0.96 | The highlight travels over the flutes |
| D04 inside the sound | M03 cinematic | 5 s | yaw −4° → +4°, tilt 2° → 0° | The display's still texture. A living waveform comes from the PoC (section 4) or a real screen recording |
| D05 keybed | M04 cinematic | 5 s | yaw +6° → −2°, dolly 1.00 → 0.97 | Keys stationary. Key motion only from captured MIDI (section 4, E1) |
| D06 material | M05 cinematic | 5 s | yaw −6° → +4°, dolly 1.04 → 0.98 | Timber, ceramic, junction |
| D06b source controls | M02 cinematic | 5 s | yaw −5° → +3°, tilt 1° → 0°, dolly 1.03 → 1.00 | Added: the small source controls |
| D07 expression | M06 R2 cinematic | 5 s | yaw −5° → +3°, dolly 1.03 → 1.00 | The corrected strips: PITCH orange, MOD charcoal-blue, both full height |
| D08 interface | A2 light, frontal | 4 s | yaw −10° → 0°, tilt 22° → 0°, distance 1.12 → 1.00, 135 mm | Ends framed exactly as the E4 orthographic camera (the panel's 1448 px across the frame); the face plays the captured editor frames |

Results and observations: section 5, after the renders.

## 3. Quality checks at final settings

A short test (1 s) at the final settings (1920 px, 96 samples, fixed seed) of the hardest shot, D03
(fine flutes, shallow focus).
- **Temporal noise / flicker:** the frame-to-frame mean difference in static areas.
- **Text stability:** the panel labels' edges over the move.
- **Aliasing:** flute edges and key gaps.
- **Focus:** the focus plane over the move.

Results in section 5.

## 4. Performance-driven animation, the proof of concept (E1–E4)

The format and scripts are in `performance_format.md`.

**The performance:**
- **Length:** 8 s at 30 fps, rendered through the real plugin (hero 3-layer state).
- **Notes:** a held chord, a melody, then a closing chord.
- **Moving controls:** FILTER on a recorded envelope (74 → 40 → 86 → 74 %), a pitch bend on the high note, and a mod-wheel swell.
- **Audio:** the plugin's own 24-bit output.

| | Driven by | Check |
|---|---|---|
| E1 keys | `notes[]`: 88 keys split from the master's key solids, pivoted at the back; travel 3.5° at the front | Keys move only for recorded notes; the face's held-key colour is the editor's own |
| E2 macro | Each macro knob's recorded pointer angle per frame, relative to the master's own capture angle | The knob and its arc (in the same frame's face texture) cannot disagree |
| E3 living display | The captured editor frames as the panel face (same crop as the master: registration measured at 0 px offset top, middle and bottom) | Read heads, waveforms and key colours are the plugin's own drawing |
| E4 transition | The frontal orthographic camera, plus D08's perspective glide that lands on it | Registers with `editor/NNNN.png` for a cut or dissolve |

**Bugs found and fixed while building it:**

**Macro pointers:**
- *Symptom:* the first frames showed every macro pointer at 12 o'clock.
- *Cause:* the script had replaced the knob's own rest rotation instead of adding to it.
- *Fix:* `rest − (angle − base)`, verified on a test frame against the editor capture.

**The frontal camera (E4, D08):**
- *Symptom:* the first version looked straight down the world's vertical and came out rolled about 15° and off-centre.
- *Cause:* the instrument lies turned by the shot's `ROT`.
- *Fix:* both cameras are now built in the panel's own frame:
  - centred on the master's editor centre (724, 491);
  - rolled with the panel;
  - the panel's 1448 px across the frame;
  - the frame in the master's 1448 : 982 aspect.
- *Result:* the ortho frame registers with the editor capture at **0 px offset**. Every landmark above the keyboard lines up: header, cards, displays, knobs, macros, envelope, strips.
- *The keyboard:* it is the physical model's keybed, narrower than the editor's on-screen keyboard (the approved model), so a software ↔ object match cut should land on the panel.

## 5. Results

### The performance-driven PoC (playable)

| File | What it is |
|---|---|
| `performance_perf.mp4` | **E1–E3:** 960 × 540, 30 fps, 8 s, 16 samples, the plugin's own audio. The three-quarter performance camera |
| `performance_E4_transition.mp4` | **E4 + D08:** 960 × 652 (the editor's aspect), 8 s, the same audio. See the sequence below |
| `p5_poc_perf_sheet.jpg`, `p5_poc_e4_sheet.jpg` | Stills at the key moments |

**The E4 + D08 sequence:**

| Frames | What plays |
|---|---|
| 1–96 | D08's glide from near-frontal into the frontal view |
| 97–108 | A dissolve into the real editor capture |
| 109–200 | The live software |
| 201–212 | A dissolve back |
| 213–240 | The registered orthographic object |

**Checked:**
- **Keys:** they move only at the recorded notes; the chord, the melody and the closing chord are visible on the keybed. The held-key colour is the editor's own, from the face texture.
- **FILTER:** its pointer follows the recorded envelope: low at 1 s, highest around 5.5 s, back to 74 % at the end. The other five macros stay at their recorded values.
- **Displays:** the read heads and grain activity move with the audio. The MOD strip lights under the closing swell, and the pitch bend shows on the PITCH strip.
- **The software ↔ object dissolve:** it holds every landmark above the keyboard in place (registration 0 px). The keybed is the physical model's, so it changes in the dissolve, which is expected.

**Limits:**
- **The sound:** the test-signal sources (synthetic), for the look only.
- **The key travel:** an estimate, not measured from a real keybed.
- **The perf camera:** it crops the PITCH strip at the left edge. A final camera would frame it in.

**For creative approval before anything is expanded:**
- the camera language for performance shots;
- whether key travel and knob motion read as intended;
- the dissolve as the software ↔ object device.

### D01–D07 drafts

(Added when the batch finishes.)
