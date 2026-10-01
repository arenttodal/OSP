#pragma once

#include "analysis/AnalysisFrames.h"
#include "model/AnalysisData.h"

#include <vector>

namespace osp
{

/** Per-frame YIN output kept for later stages (spectral harmonicity uses it). */
struct PitchFrames
{
    std::vector<double> hz;            ///< raw estimate per frame (0 = silent)
    std::vector<double> periodicity;   ///< 1 - aperiodicity, 0..1
    std::vector<bool> voiced;          ///< active && periodicity >= voicedThreshold
};

/**
    Root/F0 analysis: frame-level YIN, then a robust aggregate.

    Aggregation (documented in docs/analysis-schema.md):
      - voiced frame: active (within 40 dB of max RMS) and periodicity >= 0.5
      - stable F0: median of voiced frames in the log-frequency domain, weighted by
        periodicity * RMS
      - confidence = agreement * meanPeriodicity, where agreement is the RMS-weighted
        fraction of *active* frames that are voiced and within 100 cents of the stable F0.
        Silence, noise, octave-jumping and pitch-gliding material therefore score low.
*/
class PitchAnalyzer
{
public:
    static constexpr double voicedThreshold = 0.5;
    static constexpr double agreementCents = 100.0;
    static constexpr double highConfidence = 0.7;
    static constexpr double moderateConfidence = 0.4;

    static PitchAnalysis analyse (const AnalysisFrames& frames, const AnalysisOptions& options, PitchFrames& framesOut);
};

} // namespace osp
