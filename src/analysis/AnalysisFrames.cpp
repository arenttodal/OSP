#include "analysis/AnalysisFrames.h"

#include <algorithm>
#include <cmath>

namespace osp
{

AnalysisFrames AnalysisFrames::build (const AudioData& audio, const AnalysisOptions& options)
{
    AnalysisFrames frames;
    frames.sampleRate = audio.sampleRate;
    frames.mono = audio.mixToMono();

    const auto maxSamples = static_cast<std::size_t> (std::max (0.0, options.maxAnalysisSeconds * audio.sampleRate));
    if (frames.mono.size() > maxSamples)
        frames.mono.resize (maxSamples);

    frames.hop = std::max (1, static_cast<int> (std::lround (options.hopSeconds * audio.sampleRate)));
    frames.hopSeconds = audio.sampleRate > 0.0 ? frames.hop / audio.sampleRate : 0.0;

    const auto n = static_cast<std::int64_t> (frames.mono.size());
    frames.numFrames = n > 0 ? static_cast<int> ((n - 1) / frames.hop + 1) : 0;

    // Prefix sums of squares for O(1) windowed RMS.
    std::vector<double> prefix (static_cast<std::size_t> (n + 1), 0.0);
    for (std::int64_t i = 0; i < n; ++i)
    {
        const double v = frames.mono[static_cast<std::size_t> (i)];
        prefix[static_cast<std::size_t> (i + 1)] = prefix[static_cast<std::size_t> (i)] + v * v;
    }

    const auto halfWindow = std::max<std::int64_t> (1, std::llround (0.5 * options.rmsWindowSeconds * audio.sampleRate));
    frames.rms.resize (static_cast<std::size_t> (frames.numFrames));

    for (int f = 0; f < frames.numFrames; ++f)
    {
        const auto centre = frames.frameCentre (f);
        const auto a = std::max<std::int64_t> (0, centre - halfWindow);
        const auto b = std::min<std::int64_t> (n, centre + halfWindow);
        const double energy = prefix[static_cast<std::size_t> (b)] - prefix[static_cast<std::size_t> (a)];
        // Normalise by the full window length so edges read quieter rather than louder.
        frames.rms[static_cast<std::size_t> (f)] = std::sqrt (std::max (0.0, energy) / static_cast<double> (2 * halfWindow));
    }

    frames.maxRms = frames.rms.empty() ? 0.0 : *std::max_element (frames.rms.begin(), frames.rms.end());
    const double threshold = std::max (frames.maxRms * std::pow (10.0, -options.activeRangeDb / 20.0), 1.0e-5);
    frames.active.resize (frames.rms.size());
    for (std::size_t f = 0; f < frames.rms.size(); ++f)
        frames.active[f] = frames.rms[f] >= threshold;

    return frames;
}

} // namespace osp
