ONE-SAMPLE PERFORMANCE INSTRUMENT
Product, DSP, UX & Engineering Specification — v1.0
Status: Build specification
Target: Commercial macOS-first audio instrument; cross-platform architecture
Formats: AU, VST3, Standalone
Core stack: C++20, JUCE, CMake
Primary use case: Turn a single tonal recording into an immediately playable, expressive, inspiring instrument.
1. PRODUCT THESIS
1.1 The promise
Drop in a sound. Play an instrument.
The source may be:

* an old synthesizer note
* a vocal vowel
* a bowed violin or nyckelharpa note
* an organ
* a sustained texture
* a strange field recording
* a plucked instrument
* a harmonic
* a processed/frozen recording
* a bass
* essentially any reasonably tonal source

The software analyzes the recording and automatically constructs a playable instrument across the keyboard.
The goal is not maximum realism.
The goal is:
maximum musical beauty, playability, expression and inspiration while retaining the DNA of the original recording.
A useful reference philosophy is the immediacy and charm of old sampling keyboards such as the Casio VSS-30, reimagined with modern spectral processing, pitch transformation, performance modeling and continuation DSP.
The desired emotional benchmark is an instrument that could plausibly inspire artists working in the territory of Bon Iver, Radiohead, Sigur Rós and experimental cinematic/indie production.
The first chord should be the sales pitch.
2. PRODUCT PRINCIPLES
2.1 One sample must be enough
The central product promise cannot depend on preparing multisamples.
A user should be able to:

1. Open plugin.
2. Drag one WAV/AIFF onto it.
3. Wait briefly while it analyzes.
4. Play.

That must produce a genuinely useful result.
2.2 More samples make it smarter
The application must also accept multiple recordings.
If the user drops:
`C2.wav`
`C4.wav`
`C6.wav`
the engine should infer that these are register anchors.
If the user drops:
`C4_soft.wav`
`C4_medium.wav`
`C4_hard.wav`
it should infer a dynamic relationship.
If the user supplies:
`C4_RR1.wav ... C4_RR12.wav`
it should learn real variation from those recordings.
The workflow remains automatic.
No conventional sampler key-mapping workflow should be required.
3. NORTH-STAR EXPERIENCE
A user drops a 7-second vocal vowel into the plugin.
The interface displays:
ANALYZING…
Then approximately:
A3 · SUSTAINED · VOCAL / HARMONIC
The user plays a minor chord two octaves below the original note.
It sounds huge and beautiful rather than simply like slowed-down audio.
They hold the chord for 30 seconds.
The sound continues moving without exposing an obvious sample loop.
They play repeated notes.
No two attacks are completely identical.
Velocity changes more than volume.
They move REIMAGINED upward.
The voice gradually becomes an evolving spectral instrument while retaining recognizable DNA from the original recording.
That interaction is the product.
4. SUCCESS DEFINITION
The software succeeds when musicians respond:
“I need to make something with this.”
rather than:
“That is technically impressive pitch shifting.”
Musical preference outranks numerical fidelity.
Whenever there is a conflict between perfect reconstruction and a consistently more beautiful result, the musical result should generally win.
However, artifacts should be intentional or controlled, not accidental consequences of poor DSP.
The engine should understand what a faithful version would sound like so it can depart from that intelligently.
5. SOURCE PRIORITIES
Optimization priority:
Tier 1

* analog/digital synth tones
* sustained acoustic instruments
* bowed instruments
* vocal vowels
* sustained textures

Tier 2

* plucked tonal instruments
* harmonics
* unusual resonant objects

Tier 3

* basses
* short tonal percussion
* complex hybrid material

Percussion-only material is not the principal use case.
6. RESEARCH CORPUS
The supplied initial research corpus contains 80 recordings, including 79 WAVs and one AIFF.
It spans:

* 44.1 and 48 kHz sources
* mostly stereo material
* approximately 0.7–28.8 second durations
* repeated real performances
* multi-register plucks
* violin round robins
* bowed nyckelharpa
* sustained/tremolo material
* Trampeorgel
* Prophet 6
* other synth sources
* bass
* saxophone
* vocals
* vocal vowels
* processed/frozen recordings

This corpus becomes the initial internal DSP benchmark.
It must remain available as a regression corpus throughout development.
7. FOUR FUNDAMENTAL SYSTEMS
The product should internally be understood as four cooperating systems.
7.1 Performance Engine
Answers:
“If this sound were performed again, what could change?”
Responsibilities include:

* attack differences
* excitation
* brightness
* noise
* micro-pitch
* decay
* resonant excitation
* articulation
* performance memory
* contextual repetition

7.2 Continuation Engine
Answers:
“What happens after the recording ends?”
Responsibilities include:

* intelligent loops
* multiple loop regions
* loop interpolation
* granular continuation
* spectral continuation
* partial continuation
* stochastic/noise continuation
* modulation continuation
* release reconstruction

7.3 Transformation Engine
Answers:
“How far can this instrument move away from the recording while retaining its identity?”
Responsibilities include:

* spectral manipulation
* formant transformation
* harmonic remapping
* resonator excitation
* transient transformation
* partial manipulation
* granular reconstruction
* noise regeneration
* creative instability

7.4 Register Engine
Answers:
“How should this source behave when played far above or below its recorded pitch?”
Pitch and register must not be treated as the same thing.
A real instrument changes timbre with register.
The engine should model:
`requested pitch`
plus
`register-dependent character`
plus
`stationary resonances`
plus
`excitation behaviour`
plus
`noise spectrum`
8. INTERNAL SOURCE TYPES
Do not expose hard instrument types to the user.
Analysis should return probabilities such as:

```
transient_tonal        0.08
sustained_harmonic     0.71
expressive_sustain     0.63
periodic_modulation    0.42
noise_component        0.21
vocal_likelihood       0.66
```

