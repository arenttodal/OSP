#pragma once

#include "analysis/AnalysisFrames.h"
#include "core/AudioData.h"
#include "model/AnalysisData.h"

namespace osp
{

/**
    Runs the Phase 0 analysis pipeline:

        mono framing + RMS -> pitch (YIN) -> envelope -> spectrum (+ flux) -> onsets -> stereo

    Pure function of the audio and options: deterministic, no I/O, never throws for
    odd input (silence, noise, tiny or huge files). Problems are reported in
    AnalysisData::warnings. Expensive; never call on the audio thread.
*/
class Analyzer
{
public:
    static AnalysisData analyse (const AudioData& audio, const AnalysisOptions& options = {});
};

} // namespace osp
