# Tagging, folder scanning and one-shot discovery (Stage 6)

What the Library can tell about a sound before anyone labels it: tags and a Type from
its name and folders, and what kind of recording it is. Code: `src/library/Tagging`,
`src/library/Classifier`, `src/library/FolderScan`. Tests: `[unit][library][tagging]`,
`[classifier]`, `[scan]`, and the hidden `[library-corpus]` and `[library-scan-perf]`.

Nothing here is ever a reason to delete anything. Every answer can be corrected by the user,
and the correction wins.

## Tags from names and folders

`evidenceFromName` splits a file name into words (separators, camelCase, digits) and looks
each word up in a small dictionary: instrument and sound words ("kick", "pad", "pluck",
"vox", "808", ...), with a plural fallback ("Snares" → snare). A word gives a tag. It can
also give a Type (Bass, Keys, Pluck, Pad, Percussion, Vocal, Texture), a root note ("C3",
"F#1", "Bb4"; C4 = MIDI 60), and hints: "loop"/"bpm" or "one shot"/"hit".

`evidenceFromFolders` does the same for the folders between the scanned root and the file.
The root itself does not count. This evidence is weaker: confidence 0.55 for the nearest
folder, less further out, against 0.75 for the name.

Tags are stored with their source (`filename`, `folder`, `audio`, `user`) and confidence,
apart from the user's own tags. A tag the user removes is kept as `rejected`, so a rescan
never brings it back.

On import (`requestWithEvidence`, used by both a scan's import and the Inbox's):

- **Type**: the name's first (a person wrote it), then the folders', then the audio's.
- **Root**: the name's, but only when the audio's pitch agrees within a semitone; otherwise
  the audio's own.
- **Tags**: the name's and folders' tags, plus the audio's tags (percussive, sustained,
  tonal, noisy, bright, dark, stereo).

## What kind of recording

`classifySound` gives one of five classes, a confidence, and a reason in words (shown in the
review list):

| Class | Meaning | Rule (first that fits) |
|---|---|---|
| Likely Loop | A pattern meant to repeat | ≥ 4 attacks, ≤ 90 s, onset intervals on one grid (regularity ≥ 0.85; ≥ 0.6 when the name says loop) |
| Long Recording | A take, a field recording, a song stem | > 120 s, or > 45 s with ≥ 6 attacks or ≥ 4 separated events |
| Multiple Events | Several sounds in one file | ≥ 2 events separated by silence and the loudest < 90 % of the energy; or ≥ 3 held notes in a row (a legato phrase) |
| Likely One-Shot | One sound, however long it sustains | ≤ 1 attack, or one event with ≥ 85 % of the energy, or a stable pitch with ≤ 3 attacks |
| Possible One-Shot | Probably one sound, but unclear | anything else; also a one-shot after ≥ 1 s of silence ("trim it") |

There is no duration threshold for one-shots. A 5 s pad or a 20 s drone with one attack is
a one-shot.

The signals:

- **Events**: the slicer's attacks that die into the noise floor, and whether silence
  separates them.
- **The loudest event's share of the energy**.
- **Onset regularity**: the share of onset intervals that fall on one grid.
- **The envelope**: attack, decay, sustain, leading silence.
- **Pitch**: stability, and held notes (pitch-track stretches ≥ 200 ms within 40 cents;
  moves > 90 cents that are not an octave or a fifth).
- **Spectrum**: flatness, centroid, flux.

Answers are stored by content (`audio_analysis`, `classifierVersion` 1). The same bytes in
two folders are analysed once. A newer classifier analyses them again. The user's
correction is kept apart (`classification_overrides`) and survives every re-analysis.

## How well it does: measured, with its limits

**The labelled evaluation set** (`tests/library/LabelledSet.h`) has 12 kinds × 4 takes, all
synthetic and seeded, so the labels are certain and the set is reproducible:

- drum hits, plucks, bowed notes, vocal vowels, pads, sustained synths
- noisy recordings
- several isolated events, loops
- vocal phrases, field recordings, song stems

Results:

