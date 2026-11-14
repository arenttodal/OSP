# ANDOR OSP: what is left before it can ship

_14 Nov 2026, against `claude/satellite` (the default line plus CHARACTER's live graph, the card
EQ spectrum and SATELLITE). Sources:
- the spec (`docs/spec/specification-v1.0.md`);
- `docs/product.md`, `docs/STATUS.md`, `docs/library/*`, `docs/release/SOUND_PACKS_SPEC.md`;
- `ANDOR_SHARED_PLATFORM_PLAN.md`;
- ANDOR ANALOG's release scorecard (`docs/release/READINESS_SCORECARD.md` in its repository);
- the code.

Sizes: **S** under a day, **M** days, **L** a week or more. **BLOCKER** means needed before a paid
release._

## Summary

The engine, the instrument, the Library, presets, packs and backup are built and tested headless
(all suites green). What stands between OSP and a release is mostly:
- owner decisions and credentials;
- tests on a real Mac and in DAWs;
- listening sign-offs;
- factory content;
- installer work (notices, EULA, pluginval, signing).

## 1. Presets and patch management

| Item | Status | Evidence | Size |
|---|---|---|---|
| Save / load / browse `.osppreset`, starting states, favourites, previous / next, undo of a preset load, portable `.ospinstrument` | done | STATUS Phase 8; `docs/library/requirements-matrix.md` S3-01..06; `docs/library/preset-format.md` | — |
| **BLOCKER: factory preset content.** The Core pack (50–100 presets) is not chosen or voiced; `packaging/factory-packs/` holds only a README | missing (owner: curation) | STATUS "sound packs" `[ ] Owner: choose the Core set`; `SOUND_PACKS_SPEC.md` §7 | M–L |
| **The header's preset bar does not reach pack presets.** `presetList()` lists only the starting states, `Documents/OSP/StartingStates` and `Documents/OSP/Presets`. Factory and installed-pack presets are reachable only through the Library, so the Core pack would not appear under ‹ › | missing | `apps/plugin/Source/PluginProcessor.cpp`, `presetList()` | S–M |
| Factory presets per REIMAGINED mode | missing (needs ears) | STATUS `[ ] REIMAGINED modes listening review … and factory presets per mode` | M |
| SPACE presets voiced by ear; user SPACE presets | partial | STATUS "SPACE presets": "To do: voice each preset by ear; user presets later" | S–M |
| A newer `.osppreset` or DAW state opened in an older build is not refused or warned about (only `.ospinstrument` checks); `savedVersion` is read but never compared | missing | `PluginProcessor.cpp` (`savedVersion`, `stateVersion = 14`) | S |
| Frozen 1.0 contracts: real state and preset fixtures, per-platform render hashes (ANDOR ANALOG made these a blocker) | partial (schema golden only) | `ANDOR_SHARED_PLATFORM_PLAN.md` §3, §4.2 | M |
| Dropping `.osppreset` / `.ospinstrument` / `.osppack` on the window, or double-clicking them | missing (the drop takes audio and folders only) | `PluginEditor.cpp` `isInterestedInFileDrag`; `SOUND_PACKS_SPEC.md` §6 | S |

## 2. Sample library, import, files, packs and backups

| Item | Status | Evidence | Size |
|---|---|---|---|
| Content-addressed store, atomic import, resolver and relink, catalog (WAL, FTS5), scans, Inbox, classification, slicing, preview | done (Stages 0–8, 10, 11) | STATUS "LIBRARY SYSTEM"; requirements matrix S1–S8, S10–S11 | — |
| Packs: install, update, remove, publish, factory install from the bundle, build step | done | `docs/library/packs.md` | — |
| **Backup leaves out user data.** It covers the catalog, store, presets, starting states, packs, settings and favourites, but not `Documents/OSP/Instruments` (`.ospinstrument`), saved SHAPER patterns (`.ospshaper`) or the FX racks' user presets | partial | `src/library/Backup.h` (`BackupPlaces`); `LibraryPlaces.cpp` | S |
| **User data under three roots and two brand names:** `~/Library/Application Support/OSP/…`, `~/Documents/OSP/…`, `~/Library/Application Support/ANDOR/ANDOR OSP`. Settle it before 1.0; moving it later needs a migration | decision | the same paths | S (decision) |
| Import robustness: read-only folders and a full disk not tested | partial | requirements matrix S2-08 | S |
| Disk-cache limit (spec §65, "eventually") | missing | spec §65; matrix S10-03 | M (nice) |
| "More sounds" catalogue (phase 2) and in-app store (phase 3) | post-launch by decision | `SOUND_PACKS_SPEC.md` §1, §4 | M / L |
| Library on a Mac: FTS5 on older macOS, timings, VoiceOver, DAW hosts | blocked (needs a Mac) | matrix S2-02, S5-01/03, S6-02 | human |

