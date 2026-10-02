#pragma once

#include "core/AudioData.h"

namespace osp
{

struct TransientSeparationOptions
{
    double leadSeconds = 0.03;     ///< analysed before the note start (onset pre-roll)
    double holdSeconds = 0.25;     ///< after the note start, kept at full level
    double fadeSeconds = 0.25;     ///< then faded to silence (the buffer ends here)
    int fftSize = 1024;            ///< ~21 ms at 48 kHz
    int hop = 128;
    int timeMedianFrames = 17;     ///< harmonic smoothing along time (~45 ms)
    int freqMedianBins = 17;       ///< percussive smoothing along frequency (~800 Hz)
};

struct TransientSeparation
{
    /** The broadband (percussive) part of the attack, on the source's own frame grid:
        frame i of `transient` lines up with frame i of the source. Empty when the
        source is too short. */
    AudioData transient;
    /** Energy share of the transient in the first 100 ms after the start (0..1). */
    double share = 0.0;
    /** Where the transient is loudest (seconds from the start of the file): playback
        lines the moved and unmoved transients up here. */
    double peakSeconds = 0.0;
};

/**
    Transient/body separation of a note's onset region (spec §19), offline.

    HPSS-style median filtering of the STFT (Fitzgerald 2010) with soft masks: what is
    smooth along time is the tonal body, what is smooth along frequency is the
    transient (finger, nail, pick, hammer, breath onset). Only the onset region is
    separated; the result is short and fades to silence, so playback can swap the
    transient's pitch treatment for the first fraction of a second without touching
    the body. Allocates; never call from the audio thread.
*/
TransientSeparation separateOnsetTransient (const AudioData& audio, double startSeconds,
                                            const TransientSeparationOptions& options = {});

} // namespace osp
