#pragma once

#include <array>
#include <string>
#include <vector>

namespace osp
{

/**
    The source data REIMAGINED's TAPE FRAME and MOSAIC modes play from (TOYBOX, MIRAGE and
    KALEIDOSCOPE need none). Built once per recording off the audio thread, together with
    the continuation model (instrument::addContinuation), immutable afterwards and shared
    by every voice of the layer. A new recording means a new model and new data; switching
    modes never analyses again. Compact on purpose: references into the source, never a
    copy of its audio.

    Not persisted (always rebuilt from the recording); the schemaVersion documents what a
    consumer may rely on.
*/
struct ReimaginedAnalysis
{
    static constexpr int schemaVersion = 1;

    /** One piece of tape: `length` frames of the recording from `sourceFrame`, laid at
        `frameStart` on the tape. Consecutive splices overlap by the next one's `fadeIn`
        (a crossfade over matched audio, `correlation` of the two windows). */
    struct TapeSplice
    {
        double sourceFrame = 0.0;
        double frameStart = 0.0;
        double length = 0.0;
        double fadeIn = 0.0;
        float correlation = 1.0f;
        float gain = 1.0f;   ///< small per-repeat level difference (a real splice is never identical)
    };

    /** TAPE FRAME: the recording prepared as a finite tape (in source frames). */
    struct TapeFrame
    {
        bool ready = false;
        std::string reason;           ///< how it was made ("body loops", "natural length", ...) or why not
        double sampleRate = 0.0;
        double lengthFrames = 0.0;    ///< tape available (up to the longest FRAME)
        double bodyFrame = 0.0;       ///< tape position where the onset has settled into the body
        std::vector<TapeSplice> splices;
        static constexpr int overviewSize = 96;
        std::array<float, overviewSize> energy {};   ///< RMS over the tape (0..1 of its peak), for the display
    };

    static constexpr int maxPartials = 48;
    static constexpr int residualBands = 6;
    /** Upper edges (Hz) of MOSAIC's residual noise bands (the first starts at 40 Hz). */
    static constexpr std::array<double, residualBands> residualEdgesHz { 400.0, 1000.0, 2500.0, 5000.0, 9000.0, 18000.0 };

    /** One harmonic snapshot: partial amplitudes (linear, re the source's full scale) at the
        frame's own fundamental, the noise left between them and the frame's loudness. */
    struct MosaicFrame
    {
        float seconds = 0.0f;   ///< centre, from the onset
        float f0Ratio = 1.0f;   ///< this frame's fundamental re fundamentalHz
        float level = 0.0f;     ///< RMS (linear)
        float centroidHz = 0.0f;
        std::array<float, maxPartials> partial {};
        std::array<float, residualBands> residual {};   ///< noise RMS per band (linear)
    };

    /** MOSAIC: the recording as 16-32 harmonic frames. */
    struct Mosaic
    {
        bool ready = false;
        std::string reason;
        double fundamentalHz = 0.0;
        double inharmonicity = 0.0;   ///< B in f_h = h f0 sqrt (1 + B h^2) (0 = harmonic)
        int stableFrame = 0;          ///< the frame a held note settles on (low MOTION)
        int bodyFrame = 0;            ///< first frame after the attack
        bool sustains = false;        ///< the recording has a stable region (it may be held)
        double residualShare = 0.0;   ///< noise energy re total over the body (0..1)
        std::vector<MosaicFrame> frames;
    };

    TapeFrame tape;
    Mosaic mosaic;
};

} // namespace osp
