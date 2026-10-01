# CLAUDE.md — working rules for this repository

OSP is a one-sample performance instrument: drop in one tonal recording, play an
expressive instrument. Read `docs/product.md` (why), `docs/architecture.md` (how),
`docs/STATUS.md` (where we are) before changing anything substantial.

## Build & test

```sh
cmake -B build -G Ninja            # add -DOSP_JUCE_DIR=... -DOSP_CATCH2_DIR=... for offline builds
cmake --build build
ctest --test-dir build --output-on-failure
./build/apps/research-renderer/research-renderer --help
```

Keep `ctest` green on every commit. Never leave the main branch broken.

## Critical architectural rules

**Rule 1 — Audio thread safety.** Code that runs (or will run) on the realtime audio
thread must never perform: file I/O, heap allocation, mutex locking, filesystem
scanning, sample analysis, JSON parsing, network calls, or any blocking operation.
Design reusable DSP with these constraints from the start: allocate in `prepare()`,
render in `render()` / `process()` with no allocation. `BaselineSampler`,
`SamplerVoice`, `Adsr`, `SincInterpolator::computeKernel/apply` follow this pattern.

**Rule 2 — Determinism.** Every stochastic process must be reproducible from a stored
seed. Use `osp::Prng` (`src/core/Prng.h`) seeded via `Prng::deriveSeed (seed, eventCounter, note)`.
Never use `std::random_device`, `rand()`, time, addresses or `std::` distributions
(their output differs between standard libraries). Golden renders and DAW bounces
depend on `seed -> exact repeatable output`. The PRNG golden-value test must not be
"fixed" by changing expected values.

**Rule 3 — Separation of concerns.** Analysis never happens inside voice rendering.
Analysis produces immutable/prepared data (`AnalysisData`, `PlaybackSource`);
playback only consumes it.

**Rule 4 — Headless first.** Every DSP algorithm must be testable with
`research-renderer` (and unit tests) without opening a plugin GUI.

**Rule 5 — No speculative ML.** No PyTorch, ONNX, TensorFlow, neural inference, RAVE
or DDSP models until a later phase explicitly calls for them.

**Rule 6 — No unnecessary framework invention.** Use standard JUCE/C++ facilities
where appropriate. Do not add abstractions to look sophisticated.

**Rule 7 — Preserve working functionality.** Corpus import, analysis, rendering and
session recall must keep working while experimenting. Add a new engine or option;
do not break the baselines.

**Rule 8 — Version analysis schemas.** All persisted data (analysis, metrics, corpus
index, configs, goldens) carries a `schemaVersion`. Changing the meaning of a field
or removing one requires a version bump plus migration in `io/AnalysisJson.cpp`;
adding fields must be documented in `docs/analysis-schema.md`.

## Product/DSP rules (from the specification)

- Prefer the simplest algorithm that wins the listening test.
- Every major DSP feature needs an A/B render against **baseline A** (plain resampler)
  and **baseline B** (naive randomised sampler) using the standard fixtures.
- Musical behaviour outranks architectural cleverness; numerical fidelity is a
  guard-rail, not the goal.
- Do not expose technical parameters to musicians merely because they exist.
- Fail beautifully: odd input (silence, noise, no pitch, tiny/huge files) must never
  crash or abort a batch; report it and continue.
- Do not rewrite working architecture without documenting why (`docs/architecture.md`).
- Generated models/presets must stay backward compatible or provide migration.

## Code layout

- `src/core`, `src/audio`, `src/analysis`, `src/midi/MidiFixtures*`, `src/model` —
  **pure C++20, no JUCE** (`osp_dsp` library). Keep it that way.
- `src/io`, `src/midi/MidiFileIO*` — JUCE-backed file formats, JSON, hashing.
- `src/research` — renderer/corpus/metrics/benchmark library used by the CLI and tests.
- `apps/research-renderer` — the CLI. `apps/plugin` — JUCE plugin + standalone.
- Tests: Catch2, tags `[unit]`, `[integration]`, `[regression]`.

## Conventions

- C++20, JUCE-style formatting (4-space indent, space before call parentheses,
  `camelCase` functions, `PascalCase` types). Comments explain *why*.
- Units in names or docs: seconds, Hz, dB (dBFS when absolute), cents.
- Fixture definitions in `src/midi/MidiFixtures.cpp` are frozen; add new fixtures
  instead of changing existing ones (bump `fixtureVersion` only if unavoidable).
- Golden updates (`OSP_UPDATE_GOLDEN=1`) only for intentional DSP changes, explained in
  the commit message.
- Update `docs/STATUS.md` when completing a ticket.
