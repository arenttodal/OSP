#pragma once

#include "core/Simd.h"

#include <algorithm>
#include <cmath>

namespace osp
{

/**
    tanh as a rational function (odd degree-13 numerator over even degree-6 denominator, the
    minimax fit Eigen uses), clamped where it reaches 1 in float. Within 4e-7 of the exact
    value everywhere (std::tanh in float: 1e-7), and inline: no call into libm's tanhf, which
    goes through expm1f. Odd, exact at 0, and rising but for wobbles within its error just
    below +-1.
*/
inline float fastTanh (float x) noexcept
{
    constexpr float limit = 7.90531110763549805f;
    x = std::clamp (x, -limit, limit);
    const float x2 = x * x;
    float p = x2 * -2.76076847742355e-16f + 2.00018790482477e-13f;
    p = p * x2 + -8.60467152213735e-11f;
    p = p * x2 + 5.12229709037114e-08f;
    p = p * x2 + 1.48572235717979e-05f;
    p = p * x2 + 6.37261928875436e-04f;
    p = p * x2 + 4.89352455891786e-03f;
    float q = x2 * 1.19825839466702e-06f + 1.18534705686654e-04f;
    q = q * x2 + 2.26843463243900e-03f;
    q = q * x2 + 4.89352518554385e-03f;
    return x * p / q;
}

/**
    The 4-point cubic Hermite (Catmull-Rom) read as four tap weights for x[-1], x[0], x[1],
    x[2] at fraction t: the same polynomial as the c0..c3 form, regrouped so one set of
    weights serves both channels and the taps are one 4-lane load.
*/
inline simd::Float4 hermiteWeights (float t) noexcept
{
    using simd::Float4;
    const auto ft = Float4::broadcast (t);
    const auto a = Float4::set (-0.5f, 1.5f, -1.5f, 0.5f);
    const auto b = Float4::set (1.0f, -2.5f, 2.0f, -0.5f);
    const auto c = Float4::set (-0.5f, 0.0f, 0.5f, 0.0f);
    const auto d = Float4::set (0.0f, 1.0f, 0.0f, 0.0f);
    return ((a * ft + b) * ft + c) * ft + d;
}

/**
    sin of a steadily advancing phase, by rotation: four multiplies per step instead of a
    sin. Exact at the start; the rotation drifts by about 1e-16 per step (1e-9 of the
    amplitude after ten minutes at 48 kHz).
*/
struct Phasor
{
    double s = 0.0, c = 1.0, stepSin = 0.0, stepCos = 1.0;

    void reset (double phase, double omega) noexcept
    {
        s = std::sin (phase);
        c = std::cos (phase);
        stepSin = std::sin (omega);
        stepCos = std::cos (omega);
    }
    double sin() const noexcept { return s; }
    void advance() noexcept
    {
        const double next = s * stepCos + c * stepSin;
        c = c * stepCos - s * stepSin;
        s = next;
    }
};

} // namespace osp
