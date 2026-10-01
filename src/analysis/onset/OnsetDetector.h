#pragma once

#include "analysis/AnalysisFrames.h"
#include "model/AnalysisData.h"

#include <vector>

namespace osp
{

/**
    Peak-picks the spectral-flux curve (adaptive median threshold with a 3 dB/bin
    absolute floor, 50 ms minimum spacing, active frames only). Produces candidate articulation onsets; the primary
    onset time lives in EnvelopeAnalysis::onsetSeconds.
*/
class OnsetDetector
{
public:
    static constexpr std::size_t maxOnsets = 64;

    static std::vector<Onset> detect (const AnalysisFrames& frames, const std::vector<double>& flux);
};

} // namespace osp
