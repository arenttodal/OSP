# SYNTHESIS S2 — the spectral imprint: results and how to test it

The SYNTH oscillator can now take on the recording's own spectral envelope: its body,
formants, and bow or breath colour. The synthetic voice then keeps the recording's identity.
- The audit and the code map are in `s2-imprint-audit.md`.
- The three experiments and the choice are in `s2-imprint-research.md`.

This build is for musical review. The numbers below come from built-in stand-ins, not your
recordings, so the musical verdict is yours.

## 1. What changed

| | |
|---|---|
| **SYNTH chip** | Two parts. **SYNTH ›** (the label and its chevron) opens the page with one click. **The amount** is the fader: drag, wheel, Alt for fine, double-click for 0. A click on the amount no longer opens the page. While the imprint colours the oscillator, the chip's small light becomes a small spectral curve. The chip is 12 px wider, so the chevron and "100 %" have room. |
| **SYNTH page** | `ANALOG \| IMPRINT` beside the page's name. In Imprint, **IMPRINT** (0–100 %) and **MOTION** (0–100 %) join the controls: after PITCH in one card, after DETUNE in two, and on a row of their own in three. The wave is drawn through the imprint, so its colour can be seen. While a source's imprint is being built, the page says "Preparing spectral imprint…"; when none can be made, it says "Imprint unavailable — playing Analog". |
| **Parameters** | Per source: `layerX.synth.mode` (Analog / Imprint), `.imprint` and `.motion`. Version hint 28, appended after everything else. Automatable, undoable and saved with the session. Defaults: Analog, 0, 0. |
| **Engine** | Every model (one source, each set member, each Atlas member) gets its own profile, built off the audio thread where the model is built. Each voice runs a 24-band filter cascade on its oscillator, only while it sounds in Imprint with IMPRINT above 0. |

## 2. What was reused

| Reused | Notes |
|---|---|
| S1's oscillator | Its wave, filter, envelope, blend law and calibration are unchanged; the imprint colours its output. |
| The SYNTH page | The existing page, extended. |
| The model pipeline | `instrument::addContinuation`, so the loader, sets, Atlas Stage B and portable instruments all get profiles without new plumbing. |
| Model lifetime | `ModelExchange`: a profile cannot disappear under a voice. |
| Profile analysis | The cepstral smoothing idea of `SpectralEnvelope`. |
| UI building blocks | `EqValueField` and the page's segment choices. |

## 3. Which algorithm won

**B, the time-varying imprint.** It covers A: MOTION 0 is exactly the static imprint. The
channel vocoder (C) cost 2.5–3 times as much. It also needed the sample to be sounding, so a
held pluck lost its colour when the recording ended, and it was not clearly better. The
evidence is in `s2-imprint-research.md`. C is kept in `SpectralImprint` for research only.

## 4. What sounds better (on the stand-ins)

Timbre distance to the recording at its own pitch, at SYNTH 100 %, through the engine
(lower is closer):

| Stand-in | Analog | Imprint (MOTION 0) | Imprint (MOTION 70 %) |
|---|---|---|---|
| Cello (12 dB crescendo) | 7.0 | 4.2 | **3.2** |
| Nyckelharpa | 2.73 | 2.60 | **2.45** |

Other results:
- The level stays within 1.5 dB of Analog, and pitch is unchanged.
- **What Imprint changes follows the recording's own body:**
  - the change correlates with that body: 0.36 for the cello, 0.78 for the nyckelharpa;
  - in a set with two different bodies, each note follows its own member's body and moves
    away from the other's.
- **MOTION matters most on a sound whose colour changes during the note.** The cello opens
  as it swells; there MOTION takes the distance from 4.2 to 3.2.

## 5. Uncertainties

- **The vocal stand-in an octave below its recording:** Imprint measures worse than Analog
  there (7.8 against 4.8 in the research). The profile, read from harmonics 330 Hz apart, lifts
  formant troughs that a lower key reveals. If this is audible on real voices, the remedies are
  a profile from several keys (an Atlas has them) or less IMPRINT far below the recording.
- **Metallic or robotic character, smearing, and "one sound, not two"** are listening
  questions; the numbers do not answer them.
- **CPU:** see the table below. It costs nothing in Analog, at IMPRINT 0, or at SYNTH 0.

  | Engine alone, this build machine (shared x86-64, 48 kHz) | SYNTH 0 | Analog 100 % | Imprint | Imprint, MOTION 50 % |
  |---|---|---|---|---|
  | 1 voice | 0.32 % | 0.30 % | 0.97 % | 0.91 % |
  | 8 voices | 2.0 % | 2.6 % | 7.6 % | 8.4 % |
  | 16 voices | 3.9 % | 4.8 % | 14.5 % | 14.2 % |
  | 32 voices | 10.4 % | 12.3 % | 34.2 % | 31.2 % |
  | 8 voices, Granular | 1.6 % | 2.0 % | 6.3 % | 6.5 % |

  - That is about 120–150 ns per voice per sample, and Apple Silicon should be faster.
  - The cascade is a chain, each band feeding the next, so SIMD across bands does not help.
  - If it is too heavy, the next step is SIMD across voices: process four voices' cascades
    together.
- **Only stand-ins were tested here;** your recordings were not in the build container.

## 6. How to test it

1. Load a sustained source (a voice, a bowed instrument), set SYNTH to about 60 %, then click
   **SYNTH ›** to open the page.
2. Switch **ANALOG | IMPRINT** to Imprint and raise **IMPRINT**. The wave on the page takes on
   the recording's colour; play and compare with Analog (A/B with the switch).
3. Raise **MOTION** on a sound that changes over the note (a swell, a vowel change). At 0 the
   colour is the recording's average; higher follows it through the note.
4. Try SYNTH 100 % (the oscillator alone): with Imprint it should still sound like the source's
   body.
5. Try a held pluck (One Shot, Held): the colour stays after the recording has ended.
6. Try an Atlas, or a set of several notes: each note takes its own member's colour.
7. Check the chip: click the label (opens), drag the amount, double-click the amount (0).
8. Reopen a session saved before this build: it plays as before (Analog).

Developer harness:
- `osp_tests "[imprint]"` runs the unit and engine tests.
- `osp_tests "[imprintbench]"` runs the CPU benchmark.
- `OSP_IMPRINT_RESEARCH=<dir> osp_tests "[imprint-research]"` writes the renders and the
  measurements.
- `OSP_REFINE_DIR=<dir> osp_plugin_tests "[refine-shots]"` writes the screenshots
  (`08-imprint-*`).

## 7. Rollback

- `claude/live-capture` at **6e5f503** is everything before S2. Without this branch, sessions
  that use Imprint open as Analog: their extra parameters are ignored.
- Within this branch, Analog is S1 sample for sample. Setting every source to Analog turns the
  imprint off completely.
