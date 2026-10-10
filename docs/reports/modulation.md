# Modulation engine, panel and halos

The first spec ("OSP — Modulation engine & expandable modulation bay", Parts 1–22) asks for two
LFOs and two modulation envelopes. They are dragged onto the instrument's own controls and
routed by up to 16 routes. The second spec ("OSP — Modulation UI refinement", Parts 0–22)
replaced the first version's bay, which widened the editor. Now the AMP envelope's panel is a
shared editor with five tabs, and modulated knobs carry a two-layer halo. This report covers
the audit, the architecture, the decisions taken where the specs left room, what was tested
and what is still open.

Neither spec's mockup was attached. The panel and the halo were designed from the text, in the
instrument's existing material language.

## Stage 0: audit (what existed)

| Area | State before | Bearing on modulation |
|---|---|---|
| Framework | JUCE 8.0.9, CMake, C++20; pure C++ engine library (`osp_dsp`) | Sources, routing and registry live in `src/engine/Modulation` (no JUCE) |
| Voices | `InstrumentVoice`, 24 voices, control rate 32 samples (`controlInterval`), smoothed gain | Per-voice destinations are read at that control rate |
| Layers | 3 slots, One Shot or Granular per layer; per-layer REIMAGINED (modes per layer) | Layer destinations ×3; REIMAGINED is a per-layer amount with shared stages |
| Macros | LIFE, DRIVE, CHARACTER, MOVEMENT, SPACE (+ ECHO) in a post chain shared by all voices; CHARACTER is also the per-voice filter position | Macros are *global* destinations; CHARACTER's per-voice filter is the separate *cutoff* destination |
| Amp ADSR | Per voice, prepared at note-on | Attack and decay at note-on, release at note-off, sustain continuous |
| ARP | Note-event stage in front of the engine | Its notes are ordinary note events, so per-voice envelopes see every generated note and its gate |
| Parameters | APVTS with version hints; undo manager; presets = state tree | Routes as 16 fixed parameter slots (automatable, undoable, saved) |
| GUI | One 1448 × 1086 reference canvas scaled as a whole; ARP grows the height | Modulation lives in the AMP envelope's panel; the window keeps its size |
| Tests | Catch2 unit / integration / regression, plugin tests, xvfb UI tests in CI | Added `[unit][mod]`, `[plugin][mod]`, `[ui][mod-ui]`, `[.][mod-cpu]` |

Nothing in the spec had been implemented before. No existing DSP was replaced.

## Architecture

```
parameters (APVTS, mod.*)  ──applyModulation()──▶  mod::Settings (plain value, no allocation)
custom curves (state tree) ──publish (atomics)───▶  compiled 256-point tables
                                                     │
InstrumentEngine::setModulation ─────────────────────┤ Runtime: compiled routes per destination,
                                                     │          global LFO states, host timing
render(): 32-sample chunks while a route is active ──┤
   applyGlobalModulation: global LFOs → macros, REIMAGINED amounts, LIFE  (effective values only)
   voices: VoiceState (poly LFOs + both envelopes) → level, pan, fine tune, cutoff, resonance,
           granular POS / SIZE / DENSITY / SPREAD, REIMAGINED (own part), amp ADSR
```

- **Base values are never written.** The engine keeps the stored macro values and gives
  the post chain `base + modulation`. Voices add their offsets where each setting is used.
  Host automation, presets and undo see only the stored parameters.
- **No route is no modulation.** Without an active route, `render()` takes the exact
  pre-modulation path: one block, unchanged arithmetic. Only the global LFOs keep time, for
  the display. Sessions from before render bit-identically (`[unit][mod]` engine test; the
  13-scene ARP baseline and the 9-scene DRIVE baseline still null).
- **Real-time safety.** `Settings` is a fixed-size value. Routes are compiled into
  per-destination term arrays. Curves are compiled outside the callback and published through
  atomics with a generation counter. The audio thread has no strings, allocation or locks.
- **Determinism.** The smooth-random shape draws one value per cycle from the variation seed
  and the cycle index, through `osp::Prng`. A host-synced LFO takes its phase from the song
  position, so a bounce repeats exactly.
- **Acyclic.** Sources are never destinations.

### Sources

