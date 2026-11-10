# Pedal Loop PL7: development build and review

**Branch:** `claude/pedal-loop`, shown as a draft PR against OSP's default branch. **Not merged.**

| Milestone | Commit |
|---|---|
| PL0 audit | 39dbfd6 |
| PL1–PL3 engine and lab | bac47b2 |
| PL4–PL5 parameters and UI | 3c869fc |
| PL6–PL7 review, real-time test | this commit |

## 1. Feature

- **PEDAL LOOP per layer:**
  - **Normal:** the pedal as it always was.
  - **Forward:** the pedal holds the note in a forward loop.
  - **Ping-Pong:** the pedal holds it in a forward/reverse loop.
- It sits in the playback-mode key's menu, with nothing new on the card.
- The note's attack plays once, and the loop starts after it.
- A pedal pressed late loops near where the note is, at its level.
- Lifting the pedal with the key held returns to the recording; lifting it after the key ends the note naturally.

The design and every decision are in 00–06.

## 2. Tests (all run here, Linux, GCC 13, Release)

| Suite | Result |
|---|---|
| Core `ctest` (unit, integration, regression, performance-smoke, bootstrap) on the final engine | **5/5 passed** (unit 608 s, integration 914 s, regression 259 s) |
| `[regression]` goldens (Normal byte-identical) | passed, 332,030 assertions |
| Plugin `ctest` (plugin, visual reference, memo lab, the shell products and their 0-px visual regressions, unit, integration, regression, performance, bootstrap) | 9/10 passed on the first full run. The 10th, `plugin`, failed only the `[phase2]` parameter-tail check, which now lists the three new IDs; that case then passed. The plugin run otherwise had 173/174 cases and 733,415 assertions passing. |
| `[pedal-loop]` | 10 cases, 181 assertions: analysis, Normal, Forward, Ping-Pong at four pitches and both turnarounds, mid and late pedal, exits, chords, fast pedalling, All Notes Off and reset, LOOP and Granular and REVERSE, Note Atlas, SYNTH Imprint, the walk regression, real-time |
| `[plugin][pedal-loop]`, `[mode-selector]`, `[phase2]`, the schema | passed: parameters, sessions (older ones open on Normal), the menu, undo, Granular, the no-loop line, Normal byte-identical through `processBlock` |
| Real time | **0 heap allocations** while looping, turning, arming late, leaving, pumping the pedal and All Notes Off (`[realtime]`) |
| macOS (CI) | OSP CI on this branch (macOS 15: build, full ctest, AU/VST3/app); see the PR |

## 3. Performance (`research-renderer --pedal-loop-lab <dir> --bench yes`)

- The piano stand-in, every voice sounding in every mode over the same window, median of 3.
- Share of one core of a shared 4-core VM; this machine varies about ±25 % between runs.
- Full tables are in `benchmark-run1.md` and `benchmark-run2.md`.

| Case | Normal | Forward | Ping-Pong |
|---|---|---|---|
| 48 kHz, 16 voices | 7.7 % | 6.7 % | 6.8 % |
| 48 kHz, 32 voices | 15.2 % | 14.4 % | 14.6 % |
| 96 kHz, 32 voices (run 1 / run 2) | 20.8 / 25.0 % | 30.1 / 20.2 % | 21.9 / 21.2 % |
| 3 layers × 16 voices (run 1 / 2) | 19.7 / 19.9 % | 19.9 / 23.4 % | 19.8 / 18.1 % |
| 16 voices + Imprint (run 1 / 2) | 19.1 / 18.2 % | 19.6 / 18.3 % | 21.0 / 21.3 % |
| Pedal pumped every 20 ms (run 1 / 2) | 9.0 / 8.4 % | 5.8 / 7.0 % | 6.2 / 10.1 % |

**No systematic cost beyond the noise.** The loop adds a direction, a reflection or turnaround test, and an 8 ms crossfade a few times a second. Idle cost is unchanged: Normal returns at once. The two runs disagree in both directions about as much as the modes differ, so the only reliable statement is "within measurement noise". A quiet machine (a real Mac) should confirm it.

## 4. Screenshots

- `screens/pedal-loop-menu-oneshot@1x.png`: Ping-Pong chosen.
- `screens/pedal-loop-menu-granular@1x.png`: the section inactive in Granular.

## 5. Audio

- `research-renderer --pedal-loop-lab <dir> [--recordings …]` renders all 212 files (see 06-listening.md). Measurements are in `lab-results.md`.
- A 27-file listening set was handed to the owner. It contains the stand-ins and the owner's piano chord one-shot and organ chords; those recordings are not committed.

## 6. Known issues and limits

- **Listening is pending.** Nothing here replaces the owner's ears on real piano, nyckelharpa, vocal and chord material (06).
- **Percussive hits** loop audibly (turn energy 4–7 dB), as the spec expects: a hit has no body.
- **REVERSE, SPEED, REIMAGINED modes and Granular keep Normal.** Ping-Pong under SPEED plays Forward.
- **A late loop on a fading note stays at the note's level.** After leaving such a loop with the key held, the tail keeps that level (it may sit a little under the original tail).
- **Forward with the layer's LOOP on**, on a sustaining source, is Normal: the continuation walk already holds the note. Forward matters for LOOP off and for decaying sources.
- **The pedal is the engine's single CC64.** Per-channel (MPE) pedals are not separate; that is existing behaviour.
- **The arpeggiator owns CC64 while it runs**, so arpeggiated notes never loop.
- **Benchmarks are from a noisy VM.**

## 7. Rollback

- Do not merge the PR. Nothing reaches OSP's default branch until it is merged.
- After a merge, revert the three feature commits:
  - The parameters are appended (version hint 29). A build without them ignores them in sessions, and the notes play Normal.
  - The schema golden reverts with them.

## 8. Musical review: how to try it

1. Install the development build from the PR's macOS CI artifact (`OSP-macOS-test`). It is unsigned: right-click › Open.
2. Load a recording on card A.
3. Click the playback-mode key (the arrow symbol next to EQ) and choose **PEDAL LOOP › Ping-Pong**, or Forward.
4. Hold the sustain pedal and play:
   - before a note;
   - halfway through a decay;
   - late in a decay;
   - a chord, then lift the keys, then the pedal.
5. Compare with Normal; it must sound exactly as before.
