#pragma once

#include "analysis/AnalysisFrames.h"
#include "model/AnalysisData.h"

namespace osp
{

/**
    Energy-trajectory analysis. Describes what the envelope does (silences, onset,
    attack, peak, how it evolves, whether it is still sounding at the end) without
    assuming an attack -> exponential decay model. Onset *list* is filled later by
    OnsetDetector.
*/
class EnvelopeAnalyzer
{
public:
    static EnvelopeAnalysis analyse (const AudioData& audio, const AnalysisFrames& frames);
};

} // namespace osp
