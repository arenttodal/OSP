#pragma once

#include "analysis/AnalysisFrames.h"
#include "analysis/pitch/PitchAnalyzer.h"
#include "model/AnalysisData.h"

#include <vector>

namespace osp
{

/**
    STFT descriptors on the common analysis hop (Hann window, ~40 ms).
    Aggregates are RMS-weighted means over active frames. Also produces the raw
    spectral-flux curve used by OnsetDetector.
*/
class SpectralAnalyzer
{
public:
    static SpectralAnalysis analyse (const AnalysisFrames& frames, const PitchFrames& pitch,
                                     std::vector<double>& fluxOut);

    /** Mean spectral centroid of a mono signal (used by render metrics). */
    static double meanCentroid (const std::vector<float>& mono, double sampleRate, double* stdOut = nullptr);
};

} // namespace osp
