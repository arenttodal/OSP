#pragma once

#include "audio/sampler/BaselineSampler.h"

#include <filesystem>
#include <optional>
#include <string>

namespace osp::research
{

/** Engines selectable in the renderer. Every comparison renders against A and B. */
enum class EngineId
{
    baselineA,  ///< plain resampling sampler
    baselineB   ///< A + independent per-note randomisation
};

std::string engineName (EngineId id);
std::optional<EngineId> parseEngine (const std::string& text); ///< "A", "B", "baseline-a", ...

/** Everything that determines a render besides the source and the MIDI. */
struct RenderConfig
{
    static constexpr int schemaVersion = 1;

    EngineId engine = EngineId::baselineA;
    double sampleRate = 48000.0;      ///< output rate; <= 0 means "use the source rate"
    int blockSize = 128;              ///< processing block, mirrors a host callback
    double maxTailSeconds = 15.0;     ///< render stops this long after the last event at the latest
    SamplerSettings sampler {};

    /** Settings actually used for the engine (B turns randomisation on). */
    SamplerSettings effectiveSamplerSettings() const;
};

/**
    Loads a config JSON (see research/configs). Missing fields keep their defaults;
    unknown fields are ignored. Returns nullopt with an error for unreadable files or a
    newer schemaVersion.
*/
std::optional<RenderConfig> loadRenderConfig (const std::filesystem::path& path, std::string& error);

} // namespace osp::research
