# P1 — Polyphonic note transcription

**Status:** P1 gate passed on the controlled piano material. Simultaneous notes are found and
exported as a sensible event list. Results come from the synthetic corpus (P0) only; real
recordings are still to come.

## What was implemented

- **Shared representation** (`transcription/events.py`, schema `osp-poly.notes/1`): `id`,
  `pitch_midi`, `pitch_hz`, `onset_seconds`, `offset_seconds`, `velocity_estimate`, `confidence`,
  `source_id`, `detector`, `status`, `extra`. All three methods write it.
- **Method 1, ground truth** (`ground_truth.py`): the fixture's notes. These let extraction (P2)
  be judged without transcription errors.
- **Method 2, harmonic detector** (`heuristic.py`), deterministic, with no learned model.
  - **Per frame (8192-point window, 512 hop):**
    - the spectrum on a 10-cent log grid;
    - candidates every 20 cents, each scoring 24 partials at inharmonic places
      (`k f0 sqrt(1 + B k²)`, best of four stiffness values B) with Klapuri's weights, an
      odd-partial support term (penalises sub-octaves) and a fundamental-presence term;
    - iterative pick-and-cancel, up to 6 notes:
      - cancellation happens at each partial's actual peak, over the window's main lobe (at
        65 Hz one bin is about 150 cents wide);
      - it removes at least 60 % of each partial (a piano's partials are not smooth: the hammer's
        comb);
      - it skips a pitch already picked, and any candidate whose partials all belong to a picked
        note unless it has half that note's strength.
  - **Across frames:**
    - median smoothing and per-pitch hysteresis segmentation;
    - same-pitch gaps without a new attack are bridged (beating is not two notes);
    - onsets are snapped to a 1024-point onset function (adaptive peak picking);
    - repeated keys are split at fundamental/octave rises that coincide with an attack;
    - offsets are placed at the damper (where the level starts to fall faster than −90 dB/s);
    - **harmonic-ghost removal:** a note an octave, twelfth or double octave above a sounding
      note is dropped if it starts without its own attack (a beating fundamental briefly lets its
      partials win) or is much weaker (< 45 %).
- **Method 3, Basic Pitch** (`basic_pitch_adapter.py`): Spotify's model, version 0.4.0, ONNX
  backend, model `nmp.onnx` sha256 `2c3c1d14…a0ec`. It runs in its own environment through a
  runner script and returns the shared events.
- **Evaluation** (`evaluation/transcription.py`, mir_eval), with explicit tolerances:
  - note level: onset 50 ms, pitch 50 cents; with offsets, the offset within max(50 ms, 20 %) of
    the key release;
  - frame level (10 ms): polyphony precision, recall and accuracy;
  - octave errors, missed and false notes, onset and offset error.
- **CLI:**
  - `osp-poly detect <wav> --method harmonic|basic-pitch|ground-truth [--manifest] --out` writes
    `notes.json`, `run.json` and `detected-notes.png`;
  - `osp-poly benchmark transcription [--sweep]` writes `report.md`, `metrics.json`, timelines and
    the per-fixture events.

## Results (24 fixtures; `benchmark transcription --sweep`)

| Detector | Class | F1 (onset) | F1 + offsets | Median onset error | Octave errors | Missed / false | Time ÷ audio |
|---|---|---|---|---|---|---|---|
| harmonic (8192 / 512) | A single notes (7) | 1.000 | 0.857 | 4.0 ms | 0 | 0 / 0 | 0.25 |
| | **B chords (6)** | **0.944** | 0.833 | **5.3 ms** | **0** | 1 / 2 | 0.39 |
| | C overlap (5) | 0.975 | 0.767 | 4.3 ms | 0 | 0 / 2 | 0.27 |
| | D phrase, synthetic (1) | 0.842 | 0.526 | 4.8 ms | 0 | 1 / 5 | 0.33 |
| | E pedal (1) | 0.727 | 0.0 | 2.7 ms | 0 | 0 / 3 | 0.42 |
| | F other (4) | 0.592 | 0.500 | 4.0 ms | 2 | 6 / 20 | 0.40 |
| | all (24) | 0.895 | 0.723 | 4.0 ms | 2 | 8 / 32 | 0.33 |
| Basic Pitch 0.4.0 | B chords | 0.869 | 0.649 | 4.5 ms | 0 | 0 / 8 | 1.47 |
| | F other | 0.580 | 0.486 | 4.5 ms | 0 | 3 / 17 | 1.29 |
| | all | 0.755 | 0.491 | 7.1 ms | 0 | 3 / 67 | 1.38 |

