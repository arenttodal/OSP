# PEDAL LOOP × REIMAGINED, PL-R0: the incompatibility, reproduced

Branch `claude/pedal-reimagined`, from the default branch (56bab47). This supersedes Pedal Loop
1.0's limitation "REIMAGINED modes keep Normal" (`docs/research/pedal-loop/04-integration.md`,
`07-release-review.md`).

## Reproduction

`osp_tests "[pl-r0]"` (`tests/unit/PedalReimaginedTests.cpp`, hidden) uses the pedal-loop
suite's struck, decaying 220 Hz note: 3 s long, with a tail from 1.6 s and valid pedal regions.
- Settings: One Shot, the layer's LOOP off, PEDAL LOOP Forward or Ping-Pong.
- Performance: the pedal down before the note, the key released at 0.5 s, the pedal held to 8 s.
- A working loop still sounds at 7–8 s. A note that fell back to Normal is gone by then.

| mode | 0 % | 50 % | 100 % |
|---|---|---|---|
| KALEIDOSCOPE | loops (−39.0 / −36.9 dB) | loops (−39.5 / −37.7) | loops (−40.1 / −41.0) |
| TAPE FRAME | loops | **not armed, silent** | **not armed, silent** |
| TOYBOX | loops | **not armed, silent** | **not armed, ended (−85.8 dB residue)** |
| MOSAIC | loops | **not armed, silent** | **not armed, silent** |
| MIRAGE | loops | **not armed, silent** | **not armed, silent** |

(Levels: Forward / Ping-Pong, RMS 7–8 s. At 0 % no mode engine runs, so every mode is the
voice's own path.)

## Root cause

1. **Eligibility.** `InstrumentVoice::start` (`src/engine/InstrumentVoice.cpp` :385) forces
   `pedalMode = Normal` whenever `modeEngine != nullptr`. Only TAPE FRAME, TOYBOX, MOSAIC and
   MIRAGE create an engine, and only above 0 %. KALEIDOSCOPE is the voice's native path plus
   the layer's stage (`startModeEngine` returns before creating one, :1794), so it was never
   gated. It loops at every amount, including its doubling and grain continuation, which read
   relative to the voice's position.
2. **Rendering.** With an engine, `render()` delegates to `renderMode()` (:1838).
   - TAPE FRAME, TOYBOX and MIRAGE read the recording with their own heads; the voice's position
     does not move.
   - MOSAIC uses the voice's dry read only below full rebuild. That dry read (:1886–1922) ignores
     the pedal machinery: `ppDir`/`xDir`, pedal gains, `finishPedalCrossfade`, `pedalUpdate`,
     `reflectPedal` and the trend.
   - So removing the gate alone would arm a loop nothing plays.
3. **Lifecycle.** Each engine ends its note at the recording's end:
   - TAPE FRAME `straight`: `main.t >= straightEnd`;
   - TOYBOX: the head at `soundEnd`;
   - MIRAGE: `heads[0] >= soundEnd`;
   - MOSAIC: 0.3 s after its last frame when not holding.

   `renderMode` kills the voice on `finished() && (!dry || dryEnded)` (:1988).
4. **Ordering trap.** `startModeEngine` (:379) sets `strategy = off` for engines that read
   themselves, before the eligibility test. A relaxed gate must test the layer's LOOP
   (`params.strategy`), not `strategy`.

There is nothing in the interface (`EngineCard` greys PEDAL LOOP out only outside One Shot) or in
Learn about the limitation, so the setting stays visible while the voice ignores it, as reported.

## How each engine plays a One Shot note

| | heads / timeline | ends | own sustain (LOOP on) |
|---|---|---|---|
| TAPE FRAME | `straight`: main head at `straightOrigin + t` (source frames) plus two ghost heads 30–90 ms behind; wow and flutter on the step | `t >= straightEnd` | spliced tape rewound to the body with per-pass wear |
| TOYBOX | `heads[0..1]` in source frames, read through `storedAt` (rate/bit reduction); FWD / TURN / CHAOS legs between `lowBound`/`highBound`; memory taps from a position history | head at `soundEnd` | loops the sustain region (20 ms turns) |
| MIRAGE | `heads[0..1]` in source frames, zero-order-hold reads (`heldAt`) | `heads[0] >= soundEnd` | best-loop points, 15 ms correlated crossfade |
| MOSAIC | time-based: `seconds` → `naturalFrame` → spectral `position` (glides); dry read from the voice until the rebuild is total | 0.3 s after the last frame | holds and ping-pongs the body |

## Plan (PL-R1–R5)

**Shared contract (PL-R1).**
- `ReimaginedVoiceEngine` gains:
  - `pedal (const PedalLoopRequest&)`: the note's selected mode, the region model and turn
    settings, the pedal state;
  - `pedalArmed()` / `pedalLooping()`.
- One shared, allocation-free traversal, `PedalFold` (`src/engine/PedalFold.h`):
  - It maps a head's linear source position into the analysed pedal region: Forward wraps with
    the region's correlated jump crossfade; Ping-Pong turns at both ends with the pedal's turn
    crossfade.
  - It handles the late pedal, at most 50 ms, level-matched, into the last region, as the
    voice does.
  - Pedal up resumes ordinary playback: a forward read goes on, a backward one turns forward,
    a late loop returns to where it found the recording.
  - It is a pure function of position, so every head of an engine (main, ghosts, history
    taps) folds consistently and in either direction of movement. No engine duplicates the
    state machine.
- The voice:
  - relaxes the gate per engine;
  - forwards `setPedal` to the engine;
  - reports the engine's pedal state;
  - its `renderMode` dry read gets the same pedal handling as `render()` (MOSAIC's dry part).

**Engines (PL-R2).**
- **TAPE FRAME:** its straight heads (main and ghosts) read through the fold. Wow, flutter,
  ghosts and AGE stay as they are, and the attack plays once. Each completed pass adds the
  tape's own wear (`nextPassGain`).
- **MIRAGE:** heads through the fold. Clocking, quantisation and the filter stay as they are.
- **TOYBOX:** its own walk is kept; the pedal sets its working region.
  - Forward: the region loops with TOYBOX's own turns.
  - Ping-Pong: the walk turns at both ends.
  - TURN / CHAOS stay inside the region.
  - Pedal up restores the One Shot region.
- **MOSAIC:**
  - the dry part loops through the voice;
  - the spectral timeline (`seconds`) folds over the region's time span, so the spectrum and
    its level follow the loop (Forward wraps, Ping-Pong reflects) instead of ending or
    freezing.
  - at 100 % no dry read is kept alive.

**Lifecycle (PL-R3).**
- Engines never report `finished()` while their fold is holding.
- After pedal up, the ends are tested on the folded position, so a finished recording is never
  resurrected.
- Envelope release, stealing and All Notes Off are unchanged.

**Exclusions.**
- Normal, the pedal up, and REIMAGINED at 0 % stay bit-exact: no change to RNG draw order or
  float expressions on those paths.
- Nothing changes in parameters, presets, Granular, REVERSE/SPEED policy or the GUI layout.
