# Custom ARP Patterns — CP1 adaptive pattern lab (stop for musical approval)

18 November 2026. Headless prototype only: `src/research/ArpPatternLab.{h,cpp}`, run with
`research-renderer --arp-lab <dir>` (36 s). Its unit tests are `tests/unit/ArpPatternLabTests.cpp`
(`[arp-lab]`). The arpeggiator is untouched: the CP0 style golden still passes.

Output:
- `lab/report.md`: every gesture over 1–6 held notes and 1, 2 and 4 octaves for each mapping,
  measured;
- `lab/fixtures/*.mid`: the 12 reference gestures;
- the listening comparisons (sent separately; generated audio is not committed).

## 1. The model the lab settled on

**Capture → contour.**
- Note-ons in order; notes starting within 1/32 of a quarter of each other are one attack.
- Distinct pitches are sorted and ranked (0 = lowest). Repeats, anchors and direction changes
  are kept.
- A chord attack is one step by policy (highest or lowest note), and the drop is counted and
  reported.
- Legato overlaps stay separate steps.
- The spec's example, C3 G3 E3 C3 A3 G3 C3, gives 0 2 1 0 3 2 0 (unit test).

**Variable-duration events (Part II of the STRUM / rhythmic editing spec, built in now so the
format is never equal-step-only).**

| Item | Definition |
|---|---|
| Event | Rank or rest, a duration in integer ticks, and an optional velocity. |
| Ticks per step | 960 per nominal step (one ARP RATE), so halves, thirds, quarters, sixths and eighths are exact. |
| Shortest event | 30 ticks. |
| Onsets | The running sum of durations, so they never drift. |
| Length | Independent of the number of events. |
| Capacity | 128 steps and 256 events. |
| Chord boundary | Version 1 is monophonic. A later chord event holds several ranks in the same struct (the boundary for STRUM on custom patterns). |

**Stage A.** Each note is one step.

**Stage B.**
- Captured onsets are quantised.
- A note lasts until the next note unless a silence longer than half a step follows. Then it
  ends at its captured end and a **rest** fills the gap. A shorter gap is articulation, which is
  GATE's job.
- The loop's trailing silence is kept.

**Editing.** Every operation keeps the pattern's length; only LENGTH changes it.
- SPLIT: two equal halves, recursive down to 30 ticks.
- MERGE: same rank or both rests; different ranks are refused, never discarded.
- EXTEND: only into a following rest.
- REST / NOTE.
- Delete: becomes a rest.
- Reverse, mirror, rotate.
- LENGTH: longer adds silence; shorter only cuts trailing silence, never a note.

**Swing.** It follows the nominal grid. A subdivision keeps its relative place inside its swung
step, so splitting an event never swings the halves on their own.

**GATE.** Applied to each event's own duration. A merged event sustains without retriggering; a
split event retriggers.

## 2. The mappings

| Mapping | Rule | Strength | Weakness |
|---|---|---|---|
| **adaptive-contour** (spec default) | `round (r / R × (N − 1))` | Always reaches the top and bottom of the pool; keeps direction where N ≥ R + 1 | **Stretches every gesture over the whole pool.** With OCTAVES 4 on a triad, the two-pitch rhythm E E B E B B plays C4 C4 G7 C4 G7 G7 (43 semitones), and five-up becomes C4 C5 C6 G6 G7. With fewer held notes than ranks, neighbours collapse (five-up on a triad: C E E G G). |
| **degree-cycle** | `r mod N` | Keeps step-wise motion where the pool is large | Wraps when it is not: the top of a gesture lands at the bottom (wide-jumps on a triad: 0.33 of directions kept) |
| **contour-fit** (lab candidate) | `r` itself while N > R, else adaptive | Keeps the gesture's local shape in pool steps, never stretched: two-pitch on any chord plays the two lowest notes, five-up with 4 octaves plays C4 E4 G4 C5 E5 | OCTAVES does nothing for a short pattern; with fewer notes than ranks it is the same as adaptive |
| **fit-octave-cycles** (lab candidate) | contour-fit over the **held** notes; OCTAVES repeats the whole pattern an octave higher per cycle, as the built-in styles climb | Predictable, like UP over octaves; OCTAVES always means something; the shape stays in the hand | Within a cycle it compresses as soon as fewer notes are held than the gesture has ranks (mean direction kept 0.73 against 0.87) |

**Summary** (mean share of steps moving the same way as captured, every gesture × 7 holds × 1/2/4
octaves): adaptive 0.87, degree-cycle 0.84, contour-fit 0.87, fit-octave-cycles 0.73. No mapping
ever moved a repeated note (repeats stay repeats, by construction). Direction agreement does not
show the stretching problem, which is why `leap` and `span` are in the report: adaptive's leaps
reach 43 semitones where the captured gesture had 7.

## 3. Recommendation (needs your ears)

- **Default: contour-fit.** It is the only mapping that never distorts the gesture's size: the
  same phrase on a triad, a seventh or a six-note voicing sounds like the same phrase.
- **OCTAVES:** fit-octave-cycles behaviour (the pattern repeats, one octave up per cycle). It
  matches how OCTAVES already works for the 18 styles. An alternative is OCTAVES only widening
  the pool (contour-fit as is).
- **Degree-cycle** as the one alternative interpretation (the spec's "experimental"). It gives
  genuinely different lines on small chords.
- **Adaptive-contour** (pure stretch) is not recommended as the default; it could be offered as
  a "SPAN" interpretation later if listening says it is wanted.
- **Stage B and the editing operations** need no further research. They behave as the spec
  describes and are unit tested.

## 4. Listening files

One comparison file per gesture: captured phrase, then adaptive, degree-cycle, contour-fit and
fit-octave-cycles, with 1 s between them. Each mapping plays one bar of each of:
1. one note;
2. an interval;
3. a major triad;
4. a minor triad;
5. a seventh;
6. a five-note extended chord;
7. two bars of maj7 over two octaves.

The settings are 1/16 at 120 BPM, gate 75 %, and the phase runs on across chords. The source is a
synthetic pluck through baseline A, at the same gain for every file. The gestures are bass-pedal,
five-up, two-pitch, thumb-leaps and arch.

`rhythm-demos.wav`, in order:
1. rests (Stage B);
2. five-up edited (SPLIT ×3, REST, LENGTH +1, EXTEND);
3. the same with swing 50 %;
4. two-pitch with its repeated pairs merged.

## 5. Questions for approval (CP2 waits on them)

1. The default mapping: contour-fit?
2. OCTAVES for custom patterns: repeat the pattern per octave (fit-octave-cycles), or widen the
   pool?
3. The second interpretation: degree-cycle?
4. The parameter design from `00-audit.md` §3.3: a separate `arp.custom` bool per lane, so host
   automation of `arp.pattern` keeps its meaning.
