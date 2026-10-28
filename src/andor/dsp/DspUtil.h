#pragma once

#include "core/Prng.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace osp::andor
{

constexpr double pi = 3.141592653589793;
constexpr double twoPi = 6.283185307179586;

/** One-pole smoother towards a target (zipper-free parameter changes). Real-time safe. */
class Smoother
{
public:
    void prepare (double sampleRate, double timeSeconds) noexcept
    {
        coefficient = timeSeconds > 0.0 ? std::exp (-1.0 / (timeSeconds * sampleRate)) : 0.0;
    }
    void reset (double value) noexcept { current = target = value; }
    void setTarget (double value) noexcept { target = value; }
    double next() noexcept
    {
        current = target + coefficient * (current - target);
        return current;
    }
    /** Advances `samples` steps at once (control-rate use). */
    double advance (int samples) noexcept
    {
        current = target + std::pow (coefficient, samples) * (current - target);
        return current;
    }
    double value() const noexcept { return current; }
    double targetValue() const noexcept { return target; }

private:
    double coefficient = 0.0, current = 0.0, target = 0.0;
};

/**
    Topology-preserving-transform state-variable filter (Zavalishin), stereo. Stable under fast
    cutoff modulation, so it is safe as a per-voice, per-sample modulated tone control.
*/
class Svf
{
public:
    enum class Mode { lowPass, bandPass, highPass };

    void reset() noexcept { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }

    void setCoefficients (double cutoffHz, double q, double sampleRate) noexcept
    {
        const double fc = std::clamp (cutoffHz, 10.0, 0.49 * sampleRate);
        g = static_cast<float> (std::tan (pi * fc / sampleRate));
        k = static_cast<float> (1.0 / std::max (0.1, q));
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    float process (float x, int channel, Mode mode = Mode::lowPass) noexcept
    {
        auto& s1 = ic1[channel];
        auto& s2 = ic2[channel];
        const float v3 = x - s2;
        const float v1 = a1 * s1 + a2 * v3;
        const float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        switch (mode)
        {
            case Mode::lowPass: return v2;
            case Mode::bandPass: return v1;
            case Mode::highPass: return x - k * v1 - v2;
        }
        return v2;
    }

private:
    float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1[2] {}, ic2[2] {};
};

/** Equal-power pan law: -1 left .. 1 right. */
inline void panGains (double pan, float& left, float& right) noexcept
{
    const double angle = (std::clamp (pan, -1.0, 1.0) + 1.0) * 0.25 * pi;
    left = static_cast<float> (std::cos (angle));
    right = static_cast<float> (std::sin (angle));
}

/** 4-point cubic Hermite read of a padded channel (needs 1 sample before, 2 after). */
inline float hermite (const float* data, double position) noexcept
{
    const auto i = static_cast<std::int64_t> (std::floor (position));
    const float f = static_cast<float> (position - static_cast<double> (i));
    const float xm1 = data[i - 1], x0 = data[i], x1 = data[i + 1], x2 = data[i + 2];
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

/** Low-frequency oscillator. Shapes are bounded to -1..1. Sample & hold is seeded. */
class Lfo
{
public:
    enum class Shape { sine, triangle, saw, square, sampleHold };

    void setShape (Shape s) noexcept { shape = s; }
    void reset (double phase01, std::uint64_t seed) noexcept
    {
        phase = phase01 - std::floor (phase01);
        rng.reseed (seed);
        held = rng.bipolar();
    }
    /** Advances by `seconds` at `hz` and returns the new value. */
    double advance (double hz, double seconds) noexcept
    {
        phase += hz * seconds;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            held = rng.bipolar();
        }
        return value();
    }
    double value() const noexcept
    {
        switch (shape)
        {
            case Shape::sine: return std::sin (twoPi * phase);
            case Shape::triangle: return 1.0 - 4.0 * std::abs (phase - 0.5);
            case Shape::saw: return 2.0 * phase - 1.0;
            case Shape::square: return phase < 0.5 ? 1.0 : -1.0;
            case Shape::sampleHold: return held;
        }
        return 0.0;
    }

private:
    Shape shape = Shape::sine;
    double phase = 0.0;
    double held = 0.0;
    Prng rng { 0 };
};

} // namespace osp::andor
