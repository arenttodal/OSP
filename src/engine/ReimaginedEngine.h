#pragma once

#include "audio/pitch/SincInterpolator.h"
#include "engine/ReimaginedModes.h"
#include "model/InstrumentModel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace osp
{

/** A layer's live REIMAGINED state as voices read it (written by the engine every block). */
struct ReimaginedLive
{
    double amount = 0.0;            ///< the layer's REIMAGINED, 0..1
    ReimaginedSettings settings;    ///< mode and every mode's settings
};

/** What a mode engine gets when its note starts. */
struct ReimaginedNote
{
    const InstrumentModel* model = nullptr;
    const PlaybackSource* source = nullptr;          ///< the recording (the model's original layer)
    const ReimaginedAnalysis* analysis = nullptr;    ///< null while the model is provisional
    const SincInterpolator* sinc = nullptr;
    SincInterpolator::Kernel* kernel = nullptr;      ///< the voice's scratch kernel (used one read at a time)
    double outputRate = 48000.0;
    double startFrame = 0.0;     ///< where the read begins (START applied)
    double rootStep = 1.0;       ///< source frames per output sample at the recording's own pitch
    bool reverse = false;
    /** LOOP: a held note sustains (on loops, a rewound tape, a held spectrum); off: it plays
        the recording once and ends, whichever way it reads. */
    bool loop = true;
    bool granular = false;       ///< the layer plays grains: the engine transforms them instead of reading
    double velocity = 0.8;       ///< 0..1, after the DYNAMICS curve
    int note = 60;
    std::uint64_t seed = 1;      ///< the note's own seed (LIFE: a take repeats exactly)
};

/** Live inputs, once per control period (32 samples). */
struct ReimaginedControl
{
    double amount = 0.0;         ///< 0..1 (the voice smooths it)
    const ReimaginedSettings* settings = nullptr;
    double step = 1.0;           ///< source frames per output sample now (pitch, bend, glide, LIFE), before stepFactor()
};

/**
    The common contract of the REIMAGINED mode engines that replace or rebuild a voice's
    source signal (TAPE FRAME, TOYBOX, MOSAIC, MIRAGE). One instance of each lives inline
    in every voice (preallocated); a note uses the one its layer's mode named when it
    started, so switching modes never touches sounding notes. KALEIDOSCOPE is the voice's
    own native path plus the layer's ReimaginedStage (KaleidoscopeEngine), left exactly as
    it always was.

    The voice keeps everything around it: LIFE, DYNAMICS and the ADSR (note-off follows
    the instrument's envelope), CHARACTER, the shelves, pan and level.

    Real-time safe: prepare() runs off the audio thread; everything else never allocates,
    locks or reads files.
*/
class ReimaginedVoiceEngine
{
public:
    virtual ~ReimaginedVoiceEngine() = default;

    virtual void prepare (double outputRate) noexcept = 0;
    /** False when it cannot play this note (its analysis is not ready): the voice then plays
        the plain recording. */
    virtual bool start (const ReimaginedNote& note, const ReimaginedControl& control) noexcept = 0;
    virtual void release() noexcept {}
    /** Control rate: the live amount and settings. */
    virtual void control (const ReimaginedControl& control) noexcept = 0;
    /** Factor on the note's read step for the coming control period (wow, self-modulation). */
    virtual double stepFactor() const noexcept { return 1.0; }
    /** True when it needs the voice's plain read of the recording as `dry` (One Shot). */
    virtual bool wantsDryRead() const noexcept { return false; }
    /** n <= 32 samples of the source signal. `dry`: the plain read (when wanted) or the
        grains (Granular), else silence. */
    virtual void render (const float* dryL, const float* dryR, float* outL, float* outR, int n) noexcept = 0;
    /** Where it reads the recording now (source frames), for the display; < 0: nowhere. */
    virtual double sourcePosition() const noexcept { return -1.0; }
    /** The note has nothing left to play (the voice ends it). */
    virtual bool finished() const noexcept { return false; }
};

namespace reimagined
{
    /** One-pole low-pass, coefficient from a cutoff. */
    struct OnePole
    {
        float a = 1.0f, z = 0.0f;
        void setCutoff (double hz, double rate) noexcept
        {
            a = static_cast<float> (1.0 - std::exp (-2.0 * 3.14159265358979 * std::clamp (hz, 5.0, 0.49 * rate) / rate));
        }
        float process (float x) noexcept { return z += a * (x - z); }
        void reset (float v = 0.0f) noexcept { z = v; }
    };

    /** Bandlimited read of the recording at `pos` (both channels). */
    inline void readSinc (const ReimaginedNote& n, double pos, double step, float& l, float& r) noexcept
    {
        n.sinc->computeKernelFast (pos, std::max (step, 1.0e-6), *n.kernel);
        l = SincInterpolator::apply (*n.kernel, n.source->channelData (0));
        r = n.source->numChannels() > 1 ? SincInterpolator::apply (*n.kernel, n.source->channelData (1)) : l;
    }

    /** Cheap 4-point read (for lowpassed or deliberately primitive heads). */
    inline float readHermite (const PlaybackSource& src, int channel, double pos) noexcept
    {
        const auto last = static_cast<double> (std::max<std::int64_t> (2, src.numFrames() - 3));
        pos = std::clamp (pos, 1.0, last);
        const auto i = static_cast<std::int64_t> (pos);
        const float t = static_cast<float> (pos - static_cast<double> (i));
        const float* x = src.channelData (channel) + i;
        const float c0 = x[0], c1 = 0.5f * (x[1] - x[-1]);
        const float c2 = x[-1] - 2.5f * x[0] + 2.0f * x[1] - 0.5f * x[2];
        const float c3 = 0.5f * (x[2] - x[-1]) + 1.5f * (x[0] - x[1]);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    /** Soft saturation with unity small-signal gain. */
    inline float soft (float x, float drive) noexcept { return drive > 1.0f ? std::tanh (drive * x) / drive : x; }

    /** The live amount, glided at control rate (~25 ms). */
    inline double glide (double now, double target) noexcept { return now + (target - now) * 0.08; }
}

} // namespace osp
