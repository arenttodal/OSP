# Library full-system QA and performance (Stage 11)

What was tested, how, and with what result, on 2026-10-10. Numbers are from this
environment (a 4-core Linux cloud VM) unless they say macOS CI. Nothing here was measured on
the user's Mac or in a DAW. Those rows say so.

## 11.1 Automated coverage

| Area | Tests | Status |
|---|---|---|
| Database migrations | `[unit][library]` (a hand-made version 1 catalog migrates through 5; a newer catalog is left untouched; the search index is rebuilt) | pass |
| Asset identity | `[unit][library]` (stable ids, renames, moves), `[packs]` (identity kept through an update by uid), `[backup]` (identities kept through a restore) | pass |
| Hashing, deduplication | `[unit][library][import]` (one stored copy per content), `[packs]`, `[backup]` (no second copy on a repeated restore) | pass |
| Import transactions | `[import]` (interruption, cancel, damaged, missing), `[qa]` (40 cancellations in a row leave no partial file, no record) | pass |
| Preset and template serialisation | `[plugin][library]` (manifest 2, recall without the catalog), `[packs]`, `[backup]` (byte-identical files after restore) | pass |
| Pack import and export | `[unit][library][packs]`, `[plugin][library-packs]` | pass |
| Search | `[unit][library]`, `[qa]` (thousands of tags), `[.][library-perf]` | pass |
| Tagging | `[tagging]`, `[scan]` (sources kept apart, removals stick) | pass |
| History | `[unit][library]`, `[backup]` (history merged without repeats) | pass |
| File resolution, missing-file recovery | `[import]` (by bytes, never by name), `[plugin][library-ui]` (Missing sounds sheet, relink) | pass |
| Audio preview | `[preview]` (real-time safe: no allocation while rendering), `[plugin][audition]` | pass |
| Source loading | `[plugin][library-ui]`, `[library-capture]` (into A/B/C from the Library, the Inbox and a slice) | pass |
| Scan classification | `[classifier]` (48/48 labelled, 12/12 hard cases), `[scan]` | pass |
| Inbox import | `[inbox]`, `[plugin][library-capture]` | pass |
| Trimming, slicing | `[slicer]`, `[plugin][library-capture]` | pass |
| Backup and restore | `[unit][library][backup]`, `[plugin][library-packs]` | pass |

## 11.2 Plugin integration

| What | How | Status |
|---|---|---|
| AU | macOS CI: `auval -v aumu Osp1 Ospx` on every build | pass (CI) |
| VST3 | built and packaged on macOS CI; no VST3 validator runs | not validated |
| Standalone | built on macOS CI; launched by the user only | not tested here |
| Multiple instances | `[plugin][library][qa]`: three instances load at once and save presets into one Library: all records there, catalog intact; `[unit][library]`: 4 threads, 3 processes, a writer killed | pass |
| DAW project save and reload | headless state recall tests (sessions, presets, layers, older versions; recall with the catalog deleted) | pass (headless only) |
| Host automation | `[plugin][adaptive]` (automating every new control while notes play stays smooth and finite), `[eq]`, `[mod]` | pass (headless) |
| Preset recall | `[plugin][library]`, `[library-ui]`; rapid switching: `[plugin][library][qa]` (a preset every 3 blocks for 3 s while playing: ends on the last, finite) | pass |
| Existing modulation | the `[mod]` and `[meta]` suites, unchanged | pass |
| Sample replacement during playback | `[plugin][adaptive]` (a sound replaced while it plays finishes, a bad file never stops the others) | pass |

## 11.3 DAW testing: not done (no Mac or DAW here)

No host was tested. Nothing here claims DAW compatibility. A checklist for the first
session on the user's Mac follows. Record the host name and version for each run.

| Host (version) | Steps | Result |
|---|---|---|
| Logic Pro | insert the AU; load three Library sounds into A/B/C; save the project; quit; reopen; check sounds, macros, modulation; bounce twice and compare | to do |
| Ableton Live | the same with the VST3 or the AU; automate a macro from the Library-loaded patch; freeze and flatten | to do |
| REAPER | the same with the VST3; two instances browsing and saving at once | to do |
| JUCE AudioPluginHost | scan with the Library window open, play, look for dropouts | to do |

