# Per-layer REIMAGINED: GUI and DSP migration

This report covers the GUI and DSP migration to a REIMAGINED knob on every card, and how
older patches keep their sound.

## What existed before

The spec assumed one global Original <-> Reimagined parameter. In the repository it was
already partly per layer:

- **Parameters.** A's amount was `reimagined` (the original global ID). B and C had their
  own `layerB.reimagined` and `layerC.reimagined` since state v7, and v7 migration gave
  older sessions one amount on every layer.
- **Per-voice part.** Each layer's own amount drove continuation segments, saturation,
  the doubling head, Reimagined grains and drift.
- **Bus part.** One shared stage after the mix ran the resonator bank, the remap bank a
  fifth above and the wandering formants. It took the layers' amounts weighted by their
  power in the mix. This is the part that is not linear in the layers:
  `Reimagined(A + B) != Reimagined(A) + Reimagined(B)`. The integration test
  "per layer, each layer's amount shapes only that layer" demonstrates it.

"Legacy global" therefore means exactly this path: per-voice per layer, plus one
mix-weighted bus stage.

## Legacy compatibility

- **Schema.** The plugin state is now version 8. It stores `reimaginedRouting`
  ("perLayer" or "legacyGlobal").
  - A state without it (stateVersion < 8) opens as legacy.
  - A legacy patch saved without touching REIMAGINED stays legacy.
  - `.ospstate` starting states work the same way.
  - The factory starting states (programs) set legacy, because they were made with the
    shared stage.
- **Parameters preserved.**
  - `reimagined`, `layerB.reimagined`, `layerC.reimagined` and `reimaginedLink` keep their
    IDs and ranges. `reimaginedLink` is stored but unused.
  - Host names are now "A Reimagined", "B Reimagined" and "C Reimagined". IDs are
    unchanged, so automation is unaffected.
  - Only the defaults moved, from 20 % to 0 %. That only affects new patches, because
    every saved session contains the value.
  - In the legacy path, host automation of `reimagined` drives what it always drove: A's
    per-voice part and its share of the shared stage.
- **Audio comparison** (MIG-01 to MIG-05). There are twelve reference scenes in
  `tests/audio/reimagined-before/`:
  - Reimagined 0 / 25 / 50 / 75 / 100 % on one layer.
  - Granular.
  - Two layers at 50 %.
  - A 20 % / B 80 % with a strong blend.
  - Mixed modes.
  - CHARACTER + MOVEMENT (tape) + SPACE.
  - Three layers at 0 / 50 / 100 %.
  - Host automation of `reimagined` from 0 to 100 % while playing.

  Each scene is set up, saved, stripped of routing (as an older build saves it), reopened
  and rendered.
  - **Null test.** I wrote 32-bit float WAVs with the build before any change and with the
    final build. All twelve are sample-identical: max |before − after| = 0.
  - **Regression test.** `plugin: sessions from before per-layer Reimagined render as they
    did` keeps checking the stored hash, RMS, peak and brightness. Off the reference
    platform it falls back to metric tolerances.
  - **Goldens.** Unchanged.
- **Differences.** None. Every legacy scene is sample-for-sample equivalent.

## New architecture

- **ReimaginedStage** (`src/engine/ReimaginedStage.*`) is the one implementation of the bus
  part, extracted unchanged from PostProcessor (MIG-03 was proven bit-identical before
  anything else changed).
  - PostProcessor owns one instance: the legacy shared stage.
  - Each layer slot owns one: per-layer routing.
- **Per-layer routing.**
  - **Placement.** Each layer's stage runs on that layer's rendered signal, before LEVEL,
    PAN and the mix. Its resonances come from its own model, and its formant path has its
    own seed. The shared stage is held at 0.
  - **Bypass.** A stage at 0 % is skipped. Above 0 it runs every sample, also on silence,
    so tails ring out and output stays block-size independent (tested with 512 vs 37
    sample blocks).
  - **Legacy runs nothing extra.** The layer stages never run in legacy.
- **Resulting chain:**
  1. SOURCE
  2. One Shot / Granular
  3. START / TUNE
  4. Per-voice stages: LIFE, DYNAMICS, CHARACTER filter, Reimagined per-voice part, drift,
     ADSR (all unchanged)
  5. Layer REIMAGINED stage
  6. LEVEL / PAN
  7. Mix
  8. MOVEMENT bus
  9. SPACE
  10. VOLUME

  LIFE, DYNAMICS, CHARACTER, MOVEMENT and SPACE are untouched and stay shared.
