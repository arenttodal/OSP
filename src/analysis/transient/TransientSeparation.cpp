#include "analysis/transient/TransientSeparation.h"

#include "core/Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace osp
{

namespace
{
    float median (std::vector<float>& values)
    {
        const auto mid = values.begin() + static_cast<std::ptrdiff_t> (values.size() / 2);
        std::nth_element (values.begin(), mid, values.end());
        return *mid;
    }
}

TransientSeparation separateOnsetTransient (const AudioData& audio, double startSeconds, const TransientSeparationOptions& options)
{
    TransientSeparation result;
    const double sr = audio.sampleRate;
    const auto total = audio.numFrames();
    if (sr <= 0.0 || total < options.fftSize || audio.numChannels() == 0)
        return result;

    const int n = options.fftSize;
    const int hop = std::max (1, options.hop);
    const auto startFrame = static_cast<std::int64_t> (std::max (0.0, startSeconds) * sr);
    const auto from = std::max<std::int64_t> (0, startFrame - static_cast<std::int64_t> (options.leadSeconds * sr));
    const auto holdEnd = startFrame + static_cast<std::int64_t> (options.holdSeconds * sr);
    const auto end = std::min (total, holdEnd + static_cast<std::int64_t> (options.fadeSeconds * sr));
    if (end - from < n)
        return result;

    // Frames cover [from - n/2, end + n/2) so every output sample gets full overlap.
    const auto firstFrameStart = from - n / 2;
    const int numFrames = static_cast<int> ((end - firstFrameStart) / hop) + 1;
    const int bins = n / 2 + 1;

    std::vector<double> window (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
        window[static_cast<std::size_t> (i)] = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / n);

    Fft fft (Fft::orderForSize (n));
    const int channels = std::min (audio.numChannels(), 2);
    result.transient = AudioData::allocate (channels, end, sr);

    std::vector<std::vector<std::complex<double>>> spectra (static_cast<std::size_t> (numFrames),
                                                            std::vector<std::complex<double>> (static_cast<std::size_t> (n)));
    std::vector<float> magnitude (static_cast<std::size_t> (numFrames * bins));
    std::vector<float> harmonic (magnitude.size()), percussive (magnitude.size());
    std::vector<double> output (static_cast<std::size_t> (end)), norm (static_cast<std::size_t> (end));
    std::vector<float> scratch;

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto& x = audio.channels[static_cast<std::size_t> (ch)];
        for (int f = 0; f < numFrames; ++f)
        {
            auto& s = spectra[static_cast<std::size_t> (f)];
            const auto frameStart = firstFrameStart + static_cast<std::int64_t> (f) * hop;
            for (int i = 0; i < n; ++i)
            {
                const auto idx = frameStart + i;
                const double v = idx >= 0 && idx < total ? static_cast<double> (x[static_cast<std::size_t> (idx)]) : 0.0;
                s[static_cast<std::size_t> (i)] = { v * window[static_cast<std::size_t> (i)], 0.0 };
            }
            fft.forward (s.data());
            for (int b = 0; b < bins; ++b)
                magnitude[static_cast<std::size_t> (f * bins + b)] = static_cast<float> (std::abs (s[static_cast<std::size_t> (b)]));
        }

        // Median along time -> harmonic estimate; along frequency -> percussive estimate.
        const int ht = options.timeMedianFrames / 2;
        const int pf = options.freqMedianBins / 2;
        for (int f = 0; f < numFrames; ++f)
            for (int b = 0; b < bins; ++b)
            {
                scratch.clear();
                for (int k = std::max (0, f - ht); k <= std::min (numFrames - 1, f + ht); ++k)
                    scratch.push_back (magnitude[static_cast<std::size_t> (k * bins + b)]);
                harmonic[static_cast<std::size_t> (f * bins + b)] = median (scratch);
                scratch.clear();
                for (int k = std::max (0, b - pf); k <= std::min (bins - 1, b + pf); ++k)
                    scratch.push_back (magnitude[static_cast<std::size_t> (f * bins + k)]);
                percussive[static_cast<std::size_t> (f * bins + b)] = median (scratch);
            }

        std::fill (output.begin(), output.end(), 0.0);
        std::fill (norm.begin(), norm.end(), 0.0);
        for (int f = 0; f < numFrames; ++f)
        {
            auto& s = spectra[static_cast<std::size_t> (f)];
            for (int b = 0; b < bins; ++b)
            {
                const double h = harmonic[static_cast<std::size_t> (f * bins + b)];
                const double p = percussive[static_cast<std::size_t> (f * bins + b)];
                const double mask = p * p / std::max (h * h + p * p, 1.0e-24); // Wiener-style soft mask
                s[static_cast<std::size_t> (b)] *= mask;
                if (b > 0 && b < n / 2)
                    s[static_cast<std::size_t> (n - b)] = std::conj (s[static_cast<std::size_t> (b)]);
            }
            fft.inverse (s.data());
            const auto frameStart = firstFrameStart + static_cast<std::int64_t> (f) * hop;
            for (int i = 0; i < n; ++i)
            {
                const auto idx = frameStart + i;
                if (idx < from || idx >= end)
                    continue;
                const double w = window[static_cast<std::size_t> (i)];
                output[static_cast<std::size_t> (idx)] += s[static_cast<std::size_t> (i)].real() * w;
                norm[static_cast<std::size_t> (idx)] += w * w;
            }
        }

        // Fade in over the lead, hold, fade out: the buffer is silent at both ends.
        auto& dst = result.transient.channels[static_cast<std::size_t> (ch)];
        const double leadFrames = static_cast<double> (std::max<std::int64_t> (1, startFrame - from));
        const double fadeFrames = static_cast<double> (std::max<std::int64_t> (1, end - holdEnd));
        for (auto i = from; i < end; ++i)
        {
            double g = 1.0;
            if (i < startFrame)
                g = 0.5 - 0.5 * std::cos (std::numbers::pi * static_cast<double> (i - from) / leadFrames);
            else if (i >= holdEnd)
                g = 0.5 + 0.5 * std::cos (std::numbers::pi * static_cast<double> (i - holdEnd) / fadeFrames);
            const auto k = static_cast<std::size_t> (i);
            dst[k] = norm[k] > 1.0e-9 ? static_cast<float> (g * output[k] / norm[k]) : 0.0f;
        }
    }

    // Share of the first 100 ms that is transient.
    double te = 0.0, xe = 0.0;
    const auto shareEnd = std::min (end, startFrame + static_cast<std::int64_t> (0.1 * sr));
    for (int ch = 0; ch < channels; ++ch)
        for (auto i = startFrame; i < shareEnd; ++i)
        {
            const double t = result.transient.channels[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)];
            const double v = audio.channels[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)];
            te += t * t;
            xe += v * v;
        }
    result.share = xe > 1.0e-20 ? std::clamp (te / xe, 0.0, 1.0) : 0.0;

    // Loudest 1 ms of the transient.
    const auto block = std::max<std::int64_t> (1, static_cast<std::int64_t> (0.001 * sr));
    double best = -1.0;
    for (auto b = from; b + block <= std::min (end, holdEnd); b += block)
    {
        double e = 0.0;
        for (int ch = 0; ch < channels; ++ch)
            for (auto i = b; i < b + block; ++i)
                e += std::pow (static_cast<double> (result.transient.channels[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)]), 2.0);
        if (e > best)
        {
            best = e;
            result.peakSeconds = static_cast<double> (b + block / 2) / sr;
        }
    }
    return result;
}

} // namespace osp
