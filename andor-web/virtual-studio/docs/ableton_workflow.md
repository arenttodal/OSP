# Recording a performance for ANDOR Virtual Studio (Ableton Live)

**What Virtual Studio needs from a take:**
- the MIDI you played (notes, velocities, pitch bend, mod wheel, sustain);
- every control that moved;
- the bounced audio;
- the exact preset and sources;
- the plugin build.

The headless capture then replays the MIDI and controls through the same build, so the film's
interface frames are the plugin's own drawing, in sync with your audio.

## What is verified, and what you check on your machine

**Verified in OSP's code (`PluginProcessor.cpp`):**

| OSP responds to | As |
|---|---|
| Notes and velocity | Notes |
| Pitch bend | Pitch bend (its range set by the plugin) |
| CC 1 | The MOD strip |
| CC 64 | Sustain |
| CC 74 | Timbre |
| Channel pressure | Pressure |

**The macro controllers, CC 20–27:**

| CC | 20 | 21 | 22 | 23 | 24 | 25 | 26 | 27 |
|---|---|---|---|---|---|---|---|---|
| Macro | LIFE | Dynamics | CHARACTER (FILTER) | MOTION (MOVEMENT) | SPACE | A's REIMAGINED | ECHO | DRIVE |

**Not verified here (no Live installation in the build environment):**
- whether Live's *Export MIDI Clip* writes a clip's pitch-bend and CC envelopes into the file, or only the notes;
- how a given Live version names its automation export options.

**Do not rely on either until checked.** The check is quick: `vs.py ingest` prints how many notes, pitch-bend events and CC events it found. If you moved the mod wheel and it reports 0 CC events, the export dropped them.

**How a performed control reaches the capture:**

| How you move it | Reaches the capture as | Notes |
|---|---|---|
| Mod wheel, pitch bend, sustain | MIDI in the clip | Export check above |
| A hardware knob sent as CC 20–27 to OSP | MIDI CC, which OSP's macros follow | Sound only: the editor's knob does **not** move (measured, see the knob note below). Not for takes where the knob is filmed |
| Live's own automation of an OSP parameter (drawn, or recorded via Live's MIDI Map) | Plugin automation: **not** in a MIDI clip | Export as automation lanes (below) |
| A control without a CC (e.g. source TUNE, a card's REIMAGINED for B/C) | Plugin automation | Same |

**Knob note:**
- When a CC moves a macro, OSP's sound follows the CC. The macro's *host parameter*, and so the knob drawn in the editor, may not move.
- **Measured** with the technical test performance: CC 22 at 9.1–9.9 s sweeps CHARACTER from 31 % to 87 %, and the editor's knob stays at 88 %, its last automated value.
- **Perform macros that should visibly turn in the film as plugin automation**, drawn or recorded in Live, not as CC.

## Recommended recording routine

1. **Set up** the OSP track with the episode's preset and sources. Save the plugin state as a preset file and keep the source WAVs.
2. **Record a separate MIDI track.** Arm a MIDI track that receives your controller and records only what you play (notes, bend, mod wheel, sustain, CC 20–27). This is the performance of record.
3. **Route that MIDI track into OSP**, so what you hear is exactly what was recorded.
4. **Record parameter automation** for any OSP control you move with the mouse or a MIDI-mapped control (Live's Arrangement automation).
5. **Bounce the take** (24-bit WAV at the session's sample rate, from bar 1 or the excerpt's start). This is the audio of record.
6. **Export the MIDI clip** (*Export MIDI Clip…*) of the recorded track.
7. **Export the automation:** for each automated parameter, write its lane as `time_s,param,value` rows (seconds from the bounce's start, the parameter's own units) in one CSV.
   - Until a Live-side exporter is set up, two ways:
     - **(a)** read the breakpoints off the lane (fine for a few moves);
     - **(b)** perform those moves as CC 20–27 instead (see the knob note).
   - Parameter IDs are OSP's (`character`, `motion`, `space`, `reimagined`, `layerA.tune`, ...).
8. **Note the plugin build:** OSP's **INFO** panel (the version and commit), plus Live's version.
9. **Ingest:**

```bash
python3 vs.py init  episodes/001-nyckelharpa --id 001-nyckelharpa-take3 --title "Nyckelharpa, take 3"
```
```bash
python3 vs.py ingest episodes/001-nyckelharpa --midi take3.mid --automation take3_automation.csv \
  --audio take3_bounce.wav --source nyckelharpa_A3.wav --preset take3.state --build <commit> \
  --start 12.0 --end 27.5
```

`--start` / `--end` select the excerpt in seconds; the excerpt is re-based to 0, and the bounce must start at the same point.

10. **Check before capturing:**

```bash
python3 vs.py check episodes/001-nyckelharpa
```

Confirm the counts match what you played: notes, bend and CC events, and lanes.

## A durable solution worth building (proposal, not built)

A **plugin-side performance log** in an internal build of OSP would remove every export question:
- It records the incoming MIDI and every parameter change with sample-accurate timestamps while Live plays the take, then writes the JSON this toolkit reads.
- It is host-independent, and captures exactly what the plugin received, including Live's automation.
- **Audio-thread safety (OSP Rule 1):** events go into a pre-allocated lock-free FIFO, and a message-thread timer writes the file. Nothing is allocated or written on the audio thread.
- It is not a product feature, and it never changes the sound.

It needs your go-ahead as plugin code.
