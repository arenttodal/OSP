# Library requirements matrix

Statuses: Not Started, In Progress, Implemented, Tested, Verified, Blocked. Implemented is not
Verified. Blocked rows name what is missing.

| ID | Requirement | Stage | Location | Test | Status | Evidence | Remaining issue |
|---|---|---|---|---|---|---|---|
| NF-01 | Existing instrument features unchanged (A/B/C, One Shot, Granular, Reverse, Reimagined, macros, envelopes, LFO/ENV, modulation, EQ, ARP, MIDI) | all | - | full ctest + [ui] | Verified (baseline) | run 97; local ctest 4/4 + 5/5 |  |
| NF-02 | Existing presets and DAW recall keep working | all | PluginProcessor applyStateXml | [plugin] state tests | Verified (headless) | ctest | No DAW tested |
| NF-03 | Plugin identifiers unchanged | all | apps/plugin/CMakeLists.txt | 00-baseline-audit.md | Verified | Ospx / Osp1 / com.osp.instrument |  |
| NF-04 | Works offline, no account, no server | all | - | design review | In Progress |  |  |
| NF-05 | No file / database / hashing / decoding on the audio thread | all | osp_library is not linked by osp_dsp; no audio-thread entry | review | In Progress | CMake: osp_dsp has no osp_library | preview engine (Stage 4) to be reviewed |
| NF-06 | User content exportable; documented formats | 2,3,10 | docs/library/*-format.md | round-trip tests | Not Started |  |  |
| NF-07 | Never delete or overwrite user audio/presets without a recoverable operation | 2-10 | trash / soft delete | tests | Not Started |  |  |
| S0-01 | Repository and Sample Forge audit | 0 | docs/library/00, 01 | - | Verified | this folder |  |
| S0-02 | Existing state contract documented | 0 | 02-existing-state-contract.md | - | Verified |  |  |
| S0-03 | Risk register | 0 | 03-risk-register.md | - | Verified |  |  |
| S0-04 | Untrusted .ospinstrument import is safe | 0 | PluginProcessor::importInstrument | [plugin][security] | Verified | test passes | R-01 |
| S1-01 | Three asset types (preset, template, sound), origin separate from type | 1 | src/library/Catalog.h (AssetType, Origin) | [unit][library] | Tested | 8 cases pass |  |
| S1-02 | Stable asset IDs; content hash; locations separate from identity | 1 | Catalog (UUID ids, sounds.content_hash, source_locations) | [unit][library] | Tested | one content / several records; rename keeps identity |  |
| S1-03 | Operation contracts (inputs, outputs, errors, threading) | 1 | docs/library/architecture.md | - | Implemented | documented | each operation's tests land with its stage |
| S1-04 | Versioned schemas (database, preset, template, backup, analysis, inbox) | 1 | schema.md, preset-format.md, template-format.md; Catalog::migrate | [unit][library] (newer schema refused untouched, index rebuilt) | Tested | catalog v1; portable manifest v2 specified | manifest v2 written in Stage 3 |
| S2-01 | SQLite catalog with migrations and integrity check | 2 | src/library/Catalog.* | [unit][library] | Tested | WAL, busy timeout, integrity_check; a writer process killed mid-write leaves a sound catalog that takes new writes | macOS verification in CI |
| S2-02 | Full-text search (FTS5, fallback) | 2 | Catalog::search | [unit][library], [.][library-perf] | Tested | 10,000: 1.6 ms; 50,000: 4 ms (testing.md) | macOS timings |
| S2-03 | Content-addressed managed audio, deduplication | 2 | SoundImport (the existing store layout) | [unit][library][import] | Tested | one stored copy per content; one record per (content, path); 4 instances x 6 files -> 6 copies, 6 records |  |
| S2-04 | Recoverable import transaction | 2 | importSound | [unit][library][import] | Tested | .partial never a sound, cleared by age; cancel publishes nothing; copy hash-verified before rename | catalog-side `import_jobs` resume: with the Inbox (Stage 7) |
| S2-05 | File resolution order; no name-only substitution | 2 | resolveSound | [unit][library][import] | Tested | store -> seen places by hash; a same-name different recording is missing | pack content and user relink: Stages 5 and 10 |
| S2-06 | Indexed external files stay in place | 2 | scanning | unit | Not Started |  |  |
| S2-07 | Multiple plugin instances share the catalog safely | 2 | Catalog (WAL, one connection each); LibraryService (one thread per instance) | [unit][library]: 4 threads x 60, 3 processes x 80, 4 importers | Tested | all writes land, integrity ok |  |
| S2-08 | Integrity: duplicates, interrupted copy, corrupt audio, missing, read-only, disk full | 2 | importSound, Catalog | [unit][library][import] | In Progress | duplicates, interrupted, damaged, empty, missing, unsupported tested | read-only skipped here (tests run as root); disk full not simulated; crash during a transaction relies on SQLite WAL |
| S3-01 | Complete (portable) preset: manifest, state, audio, metadata, integrity | 3 | presets | round trip on a clean store | Not Started |  | existing .ospinstrument is the base |
| S3-02 | Template: state without audio or private paths | 3 | templates | unit | Not Started |  |  |
| S3-03 | Save / Save As / Save Template / Duplicate / Rename / Delete / Export | 3 | presets + GUI | unit + ui | Not Started |  |  |
| S3-04 | Loading a preset or template is recoverable (previous-state snapshot) | 3 | processor | unit | Not Started |  |  |
| S3-05 | Factory read-only; edits save as user copies | 3 | presets | unit | Not Started |  |  |
| S3-06 | Audio provenance and distribution-rights check on export | 3 | presets | unit | Not Started |  |  |
| S4-01 | Every loaded sound in history (added, last used, count, slot) | 4 | HistoryService | unit | Not Started |  |  |
| S4-02 | Sound metadata extraction | 4 | import | unit | Not Started |  |  |
| S4-03 | Independent preview engine (play, stop, gain, pitch) | 4 | src/library/preview | unit + plugin | Not Started |  |  |
| S4-04 | Load into A/B/C / first empty, never overwrite A silently | 4 | processor | unit | Not Started |  |  |
| S4-05 | Three-slot audition tray and commit | 4 | preview + GUI | ui | Not Started |  |  |
| S5-01 | Library overlay: search, type nav, origin filters, collections, results, detail, actions | 5 | LibraryOverlay | ui | Not Started |  | mockups not received (R-11) |
| S5-02 | Views A-K working, no placeholder controls | 5 | GUI | ui | Not Started |  |  |
| S5-03 | Keyboard, focus, scaling, accessibility labels | 5 | GUI | ui | Not Started |  |  |
| S6-01 | Filename / folder / audio-derived tags, separate from user tags | 6 | analysis | unit | Not Started |  |  |
| S6-02 | User-selected folder scanning: recursive, include/exclude, pause/resume/cancel, incremental | 6 | scanning | unit + perf | Not Started |  |  |
| S6-03 | One-shot classification (5 classes), evaluated on a labelled set | 6 | analysis | eval report | Not Started |  |  |
| S6-04 | Scanning never glitches audio (low priority) | 6 | scanning | stress | Not Started |  |  |
| S7-01 | Inbox folders, watching plus reconciliation | 7 | inbox | unit | Not Started |  |  |
| S7-02 | Stable-file detection, explicit import states, no false corruption | 7 | inbox | unit | Not Started |  |  |
| S8-01 | Non-destructive trim (handles, zoom, save as new) | 8 | editor | unit + ui | Not Started |  |  |
| S8-02 | Automatic trim suggestions and multi-event slicing (Sample Forge port) | 8 | analysis | unit + quality | Not Started |  |  |
| S8-03 | Derived assets with provenance | 8 | catalog | unit | Not Started |  |  |
| S9-01 | iOS capture app (record, review, trim, export, history, settings) | 9 | mobile/AndorCapture | XCTest + device | Blocked |  | needs Xcode, a device, an Apple Developer account |
| S9-02 | Phone-to-instrument end to end on real hardware | 9 | - | manual | Blocked |  | needs iPhone + Mac |
| S10-01 | Factory and third-party packs, validated, no path traversal, no code | 10 | packs | unit + fuzz | Not Started |  |  |
| S10-02 | Library backup (consistent snapshot) and restore on a clean machine | 10 | backup | clean-store test | Not Started |  |  |
| S10-03 | Storage usage and safe cleanup | 10 | backup | unit | Not Started |  |  |
| S11-01 | Full-system QA, stress, security, performance baselines | 11 | tests | suites | Not Started |  |  |
| S11-02 | DAW testing in representative hosts | 11 | - | manual | Blocked |  | no DAW in this environment |
| S12-01 | Signed, notarized installer | 12 | release | clean Mac | Blocked |  | needs Apple Developer certificates |
| S12-02 | User documentation | 12 | docs | - | Not Started |  |  |
| S1-05 | The catalog is never needed to recall a preset or project | 1 | D-02 | [plugin][library] | Tested | preset recalls its sound with the catalog deleted |  |
| S2-09 | Every sound the instrument loads is recorded (history when the user chose it) | 2 | LibraryService::soundLoaded (from the message thread, work on its own thread) | [plugin][library] | Tested | name, details, history layer; recall adds no history |  |
| S2-10 | Saved presets and templates are indexed (re-saving updates the same record) | 2 | LibraryService::presetSaved, Catalog::savePreset | [plugin][library] | Tested | preset with its sound; template with none |  |
| S2-11 | Audio memory lifecycle: buffers prepared off the audio thread, old ones kept while voices use them | 2 | ModelExchange, retired instruments (existing) | existing [unit] ModelExchange, [plugin] swap tests | Verified (existing) | unchanged by the Library |  |
