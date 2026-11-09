# P3: Note Atlas export, and the first playable OSP instrument

**Status: the first milestone's demonstration works on the controlled material. Stopping here
for the first playable-instrument review.**

The demonstration chain on the synthetic C-major chord:

1. The harmonic detector (P1) finds C4, E4 and G4 (F1 1.0).
2. Method C (P2) cuts them into three separate samples.
3. The lab exports an ordinary OSP Note Atlas.
4. **The real OSP engine plays it:**
   - `research-renderer --atlas` (engine C) plays the recorded keys, the recorded chord, a
     C-major scale, chords that were never recorded (F, Am, G) and a phrase;
   - the OSP plugin processor (headless) loads it with 0 validation errors and 0 missing sources;
   - it exports as a portable `.ospinstrument`, re-imports into a fresh sample store, and plays
     **bit-identically**.

**No human has listened to any of this.** Everything below is measured. "Does it sound like a
usable piano?" is exactly the question this stop is for. The material for that judgement is
listed under "For the review".

## What was implemented

- **`mapping/prepare.py`: sample regions.** All values are in each note's own WAV frames:
  - `attackStart` = onset − 5 ms. It keeps the hammer's edge; detected onsets are off by a few ms.
  - `sustain` = the last 0.5 s before the key release, ending 50 ms before it.
  - `releaseStart` = the key release (the damper).
  - `sourceEnd` = where the note has fallen 60 dB (P2).
  - fades of 2 ms in and 10 ms out.
  - pitch = the template's measured f0, with cents from the key.
  - `confidence` = detector confidence × (0.5 + 0.5 × P2's `ownership`).
  - articulation `sustained` if the key was held ≥ 0.4 s, otherwise `short`.
- **Selection:**
  - one take per key: the highest score, where score = confidence × a held-length term × a level
    term;
  - the other takes stay in the atlas, excluded, each with a reason (OSP's NOTE MAP can switch
    them on);
  - notes quieter than −60 dBFS or more than 50 cents off their key are excluded;
  - `--include all` keeps every usable take, which OSP plays as round-robin variations.
- **`mapping/atlas.py`: the exporter.** It writes one self-contained folder:
  - `instrument.andoratlas`: `andor.noteAtlas`, schema 2, only fields OSP already reads;
  - `samples/*.wav`: one source per note, identified by `sha256:` of the file's bytes and found
    through `relativePath`;
  - `instrument.osp-poly.json`: the lab's sidecar (schema `osp-poly.atlas-sidecar/1`), with
    provenance, selection scores and reasons, and the P2 metrics.

  The atlas sets FILL (12 semitones) and corrects tuning. There are two mappings:
  - `original`: every note on its own key, and FILL lets the keys between borrow the nearest one;
  - `compact`: notes on consecutive white keys from C4, untransposed, no FILL — an audition
    layout.
- **`mapping/render.py`:**
  - a minimal standard-MIDI writer;
  - demo material: recorded keys, recorded chord, C-major scale, chords F–Am–G–C, and a phrase
    over a C3 bass;
  - `render_demos`, which calls OSP's own `research-renderer --atlas … --midi …`;
  - `plugin_check`, which runs the hidden Catch2 test below on the lab's atlas.
- **CLI:**
  - `osp-poly export-atlas <extraction> --out … [--mapping] [--include] [--render] [--plugin-check]`
  - `osp-poly build <wav> --out …`: detect → extract → export → renders → plugin check. A failure
    stops at the failing step and keeps everything written so far.
- **C++: tests only. OSP itself is unchanged.** `tests/plugin/PolyLabTests.inc` is included at the
  end of `PluginProcessorTests.cpp`. It has two tests:
  - **`[plugin][poly-lab]`** (runs in ctest):
    - an atlas written in the exporter's exact JSON layout (three decaying single-note WAVs, the
      sources found only through `relativePath`, since `lastKnownPath` points nowhere) is read by
      `io::readNoteAtlas` and validates;
    - it plays C4, E4 and G4; FILL sounds D, F, A and C5; a key out of reach is silent;
    - it exports as a portable instrument; the folder is deleted; the import into a fresh store
      plays.
  - **`[.poly-lab-import]`** (hidden): the lab's real atlas, through `OSP_POLY_ATLAS` and
    `OSP_POLY_OUT`. It plays each key and the recorded chord, F major and A minor; writes the
    WAVs; exports, re-imports, re-renders; checks the result is identical; and writes
    `plugin-import.json`.

