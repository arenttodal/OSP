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

int SincInterpolator::maxReachFor (int zeroCrossings, double maxStretch) noexcept
{
    return static_cast<int> (std::ceil (std::clamp (zeroCrossings, 2, 32) * std::clamp (maxStretch, 1.0, 16.0))) + 2;
}

SincInterpolator::SincInterpolator (int zeroCrossings, double maxStretch)
    : numZeroCrossings (std::clamp (zeroCrossings, 2, 32)),
      maxStretchFactor (std::clamp (maxStretch, 1.0, 16.0))
{
    reach = maxReachFor (zeroCrossings, maxStretch);

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

    // Polyphase table for the unstretched kernel: one normalised row per fractional phase.
    const int taps = 2 * numZeroCrossings;
    polyphase.resize (static_cast<std::size_t> ((polyphaseResolution + 1) * taps));
    Kernel scratch;
    for (int p = 0; p < polyphaseResolution; ++p)
    {
        const double frac = static_cast<double> (p) / polyphaseResolution;
        computeKernel (static_cast<double> (numZeroCrossings) + frac, 1.0, scratch); // base = zc, first = 1
        for (int t = 0; t < taps; ++t)
            polyphase[static_cast<std::size_t> (p * taps + t)] = t < scratch.numTaps ? scratch.weights[t] : 0.0f;
    }
    // The closing row (fraction 1.0, same first tap) is phase 0 moved one tap later.
    for (int t = 0; t < taps; ++t)
        polyphase[static_cast<std::size_t> (polyphaseResolution * taps + t)] = t == 0 ? 0.0f : polyphase[static_cast<std::size_t> (t - 1)];
}

void SincInterpolator::computeKernelUnity (double position, Kernel& kernel) const noexcept
{
    const auto base = static_cast<int> (std::floor (position));
    const double scaled = (position - static_cast<double> (base)) * polyphaseResolution;
    const auto row = std::min (static_cast<int> (scaled), polyphaseResolution - 1);
    const auto a = static_cast<float> (scaled - static_cast<double> (row));
    const int taps = 2 * numZeroCrossings;
    kernel.firstIndex = base - numZeroCrossings + 1;
    kernel.numTaps = taps;
    const float* r0 = polyphase.data() + static_cast<std::size_t> (row * taps);
    const float* r1 = r0 + taps;
    for (int t = 0; t < taps; ++t)
        kernel.weights[t] = r0[t] + a * (r1[t] - r0[t]);
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
