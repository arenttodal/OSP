# Pedal Loop PL3: late arming, exits, fast pedalling

One compact state per voice: `off`, `armed`, `looping`. Every move between states is one crossfade (`startPedalCrossfade`), and the latest pedal state wins.
- `setPedal` records the wish.
- A change during a crossfade is taken when that crossfade ends (`pedalUpdate`).
- So nothing queues, and no transition is left half done.

| Spec scenario | What the voice does |
|---|---|
| 1. Pedal up | Nothing. The voice is exactly as before. |
| 2. Pedal before the note | Armed from its first sample (`InstrumentEngine::noteOnLayer`). The attack plays, then the first region loops. |
| 3. Pedal before the loop point | Armed on the region whose leaving point is ahead. The read continues, with no retrigger and no phase reset. |
| 4. Pedal after the last loop point | **Late arming:** a crossfade of up to 50 ms from where the read is into the last region, matched to the note's level there (`levelAt`). Bounded by what is left of the recording; under 2 ms left, nothing. Forward lands on the region's start, Ping-Pong on its end reading back. |
| 5. Pedal after the note ended | The voice is gone; nothing is brought back. |
| 6. Pedal up, key held | Forward reads on into the recording (no splice). Ping-Pong reading back turns forward where it is (a 15 ms mirror crossfade). A late loop crossfades (30 ms) back to where it found the recording, at the recording's level. The note then plays on to its own ending, or to the layer's LOOP walk with LOOP on. |
| 7. Key up, pedal down | The engine's existing `heldByPedal`: the note keeps looping. |
| 8. Key up, then pedal up | `release()`: the loop stops as in 6, the envelope releases, and there is **no release graft** (the note ends where it is). |
| 9. Fast pedalling | The latest state wins (a pedal pumped every 3 blocks for 4 s): no stuck voices, bounded, finite. |
| 10. Chords | Each voice has its own region, read, direction and gain. A shared CC64 never aligns them; the test checks four distinct read heads. |

Steal, All Notes Off, reset and the arpeggiator (which owns CC64 while it runs) keep their existing semantics.

**The bug the lab found.** After Ping-Pong handed a held note back to the layer's own LOOP walk, the walk's next jump inherited the last turnaround's flags. It read backwards and briefly re-entered the loop (−107 dB, inaudible, but wrong).
- Fix: every schedule of the walk clears the pedal flags (`scheduleNextJump`).
- Regression test: "after Ping-Pong hands a held note back…". It fails without the fix.

## Measured (lab, `lab-results.md`)

- **Pedal before the note, key held, pedal up (D/G).** Level steps at exit were 0.0–2.4 dB on tonal sources. The sung phrase reads on into its next note, which is the intent.
- **Late pedal (F).** Entry steps were 0.0–3.0 dB. Turn clicks were at most 2 dB.
- **Chords (J).** Entry steps were 1–3 dB. Every voice ended after the pedal was released (0 voices left in every render).
