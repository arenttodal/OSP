# Pedal Loop PL6: listening

**Status: renders ready; the owner's listening is pending.** Automated measurements are in `lab-results.md` (212 renders) and summarised in 02 and 03. They show no clicks, steps or stuck voices. They cannot say whether a loop is musical.

## The renders

`research-renderer --pedal-loop-lab <dir> [--recordings a.wav,b.wav]` writes one folder per source.

**Sources.** Eight synthetic stand-ins: piano, nyckelharpa, cello, vocal vowel, vocal phrase, guitar, organ, percussive. Then any recordings given; this run used the owner's piano chord one-shot and organ chords. Recordings stay local and are never committed.

**Performances** (spec §16), each in `-normal`, `-forward` and `-pingpong`:

| File | Performance |
|---|---|
| `H-key-up-under-pedal` | Pedal first, the key up at 1 s, the pedal up at 6 s. Covers spec H and I. |
| `D-G-pedal-first-released-with-key-held` | Pedal first, the pedal up at 5 s with the key still held. Covers spec D and G. |
| `E-pedal-halfway` | The pedal at half the sound's length. |
| `F-pedal-near-the-end` | The pedal after the last loop point (late arming). Released with the key held, then the key. |
| `J-chord` | Root, third, fifth and octave. Keys up at 1 s, pedal up at 6 s. |
| `turn-*` | Ping-Pong's turnaround alternatives (02-pingpong.md). |

## What to listen for (rate each 1–5)

- **Attack preservation.** One attack per note; nothing restarts.
- **Sustain smoothness.** Forward's jump, and Ping-Pong's turns.
- **Turnaround audibility.** Ping-Pong reading back should breathe, not pump or reverse a transient.
- **Pitch stability and timbre.** The note stays itself.
- **Level continuity.** Entering the loop, and a late pedal (F).
- **Release naturalness.** D/G returns to the recording; H/I fades.
- **Musical usefulness.** Forward versus Ping-Pong per source. Neither is assumed better.

## Mandatory demonstrations (spec §20), on real recordings

1. **Piano.** Play a note and press the pedal halfway through its decay (`E-pedal-halfway`). Then release the pedal with the key held (`D-G`).
2. **Nyckelharpa.** Pedal before the note. One bow attack; the body loops; no click at the reversals.
3. **Vocal.** Pedal while it sounds; Ping-Pong. It stays recognisably vocal. Note the artefacts.
4. **Chord.** Four notes, pedal, keys up. Each note holds apart; the pedal up ends all four.

The owner's piano chord one-shot and organ chords are in the lab run. The nyckelharpa and the vocals are stand-ins here and need the real recordings.

## Known from the measurements, to confirm by ear

- **Percussive hits** loop audibly (turn energy 4–7 dB). That is expected: a hit has no body to sustain.
- **A sung phrase** released with the key held reads on into its next sung note. That is the recording's own continuation.
- **A late pedal on a fast-fading note** loops quietly, at the level the note had. It never jumps back up to the loop's own level.
