# Final modulation system and key-tracked DAMPEN: design

This is the design for the queued specification. The Stage 0 audit is in
`mod-final-audit.md`. Each stage below lands as its own commit, with the suite green.

## Decisions (defaults taken from the specification)

**Tabs.** AMP | ENV | LFO 1 | LFO 2 | VEL | NOTE.
- ENV is today's ENV 1, with the same parameters (`mod.env1.*`).
- ENV 2 keeps working everywhere it is used (engine, routes, state). Only its tab goes.
- A route from ENV 2 is still listed, halo'd and editable, marked "ENV 2 (legacy)". The
  source menus offer ENV 2 only while some route uses it.
- `modTab` is migrated: ENV 2 opens ENV, LFO 1 and 2 shift by one.

**New sources.** These are appended to `mod::Source`, which sessions save by index:

| Source | Scope | Value |
|---|---|---|
| VEL | per note (voice); global destinations: the most recently pressed input note | the VEL curve of note-on velocity / 127 |
| NOTE | per note: the sounding note (arpeggiator octaves included); global destinations: the most recently pressed input note | the NOTE curve of the MIDI note (0..127; the editor shows C0..C8) |
| MIDI CC 1-4 | global | four CC slots, each with a controller number (default 2, 11, 16, 17; 0..119) and Learn, smoothed like the wheel (~10 ms) |
| PRESSURE | global | channel pressure; MPE members keep their own expression path |

**MOD WHEEL** stays a normal source (CC1). Host automation and MIDI CC stay separate paths.

**Pitch bend** does not move NOTE: NOTE follows the note-on pitch.

**Curves.**
- VEL and NOTE use `mod::Curve`, the same bounded, tension-shaped points as the LFO and
  envelope curves: no overshoot, and inverted or non-monotonic shapes are allowed on
  purpose.
- Presets: Linear, Soft, Hard, Exponential, Inverted, Flat; NOTE adds Low to High, High to
  Low, High notes only, Low notes only and Mid emphasis.
- The curves are stored in versioned state (`modCurves` "c2:" with 4 more curves; "c1"
  migrates). They are not host parameters.

**VEL and the amplitude.**
- The VEL curve replaces DYNAMICS' Soft / Linear / Hard choice for the amplitude only when
  **Apply to AMP** is on.
- Older sessions keep exactly their response: their curve is set to the gamma they had
  (0.6 / 1 / 1.7, sampled into points), and Apply to AMP is on.
- The `dynamics.curve` parameter stays for compatibility and sets the VEL curve when moved.
  No velocity is scaled twice.

**ARP destinations.** These are the controls that exist, with no new arpeggiator features:

| Destination | Kind | Behaviour |
|---|---|---|
| ARP RATE | timing | a speed multiplier on the step clock, 0.25×..4× at full depth (two octaves each way). The clock is a phase accumulator in beats: no restarts, phase kept; with the multiplier at 1 it is exactly today's grid. Option **Stepped**: the multiplier rounds to musical ratios (1/2, 2/3, 1, 3/2, 2, ...) and switches at the next step |
| ARP GATE | continuous | for newly generated notes |
| ARP SWING | continuous | clamped 0..100 %, never a step in the past |
| ARP OCTAVES | discrete | quantized with hysteresis; applies at the next cycle |
| ARP VELOCITY | new scalar | scales generated velocities. The only new arpeggiator control; the specification lists it, and it is pure scaling |

- Global sources drive the ARP destinations.
- VEL, NOTE, ENV and AMP use the input-note context (the most recent pressed key). The
  arpeggiator's own notes never feed back into its clock.

**DAMPEN (per layer, per voice).**
- Placement: after the CHARACTER filter, at both voice tails, before gain and pan. EQ stays
  on the layer bus. The editor sits next to EQ in the layer's EQ overlay: **EQ | DAMPEN**.
- Curve: `mod::Curve` over MIDI notes, giving the damping amount 0..1 (top of the graph = 0).
- Lookup: a 128-entry table per layer, rebuilt off the audio thread and published like the
  modulation curves.
- Modes:
  - **Soft Shelf** (default): a high shelf at 3 kHz (adjustable 1-8 kHz), down to -18 dB at
    full damping.
  - **Low-Pass**: 12 dB/oct, from 20 kHz down to the minimum cutoff (default 2 kHz) on a log
    scale.
- Mapping: amount = curve[note] × AMOUNT, then smoothed.
- AMOUNT A/B/C is a modulation destination (full range). So are shelf frequency and
  low-pass minimum cutoff.
- The sounding note includes the arpeggiator's octaves and transposition, not pitch bend.
- Bypass is the default, and sessions before DAMPEN open bypassed.

