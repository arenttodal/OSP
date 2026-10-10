#pragma once

#include "audio/sampler/BaselineSampler.h"
#include "engine/Arpeggiator.h"
#include "engine/InstrumentEngine.h"
#include "engine/Modulation.h"
#include "model/PlaybackPreparation.h"

#include <juce_core/juce_core.h>

#include <filesystem>
#include <optional>
#include <string>

namespace osp::research
{

/** Engines selectable in the renderer. Every comparison renders against A and B. */
enum class EngineId
{
    baselineA,  ///< plain resampling sampler
    baselineB,  ///< A + independent per-note randomisation
    instrument  ///< C: the OSP instrument engine (continuation, performance, dynamics, macros)
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
    PlaybackOptions playback {};      ///< start-at-onset / level normalisation (off = plain baselines)
    EngineSettings engineSettings {}; ///< engine C only
    bool anchors = false;             ///< engine C: build register anchors (needed for pitchCharacter = natural)

    /** Engine C's modulation (LFOs, envelopes, routes); no routes = none. */
    mod::Settings modulation {};
    double modWheel = 0.0;   ///< the MOD WHEEL source's value for the whole render (0..1)

    /** The arpeggiator ahead of the engine (off: the MIDI is played as written). */
    struct Arp
    {
        Arpeggiator::Settings settings {};
        double bpm = 120.0;
        bool transport = true;   ///< a host playing from bar 1 (a bounce); false: no host, free running
    } arp;

    /** Settings actually used for the engine (B turns randomisation on). */
    SamplerSettings effectiveSamplerSettings() const;
};

/**
    Loads a config JSON (see research/configs). Missing fields keep their defaults;
    unknown fields are ignored. Returns nullopt with an error for unreadable files or a
    newer schemaVersion.
*/
std::optional<RenderConfig> loadRenderConfig (const std::filesystem::path& path, std::string& error);

/** Applies an "instrument" settings object (engine C) to a config: macros, pitchCharacter, continuation... */
void applyInstrumentBlock (const juce::var& block, RenderConfig& config);

} // namespace osp::research
