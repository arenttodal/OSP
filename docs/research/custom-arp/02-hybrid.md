# Custom ARP Patterns — the hybrid octave experiment (before CP2)

18 November 2026. Asked for after CP1: contour-fit over as many octaves as the gesture needs, then
the whole gesture repeated at higher octave positions where there is room. Lab:
`src/research/ArpPatternLab` (`Mapping::hybrid`, `planHybrid`, `noteFor`); tests `[arp-lab]`; report
`lab/report.md`.

## Algorithm (as built)

1. **D**, the gesture's distinct relative pitches, is R + 1.
2. **Base pool:** the fewest octaves b (1 ≤ b ≤ OCTAVES) whose **distinct** pool notes number at
   least D. Duplicate held pitches count once.
3. **Contour-fit on the base pool.** Rank r plays the r-th note. When even OCTAVES is too small,
   it uses contour-fit's compression (adaptive), and there is a single position.
4. **Positions:** every octave shift (0, 12, 24, 36) at which **every note the gesture uses** is a
   note of the full pool (held × OCTAVES, nothing above 127). They are contiguous from the base
   upwards.
   - Compact voicings give OCTAVES − b + 1 positions.
   - Wide voicings, duplicates and the MIDI top follow from the real pool.
   - No pitch is ever clamped.
5. **Position per cycle:** `cycle mod positions`, advanced at the pattern's own loop boundary
   (the end of its last event), so it returns home after the top. Rests, repeats and split values
   ride along unchanged; only the octave moves.

The requested examples are unit tests:

| Case | Result |
|---|---|
| Two-pitch on a triad, 4 octaves | C E (compact), then up one octave per cycle, four positions, then home |
| Five-pitch ascending on a triad, 4 octaves | Two octaves (C4 E4 G4 C5 E5), then up one and two octaves |
| One held note, 4 octaves | A one-pitch motif climbs four places. A two-pitch motif takes two octaves (A3 A4), then climbs to three places |
| More pitches than the pool holds | Contour-fit's compression, one position |
| Held notes near 127 | Only real pool notes, no clamping |
| Wide voicing (C3 E5) | Positions from the actual pool |

## Results

Mean share of steps moving as captured, every gesture × 7 holds × 1/2/4 octaves:

| Mapping | Direction kept |
|---|---|
| adaptive | 0.87 |
| degree-cycle | 0.84 |
| contour-fit | 0.87 |
| fit-octave-cycles | 0.73 |
| **hybrid** | **0.87** |

- **The hybrid's first cycle is exactly contour-fit's.** It keeps the gesture's size and
  shape. Every later cycle is the same gesture an octave higher.
- It has none of fit-octave-cycles' compression. fit-octave-cycles squeezes five-up into C E E G G
  on a triad; the hybrid takes two octaves and keeps C E G C' E'.
- OCTAVES now always means something:
  - two-pitch on a triad over 4 octaves: C C E C E E | C' C' E' … climbing;
  - bass-pedal: C G C E C C' C E | C' G' C' E' …;
  - wide-jumps keeps its two-octave leaps and then moves up.
- The one case where it does not climb is where the gesture already needs every available note
  (one held note, five pitches, 4 octaves). Compression applies there, as with contour-fit.

## Listening

Each file plays the captured phrase, then **contour-fit**, then the **hybrid**, then
**fit-octave-cycles**, 2 s apart. Each mapping runs over the OCTAVES progression (two bars each):
1. a triad over 4 octaves;
2. one note over 4;
3. a seventh over 2;
4. a six-note voicing over 3.

The gestures are two-pitch, five-up, bass-pedal, thumb-leaps and arch.
`rhythm-demos-hybrid.wav` is the four rhythm demos through the hybrid.

## Recommendation

**The hybrid as the default.** It is contour-fit wherever contour-fit was right, and adds the
octave movement musicians expect from OCTAVES. It costs nothing in shape fidelity and needs no
new control.

- User-facing names: **CONTOUR** (the hybrid) and **DEGREES** (degree-cycle).
- Adaptive, plain contour-fit and fit-octave-cycles stay in the lab only.
- CP2 implements the mapping as one replaceable function, so plain contour-fit can be restored in
  one line if listening prefers it.
