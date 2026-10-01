#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace osp::research
{

/**
    Phase 2 pitch-engine bake-off (docs/dsp-research.md, docs/reports/phase0-report.md §8).

    Every source is rendered as one held note at each offset through:
      A  bandlimited resampling (the baseline sampler)
      B  Signalsmith Stretch, plain transposition
      C  Signalsmith Stretch with formant compensation (F0 hint from analysis)
    All engines start at the analysed onset and use the same level matching, so they
    differ only in how pitch is changed. Clips are trimmed to a common length, faded and
    loudness-matched, then written under random names; the engine behind each name is
    kept in key.json together with guard-rail metrics.
*/
struct BakeoffSource
{
    std::string family;   ///< e.g. "synth", "vocal", "bowed", "organ", "pluck"
    std::string path;     ///< relative to the plan's corpusRoot (or absolute)
};

struct BakeoffPlan
{
    static constexpr int schemaVersion = 1;
    std::string name = "pitch-bakeoff";
    std::filesystem::path corpusRoot = "research/corpus";
    std::vector<BakeoffSource> sources;
    std::vector<double> offsets { -24.0, -12.0, 0.0, 12.0, 24.0 };
    std::vector<std::string> engines { "A", "B", "C" };
    double clipSeconds = 3.0;
    double outputSampleRate = 48000.0;
    double listeningRmsDbfs = -20.0;
    std::uint64_t seed = 1;
};

std::optional<BakeoffPlan> loadBakeoffPlan (const std::filesystem::path& path, std::string& error);

struct BakeoffSummary
{
    int clips = 0;
    int groups = 0;
    std::vector<std::string> errors;
    std::filesystem::path keyFile;
    std::filesystem::path clipsDir;
};

BakeoffSummary runBakeoff (const BakeoffPlan& plan, const std::filesystem::path& outputDir,
                           const std::function<void (const std::string&)>& log = {});

/**
    Joins listener ratings with the key and writes score.json / score.md: mean ratings
    and wins per engine, per family and per offset band. Ratings JSON:
      { "ratings": [ { "clip": "<id>", "identity": 1-5, "beauty": 1-5, "artifacts": 1-5, "best": bool }, ... ] }
*/
bool scoreBakeoff (const std::filesystem::path& outputDir, const std::filesystem::path& ratingsFile, std::string& report,
                   std::string& error);

} // namespace osp::research
