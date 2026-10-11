# Final modulation system and DAMPEN: Stage 0 audit

This is the audit for the "final modulation system and key-tracked DAMPEN" specification:
six tabs, VEL and NOTE curves, MIDI CC sources, ARP destinations and per-source DAMPEN. It
records what exists before any of that work starts. Line numbers are as of commit
`knob-phase1` + full-range modulation (state version 11).

Abbreviations:
- PP: `apps/plugin/Source/PluginProcessor.cpp`
- IE: `src/engine/InstrumentEngine.cpp`
- IV: `src/engine/InstrumentVoice.cpp`

## Sources

**The enum.** `mod::Source` is `none, lfo1, lfo2, env1, env2, modWheel` (`Modulation.h:49`).
- Each route's `mod.route{N}.source` choice persists this order, so new sources (VEL, NOTE,
  MIDI CC) can only be appended.

**Places that assume five sources or this index layout:**
- `Modulation.cpp`: `isPolySource`, and the global value slots, where index 4 is the wheel
  and 2–3 are the envelopes.
- IE `publishModulation`.
- `ModulationPanel.cpp`: `sourceLabels`, `sourceColour` and `sourceOfTab`.
- `EqEditor.cpp:260`.
- The `sourceCount` clamps at PP:1136 and PP:1586.
- `src/research/RenderConfig.cpp`.

**Tabs today.** AMP | ENV 1 | ENV 2 | LFO 1 | LFO 2 (`ModulationPanel.cpp:901`,
`modui::tabCount = 5`). `modTab` is UI-only state, clamped to 0..4.

**ENV 1 and ENV 2 run both per voice and globally:**
- Per voice: `mod::VoiceState` holds its own envelopes, with times taken at note-on.
- Globally: `Runtime::globalEnv` drives the shared stages.
- Parameters: `mod.env{1,2}.{mode, attack, decay, sustain, release, curve, length}`.
- **ENV 2 is a full, independent source.** Sessions may route it, and its parameters are host
  parameters. Consolidating to one user-facing ENV therefore needs ENV 2 kept as a hidden
  legacy source: it stays in the enum, the engine and the state. Only its tab goes, and its
  routes are shown and editable from the route list.

**MOD WHEEL already exists** as a source:
- CC1 → `engine.setModWheel`, then 10 ms smoothing.
- The screen wheel is injected as CC1.
- It has no tab, which matches the specification.

## Destinations and routing

**Registry.** `mod::Dest` is append-only. The registry gives owner, domain, span, update
policy and, for times, limits.
- Full range since state 11: 100 % = the knob's whole range.
- `compatible()` holds the scope rules; `createsCycle()` the loop check through envelope-time
  destinations.

**Meta-modulation.** Depth routes are `Dest::routeDepth` with `Route.target`. In the plugin
they are linked by stable route uids in a `ModLinks` tree.

**ARP destinations.** None exist yet.

## Velocity

**The existing curve is in the Advanced popup, under DYNAMICS:**
- `dynamics.curve` chooses Soft / Linear / Hard, gamma 0.6 / 1 / 1.7 (`shaping::curvedVelocity`).
- `velocityRange` is 0..48 dB.
- The `dynamics` macro scales the level range and the timbre.
- The curve is applied in `InstrumentEngine::noteOn` to the amplitude
  (`shape.gain = dB(-range*(1-v/127))`).
- Velocity is not given to the modulation runtime: no source carries it.

**Plan.** A VEL source with a drawable curve. Moving the curve into the VEL tab must keep
DYNAMICS' amplitude response exactly ("Apply to AMP"), with no double scaling.

## NOTE

**Today.** No key-tracking source exists.
- The voice knows its sounding note.
- The arpeggiator's generated notes reach the engine as ordinary notes.

## MIDI

`handleMidi` (PP:1917) handles:
- note on/off;
- channel and poly pressure, collapsed to the channel;
- CC74 timbre;
- CC1 for the wheel;
- CC20–27 for the macros (the comment says 20–25);
- MPE pitch bend (lower zone);
- pitch bend;
- CC64 sustain;
- All Notes Off.

**Not handled.** Every other CC is ignored, and there is no MIDI learn.

## Arpeggiator

**Files.** `src/engine/Arpeggiator.{h,cpp}`.

**Parameters:**
- `arp.enabled`
- `arp.pattern` (18 patterns)
- `arp.rate` (a choice of 10 divisions, 1/4..1/16T; no free Hz rate)
- `arp.gate` (10..150 %)
- `arp.octaves` (1..4)
- `arp.swing`

There are no step count, direction or velocity parameters: each step uses the held note's
velocity.

**Clock:**
- PPQ-synced while the host plays; otherwise free-running at the last BPM.
- Loops and locates resync; rate changes continue on the new grid.
- Note-offs come from `offTime[128]` per pitch, emitted before note-ons at the same sample.
- The pedal is consumed while the arp is on.

**Implications for the rate destination:**
- Continuous rate modulation needs a rate multiplier on the step clock. A phase accumulator
  in beats would keep PPQ alignment when the multiplier is 1.
- Stepped divisions switch at step boundaries.
- Only Rate, Gate, Swing and Octaves exist to be modulated. Steps, direction and probability
  would be new functionality, which the specification says not to invent.

## Signal path (DAMPEN)

**The per-voice chain (IV:959–1188):**
1. The source: sinc read or granular, plus loop crossfade, doubling, transient and grains.
2. Body / bright shelves.
3. Reimagined saturation.
4. `charFilter`.
5. Gain and pan.

Non-Kaleidoscope Reimagined modes have the same tail (IV:1357).

**The per-layer EQ** runs on the layer bus, after the voices are summed (IE:1168).

**DAMPEN.** It belongs per voice, after `charFilter`, at both tails. The voice knows its
layer (`voiceLayer`) and its sounding note.

## Resampling (DAMPEN's anti-aliasing question)

**The sinc read is band-limited:**
- Kaiser-windowed sinc, 16 zero crossings.
- `cutoff = 0.96 / max(increment, 1)`, with polyphase tables up to 4×.
- Exact kernels above 4×, so upward transposition is already anti-aliased. Every pitch
  component, including bend and modulation, sets the increment.

**Not band-limited:**
- Kaleidoscope grains and `GranularSource` read with Hermite interpolation.
- Toybox aliases deliberately.
- Granular's high transpositions are therefore the place to measure aliasing separately
  from natural brightness.

## State

**`stateVersion = 11`.**
- The curves are serialized as `modCurves` (`"c1:"` + exactly four curves). A VEL, NOTE and
  DAMPEN curve format needs its own versioned field, or a `c2:` encoding with a migration
  from `c1`.
- The links are in the `ModLinks` tree.

**Paths.** Presets live under `Documents/OSP/Presets`; templates go through the Library.

## Existing tests to keep green

**Modulation:**
- `[unit][mod]` (18 cases, including `[meta]` and `[regression]`).
- `[plugin][mod]`.
- `[plugin][mod][meta]`.

**Arpeggiator:**
- `[unit][arp]` (20 cases).
- `[plugin][arp]` (7 cases).

**Related:** `[unit][dynamics]` and `[plugin][sprite-knob]` (halos).
