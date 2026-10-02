#pragma once

#include "engine/Shaping.h"
#include "engine/ShelfFilter.h"

namespace osp
{

/**
    The per-voice CHARACTER filter (shaping system v1.0 §23-31).

      LP24  zero-delay-feedback transistor-ladder low-pass (Zavalishin's topology) with a
            soft, slightly asymmetric saturator at its input: rounded resonance, warm
            when driven. The signature filter.
      LP12, HP12, BP12  a trapezoidal state-variable filter with input drive; resonance
            is gentler on HP and BP by design.
      TILT  a broadband dark <-> bright tilt (two shelves around ~800 Hz), no resonance.

    setParameters() runs at control rate (coefficients), process() per sample. Changing
    type crossfades the old and new filter over 40 ms, so it never clicks. Real-time safe.
*/
class CharacterFilter
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** @param cutoffHz  for TILT: ignored; @param tiltDb dark (-) .. bright (+), TILT only. */
    void setParameters (FilterType type, double cutoffHz, double resonance, double drive, double tiltDb) noexcept;
    void process (float& left, float& right) noexcept;

    FilterType type() const noexcept { return current; }

private:
    struct Ladder
    {
        float s[4] {};
    };
    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f;
    };

    float ladder (Ladder& st, float x) const noexcept;
    void svf (Svf& st, float x, float& low, float& band, float& high) const noexcept;

    double sampleRate = 48000.0;
    FilterType current = FilterType::lp24, previous = FilterType::lp24;
    int fadeRemaining = 0, fadeLength = 1;

    // Ladder (LP24)
    float G = 0.5f, k = 0.0f, comp = 1.0f;
    // SVF (LP12/HP12/BP12)
    float g = 0.5f, kSvf = 1.4f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    // Drive (shared)
    float driveGain = 1.0f, driveBias = 0.0f, driveOut = 1.0f;
    // TILT
    ShelfFilter tiltLowL, tiltLowR, tiltHighL, tiltHighR;
    float appliedTilt = 1.0e9f;

    Ladder ladderL, ladderR;
    Svf svfL, svfR;
};

} // namespace osp
