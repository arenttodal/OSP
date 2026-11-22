# Status

> **22 Nov 2026, PERFORMANCE MODE R1 - in progress (branch `claude/performance-mode`, shared with
> ANDOR ANALOG; spec: ANALOG `docs/specs/PERFORMANCE_MODE_R1.md`):**
> - **Gate 0 audit:** done (header keys, macro row + modulation panel, USER macro hooks, cards,
>   Interface.xml, session vs preset, window shaping, Learn reveals).
> - **Gate 1 shared state (done):** `PerformanceView.h/.cpp` - mode, the chosen macros by stable id
>   (core: parameter id; USER: `user:N`), default (six core + up to two assigned USER), sanitising
>   (known ids, once, at most eight, never empty), what the strip shows (no unassigned USER knob),
>   XML (schema 1) and Interface.xml helpers; both processors keep it per instance (session only,
>   never in a preset; loading a preset keeps it) and as the computer's preference. Tests
>   `[performance]` in OSP and the shell.
> - **Shared components:** `PerformanceComponents.h/.cpp` - the header's gauge / pencil key, the
>   macro housing's flap (inside the housing's top edge: the housing never grows), the
>   "Performance Macros" picker (compact popover: ticks, "N of 8", up / down, Reset to Default;
>   never more than eight, never none; every change applied at once, no value touched).
> - **Gates 2-5 in the shell (opt-in `ProductInfo::performanceMode`; ANDOR ANALOG on):** the key at
>   the end of the header's keys; `EngineCard::setPerformance` (the picture at the dark well's Edit
>   Mode height and, owner feedback, the card's knob row under it as Edit Mode shows it, still
>   playable - `performanceFoot` / `performanceTrim`; badge, keys, modifiers and well tools hidden;
>   every part's visibility restored on return); the frame's macro strip
>   (`frame::performanceMacroPlaces`, `frame::paint`'s Performance lights, no envelope divider);
>   the editor hides the modulation panel, CORE/USER tabs, the arpeggiator, racks, AUX strip, the
>   routing key and the utility keys without changing them (the window shrinks by what was open;
>   on return each is as it was); a stand-in-only hybrid leaves no column; Learn always runs in
>   Edit Mode (an active lesson leaves Performance); the strip follows USER assignments. Other
>   shell products: unchanged.
> - **Gate 7 OSP (done):** `OspAudioProcessorEditor` uses the same model, key, picture cards,
>   strip, flap and picker: the gauge key after FX ROUTING (Performance: FX ROUTING and MIX hidden,
>   the pencil right after SAVE - no gap); only layers with a sound get a column (none: the drop
>   invitation); everything Performance hides keeps its Edit visibility and gets it back; an
>   active lesson returns to Edit. The cards' displays show the picture alone
>   (`SourceDisplay::setPictureOnly`: no time labels, START marker or loop bracket).
> - **Owner feedback applied:** the pencil takes FX ROUTING's place (no gap); MORPH's lamp hidden
>   while performing.
> - Tests: OSP `[plugin][performance]` (key, 1/2/3 sources as pictures, strip, flap inside the
>   housing, picker, Edit restored, every parameter unchanged), screenshots
>   `[screenshots-performance]` (empty, 1, 2, 3 sources, picker); shell and ANALOG as before.
> - **NEXT:** QA at 150 % / 200 %, the 8-macro and mixed-USER captures, multiple instances in a
>   DAW and toggling while playing (owner tests).

> **22 Nov 2026, rack cells three knobs wide (shared, opt-in):** `rackui::Slot::threeKnobs` (3 x 104
> + 2 x 7 px; dense 3 x 89 + 2 x 5) for a product's own wide cell - ANDOR ANALOG's KONTAKT
> nameplate. Nothing in ANDOR OSP uses it: OSP's racks are unchanged (pixel-identical).

> **22 Nov 2026, a saved project reopens with its preset's name (OSP and the shell):** the
> session now records which preset (or starting state) was open - name, file, starting-state file -
> and restores it, so a reopened project shows its preset's name again and SAVE still updates it
> (a file not on this computer: the name shown, SAVE asks for a name). Preset files never carry
> the record; older sessions open as before. Tests in both.

> **22 Nov 2026, SAVE asks before writing over a preset; right-click is Save As (OSP and every
> shell product):** SAVE on the musician's own open preset now asks "Save over <name>?" (Save,
> Cancel, and "Don't warn me again for this patch", kept per preset file in the product's
> settings folder); a right-click (or Ctrl-click) on the SAVE key opens Save As. Factory, pack
> and new patches go to Save As as before. Shell: the cards' row under the knobs follows the
> cards alone (an AUX strip no longer brings it back). Shell presets carry their folder
> ("origin:user|folder:a/b"): a host that says where its presets live gets its factory set by
> CATEGORIES (its folders) right in the menu and the musician's own in one User submenu, each
> folder a submenu of its own (OSP, which groups its presets in the
> Library's collections, is unchanged). Tests in both (`[plugin][menus][save]`, `[shell][presets]`).

> **21 Nov 2026, the layer EQ editor and parameters for a product's own bus (branch
> `claude/analog-unison`, for ANDOR ANALOG's D / AUX):**
> - `EqTarget` (`EqEditor.h`): what an `EqEditor` edits - a layer's EQ (`EqTarget::ofLayer`, what
>   `EqEditor (host, layer)` builds, exactly as before) or a product's bus EQ under its own IDs
>   (its title letter, accent, no DAMPEN page, no modulation curve). `drawTabs` takes the accent.
> - `InstrumentHost::addEqParameters`: one EQ's nineteen parameters by an ID function; the layers'
>   version-hint-18 EQ is now built with it (same IDs, order, ranges, texts: schema unchanged).
> - ANDOR OSP pixel-identical (`osp_visual_reference` passes); not a SPACE / ECHO / ARP feature.

> **21 Nov 2026, AMP 2's opt-in hook on the AMP tab (branch `claude/analog-unison`, for ANDOR
> ANALOG):**
> - `ProductInfo::heldAttack` (switch and attack parameter IDs, the engine's count of notes begun
>   with it) puts AMP 2, a held-note attack, on the AMP tab: "+ AMP 2" where a source's "+ ROUTE"
>   is, then `1 | 2 ×` chips; with "2" the A knob and the attack handle edit AMP 2's attack (D, S,
>   R dimmed, AMP 1's), the other attack drawn as a dotted ghost; the "2" chip flashes as a note
>   takes it. `EnvelopePanel::setHeldAttack` and `ModulationPanel`'s keys; unset (ANDOR OSP, every
>   other product) nothing changes - the AMP tab exactly as before.
> - ANALOG-only by the owner's decision (not a SPACE / ECHO / ARP feature: no OSP twin needed).

