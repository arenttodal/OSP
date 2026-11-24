#pragma once

#include <vector>

namespace osp
{

/**
    Halves a signal's sample rate (the octave-down copies a PlaybackSource keeps for reads far
    above a recording's pitch). A zero-phase Kaiser-windowed low-pass (cutoff a quarter of the
    input rate, 33 taps, about -100 dB stopband from 3/8 of it), then every other sample:
    output sample k is input sample 2k filtered, so positions scale by exactly 1/2.

    Clean up to 1/8 of the input rate (a quarter of the output's own Nyquist band is all a
    read more than an octave faster ever takes from it); what lies above may alias into the
    output's upper half, which those reads never use. Allocates: off the audio thread only.
*/
std::vector<float> decimateByTwo (const std::vector<float>& input);

} // namespace osp