| Class | Precision | Recall | TP | FP | FN |
|---|---|---|---|---|---|
| Likely One-Shot | 1.00 | 1.00 | 28 | 0 | 0 |
| Multiple Events | 1.00 | 1.00 | 8 | 0 | 0 |
| Likely Loop | 1.00 | 1.00 | 4 | 0 | 0 |
| Long Recording | 1.00 | 1.00 | 8 | 0 | 0 |

48 of 48 correct. 659.5 s of audio took 17.6 s (about 38× real time on the cloud VM).

**Hard cases** (6 kinds × 2): a strummed chord, a swung loop, a long drone, a legato
phrase, a quiet hit in hiss, a reverse swell. All 12 are now right. They were meant to be
measured, not tuned to, but the legato phrase failed at first and the held-notes rule was
added because of it. **The hard set is therefore no longer blind.** Its result says less
than the evaluation set's.

**What these numbers do not show.** Synthetic sounds are cleaner than real ones. Real
recordings have room tone, reverb tails that bridge the silence between hits, bleed,
clipping, fades and loops that do not start on a beat. Expect lower numbers on real
libraries. The CI checks only lower bounds: one-shot precision ≥ 0.85, recall ≥ 0.8; loop
precision and recall ≥ 0.75; multiple-events and long-recording recall ≥ 0.6; overall
≥ 0.75. The Library never claims a one-shot is certain: the classes say "likely" and
"possible", and the user decides.

**The research corpus** (`[.][library-corpus]`, 80 real recordings, no labels) is for a
person to judge. It found 78 Likely One-Shot and 2 Multiple Events:
`plucks/Plucks_Bounce_Bounce_2026-06-21_131923-1.wav` and
`tagel-puck/Tagel_Puck_Bounce_2026-06-21_144438.wav` (13 s). Both are "Bounce" takes, which
may well hold several notes. Whether that is right is for the user to listen and say.

**Known limits.**

- A loop whose hits are not separated by silence and not on a steady grid (rubato, heavy
  swing above about 15 %) is "Possible One-Shot", not a loop.
- Reverb that never falls to the floor joins separate hits into one event.
- Sung phrases with very small pitch moves (under about 90 cents) look like one held note.
- Analysis reads at most the first 120 s of a file.

## Folder scanning

The user chooses each folder (**⋯ → Scan folder…**). Nothing else is ever scanned: no
drive, no home folder, no default location. A folder can have include and exclude patterns
(`*kick*; pads/*`, matched against the name and the path inside the folder,
case-insensitive), and can be scanned with or without its subfolders.

A scan has two stages, on a background thread of its own (background priority, its own
catalog connection) so it never competes with the audio:

1. **Index.** Walk the folder and read each audio file's header. A file whose size and
   modification time match the last scan is skipped. A file that has gone is forgotten (its
   Library records stay). Hidden folders are skipped.
2. **Deep** (optional, on by default). Hash each new or changed file, analyse it if its
   content has not been analysed yet, and classify it. Files longer than 60 s can be
   skipped.

Pause and cancel are checked between files. A cancel keeps everything done so far. Odd files
(unreadable, empty, a bad header) are counted as unreadable, and the scan goes on. Files on
disk are never moved, changed or deleted.

Found files are **candidates** (the Scanned list), not Library sounds, until the user
imports them. A candidate can be **set aside** (never deleted; the Set aside list brings it
back). Its kind can be corrected, and it can be trimmed or sliced. Importing a candidate
whose content is already in the Library returns the existing record.

**Speed** (`[.][library-scan-perf]`, cloud VM, tiny distinct 16-bit files):

| Files | Index | Rescan (unchanged) | Peak memory |
|---|---|---|---|
| 1,000 | 0.05 s (≈ 20,000 files/s) | 0.02 s | 11 MB |
| 10,000 | 0.41 s (≈ 24,500 files/s) | 0.19 s | 29 MB |
| 50,000 | 2.20 s (≈ 22,700 files/s) | 0.85 s | 49 MB |

- **The deep stage** is timed at 1,000 files: 1.07 s for 20 ms files. On real material it
  is bound by the analysis, about 38× real time per core. A folder of 10,000 one-second
  hits takes about 4–5 minutes.
- **Cancel** stops a running deep pass within one file (measured 4 ms).
- **Not measured**: a spinning disk, a network share or a macOS laptop. Those numbers are
  for the user's own machine.
