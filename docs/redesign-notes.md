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

## Stages 4–7 — EngineCard, adaptive layout, drag & drop, main redesign

- Typeface: Inter (SIL OFL, embedded) with its tabular figures made the default digits
  (values no longer jitter as they change); Barlow removed.
- Palette (`OspLookAndFeel.h`): housing #F2F0EA, raised #F8F7F3, recessed #E8E6DF, text
  #272622 / #77746D, hairline #D5D1C8, graphite #34332F for every sound display, accent
  #E1774F for live position / focus / active marks; spectral amber → gold → coral → rose →
  lavender → mineral; layer identities A warm coral-amber, B blue-grey lavender, C sage.
  Knobs: neutral bodies, charcoal indicator, a thin value arc in the layer's (or the
  accent) colour; flat raised buttons with a hairline and a 1 px contact shadow.
- `EngineCard` (one class for A/B/C; Hero / Dual / Triple): header (letter badge in the
  layer colour when edited, root, file, mode selector, ⋮ menu); `SourceDisplay` (waveform
  coloured per moment by its spectral centroid leaning to the layer colour, loudness ghost,
  time grid, START marker and skipped region, loop bracket, read heads, POS/SPREAD and
  grains, drop label); START TUNE PAN LEVEL (TUNE snaps to semitones, Alt-drag fine);
  LINK REVERSE LOOP FOLLOW (LOOP suppressed in Granular); POS SIZE DENS TUNE SPREAD over
  the display in Granular. Unfocused cards: waveform saturation × 0.55.
- Adaptive source area: 0 → full-width DROP A SOUND (Browse / Load example); 1 → one
  full-width card; 2 → two equal cards; 3 → three. Layout is derived from occupancy (state),
  never stored. New layers fade in, cards glide (180–200 ms, `ComponentAnimator`).
- Drag & drop: while a sound is dragged and there is room, the next layout is previewed
  with an ADD LAYER target beside the cards; a card shows REPLACE X; with three, only
  replace. Loose files = one layer each (up to three, the rest reported); a folder = one
  multi-sample layer. Replacing resets the root to automatic and keeps the layer's controls.
- Mix band (fixed height): ORIGINAL ↔ REIMAGINED always; + A/B blend (track in the two
  layers' colours) for two layers; + mix triangle with power-share readout for three.
- Header: OSP/2-OSP + "ONE SOURCE / TWO LAYER / THREE LAYER INSTRUMENT", preset bar
  (‹ name ♡ ›: starting states then the user's presets; favourites in
  Presets/Favourites.txt), VOLUME (the former Output), ⋮ menu (add layer, the edited
  layer's replace / example / root / samples / remove, restore removed layer, presets,
  instruments, undo, size, Advanced).
- Lower panel: the five macros + AMP ENVELOPE (graph with draggable points, A D S R).
  Keyboard: warm white / soft graphite keys, coral-tinted held notes; PITCH and MOD wheels.
- Advanced keeps Fine, Bend, Pitch Character, MPE, Seed. Output → header VOLUME;
  Sustain → each layer's LOOP; velocity range → DYNAMICS popup RANGE (Attack/Release left
  that popup for the envelope).
- Tests: UI test (Xvfb) for 0/1/2/3 cards, drop targets (drop / replace X / add X), drag
  preview, compaction, smallest and largest window sizes; snapshots of every state.

## Stages 8–9 — source modifiers, the instrument's envelope

- LINK (plugin, message thread): `applyLinkedDelta` — a user gesture on a linked layer's
  START / TUNE / PAN / LEVEL moves every other linked, occupied layer's same control by the
  same delta (clamped); automation never propagates; only those four controls link.
- REVERSE / LOOP / FOLLOW: see Stages 1–3 (voice + granular); LOOP is shown suppressed in
  Granular (granular sustains by itself; the engine ignores LOOP there).
