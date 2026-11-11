# One FX routing, FX rack and mixer for ANDOR OSP and ANDOR Analog: investigation

**Date:** 10 November 2026. **Status:** investigation only. No code was changed, built or committed.
**Inputs:** OSP `the OSP repository` at default `claude/charming-lovelace-n25fte` (117aa15); OSP
`claude/space-presets` at **adf8ed6** (what Analog `main` pins, read via `git show` and a scratch
worktree); OSP `origin/claude/analog-release` at d6cda7b; Analog `the analog repository` `main` at 4bc6c41.
Paths written `analog:` are in the Analog repository, `osp:` are in OSP, and `osp@sp:` are in OSP at adf8ed6.

Markers: **[V]** means I checked it in the code or in git. **[I]** means I inferred it and did not run or build anything.

---

## 0. Summary

1. **Nearly all of Analog's FX routing is Analog-owned code. It is not in OSP's shared code.**
   The DSP (`analog:dsp/fx`, 4,933 lines) is pure C++. It depends only on OSP's `core/Prng`,
   `model/ModelExchange` and `engine/PostProcessor`, so it ports to `andor_fx` almost unchanged [V].
   The UI and parameters live in `analog:product/Source/Fx*` (about 6,100 lines) and are tied to
   `AnalogEngine` and `ShellEditor` [V]. OSP's shared code holds only the hooks Analog needed. They
   are all on the **space-presets line**, not on OSP's default branch [V]:
   - the `PostProcessor::custom*` stage entry points;
   - `rack::Kind::fx`;
   - `ShellProduct::sourceAreaView`;
   - the racks and faceplates (`EffectRack`, `ParameterRack`, `RackControls`, `RackFaceplate`);
   - the latency timer and `outputPeak`.
2. **What "the Mixer" is in Analog.** It is the **ENGINE MIXER column of the routing view**:
   three ceramic knobs bound by `SliderAttachment` to the cards' existing LEVEL parameters
   (`osc1Level`, `osc2Level`, `layerB.level`). It has no DSP, no new IDs, and no mute, solo, pan
   or sends [V].
   - `analog:product/Source/FxRouting.cpp:562-582` creates the knobs.
   - `analog:product/Source/FxRouting.cpp:964` and `:1569-1612` lay out and paint the column.

   The header A/B MIX (`analog.blend`) is a separate control. It is **hidden** since 4b103de
   (`analog:product/Source/Product.cpp:348`, `p.headerMix = false`); the parameter still exists
   and plays. Per-card stems and buses (FX gate 6) were never built.
   `analog:docs/MANUAL.md:44,75` still describe the header MIX, so the manual is stale [V].
3. **OSP's own plugin cannot host any of this yet.**
   - OSP's default branch has no effects rack: 44 files in `shared/andor-ui/Source`, against 53 on space-presets.
   - OSP's default branch has no routed `PostProcessor` stages.
   - `OspAudioProcessorEditor` is not a `ShellEditor`.
   - The STATUS NEXT list on space-presets already lists "the effects rack in ANDOR OSP's own
     plugin" as not done (`osp@sp:docs/STATUS.md:801-812`).
4. **FX routing was not handled under the shared-features rule.** The rule
   (`osp:CLAUDE.md`; `analog:CLAUDE.md:7-11`) applies because FX routing re-routes SPACE and
   ECHO. FX routing landed in Analog only, and **no NEXT entry exists in OSP's STATUS on either
   line** [V].
5. **Recommendation.**
   - Run plan step 0 (reunite the OSP lines) first.
   - Then lift Analog's FX stack into OSP's shared tree in behaviour-neutral, opt-in PRs that only
     OSP consumes. This is safe during the Analog freeze, because Analog's pin does not move.
   - After Analog 1.0.1, repoint Analog and delete its copies. The gates are Analog's
     byte-identical goldens and 1.0 fixtures.
   - Do all of this **before** the plan's `filter-repo` step 2, so andor-core is born with one FX
     implementation.
   - Estimate: about 4-6 engineer-weeks, plus one Analog QA cycle.

---

## 1. Inventory: what Analog has

### 1.1 Effect types and pools [V]

