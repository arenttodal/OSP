# Pedal Loop 1.0: PL0 audit and baseline

**Branch:** `claude/pedal-loop` (from OSP default `117aa15`). **Date:** 10 Nov 2026.

## 1. What exists today (read from the code)

| Concern | Where | What it does now |
|---|---|---|
| CC64 in the plugin | `apps/plugin/Source/PluginProcessor.cpp:1336` | `engine.setSustainPedal (m.isSustainPedalOn())`. The threshold is JUCE's (value >= 64). All Notes Off and All Sound Off call `engine.allNotesOff()`. |
| CC64 with the arpeggiator | `PluginProcessor.cpp:1541`, `src/engine/Arpeggiator.cpp:224-250` | While the arpeggiator runs it owns the pedal: the engine receives sustain-off at hand-over and sustain-on at hand-back. |
| Engine pedal | `src/engine/InstrumentEngine.cpp:888-915` | One engine-wide `pedalDown` (not per channel). A key released under the pedal sets `voice.setHeldByPedal (true)` and keeps the voice. Pedal up calls `release()` on those voices. `allNotesOff` and `reset` clear the pedal. |
| One Shot read | `src/engine/InstrumentVoice.cpp:1051-1337` (`render`) | `position += direction * currentStep`, read by `readFrame` (band-limited sinc, step-aware). `direction` is -1 under REVERSE. SPEED uses `stretchRead`. REIMAGINED modes other than KALEIDOSCOPE render in `renderMode`. |
| Continuation | `InstrumentVoice::scheduleNextJump` / `beginCrossfade` (`:495-652`), `ContinuationModel` (`src/model/ContinuationModel.h`) | The layer's LOOP (on by default) plus a source that can sustain (`cont->canSustain`) gives a random walk over analysed jumps, with correlation-aware crossfades (`crossfadeGains`). Otherwise `strategy = off` and the recording plays once. |
| Release graft | `InstrumentVoice::release` (`:376-437`), `scheduleGraft` | At note-off, an exit into the recording's own ending, when one is near. |
| Analysis | `src/analysis/continuation/ContinuationAnalyzer.cpp` (`analyseOnce`, `forcedLoop`), `src/engine/InstrumentBuilder.cpp:165,199` | Built off the audio thread at load. Decaying and struck sources get `canSustain = false` ("decaying source"). `forcedLoop` (Note Atlas, `ContinuationOptions::forceLoop`) loops a note's body when nothing else is found. Register anchors re-align the jumps (`refineContinuationForLayer`). |
| Voice lifecycle | `InstrumentVoice::kill`, `beginFastFade` (steal), `retirable` | The voice ends at the end of the recording (`position >= endPosition`), when the envelope ends, or after a steal fade. A key-up or pedal-held voice that stays inaudible is retired. |
| SYNTH / Imprint | `synthHoldsVoice`, `sourceEnded` | When the recording ends, the oscillator may hold the voice. A looping read never ends, so the oscillator is untouched. |
| KEY DNA, LIFE | `NoteShape` at note-on | Fixed for the note's life. Nothing re-rolls during a held note. |
| Note Atlas and sets | `InstrumentEngine::noteOnLayer` (member chosen at note-on), each member's own `PitchLayer::continuation` | One member per voice for its whole life. |
| Layer settings | `LayerSettings` (`InstrumentEngine.h:52`), host IDs `layerA.*`, `layerB.*`, `layerC.*` (`shared/andor-ui/Source/AndorParameters.cpp:273`, for example `id ("loop")`) | START, TUNE, PAN, LEVEL, REVERSE, LOOP, FOLLOW, and so on. |

Baseline, before any change: OSP CI run 295 is green on `117aa15`, and the local plugin suite passed 10/10 (`ctest --test-dir build-plugin`, 3,933 s, 10 Nov). The regression goldens (`[regression]`) and the golden renders pin the audio. Normal mode has to keep them byte-identical.

## 2. Decisions (the safe integration path)

1. **No second sustain engine.** The engine keeps its one `pedalDown`. A pedal change additionally tells each sounding, key-held voice `setPedal (down)`. Released-under-pedal voices keep their existing `heldByPedal` path, and pedal-up still calls `release()`. **Normal mode returns at once**, so no new arithmetic runs.
2. **The loop uses the voice's existing crossfade machinery** (`pending`, `beginCrossfade`, `crossfadeGains`):
   - a Forward loop is the region's jump, scheduled again after every pass;
   - a Ping-Pong turnaround is a crossfade whose incoming stream reads backwards, so the read direction is a multiplier (`ppDir`) that is exactly 1 in Normal;
   - late arming and exits are bounded crossfades of the same kind.
3. **Loop regions are analysed offline**, next to the continuation (`ContinuationModel::pedal`), from the same helpers (`alignDestination`, `crossfadeSecondsFor`, the 10 ms level blocks of `forcedLoop`).
   - A source that can sustain offers the continuation's best loop first.
   - A decaying source (piano, pluck) gets up to 8 short body regions spread along its decay, each aligned, with its own level.
   - Arming picks the **first region whose end is still ahead** of the read head. So a pedal pressed halfway through a piano's decay loops close to where the note is, with no jump back in level or timbre.
   - Register anchors re-align the regions as they re-align jumps.
   - No audio is copied.
4. **Forward with the layer's LOOP on, on a source that already sustains, changes nothing.** The continuation walk already holds the note, and it is the better forward loop. Forward matters where today's note plays once: LOOP off, or a decaying source. Ping-Pong always takes over while the pedal is down, and gives the read back to the walk (LOOP on) or to the recording's ending (LOOP off) when it is released.
5. **A voice keeps the mode it started with.** This is the same rule as a REIMAGINED mode. A live mode change applies to the next notes, so a looping voice never changes state illegally.
6. **Releases:**
   - **Key still held** (pedal up): the voice leaves the loop and plays on through the recording. Forward simply continues past the loop end, with no splice. Ping-Pong turns forward with a short mirror crossfade at the current position, at most about 15 ms.
   - **Key already up, then pedal up:** the loop stops the same way, the envelope releases, and **no release graft** happens (the spec asks not to jump to another part of the recording).
7. **Out of scope, by design:**
   - REVERSE layers, SPEED != 1 and REIMAGINED mode engines keep Normal behaviour. They read the recording through their own paths. Ping-Pong under SPEED plays as Forward.
   - Granular is unchanged. The setting is kept but has no effect there.
   - With the arpeggiator running, the arpeggiator owns CC64 (existing behaviour), so its notes never loop.

## 3. Plan

- **PL1–PL3:**
  - the analysis (`ContinuationModel::pedal`);
  - the voice state machine (`PedalState`: off, armed, looping, with crossfade substates in the existing crossfade fields);
  - Forward and Ping-Pong, late arming and exits;
  - a research command `--pedal-loop-lab <source> --output <dir>` that renders the matrix (variants A–J) and measures it: turnaround discontinuity, level around entry and exit, NaN/Inf, peak.
- **PL4:** `LayerSettings::pedalLoop`, the host parameters `layerA/B/C.pedalLoop` (appended, choice Normal/Forward/Ping-Pong, default Normal), state, the shell schema list, and the Atlas/set/SYNTH/KEY DNA checks.
- **PL5:** one compact PEDAL LOOP row in the existing contextual One Shot settings.
- **PL6–PL7:** listening matrix, benchmarks, full suites, macOS build, draft PR.
