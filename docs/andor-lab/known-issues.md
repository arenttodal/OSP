# ANDOR Lab — known issues and open items

Status: **engineering-complete development prototypes, host validation pending.** Not release
candidates.

## Not done in this environment (needs the user, a Mac, ears or recordings)

| Item | Why open | How to close |
| --- | --- | --- |
| Ableton Live + a second host (Gate E) | no Mac, no DAW | `test-instructions.md` §2 |
| Listening: loops, SUSTAIN CHARACTER bloom, filled-key timbre, Natural ↔ Synthetic, Grain presets | no audio playback here | `test-instructions.md` §3 |
| Phrase analysis on real recordings | none in the repository (the corpus is local-only) | `test-instructions.md` §4 |
| CPU on reference hardware | only virtualised Linux numbers | `research-renderer --andor-bench` on the Mac |
| Visual review of the new screens | the 8 screenshots in `screens/` stand in for mock-ups; the direction (OSP's ivory family, vector skin) needs approval | review `screens/` |
| Factory presets with real sounds | needs legally owned recordings | — |

## Phrase

- Monophonic material only; no source separation (chords or mixes give unreliable notes).
- No formant-preserving transposition: large Fill distances change the timbre (the UI says so
  for WIDE / EXTENDED).
- Fast trills (≥ 2 semitones at ~4–6 Hz without plateaus) can be taken for vibrato.
- Variation groups follow keys; the free-text group label exists in the data and edit API
  (`edit::setGroup`) but is not in the editor.
- Selection is one note at a time; "re-analyse selection" re-describes the selected note.
- Waveform zoom is the mouse wheel only (no scroll bar); markers do not snap (fades handle
  clicks).
- `setStateInformation` assumes the message thread (JUCE convention for VST3/AU wrappers).
- Hosts that never call `prepareToPlay` before `processBlock` are not supported (none known).

## Grain

- Imported wavetables are not supported (four band-limited shapes only).
- LFOs restart per voice at note-on (no free-running global mode, no tempo sync); grain pitch
  itself is not a modulation destination (pitch scatter is).
- KEY DNA curves are session state, not host-automatable parameters (the amounts and the seed
  are automatable).
- "Whole source" on a Note Atlas granulates the whole melody from one root: pitch follows
  POSITION by design.
- With DETERMINISTIC off, sequences continue across transport restarts (intended).

## Both

- SPACE (hall) and ECHO (tape, free time) have fixed settings; only send levels are exposed.
- Editor key audition uses a fixed velocity (0.8).
- OSP's ceramic skin pack (12.5 MB) is not embedded: the shared design code draws its vector
  ivory look, as ANDOR Drums does.
- pluginval's Steinberg `validator` sub-step was not run (needs the validator binary).
- Benchmark worst cases in the container show virtualisation spikes (single blocks of
  ~3–8 ms); means are far under budget. Judge on real hardware.