`analog:dsp/fx/FxGraph.h:22-31` defines 14 types. 4 are the instrument's own and 10 are new.
The pools give 22 instances (`capacity { 1,1,1,1, 2,2,2,2,2,2, 1,1, 2,2 }`, `FxGraph.h:31`).
New effects have host parameters `analog.fx.<type>.<n>.<name>`
(`analog:product/Source/FxParameters.cpp:307-310`). The version hint is 28, and 29 for RETRO
(`:379`).

| Effect (UI name) | key | Pool | Modes | DSP class (`analog:dsp/fx`) | Params in golden |
|---|---|---|---|---|---|
| DRIVE / MOVEMENT / ECHO / SPACE | drive, movement, echo, space | 1 each | OSP's own | `osp::PostProcessor` (OSP `src/engine`) via `custom*` | 0 (OSP base IDs `drive.*`, `movement.*`, `echo.*`, `space.*`, plus `analog.movement.b.*` for lane B) |
| STEREO SPREAD | spread | 2 | HYPER / DIMENSION / BOTH | `StereoSpread` (Effects.h:202, FxModulation.cpp) | 26 |
| COMPRESSOR ("COMPRESSORS") | comp | 2 | CLEAN / FET 76 / MULTIBAND (OTT) | `Compressor` (Effects.h:160, FxDynamics.cpp) | 80 |
| EQUALIZER | eq | 2 | parametric, 4 RBJ bands, HP/LP | `Equalizer` (Effects.h:110, FxTone.cpp), `SpectrumTap` (Effects.h:44) | 58 |
| FX FILTER | filter | 2 | LP/HP 12-24, BP, NOTCH, AP, COMB ±, FORMANT I-III | `FxFilter` (Effects.h:135) | 20 |
| PHASER | phaser | 2 | CLASSIC / DEEP | `Phaser` (Effects.h:224) | 24 |
| FLANGER | flanger | 2 | CLASSIC / THROUGH ZERO (latent) | `Flanger` (Effects.h:244) | 24 |
| FREQUENCY SHIFTER | shift | 1 | SHIFT / BLUR (STFT, latent) | `FrequencyShifter` (Effects.h:263, FxSpectral.cpp) | 10 |
| CONVOLUTION | conv | 1 | REALISTIC / CREATIVE, 6 designed IRs | `Convolution`, `Impulse` (Effects.h:294-314) | 13 |
| UTILITY (missing from MANUAL §5) | utility | 2 | n/a | `Utility` (Effects.h:91) | 28 |
| RETRO (TAPE / VINYL / DIGITAL) | retro | 2 | TAPE cassette/reel, VINYL 33/45/78, DIGITAL 80s/90s/CODEC | `Retro` (Effects.h:371, FxRetro.cpp 994 lines) | 96 |

There are **379 `analog.fx.*` parameters** in total: 283 at v28 and 96 at v29. They sit at lines
166-544 of `analog:product/golden/parameter-schema.txt`, which has 544 lines; the 165 `analog.*`
lines before them are untouched.

"TAPE" is not an FX type. It is three different things:
- RETRO's TAPE personality;
- DRIVE, MOVEMENT and ECHO's TAPE modes, which are OSP's;
- the **TAPE deck** UI (reels) for ECHO TAPE and MOVEMENT TAPE. This UI is shared and opt-in:
  `rack::Faceplate::tapeDeck` in `osp@sp:shared/andor-ui/Source/MacroPopups.cpp` (6a3dd5d), set by
  Analog at `Product.cpp:376`.

### 1.2 Pieces: where DSP, UI and state live

