#include "analysis/onset/OnsetDetector.h"

#include <algorithm>
#include <cmath>

namespace osp
{

namespace
{
    constexpr double minimumFluxDb = 3.0;
}

std::vector<Onset> OnsetDetector::detect (const AnalysisFrames& frames, const std::vector<double>& flux)
{
    std::vector<Onset> onsets;
    const auto n = flux.size();
    if (n < 3 || frames.hopSeconds <= 0.0)
        return onsets;

    const double maxFlux = *std::max_element (flux.begin(), flux.end());
    if (maxFlux <= 1.0e-9)
        return onsets;

    const int halfWindow = std::max (1, static_cast<int> (std::lround (0.1 / frames.hopSeconds)));
    const auto minSpacing = static_cast<std::size_t> (std::max (1L, std::lround (0.05 / frames.hopSeconds)));
    std::vector<double> local;
    std::size_t lastOnset = 0;
    bool haveOnset = false;

    for (std::size_t f = 0; f + 1 < n; ++f)
    {
        // The frame just before an onset may still be inactive; allow either.
        if (! frames.active[f] && ! frames.active[f + 1])
            continue;
        const double previous = f > 0 ? flux[f - 1] : 0.0;
        if (! (flux[f] >= previous && flux[f] > flux[f + 1]))
            continue;

        local.clear();
        const std::size_t a = f >= static_cast<std::size_t> (halfWindow) ? f - static_cast<std::size_t> (halfWindow) : 0;
        const std::size_t b = std::min (n - 1, f + static_cast<std::size_t> (halfWindow));
        for (std::size_t k = a; k <= b; ++k)
            local.push_back (flux[k]);
        std::nth_element (local.begin(), local.begin() + static_cast<long> (local.size() / 2), local.end());
        const double median = local[local.size() / 2];

        // Relative threshold plus an absolute floor (mean dB rise per bin) so smooth
        // vibrato/tremolo modulation is not mistaken for new articulations.
        const double threshold = median + std::max (0.15 * maxFlux, minimumFluxDb);
        if (flux[f] < threshold)
            continue;
        if (haveOnset && f - lastOnset < minSpacing)
            continue;

        onsets.push_back ({ frames.frameTime (static_cast<int> (f)), flux[f] / maxFlux });
        lastOnset = f;
        haveOnset = true;
        if (onsets.size() >= maxOnsets)
            break;
    }
    return onsets;
}

} // namespace osp
