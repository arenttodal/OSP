#pragma once

#include "core/AudioData.h"

#include <cstdint>
#include <vector>

namespace osp
{

struct AnalysisOptions
{
    double hopSeconds = 0.010;
    double rmsWindowSeconds = 0.020;
    double minF0Hz = 30.0;
    double maxF0Hz = 4000.0;
    double yinThreshold = 0.15;
    double maxAnalysisSeconds = 120.0;  ///< longer sources are analysed over their first N seconds
    double activeRangeDb = 40.0;        ///< frames within this many dB of max RMS count as "active"
};

/**
    Shared framing for all analysis stages: a mono mixdown plus per-frame RMS on a common
    hop, so every stage's time series lines up (frame i is centred at i * hopSeconds).
*/
struct AnalysisFrames
{
    double sampleRate = 0.0;
    std::vector<float> mono;
    int hop = 0;
    double hopSeconds = 0.0;
    int numFrames = 0;
    std::vector<double> rms;      ///< linear RMS of a centred window
    double maxRms = 0.0;
    std::vector<bool> active;     ///< rms within activeRangeDb of maxRms (and above -100 dBFS)

    static AnalysisFrames build (const AudioData& audio, const AnalysisOptions& options);

    std::int64_t frameCentre (int frame) const noexcept { return static_cast<std::int64_t> (frame) * hop; }
    double frameTime (int frame) const noexcept { return frame * hopSeconds; }
    bool anyActive() const noexcept { return maxRms > 1.0e-5; }
};

} // namespace osp
