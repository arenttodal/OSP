#pragma once

#include "engine/EchoDelay.h"
#include "engine/SpaceReverb.h"

namespace osp::andor
{

/**
    The shared effects of the ANDOR Lab instruments: OSP's SPACE (hall) and ECHO (tape) as
    sends after the voices, reused unchanged. One instance per instrument (not per voice).
    A send at 0 whose tail has died costs nothing. prepare() allocates; process() is
    real-time safe.
*/
class SharedFx
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    /** In place. `space` / `echo` 0..1 are send levels; `tempoBpm` > 0 syncs nothing yet (free time). */
    void process (float* left, float* right, int numSamples, double space, double echo) noexcept;

private:
    SpaceReverb reverb;
    EchoDelay delay;
    double rate = 48000.0;
    int reverbQuiet = 0, echoQuiet = 0;   ///< samples of silence in and out (sleep after a while)
    bool prepared = false;
};

} // namespace osp::andor