These descriptors determine DSP blending.
Important internal behavior families:
Sustained tonal
Synths, organ, stable vowels.
Expressive sustained
Bowed strings, saxophone, voice.
Periodic/evolving
Tremolo, pulsating synths, unstable organ.
Transient tonal
Plucks, pizzicato, struck resonant objects.
Synthetic
Prophet, analog synth, Reese, etc.
Hybrid
Anything that does not classify cleanly.
Never force uncertain material into one category.
Use blended strategies.
9. APPLICATION FLOW
EMPTY
Primary screen:
DROP A SOUND
Secondary:
WAV · AIFF
Optional small button:
LOAD EXAMPLE
ANALYZING
Immediately show the waveform.
Audio should become provisionally playable as soon as a basic root estimate is available.
Deeper analysis continues in the background.
Show unobtrusive progress:
`Understanding pitch…`
`Finding movement…`
`Building sustain…`
Avoid technical terminology such as FFT, harmonic model or transient decomposition.
READY
Display approximately:
C3
SUSTAINED · TONAL
Root pitch is clickable and editable.
The central musical controls become active.
10. PRIMARY UI
The application should feel like an instrument, not a sampler editor.
The primary screen contains:

* sample waveform / visual representation
* detected root note
* Original ↔ Reimagined control
* five macro controls
* preset name
* source/sample menu
* minimal settings/menu access
* optional miniature keyboard/activity visualization

Do not expose:

* keyzones
* modulation matrix
* hundreds of parameters
* FFT controls
* granular settings
* separate oscillator pages
* sampler programming concepts

11. PRIMARY MACROS
Working names:
LIFE
Controls how different successive performances may become.
At zero:

* essentially repeat original performance
* minimal divergence

At medium:

* realistic/organic variation

At high:

* increasingly expressive performances

At maximum:

* creative but related reinterpretations

LIFE influences:

* transient state
* micro-pitch
* spectral variation
* noise excitation
* resonance excitation
* timing
* decay
* articulation state
* repetition behavior

It must not simply multiply random ranges.
DYNAMICS
Controls how strongly MIDI velocity transforms performance.
Velocity should affect:

* amplitude
* attack intensity
* spectral tilt
* upper harmonic energy
* excitation noise
* transient sharpness
* resonance excitation
* damping
* subtle pitch behavior

At Dynamics = 0:
velocity mainly affects volume.
At high values:
velocity becomes a meaningful performance dimension.
CHARACTER
Controls spectral/body transformation.
Possible internal mappings:

* formant position
* spectral-envelope transformation
* harmonic weighting
* resonant-body amount
* excitation/body ratio
* partial warping
* source-size perception

Character should remain useful across acoustic, vocal and synthetic material.
MOTION
Controls evolution after note onset.
Influences:

* sustain drift
* grain movement
* spectral movement
* partial fluctuations
* modulation depth
* tremolo evolution
* pitch drift
* stereo motion
* stochastic movement

Motion should be almost inactive on short plucks unless useful.
It becomes extremely important for pads and sustained sources.
SPACE
A musical finishing macro rather than a generic wet/dry reverb knob.
May influence:

* retained source ambience
* stereo width
* decorrelation
* diffusion
* short reflections
* derived resonant tail
* subtle modulation
* optional algorithmic ambience

Keep processing subtle at default.
12. ORIGINAL ↔ REIMAGINED
This is not a wet/dry knob.
It controls model depth.
Original end
Favor:

* source playback
* conventional resampling where attractive
* conservative pitch transformation
* source envelopes
* minimal generated material
* subtle performance changes

Middle
Introduce:

* transient/body separation
* formant compensation
* harmonic/noise separation
* generated sustain
* stronger performance synthesis
* resonance reconstruction

Reimagined end
Allow:

* harmonic remapping
* stronger formant shifts
* partial reconstruction
* granular continuation
* spectral evolution
* resonator re-excitation
* modified transient generation
* controlled instability
* alternate synthetic sustain

The original source DNA must remain perceivable.
13. ADVANCED PANEL
The default UI remains simple.
A collapsible Advanced panel may expose:

* Root Note
* Fine Tune
* Attack
* Release
* Sustain Behavior
* Pitch Character
* Formant
* Stereo
* Velocity Curve
* Variation Seed
* Quality
* Polyphony

Do not expose raw DSP internals.
Advanced parameters should mostly refine macro behavior.
14. AUDIO IMPORT
MVP:

* WAV
* AIFF

Later:

* FLAC

Support:

* mono
* stereo
* 44.1 kHz
* 48 kHz
* 88.2 kHz
* 96 kHz
* common integer and floating PCM formats

Internally convert analysis data to a canonical representation while preserving original audio.
Never destructively normalize the source file.
15. ANALYSIS PIPELINE
All expensive analysis happens outside the audio thread.
Pipeline:

```
IMPORT
 ↓
Decode
 ↓
Silence / bounds analysis
 ↓
Peak + loudness envelope
 ↓
Onset analysis
 ↓
Pitch / F0 analysis
 ↓
Pitch stability
 ↓
Spectral descriptors
 ↓
Harmonic / residual estimation
 ↓
Transient estimation
 ↓
Spectral-envelope estimation
 ↓
Modulation analysis
 ↓
Sustain-region detection
 ↓
Loop candidates
 ↓
Resonance estimation
 ↓
Stereo analysis
 ↓
Source-behavior descriptors
 ↓
Instrument Model
```

16. BASIC ANALYSIS FEATURES
Compute at minimum:
Pitch

* median F0
* frame-level F0
* confidence
* pitch deviation
* pitch contour
* vibrato rate/depth where relevant

Use a robust monophonic algorithm such as YIN/MPM or an equivalent custom estimator.
Do not require a heavyweight transcription model.
Amplitude

* peak envelope
* RMS/loudness envelope
* attack slope
* decay shape
* dynamic range

Spectrum

* spectral centroid
* rolloff
* spectral flux
* spectral flatness
* harmonicity
* spectral envelope
* high-frequency decay
* low-frequency/body energy

Temporal

* onset
* transient duration
* stable sustain regions
* decay
* release
* repeated modulation

Stereo

* channel correlation
* width
* phase stability
* frequency-dependent width where useful

Do not collapse stereo recordings unnecessarily.
17. PITCH DETECTION
If confidence is high:
automatically assign root note.
If confidence is moderate:
assign it but show the root clearly.
If confidence is low:
show:
ROOT: ?
and allow the user to select a key.
The app must remain usable for ambiguous sources.
Never reject a source merely because F0 detection fails.
18. SOURCE DECOMPOSITION
Conceptually model a recording as:

