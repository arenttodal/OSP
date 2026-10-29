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

- [x] FX-01 popup framework: a macro's name opens its mini panel (one at a time, Escape / outside click closes, hover underline, LED under the knob when customised, double-click resets)
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

## Milestone: SPACE v2, ECHO, SHAPER CUSTOM (state 10, parameters v12; docs/reports/space-echo-shaper.md)

- [x] SPACE v2 engine: stereo diffusion, 8-line FDN with an allpass per line, cubic moving
      reads (sine + seeded wander), two-band decay; ROOM from shoebox image sources, HALL
      (Berlin halls: gap, lateral reflections, bass ratio 1.2) in CHAMBER's place, PLATE with
      an instant onset, SPRING kept. Smoother tails than v1 on every type (measured), levels
      matched (-6.9..-7.5 dB at 50 %).
- [x] SPACE controls: PRE-DELAY, SIZE, DECAY, DAMP, MOD, WIDTH, LOW CUT, HIGH CUT; the EQ drawn
      over the tail with draggable corners; the picture uses the real reflection patterns.
- [x] ECHO: sixth macro (verdigris, between MOVEMENT and SPACE, CC 26), TAPE and BBD delays in
      parallel with SPACE, FREE / SYNC (1/16..1 bar, dotted, triplet), MONO / PING-PONG / WIDE,
      TIME, FEEDBACK, TONE, AGE; popover picture of the repeats; sleeps in silence. The amp
      envelope narrowed to 450 px for it.
- [x] SHAPER CUSTOM: 16 editable steps (STEP brushes HOLD/FALL/RISE/PULSE/DIP/SOFT, DRAW, right
      click cycles a shape), the magnifier's large editor (COPY, CLEAR, SAVE, LOAD), saved
      patterns (*.ospshaper JSON), undo per gesture, saved with sessions and presets.
- [x] Migration: older sessions open HALL for CHAMBER, the EQ fully open, ECHO off, CUSTOM = THREE.
- [ ] Listening round: HALL vs old CHAMBER, PLATE v2 vs v1, ECHO TAPE / BBD on the fixtures.
- [x] A third layer is heard at once (C 1/3 of the mix, A and B keep their balance; was: joins at 0).
- [x] MUTE / SOLO per layer (`layerX.mute`, `layerX.solo`, version hint 13): small M / S keys
      beside the layer letter (replacing its light); solo wins, alt-click solos one alone, an
      unheard layer is veiled; emptied slots drop their solo; saved, moved with the layer.
- [x] LOOP and REVERSE in every REIMAGINED mode (`ReimaginedNote::loop`): LOOP on sustains a
      held note, forwards or reversed; LOOP off plays the recording once and ends. TAPE FRAME
      no longer runs out by amount while LOOP is on (each pass wears a little, floored), LOOP
      off reads the recording straight through, and reversed notes start at the end (they
      started at position 0 and ended at once). TOYBOX's walk drifts the way the note plays
      (it drifted forwards on reversed notes) and plays through and ends with LOOP off; MOSAIC
      and MIRAGE hold only with LOOP on; MOSAIC reversed and held ping-pongs the body.
      Regression test per mode (`LOOP and REVERSE are respected by every mode`).
- [x] The dot after a customised macro's name is gone; the LED under the knob says it.
- [x] DRIVE (parameters v14; docs/reports/drive.md): the DYNAMICS macro's position is DRIVE now
      (ochre), a shared TUBE / TAPE / CRUNCH saturation before MOVEMENT, ECHO and SPACE (4x
      minimum-phase IIR oversampling, no latency, measured static compensation, TONE / BODY,
      live transfer-curve popover). DYNAMICS works and is saved exactly as before, in Advanced.
      Sessions from before DRIVE render bit-identically (nine-scene null test against the old
      build). Tuned by measurement; listening round on the corpus still to do.
- [ ] DRIVE listening round: corpus sources at 10-20 / 40-60 / 100 %, blind TUBE / TAPE /
      CRUNCH at 65 %, level-matched.
