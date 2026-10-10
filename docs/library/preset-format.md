# Preset formats

Two forms of a complete instrument, sharing one state document.

## Referenced preset: `.osppreset` (unchanged)

The plugin's state XML (02-existing-state-contract.md): every setting, and per layer the
sound's content hash, file name, original path and playback model. Small, and found by
content hash in the sample store, so it reproduces the sound on this computer. Unchanged
by the Library; old files keep loading.

## Portable preset: `.ospinstrument` (manifest 2)

A zip that carries its sounds, so it reproduces the sound on any computer:

```
manifest.json
preset.xml                 the state XML
source/<sha256>.<ext>      every sound the state refers to
analysis/<sha256>.analysis.json   (optional; recall skips re-analysis)
```

`manifest.json`:

| Field | Version | Meaning |
|---|---|---|
| `schemaVersion` | 1, 2 | 2 adds the fields below; a reader accepts 1 and 2 |
| `format` | 1 | "OSP portable instrument" |
| `engineVersion`, `stateVersion` | 1 | the build and state it was made with |
| `sources[] {contentHash, filename, stored}` | 1 | each sound and its path inside the zip |
| `sources[].sha256`, `sources[].bytes` | 2 | integrity (checked on import) |
| `sources[].provenance` | 2 | `original`, `user`, `licensed`, `unknown`, `cleared` |
| `name`, `category`, `tags[]`, `notes`, `creator` | 2 | Library metadata |
| `presetId` | 2 | the preset's stable Library ID |

Import rules (untrusted input): entry names are single plain names inside `source/` or
`analysis/`; each source's bytes must hash to its name; files are written as `.partial` and
renamed; nothing outside the store is written; a damaged package changes nothing.

Distribution export: every source must be `original`, `user`, `licensed` or `cleared`;
`unknown` sources are listed and need the user's confirmation, which is recorded as `cleared`
by the user (a workflow safeguard, not a legal judgement).
