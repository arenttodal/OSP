# Status

_Last updated: Phases 0–2 complete; Phase 3 (continuation) implemented and in the plugin, listening test pending; overnight run working through Phases 4–8._

## Milestone checklist — Phase 0: research harness + baseline engine

- [x] P0-01 Repository, CMake (no Projucer), JUCE 8.0.9 via FetchContent, Catch2 tests, docs
- [x] P0-02 Headless `research-renderer` (`--analyze`, `--source/--midi/--output`, `--analysis`, clear errors + exit codes)
- [x] P0-03 Corpus indexing (SHA-256 content ids, duplicates, unsupported files listed)
- [x] P0-04 Audio loader: WAV/AIFF/AIFF-C, mono/stereo(+), 44.1–96 kHz, 16/24/32-bit PCM, 32-bit float; non-destructive
- [x] P0-05 MIDI fixtures: repetition, dynamics, register, melody, chords, long-hold, long-chord, repeated-sustains (+ profiles)
- [x] P0-06 Baseline polyphonic sampler (A) + naive randomised sampler (B)
- [x] P0-07 Root/F0 analysis (YIN, weighted median, honest confidence, vibrato)
- [x] P0-08 Envelope/onset analysis (silences, onset, attack, peak, body slope/fluctuation, tremolo, secondary peaks, end state)
- [x] P0-09 Spectral analysis (centroid, rolloff, flatness, flux, periodicity, harmonic ratio, HF/LF energy + series)
- [x] P0-10 Stereo analysis (correlation, width, side/mid, balance, band widths)
- [x] P0-11 Versioned analysis JSON (documented in docs/analysis-schema.md)
- [x] P0-12 Corpus runner (profiles, engines, parallel jobs, failure isolation, summary.md/json)
- [x] Golden render regression system (8 cases) + render safety checks
- [x] Benchmark (`--benchmark`) + CTest performance smoke test
- [x] Run the real 80-file corpus: 80/80 analysed, 800 renders, 0 errors (`docs/reports/corpus-run-1.md`)
- [x] Optional minimal standalone/plugin (see Phase 1 below)

## Milestone checklist — Phase 1: playable sampler

- [x] JUCE plugin target: AU (macOS) + VST3 + Standalone, CMake only
- [x] Drag & drop / Load… / Load example (synthetic vowel until owned examples exist)
- [x] Background loading: hash → managed sample store → decode → analysis (cached) → playback data
- [x] Lock-free instrument hand-over to the audio thread with deferred destruction (`ModelExchange`)
- [x] Root detection shown; manual root override (pitch offset, no rebuild)
- [x] Waveform overview, character line ("SUSTAINED · TONAL · VIBRATO"), keyboard, voice count
- [x] MIDI: note on/off, velocity, sustain pedal, pitch bend (range parameter), all-notes-off
- [x] 24 voices, ADSR (attack/release automatable), velocity range, fine tune, output gain
- [x] Session state: parameters + sample hash/name/path + root override; recall from the sample store
- [x] Headless plugin tests (load, chromatic pitch, SR changes, root override, recall, bad files)
- [x] macOS 14 / Apple Silicon: build, all tests, `auval` (CI)
- [ ] Host validation: pluginval, Logic / Ableton / Reaper (needs a Mac)
- [x] Playback preparation: start at the analysed onset + level matching (plugin default; research option)
- [x] Session recall stores the exact playback model (bit-identical recall even if analysis changes)
- [ ] Exit check with real corpus samples in a DAW

## Milestone checklist — Phase 2: pitch engine bake-off

- [x] Offline pitch engines: A resampling, B Signalsmith Stretch 1.4.0 (MIT), C Signalsmith + formant compensation
- [x] Bake-off runner (`--bakeoff plan.json`): blind clips, common length, RMS-matched, `key.json` guard rails
- [x] Scoring (`--bakeoff-score dir --ratings file`): per engine / family / offset band
- [x] Run 1 rendered: 5 families x 5 offsets x 3 engines = 75 clips (`docs/reports/pitch-bakeoff-1.md`)
- [x] Blind listening page (`research/bakeoff/listening-page/`), ratings stored with the page
- [x] Listening ratings collected and scored (`research/bakeoff/pitch-bakeoff-1.ratings.json`)
- [x] Decision: A default, C rejected, B as "Natural" register anchors (`docs/reports/pitch-bakeoff-1.md`)
- [x] Natural pitch branch (offline register anchors, stage 3 of the model) in the plugin ("Pitch Character")