- [x] ARPEGGIATOR (parameters v15; docs/reports/arpeggiator.md): one note-event stage ahead of
      the voice engine (`engine/Arpeggiator`): UP, DOWN, UP/DOWN, PLAYED, RANDOM (seeded),
      CHORD; 1/4..1/32 with dotted and triplet; gate 10-150 %; 1-4 octaves; the host's grid to
      the sample (600-case timing matrix exact), free running when stopped; scheduled
      note-offs, pedal, panic, safe on/off hand-overs (stress-tested: no stuck notes). A light,
      the state and a chevron by the keyboard; an inline editor (16-step display bound to the
      scheduler) that grows the window; ADVANCED became a small link. ARP off: sessions from
      before render bit-identically (13-scene null test). Headless via the `"arp"` config block.
- [x] ARPEGGIATOR to the approved mockups: ARP and Advanced cards right of the keyboard, the
      inline editor with numbered step bars, Pattern / Rate boxes and GATE / OCTAVES / SWING
      knobs; SWING (`arp.swing`, hint 16, default straight) on the grid and free running.
- [x] REIMAGINED on Granular layers: KALEIDOSCOPE doubles the grain cloud and scatters it,
      TAPE FRAME's tape head and TOYBOX's turning heads steer POS (docs/reports/reimagined-modes.md,
      "Granular layers"). One Shot and Granular at 0 % unchanged (null tests).
- [ ] ARPEGGIATOR in hosts (Logic, Ableton, Reaper): window resizing, automation, bounces,
      loops; MPE with the arpeggiator; a listening pass on the gate and pattern defaults.
- [x] MODULATION (parameters v17; docs/reports/modulation.md): LFO 1/2 (7 shapes incl. a
      CUSTOM breakpoint curve, Hz or host-synced 1/32..8 bars with D/T, phase, polarity, FREE /
      RETRIGGER / ONE SHOT, GLOBAL / POLY) and per-voice ENV 1/2 (ADSR + curve, or a ONE SHOT
      curve); a destination registry (48 entries with the EQ's) (macros, layer level / pan / fine tune /
      REIMAGINED, granular POS / DENS / SIZE / SPREAD, CHARACTER cutoff / resonance, amp ADSR);
      16 automatable route slots, undo, recall; no route = bit-identical to before. The first GUI
      (a bay that widened the window) was replaced by the refinement below. Headless via the `"modulation"` block.
- [x] PER-LAYER EQ (parameters v18; docs/reports/eq.md): five bands per layer (HP / LP 12 or
      24 dB/oct, low and high shelf, bell; SVF sections, glided, crossfaded switches, zero
      latency) after the layer's voices and REIMAGINED stage, before LEVEL / PAN; off by
      default and out of the path when off (older sessions bit-identical). The editor opens
      over the waveform from an EQ key in the card's header: quiet markers that become bands
      when dragged, the exact response curve, a typed / dragged band inspector, one editor at a
      time. Bell frequency / gain and shelf gains are modulation destinations (global LFOs).
      Headless via the `"eq"` block.
- [ ] PER-LAYER EQ: compare against the Option 3 reference (not received); DAW checks.
- [x] MODULATION UI refinement (docs/reports/modulation.md, "The modulation panel and halos"):
      the bay is gone and the window keeps its size. The AMP envelope's panel has five minimal
      tabs (AMP, ENV 1/2, LFO 1/2) that are also drag handles; compact one-row editors; one RATE
      knob with a Sync / Hz caption menu; polarity in a context menu; a routes popover and a
      large curve editor; MOD toggles AMP / last source; one selection state. Two-layer halos
      outside each knob's reach with hover guide, endpoint handle, tooltip (route, depth, base,
      range), direct depth drag through zero (also Option-drag on the knob), a context menu and
      subdued secondary routes; macro labels raised clear of them.
- [x] ARP panel revision (docs/reports/arpeggiator.md §4, §6b): PATTERN as a picture of the
      style plus a list of all 18 styles; twelve Live styles appended to `arp.pattern` (DOWN/UP,
      UP & DOWN, DOWN & UP, CONVERGE, DIVERGE, CON & DIVERGE, PINKY UP(/DOWN), THUMB UP(/DOWN),
      RANDOM OTHER, RANDOM ONCE; seeded, older sessions unchanged); RATE as a knob from slowest
      to fastest; OCTAVES as four stacked keys. The header reads ANDOR/2-OSP.