Scope check against the spec:

- no change to the sampler, the phrase detection, Note Atlas, NOTE MAP, playback, parameter IDs
  or the GUI;
- no Python or ML on the audio thread;
- no new top-level mode;
- the only C++ added is the test file above.

## Results

### The milestone: `osp-poly build` on B-chord-C-major (harmonic detector, method C)

| Step | Measured |
|---|---|
| Detect | C4, E4, G4 at 0.093 s (truth 0.100). F1 1.0, offsets included. 0 octave errors. |
| Extract | Each note's SI-SDR is 15.2 dB above the mixture's (C4 12.7, E4 20.3, G4 12.1 dB). SIR 25.7 dB. Pitch +0.9 / −0.7 / −1.0 cents. Notes + residual = mixture within 1.1e-16. |
| Samples | 3 WAVs, 0.073–2.44 s of the recording each, peaks −17.9 / −17.9 / −19.0 dBFS. |
| Atlas | 3 of 3 notes included, keys 60, 64, 67. FILL 12 st. |
| Plugin | 3 members, 0 validation errors and warnings, 0 missing sources. Each key −38 to −40 dBFS RMS over its first second. Export → import is identical. |

### OSP renders, transcribed back by the P1 detector

This is an objective check that the instrument plays the requested pitches:

| Render | Requested | F1 | Missed | Extra |
|---|---|---|---|---|
| recorded keys (C4, E4, G4 alone) | 3 | 1.00 | 0 | 0 |
| recorded chord | 3 | 0.75 | 0 | 2 (in the release tail, after note-off) |
| C-major scale (5 of 8 keys are FILL) | 8 | 1.00 | 0 | 0 |
| **new chords F, Am, G, C** (never recorded) | 12 | 0.96 | 0 | 1 |
| phrase over a bass C3 (one octave below the lowest recording) | 8 | 0.94 | 0 | 1 (C3 split) |

`images/p3-osp-render-new-chords.png` shows the new chords; green marks the requested notes.

### A second example: the synthetic phrase (17 notes, 4 chords and a melody)

With the harmonic detector: 21 events, 1 missed, 5 false. Extraction SI-SDR is 6.1 dB on the
matched notes (+17.3 over the mixture). Selection kept 16 keys and excluded 5 duplicate takes.
The plugin check passed: 16 notes loaded, the re-import is identical.

**But two detector false positives became keys** (B5 and F5, scores 0.26 and 0.25), and their
scores overlap real notes' (D4 0.23, C4 0.33). A confidence floor would not separate them.
Better transcription, or the user's NOTE MAP review, has to.

## Findings for the review

1. **Holding a key past the recorded length makes OSP's continuation swell.** The recorded chord
   held for 2.5 s rises from −46 to −36 dBFS at about 1.85 s (+10 dB over 0.25 s).
   - This is OSP's own continuation analyser (`analysis/continuation`, Phase 5 `forceLoop`): it
     loops a fading note's body, and its header warns that "a fading note's loop may pulse a
     little".
   - Moving the atlas's sustain region does not change it (measured: an identical render), because
     the analyser chooses its own region.
   - It is not a lab defect, and changing playback is outside this prototype's scope. For decaying
     instruments it needs a decay-aware continuation in OSP, or the notes need to play as recorded.
     **A decision for the review.**
2. **Separation is at the oracle limit for this STFT on the milestone chord.** C4 and G4 lose
   about 7 dB against E4 to shared partials. Whether 12 dB SI-SDR sounds clean as a piano sample
   is a listening question.
