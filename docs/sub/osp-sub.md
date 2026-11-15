# SUB in ANDOR OSP

_15 Nov 2026. Branch `claude/osp-sub`._

SUB is ANDOR ANALOG's sub oscillator (Cinematic V6 gate 2, popover V7), now in ANDOR OSP. It is
an independent polyphonic sub-bass instrument. It plays one or two octaves below the keys the
musician holds and joins the output after the effects. It is off by default, and off in every
session from before. While it is off the output is bit-identical to before.

## What was ported, and where it lives now

| Part | ANDOR ANALOG (pinned `feature/aux-live-input`) | ANDOR OSP (shared) |
|---|---|---|
| Oscillator | `dsp/SubOsc.{h,cpp}`, `dsp/Envelope.h` (`andor::analog`) | `src/engine/sub/SubOsc.{h,cpp}`, `SubEnvelope.h` (`osp::sub`), verbatim apart from the namespace and one `#include <initializer_list>` |
| Parameters | `addSubTo`, `analog.sub.*`, hint 30 | `InstrumentHost::addSubParameters (layout, hint, idOf)`: the same names, ranges, defaults, choices and texts. OSP registers `sub.*` at hint 33, after SATELLITE |
| Reader | `Reader::readWith` (SUB part) | `InstrumentHost::subSettingsFrom`, the same mapping |
| Popover V7 | `product/Source/SubPanel.{h,cpp}` | `shared/andor-ui/Source/SubPanel.{h,cpp}` (`osp::plugin`): the IDs come from `subview::Ids`, and the lit keys from an `isKeyDown` function (no `ShellProcessor` cast) |
| ARP / SUB housing | the shell's `arpCompanion` | `ArpControl::setSecondRow` (shared, from the same line), wired in OSP's editor |

ANDOR ANALOG keeps its own copy until its pin moves; the plan is under NEXT in `docs/STATUS.md`.

## The audio path

- **The keys before the arpeggiator.** `OspAudioProcessor::processBlock` walks the MIDI before
  the arpeggiator beside the arpeggiator's notes, sample by sample
  (`InstrumentEngine::handleSubMidi`).
  - On the same sample, SUB's key comes first.
  - SUB hears the keys even while it is off, so switching it on under a held chord plays the
    chord.
  - SUB holds the chord while the cards arpeggiate.
  - The on-screen pitch wheel bends it (`setSubBend`); the bend range is OSP's BEND.
  - Under MPE a member channel's bend is its own note's, so SUB ignores it.
- **After the effects, before the output gain.** `InstrumentEngine::renderSub` runs after
  `fxRouter.process` (DRIVE, MOVEMENT, ECHO, SPACE, or a CUSTOM route) and before the output
  gain, as in ANDOR ANALOG.
  - It is never any effect's input.
  - Under a CUSTOM route it is delayed by the route's latency (`fx::AlignLine`), so it stays in
    time with the cards.
  - It is mono and centred; a mono output adds it once.
- **MOVEMENT.** PULSE and SHAPER move SUB's level with the rest: `PostProcessor`'s movement
  trace, taken only while SUB sounds. DRIFT, TAPE and CHORUS leave it alone.
- **FILTER (CHARACTER).** SUB follows FILTER while it is a low-pass (LP24 or LP12). OSP's filter
  is not ANDOR ANALOG's, so the mapping is written for OSP (`InstrumentEngine::subFollow`):
  - **Cutoff:** CHARACTER's position on the MIN..MAX travel, plus the mean offset of the sounding
    voices (CUTOFF routes, velocity, drift), smoothed.
  - **Envelope:** CHARACTER's own, with the same depth, ATTACK and DECAY. Sustain is 0 and the
    release is DECAY, because CHARACTER's envelope keeps decaying after the key is released.
  - **No key tracking:** CHARACTER has none.
  - **No resonance:** SUB's follow is a flat 24 dB / octave low-pass.
  - **HP12, BP12 and TILT** have no low-pass to follow, so SUB stays unfiltered.

  With the default MIN of 450 Hz the follow rarely touches a 30-130 Hz sub. A lower MIN, or a
  negative envelope, makes it audible.
