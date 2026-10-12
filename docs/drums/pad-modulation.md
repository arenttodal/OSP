# ANDOR Drums: per-pad modulation

Each pad has its own modulation, in the MODULATION card (bottom left) next to its AMP
envelope. Screenshots: `screens/17-modulation-env.png`, `screens/18-modulation-lfo.png`.

## What a musician does

- **Tabs.** The MODULATION card has five tabs:
  - **AMP:** the pad's amplitude envelope, as before;
  - **ENV:** a per-hit envelope (ATTACK, DECAY, SUSTAIN, RELEASE). It starts as a hit shape,
    with an instant attack, a 150 ms decay and no sustain;
  - **LFO:** RATE (or a host-synced division with **SYNC**) and a shape (sine, triangle,
    ramps, pulse, random). **PER HIT** restarts it on every hit; switched off, one LFO runs
    freely for the pad;
  - **VEL:** how hard the pad is hit, with SOFT, LINEAR or HARD response;
  - **CTRL:** the **mod wheel** (CC 1) or **aftertouch**.

  A dot on a tab means that source is in use on this pad.
- **Routes.** Each source sends to up to **three targets**:
  - **PITCH:** ±24 semitones;
  - **LEVEL:** ±54 dB, stopping at the level knob's ends;
  - **PAN;**
  - **FILTER:** the CHARACTER filter's travel;
  - **START:** where a hit begins, taken as it starts;
  - **DECAY:** the AMP decay, × or ÷ up to 20.

  A depth reads in the target's own units: "+12.0 st", "-6.0 dB", "+45 %". Double-click a
  depth to clear it. Choosing a target starts it at a gentle, audible depth.
- **How VELOCITY reads.** The hardest hit plays the pad exactly as set; softer hits move
  away from it by the depth. So VEL → FILTER +45 % means "soft hits darker", even when the
  pad's filter is already wide open. VEL → START −4 % makes soft hits start a little later:
  ghost notes lose their attack.
- **PRESETS** gives a starting point. Each replaces the pad's whole modulation:
  - **Kick pitch drop:** ENV → PITCH +12 st over 60 ms;
  - **Harder is brighter:** VEL → FILTER;
  - **Harder is higher:** VEL → PITCH, one semitone over the velocity range;
  - **Tom pitch bend:** ENV → PITCH +3 st over 250 ms;
  - **Moving hats:** a 1-bar LFO → PAN and FILTER;
  - **Soft ghost notes:** VEL → START and FILTER;
  - **Clear modulation.**
- **Undo, saving and pad edits.**
  - Every change is one undo step; a drag is one step.
  - Modulation is saved with the session and with kits.
  - **Duplicate** copies it; **Clear pad** resets it, in the same undo step.
  - Swapping pads keeps it with its pad.

## How it is built

- **OSP's engine underneath.**
  - Every pad already plays through its own OSP `InstrumentEngine`. So a pad's modulation
    (`drums::PadModulation`, `src/drums/PadModulation.*`, pure C++) compiles into that
    engine's own `mod::Settings` (LFO 1, ENV 1, VEL, MOD WHEEL, PRESSURE).
  - Nothing new runs per voice: the same OSP modulation code drives PITCH (fine tune), LEVEL,
    PAN, CUTOFF, START and the AMP decay. It is real-time safe and deterministic: a
    smooth-random LFO repeats from the pad's seed.
  - **Off is off.** With no route, a pad renders bit-identically to one without modulation.
- **Getting to the audio thread.** The processor keeps each pad's modulation and publishes it
  through a lock-free `ModelExchange` per pad. On arrival the audio thread copies it into the
  pad's engine.
- **MIDI.** CC 1 and channel pressure reach every pad as WHEEL and PRESSURE.
- **State 4.** A `<Modulation>` element is written in a pad only when the pad differs from
  the default:
  - its children are `<Env>`, `<Lfo>` and one `<Route source slot target depth>` per route;
  - sources and targets are saved by index (append only); an unknown one, written by a newer
    build, is left out rather than misread;
  - states 1–3 open with no modulation;
  - kits keep schema 1, because this is an addition only.

## Tests

`osp_drums_tests "[padmod]"`:
- no routes, or routes at depth 0, are bit-identical;
- ENV → PITCH: the kick starts more than 4 st high and settles at 110 Hz;
- VEL → FILTER: soft hits are darker, and a velocity-127 hit is identical to an unmodulated
  one;
- VEL → PITCH: one semitone between velocity 1 and 127;
- LFO → PAN swings left and right, deterministically;
- WHEEL → LEVEL: −12 dB at full wheel;
- PRESSURE → PITCH: an octave at full pressure;
- one pad's modulation never reaches another;
- every preset compiles to active routes; depths read as words.

`andor_drums_plugin_tests "[padmod]"`:
- undo and redo;
- heard on its own pad only;
- state recall: a pad the state does not describe is reset;
- duplicate and clear;
- the card's tabs edit the selected pad, and undo returns to it.

The zero-allocation guard (M8) also edits modulation, undoes it, and sends CC 1 and
aftertouch while playing: still 0 allocations.

## Not done yet

- Modulating the kit's shared effects per pad (DRIVE, ECHO, SPACE are shared stages).
- KEY (note) as a source, for EXPANDED zones; it comes with the multi-sound spec.
- Host automation of route depths. Today they are saved and undoable, but not host
  parameters.
