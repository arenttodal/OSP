# Pedal Loop PL1: the loop regions and Forward

## Loop regions (analysis, off the audio thread)

`analysePedalLoops` (`src/analysis/continuation/ContinuationAnalyzer.cpp`) runs inside `analyseContinuation`. So every model gets its regions in `ContinuationModel::pedal`: single files, Note Atlas members and set members, each from its own audio. `refineContinuationForLayer` re-aligns them for register anchors. The loader already runs this on its background thread.

1. The level is measured in 10 ms blocks (dBFS). The sound ends at the last block within 30 dB of the loudest.
2. **The body starts after the attack:** onset + attack + 60 ms. The note's own attack is played once and never looped.
3. **A source that sustains** (`canSustain`) offers its continuation's best loop as region 0. These are the analysed, waveform-aligned jumps the walk already uses.
4. **The rest of the body** gets up to 8 regions in all, spread evenly:
   - each is 0.4 s long, or a third of a short body (your 0.87 s piano one-shot gets 3);
   - each is at least 0.12 s;
   - each is shortened until its level falls by no more than 6 dB.

   Each Forward jump (end − crossfade → start) is aligned with the continuation's own `alignDestination` (waveform correlation within ±1.2 periods), with a crossfade of `crossfadeSecondsFor` (40–150 ms), capped at 30 % of the region.
5. Each region also stores its RMS level and its **level trend** (the last 50 ms minus the first 50 ms).
6. **No region** (silence, a click with no body, under 0.15 s): `valid = false` with a reason. The note then plays as in Normal.

No audio is copied. The descriptor holds frame positions, levels and a 10 ms level curve for late arming.

## Forward (the voice)

`src/engine/InstrumentVoice.cpp`, "PEDAL LOOP" section, uses the continuation's crossfade machinery:
- **Arming** picks the **first region whose leaving point is still ahead** of the read head, and schedules its jump. The read goes on forward to it, with no reset and no attack replayed.
- After every pass the same jump is scheduled again (`schedulePedal`).
- **Where the layer's LOOP already holds the note** (a source that sustains, LOOP on), Forward changes nothing: the continuation walk is the better forward loop. Forward is for notes that play once today: LOOP off, or a decaying source.

**Trend compensation.** A forward loop on a decaying note jumped from the quieter end back to the louder start: +3 dB every 0.4 s on the piano stand-in, a pulse (the first test run caught it).
- While Forward loops, the region's **overall trend** is taken out: `trendGainAt`, 0 dB at the jump's leaving point, linear in dB across the region.
- So the jump lands at the level the note had. Shorter fluctuations stay.
- Nothing is normalised per pass, and Ping-Pong keeps its natural rise and fall.
- Result: the attack-replay test (largest 10 ms rise over 30 ms after 0.3 s) went from 3.2 dB to under 3 dB. In the lab, Forward jump clicks on the piano stand-in are 0.1 dB.

## Gate

Forward holds a decaying note from one attack for as long as the pedal is down (`[unit][pedal-loop]`), and Normal is unchanged. The `[regression]` goldens passed at 332,030 assertions, and the full core suite passed 5/5.