## 11.4 Performance (measured, this environment)

| Target (provisional) | Measured | How |
|---|---|---|
| Search feels immediate for 10,000 assets | 4.3 ms (a word over 10,000 sounds, through the Library thread); 1.6 ms (10,000) and 4 ms (50,000) raw catalog | `[.][library-qa-perf]`, `[.][library-perf]` |
| Recent Sounds opens without blocking | 0.08 ms (the recent list); the window asks on its own thread | `[.][library-qa-perf]` |
| Preview starts promptly | 47 ms from selecting a 2 s file to its preview being ready | `[.][library-qa-perf]` |
| Scanning does not cause dropouts | processBlock with 6 notes: idle mean 174 us, p99 265 us, max 446 us; during a 1,000-file scan mean 183 us, p99 290 us, max 438 us; the budget is 5,333 us per 256-sample block | `[.][library-qa-perf]` |
| Browsing does not raise audio-thread load | the output is bit-identical with the Library window open and a scan and imports running | `[plugin][library][qa]` |
| Multiple instances stay stable | 3 instances at once (above); 4 threads x 60 writes; 3 processes x 80 writes | `[plugin][qa]`, `[unit][library]` |
| Memory stays bounded during large scans | 50,000 files: peak 49 MB | `[.][library-scan-perf]` |
| Classifying | 38x real time | `[classifier]` |
| Indexing | 22,700 files/s | `[.][library-scan-perf]` |

Final thresholds belong on the user's Mac. Run the hidden cases there with
`osp_plugin_tests "[library-qa-perf]"` and `osp_tests "[library-scan-perf]"`.

## 11.5 Stress

| Case | Test | Result |
|---|---|---|
| 50,000 indexed files | `[.][library-scan-perf]` | index 2.2 s, rescan 0.85 s, 49 MB |
| Thousands of tags | `[qa]`: 3,120 tags on 600 sounds, 125 on one | search < 100 ms; lists correct |
| Long file names | `[qa]`: 255 bytes | imported, named, scanned |
| Unicode file names | `[qa]`: accents, CJK and emoji in folders and names | import, search, scan, Inbox |
| Corrupt audio | `[qa]`: random bytes, empty, truncated (a header promising more than the file holds, D-23), no channels, fake AIFF | refused with a reason; no record, no partial file |
| Huge audio | `[qa]`: 150 s | imported and classified (first 120 s) in under 30 s |
| Very short audio | `[qa]`: 1 and 10 samples | imported, classified, sliced, trimmed |
| Unsupported formats | `[qa]` | ignored by scans; refused by import |
| External drives | `[scan]` and `[inbox]`: an offline folder | missing, not failed; comes back |
| Read-only folders | `[import]` | skipped: the tests run as root, which ignores permissions |
| Full disk | `[qa]`: the store's place is a file, so nothing can be written | refused cleanly, nothing recorded; a real full volume was not filled |
| Database contention | `[unit][library]` | pass |
| Host crash, forced termination | `[unit][library]` (a writer process killed mid-write: the catalog stays intact and takes new writes); `[inbox]` (a crash mid-import goes back to ready) | pass |
| Repeated import cancellation | `[qa]` | pass |
| Rapid preset switching | `[plugin][qa]` | pass |
| Simultaneous scanning and playback | `[plugin][qa]` (bit-identical output) | pass |

## 11.6 Security

