# ARP STRUM — ST0 audit and design

18 November 2026. Gate ST0 only: audit, baseline and fixtures, with **no production code**. The spec
is "ANDOR — ARP STRUM & CUSTOM PATTERN RHYTHMIC EDITING", Part I. ST1 needs approval.

## 1. Baseline

The CP0 golden (`tests/regression/ArpStyleBaselineTests.cpp`, `docs/research/custom-arp/00-audit.md`
§2) already freezes every event of all 18 styles, **CHORD included**, in four scenes:
- free running;
- a looping host with swing, gate 120 % and a chord change;
- the modulated clock;
- the sustain pedal.

"STRUM OFF uses the original path, bit-identical" is that test staying green. "STRUM ON with zero
spread" will be checked against the same CHORD events.

## 2. The scheduler today (`src/engine/Arpeggiator.cpp`)

- `advanceTo (offset)` loops over two kinds of due time: the earliest scheduled **note-off**
  (`offTime[128]`, by pitch) and the next **step** (`nextStepTime()`). A note-off wins a tie.
  Everything stays inside the block (`emit` clamps the offset). The loop is bounded by `maxEvents`.
- `fireStep (time)`:
  - moves the clock (synced grid, free, or the ARP RATE accumulator);
  - applies OCTAVES at the cycle start;
  - takes the step's notes from `stepNotes`. CHORD returns every pool note, climbing an octave
    per step over OCTAVES;
  - ends any of those pitches still sounding;
  - then emits each note-on with its note-off at `time + gate × step / rate`.
- Lifecycle points: `startPhrase` (a new phrase, with a seed from the phrase counter),
  `restartPattern` (the host's transport start), `heldChanged` (all released ends the phrase), the
  on/off hand-overs, `allNotesOff`, `reset`, and `syncGridTo` (loops and jumps).

## 3. Proposed engine change (ST1–ST2, for approval)

A **bounded pending note-on queue** inside the same class, as a third kind of due time in
`advanceTo`. It is not a second clock or processor.

| | |
|---|---|
| Storage | `std::array<PendingOn, maxHeld>` (64): a strum never holds more notes than a CHORD step has. Each entry holds its absolute sample time, pitch, velocity, channel, the held key it came from (for release cancellation), the chord step's generation, and its gate in samples. |
| Order at one sample | note-offs, then the step, then pending note-ons. Within the pending note-ons, queue order is strum order. A pitch still sounding is ended right before its delayed note-on (the existing retrigger rule). |
| A chord step | `stepNotes` as today. With STRUM on and more than one note, the order is UP / DOWN (by pitch), RANDOM (a Fisher-Yates shuffle seeded by `Prng::deriveSeed (phraseSeed, chordStep, 'STRM')`) or ALTERNATE (the parity of chord steps since the phrase began). The first note plays at the boundary; note i is due at `S_eff × i / (N − 1)`. |
| Spread | FREE: ms × sample rate / 1000. SYNC: the division × samples per quarter, at the host's tempo, or the free-running tempo when stopped. The values come from `Settings`, read once per chord step (a macro or modulator moving mid-strum changes the next chord). |
| Fast-rate protection | `S_eff = min (S, interval to the next step − margin)`. The interval comes from the scheduler's own quantities (step / rate multiplier, the next step's swing offset, the grid). The margin is 1 ms. Pending note-ons still due when the next step fires are dropped. The user's setting is never changed; the panel shows the effective spread when it is shorter. |
| Gate | Each note's note-off is scheduled when **its** note-on is emitted: `onset + gate × step / rate`. Later notes are not shortened. |
| Cancellation | The queue is cleared on all released, ARP off, STRUM off, a pattern other than CHORD, `allNotesOff`, `restartPattern`, a loop or jump (`syncGridTo` on a discontinuity), `reset`, and lane reconfiguration and preset load (which go through the same calls). Releasing one key drops that key's pending notes (octave copies included) unless the pedal keeps it. A key pressed mid-strum joins the next chord step (the pool is rebuilt at a step, as today). |
| Real time | Fixed arrays only, with a bound on the scan. No allocation, locks or strings. |

**Eligibility.** STRUM applies in v1 only when the pattern is CHORD (later also to a custom
pattern's chord events). For any other pattern the engine path is unchanged. The setting is kept,
and the LED shows "on, unavailable" using the existing suppressed look.

## 4. Parameters (ST3)

One set per lane, following `arpCardSetting`: card A uses `arp.strum.*`, cards B and C use
`arp.cardB.strum.*` and `arp.cardC.strum.*`.

| Parameter | ID | Type, range, default |
|---|---|---|
| ENABLED | `.on` | bool, OFF |
| DIRECTION | `.direction` | choice UP / DOWN / RANDOM / ALTERNATE, UP (append only) |
| TIMING | `.sync` | bool FREE / SYNC, FREE |
| FREE SPREAD | `.ms` | 0–500 ms, skewed for 1–50 ms, 20 ms; a USER macro and modulation destination |
| SYNC SPREAD | `.division` | 1/64, 1/32, 1/16, 1/8, 1/4 with T and D variants, 1/32 (the ARP's rate names) |

- They take the next free version hint, and are added through the shared schema so OSP and the
  shell (ANALOG) both get them.
- Old sessions load with STRUM off.
- FREE and SYNC keep separate values, so switching never loses either.

## 5. Interface (ST4)

- **The key.** A STRUM key just right of SATELLITE on the ARP title row (SATELLITE is at x 566;
  the row has room before the collapse key). It is the same `SatelliteKey` component, made
  reusable: a light part that switches, a name and chevron that open, the same metrics, LED,
  hover and keyboard. It never opens when switched, and never switches when opened.
- **The popover.** Opened through `AnchoredPopoverHost`, the stacking fixed for SATELLITE. It
  holds:
  - a graphite sweep picture (the held chord, or a C major preview drawn as preview);
  - DIRECTION as a segmented control;
  - FREE / SYNC;
  - SPREAD (a mini knob plus a numeric field);
  - the existing close affordance.
- **EDIT A / B / C** chooses whose settings show, as for PATTERN, RATE and OCTAVES.
- **SATELLITE** gets STRUM's real delayed note-ons as ordinary MIDI events, so its BOUNCE + NOTE
  retriggers on each strummed note. Nothing is simulated.

## 6. Timing fixtures for ST1 (to be written as tests)

| Fixture | Expectation |
|---|---|
| CHORD, 4 notes, UP, FREE 30 ms, 48 kHz | Note-ons at +0, +480, +960, +1440 samples from the boundary; note-offs at each onset + gate |
| 6 notes, FREE 30 ms | +0, 288, 576, 864, 1152, 1440 (the total stays 30 ms) |
| 1 note, FREE 30 ms | +0 only; 0 notes: nothing |
| SYNC 1/32 at 120 BPM | Total 62.5 ms (3000 samples at 48 kHz) |
| 1/16 at 120 BPM (125 ms) with 250 ms requested | Effective spread 124 ms; no note-on reaches the next boundary |
| ALTERNATE | UP, DOWN, UP, … from the phrase start; reset by transport restart |
| RANDOM, seed s | The same order on every render; each note exactly once |
| Spread across a block boundary | Block sizes 32, 64, 128, 512, 1024 give identical events |
| Key released before its onset | That attack is dropped; a pedal-held key is kept |
| ARP off, All Notes Off, transport restart, loop jump, all released | No pending note-on survives; no note is left hanging |
| 44.1, 48 and 96 kHz | The same musical timing to within a sample |
| Swing 50 %, ARP RATE modulated | The strum fits the actual next interval |
