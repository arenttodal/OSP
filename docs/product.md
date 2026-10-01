# Product philosophy

> **Drop in a sound. Play an instrument.**

OSP turns a single tonal recording — an old synth note, a vocal vowel, a bowed
violin or nyckelharpa note, an organ, a pluck, a harmonic, a strange processed
texture — into an immediately playable, expressive instrument across the keyboard.

The goal is **not** maximum acoustic realism. The goal is maximum musical beauty,
playability, expression and inspiration while retaining the DNA of the original
recording. Think of the immediacy and charm of an old Casio VSS-style sampling
keyboard, rebuilt with modern DSP. Creative reference territory: Bon Iver, Radiohead,
Sigur Rós, cinematic experimental sampling, acoustic/electronic hybrids.

The first chord should be the sales pitch. Success is a musician saying *"I need to
make something with this"*, not *"that is technically impressive pitch shifting."*

## Principles

**One sample is enough.** Open, drop one WAV/AIFF, wait briefly, play — and it must
already be genuinely useful. No key-mapping, no zones, no preparation.

**More samples make it smarter (later).** Several pitches, dynamics or round robins
dropped together are inferred automatically (register anchors, velocity layers, real
variation). No conventional sampler mapping workflow is ever required.

**Beauty over strict realism.** When perfect reconstruction and a consistently more
beautiful result disagree, the musical result wins — but artifacts must be intended
or controlled, never accidents of poor DSP. The engine should know what a faithful
version would sound like so it can depart from it intelligently.

**Instant playability.** Audio becomes provisionally playable as soon as a root
estimate exists; deeper analysis continues in the background. Target: a 5–10 s
recording meaningfully playable within ~1–2 s on Apple Silicon.

**Sustained material is first class.** Holding a note or chord far longer than the
recording must work and must not expose an obvious loop (Continuation Engine).

**Simple musician-facing interface.** A waveform, a detected root (editable), an
Original ↔ Reimagined control and five macros (LIFE, DYNAMICS, CHARACTER, MOTION,
SPACE). No keyzones, modulation matrices, FFT or granular settings in the primary UI.

**Fail beautifully.** Ambiguous pitch shows `ROOT: ?` and asks for a key; no stable
loop means another continuation strategy; failed harmonic analysis falls back toward
sample playback. The app should almost never say "unsupported sound".

**Offline and private.** Everything works without a network connection; user audio
never leaves the machine unless a future feature is explicitly opted into.

## The four engines (long-term)

1. **Performance Engine** — "if this sound were performed again, what could change?"
   Correlated alternate performances; repeated notes feel alive; velocity changes
   timbre, not only gain.
2. **Continuation Engine** — "what happens after the recording ends?" Indefinite,
   non-repeating sustain; release grafting.
3. **Register Engine** — pitch and register are different things. Excellent within
   ±12 semitones, useful around ±24; extra pitch samples teach register behaviour.
4. **Transformation Engine** — a continuum from faithful reproduction to creative
   reinterpretation that keeps the source's identity.

Phase 0 builds none of these. It builds the scientific environment needed to develop
them: a deterministic headless renderer, analysis, fixed MIDI fixtures, a corpus
runner, golden regressions and benchmarks — and the two baselines every future idea
must beat (A: plain resampler, B: naive randomised sampler).

## Non-goals

Not Kontakt, not Falcon, not a modular sampler, drum machine, wavetable synth, effects
rack, DAW, spectral editor or cloud AI service. No mandatory accounts, no hundreds of
parameters, no "AI variations" that are really independent random pitch/filter/volume.

## Roadmap (from the v1.0 specification)

| Phase | Content | Exit condition |
|---|---|---|
| 0 | Research harness + baseline engine | whole corpus analysed/rendered without crashing |
| 1 | Playable JUCE sampler (AU/VST3/Standalone, drag & drop, root detection, 24 voices) | any stable tonal corpus sample is a working chromatic instrument |
| 2 | Pitch bake-off (resampling vs Signalsmith Stretch vs formant-aware) at ±12/±24 | default pitch strategy chosen by blind listening |
| 3 | Continuation MVP (sustain detection, multi-loop, crossfades, release graft) | organ, vowel and bowed sources hold 60 s without obvious looping |
| 4 | Performance MVP (correlated performance vector, memory, LIFE) | repeated-note tests prefer engine over identical repeats |
| 5 | Dynamic synthesis (velocity → transient/spectrum/noise/resonance) | a crescendo on one sample is clearly more than gain |
| 6 | Original ↔ Reimagined continuum | useful sounds across the whole range |
| 7 | Multi-sample intelligence + Samples inspector | small related sets improve automatically |
| 8 | Commercial polish | presets, export, installers, host certification, Windows |
