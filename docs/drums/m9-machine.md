# ANDOR Drums M9: MACHINE (experimental prototype)

Spec Part 9 and Milestone 9. One control, **MACHINE**, in the GLOBAL card's header: it adds
the character of an older drum machine to the whole kit. As spec 9.4 asks, it is built only
from processing OSP already has. There is no new DSP module and no model. Screenshot:
`screens/16-machine.png`.

## What a musician does

- **The fader** sits beside GLOBAL (KIT). Its amount is shown as a word:
  - OFF, the samples exactly as they are;
  - TOUCH;
  - SUBTLE, about 35 %;
  - CLEAR;
  - STRONG.
- **Turning it back.** Double-click turns it off. It is a host-automatable parameter
  (`kit.machine`, 0–1, default 0) and is saved with the session and the kit.
- **The other controls stay usable.** MACHINE never lowers DRIVE, MOVEMENT or LIFE below
  what the musician set; it only raises their floor. The pads' own settings still apply on
  top.

## How it is built (`DrumKitEngine::effectiveMacros / effectiveShaping / applyPad`)

At 0, both functions return the kit's settings unchanged, and applyPad leaves the layer
alone. Above 0, with `a` the amount from 0 to 1, each characteristic comes from an existing
module:

| Older-machine characteristic | Existing OSP module | MACHINE sets |
|---|---|---|
| Lower sample rate, fewer bits, a simple DAC | REIMAGINED **TOYBOX** (its DIGITAL character only: forward play, no motion) | digital 0.25 + 0.6a; REIMAGINED blend ≥ 0.22a |
| Converter filtering: less top end | CHARACTER's filter | 12 dB/oct low-pass; its top closes towards 6 kHz (log scale); resonance ≤ 0.1 |
| Saturation, rounded transients | DRIVE, tape mode | DRIVE ≥ 0.35a; body 0.5 + 0.25a; tone 0.5 − 0.2a |
| Instability, noise and wear | MOVEMENT, tape mode | MOVEMENT ≥ 0.22a; wow 0.15; flutter 0.25 + 0.2a; wear 0.25 + 0.5a |
| Hit-to-hit variation | LIFE | LIFE ≥ 0.25 + 0.2a; tone variation ≥ 0.3 + 0.25a |
| Tuned percussion stays in tune | LIFE's pitch range | at most 1.5 + 3.5a cents per hit |
| Timing imperfection (a machine's clock and MIDI jitter) | none per pad | **not done**: it would move notes, and a sampler that plays late is not what a musician asks for. A later decision |

**One engine fix was needed for the TOYBOX path.** A REIMAGINED mode voice reads the
recording on its own, so START/END slices (M3 layers, M6 break slices) used to play past
END. `InstrumentVoice` now also counts the mode voice's output and fades it out at END. The
fade is the same at least 1.5 ms fast fade as before. This changes nothing in OSP: END is
only set by Drums, and with END at 1 the new code never runs. The OSP suite (unit,
integration, regression goldens) passes unchanged.

## Validation (`osp_drums_tests "[machine]"`, `andor_drums_plugin_tests "[machine]"`)

- **Off is off.** Kits at MACHINE 0 render bit-identically to kits without it, and so do
  sessions with the parameter at its default. Turning it up and back down restores the same
  settings.
- **Bandwidth.** On a bright saw-like hit, the high-frequency share (the RMS of the first
  difference over the RMS) is:
  - 0.193 off;
  - 0.096 at 35 %;
  - 0.065 at 100 %.

  The level stays within a few dB.
- **Pitch.** A 110 Hz tom at 100 %: six hits within −5.0 … +1.7 cents of the original. The
  test limit is 12 cents.
- **Variation.** Repeated hits differ from each other. The same seed renders identically.
- **END with TOYBOX.** A layer ending at 20 % still stops there at 100 % MACHINE.
- **Real time.** The zero-allocation guard (M8) now also turns MACHINE on and changes it
  while playing: 0 allocations.

## Open (needs listening)

- **Is 35 % "subtle" enough?** The bandwidth halves at 35 %, which may already be more than
  subtle. Tuning the curve needs the A/B listening test against the plain kit (CLAUDE.md:
  baseline A/B). The curve lives in the two functions above, so a retune changes nothing
  else.
- **Named machines.** A choice of machines (different rates, filters, drives) would be
  presets of the same table. Not done until the single control is judged.
