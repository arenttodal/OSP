# SYNTHESIS S2 — Spectral imprint: audit and code map (S2.0)

Branch `claude/synth-imprint`, from `claude/live-capture` at **6e5f503** (the rollback
checkpoint: everything before this phase). Decision taken with the musician: keep today's SYNTH
page (WAVE, OCTAVE, DETUNE, TONE, SHAPE, PITCH) and add ANALOG | IMPRINT to it; Imprint adds
IMPRINT and MOTION and colours the same oscillator.

## 1. Where things are (as of 6e5f503)

| Concern | Where | Notes |
|---|---|---|
| SYNTH amount | `layerA/B/C.synthesize` (hint 22), `InstrumentHost::synthesizeId` | 0..100 %; modulation adds `mod::Dest::synthAmountA..C` |
| SYNTH page settings | `layerX.synth.wave/octave/detune/tone/shape/pitch` (hint 23), `InstrumentHost::synthSettingsFrom` | real-time safe conversion from raw values |
| Oscillator | `src/engine/SynthVoice.h`, `synth::Voice` | PolyBLEP saw (square, triangle, sine), optional detuned pair, TPT 12 dB low-pass (Q 0.6, cutoff = pitch x 4..8 by velocity x TONE), envelope Held / Follow |
| Blend law | `synth::blendGains`, `Blend::hybrid` | sample whole to 35 %, oscillator a^1.5, power-normalised |
| Level calibration | `InstrumentVoice::start`, `synthLevel` | the member's typical level (loudest 20 ms and body median) over `referenceRms` |
| Per-voice control | `InstrumentVoice::synthControl` (every control period, 32 samples) | amount smoothing 20 ms, pitch (key x intentional bends, PITCH Recording follows the track), `setPitch` |
| Per-sample mix | `InstrumentVoice::mixSynth` | after the source read (One Shot, continuation, Granular), before the voice's shelves, CHARACTER, DAMPEN, ADSR, pan |
| Lifetime | `synthHoldsVoice`, `sourceEnded` + `isSilent` | a One Shot held past its end keeps its voice for the oscillator |
| Settings into voices | `InstrumentEngine::setLayerSynth` → `Slot::synthLive` → `InstrumentVoiceStart::synthSettings` (pointer, read live) | |
| The source's analysis | `InstrumentModel::analysis` (pitch track, envelope, spectral summary) | one model per source, **one per Atlas member** (`InstrumentSet`, `groupFor`): the voice's `currentModel` |
| Existing spectral envelope | `analysis/spectrum/SpectralEnvelope` (`computeSpectralEnvelope`) | cepstrally smoothed, energy-weighted STFT (4096), 50 Hz-10 kHz in 10-cent steps, dB mean-removed; already run in `InstrumentBuilder::findResonances` |
| Immutable per-model extras | `InstrumentModel::reimagined` (`shared_ptr<const ...>`, built in stage 2) | the pattern for a spectral descriptor |
| Model lifetime | `ModelExchange`, voices hold their model; replaced models retire off the audio thread | a descriptor inside the model cannot disappear under a voice |
| Granular / KEY DNA | the voice's granular path, then `mixSynth` on the granular output (`wetL/R`) | the oscillator is per voice, not per grain |
| SYNTH chip | `shared/andor-ui/Source/EngineCard.{h,cpp}`, `SynthChip` (a Slider) | today: drag anywhere = amount; click without drag opens the page; double-click = 0; right-click = development audition |
| SYNTH page | `shared/andor-ui/Source/EqEditor.{h,cpp}`, `SynthEditor` | opens in the well; one well page at a time (EQ, NOTES, SYNTH, CAPTURE) |
| State | APVTS parameters (appended, version hints), schema golden `tests/plugin/golden/osp-parameter-schema.txt`, the Phase 2 tail test | |
| Benchmarks | `osp_tests "[synthbench]"`, `osp_plugin_tests "[synthbench]"` | |

## 2. Integration proposal

- **The spectral descriptor lives in the model.** A `std::shared_ptr<const spectral::Imprint>`
  in `InstrumentModel`, built by `InstrumentBuilder` off the audio thread from the same audio
  every other stage uses:
  - every Atlas member gets its own, from its own region, through the existing builder;
  - it is versioned, derived (never persisted, never touching the audio) and rebuilt with the
    model wherever a model is built (load, preset, session, portable instrument, relink).
    So a saved patch reproduces its sound without any machine-specific cache.
- **The voice colours its oscillator.** `synth::Voice` keeps its oscillator, filter and envelope
  unchanged. In Imprint mode with IMPRINT above 0, its output passes through a bank of band
  filters whose gains come from the descriptor at control rate. In Analog, or at IMPRINT 0,
  the bank is not run: the arithmetic is S1's exactly.
- **New per-layer parameters** (appended, hint 28): `layerX.synth.mode` (Analog / Imprint),
  `layerX.synth.imprint` 0..100 %, `layerX.synth.motion` 0..100 %. They travel with the SYNTH
  page's settings (`synth::Settings`), so no new plumbing is needed.
- **SYNTH chip:** split into two zones. The label (with a small chevron) opens and closes the
  page; the amount drags, wheels, Alt-fine and double-click resets. No other main-view change.

## 3. Risks

| Risk | Handling |
|---|---|
| Analog must stay bit-identical | the imprint stage is a separate branch, entered only in Imprint with IMPRINT > 0; tests compare renders sample for sample |
| SYNTH 0 % must stay bit-identical | nothing new runs before the oscillator does (it does not run at 0 %) |
| Old sessions | the new parameters default to Analog / 0 / 0 |
| Model size | a descriptor is a few kB (bands x frames of floats) per model; an Atlas of 50 members adds about 100-200 kB |
| Build time | one more envelope pass over each model's audio (the builder already does one); measured in S2.1 |
| CPU | a bank of 16-32 biquads per sounding voice in Imprint, only while it sounds; measured in S2.1 against the 5 ns per sample oscillator |

## 4. Reference material

The musician's recordings are not in this container (the corpus is git-ignored). Agreed: the
experiments run on built-in stand-ins (formant vowels, a bowed tone with bow noise, a pluck, a
granular pad, an Atlas phrase), the choice is made on measurements, and the real-sound verdict
is the musician's, in the Mac build.
