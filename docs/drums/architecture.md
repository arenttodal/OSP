# ANDOR Drums — architecture, audit and compatibility boundary

ANDOR Drums is a second plugin built from this repository, beside ANDOR/OSP. It reuses OSP's
engine, effects, loader and design system. It is not a fork, and it is not a rewrite of OSP.
This document records the Milestone 0 audit, the design chosen for nine pads, what was
shared and how, and the rules that keep OSP independent.

## 1. Audit (Milestone 0, base commit 31aada0)

| Area | What exists | Ready for nine pads? |
|---|---|---|
| Build | CMake ≥ 3.25, JUCE 8.0.9, Catch2 3.9.1, signalsmith-stretch 1.4.0 (FetchContent). `osp_dsp` (pure C++), `osp_library` (SQLite), `osp_research`; plugin behind `OSP_BUILD_PLUGIN`. | Yes. A second `juce_add_plugin` target fits beside OSP's. |
| OSP identity | `PRODUCT_NAME "OSP"`, `BUNDLE_ID com.osp.instrument`, manufacturer `Ospx`, plugin code `Osp1`, state tag `OSP` (stateVersion 18). | Untouched. Drums has its own identity (below). |
| Playback | `InstrumentEngine` (engine C): 3 layers (A/B/C), 208 voice slots, per-voice LIFE / DYNAMICS / CHARACTER / REIMAGINED / drift / ADSR, per-layer EQ and REIMAGINED stage, then one shared `PostProcessor` (DRIVE, MOVEMENT, ECHO, SPACE). `BaselineSampler` (baselines A/B) is separate. | **No, as one engine.** Every note starts a voice on *every* occupied layer, because A/B/C are layered, not routed. Three slots is also a hard `std::array` size. |
| Round robin / velocity | `InstrumentSet` + `SampleSetInference`: pitch groups → velocity layers → round-robin takes (never the previous take). | Yes, per pad, as a set per engine. Members keep their own detected roots, so unpitched drum takes need one forced root (Milestone 3). |
| Pitch | `SincInterpolator`, 16 zero crossings; Tape / Natural pitch characters; root correction as a layer pitch offset. | Yes. Drums use Tape (speed and pitch together). |
| Envelopes, modulation | One ADSR per engine; `mod::` system with 16 routes, 2 LFOs, 2 envelopes, macros; per-voice and shared sources. | Per pad: yes (each pad has its own engine). A kit-wide modulation policy is Milestone 8 work. |
| Effects | `PostProcessor`: REIMAGINED (legacy shared), DRIVE, MOVEMENT bus, ECHO, SPACE. Bypassed or asleep at 0. | Yes, once, for the kit (see design). |
| Arpeggiator | `engine/Arpeggiator`: a note stage in front of the engine. | Not used by Drums V1. |
| Loading | `apps/plugin/Source/InstrumentLoader` (content-addressed `SampleStore`, cached analysis, staged builds) + `LoadedInstrument`; `ModelExchange` hands immutable models to the audio thread. | Yes. It depends only on JUCE core and `src/`, so Drums compiles the same file. |
| Library | `src/library`: catalog, preview, tagging, **Slicer**, **Classifier**, folder scan, packs. | Reusable later for break slicing (M6) and classification (M7); not wired in yet. |
| Presets / state | OSP state XML (`OSP`, stateVersion 18); `.osppreset`, `.ospinstrument`. | OSP-specific. Drums has its own versioned state (below). |
| GUI | `Design.h` (palette and layout measured from the approved references), `OspLookAndFeel` (fonts, knobs, buttons, combos), Inter/Outfit fonts as binary data. The editor itself is OSP's single-instrument layout. | Palette, fonts and knobs: yes. Layout: no. Drums has its own editor. |
| Blender knob | Not in the committed code. It is an open item in STATUS (`tools/blender-assets`, `docs/reports/knob-phase1.md`) and still being committed from another session. | Drums uses `OspLookAndFeel`'s knob, so the rendered knob arrives in both products through the same switch once it lands. |
| Tests | 113+ Catch2 cases (unit / integration / regression goldens) + performance smoke; plugin tests headless (xvfb for `[ui]`). | Kept green; Drums adds its own executables and CTest entries. |