| Piece | DSP | UI | Params / state | Owner and line |
|---|---|---|---|---|
| Graph model (CLASSIC/CUSTOM, series, parallel `send`/`linear`, splitters, edits, text form `v1 custom …`) | `analog:dsp/fx/FxGraph.{h,cpp}` (140 + 547 lines) | n/a | grammar schema 1 | Analog `dsp/` |
| Router (compile, ModelExchange hand-off, 5 ms duck, 20 ms bypass glide, tails, latency planning and alignment) | `analog:dsp/fx/FxEngine.{h,cpp}`; CLASSIC calls `legacy.process` untouched (`FxEngine.cpp:225`); CUSTOM calls `customReimagined/Drive/Movement/Echo/Space` (`:277-404`) | n/a | n/a | Analog `dsp/` |
| Stage entry points for the original four | `osp@sp:src/engine/PostProcessor.h:93-114` (89795a3), plus MOVEMENT lanes `:88` | n/a | n/a | **OSP shared, space-presets only**; not on OSP default |
| Hook into the synth | `analog:dsp/Synth.cpp:2245` `router.process (effects, …)`; `Synth.h:117-122,577`; settings in `Patch::fx` (`Patch.h:268-270`) | n/a | n/a | Analog `dsp/` |
| New effects | `analog:dsp/fx/{Effects.h,FxDsp.h,FxTone,FxDynamics,FxModulation,FxSpectral,FxRetro,FxProcessors}.cpp`; `fx::Settings` (`FxSettings.h`) | n/a | n/a | Analog `dsp/` |
| Parameter specs, IDs, `Reader` | n/a | n/a | `analog:product/Source/FxParameters.{h,cpp}`; prefix hard-coded at `FxParameters.cpp:309`; marked as read by the engine at `AnalogParameters.cpp:271` (`"analog.fx.*"`) | Analog `product/` |
| Route state, engine API | `AnalogEngine::setFxRoute/fxRoute/onFxRouteChanged/fxProcessor/convolutionImpulse/updateConvolutionImpulses` (`AnalogEngine.h:100-119,174-193`) | n/a | `<FxRouting schema="1" graph="…"/>` in the engine's state, written only when CUSTOM; absent means CLASSIC (`AnalogEngine.cpp:128-173`); state version 12/13 (`Product.cpp:450`) | Analog `product/` |
| Routing view (board, drag/stack, library, quick settings, gear: Restore original, Save/Load/Show FX chain, the mixer) | n/a | `analog:product/Source/FxRouting.{h,cpp}` (193 + 2044 lines), hosted by `ShellProduct::sourceAreaView` (`Product.cpp:380`) | interface only | Analog `product/`; the host slot is OSP shell, space-presets only (49a6ed3) |
| Undo | n/a | `RouteEdit : juce::UndoableAction` through `host.undo()` (`FxRouting.cpp:527-532,601-612`); parameter edits use APVTS gestures | n/a | Analog `product/` on the shared `InstrumentHost::undo()` |
| FX rack (one 214 px module, pages, graphs, EQ graphic with live spectrum, save key) | meters read `AnalogEngine::fxProcessor` | `analog:product/Source/FxRack.{h,cpp}` (2482 lines); `EqGraphic` at `FxRack.cpp:840`; rides on the shared `ParameterRack` (`setFullWidthGraph`, 78b9ad6; `setSaveAction`, d591713), `EqEditor`, `GraphFrame`, `rack::Kind::fx` (25b940b) | n/a | Analog `product/` on OSP space-presets shared racks |
| Visual identities (enamel, motif, ink per device) | n/a | `analog:product/Source/FxIdentity.{h,cpp}` (576 lines) | n/a | Analog `product/` (Analog's palette) |
| Effect presets and FX chains | n/a | rack rail and routing gear | `analog:product/Source/FxPresets.{h,cpp}`; factory lists in code; user files under the hard-coded `…/ANDOR/ANDOR Analog/` (`FxPresets.cpp:93`), `FX Presets/<TYPE>/`, `FX Chains/` | Analog `product/` |
| Latency reporting | `FxEngine::latencySamples()`; `AnalogEngine::latencySamples` adds it (`AnalogEngine.h:39`) | n/a | host told by `ShellProcessor`'s timer (`osp@sp:shared/andor-shell/Source/ShellProcessor.cpp:1177-1181`, de3253c) | split: Analog plus OSP shell (space-presets only) |
| Mixer | none | `FxRouting.cpp:562-582,1569-1612` | the cards' LEVEL params | Analog `product/` |
| Header MIX on/off, X close keys | n/a | `ShellProduct::headerMix`, `crossCloseKeys` (`osp@sp:…/ShellProduct.h:185-187`, 29c48d2, 460f4d3) | `analog.blend` (unchanged) | OSP shell, space-presets only |
| Automation filter (unread parameters are not offered) | n/a | n/a | `parameters::ScopedAutomatableFilter` (d6cda7b, `osp@sp:…/AndorParameters.cpp`) | OSP shared, space-presets and analog-release |

### 1.3 Analog tests that guard FX [V]

| Tests | What they check |
|---|---|
| `analog:tests/FxBaselineTests.cpp` `[fx][legacy]` | 114 frozen renders in `tests/golden/fx-baseline-renders.txt` |
| `FxGraphTests.cpp` (11 cases) | `[fx][graph]`, `[fx][route]`: CLASSIC bit for bit, CUSTOM classic < -100 dB, latency |
| `FxEffectTests.cpp` (9 cases) | `[fx][effects]` |
| `FxRetroTests.cpp` (14 cases) | `[retro]` |
| `RealtimeTests.cpp` | no allocation in the FX path |
| `analog:product/tests/AnalogProductTests.cpp` (15 FX / routing / V4 / V5 cases) | `[analog][fx][state]`, `[routing][ui]`, `[rack][ui]`, `[eqgraph]`, `[presets]`, `[conv]`, `[latency]`, `[v4]`, `[v5]` |
| `[analog][host][golden]` modulation destinations (`:7768`) | 87 lines; no FX entries |
| `[analog][state][fixtures]` with `product/tests/fixtures/state-1.0/` | 11 states including `fx-custom` (`:42-110`) |

---

## 2. What OSP has today

**Post chain.** In `osp:src/engine/InstrumentEngine.cpp` each layer's voices go to its own buffer:
- the per-layer REIMAGINED stage (`runLayerStage`), then
- the layer EQ (`slot.eq.process`, `:1392`), then
- the layer gain mixed in (blend, or the X/Y triangle; `:1393-1402`).

The mix then goes through **`post.process (left, right, numSamples)` (`:1421`)** and then
`outputGain`. `PostProcessor` (`osp:src/engine/PostProcessor.h`, default branch, 132 lines) is
the fixed chain:
- legacy-global REIMAGINED;
- DRIVE;
- one MOVEMENT bus;
- ECHO and SPACE as parallel sends.

The default branch has **no `custom*` entry points and no MOVEMENT lanes**; they exist only at
`osp@sp:src/engine/PostProcessor.h:88-114`.

Two more facts:
- Mono output aliases `right = left` (`InstrumentEngine.cpp:1195,1213`), which matters for any routed path [V].
- OSP never calls `setLatencySamples`. It also does not publish `outputPeak` (grep of `apps/plugin/Source`) [V].

**Plugin.** `OspAudioProcessor` (`apps/plugin/Source/PluginProcessor.h:42`) is its own
`AudioProcessor` + `InstrumentHost`, not a `ShellProcessor`. Its state: `stateVersion = 14`
(`:478`), state tree `"OSP"` (`PluginProcessor.cpp:231`), and its own `undoManager` (`:440`).
`OspAudioProcessorEditor` uses the shared `InstrumentFrame`, `EngineCard` and macro popups.
Neither line has racks in OSP's own editor: no `rack` reference in `PluginEditor.cpp` on either line.

**Parameters.** `osp:tests/plugin/golden/osp-parameter-schema.txt` has 434 lines on the default
branch and 415 at adf8ed6. The highest hint is v28 (9 imprint params on the default branch).
OSP's IDs have no prefix: `drive`, `echo`, `space`, `motion`, `layerA.level`, `mix.x`,
`layerA.mute/solo`, and so on. OSP-only shared generators take a namespace, which is empty for OSP:
`addCardArpParameters (layout, {})` at `PluginProcessor.cpp:88`, against `slug + "."` in the shell
(`ShellProcessor.cpp:75`).

**Presets and sessions.** Presets are full state XML (`Documents/OSP/{Presets,Instruments}`), plus
the Library (SQLite) and packs.

**Tests and visual constraints.**

| Guard | What it pins |
|---|---|
| `[plugin][parameter-schema]` (`tests/plugin/ParameterSchemaTests.inc:9`) | the schema |
| `[regression]` (`tests/regression/GoldenTests.cpp`) | metric goldens (hash informational); within one build, bit-identical |
| `[unit][post]` | PostProcessor tests |
| state-migration tests | synthetic old trees, e.g. `PluginProcessorTests.cpp:452,510` |
| `[design-capture]` (hidden; `DesignCaptureTests.inc:63`) | OSP's design bible |
| `osp_visual_reference` (`apps/plugin/CMakeLists.txt:88`) | the reference renders every product's `[visual-regression]` compares against at **tolerance 0** (`tests/shell/ShellTests.cpp:641-690`) |
| `[shell]` (`ShellTests.cpp:209-262`) | each product's base schema must **equal OSP's golden minus an explicit `ospOnly` list** (arp.*, `layer*.synth.*`) |

---

## 3. The gap

### 3.1 What moves, and to which target in the plan

| Today (Analog) | Target | Generalisation needed |
|---|---|---|
| `dsp/fx/*` (graph, router, 10 effects, settings) | `andor_fx`; interim `osp:src/engine/fx/` | Namespace `andor::analog::fx` becomes neutral (`osp::fx` / `andor::fx`), with an alias in Analog. Make the pool `capacity` a per-product table if OSP takes fewer slots (§6). No JUCE, already Prng-seeded (`FxRetro.cpp:250-255`), so it follows OSP rules 1-2 [V]. |
| `PostProcessor::custom*`, lanes | already `andor_fx` (`src/engine`) | Arrives on OSP default with step 0; it auto-merges (PostProcessor is not in the conflict list) [V]. Its tests live only in Analog (89795a3 added none): move them with it. |
| `FxParameters` (specs, `addTo`, `Reader`, `format`) | `andor_schema` (with `ParameterFactory`) | `addTo (layout, ns, hintFor (type))`: Analog passes `"analog."`, 28/29. OSP passes its namespace and hint ≥ 29. The ID builder at `FxParameters.cpp:309` and the 4 literals in `FxRouting.cpp:302,648,1198,1785` must use `idOf`. |
| Route state and engine API (`setFxRoute`, compile, undo action, XML, convolution impulse timer, latency sum) | `andor_ui` (JUCE) as a small `FxRouteHost` owned by the product's processor; `andor_shell` forwards it | Today it is reached by `dynamic_cast<AnalogEngine&> (ShellProcessor::productEngine())` (`FxRouting.cpp:34`, `FxRack.cpp:35`). Replace with a virtual `InstrumentHost::fxRouting()` that returns nullptr by default. `outputPeak` (`osp@sp:InstrumentHost.h:420`) is the precedent. |
| `FxRack`, `FxRouting`, `FxIdentity`, `FxPresets` | `andor_ui` | Remove `ShellEditor` uses (`FxRouting.cpp:553,789,1969`; `FxRack.cpp:2415-2429`). Instead, add an interface for "open rack kind / close source view" that `ShellEditor` and `OspAudioProcessorEditor` both implement. Make the mixer rows data: label, model text, level param ID, colour (today hard-coded `osc1Level/osc2Level/layerB.level`, "OSC 1/OSC 2/HYBRID" at `FxRouting.cpp:563,1584-1586`; MOVEMENT subtitle via `params::movementLaneId` at `:909-910`). Identities come from the product (Analog's enamel palette; OSP's teak). The preset root takes the product folder (`FxPresets.cpp:93`). |
| `sourceAreaView` (shell only, 49a6ed3) | `andor_ui` (`InstrumentFrame` part) or a twin in OSP's editor | OSP's editor has no "other view of the sources" slot. |
| Racks: EffectRack, ParameterRack, faceplates, TapeDeck, save key | already `andor_ui` (space-presets) | OSP's own editor must adopt them first (STATUS NEXT `osp@sp:docs/STATUS.md:801-826`). |
| Latency timer | `andor_shell` already | `OspAudioProcessor` needs the same message-thread check (it already is a `juce::Timer`). |

### 3.2 What OSP must add to host it

1. **Engine.** Wrap `InstrumentEngine.cpp:1421` with the router: `router.process (post, left, right, n)`.
   CLASSIC goes straight to `post.process`, so it is identical by construction (`FxEngine.cpp:225`).
   Settings come from a `Reader` (atomics) per block, as `AnalogEngine.cpp:56,342` does.
2. **Parameters.** Append 379 IDs, or a subset (§6), after OSP's last ID with hint 29.
   - Add them to the shell test's `ospOnly` filter (`ShellTests.cpp:246-258`). Otherwise every shell product fails `[shell]`.
   - Re-record `osp-parameter-schema.txt` by append only.
3. **State.**
   - `stateVersion` 14 becomes 15.
   - Add an `<FxRouting schema graph>` child, written only when CUSTOM. Absent or unreadable means CLASSIC.
   - Presets without it restore CLASSIC.
   - No migration. The FX params are not read in CLASSIC, so their values cannot change audio.
4. **Host.** Report latency (timer); publish `outputPeak` for the TAPE deck. Route undo already works through `undoManager`.
5. **UI.**
   - The effects rack in OSP's editor (a prerequisite).
   - An entry point to the routing view, and the view in the source area.
   - The mixer bound to `layerA/B/C.level` with the 1-3-layer adaptive count. The fixed `std::array<…,3>` at `FxRouting.h:150-151` must become variable.

---

## 4. Compatibility constraints and their guards

| Constraint | Guard (exists = E, add = A) |
|---|---|
| OSP's IDs, order and hints unchanged; new parameters only appended | E `[plugin][parameter-schema]` + `osp-parameter-schema.txt`; E `[shell]` base-schema equality (needs the `ospOnly` extension) |
| Old OSP sessions and presets load as CLASSIC with identical audio | E state migration tests (synthetic trees). **A**: a test that loads a v14 tree, checks the route is CLASSIC, and finds the FX params unread. **A**: a bit-exact A/B, `InstrumentEngine` with router in CLASSIC vs direct `post.process`, over OSP's fixtures, mono and stereo, block sizes 1-512. E `[regression]` (metric) |
| OSP CLASSIC render unchanged | E `[regression]`, `[unit][post]`, unified-baseline goldens. **A**: real binary OSP state fixtures (plan §4.2). OSP has none today; Analog does |
| Analog's 1.0 IDs unchanged (379 `analog.fx.*`, 165 `analog.*`) | E `analog:product/golden/parameter-schema.txt` via `GOLDEN` (`product/CMakeLists.txt:44`). **A** (early, in OSP): a contract test that generates the FX specs with `ns="analog."`, hints 28/29, and diffs lines 166-544 byte for byte |
| Modulation destinations unchanged | E `[analog][host][golden]` (`AnalogProductTests.cpp:7768`); FX params are not destinations (0 hits) [V] |
| 1.0 state fixtures load and sound the same | E `[analog][state][fixtures]` (11 fixtures incl. `fx-custom`, contours) |
| Analog CLASSIC and CUSTOM sound unchanged | E `[fx][legacy]` 114 hashes (Linux-exact), `[fx][route]`, `[fx][effects]`, `[retro]`, `[v5-audio]`, `[analog][fx][latency]`, realtime suites |
| OSP `[design-capture]` pixel identity | E hidden test; run before and after each shared-UI PR and diff byte-for-byte |
| Every product's visual regression at 0 px | E `osp_visual_reference` + `[visual-regression]` (ShellTests.cpp:641, tolerance 0); `andor-design-system/regression` masks |
| Host identity, plug-in codes | E registry checks (`product-registry.json`) |

**Coupling to watch.** If OSP shows a routing icon in its header (as Analog does beside SAVE),
OSP's reference images change. Then every product's parity render differs unless the shell parity
render also draws it, or a mask is added. Masks have been added before for Analog's header items:
460f4d3, 25b940b, e703b0c. Either choice is a deliberate change to the design-system contract and
needs owner approval.

---

## 5. Recommended PR sequence

The release freeze applies. Analog 1.0 ships from Analog `main` (pin adf8ed6) for Black Friday,
27 Nov 2026, and the plan forbids migration merges within two weeks of a release. So every PR
before step F changes **only OSP's default branch**. That branch is not what Analog builds, so
none of it can destabilise Analog. Analog's pin moves only in step G, after 1.0.1.

| # | PR (OSP unless stated) | Behaviour | Gate | Effort | When |
|---|---|---|---|---|---|
| A | **Docs.** Add the FX routing gap and this plan to OSP STATUS NEXT, as the shared rule requires. Note the stale plan numbers (§7). | none | n/a | 0.5 d | now |
| B | **Plan step 0, OSP side: reunite the lines.** Merge adf8ed6 (space-presets + analog-release) into OSP default on a branch. A dry run gives **7 conflicting files** today: STATUS, `ShellProcessor.{h,cpp}`, `AndorUi.cmake`, `AndorParameters.cpp`, `EngineCard.{h,cpp}`, `PluginProcessorTests.cpp` [V via `git merge-tree`]. Keep OSP's v28 imprint hints; Analog's own params are filtered by namespace. | neutral | OSP full ctest; schema golden equal to default's 434 lines; `[design-capture]` byte-identical; `[visual-regression]` 0 px for andor-example; a CI job that builds Analog main against the merged OSP (no repin) and is green | 2-3 d | now (merge to OSP default allowed: Analog does not pin it) |
| C | **Contracts in OSP** (pulled forward from plan step 1). Copy Analog's `parameter-schema.txt` lines 166-544, `modulation-destinations.txt`, and the fx-baseline hash list as read-only contract files. Move the `[fx][graph]/[route]/[effects]/[retro]` vectors with them. Add OSP binary state fixtures (v14 default and a few factory presets). | none | new contract tests green on Linux; CI on macOS/Windows | 2 d | now |
| D | **DSP into shared.** Copy `analog:dsp/fx` to `osp:src/engine/fx/`, neutral namespace, `osp_dsp` (no JUCE). Port the DSP tests. Nothing calls it yet. | none | ported tests reproduce Analog's numbers (same inputs, bit-identical where Analog asserts hashes); `[realtime]` | 2 d | now |
| E1 | **Router in `InstrumentEngine`, CLASSIC only.** | identical | **A** bit-exact A/B (router CLASSIC vs `post.process`), mono-alias case, `[regression]`, CPU smoke within 5 % | 1 d | now |
| E2 | **Shared JUCE layer.** FX parameter specs and `Reader` (namespace and hint arguments, capacity table); `FxRouteHost` (route, compile, undo, XML, impulses, latency); `InstrumentHost::fxRouting()` default null; product preset root. No product registers anything yet. | none | contract test: `ns "analog."` reproduces Analog's 379 lines byte-for-byte; OSP schema unchanged | 2-3 d | now |
| E3 | **Shared UI, opt-in.** Move `FxRouting`, `FxRack`, `FxIdentity`, `FxPresets` UI into `shared/andor-ui`. Mixer rows as data; identities from the product; an "open rack / close view" interface instead of `ShellEditor`; `sourceAreaView` mechanism available to `InstrumentFrame`. Nothing opted in. | none | `[design-capture]` identical; 0 px; Analog's `[analog][fx]` UI tests ported against a test host | 5-7 d | now |
| E4 | **Effects rack in OSP's own plugin** (existing NEXT item, a prerequisite). | new UI only when a rack opens | default capture states unchanged; new rack captures at 869/1448/1810 reviewed | 3-5 d + review | now / after design review |
| E5 | **OSP FX parameters and state.** Append the FX params (hint 29); `stateVersion` 15; `<FxRouting>` child; latency timer; `outputPeak`; extend `ospOnly`. | identical in CLASSIC | schema append-only; old-state tests CLASSIC; bit-exact A/B; `[shell]` green; auval/pluginval latency checks | 2-3 d | now |
| E6 | **OSP routing view and mixer**, in the agreed place (§6). | new opt-in UI | owner design approval; regenerated OSP captures and references only with that approval; every product's parity 0 px (mask or parity update agreed) | 3-5 d + review | after the design decision |
| F | Analog 1.0 ships; 1.0.1 patch out (plan precondition). | n/a | n/a | n/a | after BF |
| G | **Analog repoint** (completes plan step 0). Point `external/osp` at OSP default. Analog uses the shared FX: deletes `dsp/fx` and `product/Source/Fx*` and supplies `ns "analog."`, hints 28/29, its identities, mixer rows, preset folder `ANDOR Analog`, and its `FxRouteHost` through `AnalogEngine`. | identical | Analog full suite: `parameter-schema.txt` and modulation golden byte-identical; `[fx][legacy]` 114 hashes; 1.0 fixtures incl. `fx-custom`; `[fx][route]`, `[latency]`; visual parity 0 px; auval/pluginval; macOS/Windows CI | 3-5 d + 1 QA cycle | after 1.0.1 |
| H | Plan steps 1-6 (andor-core). `andor_fx` now contains the router and effects; `andor_ui` contains routing and the rack. | neutral | the plan's gates | per plan | after G |

**How this fits ANDOR_SHARED_PLATFORM_PLAN.md** (`osp:ANDOR_SHARED_PLATFORM_PLAN.md`):

- **Step 0 is split.** The OSP-side merge (B) can happen during the freeze. Repointing Analog (G)
  waits for 1.0.1, as the plan asks.
- **Step 1 (contracts) moves earlier (C).** The FX work needs Analog's goldens inside OSP to prove
  neutrality.
- **The FX unification (D-E3) goes between step 0 and step 2.** That way `filter-repo` does not
  carry two FX implementations into andor-core.
- **The plan's §2 table should list** `fx/` (graph, router, effects) under `andor_fx`;
  `FxParameters` under `andor_schema`; and `FxRouting`, `FxRack`, `FxIdentity`, `FxPresets` and
  `FxRouteHost` under `andor_ui`.

**Total:** about 4-6 engineer-weeks for A-E6, plus 1 week and a QA cycle for G.

### Risks

- **Divergence while two copies exist (D to G).** Analog's `dsp/fx` is frozen by the release, but
  1.0.x hotfixes may touch it. Mitigation: hotfixes land in both, and a CI job diffs the two trees.
- **Bigger merge than the plan says.** There are 39/25 commits and 7 conflicts today, against the
  plan's 29/19 and 4. The `AndorParameters.cpp` conflict touches the schema (`parameters::make`
  refactor on space-presets, imprint and arp on default). A mistake there breaks both goldens.
  Resolve it under C's contract tests.
