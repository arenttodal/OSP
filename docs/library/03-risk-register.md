# Library risk register

| ID | Risk | Severity | Mitigation | Status |
|---|---|---|---|---|
| R-01 | `.ospinstrument` import trusted entry names (path traversal outside the store) and did not check content against its hash | High (security) | Plain names only, `.partial` then rename, hash verified; `[plugin][security]` | **Fixed in Stage 0** |
| R-02 | Breaking DAW recall (state XML, parameter IDs, plugin codes) | Critical | Library state lives beside the instrument state; IDs frozen; old-state fixtures in every stage's tests | Open (guarded) |
| R-03 | Database or file work reaching the audio thread | Critical | `src/library` has no audio-thread entry point; sounds reach the engine only through the existing `ModelExchange`; review + a test that the engine target does not link SQLite | Open (guarded) |
| R-04 | Several plugin instances writing one database | High | SQLite WAL, short transactions, `busy_timeout`, no lock held while decoding; contention test (Stage 2) | Open |
| R-05 | Deleting the catalog loses audio a project needs | High | The store stays the source of truth for audio; cleanup only removes assets no preset, template or project refers to, and goes through a recoverable trash | Open |
| R-06 | The App Store / sandbox (AU in a sandboxed host) blocks folder access | Medium | Security-scoped bookmarks where available; the Library works without scan/inbox folders | Open (investigate Stage 7) |
| R-07 | One-shot classification disappoints on real material | Medium | Labelled evaluation set, reported precision/recall; manual override always wins; nothing is deleted | Open (Stage 6) |
| R-08 | Stages needing hardware or accounts cannot be verified here (iPhone, TestFlight, signing, notarization, DAWs) | High (schedule) | Build and unit-test what can be; mark the rest Blocked in the matrix with the exact user action | Open |
| R-09 | System SQLite differs between macOS versions (FTS5 availability) | Medium | Check FTS5 at startup; fall back to LIKE search; tested in CI on macOS | Mitigated: macOS 14 SDK SQLite has FTS5 (CI run 99); older macOS still to check on hardware |
| R-10 | Sample Forge is JavaScript: nothing links directly | Low | Port the small slicer algorithm with its tests (01-sample-forge-audit.md) | Accepted |
| R-11 | The approved Library mockups were not attached | Medium | Received: docs/library/mockups.md maps each panel to a view and stage | Closed |
| R-12 | Scope: a 13-stage plan cannot be finished in one sitting | High | Each stage is gated and committed; the matrix says what is verified, never more | Open |
