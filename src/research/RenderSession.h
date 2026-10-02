#pragma once

#include "core/AudioData.h"
#include "midi/MidiEvent.h"
#include "model/AnalysisData.h"
#include "model/InstrumentModel.h"
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

/**
    Renders with whichever engine the config selects. Engine C builds its instrument
    model from the source and analysis first (always start-at-onset and level matched,
    like the plugin); A and B use `preparation` as given.
*/
RenderOutput renderWithEngine (const AudioData& source, const AnalysisData& analysis, double rootMidi, const MidiSequence& sequence,
                               const RenderConfig& config, const PlaybackPreparation& preparation);

/** Same block loop through the OSP instrument engine (EngineId::instrument). */
RenderOutput renderInstrument (const InstrumentModel& model, const MidiSequence& sequence, const RenderConfig& config);

} // namespace osp::research