- **Mono aliasing.** OSP passes `left == right` for mono. Analog's CUSTOM path copies into lane
  buffers, and was never tested aliased [I]. Add a test before E5.
- **Memory and CPU.** 22 prepared processors (CONVOLUTION up to 4 s, RETRO/CODEC FFTs) in every
  OSP instance [I, not measured]. Measure at E1/E5, or prepare lazily per inserted slot (that is a
  behaviour change, so it must also be gated in Analog).
- **Size of the automation list.** OSP would grow from 434 to about 813 parameters. Hosts cope
  [I], but it is worth a decision (§6).
- **The shell's `ospOnly` list.** It is the single place that keeps products' base schemas equal
  to OSP's. If it is forgotten, every product's `[shell]` test goes red.
- **Visual contract.** Any OSP header change ripples into every product's parity; decide it once (§6).
- **Stale manual.** `MANUAL.md` §2/§3 describe a header MIX that is hidden, and §5 omits UTILITY.
  This is a docs fix Analog can take in its release branch.

---

## 6. Open decisions for the owner

1. **Where OSP's routing view lives.** Options:
   - (a) An icon beside SAVE plus the view in the source area, as in Analog. This changes OSP's
     header pixels and every product's parity reference.
   - (b) An entry in ☰ / Advanced or in the effects rack's rail. This changes 0 px in today's
     capture states.
   - (c) A tab in the macro area.

   Recommendation: (b) first, then (a) after a design pass.
