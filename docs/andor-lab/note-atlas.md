# Note Atlas — schema version 1

The Note Atlas is the shared data model of the ANDOR Lab instruments: **ANDOR Phrase produces
it, ANDOR Grain consumes it**, and a later hybrid instrument can use both halves of it. It
describes one (or more) source recordings and their musical interpretation: where each note is,
what pitch it is, how it is articulated, what the user corrected.

Code: `src/andor/atlas/NoteAtlas.{h,cpp}` (plain C++20 data, no JUCE, no editor, no host, no
singleton, no voice) and `src/andor/io/NoteAtlasJson.{h,cpp}` (JSON).

## Principles

- **Absolute source frames.** Every position is a frame index in the source file at its own
  sample rate (`int64`, written exactly). Nothing is stored as rounded UI time.
- **Audio is referenced, never embedded.** A source is identified by its content fingerprint
  (`sha256:<hex>`), with a relative path (portable bundles), the last known absolute path and the
  managed store copy as ways to find it.
- **Readable and versioned.** JSON with a format tag and `schemaVersion`; unknown fields are
  ignored, so adding a field is a documented, non-breaking change. Changing or removing a
  field's meaning requires a version bump and a migration in `noteAtlasFromJson`.
- **Exact round trip.** Values the instruments play from (pitch, cents, confidence, levels)
  are written with 17 significant digits: a recalled session plays sample for sample like the
  saved one (tested).
- **User edits are first-class.** `corrections` records what the user changed; a re-analysis
  keeps every corrected note (`phrase::edit::mergeReanalysis`).

## File

Extension `.andoratlas` (UTF-8 JSON). Inside plug-in sessions the same JSON is stored as the
`atlas` property of the state tree.

```json
{
  "format": "andor.noteAtlas",
  "schemaVersion": 1,
  "atlasId": "atlas:6f1c2a...",
  "createdBy": "ANDOR Phrase 0.1.0",
  "analysis": {
    "method": "yin-viterbi", "version": 1, "a4Hz": 440.0,
    "tuningOffsetCents": 0.0, "settingsDigest": "yin-viterbi|40.0-2200.0|h0.0050|..."
  },
  "sources": [ {
    "id": "src0", "contentHash": "sha256:...", "fileName": "phrase.wav",
    "relativePath": "audio/phrase.wav", "lastKnownPath": "/Users/me/phrase.wav",
    "managedCopy": "/Users/me/Library/Application Support/ANDOR Lab/Samples/<hash>.wav",
    "sampleRate": 48000.0, "numChannels": 2, "numFrames": 331200
  } ],
  "notes": [ { ... } ]
}
```

### Source fields

| Field | Meaning |
| --- | --- |
| `id` | stable id inside the atlas, referenced by notes (`"src0"`) |
| `contentHash` | `sha256:<hex>` of the file's bytes — the fingerprint used for relinking |
| `fileName` | display name |
| `relativePath` | path relative to the atlas file's folder; rewritten on every write from `lastKnownPath` |
| `lastKnownPath` | absolute path when written |
| `managedCopy` | copy in the ANDOR Lab sample store (by hash) |
| `sampleRate`, `numChannels`, `numFrames` | the decoded source: channels are the ones used (at most 2) |

### Note fields

| Field | Meaning |
| --- | --- |
| `id` | stable note id (`"n0007"`); never reused inside an atlas (an undone split cannot collide) |
| `source` | source id |
| `sourceStart`, `sourceEnd` | the note's whole region (frames) |
| `attackStart` | suggested playback start: keeps consonants, bow noise, picks |
| `sustain.start`, `sustain.end` | the pitch-stable body (loops, grains) |
| `releaseStart` | where the recorded release begins |
| `fadeIn`, `fadeOut` | non-destructive fades (frames) |
| `pitchHz`, `midiPitch` | robust central pitch of the voiced body (A4 = `analysis.a4Hz`) |
| `rootMidi` | the key the note is *at* (rounded under the tuning rule) |
| `cents` | `midiPitch - rootMidi` in cents (kept apart from the note identity) |
| `stabilityCents` | robust spread of the body's pitch |
| `vibratoCents`, `vibratoHz` | expressive deviation, measured separately (0 = none detected) |
| `voicing` | fraction of the body's frames that are voiced (0..1) |
| `confidence` | overall trust in pitch and boundaries (0..1) |
| `peakDb`, `rmsDb` | dBFS (peak over the region, RMS over the sustain) |
| `articulation` | `sustained`, `short`, `legato`, `noisy` |
| `method`, `methodVersion` | analysis backend and version that described this note |
| `corrections` | `{pitch, boundaries, key, group, split, merged}`: what the user changed |
| `included` | false: kept in the atlas but not playable |
| `assignedKey` | -1: plays on `rootMidi`; otherwise the key the user mapped it to (it sounds there as recorded) |
| `group` | variation group; empty = grouped by key |
| `variation` | order inside the group (time order unless edited) |
| `grainRegion` | optional `{start, end}` a granular consumer should use (default: `sustain`) |

Order invariant (checked by `atlas::validate`, restored by `atlas::repair`):
`sourceStart <= attackStart <= sustain.start <= sustain.end <= releaseStart <= sourceEnd`,
all inside `[0, numFrames]`.

### The tuning rule

`atlas::roundPitch (midiPitch, tuningOffsetCents)`: subtract the recording's global offset (0
unless auto tuning is on), then round to the nearest key; exactly +50 cents belongs to the
lower key (deterministic, independent of floating-point tie behaviour). `cents` is always
measured against the chosen key.

## Operations

| Function | Purpose |
| --- | --- |
| `validate (atlas)` | ids, references, ordering, ranges, pitch sanity → errors / warnings |
| `repair (atlas)` | clamps markers back into order inside their source |
| `remapNotes (atlas)` | recomputes root/cents for uncorrected notes; renumbers variations |
| `notesByKey (atlas)` | included notes per key (0..127) in variation order |
| `checkRelink (source, hash, rate, channels, frames)` | `exact` / `compatible` (same shape, different bytes) / `incompatible` |
| `io::resolveSource (source, atlasFolder, searchFolders)` | relative path → last path → managed copy → by name in folders, checked against the fingerprint |
| `io::relinkSource (atlas, id, file)` | points a source at a new file after checking it (refuses a different length or rate) |

## Versioning

| Version | Change |
| --- | --- |
| 1 | first version (ANDOR Lab, 2026-10) |

Readers refuse a file without the format tag, without `schemaVersion`, or with a newer version
than they understand (clear error). Older versions will be migrated in `noteAtlasFromJson`
step by step.
