# ANDOR release infrastructure (macOS)

`andor-release.sh --product osp` builds, tests, signs, packages, notarizes, staples and
verifies an ANDOR product, and writes its checksum, manifest, release notes and verification
report. Read `docs/release/RELEASE_RUNBOOK.md` first; one-time setup is in
`docs/release/OWNER_SETUP_MACOS.md`.

| Path | What |
|---|---|
| `andor-release.sh` | the release command (shared by every ANDOR product) |
| `signing.conf` | the team ID and the notarytool profile name (nothing secret) |
| `lib/` | the steps: signing, the installer, notarization, verification, release files |
| `templates/` | the installer's postinstall scripts |
| `products/osp/` | ANDOR OSP: `product.conf` (identity, bundles, install locations), `version.env` (the one version), `CHANGELOG.md`, the installer's pages |
| `tests/run-mock-release.sh` | the scripts' own tests, with stand-ins for Apple's tools (runs in CI) |

Certificates, keys and passwords are never in this repository.