## Milestone checklist — Phase 3: continuation MVP

- [x] Sustain-region detection (one-shot for decaying/struck sources)
- [x] Loop candidates scored on spectrum, level + slope, F0 + slope; aligned by cross-correlation
- [x] Multi-loop random walk (never repeats immediately), correlation-aware crossfades
- [x] Movement: slow level / pitch / brightness drift (MOTION)
- [x] Release grafting into the recording's own ending
- [x] Experiment 2 (A naive loop / B best loop / C multi-loop / D + movement), 60 s holds, 5 sources
- [x] InstrumentEngine ("engine C") in the plugin; staged model (playable -> sustain -> anchors)
- [x] Listening verdict round 1: loop mostly inaudible (multi-loop 4.0–5.0 on non-vocal sources); movement now follows the source, re-test "Sustain 2"

## Milestone checklist — Phase 4: performance MVP

- [x] Performance vector (level, brightness, body, transient, micro-pitch + settle, damping, start, stereo)
- [x] Correlation through latent factors (force, colour, timing) — not independent draws
- [x] Deterministic variation (seed + event sequence); transport start resets memory in the plugin
- [x] Performance memory (Ornstein-Uhlenbeck, 4 s)
- [x] Repetition awareness (alternating attacks, less pitch drift on fast repeats)
- [x] LIFE macro (0 identical, 0.5 realistic, 1 reinterpreted)
- [x] Calibration against the corpus' real repeated takes (Tagel, plucks; violin RR set has only 2 takes)
- [x] Experiment 3 (A identical / B independent / C LIFE 50 % / D LIFE 80 %), steady + fast repeats
- [x] Listening verdict round 1: **passed**, LIFE 50/80 % beat identical repeats on every non-vocal source (default now 50 %)

## Milestone checklist — Phase 5: dynamic synthesis

- [x] velocity -> intensity (velocity 100 = as recorded; soft range wide, hard range narrow)
- [x] velocity -> transient, spectral tilt (high shelf), attack bite (decaying shelf), body, pitch transient, damping, soft attack
- [x] DYNAMICS macro (0 = velocity is volume only; 0.5 calibrated; 1 = twice)
- [x] Experiment 4 (A gain / B gain + filter / C model 50 % / D model 100 %), 5 sources
- [~] Listening verdict round 1: plucks yes (model 100 % best), sustained sources preferred gain only; source-aware strength now, re-test "Dynamics 2"
- Known limit: near-sinusoidal sources (some vocals, soft plucks) have no upper harmonics to
  brighten; velocity then acts mainly on the attack. Harmonic generation belongs to Reimagined.

## Milestone checklist — Phase 6: Original <-> Reimagined and the remaining macros

- [x] Multiple pitch branches (Tape/Natural) + continuation depth (segment length) by Reimagined
- [x] Spectral-envelope transformation: CHARACTER moves the source's own body resonances + tilt
- [x] Resonance reconstruction: sympathetic resonator bank (source partials + body peaks)
- [x] Harmonic manipulation: soft saturation towards the Reimagined end
- [x] MOTION: drift depth/rate, stereo motion, jump rate; nearly inactive on plucks
- [x] SPACE: width, decorrelation for narrow sources, FDN ambience, loudness trim
- [x] Macro smoothing (no clicks when automating)
- [x] Experiment 5 (Reimagined 0/25/50/75/100 %, 4 sources)
- [ ] Listening verdict round 1: **failed**, the range was inaudible; far end rebuilt, re-test "Reimagined 2"
- [x] Transient/body separation (spec §19): offline HPSS of the onset region
  (`analysis/transient`); from about a fifth away the separated transient plays at its own
  speed, aligned on its peak, while its transposed copy is removed from the body.
  Synthetic picked tone two octaves down: pick 16 ms -> 4.5 ms (original 4.5 ms).
  Lab round 1: lost on pluck, bowed and violin, so it is **off by default** (`"transientPreservation"` in render configs).
