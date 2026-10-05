#pragma once

#include "model/AnalysisData.h"

#include <algorithm>
#include <cmath>

namespace osp
{

/**
    FOLLOW off (a layer's source modifier): the recording's own loudness contour is
    flattened, so a decaying sound plays on like a sustained one before the instrument's
    ADSR shapes it. Uses the analysis' 20 ms RMS series (no work on the audio thread
    beyond a table read). Only boosts, never cuts: quiet parts are lifted towards the
    recording's loudest level, at most +24 dB, and not at all near its noise floor (so
    trailing silence stays silent).
*/
namespace levelContour
{
    constexpr double maxBoostDb = 24.0;

    /** The boost (dB, >= 0) at `fraction` (0..1) of a recording `durationSeconds` long. */
    inline double boostDb (const EnvelopeAnalysis& envelope, double durationSeconds, double fraction) noexcept
    {
        const auto& series = envelope.rmsDb;
        if (series.values.empty() || series.hopSeconds <= 0.0 || durationSeconds <= 0.0)
            return 0.0;
        const double t = std::clamp (fraction, 0.0, 1.0) * durationSeconds / series.hopSeconds;
        const auto last = static_cast<double> (series.values.size() - 1);
        const double x = std::clamp (t, 0.0, last);
        const auto i = static_cast<std::size_t> (x);
        const double f = x - static_cast<double> (i);
        const double a = series.values[i];
        const double b = series.values[std::min (i + 1, series.values.size() - 1)];
        const double level = a + (b - a) * f;
        const double reference = envelope.maxRmsDbfs;
        // Full boost down to 45 dB under the loudest frame, none below 60 dB (noise floor).
        const double weight = std::clamp ((level - (reference - 60.0)) / 15.0, 0.0, 1.0);
        return weight * std::clamp (reference - level, 0.0, maxBoostDb);
    }
}

} // namespace osp
