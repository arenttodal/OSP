#include "audio/pitch/SincInterpolator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp
{

namespace
{
    double besselI0 (double x) noexcept
    {
        double sum = 1.0;
        double term = 1.0;
        const double halfX = 0.5 * x;
        for (int k = 1; k < 64; ++k)
        {
            term *= (halfX / k) * (halfX / k);
            sum += term;
            if (term < 1.0e-12 * sum)
                break;
        }
        return sum;
    }

    constexpr double kaiserBeta = 9.0; // ~ -90 dB sidelobes
}

SincInterpolator::SincInterpolator (int zeroCrossings, double maxStretch)
    : numZeroCrossings (std::clamp (zeroCrossings, 2, 32)),
      maxStretchFactor (std::clamp (maxStretch, 1.0, 16.0))
{
    reach = static_cast<int> (std::ceil (numZeroCrossings * maxStretchFactor)) + 2;

    sincTable.resize (static_cast<std::size_t> (numZeroCrossings * sincResolution + 2));
    for (std::size_t i = 0; i < sincTable.size(); ++i)
    {
        const double u = static_cast<double> (i) / sincResolution;
        sincTable[i] = u == 0.0 ? 1.0f : static_cast<float> (std::sin (std::numbers::pi * u) / (std::numbers::pi * u));
    }

    windowTable.resize (static_cast<std::size_t> (windowResolution + 2));
    const double norm = besselI0 (kaiserBeta);
    for (std::size_t i = 0; i < windowTable.size(); ++i)
    {
        const double r = std::min (1.0, static_cast<double> (i) / windowResolution);
        windowTable[i] = static_cast<float> (besselI0 (kaiserBeta * std::sqrt (1.0 - r * r)) / norm);
    }
    windowTable.back() = 0.0f;
}

float SincInterpolator::lookup (const std::vector<float>& table, double index) noexcept
{
    const auto i = static_cast<std::size_t> (index);
    if (i + 1 >= table.size())
        return 0.0f;
    const auto frac = static_cast<float> (index - static_cast<double> (i));
    return table[i] + frac * (table[i + 1] - table[i]);
}

int SincInterpolator::reachFor (double increment) const noexcept
{
    const double stretch = std::clamp (increment, 1.0, maxStretchFactor);
    return static_cast<int> (std::ceil (numZeroCrossings * stretch)) + 1;
}

void SincInterpolator::computeKernel (double position, double increment, Kernel& kernel) const noexcept
{
    const double stretch = std::clamp (increment, 1.0, maxStretchFactor);
    const double cutoff = rolloff / std::max (increment, 1.0);   // relative to source Nyquist
    const double halfWidth = numZeroCrossings * stretch;         // in source samples

    const auto base = static_cast<int> (std::floor (position));
    const int reachHere = static_cast<int> (std::ceil (halfWidth));
    const int first = base - reachHere + 1;
    const int last = base + reachHere;

    kernel.firstIndex = first;
    kernel.numTaps = std::min (last - first + 1, Kernel::maxTaps);

    const double sincScale = static_cast<double> (sincResolution) * cutoff;
    const double windowScale = static_cast<double> (windowResolution) / halfWidth;

    float sum = 0.0f;
    for (int t = 0; t < kernel.numTaps; ++t)
    {
        const double distance = std::abs (position - static_cast<double> (first + t));
        float w = 0.0f;
        if (distance < halfWidth)
            w = lookup (sincTable, distance * sincScale) * lookup (windowTable, distance * windowScale);
        kernel.weights[t] = w;
        sum += w;
    }

    // Normalise for unity DC gain (removes ripple from truncation / table interpolation).
    if (sum > 1.0e-6f)
    {
        const float inv = 1.0f / sum;
        for (int t = 0; t < kernel.numTaps; ++t)
            kernel.weights[t] *= inv;
    }
}

} // namespace osp
