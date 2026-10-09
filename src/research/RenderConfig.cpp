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
        s.movementMode = parseName (movement, "mode", std::array { "drift", "tape", "chorus", "pulse", "shaper" }, s.movementMode);
        // Each mode's own settings ("drift": {"speed", "pitch", "tone"}, ...).
        const auto& drift = movement["drift"];
        s.driftSpeed = json::getDouble (drift, "speed", s.driftSpeed);
        s.driftPitch = json::getDouble (drift, "pitch", s.driftPitch);
        s.driftTone = json::getDouble (drift, "tone", s.driftTone);
        const auto& tape = movement["tape"];
        s.tapeWow = json::getDouble (tape, "wow", s.tapeWow);
        s.tapeFlutter = json::getDouble (tape, "flutter", s.tapeFlutter);
        s.tapeWear = json::getDouble (tape, "wear", s.tapeWear);
        const auto& chorus = movement["chorus"];
        s.chorusRate = json::getDouble (chorus, "rate", s.chorusRate);
        s.chorusWidth = json::getDouble (chorus, "width", s.chorusWidth);
        s.chorusStereo = json::getDouble (chorus, "stereo", s.chorusStereo);
        const auto& pulse = movement["pulse"];
        s.pulseRate = json::getDouble (pulse, "rate", s.pulseRate);
        s.pulseShape = json::getDouble (pulse, "shape", s.pulseShape);
        s.pulseStereo = json::getDouble (pulse, "stereo", s.pulseStereo);
        const auto& shaper = movement["shaper"];
        s.shaper.pattern = json::getInt (shaper, "pattern", s.shaper.pattern);
        s.shaper.rate = parseName (shaper, "rate", std::array { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, s.shaper.rate);
        s.shaper.target = parseName (shaper, "target", std::array { "vol", "filter", "both" }, s.shaper.target);
        s.shaper.smooth = json::getDouble (shaper, "smooth", s.shaper.smooth);
        // Older experiment files: generic "a"/"b"/"c" mean the selected mode's three settings.
        if (json::has (movement, "a") || json::has (movement, "b") || json::has (movement, "c"))
        {
            double* slots[3] = { &s.driftSpeed, &s.driftPitch, &s.driftTone };
            if (s.movementMode == MovementMode::tape)
                slots[0] = &s.tapeWow, slots[1] = &s.tapeFlutter, slots[2] = &s.tapeWear;
            else if (s.movementMode == MovementMode::chorus)
                slots[0] = &s.chorusRate, slots[1] = &s.chorusWidth, slots[2] = &s.chorusStereo;
            else if (s.movementMode == MovementMode::pulse)
                slots[0] = &s.pulseRate, slots[1] = &s.pulseShape, slots[2] = &s.pulseStereo;
            const char* keys[3] = { "a", "b", "c" };
            for (int i = 0; i < 3; ++i)
                *slots[i] = json::getDouble (movement, keys[i], *slots[i]);
        }
        const auto& space = b["space"];
        // "chamber" (SPACE v1) is read as HALL, which took its place.
        if (json::getString (space, "type") == "chamber")
            s.spaceType = SpaceType::hall;
        else
            s.spaceType = parseName (space, "type", std::array { "room", "hall", "plate", "spring" }, s.spaceType);
        s.spaceDecaySeconds = json::getDouble (space, "decaySeconds", s.spaceDecaySeconds);
        s.spacePreDelayMs = json::getDouble (space, "preDelayMs", s.spacePreDelayMs);
        s.spaceSize = json::getDouble (space, "size", s.spaceSize);
        s.spaceDamping = json::getDouble (space, "damping", s.spaceDamping);
        s.spaceModulation = json::getDouble (space, "modulation", s.spaceModulation);
        s.spaceWidth = json::getDouble (space, "width", s.spaceWidth);
        s.spaceLowCutHz = json::getDouble (space, "lowCutHz", s.spaceLowCutHz);
        s.spaceHighCutHz = json::getDouble (space, "highCutHz", s.spaceHighCutHz);
        const auto& echo = b["echo"];
        s.echoType = parseName (echo, "type", std::array { "tape", "bbd" }, s.echoType);
        if (json::has (echo, "sync"))
            s.echoSync = static_cast<bool> (echo["sync"]);
        s.echoDivision = static_cast<int> (json::getDouble (echo, "division", s.echoDivision));
        s.echoTimeMs = json::getDouble (echo, "timeMs", s.echoTimeMs);
        s.echoFeedback = json::getDouble (echo, "feedback", s.echoFeedback);
        s.echoTone = json::getDouble (echo, "tone", s.echoTone);
        s.echoAge = json::getDouble (echo, "age", s.echoAge);
        s.echoStereo = parseName (echo, "stereo", std::array { "mono", "pingpong", "wide" }, s.echoStereo);
        const auto& drive = b["drive"];
        s.driveMode = parseName (drive, "mode", std::array { "tube", "tape", "crunch" }, s.driveMode);
        s.driveTone = json::getDouble (drive, "tone", s.driveTone);
        s.driveBody = json::getDouble (drive, "body", s.driveBody);
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
    es.macros.echo = json::getDouble (m, "echo", es.macros.echo);
    es.macros.drive = json::getDouble (m, "drive", es.macros.drive);
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

    // Modulation (optional, engine C): "modulation": { "lfo1": { "shape": "sine", "rateHz": 2, ... },
    // "env1": { "attack": 0.01, ... }, "routes": [ { "source": "lfo1", "dest": "cutoff", "depth": 0.5 } ] }
    if (json::has (root, "modulation"))
    {
        const auto& m = root["modulation"];
        auto& ms = config.modulation;
        ms.seed = config.engineSettings.seed;
        static const char* shapes[] { "sine", "triangle", "rampup", "rampdown", "pulse", "random", "custom" };
        for (int i = 0; i < 2; ++i)
        {
            const auto& l = m[juce::Identifier ("lfo" + juce::String (i + 1))];
            auto& lfo = ms.lfo[static_cast<std::size_t> (i)];
            const auto shape = juce::String (json::getString (l, "shape", "sine")).toLowerCase().removeCharacters (" _-");
            for (int k = 0; k < mod::lfoShapeCount; ++k)
                if (shape == shapes[k])
                    lfo.shape = static_cast<mod::LfoShape> (k);
            lfo.rateHz = std::clamp (json::getDouble (l, "rateHz", lfo.rateHz), 0.01, 30.0);
            lfo.sync = json::getBool (l, "sync", lfo.sync);
            const auto division = juce::String (json::getString (l, "division", "1/4"));
            for (int k = 0; k < mod::syncDivisionCount; ++k)
                if (division == mod::syncName (k))
                    lfo.division = k;
            lfo.phase = std::clamp (json::getDouble (l, "phase", lfo.phase), 0.0, 1.0);
            lfo.bipolar = json::getBool (l, "bipolar", lfo.bipolar);
            const auto mode = juce::String (json::getString (l, "mode", "free")).toLowerCase();
            lfo.mode = mode == "retrigger" ? mod::LfoMode::retrigger : (mode == "oneshot" || mode == "one shot" ? mod::LfoMode::oneShot : mod::LfoMode::free);
            lfo.scope = juce::String (json::getString (l, "scope", "global")).toLowerCase() == "poly" ? mod::Scope::poly : mod::Scope::global;
            const auto& e = m[juce::Identifier ("env" + juce::String (i + 1))];
            auto& env = ms.env[static_cast<std::size_t> (i)];
            env.oneShotCurve = json::getBool (e, "oneShot", env.oneShotCurve);
            env.attackSeconds = std::max (0.0, json::getDouble (e, "attack", env.attackSeconds));
            env.decaySeconds = std::max (0.0, json::getDouble (e, "decay", env.decaySeconds));
            env.sustain = std::clamp (json::getDouble (e, "sustain", env.sustain), 0.0, 1.0);
            env.releaseSeconds = std::max (0.0, json::getDouble (e, "release", env.releaseSeconds));
            env.curve = std::clamp (json::getDouble (e, "curve", env.curve), -1.0, 1.0);
            env.lengthSeconds = std::max (0.01, json::getDouble (e, "length", env.lengthSeconds));
        }
        if (const auto* routes = m["routes"].getArray())
        {
            int r = 0;
            for (const auto& route : *routes)
            {
                if (r >= mod::maxRoutes)
                    break;
                auto& out = ms.routes[static_cast<std::size_t> (r++)];
                const auto source = juce::String (json::getString (route, "source", "")).toLowerCase().removeCharacters (" ");
                for (int k = 1; k <= mod::sourceCount; ++k)
                    if (source == juce::String (mod::sourceName (static_cast<mod::Source> (k))).toLowerCase().removeCharacters (" "))
                        out.source = static_cast<mod::Source> (k);
                const auto dest = json::getString (route, "dest", "");
                for (int k = 1; k < mod::destCount; ++k)
                    if (dest == mod::destInfo (static_cast<mod::Dest> (k)).id)
                        out.dest = static_cast<mod::Dest> (k);
                out.depth = std::clamp (json::getDouble (route, "depth", 0.0), -1.0, 1.0);
                out.enabled = json::getBool (route, "enabled", true);
            }
        }
    }

    // Layer A's EQ (optional, engine C): "eq": { "enabled": true, "bands": { "hp": { "frequencyHz": 90,
    // "steep": false }, "bell": { "frequencyHz": 3200, "gainDb": -4, "q": 2 }, ... } }; a band
    // listed is on unless it says "enabled": false. Band names: hp, lowShelf, bell, highShelf, lp.
    if (json::has (root, "eq"))
    {
        const auto& e = root["eq"];
        auto& eqs = config.engineSettings.layer[0].eq;
        eqs.enabled = json::getBool (e, "enabled", true);
        static const char* names[] { "hp", "lowShelf", "bell", "highShelf", "lp" };
        const auto& bands = e["bands"];
        for (int b = 0; b < eq::bandCount; ++b)
        {
            if (! json::has (bands, names[b]))
                continue;
            const auto& band = bands[juce::Identifier (names[b])];
            auto& out = eqs.bands[static_cast<std::size_t> (b)];
            const auto range = eq::frequencyRange (static_cast<eq::Band> (b));
            out.enabled = json::getBool (band, "enabled", true);
            out.frequencyHz = std::clamp (json::getDouble (band, "frequencyHz", out.frequencyHz), range.minHz, range.maxHz);
            out.gainDb = std::clamp (json::getDouble (band, "gainDb", out.gainDb), -eq::maxGainDb, eq::maxGainDb);
            out.q = std::clamp (json::getDouble (band, "q", out.q), eq::minBellQ, eq::maxBellQ);
            out.steep = json::getBool (band, "steep", out.steep);
        }
    }

    // The arpeggiator (optional): "arp": { "enabled": true, "pattern": "up", "rate": "1/8", ... }
    if (json::has (root, "arp"))
    {
        const auto& a = root["arp"];
        auto& arpSettings = config.arp.settings;
        arpSettings.enabled = json::getBool (a, "enabled", true);
        const auto pattern = juce::String (json::getString (a, "pattern", "up")).toLowerCase().removeCharacters ("/ _-");
        static const char* patterns[] { "up", "down", "updown", "played", "random", "chord" };
        for (int i = 0; i < arp::patternCount; ++i)
            if (pattern == patterns[i])
                arpSettings.pattern = static_cast<ArpPattern> (i);
        const auto rate = juce::String (json::getString (a, "rate", "1/8")).toUpperCase();
        for (int i = 0; i < arp::rateCount; ++i)
            if (rate == juce::String (arp::rateName (static_cast<ArpRate> (i))))
                arpSettings.rate = static_cast<ArpRate> (i);
        arpSettings.gate = std::clamp (json::getDouble (a, "gate", arpSettings.gate), arp::minGate, arp::maxGate);
        arpSettings.octaves = std::clamp (json::getInt (a, "octaves", arpSettings.octaves), arp::minOctaves, arp::maxOctaves);
        arpSettings.swing = std::clamp (json::getDouble (a, "swing", arpSettings.swing), 0.0, arp::maxSwing);
        config.arp.bpm = std::clamp (json::getDouble (a, "bpm", config.arp.bpm), 20.0, 400.0);
        config.arp.transport = json::getBool (a, "transport", config.arp.transport);
    }

    return config;
}

} // namespace osp::research