- [x] ANDOR/OSP refinement (docs/reports/modulation.md, "ANDOR/OSP refinement"): the global LFO
      no longer freezes after ONE SHOT (and RETRIGGER survives repeated keys); ENV 1/2 reach the
      macros and EQ through a global envelope (restarted per note, released by the last key /
      pedal); MOD WHEEL is a source (drag its socket or right-click); new destinations ECHO,
      START A/B/C (taken at note-on) and ENV 1/2 ATTACK / DECAY / SUSTAIN / RELEASE (LFOs and
      wheel only, no loops); one assignment path (rules, loops, duplicates, capacity, reasons);
      right-click "Assign ..." on every destination; Granular controls open under a resting drag;
      tab sockets and spacing; macro names clear of all a halo draws; the sample's details in
      the badge's card (no file name in the header); the MOD button gone, ADVANCED full width;
      the three-layer MIX triangle hidden behind `design::showMixTriangle` (DSP and state kept);
      ANDOR/OSP, One Shot Performer.
- [x] META-MODULATION (docs/reports/modulation.md, "Meta-modulation"): a source dropped on a
      halo (or assigned from its right-click) moves that route's depth: clamp (depth + Σ amount x
      source), evaluated in the route's own scope and timing, base values and automation
      untouched. Depth routes are ordinary routes (`ROUTE DEPTH`, appended) linked by stable
      route IDs (`ModLinks` in the state); one level, no self, the ordinary scope / timing / loop
      rules, each refusal explained; removing a route removes its depth routes in one undo step.
      Halo vs knob drop targets, a chooser where arcs lie together, drag tooltips, a quiet
      effective-depth accent, readout, amount editing. Headless `"routeDepth"` routes.
