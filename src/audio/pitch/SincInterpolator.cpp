#include "audio/pitch/SincInterpolator.h"

#include "core/Simd.h"

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

void SincInterpolator::prepareStretchTables()
{
    if (! stretchTables.empty())
        return;
    stretchTables.resize (static_cast<std::size_t> (stretchSteps + 1));
    Kernel scratch;
    for (int k = 1; k <= stretchSteps; ++k)
    {
        const double stretch = std::min (std::pow (2.0, k / 12.0), maxStretchFactor);
        auto& table = stretchTables[static_cast<std::size_t> (k)];
        table.reach = reachFor (stretch) - 1; // == ceil (zeroCrossings * stretch)
        const int taps = 2 * table.reach;
        if (taps > Kernel::maxTaps)
            continue;
        table.rows.assign (static_cast<std::size_t> ((stretchResolution + 1) * taps), 0.0f);
        for (int p = 0; p < stretchResolution; ++p)
        {
            const double frac = static_cast<double> (p) / stretchResolution;
            computeKernel (static_cast<double> (table.reach) + frac, stretch, scratch); // first tap = 1
            for (int t = 0; t < taps && t < scratch.numTaps; ++t)
                table.rows[static_cast<std::size_t> (p * taps + t)] = scratch.weights[t];
        }
        for (int t = 1; t < taps; ++t)
            table.rows[static_cast<std::size_t> (stretchResolution * taps + t)] = table.rows[static_cast<std::size_t> (t - 1)];
    }
}

void SincInterpolator::computeKernelFast (double position, double increment, Kernel& kernel) const noexcept
{
    LevelCache once;
    computeKernelFast (position, increment, kernel, once);
}

int SincInterpolator::tableLevel (double increment, LevelCache& cache) const noexcept
{
    if (stretchTables.empty() || increment > maxTableStretch)
        return 0;
    // Next semitone level up (a slightly lower cutoff, never a higher one).
    if (increment != cache.increment)
    {
        cache.increment = increment;
        cache.level = std::clamp (static_cast<int> (std::ceil (12.0 * std::log2 (increment) - 1.0e-9)), 1, stretchSteps);
    }
    return stretchTables[static_cast<std::size_t> (cache.level)].rows.empty() ? 0 : cache.level;
}

SincInterpolator::Row SincInterpolator::rowFor (double position, int resolution, int reachOfTable, const float* rows, int taps) noexcept
{
    const auto base = static_cast<int> (std::floor (position));
    const double scaled = (position - static_cast<double> (base)) * resolution;
    const auto row = std::min (static_cast<int> (scaled), resolution - 1);
    return { rows + static_cast<std::size_t> (row * taps), static_cast<float> (scaled - static_cast<double> (row)), base - reachOfTable + 1 };
}

void SincInterpolator::computeKernelFast (double position, double increment, Kernel& kernel, LevelCache& cache) const noexcept
{
    if (increment <= 1.0)
    {
        computeKernelUnity (position, kernel);
        return;
    }
    const int level = tableLevel (increment, cache);
    if (level == 0)
    {
        computeKernel (position, increment, kernel);
        return;
    }
    const auto& table = stretchTables[static_cast<std::size_t> (level)];
    const int taps = 2 * table.reach;
    const auto row = rowFor (position, stretchResolution, table.reach, table.rows.data(), taps);
    kernel.firstIndex = row.first;
    kernel.numTaps = taps;
    const float* r0 = row.r0;
    const float* r1 = r0 + taps;
    for (int t = 0; t < taps; ++t)
        kernel.weights[t] = r0[t] + row.a * (r1[t] - r0[t]);
}

namespace
{
    /** Weights blended between two table rows (r0 + a * (r1 - r0), as the kernel functions
        make them) and applied to one or two channels, four taps at a time. */
    inline void blendApply (const float* r0, const float* r1, float a, int taps, const float* left, const float* right,
                            float& outLeft, float& outRight) noexcept
    {
        using simd::Float4;
        const auto fa = Float4::broadcast (a);
        auto accL = Float4::zero(), accR = Float4::zero();
        int t = 0;
        if (right != nullptr)
        {
            for (; t + 4 <= taps; t += 4)
            {
                const auto w0 = Float4::load (r0 + t);
                const auto w = w0 + fa * (Float4::load (r1 + t) - w0);
                accL = accL + w * Float4::load (left + t);
                accR = accR + w * Float4::load (right + t);
            }
            float l = accL.sum(), r = accR.sum();
            for (; t < taps; ++t)
            {
                const float w = r0[t] + a * (r1[t] - r0[t]);
                l += w * left[t];
                r += w * right[t];
            }
            outLeft = l;
            outRight = r;
            return;
        }
        for (; t + 4 <= taps; t += 4)
        {
            const auto w0 = Float4::load (r0 + t);
            accL = accL + (w0 + fa * (Float4::load (r1 + t) - w0)) * Float4::load (left + t);
        }
        float l = accL.sum();
        for (; t < taps; ++t)
            l += (r0[t] + a * (r1[t] - r0[t])) * left[t];
        outLeft = outRight = l;
    }
}

void SincInterpolator::applyLanes (const Kernel& kernel, const float* left, const float* right, float& outLeft, float& outRight) noexcept
{
    using simd::Float4;
    const float* w = kernel.weights;
    const float* pl = left + kernel.firstIndex;
    const float* pr = right != nullptr ? right + kernel.firstIndex : nullptr;
    const int taps = kernel.numTaps;
    auto accL = Float4::zero(), accR = Float4::zero();
    int t = 0;
    if (pr != nullptr)
    {
        for (; t + 4 <= taps; t += 4)
        {
            const auto wt = Float4::load (w + t);
            accL = accL + wt * Float4::load (pl + t);
            accR = accR + wt * Float4::load (pr + t);
        }
        float l = accL.sum(), r = accR.sum();
        for (; t < taps; ++t)
        {
            l += w[t] * pl[t];
            r += w[t] * pr[t];
        }
        outLeft = l;
        outRight = r;
        return;
    }
    for (; t + 4 <= taps; t += 4)
        accL = accL + Float4::load (w + t) * Float4::load (pl + t);
    float l = accL.sum();
    for (; t < taps; ++t)
        l += w[t] * pl[t];
    outLeft = outRight = l;
}

void SincInterpolator::readFast (double position, double increment, LevelCache& cache, Kernel& scratch,
                                 const float* left, const float* right, float& outLeft, float& outRight) const noexcept
{
    if (increment <= 1.0)
    {
        const int taps = 2 * numZeroCrossings;
        const auto row = rowFor (position, polyphaseResolution, numZeroCrossings, polyphase.data(), taps);
        blendApply (row.r0, row.r0 + taps, row.a, taps, left + row.first, right != nullptr ? right + row.first : nullptr, outLeft, outRight);
        return;
    }
    const int level = tableLevel (increment, cache);
    if (level == 0)
    {
        computeKernel (position, increment, scratch);
        applyLanes (scratch, left, right, outLeft, outRight);
        return;
    }
    const auto& table = stretchTables[static_cast<std::size_t> (level)];
    const int taps = 2 * table.reach;
    const auto row = rowFor (position, stretchResolution, table.reach, table.rows.data(), taps);
    blendApply (row.r0, row.r0 + taps, row.a, taps, left + row.first, right != nullptr ? right + row.first : nullptr, outLeft, outRight);
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
