# Analysis report schema (`schemaVersion: 1`)

Produced by `research-renderer --analyze` and the corpus runner
(`research/reports/<folder>/analysis.json`). Defined in `src/model/AnalysisData.h`,
serialised in `src/io/AnalysisJson.cpp`.

Units: seconds, Hz, cents, dB. `dBFS` = relative to digital full scale; `dB re max` =
relative to the loudest 20 ms RMS frame. Ratios are 0–1 and defined per field.
`null` means "not available" (never a fabricated value).

## Versioning

- `schemaVersion` is mandatory. Readers reject files without it and files newer than
  they support. Changing a field's meaning or removing a field requires a bump and a
  migration path in `analysisFromJson`. Adding a field keeps the version but must be
  added here.
- `analyzer` is the analysis code version (`0.1.0`); results can change between
  analyzer versions without a schema change.

## Framing

Mono analysis signal = equal-weight mix of all channels (stereo is analysed
separately, never collapsed in the source). Common hop 10 ms (`series.hopSeconds`,
exactly `round(0.01·sr)/sr`); frame *i* is centred at *i·hop*. "Active" frames are
those within 40 dB of the maximum RMS (and above −100 dBFS). Sources longer than
120 s are analysed over their first 120 s (with a warning).

## `source`

| Field | Meaning |
|---|---|
| `filename`, `path` | as given (corpus: path relative to the corpus root) |
| `contentHash` | `sha256:<hex>` of the file bytes |
| `format` | `WAV` or `AIFF` |
| `bitDepth`, `isFloatingPoint` | encoding |
| `sampleRate`, `channels`, `frames`, `durationSeconds` | as decoded (`channels` = channels in the file) |
| `truncated` | loader stopped at its maximum length (600 s) |

## `pitch` (YIN, 30 Hz – 4 kHz, threshold 0.15)

| Field | Meaning |
|---|---|
| `detected` | `true` only when `confidence ≥ 0.4` |
| `fundamentalHz` | weighted median (log-frequency) of voiced frames; weight = periodicity × RMS; `null` if no voiced frames |
| `midiNote`, `noteName`, `centsOffset` | nearest note (MIDI 60 = C4) and the offset of `fundamentalHz` from it |
| `confidence` | `agreement × meanPeriodicity`. *agreement*: RMS-weighted fraction of active frames that are voiced (periodicity ≥ 0.5) **and** within 100 cents of `fundamentalHz`. *meanPeriodicity*: RMS-weighted mean periodicity of those frames |
| `confidenceLevel` | `high` (≥ 0.7), `moderate` (≥ 0.4), `low`, `none` (nothing voiced) |
| `voicedFraction` | RMS-weighted fraction of active frames that are voiced |
| `stabilityCents` | RMS-weighted std dev of agreeing frames around `fundamentalHz` |
| `rangeCents` | 5th–95th percentile spread of agreeing frames |
| `vibrato` | `{rateHz, depthCents (peak deviation), strength}` from the autocorrelation of the cents contour (3–9 Hz, longest agreeing run) when strength ≥ 0.5 and depth ≥ 3 cents; else `null` |

Root policy: the **sounding pitch** wins. Filenames are never used for the root; for
harmonics and other sources that alternate with a sub-pitch, the majority (sounding)
pitch is the root and the alternation lowers the confidence.

A low-confidence estimate is still reported (it can seed a UI suggestion) but
`detected` is `false`, a warning is added, and renders label the root
`analysis-low-confidence`.

## `envelope`

| Field | Meaning |
|---|---|
| `peakDbfs` | absolute sample peak over all channels |
| `maxRmsDbfs` | loudest 20 ms RMS frame |
| `leadingSilenceSeconds` / `trailingSilenceSeconds` | before the first / after the last sample above −60 dB re peak |
| `onsetSeconds` | first frame within 20 dB of max RMS |
| `attackSeconds` | from onset to the first frame within 3 dB of max RMS |
| `peakSeconds` | time of the max RMS frame |
| `estimatedDecaySeconds` | from peak to the last frame within 20 dB of max RMS |
| `decaySlopeDbPerSecond` | least-squares slope of RMS dB over the *body* (end of attack → last active frame) |
| `sustainLevelDb` | median RMS (dB re max) over the body |
| `sustainFluctuationDb` | std dev of the linearly detrended body RMS (movement, tremolo, swells) |
| `endsWhileSounding` | the last 5 % of the file is within 12 dB of max RMS (recording cut before a natural release) |
| `secondaryPeakCount` | later envelope peaks rising ≥ 6 dB above the preceding trough |
| `tremolo` | `{rateHz, depthDb (peak-to-peak), strength}` from the body RMS (2–15 Hz), or `null` |
| `onsets` | spectral-flux onsets `{timeSeconds, strength}` (adaptive median threshold + 3 dB/bin floor, ≥ 50 ms apart, max 64) |

No attack → exponential-decay model is assumed: the `rmsDb` series carries the full trajectory.

## `spectral` (Hann STFT, ~40 ms: 2048 points at 44.1/48 kHz, 4096 at 88.2/96 kHz)

Aggregates are RMS-weighted means over active frames.

