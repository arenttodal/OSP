# Library decisions

| ID | Decision | Why | Revisit if |
|---|---|---|---|
| D-01 | The Library is built into the existing plugin (`OSP` identifiers unchanged); the product name ANDOR/OSP stays a display name | Spec 2.1/2.5: existing projects must find the plugin | A rename with migration is planned |
| D-02 | Keep audio out of host state; the content-addressed `SampleStore` stays the store of record for audio; the Library's catalog (SQLite) indexes it and adds metadata. The catalog can be rebuilt from the store and the presets | Projects stay small and keep working without the catalog (spec 1.6) | Users ask for self-contained projects (then: an opt-in embed) |
| D-03 | SQLite from the system: on macOS the SDK's `libsqlite3` linked by name (universal; never a Homebrew copy), elsewhere `find_package(SQLite3)`; not vendored | It ships with macOS; the environment cannot download the amalgamation; avoids a second copy in the bundle | FTS5 is missing on a supported macOS version (R-09) |
| D-04 | Port Sample Forge's slicer to C++ (with its tests) rather than reusing code directly | Sample Forge is JavaScript; the algorithm is small and tested | - |
| D-05 | New formats keep the `osp` family: `.osppreset` (unchanged, references), `.ospinstrument` (portable preset, versioned manifest), `.ospstate` (template); new: `.osppack`, `.ospbackup`. The spec's `.andorpreset` / `.andortemplate` names are not introduced | Existing files keep opening; one family of extensions | Branding decides otherwise (then: accept both) |
| D-06 | The Library code lives in `src/library` (pure C++ where possible; JUCE only for files and audio formats, like `src/io`) and `apps/plugin/Source/Library*` for the GUI | Matches the repo's layout (CLAUDE.md) | - |
| D-07 | Asset and collection IDs come from SQLite's `randomblob` (UUID form) | They are identities, not audio: CLAUDE.md rule 2 (seeded randomness) governs sound, and the C++ alternatives it forbids are avoided | - |
| D-08 | The audio store keeps its existing flat layout (`<store>/<sha256>.<ext>`) instead of the spec's `ab/cd/` sharding | Existing projects and portable instruments resolve by that layout; APFS handles 50,000 files in one folder | A measured slowdown |
| D-09 | Preset and template artwork: a generated gradient placeholder (colours derived from the asset id, stable across sessions); no images attached or shipped. Confirmed by the user 2026-10-10 | The mockups show photographs, which the plugin cannot invent; nothing is downloaded (local-first) | The user wants real artwork later |