### Constraints found

1. **Layers are not pads.** One `InstrumentEngine` cannot host nine independently triggered
   sounds without changing how every OSP note is played. Widening its layer array or adding
   note routing inside it would touch the code every OSP session renders through.
2. **One post stage per engine.** Nine engines with their own post stages would mean nine
   reverbs, delays and saturators.
3. **Voice memory is fixed.** Each engine allocates 208 voice slots × 19 KB (the REIMAGINED
   mode engines live inline in every voice), so 4.0 MB per engine.
4. **Set members keep their own roots.** For pitched instruments that is correct. For drums,
   round-robin takes with different detected "pitches" would be transposed against each other.

## 2. Design: a kit of OSP engines

```
 MIDI ─► DrumKitEngine (src/drums, pure C++)
           key map: PAD (note → pad) or EXPANDED (zone → pad, semitones from the root)
           choke groups, mono retrigger, one-shot / gate, note-off bookkeeping
           ├─ pad 1: InstrumentEngine (layer A; postStage = false)  ─┐
           ├─ pad 2: InstrumentEngine                                ├─ pad gain × pan × mute/solo (10 ms ramps)
           ├─ …                                                      │
           └─ pad 9: InstrumentEngine                               ─┘
                                   ▼ sum
                     ONE PostProcessor (DRIVE, MOVEMENT, ECHO, SPACE) ─► kit VOLUME
```

- **Every pad is a complete, unmodified OSP engine.** Its voices, LIFE, DYNAMICS,
  CHARACTER, REIMAGINED, EQ, sets and round robins are OSP's own code. Nothing is
  re-implemented as a "simpler generic sampler".
- **Two small additions to `InstrumentEngine`** (default behaviour unchanged, so OSP is
  bit-identical; the golden and plugin suites pass unchanged):
  - `EngineSettings::postStage` (default `true`). When it is false, the engine skips its post stage
    so a host can run one for several engines.
  - `fadeOutAll (seconds)`: a short fade on every voice, used for chokes and mono retrigger.
- **Routing.** `DrumMapping` resolves a note to hits `(pad, semitones)`. The engine plays
  `round(root) + semitones`, and the pad's layer pitch offset cancels the fractional root,
  so semitone 0 is exactly the recording's own pitch. A table remembers which engine note
  each source note started, so a note-off always ends what its note-on began, even across a
  PAD/EXPANDED switch.
- **Shared versus per pad.** Kit-wide: SPACE, ECHO, DRIVE, MOVEMENT, VOLUME, and the kit's
  default LIFE, DYNAMICS and CHARACTER. Per pad: gain, pan, tune, mute/solo, envelope, voice
  mode, trigger mode, choke group, START, REVERSE, and optional overrides of LIFE, DYNAMICS,
  CHARACTER and REIMAGINED. Per-pad REIMAGINED uses OSP's per-layer routing inside the pad's
  engine. The shared REIMAGINED stage is held at 0.
- **Determinism.** Each pad's seed is `Prng::deriveSeed (kitSeed, "DRMPADS", pad)`. Every
  pad renders every block, so output does not depend on block size (tested at 64 and 480
  samples with SPACE and ECHO on). Performance memory restarts when the transport starts.
- **Real-time safety.** `prepare()` allocates. Note handling, mixing and settings changes
  do not. The plugin hands over sounds with one `ModelExchange` per pad, and the key map and
  pad modes with a seqlock of atomics. Mouse auditions go through an `AbstractFifo`. A
  replaced sound stays alive in up to 8 retired slots per pad until no voice reads it.

### Why not extract `shared/` and `products/` directories now

The suggested layout (`shared/audio`, `products/OSP`, ...) would move most of `src/` and
`apps/plugin/Source`. That is a large churn with no behavioural gain, and it risks OSP. The
boundary is instead expressed in the build:

| Layer | Target | Linked by OSP? | Linked by Drums? |
|---|---|---|---|
| Shared DSP and engine | `osp_dsp` (`src/core`, `audio`, `analysis`, `engine`, `model`) | yes | yes |
| Shared library | `osp_library` | yes | yes (not used yet) |
| Shared plugin code | `apps/plugin/Source/{InstrumentLoader, Design, OspLookAndFeel}.cpp`, `OSP_PluginData` (fonts) | yes | yes, compiled again with Drums' JUCE settings |
| Drums only | `osp_drums` (`src/drums`), `apps/drums` | **no** | yes |

Files are extracted only when Drums actually needs to change one. In that case the shared
part moves out with tests for both products (CLAUDE.md Rule 6).

## 3. Product identity

| | ANDOR/OSP | ANDOR Drums |
|---|---|---|
| CMake target | `OSP_Plugin` | `ANDOR_Drums` |
| Product name | OSP | ANDOR Drums |
| Bundle id | com.osp.instrument | com.osp.andordrums |
| Manufacturer code | Ospx | Ospx (same maker) |
| Plugin code / AU subtype | Osp1 | **Adrm** |
| VST3 class id | derived from Ospx+Osp1 | derived from Ospx+Adrm |
| VST3 category | Instrument, Sampler | Instrument, Drum, Sampler |
| State root tag | `OSP` (stateVersion 18) | `AndorDrums` (schemaVersion 3) |
| Parameter IDs | OSP's (unchanged) | `keyboardMode`, `kit.*`, `padN.{gain,pan,tune,mute,solo}` |
| Version | 0.1.0 | 0.1.0 (own history in docs/drums/CHANGELOG.md) |

A `<Groove>` element keeps the last applied break's timing (`m6-breaks.md`). It is
additive, so the schema stays 3.

Kit files (`.andorkit`, root `AndorDrumsKit`, `kitSchemaVersion` 1) carry the same
children as the session state. Portable kits add `kitPath` per sound
(`m2-kits.md`).

Both plugins can be installed together. An OSP session loaded into Drums is ignored
(tested), and OSP never sees Drums' tag.

**Shared on purpose:** the content-addressed sample store (`<app data>/OSP/Samples`, keyed
by SHA-256). The same file imported by either product is stored once. Settings and presets
are not shared. Drums has no preset files yet (Milestone 2), and they will get their own
extension and schema.

## 4. State (schemaVersion 3)

```xml
<AndorDrums schemaVersion="3" product="ANDOR Drums">
  <Parameters …/>                          APVTS: keyboardMode, kit.*, padN.*
  <Pads><Pad id="1" name="" category="kick" choke="0" voice="mono" trigger="oneShot">
          <Sound hash="sha256:…" filename originalPath rootMidi rootOrigin startSeconds gainDb/>
          <Layers mode="roundRobin|velocity|stack" order="shuffle|sequential|random">
            <Layer hash filename originalPath rootMidi rootOrigin startSeconds gainDb
                   name level start end velocityLow velocityHigh/>…
          </Layers>
        </Pad>…</Pads>
  <KeyMap allowOverlap="0"><Pad id="1" note="36" low="12" high="23" root="-1"/>…</KeyMap>
  <Order slots="1,2,…"/>
  <View layout="1x8" kitName="" selectedPad="1"/>
</AndorDrums>
```

History:

- **State 2** added `<Order>` and the per-pad envelope, START and REVERSE parameters.
- **State 3** added `<Layers>` (`m3-layers.md`). `<Sound>` still names the first loaded
  layer, for older builds. States 1 and 2 open as one layer per pad. A state-2
  `<Missing filename/>` is kept as a missing layer.

Pads are stored by stable id, never by screen position. Sounds are stored by content hash,
with the original path as a fallback. The exact playback model (root, start, gain) is stored
too, so a reopened project is bit-identical (tested). A missing sample is reported and
remembered. It is never substituted.

## 5. Measurements (Milestone 1)