- **Conversion.** `OspAudioProcessor::convertLegacyReimaginedToPerLayer()` is the only
  switch.
  - **What triggers it.** A gesture on one of the three REIMAGINED parameters: the knob,
    its double-click reset, the mouse wheel, LINK carrying an edit to another layer, or a
    host's control surface.
  - **What never triggers it.** Loading, opening the editor, slider attachments, presets
    being inspected, or automation playback (which sends no gestures).
  - **Values.** Amounts are kept, so the knobs do not move.
  - **Automation afterwards.** Automation of `reimagined` then drives A's REIMAGINED (it is
    A's parameter); it does not fan out to B and C.
  - **Undo.** The routing switch itself is not undoable. Undo returns the value, not the
    routing.
- **New patches.** A fresh instance, INIT and Reset settings are per-layer with every
  REIMAGINED at 0 %. A layer added later still starts at A's amount, as before.
- **CPU.** Engine benchmark, 4 notes at 48 kHz, % of one core in real time, measured on
  this container:

  | Mode | Routing | 1 layer 0 % / 100 % | 2 layers 0 % / 100 % | 3 layers 0 % / 100 % |
  |---|---|---|---|---|
  | One Shot | legacy | 7.2 / 14.0 | 13.6 / 29.4 | 18.1 / 45.1 |
  | One Shot | per layer | 7.2 / 16.1 | 14.6 / 30.6 | 17.6 / 41.5 |
  | Granular | legacy | 5.5 / 8.1 | 11.4 / 13.1 | 14.4 / 17.2 |
  | Granular | per layer | 5.6 / 8.7 | 9.2 / 13.5 | 13.8 / 18.4 |

  The per-voice part dominates. The extra stages cost about 1–2 % per active layer and
  are within run-to-run noise. No quality was reduced.

## GUI

- **Five-knob row.** START TUNE PAN LEVEL REIMAGINED, all at 87 % of the four-knob size
  (one family).
  - **Spacing.** One pitch, with 0.62 pitch at the row ends (more than half the gap
    between neighbours).
  - **REIMAGINED identity.** The same knob body, with a coral arc, a small coral light
    after its name and slightly stronger label contrast. The tracking is a touch tighter;
    the weight is unchanged.
  - **Tooltip.** "Reimagined: how far this source moves from its original character".
- **Hero, dual and triple.**
  - **Hero (one source).** No mix band at all. The card is 66 px taller (the display
    takes it) and the rest is air above the macros.
  - **Dual.** The 2 × 2 modifiers keep their place behind the hairline; the five knobs sit
    left of it.
  - **Triple.** The modifiers stay in their row under the knobs.
  - **Labels.** Never abbreviated; REIMAGINED fits in triple.
- **Mix band.**
  - The central ORIGINAL ↔ REIMAGINED track and its link key are removed, with no
    placeholder.
  - The band is 72 px (was 83), with more space around it.
  - Two layers show only A / B BLEND on the centre line; three show the MIX triangle and
    shares.
- **Master volume.** A thin custom slider in the upper-right utility area: VOLUME and the
  value (e.g. "0.0 dB") above a 1.5 px recessed track, a barely warm active part and a
  12 px cream thumb with a rim and contact shadow.
  - It is bound to the same `gain` parameter, with the same range (−36…+12 dB), default
    (0 dB), dB law and place in the chain.
  - Double-click returns to 0 dB.
  - Test: output level follows dB exactly at −36 / −24 / −12 / −6 / 0 / +12.
- **Screenshot matrix** in `design/current/`:
  - `02-one-oneshot`: new patch, one layer.
  - `04-two-oneshot` and `05-two-mixed`: two layers.
  - `07-three`: three layers.
  - `25-new-a0-b100`.
  - `26-new-a20-b50-c90`.
  - `27-old-1-layer` and `28-old-2-layer`: older patches at 63 %, shown on every card and
    not converted by being displayed.

## Tests

- **Old presets.** The twelve legacy scenes against the references (hash and metrics);
  the null test is above.
- **New presets.** Fresh patches are per-layer at 0 %. Saving and reopening keeps
  per-layer and each layer's value.
  - Plugin level: A alone sounds identical whatever B's REIMAGINED is, and vice versa.
  - Engine level: per-layer layers add up exactly (|both − (A + B)| < 1e-5 of peak),
    while legacy does not.
- **Automation.** Host value changes and rendering never convert. A gesture converts and
  keeps every value. The legacy scene "one-automated" renders exactly as before.
- **DAW session recall.** Older sessions (state < 8) open legacy. An untouched resave
  stays legacy; a converted one reopens per-layer.
- **Programs and INIT.** Factory programs set legacy; INIT gives per-layer at 0 %.
- **Editor.** The central track is gone. Each card has its REIMAGINED dial. LINK on A and
  B carries an A edit to B but not to the unlinked C.
- **Not done here.** Opening real projects in Logic, Ableton and Reaper still needs a Mac
  (Stage 20).
