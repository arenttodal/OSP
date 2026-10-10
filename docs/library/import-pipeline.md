# Import pipeline

`osp::library::importSound` (`src/library/SoundImport.*`): background worker only.

| Step | What | Failure leaves |
|---|---|---|
| 1 | The file exists, opens, and its header reads as audio OSP plays (WAV, AIFF, FLAC; header only, nothing decoded) | nothing; a reason ("not there any more", "no permission", "damaged", "unsupported") |
| 2 | SHA-256 of the bytes (the content identity) | nothing |
| 3 | The managed copy: if the store already has these bytes, reuse them; else copy to `<hash><.ext>.<stamp>.partial`, hash the copy, and rename it to `<hash><.ext>` (atomic on one volume). If another instance published the same bytes meanwhile, keep theirs | nothing, or a `.partial` that is never taken for a sound and is removed later (`clearInterruptedImports`, older than an hour) |
| 4 | One catalog transaction: the sound record (or the existing one, if these bytes were imported from this path before), its location, an `imported` history entry, its search text | a stored copy with no record: harmless, found again by the next import of those bytes |

Cancellation is honoured before step 3 publishes anything.

## Finding a sound (`resolveSound`)

1. The managed store (by hash).
2. Places it was seen (original, indexed, inbox), each checked by its bytes' hash.
3. Otherwise: missing, with the places that no longer hold it, for the relink view.

A file is never used because it has the right name: a replaced recording with the same name is
reported as missing, not played.

## Store layout

The instrument's existing store (`~/Library/Application Support/OSP/Samples`): flat,
`<sha256><.ext>` plus `<sha256>.analysis.json` (decision D-08). The catalog lives beside it in
`OSP/Library/catalog.db`.