```
TRANSIENT
+
HARMONIC BODY
+
STOCHASTIC / NOISE
+
RESONANCE
+
AMBIENCE
```

These do not necessarily need to become separate rendered audio files.
They may exist as spectral models.
The key requirement is that the engine can manipulate these components differently.
19. TRANSIENT SEPARATION
Transients should not necessarily receive the same pitch transformation as the tonal body.
Example:
A plucked string transposed +12 semitones should not automatically make every broadband finger/nail component sound exactly half-sized.
Candidate techniques:

* STFT spectral-flux masks
* HPSS-style median filtering
* onset-region transient mask
* transient/body crossfade
* time-domain transient preservation

This requires listening bake-offs.
20. HARMONIC MODEL
For sufficiently tonal material, estimate:
`f0`
and partials approximately:
`fₙ(t), Aₙ(t), φₙ(t)`
Track:

* amplitude
* frequency deviation
* decay
* spectral envelope
* partial stability

Do not assume perfect harmonicity.
Bells, metallic objects and strange sounds require inharmonic partial support.
21. STOCHASTIC MODEL
Estimate residual energy not explained by tonal partials.
Examples:

* bow noise
* breath
* finger noise
* synth noise
* room/noise floor
* attack noise

Represent using a time-varying filtered stochastic process rather than merely looping the original noise.
This becomes particularly useful during long generated sustains.
22. SPECTRAL ENVELOPE / FORMANTS
Estimate spectral-envelope behavior separately from F0.
Candidate approaches:

* cepstral smoothing
* LPC
* harmonic-envelope interpolation
* STFT envelope fitting

The engine needs independent control of:
note pitch
and
apparent source size / resonant envelope
Perfect formant preservation is not always the musical goal.
23. PITCH ENGINE
Pitch processing should be hybrid.
Never assume one pitch-shift algorithm is best for every source.
Three useful branches:

```
A — RESAMPLE
B — SPECTRAL / FORMANT-AWARE SHIFT
C — HARMONIC / RESYNTHESIS SHIFT
```

The source model chooses/blends them.
24. RESAMPLING BRANCH
Classic resampling often sounds excellent, especially near the source pitch.
Advantages:

* immediate
* coherent
* transient-friendly
* characterful
* reminiscent of vintage samplers

Do not “improve” it automatically when it already sounds better.
This branch is particularly important to the VSS-inspired identity of the instrument.
25. MODERN PITCH-SHIFT BRANCH
For initial implementation, benchmark Signalsmith Stretch.
It is an embeddable C++ pitch/time library, supports wide pitch-shift ranges and includes formant controls; its repository currently uses the MIT license.
Also benchmark Rubber Band because it provides high-quality real-time/offline pitch processing, spectral-envelope/formant preservation and RT-safe processing modes. Proprietary commercial distribution requires an appropriate commercial license rather than relying on its GPL licensing.
A later commercial bake-off may include zplane élastique; its monophonic mode is specifically intended for single-voice material and formant-preserving pitch shifting.
Do not commit permanently to one vendor before listening tests.
26. PITCH-RANGE TARGET
Desired quality:
±12 semitones
Primary quality zone.
Should routinely sound excellent.
±24 semitones
Creative usable zone.
Should still be musically worthwhile.
Beyond ±24
Allowed.
Do not hard-limit playback.
Degradation should happen gracefully and, ideally, interestingly.
The engine may increasingly transition toward re-synthesis farther from the source.
27. REGISTER ANCHORS
For CPU-efficient high-quality processing, investigate offline creation of transformed anchor representations.
Potential anchor layout:

```
-24
-12
  0
+12
+24 semitones
```

Then use high-quality interpolation/resampling between nearby anchors.
Do not blindly render five complete 30-second stereo files if a decomposed representation can achieve the same result with lower memory.
Optimize after quality is established.
28. REGISTER INTELLIGENCE
When multiple pitches are supplied:

1. detect root pitch of each source
2. compare timbre
3. group related samples
4. infer spectral changes across register
5. interpolate those behaviors
6. extrapolate cautiously outside supplied range

This allows the engine to learn:
`brightness(register)`
`resonance(register)`
`noise(register)`
`decay(register)`
`formant(register)`
rather than merely choosing nearest samples.
29. PERFORMANCE ENGINE
A performance is represented by a bounded state vector such as:

```
P = {
    force,
    transientShape,
    attackBrightness,
    bodyBrightness,
    noiseAmount,
    pitchDeviation,
    pitchSettling,
    damping,
    resonanceExcitation,
    timing,
    stereoBias
}
```

These parameters must be correlated.
Do not draw each from independent random distributions.
30. PERFORMANCE CORRELATION
Example:
A higher-force performance might produce:

```
transient energy      ↑
upper harmonics       ↑
noise excitation      ↑
body resonance        ↑
initial pitch shift   slightly ↑
damping behavior      changed
```

Those relationships should move together.
Initial v1 correlations may be handcrafted using measurements from the supplied real-performance corpus.
Later versions can learn these relationships statistically.
31. PERFORMANCE MEMORY
Humans do not produce independent random performances.
Maintain a slowly changing hidden performance state.
Example:

```
0.31
0.36
0.42
0.37
0.45
0.40
```

rather than:

```
0.02
0.91
0.11
0.73
0.05
```

Possible implementation:

* bounded random walk
* Ornstein-Uhlenbeck process
* low-frequency correlated noise
* state-dependent sampling

32. REPETITION AWARENESS
Track:

* most recent note
* repeated-note count
* inter-onset interval
* recent velocity
* previous performance vector

Fast repeated notes should deliberately avoid obvious duplicate attacks.
Variation behavior should depend upon repetition rate.
For example:
rapid repetition may increase attack differentiation while reducing implausible large pitch drift.
33. DETERMINISM
DAW playback and offline bounce should be reproducible.
Use a deterministic PRNG.
Seed from:

* stored instrument seed
* voice/note information
* deterministic note-event counter

Expose a hidden/advanced:
RESEED
control.
Saving/loading a project must recreate the same performance behavior.
34. VELOCITY / DYNAMIC SYNTHESIS
Velocity must not equal volume alone.
For a one-sample instrument:

