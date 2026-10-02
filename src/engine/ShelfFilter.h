#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp
{

/**
    RBJ shelving biquad (transposed direct form II), one instance per channel.
    Coefficients are recomputed at control rate only when the gain changes, so the
    per-sample cost is five multiplies. Real-time safe.
*/
class ShelfFilter
{
public:
    enum class Type { low, high };

    void setup (Type type, double sampleRate, double frequencyHz, double gainDb) noexcept
    {
        const double a = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * std::numbers::pi * std::min (frequencyHz, 0.45 * sampleRate) / sampleRate;
        const double cosw = std::cos (w0);
        const double alpha = std::sin (w0) / 2.0 * std::sqrt (2.0); // shelf slope S = 1
        const double sq = 2.0 * std::sqrt (a) * alpha;
        double b0, b1, b2, a0, a1, a2;
        if (type == Type::low)
        {
            b0 = a * ((a + 1) - (a - 1) * cosw + sq);
            b1 = 2 * a * ((a - 1) - (a + 1) * cosw);
            b2 = a * ((a + 1) - (a - 1) * cosw - sq);
            a0 = (a + 1) + (a - 1) * cosw + sq;
            a1 = -2 * ((a - 1) + (a + 1) * cosw);
            a2 = (a + 1) + (a - 1) * cosw - sq;
        }
        else
        {
            b0 = a * ((a + 1) + (a - 1) * cosw + sq);
            b1 = -2 * a * ((a - 1) + (a + 1) * cosw);
            b2 = a * ((a + 1) + (a - 1) * cosw - sq);
            a0 = (a + 1) - (a - 1) * cosw + sq;
            a1 = 2 * ((a - 1) - (a + 1) * cosw);
            a2 = (a + 1) - (a - 1) * cosw - sq;
        }
        c0 = static_cast<float> (b0 / a0);
        c1 = static_cast<float> (b1 / a0);
        c2 = static_cast<float> (b2 / a0);
        c3 = static_cast<float> (a1 / a0);
        c4 = static_cast<float> (a2 / a0);
    }

    void reset() noexcept { s1 = s2 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = c0 * x + s1;
        s1 = c1 * x - c3 * y + s2;
        s2 = c2 * x - c4 * y;
        return y;
    }

private:
    float c0 = 1.0f, c1 = 0.0f, c2 = 0.0f, c3 = 0.0f, c4 = 0.0f;
    float s1 = 0.0f, s2 = 0.0f;
};

} // namespace osp
