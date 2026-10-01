#pragma once

#include "core/AudioData.h"
#include "model/AnalysisData.h"

namespace osp
{

/**
    Stereo image descriptors. Never collapses the source; this only measures it so we
    can later tell whether processing damaged the original stereo character.
*/
class StereoAnalyzer
{
public:
    static StereoAnalysis analyse (const AudioData& audio, double maxAnalysisSeconds);
};

} // namespace osp