- [ ] LIBRARY SYSTEM (queued spec, 13 stages; docs/library/): Stages 0-8, 10 and 11 done.
      0 audit, risk register, decisions; `.ospinstrument` import hardened.
      1 SQLite catalog (WAL, FTS5, migrations; now version 5: scan candidates, the Inbox,
        classification overrides, pack ids).
      2 import transaction and resolver; every load and save recorded; cross-process safety.
      3 portable manifest 2, rights check, recoverable loads, preset file operations.
      4 preview engine (`src/library/PreviewEngine`, real-time safe, tested allocation-free),
        audition (browser preview, A/B/C tray, keyboard audition, Load combination, load into
        a layer, all recoverable), storage usage and emptying the trash into the system Trash.
      5 the Library window (presets, templates, sounds, inspector, tray, save, settings,
        missing sounds; docs/library/gui.md).
      6 tags from names, folders and audio; user-chosen folder scans (index + deep, pause,
        cancel, incremental; 50,000 files indexed in 2.2 s); one-shot classification in five
        classes, 48/48 on the synthetic labelled set, 12/12 hard cases (not blind any more),
        corpus 78 one-shots + 2 multiple events for the user to judge; Scanned / Set aside
        review with corrections that stick (docs/library/classification.md).
      7 the capture Inbox: user-chosen folders reconciled at startup, on opening and every
        3 s while open; explicit states, stable-file detection, iCloud placeholders,
        duplicates, renames, offline folders, crash recovery; import (Captured) and load
        in one step (docs/library/inbox.md). Sandbox bookmarks and the helper: investigated,
        not built (D-18).
      8 trim and slice editor (Sample Forge's slicer ported, 20 s slices), conservative trim,
        fades, optional gain, stereo kept; each slice a new sound with its provenance; the
        original untouched (docs/library/slicing.md).
      10 packs (`.osppack`: checked whole before anything changes, updates keep identities
        and the user's edits, removal recoverable, export from a collection with the rights
        question, factory packs apart from user data; none shipped yet) and the Library
        backup (`.ospbackup` folder: VACUUM INTO snapshot; restore merges with Keep mine /
        Newer wins, never overwrites a file); storage and cache cleanup
        (docs/library/packs.md, backup.md).
      11 QA (docs/library/qa.md): renders byte-identical to the pre-Library baseline 1537f7e;
        output bit-identical while scanning; stress and security suites; measured search
        4.3 ms / 10,000, preview ready 47 ms, processBlock unchanged under a scan. Not done:
        DAW hosts and usability sessions (need the user's Mac and people).
      Next: Stage 9 (iPhone capture) and Stage 12 (signing) need the user's Apple account
      and hardware; DAW checks need a Mac.
- [ ] MODULATION: compare the panel and halo against the mockups (not received); host checks
      (Logic, Ableton, REAPER, AudioPluginHost); a global envelope mode needs a
      gate-aggregation rule before ENV may reach the macros.
- [x] FULL-RANGE MODULATION (state 11): 100 % depth now covers a destination's whole knob
      (LEVEL 54 dB from silence to +6, TUNE 48 semitones, PAN, START, POS, CUTOFF along the
      filter's travel, times and EQ at their log ranges); the engine stops at the knob's ends
      (LEVEL could reach +24 dB past +6 before; times had no limit). Older sessions' depths
      are scaled by old / new span on load, so they sound as before; depth steps 0.01 %.
      `[unit][mod]`, `[plugin][mod]` (docs/reports/modulation.md).
- [x] Library fix (the flaky "sideCount all = 4" window test): re-importing a sound that is
      in the Library's trash reuses and restores its record instead of adding a second one;
      connections that open a brand-new catalog at the same moment retry the migration
      instead of failing, and LibraryService no longer latches a failed open. Both bugs
      reproduced under CPU load before the fix. `[library]`.
- [x] RENDERED MACRO KNOB, phase 1 (tools/blender-assets, docs/reports/knob-phase1.md): a
      procedural Blender generator (geometry, three ivory finishes, fixed studio, calibrated
      exposure, 129 deterministic frames); Satin Ivory with an engraved line chosen at real
      size; SpriteKnobRenderer (lazy decode, exact-size area resampling, shared per process);
      the LIFE macro as pilot; Cmd+Option+K / OSP_KNOB_SKIN switches original / pilot / all
      macros (looks only: output bit-identical, state unchanged). `[plugin][sprite-knob]`.
      Next: judge it on the Mac; engine-card knobs from config/future.
- [x] Layer knobs: each name sits as far above the dial's tick dots as the value sits below
      the lowest ones (the knob component grows upwards by `LayerKnob::labelLift`; dial and
      value unchanged). The MACROS title is gone from the macro panel.
- [x] CERAMIC SKIN (direction 09 Warm Ceramic + Teak, built into the plugin; style guide and spec
      docs/design/warm-ceramic-teak-style-guide.md): rendered pieces (tools/blender-assets/skin,
      from the design lab's own config: housing + teak cheeks, cards at the three layout widths,
      D3 panels, A1 keys per role and state, badges, C1 fader cap on the T3 rail, lens LEDs,
      display bezels, keybed recess) nine-sliced by `skin::Pieces` (CeramicSkin) at the physical
      scale; keys and cards drawn by the surface below so their shadows fall there; B1 tabs; all
      six macros rendered by default. Classic stays (Cmd+Option+G, OSP_SKIN=classic). Tests
      [ceramic]; hidden screenshots [ceramic-shots].
- [x] CPU, three layers (docs/reports/cpu-three-layers.md; harness [cpu-preset]). Step 1:
      bit-exact granular and KALEIDOSCOPE reads and the sinc level cache (−12.6 % instructions
      on the measured patch); voices: two per key per layer, inaudible released voices retired
      (−90 dBFS for 0.25 s); the source display is opaque with a cached parents' backdrop
      (pixel-identical, a frame about 4× cheaper) and draws at most 8 read heads. Step 2:
      SIMD sinc reads (both channels, one pass), the two-lane LP24 ladder, a rational tanh and a
      rotating sway for KALEIDOSCOPE, Hermite grains as tap weights (core/Simd.h,
      engine/FastMath.h): instructions −38.5 % (−46 % with step 1); chords 31 → 18 %,
      pedal 100 → 57 % on the VM. Renders differ by −120 dBFS RMS (one float step's worth);
      baselines and goldens unchanged. Step 3 (REIMAGINED scaling, voice budget) only if needed.
- [x] UNIFIED ANDOR research (docs/research/unified-andor, research only): audit of OSP, ANDOR
      Grain and ANDOR Phrase (origin/andor-lab), same-VM benchmarks, architecture options and a
      recommendation (extend OSP: KEY DNA into the existing granulator, Note Atlas as an
      InstrumentSet), annotated GUI mockups, roadmap. Decisions approved (Architecture A:
      extend OSP); Phase 0 and Phase 1 approved, Phase 2 onward wait for review.
- [x] UNIFIED ENGINE Phase 0 (docs/research/unified-andor/phase-0-baseline.md): the baseline
      harness. 15 headless scenes (src/research/UnifiedScenes.cpp: One Shot, granular single,
      repeated, scale, 4- and 8-note chords, A/B/C layers, modulation-heavy, non-default
      patches, mono glide, a CPU stress), `research-renderer --unified`, config `"layers"`;
      digests captured from the unmodified engine (tests/regression/golden/unified, exact on
      linux-x86_64-gcc, figures elsewhere) plus three plugin sessions
      (tests/audio/unified-baseline); per-block CPU and memory. Found: output depends on the
      block size (existing: block-to-block gain glides), so baselines fix it.
- [x] UNIFIED ENGINE Phase 1, KEY DNA in OSP's Granulator, engine only
      (docs/research/unified-andor/phase-1-key-dna.md): src/engine/KeyDna (ANDOR Lab's maths on
      POS, SIZE, DENS, SPREAD, TUNE; OSP curve tables; own "keydna" PRNG stream), a per-layer
      128-key table, offsets fixed at note-on and applied first in notesGranular(). Off/neutral
      bit-identical to the Phase 0 baseline; 7 unit + 11 integration tests (identity per key,
      polyphony, A/B/C, held notes, modulation, no allocation, bounds); demonstrations A static /
      B organic / C expressive (`--unified --set keydna`, research/configs/keydna-*.json, pinned
      in tests/regression/golden/keydna); CPU within noise, +49.5 KB per engine. No plugin
      parameter, state or GUI change. Stopped for review before Phase 2.
- [x] UNIFIED ENGINE Phase 2, the Granulator's options and budget, plus KEY DNA in the plugin
      (docs/research/unified-andor/phase-2-report.md):
      - KEY DNA AMOUNT is live on sounding notes: 30 ms smoothing per voice, the
        `keyDnaAmountA..C` modulation destinations (not offered by the plugin's frozen route
        list) and the host parameter `layerX.keyDna.amount`.
      - Profiles are precomputed off the audio thread and published as immutable tables
        (per-layer ModelExchange<keydna::Profile>).
      - Grain options SCATTER, PAN and WINDOW (HANN / SOFT / PERCUSSIVE), on their own PRNG
        stream; KEY DNA gains SCATTER and PAN.
      - A deterministic max-min fair grain budget across A/B/C that keeps each thinned stream's
        level. Old sessions and presets open Unlimited; new sessions start at 256, chosen from
        the benchmark sweep.
      - Plugin: 13 parameters appended (hint 20; the first 350 schema lines are unchanged).
        stateVersion 13 adds the `KeyDna` child (schemaVersion 1); v12 sessions are migrated.
      - Stereo investigation: the expressive demonstration is narrower because of POS, not
        SPREAD; the stereo code is unchanged.
      - Every Phase 0/1 golden and plugin reference is unchanged.
      - Demonstrations: `--set phase2`, `--set budget`, research/configs/phase2-*.json, and
        tests/regression/golden/phase2.
      Stopped for review before Phase 3.
- [x] UNIFIED ENGINE Phase 3, the Note Atlas as an OSP instrument
      (docs/research/unified-andor/phase-3-report.md):
      - src/model/NoteAtlas + io/NoteAtlasJson (ported from ANDOR Lab, schema 2: asset
        origins incl. live capture, paths only hints, velocity layers, note origin, FILL
        mapping; Lab schema 1 migrated).
      - engine/AtlasSet: atlas -> InstrumentSet from region copies, one member per note,
        built by OSP's own builder (no second sampler); one gain per atlas; limits 300 s total
        / 60 s per region (measured 0.57-0.78 MB per region second).
      - FILL KEYBOARD in InstrumentSet::groupFor: exact key, velocity/round robin, nearest
        within the limit (ties lower), else silent without stealing. Legacy sets unchanged.
      - Plugin: loadAtlas (loader thread, ModelExchange), stateVersion 14 (`Atlas` child: JSON
        + store hashes, no audio; a session with missing audio recalls empty and failed,
        naming the sources), atlas sources in presets and portable instruments.
      - Grain budget: `bounded` policy (engine-wide gate; never above the budget, also with
        more streams than grains), default for new sessions; v13 and older keep `shared`.
      - Harness: `--unified --set atlas` (22 scenes, 21 pinned in golden/phase3, memory /
        build / lookup in atlas.md), `--atlas <file>`, `--write-atlas-fixtures`.
      - Tests: 8 unit, 19 integration, 7 plugin; every Phase 0-2 golden unchanged.
      - Visual regression: snapshots now use JUCE's software renderer on every platform
        (the intermittent macOS-only difference, runs 151 and 164).
      Stopped for review before Phase 4.
- [x] UNIFIED ENGINE Phase 4, phrase analysis and staged preparation
      (docs/research/unified-andor/phase-4-report.md):
      - src/analysis/phrase: ANDOR Lab's analysis ported (yin-viterbi, segmentation, description,
        edits), analysis version 3: per-note doubts, the recording's verdict and possible
        missed notes, quiet alternates off the keyboard, note-level harmonic repair.
      - Jobs and cache: phrase::AnalysisQueue (bounded workers and queued audio, shared
        identical requests, cancel, progress); io::PhraseAnalysisCache (hash + version +
        settings digest). mergeReanalysisReported keeps ids and corrections, reports conflicts.
      - Preparation: Stage A (playable) / Stage B (continuation) through one assembly
        (bit-identical to single pass, any thread count); exact sliding median in the transient
        separation (bit-identical, 2.9x); parallel members. Capacity 300 s per layer, 600 s in
        all for new loads; partial builds listed.
      - Plugin: analyseRecording / cancelAnalysis / atlasStatus; unsuitable recordings load as
        one sample with the reason; state stays 14 (fields only added).
      - Evaluation on real material: 8 spliced real phrases (F 0.87 at ±100 ms) and 78 real
        single-pitch takes (77 pitch labels right; over-splitting of held notes is the main
        defect). research-renderer --phrase-eval/-eval-file/-corpus/-analyze, --prepare-bench.
      - Tests: 23 unit/integration + 6 grain-budget stress + 6 plugin; every golden unchanged.
      Stopped for review before Phase 5 (the unified GUI not started).
- [x] UNIFIED ENGINE Phase 4B, held and repeated notes, 0.3 s sustain
      (docs/research/unified-andor/phase-4b-report.md):
      - Segmentation (analysis version 4):
        - a dip splits only as a re-attack (sharp rise, voicing gap at full depth, or 20 dB);
        - a re-pluck without a dip splits by its step;
        - slow dips (bow changes, swells) stay whole, flagged `possibleRepeat` with `repeatAt`;
        - beating is recognised and left alone;
        - pieces cut by a pitch move but described on one key are joined.
      - DETECTION MORE keeps Phase 4's splitting. `--phrase-corpus --detection`, `missedFlagged`.
      - Real single-pitch takes as one note 41 → 53 of 78, extra regions 169 → 87 (bowed 119 → 56);
        spliced phrases' false splits 10 → 4 with nothing lost; slow-dip repeats now offered as
        splits instead of split.
      - Atlas notes sustain from 0.3 s of stable body (`BuildOptions::minSustainSeconds`; loops
        scaled to short regions); sample sets and regions of 0.5 s or more unchanged. A note's
        stable body is much shorter than the note: notes under about 0.5 s still play as recorded.
      - Tests: 2 short-sustain integration, 1 segmentation unit case, 2 lab fixtures added. The
        Phase 3 atlas goldens with 0.3-0.5 s regions were updated (the sustain change, proved by
        reverting only the threshold); every other golden unchanged.
      Stopped for review before Phase 5.
- [x] UNIFIED ENGINE Phase 5, NOTE MAP and KEY DNA in the real interface
      (docs/research/unified-andor/phase-5-report.md; testing: phase-5-ableton-guide.md):
      - MAP NOTES (layer menu) turns a recording into an instrument of its notes in the
        background, playable at once; one undo step back to the ordinary sample; Play as one
        sample. Ordinary samples are never analysed by themselves and look unchanged.
      - NOTE MAP in the display's place (beside EQ | DAMPEN, the NOTES key on atlas layers):
        - regions, zoom and scroll, audition;
        - boundaries, PITCH / OCT / FINE, PLAYS ON / AUTO, velocity layers;
        - include, split (double-click, repeat markers), merge;
        - restrained doubts, per-note sustain explanation;
        - DETECTION FEWER / NORMAL / MORE (corrections kept, conflicts shown);
        - FILL and its reach, tuning, its own undo / redo.
      - Edits run on the loader thread with the recording's audio and publish through Stage A /
        B (one undo step each, queued and bounded, held notes keep sounding, evicted undo states
        rebuilt). Status: Analyzing / Preparing / Playable / Enhancing / Ready / Needs review /
        Failed.
      - Keyboard: the edited layer's map (recorded with takes, filled, silent).
      - KEY DNA editor (Granular strip's KEY DNA key, the Library's band, aligned to the real
        keys): ON, AMOUNT (host parameter, live), INDIVIDUALITY, SEED / RANDOMIZE; per
        parameter a curve, DEPTH, VARIATION; the computed profile per key; undoable; drawn
        points saved (`points`, additive). AMOUNT is not a modulation destination (frozen
        list): reported in the editor.
      - Tests: 10 plugin cases (workflows A-D, undo, queue, realtime swap, state, KEY DNA).
      Stopped for musical review before Phase 6.
- [x] ANDOR DESIGN SYSTEM and INSTRUMENT SHELL (andor-design-system/, start with HANDOFF.md).
      ANDOR OSP's interface was extracted, unchanged, into shared/andor-ui:
      - the components, look and assets;
      - the frame;
      - the parameter schema;
      - the loader;
      - the parameter-only host behaviour, behind InstrumentHost.
      OSP is pixel-identical before and after (design capture), and its 350-parameter schema is
      pinned to HEAD's by a golden test.
      The ANDOR Instrument Shell (shared/andor-shell) adds:
      - andor_add_instrument;
      - a ShellProcessor and a ShellEditor, ANDOR OSP's layout from the shared code;
      - the Engine DSP boundary;
      - a labelled diagnostic engine (baseline A / test tone);
      - disabling of every control the engine does not read.
      tools/create_andor_instrument.py validates arguments, checks the product registry,
      never overwrites, and wrote products/andor-example (ANDOR Example, Aexm).
      The visual regression (osp_visual_reference + andor_example_tests_visual) covers 18 states
      at tolerance 0: 0 pixels differ outside three masks (identity, preset name, status line).
      Mac AU validation of ANDOR Example runs in CI.
- [ ] DESIGN LAB round 1 (design-lab/, exploration only, nothing integrated): Blender-rendered
      surfaces composited under the plugin's own drawing; 6 full-GUI directions, button/tab/
      fader/panel/background/detail studies, close-ups, actual-size tests, review gallery,
      reports (design-lab/review/). Round 2 chosen: 09 Warm Ceramic + Teak (C1 fader on T3,
      lens LEDs, painted divider); style guide and build spec in
      docs/design/warm-ceramic-teak-style-guide.md; built (CERAMIC SKIN above).
- [ ] FINAL MODULATION SYSTEM + DAMPEN (queued spec, stages 0-10; design
      docs/reports/mod-final.md): Stage 0 audit done (docs/reports/mod-final-audit.md).
      Stages 1-4 done: sources VEL, NOTE, CC 1-4 and PRESSURE appended (VEL and NOTE per note,
      the shared stages following the player's latest key, also behind the arpeggiator);
      six tabs AMP | ENV | LFO 1 | LFO 2 | VEL | NOTE (ENV 2 legacy: no tab, its routes still
      work and show; older sessions' tab migrated); VEL and NOTE curves with presets, the
      played-key marker and APPLY TO AMP (older sessions keep their DYNAMICS response
      exactly; `dynamics.curve` sets the VEL curve when moved); curves saved as "c2"; CC 1-4
      follow controllers 2, 11, 16, 17 by default, chosen or learned from any control's
      right-click menu (MIDI Controllers), saved with the session. Stage 5 done: ARP RATE
      (phase-accumulator clock, smooth or Stepped), GATE, SWING, OCTAVES (next cycle) and the
      new ARP VELOCITY as destinations. Stage 6 done (DSP, editor, destinations): DAMPEN per
      layer and voice (Soft Shelf / Low-Pass by key, EQ | DAMPEN page), off by default.
      Stage 7: state 12. Still open: the resampling audit, CPU measurements, scenarios A-J,
      screenshots (docs/reports/mod-final.md, Results). Tests [mod-final], [dampen].
- [x] `plugin-pipeline/`: the build-and-download pipeline as a reusable kit for other JUCE
      plug-ins (`adopt.sh` for an existing repo, `new-plugin.sh` + starter for a new one, a fixed
      download link per branch); self-tested on macOS and Windows by its own workflow.

## Milestone: ANDOR Instrument Shell extensions for ANDOR ANALOG (a synthesizer)

- [x] Opt-in extension points (HANDOFF.md, "What a product may add"): product parameters after
      the frozen schema (own golden), generator engines (a card without recordings, its waveform
      picture), the host transport and latency at the engine boundary, factory presets, product
      test sources, products built from another repository (`ANDOR_EXTERNAL_PRODUCTS`), VST3
      categories, ANDOR OSP's macro names in the parity render.
- [x] ANDOR ANALOG registered (`Aanl`, `com.osp.andoranalog`); it lives in arenttodal/analog.
- [x] ANDOR OSP unchanged (Linux, Xvfb): the design capture is byte-identical before/after in
      every state and component except the MOVEMENT popover (06-popup-movement and its @2x crop),
      which also differs between two runs of the unchanged build (its moving picture: not this
      change); the schema golden is unchanged; ctest 12/12 green in the arenttodal/analog build
      (unit, integration, regression, plugin, osp_visual_reference, ANDOR Example's and ANDOR
      Analog's visual regressions at 0 px outside the masks in all 18 states).
- [x] ANDOR OSP's macro popovers shared (for ANDOR ANALOG's effects): MacroPopups (LIFE, DRIVE,
      CHARACTER, MOVEMENT, SPACE, ECHO and REIMAGINED) moved unchanged from apps/plugin to
      shared/andor-ui, on `InstrumentHost` instead of OSP's processor. The host gained what they
      read: editLayer, hostTempo, shaperPhase, SHAPER's custom pattern and editor size (OSP's
      own, now overrides), the shaper-pattern files and codec (moved from OSP's processor), and
      `reimaginedModeUnavailable` (default: every mode plays; OSP is unchanged). The shell
      implements them (the custom pattern saved with the session and handed to the engine on
      the audio thread, lock-free), opens the REIMAGINED popover from a card when the engine
      reads `reimagined`, hands the engine the modulation curves, and lets a product migrate
      an older state (`ProductInfo::stateVersion`, `migrateState`). OSP: design capture
      byte-identical before/after except the moving MOVEMENT strip (differs run to run on the
      same build); ctest 9/9 green.

## DONE

- Pure C++20 DSP/analysis library (`osp_dsp`) independent of JUCE; JUCE used for file
  formats, MIDI files, JSON, SHA-256.
- Deterministic rendering: bit-identical across runs and across block sizes
  (1–4096, also host-varied, at 22.05–192 kHz in the plugin); seed-dependent randomisation in baseline B.
- 113 Catch2 test cases (unit/integration/regression) + performance smoke + 12 plugin
  test cases; all green on Linux (GCC 13), macOS 14 arm64 (Apple Clang, CI, auval) and
  Windows (MSVC). CI now runs macOS only (the target; saves hours of runner time): Linux
  (incl. the xvfb `[ui]` snapshot tests) is checked locally, Windows on demand.
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
