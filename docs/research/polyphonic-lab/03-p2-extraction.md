# P2 — Per-note extraction

**Status:** implemented and measured on the synthetic corpus. Method C extracts the notes of a
controlled C-major chord to within 0.2–0.5 dB of what an oracle mask achieves at the same STFT.
The notes plus the residual equal the mixture to 1e-16.

**No human listening has taken place.** Every number below is a measurement against the corpus's
isolated stems. None of it is a judgement of how the notes sound. The audio is written to
`output/` for that judgement (see Reproduce).

## What was implemented

All files are in `research/polyphonic-lab/polyphonic_lab/`.

- **`extraction/templates.py` (per-note harmonic templates).** For each note event:
  1. average the mixture's magnitude spectrum over the note's first half second;
  2. choose the tuning (±50 cents) and string stiffness *B* whose partials carry the most energy;
  3. find the real peak near each predicted partial `k f0 sqrt(1 + B k²)`;
  4. refit *f0* and *B* by weighted least squares on the peaks no other note can claim
     (`(f_k/k)² = f0² + f0² B k²` is linear in k²).

  Up to 60 partials per note, up to 16 kHz.
- **`extraction/masks.py`**: masks that are finite, lie in [0, 1] and sum to exactly 1 in every
  bin.
  - **A, simple harmonic masks.** Each note owns a comb of window-lobe peaks at its partials,
    inside its activity window. Shared bins are split by comb height, and everything between
    partials goes to the residual.
  - **B, harmonic NMF, then Wiener masks.**
    - One component per partial: a fixed spectral shape (the Hann main lobe at the measured
      frequency) with a free activation over time, zero outside the note's window.
    - A bank of 40 broad, smooth bands models the rest (noise, room, bloom). These are spaced on
      log(1 + f/400 Hz), so that at the bottom they are never as narrow as a low note's partial.
    - KL multiplicative updates, 60 iterations. The dictionary is sparse, so a 5 s phrase costs
      under a second.
    - Masks are the components' power ratios.
  - An optional **spectral-smoothness** step re-divides colliding partials from each note's
    nearest unshared partials (Klapuri). It is **off**: it loses 1.7 dB on chords (see below).
- **`extraction/transients.py`, method C (B plus attacks).** The 170 ms window smears every attack
  and hands the broadband hammer noise to the residual. C fixes this in the time domain, moving
  signal only between estimates:
  1. **Pre-onset gate:** what a note holds before its onset − 5 ms moves to the residual (a 2 ms
     edge).
  2. **Attack allocation:**
     - the residual's short-window STFT (1024 / 128) is shared among notes struck together
       (within 30 ms);
     - each note's share is its own short-window energy, smoothed over a third of an octave;
     - the envelope is 1 at the attack, then decays with τ = 25 ms (120 ms span).