- ADSR: `attack`, `decay`, `sustainLevel`, `release` drive the one envelope every voice of
  every layer uses (shared instrument envelope; per-voice as before, so note stealing and
  release grafts keep working). A sustain level changed during a held note now glides there
  over ~10 ms (no step); unchanged, the value stays exactly the level (baselines and goldens
  unchanged). S = 0 turns any layer, One Shot or Granular, into a pluck that ends while held.
- Tests: live sustain change, decay → sustain ratio through the engine for One Shot and
  Granular layers, Granular START offset and FOLLOW lift, LINK deltas / clamping / scope.

## Stages 10–15 — popup framework and the five macro visualisations

- Shell (`MiniPanel`, ShapingPopups): raised #F3F1EA card, hairline, a short soft shadow;
  title + a one-line question as subtitle; close button (closed asynchronously); one
  popup at a time; closes on Escape, a click elsewhere in the editor or its own name;
  opens with a 110 ms fade and 4 px settle; never resizes the window. Mode tabs
  (`SegmentedControl`): the chosen one sits in with accent text and rim.
- `Visual` base: graphite well (the same deep graphite as the sound displays — the spec's
  #E9E7E0 light recess was tried against the references, where colour has to read as
  light; the graphite wells match the approved images), repaints at 30 Hz only while open
  and only when its picture moves or its parameters changed, fades 140 ms between kinds.
- SPACE (built first, the benchmark): transient at 0, early reflections at the type's real
  times and levels (`SpaceReverb::portrait`), the dense tail drawn in dB (straight to
  −60 dB at DECAY), filled in at the diffusers' rate, wider for wider types, rippled for
  SPRING; colour from amber through rose to mineral, losing saturation towards silence,
  faster for darker (more damped) types; −60 dB marker; SPACE's amount sets the tail's
  strength. Cached per (type, decay, amount, size). Controls: TYPE, DECAY only.
- CHARACTER: log-frequency response (20 Hz–20 kHz, −36..+15 dB) of the CharacterFilter
  designs (ladder with its resonance feedback and compensation, SVF LP/HP/BP with their Q
  laws, TILT shelves), cutoff where CHARACTER puts it between MIN and MAX, the envelope's
  peak as a dashed ghost, the edited layer's own average spectrum (computed on the loader
  thread: 48 Hann frames, 96 log bins, `LoadedInstrument::spectrumDb`) faintly in spectral
  colours. Controls: MIN MAX RES, then smaller DRIVE ENV ATTACK DECAY.
- MOVEMENT: DRIFT — the voices' shared value noise (`shaping::sharedWander`) at SPEED for
  pitch and tone; TAPE — wow (0.7 sin + wander) and flutter at their Hz, WEAR roughness;
  CHORUS — the two taps' phase-offset sines (STEREO separates them, WIDTH their depth);
  PULSE — the bus' gain curve tanh(k sin)/tanh(k) at MOVEMENT's depth; SHAPER — the pattern
  contour with SMOOTH, step grid and the DSP's own host-synced phase (`shaperPhase`).
- LIFE: a cloud of seven contours of the same note; LIFE (× mode) spreads them in height
  (PITCH), onset (ATTACK), body and colour (TONE); FRAY strays one; the strongest is the
  newest note's and it changes with every note played.
- DYNAMICS: level against velocity through the curve and the effective range (Advanced
  range × DYNAMICS as `levelRangeDb`), the linear response dashed, a brightness halo
  towards hard notes (TONE × DYNAMICS), the last 16 played velocities as dots (lock-free
  ring written by the audio thread). Controls: CURVE, RANGE, TONE.
- UI test snapshots every SPACE type and MOVEMENT mode.

## Stages 16–23 — migration, polish, regression, automation, UI performance, edge cases

- Migration tests: a v5 single-layer session with the old "Recording" sustain and a v5 A/B
  session with its blend open bit-identically (into an instance that had three layers and
  moved controls: C is cleared, everything new returns to neutral); v6 three-layer
  sessions recall every layer control, the mix and the envelope.
