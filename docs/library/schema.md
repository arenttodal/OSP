# Library catalog schema (version 3)

SQLite, WAL. File: `~/Library/Application Support/OSP/Library/catalog.db` (macOS;
`<user app data>/OSP/Library/catalog.db` elsewhere; `OSP_LIBRARY_DIR` overrides for tests).
Audio is never stored in the database.

| Table | Columns (key ones) | Purpose |
|---|---|---|
| `meta` | key, value | `schema_version`, creation time, last integrity check |
| `assets` | id (UUID), type (`sound`/`preset`/`template`), origin, name, created, modified, favourite, notes, category, trashed_at, rating (v3, 0-5) | every asset; a sound's category is its Type (Bass, Keys, ...) |
| `sounds` | asset_id, content_hash, managed (0/1), format, sample_rate, channels, bit_depth, frames, duration, file_size, root_midi, provenance, parent_asset, slice_start, slice_end, operations | sound details; derived slices point at their parent |
| `presets` | asset_id, file (relative to its root), root (`user`/`factory`/`pack`), state_version, layers (source count), trashed_from (v2) | preset and template files; a trashed file remembers where it came from |
| `preset_sounds` | preset_id, content_hash | what a preset needs (cleanup safety) |
| `collections` | id, name, created | user collections |
| `collection_items` | collection_id, asset_id | membership |
| `tags` | id, name (unique, case-folded) | tag names |
| `asset_tags` | asset_id, tag_id, source (`user`/`filename`/`folder`/`audio`), confidence, rejected | inferred tags kept apart from user tags; a user removal is a `rejected` row so rescans respect it |
| `history` | id, asset_id, at, action (`loaded`/`imported`/`previewed`), layer | recent sounds, use counts |
| `source_locations` | content_hash, path, kind (`original`/`indexed`/`inbox`), last_seen, available | where bytes were found |
| `audio_analysis` | content_hash, version, classification, features (JSON) | cached analysis |
| `scan_roots` / `scan_files` | root path, include/exclude; path, size, mtime, content_hash, state | incremental scanning |
| `import_jobs` | id, path, state, error, created | resumable imports, inbox states |
| `packs` | id, name, version, creator, manifest (JSON), installed | installed packs |
| `assets_fts` | FTS5 (name, tags, collections, notes, folder, pack) | search; rebuilt from the tables when missing |

Integrity: `PRAGMA integrity_check` on open after an unclean shutdown; foreign keys on; every
multi-row change in one transaction.

## Migrations

| From | To | Change |
|---|---|---|
| - | 1 | the tables above |
| 1 | 2 | `presets.trashed_from` (where a preset sent to the Library's trash came from) |
| 2 | 3 | `assets.rating` (0-5 stars, default 0) |

Each step runs in one transaction on open; a catalog newer than the build is refused and left
untouched. Tested: a version 1 catalog (made by hand) opens as the current version with its records.
The search index carries its own version (`meta.fts_version`, 2: keyed by asset rowid) and is
rebuilt from the tables when it differs.

Removing for good: `Catalog::purge` deletes only a trashed record (its tags, history and
collection places cascade); `library::emptyTrash` does it for the whole trash and moves
files nothing holds to the system Trash (docs/library/preview.md, Storage).

Per-user Library settings are not in the catalog: `<Library>/settings.json`
(schemaVersion 1: `previewGainDb`).