- **Residual strategies** scale the residual model before the Wiener masks:
  - `none`: notes take everything they claim;
  - `conservative`: ×0.5 (C's default);
  - `standard`: ×1 (A's and B's default);
  - `generous`: ×2.
- **`extraction/separate.py`** (the pipeline):
  - mixture-phase reconstruction with an exact inverse STFT;
  - an activity window per note: onset − 70 ms to key release + 1.2 s, cut at a re-strike of the
    same key;
  - a consistency check;
  - `ownership`: the share of the energy under a note's partials that its mask gives it, a
    reference-free hint for P3's confidence;
  - trimmed samples: onset − 20 ms to where the note falls 60 dB below its peak, with 2 ms / 20 ms
    safety fades.
- **`extraction/outputs.py`** writes:
  - `notes/` (playable, trimmed) and `full/` (full length, aligned with the mixture);
  - `residual.wav`, `reconstruction.wav` and `reconstruction-error.wav`;
  - `masks.png`;
  - `extraction.json` (schema `osp-poly.extraction/1`).
- **`evaluation/separation.py`** (measured metrics):
  - SI-SDR;
  - **projection SIR/SAR**: zero-lag orthogonal projections, a simplified BSS Eval, labelled as
    such;
  - pitch error in cents (the estimate against the reference stem, both measured the same way);
  - attack energy above 2 kHz in the first 35 ms, in dB, and the correlation of 1 ms envelopes;
  - log-spectral distance (−60 dB floor);
  - anchors: the **oracle ideal ratio mask** from the true stems (the best any mask can do with
    this STFT) and the unprocessed mixture.

  Ratios are capped at 100 dB.
- **`evaluation/benchmark_extraction.py`** and the CLI commands `osp-poly extract` and
  `osp-poly benchmark extraction [--sweep]`.

## Results

These come from `benchmark extraction --sweep`: 24 fixtures, 80 notes, medians over notes.
"Δ" is the SI-SDR gain over the unprocessed mixture (not defined for single notes).

### Fed by ground-truth events

| Class | Oracle IRM | A | B | **C** | C: SIR / SAR | C: attack dB / env r | C: LSD dB |
|---|---|---|---|---|---|---|---|
| A single notes (7) | exact | 16.7 | 17.9 | **35.0** | 100 / 35.0 | −0.1 / 1.00 | 0.4 |
| B chords (21) | 12.1 | 9.0 | 10.0 | **10.5** | 25.6 / 11.6 | −1.5 / 0.88 | 5.3 |
| C overlaps (16) | 21.5 | 9.9 | 9.7 | **11.9** | 35.9 / 12.8 | −0.6 / 0.96 | 3.4 |
| D phrase (17) | 13.0 | 6.5 | 6.5 | **7.7** | 25.6 / 8.7 | −1.7 / 0.85 | 7.4 |
| E pedal (4) | 10.7 | 8.0 | 6.6 | **7.3** | 21.5 / 8.9 | −0.9 / 0.95 | 7.2 |
| F other families (15) | 6.2 | 0.5 | −0.2 | **1.0** | 14.8 / 3.6 | −1.3 / 0.94 | 12.7 |
| **all (80)** | 13.1 | 8.1 | 8.3 | **8.6** (Δ +15.6) | 25.6 / 10.0 | −1.0 / 0.91 | 5.7 |

All values are SI-SDR in dB, except where a column says otherwise.

- **Pitch:** median |error| 0.01 cents for every method; at most 0.24 cents in the phrase.
- **Consistency:** |mixture − (notes + residual)| is 1.1e-16 in the direct runs (`extract`,
  `build`). The tests assert ≤ 1e-9 for A, B and C. The benchmark's records rounded it to four
  decimals (0.0000); later runs keep six significant digits.
- **Masks:** finite and in [0, 1] everywhere; the sum-to-one error is below 1e-4 in every
  benchmark row, and 1.1e-16 in the direct runs.
- **Speed:** C runs at 0.30× real time (0.9× on the 5 s phrase), single-threaded.

### Fed by the harmonic detector (P1), C

All classes: SI-SDR 9.6 dB on the 72 matched notes. 8 notes were missed and 32 were false; these
are the detector's own errors, mostly in class F.

| Class | SI-SDR |
|---|---|
| Chords | 11.3 |
| Overlaps | 13.6 |
| Phrase | 6.1 |

On the matched notes, extraction quality is the same as with ground truth. Separation is not
what limits the pipeline on piano; transcription errors are.

### Per fixture: the milestone material

| Fixture | Oracle | A | B | C |
|---|---|---|---|---|
| **B-chord-C-major** (C4 E4 G4) | 12.9 | 10.8 | 11.8 | **12.7** |
| B-chord-close-C-D-E | 21.0 | 15.5 | 16.3 | 19.8 |
| C-two-150ms | 30.7 | 16.4 | 17.6 | 28.5 |
| C-sustain-then-entry | 26.1 | 12.0 | 17.2 | 23.7 |
| C-melody-over-chord | 17.6 | 7.9 | 8.0 | 8.5 |
| B-chord-wide-C2-G3-E4-C6 | 8.9 | 6.8 | 6.6 | 6.9 |
| F-guitar-E-major | 2.9 | −2.6 | −4.7 | −4.2 |
| F-voices-A-major | 5.3 | 0.5 | −1.0 | −0.8 |

The C-major chord with method C:

| Note | SI-SDR | SIR |
|---|---|---|
| C4 | 12.7 | 25.7 |
| E4 | 20.3 | — |
| G4 | 12.1 | — |

- That is 15.2 dB better than the mixture, and within 0.2–0.5 dB of the oracle for C4 and G4.
- C4 and G4 share every 3rd and 2nd partial respectively (C4's 3rd partial is 2 cents from G4's
  2nd). At an 8192-point STFT those bins are unresolvable, and that sets the ceiling.

### What the sweep shows

- **Attacks are the largest error in A and B.** On C-two-150ms, 79–89 % of B's error lies in the
  first 60 ms: the hammer is missing, and there is pre-echo up to 85 ms early.
  - C moves that note from 17.6 to 28.5 dB; the oracle is 30.7.
  - Single notes go from 18 to 35 dB.
  - The attack-energy error falls from −2.8 dB (B) to −1.0 dB (C).
  - `images/p2-attack-B-vs-C.png` shows the difference.
- **Spectral smoothness hurts chords.** It loses 1.7 dB on chords and 3–4 dB on the C-major
  chord, because a piano's partial levels follow the hammer's comb rather than a smooth envelope.
  It helps the melody over a chord (+2.7 dB). It stays an option and is off by default.
- **Residual strategy barely matters on piano:** 8.5–8.7 dB across all four strategies. `none`
  is best for low notes (C2: 26.5 vs 23.7 generous). `generous` is best for the phrase
  (8.3 vs 7.4).
  - `conservative` stays the default as a middle ground.
  - The pedal fixture's sympathetic resonance does not come out as residual: it sits on the
    notes' own partials, an octave and a twelfth up. Measured residual SI-SDR against the true
    sympathetic stem is −13 dB (C).

## Images

- `images/p2-masks-C-major.png`: the mixture and the C4/E4/G4/residual masks (method C).
- `images/p2-attack-B-vs-C.png`: C4's first 90 ms, the true stem against B and C.
- `images/p2-si-sdr-by-class.png`: median SI-SDR per class (oracle, A, B, C).

## Limitations and recommendations

- **Synthetic only.** The piano model has exactly the structure the templates assume: stiff
  harmonic partials and a known damper. Real recordings add a soundboard, room, longitudinal
  modes and microphone bleed. These numbers are an upper bound.
- **The C4/G4 class of collision is at the oracle ceiling for this STFT.** Going further needs
  information the magnitude spectrogram does not hold: phase or partial-frequency tracking, or
  a longer analysis. Both are later work, not P3.
- **Guitar and voices fail:**
  - Karplus-Strong decays and stretched partials;
  - formant vibrato moves the partials ±14 cents, so fixed partial positions miss them.

  They need vibrato-aware templates (P5).
- **Pedal:** sympathetic resonance cannot be told apart from the notes by harmonic masking.
- **Recommendation:** use C with the conservative residual for P3. Keep A and B as baselines in
  the benchmark.

## Tests

`pytest`: 35 passed in total (P0 7, P1 12 including the Basic Pitch adapter, P2 11, P3 5). The P2
tests:

- SI-SDR and projection metrics on known signals;
- masks finite, in range and summing to 1 (A, B);
- notes + residual = mixture for A, B and C (≤ 1e-9);
- templates find the tuning and a non-zero stiffness;
- **C major separates into C, E and G:**
  - each note at least 10 dB better than the mixture;
  - SIR above 15 dB;
  - pitch within 2 cents;
- C's attack allocation beats B by more than 3 dB and reduces the attack-energy error;
- deterministic; silence and an empty note list are handled;
- the trimmed sample's bounds and fades, and the written extraction;
- unknown methods and residual strategies are rejected.

## Reproduce

Commit: see the P3 report (P2 and P3 land in one commit). Then:

```bash
cd research/polyphonic-lab
.venv/bin/osp-poly corpus generate
.venv/bin/osp-poly benchmark extraction --sweep --out output/benchmark-extraction    # ~25 min
.venv/bin/osp-poly detect output/corpus/B-chord-C-major/mix.wav --method ground-truth \
    --manifest fixtures/manifests/B-chord-C-major.json --out output/gt
.venv/bin/osp-poly extract output/corpus/B-chord-C-major/mix.wav --notes output/gt/notes.json --method C \
    --manifest fixtures/manifests/B-chord-C-major.json --out output/extract-c-major
```

Listening material from the benchmark is in
`output/benchmark-extraction/examples/<fixture>/<events>-<method>/`:

- `notes/*.wav`
- `full/*.wav`
- `residual.wav`
