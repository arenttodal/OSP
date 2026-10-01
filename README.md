# OSP — One-Sample Performance instrument

**Drop in a sound. Play an instrument.** OSP turns a single tonal recording (a synth
note, a vocal vowel, a bowed string, an organ, a pluck, a strange texture) into a
playable, expressive instrument across the keyboard — aiming for musical beauty and
inspiration rather than strict realism. See [docs/product.md](docs/product.md).

The repository is currently at **Phase 0: research harness + baseline engine** — a
headless, deterministic environment for analysing sources and rendering them through
competing DSP engines against a fixed corpus. Progress: [docs/STATUS.md](docs/STATUS.md).

## Build

Requirements: CMake ≥ 3.22, a C++20 compiler (Xcode 14+/AppleClang, GCC 11+, Clang 14+
or MSVC 2022), Ninja (optional), Git. JUCE 8.0.9 and Catch2 3.9.1 are fetched
automatically.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

macOS (Apple Silicon) is the primary target; the code is portable (also built and
tested on Linux). Offline builds can use local checkouts:

```sh
cmake -B build -DOSP_JUCE_DIR=/path/to/JUCE -DOSP_CATCH2_DIR=/path/to/Catch2
```

Options: `-DOSP_BUILD_TESTS=OFF`, `-DOSP_BUILD_PLUGIN=ON`.

## The instrument (Phase 1: baseline sampler plugin)

```sh
cmake -B build-plugin -G Ninja -DCMAKE_BUILD_TYPE=Release -DOSP_BUILD_PLUGIN=ON
cmake --build build-plugin
ctest --test-dir build-plugin --output-on-failure      # adds the headless plugin tests
```

Artefacts land in `build-plugin/apps/plugin/OSP_Plugin_artefacts/Release/`: `AU/OSP.component`
(macOS), `VST3/OSP.vst3` and `Standalone/OSP(.app)`. Copy the AU/VST3 into
`~/Library/Audio/Plug-Ins/Components` / `.../VST3` (macOS) to use them in a DAW.

Drop a WAV/AIFF onto the window (or use *Load…* / *Load example*), play. The detected
root is shown and can be overridden; Attack, Release, Velocity range, Fine tune, Bend
range and Output are automatable. Imported samples are copied (by content hash) into
`~/Library/Application Support/OSP/Samples` (macOS) or `~/.config/OSP/Samples` (Linux)
so sessions recall even if the original file moves; set `OSP_SAMPLE_STORE` to override.
On Linux the plugin build needs the usual JUCE GUI packages (X11, Xrandr, Xinerama,
Xcursor, freetype, fontconfig, ALSA headers).

## The research renderer

```sh
R=./build/apps/research-renderer/research-renderer

# Analyse a file (JSON to stdout, or to a file with a printed summary)
$R --analyze source.wav
$R --analyze source.wav --output-analysis report.json

# Render a MIDI file or a standard fixture through baseline A (or B)
$R --source source.wav --midi research/midi/melody.mid --output out.wav
$R --source source.wav --fixture register --output out.wav --metrics out.json
$R --source source.wav --midi test.mid --analysis report.json --output out.wav   # reuse stored analysis
$R --source source.wav --fixture chords --root A3 --engine B --seed 7 --output out.wav
$R --source source.wav --fixture repetition --start onset --level normalise --output out.wav  # as the plugin plays it

# Whole corpus (continues past broken files; writes research/reports + research/renders)
$R --corpus research/corpus --profile standard --engines A,B
$R --corpus research/corpus --profile sustain        # 60 s renders, opt-in

# Utilities
$R --index research/corpus                  # content-hash index
$R --write-fixtures research/midi           # standard MIDI fixtures
$R --generate-test-signals /tmp/signals     # synthetic ground-truth mini-corpus
$R --benchmark                              # 24 voices, 48 kHz, 128-sample blocks
```

Exit codes: 0 ok · 1 usage · 2 unreadable input · 3 processing/output failure ·
4 render produced errors (or `--strict` corpus failures). Run `$R --help` for all options.

## Adding corpus files

Copy WAV/AIFF files (any names, any sub-folders) into `research/corpus/`. They are
git-ignored. Run `--index` to see what was found, then `--corpus`. Identity is the
SHA-256 of the file content, so renaming or moving files does not change results.
See [docs/corpus.md](docs/corpus.md).

## Tests

```sh
ctest --test-dir build --output-on-failure            # unit, integration, regression, performance-smoke
./build/tests/osp_tests "[unit]"                       # Catch2 tags: [unit] [integration] [regression] ...
OSP_UPDATE_GOLDEN=1 ./build/tests/osp_tests "[regression]"   # only after intentional DSP changes
```

See [docs/testing.md](docs/testing.md) for fixtures, golden renders and benchmarks.

## Documentation

| | |
|---|---|
| [docs/product.md](docs/product.md) | product philosophy and roadmap |
| [docs/architecture.md](docs/architecture.md) | modules, data flow, thread safety, determinism |
| [docs/analysis-schema.md](docs/analysis-schema.md) | every field of the analysis JSON |
| [docs/dsp-research.md](docs/dsp-research.md) | bake-off method and planned experiments |
| [docs/testing.md](docs/testing.md) | fixtures, metrics, goldens, corpus tests, benchmarks |
| [docs/corpus.md](docs/corpus.md) | corpus handling and source families |
| [docs/STATUS.md](docs/STATUS.md) | progress, milestones, known issues |
| [CLAUDE.md](CLAUDE.md) | engineering rules for contributors (human or AI) |
