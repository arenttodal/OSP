# Extraction audit (DS-1 … DS-4)

How ANDOR OSP's interface was made shareable without changing it.

## 1. Dependency inventory (before)

- **Everything in one folder.** All interface code lived in `apps/plugin/Source`: processor,
  editor, components, look and assets.
- **Coupling.** The editor and its components used about **127 members** of
  `OspAudioProcessor` (counted by grep over the GUI sources).
- **The reusable cluster** needed only about **40 instance functions** and some **20 statics**
  (the parameter-ID scheme and value types):
  - EngineCard;
  - MainSections (preset bar, volume, MIX, wheels, envelope);
  - ModulationPanel (with its overlay and popovers);
  - EqEditor and DAMPEN;
  - ArpSection;
  - ShapingPopups (MiniPanel, MiniKnob, SegmentedControl, MacroLabel).
- **OSP-only parts:**
  - the editor's own windows (Library, Samples inspector);
  - the macro popovers and REIMAGINED, which describe OSP's engine;
  - OSP's preset, instrument and starting-state model.

## 2. Options considered

| Option | Verdict |
|---|---|
| A product subclasses `OspAudioProcessor` | No: it would carry OSP's engine, parameters, Library and identity. |
| A product re-implements the 127 members | No: a parallel copy that drifts. |
| **A narrow host interface (`InstrumentHost`) the components depend on, implemented by OSP and by the shell; parameter-only behaviour implemented once inside it** | **Chosen.** This is the smallest dependency injection that removes the coupling, and OSP keeps its code paths. |

## 3. Incremental extraction (each step: build OSP, capture, compare, run the suite)

| Step | What moved | Check |
|---|---|---|
| Baseline | Hidden capture test written (`[design-capture]`): 30 states, 48 component crops, bounds JSON. | Two runs, 0 differences (deterministic). |
| 1 | `git mv` of the components, look, `LoadedInstrument` and assets into `shared/andor-ui`. `InstrumentHost` created; components rewired to it. OSP's popovers split into `OspPopups`. `AndorUi.cmake` added. | Capture: 0 pixels differ. ctest 5/5. |
| 2 | The frame out of OSP's editor (`InstrumentFrame`): painting, placement, keyboard, macro setup, scaling, window shape, drop zone, menu button, popover placement. OSP's parameter layout and IDs (`AndorParameters`). The parameter-only logic into `InstrumentHost`: modulation routes, links, rules, MUTE/SOLO, LINK, the curve codec. `InstrumentLoader`. Binding tags (non-visual). | Capture: 0 pixels differ. New golden test: OSP's 350-parameter schema equals the listing from the **unmodified HEAD build**. ctest 5/5. Committed (`426a46a`). |
| 3 | The Advanced popover into the shared popups. Layer-mix behaviour (`makeLayerAudible`) into `InstrumentHost`. The shell, the Example, the tool, the regression. | Capture: 0 pixels differ. Visual regression: 0 pixels outside the masks. Full ctest (see validation.md). |

Every moved body is the original code. The only edits were mechanical:
- `parameters` → `apvts()`;
- `undoManager` → `undo()`;
- `OspAudioProcessor::` → `InstrumentHost::`.

## 4. What OSP still owns

- Its processor (engine, Library, presets, starting states, migrations).
- Its editor's composition and behaviour (popovers, drag and drop, Library, Samples).
- `MacroPopups.cpp`, `OspPopups.cpp`, `LibraryPanel.cpp`.

## 5. OSP invariants checked

Each of these was checked and holds:
- **Identity:** plugin code `Osp1`, bundle `com.osp.instrument`, name `OSP` (CMake unchanged).
- **Parameter schema and automation IDs:** golden test, 350 parameters, equal to HEAD.
- **Audio and state:** OSP's full plugin suite, including the reference renders, state migrations and bit-exact checks, passes.
- **Default appearance:** the capture, all states and crops, is identical before and after.