- **Real-time.** The buffers are sized in `prepare`. Keys, settings and rendering never allocate
  (tested). There is nothing random.

## The interface

- **The housing.** The arpeggiator's housing has two rows: ARP, and SUB under it.
  - SUB's light switches it, as one host gesture (undoable).
  - The rest of the SUB row opens its panel, above the housing with left edges aligned, and
    closes it again.
  - The "UP · 1/8" inset is not drawn while there are two rows (the shared `ArpControl`'s rule).
  - This is a deliberate visual change to OSP. The shell's parity render now draws the same row
    (`ShellProcessor::referenceSubRow`), so the example product's visual check stays at 0 px.
- **The panel** (V7, 440 x 340 reference px):
  - WAVE and OCT;
  - the dark display: the wave, the key response, the range on a keyboard, and the keys held
    now lit;
  - GRIT with its valve, and LEVEL;
  - RESPONSE over FROM and dB / OCT.

  Every control is one of SUB's parameters, so automation, presets and undo move it.

  Screenshots: `docs/sub/screenshots/` (1448 px, and the panel at 869 px).

## Learn

- **Info:** the SUB row has its own topic. ARP's Info region is now the housing's upper row.
- **Targets:** `osp.sub.row`, `osp.sub.enable`, `osp.sub.level`, `osp.sub.range`, and the state
  `osp.popup.sub.open`.
- **The lesson.** "SUB: a bass under your keys" (`osp.sub`) has six steps: set-up, open, on,
  hold a chord, LEVEL, range. Its set-up offers the bowed example but needs no recording.

## Tests

- **`osp_tests "[sub]"`** (10 cases):
  - ANDOR ANALOG's SubOsc cases: pedal, ALL NOTES / SOUND OFF, repeated keys, FLAT / TAPER, a
    note right after ALL SOUND OFF;
  - off is bit-identical whether or not it hears keys;
  - C4 plays C2, the same in both channels, and exactly SubOsc alone with SPACE and ECHO at full;
    the tail is SUB's own;
  - switching on under held keys, and off without a click;
  - the range and the wheel;
  - the FILTER follow (LP closes it; HP and TILT leave it);
  - PULSE moves it and DRIFT does not;
  - nothing allocates.
- **`osp_plugin_tests "[sub]"`** (8 cases):
  - the ten parameters (names, defaults, texts) and the reader;
  - the output with SUB off; C2 from C4; recall;
  - a session from before opens with SUB off;
  - steady under the arpeggiator;
  - the housing's light and row, and the panel above it at 869 / 1086 / 1448 / 1810 px;
  - the lit keys;
  - the drawn response equals `SubOsc::keyGain`; the range drag never crosses; FROM on the curve;
  - the SUB lesson end to end (automation does not complete a step).
- **Schema golden:** ten `sub.*` lines appended at v33 (intentional).
- **Hidden `[sub-shots]`** (`OSP_SUB_SHOTS=<dir>`): the screenshots and five demo renders.
  - The renders use the plucked tutorial note, C3 E3 G3 held under the arpeggiator.
  - They are: off, on, on with FILTER closed, on with PULSE, and SAW with GRIT one octave down.
  - Measured: SUB on adds about 16 dB RMS of bass; its tail ends with its release, with no
    reverb on it.

**Baselines A / B.** SUB adds a separate instrument and changes nothing in the voices. Off, the
output is bit-identical (tested), so every baseline render and golden is unchanged.

## Not verified

- No real DAW, and nobody has listened on monitors.
- **Output headroom.** OSP has no output guard after the master gain. SUB at 0 dB with GRIT and
  many keys can pass 0 dBFS, as any loud patch can.
- **PEDAL LOOP.** OSP's per-layer PEDAL LOOP repurposes the sustain pedal for the layers, but
  SUB always treats CC64 as sustain.
