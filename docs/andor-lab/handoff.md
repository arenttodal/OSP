# ANDOR Lab — handoff report (Phrase + Grain, 0.1.0)

**Status: engineering-complete development prototypes — host validation pending.** Both
instruments build, load, analyse, play, save and reopen, pass their automated tests and
pluginval at the strictest level on Linux. Nothing has been heard, run on a Mac or opened in
Ableton yet; those steps are the user's (`test-instructions.md`). No installer was made.

## 1. Repository

| | |
| --- | --- |
| Checkout | `/home/user/OSP` — a **separately cloned cloud container**, not a worktree on the user's Mac |
| Branch | `andor-lab` (the session's pinned branch; pushed to `origin/andor-lab`; nothing merged anywhere) |
| Source revision | `50103c0` (latest OSP: "CPU step 1: bit-exact grain reads…") |
| Latest commit | see `git log -1 andor-lab` (this report is part of the final commit) |
| Drums | `origin/drum-plugin` inspected, **not merged** (its slicing/classification are not needed); its second-plug-in pattern reused |
| New dependencies | **none** (JUCE 8.0.9, Catch2 3.9.1 as before; pluginval 1.0.4 used only as an external validation tool) |
| Shared OSP code touched | one read-only accessor on `YinPitchTracker`; everything else is new files. OSP targets do not link ANDOR Lab code |

## 2. Products

**ANDOR Phrase** (`apps/phrase`, `src/andor/phrase`) — import by drag-and-drop or picker
(WAV/AIFF, background decode + analysis with progress and cancel, managed store, cache by
content + version + settings); analysis with a pYIN-inspired YIN + Viterbi tracker,
vibrato/tremolo-aware segmentation, robust pitch, attack/sustain/release refinement, tuning
rule; exact-key mapping, variations (in turn / random / shuffle, seeded), Fill Keyboard with
explicit limits (2 / 5 / 12 / 24 st, silent beyond), ONE SHOT / GATE / SUSTAIN (correlated
loops with release cross-fade, granular bloom, granular hold for unloopable regions),
in-tune / as-played tuning, keep-attack, Natural ↔ Synthetic, tone, level match, SPACE / ECHO,
4–28 voices; editor with note regions, markers, pitch / octave / plays-on / include / split /
merge / re-analyse, undo/redo, keyboard map; presets; sessions recalled sample-for-sample
without re-analysis; missing source + relink; Note Atlas export.

**ANDOR Grain** (`apps/grain`, `src/andor/grain`) — shared read-only source, 512-grain pool,
deterministic fair-share budget (ECO / STANDARD / HIGH = 8 / 12 / 16 voices, 96 / 192 / 384
grains), three windows, bounded reads, coherent stereo, band-limited oscillator with OSC ↔
GRAIN, per-voice filter, amp/aux envelopes, two LFOs, macros, 8 modulation slots, the spec's
precedence chain; KEY DNA (7 parameters, drawn curves, seeded individuality with predictable
Smooth ↔ Individual correlation, separate per-trigger and per-grain randomness); Note Atlas
sources with four policies; editor with the live grain cloud and the KEY DNA curve editor;
presets; deterministic recall.

**Note Atlas** (`src/andor/atlas`, `src/andor/io`) — schema 1, absolute source frames,
fingerprinted sources, corrections, variations, grain regions; exact JSON round trip;
validate, repair, resolve, relink (`note-atlas.md`).

Remaining limitations: `known-issues.md` (monophonic only, no formant preservation, no
imported wavetables, curves not automatable, fixed effect settings, …).

## 3. Builds

```sh
cmake -B build -G Ninja && cmake --build build && ctest --test-dir build --output-on-failure
cmake -B build-plugin -G Ninja -DOSP_BUILD_PLUGIN=ON
cmake --build build-plugin --target ANDOR_Phrase_All ANDOR_Grain_All andor_phrase_plugin_tests andor_grain_plugin_tests
xvfb-run -a ctest --test-dir build-plugin -R andor --output-on-failure
```

| Artefact (Linux, Release) | Path |
| --- | --- |
| ANDOR Phrase VST3 | `build-plugin/apps/phrase/ANDOR_Phrase_artefacts/Release/VST3/ANDOR Phrase.vst3` |
| ANDOR Phrase Standalone | `build-plugin/apps/phrase/ANDOR_Phrase_artefacts/Release/Standalone/ANDOR Phrase` |
| ANDOR Grain VST3 | `build-plugin/apps/grain/ANDOR_Grain_artefacts/Release/VST3/ANDOR Grain.vst3` |
| ANDOR Grain Standalone | `build-plugin/apps/grain/ANDOR_Grain_artefacts/Release/Standalone/ANDOR Grain` |

Missing platform builds: **macOS (AU, VST3, .app) — not built here**; Windows not attempted.
The CMake targets are the standard JUCE ones and include AU on Apple (`build-and-validation.md`).

## 4. Verification

| Suite | Result |
| --- | --- |
| OSP headless (`unit`, `integration`, `regression` goldens, `performance-smoke`) after all changes | **pass** (same as the baseline) |
| OSP plug-in suite (`plugin`) after all changes | RESULT_OSP_PLUGIN |
| `andor_tests`: atlas, phrase analysis (13 + 8 fixtures), engines, edits, interop, real-time allocation (43 cases, ~6000 assertions) | **pass** |
| `andor_tests` under AddressSanitizer + UBSan + LeakSanitizer | **clean** (the only reports are alloc/dealloc "mismatches" caused by the test's own allocation-counting `operator new`; with that check off: 0) |
| `andor_phrase_plugin_tests` (10 cases) / `andor_grain_plugin_tests` (10 cases) | **pass** |
| Plug-in tests under ASan + UBSan | RESULT_ASAN_PLUGIN |
| pluginval 1.0.4, strictness 10, VST3, in-process | **SUCCESS** (both) |
| Standalone start / SIGTERM | both start and exit cleanly (Linux, Xvfb, no audio device) |
| AU / auval, Ableton, second host, multiple instances in a host, offline bounce in a host | **pending (needs a Mac)** |

Highlights the tests prove: sessions and presets recall **bit-identically** (Phrase and Grain);
renders are identical across host block sizes (1–4096) and work at 22.05–192 kHz;
`processBlock` of both plug-ins **never allocates** under notes, pedal, bend, auditions,
modulation and KEY DNA changes; editor notes reach the audio thread lock-free; Grain reads
never leave their region; the grain budget is never exceeded and is shared fairly; the
oscillator's aliases stay > 60 dB down; Fill never transposes beyond its limit.

**Analysis accuracy** (generated ground truth): 79/79 notes on the 13 standard fixtures for
both backends; on the held-out stress set the default backend went 33/42 (round 1) → 42/42
after general fixes (round 2, no longer held out). **No real recording was evaluated.**

**CPU** (virtualised 4-vCPU Xeon, not a Mac): typical patches peak at 23 % (Grain) and 18 %
(Phrase) mean block load across 44.1/48/96 kHz × 64–512 × 1–16 voices; dense-grain stress
reaches 50–60 % at 96 kHz with 12–16 voices (`benchmarks.md`).

**Audio evaluation**: objective only — the A/B renders against baselines A and B show Phrase
playing a melody within 1 cent per note where the single-root baselines are ~330 cents off;
listening is pending.

## 5. User review

- Screens (rendered by the working plug-ins): `screens/phrase-01…05`, `screens/grain-05…08`
  — the eight states the spec asked for (`README.md` shows them).
- Demo scenarios: Phrase "Load example phrase" (generated sung phrase); Grain "Example:
  sustained tone" and "Example: sung phrase as a Note Atlas".
- Demo presets: 5 per instrument, generated with their rights-free sources on the target
  machine: `ANDOR_DEMO_DIR=~/Documents/ANDOR\ Lab xvfb-run andor_phrase_plugin_tests "[.demo-presets]"` (and Grain's).
- Test instructions: `test-instructions.md` (Standalone, Ableton checklist, listening
  renders, real-recording evaluation).

## 6. Future hybrid

What the Note Atlas already carries: fingerprinted sources, per-note regions in source frames,
attack / sustain / release markers, robust pitch with cents and vibrato apart, confidence,
articulation, the user's corrections, variation groups and an optional grain region per note.
Both engines already play from one `PlaybackSource` (tested), and Grain's MAPPED policy uses
Phrase's key mapping and variation order.

What must change for one product:
1. One processor owning one document (source + atlas + KEY DNA curves) and one model holding
   both Phrase regions/loops and Grain regions.
2. A per-voice blend: Phrase's attack (sinc read of the recorded onset) handing over to
   Grain's per-key cloud for the sustain — the SUSTAIN CHARACTER bloom is a first,
   single-voice version of exactly this hand-over.
3. KEY DNA applied to Phrase-side parameters too (attack preservation, loop/bloom balance,
   tone per key).
4. One editor combining Phrase's waveform/notes with Grain's KEY DNA tab; state schema v2 with
   a migration from either product's v1 state.

Expected complexity: moderate — the data model and both engines exist and share memory; the
work is a combined voice (2–3 weeks of engineering) plus UI and state migration (2 weeks),
then listening-driven tuning. Recommended path: keep both products; build the hybrid voice
as a third engine in `src/andor/` behind the same atlas, prove it headlessly with
`research-renderer`, then decide product packaging.

## 7. Installer readiness (not started on purpose)

Blockers before any installer:
- Gate E on a Mac (AU validation, Ableton + second host, multiple instances, save/reopen,
  offline render, audio/MIDI settings) and a listening round.
- Real-recording evaluation of Phrase's analysis.
- Visual approval of the screens (and whether to build a ceramic skin for these layouts).
- Factory content made from legally owned recordings.

Licensing questions:
- **JUCE 8** is AGPLv3 or commercial: closed-source distribution of both plug-ins needs the
  appropriate commercial JUCE licence (as for OSP).
- Inter and Outfit fonts: SIL OFL 1.1 (embedding allowed; ship the licence texts).
- VST3 SDK (via JUCE) — Steinberg's licence terms apply for VST3 distribution; AU via Apple SDK.
- No third-party analysis libraries or models are included (WORLD / Basic Pitch were assessed,
  not integrated — `phrase-analysis.md`).

Code signing and notarisation (macOS): Developer ID Application certificate for the
`.component`, `.vst3` and `.app` bundles (hardened runtime, no special entitlements — no
microphone, no network), Developer ID Installer for a `.pkg`, `notarytool` submission and
stapling. OSP's `scripts/package-macos.sh` is the template; it has never been run.

Versioning / release checklist: bump `VERSION` in `apps/phrase/CMakeLists.txt` and
`apps/grain/CMakeLists.txt`; keep plug-in codes and bundle ids; bump `stateVersion` only with
a migration; Note Atlas schema changes per `note-atlas.md`; universal binaries
(`arm64;x86_64`); full test suites + pluginval + auval on the release machine; changelog;
tag the commit.
