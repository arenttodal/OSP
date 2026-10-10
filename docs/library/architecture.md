# Library architecture

The Library makes sounds, presets and templates easy to find, preview and reuse, without
changing how the instrument plays or how projects are saved. It is local-first: one SQLite
catalog and the existing content-addressed sample store, both on this computer.

## The three asset types

| Type | What it is | Audio | File |
|---|---|---|---|
| **Sound** | one audio asset: a loaded file, an import, a capture, a slice | the asset itself (managed copy, or indexed in place) | `<store>/<sha256>.<ext>` |
| **Preset** | a complete instrument: every setting plus the sounds it needs | referenced by content hash (or packed, when portable) | `.osppreset` (references), `.ospinstrument` (portable, sounds inside) |
| **Template** | a complete instrument without sounds | none, and no paths to private recordings | `.ospstate` |

**Origin** is a separate field: `factory`, `user`, `pack`, `captured`, `external` (indexed in
place). "Factory template" or "captured sound" are combinations, not classes.

## Identity

| Concept | Field | Notes |
|---|---|---|
| Asset identity | `assets.id` (random UUID) | stable for the life of the record; survives renames and moves |
| Content identity | `sounds.content_hash` (`sha256:<hex>`) | what presets and projects refer to; several sound records may share one (different names, collections) |
| Managed location | the store file named by the hash | written once, never changed in place |
| Original / indexed locations | `source_locations` | where the bytes were found; never used to substitute a different file by name |
| Display name, collections, tags, notes, favourite | `assets`, `asset_tags`, `collection_items` | user metadata; never inside the audio file |

A preset's state XML already refers to its sounds by content hash. The catalog records the
dependency (`preset_sounds`) so cleanup never removes a sound something still needs.

## Modules

```
src/library/                 (osp_library: C++20; SQLite; std::filesystem; JUCE only through src/io)
  Catalog        the database: open, migrate, integrity check, WAL, transactions
  AssetTypes     Asset, SoundInfo, PresetInfo, Tag, Collection, History ... (plain structs)
  SoundRepository / PresetRepository / CollectionRepository / HistoryService
  SearchService  FTS5 over names, tags, collections, notes, folders, packs (LIKE fallback)
  ImportService  the import transaction (validate, hash, stage, verify, publish, commit)
  FileResolver   managed store -> pack content -> original path -> indexed folders -> relink
  Analysis       metadata, tags from names and folders, one-shot classification, slicing
  Scanner        user folders, incremental, pausable (Stage 6)
  Inbox          watched folders, stable-file detection (Stage 7)
  Backup / Packs (Stage 10)
apps/plugin/Source/Library*  (GUI: overlay, browser views, inspector, audition tray, editors)
src/library/preview          (the preview voices, fed by the plugin's audio callback; Stage 4)
```

Rules (CLAUDE.md 1, 3, 6): the catalog never runs on the audio thread and is not linked by
the engine (`osp_dsp`); sounds reach the engine only through the existing loader and
`ModelExchange`. The Library service does not depend on GUI widgets.

## Threads

- **Message thread**: GUI; it calls Library operations that return immediately with a job, or
  short reads (a page of search results) that touch only the catalog.
- **Library worker** (one `juce::ThreadPool` per process, low priority): imports, hashing,
  decoding, analysis, scanning, inbox. Results return to the message thread
  (`MessageManager::callAsync`).
- **Audio thread**: never touches the Library. The preview engine reads only prepared
  buffers handed over by atomic pointer swap (like `ModelExchange`).
- **Database**: one connection per thread, WAL, `busy_timeout` 5 s, short transactions;
  nothing is decoded or hashed inside a transaction. Several plugin instances in one host (or
  several hosts) share the catalog this way.

## Operation contracts

| Operation | Input | Output | Errors | Cancel | Thread | Guarantee |
|---|---|---|---|---|---|---|
| importSound | file, origin, collection? | sound asset id | unreadable, unsupported, disk full, permission | yes (before publish) | worker | all-or-nothing; a crash leaves only a `.partial` that the next start removes |
| indexExternalSound | file | asset id (external, not copied) | unreadable | yes | worker | file stays in place |
| findSound | content hash | resolved file or "missing" with candidates | - | - | any (no audio) | never substitutes by name |
| searchAssets | text, type, origin, tags, collection, sort, page | page of results | - | - | message (fast) | read-only |
| loadSoundIntoSource | asset id, layer A/B/C or first empty | load job | missing, decode | yes | worker -> loader | previous state recoverable (undo) |
| loadPreset / loadTemplate | asset id or file | state applied | unreadable, missing sounds (listed) | - | message (+ loader) | previous state recoverable |
| savePreset / saveTemplate | name, type, category, tags, collection, notes | asset id + file | disk full, permission, name taken | - | message (small file) | written to `.partial`, renamed |
| exportPortablePreset | preset, rights confirmation | `.ospinstrument` | missing sound, rights unconfirmed | yes | worker | validated after writing |
| previewSound / auditionCombination | asset ids | playing | missing, decode | stop | worker (prepare) + audio (play) | never changes the instrument |
| scanFolder | root, include/exclude | scan job, progress | permission, vanished | pause/resume/cancel | worker | incremental, resumable |
| processInboxItem | file | inbox state | not yet stable, unavailable | yes | worker | never marks a downloading file as corrupt |
| trimSound / sliceRecording | asset, ranges | new derived assets | - | yes | worker | the original is never changed |
| backupLibrary / restoreLibrary | target | archive / restored catalog | disk full, conflicts | yes | worker | consistent snapshot (`VACUUM INTO`), restore never overwrites newer user data silently |

Errors are values (`Result { ok, message, code }`), never exceptions across the boundary,
and say what to do ("Reconnect the drive", "Choose the file").

## State versus catalog

The plugin's state (host project, preset, template) is self-sufficient with the sample
store: it never needs the catalog or the browser. The catalog is an index plus user metadata,
rebuildable from the store, the preset folders and the packs. Deleting the catalog loses tags
and collections, never a sound a project needs (D-02).

## Versions

| What | Where | Version |
|---|---|---|
| Catalog schema | `meta.schema_version` | 1 (migrations in `Catalog::migrate`) |
| Preset / template | the state XML's `stateVersion` | 10 (unchanged) |
| Portable preset | `.ospinstrument` `manifest.json` `schemaVersion` | 1 -> 2 (adds metadata, provenance, checksums; 1 still reads) |
| Library backup | `.ospbackup` manifest | 1 |
| Analysis | `audio_analysis.version` | per analyser |
| Inbox manifest | `inbox_items` | 1 |