2. **Which effects OSP gets.** All 10 new types and 22 slots (379 params), or a subset: for
   example one slot per type, or no RETRO. A subset needs per-product pool capacity in the graph
   validator and parameter generator.
3. **OSP's parameter namespace.** Unprefixed `fx.<type>.<n>.<name>` matches OSP's convention and
   the empty-namespace arp precedent. `osp.fx.*` is the alternative. Analog stays `analog.fx.*` either way.
4. **The mixer in OSP.** Should it use `layerA/B/C.level`, the Analog equivalent? Should it also
   show mute/solo (`layerX.mute/solo` exist), and how does it relate to OSP's blend / X-Y mix band?
   Separately: OSP renders layers as separate stems before the mix (`InstrumentEngine.cpp`
   ~1346-1402), so per-layer inserts and buses (Analog's unbuilt gate 6) are easier in OSP. Should
   that wait for both products?
5. **Visual identity.** Do OSP's tiles and racks use Analog's V5 enamel identities or an OSP teak
   set? (STATUS NEXT already asks for "OSP's own palette".)
6. **Presets.** Should OSP's FX presets and chains be files (as in Analog) or Library assets
   (SQLite, packs)? Should FX chains be shareable between the products (same grammar and keys;
   IDs differ only by namespace)?
7. **Timing.** Can B-E5 merge to OSP default during Analog's freeze? The plan's text says "do not
   start before 1.0.1", but those PRs do not touch what Analog builds.

---

## 7. Corrections to the plan's facts (verified 10 Nov)

- Analog `main` pins OSP **adf8ed6**, not 3d949cd. adf8ed6 is a merge of analog-release d6cda7b
  into space-presets. The local `external/osp` checkout sits at d6cda7b (`+` in
  `git submodule status`).
- Since merge base 055f584: space-presets is 39 commits ahead and 25 behind. There are 7
  conflicting files, not 4.
- OSP schema: 434 IDs on default and 415 at adf8ed6, not "415". Analog's own golden: 544 lines,
  of which 379 are `analog.fx.*`.
