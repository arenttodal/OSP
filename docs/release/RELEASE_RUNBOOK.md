# Release runbook: ANDOR macOS installers

One command builds, tests, signs, packages, notarizes, staples and verifies an ANDOR product,
and writes its checksum, manifest, release notes and verification report. Every mandatory step
fails closed: when one fails, the release stops and nothing lands in the release folder.

## Make a release

On the owner's Mac (setup: `OWNER_SETUP_MACOS.md`), from a clean checkout of the release
commit:

```sh
git fetch origin && git checkout <release branch> && git pull
packaging/release/andor-release.sh --product osp --install-test
```

The command takes about an hour, mostly the build and the full regression suite. Apple's
notarization usually takes a few minutes. `--install-test` installs the finished installer on
this Mac, then runs `auval`, `pluginval`, Gatekeeper and a launch of the app. It asks once
for an administrator password.

The result is in `dist/release/<product>/<version>/`:

| File | What |
|---|---|
| `ANDOR-OSP-<version>-macOS-universal.pkg` | the signed, notarized, stapled installer (read-only) |
| `….pkg.sha256` | its SHA-256, computed after stapling |
| `release-manifest.json` | machine-readable metadata (below) |
| `RELEASE_NOTES.md` | this version's section of the product's CHANGELOG |
| `VERIFICATION_REPORT.md` | every check and its result; untested scenarios named |
| `BETA_TESTING.md`, `KNOWN_ISSUES.md`, `INSTALL_GUIDE.md` | for testers |
| `logs/` | the full log, the release record, Apple's notarization submission and log, `pkgutil`, `spctl`, `auval` and `pluginval` output, the installer's `distribution.xml`, every payload file |

The folder is written as `<version>.partial` and renamed only when everything passed.

## What it does, in order

1. **Preflight.** The checks:
   - macOS, Xcode and the Apple tools are present;
   - the working tree is clean, and the commit is recorded;
   - exactly one Developer ID Application and one Developer ID Installer certificate are found for the team;
   - the notarytool profile answers;
   - the changelog has this version's section;
   - this version's installer does not exist yet. Released installers are never overwritten.
2. **Factory content.** `packaging/factory-packs/fetch.sh` fetches the packs pinned in
   `packs.lock` and verifies each SHA-256.
3. **Build.** A fresh build folder, universal (arm64 + x86_64), Release, deployment target
   from `version.env`, `OSP_FACTORY_AUTHORING=OFF`. The CMake cache is checked against the
   customer configuration.
4. **Regression suite.** The full `ctest`, unchanged. Goldens are never updated here.
5. **Stage and check.** Each bundle is checked for:
   - its identifier;
   - CFBundleShortVersionString and CFBundleVersion;
   - its AU codes;
   - both architectures, and the deployment target of each;
   - the microphone usage text (the app only).
6. **Sign.** Inside-out: nested code first, deepest first, then the bundle. The hardened
   runtime and a secure timestamp on every signature. Entitlements only where justified: the
   app's `com.apple.security.device.audio-input`, for RECORD and GRAB. The plug-ins get none,
   because they run with their host's. Then each bundle is verified: `codesign --verify --deep
   --strict` (used to verify, never to sign), plus the identifier, team, authority, runtime flag
   and timestamp from `codesign -dv --verbose=4`.
7. **Installer.** The packages:
   - one component package per format, not relocatable, so it installs exactly at its
     location and never "updates" a copy found elsewhere;
   - a payload-free package for the optional move of earlier test builds;
   - a product archive with a welcome page, a conclusion page and selectable choices, signed
     with the Developer ID Installer certificate.

   Then the installer is expanded. Every shipped bundle must have a valid signature and the
   same CDHash as the bundle signed in step 6, so nothing changed after signing. The payload
   is listed and checked against forbidden patterns: debug symbols, audio files, keys,
   `.DS_Store`, Memo Lab, test tools.
8. **Notarize.** `notarytool submit --wait`. The submission ID and Apple's log are always
   kept. The result must be **Accepted** with no issues. Issues stop the release unless
   reviewed and allowed with `--accept-notary-issues`.
9. **Staple.** `stapler staple`, then `stapler validate`. Gatekeeper (`spctl --assess --type
   install`) must answer `accepted, source=Notarized Developer ID`.
10. **Checksum.** The installer is made read-only, then its SHA-256 and size are recorded.
    Nothing writes to it afterwards.
11. **Install test** (`--install-test`). Install with `installer`, then:
    - verify each installed bundle;
    - Gatekeeper on the app;
    - `auval -v aumu Osp1 Ospx`;
    - pluginval (pinned version, strictness 5) on the VST3 and the AU;
    - launch the app for ten seconds;
    - check for per-user copies left by earlier test builds.

    The installer's checksum is checked again afterwards.
12. **Release files.** The release notes, the manifest and the report are written from the
    release record, so they claim only what ran.

## Versions

`packaging/release/products/osp/version.env` is the only place a version is written:

```
PRODUCT_VERSION=0.9.0        # numeric; JUCE VERSION, CFBundleShortVersionString, AU version
PRODUCT_PRERELEASE=beta.1    # empty for stable
PRODUCT_BUILD=1              # only ever grows, across beta and stable
PRODUCT_CHANNEL=beta         # beta or stable
PRODUCT_MIN_MACOS=11.0
```

- The semantic version is `0.9.0-beta.1`: the installer name, the About panel, the manifest.
- CFBundleVersion is `PRODUCT_VERSION.PRODUCT_BUILD` (`0.9.0.1`), so macOS always sees each
  new build as newer.
- The plug-in's numeric version (`0.9.0`) is what factory packs compare against. A prerelease
  label is never part of it.
- **Next beta:** `PRODUCT_PRERELEASE=beta.2`, `PRODUCT_BUILD=2`, and a `## 0.9.0-beta.2`
  section in `CHANGELOG.md`. Commit, then run the command.
