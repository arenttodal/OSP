#include "analysis/spectrum/SpectralEnvelope.h"

#include "core/Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace osp
{

namespace
{
    int gridSize()
    {
        return static_cast<int> (std::floor (1200.0 * std::log2 (SpectralEnvelope::maxHz / SpectralEnvelope::minHz)
                                             / SpectralEnvelope::stepCents)) + 1;
    }
}

SpectralEnvelope computeSpectralEnvelope (const std::vector<float>& mono, double sampleRate, double maxF0Hz)
{
    SpectralEnvelope envelope;
    const int points = gridSize();
    envelope.db.assign (static_cast<std::size_t> (points), 0.0);
    if (mono.empty() || sampleRate <= 0.0)
        return envelope;

    const Fft fft (12); // 4096
    const int n = fft.size();
    const int hop = n / 4;
    // Lifter below half the harmonic period of the highest F0 so harmonics are smoothed away.
    const int cutoff = std::max (4, static_cast<int> (0.5 * sampleRate / std::max (maxF0Hz, 50.0)));

    std::vector<double> window (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
        window[static_cast<std::size_t> (i)] = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / n);

    std::vector<std::complex<double>> buffer (static_cast<std::size_t> (n));
    std::vector<double> frameEnergy;
    std::vector<std::vector<double>> frameEnvelopes;

    for (std::size_t start = 0;; start += static_cast<std::size_t> (hop))
    {
        double energy = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto index = start + static_cast<std::size_t> (i);
            const double x = index < mono.size() ? mono[index] * window[static_cast<std::size_t> (i)] : 0.0;
            buffer[static_cast<std::size_t> (i)] = { x, 0.0 };
            energy += x * x;
        }

        if (energy > 1.0e-12)
        {
            fft.forward (buffer.data());
            for (auto& v : buffer)
                v = { std::log (std::abs (v) + 1.0e-9), 0.0 };
            fft.inverse (buffer.data()); // real cepstrum
            for (int q = cutoff; q <= n - cutoff; ++q)
                buffer[static_cast<std::size_t> (q)] = 0.0;
            fft.forward (buffer.data()); // smoothed natural-log magnitude

            std::vector<double> env (static_cast<std::size_t> (points));
            for (int p = 0; p < points; ++p)
            {
                const double hz = SpectralEnvelope::minHz * std::exp2 (p * SpectralEnvelope::stepCents / 1200.0);
                const double bin = std::min (hz / sampleRate * n, n / 2.0 - 1.0);
                const auto b0 = static_cast<std::size_t> (bin);
                const double frac = bin - static_cast<double> (b0);
                const double value = (1.0 - frac) * buffer[b0].real() + frac * buffer[b0 + 1].real();
                env[static_cast<std::size_t> (p)] = 20.0 / std::log (10.0) * value;
            }
            frameEnvelopes.push_back (std::move (env));
            frameEnergy.push_back (energy);
        }

        if (start + static_cast<std::size_t> (n) >= mono.size())
            break;
    }

    if (frameEnergy.empty())
        return envelope;

    // Energy-weighted average over frames within 30 dB of the loudest.
    const double maxEnergy = *std::max_element (frameEnergy.begin(), frameEnergy.end());
    double weightSum = 0.0;
    for (std::size_t f = 0; f < frameEnvelopes.size(); ++f)
    {
        if (frameEnergy[f] < maxEnergy * 1.0e-3)
            continue;
        const double w = std::sqrt (frameEnergy[f]);
        weightSum += w;
        for (int p = 0; p < points; ++p)
            envelope.db[static_cast<std::size_t> (p)] += w * frameEnvelopes[f][static_cast<std::size_t> (p)];
    }
    double mean = 0.0;
    for (auto& v : envelope.db)
    {
        v /= weightSum;
        mean += v;
    }
    mean /= points;
    for (auto& v : envelope.db)
        v -= mean;
    return envelope;
}

double envelopeShiftSemitones (const SpectralEnvelope& a, const SpectralEnvelope& b, double maxShiftSemitones)
{
    const auto n = static_cast<int> (std::min (a.db.size(), b.db.size()));
    const int maxLag = static_cast<int> (maxShiftSemitones * 100.0 / SpectralEnvelope::stepCents);
    if (n < 2 * maxLag + 10)
        return 0.0;

    auto correlation = [&] (int lag) {
        double sab = 0.0, saa = 0.0, sbb = 0.0;
        for (int i = std::max (0, -lag); i < std::min (n, n - lag); ++i)
        {
            const double x = a.db[static_cast<std::size_t> (i)];
            const double y = b.db[static_cast<std::size_t> (i + lag)];
            sab += x * y;
            saa += x * x;
            sbb += y * y;
        }
        return (saa > 0.0 && sbb > 0.0) ? sab / std::sqrt (saa * sbb) : 0.0;
    };

    int best = 0;
    double bestValue = -2.0;
    std::vector<double> values (static_cast<std::size_t> (2 * maxLag + 1));
    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        const double v = correlation (lag);
        values[static_cast<std::size_t> (lag + maxLag)] = v;
        if (v > bestValue)
        {
            bestValue = v;
            best = lag;
        }
    }

    double refined = best;
    if (best > -maxLag && best < maxLag)
    {
        const double y0 = values[static_cast<std::size_t> (best - 1 + maxLag)];
        const double y1 = values[static_cast<std::size_t> (best + maxLag)];
        const double y2 = values[static_cast<std::size_t> (best + 1 + maxLag)];
        const double denom = y0 - 2.0 * y1 + y2;
        if (std::abs (denom) > 1.0e-12)
            refined = best + std::clamp (0.5 * (y0 - y2) / denom, -0.5, 0.5);
    }
    return refined * SpectralEnvelope::stepCents / 100.0;
}

} // namespace osp
