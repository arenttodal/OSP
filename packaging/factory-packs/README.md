# Factory packs

Put the core pack here (`*.osppack`, made in the Library with **Publish as pack…** and the
*Factory pack* box ticked). The build copies every pack in this folder into each plugin
bundle (`Contents/Resources/Factory`), and OSP installs it into `<Library>/Factory` the first
time it starts, and again whenever a newer version of the pack ships.

- Packs are not committed (`.gitignore`): they are large and their audio is licensed. Keep
  the released pack with the release (a GitHub Release asset, pinned by SHA-256), and copy it
  here before building an installer.
- With no pack here, the build is exactly as before: nothing is added to the bundles.
- Another folder: `cmake -DOSP_FACTORY_PACKS_DIR=/path/to/packs ...`.
- Only packs marked factory are installed from the bundle; an ordinary pack here is ignored.

See `docs/library/packs.md` (Publishing) and `docs/release/SOUND_PACKS_SPEC.md`.
