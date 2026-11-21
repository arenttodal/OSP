# Rack modulation

20 November 2026, branch `claude/macos-release`. The owner asked why MOD, the LFOs and the
other sources cannot be assigned to the controls inside the effect racks (for example the
FILTER rack's envelope DECAY), and what it takes to make every rack control modulatable.

## Why they were not

A control can be modulated only when three things exist for it:

1. **A destination in the engine** (`mod::Dest`, `src/engine/Modulation.h`), with its range,
   its owner (one value per voice, or one shared stage) and when it is read (all the time, at
   note-on, at note-off).
2. **A way to save a route to it.** A route's destination is a host parameter. The first one
   (`mod.routeN.dest`) is a frozen choice list: adding choices would move every existing
   choice's normalised value and so change DAW automation. Later destinations go into the
   second parameter, `mod.routeN.destExt`, whose choices are append-only (sessions store the
   index).
3. **The interface link** from the knob's parameter ID to the destination
   (`modui::destinationFor`), which gives the right-click menu, drag-and-drop and halos.

The FILTER rack's CUTOFF and RESONANCE had all three; MIN, MAX, DRIVE, the envelope and the
SYNTH page's controls had none.

## The plan (five steps)

| Step | What | State |
|---|---|---|
| 1 | Addressing and saving the new destinations | done: appended to `destExt` (choices 11-28), as VIBRATO and SYNTH were |
| 2 | Voice controls: FILTER rack MIN, MAX, DRIVE, ENV AMOUNT, ENV ATTACK, ENV DECAY; SYNTH DETUNE, TONE, IMPRINT, MOTION per layer | done |
| 3 | SPACE, ECHO and MOVEMENT rack parameters (shared stages: global sources only) | done: parameter routes |
| 4 | The FX library's effects | done: parameter routes (the same mechanism) |
| 5 | Interface pass (rack popovers, halos, menus) and ANDOR ANALOG checks | done in ANDOR OSP (halos on rack knobs fixed); ANDOR ANALOG: NEXT, below |

Step 1 first planned a parameter-ID address kept in the `ModLinks` tree. The codebase already
had a working, tested path for new destinations (`destExt`, append-only), so steps 1-2 use it:
no new persistence, no new abstraction. With 28 choices used, `destExt` stays a host
parameter. An address by parameter ID is the option if step 3-4's effect parameters outgrow a
choice list.

## How each destination acts (per voice)

| Destination | Range at 100 % | Read |
|---|---|---|
| CHARACTER MIN, MAX | the knob's 20 Hz .. 20 kHz (about 10 octaves); the end moves in octaves, clamped | control rate |
| CHARACTER DRIVE | 0 .. 100 % | control rate |
| CHARACTER ENV AMOUNT | -100 .. +100 % | control rate |
| CHARACTER ENV ATTACK | 1 ms .. 2 s (from 1 ms when the knob is at 0) | as the note starts |
| CHARACTER ENV DECAY | 20 ms .. 12 s | as the note starts |
| SYNTH x DETUNE | 0 .. 50 cents | control rate |
| SYNTH x TONE, IMPRINT, MOTION | 0 .. 100 % (IMPRINT and MOTION in Imprint mode only, as the knobs) | control rate |

The names are CHARACTER ... in the engine; ANDOR OSP shows them as FILTER ... (its
`characterName()`). With no route, every voice computes exactly what it did before (the
offsets are 0 and skipped).

## Steps 3-4: parameter routes

The racks and the FX library have far more controls than the destination list can hold (a
`mod::Dest` is one byte, and every entry would be one more host-parameter choice). They do not
need one each: ANDOR OSP already has a per-parameter offset layer, `LiveOffsets`
(`shared/andor-ui/Source/LiveParameters.h`). The USER MACROS write it every block, and every
control the engine reads as a `LiveParam` (the racks', the FX library's through its `Reader`)
reads its value through it. A route can write there too.

- **What a route stores:** `destExt` = PARAMETER (choice 29) and, in the session's `ModLinks`
  tree, `param<n>` = the control's parameter ID (undoable, saved, as the depth links are).
- **The engine:** route r's destination is `paramRoute0 + r` (16 entries, one per route), a
  shared stage (global sources only: no POLY LFO; the envelopes' global instances, VEL and
  NOTE of the latest key, the LFOs, the wheel, the CCs, PRESSURE). Its value per block is span
  1 x the sum of its sources, depth routes included: a share of the control's normalised
  travel. `InstrumentEngine::parameterModulation (slot)` gives it.
- **The processor** (`OspAudioProcessor::applyParameterRoutes`, right after the USER MACROS):
  each control's sum of routes is added to the macros' offset on it. A control no route moves
  any more goes back to the macros' offset alone; with no route nothing is written, so it plays
  exactly as before. The knob and its automation are never written. The value is the latest
  block's (one block late), which is deterministic for a given block size.
- **What can be moved:** what a USER macro can (`userMacroReads`: a float the engine reads
  every block; not switches, choices, routes, the seed or the grain budget). The instrument's
  own destinations come first: a control that has one (SPACE, the FILTER rack's ENV DECAY)
  keeps it.
- **Interface:** right-click on such a control lists the sources a shared stage takes;
  dragging a source onto it assigns; the halo is drawn in the knob's normalised travel; the
  route is named after the control (ECHO FEEDBACK).

## A fix in the interface (step 5)

ANDOR OSP's editor added the rack host after the modulation overlay, so the rack covered the
overlay: no halo ever showed on a rack's knob (the FILTER rack's included, after steps 1-2).
The rack is now inserted under the overlay, as the shell already did. A test checks the order.

## ANDOR ANALOG (NEXT)

The shared parts are in place: the destinations, the route links, the overlay, and the shell's
racks already lie under its overlay. What ANDOR ANALOG lacks is the processing step: its
product engine applies USER MACROS to its own raw values (`AnalogEngine::applyUserMacros`, in
the ANALOG repository), so it must add each parameter route's value there too. Until it does,
`ShellProcessor` keeps `parameterModulatable` false (nothing is offered that would not play).
Plan: (1) move ANALOG's OSP pin to this commit; (2) in `AnalogEngine`, after
`applyUserMacros`, add `parameterModulation (r)` to the raw value of each route's control
(resolved from `ModLinks.param<n>` on the message thread, as OSP's `refreshModLinks`); (3) the
shell's `parameterModulatable` answers from the product (its USER macro eligibility); (4) the
same `[rackmod]` checks on ANALOG's racks.

## A fix found on the way

A voice reused for a new note set its filter's first value (before its first control period)
with the previous note's route offsets: `clearModulation()` ran a few lines after the CHARACTER
start. It now runs first. The existing CUTOFF and RESONANCE routes had the same leak (the new
note's first few milliseconds of filter took the old offset), so a render could depend on the
note before it.

## Tests

`[rackmod]` (9 cases; `[parammod]` for steps 3-5) in `tests/plugin/RackModulationTests.inc`: the mapping and the appended choices
(earlier ones in place), each destination heard and gone again when its route is removed,
saved and reloaded, a poly LFO and the depth's sign, another layer's SYNTH route leaving
layer A alone, IMPRINT routes silent in Analog mode, and the right-click menu on the rack's
knobs. Steps 3-5: which controls take parameter routes, the rules (no POLY LFO), an ECHO and a
SPACE rack control and an FX library effect heard, routes adding up, saved, undone, removed
without a trace, the right-click menu, and the racks under the halos. Screenshots on demand:
`OSP_RACKMOD_SHOTS=<folder> osp_plugin_tests "[rackmod-shots]"`.
