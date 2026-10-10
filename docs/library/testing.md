# Library testing

| Suite | Where | What |
|---|---|---|
| `[unit][library]` | `tests/library/CatalogTests.cpp` | catalog schema and reopen, identities, search (words, filters, sort, query syntax neutralised), inferred vs user tags, history, locations, newer catalog left untouched, search index rebuilt, four instances writing at once, 10,000-sound search bound |
| `[unit][library][import]` | `tests/library/ImportTests.cpp` | import transaction: stored once under its hash, details, duplicates, damaged / empty / unsupported / missing refused with nothing left, interruption and cancel, read-only store (skipped as root), four instances importing at once, resolver by bytes never by name |
| `[unit][library]` (processes) | `CatalogTests.cpp` | three writer processes at once; a writer killed mid-write (catalog intact, takes new writes). The writers are the hidden `[.][library-child]` case of the same binary |
| `[plugin][library]` | `tests/plugin/PluginProcessorTests.cpp` | a sound, a preset and a template indexed from the real plugin; recall with the catalog deleted; every load and save recorded by `LibraryService` (recall adds no history) |
| `[plugin][library]` (presets) | same | portable manifest 2 (checksums, sizes, rights, metadata), the distribution rights check (refused, named, confirmed, recorded), incomplete and newer packages refused with nothing changed; loading a preset / template / factory state is recoverable (a swap); factory content stays read-only; rename, duplicate, trash and restore keep the records |
| `[plugin][security]` | same | untrusted `.ospinstrument` packages |
| `[.][library-perf]` | `CatalogTests.cpp` | measured catalog sizes (below) |

## Measurements

Linux build machine (shared, loaded), Release, one sound per insert transaction:

| Sounds | Insert (each its own transaction) | Text search "warm bas" | Filtered (text + type + tag, recent first) | Recent 50 |
|---|---|---|---|---|
| 1,000 | 0.24 s | 0.22 ms | 0.22 ms | 0.06 ms |
| 10,000 | 2.96 s | 1.6 ms | 1.5 ms | 0.12 ms |
| 50,000 | 15.8 s | 4.1 ms | 9.3 ms | 0.22 ms |

The first measurement (before keying the search index by rowid) was quadratic: 15.5 s for
10,000 and 351 s for 50,000 inserts. Scanning (Stage 6) will batch inserts per transaction.
The macOS numbers come from CI and real hardware later; thresholds are set after that.
