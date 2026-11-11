# Pedal Loop PL2: Ping-Pong

**Reading direction.** `position += direction * ppDir * currentStep`. `ppDir` is ±1 and is exactly 1 outside Ping-Pong, so Normal's arithmetic is unchanged.

The pitch step is the voice's own (root mapping, bend, glide, modulation), so direction changes never reset pitch. Stereo channels share one read and one gain. The sinc read is step-aware in either direction.

**Turnarounds.** Three alternatives were compared (spec §7). The lab (`research-renderer --pedal-loop-lab`, `src/research/PedalLoopLab.cpp`) measured each turn's click energy: the high-passed (second-difference) energy around the turn against the audio 20–60 ms either side, in dB. 0 means the turn is indistinguishable from the surrounding audio.

- **A: plain reflection.** The read folds back at the end, and overshoot reflects (the fold is bounded: two folds, then the region's middle).
- **B: a mirror crossfade** of 4, 8 or 15 ms. The read carries on to the turning point while an incoming stream leaves it the other way. Both read the same audio, and at the crossfade's middle the same sample.
- **C: the 8 ms crossfade's gains**, assuming uncorrelated audio (equal power) or identical audio (linear).

Median of the turn click energy, in dB above the surrounding audio:

| Source | A reflect | B 4 ms | B 15 ms | C 8 ms equal power | C 8 ms linear |
|---|---|---|---|---|---|
| piano (stand-in) | 1.8 | 1.1 | 0.1 | 0.9 | **0.0** |
| nyckelharpa (stand-in) | −0.8 | −0.2 | −0.3 | 0.1 | **−0.8** |
| cello (stand-in) | 1.3 | 0.2 | −0.8 | −0.3 | **−0.9** |
| vocal vowel (stand-in) | −1.4 | −0.1 | 1.0 | 0.8 | **−0.2** |
| vocal phrase (stand-in) | 0.7 | −0.4 | −0.9 | −0.4 | **−0.6** |
| guitar (stand-in) | 4.2 | 3.6 | 0.4 | 0.9 | **0.3** |
| organ (stand-in) | 1.7 | −0.3 | −0.5 | 0.1 | **−0.4** |
| percussive (stand-in) | 3.7 | 3.7 | 6.3 | 6.3 | 5.8 |
| piano chord one-shot (recording) | 2.3 | 0.9 | 1.0 | 1.8 | **0.7** |
| organ chords (recording) | −0.4 | 1.1 | 0.4 | 0.3 | **−0.4** |

**Chosen: an 8 ms mirror crossfade with linear gains** (`EngineSettings::pedalTurnSeconds = 0.008`, `pedalTurnCorrelation = 1`).
- It is the quietest or near it on every tonal source.
- It matches the theory: the two streams meet on the same sample at the middle, so linear gains keep the level with no dip, and equal power bumps it.
- Reflection is clearly worse on plucks (guitar 4.2 dB).
- 15 ms is as quiet but smears more.
- A percussive hit is artificial whichever way. The spec allows that, and it is documented.

## Gate

- Ping-Pong turns at both ends and stays in its region at every pitch tested: notes −12, 0, +24 and +36 from the root, reflection and crossfade.
- No NaN or Inf in 212 lab renders.
- No severe clicks: every tonal source's median turn energy is within 2 dB of its surroundings.
