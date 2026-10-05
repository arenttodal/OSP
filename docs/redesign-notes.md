# Implementation notes: adaptive 1–3 layer redesign

Working notes for the "Master GUI Redesign, Macro Visualization System & Adaptive 1–3
Layer Architecture" milestone. Updated after every stage; the final report is at the end.

## Stage 0 — baseline (tag `baseline-before-adaptive-layers`)

### Source model
- `LoadedInstrument` (plugin) holds one dropped sample or a multi-sample set: hash, file
  identity, analysis, the immutable `InstrumentModel` (or `InstrumentSet`), waveform peaks.
- Loading runs on one loader thread in stages (provisional → continued → complete), each
  stage published to the audio thread through a per-layer `ModelExchange`.

### A/B implementation (before this milestone)
- `EngineSettings::layers = 2`; the engine keeps per-layer arrays (model, set, round-robin
  takes, root-shift pitch ratio, live granular settings, render buffers, performance engine,
  grain snapshot).
- Every note-on starts one voice per loaded layer (same event index, layer B salted).
- Each layer renders into its own buffer; equal-power blend (`cos`/`sin` of `ab.blend`,
  smoothed ~20 ms), summed, then ONE shared `PostProcessor` (Reimagined resonance,
  MOVEMENT bus incl. SHAPER, SPACE) and the output gain.
- Per-voice stages (LIFE shape, DYNAMICS, CHARACTER filter, DRIFT) use the same shared
  settings on every layer.

### Voices
- 2 × 64 + 16 = 144 `InstrumentVoice`s on the heap; polyphony is counted per layer.
- One Shot: band-limited read through the recording with continuation jumps (multi-loop
  random walk) and release graft. Granular: `GranularSource` (24 Hann grains around POS).

### Parameter IDs (stable, never renamed)
- Envelope/output: `attack`, `release`, `gain`, `velocityRange`, `fineTune`, `bendRange`.
- Macros: `life`, `dynamics`, `character`, `motion` (MOVEMENT), `space`, `reimagined`.
- Advanced: `pitchCharacter`, `sustain` (Recording / Endless), `seed`, `mpe`.
- Popups: `life.mode/pitch/tone/attack`, `dynamics.curve/tone`,
  `character.type/min/max/resonance/drive/envAmount/envAttack/envDecay`,
  `movement.mode`, `movement.drift.*`, `movement.tape.*`, `movement.chorus.*`,
  `movement.pulse.*`, `movement.shaper.pattern/rate/target/smooth`, `space.type/decay`.
- Layers: `ab.blend`; per layer `layerA.sourceMode`, `layerA.granular.position/size/density/tune/spread`
  (and `layerB.*`).

### State schema (stateVersion 5)
- APVTS parameters + `stateVersion`, `uiScale`, `advancedOpen`, `program`, `editLayer`.
- `Instrument` (layer A) and `InstrumentB` child trees: content hash, filename, original
  path, exact playback root/start/gain, optional root override, optional `Set` (members +
  user assignments). Migrations: v1 neutral engine, v3 CHARACTER filter, v4 layers,
  v5 MOVEMENT per-mode settings.

### Macro routing
`voices (LIFE, DYNAMICS, CHARACTER filter, DRIFT) → layer buffers → equal-power blend →
PostProcessor (Reimagined resonance → MOVEMENT bus [TAPE/CHORUS/PULSE/SHAPER] → SPACE) → output gain`.

### GUI hierarchy (before)
`OspAudioProcessorEditor`: header (root, title, character line, starting-state box, menu),
one `WaveformView` with `LayerTabs`, `BlendControl`, `SourceModeSwitch`, `GranularOverlay`
and `SamplesPanel` inside it, six macro knobs (five with popups + Original/Reimagined),
`OspKeyboard`, status row, Advanced button. Popups: `MiniPanel` subclasses in
`ShapingPopups.cpp` (one at a time, outside click / Escape / own label closes).

### Reference renders
Before the refactor, seven engine-C scenarios (single layer, A/B with a pitch offset,
A One Shot + B Granular, Granular A, blend automation, SHAPER with host timing, B only)
were rendered to raw float files and are compared bit-for-bit after Stage 1.

## Stages 1–3 — three engine slots, layer C, generalised mixer

- `EngineSettings::layers = 3`. The engine keeps a fixed `std::array<Slot, 3>` (model, set,
  round-robin takes, root × TUNE pitch ratio, live granular settings, render buffers,
  performance engine, grain snapshot, last applied L/R gain). 3 × 64 + 16 = 208 voices.
- New per-layer `LayerSettings`: START (0..1), TUNE (±24 st), PAN (balance), LEVEL
  (−48 dB = silent .. +6 dB), REVERSE, LOOP, FOLLOW. LINK is a plugin (UI) behaviour.
- `InstrumentEngine::mixWeights (occupied, blend, x, y)` — pure:
  one occupied layer = unity (whichever slot); two = the A/B equal-power `cos`/`sin` of
  `ab.blend` between the two occupied slots; three = barycentric shares of the triangle
  position (A bottom-left, B top, C bottom-right) taken as power (`sqrt`) → constant total
  power (centre = −4.8 dB each, edge midpoint = the two-layer middle).
- Order: source voice (START/TUNE/REVERSE/LOOP/FOLLOW) → layer buffer → × weight × LEVEL ×
  PAN (gains glide per block; any jump takes ≥ 10 ms) → sum → one shared PostProcessor.
- CPU: a layer whose gain is 0 for a whole block is not rendered: held notes wait (and
  continue when faded back in), released notes end, an emptied layer's notes all end.
- Musical voice count (`musicalVoiceCount`) counts note-ons, not layer voices.
- Bit-exact check against the Stage 0 renders: single layer, A/B, A/B granular, granular A,
  B only — identical. Blend moved before the first block: no longer ramps from the
  prepare-time blend in the very first block (−69 dB peak difference, first block only,
  carried by the reverb). SHAPER scenario with B silent: −151 dB (B no longer rendered).
- Plugin: `layerC.*` (sourceMode, granular), per layer `start/tune/pan/level/link/reverse/
  loop/follow`, `mix.x`, `mix.y`, `decay`, `sustainLevel` (all version hint 7). Existing
  `layerA.*`, `layerB.*`, `ab.blend` IDs kept (already in distributed builds).
- State v6: `InstrumentC` tree. Migration (< 6): layer controls neutral, layer C neutral,
  mix centred, ADSR sustain 100 %; the old global Sustain becomes every layer's LOOP and
  Sustain returns to Endless (so they never disagree). LOOP off also keeps a layer free of
  per-voice DRIFT wander, exactly as "Recording" did (v5 sessions play bit-identically).
- Layer management (message thread): `addLayers` (one file per free slot, up to three,
  extra files reported; the new layer is made audible: blend 0.5 / triangle centre),
  `removeLayer` (compacts: complete state moves down — sound, root, mode, granular and
  layer controls; not while loading), `restoreRemovedLayer` (one step). Removal does not
  go through the UndoManager (APVTS flushes parameter changes into it later, which would
  corrupt redo); undo history is cleared when slots move.
