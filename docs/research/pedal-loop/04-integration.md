# Pedal Loop PL4: integration

| System | Behaviour | Evidence |
|---|---|---|
| **CC64** | The engine's one `pedalDown`, unchanged (JUCE's ≥ 64 threshold, binary). A pedal change also calls `voice.setPedal (down)` on key-held voices; released-under-pedal voices keep `heldByPedal`; pedal up still calls `release()`. One release per voice; no second sustain layer. | `[unit][pedal-loop]` |
| **Normal** | `setPedal` returns at once; `ppDir` and `xDir` stay exactly 1, so the read's arithmetic is unchanged. | `[regression]` goldens (332,030 assertions), full suites |
| **LOOP** (the layer's own) | Forward with LOOP on, on a source that sustains: Normal (the walk already holds it). Ping-Pong takes the read while the pedal is down and gives it back to the walk. | "Forward on a note the layer's LOOP already holds…", "after Ping-Pong hands a held note back…" |
| **Note Atlas / sets** | Each member has its own regions (analysed with its own continuation). A voice keeps its member and its loop for its life. | "Note Atlas members each loop over their own body" |
| **SYNTH / Imprint** | A looping read never ends, so `sourceEnded` and `synthHoldsVoice` never trigger under the pedal. The oscillator follows its own envelope with no new attack. | "with SYNTH (Imprint) the oscillator carries on…" |
| **KEY DNA, LIFE** | Fixed in the note's shape at note-on; nothing is re-rolled at a turn. | by construction (`NoteShape`) |
| **Granular, REVERSE, REIMAGINED modes, SPEED** | Normal. The setting is kept and has no effect. Ping-Pong under SPEED plays as Forward. | "Granular and REVERSE keep Normal" |
| **DAMPEN, CHARACTER, MOVEMENT, SPACE** | After the read, as before. The loop is in the source read only. | — |
| **Arpeggiator** | Owns CC64 while it runs (existing hand-over), so its notes never loop and never stick. | existing `[arp]` tests |
| **Steal, All Notes Off, reset** | Unchanged semantics: fade, release, kill. | "All Notes Off and a reset end looping notes" |
| **Live mode change** | A voice keeps the mode it started with (like a REIMAGINED mode); new notes take the new one. | by construction |

## Host parameters and sessions

- **`layerA.pedalLoop`, `layerB.pedalLoop`, `layerC.pedalLoop`:**
  - a choice of Normal / Forward / Ping-Pong;
  - **version hint 29**, appended after the imprint's (`InstrumentHost::addPedalLoopParameters`);
  - **default Normal**.
- `tests/plugin/golden/osp-parameter-schema.txt` grows by these three lines only (an intentional append; IDs 0–433 unchanged).
- The `[phase2]` tail check lists them.
- They are ANDOR OSP's own: the shell schema test lists them with OSP's other own parameters, so ANDOR Analog's schema is untouched.
- **Sessions and presets** save and recall the choice. A session from before has no such parameter and opens on Normal (`[plugin][pedal-loop]`). Undo works as for any parameter. Host automation reaches new notes.

## Gate

Existing instruments sound unchanged with the feature off:
- Normal renders byte-identical: `[plugin][pedal-loop]` renders the same session before and after switching modes back to Normal, and checks the hashes.
- Core suite 5/5.
- Plugin suite: every case passed except the `[phase2]` tail check, which needed the new IDs listed (fixed).
