#pragma once

#include "core/AudioData.h"
#include "midi/MidiEvent.h"
#include "model/AnalysisData.h"
#include "model/PlaybackPreparation.h"
#include "model/RootChoice.h"
#include "research/RenderConfig.h"

#include <optional>
#include <string>

namespace osp::research
{

using osp::RootChoice;
using osp::chooseRoot;

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
                             const RenderConfig& config, const PlaybackPreparation& preparation = {});

} // namespace osp::research