- Polish: at the minimum size (900 × 720) three Granular layers still show their
  waveforms (the granular controls float on a translucent band when a display is short);
  the lower sections scale a little with the window height, never with the layer count.
- Regression: all eight One Shot / Granular combinations of three layers (playable,
  block-size independent, release to silence); every modifier on three layers while the
  triangle mix sweeps, the sustain level moves and a LEVEL jumps from silent to +6 dB
  (finite, the largest sample step < 0.35 × peak, all voices end after release).
- Automation (plugin): 22 controls automated at random across their full ranges every
  2.7 ms for 4 s with notes held, plus a MOVEMENT mode switch: finite, largest step 0.16 ×
  peak (a click would be ~1×).
- Edge cases: replacing a sound while its note plays (the note finishes on the old one,
  the root returns to automatic); a broken file among good ones (its layer fails and
  stays empty, the others load and play); 3 ms / 20 ms / 80 ms sources in every mode,
  forwards and reversed, with START and FOLLOW off; removing layers while notes sound.
- UI performance: popups repaint only while open and only when their picture changed;
  the editor no longer repaints knobs, the envelope graph or the mix band every tick, and
  favourites are read from disk once.
- Sanitizers: the core suite and the plugin tests built with AddressSanitizer +
  UndefinedBehaviorSanitizer (see the final report for the result).

## Stage 24 — final acceptance and report

### Architecture
- **Engine slots.** `InstrumentEngine` owns a fixed `std::array<Slot, 3>`; nothing about
  a slot is allocated after `prepare()`. A slot is occupied when it has a model (or set);
  the layout, the mix law and the voice allocation all follow occupancy, never a stored
  "layer count".
- **Routing 1 / 2 / 3.** Every note-on starts one voice per occupied, audible slot (the
  same event index, salted per slot for determinism). Each slot renders into its own
  buffer, is weighted (`mixWeights`), scaled by LEVEL and balanced by PAN, summed, then
  goes through the one shared PostProcessor (Reimagined → MOVEMENT bus → SPACE) and VOLUME.
- **Mixer.** One layer: unity. Two: equal-power `cos`/`sin` of the A/B blend between the
  two occupied slots (whichever they are). Three: barycentric shares of the triangle
  position taken as power, so total power stays constant (centre ≈ −4.8 dB each; an edge
  midpoint equals the two-layer middle). Gains glide per sample, never faster than 10 ms
  for a full jump.
- **State.** stateVersion 6 adds the `InstrumentC` tree; parameters gained `layerC.*`,
  per-layer `start/tune/pan/level/link/reverse/loop/follow`, `mix.x`, `mix.y`, `decay`,
  `sustainLevel`. Sessions older than v6 are migrated on load (below).
- **UI layout model.** The source area is derived from occupancy every time it changes:
  0 → drop zone, 1 → Hero card, 2 → two Dual cards, 3 → three Triple cards. One
  `EngineCard` class serves A, B and C; density changes sizes and arrangement, never what
  is there. The mix band, macro row, envelope and keyboard keep their place and height
  whatever the layer count.

### DSP
- **Source modes.** One Shot (band-limited read with continuation loops and release graft)
  and Granular (24 Hann grains around POS), per layer, freely combined.
- **Per-layer processing.** START (offset into the recording with a 3 ms fade-in; added to
  POS in Granular), TUNE (± 24 st), PAN, LEVEL (−48 dB = silent, not rendered), REVERSE
  (reads backwards; a mirrored loop when LOOP is on), LOOP (sustain by continuation; off =
  the recording plays out once, no per-voice drift, as the old "Recording" sustain),
  FOLLOW (on = the recording's own loudness contour; off = that contour flattened, at most
  +24 dB, never lifting the noise floor). LINK moves linked layers' START/TUNE/PAN/LEVEL by
  the same user delta (UI only; automation never propagates).
