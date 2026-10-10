#include "library/Slicer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace osp::library
{

namespace
{
    constexpr int frame = 512;
    constexpr int hop = 128;   // about 2.7 ms at 48 kHz
    constexpr double minGapSeconds = 0.05;
    constexpr double preRollSeconds = 0.002;

    float percentile (std::vector<float> values, double p)
    {
        if (values.empty())
            return -120.0f;
        std::sort (values.begin(), values.end());
        const auto i = std::min (values.size() - 1, static_cast<std::size_t> (std::floor (p * static_cast<double> (values.size()))));
        return values[i];
    }

    /** Earliest sample before the attack's peak already at 10 % of it, minus a short pre-roll. */
    std::int64_t refineStart (const std::vector<float>& data, std::int64_t from, std::int64_t to, double sampleRate)
    {
        const auto n = static_cast<std::int64_t> (data.size());
        const auto end = std::min (to, n);
        float peak = 0.0f;
        for (auto i = from; i < end; ++i)
            peak = std::max (peak, std::abs (data[static_cast<std::size_t> (i)]));
        auto i = from;
        while (i < end && std::abs (data[static_cast<std::size_t> (i)]) < peak * 0.1f)
            ++i;
        return std::max<std::int64_t> (0, i - static_cast<std::int64_t> (std::lround (preRollSeconds * sampleRate)));
    }

    float windowDb (const std::vector<float>& data, std::int64_t from, std::int64_t length)
    {
        double sum = 0.0;
        for (auto i = from; i < from + length; ++i)
            sum += static_cast<double> (data[static_cast<std::size_t> (i)]) * data[static_cast<std::size_t> (i)];
        return static_cast<float> (10.0 * std::log10 (sum / static_cast<double> (length) + 1.0e-12));
    }
}

std::vector<float> monoOf (const AudioData& audio)
{
    const auto frames = static_cast<std::size_t> (audio.numFrames());
    std::vector<float> mono (frames, 0.0f);
    const auto channels = audio.numChannels();
    if (channels == 0)
        return mono;
    for (const auto& c : audio.channels)
        for (std::size_t i = 0; i < frames; ++i)
            mono[i] += c[i] / static_cast<float> (channels);
    return mono;
}

std::vector<float> energyEnvelope (const std::vector<float>& data)
{
    const auto length = static_cast<std::int64_t> (data.size());
    const auto n = std::max<std::int64_t> (0, (length - frame) / hop + 1);
    std::vector<float> db (static_cast<std::size_t> (n));
    for (std::int64_t f = 0; f < n; ++f)
        db[static_cast<std::size_t> (f)] = windowDb (data, f * hop, frame);
    return db;
}

float noiseFloorDb (const std::vector<float>& envelope)
{
    return percentile (envelope, 0.1);
}

std::vector<std::int64_t> detectOnsets (const std::vector<float>& data, double sampleRate, double sensitivity)
{
    const auto db = energyEnvelope (data);
    std::vector<std::int64_t> onsets;
    if (db.empty() || sampleRate <= 0.0)
        return onsets;
    const float floor = percentile (db, 0.1);
    const float peak = percentile (db, 1.0);
    const float s = static_cast<float> (std::clamp (sensitivity, 0.0, 1.0));
    const float gate = floor + std::max (6.0f, (peak - floor) * (0.45f - 0.35f * s));   // a hit stands this far above the floor
    const float rise = 14.0f - 10.0f * s;   // dB rise within about 10 ms that counts as an attack
    const int lookback = std::max (1, static_cast<int> (std::lround (0.01 * sampleRate / hop)));
    const auto minGap = static_cast<std::int64_t> (std::lround (minGapSeconds * sampleRate / hop));
    const auto n = static_cast<std::int64_t> (db.size());
    auto at = [&db] (std::int64_t f) { return db[static_cast<std::size_t> (f)]; };

    std::int64_t last = std::numeric_limits<std::int64_t>::min() / 2;
    for (std::int64_t f = lookback; f < n; ++f)
    {
        float before = std::numeric_limits<float>::infinity();
        for (int k = 1; k <= lookback; ++k)
            before = std::min (before, at (f - k));
        if (at (f) < gate || at (f) - before < rise || f - last < minGap)
            continue;
        // Forward to this attack's energy peak, then back to where it starts.
        auto top = f;
        while (top + 1 < n && at (top + 1) >= at (top))
            ++top;
        auto start = f;
        while (start > 0 && at (start - 1) < at (start) && at (start - 1) > floor + 3.0f && start > last + 1)
            --start;
        onsets.push_back (refineStart (data, start * hop, top * hop + frame, sampleRate));
        last = top;
        f = top;
    }
    return onsets;
}

std::int64_t sliceEnd (const std::vector<float>& data, std::int64_t start, std::int64_t limit, double sampleRate, float floorDb, double maxSliceSeconds)
{
    const auto max = std::min ({ limit, start + static_cast<std::int64_t> (std::lround (maxSliceSeconds * sampleRate)), static_cast<std::int64_t> (data.size()) });
    const auto win = std::max<std::int64_t> (1, static_cast<std::int64_t> (std::lround (0.01 * sampleRate)));
    float peakDb = -std::numeric_limits<float>::infinity();
    int quiet = 0;
    for (auto p = start; p + win <= max; p += win)
    {
        const float d = windowDb (data, p, win);
        peakDb = std::max (peakDb, d);
        const bool silent = d < std::max (floorDb + 4.0f, peakDb - 50.0f);
        quiet = silent ? quiet + 1 : 0;
        if (quiet >= 3)
            return std::min (max, p + win);
    }
    return max;
}

std::vector<Slice> sliceRecording (const AudioData& audio, const SliceOptions& options)
{
    std::vector<Slice> out;
    const auto mono = monoOf (audio);
    if (mono.empty() || audio.sampleRate <= 0.0)
        return out;
    const auto onsets = detectOnsets (mono, audio.sampleRate, options.sensitivity);
    const float floorDb = noiseFloorDb (energyEnvelope (mono));
    for (std::size_t i = 0; i < onsets.size(); ++i)
    {
        const auto start = onsets[i];
        const auto next = i + 1 < onsets.size() ? onsets[i + 1] : static_cast<std::int64_t> (mono.size());
        const auto end = sliceEnd (mono, start, next, audio.sampleRate, floorDb, options.maxSliceSeconds);
        if (static_cast<double> (end - start) < options.minSliceSeconds * audio.sampleRate)
            continue;
        out.push_back ({ start, end });
    }
    return out;
}

Slice suggestTrim (const AudioData& audio)
{
    const auto mono = monoOf (audio);
    const auto n = static_cast<std::int64_t> (mono.size());
    if (n == 0 || audio.sampleRate <= 0.0)
        return { 0, n };
    float peak = 0.0f;
    for (const float v : mono)
        peak = std::max (peak, std::abs (v));
    if (peak <= 1.0e-6f)
        return { 0, n };
    // Measured in 5 ms windows (single samples would catch every crackle).
    const auto win = std::max<std::int64_t> (1, static_cast<std::int64_t> (std::lround (0.005 * audio.sampleRate)));
    const auto envelope = energyEnvelope (mono);
    const float floorDb = envelope.empty() ? -120.0f : noiseFloorDb (envelope);
    const float peakDb = 20.0f * std::log10 (peak);
    const float threshold = std::max (peakDb - 60.0f, floorDb + 6.0f);
    std::int64_t first = -1, last = -1;
    for (std::int64_t p = 0; p + win <= n; p += win)
    {
        float m = 0.0f;
        for (auto i = p; i < p + win; ++i)
            m = std::max (m, std::abs (mono[static_cast<std::size_t> (i)]));
        if (20.0f * std::log10 (m + 1.0e-9f) >= threshold)
        {
            if (first < 0)
                first = p;
            last = p + win;
        }
    }
    if (first < 0)
        return { 0, n };
    const auto pre = static_cast<std::int64_t> (std::lround (0.010 * audio.sampleRate));
    const auto post = static_cast<std::int64_t> (std::lround (0.050 * audio.sampleRate));
    return { std::max<std::int64_t> (0, first - pre), std::min (n, last + post) };
}

AudioData renderSlice (const AudioData& audio, Slice region, const RenderOptions& options)
{
    AudioData out;
    out.sampleRate = audio.sampleRate;
    const auto n = audio.numFrames();
    auto start = std::clamp<std::int64_t> (region.start, 0, n);
    const auto end = std::clamp<std::int64_t> (region.end, start, n);
    if (options.snapToZeroCrossing && start > 0 && ! audio.channels.empty())
    {
        // Back to the nearest sign change of the first channel within 1 ms (never later: the
        // attack stays whole).
        const auto& c = audio.channels[0];
        const auto limit = std::max<std::int64_t> (0, start - static_cast<std::int64_t> (std::lround (0.001 * audio.sampleRate)));
        for (auto i = start; i > limit; --i)
            if ((c[static_cast<std::size_t> (i - 1)] <= 0.0f) != (c[static_cast<std::size_t> (i)] <= 0.0f))
            {
                start = i;
                break;
            }
    }
    const auto length = static_cast<std::size_t> (end - start);
    const float gain = static_cast<float> (std::pow (10.0, options.gainDb / 20.0));
    const auto fadeIn = std::min (length / 2, static_cast<std::size_t> (std::lround (options.fadeInSeconds * audio.sampleRate)));
    const auto fadeOut = std::min (length / 2, static_cast<std::size_t> (std::lround (options.fadeOutSeconds * audio.sampleRate)));
    for (const auto& c : audio.channels)
    {
        std::vector<float> slice (c.begin() + start, c.begin() + start + static_cast<std::int64_t> (length));
        for (std::size_t i = 0; i < length; ++i)
        {
            float g = gain;
            if (i < fadeIn)
                g *= static_cast<float> (i) / static_cast<float> (fadeIn);
            if (i >= length - fadeOut && fadeOut > 0)
                g *= static_cast<float> (length - 1 - i) / static_cast<float> (fadeOut);
            slice[i] *= g;
        }
        out.channels.push_back (std::move (slice));
    }
    return out;
}

double peakGainDb (const AudioData& audio, Slice region, double targetDbfs)
{
    float peak = 0.0f;
    const auto n = audio.numFrames();
    for (const auto& c : audio.channels)
        for (auto i = std::clamp<std::int64_t> (region.start, 0, n); i < std::clamp<std::int64_t> (region.end, 0, n); ++i)
            peak = std::max (peak, std::abs (c[static_cast<std::size_t> (i)]));
    if (peak <= 1.0e-9f)
        return 0.0;
    return std::min (24.0, targetDbfs - 20.0 * std::log10 (static_cast<double> (peak)));
}

} // namespace osp::library
