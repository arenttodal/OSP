# Library catalog schema (version 5)

SQLite, WAL. File: `~/Library/Application Support/OSP/Library/catalog.db` (macOS;
`<user app data>/OSP/Library/catalog.db` elsewhere; `OSP_LIBRARY_DIR` overrides for tests).
Audio is never stored in the database.

| Table | Columns (key ones) | Purpose |
|---|---|---|
| `meta` | key, value | `schema_version`, creation time, last integrity check |
| `assets` | id (UUID), type (`sound`/`preset`/`template`), origin, name, created, modified, favourite, notes, category, trashed_at, rating (v3, 0-5), pack_id, pack_key (v5) | every asset; a sound's category is its Type (Bass, Keys, ...); a pack's asset names its pack and its item's stable key (the manifest's uid), so an update keeps the record (index `assets_pack`) |
| `sounds` | asset_id, content_hash, managed (0/1), format, sample_rate, channels, bit_depth, frames, duration, file_size, root_midi, provenance, parent_asset, slice_start, slice_end, operations | sound details; derived slices point at their parent |
| `presets` | asset_id, file (relative to its root), root (`user`/`factory`/`pack`), state_version, layers (source count), trashed_from (v2) | preset and template files; a trashed file remembers where it came from |
| `preset_sounds` | preset_id, content_hash | what a preset needs (cleanup safety) |
| `collections` | id, name, created | user collections |
| `collection_items` | collection_id, asset_id | membership |
| `tags` | id, name (unique, case-folded) | tag names |
| `asset_tags` | asset_id, tag_id, source (`user`/`filename`/`folder`/`audio`), confidence, rejected | inferred tags kept apart from user tags; a user removal is a `rejected` row so rescans respect it |
| `history` | id, asset_id, at, action (`loaded`/`imported`/`previewed`), layer | recent sounds, use counts |
| `source_locations` | content_hash, path, kind (`original`/`indexed`/`inbox`), last_seen, available | where bytes were found |
| `audio_analysis` | content_hash, version, classification, features (JSON) | cached analysis, by content: the same bytes anywhere are analysed once per classifier version (`classifierVersion`, now 1). `features` is the classifier's JSON (docs/library/classification.md): `class`, `confidence`, `reason`, `type`, `tags`, `f0`, `pitched`, the measured features |
| `classification_overrides` (v4) | content_hash, classification, updated | the user's say on what a sound is; wins over every analysis and survives re-analysis |
| `scan_roots` / `scan_files` | root path, include/exclude; path, size, mtime, content_hash, state (`indexed`/`analysed`/`failed`/`imported`), asset_id, rejected (v4), duration, sample_rate, channels, format, error (v4) | incremental scanning; a candidate stays a candidate until imported; `rejected` is "set aside" (never deleted); index `scan_files_root` (v4) |
| `inbox_items` (v4) | path, folder, state (`detected`/`waiting`/`ready`/`importing`/`imported`/`failed`/`missing`), size, mtime, stable_since, content_hash, asset_id, error, attempts, detected, updated | the capture Inbox's reconciliation state (docs/library/inbox.md) |
| `import_jobs` | id, path, state, error, created | resumable imports, inbox states |
| `packs` | id, name, version, creator, manifest (JSON, as installed), installed | installed packs (docs/library/packs.md) |
| `assets_fts` | FTS5 (name, tags, collections, notes, folder, pack) | search; rebuilt from the tables when missing |

Integrity: `PRAGMA integrity_check` on open after an unclean shutdown; foreign keys on; every
multi-row change in one transaction.

## Migrations

| From | To | Change |
|---|---|---|
| - | 1 | the tables above |
| 1 | 2 | `presets.trashed_from` (where a preset sent to the Library's trash came from) |
| 2 | 3 | `assets.rating` (0-5 stars, default 0) |
| 3 | 4 | `scan_files` gains rejected, duration, sample_rate, channels, format, error; new `classification_overrides`, `inbox_items`; index `scan_files_root` (Stages 6-8) |
| 4 | 5 | `assets.pack_id`, `assets.pack_key`, index `assets_pack` (Stage 10); the search index (fts_version 3) also finds a pack's name |

Each step runs in one transaction on open; a catalog newer than the build is refused and left
untouched. Tested: a version 1 catalog (made by hand) opens as the current version with its records.
The search index carries its own version (`meta.fts_version`, 3: keyed by asset rowid, the pack
column holding the pack's name) and is rebuilt from the tables when it differs.

A backup (docs/library/backup.md) holds a snapshot of the whole catalog (`VACUUM INTO`); a
restore merges it with `Catalog::mergeFrom` (identities kept; scan and Inbox state left out).

Removing for good: `Catalog::purge` deletes only a trashed record (its tags, history and
collection places cascade); `library::emptyTrash` does it for the whole trash and moves
files nothing holds to the system Trash (docs/library/preview.md, Storage).

Per-user Library settings are not in the catalog: `<Library>/settings.json`
(schemaVersion 1: `previewGainDb`; added without a version bump, as optional fields with
defaults: `inboxFolders` (array of paths), `scanSubfolders`, `scanFindOneShots`,
`scanSkipLong` (the Scan sheet's last choices)).

A derived sound (a trim or slice, Stage 8) is an ordinary `sounds` row with `parent_asset`
(the original's asset id), `slice_start`/`slice_end` (seconds in the original) and
`operations`: JSON `{"fadeIn", "fadeOut", "gainDb", "zeroCrossing", "slicer":
"sampleforge-1", "analysis": <classifierVersion>, "source": <path, when cut from a
candidate>}`. Its place is recorded as `(derived)`.