## 3. Sound: listening sign-offs (the DSP from the spec is built)

The spec's rule is that the listening test wins.

| Item | Status |
|---|---|
| **BLOCKER: lab round 2** (Reimagined 2, Sustain 2, Dynamics 2, Multi-sample 2); Reimagined round 1 failed | pending (STATUS Phases 5–7, NEXT, `docs/morning-checklist.md`) |
| Lab round 3 (Reimagined 3, violin) | in progress |
| Listening passes still open:<br>- granular defaults<br>- MOVEMENT v2<br>- FOLLOW off, REVERSE + LOOP<br>- the three-layer mix law and default LEVEL<br>- SPACE v2 (HALL, PLATE) and ECHO<br>- DRIVE<br>- arpeggiator defaults<br>- SYNTH Imprint<br>- Pedal Loop<br>- SATELLITE<br>- multi-sample mapping (with a real pizzicato set) | pending |
| Modulation final stages 7–10: resampling audit (+24 / +36 st), DAMPEN and modulated-ARP CPU, scenarios A–J | not done (`docs/reports/mod-final.md`) |
| Timbre clustering for organ-stop sets; Natural as the organs' default; the held-note swell on looped fading notes | owner decisions |
| Not built on purpose: harmonic and stochastic resynthesis (§20–21), pitch branch C (lost the bake-off), quality modes (§63), Polyphony in Advanced (§13) | out of scope or deferred |

## 4. Interface

| Item | Status | Size |
|---|---|---|
| **BLOCKER: first-run example sounds (spec §100).** "Load example" plays a synthetic vowel | missing (owner: content with rights) | S code + content |
| The effects rack's view state is not saved with the session (ANDOR ANALOG's is) | missing | S |
| TAPE deck reels never rest in silence: OSP does not override `InstrumentHost::outputPeak()` | partial | S |
| Review of the 869 / 1448 / 1810 px captures for the racks and faceplates | pending | S |
| Design approvals: per-layer EQ against "Option 3"; the modulation panel against its mockups | owner | — |
| The ceramic skin and rendered knobs judged on a Mac | human | — |
| About box / visible version; parameter groups for hosts (846 flat parameters) | nice | S / M |
| On-screen keyboard: `MidiKeyboardState`'s lock on the audio thread bends Rule 1 | known | S–M |

## 5. Platform and release

| Item | Status | Size |
|---|---|---|
| **BLOCKER: identity.** `COMPANY_NAME "OSP"`, `BUNDLE_ID com.osp.instrument`, codes `Ospx` / `Osp1`, product name. Permanent after the first public build | owner decision | S |
| **BLOCKER: version 0.1.0.** It feeds the plugin version, `.ospinstrument`'s `engineVersion` and packs' `requiresPlugin`, so bump it before publishing the Core pack | missing | S |
| **BLOCKER: signed and notarised installer.** `scripts/package-macos.sh` exists and has never been run; CI ships an unsigned test zip | needs Developer ID | S + a clean-Mac test |
| **BLOCKER: third-party notices and EULA in the installer** (JUCE, VST3 SDK, signalsmith-stretch, SQLite, the OFL fonts); ANDOR ANALOG's `packaging/common/` is the template | missing | S–M |
| **BLOCKER: licences** (JUCE 8 commercial, Steinberg VST3); "ANDOR" trademark clearance | owner | — |
| **BLOCKER: pluginval** (AU and VST3; OSP CI runs `auval` only) | missing | S |
| **BLOCKER: DAW host matrix** (spec §74): Logic, Live, Reaper, then Cubase and Studio One. Recall, bounce determinism, automation, sample-rate and buffer changes, freeze, live-capture routing, MPE | needs a Mac | M (human) |
| Real-Mac CPU and worst-case callback timing; the GUI at 60 fps | unverified | S (human) |
| Windows: CI is macOS-only now; SQLite comes from `find_package`; no Windows installer or Authenticode | blocker only if Windows ships at launch (owner) | M |
| A CI installer artefact; the Core pack's release asset with its SHA-256 | missing / documented | S |
| Purchase and delivery: store page, VAT, download limits, price or "free beta for ANDOR ANALOG buyers" | owner | — |
| Scope: Memo Lab app; AAX (out of scope) | owner | — |

