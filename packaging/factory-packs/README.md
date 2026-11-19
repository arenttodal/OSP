# Factory packs

Put the core pack here (`*.osppack`, made in the Library with **Publish as pack…** and the
*Factory pack* box ticked). The build copies every pack in this folder into each plugin
bundle (`Contents/Resources/Factory`), and OSP installs it into `<Library>/Factory` the first
time it starts, and again whenever a newer version of the pack ships.

- Packs are not committed (`.gitignore`): they are large and their audio is licensed. The
  released pack is a GitHub Release asset, pinned by SHA-256 in `packs.lock`:
  `<release-tag> <asset-file-name> <sha256>`. CI runs `fetch.sh` before configuring, which
  downloads each listed pack here and verifies it. Run it locally the same way
  (`bash packaging/factory-packs/fetch.sh`, with the GitHub CLI logged in), or copy a pack here
  by hand.
- To ship a new Core pack:
  1. publish it in the Library;
  2. attach it to a release (`gh release create packs-core-1.0.0 ANDOR-Core-1.0.0.osppack`);
  3. put its line in `packs.lock` (`shasum -a 256 ANDOR-Core-1.0.0.osppack`).
- With no pack here, the build is exactly as before: nothing is added to the bundles.
- Another folder: `cmake -DOSP_FACTORY_PACKS_DIR=/path/to/packs ...`.
- Only packs marked factory are installed from the bundle; an ordinary pack here is ignored.

See `docs/library/packs.md` (Publishing) and `docs/release/SOUND_PACKS_SPEC.md`.