```
velocity
 ↓
performance intensity
 ↓
amplitude
transient
spectral tilt
noise
harmonics
resonance
damping
pitch transient
```

The transform should be source-dependent.
35. MULTI-VELOCITY LEARNING
When multiple recordings of the same pitch appear to represent different strengths:
derive descriptors for each.
Fit relationships such as:
`velocity → spectral centroid`
`velocity → attack energy`
`velocity → noise`
`velocity → decay`
`velocity → resonance excitation`
Then interpolate.
Filename metadata should help when present:
`pp`
`p`
`mp`
`mf`
`f`
`ff`
`soft`
`medium`
`hard`
Do not rely solely on filenames.
36. CONTINUATION ENGINE
Sustains are a first-class feature.
A user must be able to drop a finite recording into the plugin and hold a note or chord far longer than that recording.
The engine should support indefinite sustain.
37. CONTINUATION STRATEGIES
The continuation engine can blend:
Traditional looping
Use waveform and spectral similarity to find strong loop candidates.
Multi-loop continuation
Maintain several compatible loop regions and move among them.
Granular continuation
Select source regions with controlled grain scheduling.
Spectral continuation
Continue stable spectral distributions without replaying exact waveform sections.
Harmonic continuation
Resynthesize stable tracked partials.
Stochastic continuation
Regenerate bow/breath/noise components.
Modal continuation
Maintain or re-excite detected resonances.
Different sounds use different combinations.
38. LOOP DETECTION
Score candidate loop points using:

* waveform similarity
* phase compatibility
* spectral similarity
* F0 similarity
* amplitude-envelope compatibility
* stereo compatibility
* distance from unstable onset/release material

Build multiple candidate regions rather than finding only one magical loop.
39. MULTI-LOOP ENGINE
A major improvement over conventional sustain loops:
construct perhaps 3–8 compatible continuation regions.
During long holds:

* choose/crossfade among them
* prevent immediate repetition
* subtly alter timing
* preserve overall source state

This can dramatically reduce loop recognition before more sophisticated resynthesis is even implemented.
40. GRANULAR CONTINUATION
Grains should be source-aware.
Do not simply randomize grain selection.
Potential grain descriptors:

* pitch
* amplitude
* spectral centroid
* phase
* noise/harmonic ratio
* position in modulation cycle

Choose grains compatible with current voice state.
Granular behavior should primarily support continuity, not sound like a granular synth unless Reimagined is increased.
41. MODULATION EXTRACTION
Sustained recordings may contain:

* vibrato
* tremolo
* bow fluctuation
* organ instability
* synth LFO
* filter movement

Estimate slow trajectories from:

* F0
* amplitude
* centroid
* harmonic distribution
* stereo position

Separate predictable periodic behavior from stochastic drift.
Continuation should maintain the character without repeating the exact original cycle forever.
42. TREMOLANDO
Tremolo sources require special care.
Do not loop:
`cycle A → cycle A → cycle A`
Extract approximately:

* modulation frequency
* depth
* cycle instability
* spectral changes
* timing irregularity

Generate future cycles with bounded deviation.
43. RELEASE GRAFTING
Whenever possible, preserve the real release/tail from the source.
When the user releases a note after an artificially prolonged sustain:

1. select an appropriate original release/tail region
2. match current amplitude/spectrum
3. phase/crossfade appropriately
4. transition into the original ending

This can make synthesized long sustains feel much more organic.
44. PERFORMANCE + CONTINUATION INTERACTION
Each new sustained note should not necessarily enter the same sustain state.
The Performance Engine should initialize:

* attack
* pitch movement
* brightness
* body state
* sustain trajectory

Then the Continuation Engine extends that particular performance.
This is essential.
Otherwise the attacks vary but every note eventually converges into the same loop.
45. TRANSFORMATION ENGINE
Transformation should preserve a notion of source identity.
Candidate transformations include:

* spectral-envelope movement
* formant displacement
* harmonic weighting
* harmonic thinning
* partial detuning
* partial locking
* nonlinear frequency mapping
* modal excitation
* transient/body ratio
* residual/noise regeneration
* spectral blur
* spectral freeze
* grain-cloud continuation
* controlled phase dispersion
* subtle saturation/nonlinearity

Avoid generic “big FX rack” design.
Transformation should operate on the internal source model.
46. MODAL / RESONANCE MODEL
Investigate detection of relatively stationary resonant peaks.
Conceptually:

```
EXCITATION
 ↓
RESONANT BODY
 ↓
OUTPUT
```

This is particularly relevant for:

* strings
* plucks
* acoustic objects
* organ-like instruments
* resonant synth patches

Possible implementation:
a bank of damped resonators:
`yᵢ[n] = resonator(fᵢ, Qᵢ, gainᵢ)`
derived from stable spectral peaks.
Then vary excitation while preserving body identity.
This may become a major proprietary-feeling part of the system.
47. SAMPLE-SET INTELLIGENCE
When multiple files are dropped simultaneously:
calculate:

* F0
* duration
* loudness
* spectral fingerprint
* onset behavior
* filename tokens
* source similarity

Then construct clusters.
Potential roles:

* pitch anchor
* round robin
* velocity anchor
* alternate articulation

Inference confidence must be retained.
48. SAMPLE-SET INSPECTOR
Default workflow remains automatic.
A small optional Samples drawer displays inferred structure.
Example:

```
C3
 ├ Medium
 │  ├ Take 1
 │  ├ Take 2
 │  └ Take 3

C4
 ├ Soft
 └ Hard
```

Allow drag/reassignment if the automatic inference is wrong.
This is the only mapping UI the product should need.
49. VOICE ENGINE
Initial target:
24-voice polyphony
Configurable later.
Each voice contains:

* note
* velocity
* sample/register source
* pitch transform
* transient state
* performance state
* continuation state
* envelope
* per-note expression
* deterministic variation state

Voice stealing must favor oldest/quietest/released voices and avoid obvious clicks.
50. MIDI
Required:

* Note On/Off
* Velocity
* Sustain pedal
* Pitch Bend
* Mod Wheel
* Channel Pressure
* CC mapping for primary macros

