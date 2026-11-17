# ANDOR OSP — USER MACROS V1.2 (port of ANDOR ANALOG's USER MACROS)

17 November 2026. Branch `claude/osp-user-macros`, on top of `claude/osp-atlas-refine`.

This is a faithful port of ANDOR ANALOG's USER MACROS: V1.1, the destination-first halos, the
right-click and tab fixes. It is not a new design. Wherever ANALOG made a choice, OSP makes the
same one.

## UM0 — audit: what ANALOG has and where it went

| ANALOG (pinned OSP inside `analog/external/osp`) | ANDOR OSP now |
|---|---|
| `product/Source/UserMacroCore.h`: mapping, anchored `offsetOf`, the compiled snapshot (≤ 48), `TripleBuffer` | `src/engine/usermacro/UserMacroCore.h`, verbatim (`osp::usermacro`), with ANALOG's unit tests (`tests/unit/UserMacroCoreTests.cpp`) |
| `product/Source/UserMacroState.*`: names, assignments, XML (bounded, sanitised) | `shared/andor-ui/Source/UserMacroAssignments.*` (`osp::plugin::UserMacroState`) |
| `product/Source/UserMacroUI.*`: eligibility, the undoable edits, the range drag, the panel | `shared/andor-ui/Source/UserMacroEditor.*` (`osp::plugin::usermacros`, `UserMacroPanel`) |
| `ProductInfo::UserMacros` (shell): the editor's hooks | `shared/andor-ui/Source/UserMacroHooks.h`; `ShellProduct.h` keeps `ProductInfo::UserMacros` as an alias |
| `ShellEditor`'s glue (~550 lines): the bank, lenses, pick mode, the right-click menu, the halos' hooks, rename, the label menu | `shared/andor-ui/Source/UserMacroController.*`, product-neutral. The editor gives it an `Editor` adapter: host, parameters, content, CORE row, the modulation overlay, popover open/close, Learn, the bank preference and the macro panel's area |
| `MacroBankTab`, `MacroLens`, `MacroAssignOverlay`; `ModulationOverlay`'s macro hooks | Already shared in OSP, unchanged |
| `AnalogEngine`'s offsets: raw value + offset in normalised space, before modulation, ~20 ms glide, sum, then one clamp | `OspAudioProcessor::applyUserMacros`, called before `applyParameters`. Every parameter read goes through `LiveParam` (`shared/andor-ui/Source/LiveParameters.h`), which is untouched while the offset is 0 |
| `ShellProcessor::macroBank()`: `<MacroBank shown=…>` in the interface file | `OspAudioProcessor::macroBank()`: Documents/OSP/Interface.xml (`OSP_DOCUMENTS_DIR` overrides it) |

**Not changed: ANALOG's own `ShellEditor` copy of the glue.** ANALOG builds against an older OSP
pin. It cannot build against OSP's current tree yet, because of `FxRouting.h` header collisions.
Moving `ShellEditor` onto `UserMacroController` now would change code that only ANALOG's tests
cover, without running them. **NEXT:** when ANALOG's pin moves, replace `ShellEditor`'s glue with
the controller. That deletes about 550 lines. ANALOG's tests already describe the behaviour, so
they move unchanged. This is listed in `docs/STATUS.md`.

## UM1–UM3 — parameters, engine, state

- **Parameters:** six `userMacro.1`–`userMacro.6`, 0–100 %, default 0, appended at version hint 34.
  The parameter-schema golden gains six lines (intentional).
- **Engine:** offsets are applied on the audio thread before global modulation:
  - no allocation, no lock;
  - a new table arrives through the `TripleBuffer`;
  - each offset glides in about 20 ms (one-pole), then the macros are summed and clamped once.
  - An unassigned macro moving is bit-exact. This is tested by rendering against a still macro.
- **State:**
  - session and preset XML get a `UserMacros` child, written only when not default;
  - `stateVersion` is 17;
  - a state without the child clears the assignments;
  - bad data is made safe: unknown targets, duplicates, more than 8 per macro, NaN, long names.
