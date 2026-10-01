#include "core/Statistics.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace osp::stats
{

double mean (std::span<const double> values) noexcept
{
    if (values.empty())
        return 0.0;
    return std::accumulate (values.begin(), values.end(), 0.0) / static_cast<double> (values.size());
}

double standardDeviation (std::span<const double> values) noexcept
{
    if (values.size() < 2)
        return 0.0;
    const double m = mean (values);
    double sum = 0.0;
    for (double v : values)
        sum += (v - m) * (v - m);
    return std::sqrt (sum / static_cast<double> (values.size()));
}

std::optional<double> weightedMedian (std::span<const double> values, std::span<const double> weights)
{
    if (values.size() != weights.size() || values.empty())
        return std::nullopt;

    std::vector<std::size_t> order (values.size());
    std::iota (order.begin(), order.end(), std::size_t { 0 });
    std::sort (order.begin(), order.end(), [&] (auto a, auto b) { return values[a] < values[b]; });

    const double total = std::accumulate (weights.begin(), weights.end(), 0.0);
    if (! (total > 0.0))
        return std::nullopt;

    double running = 0.0;
    for (auto index : order)
    {
        running += weights[index];
        if (running >= 0.5 * total)
            return values[index];
    }
    return values[order.back()];
}

std::optional<double> percentile (std::vector<double> values, double pct)
{
    if (values.empty())
        return std::nullopt;
    std::sort (values.begin(), values.end());
    const double position = std::clamp (pct, 0.0, 100.0) / 100.0 * static_cast<double> (values.size() - 1);
    const auto lower = static_cast<std::size_t> (std::floor (position));
    const auto upper = std::min (lower + 1, values.size() - 1);
    const double frac = position - static_cast<double> (lower);
    return values[lower] + frac * (values[upper] - values[lower]);
}

std::optional<double> linearSlope (std::span<const double> x, std::span<const double> y)
{
    if (x.size() != y.size() || x.size() < 2)
        return std::nullopt;
    const double mx = mean (x);
    const double my = mean (y);
    double num = 0.0;
    double den = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        num += (x[i] - mx) * (y[i] - my);
        den += (x[i] - mx) * (x[i] - mx);
    }
    if (den <= 0.0)
        return std::nullopt;
    return num / den;
}

std::optional<Periodicity> findPeriodicity (std::span<const double> series,
                                            double frameRateHz,
                                            double minHz,
                                            double maxHz)
{
    const int minLag = std::max (2, static_cast<int> (std::floor (frameRateHz / maxHz)));
    const int maxLag = static_cast<int> (std::ceil (frameRateHz / minHz));
    const int n = static_cast<int> (series.size());

    if (n < 2 * maxLag + 4)
        return std::nullopt;

    // Remove slow trend with a centred moving average spanning the longest period.
    const int half = maxLag;
    std::vector<double> detrended (static_cast<std::size_t> (n));
    std::vector<double> prefix (static_cast<std::size_t> (n + 1), 0.0);
    for (int i = 0; i < n; ++i)
        prefix[static_cast<std::size_t> (i + 1)] = prefix[static_cast<std::size_t> (i)] + series[static_cast<std::size_t> (i)];

    for (int i = 0; i < n; ++i)
    {
        const int a = std::max (0, i - half);
        const int b = std::min (n, i + half + 1);
        const double avg = (prefix[static_cast<std::size_t> (b)] - prefix[static_cast<std::size_t> (a)]) / (b - a);
        detrended[static_cast<std::size_t> (i)] = series[static_cast<std::size_t> (i)] - avg;
    }

    double energy = 0.0;
    for (double v : detrended)
        energy += v * v;

    if (energy <= 1e-12)
        return Periodicity {};

    std::vector<double> ac (static_cast<std::size_t> (maxLag + 2), 0.0);
    for (int lag = minLag - 1; lag <= maxLag + 1; ++lag)
    {
        double sum = 0.0;
        for (int i = 0; i + lag < n; ++i)
            sum += detrended[static_cast<std::size_t> (i)] * detrended[static_cast<std::size_t> (i + lag)];
        // Unbiased normalisation so long lags are not penalised.
        ac[static_cast<std::size_t> (lag)] = (sum / (n - lag)) / (energy / n);
    }

    auto isPeak = [&] (int lag) {
        const double v = ac[static_cast<std::size_t> (lag)];
        return v >= ac[static_cast<std::size_t> (lag - 1)] && v >= ac[static_cast<std::size_t> (lag + 1)];
    };

    double best = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag)
        if (isPeak (lag))
            best = std::max (best, ac[static_cast<std::size_t> (lag)]);

    // Take the shortest-lag peak that is nearly as strong as the strongest one, so a
    // periodic series reports its fundamental period rather than a multiple of it.
    int bestLag = -1;
    for (int lag = minLag; lag <= maxLag && best > 0.0; ++lag)
        if (isPeak (lag) && ac[static_cast<std::size_t> (lag)] >= 0.85 * best)
        {
            bestLag = lag;
            best = ac[static_cast<std::size_t> (lag)];
            break;
        }

    Periodicity result;
    result.amplitude = std::sqrt (2.0 * energy / n);

    if (bestLag < 0)
        return result;

    // Parabolic refinement of the lag.
    const double y0 = ac[static_cast<std::size_t> (bestLag - 1)];
    const double y1 = ac[static_cast<std::size_t> (bestLag)];
    const double y2 = ac[static_cast<std::size_t> (bestLag + 1)];
    const double denom = y0 - 2.0 * y1 + y2;
    const double shift = std::abs (denom) > 1e-12 ? std::clamp (0.5 * (y0 - y2) / denom, -0.5, 0.5) : 0.0;

    result.rateHz = frameRateHz / (bestLag + shift);
    result.strength = std::clamp (best, 0.0, 1.0);
    return result;
}

} // namespace osp::stats