- [x] Experiment 7 (`transients-1.json`: pluck, Tagel, nyckelharpa, violin at ±12/±24, off vs on)
- [x] Listening verdict round 1: lost on 3 of 4 sources, off by default

## Milestone checklist — Phase 7: multi-sample intelligence

- [x] Group import (multi-file drop, folder drop, multi-select chooser; sessions store every member)
- [x] Pitch clustering (roots within half a semitone)
- [x] Round-robin inference + engine rotation that never repeats the previous take
- [x] Velocity inference (dynamics words in names, else loudness gaps >= 4.5 dB); one shared set gain keeps natural level differences
- [x] Alternate articulations (much shorter/longer takes) kept but not used for ordinary notes
- [x] Register model (brightness vs pitch, shrunk when few pitches); ground truth: plucks 6.2 -> 4.6 st error, organ 5.2 -> 4.8, tagel 3.0 -> 3.1 (neutral)
- [x] Multi-velocity learning (spec §35): per-layer-step loudness, centroid and attack fitted over groups with several layers; velocities between layers move half-way towards the neighbouring layer, so a layer boundary no longer jumps (synthetic soft/hard pair: 13.4 dB / 12 st step -> 1.9 dB / 1.3 st at the boundary). Corpus: plucks 9.6 dB + 2.0 st per step, Tagel 7.3 dB + 0.9 st, organ 11.7 dB + 4.9 st (its "layers" are mostly different stops). `--register-test` prints it
- [x] Samples inspector (role, layer, root corrections; rebuild without re-analysis; recall)
- [x] Experiment 6 (single file plain / single file engine / whole set)
- [~] Listening verdict round 1: Tagel set **passed** (4.33 vs 2.67), plucks set failed on level jumps, fixed, re-test "Multi-sample 2"
- Known limits: sets mixing different organ stops are grouped by pitch only (timbre clustering
  is not done); register anchors are not built for sets.

## Milestone checklist — Phase 8: commercial polish (what can be done without a Mac/certificates)

- [x] Starting states (Natural, Alive, Floating, Broken, Frozen, Dream, Wide) as host programs + selector
- [x] Presets (.osppreset) and portable instruments (.ospinstrument: sources + analysis + settings; bit-identical on another machine)
- [x] Undo/redo: sample loads (incl. sets and stages), root changes; parameters via the APVTS undo manager
- [x] UI scaling 80–200 %, accessibility titles on controls
- [x] Primary screen per spec §10/§13: preset name shown; Advanced panel collapsible, closed by default, remembered per session
- [x] MIDI: mod wheel, aftertouch/pressure, CC74, CC 20–25 -> macros; MPE lower zone (bend/pressure/slide per note)
- [x] Windows VST3: CI builds it and runs every test with MSVC
- [x] Performance tuning: polyphase tables for unity and stretched reads (semitone grid up to x4); engine C costs about two-thirds of the plain baseline
- [x] Host robustness matrix (plugin test): 22.05–192 kHz × blocks 1–4096 and host-varied blocks; bit-identical output, finite, click-free, chord ±2 octaves + bend
- [x] Memory (spec §65): recordings over 60 s skip register anchors (Natural plays as Tape, the status line says so). A 5-minute stereo file peaks at 380 MB in the renderer instead of 781 MB; files over 10 minutes are truncated with a warning
- [x] Crash-safe sample management (atomic store copies, analysis cache re-derived when stale, original-path fallback)
- [x] User guide (docs/user-guide.md)
- [~] Installer/signing/notarisation: `scripts/package-macos.sh` written, **not run** (needs a Developer ID)
- [ ] Factory example sounds (need legally owned recordings — see morning checklist)
- [x] Preset browser: ☰ → Presets / Instruments list `Documents/OSP/{Presets,Instruments}` (sub-folders = sub-menus), previous/next preset, Show folder
- [ ] Host certification matrix (Logic, Ableton, Reaper, Cubase, Studio One) — needs a Mac
- [x] Onboarding: three-step empty state (drop, play and hold, shape) + "Load example"; real example sounds still pending (§100)

## Milestone checklist — Shaping system v1.0 (five-macro popups) and UI redesign

