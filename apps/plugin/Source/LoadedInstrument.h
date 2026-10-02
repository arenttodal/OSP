#pragma once

#include "core/AudioData.h"
#include "model/AnalysisData.h"
#include "model/InstrumentModel.h"

#include <memory>

#include <cstdint>
#include <string>
#include <vector>

namespace osp::plugin
{

/**
    Everything the plugin needs for one dropped sample. Built on the loader thread,
    then immutable. The audio thread only touches `model` (through the engine) and
    `generation` (through ModelExchange). One load publishes up to three of these, one
    per model stage (spec §62); they share audio buffers.
*/
struct LoadedInstrument
{
    std::uint64_t generation = 0;
    std::uint64_t loadId = 0;        ///< the load request this belongs to (stages share it)

    std::string contentHash;     ///< "sha256:<hex>"
    std::string filename;        ///< original file name (metadata only)
    std::string originalPath;    ///< where it was dropped from (metadata, fallback for recall)
    std::string storedPath;      ///< copy in the managed sample store

    AnalysisData analysis;
    double analysisRootMidi = 60.0;  ///< root the PlaybackSource was built with
    std::string rootOrigin;          ///< "analysis", "analysis-low-confidence", "fallback"

    std::shared_ptr<const InstrumentModel> model;
    double startSeconds = 0.0;       ///< where notes start reading (analysed onset minus pre-roll)
    double playbackGainDb = 0.0;     ///< non-destructive level match

    /** Waveform overview: min/max per bucket of the mono mix. */
    std::vector<float> peakMin;
    std::vector<float> peakMax;
    double durationSeconds = 0.0;

    /** Short descriptor line for the UI, e.g. "SUSTAINED · TONAL". */
    std::string character;
};

} // namespace osp::plugin
