# Listening lab, round 1: results and decisions

One listener rated the seven tabs of the lab on 2026-10-02. Each take was rated 1–5
on three scales. The tables show the mean of the three scales; "best" is the take the
listener picked. Groups left unrated were "not noteworthy or poor examples". The
vocal source is set aside as a poor reference, at the listener's request; the violin
is the reference for sustained sources. The raw ratings are in the lab page's database
(collection `ratings`).

## Decisions

| Tab | Finding | Change |
|---|---|---|
| Sustain | Without the vocal, multi-loop scores best: 18.3 points against 17.7 with movement, 17.0 for the best single loop and 16.7 for the naive loop. Movement hurt the steady organ (4.67 → 3.67) and helped the tremolo (4.67 → 5.00). | Multi-loop stays. Movement is now proportional to the recording's own fluctuation, so the organ (0.4 cents, 1.4 dB) barely moves. Re-test: **Sustain 2**. |
| Repetition | LIFE 50 % and 80 % beat identical retriggers on every non-vocal source (3.3–5.0 against 2.3). Tagel and violin preferred 80 %, the pluck 50 %. | Default LIFE 35 % → **50 %** (Natural 45 %, Alive 70 %). |
| Dynamics | Sustained sources (bowed, violin) preferred velocity as level only (5.0); the sax liked none of them. The pluck preferred the full model at 100 % (5.0). | The dynamic model's strength follows the source: 0.25× for sustained sources, up to 2× for plucks. Re-test: **Dynamics 2**. |
| Original ↔ Reimagined | "Cannot hear the difference" on three of four sources. Measured: 100 % moved the average spectrum by only 0.7–2.3 dB. | From 35 % a slowly moving doubling head; resonators 2.4× stronger with up to 5 s decay; from 50 % a second bank a fifth above (harmonic remapping). Re-test: **Reimagined 2**. |
| Multi-sample | The Tagel set was "by far the most real and dynamic" (4.33). The plucks set scored 1.0: notes from different pitch groups jumped by up to 19 dB. | Fixed. Sets are now levelled per (pitch group, layer) by onset loudness, and round-robin takes keep their own differences. The plucks phrase's note-to-note spread fell from 4.0 dB to 1.4 dB. Re-test: **Multi-sample 2**. |
| Transients | Preservation lost on the pluck (3.67 → 1.0), bowed (3.67 → 1.67) and violin (5.0 → 3.67), and won only on Tagel (2.0 → 3.0). | **Off by default**, kept as an option. |
| Pick & velocity | Split: the pluck preferred attack gain, Tagel transient mixing. | Stays off; the simpler engine wins ties. |

## Scores

### continuation-1

| group | A single loop (naive) | B best loop, matched crossfade | C multi-loop | D multi-loop + movement | best |
|---|---:|---:|---:|---:|---|
| bowed | 4.00 | 4.00 | 4.00 | 4.00 | C |
| organ | 5.00 | 4.00 | 4.67 | 3.67 | A |
| synth | 3.67 | 5.00 | 5.00 | 5.00 | D |
| tremolo | 4.00 | 4.00 | 4.67 | 5.00 | D |
| vocal | 1.33 | 4.67 | 3.33 | 3.00 | B |

### continuum-1

| group | R25 Reimagined 25% | R75 Reimagined 75% | best |
|---|---:|---:|---|
| organ | — | 5.00 | R75 |
| pluck | 4.67 | — | R25 |
| vocal | — | 4.67 | R75 |

### dynamics-1

| group | A gain only | B gain + velocity filter | C dynamic model, DYNAMICS 50% | D dynamic model, DYNAMICS 100% | best |
|---|---:|---:|---:|---:|---|
| bowed | 5.00 | 4.67 | 4.00 | 3.67 | A |
| pluck | 4.67 | 4.00 | 4.00 | 5.00 | D |
| sax | 3.00 | 4.00 | 3.00 | 2.00 | — |
| violin | 5.00 | 4.00 | 3.67 | 4.00 | A |
| vocal | 4.33 | 4.00 | 4.33 | 5.00 | D |

### dynamics-2

| group | A attack gain (current) | B transient mixing | best |
|---|---:|---:|---|
| pluck | 4.33 | 3.33 | A |
| tagel | 2.33 | 4.00 | B |

### multisample-1

| group | A single file, plain sampler | B single file, OSP engine | C whole set, OSP engine | best |
|---|---:|---:|---:|---|
| plucks | 2.00 | 3.33 | 1.00 | B |
| tagel | 1.00 | 2.67 | 4.33 | C |

### performance-1

| group | A identical retrigger | B independent randomisation (baseline B) | C performance engine, LIFE 50% | D performance engine, LIFE 80% | best |
|---|---:|---:|---:|---:|---|
| pluck fast | 2.33 | 2.00 | 4.33 | 3.00 | C |
| pluck steady | 2.33 | 3.00 | 3.67 | 3.00 | C |
| tagel fast | 2.33 | 3.33 | 4.00 | 4.33 | D |
| tagel steady | 2.33 | 2.33 | 3.33 | 4.00 | D |
| violin fast | 2.33 | 2.33 | 4.67 | 5.00 | C |
| violin steady | 2.33 | 3.67 | 4.33 | 4.67 | D |
| vocal fast | 2.33 | 3.67 | 1.00 | 2.33 | B |
| vocal steady | 2.00 | 4.33 | 2.00 | 3.00 | B |

### transients-1

| group | A transposed attack | B transient preservation | best |
|---|---:|---:|---|
| bowed | 3.67 | 1.67 | A |
| pluck | 3.67 | 1.00 | — |
| tagel | 2.00 | 3.00 | B |
| violin | 5.00 | 3.67 | A |