- [x] FX-01 popup framework: a macro's name opens its mini panel (one at a time, Escape / outside click closes, hover underline, dot when customised, double-click resets)
- [x] FX-02 CHARACTER: per-voice LP24 ladder, LP12, HP12, BP12, Tilt; MIN/MAX (reversible), RES, DRIVE, AD envelope (ENV/ATTACK/DECAY)
- [x] FX-03 DYNAMICS: velocity curve, attack, release (20 ms–15 s), TONE couples velocity into CHARACTER
- [x] FX-04..07 MOVEMENT: DRIFT (per voice + shared wander), TAPE, CHORUS, PULSE; 25 % and 50 % clearly differ (test)
- [x] FX-08 SPACE: ROOM, CHAMBER, PLATE, SPRING with DECAY; type changes crossfade
- [x] FX-09 LIFE: NATURAL / LOOSE / FRAY, PITCH / TONE / ATTACK spreads, repeat guard
- [x] FX-10 stable parameter IDs (`life.*`, `dynamics.*`, `character.*`, `movement.*`, `space.*`), state version 3 with migration, preset/session recall test, experiment `shaping` block
- [x] UI redesign: housing, header, charcoal display with time grid, cream knobs, ivory keyboard, status light; Barlow embedded (SIL OFL)
- [x] Reimagined far end (violin benchmark): granular continuation with octave/fifth/sub-octave remapping and pitch jitter takes over the sustain from ~45 %, wandering formants (spectral evolution) from 40 %
- [ ] Listening round 3: `research/experiments/reimagined-3.json` (violin)

## Milestone checklist — GUI finalisation, A/B layers, Granular mode MVP

- [x] Header: pitch, title, description; only the starting state and ☰ on the right. Load, Load example, Samples, Root and Clear live in ☰ (context: the edited layer)
- [x] Display: A/B tabs (top left), equal-power blend (top right), layer + file name (bottom left), ONE SHOT / GRANULAR (bottom right), granular overlay POS SIZE DENS TUNE SPREAD (Granular only), POS line and SPREAD band on the waveform
- [x] Advanced under the right side of the keyboard
- [x] Engine: two layers, per-layer model/set/root/mode, blend before the shared post stage; per-voice Granular source (pool of 24 grains, Hann, cubic, seeded); sustain while held, grains finish after note-off
- [x] Plugin: per-layer loading/recall/undo, stable IDs `ab.blend`, `layerA|B.sourceMode`, `layerA|B.granular.{position,size,density,tune,spread}`; state version 4 (older sessions = layer A, One Shot); POS starts in the analysed sustain
- [x] Tests: blend, empty layer, granular sustain/pitch/TUNE/level, note-off, polyphony with per-layer modes, block-size independence, per-layer load/recall (bit-identical), clear, old sessions
- [ ] Listening pass on granular settings (defaults SIZE 150 ms, DENS 14/s, SPREAD 20 %)
- [x] Display: live grain cloud (Granular) and One Shot read heads, lock-free from the audio thread

## Next edition (stacked for one build; the user is collecting test notes)

- [x] LIFE in Granular: per-note POS/SIZE/DENS/SPREAD/TUNE variation (test: repeated notes, LIFE 0 vs 50 vs 100 %)
- [x] MOVEMENT v2: five modes with their own settings (state version 5 migrates the old shared knobs), host timing snapshot, SHAPER (12 patterns, 6 rates, VOL/FILTER/BOTH, SMOOTH), popup with pattern strip and live playhead; tests: sync from PPQ 0/4/16/37.5, block-size independence, bypass, targets, smooth, click-free changes, stopped transport, no drift over 10 min, recall/migration, host play head through processBlock
- [x] Fix: choosing a SHAPER PATTERN or RATE from its menu crashed the host (Ableton Live 12). The outside-click watcher closed the popup on the menu click and deleted the selector before its choice arrived; now only presses inside the editor close a popup, and menus never call into a deleted control
- [ ] MOVEMENT v2 listening pass (MV-12): patterns on the corpus (organ, nyckelharpa, vocal, synth, granular, A/B); remove or retune any that sound gimmicky

## Milestone: GUI redesign, macro visualisations, adaptive 1–3 layers (see docs/redesign-notes.md)