- **Undo:** assign, range, swap, rename, clear and remove are each one step through the
  processor's `UndoManager`.

## UM4–UM6 — the interface (`UserMacroController` in OSP's editor)

- **CORE / USER folder tabs:** they rise from the macro panel's top-left edge.
  - Nothing below them moves.
  - Switching changes only which six knobs show. The sound and every parameter stay the same.
  - The bank shown is a preference of this computer, never of a sound: presets, undo and
    sessions never switch it.
- **USER knobs:** each sits in the CORE knob's place, in visual column order (ECHO and SPACE are
  swapped in data order). The frame lights the USER knobs while USER shows.
- **A USER macro's name:**
  - a click opens its assignments, directly above the name like CORE's popovers;
  - a double-click renames it in place (24 characters);
  - a right-click offers Edit Assignments / Rename / Clear Assignments (with confirmation).
- **The lens under a USER knob:** drag it onto a control to assign. Escape or a release on
  nothing cancels, with no undo step.
- **+ ADD ASSIGNMENT:** folds the panel into the pick banner ("Assign to MACRO n · Click a
  control · Esc"). Every eligible control is ringed.
- **A control's right-click menu** keeps its own items, then adds:
  - Assign to Macro (the six names, ticked where assigned);
  - Edit / Reset Macro Range;
  - Show Macro Assignment Details;
  - Remove from Macro n;
  - Remove All.
  
  This works on the overlay and on a plain right-click on the knob itself.
- **Halos:** the focused macro's range sits on the control. Its ends can be dragged: the 100 % end
  never moves the knob; the 0 % end is the knob's own value. Each drag is one undo step, and
  Escape cancels. A click elsewhere returns the halos to their quiet arcs. Assigning or dragging
  never switches the tab.
- **Learn:**
  - the targets `macros.bank`, `usermacro.N` (touches) and `usermacro.N.label`;
  - the states `macros.bank.user` and `osp.usermacro.1.level`;
  - an Info topic for the tabs;
  - a new lesson, "Create a macro" (`osp.usermacros`): USER → right-click LEVEL → Assign to
    Macro 1 → drag the halo → turn Macro 1 → review. It is ANALOG's lesson, on the edited
    layer's LEVEL.

## UM7 — coverage

See `01-coverage.md`. 491 of OSP's 872 parameters can be assigned: every continuous control the
engine reads live. The excluded ones:

| Excluded | Count |
|---|---|
| Switches and choices | 293 |
| Modulation-route parameters | 80 |
| The macros themselves | 6 |
| The seed and the grain budget | 2 |

## UM8 — verification

- **Tests:** `osp_plugin_tests "[usermacro]"` has 7 cases.
  - Parameters and bit-exactness; assign with no jump, undo and state; bad data.
  - The tabs at 869, 1448 and 1810 px: places, nothing moves, remembered.
  - Lens drag, the panel above the name, SWAP / HIGH with undo, the right-click menu, pick mode
    with Escape, rename with undo.
  - The halo: assign from the menu, range from the halo, the engine follows, one undo step,
    Escape cancels, click elsewhere, removal with undo.
  - A plain right-click on the knob itself.
- **Learn test:** `[learn][usermacro]` walks "Create a macro" to its end.
- **Core tests:** the `[usermacro]` tests of `osp_tests` (5 cases).
- **CPU** (hidden test `[usermacro-cpu]`): 2000 blocks of 256 at 48 kHz (10.7 s of audio) took
  157 ms with no assignment and 190 ms with 48 assignments on six macros moving every block.
  That is about +0.3 % of real time, in the same headless build.
- **Screenshots** (hidden test `[usermacro-shots]`, in this folder):
  - `01-core-tabs`, `02-user-bank`;
  - `03-full-halo-user`, `04-level-halo` (LEVEL's range for Macro 1 and its readout);
  - `05-panel-above-name`, `06-pick-mode`.
- **Not verified here:** a real host (Logic, Ableton). Automation of `userMacro.N`, and
  right-click delivery under each host's mouse routing, still need a manual pass. The headless
  tests cover the press the host gives the knob and the one it gives the overlay.
