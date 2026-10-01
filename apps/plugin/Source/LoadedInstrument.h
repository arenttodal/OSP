#pragma once

#include "core/AudioData.h"
#include "model/AnalysisData.h"
#include "model/PlaybackSource.h"

#include <cstdint>
#include <string>
#include <vector>

namespace osp::plugin
{

/**
    Everything the plugin needs for one dropped sample. Built on the loader thread,
    then immutable. The audio thread only touches `playback` (through the sampler) and
    `generation` (through ModelExchange).
*/
struct LoadedInstrument
{
    std::uint64_t generation = 0;

    std::string contentHash;     ///< "sha256:<hex>"
    std::string filename;        ///< original file name (metadata only)
    std::string originalPath;    ///< where it was dropped from (metadata, fallback for recall)
    std::string storedPath;      ///< copy in the managed sample store

    AnalysisData analysis;
    double analysisRootMidi = 60.0;  ///< root the PlaybackSource was built with
    std::string rootOrigin;          ///< "analysis", "analysis-low-confidence", "fallback"

    PlaybackSource playback;

    /** Waveform overview: min/max per bucket of the mono mix. */
    std::vector<float> peakMin;
    std::vector<float> peakMax;
    double durationSeconds = 0.0;

    /** Short descriptor line for the UI, e.g. "SUSTAINED · TONAL". */
    std::string character;
};

} // namespace osp::plugin
