#pragma once

#include "analysis/pitch/YinPitchTracker.h"
#include "core/AudioData.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace osp::andortest
{

/** Median YIN pitch (Hz) of the left channel over [fromSeconds, toSeconds). 0 when unvoiced. */
inline double pitchOf (const AudioData& audio, double fromSeconds, double toSeconds)
{
    YinPitchTracker yin (audio.sampleRate, 30.0, 3000.0, 0.15);
    const auto& x = audio.channels[0];
    std::vector<double> hz;
    for (double t = fromSeconds; t < toSeconds; t += 0.01)
    {
        const auto r = yin.analyse (x.data(), static_cast<std::int64_t> (x.size()), static_cast<std::int64_t> (t * audio.sampleRate));
        if (r.hz > 0.0 && r.aperiodicity < 0.3)
            hz.push_back (r.hz);
    }
    if (hz.empty())
        return 0.0;
    std::nth_element (hz.begin(), hz.begin() + static_cast<std::ptrdiff_t> (hz.size() / 2), hz.end());
    return hz[hz.size() / 2];
}

inline double rmsOf (const AudioData& audio, double fromSeconds, double toSeconds)
{
    double sum = 0.0;
    std::size_t n = 0;
    const auto a = static_cast<std::size_t> (std::max (0.0, fromSeconds * audio.sampleRate));
    const auto b = std::min (audio.channels[0].size(), static_cast<std::size_t> (toSeconds * audio.sampleRate));
    for (const auto& ch : audio.channels)
        for (auto i = a; i < b; ++i)
        {
            sum += static_cast<double> (ch[i]) * ch[i];
            ++n;
        }
    return n > 0 ? std::sqrt (sum / static_cast<double> (n)) : 0.0;
}

inline bool allFinite (const AudioData& audio)
{
    for (const auto& ch : audio.channels)
        for (float s : ch)
            if (! std::isfinite (s))
                return false;
    return true;
}

/**
    Largest sample-to-sample step in [from, to) relative to the signal's RMS there: a click
    shows up as a step far larger than the waveform's own slope.
*/
inline double worstStepRatio (const AudioData& audio, double fromSeconds, double toSeconds)
{
    const auto& x = audio.channels[0];
    const auto a = static_cast<std::size_t> (std::max (1.0, fromSeconds * audio.sampleRate));
    const auto b = std::min (x.size(), static_cast<std::size_t> (toSeconds * audio.sampleRate));
    double worst = 0.0, sum = 0.0;
    for (auto i = a; i < b; ++i)
    {
        worst = std::max (worst, static_cast<double> (std::abs (x[i] - x[i - 1])));
        sum += static_cast<double> (x[i]) * x[i];
    }
    const double r = b > a ? std::sqrt (sum / static_cast<double> (b - a)) : 0.0;
    return r > 0.0 ? worst / r : 0.0;
}

} // namespace osp::andortest
