# ANDOR Learn: guided lessons and Info for shell products

ANDOR Learn adds a temporary teaching layer to a product built from the ANDOR Instrument Shell:

- short **lessons** the musician does with the real controls, each step outlining the exact
  control in green and checking the real state before it moves on;
- **Info** (the INFO key beneath ARP: point to a control to read about it; clicks still reach the control);
- a **lesson browser** (Tutorials...);
- a **one-time invitation**;
- **per-user progress**;
- optional **example sounds** a lesson can offer, loaded only on the musician's choice, with the
  sound before them kept for **Restore Previous Sound**.

The menu has one entry, **Learn**, holding **Tutorials...** and
**Reset Tutorials...** (and **Restore Previous Sound** while one is kept). There is no Skip: the X
exits, Back goes back, Next / Continue move on once the task is done or already so.

It is native JUCE: no web view, no network, no analytics, no synthetic clicks or notes, and no parameter. A product without a catalog (ANDOR OSP's shell products by default) is exactly as before: no menu item, no invitation, no component and no event.

## The parts

| Part | Where | What |
|---|---|---|
| Data | `core/LessonTypes.h` | `Lesson`, `Step`, `Completion`, `Condition`, `HelpTopic`, `Catalog`, `LearnEvent`, `Origin` (juce_core only) |
| Checks | `core/LessonCatalog.*` | `validate` (duplicates, unknown targets / states / parameters, empty actions, broken help), `available` (capability filter) |
| Runner | `core/LessonRunner.*` | The state machine (see "How a step completes" below) |
| Progress | `core/ProgressStore.*` | `<user app data>/ANDOR/Learn/<product>.json` (`ANDOR_LEARN_DIR` overrides): temp file + replace, corrupt-tolerant, process-wide lock, one invitation per product |
| Action IDs | `core/ActionEvents.h` | The IDs the shared interface reports |
| Targets | `juce/LearnTargetRegistry.*` | Semantic target → locator, part, reveal, readiness; named UI states |
| Touches | `juce/LearnActionBridge.*` | `TouchWatcher`: a mouse listener reporting a control's touches as user events, holding the baseline while pressed |
| Overlay | `juce/LearnOverlay.*` | `HighlightLayer` (the green outline and connector; paint-only, never intercepts), `Note` (the ceramic note: label, STEP n OF N, title, text, status line, keys), `HitRegion` (Info only) |
| Browser | `juce/LearnBrowser.*` | The lesson browser (START HERE, LESSONS, Resume / check) |
| Controller | `juce/LearnController.*` | One per editor: wires everything; created by `ShellEditor` only when `ProductInfo::learn` is set |

### How a step completes

A step completes only when all of these hold:

1. everything it **requires** holds (`Step::requirements`: real readiness - a playable source,
   audio arriving, a rack open); while one fails the step is **blocked**: the note says what is
   missing, outlines the control that fixes it and offers the fix (and the lesson's example, if
   it has one) - never Skip;
2. a **user-origin event** of its own generation arrives (for a `parameterId` step, about that
   control);
3. the event is among the step's **accepted actions**;
4. the step's **postcondition** holds afterwards (a dropdown's step, `viaMenu()`, waits a little
   for the menu's choice that follows the click).

The baseline of a "changed" postcondition follows the parameter while no gesture is under way. So automation, a preset or undo between gestures never counts as the musician's change.

## Add Learn to a product

1. **Opt in** in `Product.cpp`. A capability is a string a lesson can require; a lesson for a missing capability is never shown.

   ```cpp
   andor::shell::LearnDescriptor d;
   d.catalog = [] { return myCatalog(); };              // immutable data
   d.registerTargets = [] (andor::learn::TargetRegistry& t, andor::shell::ShellEditor& e) { /* product targets */ };
   d.hasCapability = [] (const juce::String& cap, osp::plugin::InstrumentHost& host) { return cap == "my.feature"; };
   p.learn = d;
   ```

2. **Use the shell's targets** (registered for every product):

   | Area | Targets |
   |---|---|
   | Header | `header.preset`, `header.menu` |
   | Cards | `source.card.A/B/C`, `sources` |
   | Macros | `macro.0` … `macro.5` (data order; their knobs report touches as `macro.N.touched`), `macros` |
   | Rest of the instrument | `mod.panel`, `keyboard`, `advanced` |
   | Arpeggiator | `arp.control`, `arp.enable`, `arp.panel`, `arp.pattern`, `arp.rate`; with the per-card arpeggiator also `arp.cards`, `arp.link`, `arp.card.B.edit` |
   | Racks and routing | `rack.area` and `rack.<kind>` (with racks), `routing.button` (with a source view) |

   The shell's states are `arp.panel.open`, `rack.<kind>.open` and `sourceView.open`.

3. **Add a target** for the product's own controls. Locators are called each time, so a rebuilt popover or view is found again, and nothing raw is stored.

   ```cpp
   t.add ({ "my.knob", [e] { return e->macroKnob (2); } });                 // whole component
   t.add ({ "my.part", [e] { return e->sourceViewComponent(); },            // part of it
            [e] (juce::Component& c) { return dynamic_cast<MyView&> (c).partBounds(); },
            [e] { e->setSourceViewShown (true); return e->sourceViewShown(); },   // reveal: navigation only
            {}, /* Info region */ true, /* report touches */ true });
   t.addState ("my.view.open", [e] { return /* a UI state */ true; });
   ```

4. **Add a user-action hook**, if a control needs a precise action. Report from the handler that commits the musician's gesture, after its attachment wrote the parameter (as `ArpInlinePanel::onLearnAction` does). Never report from a parameter callback.

   Where a touch is enough, set `watchTouches` on the target instead: the event is `<target>.touched`.

5. **Write a lesson**:

   ```cpp
   Step s;
   s.id = "rate"; s.targetId = "arp.rate"; s.title = "Try another rate";
   s.body = "Turn RATE and hear how the pulse changes.";              // ~140 characters at most
   s.completion = Completion::action ({ "arp.rate.changed" }, Condition::changed ("arp.rate"));
   ```

   - **`Completion::manual()`** is an explanation step: the musician presses Next.
   - **`withAlreadyDone (condition, "ARP is already on.")`**: a status line and Continue when the
     task is already done (a sentence, never inside a key).
   - **`doneText`**: the status line once done ("OSC 1 now plays sustained notes.").
   - **`requirements`**: what the step needs (see above); **`preparation`**: the lesson's "Set
     up the lesson" step (Use Current Sound / Load Example Sound; `Lesson::exampleId`).
   - **`revealOnEnter = false`** when opening the target is the task itself; `revealLabel` names
     the key that opens a hidden target ("Open Rack", "Show AUX").
   - **`keepClear`**: other controls the note must not cover; **`outlineTarget = false`** when the
     instruction is about a menu item the outline cannot reach; **`advanceWhenDone`** when the
     task opens a menu (Next would close it).
   - **`unavailableBody`** is what the note says while the target cannot be shown.

   - **The Quick Start ends with `findLearnAgainStep()`**: the INFO key ("keyboard.info": the i
     in the corner brings back Learn mode) and the three-dot menu's Learn (every tutorial).
     Every ANDOR instrument ends its Quick Start the same way.
   - **A finished lesson opens the tutorials**, its tick shown. A lesson that played on its
     example first asks Keep / Restore, then the tutorials open.

6. **Write help** for Info: `HelpTopic { targetId, title, body, lessonId }`. With no lesson, there is no Start button.

   Exact targets: a control can be found by the parameter it edits
   (`osp::plugin::binding::findControl (root, parameterId)`: a rack's knob, switch or selector,
   a card dial, a macro knob, a tagged strip control); a part of one by its own area
   (`SegmentedSwitch::segmentBounds`). Never a fixed rectangle, never a whole rack or card for one
   control.

7. **Test.** `tests/shell/LearnTests.cpp` runs with every product: the catalog must be valid against the editor's targets, and Learn must be idle until asked. Add product tests that drive the real controls (see ANDOR Analog's `product/tests/AnalogLearnTests.cpp`).

   `LearnTests.cpp` also holds a test-only sample catalog (`testCatalog`). ANDOR OSP's own plugin does not use the shell editor; bringing Learn there is planned in `docs/STATUS.md` (NEXT).

## Rules

- **Interface only.** Reveals open panels, select tabs and show views; they never write a parameter. The one exception is an example sound, loaded only when the musician chooses Load Example Sound, with the complete sound before it kept for Restore (X never restores or replaces anything). A test drives every lesson step's keys and checks every parameter is unchanged.
- **Message thread only.** Nothing touches the audio thread. Learn adds no timer of its own (the editor's existing one calls `tick()`, which returns at once while idle) and no child component until something is shown.
- **Gone when closed.** Closing the note, the browser or Info removes their components.
- **Escape order:** the note, then the browser, then Info, then the editor's popover.
- **Never advertise unshipped features.** Gate lessons by capability.