## 6. Documentation

| Item | Status | Size |
|---|---|---|
| **The user guide** lacks NOTE MAP / MAP NOTES, KEY DNA, Pedal Loop, FX routing and racks, ABYSS / VINTAGE / BLOOM, ASCEND, DAMPEN and SPACE presets, and still describes the synthetic example (multi-sample mapping and SATELLITE are now in) | partial | M |
| `README.md` ("Phase 0") and the STATUS header are stale; some STATUS "DONE" and "KNOWN ISSUES" lines contradict later work (CI platforms, test counts, SIMD sinc, multi-file drops) | stale | S |
| The Library requirements matrix is out of date (S12-02, S5-02) | stale | S |

## 7. Parity with ANDOR ANALOG (CLAUDE.md's shared SPACE / ECHO / ARP rule)

| Item | Status |
|---|---|
| Per-card arpeggiator; effects rack, enamel faceplates, TAPE deck; **SATELLITE** | done (SATELLITE: 14 Nov) |
| **MAIN + SEND routing** (ECHO and SPACE as a wet send, route schema 2) exists only in ANDOR ANALOG's own FX copy | gap, plan needed |
| `arpCompanion`, the MOVEMENT level-gain trace and ANDOR ANALOG's other opt-in hooks live on OSP's `feature/satellite-rhythm` (ANDOR ANALOG's pin). The lines are drifting apart again | line split; reunite before release |
| ANDOR ANALOG's card EQs can show the spectrum once its host returns its engine's taps; ANDOR ANALOG can use the shared SATELLITE panel | open (ANDOR ANALOG side) |

## 8. Contradictions to settle in the documents

1. The spec and `docs/product.md` say five macros, no modulation matrix, no granular settings,
   no oscillator pages and "not an effects rack". The product has six macros, per-layer
   REIMAGINED, a 16-route modulation panel, a SYNTH page, an arpeggiator and FX routing. Record
   the decision, or update both.
2. `SPACE_ENGINES_PLAN.md` says ABYSS's variants are both built and "not built yet" (they are built).
3. STATUS NEXT still lists the per-card arpeggiator in OSP (done).
4. STATUS Phase 8 says "Windows VST3: CI builds it"; CI is macOS-only.
5. Spec §54 `.instrumentname` and the library spec's `.andorpreset` against the shipped
   `.ospinstrument` / `.osppreset` (intentional, D-05; settle it with the branding).

## Shortest path to a macOS 1.0

1. **Owner:**
   - identity and IDs, version 1.0.0;
   - company and EULA, licences, Developer ID;
   - Mac-first or Windows too;
   - price or free beta;
   - the Core pack and the example sounds (rights).
2. **Engineering (about 1–2 weeks):**
   - notices and EULA in the pkg;
   - pluginval and an installer artefact in CI;
   - pack presets in the header's preset bar;
   - drop or double-click to install presets and packs;
   - backup of Instruments, SHAPER patterns and FX presets;
   - rack view recall and `outputPeak`;
   - the newer-state warning;
   - frozen 1.0 fixtures;
   - the user guide and the stale docs;
   - MAIN + SEND parity and reuniting the lines with ANDOR ANALOG.
3. **People:** lab rounds 2 and 3 and the listening passes, the DAW matrix on a Mac, and a
   signed and notarised clean-install test.
