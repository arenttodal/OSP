#pragma once

#include "core/AudioData.h"
#include "midi/MidiEvent.h"
#include "research/RenderConfig.h"
#include "research/RenderSession.h"

#include <juce_core/juce_core.h>

#include <array>
#include <string>
#include <vector>

namespace osp::research::unified
{

/**
    The unified-engine baseline (docs/research/unified-andor, Phase 0): fixed scenes that
    exercise OSP's existing granular and One Shot playback headless, so every later phase
    can prove it changed nothing it was not meant to. Each scene fixes the sources (deterministic
    test signals, no licensed audio), the MIDI, the sample rate, the block size, the seeds and
    every engine setting. The digests of their renders are the baseline
    (tests/regression/golden/unified/), captured from the build before KEY DNA existed.

    Scenes are append-only: a scene's definition never changes once its digest is stored
    (add a new scene instead), like the MIDI fixtures.
*/

/** The scenes' sources: synthetic, reproducible from their name. */
enum class SourceId
{
    vowelA3,   ///< a sung "ah" at A3 (220 Hz), 2.5 s
    pluckE2,   ///< a plucked string at E2, 2.5 s
    vowelE4,   ///< a sung vowel at E4, 2.5 s
};
const char* sourceName (SourceId id) noexcept;
AudioData sourceAudio (SourceId id);

struct LayerSpec
{
    SourceId source = SourceId::vowelA3;
};

struct Scene
{
    std::string name;
    std::string description;
    std::vector<LayerSpec> layers;   ///< 1..3: A, B, C (the engine's source mode, granular and layer settings are in config)
    RenderConfig config;
    MidiSequence midi;
};

/** Every baseline scene, in a fixed order (append only). */
std::vector<Scene> baselineScenes();
/** A scene by name (nullptr: none). */
const Scene* findScene (const std::vector<Scene>& scenes, const std::string& name);

/** The engine models for the scene's layers (built once per source and kept for the process). */
std::array<const InstrumentModel*, 3> modelsFor (const Scene& scene);

/** Renders a scene (its own block size unless `blockSizeOverride` > 0). */
RenderOutput render (const Scene& scene, int blockSizeOverride = 0);

/** What is stored per scene: an exact sample hash and robust level/brightness figures. */
struct Digest
{
    std::string hash;           ///< FNV-1a over every float's bits, both channels (exact)
    double seconds = 0.0;
    double rmsDb = -200.0;      ///< over the whole render, both channels
    double peakDb = -200.0;
    double brightness = 0.0;    ///< sqrt (first-difference energy / energy): a cheap spectral tilt
    double stereoWidth = 0.0;   ///< side / mid energy ratio
};
Digest digest (const AudioData& audio);
juce::var digestToJson (const Scene& scene, const Digest& d, const std::string& platform);

/** "linux-x86_64-gcc", "macos-arm64-clang": where a digest was made (exact hashes are only
    compared on the platform that stored them; elsewhere the figures are). */
std::string platformTag();

/** Real-time cost of one scene, measured as a host would drive it (one call per block). */
struct Cost
{
    double renderedSeconds = 0.0;
    double meanBlockMicros = 0.0, p99BlockMicros = 0.0, worstBlockMicros = 0.0;
    double meanBudgetPercent = 0.0;   ///< mean block time / block duration
    double worstBudgetPercent = 0.0;
    int peakVoices = 0;
    int peakGrains = 0;               ///< grains displayed at once, all layers (GrainSnapshot)
};
/** Renders the scene `repeats` times and keeps the fastest run per block (less noise). */
Cost measure (const Scene& scene, int repeats = 3);

/** Peak resident memory of this process so far, in MB (getrusage). */
double peakResidentMegabytes();

} // namespace osp::research::unified