**Resampling audit.**
- The sinc reads are already band-limited (cutoff 0.96 / increment).
- Granular (Hermite) and Kaleidoscope grains are not band-limited. They are measured at +24
  and +36 semitones and fixed separately if aliasing is audible. DAMPEN does not disguise it.

**State.** Stage 8 is state version 12: the curves, Apply to AMP, the CC slots, ARP RATE
mode and DAMPEN settings. New parameters get their own parameter version hints. Old
sessions get safe defaults (DAMPEN bypassed, no new routes).

## Stage order

1. Source / destination metadata: append the sources, the destination classification and
   the CC slots; engine evaluation of VEL and NOTE per voice and globally.
2. Six tabs, ENV 2 as legacy, the VEL tab with curve, presets, marker and Apply to AMP.
3. The NOTE tab.
4. CC sources, MIDI Learn and PRESSURE.
5. ARP destinations: the phase-accumulator clock and the RATE multiplier, then GATE, SWING,
   OCTAVES and VELOCITY.
6. DAMPEN DSP and lookup; then the editor; then its destinations.
7. State 12 and migrations, the full tests, renders, CPU measurements and screenshots.

## Results (stages 1-7)

Stages 1-6 and the state of stage 7 landed together in one commit (the stages share their
files: the registry, the processor's state and the panel), with the suite green.

**Sources (1-4).**
- VEL, NOTE, CC 1-4 and PRESSURE are appended to `mod::Source`.
- VEL and NOTE are per note.
- The shared stages follow the player's latest key, also behind the arpeggiator.
- The VEL presets reproduce SOFT / LINEAR / HARD exactly, for all 127 velocities.
- Older sessions get their DYNAMICS curve as the VEL curve, with APPLY TO AMP on.
- CC slots follow controllers 2, 11, 16 and 17 by default. Any control's right-click menu
  has **MIDI Controllers**, where each slot can learn the next controller moved or take a
  controller from a list (any one by number). The assign menus show each slot's
  controller, for example "CC 1 (CC 2 Breath)".
- Tests: [mod-final] (unit and plugin).

**ARP destinations (5).**
- RATE, GATE, SWING, OCTAVES and VELOCITY are destinations.
- `arp.velocity` is the one new arpeggiator control: a knob in the ARP panel, 100 % by
  default.
- RATE runs a phase accumulator in steps while the multiplier is off 1x:
  - the phase is kept across speed changes;
  - free running, the plain clock continues at the same phase;
  - on the host's grid, it returns to the boundary nearest the step it expected, with no
    double and no drop;
  - Stepped (the RATE knob's right-click menu) keeps to 1/4, 1/3, 1/2, 2/3, 3/4, 1, 4/3,
    3/2, 2, 3 and 4, and changes at the next step;
  - at 1x, the timing is exactly the old grid. The existing arpeggiator tests are unchanged
    and pass.
- OCTAVES by modulation is quantised with hysteresis. A new count waits for the next cycle,
  which starts from the pattern's beginning.
- The OCTAVES keys take routes through their right-click menu.

**DAMPEN (6).**
- `engine/Dampen` runs per voice, after CHARACTER, at both voice tails.
- **Soft Shelf** (default) is a high shelf down to -18 dB at the shelf frequency (1-8 kHz,
  default 3 kHz).
- **Low-Pass** is 12 dB/oct, from 20 kHz down to MIN CUTOFF on a log scale (300 Hz - 12 kHz,
  default 2 kHz).
- The damping is curve[key] × AMOUNT, glided over about 10 ms. At 0 the filter is skipped.
- Off by default: renders are bit-identical with DAMPEN off, whatever its curve (tested).
- Destinations, per layer: AMOUNT, FREQ, MIN CUTOFF.
- Editor: the EQ overlay's second page (**EQ | DAMPEN**):
  - the curve over C0 … C8, no damping at the top;
  - the last played key is marked;
  - drag a point, double-click to add or remove one, drag a segment to bend it;
  - presets;
  - the mode, AMOUNT and the frequency or minimum cutoff.
- Edits are undoable.

**State (7).**
- State version 12. The curves are saved as "c3"; "c2" and "c1" still read.
- New properties: `arpRateStepped`, `velToAmp`, `modControllers`, `modTabs`.
- New parameters (hint 19): ARP VELOCITY and DAMPEN, with defaults that reproduce older
  sessions.

**Not done yet.**
These parts of stages 7-10 were not run, so no results are claimed:
- the resampling audit (Granular / Kaleidoscope aliasing at +24 / +36 semitones);
- CPU measurements of DAMPEN and the modulated arpeggiator;
- the listening scenarios A-J;
- new screenshots.

The destination and source choice parameters grow as before (append only). Saved sessions
keep their indices.
