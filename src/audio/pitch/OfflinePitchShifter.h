#pragma once

#include "core/AudioData.h"

#include <cstdint>

namespace osp
{

/** Offline pitch shifting with Signalsmith Stretch (Phase 2 bake-off engines B and C). */
struct StretchShiftOptions
{
    double semitones = 0.0;
    bool preserveFormants = false;  ///< engine C: keep the spectral envelope while transposing
    double formantBaseHz = 0.0;     ///< source F0 hint for formant analysis (0 = let the library guess)
    std::uint64_t seed = 1;         ///< the library only randomises when time-stretching > 2x; seeded anyway
};

/**
    Transposes a whole recording, keeping its duration and channel count. Not real-time:
    allocates and processes the full buffer (the bake-off compares sound first; real-time
    integration of the winner comes later). Sources shorter than the shifter's latency are
    returned unchanged.
*/
AudioData stretchShiftOffline (const AudioData& source, const StretchShiftOptions& options);

} // namespace osp
