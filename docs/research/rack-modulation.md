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
| 3 | SPACE, ECHO and MOVEMENT rack parameters (shared stages: global sources only) | not started |
| 4 | The FX library's effects | not started |
| 5 | Interface pass (rack popovers, halos, menus) and ANDOR ANALOG checks | not started |

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

## A fix found on the way

A voice reused for a new note set its filter's first value (before its first control period)
with the previous note's route offsets: `clearModulation()` ran a few lines after the CHARACTER
start. It now runs first. The existing CUTOFF and RESONANCE routes had the same leak (the new
note's first few milliseconds of filter took the old offset), so a render could depend on the
note before it.

## Tests

`[rackmod]` in `tests/plugin/RackModulationTests.inc`: the mapping and the appended choices
(earlier ones in place), each destination heard and gone again when its route is removed,
saved and reloaded, a poly LFO and the depth's sign, another layer's SYNTH route leaving
layer A alone, IMPRINT routes silent in Analog mode, and the right-click menu on the rack's
knobs.
