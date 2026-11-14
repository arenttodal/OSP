# Universal modulation-aware visual feedback

Rule: if turning a control by hand changes a picture, then modulation that moves the same
control must change the picture in the same way. The picture draws what the DSP plays (the
effective value). The knob keeps showing its stored value, and nothing is ever written back to
it.

Baseline: Analog `5a31337`, with OSP `e6679008` (`feature/aux-live-input` in both repositories).

## Architecture (shared, in ANDOR OSP)

```
audio thread                                   message thread (30 Hz editor refresh)
------------                                   ---------------------------------------------
engine computes effective values      ->  osp::EffectiveValues  ->  InstrumentHost::effectiveValue (id)
(its own modulation sum, mapping,         (fixed table, 512 slots;     |
 clamps; nothing extra computed)           seqlock per slot;           +-> MacroPopups value()  (popover / rack pictures)
                                           value + span over the       +-> InstrumentHost::shownValue (any picture)
                                           last 1-2 windows of         +-> ModulationOverlay: halo live dot + span trail,
                                           ~33 ms)                     |   livePosition (AMP picture)
                                                                       +-> EqEditor (card EQ curve)
                                                                       +-> ShellProcessor::liveMix (header MIX node)
                                                                       +-> product pictures (Analog racks, cards)
```

### `src/engine/EffectiveValues.h`

A pure C++ class with no JUCE dependency.

- **What the audio thread does:**
  - `beginBlock` once per block;
  - `publish (slot, value)` for each value the engine has of its own;
  - `clear (slot)` when it has none.
- **What other threads do:** `read (slot)` returns the value and the low and high it covered.
- **How it stays real-time safe:**
  - fixed storage, with no allocation and no locks;
  - each slot is a sequence lock, so a reader never mixes two blocks.
- **The span:** each slot keeps the low and high of the last one or two windows of about 33 ms
  (one UI frame each). A modulation faster than the UI's refresh shows as the range it sweeps,
  never as one aliased point.

### `InstrumentHost::effectiveValue (id, out)`

- It returns the value the DSP plays for that parameter, in the parameter's own plain units, so a
  picture uses its usual mapping.
- `false` means the engine has no value of its own right now. The picture then draws the stored
  value, which is what plays in that case.

### `InstrumentHost::shownValue (state, id)`

This is the single helper the pictures read through. It returns the effective value when there
is one and the stored value otherwise.

- MacroPopups' `value()` now calls it, so every popover and rack picture reads effective values
  with no other change.
- `stored()` reads the stored value. It is used wherever a value is kept rather than drawn:
  - rack-preset capture and matching;
  - MOVEMENT lane copy, swap and snapshot.

  Saving a preset while an LFO runs stores the knob's value, not the LFO's position.

### Engines

| Engine | What it publishes |
|---|---|
| ANDOR OSP (`InstrumentEngine`) | LIFE, DRIVE, CHARACTER, MOVEMENT, SPACE, ECHO: the macros exactly as the shared stages take them (`post.setMacros`). Each layer's EQ as `effectiveEq` plays it: bell frequency and gain, low-shelf gain, high-shelf gain. Active only while a route reaches a shared stage. **Per voice:** each layer's grain POSITION, SPREAD and START as its newest sounding note plays them. START is the value taken at note on, as the DSP takes it. |
| ANDOR Analog (`AnalogEngine::publishEffective`) | **Global:** every parameter whose block value (`raw`, after USER macros and the global routes) differs from the knob. This covers MOVEMENT, DRIVE, SPACE, ECHO, the MIX (`ab.blend`), every global product destination and every USER macro target. **Per voice:** each product voice destination as the representative voice plays it (the newest sounding voice, as the modulation display shows it), mapped through the same `Reader::move` that `moveControl` uses. Silent: per-voice values are inactive, so the pictures draw the knob. |
| Shell (`ShellProcessor`) | Passes these through (`Engine::effectiveValue`). `liveMix()` is built from the engine's `ab.blend`, so the header MIX node now lives in Analog too. |

## Inventory (Gate 0) and what changed

Status key:

- **FIXED**: the picture now draws the DSP's effective value.
- **LIVE**: the picture was already driven by engine telemetry.
- **N/A**: the parameter is not a modulation destination.
- **OPEN**: listed at the end, with the reason.