51. MPE
Support MPE in v1 if it does not delay the core DSP milestone.
At minimum architecture must be MPE-safe from day one.
MPE permits per-note pitch/timbre/expression through channel-per-note style control and remains broadly supported across modern controllers and hosts.
Map:
Pressure
→ performance intensity/dynamics
Slide / CC74
→ Character or Motion
Per-note pitch bend
→ voice pitch
Exact defaults can be UX-tested.
52. PRESETS
A preset stores:

* source asset hashes
* source-set structure
* analysis descriptors
* instrument model
* macro values
* advanced parameters
* variation seed
* engine version

Preset loading should be fast.
Do not rerun analysis unnecessarily.
53. SAMPLE STORAGE
Do not depend solely on the original filesystem location.
When a sample is imported:

1. calculate SHA-256/content hash
2. copy or canonicalize into application-managed sample storage
3. reference by hash
4. preserve original filename as metadata

This prevents broken DAW sessions when the original file moves.
54. PORTABLE INSTRUMENT FORMAT
Later create a single portable package, e.g.:
`.instrumentname`
Internally:

```
manifest.json
source/
analysis/
model/
preview/
```

Exporting this package should make an instrument transferable between computers.
55. PLUGIN STATE
Use JUCE `AudioProcessorValueTreeState` for automatable parameters and thread-safe UI/parameter binding; JUCE explicitly supports this pattern for plugin parameter/state storage.
Do not store large raw sample blobs directly as ordinary parameter state.
Store identifiers and model metadata.
56. UNDO / REDO
Support undo/redo for:

* dropping/replacing samples
* root note changes
* macro values
* sample-set reassignments
* advanced parameters

DAW parameter automation itself remains host-managed.
57. BUILD STACK
Use:
C++20
JUCE
CMake
Target initially:

* macOS arm64
* macOS Intel if economically justified
* AU
* VST3
* Standalone

JUCE supports the necessary plugin/application formats and provides suitable plugin, MIDI, synthesizer and parameter-state infrastructure.
Windows VST3 follows once macOS core behavior is stable.
Do not write platform-dependent DSP.
58. REPOSITORY STRUCTURE
Recommended:

```
/
├ CMakeLists.txt
├ README.md
├ CLAUDE.md
├ docs/
│  ├ architecture.md
│  ├ dsp.md
│  ├ testing.md
│  └ product.md
│
├ apps/
│  ├ plugin/
│  ├ standalone/
│  └ research-renderer/
│
├ src/
│  ├ core/
│  ├ audio/
│  │  ├ Voice/
│  │  ├ Pitch/
│  │  ├ Performance/
│  │  ├ Continuation/
│  │  ├ Transformation/
│  │  └ Resonance/
│  │
│  ├ analysis/
│  ├ model/
│  ├ presets/
│  ├ midi/
│  └ ui/
│
├ tests/
│  ├ unit/
│  ├ dsp/
│  ├ regression/
│  └ performance/
│
├ research/
│  ├ python/
│  ├ renders/
│  └ reports/
│
└ third_party/
```

Keep DSP modules decoupled from JUCE where practical.
Core DSP should be testable from a command-line renderer without loading a DAW.
59. IMMUTABLE INSTRUMENT MODEL
Analysis produces an immutable `InstrumentModel`.
Conceptually:

```
struct InstrumentModel {
    SourceAsset source;
    AnalysisData analysis;
    PitchModel pitch;
    PerformanceModel performance;
    ContinuationModel continuation;
    TransformationModel transformation;
    RegisterModel registers;
};
```

Playback voices read from a shared immutable model.
When analysis finishes:

* construct new model off-thread
* atomically swap model reference
* existing voices may finish against previous model

Avoid mutating large shared DSP structures from the audio thread.
60. REAL-TIME SAFETY
The audio callback must perform:
NO

* file I/O
* heap allocation
* mutex locking
* networking
* sample analysis
* model building
* blocking calls

Preallocate voice state.
Use lock-free or atomic handoff for model changes.
Rubber Band, for example, documents RT-safe behavior under its normal real-time processing conditions; whichever pitch engine is selected must satisfy equivalent constraints in our integration.
61. THREAD MODEL
Audio thread

* MIDI
* voices
* prepared DSP
* output

UI/message thread

* interface
* parameter changes
* drag/drop

Analysis worker

* decoding
* descriptors
* source decomposition
* loop analysis
* model generation

Render/cache worker

* optional pitch anchors
* derived sustain data
* preview generation

62. ANALYSIS EXPERIENCE
Target:
A typical 5–10 second recording should become meaningfully playable within roughly 1–2 seconds on contemporary Apple Silicon.
Treat this as a product target, not an absolute promise across every machine.
Use staged analysis:
Stage 1

* decode
* root
* onset
* provisional playable sampler

Stage 2

* continuation
* deeper spectral model

Stage 3

* resonance / performance descriptors
* optional cache generation

The musician should not have to wait for every analysis stage before touching the keyboard.
63. DSP QUALITY MODES
Potential internal modes:
Draft
during rapid editing.
Normal
default playback.
High
offline/freeze/export if needed.
Avoid requiring the musician to think about this unless CPU becomes an issue.
64. CPU TARGET
Initial benchmark machine:
Apple Silicon Mac.
At:

* 48 kHz
* 128-sample buffer
* 16 sustained voices
* default macros

target average audio processing time below roughly 25% of available callback duration, with substantial safety margin on worst-case callbacks.
24 voices should remain practical for typical patches.
Performance tests must measure callback timing, not only overall process CPU.
65. MEMORY
Typical instrument target:
<300 MB active memory.
Avoid enormous pre-render banks.
Cache only derived data that materially improves performance.
Provide a disk-cache limit eventually.
66. DSP RESEARCH HARNESS
Before refining the GUI, build a command-line/offline renderer.
Input:

```
source.wav
test.mid
config.json
```

Output:

```
baseline.wav
engine.wav
analysis.json
metrics.json
```

