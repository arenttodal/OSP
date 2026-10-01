#include "model/PlaybackPreparation.h"

#include <algorithm>

namespace osp
{

PlaybackPreparation preparePlayback (const AnalysisData& analysis, const PlaybackOptions& options)
{
    PlaybackPreparation result;
    const auto& env = analysis.envelope;
    const bool hasSound = env.maxRmsDbfs > -100.0;

    if (options.startAtOnset && hasSound)
        result.startSeconds = std::max (0.0, env.onsetSeconds - options.onsetPrerollSeconds);

    if (options.normaliseLevel && hasSound)
    {
        const double wanted = options.targetMaxRmsDbfs - env.maxRmsDbfs;
        const double peakRoom = options.peakCeilingDbfs - env.peakDbfs;
        result.gainDb = std::clamp (std::min (wanted, peakRoom), -options.maxCutDb, options.maxBoostDb);
    }
    return result;
}

} // namespace osp