- [x] Stage 0: audit, rollback tag (local), reference renders of engine C
- [x] Stages 1–3: three engine slots, layer C, START/TUNE/PAN/LEVEL, REVERSE/LOOP/FOLLOW, mixWeights (unity / equal-power A/B / constant-power triangle), silent layers not rendered, musical voice count; single and A/B renders bit-identical to before
- [x] Stages 4–6: EngineCard (Hero/Dual/Triple), adaptive layout 0/1/2/3, drag & drop (ADD LAYER preview, REPLACE X, loose files one layer each, folder one multi-sample layer, max three, the rest reported)
- [x] Stage 7: new visual system (palette, Inter with tabular figures, graphite displays, spectral waveforms, layer identities), header (preset bar, favourites, volume, menu), mix band, macros + amp envelope, keyboard and wheels, Advanced slimmed
- [x] Stages 8–9: LINK, REVERSE, LOOP, FOLLOW; the instrument's ADSR (decay/sustain added; live sustain glide)
- [x] Stages 10–15: popup shell; SPACE, CHARACTER, MOVEMENT, LIFE, DYNAMICS visualisations from the DSP's own formulas
- [x] Stage 16: state v6 + migration (v5 single and A/B sessions bit-identical; old Sustain -> LOOP)
- [x] Stages 17–19, 21–23: polish at min/max sizes, all 8 One Shot/Granular combinations, modifier regression under automation, automation of every new control, edge cases (replace/remove while playing, bad file among good ones, too-short sources); ASan/UBSan runs
- [x] Stage 24: acceptance — full core and plugin suites (incl. UI) clean under ASan/UBSan after fixing a use-after-free when an instance closes mid-load; CPU measured for 1/2/3 layers; final report in docs/redesign-notes.md
- [x] REVERSE keeps Reimagined (doubling head, grains and continuation walk run backwards)
- [x] Mono / Poly with legato and GLIDE (Advanced; `voiceMode`, `glide`, version hint 8); sessions without a parameter open with its default
- [x] SHAPER DEPTH (= the MOVEMENT macro, also in the SHAPER popup): patterns span their full range, 100 % on VOL gates to silence, 10 % is subtle; MACHINE is now an on/off gate
- [x] Large MIX popup for three layers (click the small triangle or MIX); macro value bubble readable (light on graphite, whole percent)
- [x] Per-layer Original <-> Reimagined (engine: LayerSettings::reimagined, shared resonance weighted by the mix; plugin: layerB/C.reimagined, A keeps `reimagined`; state v7 migration)
- [x] Pixel-accurate visual pass against the approved references (design/README.md): layout in the reference's 1448 x 1086 coordinates scaled uniformly; design tokens (Design.h, palette.md); knobs, cards, waveform renderer, mix band, macros, envelope, wheels, keyboard and every popup rebuilt; canonical render + overlay/diff harness; final comparison in design/final-review/ (mean difference main 33.7 -> 17.6, SPACE popup 49 -> 20.9)
- [x] Compact macro popovers again (anchored above each macro, one at a time, no close button or backdrop)
- [x] Reimagined thumbs linked by default (`reimaginedLink`, small link key); INIT, Reset settings and user starting states (`.ospstate`, settings + slots); Clear all samples keeps slots and settings (`keptSlots`, engine mix slots)
- [x] LIFE round robins and character (version hint 10): TAKES ∞ / 2–16 (`life.takes`), ORDER cycle / random (`life.takeOrder`), NEW re-roll (`life.takesSeed`), CHARACTER Auto / Pluck / Synth / Drum (`life.character`) from the round-robin generator's priors and trained kick/snare models. Takes are stratified per axis, re-centred and recomputed from the seed at note-on (no memory beyond 128 bytes of per-note position); player drift and DYNAMICS still apply on top. Defaults (∞, Auto) are bit-identical to before (goldens unchanged)
- [x] Final visual revision: waveform without halo (layered peaks/body/RMS/spine + onset detail from `peakFlux`), slim read heads and START, LOOP as circular arrows, lighter central typography (`type::` roles, Inter Light)
- [x] Per-layer REIMAGINED migration (docs/reports/reimagined-migration.md): `ReimaginedStage` extracted (bit-identical), routing legacyGlobal / perLayer (state v8, explicit conversion on a REIMAGINED gesture only), REIMAGINED knob in every card (five-knob row at 87 %), central Original <-> Reimagined track and its link key removed, one source without a mix band, VOLUME as a thin slider; 12 legacy reference scenes null against the pre-migration build
- [x] Waveforms fill their display (onset to end of sound; trailing silence left out); five macro colour identities (thin arcs, inherited by their popovers); REIMAGINED's spectral value arc (OKLab continuum, grows with the value); MIX moved into the header (hidden / A-B line / C-top triangle, 200 ms unfolding, a third layer joins at 0, removing one projects the shares onto the blend); the body's mix band removed
- [x] REIMAGINED modes (docs/reports/reimagined-modes.md): KALEIDOSCOPE (the original algorithm, FOCUS / SPREAD neutral at 50 %, 18 reference scenes null to the sample), TAPE FRAME, TOYBOX, MOSAIC, MIRAGE as per-voice engines behind one contract; shared off-thread ReimaginedAnalysis (tape splice map, harmonic frames); per-layer mode + 14 settings (version hint 11, state v9, older states KALEIDOSCOPE); popover from the card's REIMAGINED name with live mode pictures; spectral label dot
- [ ] REIMAGINED modes listening review (RI-08: differentiation, 10 patches per mode, chords / melodies / ranges) and factory presets per mode — needs ears
- [ ] Stage 20: DAW testing (Logic AU, Ableton AU/VST3, Reaper VST3) — needs the user's Mac
- [ ] Listening pass: FOLLOW-off lift, REVERSE + LOOP, the three-layer mix law, default LEVEL 0 dB