This lets DSP development happen independently of plugin UI.
67. STANDARD TEST MIDI
Every meaningful DSP revision must render the same suite.
Repetition
Eight identical notes.
Tests performance variation.
Dynamics
Same note at increasing velocities.
Tests dynamic synthesis.
Register
Play root ±12 ±24 semitones.
Tests pitch/register behavior.
Melody
Fixed melodic phrase.
Tests contextual behavior.
Chords
Close and wide chords.
Tests polyphony and phase/pitch interactions.
Long Hold
60-second single note.
Tests continuation.
Long Chord
60-second chord.
Tests continuation under polyphony.
Repeated Sustains
Repeated 5–15 second notes.
Tests whether each performance evolves differently.
68. BASELINES
Every listening test includes:
A
ordinary sample playback/resampling
B
basic randomized sampler
C
current engine
This is essential.
The product must demonstrate value over something musicians already have.
69. ROUND-ROBIN GROUND TRUTH
For a real RR set:
Choose one recording as:
`SOURCE`
Hide remaining recordings from the algorithm.
Generate alternate performances.
Compare generated performances against real ones.
Measure distributions of:

* attack
* brightness
* F0 trajectory
* spectral envelope
* loudness
* decay
* noise/harmonic ratio

Do not optimize solely to match those numerical distributions.
Use them to prevent obviously implausible behavior.
70. MULTI-PITCH GROUND TRUTH
Hide all but one register sample.
Generate other registers.
Compare generated behavior with real recordings.
This evaluates the Register Engine.
71. SUBJECTIVE TESTING
The principal test remains listening.
For each source ask:
A/B preference
Which version would you rather play?
Identity
Does it still feel related to the original recording?
Repetition
Do repeated notes become annoying?
Sustain
Can you hear an obvious loop?
Register
Does the sound collapse when moved an octave?
Inspiration
Does the processing create musically useful surprises?
The final question carries significant weight.
72. GOLDEN AUDIO REGRESSION
Maintain fixed input/preset/seed combinations.
Render after DSP changes.
Use automatic measurements to detect:

* silence
* NaN/Inf
* clipping
* unexpected level changes
* incorrect duration
* major spectral regressions
* non-determinism

Do not demand sample-identical output after intentional algorithm changes.
73. AUTOMATED TESTING
Unit-test:

* pitch utilities
* interpolation
* envelopes
* PRNG determinism
* parameter smoothing
* voice allocation
* serialization
* loop selection
* sample hashing

Integration-test:

* sample import
* analysis
* preset recall
* DAW state recall
* file relocation
* sample-rate changes
* mono/stereo
* offline rendering

74. HOST TEST MATRIX
macOS v1:

* Logic Pro
* Ableton Live
* Reaper

Add:

* Cubase
* Studio One

before broad commercial release.
Test:

* AU
* VST3
* validation
* automation
* project reload
* freeze
* bounce
* offline rendering
* sample-rate switching
* buffer-size switching
* MIDI sustain
* plugin duplication

75. SAMPLE-RATE TEST MATRIX
At minimum:

* 44.1
* 48
* 88.2
* 96 kHz

Buffer sizes:

* 32
* 64
* 128
* 256
* 512
* 1024

No crash or pitch change may occur when host sample rate changes.
76. GUI PERFORMANCE
UI target:

* smooth high-DPI rendering
* responsive waveform
* 60 fps when actively animating
* no unnecessary constant repainting
* scalable interface

Support approximately:
80–200% UI scaling.
77. FAILURE PHILOSOPHY
The engine will encounter inputs it cannot understand.
Failure must be graceful.
If pitch confidence is poor:
still create a playable texture.
If no stable loop exists:
use alternative continuation.
If harmonic analysis fails:
fall back toward sample/granular behavior.
If spectral resynthesis sounds worse:
favor original playback.
The app should almost never display:
“Unsupported sound.”
The desired philosophy is:
Fail beautifully.
78. MVP DEFINITION
The first meaningful MVP is not merely a GUI sampler.
It must prove the product thesis.
MVP must include:

* AU/VST3/Standalone
* drag/drop
* automatic root detection
* manual root correction
* chromatic polyphonic playback
* excellent baseline pitch engine
* sustain support
* intelligent loop/continuation v1
* velocity-to-timbre behavior
* LIFE variation system
* five macros
* Original ↔ Reimagined
* presets
* session recall
* deterministic playback
* background analysis
* test renderer
* supplied corpus test suite

79. MVP CONTINUATION
MVP sustain does not need full sinusoidal or neural resynthesis.
Start with:

1. intelligent stable-region detection
2. multiple loop candidates
3. randomized/nonrepeating loop scheduling
4. high-quality crossfades
5. subtle spectral/level drift
6. release grafting

If that produces excellent 30–60 second holds, ship it before replacing it with something theoretically more sophisticated.
80. MVP PERFORMANCE MODEL
Start with handcrafted correlated performance profiles.
For transient sources:

* transient gain
* transient brightness
* tiny start variation
* spectral tilt
* micro-pitch
* decay
* noise
* resonance

For sustained sources:

* attack behavior
* F0 trajectory
* brightness
* modulation depth
* sustain movement
* noise level

Use the real supplied recordings to choose realistic ranges.
81. MVP TRANSFORMATION
Start with musically strong operations:

* resample ↔ formant-aware blend
* spectral-envelope shift
* harmonic weighting
* transient/body balance
* subtle resonance enhancement
* continuation drift
* controlled stereo transformation

Do not start by writing a giant granular/spectral engine.
82. PHASE 0 — RESEARCH HARNESS
Build first.
Deliverables:

* repository
* CMake
* CLI renderer
* audio loader
* analysis JSON
* MIDI test playback
* deterministic rendering
* baseline sampler
* corpus runner

Exit condition:
All 80 current corpus files can be batch analyzed/rendered without crashing.
83. PHASE 1 — PLAYABLE SAMPLER
Build:

* JUCE plugin
* AU
* VST3
* Standalone
* drag/drop
* root detection
* waveform
* MIDI
* 24 voices
* ADSR internally
* standard resampling
* project state

Exit condition:
Dropping any stable tonal corpus sample produces a functioning chromatic instrument.
84. PHASE 2 — PITCH BAKE-OFF
Integrate candidates:

* native resampling
* Signalsmith
* Rubber Band test build if licensing/testing permits
* optional commercial algorithm evaluation

