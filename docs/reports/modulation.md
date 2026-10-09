# Modulation engine and modulation bay

The spec ("OSP — Modulation engine & expandable modulation bay", Parts 1–22) asks for two LFOs
and two modulation envelopes. They are dragged onto the instrument's own controls, routed by
up to 16 routes, and edited in a bay that widens the editor to the right. This report covers
the audit, the architecture, the decisions taken where the spec left room, what was tested
and what is still open.

The approved mockup for the bay was not attached to the request. The bay was designed from
the spec's text in the instrument's existing material language. Its proportions are a first
proposal, to be refined against the mockup when it arrives.

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
| GUI | One 1448 × 1086 reference canvas scaled as a whole; ARP grows the height | The bay adds width on the same canvas; the instrument's coordinates never change |
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

## The bay (GUI)

- **Window.** MOD widens the editor by `layout::modWidth` = 440 reference pixels, about
  330 px at the default size. The height stays. The instrument keeps its coordinates
  exactly: the UI test checks that the ARP card and the CHARACTER knob have not moved.
- **Housing.** The housing grows into one continuous body. A fine seam (a dark line, a lit
  line and a soft shadow) separates the expansion module. Its face is the panel colour
  shifted a little towards the housing.
- **Window shape for all four ARP/MOD combinations.** The scale stays the same. The ARP
  editor adds height, the bay adds width, and the resize limits and fixed aspect follow.
- **MOD control.** "Advanced" and "MOD ›" are half-width cards under the ARP card, at the
  same height and in the same style. MOD's chevron turns to "‹" while the bay is open. A small
  orange light shows that the patch has active modulation; it is not a switch.
- **Source tabs: LFO 1 sage, LFO 2 blue, ENV 1 amber, ENV 2 rust.** Each tab has:
  - a small live meter of the source;
  - a patch socket, the drag handle, which fills while the source is routed.
- **Selecting a tab.** A click selects the source. Selection never touches the audio.
- **The display.**
  - A graphite well with a fine grid and a soft fill under the curve.
  - The curve is computed by the engine's own functions (`lfoOutput`, `shapeSegment`,
    `curveAt`), so the picture is the sound.
  - The playhead comes from the audio thread: the global LFO, or the newest sounding voice
    for per-voice sources. A per-voice LFO with no note says "per note" instead of showing a
    misleading playhead.
  - The envelope's playhead follows its stage. Releasing a note moves it into the release.
- **Curve editing (CUSTOM LFO, one-shot ENV).** Drag a point; double-click to add or remove
  one; drag a line to bend it. RESET restores the shape. Up to 16 points.
- **Controls.** These are the instrument's own selectors, segmented switches and small knobs,
  in the source's colour:
  - LFO: SHAPE, MODE, HZ/SYNC, BIPOLAR/UNIPOLAR, GLOBAL/POLY, RATE (or DIVISION when synced),
    PHASE.
  - ENV: MODE, then ATTACK / DECAY / SUSTAIN / RELEASE / CURVE, or LENGTH.
- **Routing list.** One row per route showing source → destination, a bipolar depth bar,
  a bypass light and remove.
  - Clicking a row highlights its destination's knob in the instrument.
  - "+ ADD" offers SOURCE → DESTINATION menus, so a route can be made without dragging.
    Destinations the source cannot reach are disabled.
  - The list scrolls on its own; the curve editor keeps its size.
- **Drag and drop.** Pressing a tab and dragging it carries a cable:
  - Every control the source can reach shows a faint ring in the source's colour. The
    control under the pointer shows a strong ring.
  - Dropping makes the route at +50 %, selects it in the list and starts it at once.
  - Resting on CHARACTER for about half a second opens its popover, so the drag can continue
    onto RES.
  - Invalid targets (scope) take no drop.
- **Rings on modulated knobs.** The rings are drawn by an overlay above the instrument; the
  knobs themselves are not redrawn. Each ring is a thin arc just outside the knob's own value
  arc:
  - The arc spans the range the routes sweep, mapped in the destination's domain onto the
    knob's travel.
  - A small marker shows the effective value now.
  - It is in the source's colour, or a quiet neutral when several sources meet.
  - Bypassed routes leave a faint trace.
  - Dragging a ring vertically edits the depth of the route it shows, preferring the source
    selected in the bay. A right-click lists the routes to remove.
  - Only the ring areas are repainted at 30 Hz.

## Tests

| Test | What it covers |
|---|---|
| `[unit][mod]` (7 cases) | Shapes and polarity. Phase without drift and independent of block size (1–4096). Sync follows the song position. ONE SHOT holds. ADSR stage timing, release from the current level, one-shot curve. Scope, sign, sum, bypass and empty rules. No route is bit-identical to no modulation. Routes are heard, deterministic and block-size independent. Per-voice envelopes and poly LFOs are independent per note. |
| `[plugin][mod]` (2 cases) | Parameters and IDs; routes add, reuse and remove. Recall through the saved state (sessions and presets share it), older sessions with no routes, curves, undo. Every source with the ARP, Granular and three layers: heard, finite, every note ends. ENV 1 retriggers on each ARP note. |
| `[ui][mod-ui]` (CI, xvfb) | The window widens by the bay and the instrument does not move. ARP and MOD open together without overlap. Drag onto CHARACTER makes a global route, or a per-voice cutoff route for ENV 1. ENV 1 onto LIFE is refused. Hover opens CHARACTER's popover and RES takes the drop. Routes onto PAN, REIMAGINED and RELEASE. Rings, MOD's light, the routing list and the + ADD menu. Recall of the bay and routes. Screenshots `mod-01` … `mod-11` (the spec's list in 18.6). |
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
| Standalone (Linux, xvfb) | Yes: window widening and narrowing, ARP + MOD combinations, drag and drop through the test harness |
| JUCE AudioPluginHost, REAPER, Ableton Live, Logic Pro | **Not yet.** No Mac or DAW is available in this environment. The resize path uses JUCE's standard constrainer (as the ARP editor does). |

The spec's fallback for hosts that refuse the wider window (17.4) is partly in place. The
instrument scales into the window it gets: when a host refuses the resize, it is shown whole
(smaller) with the bay beside it. It is never clipped and never squeezed.

## Open decisions and limitations

1. **The mockup.** The bay's width (440 reference px), proportions and colours are a proposal
   from the text. Refine them against the approved image when it is available.
2. **Global envelope mode.** It is not implemented, so ENV cannot reach the macros. The spec
   asks for an explicit gate-aggregation rule first, for example "first note on, last note
   off".
3. **CHARACTER's ring for cutoff routes.** It is an approximation: four octaves drawn over
   the macro's travel. The audio is exact. Only the drawing approximates.
4. **Depth on the ring.** Depth is changed by dragging the ring vertically; there is no
   separate depth handle.
5. **Host validation.** Logic, Ableton, REAPER and AudioPluginHost resizing and automation
   are still to be checked on a Mac or PC.
