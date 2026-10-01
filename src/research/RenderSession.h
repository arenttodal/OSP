#pragma once

#include "core/AudioData.h"
#include "midi/MidiEvent.h"
#include "model/AnalysisData.h"
#include "research/RenderConfig.h"

#include <optional>
#include <string>

namespace osp::research
{

/** Which root the renderer played the source at, and why. */
struct RootChoice
{
    double rootMidi = 60.0;     ///< fractional: includes the detected cents offset
    std::string origin;         ///< "override", "analysis", "analysis-low-confidence", "fallback"
    double sourceF0Hz = 0.0;    ///< measured source F0 (0 if unknown); used to predict output pitch
};

/**
    Resolves the playback root: explicit override > detected pitch > low-confidence
    estimate > C4 fallback. A low-confidence choice is labelled so it is never mistaken
    for a certain one.
*/
RootChoice chooseRoot (const AnalysisData* analysis, std::optional<double> overrideMidi);

struct RenderOutput
{
    AudioData audio;            ///< always 2 channels at the output rate
    double lastEventSeconds = 0.0;
    int blocks = 0;
};

/**
    Renders a MIDI sequence through the configured engine, block by block exactly as a
    host would call it (events are applied sample-accurately inside blocks). Stops when
    all events are processed and every voice has finished, or after maxTailSeconds.

    Deterministic: identical inputs and config produce identical samples.
*/
RenderOutput renderSequence (const AudioData& source, double rootMidi, const MidiSequence& sequence,
                             const RenderConfig& config);

} // namespace osp::research