Render corpus ±12/24 semitones.
Create blinded listening comparison.
Exit condition:
Choose default processing/blending strategy by listening, not assumption.
Signalsmith is a particularly practical first candidate because it is a small C++ integration with broad pitch range and formant controls.
85. PHASE 3 — CONTINUATION MVP
Implement:

* sustain detection
* loop candidates
* loop scoring
* multi-loop selection
* crossfading
* modulation preservation
* release graft

Exit condition:
Trampeorgel, vocal vowel and bowed examples can sustain for 60 seconds without an immediately obvious repetitive loop.
86. PHASE 4 — PERFORMANCE MVP
Implement:

* performance vector
* correlation matrix
* deterministic variation
* state memory
* repetition awareness
* LIFE

Calibrate against:

* Tagel set
* Plucks set
* violin RR set

Exit condition:
Repeated-note listening tests consistently prefer the engine over identical-repeat baseline.
87. PHASE 5 — DYNAMIC SYNTHESIS
Implement:

* velocity → transient
* velocity → spectrum
* velocity → noise
* velocity → resonance
* velocity → damping
* DYNAMICS macro

Exit condition:
A crescendo played on one sample sounds materially different from merely changing playback gain.
88. PHASE 6 — ORIGINAL ↔ REIMAGINED
Introduce:

* multiple pitch/render branches
* spectral-envelope transformation
* transient/body mixing
* harmonic manipulation
* sustain reconstruction depth
* Character/Motion coupling

Exit condition:
Useful sounds exist across the entire continuum rather than only at the endpoints.
89. PHASE 7 — MULTI-SAMPLE INTELLIGENCE
Implement:

* group import
* pitch clustering
* RR inference
* velocity inference
* register model
* Samples inspector

Exit condition:
Dropping a small related source set automatically produces a better instrument with no mandatory manual mapping.
90. PHASE 8 — COMMERCIAL POLISH
Implement:

* preset browser
* portable instrument export
* factory examples
* onboarding
* UI scaling
* accessibility pass
* crash recovery
* performance tuning
* host certification matrix
* installer/signing/notarization
* Windows VST3

91. POST-V1 DSP RESEARCH
Do not block v1 on these.
Investigate:
Sinusoidal + residual synthesis
Partial tracking plus stochastic residual.
Modal body inference
Automatically infer resonant structures.
Learned performance priors
Learn how real performances vary.
Learned register priors
Learn how acoustic/synthetic sources tend to transform across pitch.
Neural latent models
Explore only if they outperform deterministic DSP.
92. DDSP RESEARCH DIRECTION
DDSP is philosophically relevant because it combines known DSP structures with learned models and demonstrates independent pitch/loudness manipulation, timbral transformation and extrapolation.
MIDI-DDSP goes further by explicitly separating notes, performance attributes and synthesis parameters—very close conceptually to our own separation between musical event, performance state and renderer.
Long-term architecture may therefore become:

```
SOURCE IDENTITY
+
PERFORMANCE STATE
+
NOTE / REGISTER
 ↓
INTERPRETABLE SYNTHESIS PARAMETERS
 ↓
DSP RENDERER
```

This is preferable to an opaque waveform generator.
93. RAVE / NEURAL AUDIO RESEARCH
RAVE demonstrates real-time neural audio generation and latent audio representations; its original research reported 48-kHz synthesis substantially faster than real time on a laptop CPU.
Potential future uses:

* timbral latent representation
* reconstructive transformation
* continuation
* learned variation

Do not make RAVE or another neural model a dependency of v1.
Reasons:

* model distribution
* training-data requirements
* unpredictable artifacts
* CPU/GPU variance
* less deterministic control
* harder debugging

94. LONG-TERM PERFORMANCE PRIOR
The strongest future moat is likely not generic generative AI.
It is a dataset/model that understands:
What changes and what remains invariant when the same sound source is performed repeatedly?
Training material should contain:

```
instrument
 ×
pitch
 ×
velocity
 ×
round robin
 ×
articulation
```

Prefer recordings owned/commissioned/cleared for model-training use.
Avoid relying on uncertain third-party sample-library rights.
95. POTENTIAL LEARNED MODEL
Training target:
Input:

* one source recording
* desired note
* velocity
* performance state

Output:
interpretable synthesis transformations such as:

* spectral-envelope delta
* attack delta
* noise delta
* harmonic delta
* damping
* micro-pitch trajectory
* resonance changes

The neural network should ideally predict DSP control parameters, not raw waveform samples.
This keeps the engine:

* controllable
* light
* explainable
* deterministic
* compatible with existing DSP

96. COMPETITIVE MOAT
The moat is not:
“round robin from one sample.”
That is easy to describe and eventually easy to copy.
The moat is the combined behavior of:
Source Decomposition
Understanding what parts of the source should and should not transform.
Performance Model
Generating correlated musical performances.
Continuation Model
Making finite recordings behave like living sustained instruments.
Register Intelligence
Separating pitch from register character.
Transformation Model
Moving from faithful reproduction into musically coherent reinterpretation.
Learned Acoustic Prior
Eventually learning transformations from real performed datasets.
The value comes from how these systems interact.
97. NON-GOALS
Do not turn the application into:

* Kontakt
* Falcon
* a general modular sampler
* a drum machine
* a wavetable synthesizer
* a giant effects rack
* a DAW
* a spectral editor
* an AI cloud service

Avoid:

* modulation matrices in primary UI
* manual zone programming
* mandatory accounts
* cloud processing
* hundreds of parameters
* generic “AI generated variations”
* independent random pitch/filter/volume masquerading as performance modeling

98. OFFLINE-FIRST PRODUCT
The complete core instrument must work offline.
No internet connection should be necessary to:

* import
* analyze
* play
* save
* load
* transform
* render

This matters creatively and commercially.
99. PRIVACY
Imported user audio stays local by default.
If future cloud/ML features are introduced they must be explicit opt-in features.
Do not upload samples silently.
100. FIRST-RUN CONTENT
Ship a small number of legally owned example sounds showing:

* vocal
* bowed string
* synth
* pluck
* strange texture