**Per fixture.**
- Every single note, five of the six chords, every overlap case except the melody over a chord
  (0.88), the organ and the strings are perfect for the harmonic detector.
- Wide voicing C2-G3-E4-C6: 0.67. G3 and E4 sit on C2's 3rd and 5th partials, and C6 on its 16th;
  this was listed as an expected failure.
- Guitar (Karplus-Strong): 0.20.
- Voices: 0.17. With formants the fundamental is weak, so the detector picks octaves. Basic Pitch
  gets 0.86 here.

**FFT / hop sweep.** Chords mean F1:

| FFT | hop 256 | hop 512 | hop 1024 |
|---|---|---|---|
| 4096 | 0.84 | 0.88 | 0.83 |
| 8192 | 0.94 | **0.94 (baseline)** | 0.98 |

- 8192 resolves close chord tones better than 4096. The long window's smeared attacks are
  recovered by the onset snap (onset errors stay around 4–5 ms at every setting).
- 1024 hop is slightly better on chords but worse on the phrase's offsets, so 512 stays the
  baseline.

**Stability:** identical events across repeated runs on every fixture.

### Against the P1 targets (controlled chords)

| Target | Result |
|---|---|
| Note F1 ≥ 0.85 | 0.944 (harmonic), 0.869 (Basic Pitch) |
| Median onset error ≤ 50 ms | 5.3 ms |
| No systematic octave doubling | 0 octave errors on piano (harmonic) |
| Stable across repeated analysis | yes |

## Images

- `images/p1-harmonic-C-major.png`
- `images/p1-harmonic-phrase.png`
- `images/p1-harmonic-voices.png` (fails)
- `images/p1-basic-pitch-voices.png` (succeeds)

Green is ground truth to the key release; red is detected.

## Limitations and recommendations

- **All synthetic.** The piano model has exactly the structure the detector looks for (stiff
  harmonic partials). A real piano adds a soundboard, room, longitudinal modes and
  noise. These numbers are an upper bound until they are checked on real recordings.
- **Offsets** are the weakest part (F1 + offsets 0.72 overall). With the pedal there are no key
  releases to find, so offset F1 is 0.
  - For extraction this matters less: P2 extends each note to its audible end anyway.
- **Voices, guitar.** Weak fundamentals and fast decays need either Basic Pitch or a
  family-specific configuration. That is P5 work (spec P5.2).
- **Recommendation:** use the harmonic detector for piano and Basic Pitch for voices.
  - Keep ground truth for judging extraction (P2).
  - Both detectors write the same events, so P2 can be fed by either.

## Tests

`pytest`: 19 passed (P0 and P1).
- Conversions and names; event round trip.
- Metrics: perfect for the truth; missed, false, octave error and onset error counted.
- Harmonic detector perfect (F1 1.0, onset error ≤ 20 ms, no octave errors) on C major, A minor,
  Dm7, C+E at 150 ms, single G4 and single C2.
- Deterministic; silence gives no notes.
- Ghost removal drops a late, attack-less octave and keeps a struck one.
- The Basic Pitch adapter returns shared events with the model's hash (skipped where it is not
  installed).

## Reproduce

```bash
cd research/polyphonic-lab
.venv/bin/osp-poly corpus generate
.venv/bin/osp-poly benchmark transcription --sweep --out output/benchmark-transcription
.venv/bin/osp-poly detect output/corpus/B-chord-C-major/mix.wav --manifest fixtures/manifests/B-chord-C-major.json --out output/detect-c-major
```