3. **All of this is synthetic piano.** The P1 and P2 limitations carry over unchanged.

## For the review (listening material, under `research/polyphonic-lab/output/`)

These files are not committed: they are generated, and the corpus policy keeps audio out of git.
`osp-poly build` regenerates them byte for byte.

| What | Path |
|---|---|
| the recording | `corpus/B-chord-C-major/mix.wav` |
| the three extracted samples | `milestone-c-major/extraction/notes/note-000{1,2,3}_{C4,E4,G4}.wav` |
| what was left over | `milestone-c-major/extraction/residual.wav` |
| the OSP instrument | `milestone-c-major/instrument/instrument.andoratlas` (open in OSP; samples beside it) |
| OSP engine renders | `milestone-c-major/instrument/renders/{recorded-keys,recorded-chord,c-major-scale,new-chords-F-Am-G-C,phrase}.wav` |
| plugin renders | `milestone-c-major/instrument/plugin/plugin-{key-60,key-64,key-67,recorded-chord,F-major,A-minor,reimported-chord}.wav` |
| the phrase instrument | `example-phrase/instrument/` |

Suggested questions:

- Do the three notes sound like single piano notes, or like a chord with two notes turned down?
- Is the attack intact?
- Do F, Am and G sound like the same piano?
- Is the held-note swell acceptable?

## Tests

- **pytest:** 35 passed (P3: 5).
  - The exported atlas has OSP's exact field set for sources and notes, the markers in order,
    hashes that match the files, and tuning within 5 cents.
  - The compact mapping uses white keys and no FILL.
  - Selection keeps one take per key and gives reasons.
  - The MIDI writer writes a valid format-0 file.
  - OSP renders every demo (skipped where research-renderer is not built).
- **Catch2:** `[plugin][poly-lab]` passed (30 assertions). `[.poly-lab-import]` passed on the
  milestone atlas and on the phrase atlas.
- **ctest (build-plugin):** 10/10 passed, including the plugin suite with `[plugin][poly-lab]`.

## Reproduce

```bash
cmake -B build-plugin -G Ninja -DOSP_BUILD_PLUGIN=ON && cmake --build build-plugin
cd research/polyphonic-lab
.venv/bin/osp-poly corpus generate
.venv/bin/osp-poly build output/corpus/B-chord-C-major/mix.wav --detector harmonic \
    --manifest fixtures/manifests/B-chord-C-major.json --out output/milestone-c-major --render --plugin-check
.venv/bin/osp-poly build output/corpus/D-phrase-synthetic/mix.wav --detector harmonic \
    --manifest fixtures/manifests/D-phrase-synthetic.json --out output/example-phrase --render --plugin-check
```

Starting commit of the lab: c49d77f. P0 is 8308f0a, P1 is 87463f6, **P2 and P3 are 8da975f**
(branch `claude/polyphonic-lab`).

## After the review: curated selection (the new default)

**The owner's verdict on four real loops:** "fine, not realistic". The goal is *some* playable
instrument derived from the recording, not a faithful split. So `export-atlas` and `build` now
default to `--include curated` (`mapping/curate.py`):

- every note is scored from its own audio: ownership, tuning, pitch stability, harmonicity,
  ringing length and level;
- clearly bad notes are dropped;
- the best are kept, at least 3 semitones apart, at most 12;
- FILL is stretched so that every key from A0 to C8 sounds.

| Recording (the owner's, not committed) | Notes before | After | Reach |
|---|---|---|---|
| piano one-shot | 4 | 3 | 31 semitones |
| Rhodes loop | 19 | 8 | 27 |
| organ loop | 18 | 8 | 41 |
| full hip-hop mix | 33 | 9 | 38 |

Every key from 21 to 108 sounds through OSP's renderer: finite, −33 to −53 dBFS. The plugin load,
export and re-import check passes for all four.

## What comes next (only after the review)

Not started, per the spec's mandatory stop:

- real recordings;
- vibrato-aware templates for voices and strings (P5);
- the gospel recording.