`osp_drums_tests "[perf]"`, 48 kHz, 256-sample blocks. The container's CPU is a shared
2.1 GHz Xeon. For scale, OSP's own baseline sampler uses 35 % of a block for 24 voices on it
(`performance-smoke`).

| Load | Mean | Worst | Peak voices |
|---|---|---|---|
| Idle kit (9 sounds) | 54 µs (1.0 %) | 237 µs | 0 |
| Idle, shared effects on | 153 µs (2.9 %) | 1.2 ms | 0 |
| 16ths at 120 BPM, 2 pads/step, effects on | 694 µs (13 %) | 2.4 ms | 22 |
| 32nds at 120 BPM, 3 pads/step, effects on | 1.56 ms (29 %) | 3.7 ms | 51 |
| Stress: a hit every 5 ms, effects on | 2.2 ms (42 %) | 20 ms* | 75 |

\* A single scheduling spike on a shared machine. Worst-case numbers need a quiet Mac
(Milestone 8).

The cost per playing voice is OSP's voice (about 25–30 µs per 256-sample block here). The
idle cost of nine engines is about 1 %.

**Memory:** OSP allocates 9 × 208 voice slots × 19 KB ≈ 36 MB of preallocated voices. Since
M8, Drums constructs each pad's engine with 48 slots (`InstrumentEngine (int)`, an inert
addition; OSP keeps its default), about 8 MB per kit. See `m8-integration.md`.

## 6. Editor (V1.1)

`DrumsEditor` scales one `DrumsContent` laid out at 1280 x 900 (fixed aspect, 0.7-1.5x).
`PadGrid` / `PadView` (one cell component, geometry only from the grid), `SamplePanel`,
`EnvelopePanel`, `KeyMapEditor`. Every map edit is a validated `drums::` command applied
through `DrumsProcessor::applyEdit` (one `juce::UndoManager` transaction holding the
arrangement before and after: both key maps, the pad order, the names). The audio thread
sees the result through the existing seqlock. MIDI Learn and the held-note display use a
lock-free FIFO and atomic bits written by `processBlock`. Details and test coverage:
`v1.1-report.md`.

## 6b. Shared design language, separate downloads (2026-10-13)

ANDOR Drums and OSP are separate plugins that share one design language. What the two
share, and what stays each plugin's own:

- **Shared, from one source.** OSP's typefaces (Inter, Outfit) and the rendered macro knob
  (`apps/plugin/Resources/fonts`, `apps/plugin/Resources/knobs`), together with the shared
  drawing code Drums compiles (`OspLookAndFeel`, `Design`, `SpriteKnobRenderer`,
  `CeramicSkin`). A change to the shared design language there reaches both plugins at their
  next build.
- **Each plugin's own embedded data.** Drums embeds `ANDOR_DrumsData` (about 12 MB: the fonts
  and the knob's two sizes). OSP embeds `OSP_PluginData`, which also holds OSP's
  ceramic skin pack. Drums draws its own ceramic look (`Source/CeramicSkin`), and the shared
  drawing code falls back to vector drawing without the pack.
- **Merging OSP into Drums does not grow Drums.** After the 2026-10-12 merge, each Drums
  binary grew by the 12.5 MB skin, to 37.9 MB (Linux VST3; a universal Mac binary holds the data
  twice). With its own data it is back to 25.4 MB.
- **A guard.** The test `[drums-plugin][size]` fails if OSP's skin is embedded again, or if the
  embedded data grows past its 13 MB budget.

## 7. Known limitations at this checkpoint

- ~~Several files dropped on one pad load only the first.~~ Done in M3 (`m3-layers.md`).
  For sample layers there are two additive OSP engine hooks, both inert unless set:
  `InstrumentEngine::NoteOverride` and `InstrumentVoiceStart::endFraction`.
- Pad polyphony is applied at `prepare()` (the host's next prepare), not live.
- A note transposed past MIDI 0..127 from the pad's root is clamped (about ±60 semitones).
- REIMAGINED on short transients: not yet evaluated (Part 8.5 list, Milestone 5).
- DAW hosts (Logic, Live, REAPER, auval) are untested here (no Mac in this container).
