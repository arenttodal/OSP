#include "andor/dsp/Wavetable.h"

#include <algorithm>
#include <cmath>

namespace osp::andor
{

namespace
{
    constexpr double twoPi = 6.283185307179586;

    double harmonicAmplitude (int shape, int n)
    {
        switch (shape)
        {
            case WavetableBank::sine: return n == 1 ? 1.0 : 0.0;
            case WavetableBank::triangle: return n % 2 == 1 ? ((n / 2) % 2 == 0 ? 1.0 : -1.0) / (static_cast<double> (n) * n) : 0.0;
            case WavetableBank::saw: return (n % 2 == 1 ? 1.0 : -1.0) / n;
            case WavetableBank::square: return n % 2 == 1 ? 1.0 / n : 0.0;
            default: return 0.0;
        }
    }
}

void WavetableBank::prepare (double sampleRate)
{
    if (sampleRate == rate && ! tables.empty())
        return;
    rate = sampleRate;
    const auto stride = static_cast<std::size_t> (tableSize + 1);
    tables.assign (static_cast<std::size_t> (numShapes * numLevels) * stride, 0.0f);
    for (int level = 0; level < numLevels; ++level)
    {
        const double topHz = baseHz * std::exp2 (level + 1);
        harmonics[static_cast<std::size_t> (level)] = std::max (1, static_cast<int> (std::floor (0.45 * sampleRate / topHz)));
    }

    std::vector<double> buffer (static_cast<std::size_t> (tableSize));
    for (int shape = 0; shape < numShapes; ++shape)
    {
        // One normalisation per shape (from the richest level), so octaves keep their level.
        double norm = 1.0;
        for (int level = 0; level < numLevels; ++level)
        {
            const int count = harmonics[static_cast<std::size_t> (level)];
            std::fill (buffer.begin(), buffer.end(), 0.0);
            for (int n = 1; n <= count; ++n)
            {
                const double a = harmonicAmplitude (shape, n);
                if (a == 0.0)
                    continue;
                for (int i = 0; i < tableSize; ++i)
                    buffer[static_cast<std::size_t> (i)] += a * std::sin (twoPi * n * i / tableSize);
            }
            if (level == 0)
            {
                double peak = 0.0;
                for (double v : buffer)
                    peak = std::max (peak, std::abs (v));
                norm = peak > 0.0 ? 1.0 / peak : 1.0;
            }
            float* dst = tables.data() + (static_cast<std::size_t> (shape * numLevels + level)) * stride;
            for (int i = 0; i < tableSize; ++i)
                dst[i] = static_cast<float> (buffer[static_cast<std::size_t> (i)] * norm);
            dst[tableSize] = dst[0];
        }
    }
}

float WavetableBank::lookup (int shape, int level, double phase) const noexcept
{
    const float* t = tables.data() + static_cast<std::size_t> (shape * numLevels + level) * static_cast<std::size_t> (tableSize + 1);
    const double x = phase * tableSize;
    const auto i = static_cast<int> (x);
    const float f = static_cast<float> (x - i);
    return t[i] + f * (t[i + 1] - t[i]);
}

float WavetableBank::read (double shapePosition, double phase, double hz) const noexcept
{
    if (tables.empty())
        return 0.0f;
    phase -= std::floor (phase);
    const double octave = std::log2 (std::max (hz, 1.0) / baseHz);
    const int level = std::clamp (static_cast<int> (std::floor (octave)), 0, numLevels - 1);
    const float levelBlend = level + 1 < numLevels ? static_cast<float> (std::clamp (octave - level, 0.0, 1.0)) : 0.0f;
    const double sp = std::clamp (shapePosition, 0.0, static_cast<double> (numShapes - 1));
    const int shapeA = std::min (static_cast<int> (sp), numShapes - 2);
    const float shapeBlend = static_cast<float> (sp - shapeA);

    auto at = [&] (int shape)
    {
        const float a = lookup (shape, level, phase);
        return levelBlend > 0.0f ? a + levelBlend * (lookup (shape, level + 1, phase) - a) : a;
    };
    const float a = at (shapeA);
    return shapeBlend > 0.0f ? a + shapeBlend * (at (shapeA + 1) - a) : a;
}

} // namespace osp::andor
