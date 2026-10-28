# ANDOR Lab — user test instructions (Mac, Ableton Live, real recordings, listening)

Everything here needs a person, a Mac, ears or real recordings, which this build environment
does not have. Results go into `known-issues.md` and `STATUS.md`.

## 0. Build and install (development only)

Easiest: download the `ANDOR-Lab-macOS-test` artefact from a green CI run (GitHub → Actions →
the run → Artifacts; universal, unsigned), unzip it and double-click `install.command` (copies
both plug-ins and apps for the current user and clears the download quarantine; see its
`README.txt`). CI already runs auval on both. Or build locally: `build-and-validation.md`
("Development-only installation on a Mac"). Run `auval -v aumu Aphr Ospx` and
`auval -v aumu Agrn Ospx` and note the result.

## 1. Standalone first (both instruments)

1. Open `ANDOR Phrase.app` → Options → Audio/MIDI settings: choose the output and your MIDI
   keyboard. Repeat for `ANDOR Grain.app`.
2. Phrase: PRESET ▾ → Load example phrase. Expect "8 notes" and coloured regions. Play the
   keyboard: D4 E4 F#4 A4 G4 sound as recorded; C#4 / D#4 sound transposed (BALANCED fill);
   C2 is silent. Play chords.
3. Drag one of your own monophonic recordings in (10–30 s, WAV/AIFF). Check the detected
   notes against what you sang/played (see §4).
4. Grain: SOURCE ▾ → Example: sustained tone; play; turn SIZE / DENSITY; open KEY DNA, choose
   SIZE, click three points (low keys up, high keys down); play low and high keys: low keys
   must sound longer-grained. NEW SEED with VARIATION up: each key changes, the same seed
   brings it back.
5. Save presets in both (PRESET ▾ → Save preset…), quit, reopen, load: identical behaviour.

## 2. Ableton Live (primary host) — Gate E

For each instrument (VST3, then AU):

| # | Check | Expected |
| --- | --- | --- |
| 1 | Insert on a MIDI track; plug-in window opens; resize the window | scales without overlap; minimum ~60 %, maximum 200 % |
| 2 | Phrase: drag a recording onto the window; Grain: load a sample and a `.andoratlas` | analysis progress, then notes / grains; Live keeps playing |
| 3 | Play MIDI clips: melody, chords, long holds with the sustain pedal | no stuck notes; SUSTAIN holds; pedal works |
| 4 | Automate 3 parameters (Phrase: TONE, SUSTAIN CHAR., SPACE; Grain: POSITION, DENSITY, OSC/GRAIN) | smooth, no zipper noise, written and read back |
| 5 | Save the Live set, close Live, reopen | same sound; Phrase must **not** re-analyse (status shows the note count at once); edits (moved markers, corrected pitches) intact |
| 6 | Rename/move the original sample file, reopen the set | still plays (managed store). Delete the store folder too → "Missing …"; Relink (Phrase menu) finds the file |
| 7 | Export audio (offline render) twice | the two renders are identical (null test in Live: invert one) |
| 8 | Change buffer size (64 → 2048) and sample rate (44.1 → 96 kHz) while playing | no crash, correct pitch |
| 9 | Two instances of each instrument on separate tracks with different sources | independent; no cross-talk of presets or sessions |
| 10 | Delete and re-insert the plug-in; undo in Live | no crash |
| 11 | Phrase → Grain: in Phrase, menu → Export Note Atlas; in Grain, SOURCE ▾ → Load Note Atlas | the same notes appear as regions in Grain; playing the recorded keys gives the same pitches |
| 12 | CPU meter with 12 Grain voices held (STANDARD) and 16 Phrase voices | note the Live CPU % on your Mac model |

Repeat 1, 3, 5, 7 in a second host (Logic or Reaper). Report the Mac model, macOS version,
Live version, buffer size, and anything that looked or sounded wrong.

## 3. Listening (the tests cannot judge these)

Render with the CLI (no GUI needed) and listen on good headphones:

```sh
research-renderer --phrase-ab your-phrase.wav --output-dir listen --demo melody
research-renderer --phrase-ab your-phrase.wav --output-dir listen --demo hold      # SUSTAIN quality
research-renderer --phrase-ab your-phrase.wav --output-dir listen --demo chords
research-renderer --phrase-render your-phrase.wav --demo hold --character 0.6 --output listen/bloom.wav
```

`-A-baseline.wav` is a plain resampler of the whole file at one root, `-B-randomised.wav` the
naive randomised sampler (the project's baselines A and B), `-C-phrase.wav` ANDOR Phrase.
Judge: attack preserved, clicks at boundaries, loop seams and sustain artefacts, timbre
change on filled keys (formants), tuning, does it keep the character of the recording.

## 4. Real recordings: analysis accuracy

1. Annotate a recording as JSON: `{"notes": [{"start": 0.42, "end": 0.91, "midi": 64}, ...]}`
   (start/end in seconds, midi may be fractional).
2. `research-renderer --phrase-eval-file take.wav --truth take.truth.json --backend all`
3. Send the printed lines (precision, recall, octave errors, false splits, onset error).
   Suggested set: one sung phrase with vibrato, one nyckelharpa or violin phrase (legato,
   bow changes), one flute line (breath), one synth lead. Keep private recordings local.

## 5. What to report

Use `known-issues.md` as the list to confirm or extend. Gate E is complete only when
Standalone, VST3, AU, Ableton, multiple instances, save/reopen, offline render and audio/MIDI
settings have each been checked on a Mac.