## DONE

- Pure C++20 DSP/analysis library (`osp_dsp`) independent of JUCE; JUCE used for file
  formats, MIDI files, JSON, SHA-256.
- Deterministic rendering: bit-identical across runs and across block sizes
  (1–4096, also host-varied, at 22.05–192 kHz in the plugin); seed-dependent randomisation in baseline B.
- 113 Catch2 test cases (unit/integration/regression) + performance smoke + 12 plugin
  test cases; all green on Linux (GCC 13), macOS 14 arm64 (Apple Clang, CI, auval) and
  Windows (MSVC, CI).
- Phase 1 plugin: see checklist above.
- Synthetic ground-truth generators and a synthetic mini-corpus generator
  (`--generate-test-signals`), including deliberately broken/unsupported files.

## IN PROGRESS

- Lab round 3 (Reimagined 3, violin).
- DAW/host validation on a Mac.

## NEXT

- Lab round 1 scored and acted on (docs/reports/lab-1.md). Round 2 in the lab: Reimagined 2,
  Sustain 2, Dynamics 2, Multi-sample 2.
- Transient preservation and transient mixing: both kept as options, off by default
  (lab round 1: preservation lost on 3 of 4 sources, mixing split 1–1).
- Timbre clustering for sets that mix organ stops (waits on morning question 2).
- Real-Mac worst-case callback timing. Reads above x4 (more than about +2 octaves) still
  evaluate the stretched kernel; mip-mapped sources would cap them if that matters.

## BLOCKERS

- DAW testing (Logic, Ableton, Reaper) needs a Mac; CI covers build, tests and auval.

## KNOWN ISSUES

- **CPU audit (docs/reports/cpu-deep-dive.md)**: default patch 16 notes 12 % of a VM core (≈ 3–6 % on
  Apple Silicon); +24 st 4.6x, three layers 2.8x, KALEIDOSCOPE 100 % 2.4x. Hot spots: sinc reads 43 %,
  per-voice LP24 ladder 23 %, SPACE's per-sample `std::sin`. The editor repaints whole waveform displays
  through live DropShadows at 30 fps while playing (44 ms per display per frame on the VM): the largest
  CPU user overall. Ranked fixes are listed there. **Done:** cached shadow masks + dirty-strip
  repaints (editor 331 → 38 ms full paint, a playing display ≈ 2 ms per head instead of 44 ms),
  SPACE phasor modulation + reverb sleep in silence + control-rate caching (idle 1.0 → 0.15 %,
  reference −5 % instructions; output within −132 dBFS). Open: SIMD sinc, mip-mapped sources.