| Product | Module / component | Parameters | Before | Now |
|---|---|---|---|---|
| Both | MOVEMENT SHAPER popover and rack (`MovementVisual`, `ShaperEditor`) | motion, lane INTENSITY, shaper.* | stored depth, live playhead | **FIXED**. Tests `[effective][shaper]`: a modulated 80 % is pixel-identical to a hand-set 80 % (OSP; Analog lanes A and B). |
| Both | MOVEMENT DRIFT, TAPE, CHORUS, PULSE pictures | motion, mode settings | stored | **FIXED** for MOVEMENT. In Analog, also for lane A's global product destinations (drift, tape, chorus and pulse settings). |
| Analog | SHAPER DNA display | shaper.smooth | stored | **FIXED** (it reads through `plain`/`shownValue`) |
| Both | DRIVE transfer curve | drive; in Analog also drive.tone and drive.body | stored | **FIXED** |
| Both | ECHO picture (popover and rack) | echo; in Analog also echo.time, feedback, tone, age | stored | **FIXED**. Tails are untouched: pictures read, the DSP is not reset. |
| Both | SPACE picture and EQ handles | space; in Analog also space.* | stored | **FIXED**. The value shown is the one already applied to the DSP. |
| OSP | CHARACTER popover | character | stored | **FIXED** (macro). Per-voice CUTOFF and RESONANCE: OPEN (OSP). |
| OSP | LIFE picture | life | stored | **FIXED** |
| Both | Card EQ curve (`EqEditor`) | layerX.eq.* | UI mirror (newest voice's envelope; wrong clamp) | **FIXED**: the engine's `effectiveEq` (OSP). In Analog the EQ destinations do not move the DSP, so the curve no longer pretends they do. |
| Analog | FILTER rack: graph, VOICE, MOD, CHARACTER, PAIR pages | tone, resonance, drive, bias, poly mod, LFO amount … | cutoff live, the rest stored | **FIXED**: resonance, drive and the rest now read the effective value (the representative voice for per-voice controls). |
| Analog | MORPH rack (per voice) | morph position, texture, detail, formants | stored | **FIXED** (representative voice) |
| Analog | Oscillator card waveforms (`sourcePicture`) | width, PWM, shape, spread, sub, wave … | stored | **FIXED** (`shownPatch`). Test `[effective][voice]`: the modulated OSC 1 WIDTH equals the hand-set width; silent, it shows the knob. |
| Both | Halos: live dot (`ModulationOverlay`) | every routed control | UI mirror of each route alone | **FIXED** where published: the engine's sum (all routes, USER macros, clamps), plus a faint trail over the span covered since the last frame. Elsewhere the mirror is kept. |
| Both | AMP envelope picture | attack, decay, sustain, release | UI mirror (`livePosition`) | engine first (`livePosition`), mirror otherwise. The amp ADSR are ANDOR OSP voice destinations that neither engine publishes yet: OPEN. |
| OSP / Analog | Header MIX live node | ab.blend | OSP live (`liveMix`); Analog never | **FIXED** in Analog (`ShellProcessor::liveMix`) |
| Both | ARP step display | arp.rate, gate, swing | live steps (actual events) | **LIVE** |
| Both | Granular grain dots, read heads, POSITION band, START | grain position, spread, start | live dots; band and START stored | dots **LIVE**. Band and START **FIXED** in OSP: the newest note per layer (`publishVoiceEffective`); START as taken at note on. Test `[plugin][effective][voice]`. In Analog the grain controls are ANDOR OSP layer destinations: OPEN. |
| Analog | SATELLITE, C RHYTHM, AUX displays, SUB, FX library graphs | (not modulation destinations) | stored plus live telemetry | **N/A** |
| Both | MOD panel curves (LFO / ENV shapes) | mod.lfoN.*, mod.envN.* | shape stored, playhead live | shape is a modulation-source setting. Modulating ENV1/2 times: OPEN. |

## Tests

| Product | Test | What it shows |
|---|---|---|
| OSP | `[unit][effective]` | The table: one block's value, the span over a frame, inactive when cleared, bounds. |
| OSP | `[plugin][effective][shaper]` | SHAPER at a hand-set 80 % and at a stored 20 % plus the mod wheel's 60 % are pixel-identical; a hand-set 20 % differs. The knob stays at 20 %. An LFO sweep moves the picture frame after frame, and the reported span covers it. |
| Analog | `[analog][effective][shaper]` | The same for MOVEMENT lanes A and B (B at its own INTENSITY 70 %), in the rack. |
| Analog | `[analog][effective]` | LFO sweep: span reported; the knob never written back; the saved state keeps the knob. Two routes summed, stopping at 100 % where MOVEMENT clamps; opposite polarity. |
| OSP | `[plugin][effective][voice]` | Layer A's grain POSITION (20 % plus 30 % from the wheel plays 50 %) and START taken at note on; once the note has faded, the knobs again. The knob is never written. |
| Analog | `[analog][effective][voice]` | OSC 1 WIDTH per voice (mod wheel, product destination): the card waveform equals the hand-set width; with no voice sounding it shows the stored width. |

## Real-time and cost

**What the audio thread does:**

- **OSP:** one publish per macro (6) and four per layer's EQ while a route moves them.
- **Analog:**
  - one compare per reader value (about 300 floats) per block;
  - one publish per value that moved;
  - one `Reader::move` per routed voice destination, for the representative voice only.

There are no allocations, locks or strings in any of this. No extra modulation is computed:
these are the values the engine already uses.

**What the UI does:** one map lookup and one sequence-lock read per value drawn. Pictures
already repaint at the editor's 30 Hz only while they change or animate.

**Editor closed:** the publishing continues; it is a few hundred relaxed atomic stores per block.

## OPEN (not done in this pass, and why)

- **The remaining ANDOR OSP per-voice destinations** have no published value yet. Grain
  POSITION, SPREAD and START are published from each layer's newest sounding voice
  (`publishVoiceEffective`); the same snapshot can be extended to the rest. Still affected:
  - the CHARACTER picture's CUTOFF and RESONANCE;
  - grain size and density;
  - layer level, pan and tune;
  - DAMPEN;
  - the amp and modulation envelope times.

  Their halos keep the UI mirror.
- **Analog's ANDOR OSP layer destinations** (grain position etc. on cards A and B, amp ADSR) are
  moved by `Synth::applyVoiceModulation` in the DSP's own units. They are not yet mapped back to
  parameter values.
- **Envelope pictures** under note-on-only modulation (attack and decay taken at note on):
  waiting on the same per-voice snapshot.
- **Visual QA in a DAW**, and automation compared with modulation in a host: UNVERIFIED (OWNER
  TEST).
