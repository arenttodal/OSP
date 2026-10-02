#pragma once

#include "core/AudioData.h"
#include "model/AnalysisData.h"
#include "model/ContinuationModel.h"

#include <cmath>

namespace osp
{

struct ContinuationOptions
{
    double minLoopSeconds = 0.35;      ///< shortest jump distance
    double minSegmentSeconds = 0.25;   ///< shortest playback between two jumps
    int maxJumps = 32;
    int maxGraftExits = 96;
    double maxDecayDbPerSecond = 6.0;  ///< steeper bodies are treated as one-shot (plucks)
    double minRegionSeconds = 0.5;
};

/**
    Finds a stable region and a set of compatible jump points in it (spec §38, §39).

    Candidates are compared on log-band spectra, level, level slope (so a tremolo
    continues in phase), F0 and F0 slope (vibrato phase); the best are aligned by
    waveform cross-correlation and given a correlation-aware crossfade length.
    Never throws on odd input: silence, noise or very short sources give canSustain =
    false with a reason. Offline only (allocates; may take tens of milliseconds).
*/
ContinuationModel analyseContinuation (const AudioData& audio,
                                       const AnalysisData& analysis,
                                       const ContinuationOptions& options = {});

/**
    Re-aligns every jump of `base` for another buffer with the same timing (a register
    anchor rendered with duration preserved). Phase differs between buffers, so each
    jump's destination is re-searched locally and its correlation re-measured.
*/
ContinuationModel refineContinuationForLayer (const ContinuationModel& base, const AudioData& layer, double layerF0Hz);

/** Correlation-aware crossfade gains at progress s in [0, 1] (spec §38 "phase compatibility"). */
inline void crossfadeGains (float s, float correlation, float& outGain, float& inGain) noexcept
{
    // Linear fades keep the amplitude of identical signals constant; for uncorrelated
    // signals the sum needs sqrt(2) more at the midpoint. Normalising by the expected
    // amplitude of the mix handles everything in between.
    const float a = 1.0f - s;
    const float b = s;
    const float r = correlation < 0.0f ? 0.0f : (correlation > 1.0f ? 1.0f : correlation);
    const float norm = a * a + b * b + 2.0f * a * b * r;
    const float k = norm > 1.0e-9f ? 1.0f / std::sqrt (norm) : 1.0f;
    outGain = a * k;
    inGain = b * k;
}

} // namespace osp