- **CPU (engine C)**: about 1.2x the baseline per voice (continuation crossfades, shelves,
  post stage). macOS 14 arm64 CI runner (48 kHz / 128): engine C 16 held voices **17.1 %**
  mean (spec target < 25 %: met), 24 voices with dense retriggers 30.5 % (baseline A 26 %).
  Worst-case callbacks on the shared CI VM spike to several ms; needs measuring on a real Mac.
  Since then engine C reads at or below the original speed through a precomputed
  polyphase table (2048 phases): on the cloud VM 16 held voices went 38 % -> 22 % (the
  baseline measures 31 % on the same VM), 24 dense voices 63 % -> 41 %. Stretched reads
  (upward transposition) then got polyphase tables too (24 semitone levels up to x4,
  next level up so the cutoff is never higher; 4.5 MB per engine): 16 held voices
  22 % -> 10 %, an octave up 38 % -> 17 %, 24 dense voices 41 % -> 21 % (baseline A
  32 % on the same VM). macOS 14 arm64 CI after both: 16 held voices **4.9 %** mean
  (worst 28 %), 24 dense voices 13.2 % (baseline A 18.7 %).
- **CPU**: the windowed-sinc kernel is computed per output sample (two table lookups
  per tap). Measured on a 2.1 GHz Xeon cloud VM: 16 sustained voices ≈ 19–20 % of the
  48 kHz/128 block budget, 24 voices with dense retriggers ≈ 33 %. Transposing up
  stretches the kernel (more taps). Planned fix when it matters: mip-mapped
  (pre-decimated) sources prepared off-thread so the kernel never stretches, and/or
  a precomputed polyphase table. Not done in Phase 0 to keep the baseline simple.
- Worst-case callback times on shared cloud VMs show scheduler spikes (several ms);
  they need measuring on the real Apple Silicon target.
- YIN range is 30 Hz – 4 kHz. Notes outside it are not judged by render pitch checks;
  very low basses (< 30 Hz fundamentals) will report a wrong/low-confidence root.
- Very short soft sources (< 0.5 s synthetic vowel) can be pitch-tracked on a harmonic
  with moderate confidence (0.4 s vowel at C4 -> G5). Longer material is unaffected.
- The synthetic vowel at A4 (strong 2nd-harmonic formant) is tracked an octave up with
  confidence 0.46 ("moderate"). Real corpus files were fine (66/80 high confidence), but
  multi-sample sets inherit any octave error into their pitch grouping; the Samples
  inspector shows each file's group so it can be corrected.
- Onset list uses spectral flux with a 3 dB/bin absolute floor: very soft attacks
  (bowed swells) may produce no listed onset (the envelope `onsetSeconds` still exists).
- Pitch analysis runs at the source rate; 96 kHz sources cost ~2x analysis time.
- Baseline voices stop when the recording ends (no looping) — by design until the
  Continuation Engine; `long-hold`/`long-chord` renders of short sources are mostly
  silence after the source ends.
- JUCE writes integer AIFF only; float AIFF-C is read (tested) but never written.
- Cross-platform bit-identity is not guaranteed (libm); golden tests use tolerances.
- Plugin: no undo/redo of sample loads yet (parameters have an UndoManager); multi-file
  drops use the first file; mod wheel / channel pressure have no destination yet; no
  MPE; the UI is a debug UI (no scaling options beyond window resize).
- Plugin RT caveat: merging on-screen keyboard events uses JUCE's
  `MidiKeyboardState::processNextMidiBuffer`, which takes a short internal lock and can
  grow the MIDI buffer when UI notes are injected. Standard JUCE practice, but it bends
  Rule 1; replace with a lock-free FIFO from the UI when the real UI is built.
- Bake-off engine C (formant compensation) goes an octave or more off pitch at ±24
  on organ and bowed sources; B (same library, plain) is clean there.
- The cepstral envelope-shift metric is only reliable on synthetic sources; on real
  high-F0 or broadband sources it is noise. Guard rail only.
- Plugin: a host that never calls processBlock keeps the previous instrument alive in
  memory until processing starts (by design of the hand-over; harmless).
