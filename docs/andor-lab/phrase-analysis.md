# ANDOR Phrase — analysis pipeline and evaluation

Code: `src/andor/phrase/PhraseAnalysis.{h,cpp}` (pure C++20, headless), fixtures and metrics in
`src/andor/phrase/PhraseFixtures.{h,cpp}`, CLI in `apps/research-renderer/AndorCommands.cpp`.
Analysis never runs on the audio thread and is deterministic (no randomness at all).

## Stages

| # | Stage | What it does |
| --- | --- | --- |
| 1 | Preprocess | mono analysis copy (the stereo source is untouched), 50 Hz high-pass (analysis only), RMS level per 5 ms hop, peak; silence threshold = max(peak − 45 dB, −72 dBFS, min(noise floor + 6 dB, peak − 30 dB)) where the noise floor is the quietest tenth of the audible frames |
| 2 | Pitch contour | a pluggable `PitchTracker` gives frequency, MIDI pitch, confidence and voicing per 5 ms frame (40–2200 Hz by default) |
| 3 | Segment | voiced runs (≤ 30 ms dropouts above the silence threshold are bridged); pitch changes (light smoothing, a held excursion of ≥ 0.6 st for ≥ 40 ms confirmed by the median of the next 150 ms; **vibrato veto**: where the pitch keeps moving and reversing, the one-cycle moving mean must step too); **re-articulation** at level dips ≥ 6 dB between two rises (3 dB with a voicing dropout), with a **tremolo veto** (≥ 4 evenly spaced dips at 3–10 Hz do not split); short pieces glued to a neighbour are merged; short glides between legato notes are absorbed, unless they have a pitch plateau (a real short note) |
| 4 | Describe | confidence × level-weighted median of the voiced body (glides at both ends left out) = the note's identity; robust spread (weighted MAD); vibrato rate and depth from the residual around a 200 ms trend; voicing; confidence = voicing × mean periodicity × stability factor (vibrato is not counted as instability) |
| 5 | Refine | attack start: walk back from the voiced onset while the level is above max(silence, note peak − 35 dB, noise floor + 3 dB), stop in the valley before the onset (breath, a previous tail), at most 120 ms; one hop of pre-roll. Sustain: from the attack peak once the pitch settles within 35 cents, to the last settled frame within 12 dB of the body level. Release: to the next note (legato) or until the tail falls into silence / the noise floor (≤ 400 ms), stopping when the level rises again. Fades: 32 samples from silence, 2 ms otherwise; 2–10 ms fade-out |
| 6 | Map | `atlas::roundPitch` (explicit tuning rule, optional auto tuning = weighted median of the notes' cents), cents kept apart, variation groups by key in time order |
| 7 | Atlas | `NoteAtlas` with source fingerprint, settings digest, method and version per note |

`analyseRange` re-describes a user-chosen range as one note (split halves, merges, "re-analyse
note"). The plug-in caches results by content hash + analysis version + settings digest.

## Pitch backends

| Backend | How |
| --- | --- |
| `yin` | YIN (de Cheveigné & Kawahara 2002, absolute threshold 0.15) per frame, octave slips folded onto the local median |
| `yin-viterbi` (**default**) | pYIN-inspired (Mauch & Dixon 2014, simplified): every YIN dip that is the first dip below one of 30 thresholds (0.02–0.6) collects that threshold's weight, so each frame has up to 6 pitch candidates with probabilities plus an unvoiced state; a Viterbi pass picks the cheapest path (0.6 per semitone beyond 0.5 st, 2.0 to switch voicing). Octave slips lasting a few frames are not worth taking; real note changes (one jump each) are |

Both reuse OSP's FFT-based `YinPitchTracker` (one read-only accessor was added for the CMND
curve; its own output is unchanged and the OSP goldens pass).

### Libraries considered (not integrated)

| Candidate | Verdict |
| --- | --- |
| **WORLD** (Harvest / DIO; modified-BSD licence, plain C++) | Licence-compatible and small, but adds a dependency whose strength is speech F0 at low sample rates; our needs (note-level pitch, robust voicing, octave stability) are met by the Viterbi tracker on every generated case, and we must not pull a vocoder path in only for F0. Worth a bake-off on the user's real vocal recordings before deciding (`--phrase-eval-file` makes that a one-command comparison once a backend is added). |
| **Spotify Basic Pitch** (Apache-2.0 code and model) | Would need a neural runtime (TensorFlow Lite / ONNX / CoreML) inside the plug-in: excluded by project rule 5 (no speculative ML) for this phase, and it is a polyphonic transcriber where Phrase needs monophonic precision. Revisit only if real-recording results demand it; it must then run locally (no cloud). |

The prototype is fully functional with its built-in offline backend; no network, no model files.

## Evaluation

Note-level matching like `mir_eval`: an onset within 50 ms of a truth note and a pitch within
50 cents is a match; matched onsets with the pitch an octave off are octave errors; extra
detections starting inside a truth note are false splits. Offsets are measured to `sourceEnd`
(which includes the recorded release tail by design: the generated notes have a 60 ms release
after their nominal end, so ~50–65 ms offset error is expected).

```sh
research-renderer --phrase-eval [--backend yin|yin-viterbi|all] [--stress] [--verbose]
research-renderer --phrase-eval-file take.wav --truth take.truth.json      # your own annotated recording
research-renderer --phrase-fixtures research/phrase-fixtures                 # WAV + truth JSON of every fixture
```

### Standard fixtures (13 phrases, 79 notes)

Clean scale, 15 dB noise, vibrato (60 and 100 cents), legato with glides, a 6 s sustain,
staccato (90 ms), silent gaps, repeated notes (separated and legato with 12 dB dips),
out-of-tune notes (+30/−40/+45/−20/+10 c), octave ambiguity (fundamental 18 dB down), breath
between notes, −24 dB quiet notes, and a sung-style phrase (scoops, vibrato, legato pairs,
breath, 35 dB noise).

| Backend | Matched | P / R / F | Octave err. | False splits | Onset error | Pitch error |
| --- | --- | --- | --- | --- | --- | --- |
| yin | 79 / 79 | 1.00 / 1.00 / 1.00 | 0 | 0 | 4.5 ms | 0.3 c |
| yin-viterbi | 79 / 79 | 1.00 / 1.00 / 1.00 | 0 | 0 | 4.6 ms | 0.3 c |

These fixtures were used while developing the pipeline: they are a regression guard, not an
unbiased measurement. Per-fixture lines: `data/phrase-eval.txt`, `data/phrase-eval.json`.

### Stress set (8 phrases, 42 notes) — honest history

The stress set was written *after* the pipeline passed the standard fixtures and run once
before any further change (round 1, `data/phrase-stress-round1.txt`):

| Fixture | yin | yin-viterbi |
| --- | --- | --- |
| 107 ms legato run | 8/12 | 8/12 |
| wide slow vibrato (150 c at 4.5 Hz) | 3/3, 18 false splits | 3/3, 18 false splits |
| 250 ms portamento | 4/4 | 4/4 |
| E1–E2 bass | 3/5 | 2/5 |
| C6–C7 high | 3/5 (2 octave errors) | 3/5 (2 octave errors) |
| 6 dB SNR | 0/5 | **5/5** |
| 9 dB tremolo | 3/3, 15 false splits | 3/3, 15 false splits |
| breathy tone (aspiration 8 dB under) | 0/5 | **5/5** |
| **total** | **24/42, F 0.35** | **33/42, F 0.59** |

Round 1 decided the backend: the Viterbi tracker is the only one that survives noise and
breathiness. It also showed real faults, fixed with general rules rather than per-fixture
tuning: the vibrato veto (one-cycle mean), the tremolo veto (periodic dips), lighter
smoothing plus the plateau rule for glides (fast runs), and a 40–2200 Hz default range.
Round 2 (`data/phrase-stress-round2.txt`):

| Backend | Matched | F | False splits |
| --- | --- | --- | --- |
| yin | 37 / 42 | 0.66 | 28 (noise, breath) |
| yin-viterbi | **42 / 42** | **1.00** | 0 |

**After round 2 the stress set is no longer held out** — it now guards the fixes
(`[phrase]` tests). The analysis has **not** been evaluated on real recordings in this
environment (none are in the repository; the corpus is local-only). That evaluation is the
most important open item: see `test-instructions.md` ("Real recordings").

### Cost

Measured in the build container (4 vCPU Xeon 2.8 GHz, virtualised, under concurrent load):
about 35–45× real time (a 30 s phrase in 0.8 s); peak resident memory of the whole CLI
process 13 MB for a 10 s phrase and 28 MB for 30 s. Recordings over 180 s are analysed in
part (warning); the plug-in loads up to 300 s.

## Known limits

- Monophonic material only. Chords, full mixes or overlapping voices give unreliable notes;
  nothing separates sources.
- Trills faster than the vibrato veto's view (≥ 2 semitones, ~4–6 Hz, no plateaus) may be read
  as vibrato on one note.
- Consonants are kept inside the attack only up to 120 ms before voicing.
- Very low notes below 40 Hz and above 2200 Hz are out of the default range (settings).
