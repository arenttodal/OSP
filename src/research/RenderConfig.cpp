#include "research/RenderConfig.h"

#include "io/JsonUtil.h"

#include <algorithm>
#include <cctype>

namespace osp::research
{

std::string engineName (EngineId id)
{
    switch (id)
    {
        case EngineId::baselineA: return "baseline-a";
        case EngineId::baselineB: return "baseline-b";
    }
    return "unknown";
}

std::optional<EngineId> parseEngine (const std::string& text)
{
    std::string t = text;
    std::transform (t.begin(), t.end(), t.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    if (t == "a" || t == "baseline-a" || t == "baseline")
        return EngineId::baselineA;
    if (t == "b" || t == "baseline-b" || t == "randomized")
        return EngineId::baselineB;
    return std::nullopt;
}

SamplerSettings RenderConfig::effectiveSamplerSettings() const
{
    auto settings = sampler;
    settings.randomization.enabled = engine == EngineId::baselineB;
    return settings;
}

std::optional<RenderConfig> loadRenderConfig (const std::filesystem::path& path, std::string& error)
{
    const auto parsed = json::readFile (path, error);
    if (! parsed)
        return std::nullopt;

    const auto& root = *parsed;
    const int version = json::getInt (root, "schemaVersion", 1);
    if (version > RenderConfig::schemaVersion)
    {
        error = "config schemaVersion " + std::to_string (version) + " is newer than supported";
        return std::nullopt;
    }

    RenderConfig config;
    if (json::has (root, "engine"))
    {
        const auto engine = parseEngine (json::getString (root, "engine"));
        if (! engine)
        {
            error = "unknown engine '" + json::getString (root, "engine") + "' in " + path.string();
            return std::nullopt;
        }
        config.engine = *engine;
    }

    config.sampleRate = json::getDouble (root, "sampleRate", config.sampleRate);
    config.blockSize = std::clamp (json::getInt (root, "blockSize", config.blockSize), 1, 8192);
    config.maxTailSeconds = json::getDouble (root, "maxTailSeconds", config.maxTailSeconds);

    auto& s = config.sampler;
    const auto& sampler = root["sampler"];
    s.polyphony = json::getInt (sampler, "polyphony", s.polyphony);
    s.adsr.attackSeconds = json::getDouble (sampler, "attackSeconds", s.adsr.attackSeconds);
    s.adsr.decaySeconds = json::getDouble (sampler, "decaySeconds", s.adsr.decaySeconds);
    s.adsr.sustainLevel = json::getDouble (sampler, "sustainLevel", s.adsr.sustainLevel);
    s.adsr.releaseSeconds = json::getDouble (sampler, "releaseSeconds", s.adsr.releaseSeconds);
    s.velocityRangeDb = json::getDouble (sampler, "velocityRangeDb", s.velocityRangeDb);
    s.outputGainDb = json::getDouble (sampler, "outputGainDb", s.outputGainDb);
    s.interpolationZeroCrossings = json::getInt (sampler, "interpolationZeroCrossings", s.interpolationZeroCrossings);
    s.stealFadeSeconds = json::getDouble (sampler, "stealFadeSeconds", s.stealFadeSeconds);
    s.seed = static_cast<std::uint64_t> (json::getDouble (root, "seed", static_cast<double> (s.seed)));

    const auto& playback = root["playback"];
    config.playback.startAtOnset = json::getBool (playback, "startAtOnset", config.playback.startAtOnset);
    config.playback.normaliseLevel = json::getBool (playback, "normaliseLevel", config.playback.normaliseLevel);
    config.playback.targetMaxRmsDbfs = json::getDouble (playback, "targetMaxRmsDbfs", config.playback.targetMaxRmsDbfs);

    const auto& random = root["randomization"];
    s.randomization.gainDb = json::getDouble (random, "gainDb", s.randomization.gainDb);
    s.randomization.detuneCents = json::getDouble (random, "detuneCents", s.randomization.detuneCents);
    s.randomization.startOffsetMs = json::getDouble (random, "startOffsetMs", s.randomization.startOffsetMs);

    return config;
}

} // namespace osp::research