Each should demonstrate a different strength.
The examples are onboarding, not a factory library.
The product remains about the user's own sounds.
101. FACTORY STARTING STATES
Rather than hundreds of presets, consider a few macro-state starting points:
Natural
Alive
Floating
Broken
Frozen
Dream
Wide
These change engine behavior while retaining the user's sample.
Keep the preset philosophy small and musical.
102. QUALITY GATES
Do not advance an algorithm because it is sophisticated.
Every new DSP component must answer:

1. Is the result more playable?
2. Is it more beautiful?
3. Does it retain useful source identity?
4. Does it outperform the simpler method often enough to justify complexity?
5. Does it fail gracefully?
6. Can it run within real-time constraints?

If not, remove it.
103. MVP ACCEPTANCE CRITERIA
A candidate MVP is ready for serious musician testing when:
Import
One file can be dropped with no setup.
Analysis
The majority of clearly tonal corpus sources receive the correct root note; ambiguous material visibly falls back to manual correction rather than confidently choosing nonsense.
Time to play
Typical source is provisionally playable almost immediately and fully modeled after a short analysis.
Pitch
±12 semitone playback is consistently musically useful.
Extended pitch
±24 frequently produces useful creative results.
Sustain
Long notes do not expose a crude static loop.
Dynamics
Velocity audibly changes timbre/performance.
Repetition
Repeated notes no longer feel like identical sample retriggers.
Polyphony
16-note chords remain stable at normal buffer settings.
Recall
DAW project reopens identically.
Determinism
Offline bounces reproduce reliably.
Simplicity
A new user can create an instrument without opening Advanced.
104. V1 ACCEPTANCE CRITERIA
Commercial v1 additionally requires:

* 24 practical voices
* robust multi-sample import
* portable presets/instruments
* MPE or architecture complete enough to safely add it immediately after launch
* polished UI
* host compatibility
* crash-safe sample management
* signed/notarized installers
* Windows support or explicitly communicated Mac-first launch
* preset/example content
* full regression suite
* profiling
* manual/user documentation

105. CLAUDE CODE IMPLEMENTATION RULES
These rules should be placed in `CLAUDE.md`.
Rule 1
Do not rewrite working architecture without documenting why.
Rule 2
DSP must be testable outside the plugin GUI.
Rule 3
Never allocate, lock or perform file access on the audio thread.
Rule 4
Every random system must be reproducible from a stored seed.
Rule 5
Do not implement speculative ML during the MVP phases.
Rule 6
Prefer the simplest algorithm that wins the listening test.
Rule 7
Every major DSP feature requires an A/B render against the baseline sampler.
Rule 8
Never degrade existing source import/session recall while developing experimental DSP.
Rule 9
New analysis fields require schema/version handling.
Rule 10
Generated instrument models must remain backward-compatible or provide migration.
Rule 11
Do not expose technical parameters merely because they exist internally.
Rule 12
Musical behavior outranks architectural cleverness.
106. FIRST ENGINEERING TASKS
The initial implementation sequence should be:
P0-01 — Repository / CMake / JUCE skeleton
P0-02 — Headless audio renderer
P0-03 — Corpus manifest
P0-04 — WAV/AIFF loader
P0-05 — MIDI test-suite loader
P0-06 — Baseline polyphonic sampler
P0-07 — Analysis descriptor framework
P0-08 — Root/F0 detector
P0-09 — Onset/envelope analysis
P0-10 — Spectrum/harmonicity analysis
P0-11 — JSON analysis output
P0-12 — Golden rendering harness
Only after these succeed should the first sophisticated DSP bake-off begin.
107. FIRST DSP EXPERIMENT
Use one sustained source, one vocal source, one bowed source and one pluck.
For each:
render:

```
root -24
root -12
root
root +12
root +24
```

with:
Engine A
bandlimited resampling
Engine B
Signalsmith
Engine C
alternative/formant mode
Listen blind.
Document:

* identity
* beauty
* artifacts
* transient
* stereo
* useful range

Do not decide the permanent pitch architecture before this experiment.
108. SECOND DSP EXPERIMENT
Continuation.
Use:

* Trampeorgel
* vocal vowel
* nyckelharpa/bowed sustain
* tremolo source
* synth source

Compare:
A
single loop
B
optimized loop/crossfade
C
multi-loop continuation
D
multi-loop + movement
Render 60 seconds.
Determine whether sophisticated spectral continuation is actually necessary for MVP.
109. THIRD DSP EXPERIMENT
Performance synthesis.
Use the supplied real repeated-performance sets.
Choose one take as source.
Generate repeated performances.
Compare:
A
identical retrigger
B
independent randomization
C
correlated Performance Engine
This experiment should establish the first LIFE implementation.
110. FOURTH DSP EXPERIMENT
Dynamics.
Use an expressive tonal source.
Create:

```
velocity 20
40
60
80
100
127
```

Compare:
A
gain only
B
gain + filter
C
dynamic performance model
The difference should be obvious without knowing which is which.
111. RESEARCH PHILOSOPHY
Do not attempt to predict the final architecture completely in advance.
Build an environment where competing ideas can be rendered quickly and compared.
The advantage of this project will come from thousands of small listening decisions.
Therefore:
The research harness is as important as the first plugin GUI.
112. FINAL PRODUCT VISION
The finished instrument should feel almost impossibly simple.
The musician experiences:

```
DROP SOUND
    ↓
PLAY
```

Underneath that interaction:

```
SOURCE
 ↓
ANALYSIS
 ↓
DECOMPOSITION
 ├ TRANSIENT
 ├ HARMONICS
 ├ NOISE
 ├ RESONANCE
 └ AMBIENCE
 ↓
SOURCE MODEL
 ↓
┌──────────────────────────────────┐
│ PERFORMANCE ENGINE               │
│ REGISTER ENGINE                  │
│ CONTINUATION ENGINE              │
│ TRANSFORMATION ENGINE            │
└──────────────────────────────────┘
 ↓
VOICE MODEL
 ↓
MIDI / VELOCITY / MPE
 ↓
PLAYABLE INSTRUMENT
```

The complexity belongs entirely beneath the surface.
The product is not selling DSP.
It is selling the moment when someone drags in a forgotten recording, plays a chord and discovers an instrument they did not know existed.
That is the bar.