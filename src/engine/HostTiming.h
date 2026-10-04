#pragma once

namespace osp
{

/**
    One canonical snapshot of the host's musical time at the start of a block (MOVEMENT
    v2). The plugin fills it once per processBlock from the play head; research renders
    and tests leave it invalid (stopped) or set it explicitly. Everything tempo-synced
    derives its phase from this, never from its own counters, while the host plays.
*/
struct HostTiming
{
    bool valid = false;       ///< the host reported a musical position
    bool playing = false;     ///< transport running
    double bpm = 120.0;
    double ppq = 0.0;         ///< quarter notes since the song start, at the block's first sample
    int numerator = 4;
    int denominator = 4;
};

} // namespace osp