- **The stable release:** `PRODUCT_PRERELEASE=` (empty), `PRODUCT_CHANNEL=stable`,
  `PRODUCT_BUILD=3` (it still grows), and `## 0.9.0` in the changelog.

The identity in `product.conf` never changes: bundle ID `com.osp.instrument`, AU `aumu Osp1
Ospx`, the VST3 class ID derived from them, parameter IDs and state formats. Saved projects
find OSP by these.

## The release manifest

`release-manifest.json`: kind `andor.release-manifest`, `schemaVersion` 1.

- **Identifies:** the product (id, name, bundle ID, AU codes), the version (semver,
  marketing, prerelease, build, bundleVersion), the channel, the platform (minimum macOS,
  architectures) and the source (repository, branch, commit, clean).
- **Records the build:** date, Xcode, SDK, host, tests, factory packs.
- **Lists the artifacts:** file name, bytes, SHA-256, signing and notarization (submission
  ID, status, stapled); and each component (install path, bytes, CDHash).
- **Leaves for later:** `publication` (date and URL) is null until the owner publishes;
  `releaseReady` is false for any development run.

The manifest is built for an update service to read later: one manifest per release, grouped
by product and channel. Beta manifests are only ever offered on the beta channel. Adding fields
is compatible; changing or removing one needs `schemaVersion` 2. No updater reads it yet.

## Publishing (owner only)

Nothing is published by the scripts. To give the beta to testers:

1. Read `VERIFICATION_REPORT.md`: release-ready **yes**, every check passed.
2. Do the manual checks in `BETA_TESTING.md` (owner checklist) that the beta needs.
3. Share the `.pkg`, its `.sha256`, `INSTALL_GUIDE.md`, `RELEASE_NOTES.md` and
   `KNOWN_ISSUES.md` with the invited testers by a private link. Or run the GitHub workflow
   with **draft_release**, which makes a draft pre-release you can publish later.
4. Record the publication date and link in the release folder's `release-manifest.json`
   (`publication`).

## Keeping installers and rolling back

- **Keep:** every release folder in `dist/release/` (never committed; Git ignores `dist/`).
  Copy each published release folder to durable storage, for example a private cloud folder
  or, later, Cloudflare R2. The installer, its checksum and Apple's log are the record of
  what testers received.
- **Roll back:** give testers the previous release's installer. The component packages do
  not check versions (`BundleIsVersionChecked` false), so an older installer replaces a newer
  install cleanly. User content is never part of an installer, so it is unaffected. A DAW
  project saved with a newer version may hold settings the older version does not have, so
  tell testers to keep a copy of projects before rolling back.
- **Withdraw:** a notarized installer cannot be "un-notarized" without Apple. Revoking the
  Developer ID certificate would break every release signed with it, and is only for a leaked
  key. To withdraw a bad build, stop sharing it and ship a fixed one with a higher build
  number.

## Reproducing a release

The manifest names the commit, and `logs/release.log` has every command, with Xcode and SDK
versions. Check out that commit (`git checkout <commit>`) and run the same command with the
same Xcode. The binaries match functionally. Signatures and timestamps differ, so checksums
do not.

## Another ANDOR product

`packaging/release/` is shared. A product brings its own `product.conf` (identity, CMake
arguments, bundles and install locations, the legacy paths, the forbidden payload),
`version.env`, `CHANGELOG.md` and installer pages, and runs:

```sh
packaging/release/andor-release.sh --config <path to its product.conf>
```

ANDOR ANALOG builds ANDOR OSP's shared code as `external/osp`, so it can call
`external/osp/packaging/release/andor-release.sh --config product/release/product.conf`.

## Testing the release scripts

`packaging/release/tests/run-mock-release.sh` runs the whole flow on any machine, with stand-ins
for Apple's tools, then trips each fail-closed gate once (25 checks). Run it after changing the
scripts. It cannot test Apple's real behaviour: only a release on a Mac does.