| | Behaviour |
|---|---|
| LFO 1, LFO 2 | SINE, TRIANGLE, RAMP UP, RAMP DOWN, PULSE, RANDOM (smooth), CUSTOM (a breakpoint curve). Free 0.01–30 Hz, or synced: 1/32 … 8 bars, with dotted and triplet. PHASE 0–360°. BIPOLAR (−1..1) or UNIPOLAR (0..1). FREE / RETRIGGER / ONE SHOT. GLOBAL (one phase) or POLY (one per voice). Global and poly use the same oscillator code; only the state's owner differs. |
| ENV 1, ENV 2 | Per voice: ADSR with a CURVE (−100 % fast start … +100 % slow start), or a ONE SHOT breakpoint curve over LENGTH. Note-off moves an ADSR into release from its current level. A one-shot curve ignores note-off: it is a shape in time. |

**Policies the spec left open:**

- **ONE SHOT (LFO).** It runs one cycle from PHASE and then *holds its last value*.
- **Global RETRIGGER and ONE SHOT.** These restart when a note starts after silence (the first
  held note), not on every note. A free global LFO is never reset by notes, so ARP steps do
  not reset it (spec 14.3).
- **Per-voice ENVs only reach per-voice destinations.** The spec (7.5) asks for a defined
  aggregation rule before a per-voice envelope may drive a shared stage. None is defined yet,
  so the scope rule refuses that route:
  - A per-voice source (ENV, or an LFO set to POLY) cannot reach the macros.
  - Dropping a per-voice source onto CHARACTER makes a route to the per-voice cutoff instead.
  - A route made invalid later (an LFO switched to POLY) stays listed, marked, and silent.

### Destination registry

`mod::destInfo()` defines each destination's stable ID, display name, owner (global or voice),
layer, domain, span (what 100 % depth moves) and update policy. The enum is append-only
because sessions store it as an index.

