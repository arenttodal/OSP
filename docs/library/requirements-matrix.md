# Library requirements matrix

Statuses: Not Started, In Progress, Implemented, Tested, Verified, Blocked. Implemented is not
Verified. Blocked rows name what is missing.

| ID | Requirement | Stage | Location | Test | Status | Evidence | Remaining issue |
|---|---|---|---|---|---|---|---|
| NF-01 | Existing instrument features unchanged (A/B/C, One Shot, Granular, Reverse, Reimagined, macros, envelopes, LFO/ENV, modulation, EQ, ARP, MIDI) | all | - | full ctest + [ui] | Verified (baseline) | run 97; local ctest 4/4 + 5/5 |  |
| NF-02 | Existing presets and DAW recall keep working | all | PluginProcessor applyStateXml | [plugin] state tests | Verified (headless) | ctest | No DAW tested |
| NF-03 | Plugin identifiers unchanged | all | apps/plugin/CMakeLists.txt | 00-baseline-audit.md | Verified | Ospx / Osp1 / com.osp.instrument |  |
| NF-04 | Works offline, no account, no server | all | - | design review | In Progress |  |  |
| NF-05 | No file / database / hashing / decoding on the audio thread | all | src/library (no audio entry) | review + link test | Not Started |  |  |
| NF-06 | User content exportable; documented formats | 2,3,10 | docs/library/*-format.md | round-trip tests | Not Started |  |  |
| NF-07 | Never delete or overwrite user audio/presets without a recoverable operation | 2-10 | trash / soft delete | tests | Not Started |  |  |
| S0-01 | Repository and Sample Forge audit | 0 | docs/library/00, 01 | - | Verified | this folder |  |
| S0-02 | Existing state contract documented | 0 | 02-existing-state-contract.md | - | Verified |  |  |
| S0-03 | Risk register | 0 | 03-risk-register.md | - | Verified |  |  |
| S0-04 | Untrusted .ospinstrument import is safe | 0 | PluginProcessor::importInstrument | [plugin][security] | Verified | test passes | R-01 |
| S1-01 | Three asset types (preset, template, sound), origin separate from type | 1 | src/library/core | unit | Not Started |  |  |
| S1-02 | Stable asset IDs; content hash; locations separate from identity | 1 | src/library/core | unit | Not Started |  |  |
| S1-03 | Operation contracts (inputs, outputs, errors, threading) | 1 | docs/library/architecture.md | - | Not Started |  |  |
| S1-04 | Versioned schemas (database, preset, template, backup, analysis, inbox) | 1 | docs/library/schema.md | migration tests | Not Started |  |  |
| S2-01 | SQLite catalog with migrations and integrity check | 2 | src/library/storage | unit | Not Started |  |  |
| S2-02 | Full-text search (FTS5, fallback) | 2 | src/library/search | unit + 10k perf | Not Started |  |  |
| S2-03 | Content-addressed managed audio, deduplication | 2 | SampleStore + catalog | unit | Not Started |  |  |
| S2-04 | Recoverable import transaction | 2 | src/library/import | interruption tests | Not Started |  |  |
| S2-05 | File resolution order; no name-only substitution | 2 | FileResolver | unit | Not Started |  |  |
| S2-06 | Indexed external files stay in place | 2 | scanning | unit | Not Started |  |  |
| S2-07 | Multiple plugin instances share the catalog safely | 2 | storage | contention test | Not Started |  |  |
| S2-08 | Integrity: duplicates, interrupted copy, corrupt audio, missing, read-only, disk full | 2 | storage/import | unit | Not Started |  |  |
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