- **ADSR.** `attack`, `decay`, `sustainLevel`, `release` shape every voice of every layer.
  A sustain change during a held note glides over ~10 ms.
- **Macro routing.** Unchanged: LIFE, DYNAMICS, CHARACTER and DRIFT per voice on every
  layer; Reimagined, MOVEMENT (TAPE/CHORUS/PULSE/SHAPER) and SPACE once on the sum.
- **Movement / Shaper.** As in MOVEMENT v2, on the summed signal, host-synced; the earlier
  host crash fix (pattern/rate menu) is kept and covered by a UI regression test.
- **Blend normalisation.** Equal power for two layers, power-share for three; one layer is
  always unity regardless of the blend or triangle position.

### UI
- **Four adaptive states.** Empty (drop a sound / Browse / Load example), one layer
  (Hero), two (Dual, A/B blend in the mix band), three (Triple, mix triangle with share
  readout). Transitions glide (180–200 ms); dragging a file previews the next layout.
- **EngineCard.** Header (letter, root, file, mode selector, menu), the sound display,
  START TUNE PAN LEVEL, LINK REVERSE LOOP FOLLOW; Granular puts POS SIZE DENS TUNE SPREAD
  over the display (on a translucent band when the display is short).
- **Palette.** Warm housing, graphite displays, one coral accent for live/focus marks,
  layer identities A coral-amber, B blue-grey lavender, C sage; spectral colours from
  amber to mineral. Typeface Inter with tabular figures.
- **Waveform.** Each moment coloured by its spectral centroid (leaning to the layer
  colour), loudness ghost, time grid, START and skipped region, loop bracket, read heads,
  POS/SPREAD and live grains. Static part cached; overlays at 30 Hz.
- **Macro popups.** A shared shell (title, one-line subtitle, close, one at a time, Escape
  or an outside click closes) with a live graphite visualisation per macro: SPACE
  (reflections and dB tail), CHARACTER (filter response over the layer's spectrum),
  MOVEMENT (one picture per mode, SHAPER at the host phase), LIFE (contour cloud),
  DYNAMICS (velocity curve with the last 16 velocities).
- **Header / keyboard.** Preset bar (‹ name ♡ ›), VOLUME, the menu; the keyboard with
  PITCH and MOD wheels; AMP ENVELOPE with draggable points beside the macros.

### Compatibility
- **Old state.** v1–v5 sessions load: layer controls neutral, C empty, mix centred,
  sustain 100 %, the old global Sustain becomes every layer's LOOP. A v5 single-layer
  session and a v5 A/B session render bit-identically to before (tested).
- **Automation IDs.** No existing ID was renamed or removed; `layerA.*`, `layerB.*` and
  `ab.blend` keep their IDs, so existing automation still reads. New IDs carry version
  hint 7. Sustain and Output stay as parameters (Output is the header's VOLUME).
- **DAW testing.** Not possible here (Linux container, no host). Covered instead by the
  headless plugin tests (state round trips, migration, automation stress, sample-rate /
  buffer matrix). The CI build runs `auval` on macOS. Needs a pass in Ableton / Logic.

### Performance
CPU_TABLE_PLACEHOLDER

### Sanitizers
SANITIZER_PLACEHOLDER

### Known issues
- No listening pass yet: the mix law, FOLLOW's lift and the START fade are set by ear on
  test material only.
- Two of the seven Stage 0 reference renders differ, inaudibly and for documented reasons
  (blend set before the first block; a silent layer no longer rendered).
- Removing a layer clears the undo history (slots move); "Restore removed layer" brings
  back the last one only.
- CHARACTER's per-note sweep is not animated in its popup (the envelope's peak is drawn
  as a ghost instead).
- Macros stay enabled when no sound is loaded.
- Three One Shot layers with 16 held notes is the heaviest case (see Performance).