| Case | Test | Result |
|---|---|---|
| Malformed preset archives | `[plugin][security]` (`.ospinstrument`), `[packs][security]` | refused whole |
| Invalid manifests | `[packs][security]`, `[backup][security]` | refused, nothing changed |
| Relative-path traversal | `[packs][security]` (`../`, absolute, backslash), `[backup][security]` | refused |
| Oversized files | `[packs][security]` (a header claiming 3 GiB); limits per entry, in all, on the manifest | refused |
| Corrupt database records | `[qa]`: unknown origin, a junk hash, bad JSON, odd states | read safely; nothing crashes |
| Untrusted metadata | `[packs][security]`: control characters, very long text | cleaned before showing or storing |
| Invalid audio headers | `[qa]` | refused |
| Unexpected symbolic links | `[qa]`: links in scanned and Inbox folders are not followed (D-22); `[packs]`: link entries refused; `[backup]`: a linked file refuses the backup | pass |
| Unauthorised folder access | scans skip folders they may not read (`skip_permission_denied`) | not testable as root here |

## 11.7 Usability: protocol (no sessions held)

No usability session with people was held, so there are no human measurements. The table
counts the fewest actions each workflow needs in this build. The system's own response
times are those measured above. The timing column is for the user's first session.

| Workflow | Fewest actions now | System time | Timed with a person |
|---|---|---|---|
| Retrieve a recent sound | Cmd+L, Sounds, Recent, double-click | < 1 ms list, load as any | to do |
| Save a template | Save…, choose Template, name, Save | file write | to do |
| Find a bass one-shot | Sounds, Types → Bass (or type "bass") | < 5 ms | to do |
| Audition three sounds | three drags (or clicks) into A/B/C, ▶ | ≈ 50 ms per preview | to do |
| Import a phone recording | it appears in the Inbox (≈ 2 s after the copy settles); select, Import to A | ≈ 2–3 s | to do |
| Recover a missing file | ⋯ → Missing sounds, Locate…, choose the file | one hash | to do |

What to watch for in a session: where people hesitate (the Inbox and Scanned lists versus
All Sounds; Kind versus Type; the Keep mine / Newer wins question). Where the protocol
shows a problem, remove complexity rather than add it.

## 11.8 Regression: the Library does not change the sound

Two pieces of evidence:

1. **Before and after, byte for byte.** `tests/plugin/RenderRegression.inc`
   (`[.][render-dump]`) renders three scenes through the whole processor as raw float32
   stereo:
   - a pluck chord;
   - a pluck with four macros set and a change mid-note;
   - two layers.

   The same file was built into the pre-Library baseline (commit `1537f7e`, in a separate
   worktree) and into this build. **All three renders are byte-identical** (1,153,024 bytes
   each; peaks 0.19, 0.15 and 0.23, so they are not silent).
2. **The DSP code is the same.** `src/engine`, `src/audio`, `src/analysis`, `src/core` and
   `src/model` are unchanged since `1537f7e`, and the engine goldens (`tests/regression`) are
   unchanged.

There is no intentional difference. The preview engine adds its voices after the
instrument and adds nothing when it is silent.

To repeat the comparison:

```sh
git worktree add ../osp-baseline 1537f7e
cp tests/plugin/RenderRegression.inc ../osp-baseline/tests/plugin/
printf '\n#include "RenderRegression.inc"\n' >> ../osp-baseline/tests/plugin/PluginProcessorTests.cpp
cmake -S ../osp-baseline -B ../osp-baseline/build-plugin -G Ninja -DOSP_BUILD_PLUGIN=ON
cmake --build ../osp-baseline/build-plugin --target osp_plugin_tests
mkdir -p /tmp/before /tmp/after
OSP_RENDER_DIR=/tmp/before ../osp-baseline/build-plugin/apps/plugin/osp_plugin_tests "[render-dump]"
OSP_RENDER_DIR=/tmp/after build-plugin/apps/plugin/osp_plugin_tests "[render-dump]"
for f in /tmp/before/*.f32; do cmp "$f" "/tmp/after/$(basename "$f")" && echo "same: $(basename "$f")"; done
```

## Exit gate

| Gate | State |
|---|---|
| No known critical defects | none known |
| No unresolved data-loss issues | none known (every removal recoverable; restores never overwrite) |
| No known audio-thread blocking | none known (bit-identical output under Library load; allocation-free preview) |
| All required workflows verified | headless and in the real window here; **not yet in a DAW or by people** (11.3, 11.7) |
