#pragma once

#include "core/AudioData.h"
#include "model/AnalysisData.h"
#include "model/ContinuationModel.h"
#include "model/ReimaginedAnalysis.h"

namespace osp
{

/**
    Prepares REIMAGINED's TAPE FRAME and MOSAIC data for one recording (offline, allocates;
    a few tens of milliseconds for a typical sample). Reuses the analysis and the
    continuation model rather than analysing again: the tape is spliced at the
    continuation's matched jumps, MOSAIC follows the pitch track.

    Odd input never throws: a part that cannot be made reports why (not ready) and the
    voices fall back to the plain recording.

    @param startFrame   where notes start reading (the onset)
*/
ReimaginedAnalysis analyseReimagined (const AudioData& audio, const AnalysisData& analysis, const ContinuationModel& continuation,
                                      double startFrame);

} // namespace osp