| Field | Meaning |
|---|---|
| `fftSize` | STFT size |
| `meanCentroidHz`, `centroidStdHz` | power-weighted spectral centroid and its spread |
| `meanRolloffHz` | 85 % energy rolloff |
| `meanFlatness` | geometric / arithmetic mean of the power spectrum (0 tonal … 1 white noise) |
| `meanFlux` | mean positive dB change per bin between frames (floor 80 dB below max) |
| `periodicity` | mean (1 − YIN aperiodicity) |
| `harmonicEnergyRatio` | energy within ±3 % (≥ ±2 bins) of multiples of the frame F0, up to 10 kHz; unvoiced frames count as 0 |
| `highFrequencyEnergyRatio` | energy ≥ 4 kHz / total |
| `lowFrequencyEnergyRatio` | energy < 250 Hz / total ("body") |

## `stereo`

| Field | Meaning |
|---|---|
| `isMono` | single-channel file |
| `isDualMono` | two bit-identical channels |
| `correlation` | Pearson L/R correlation (−1 … 1) |
| `width` | side energy / (mid + side) energy: 0 mono, 0.5 uncorrelated, 1 anti-phase |
| `sideToMidDb` | side / mid energy in dB |
| `balanceDb` | L energy re R energy (positive = louder left) |
| `widthLow`, `widthMid`, `widthHigh` | `width` below 300 Hz, 300 Hz–3 kHz, above 3 kHz |

## `warnings`

Human-readable strings: loader notes (truncation, dropped channels, non-finite
samples), silence, uncertain/no pitch, extremely short source, source peaking at
0 dBFS.

## `series` (all on `hopSeconds`)

`pitchHz` (0 = unvoiced), `pitchConfidence` (periodicity), `rmsDb` (20 ms RMS, dBFS,
floor −120), `centroidHz`, `flux`, `flatness`.

## Other versioned files

| File | Producer | Key fields |
|---|---|---|
| `<fixture>[.<engine>].metrics.json` | renderer / corpus | `schemaVersion`, `status`, safety counts, levels, `notes[]`, `issues[]`, `sampleHash`, `fixture`, `fixtureVersion`, `engine`, `rootMidi`, `rootOrigin`, `seed`, `startAtOnset`, `normaliseLevel`, `startSeconds`, `playbackGainDb` |
| `index.json` | `--index`, corpus | `schemaVersion`, `files[] {id, path, filename, extension, sizeBytes, supported, duplicateOf?}` |
| `summary.json` | corpus | `schemaVersion`, `counts`, `files[]` with per-file status, root and renders |
| `research/configs/*.json` | hand-written | `schemaVersion`, `engine`, `sampleRate`, `blockSize`, `seed`, `sampler{}`, `randomization{}`, `playback{startAtOnset, normaliseLevel, targetMaxRmsDbfs}` |
| benchmark JSON | `--benchmark --output-json` | `schemaVersion`, `config` (incl. `engine`), block timing statistics |
| `research/configs/*.json` `instrument` block | hand-written | engine C: `macros{life, dynamics, character, motion, space, reimagined}`, `pitchCharacter`, `continuation`, `releaseGraft`, `transientPreservation`, `transientMixing`, `dynamicsMode`, `velocityRangeDb`, `releaseSeconds`, `anchors` |
| `research/experiments/*.json` | hand-written | `schemaVersion`, `name`, texts (`tab`, `title`, `heading`, `intro`, `groupLabel`), `scales[]`, `sources[] {id, label, path, paths?, sequence?}`, `sequence` / `variants[]`, `conditions[] {id, label, engine, instrument{}, useSet?, sequence?}`, rendering (`seed`, `outputSampleRate`, `listeningRmsDbfs`, `format`, `trimToCommon`, `maxSeconds`, `tail`) |
| experiment `key.json` | `--experiment` | `schemaVersion`, `clips[] {id, group, section, variant, source, condition, seconds, renderMs, levelGainDb, repetitionScore, repetitionLagSeconds, seamSpikeDb}` — never shown to listeners |
| experiment `listening.json` | `--experiment` | texts, `scales`, `sections[]`, `groups[] {id, section, label, note, clips[] (shuffled)}`, `ext` |
| `*.osppreset` | plugin | the plugin state XML (below) |
| `*.ospinstrument` | plugin | zip: `manifest.json {schemaVersion 1, format, engineVersion, stateVersion, sources[] {contentHash, filename, stored}}`, `source/<sha256>.<ext>`, `analysis/<sha256>.analysis.json`, `preset.xml` |
| plugin state (XML) | host session | `stateVersion` (9; 8 adds `reimaginedRouting` "perLayer" / "legacyGlobal", absent = legacy; 9 adds the REIMAGINED mode parameters `layerX.reimagined.mode` and `layerX.reimagined.<mode>.<setting>`, absent = KALEIDOSCOPE at its defaults), `uiScale`, `program`, APVTS parameters, `ModLinks? {schemaVersion 1, uidN, targetN}` (meta-modulation, additive: each route slot's stable route ID and a depth route's target ID; absent = no links), `Instrument {contentHash, filename, originalPath, playbackRootMidi, rootOrigin, startSeconds, playbackGainDb, rootOverride?, Set? {Member{contentHash, filename, originalPath}*, Assignment{filename, role, layer, rootMidi?}*}}` |

The engine's in-memory models (`InstrumentModel`, `ContinuationModel`, `InstrumentSet`,
`ReimaginedAnalysis` schema 1: the TAPE FRAME splice map and MOSAIC's harmonic frames)
are never persisted: they are rebuilt from the cached analysis, deterministically, so
there is no model file format to migrate.
