# ANDOR OSP 0.9.0-beta.1: release candidate record

The audit behind the first signed beta (19 November 2026), for the owner's go / no-go. The
release command's own `VERIFICATION_REPORT.md` adds what the Mac run proves.

## Source

- **Branch:** `claude/macos-release`, built from `claude/custom-arp`, which is OSP's default
  line (`claude/charming-lovelace-n25fte` @ `6d07440`) plus:
  - Custom ARP CP2–CP4;
  - one shared window fix (`e39b0bb`);
  - the CI runner option.
- **Release commit:** recorded by the release command in `release-manifest.json`
  (`source.commit`) and in the report. It is the commit the owner builds from.
- **Custom ARP CP3/CP4** (record, import, edit, keep patterns) are included at the owner's
  request (19 Nov). They were awaiting the owner's review when the beta was cut.

### Branches reviewed

| Branch | State | In the beta |
|---|---|---|
| Menus, CORE palette, menu visual refinement | merged into default | yes |
| STRUM | merged into default (#32) | yes |
| PEDAL LOOP × REIMAGINED | merged into default | yes |
| USER MACROS, Library polish, wheels, Learn | merged into default | yes |
| `claude/custom-arp` (CP2–CP4) | 11 commits ahead of default, unmerged | yes (owner's choice) |
| `claude/ceramic-cache-settle`, `claude/ceramic-skin-memory` | unmerged; 33 behind | no: resize speed and memory fix still in progress (a known issue) |
| `claude/gui-layer-ivory`, `claude/gui-macro-knob-d2` | unmerged visual experiments | no |

## Identity (verified against the release branch)

| | Value | Where |
|---|---|---|
| Bundle ID (AU, VST3, app) | `com.osp.instrument` | `apps/plugin/CMakeLists.txt` BUNDLE_ID |
| AU | `aumu` / `Osp1` / `Ospx` | PLUGIN_CODE, PLUGIN_MANUFACTURER_CODE |
| VST3 class ID | derived by JUCE from the codes above | unchanged |
| Bundle names | `OSP.component`, `OSP.vst3`, `OSP.app` | PRODUCT_NAME "OSP" |
| Manufacturer shown by hosts | "OSP" (COMPANY_NAME) | unchanged on purpose |

Unchanged by this release work:
- the parameter IDs and the state formats;
- the user library paths:
  - OSP's Library, samples and packs: `~/Library/Application Support/OSP`;
  - presets and interface settings: `~/Documents/OSP`;
  - custom ARP patterns: `~/Documents/ANDOR/Patterns`.

The release command checks the identifiers and AU codes on every build and stops on any
difference.

**Version:** 0.9.0 (it was 0.1.0), prerelease beta.1, build 1, CFBundleVersion 0.9.0.1.
- JUCE's version, the AU version and CFBundleShortVersionString become 0.9.0. Hosts rescan,
  and projects keep finding OSP by the unchanged codes.
- Factory packs compare plug-in versions numerically: packs requiring a version up to 0.9.0
  load.

## Customer build

- Release, universal arm64 + x86_64, macOS 11.0 deployment target.
- `OSP_FACTORY_AUTHORING=OFF`. The release command checks the CMake cache and stops if not.
- Installer payload: AU, VST3 and app only. ANDOR Memo Lab is built for the tests but not
  packaged (owner's decision). The release command lists every payload file and refuses
  audio files, debug symbols, keys and certificates, `.DS_Store`, Memo Lab and test tools.
- No development menus: factory publishing exists only with OSP_FACTORY_AUTHORING.
- Component and installer sizes are measured on the Mac (report).

## Factory content

| Content | State |
|---|---|
| Starting States | 7, built into the plug-in (`apps/plugin/Resources/StartingStates`). Audio-independent: no recordings, no file paths, nothing private (checked). |
| Factory packs (instruments, recordings) | **none**: `packs.lock` pins no pack. The beta ships no factory instruments; testers use their own recordings. Stated in the release notes and the known issues. |
| Fonts | Inter and Outfit, SIL Open Font License (licence texts in `shared/andor-ui/Resources/fonts`) |
| Knob sprites, ceramic skin | rendered in-house (`shared/andor-ui/Resources`, about 24 MB). Compiled into each of the AU, VST3 and app: three copies. A shared resources location could save about two thirds; later, not now. |
| Custom ARP factory patterns | nine, defined in code (`src/model/ArpFactoryPatterns.cpp`) |

## Licences the owner must confirm before external distribution

| Component | Licence | Needed |
|---|---|---|
| JUCE 8.0.9 | AGPLv3 or a JUCE commercial licence | a JUCE licence (Starter, Indie or Pro) that covers distributing closed-source plug-ins |
| VST3 SDK (in JUCE 8.0.9) | Steinberg's proprietary VST3 licence, or GPLv3 | the Steinberg VST3 licence agreement, signed, for a closed-source VST3 |
| signalsmith-stretch | MIT | none |
| Inter, Outfit | OFL 1.1 | none |

## Installation and migration

- `/Library/Audio/Plug-Ins/Components/OSP.component`, `/Library/Audio/Plug-Ins/VST3/OSP.vst3`,
  `/Applications/OSP.app`. These are the locations the earlier, never-used
  `scripts/package-macos.sh` named.
- The component packages are not relocatable. Without that, Installer would "update" an older
  copy it found in `~/Applications` instead of installing to `/Applications`.
- **Earlier unsigned test builds** were installed per user (`~/Library/Audio/Plug-Ins/…`,
  `~/Applications/OSP.app`). Both copies carry the same AU codes and VST3 class, so a host
  can load the old one.
  - The installer offers **Move earlier OSP test builds aside**, unticked, with an
    explanation on the welcome page. Ticking it is the consent.
  - It moves exactly those three paths, as the logged-in user, to
    `~/Library/Application Support/ANDOR/Previous Test Builds/<date>`. Nothing is deleted, and
    nothing else is touched.
  - `--install-test` reports any per-user copy still present.
- User data is never in a package: presets, samples, packs, patterns, preferences, MIDI
  mappings.

## Untested

The automated checks are in the report. Real hosts, a second architecture, other macOS
versions, upgrade paths and offline use are the owner's checklist in `BETA_TESTING.md`.
Nothing there is claimed until it is ticked.
