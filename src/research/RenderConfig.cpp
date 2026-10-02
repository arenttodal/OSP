#include "research/RenderConfig.h"

#include "io/JsonUtil.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace osp::research
{

std::string engineName (EngineId id)
{
    switch (id)
    {
        case EngineId::baselineA: return "baseline-a";
        case EngineId::baselineB: return "baseline-b";
        case EngineId::instrument: return "instrument";
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
    if (t == "c" || t == "instrument" || t == "osp" || t == "engine")
        return EngineId::instrument;
    return std::nullopt;
}

SamplerSettings RenderConfig::effectiveSamplerSettings() const
{
    auto settings = sampler;
    settings.randomization.enabled = engine == EngineId::baselineB;
    return settings;
}

namespace
{
    template <typename Enum, std::size_t N>
    Enum parseName (const juce::var& obj, const char* key, const std::array<const char*, N>& names, Enum fallback)
    {
        const auto text = json::getString (obj, key);
        for (std::size_t i = 0; i < N; ++i)
            if (text == names[i])
                return static_cast<Enum> (i);
        return fallback;
    }

    /** The shaping system's settings (the plugin's popups); see docs/testing.md. */
    void applyShapingBlock (const juce::var& b, Shaping& s)
    {
        if (json::getString (b, "preset") == "neutral")
            s = Shaping::neutral();
        const auto& life = b["life"];
        s.lifeMode = parseName (life, "mode", std::array { "natural", "loose", "fray" }, s.lifeMode);
        s.lifePitchCents = json::getDouble (life, "pitchCents", s.lifePitchCents);
        s.lifeTone = json::getDouble (life, "tone", s.lifeTone);
        s.lifeAttack = json::getDouble (life, "attack", s.lifeAttack);
        const auto& dynamics = b["dynamics"];
        s.velocityCurve = parseName (dynamics, "curve", std::array { "soft", "linear", "hard" }, s.velocityCurve);
        s.dynamicsTone = json::getDouble (dynamics, "tone", s.dynamicsTone);
        const auto& character = b["character"];
        s.filterType = parseName (character, "type", std::array { "lp24", "lp12", "hp12", "bp12", "tilt", "off" }, s.filterType);
        s.filterMinHz = json::getDouble (character, "minHz", s.filterMinHz);
        s.filterMaxHz = json::getDouble (character, "maxHz", s.filterMaxHz);
        s.resonance = json::getDouble (character, "resonance", s.resonance);
        s.drive = json::getDouble (character, "drive", s.drive);
        s.envAmount = json::getDouble (character, "envAmount", s.envAmount);
        s.envAttackSeconds = json::getDouble (character, "envAttackSeconds", s.envAttackSeconds);
        s.envDecaySeconds = json::getDouble (character, "envDecaySeconds", s.envDecaySeconds);
        const auto& movement = b["movement"];
        s.movementMode = parseName (movement, "mode", std::array { "drift", "tape", "chorus", "pulse" }, s.movementMode);
        s.movementA = json::getDouble (movement, "a", s.movementA);
        s.movementB = json::getDouble (movement, "b", s.movementB);
        s.movementC = json::getDouble (movement, "c", s.movementC);
        const auto& space = b["space"];
        s.spaceType = parseName (space, "type", std::array { "room", "chamber", "plate", "spring" }, s.spaceType);
        s.spaceDecaySeconds = json::getDouble (space, "decaySeconds", s.spaceDecaySeconds);
    }
}

void applyInstrumentBlock (const juce::var& e, RenderConfig& config)
{
    auto& es = config.engineSettings;
    if (json::has (e, "pitchCharacter"))
        es.pitchCharacter = json::getString (e, "pitchCharacter") == "natural" ? PitchCharacter::natural : PitchCharacter::tape;
    if (json::has (e, "continuation"))
        parseContinuationStrategy (json::getString (e, "continuation"), es.continuation);
    es.releaseGraft = json::getBool (e, "releaseGraft", es.releaseGraft);
    es.transientPreservation = json::getBool (e, "transientPreservation", es.transientPreservation);
    es.transientMixing = json::getBool (e, "transientMixing", es.transientMixing);
    if (json::has (e, "dynamicsMode"))
    {
        const auto mode = json::getString (e, "dynamicsMode");
        es.dynamicsMode = mode == "gain" ? DynamicsMode::gainOnly : (mode == "gain-filter" ? DynamicsMode::gainFilter : DynamicsMode::full);
    }
    if (json::has (e, "velocityRangeDb"))
        es.velocityRangeDb = json::getDouble (e, "velocityRangeDb", es.velocityRangeDb);
    config.anchors = json::getBool (e, "anchors", es.pitchCharacter == PitchCharacter::natural);
    if (json::has (e, "releaseSeconds"))
        es.adsr.releaseSeconds = json::getDouble (e, "releaseSeconds", es.adsr.releaseSeconds);
    if (json::has (e, "attackSeconds"))
        es.adsr.attackSeconds = json::getDouble (e, "attackSeconds", es.adsr.attackSeconds);
    if (json::has (e, "shaping"))
        applyShapingBlock (e["shaping"], es.shaping);
    const auto& m = e["macros"];
    es.macros.life = json::getDouble (m, "life", es.macros.life);
    es.macros.dynamics = json::getDouble (m, "dynamics", es.macros.dynamics);
    es.macros.character = json::getDouble (m, "character", es.macros.character);
    es.macros.motion = json::getDouble (m, "motion", es.macros.motion);
    es.macros.space = json::getDouble (m, "space", es.macros.space);
    es.macros.reimagined = json::getDouble (m, "reimagined", es.macros.reimagined);
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

    config.engineSettings.adsr = s.adsr;
    config.engineSettings.polyphony = s.polyphony;
    config.engineSettings.outputGainDb = s.outputGainDb;
    config.engineSettings.interpolationZeroCrossings = s.interpolationZeroCrossings;
    config.engineSettings.seed = s.seed;
    applyInstrumentBlock (root["instrument"], config);

    return config;
}

} // namespace osp::research
