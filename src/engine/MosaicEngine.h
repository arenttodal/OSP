#pragma once

#include "core/Prng.h"
#include "engine/ReimaginedEngine.h"

#include <array>

namespace osp
{

/**
    MOSAIC: the recording rebuilt from its harmonic fingerprint instead of replayed.

    Analysis (ReimaginedAnalysis::Mosaic) cuts the sound into 16-32 harmonic frames:
    partial amplitudes at each frame's own fundamental, the measured inharmonicity, and
    the noise left between the partials in six bands. A note resynthesises them at its own
    pitch (the played fundamental times the recording's partial pattern), so the timbre
    holds far beyond the range a resampler stretches well, and time no longer depends on
    pitch. Frames are interpolated continuously, never stepped.

    DETAIL sets how many partials (8-48) and how much of the fine spectral detail survives
    (low: a smoother, cleaner envelope). MOTION sets the travel through the frames: low
    settles on one stable frame, high keeps moving through the recording's own evolution
    (a ping-pong over its body). MODEL: PURE is mostly harmonic; TEXTURED keeps the
    residual noise, the inharmonicity and a little partial detune, the source's breath.

    Amount: 0-25 % crossfade towards the reconstruction (the recording's own attack stays
    at the front); 25-60 % the harmonic model dominates; 60-85 % the travel through frames
    grows; 85-100 % a slowly moving spectral emphasis abstracts it further, on pitch.

    Sources without a stable pitch cannot be rebuilt: those notes play the recording.
*/
class MosaicEngine final : public ReimaginedVoiceEngine
{
public:
    void prepare (double outputRate) noexcept override;
    bool start (const ReimaginedNote& note, const ReimaginedControl& control) noexcept override;
    void control (const ReimaginedControl& control) noexcept override;
    /** Until the reconstruction has fully taken over (then the plain read is not needed). */
    bool wantsDryRead() const noexcept override { return ! (mix >= 1.0f && mixStep >= 0.0f); }
    void render (const float* dryL, const float* dryR, float* outL, float* outR, int n) noexcept override;
    bool finished() const noexcept override { return done; }

private:
    static constexpr int partials = ReimaginedAnalysis::maxPartials;
    static constexpr int bands = ReimaginedAnalysis::residualBands;

    struct Biquad
    {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;
        float process (float x) noexcept
        {
            const float y = b0 * x + s1;
            s1 = b1 * x - a1 * y + s2;
            s2 = b2 * x - a2 * y;
            return y;
        }
    };
    double naturalFrame (double seconds) const noexcept;
    float frameValue (double position, int partial) const noexcept;

    ReimaginedNote note;
    const ReimaginedAnalysis::Mosaic* mosaic = nullptr;
    double rate = 48000.0;
    double amount = 0.0;
    Prng rng;
    bool done = false;
    double seconds = 0.0;      ///< since the note started (real time)
    double position = 0.0;     ///< spectral position (fractional frame)
    double step = 1.0;

    // Partials (rotating phasors, renormalised at control rate). Stored odd partials first
    // (1, 3, 5 ...), then even ones, so each half is one contiguous, vectorisable loop.
    static constexpr int half = partials / 2;
    static constexpr std::size_t slotOf (int h) noexcept   ///< h = 1..partials
    {
        return static_cast<std::size_t> ((h & 1) != 0 ? (h - 1) / 2 : half + h / 2 - 1);
    }
    std::array<float, partials> re {}, im {}, rotRe {}, rotIm {}, amp {}, ampStep {}, out {};
    std::array<float, partials> detune {};
    int oddCount = 0, evenCount = 0;   ///< partials sounding in each half
    double emphasisPhase = 0.0;

    // Residual noise bands per channel, each normalised to unit gain for white noise.
    std::array<Biquad, 2 * bands> noise;
    std::array<float, bands> noiseNorm {};
    std::array<float, bands> noiseLevel {}, noiseStep {};

    float mix = 0.0f, mixStep = 0.0f;
    float evenLeft = 1.0f, oddLeft = 0.85f;
};

} // namespace osp