| Destinations | Owner | Domain / span at 100 % | Update |
|---|---|---|---|
| LIFE, DRIVE, CHARACTER, MOVEMENT, SPACE | global | amount, full travel | continuous (32 samples) |
| LEVEL A/B/C | voice | ±24 dB | continuous (gain ramped over the interval) |
| PAN A/B/C | voice | full pan | continuous |
| REIMAGINED A/B/C | voice (+ the layer's shared stages hear global LFOs) | full amount | continuous |
| FINE TUNE A/B/C | voice | ±100 cents | continuous |
| POSITION A/B/C (granular) | voice | ±50 % of the source | continuous; grains read inside the source, reversed sources mirror |
| DENSITY, GRAIN SIZE A/B/C | voice | ±2 octaves | continuous |
| SPREAD A/B/C | voice | full | continuous |
| CHARACTER CUTOFF | voice | ±4 octaves | continuous |
| CHARACTER RESONANCE | voice | full | continuous |
| AMP ATTACK, DECAY | voice | ±4 octaves (time ratio) | at note-on |
| AMP SUSTAIN | voice | full | continuous |
| AMP RELEASE | voice | ±4 octaves | at note-off |
| EQ A/B/C BELL FREQ | global (the layer's EQ, after its voices are summed) | ±2 octaves | continuous |
| EQ A/B/C BELL GAIN, LOW SHELF, HIGH SHELF | global (the layer's EQ) | ±12 dB | continuous |

The spec's exclusions (8.6) are not destinations: sample selection, presets, ARP pattern and
mode, REIMAGINED mode, mono/poly, quality settings and routing topology.

### Routes

There are 16 fixed slots, `mod.routeN.{source,dest,depth,enabled}`, introduced in parameter
version 17. Each slot is a host parameter: automatable, saved in presets and sessions, and
undoable.

- Several routes may share a destination. Their contributions add in the destination's
  domain, and the result is clamped where it is used.
- Depth is signed: a negative depth turns the movement around.
- Adding a route uses the first free slot, or the slot already joining the same source and
  destination.
- The routing list and the rings on the knobs edit the same depth parameter. No depth is
  duplicated.
- Custom curves are saved in the state tree (`modCurves`, `"c1:"` text, four curves). Curve
  edits are one undo step each.

## The modulation panel and halos (GUI)

The first version's bay (a module that widened the window to the right) is gone. The plugin
keeps its original size, 1086 × 814 by default, and modulation lives where the AMP envelope
was.

### One panel, five tabs

- **Tabs.** The AMP envelope's panel (`ModulationPanel`) carries a row of small tabs: AMP,
  ENV 1, ENV 2, LFO 1, LFO 2.
  - Exactly one tab is shown at a time. AMP is the old envelope panel, unchanged.
  - The tabs are text only: 13 px medium type, with a 1.6 px underline in the source's colour
    under the selected tab.
  - A tiny dot after a source's name means it has active routes.
  - Switching tabs only changes the view, never the sound. An LFO's phase is untouched (tested).
  - The open tab is saved with the editor state (`modTab`), not as a parameter.
- **Tabs are the drag handles.** Press a source's tab and move it more than 6 px to carry a
  cable.
  - Every control the source can reach shows a faint ring; the control under the pointer
    shows a strong one.
  - Dropping makes a +50 % route and selects it.
  - Resting on CHARACTER opens its popover, so the drag can continue onto RES.
  - A click shorter than the threshold only selects the tab.
- **Compact editors.** Each source page is one row in the AMP envelope's knob row, under the
  same graph well, so every feature is still there.
  - LFO: SHAPE, MODE, RATE, PHASE, VOICES (Global / Poly).
  - ENV: MODE, then ATTACK / DECAY / SUSTAIN / RELEASE / CURVE, or LENGTH in one-shot mode.
- **RATE.** It is one knob. Clicking its caption (RATE ⌄) opens a small menu with Sync and Hz,
  which sets `lfoN.sync` as one undo step. The same knob then shows a division (1/8, 1/4 D, …)
  or a frequency in Hz.
- **Polarity.** It is in a context menu, shown in the graph's corner as "± BIPOLAR" or
  "+ UNIPOLAR". Clicking that tag, right-clicking the graph or right-clicking the source's tab
  opens the menu: POLARITY Bipolar / Unipolar, Edit shape…, Reset, Routes….
- **Routes.** The top right of the panel reads "+ ROUTE" or "N ROUTES" and opens a compact
  routes popover (`ModRoutesPopup`).
  - One row per route of the source: → DESTINATION, a depth bar (drag; double-click for 0), a
    bypass light and remove.
  - Clicking a row selects that route.
  - "+ ADD" offers grouped menus: Macros, Character, Amp envelope, Layer A, Layer B, Layer C.
    Destinations the source cannot reach are disabled.
- **Expanded curve editor.** The corner mark beside the routes key opens a larger floating
  editor (`ModCurvePopup`, 640 × 330 reference px). It holds the full curve view: drag points,
  double-click to add or remove, drag a line to bend, RESET. It closes like every popover, and
  the compact graph stays editable too.
- **MOD button.** MOD under the ARP card now toggles the panel between AMP and the last
  source tab shown. Its small light still means that the patch has active modulation.
- **One selection state.** The editor holds the selected source and route (`modSelSource`,
  `modSelSlot`). The tabs, the routes popover, the halos and new drops all read and set it.

### Two-layer halo (`ModulationOverlay`)

- **Geometry.** The halo is a separate arc outside the knob's own outermost mark.
  - That mark is the tick ring on the macros and card knobs, or the value arc on popover
    knobs.
  - The gap is 5–7 px on ticked knobs, 6 px on popover knobs and 4.5 px on mini knobs.
  - A slider property `haloGap` overrides the gap: the card knobs use 2 px, because their
    caption sits close above.
  - A property `haloAvoid` names a caption rectangle that the ring breaks around, so a label
    is never crossed at any scale.
- **States.**
  - Idle: a 4.5 px arc for the range the route sweeps, and a marker for the value now.
  - Hover: a faint full guide track behind the arc, a 5.5 px arc and an endpoint handle where
    the source at its top takes the value.
  - Editing: a 6.5 px arc and a larger handle.
  - The hit band runs from 5 px inside the ring to 9 px outside, so a thin ring is still easy
    to grab.
- **Depth editing.** Dragging the halo up or down edits that route's depth, with a fine factor
  of 0.5, or 0.15 with Shift.
  - It never touches the knob's base value (tested).
  - It passes smoothly through zero. Negative depth turns the arc to the other side of the
    base.
  - Each drag is one host gesture and one undo step.
  - Option-drag on the knob body edits the depth too. Plain drags, double-clicks and
    right-clicks on the knob keep their usual meaning. Option-drag on TUNE is excluded,
    because TUNE uses Option for fine steps.
- **Context menu** on a halo: Select, Bypass / Enable, Depth to 0, Remove, "<SOURCE> routes…".
- **Several sources on one control.** The emphasised route gets the main arc. That is the
  selected route if it is here, else the selected source's route, else the first. Other routes
  are thin 1.6 px arcs 6.5 px further out, at 45 % opacity, in their own colours. A bypassed
  route leaves a faint tick.
- **Tooltip.** On hover a dark card beside the ring shows, from the real mapping and clamped to
  the parameter's range:
  - the route: LFO 1 → CHARACTER;
  - Depth: +50 %;
  - Base, and Range lo – hi;
  - "+ n other routes" when there are more.
- **Macro labels** moved up about 10 logical px (they are centred 13 reference px higher), so
  they sit clear above the halo. The macro panel grew 20 reference px upward to keep its
  title clear. The UI test checks that each label's bottom is above the halo's top.
- **EQ fields** (bell frequency and gain, shelf gains) show their range as an underline
  instead of a ring.
- **Repainting.** Only the ring areas are repainted at 30 Hz. The overlay is transparent to
  the mouse outside the halo bands.

## ANDOR/OSP refinement: modulation everywhere

The third spec ("ANDOR/OSP — final modulation & GUI refinement") asked for modulation to reach
every eligible control and to be assignable without dragging. It also asked for four defects
to be fixed. The five reference screenshots it mentions were not attached; the work follows
the text and the plugin as it is.

### Root causes

| Defect | Cause | Fix |
|---|---|---|
| Global LFO 1 sometimes standing still | `LfoState::finished` (ONE SHOT done) survived a change of mode or a preset load; `advance()` returned early forever, and a global LFO in FREE mode is never restarted. A second, rarer cause: the held-note count went up on a repeated note-on without its note-off, so RETRIGGER / ONE SHOT never saw "silence" again. | `finished` holds only while the mode is ONE SHOT; keys are tracked per note and channel (a bit each), so a key pressed twice counts once. Regression test: `[unit][mod][regression]` (after ONE SHOT, no voices, 20 transport changes with and without a position, repeated keys). |
| ENV 1/2 could not reach the macros | The scope rule refused every per-voice source on a shared stage, and an envelope only existed per voice. | A global envelope per ENV (below). |
| Dropping on the Granulator did nothing | Its POS / SIZE / DENS / TUNE / SPREAD appear on a mouse-move over the waveform. During a drag the mouse belongs to the tab, so the card never saw it, the knobs stayed hidden, and hidden knobs are not drop targets. | Resting a drag on a Granular layer's display for 380 ms shows them at once, without the fade. The drag carries on and the drop lands on the knob. |
| Halos crossing the macro names | The names were placed against the ring itself, but the halo also draws other routes' arcs (+6.5 px), the selection glow (9 px wide) and the handle (7.5 px). Those reached about 1 px past the names. | The names moved up 6 reference px and the knobs (with their lights) down 6, so there are about 10 px clear of everything the halo draws. The UI test checks it for all six macros with two routes on DRIVE. |

### Decisions

- **Global envelopes (ENV on shared stages).** Each ENV also runs one instance for the whole
  instrument (`mod::Runtime::globalEnv`). It is used only by the shared stages: the macros
  (LIFE, DRIVE, CHARACTER, MOVEMENT, SPACE, ECHO) and each layer's EQ.
  - Every note-on restarts it from its current level (no jump): multi-trigger, as a mono
    synth's envelope, so each ARP note pumps DRIVE.
  - It releases when the last key is up and the sustain pedal is not down.
  - This was chosen over aggregating the voices' envelopes because it is deterministic, needs
    no rule for overlapping notes, and does not depend on which voice was processed last.
  - Voices never see it: their own envelopes stay per note.
  - REIMAGINED's layer amount (a voice destination with a shared stage) keeps taking only the
    single-valued sources, so nothing is counted twice.
- **START (A/B/C, new destinations).** It is read once, as a voice starts: its sources are
  evaluated at that instant (a probe `VoiceState`). A free-running global LFO therefore gives
  each note a different start, and a sounding note never jumps.
  - Reverse reads it from the sound's end, as the knob does, and it is clamped to the
    recording.
  - Granular voices carry the same note-on offset on their grain position, while POS keeps
    its own continuous modulation.
  - An envelope may not choose START, because an ADSR is at 0 when a note begins. The route is
    refused rather than accepted and silent.
  - A POLY LFO in RETRIGGER mode gives a constant offset (its PHASE). That is allowed and
    documented, and no randomness is added.
- **Envelope times as destinations (ENV 1/2 ATTACK, DECAY, SUSTAIN, RELEASE).** The LFOs and
  the wheel may move them; the envelopes may not.
  - ATTACK and DECAY are taken when the envelope starts, and RELEASE when it is released.
    SUSTAIN follows continuously. A running stage is never retimed and nothing restarts.
  - Without a route the envelopes follow their knobs live, exactly as before.
- **No loops.** The only destinations owned by a source are the envelopes' own settings
  (`DestInfo::modulates`), and only the LFOs and the wheel reach them. Nothing modulates an
  LFO or the wheel, so the graph is acyclic by construction. `mod::createsCycle` checks it all
  the same in the assignment path, and evaluation order stays fixed: LFOs and wheel, then
  envelope times, then destinations.
- **MOD WHEEL (a new source, appended).** It comes from MIDI CC 1 or the on-screen wheel (both
  arrive as CC 1), as 0..1 smoothed over about 10 ms.
  - It is global, so it reaches every destination, START and the envelope times included.
  - The wheel's built-in "opens MOVEMENT up" stays for sessions that do not route it. Once
    any route uses the wheel, that built-in mapping steps aside, so the wheel does only what
    its routes say.
- **One assignment path.** `OspAudioProcessor::assignModulation` is used by drag-and-drop,
  the right-click menu and the overlay. It checks:
  - the rules: scope, START timing and envelope-time sources;
  - loops;
  - an existing route for the same pair, which is selected with its depth kept, never
    duplicated;
  - capacity, which is 16 routes; when full it says so.

  When it refuses, it gives the reason, and the overlay shows it as a short note.
- **Right-click everywhere.** The overlay takes a right-click (or control-click) on any
  registered destination, inside popovers too, because it sits above them. The menu offers the
  knob's routes and "Assign <source>" for every allowed source, with "(each note's filter)" /
  "(whole instrument)" on CHARACTER for an envelope. "Remove All" is one undo step. The
  destinations are the registry's, so a control is listed whether or not it is on screen.
- **The tabs' sockets.** A small patch socket (ring and centre dot, the bay's old assignment
  glyph) follows each source's name; its centre fills while the source is routed.
  - The tab gap grows to 22 px, shrinking only if the routes key needs the room.
  - Dragging starts after 2 px from the socket and 6 px from the word.
  - AMP has no socket, because it is not a source.
- **ECHO** joined the macro destinations (appended).

### Parameters and compatibility

- `mod.routeN.source` gains MOD WHEEL (index 5) and `mod.routeN.dest` gains ECHO, START A/B/C
  and ENV 1/2 ATTACK / DECAY / SUSTAIN / RELEASE. Both are appended, so every saved route keeps
  its meaning.
- No ID, range or default changed, and no audio reference was regenerated. With no route,
  the engine is bit-identical (`[unit][mod]` null test, ARP / DRIVE baselines).
- As with any appended choice, a DAW automation lane recorded on a route's source or
  destination choice in an older build would land on other entries. Sessions and presets
  store the index and are unaffected.

## Tests

| Test | What it covers |
|---|---|
| `[unit][mod]` (7 cases) | Shapes and polarity. Phase without drift and independent of block size (1–4096). Sync follows the song position. ONE SHOT holds. ADSR stage timing, release from the current level, one-shot curve. Scope, sign, sum, bypass and empty rules. No route is bit-identical to no modulation. Routes are heard, deterministic and block-size independent. Per-voice envelopes and poly LFOs are independent per note. |
| `[plugin][mod]` (2 cases) | Parameters and IDs; routes add, reuse and remove. Recall through the saved state (sessions and presets share it), older sessions with no routes, curves, undo. Every source with the ARP, Granular and three layers: heard, finite, every note ends. ENV 1 retriggers on each ARP note. |
| `[ui][mod-ui]` (xvfb) | The window keeps 1086 px, and the panel sits inside the macro section. Switching tabs leaves the LFO's phase untouched. The RATE menu switches Sync / Hz. A tab drag needs the threshold and drops onto CHARACTER. The polarity menu works. The halo clears the knob's reach, and the labels sit above the halos. The hover readout appears. Drag +70 then −40 changes depth through zero while the base stays put. ENV 1 → cutoff; per-route editing with several sources. The routes popover has its rows and six add groups; the curve popover opens. Rapid tab switching, the MOD button, ARP with the panel, the smallest and largest scales, and recall of the tab. Screenshots `modui-01` … `modui-15`. |
| `[unit][mod]` (refinement, 5 cases) | The global LFO after ONE SHOT, with no voices, across transport changes and repeated keys; the global envelope (restart, hold with a key or the pedal, release); MOD WHEEL (glide, -12 dB at full wheel on LEVEL, no change wheel-down); START (= the equivalent knob setting, forwards and reversed, no jump when the wheel moves mid-note, bounded); envelope times (taken at note-on, sustain followed, envelopes refused). |
| `[ui][andor-ui]` (xvfb) | Tab sockets and spacing; ENV 1 dropped on DRIVE; halos on all six macros clear of their names; right-click: hit test, Assign / Edit / Remove, no duplicates, refused sources absent; the wheel dragged onto SPACE, then moved on screen and by CC 1; LFO 1 on START A and on ENV 1 ATTACK; Granular hover-to-open (not on a pass), drop on POS; the source card; ADVANCED full width; the MIX triangle hidden with three layers, its values kept; recall. Screenshots `andor-01` ... `andor-12`. |
| `[.][mod-cpu]` | Stress measurement (below). |

## CPU

The stress case uses:

- three layers, A in Granular, REIMAGINED at 60 % on every layer;
- SPACE and MOVEMENT on, the ARP at 1/8 over two octaves, an 8-note chord;
- all four sources feeding all 16 routes, with LFO 2 per voice.

It compares this with the same patch with no route, at 48 kHz in 256-sample blocks, on the
Linux build machine.

| | per 256-sample block | share of real time |
|---|---|---|
| no route | 2047 µs | 38.4 % |
| 16 routes, 4 sources | 2294 µs | 43.0 % |

The modulation stage at full load costs about 4.6 % of real time on top of the stress patch,
about 12 % relative. This was measured on a shared, loaded build machine while other test
suites ran, so treat the absolute numbers as pessimistic and the relative cost as the useful
figure. Most of it is the 32-sample control-rate chunking of the shared stages. With no
active route that path is not taken at all. The audio thread does no allocation, takes no
locks and compares no strings.

## Host testing

| Host | Tested |
|---|---|
| Standalone (Linux, xvfb) | Yes: tabs, drag and drop, halos and popovers through the test harness, at the smallest and largest scales |
| JUCE AudioPluginHost, REAPER, Ableton Live, Logic Pro | **Not yet.** No Mac or DAW is available in this environment. The resize path uses JUCE's standard constrainer (as the ARP editor does). |

The window no longer changes width for modulation, so hosts that refuse a resize are no longer
a concern for MOD. Only the ARP editor still grows the height, as before.

## Open decisions and limitations

1. **The mockup.** Neither spec's mockup was attached. The tab type, the halo's gap and widths,
   and the readout card are a proposal from the text, so refine them against the image.
2. **Global envelope policy.** ENV reaches the macros through one global instance per
   envelope: every note-on restarts it and the last key (with the pedal up) releases it. It
   is a deliberate choice, and a per-voice aggregation could be added as an option if
   listening asks for it.
3. **Not done in this round (refinement).** No DAW was used, so automation lanes, project
   recall in hosts and real controller wheels are untested; CC 1 is tested headless (the
   source and the on-screen wheel follow it). The A/B/C source card opens on hover and click,
   not from keyboard focus. Meta-modulation (a source on a route's depth) is queued as the
   next piece of work.
4. **CHARACTER's ring for cutoff routes.** It is an approximation: four octaves drawn over
   the macro's travel. The audio is exact. Only the drawing approximates.
5. **Depth on the halo.** Depth is changed by dragging the halo, or the knob with Option,
   vertically. The endpoint handle shows where depth leads but is not dragged on its own
   path.
6. **Host validation.** Logic, Ableton, REAPER and AudioPluginHost resizing and automation
   are still to be checked on a Mac or PC.