> **20 Nov 2026, Custom Patterns open when clicked, on the ANALOG parity line (branches
> `claude/analog-parity-r1-custom-arp` and `claude/analog-parity-r1`):**
> - **The owner's report** (OSP's first test build: "nothing happens when I click custom
>   patterns") was fixed on the beta line as 9ff21209; that commit is cherry-picked here, so the
>   shared `ArpInlinePanel::mouseDown` is OSP's own: the row opens the browser after the press
>   (the editor's outside-click watcher, told of a press after the pressed component, closed a
>   browser opened inside it, as the press was on the list rather than the browser's anchor).
> - **The same fix had been made here independently first (7aa93c49)** and is kept as a test:
>   `custom pattern r2: clicks...` clicks through PopoverRig (the real window where there is one,
>   else JUCE's routing step by step) on cards A, B and C: the row opens the browser and it stays
>   open; + New Pattern, a factory pattern, Edit and the pencil work. It fails without the fix.
>   ANDOR ANALOG has the same test (`CustomArpParityTests`), real window under Xvfb.
> - OSP's own real-window tests still fail under this container's Xvfb (`BadAtom`, already noted
>   for `[menu-visual]`); their routed path runs here.
> - **macOS CI:** the browser opens a message after the click, and a test cannot pump the
>   message loop on macOS (`pumpMessages` returns false), so the click test failed there; it now
>   returns early on such platforms, as OSP's other message-loop tests do (31aed041). Linux and
>   Windows run it; on macOS the clicks are the owner's test in the hosts.
> - **Windows (MSVC):** `ShellEditor.h` included `"FxRouting.h"` by name; MSVC looks first beside
>   the file that included it, and ANDOR ANALOG has its own `FxRouting.h` there, so the shell
>   saw ANALOG's header and not `RoutingKey`. It now includes OSP's by path (no other shared
>   header has the problem: the rest find their own folder's file first).
> - **Popovers in a short window:** a window shorter than the instrument scrolls it; a popover
>   opened low (the Custom Patterns browser under the ARP panel) ran below the window's edge.
>   `frame::revealPopover` scrolls it into view once, on opening (ANDOR OSP's editor and the
>   shell alike; nothing changes while all fits, so the captures are unchanged). Test:
>   `custom pattern r2: in a window shorter than the instrument...`.
> - Not taken from the beta: the DAMPEN Starting States commit (3c260390, ANDOR OSP's own
>   presets), as with its release work, MIDI Learn and the POS-line fix.
>
> **20 Nov 2026, R2 on the ANDOR ANALOG parity line (branch `claude/analog-parity-r1-custom-arp`):**
> R2 (6ef33767) cherry-picked alone (8d3b3729); the beta's release work, MIDI Learn and the POS
> line fix are not part of it.
> - **Card letter:** the Custom Pattern editor and browser named the card by its *shown* layer
>   (`shownLayerName`). Their card index is already the arpeggiator's (EDIT A / B / C, as shown),
>   so ANALOG (layers shown A, C, B) labelled card B's editor "CARD C". They now use
>   `layerName`. ANDOR OSP shows its layers in order, so nothing changes there (its R2 tests,
>   visual reference and the example product's visual test pass).
>
> **20 Nov 2026, Custom Pattern UX redesign R2 (owner's spec; shared, so ANDOR OSP and every
> shell product; `docs/research/custom-arp/05-r2.md`):**
> - **ARP list:** the three custom rows became one, **Custom Patterns ›**, which opens a
>   browser: the card's pattern with Edit, recent ones, your library, the nine factory patterns
>   (a collapsible group), + New Pattern. A click chooses (CUSTOM on, one undo step) and closes;
>   opening and closing it changes nothing. The pattern field shows the pattern's name and a
>   pencil that opens the editor.
> - **Editor (700 × 424):**
>   - one dominant grid; the original take folds into a strip;
>   - TIMING / GRID / MAPPING selectors and LENGTH − +;
>   - a contextual toolbar (Split, Make Rest, Add Step, Transform ▾; Merge and Extend when they
>     apply; Undo, Redo);
>   - Preview, Cancel, Apply Pattern (the one accent);
>   - the library in the ··· menu; help and close icons.
> - **Direct manipulation:** double-click a rest for a note, drag a note's edge for its length
>   (the pattern's length kept), and a right-click menu that keeps "make a rest" apart from
>   "remove its time". Delete makes a rest, Shift-Delete removes the time. Long patterns scroll.
> - **Safety:**
>   - A rebuild from the take asks only when manual edits would be lost; so do Record and Import.
>   - Undo restores whole documents (TIMING with its pattern).
>   - Changes neither applied nor saved ask "Discard changes to this pattern?" through the
>     hosts' new dismissal hook (`MiniPanel::dismissRequested`), never from a destructor.
> - **Model:** `edit::resizeEnd`, `resizeStart` and `placeNote`, plus
>   `SnapshotHistory<Document>`, all pure and tested.
> - **Unchanged:** the engine, the file format, the state, the parameter IDs and the factory
>   patterns.
> - **Tests:** `[arp-custom-r2]` (17 cases), `[arp-pattern-r2]` (3), the shell's R2 case, and
>   the updated `[arp-custom-ui]`. Screenshots: `[custom-arp-shots]`.
>
> **20 Nov 2026, granular POS line flicker (owner's report, ANDOR OSP and the shell):**
> - **Symptom:** on older granular patches the POS line (with its flare) jumped between where
>   it should be and another place, often the left edge of the sample.
> - **Cause:** in a patch whose only modulation routes are per voice (an envelope or LFO on
>   grain POS, SPREAD or START), `applyGlobalModulation` cleared every effective slot at each
>   32-sample chunk, the per-voice ones included, and `publishVoiceEffective` set them again
>   only after the block. For most of every block the card read "no value" and drew the knob,
>   the rest of the time the modulated value: the line flipped between the two.
> - **Fix:** `clearEffective` clears only the shared-stage and EQ slots (the per-voice slots
>   belong to `publishVoiceEffective`). The reader also yields when it meets the audio thread
>   mid-write, instead of giving up after 64 spins.
> - **Test:** `[plugin][effective][voice]` holds a note while a second thread reads POS during
>   rendering: no read may fall back to the knob. Without the fix, 92 % did.
> - **Note:** the line now shows steadily what the newest note plays. If an old patch has a
>   route that pulls POS to 0, the line stays at the left, and so does the sound.
>
> **19 Nov 2026, macOS release infrastructure and ANDOR OSP 0.9.0-beta.1 (branch
> `claude/macos-release`, from `claude/custom-arp`; `docs/release/`, `packaging/release/`):**
> - **One release command:** `packaging/release/andor-release.sh --product osp`. It runs
>   preflight, a fresh universal Release build (OSP_FACTORY_AUTHORING OFF) and the full ctest,
>   then signs inside-out (hardened runtime, secure timestamps, the app's audio-input
>   entitlement only). It builds a PKG with selectable AU / VST3 / app choices and an optional,
>   unticked move of earlier per-user test builds (moved, never deleted). It notarizes
>   (Accepted with no issues, or it stops), staples and verifies (codesign, pkgutil, spctl,
>   stapler), checks the payload, and writes a SHA-256, a release manifest
>   (`andor.release-manifest` 1), release notes and a verification report. `--install-test`
>   also installs and runs auval, pluginval and a launch. Every mandatory step fails closed.
> - **Version:** one source, `packaging/release/products/osp/version.env`: 0.9.0-beta.1,
>   build 1, CFBundleVersion 0.9.0.1, channel beta. CMake reads it; the About panel shows
>   version, build and commit. The identity is unchanged: `com.osp.instrument`, AU
>   `aumu Osp1 Ospx`, parameter IDs and state.
> - **Reusable:** product-specific values live in `products/<product>/`; ANALOG and Modular add
>   their own.
> - **GitHub Actions:** `release-macos.yml`, run by hand. It is guarded by two environments
>   with the owner as required reviewer, signs from a temporary keychain and notarizes with an
>   App Store Connect API key. It only ever creates a draft pre-release. `ci.yml` stays
>   unsigned, and now also runs the release scripts' tests (25 checks, stand-in Apple tools).
> - **Tested here (Linux):** the release flow and each fail-closed gate with stand-in tools.
>   **Not run yet:** any real signing, notarization or installation. That runs on the owner's
>   Mac (`docs/release/RELEASE_RUNBOOK.md`).
> - **NOTES FILL (owner's request):** a phrase made into NOTES (dropped, RECORDed, GRABbed)
>   starts with FILL at ±24 st (`atlas::Mapping::phraseFillSemitones`), set in
>   `loadAnalysedRecording`. Saved atlases keep their reach, as do multi-sample sets, the
>   research renders and the goldens (the struct default stays 12). Checked in the plugin's
>   phrase test (`[phase4]`).
> - **DETECTION FEWER minimum (owner's request):** with FEWER no note is shorter than 0.4 s
>   (`AnalysisSettings::fewerMinNoteSeconds`). Before, FEWER only made the splitting rules
>   stricter, so a phrase of clearly separated notes looked the same at all three settings.
>   - A shorter piece joined to a neighbour becomes part of it (the nearer in pitch, else the
>     longer).
>   - A lone short note stays in the atlas, switched off.
>
>   NORMAL and MORE are unchanged; the settings digest gains the minimum.
>   `[phrase][fewer]` covers it on contours and on synthesized audio.
> - **MIDI LEARN (owner's request, ANDOR OSP and the shell):** right-click a control, **Learn
>   MIDI CC**, move a controller. That controller now turns the parameter itself, through the
>   host, in a change gesture (`shared/andor-ui/Source/MidiLearn`).
>   - The audio thread only posts the value; a 60 Hz message timer applies it (no lock, no
>     allocation; tested).
>   - Mappings are saved with the session only: presets never carry them, and loading a preset
>     keeps them.
>   - A learned controller takes over the built-in CC 20-27 macro shortcut.
>
>   Tests: `[midi-learn]` (plugin, 3 cases) and the shell test.
> - **NEXT:**
>   - the owner runs the release on the Mac, then the manual checklist
>     (`docs/release/BETA_TESTING.md`);
>   - the GitHub secrets (`docs/release/OWNER_SETUP_MACOS.md`);
>   - factory packs (none pinned yet);
>   - the ceramic cache fix (resize speed and memory), still unmerged.
> **20 Nov 2026, ANDOR ANALOG parity R1: the shell's preset saving and menus (branch
> `claude/analog-parity-r1`, on the default line plus `claude/shell-window-once`; ANALOG's
> `docs/OSP_PARITY_AUDIT.md`):** what ANDOR OSP's own editor gained (79e56e82, 0719b910, 897d1130)
> now reaches every shell product (ANDOR ANALOG, the example product).
> - **SAVE** updates the open preset in place when it is the musician's own (writable, not under
>   `Presets/Factory`), as OSP's SAVE; otherwise it asks for a name. The main menu has **Save
>   preset** and **Save preset as...**; Save As starts at the musician's preset, or beside the
>   factory one opened, never inside Factory. Saving into Factory on purpose still works (a
>   product's installer keeps the musician's files there, as ANALOG's does).
> - **The flat preset menu** lists the factory set first under "Factory", then the musician's
>   under "Your presets" (`PresetEntry::context` `origin:factory|user`; the arrows walk the same
>   order). One kind only: "Presets", as before. "Save starting state..." is no longer shown
>   where no handler is set (it did nothing in shell products).
> - **Confirmations:** removing a layer or the generator's recording asks once more (a sub-menu,
>   as OSP's source menus). **Shortcuts** are shown on Undo / Redo.
> - **The main menu right-aligns to its key**, as OSP's (it opened rightwards, off the plugin),
>   at every interface scale (the key stays the menu's target, so JUCE scales it).
> - **Tests under a bare X server:** the shell test main creates the window-manager atoms JUCE
>   looks up only if they exist, on a connection kept open (Xvfb would reset them otherwise), so a
>   product's real-window tests (ANALOG's menu geometry at 100 / 150 / 200 %) run after any other.
>   OSP's own `[menu-visual]` test still needs `xvfb-run -s -noreset` when run after other tests.
> - **USER MACROS:** turning a control the chosen macro moves keeps its ranges shown (OSP's
>   `UserMacroController` did; the shell's own glue cleared them).
> - Products without factory presets or USER MACROS see no change. ANDOR OSP's editor is not
>   touched.
> - **NEXT (ANALOG parity, OSP side):** move the shell onto `UserMacroController` (one copy of
>   the glue); upstream ANALOG's V6 MAIN + SEND, RETRO 100 % and AUX identity into the shared FX,
>   and SUB vibrato into `osp::sub::SubOsc`, so ANALOG can drop its copies (ANALOG's
>   `docs/OSP_PARITY_AUDIT.md`, section 5). Custom ARP reaches ANALOG with its pin once the
>   Custom Pattern work is approved (R2).

> **19 Nov 2026, Custom ARP CP3 + CP4 (branch `claude/custom-arp`; `docs/research/custom-arp/04-cp3-cp4.md`):**
> make, edit and keep custom patterns, in ANDOR OSP and every ANDOR shell product (ANALOG).
> - **Make:** RECORD from the keys or the host. The capture is taken before the arpeggiator,
>   which steps aside while armed, so there is no feedback, and comes back on stop. Or IMPORT
>   MIDI: tracks, channels, tempo and PPQ are kept, the file is parsed off the audio thread,
>   and you pick a part. Or start from one of nine factory patterns.
> - **Edit:** the compact Custom Pattern editor keeps the take (RECORDED) separate from the
>   pattern (PATTERN: relative ranks on the step grid).
>   - SPLIT / MERGE / EXTEND / REST / DELETE keep the length; + / - STEP and LENGTH change it.
>   - REVERSE / MIRROR / ROTATE, plus drag to change a note's rank.
>   - STEPS or a 1/2, 1/4, 1/8 grid; CONTOUR or DEGREES.
>   - Every action is one undo step.
>   - PREVIEW plays it on the card; CANCEL restores the card exactly; USE PATTERN keeps it.
> - **ARP list:** ends with `Custom: <name>`, `Create Pattern...` and `Custom Patterns...`. The
>   card is A when linked, else the card being edited.
> - **Keep:** each card's whole document is embedded in the session, presets and portable
>   instruments (`<CustomPatterns>`). A missing library file never breaks a session; a state
>   without patterns clears them.
> - **Library:** `~/Documents/ANDOR/Patterns/*.andorpattern` (schema 1, shared by the
>   products), with Save / Rename / Duplicate / Delete / Load. Factory patterns are protected.
> - **NEXT:**
>   - pattern packs (library patterns shipped on their own; pack presets already carry theirs);
>   - drawing and editing per-event velocity (CP6);
>   - host tests in real DAWs and listening with real takes;
>   - running the shell `[arp-custom]` test in ANDOR ANALOG. ANALOG's own
>     `AnalogProductTests.cpp` must first follow the Shared FX move (it still names
>     `andor::analog::FxRoutingView`, `fxRackShown` and `fxpresets`), and its OSP pin must follow
>     this branch. Every shared source and the shell test already compile for ANALOG here.

> **19 Nov 2026, Custom ARP CP2 (branch `claude/custom-arp`; `docs/research/custom-arp/03-cp2.md`):**
> recorded patterns play inside the one arpeggiator, in ANDOR OSP and the ANDOR shell (ANALOG).
> - **Mapping:** the approved hybrid as **CONTOUR**; degree-cycle as **DEGREES**. The engine
>   picks exactly the lab's notes.
> - **Rhythm:** variable-duration events in exact ticks (960 per step), including rests, splits
>   and merges. Subdivisions keep their place in the swung step, and GATE is a share of each
>   event's duration. The engine plays the lab's rhythmSequence to the sample.
> - **Parameters:** CUSTOM per card (`arp.custom`, `arp.cardB|C.custom`, hint 36), off by
>   default. CUSTOM on without a pattern plays the selected style unchanged.
> - **Hand-over:** each card's pattern reaches the audio thread lock-free
>   (`CustomPatternExchange`, `InstrumentHost::setCustomArpPattern`).
> - **Renderer:** custom patterns render headlessly (`research/configs/custom-*.json`).
> - The 18-style golden is unchanged; STRUM does not apply to custom patterns.
> - **NEXT:** CP3 (the CUSTOM row, Create Pattern, recording, import, the editor) and CP4
>   (patterns saved in sessions and presets, the library).

> **18 Nov 2026, ARP STRUM (branch `claude/arp-strum`; `docs/research/arp-strum/`):** a CHORD
> step's notes one after another, inside the one arpeggiator.
> - **Engine:** a bounded pending note-on queue; UP / DOWN / RANDOM / ALTERNATE; FREE ms or
>   SYNC note value; the total spread never reaches the next step; each gate runs from its own
>   onset; nothing waits past a release, panic, restart or jump.
> - **Parameters:** per card `arp.strum.*` / `arp.cardB|C.strum.*` (hint 35), the same in the
>   shell.
> - **Interface:** a STRUM key beside SATELLITE (light on / off / unavailable without CHORD)
>   opening a shared panel.
> - The 18-style golden is unchanged; zero spread is sample-identical.
> - **NEXT:** review the audio examples; decide on FREE SPREAD as a modulation destination.

> **18 Nov 2026, PEDAL LOOP × REIMAGINED (branch `claude/pedal-reimagined`;
> `docs/research/pedal-reimagined/`):** PEDAL LOOP Forward and Ping-Pong now work in every
> REIMAGINED mode at every amount, for One Shot with the layer's LOOP off.
> - **Before:** TAPE FRAME, TOYBOX, MOSAIC and MIRAGE fell back to Normal above 0 %. Only
>   KALEIDOSCOPE, which is the voice's own path, looped.
> - **Shared contract:** each engine reports the pedal loop it can play and receives the pedal.
>   `PedalFold` is one allocation-free traversal of the analysed regions, with late entry and
>   release.
> - **Per engine:**
>   - TAPE FRAME and MIRAGE heads read through the fold; tape passes wear.
>   - TOYBOX walks inside the pedal's region.
>   - MOSAIC's spectral timeline folds; its dry part loops through the voice.
> - **Unchanged:** Normal, an unpressed pedal and 0 % are bit-identical; goldens are unchanged.
> - **Tests:** 216 listening-lab renders, all finite. CPU: +0.3–1.3 points of one core per 16
>   held voices.
> - **Limitation:** TAPE FRAME with LOOP on keeps its own rewinding tape, so Ping-Pong is not
>   applied there.
> - **NEXT:** listening on the real recordings (the lab takes them); the user's review of the
>   development build (PL-R5).

> **18 Nov 2026, shared menu visual refinement (branch `claude/menu-visual`, on
> `claude/osp-menu-content`; `docs/research/menu-visual/`):**
> - **Every menu takes the ANDOR look.** Until now, no menu set a look of its own, so in a DAW
>   and in the standalone they were desktop windows in JUCE's default dark look.
>   `SharedMenuLook` now makes one shared OspLookAndFeel the default look while an editor is
>   open. It applies to OSP and the shell products alike.
> - **Ceramic card:**
>   - `#F7F0E6`, 8 px corners, a soft two-layer shadow in a translucent window (opaque square
>     fallback where windows cannot be translucent);
>   - 28 px rows, quiet small-capital section labels;
>   - sage `#DDE3D4` rounded hover;
>   - each card sits on its anchor, and submenus overlap their parent by 3 px.
> - **Icons:** monochrome icons for actions, with warm rust for destructive ones.
> - **Identity headers** (chip, name, file) on OSP's source menus and the shell's layer menus.
> - **Preset dropdown:** "Previous patch"; category prefixes are dropped inside their flyouts
>   (display only).
> - **Library, USER macro, FX routing and mod menus:** icons for their actions.
> - **Screenshots:** before and after for menus 1–8.
> - **Tests:** the shared look's lifetime; the card's geometry against its anchor and a real
>   hover-opened submenu at 100 / 150 / 200 %.

> **18 Nov 2026, menu architecture, content authoring, preset navigation (branch
> `claude/osp-menu-content`; `docs/research/menu-content/`, `docs/research/preset-navigation/`):**
> - **Main menu:** INSTRUMENT / CONTENT / PREFERENCES / SUPPORT, with no layer items. Each
>   source A/B/C has its own menu.
> - **SAVE:** updates a writable user preset in place; otherwise it is Save As. Factory presets
>   are read-only.
> - **Save as Starting State…:** one dialog with a name and a description. The state holds no
>   audio and the instrument is left unchanged.
> - **Packs:**
>   - dependencies, rights and content validation on export;
>   - user installs are never factory;
>   - drafts in the pack builder;
>   - factory publishing only in `OSP_FACTORY_AUTHORING` builds.
> - **Quick preset menu:**
>   - grouped as COLLECTIONS, CATEGORIES (14, the empty ones hidden) and PACKS;
>   - one column, at most 20 rows per flyout, then Browse all in the Library;
>   - the arrows walk the collection a preset was chosen from;
>   - categories are derived (`src/library/PresetCategory.h`), so nothing is rewritten;
>   - tested at 50 / 500 / 5,000 presets.
> - **CORE macro palette: harmonised** (approved).
>   - LIFE sage, DRIVE copper, FILTER teal, MOVEMENT brass, SPACE slate, ECHO clay.
>   - The beads were re-rendered in Blender (`bpy` 4.2) and swapped in with
>     `pack.py --update`.
>   - The USER bank keeps its frozen palette and lenses; the pedals keep V5.
> - **NEXT:** SUPPORT's user guide link needs a URL.

> **17 Nov 2026, final interface polish (branch `claude/osp-final-polish`):**
> - **Popovers stay open** through right-click, Assign to Macro, halo range drags,
>   invert/reset, Escape and menu cancel, and knob turns.
>   - Cause: presses on the shared modulation overlay looked like outside clicks.
>   - Fix: `ModulationOverlay::pressBelongsTo`.
>   - Docs: `docs/research/popover-macro-interaction.md`.
> - **Popovers and halos sit above the Mixer / FX Routing view.**
>   - Cause: the view's per-tick `toFront`.
>   - Fix: it is placed once, at the cards' layer.
> - **Text fields read on every surface:** `styleTextField` (light / dark), with every field
>   audited (`docs/research/text-inputs-and-layers.md`). The Library opens above the Mixer.
> - **Signal chain pedal light:** follows the effect's real ON (a click, automation, undo,
>   presets). Bypassed, only the display dims (`docs/research/pedal-led/`).
> - **INFO: See tutorials.** Preset card heart and actions aligned. A compact **ROOT NOTE**
>   selector (any MIDI root). **TYPE** in ANDOR's dropdown look (`docs/research/final-polish/`).
> - **Built-in starting states are whole states:**
>   - embedded `.ospstate` files (`apps/plugin/Resources/StartingStates`);
>   - a new instance opens on the first, applied in full;
>   - CI fetches the Core factory pack listed in `packaging/factory-packs/packs.lock`;
>   - how to design them: `docs/sound-design.md`.
> - **NEXT:**
>   - replace the seven baked states with designed ones;
>   - publish the Core pack and add its line to `packs.lock`.

> **17 Nov 2026, Library & Preset Browser polish (branch `claude/osp-library-polish`;
> `docs/research/library-polish/`, before/after shots):**
> - **Space:** plays, pauses (keeping the position) and resumes the selected sound.
>   - It used to restart every time: `previewSelected()` always called `play()`, and the
>     engine had no pause.
>   - One press gives one transition (key-repeat is eaten).
>   - A 3 ms ramp is used: `PreviewEngine::playFrom` / `pause`, `Audition::Transport`.
> - **Removed:** LOAD TO A/B/C, Load to First Empty, Keys play the instrument and Load to
>   Instrument. A drop on **SOURCES A B C** loads into that source at once (recoverable).
> - **STARTING STATES:** tiles above the PRESETS and TEMPLATES lists, by program index (a user
>   preset called Natural stays a preset).
> - **Sounds rows:** name first; Root, Type and Length quiet. The inspector is reordered.
> - **Library glyph:** the leaning book is at 5°.

> **17 Nov 2026, USER MACROS V1.2: ANDOR ANALOG's USER MACROS ported into OSP (branch
> `claude/osp-user-macros`, on `claude/osp-atlas-refine`; `docs/research/osp-user-macros/`):**
> - **Macros:** six USER macros (`userMacro.1..6`) behind a CORE / USER folder tab on the macro
>   panel. Each moves up to eight controls, with an anchored range and no jump when assigned.
> - **Engine and state:**
>   - offsets are summed and clamped once, before modulation, gliding in ~20 ms;
>   - an unassigned macro is bit-exact;
>   - the `UserMacros` state is stateVersion 17;
>   - the bank shown is a preference of this computer (Documents/OSP/Interface.xml).
> - **Shared code:**
>   - core, state, edits and panel are in `src/engine/usermacro` and `shared/andor-ui`
>     (UserMacroCore, UserMacroAssignments, UserMacroEditor);
>   - the editor glue is the new `UserMacroController`;
>   - `LiveParam` routes every parameter read through the offsets.
> - **Interface:**
>   - lens drag, pick mode, the right-click menu "Assign to Macro" (overlay and knob);
>   - range ends on the halos;
>   - rename, the label menu, the panel above the name;
>   - Learn lesson "Create a macro".
> - **Coverage:** 491 of 872 parameters (`01-coverage.md`).
> - **CPU:** about +0.3 % of real time with 48 moving assignments.
> - **NEXT:**
>   - ANDOR ANALOG's `ShellEditor` replaces its own copy of the glue with
>     `UserMacroController` when its OSP pin moves (FxRouting.h header collisions block that
>     build today);
>   - a manual pass in Logic and Ableton (automation of `userMacro.N`, right-click routing).

> **17 Nov 2026, NOTE ATLAS refinements and tutorials (branch `claude/osp-atlas-refine`, for
> review; `docs/research/note-atlas-refinements.md`):**
> - **Round robin with LOOP off:** not reproduced. Selection is the same with LOOP on or off, and
>   LOOP off only removes the "cannot loop" filter. Regression tests added (engine and plugin).
> - **FADE IN / FADE OUT:**
>   - under SPEED in NOTES, per note, in output time;
>   - LOOP off only (dimmed with LOOP on), clamped on short notes;
>   - saved in the atlas mapping; 0 / 0 is bit-identical.
> - **NOTES lights the regions the voices really play** (member identity published with the read
>   heads): round robin, FILL and chords. A voice whose recording has ended shows no read head.
> - **Footer: LIBRARY, SETTINGS, INFO** (KeyboardUtilityActions::Action::library). LIBRARY
>   opens and closes the existing Library.
> - **Learn:**
>   - "From a phrase to an instrument" comes after the Quick Start, with a sung example phrase,
>     MAP NOTES and NOTES;
>   - a done step moves on by itself after 3 s;
>   - Layering's last step says a sound or phrase can just be dropped.
> - **NEXT:**
>   - ANDOR Analog gets the 3-second auto-advance when its OSP pin moves;
>   - a lesson for Record (live capture), SYNTH and FX ROUTING.

> **16 Nov 2026, Learn: every lesson ends in the tutorials; the Quick Start ends on INFO (both
> instruments, shared `shared/andor-learn`):**
> - A finished lesson opens the tutorials, with its tick, instead of the "Lesson complete" note.
>   A lesson that played on its example first asks Keep / Restore, then the tutorials open.
> - The Quick Start's last step (`findLearnAgainStep()`) points at the INFO key. Its text:
>   - the i in the corner always brings back Learn mode;
>   - every tutorial is under the three-dot menu, in Learn.
> - In OSP it follows "Hear the original again". In ANDOR Analog it follows AUX D (lesson
>   revision 5), on the OSP pin `claude/analog-learn-end` (4489e33 plus this shared change).

> **16 Nov 2026, merged into OSP's main line: the keyboard keys and the compact bottom edge
> (`feature/aux-live-input` `7984022` and `4489e33`) with the wheels' line (`claude/osp-wheels`):**
> - OSP's Learn stays Learn 2.0 (its thirteen lessons and Quick Start), not that branch's
>   seven-step catalog. The two entries below describe that branch as it was.
>   - SETTINGS and INFO are Learn targets (`keyboard.settings`, `advanced`, `osp.advanced`,
>     `keyboard.info`) with help.
>   - The menu's help text no longer mentions Advanced.
> - PITCH BEND, MOD WHEEL and ADVANCED are the wheels' line's compact popovers
>   (`docs/wheels/osp-wheels.md`). ADVANCED opens above SETTINGS, clear of the ARP / SUB housing.
> - The trimmed footer (`claude/friendly-pascal-0y48r0`) is not merged. The musician chose this
>   compact bottom edge instead.

> **16 Nov 2026, compact bottom edge (both instruments, shared):**
> - `design::height` is now 1050 (was 1086), and `layout::housing` ends at 1036: 22 px of
>   housing under the keys, about 20 px under the PITCH / MOD captions, then the same 14 px
>   surround and shadow.
> - Every position above the keys' foot is unchanged, and nothing is scaled.
> - The footer status line (`layout::status`, `Parts::status`) is removed.
>   - Its messages that matter now appear once in `ModulationOverlay::showStatusNotice`: a file
>     that could not load, the processor's messages, and OSP's natural-pitch warning.
>   - "Ready", "No sound loaded" and the note count are gone.
> - `placePopover` now also keeps a popover above the instrument's foot.
> - The visual regression drops the `status-line` mask.
> - Verified:
>   - Plugin tests: 4 / 4 shards (171 cases), including the new bottom-edge test.
>   - The osp_visual_reference, andor_example_tests and example visual tests pass. The shell
>     bottom-edge test runs for every product.
>   - DAW and Mac checks are UNVERIFIED (OWNER TEST).
> - ANDOR Analog: see its docs/BOTTOM_EDGE.md for before / after screenshots and measurements.

> **16 Nov 2026, keyboard keys SETTINGS / INFO, and ANDOR Learn in OSP (both instruments,
> shared):**
> - `shared/andor-ui` `KeyboardUtilityActions` replaces the large Advanced key beneath ARP.
>   - The keys are small square ceramic keys with an icon each.
>   - OSP has SETTINGS and INFO, centred. ANDOR Analog adds D (AUX D's visibility).
>   - SETTINGS opens and closes the Advanced popover, now anchored to the key.
>   - INFO turns Info on and off.
> - OSP now has ANDOR Learn (`apps/plugin/Source/OspLearn.cpp`):
>   - Info texts for its controls and a seven-step Quick Start, ending on SETTINGS.
>   - The Learn submenu: Tutorials… and Reset Tutorials….
>   - No first-run invitation (`EditorHooks::offerInvitation = false`).
> - Info now follows the pointer. After a 300 ms rest it explains the control, and clicks pass
>   through to it.
> - Toggle Info, OSP's "Advanced settings" and the shell's Show / Hide AUX are gone from the
>   menus.
> - MOD WHEEL, PITCH BEND and ADVANCED use the compact MiniPanel proportions.
> - The shell visual regression gains one approved product mask, `keyboard-actions`, because
>   which keys exist is the product's choice. Every product is 0 pixels outside the masks.
> - Verified (Linux):
>   - osp_tests: 485 cases.
>   - Plugin tests: 4 / 4 shards (170 cases). New: the utility keys' geometry, SETTINGS,
>     INFO, the menus and OSP's Quick Start.
>   - The osp_visual_reference, andor_example_tests and example visual tests pass.
>   - DAW and Mac checks are UNVERIFIED (OWNER TEST).

> **15 Nov 2026, the wheels' settings (both instruments, shared):**
> - PITCH and MOD under the wheels are now doors, like a macro's name. A click opens their
>   popover above the caption; a drag of 3 px or more from MOD still makes a modulation cable.
> - The captions are no longer clipped: each wheel's component is 7 px wider on either side,
>   and the wheel itself is drawn where it always was.
> - PITCH BEND holds RANGE (`bendRange`, unchanged), which moved out of Advanced.
> - MOD WHEEL holds VIBRATO (`src/engine/Vibrato.h`): per note, DELAY then RISE, DEPTH in
>   cents, RATE, and CONTROL (MOD WHEEL / ALWAYS ON / ASSIGNED, AMOUNT 0 by default), plus
>   the sources.
> - New parameters `vibrato.*` (version hint 28) are off by default, and off is
>   bit-identical. VIBRATO DEPTH and AMOUNT are appended to `destExt`.
> - ANDOR Analog ships it too (OSC 1, OSC 2, card C pitched, and SUB as its own source).
> - Verified: osp_tests (481, plus the 4 vibrato cases), the plugin tests (4 / 4 shards, 169
>   cases, the new wheels case included) and the example product (24) pass. The visual
>   references (OSP's, the example's and Analog's) pass unchanged.

> **15 Nov 2026:** `ParameterRack::customCell` (opt-in, shared): a product can place its own
> read-only cell in a rack row. ANDOR Analog uses it for MORPH TO D's source and state (GLOBAL
> MORPH 2.0). ANDOR OSP never calls it and is unchanged: 4 / 4 plugin-test shards and the example
> product pass.

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
- [x] UNIFIED ENGINE Phase 5, first musical review (phase-5-report.md, "After the musical review"):
      - Every Note Atlas note of 0.1 s or more holds: the shortest sustain region drops from
        0.3 s to 0.1 s, and a note the continuation analysis would not loop (fading, no loop
        points) gets one crossfaded loop over its body (ContinuationOptions::forceLoop, atlas
        only; reason "loop over the note"). Notes already looped keep their loops; sample sets
        and ordinary samples are unchanged. Atlas goldens updated (levels listed in the commit).
      - "Identify notes?": a dropped recording of a second or more whose ordinary load hears two
        or more held pitches (library::heldNotes over its pitch track, the Library classifier's
        rule) asks at once on its card: MAP NOTES / NO. Recalled sessions, one-note sounds,
        sets and atlases ask nothing; an ordinary card is drawn as before.
      - The played note: its read head moves inside its own region (InstrumentModel display
        range) and its region lights up; regions, NOTE MAP and keys coloured by pitch
        (design::colour::forPitch, drawn into the cached picture).
      - LOOP on plays only the notes that can loop (InstrumentSet::loopSelects after Stage B;
        FILL reaches past them); LOOP off plays all. NOTE MAP, overview and keyboard say so.
      - The keyboard map is a light strip along the top of the keys (one segment per key).
- [x] FINAL PRECISION UI REFINEMENT, visual only (docs/research/ui-refinement/README.md). Five
      passes, one commit each:
      1. NOTE MAP and its inspector;
      2. the source header;
      3. preset navigation;
      4. EQ / DAMPEN;
      5. KEY DNA.
      The housing keeps its depth; the displays share one flat language (device-pixel hairlines,
      text tabs, readouts, segmented choices, one node and telltale vocabulary). Fixed on the
      way: the EQ and DAMPEN pages showed the layer below through them, and "20k" and DAMPEN's
      "0" were clipped. No DSP, parameter, schema or bounds change. The OSP–shell visual
      regression stays at 0 px; design-bible captures regenerated.
- [ ] ANDOR MEMO LAB MVP 0.1 (docs/research/memo-lab: brief, architecture, guide): a standalone
      Mac app (apps/memo-lab) that turns iCloud-synced Voice Memos (or any approved folder) into
      playable ANDOR instruments. It hosts OSP's own processor:
      - ONE SHOT = `loadFile`, MAP NOTES = `analyseRecording` (Stage A then B);
      - NOTE MAP = the plugin's editor;
      - Save = `exportInstrument`, the `.ospinstrument` the plugin opens.
      It adds a read-only scanner, decoding (JUCE / CoreAudio, AVFoundation fallback),
      passages, a preview and the window; it adds no DSP, changes no plugin code or format,
      and never writes the recordings. Tests `memo_lab` (13 cases) pass, including the round
      trip into a fresh processor with an empty store and the recording deleted. Awaiting the
      owner's test on a Mac: the real Voice Memos folder, real memos, Ableton.
      Added after the owner's first test (README, "Audition and the Mac keys", "Library scan",
      "SPEED"):
      - Audition: arrows browse and play each memo from its first sound; long memos start
        from their opening while the rest is read;
      - Z / X move the Mac keys an octave; a new instrument centres them;
      - Scan (on request) sorts memos into Talk / Hum / Phrase / Jam / Noise / Quiet by rules
        (`MemoClassifier`, no model), Show ▾ filters, cached in `library-scan.json`;
      - SPEED (NOTE MAP > TUNING, 30-200 %): Note Atlas `mapping.speed`, a pitch-preserving
        two-grain stretch in `InstrumentVoice` for atlas notes; 100 % is bit-identical.
      Tests `[audition]`, `[classify]`, `[speed]`.
      SPEED fix (owner: "a loud pop and the audio dies" at 50-60 %): the grains read past a
      note's audio near its end (an out-of-bounds read, found with AddressSanitizer); now kept
      inside it. A safety net in the engine drops any non-finite layer block (its notes end;
      `droppedBlockCount()`), so a bad sample can no longer poison the reverb. The grain window
      is a rotation instead of a sin per sample. Cost: ~+40 % per stretched voice.
- [x] KEY DNA keys by hand: drag a key's bar (away from the curve) and that key plays the value
      drawn, in place of the curve and its variation (a stroke draws every key it passes, one
      undo step; a click alone draws nothing, so double-click still adds a curve point).
      Option-click or right-click returns a key, or all, to the curve; Reset clears them too.
      Drawn keys carry a ring. Stored per parameter as `ParamSettings::drawn` (KEY DNA schema 1,
      additive `keys` field; older sessions have none). AMOUNT still scales them. Tests
      [keydna] (engine and editor). The EQ and DAMPEN pages also stack their values under the
      tabs on narrow (three-card) layouts.
- [x] MIX as a modulation destination (docs/research/mix-modulation-plan.md, "Implemented"):
      - engine: `mod::Dest::mixPosition` (MIX: the A <-> B blend, the triangle's A -> B
        direction) and `mixTowardsC`, global and continuous; a routed mix follows within 2 ms
        (an LFO keeps its shape, the gains still ramp per sample); unrouted, the 20 ms glide is
        unchanged bit for bit;
      - host: a second destination per route, `mod.routeN.destExt` (version hint 21; "None",
        KEY DNA A-C AMOUNT, MIX, MIX TOWARDS C, append only). It wins when set; `dest` keeps its
        list and its automation; older sessions open unchanged. KEY DNA AMOUNT is now reachable
        from the routes too;
      - interface: drop a source on the header MIX, or right-click it (Assign / Edit / Remove);
        the routes popover's + ADD has Mix and each layer's KEY DNA AMOUNT. While routed the
        MIX draws the range in the source's colour and a hollow live node; nothing changes
        without a route. With three layers (the header triangle is hidden) MIX TOWARDS C is in
        + ADD.
      Tests `[mix]` (engine 3, plugin 2, editor `[mix-ui]` with snapshots). Parameter schema
      golden updated for the 16 new parameters. Per-note MIX (VEL -> MIX per voice) is later.
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

## SPACE engines in ANDOR OSP (merged from claude/andor-analog-shell)

- [x] ABYSS, VINTAGE and BLOOM (built for ANDOR ANALOG in OSP's own engine and SPACE popover;
      docs/spec/SPACE_ENGINES_PLAN.md) are in ANDOR OSP: SPACE's header chooses Rooms / Abyss /
      Vintage / Bloom (`space.engine`, ROOMS by default; older sessions open on their room).
      The merge also brings SPACE's new defaults (HALL, 3 s, HIGH CUT 5 kHz; saved sessions keep
      theirs), the EQ-corner snap and the shell's opt-in hooks. Parameter order: SYNTHESIZE
      (22), the SYNTH page and ABYSS (23), VINTAGE and BLOOM (24). Test `[space-engines]`: every
      engine heard through OSP's processor and distinct, switching on held notes (and FREEZE)
      finite without allocation, saved, an older session on ROOMS.

## Milestone: SYNTHESIZE Phase S1 — analog oscillator blend (docs/research/synthesize)

- [x] S1.1 audit of PHRASE's Natural / Synthetic (`s1-audit.md`): what it did, the OSP voice
      integration point, and the differences.
- [x] S1.2 DSP (`engine/SynthVoice.h`): one PolyBLEP saw per sounding voice, a fixed warm
      12 dB/oct low-pass (about 4-8 x the pitch with velocity), envelope A (gate, default) and
      B (sample-informed, internal). Real-time safe, deterministic (start phase from the note's
      seed). Plays the played key with bend, fine, TUNE, LIFE, drift, routes and glide, never the
      recording's root or a root correction; in Granular the layer's TUNE, not grain scatter.
- [x] S1.3 blend: hybrid law (the sample whole up to 35 %, the oscillator rising as a^1.5,
      power-normalised), chosen against equal-power and additive; each voice's oscillator
      calibrated to its recording's loudest 20 ms. 0 % computes nothing: bit-identical (main
      ctest and goldens unchanged).
- [x] S1.4 GUI: SYNTH, a slim graphite fader in each source well's top-right corner (no
      window growth, no new section, no third mode, established controls untouched; the
      ruler keeps clear of it). Drag or wheel, Alt fine, double-click 0. A host capability
      (`InstrumentHost::hasSynthesize`): sister products show nothing; the shell's parity
      engine shows it, so the OSP-vs-shell visual regression stays at 0 px.
- [x] S1.5 host parameter `layerX.synthesize` (version hint 22, appended; 0 % default; older
      sessions open with it off), automation smoothed in the voices, saved with the session.
      Tests: `[synth]` (engine) and `[plugin][synthesize]` (pedal, stealing, all notes off,
      panic, a One Shot held past its end, mute/solo, A/B/C, Atlas keys left silent,
      automation without allocation, persistence).
- [x] S1.6 musical evaluation by the user: "sounds great". Results (loudness matrix, blend
      laws, tuning, tests, listening guide, known limits) in `s1-results.md`; renders local only.
- [x] S2 the SYNTH page (`s2-synth-page.md`): a click on SYNTH opens a page over the
      waveform (the oscillator's wave drawn as it sounds) with WAVE (saw, square, triangle,
      sine), OCTAVE (-2..+1), DETUNE (0-50 cents, two oscillators), TONE, SHAPE (Held /
      Follow) and PITCH (Keys / Recording: the recording's own tuning and vibrato from its
      pitch track). Per source, host parameters `layerX.synth.*` (version hint 23, appended;
      the defaults are S1 sample for sample). Tests `[synthpage]`.
- [x] After S2: the oscillator has no release of its own (Held follows the ADSR, its R
      included; Follow follows the recording's contour and ends when the recording does);
      SYNTH A-C are modulation destinations (per voice; `mod.routeN.destExt`, appended); the
      note colours (regions, NOTE MAP, keyboard strip) are earth and plant pigments, warm for
      the even pitch classes and green / teal for the odd ones, no violets.

## Milestone: SYNTHESIS S2 — the spectral imprint (docs/research/synthesize/s2-imprint-*.md)

- [x] S2.0 audit and code map (`s2-imprint-audit.md`); rollback checkpoint 6e5f503.
- [x] S2.1 three experiments on stand-in references (static, time-varying, real-time vocoder;
      16/24/32 bands): B (time-varying) chosen, MOTION 0 being A (`s2-imprint-research.md`).
- [x] S2.2 the descriptor `imprint::Profile` (`model/ImprintProfile.h`) in every
      `InstrumentModel` (each set and Atlas member its own), built in `addContinuation`.
- [x] S2.3 the voice: a 24-band shaper on the oscillator, only in Imprint with IMPRINT above
      0; Analog, IMPRINT 0 and SYNTH 0 are sample-identical to S1.
- [x] S2.4 per layer `layerX.synth.mode` / `.imprint` / `.motion` (version hint 28, appended
      after `arp.*`); older sessions open as Analog; `InstrumentHost::imprintState`.
- [x] S2.5-S2.6 the SYNTH chip in two parts (SYNTH › opens the page; the amount drags, wheels,
      Alt fine, double-click 0; the imprint's curve as its light) and the page's ANALOG | IMPRINT,
      IMPRINT and MOTION in one, two and three cards (`08-imprint-*` screenshots).
- [x] S2.7 regression and CPU (`s2-imprint-results.md`). Tests `[imprint]`, `[imprint-engine]`,
      `[plugin][imprint]`.
- [ ] Musical review of real recordings in the development build (S2.8 report:
      `s2-imprint-results.md`).

## ANDOR OSP: SYNTH chip and Refined Core region colours (8 Nov 2026)

- [x] SYNTH chip, presentation only (interactions and hit areas as before, tested):
  - The amount keeps a fixed 38 px part on the right, so "100%" has room and the number is
    right-aligned on one edge. Percentages now read "42%", without the space.
  - The label has 2 px more on its left and 6 px before the chevron.
  - The light is 4 px, and the imprint curve is drawn at 85 % of its size.
  - Hover is quieter (6 %), with the seam between the two parts shown only on hover.
- [x] Note colours, Refined Core: `design::colour::forPitch` stays one colour per pitch class
  (the meaning is unchanged). The palette is new:
  - C ember `#C97B2E`, C# deep teal `#527B78`, D bronze `#A78667`, D# slate `#73868B`,
    E rust `#9A614D`, F sage `#84917D`;
  - F# to B are a lighter shade of each family;
  - the even pitch classes are warm and the odd ones cool, as before.

  The card's regions, the NOTE MAP and the keyboard's light strip share it.
- [x] Regions:
  - Card: a translucent field at `regionFillAlpha` (base 28 %, balanced by brightness to
    0.82–1.22×), with a 1 px edge a shade lighter and 2 px corners. Sounding notes are
    stronger (30–36 %) with a 1.2 px edge.
  - NOTE MAP:
    - the same fields behind a bone/ivory waveform (`#E8E3DA`; 40 % normal, 52 % hovered,
      86 % selected) instead of the layer colour;
    - hovered: its own hue brighter, with tinted edges;
    - selected: stronger fill, a bronze-ivory outline (`#BE9B77`) and ivory handles; the
      dragged one is brightest;
    - warm neutral boundaries.
  - Geometry, hit areas and editing are unchanged. Screenshots: `01-notemap-1L-dragging`,
    `01-notemap-2L-hover` and `09-synth-chip-*` in `[refine-shots]`.

## ANDOR OSP: the playback-mode selector as symbols (8 Nov 2026)

- [x] The source header's ONE SHOT / GRANULAR key is a small symbol key (42 px, 40 in three
      cards): a start marker and forward arrow, or a small uneven cloud of grains, and a quiet
      chevron (`icons::drawOneShotIcon` / `drawGranularIcon`, vector paths). Its menu names both
      modes with their symbols, a line on what each does and a check by the active one. Tooltip
      and accessible description "Playback mode: One Shot / Granular"; a pop-up button for
      assistive technology; Return, Space or Down opens it. Presentation only: `layerX.sourceMode`,
      its values, automation, sessions and the sound unchanged. A product that names its first
      mode (ANDOR ANALOG's generator) keeps its named key. Tests `[mode-selector]`; screenshots
      `OSP_MODE_SHOTS=<dir> osp_plugin_tests "[mode-shots]"`.

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
- [x] The shell plays modulation, the arpeggiator and a generator's grains for a product that
      binds them (ANDOR ANALOG): `Engine::setModulation` (ANDOR OSP's mod::Settings and the CC
      controllers, on change), `arpModulation`, `setPlayedKeys`, `modulationView`,
      `grainSnapshot`; ANDOR OSP's arpeggiator in front of an engine binding `arp.*`; CC Learn.
      A product's own modulation destinations (`ProductInfo::modulationDestinations`,
      `mod::ProductDest`) follow ANDOR OSP's in the routes' choices (`mod::destFromChoice` /
      `choiceOf`) and in the drag-to-assign mapping; ANDOR OSP registers none, so its route
      choices, schema golden and sessions are unchanged. A generator's card draws POS, SPREAD
      and its grains. Proof: see the PR (design capture before/after, OSP suites).
- [x] KEY DNA, EQ, playheads and an ORGANIC dock for a hybrid product (ANDOR ANALOG), all
      opt-in: the KEY DNA codec moved unchanged from ANDOR OSP's processor to the shared
      `KeyDnaState` (ANDOR OSP's statics call it: its sessions are byte-identical); the shell
      implements InstrumentHost's KEY DNA (settings, curves, undo, the `KeyDna` tree, the
      profile handed to `Engine::setKeyDnaTable`) and opens `KeyDnaEditor` when the engine
      has KEY DNA; `InstrumentHost::productWordmark()` (ANDOR OSP: "ANDOR/OSP", drawn exactly as
      before); `ProductInfo::hybridSampleDock`. Proof: OSP suites and design capture.
- [x] An engine-owned source for a hybrid product (ANDOR ANALOG's Sample Source importer),
      all opt-in: `Engine::saveState` / `restoreState`, `engineSourceName` /
      `removeEngineSource`, `ProductInfo::hybridHeaderPopover` / `hybridAction`,
      `GeneratedSource::periodic`, `ShellProcessor::productEngine()`. ANDOR OSP uses none of
      them. Proof: OSP suites and design capture.
- [x] The AMP envelope's note and touch for a product (ANDOR ANALOG's VCA GATE), opt-in:
      `ProductInfo::ampEnvelopeNote` (`EnvelopePanel::setNote`; empty draws exactly as before)
      and `ProductInfo::ampEnvelopeTouched` (a gesture on the four AMP parameters). ANDOR OSP
      sets neither. Proof: OSP suites and design capture.
- [x] Three engines for a hybrid product (ANDOR ANALOG), all opt-in: `ProductInfo::threeEngines`
      (cards always shown, in the product's order; letters and colours follow it via
      `InstrumentHost::shownLayer`), `Engine::layerGeneratorName`, `ProductInfo::cardControls`
      (`EngineCard::replaceKnob` / `restoreKnob`), `ProductInfo::wellOverlay` (the shared
      `WellOverlay`: controls revealed in a card's well by the pointer). ANDOR OSP and the
      parity render use none of them. Proof: OSP suites and design capture.

## Research: Polyphonic Instrument Extraction Lab, P0–P3 (9 Nov 2026; branch claude/polyphonic-lab)

Research only (`research/polyphonic-lab`, Python). The plugin is unchanged; the only C++ added is a
test file. Reports are in `docs/research/polyphonic-lab/`. **Stopped after P3 for the first
playable-instrument review**, as the spec requires. Commits: P0 8308f0a, P1 87463f6, P2 and P3
8da975f.

- [x] P0: audit of the Note Atlas import route; a deterministic synthetic corpus (24 fixtures,
  stems, manifests) (`01-p0-corpus.md`)
- [x] P1: transcription (`02-p1-transcription.md`)
  - ground truth, a deterministic harmonic detector, and Basic Pitch (its own environment)
  - chords F1 0.944, median onset error 5.3 ms
- [x] P2: per-note extraction, methods A, B and C (`03-p2-extraction.md`)
  - harmonic templates with inharmonicity, harmonic NMF with Wiener masks, attack allocation
  - notes + residual = mixture within 1e-16
  - C-major chord: C 12.7 dB, E 20.3 dB, G 12.1 dB SI-SDR, within 0.5 dB of the oracle mask
- [x] P3: Note Atlas export, renders by OSP's engine, plugin load / export / re-import
  (`04-p3-atlas.md`)
  - The first milestone works: a C-major recording becomes C, E and G samples and a playable OSP
    instrument.
  - It plays chords that were never recorded through FILL. Its re-import is bit-identical.
  - `[plugin][poly-lab]` runs in ctest; `[.poly-lab-import]` is hidden.
- [ ] Review (the user): listen to the milestone material. No human listening has happened yet.
- [ ] Decide on the held-note swell: OSP's continuation loops a fading piano note's body (+10 dB
  when a key is held past the recording). This is outside the prototype's scope.
- [ ] Then, and only then: real recordings, P5 (voices and strings), the gospel recording.

## Shared FX library in ANDOR OSP (10 Nov 2026; branch claude/shared-fx, draft PR)

ANDOR ANALOG's mixer and FX (routing graph, ten new effects, their racks) moving into the shared
code so both instruments run one implementation (docs/research/shared-fx/). ANALOG keeps its own
copy (pin adf8ed6) until after its launch, then repoints. A **design approval gate** sits between
the shared rack (done) and OSP's routing view and mixer (not started).

- [x] M1 the two OSP lines reunited (merge of claude/space-presets adf8ed6, with analog-release):
  every suite green; the one visible change is the shared SPACE presets bar in the SPACE / ECHO /
  MOVEMENT popovers (for the design review).
- [x] M3/M4 `src/engine/fx` (osp::fx, pure C++20): FxGraph, the FxEngine router, the ten effects.
  InstrumentEngine plays its post stage through the router; with no route or CLASSIC it calls
  PostProcessor::process itself - 55 research-renderer renders byte-identical before and after,
  no golden changed. A CUSTOM route on a mono output runs in stereo and folds down. Tests:
  ANALOG's effect / RETRO / graph tests ported, `FxRouteTests` (ANALOG's route cases through
  OSP's engine, mono, no allocation).
- [x] M5 `shared/andor-ui/Source/FxParameters.*`: the specs, product prefix and hints as
  arguments; built with `analog.` / 28 / 29 they are ANALOG's 379 schema lines exactly (test
  against a copy of its golden). InstrumentHost FX hooks (hasFxLibrary, route, processors,
  impulses, prefix, user folder), off by default (other products unchanged).
- [x] M6 FxIdentity, FxPresets and FxRack shared (`shared/andor-ui/Source/Fx*`): ANALOG's code
  with its engine calls replaced by the host hooks and `RackOpener` (the editor that opens racks).
- [x] M7 ANDOR OSP: `fx.<type>.<n>.*` parameters (version hint 30, the 379 appended after
  everything; schema golden +379 lines, nothing else), the reader each block, the route saved as
  an `FxRouting` child only when CUSTOM (absent: CLASSIC; no state-version bump), latency to the
  host, CONVOLUTION impulses, user presets in `<user data>/ANDOR/ANDOR OSP`. The editor hosts the
  FX rack below the sources (full bleed, as ANALOG's); nothing shows until an effect opens, and
  closing it gives back the very same window (design captures byte-identical but for MOVEMENT's
  live LFO, which differs from run to run).
- [x] GATE design approval of the shared rack in OSP (approved 10 Nov 2026), with the SPACE
  presets bar the merge brought; SPACE / ECHO / MOVEMENT to open full racks as in ANALOG.
- [x] R1 FX ROUTING shared (`shared/andor-ui/Source/FxRouting.*`): ANALOG's view with its engine
  calls replaced by host hooks (route, `onFxRouteChanged`, `fxMixerCards`, a second MOVEMENT
  lane's prefix) and `RackOpener::setSourceViewShown`. The header key (`RoutingKey`) is shared
  too: the Instrument Shell draws the same class (products pixel-identical).
- [x] R2 ANDOR OSP: the key beside SAVE (SAVE's size, 8 px after it, as ANALOG's; MIX moves
  right by as much), the view in the cards' place (the window keeps its size); its ENGINE
  MIXER has a row per loaded layer (the sound's name, its playback mode, its LEVEL - the card's
  own parameter). Every edit is one undoable step and makes the route CUSTOM.
- [x] R3 SPACE, ECHO and MOVEMENT open their full racks in OSP (the shared EffectRack modules)
  wearing ANALOG's enamel faceplates (`OspAudioProcessor::rackFaceplate`; OSP's popovers are
  untouched); DRIVE keeps its popover as in ANALOG.
- [x] R4 the macros' names open the same racks (as ANALOG's): a double-click (or Enter) on
  SPACE, ECHO or MOVEMENT opens or closes its rack, a right-click offers Quick Settings or the
  rack, and a single click still opens the popover (once a double-click can no longer follow).

## ANDOR OSP: macro racks and the cards' playback keys (13 Nov 2026)

- [x] DRIVE's and CHARACTER's names open racks, as ANDOR ANALOG's macros do.
  - DRIVE opens the FX rack's DRIVE page.
  - CHARACTER opens a new shared `CharacterRack` (rack::Kind::filter). It is built as ANALOG's
    FILTER rack is, with the voice filter's enamel, TYPE, CHARACTER, MIN, MAX, RESO, DRIVE and
    the ENVELOPE group (ENV, ATTACK, DECAY), and the popover's response as its graph.
- [x] LINK / REVERSE / LOOP / FOLLOW moved into each card's display, as ANALOG OSC C's: the
  shared `EngineCard::setModifiersInWell`, small graphite keys in the top-left of the ruler
  band.
  - With three cards, the 64 px row they had under the knobs is gone
    (`EngineCard::footTrim`): the cards, and the window, are that much shorter, and the macros,
    envelope and keyboard sit that much higher. The display keeps its height.
  - With one or two cards the keys sat beside the knobs, so no row is freed. The window keeps
    its height and the knob row takes the card's width.
  - The shell's reference parity follows the same rule (`ShellProcessor::trimsOnlyWithThreeCards`),
    and a product's own setting is unchanged (ANALOG: in the well, always trimmed).

## ANDOR Learn V1 in the shell (branch feature/andor-learn-v1, 11 Nov 2026)

- **What it is.** `shared/andor-learn`: guided lessons, a contextual Learn mode, a topic browser,
  a one-time invitation and per-user progress for shell products. The authoring guide is
  `shared/andor-learn/README.md`.
- **Opt-in.** `ProductInfo::learn` turns it on. Without it, nothing is created, nothing is in
  the menu and nothing is drawn.
- **Shared ARP UI.** It gains opt-in `onLearnAction` hooks (null by default) and `MiniKnob`
  gains `onUserChange`, so ANDOR OSP's plugin is unchanged.
- **Completion.** Only a user event from a control's own gesture handler can complete a step,
  and the parameter is then checked against its value before the gesture. Automation, presets,
  undo and programmatic calls never complete one.
- **Tests.** `tests/shell/LearnTests.cpp` runs in every product's test program.
- **First consumer.** ANDOR Analog (docs/LEARN_V1.md there).

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
- Modulation panel: VEL's and NOTE's larger curve editor and routes popover open again (the
  editor took LFO 1 .. ENV 2 only, so their expand key and "Edit curve..." did nothing).
  Opt-in for products (ShellProduct::stableModulationTabs, ANDOR ANALOG): the six tabs at
  fixed places, the route count in a reserved place, the expand key inside the display;
  ANDOR OSP's own row is unchanged (pixel-identical).
- Hardware rack faceplates (shared, opt-in; ANDOR ANALOG's FX Rack V2): `rack::Faceplate` and
  `rack::hardware::paint` (RackFaceplate.h) - ceramic chassis, enamel rail with title and
  subtitle, LED, stripes, graphite glass - worn by `ParameterRack` and, through
  `InstrumentHost::rackFaceplate` / `ProductInfo::rackFaceplates`, by the MOVEMENT, ECHO and
  SPACE racks (`UnifiedRack::wearFaceplate`: preset and actions in the rail, LED = the macro
  playing). Without a faceplate (ANDOR OSP, the parity render) nothing changes: 0 px.
- Full-bleed racks (shared, opt-in; ANDOR ANALOG's FX Rack V3): `rack::Host::setFullBleed` (no
  panel around the module, the module the rack's whole 214 px, one shadow via
  `SurfaceFace::surfaceShadowOnly`, the collapse key a borderless X), `Faceplate::fullBleed`
  (`hardware::measures`), `PowerKey::Style::slide` (a horizontal ON), MOVEMENT's lane power as a
  slide, `Materials::macros` (a product's macro identities; SPACE's reverb tint follows it), the
  shell telling the host when an engine's latency changes. ANDOR OSP sets none of it: 0 px.
- Enamel faceplate (shared, opt-in; ANDOR ANALOG's V5 Patterned Hardware):
  `rack::Faceplate::enamelBody` makes the whole face the device's enamel. That covers the body
  and a deeper rail. A product motif painter replaces the stripes, the face gets four corner
  bolts and a 17 px title, and the glass sits in a deeper enamel bezel. `rackui::Ink`
  (`inkFor` / `setInk`) is the face's ink, which the cells read for captions, values,
  switches, tabs, rules and the deck readout. ANDOR OSP sets none of it, so it renders 0 px
  different.
- Rack rail save key and full-width graph (shared, opt-in; ANDOR ANALOG): `rack::hardware::SaveKey`
  sits beside the preset browser on faceplate rails (UnifiedRack's; `ParameterRack::setSaveAction`).
  `ParameterRack::setFullWidthGraph` makes the graph the whole body, with no cells and no divider
  (ANALOG's graphic EQ). ANDOR OSP uses neither: 0 px.
- TAPE deck (shared, opt-in; ANDOR ANALOG's ROUTING + TAPE V4): `rack::Faceplate::tapeDeck`
  shows ECHO's TAPE and MOVEMENT's TAPE as a light reel deck in the same 214 px rack (knobs
  left, `TapeDeck` reels / head / readout in the middle, the display smaller on the right,
  marked PREDICTED; ECHO's TIMING / STEREO as segmented keys on the same parameters).
  `InstrumentHost::outputPeak()` (default -1; `ShellProcessor` publishes the last block's peak,
  read-only telemetry) lets the reels rest in silence. ANDOR OSP sets no faceplate: 0 px.

- Two shared fixes (11 Nov 2026, from ANDOR ANALOG's first Mac test):
  - **An anchored popover no longer closes in the click that opens it.** JUCE tells the
    editor's outside-click watcher after the key has opened its panel, and the watcher closed
    it at once (SATELLITE's, ANDOR ANALOG's RHYTHM and every `openAnchoredPopover` key opening
    on mouse-down). `ShellEditor::mouseDownAnywhere` now leaves the panel's own anchor alone.
    ANDOR ANALOG's tests now replay the watcher after each press.
  - **Lamps have no seam cross at fractional display scales.** A skin piece whose insets meet
    (no centre to stretch: the arp lights, the macro LEDs) was composed from four quarters,
    each resampled alone. At 1.8× and 2.5× they met as a light cross. Such a piece is now
    resampled whole. At whole scales the pixels are the same either way.

- **Macro ranges on the halos** (shared, opt-in; ANDOR ANALOG's USER macros,
  its docs/MACRO_DESTINATION_FIRST.md).
  - `ModulationOverlay` can draw a product's macro assignments in its own vocabulary:
    - a quiet arc in the macro's colour;
    - the focused macro's range as the ring, with 0 % (hollow) and 100 % (filled) handles that
      drag along the ring (Shift: fine; Escape: cancel), the marker and the readout.
  - Hooks: `macroArcs`, `macroEligible`, `anyMacroArcs`, `macroOffset` (a route's sweep starts
    after the macros), `editMacroRange`, `resetMacroEnd`, `setMacroFocus`.
  - A control with no modulation destination can be walked for them. Its menu then has no empty
    MODULATION heading.
  - `ProductInfo::UserMacros` gained ranges / offsetNow / editRange / resetEnd / resetRange /
    unassign / unassignAll.
  - `ShellEditor`: "Assign to Macro" with the macros' names and checkmarks, plus the assigned
    items. The chosen macro's rings replace `MacroAssignOverlay`'s show mode.
  - ANDOR OSP sets none of it: 0 px, its tests unchanged.

## IN PROGRESS

- Lab round 3 (Reimagined 3, violin).
- DAW/host validation on a Mac (now including live capture's sidechain routing and the
  standalone app's microphone input).

## NEXT

- **AUX strip (shell, opt-in; ANDOR ANALOG's D / AUX, analog `docs/aux/AUX_REPORT.md`).**
  `ProductInfo::auxStrip` adds an optional strip under the source cards:
  - "Show / Hide AUX" in the main menu;
  - `<View auxShown>` kept with the session, never with a preset;
  - everything under the cards moves down by its room (`ShellEditor::auxShift`, the cards' own
    `cardTrim` unchanged);
  - `rack::Kind::aux` for its rack, and Escape closing that rack.

  Addendum 03 (all opt-in; ANDOR OSP unchanged):
  - `osp::plugin::DropOwner`: the shell's editor gives a file drop to the part under the pointer
    that owns drops, consumed (never a card's too);
  - `LayerStateButton` for a product source's own M / S parameter;
  - `ShellEditor::cardDisplayBounds`, which the strip aligns its zones to.
  - `AuxStrip::emptyDropsIgnored`: a file dropped on empty space (neither a card nor an owner)
    does nothing in a product that asks for it (ANDOR ANALOG).

  The window fits its screen (both instruments, not opt-in; analog `docs/aux/ADDENDUM_04.md`):
  `frame::applyWindowShape` never asks for more height than the screen holds when the
  arpeggiator or a rack makes the instrument taller (a host clamped such a window and left the
  editor cut off at the side). It narrows to the widest that fits, the instrument whole, and the
  chosen width comes back once it fits; `frame::ScreenFit` fits an editor first shown with a tall
  layout. Off screen nothing changes (the captures; shell test `[window]` on
  `frame::fittedWindowWidth`). Owner test: a real Mac screen in a DAW.

  Right-click menus reach the knob's press (both instruments): `ModulationOverlay` also listens to
  its root's presses, so a plain right-click on a destination or macro-eligible knob opens its menu
  in a host (the host gave the press to the knob, which has none; control-click worked). Tests
  `[rightclick]` in both. Second fix: with the right button down the overlay itself claims the
  press (its hitTest counts any destination for a popup), but its mouseDown looked only for
  modulated knobs, so an unmodulated knob opened nothing - found with a real right-click in
  ANDOR Analog's standalone. mouseDown now uses the same test; `[rightclick]` routes the press as
  JUCE does (the component under the point while the button is down) in both instruments.

  The AMP envelope's picture follows A D S R's modulation (both instruments): `EnvelopePanel::
  livePosition` (set by the editor from `ModulationOverlay::livePosition`, the halos' own sum of
  active routes and macros) draws the shape the newest note plays, the set shape dashed under it
  with the handles; with nothing sounding or routed the picture is the set one, pixel for pixel
  (`[ampenv]` in both). ANDOR ANALOG's macro tab no longer switches to USER on an assignment
  or a halo range drag (the shell's `setChosenUserMacro`; the halos show the chosen macro on
  either tab).

  14 Nov (feature/aux-live-input): ANDOR Learn V1 merged (shell targets for SUB's row, macro
  lamps, USER macros and the AUX strip; events macroBank.changed, usermacro.assigned).
  `Engine::takeRecording` (a product's card may take a dropped recording: ANDOR Analog's C2). A
  state load tells a hybrid engine its recording is gone. The window's width is the
  musician's alone (frame::applyWindowShape: the height follows, capped at the screen; the
  instrument scales inside; fittedWindowHeight) - both instruments. Card EQ spectrum hooks
  (InstrumentHost::stepLayerEqSpectrum / paintLayerEqSpectrum; none in ANDOR OSP here - its
  own card EQ spectrum is on the default line).

  Vertical-only expansion, fixed width (the owner's layout spec, 15 Nov): opening or closing a
  rack, the arpeggiator or a strip changes only the window's height - never its width, the
  instrument's scale or anything's horizontal place. `frame::applyWindowShape` no longer sets
  a fixed aspect ratio (a host could answer a taller window with another width); the editor's
  constrainer (`ScreenFit::Shape`) makes a width change (the corner, a host) take the height
  that goes with it - an even zoom - and keeps a height-only change (no taller than the
  instrument). `frame::fit` scales by the window's width only: the window as tall as the
  instrument is placed exactly as before (pixel-identical); shorter - capped at the screen, a
  host that refused, a dragged bottom edge - the instrument stays at the top-left at the same
  scale and scrolls (`frame::Overflow`: a slim vertical bar, the wheel where no knob takes it);
  taller - top-left, the surround below. Never shrunk, never centred, no side margins. Tests
  `[window][geometry]` (every shell product): each rack, rack to rack, the arpeggiator, a
  strip, all together, 100 cycles - width, cards, macros unchanged, the keyboard lower by the
  section only, back on closing; a refused height scrolls at the same scale; the constrainer.
  Refined (the owner's clarification, 15 Nov): the window always grows first, and scrolling is
  only the last resort. The constrainer answers every resize with the instrument's full height
  (never an old, shorter height kept). A host that first reports the old size while it
  negotiates is not taken as a refusal: `frame::Overflow` waits (`settleDelayMs`, no bar,
  unscrolled), asks for the full height once more, and only a window still short after that
  scrolls (`settleNow` steps it in tests). A shrink answered with the old, taller size is asked
  again too. The screen's own limit scrolls at once (the window already stands at its room;
  `overrideScreenRoom` in tests). Tests `[window][geometry][host]` with a stand-in host window:
  SPACE + ARP + the strip opened together grow the window to the whole instrument and closing
  shrinks it (a granting host); a host reporting the old size first never shows a bar and ends
  at the full height; a refusing host is asked twice, then scrolls at the same width and scale;
  a short screen scrolls at once. Real DAWs: UNVERIFIED (OWNER TEST).

  ANDOR Learn 2.0 (shared/andor-learn; ANDOR ANALOG's `docs/LEARN_2.md`, 15 Nov): Learn as an
  interactive manual. Changes:
  - One **Learn** menu entry: Tutorials..., Toggle Info (was "Learn mode"), Reset Tutorials...
    (asks first; clears lesson records only), and Restore Previous Sound while one is kept.
  - A quieter browser: START HERE / LESSONS, a description per row, the time, "Resume · Step n
    of N" or a check; keys; it re-reads progress on opening.
  - The note: label + STEP n OF N, a status line ("ARP is already on." + Continue; a check and
    what changed once done), one primary key and Back. **Skip is gone** (`LessonRunner::skip`
    and `StepStatus::skipped` removed; a finished lesson had every task done or already so; a
    resumed one counts from where it began).
  - A green outline (2 px, soft glow, padded, corners matched) on the exact control, with a
    connector when the note sits apart; scored placement (right / left / above / below, never
    on the target, avoiding the step's other controls and the panel the control is in, inside
    the visible part of a scrolled editor, the target scrolled into view).
  - Readiness: `Step::requirements` block a step while what it needs is missing (the note
    names it, outlines and offers the fix, and the lesson's example); a preparation step (Use
    Current Sound / Load Example Sound).
  - Example sounds: `LearnDescriptor::exampleState`; `ShellProcessor::loadLearnExample` keeps
    the whole sound first, `restoreLearnSound` brings it back; never on X, never unasked.
  - Exact targets: `binding::findControl` finds a control by the parameter it edits
    (`ParameterRack` marks its knobs, switches and selectors: no visual change); new shell
    targets `arp.on`, `arp.opener`, `arp.plays.A/B/C`, `arp.edit.A/B/C`, `arp.card.B.pattern` /
    `.rate`, `macro.N.knob` / `.label`, `usermacro.N.label`; states `arp.edit.B`,
    `rack.any.open`; registry values (`addValue`: a product's number compared like a parameter);
    events `control.menu.opened` (a control's right-click), `usermacro.range.changed` (a halo end
    committed).
  - Progress: records other than complete / in-progress (an earlier build's skipped) read as
    new; in-progress resumes by stable step ID across revisions; complete stays.
  ANDOR OSP's own plugin had no Learn then (it has now: below); its editor and captures were unchanged
  (4 / 4 shards). Tests: `[learn][unit]` (no Skip, requirements and preparation, a dropdown's
  late choice, legacy records), and the product suites (ANDOR ANALOG: 23 Learn cases).

  Universal modulation-aware visual feedback (docs/EFFECTIVE_VALUES.md): a picture draws what
  the DSP plays. `osp::EffectiveValues` (src/engine) is a fixed, lock-free table published by
  the audio thread (a sequence lock per slot, with the span covered over a UI frame);
  `InstrumentHost::effectiveValue` reads it; `InstrumentHost::shownValue` is what the pictures
  draw (MacroPopups' `value()`; `stored()` for presets, lane copies and snapshots); the halos'
  live dot and `livePosition` take the engine's sum (every route, clamps) with a faint span
  trail; the card EQ draws the engine's `effectiveEq`; the shell builds `liveMix()` from the
  engine's blend. ANDOR OSP publishes its six macros and each layer's EQ while a route moves
  them. Tests `[effective]`: SHAPER modulated to 80 % is pixel-identical to a hand-set 80 %;
  the knob is never written back. Unmodulated, every picture is as before. OPEN (listed in
  the doc): ANDOR OSP's per-voice destinations (CUTOFF / RESONANCE, grains, START, DAMPEN,
  envelope times) keep the UI mirror until the engine publishes a representative voice.

  AUX arpeggiator follower (opt-in; ANDOR ANALOG's D ARP, analog `docs/aux/D_ARP_FOLLOWER.md`):
  `ProductInfo::arpFollower` adds one more listener at the per-card arpeggiator's delivery
  (`ShellProcessor::runArpFollower`, after `runCardArpeggiators`; `Engine::setAuxNotes`). It
  plays exactly one lane - LINK's shared pattern, or with LINK off card A, B or C's, generated
  even while that card's own arpeggio is off - as a copy of that lane's arpeggiator taken the
  block the lane starts (fixed-size arrays: no allocation), so the notes and samples are the
  lane's. What the cards are given is unchanged. Any change of lane, mode, power, the
  arpeggiator or a state load ends what it played (note-offs, pedal off, All Notes Off). Its two
  parameters are read by the shell, so they count as bound. ANDOR OSP sets none: nothing runs.
  NEXT (shared ARP rule): ANDOR OSP has no AUX source to follow with; the follower ships there
  with an AUX source, if ANDOR OSP ever gains one - until then the gap is this one.

  External audio input (opt-in; ANDOR ANALOG's AUX D LIVE INPUT, analog
  `docs/aux/ADDENDUM_05.md`): `ProductInfo::externalInputName` adds one input bus, off by
  default, a VST3 auxiliary (sidechain) bus (`getVST3ClientExtensions`), copied before the output
  is written and handed to `Engine::setExternalInput`. ANDOR OSP sets none: output only, unchanged
  (shell test `[buses]`).

  It is a source, not a SPACE / ECHO / ARP feature. ANDOR OSP sets none and is unchanged: no menu
  item, no view attribute, no room (the shell test `[aux]`; the OSP captures). Should ANDOR OSP
  want a fourth source, it needs a product strip and an engine bus of its own.

- **ANDOR ANALOG's product tests on OSP's current line.** Building ANDOR ANALOG with
  external/osp at `claude/osp-wheels` compiles everything but `andor_analog_tests`.
  - ANALOG's own `product/Source/FxRouting.h` (and the FX presets, FX EQ, SATELLITE rig and
    SynthRack helpers its tests name as `andor::analog::`) are now shared in OSP as
    `osp::plugin::` (Shared FX R1-R3).
  - ANALOG's same-named headers shadow or are shadowed by OSP's.
  - When its pin moves: delete ANALOG's copies, include the shared ones, and change the tests'
    namespaces. No DSP change.
- **ANDOR ANALOG on the shared SUB.** ANDOR OSP's SUB lives in shared code:
  `src/engine/sub` (`osp::sub::SubOsc`), `InstrumentHost::addSubParameters` /
  `subSettingsFrom`, and `shared/andor-ui` `SubPanel` (in `AndorSubPanel.h`). ANDOR ANALOG still uses its own copies
  (`dsp/SubOsc`, `product/Source/SubPanel`). When its pin moves past `claude/osp-sub`:
  - its Synth switches to `osp::sub::SubOsc` (the same code);
  - `addSubTo` becomes `addSubParameters (layout, 30, "analog.sub." + field)`;
  - `createSubPopover` becomes `SubPanel (host, subview::Ids::from (...), shell keyboard)`;
  - its copies are deleted, and its SUB and panel tests run unchanged.
- **C RHYTHM in ANDOR OSP** (shared modulation, not a SPACE / ECHO / ARP feature). The source
  is appended to `osp::mod::Source` and offered only when a product asks
  (`ProductInfo::rhythmSource`). ANDOR OSP keeps its twelve sources and its golden is
  unchanged (tested). Should ANDOR OSP want it, it needs:
  - its own loop analysis and player (ANDOR ANALOG's are in its `dsp/rhythm`; they could move
    to `src/analysis` and `src/audio` unchanged: pure C++);
  - a schema bump for the route-source list.
- **The effects rack in ANDOR OSP's own plugin** (the rule on shared SPACE / ECHO / ARP
  features). The rack host (`shared/andor-ui/Source/EffectRack.*`), its modules (built in
  `MacroPopups.cpp` from the popovers' parts) and the frame's rack slot are shared and opt-in
  (`ShellProduct::rackModules`); ANDOR ANALOG shows MOVEMENT's (with its two lanes:
  `PostProcessor::setMovementLanes`), ECHO's and SPACE's. ANDOR OSP shows none, so its look
  and the visual parity stay exactly as they were; its MOVEMENT stays one lane (every golden
  unchanged).
  To bring it to ANDOR OSP:
  - done (Shared FX R3/R4): the macros' names open the racks, OSP's editor hosts them;
  - save the rack's view state with OSP's session (as `arpOpen`);
  - review the screenshots at 869 / 1448 / 1810 px and update OSP's own captures.

- **The enamel faceplate (V5) in ANDOR OSP** (the rule on shared ECHO / SPACE features): shared
  and opt-in through the rack faceplate. It arrives with the effects rack: give OSP's ECHO /
  MOVEMENT / SPACE faceplates `enamelBody`, a motif painter and an ink in OSP's own palette,
  then review 869 / 1448 / 1810 px.

- **The TAPE deck in ANDOR OSP** (the rule on shared ECHO features). It is shared
  (`MacroPopups.cpp`: `TapeDeck`, `UnifiedRack::setDeck`) and opt-in by the rack faceplate,
  which ANDOR OSP's plugin does not use yet. It comes with the effects rack above: when OSP's
  plugin shows the rack, give its ECHO / MOVEMENT modules a faceplate with `tapeDeck` (or a
  plain-rack flag for the deck alone), publish the output peak from OspAudioProcessor
  (`InstrumentHost::outputPeak`), and review the 869 / 1448 / 1810 px captures.

- **Per-card arpeggiator in ANDOR OSP's own plugin** (the rule on shared SPACE / ECHO / ARP
  features). The shell's lanes and the ARP panel's PLAYS / LINK / EDIT are shared and in
  ANDOR ANALOG. Still to do in ANDOR OSP:
  - share the lane logic between the shell and OspAudioProcessor;
  - give InstrumentEngine a layer mask per note (it already starts a voice per layer);
  - opt OSP's panel in;
  - leave old sessions identical.

- Lab round 1 scored and acted on (docs/reports/lab-1.md). Round 2 in the lab: Reimagined 2,
  Sustain 2, Dynamics 2, Multi-sample 2.
- Transient preservation and transient mixing: both kept as options, off by default
  (lab round 1: preservation lost on 3 of 4 sources, mixing split 1–1).
- Timbre clustering for sets that mix organ stops (waits on morning question 2).
- Real-Mac worst-case callback timing. Reads above x4 (more than about +2 octaves) still
  evaluate the stretched kernel; mip-mapped sources would cap them if that matters.

## SPACE (2 Nov 2026)

- **New defaults:** SPACE now starts as a HALL at 3.0 s with HIGH CUT at 5 kHz, for every
  room type. Before, it was a PLATE at 1.8 s with HIGH CUT at 12 kHz.
  - This changes `Shaping` and `SpaceReverb::Settings`, so the parameter defaults follow, in
    ANDOR OSP and in every shell product.
  - Saved sessions keep the values they stored. Only new sounds, resets and partial configs
    take the new defaults.
  - The parameter schema golden is updated (three defaults).
- **Plan:** three more SPACE engines (VINTAGE, BLOOM, ABYSS) in
  `docs/spec/SPACE_ENGINES_PLAN.md`.
- **ABYSS (phase R1): built.**
  - `engine/AbyssReverb`, selected by `space.engine`, with GRAVITY, FREEZE and RESONANCE
    (`space.abyss.*`, version hint 23).
  - The SPACE popover lists ABYSS under the rooms. It shows FLOW / FREEZE, GRAVITY and
    RESONANCE, and its own picture.
  - ROOMS stays bit-exact: every regression golden is unchanged, and visual parity is unchanged.
  - Evidence: the `[abyss]` tests (RT60, gravity swell, freeze, stability, determinism, no
    allocation, level matched to HALL); the click test for engine and FREEZE switches.
  - CPU is about 2x HALL (SIMD later).
- **VINTAGE and BLOOM (phases R2, R3): built.**
  - `engine/VintageReverb`: a low internal rate (20.48 / 34.13 kHz), short words, coarse
    coefficients, tap-crossfade chorus. Variants HALL / PLATE / CHAMBER / ROOM, ERA, DIFFUSION.
  - `engine/BloomReverb`: a diffusion delay, synced or free, CLEAN / VINTAGE / TAPE / WASH,
    DIFFUSION.
  - Both are chosen in the same list (`space.engine` now has four choices) and have their own
    popover layouts and pictures (`space.vintage.*`, `space.bloom.*`, version hint 24).
  - Evidence: the `[vintage]` and `[bloom]` tests (RT60 in every variant, era bandwidth,
    short-word grain, repeats on the beat at two tempi, diffusion's crest factor, decay,
    stability, determinism, no allocation, level matched to HALL); engine switches never click.
  - CPU: VINTAGE about 0.75x, BLOOM about 0.64x HALL.
- **ABYSS's variants: built.** `space.abyss.variant` (version hint 25): DEEP / DARK / INVERSE /
  ENDLESS, as the ABYSS popover's tabs (FREEZE moved to its second row).
  - DEEP is ABYSS as it was, bit-identical.
  - DARK: lower band corners and a much darker tail.
  - INVERSE: GRAVITY's rising window always on and longer, and a faster loop: it swells in.
  - ENDLESS: 6x the reverberation time, more motion.
  - Each variant is level-matched to HALL. Evidence: the variants test in `[abyss]` (DARK's
    tail much darker, INVERSE peaks at 1.65 s, ENDLESS's RT60 5.7x DEEP's, every variant
    within 0.01 dB of HALL and stable at the extremes).

## ECHO: ASCEND (3 Nov 2026)

- **ASCEND: built.** ECHO's third machine (`echo.type` gained ASCEND after TAPE and BBD), an
  original design in the spirit of an octave-up delay pedal: a pitch shifter inside the
  delay's loop.
  - The loop, in order:
    - CLOCK: the line is written through a sample-rate reducer, 13 kHz down to 7 kHz. It
      has a half-clock anti-alias filter before the hold and a reconstruction filter after.
    - TONE's low-pass (2 .. 5 kHz), placed before the shifter.
    - The shifter: two heads 180 degrees apart sweep a window of TIME / 8 (held within
      30 .. 50 ms), Hann-crossfaded.
  - The aligned splice. Where a head jumps back, it lands at the point in a 15 ms span
    where the waveform best lines up with the other head (coarse and then exact
    correlation).
    - Without it, the textbook dual-head shifter drifted by up to a grain's rate: about
      -25 cents on an A, different for every note, so partials went out of tune.
    - With it, every note tested is within 8 cents of a true octave.
  - New parameters (`echo.ascend.interval`, `echo.ascend.motion`, version hint 26):
    - INTERVAL: +12 / +7 / +5 / -12.
    - MOTION:
      - RISE: the output and the loop are both shifted. The first repeat is up, and each
        next one is higher.
      - STEADY: only the output is shifted. Every repeat is at the interval.
      - LATE: only the loop is shifted. The first repeat is as played, then climbing (the
        spec as first drawn).
  - Existing controls on ASCEND:
    - AGE is shown and works as CLOCK.
    - FEEDBACK reaches a loop gain of 0.85, with a soft ceiling on every write.
    - There is no wow.
  - The popover: TIME / FEEDBACK / TONE / CLOCK, plus a second row (INTERVAL, MOTION) only
    on ASCEND. TAPE's and BBD's layout is unchanged. The picture labels each repeat's pitch.
  - Existing sessions: TAPE and BBD are untouched.
  - Evidence: the `[ascend]` tests:
    - the octave, two octaves, STEADY and LATE pitches;
    - +7 / +5 / -12;
    - in tune across seven notes;
    - TONE darkens;
    - level within 30 % of TAPE's (measured 0.95 .. 1.02 from 200 Hz up);
    - stable at FEEDBACK 100 % in every motion, interval and stereo picture;
    - deterministic, and silent in silence.
  - CPU: about 1.3 % of a core at 48 kHz (TAPE about 0.5 .. 0.8 %).

## SPACE presets (3 Nov 2026)

- **Built (shared: ANDOR OSP and ANDOR ANALOG).** SPACE's row of four tabs (redundant with
  the header's list on ROOMS) is now a presets bar: `‹ NAME ›`.
  - The arrows step through the presets.
  - The name opens them by category: ROOMS, HALLS, PLATES, SPRINGS, ABYSS, VINTAGE, BLOOM
    (38 to start; `shared/andor-ui/Source/SpacePresets`).
  - ABYSS's, VINTAGE's and BLOOM's variants moved into the header's list, as submenus of
    their engines; the header shows "ABYSS · DARK".
- A preset is values for the parameters SPACE already has, nothing else.
  - Choosing one sets every SPACE parameter (one undo step).
  - Sessions and patches store the parameters as ever: everything made before plays as it
    did.
  - The bar works its name out from the values: the preset they match; the last one chosen
    with " *" while SPACE is still of its kind; else what is set ("ABYSS DARK").
  - A new instrument is "Concert Hall" (SPACE's defaults).
- Evidence: `[presets]`:
  - every preset is recognised by its values and names its engine;
  - edited, " *" or what is set;
  - the bar is in every engine's popover;
  - a session from before is named by what it plays.
  - ANDOR ANALOG's three-engine screenshots show the bar on every engine.
- To do: voice each preset by ear (they are starting points); user presets later.

## Shell: the per-card arpeggiator (3 Nov 2026)

- **Built (opt-in: `ProductInfo::cardArpeggiator`, a three-engine product; ANDOR OSP has it
  too since 4 Nov, below).** The shell adds, in the product's namespace (`analog.`), `arp.link` and `arp.cardA` /
  `arp.cardB` / `arp.cardC` (which cards play the arpeggio; the others hold the keys), and B's
  and C's own PATTERN / RATE / OCTAVES (`arp.cardB.*`, `arp.cardC.*`), all version hint 27.
  ANDOR OSP's own copies are unprefixed (`arp.link` ..), so the shell's schema test compares
  against OSP's golden less them. The defaults (LINK, every card)
  are the one arpeggiator, exactly as before: old sessions and presets play unchanged.
- How it plays (`plugin::CardArpLanes`, shared by `ShellProcessor` and `OspAudioProcessor`):
  - With LINK on and every card in it (or the arpeggiator off), the one arpeggiator runs as
    before.
  - Otherwise up to three lanes run, each an arpeggiator playing some cards. LINK gives one
    lane for the cards in it; unlinked gives one per card, B and C on their own settings, with
    GATE / SWING / VELOCITY and the ARP destinations shared.
  - The cards left out hold the keys as played, with their own sustain pedal; the engine's
    pedal would hold the arpeggio too.
  - Each note's MIDI channel names its cards (1 + mask) for the engine
    (`Engine::setCardRouting`).
  - Unlinked lanes' random patterns have their own seeds derived from the stored one.
  - A change of cards or LINK mid-performance ends what sounds and plays the keys still
    down into the new lanes, so no note is ever left hanging.
- The ARP panel (`InstrumentHost::cardArpeggiator`; the visual regression masks the row,
  `arp-cards`, for products with one arpeggiator):
  - PLAYS A B C, each lit in its card's colour;
  - LINK;
  - unlinked, EDIT A | B | C, which picks whose PATTERN, RATE and OCTAVES the panel shows and
    sets, and whose steps the display plays.
- Evidence: ANDOR ANALOG's `[cardarp]` tests, its bindings test (all ten new parameters move
  the sound), and its screenshots `analog-arp-card-c` and `analog-arp-unlinked-b`.

## ANDOR OSP: live capture into a chopped phrase (6 Nov 2026)

- **What the musician does.** Each card has a REC key (beside EQ; a dot alone on narrow cards).
  It opens the CAPTURE page in the card's display:
  - the INPUT meter and a scrolling level picture of what arrives;
  - RECORD / STOP: a take for this layer, a quarter second before the press kept, stopping
    itself at 1:00 (the processor's timer, page open or not); DISCARD throws it away;
  - GRAB LAST 10 / 20 / 30 / 60 s: what was just played, kept after the fact.
  - A kept take is trimmed (silence before and after, a 50 ms lead-in and 250 ms tail kept),
    stored, and MAP NOTES runs on it at once: it becomes the layer's notes (one undo step when
    it replaces a sound). The page closes and the card shows the notes.
  - An empty instrument has Record... beside Browse... and Load example; the header menu has
    Record (into a new layer)... while a slot is free. The page covers the sources until the
    take arrives.
- **Inputs.** A stereo input bus named Sidechain: off by default in a plugin (the host routes a
  track to it: VST3 reports it as an aux bus, `getPluginHasMainInput` false), on in the
  standalone app (the chosen microphone or interface input; macOS asks for the microphone with
  NSMicrophoneUsageDescription). The standalone app's feedback mute is lifted because OSP never
  plays its input back: the instrument's render overwrites the shared buffer.
- **How.** `audio/capture/CaptureBuffer` (pure C++): a ring of 64 s + 4 s margin, allocated in
  prepareToPlay only while an input is connected (about 27 MB at 48 kHz). The audio thread only
  writes it (memcpy and an atomic store); the message thread copies a take out by absolute
  position, and a stretch that may have been overwritten is refused, never torn. The loader
  thread writes the take into the sample store and analyses it (`AnalyseRequest::captured`,
  origin `liveCapture` / `bufferSnapshot`, `captureId`).
- **Shared code.** `InstrumentHost::liveCapture()` (false by default) and the capture calls;
  the CAPTURE page, REC key and the drop zone's Record... are in shared/andor-ui. The shell has
  no capture yet; its parity render draws the REC key and Record... as ANDOR OSP does, so the
  visual regression stays exact.
- **Evidence.**
  - Unit `[capture]` (7 cases): exact positions across the wrap, overwritten stretches refused,
    mono kept mono, the meter, a concurrent writer and reader (whole or refused), no allocation
    in write, trimming.
  - Plugin `[capture]` (8 cases):
    - the bus layouts and VST3's aux bus; unconnected, the page says how to connect;
    - the input is never heard;
    - RECORD maps a four-note phrase onto B with its origin, trimmed, stored once;
    - GRAB on a mono input keeps only what was just played; replacing it is one undo step;
    - silence, DISCARD, the minute's limit, a new sample rate;
    - no allocation on the audio thread;
    - the card's REC key, the page's clicks, and recording from an empty instrument.
  - Screenshots: `OSP_CAPTURE_SHOTS=<dir> osp_plugin_tests "[capture-shots]"`.
- **Not verified yet:** routing a sidechain into OSP in Logic, Ableton and Reaper, and the
  standalone app's microphone prompt, need a Mac.

## ANDOR OSP: the per-card arpeggiator (4 Nov 2026)

- **Shared lanes.** The lane logic moved out of `ShellProcessor` into
  `shared/andor-ui/Source/CardArpLanes.{h,cpp}`. The shell and `OspAudioProcessor` both run it
  through a small `CardArpLanes::Host` adapter (their arpeggiator, its settings, the engine's
  card routing, the display), so the two instruments cannot drift apart.
- **Layer masks in the engine.** `InstrumentEngine::setCardRouting`: on, a note's MIDI channel
  names its layers (channel = 1 + mask, bit 0 A .. bit 2 C) and a note-off ends only its own
  channel's voices. The event index is spent either way, so each layer's randomness does not
  depend on which others a note plays. Off (the default) is bit-identical to before.
- **Parameters** (version hint 27, appended; `InstrumentHost::addCardArpParameters (layout, "")`):
  `arp.link`, `arp.cardA/B/C`, `arp.cardB/C.pattern/rate/octaves`. The defaults (LINK, every card)
  are the one arpeggiator. A session without them opens there, so it plays exactly as before.
- **Fallbacks.** Under MPE (each member channel is a note) and MONO (one held-key stack), the
  channels cannot name layers, so OSP runs the one arpeggiator on every layer. A switch in or
  out of either ends the lanes' notes cleanly.
- **Panel.** OSP's ARP panel shows PLAYS / LINK / EDIT (`cardArpeggiator()` is true). The
  display follows the card being edited.
- **Fixed on the way:** `Arpeggiator::ensureSequence` used `std::stable_sort`, which allocates
  a temporary buffer on the audio thread (both instruments). It is now an insertion sort with
  the same order and no allocation.
- **Evidence:**
  - `[card-arp]` engine tests: a channel plays only its layers, and a note-off ends only its
    own; routing off is sample-identical whatever the channel.
  - Plugin `[card-arp]` tests:
    - recall, and an old session opening as the one arpeggiator;
    - LINK plus every card is sample-identical to the arpeggiator;
    - each card heard: one card arpeggiates while the other holds the key;
    - unlinked lanes on their own rate;
    - nothing hangs after rapid changes with the pedal, or after a panic;
    - MPE and MONO fall back;
    - no allocation in any arrangement.
  - The shell suites and ANDOR ANALOG's `[cardarp]` tests run against the shared lanes.

## ANDOR OSP: sound packs - publishing, the Core pack in the installer (9 Nov 2026)

Decision (owner): one installer with a Core factory pack of 50-100 presets; instruments and
styles as expansion packs, sold as `.osppack` downloads from the Evenant website. Spec and
the case for website-first over an in-app store: `docs/release/SOUND_PACKS_SPEC.md`.

- [x] `PackExport::factory` (off by default: every earlier export is unchanged).
- [x] Library: **Publish as pack (id, version, licence)…** on a collection; fields remembered
      per collection, the next version suggested.
- [x] Build: every `*.osppack` in `OSP_FACTORY_PACKS_DIR` (default `packaging/factory-packs/`,
      git-ignored) goes into each bundle's `Contents/Resources/Factory`. With no pack the
      bundles are exactly as before (checked: no copy step in the build, VST3 `Resources`
      holds only `moduleinfo.json`); with one, it lands where the plugin installs from.
- [x] Tests: `[unit][library][packs]` (factory export installs from the plugin's folder,
      updates in place, user content untouched); `[plugin][library-packs]` (publishing
      through the window, installed at startup beside a musician's preset, which stays
      byte-identical).
- [ ] Owner: choose the Core set and publish it; the first expansions.
- [ ] Drop or double-click a `.osppack` to install it (phase 1, small).
- [ ] "More sounds" catalogue from the website's `packs.json` (phase 2).

## ANDOR OSP: Pedal Loop 1.0 (10 Nov 2026; merged with the shared FX into the default line 11 Nov 2026)

Merged at the user's request: the parameters now follow the FX library's (version hint 31,
was 29), the default sound stays byte-identical (the 55 research-renderer references), and
every suite is green with both features.


The sustain pedal can hold a One Shot note in a loop: per layer **PEDAL LOOP** Normal / Forward /
Ping-Pong (`layerA..C.pedalLoop`, version hint 31, Normal by default). Reports and measurements:
`docs/research/pedal-loop/` (00-audit .. 07-release-review).

- [x] Loop regions analysed offline beside the continuation (`ContinuationModel::pedal`): a
      sustaining source's best loop first; up to 8 short regions along a decaying source's body;
      levels and trend for late arming and Forward's trend compensation.
- [x] Per-voice state (off / armed / looping) on the continuation's crossfade machinery: Forward
      jumps; Ping-Pong turns with an 8 ms mirror crossfade, linear gains (chosen by measurement
      over reflection, 4 / 15 ms and equal power); late arming matched in level; exits that read on
      into the recording; the latest pedal state wins. Normal: nothing new runs.
- [x] Host parameters, sessions (older ones open on Normal), the mode menu's PEDAL LOOP section
      (no control on the card), the "No suitable loop" line.
- [x] Tests `[pedal-loop]` (unit, integration, plugin); `research-renderer --pedal-loop-lab`
      (212 renders: stand-ins and two recordings, all finite).
- [ ] Owner listening with real recordings (piano, nyckelharpa, vocal, a chord): 06-listening.md.

## ANDOR OSP: automatic multi-sample mapping (13 Nov 2026)

Several one-shot files dropped together (or chosen with **Add layer…** / **Replace sound…**)
are ONE instrument in the source they land on. That is a new source on an empty instrument or
on the ADD place, or only the card dropped on. Audit, measured reproduction and report:
`docs/research/multisample-mapping/` (`00-audit.md`, `01-report.md`).

- Already present and unchanged:
  - nearest root, ties to the lower, the whole keyboard 0–127 for file sets;
  - octaves kept apart;
  - the acoustic root of plucks;
  - dynamics-word velocity layers;
  - the Note Atlas FILL limits;
  - a folder drop as one sound.
- New:
  - several loose files are one set;
  - natural file-name order;
  - a strict round-robin cycle in file order per (source, root, velocity layer), starting at
    take 01 and independent of other notes (filled keys share their root's cycle);
  - unlabeled louder or softer takes stay round robins.

  The policy is stored on the set (`InstrumentSet::cycleTakes` / `loudnessLayers`, optional
  properties of the session's `Set` tree). A session saved before has no policy and keeps the
  original rules (random never-repeat pick, ≥ 4.5 dB loudness layers), so it sounds as it did.
- Headless: `research-renderer --multisample <dir>` (`--print-members`, `--legacy-mapping`).
  Render configs take `"mode": "imprint"`, `"imprint"` and `"motion"` in a layer's `"synth"`.
  The demo is synthetic violin pizzicato (`make_demo.py`, no recorded audio).
- Tests: `[multisample]`, 9 core and 5 plugin cases. The 55-render engine-C bit-exact matrix
  is byte-identical. Full suites green: core `ctest` 5/5, plugin tree `ctest` 10/10 (OSP plugin, shell visual reference, memo lab, the example products, unit, integration, regression).

## ANDOR OSP: the CHARACTER rack's graph moves; the card EQ shows the spectrum (14 Nov 2026)

- **CHARACTER rack graph, as ANDOR ANALOG's FILTER graph.**
  - LIVE: it follows the most recent sounding voice's filter (a newer note takes over; the
    followed one is kept through its release). The cutoff is the one the voice uses:
    CHARACTER, its routes, velocity, drift and the envelope. The base is drawn muted, the
    envelope's peak dashed, with BASE / NOW / PEAK figures and the A / D stage lit.
  - PREVIEW: an edit to CHARACTER, its range or its envelope with nothing playing runs a
    silent C4 through the same AD envelope.
  - IDLE: the knobs.
  - The drawn cutoff eases over ~45 ms (drawing only).
  - Engine telemetry: `CharacterTap`, one per voice slot, a lock-free sequence-counted
    snapshot the audio thread writes once a block from the voice's last control update. It
    changes nothing a voice plays: no allocation, no lock.
  - The curve now stays inside its plot (it used to dip into the frequency labels).
  - Fix (14 Nov, after the first Mac test): OSP's editor never refreshed its racks on its timer
    (only on open / close), so the graph did not move in the plugin. `racks.refresh()` now runs
    on every tick, as in the shell's editor. Test: `[character-graph]` "the editor's own refresh
    drives it", which fails without the line.
- **Card EQ spectrum.** Each source card's EQ draws the routing page's EQUALIZER rack spectrum
  behind its curve: the layer's output filled, its input a quiet line.
  - It is the same analyser, not a copy: `EqSpectrum` (shared/andor-ui) is now what both
    draw.
  - The engine gives each layer's EQ the two `fx::SpectrumTap`s the EQUALIZER has (around
    `eq::Processor::process`, EQ on or off; telemetry only, audio unchanged).
- Both reach the editor through one read-only host hook, `InstrumentHost::telemetryEngine()`
  (nullptr in the shell products: their card EQs and graphs are unchanged).
- Tests: `[character-graph]` (idle, preview rise and decay, live note / stage / peak, a live
  CHARACTER turn, easing, back to idle) and `[card-eq-spectrum]` (floor, rising while a note
  plays, the same reading as the shared analyser, the fall). On demand:
  `[character-graph-shots]`, `[card-eq-spectrum-shots]`.
- NEXT: ANDOR ANALOG's card EQs (the shell's) can show the spectrum once its host returns its
  engine's layer taps.

## SYNTH defaults: SHAPE FOLLOW, IMPRINT and MOTION 100 % (15 Nov 2026, state 15)

- **SHAPE** (`layerX.synth.shape`) now defaults to FOLLOW, so the synthesizer's level follows the
  recording.
- **IMPRINT** and **MOTION** (`layerX.synth.imprint`, `.motion`) now default to 100 %, so turning
  IMPRINT on is heard at once.
- **Older sessions and presets** (state below 15, factory ones included) open with every layer's
  SHAPE as FOLLOW. IMPRINT and MOTION at 0 %, the old default, open as 100 %; values above 0 are
  kept.
- **Not migrated: HELD saved after this change.** A preset saved now with HELD keeps HELD.
- **Audible change.** A session that played SYNTH with HELD now plays it with FOLLOW. Renders
  with SYNTH off are unchanged.
- **Schema golden.** It follows the new defaults: three SHAPE lines and six IMPRINT / MOTION
  lines.

## PITCH, MOD WHEEL and ADVANCED: the wheels in OSP, one popover scale (16 Nov 2026, branch claude/osp-wheels)

- **The wheel line in OSP.** OSP's `feature/aux-live-input` wheel work (`9c97d21..cd2291d`:
  PITCH / MOD caption popovers, per-voice VIBRATO) is merged onto OSP's current line, keeping
  SUB, LOOP's fallback, Learn 2.0 and SATELLITE.
  - `vibrato.*` is appended after SUB (version hint 28, ANDOR ANALOG's).
  - `destExt` gains VIBRATO DEPTH and AMOUNT at its end; saved indices are unchanged.
- **One popover scale.** The shared measures are in `ShapingPopups.h` `popover::`, used by the
  macro popovers too (which are unchanged).
  - PITCH BEND: 280 x 142 (was 436 x 350).
  - MOD WHEEL: 340 x 256 (was 610 x 634). VIBRATO ON in the title row; DEPTH, RATE, DELAY,
    RISE and AMOUNT in one row; CONTROL; SOURCES.
  - ADVANCED: 340 x 249 (was 436 x 675). One view in two sections, PLAYING and DYNAMICS; no
    BEND (under PITCH); placed clear of ARP / SUB.
  - Every control and parameter is kept.
- **Learn.** Help for Pitch Bend, Mod Wheel, Vibrato and Advanced Settings; exact caption and
  VIBRATO targets; the lesson "Add vibrato to a recording".
- **Tests.**
  - `[popover-scale]` at 869, 1086, 1448 and 1810 px: scale, 58 px cells, no overlap, inside
    the window, every control, clear of ARP / SUB;
  - the vibrato lesson end to end;
  - `[wheels]`, `[vibrato]`, `[sub]`, `[short-loop]`, `[learn]` and the schema golden
    (`vibrato.*`, `destExt`: intentional).
- **ANDOR ANALOG against this branch** (`feature/aux-live-input` with external/osp pointed here):
  - its engine, plugin and standalone build, and `analog_dsp_tests` passes (321 cases, vibrato
    and wheel included);
  - its product tests do not compile yet (see NEXT);
  - the shared SUB panel's file was renamed `AndorSubPanel.h`, so it no longer shadows
    ANALOG's own `SubPanel.h`.
- Report: `docs/wheels/osp-wheels.md`.

## LOOP holds short notes (15 Nov 2026, branch claude/osp-short-loop)

- **The problem.** With LOOP on, a short sustained recording played once: the continuation
  analysis found no loop in a body shorter than 0.5 s.
- **The fix.** LOOP falls back to Note Atlas's forced loop, one crossfaded loop over the note's
  body (`fallbackLoop`, `PitchLayer::fallbackContinuation`), built in the background with the
  continuation.
  - Only for short notes: "too short", "stable region too short" or "no body after the attack".
  - Never for struck or plucked notes, which still play once.
  - The natural continuation always wins when it exists.
- **Compatibility** (state version 16, the musician's choice):
  - Sessions and presets saved before keep their sounds as they played (`loopFallback` off for
    each layer that holds a saved sound); re-saving keeps that.
  - A sound loaded later, and every new session, has the fallback.
- **No change elsewhere.** The engine default is off, so the research renderer, baselines A/B
  and every golden render are unchanged.
- **Tests.** `osp_tests "[short-loop]"` (the duration matrix; held, once, LOOP off;
  bit-identical for long notes) and `osp_plugin_tests "[short-loop]"` (the session policy end to
  end) pass. `[short-loop-renders]` makes before / after renders.
- **Measured.** Short notes now hold at their level for 3 s (before: -69 dB by 1 s). The seam
  steps and level wobble match a natural loop's. Report: `docs/research/short-loop.md`.
- **Not done.** No listening on real short recordings.

## SUB in ANDOR OSP (15 Nov 2026, branch claude/osp-sub)

- **What.** ANDOR ANALOG's SUB (sub oscillator V6, popover V7) in ANDOR OSP. It plays one or two
  octaves below the keys held, from the MIDI before the arpeggiator, so it holds a chord while
  the cards arpeggiate. It joins after the effects and before the output gain, and is never sent
  to ECHO or SPACE.
  - It follows FILTER while FILTER is a low-pass, with an OSP mapping of CHARACTER.
  - It follows MOVEMENT's PULSE and SHAPER.
  - It is delayed by a CUSTOM route's latency.

  Report: `docs/sub/osp-sub.md`.
- **Shared code:**
  - `src/engine/sub` (SubOsc, verbatim, `osp::sub`);
  - `InstrumentHost::addSubParameters` / `subSettingsFrom` (ANDOR ANALOG's names, ranges and
    defaults);
  - `shared/andor-ui` `SubPanel` (V7; IDs and lit keys supplied by the product).

  ANDOR ANALOG adopts them when its pin moves (NEXT).
- **Parameters.** Ten `sub.*` at version hint 33, appended after SATELLITE. They are off by
  default and off in sessions from before (absent IDs load the defaults). The schema golden gains
  the ten lines (intentional).
- **Interface.**
  - The arpeggiator housing has two rows, ARP and SUB. SUB's light switches it; its row opens
    the panel above. This is a deliberate visual change.
  - The shell's parity render draws the same row, so the example product's visual check stays
    at 0 px.
  - Learn has a SUB Info topic, targets, and a six-step lesson (`osp.sub`).
- **No sound change while off.** Bit-identical (tested). Every golden render is unchanged.
- **Tests.** `osp_tests "[sub]"` (10 cases) and `osp_plugin_tests "[sub]"` (8) pass.
  `[sub-shots]` makes the screenshots and five demo renders.
- **Not done.**
  - No DAW pass, and no listening on monitors.
  - SUB treats CC64 as sustain even when a layer's PEDAL LOOP uses the pedal.
  - ANDOR ANALOG is not switched to the shared copy yet.

## ANDOR Learn 2.0 in ANDOR OSP's own plugin (15 Nov 2026, branch claude/osp-learn-2)

- **What.** Learn 2.0, the shared framework (`shared/andor-learn`) that ANDOR ANALOG uses, now
  runs on `OspAudioProcessorEditor` through an OSP adapter (`apps/plugin/Source/OspLearn.cpp`).
  OSP has no copy or parallel version.
  - It has 11 lessons for the recording-to-instrument workflow and 32 Info topics.
  - Each lesson but Quick Start opens with a set-up step: Use Current Sound or Load Example
    Sound.
  - Step requirements (a playable sound, Granular, two layers, a sample set) block a step with a
    reason and the control that fixes it.
  - It ships synthetic tutorial sounds, Restore Previous Sound and per-user progress under `osp`.

  The report, the target inventory and the editorial checks are in `docs/learn/osp-learn.md`.
- **The merge.** OSP's `feature/aux-live-input` (Learn 2.0, user macros, effective values, the
  shell's inline arpeggiator lanes and follower) is merged into OSP's line.
  - The shell takes that line's version.
  - OSP's processor keeps its own `CardArpLanes` and SATELLITE.
  - This branch replaces `claude/osp-learn`: its V1 extensions are dropped in favour of 2.0's.
- **One shared addition:** `ParameterGestureBridge`. It reports gestures of OSP's own controls as
  `param:<id>`. It is not used by the shell, so ANDOR ANALOG is unchanged.
- **Tests.** `osp_plugin_tests "[learn]"` (8 cases, 438 assertions) passes, and so does the
  shell's Learn 2.0 `[learn]`. `[learn-shots]` renders every step (182 images).
- **No sound change.** Learn adds no parameter and no DSP change.
- **Not done.** No DAW pass. The tutorial sounds have not been listened to. ANDOR ANALOG has not
  been rebuilt against this branch.

## ANDOR OSP: SATELLITE opens on its key; CHARACTER is called FILTER (15 Nov 2026)

- **SATELLITE's panel opens.** It used to close in the press that opened it: JUCE tells the
  editor's outside-click watcher after the key has opened its panel. OSP's editor and the
  shell's now leave the anchored panel's own key alone (ANDOR ANALOG's d99f5d8). A press
  elsewhere still closes it. `[satellite]` replays the watcher after each press; it failed
  before the fix.
- **Lamps without a seam cross** (the same Analog commit): a skin piece with no centre to
  stretch is resampled whole at fractional display scales.
- **FILTER.** ANDOR OSP's macro row (`frame::ospMacroSpecs`) names the `character` macro
  FILTER. Its rack, popover and the modulation names follow it through
  `InstrumentHost::characterName()` (FILTER CUTOFF, FILTER RESONANCE). The shell takes the
  name from the product's macro row, so the example product matches OSP (its visual
  regression stays at 0 px). Unchanged: the parameter IDs (`character`, `character.*`), the
  host's parameter names ("Character ...") and every saved session.

## ANDOR OSP: SATELLITE, each layer's own stereo motion (14 Nov 2026)

ANDOR ANALOG's SATELLITE, the per-card arpeggiator's companion, is now in ANDOR OSP. This was
the plan noted under NEXT on `feature/satellite-rhythm`. Shared parts:
- the motion (`src/engine/SatelliteMotion.h`);
- the arpeggiator's SATELLITE key (`ArpSection`: `SatelliteKey`);
- `AnchoredPopoverHost` (ShapingPopups.h, both editors);
- the panel, moved out of ANDOR ANALOG into `shared/andor-ui/Source/SatellitePanel` (the product
  names its parameters; the marker reads `InstrumentHost::satelliteView`);
- `MacroSwitchLamp`.

Only those were taken from that branch. Its USER MACROS, SHAPER DNA and macro-lamp hooks stay
there.

- **Engine.** A layer's voices are always that layer's: ANDOR ANALOG's "a voice of one card
  moves whole", exact, with no twin filters.
  - Per chunk, the layer's place uses the shared paths:
    - PAN, ORBIT with its rear cue (−3 dB and a darkening near 2.2 kHz), BOUNCE, BOUNCE + NOTE
      held per voice;
    - SYNC on the host's quarter notes, or a local clock started by a note; FREE at
      0.02–10 Hz;
    - 6 ms path smoothing, a 20 ms glide on and off.
  - Each voice applies it at its pan: its own pan (scaled by WIDTH) times the place's
    equal-power gains, interpolated across the chunk.
  - No allocation, no lock.
  - With every layer off and its glide done, the voices take the original path, bit for bit.
- **Plugin.** `layerX.satellite.{enabled, mode, sync, division, rate, depth, width, phase,
  offset, retrigger}` use ANDOR ANALOG's ranges, defaults and texts, appended at version hint 32.
  The schema golden gains exactly these 30 lines.
  - Sessions from before open with every layer still (tested by stripping the parameters).
  - The panel opens anchored under the key, or above it where there is no room.
- **Effects after the layers.** SPACE, ECHO and REIMAGINED's KALEIDOSCOPE still spread what
  they receive. Hard right therefore keeps what they put on the left (as in ANDOR ANALOG,
  whose effects follow its voices too).
- **Tests.**
  - `[satellite]`, unit, 7 cases: off bit for bit, the place, PAN's rate, SYNC, ORBIT's cue,
    BOUNCE + NOTE, off returning exactly with no jump, determinism, the marker.
  - `[satellite]`, plugin, 2 cases: the key, the panel, contextual controls, the audio,
    recall, older sessions.
  - On demand: `[satellite-shots]` (869 / 1448).
  - The shell: its parity render masks the key's place (`arp-satellite`,
    `ArpInlinePanel::satelliteKeyArea`). Its schema check counts `layerX.satellite.*` as ANDOR
    OSP's own (a product with SATELLITE keeps its own namespace, as ANDOR ANALOG's
    `analog.satellite.*`). The Phase 2 tail test pins version 32.
- NEXT: ANDOR ANALOG can drop its own copy of the panel for the shared one (its
  `params::satelliteId` as the `idOf`).
- **A macro's own on / off lamp (shared, opt-in: `ProductInfo::macroSwitches`).**
  - `MacroSwitchLamp` (`MainSections.h`) is a tiny ring-and-dot button left of a macro's name, on
    its centre line. It is about 14 px drawn, with a 26 px target.
  - A click, Return or Space toggles it; each toggle is one host gesture and can be undone.
  - The tooltip is the product's.
  - While off, the macro's knob arc is dimmed; the value is untouched.
  - ANDOR ANALOG uses it for MORPH.
  - In ANDOR OSP the list is empty and the parity render never shows one: OSP is pixel-identical.
  - Evidence: ANDOR ANALOG's `[analog][morph][ui]` test and `docs/screenshots/morph-lamp`.

- **SHAPER DNA hooks (shared, opt-in; ANDOR ANALOG's SHAPER DNA V1).**
  - `MovementBus::setShaperHandoff` / `PostProcessor::setShaperHandoff`:
    - lane A's SHAPER hands its share of one gate to a product's per-note shaping over 40 ms;
    - `RhythmicShaper::process`'s `share` gives gain^share and share of the filter's octaves.
  - `InstrumentHost::shaperDnaParameterId / createShaperDnaPanel / shaperDnaNote`, through
    `ProductInfo::shaperDna`: the MOVEMENT rack's SHAPER shows a small DNA key beside its
    magnifier (lane A only).
  - `AnchoredPopoverHost`: the shell opens a product's panel anchored to a component, over a
    backdrop that swallows the closing click.
  - All of it is off by default and never in the parity render. ANDOR OSP keeps a single,
    whole-mix SHAPER, exactly as before.
  - ANDOR OSP has no per-note voice stage of this kind, so DNA is ANDOR ANALOG's alone. It is not
    a SPACE / ECHO / ARP feature, and nothing is planned for OSP's own engine yet.
  - Evidence: ANDOR ANALOG's `[shaperdna]` tests and `docs/SHAPER_DNA_V1.md`.

- **USER MACROS hooks (shared, opt-in; ANDOR ANALOG's USER MACROS V1.1).**
  - `ProductInfo::userMacros`: a product's six USER parameter IDs and its callbacks (names,
    rename, targets, eligibility, assign, clear, its panel, a revision counter).
  - `UserMacroBank.h`:
    - `MacroBankTab`: CORE and USER as two small folder tabs on the macro panel's top-left edge;
    - `MacroLens`: the drag handle on a USER knob's light;
    - `MacroAssignOverlay`: rings while assigning, arcs on mapped controls, the pick banner.
  - The shell:
    - a second, hidden macro row in CORE's places (USER 1..6 in visual order);
    - the bank shown is kept in `Interface.xml` (`MacroBank`), never in a preset;
    - pick mode and Escape;
    - "Assign to USER" through `ModulationOverlay::extendContextMenu`.
  - All of it is off unless a product asks, and never in the parity render. ANDOR OSP has no
    tab, no USER parameters and the same pixels.
  - Not a SPACE / ECHO / ARP feature. ANDOR OSP's macros stay its six.
  - Evidence: ANDOR ANALOG's `[usermacro]` tests and `docs/USER_MACROS_V1_1.md`.

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
