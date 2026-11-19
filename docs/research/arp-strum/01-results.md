# ARP STRUM — ST1 to ST5 results (stop for review)

18 November 2026, branch `claude/arp-strum` (on `claude/arp-baseline`, independent of Custom ARP).
Design: `00-audit.md`.

| Gate | What | Commit |
|---|---|---|
| ST1–ST2 | Engine: a bounded pending note-on queue inside `Arpeggiator` (a third kind of due time beside note-offs and steps), the four orders, FREE / SYNC spread, fast-rate protection, gate from each note's onset, cancellation | `0fdf73b` |
| ST3 | Per-card parameters (hint 35), `StrumReader`, OSP's lanes and the shell's lanes (ANALOG), recall, older sessions, no allocation, renderer config | `d8b8f19` |
| ST4 | The STRUM key beside SATELLITE (SATELLITE's component, generalised), three light states, the shared `StrumPanel` | `d70b909` |
| ST5 | CPU, audio examples, this report | this commit |

## Guarantees and how they are tested

- **STRUM off is the original path.** The 18-style event golden (`[arp-style-baseline]`, CHORD
  included in all four scenes) is unchanged.
- **Zero spread is the chord as before:**
  - event-identical (order, times, velocities) in `[unit][strum]`;
  - sample-identical audio through the processor in `[plugin][strum]`.
- **Timing:**
  - the ST0 fixtures (4 and 6 notes at 30 ms, 1 note, SYNC 1/32 at 120 BPM);
  - 44.1, 48 and 96 kHz within a sample;
  - block sizes 7–1024 identical.
- **Never past the next step:** fast rates, swing 100 %, ARP RATE modulation. The setting is kept;
  the panel says how much plays.
- **Gate from each onset.** Legato balanced; a pitch still sounding ends before it plays again.
- **Cancellation:**
  - triggers: release before onset (kept by the pedal), pedal lift, all released, All Notes Off,
    ARP off, STRUM off, CHORD left, transport restart, host jump;
  - a mutation that removes release and panic cancellation fails four checks.
- **Lanes:** LINK strums every lane as A; unlinked, B strummed DOWN 60 ms while A and C play
  together (the shared `CardArpLanes`).
- **State:** defaults off; recall; a session from before opens with STRUM off. The schema
  golden gains exactly the 15 STRUM lines.
- **Shell (ANALOG):**
  - the same `StrumReader`; card A for every product, B and C with per-card lanes;
  - the shell schema test passes;
  - `andor_example_tests` and the visual regression (OSP reference and the example) pass.
- **Real time:** no allocation while strumming in every arrangement (`AllocationScope`).

## CPU

Worst case: CHORD at 1/32 over four octaves with 16 keys held (64 notes a step), RANDOM, the
widest spread (limited by the step), against the same chord unstrummed. Three runs: −0.10 %,
+0.08 %, +0.31 % of one core per 128-sample block. That is within the measurement's noise; the test
requires under 2 %.

## Audio examples

`research/configs/strum-*.json` with `--fixture chords`, engine C, level-matched, then +12 dB
uniformly for listening. Generated audio is not committed.

| File | Source | Settings |
|---|---|---|
| strum-off-reference | pluck | CHORD, no STRUM (the comparison) |
| strum-guitar-alternate | pluck | ALTERNATE, 28 ms, quarters at 92 BPM |
| strum-piano-roll | pluck | UP, 70 ms, 70 BPM |
| strum-harp-sweep | pluck | UP over two octaves, 260 ms, 60 BPM |
| strum-random-pulses | saw | RANDOM, 45 ms, 1/8 at 112 BPM |
| strum-cinematic-sync | vowel | DOWN, SYNC 1/8 at 64 BPM, legato |

## Not done in ST1–ST5 (for the review)

- **FREE SPREAD as a modulation destination.** It is a USER macro candidate as an ordinary
  float parameter, but it is not in the modulation destination list. That would extend
  `mod::Dest`, which belongs with the modulation architecture, so it is left for a decision.
- **Host tests.** No DAW is in this environment. The plugin's processor paths are tested
  headless.
- **Real recordings** for the musical review: the examples use the synthetic test sources.
