#pragma once

#include "model/AnalysisData.h"

namespace osp
{

/** How a recording is prepared for playback. Both options are non-destructive. */
struct PlaybackOptions
{
    /** Start notes just before the analysed onset instead of at sample 0 (skips silence,
        bellows/breath pre-roll before the tone speaks). */
    bool startAtOnset = false;
    double onsetPrerollSeconds = 0.03;  ///< start this much before the onset so the attack is kept

    /** Match the loudest 20 ms RMS of every source to targetMaxRmsDbfs (peak-limited). */
    bool normaliseLevel = false;
    double targetMaxRmsDbfs = -16.0;
    double peakCeilingDbfs = -1.0;      ///< never push the source's peak above this
    double maxBoostDb = 36.0;
    double maxCutDb = 12.0;
};

struct PlaybackPreparation
{
    double startSeconds = 0.0;
    double gainDb = 0.0;
};

/**
    Derives start offset and playback gain from analysis. Pure and deterministic; with
    default options it returns {0, 0}, i.e. plain playback of the file as recorded
    (baselines A and B).
*/
PlaybackPreparation preparePlayback (const AnalysisData& analysis, const PlaybackOptions& options);

} // namespace osp
