#pragma once

#include <optional>
#include <span>
#include <vector>

namespace osp::stats
{

double mean (std::span<const double> values) noexcept;
double standardDeviation (std::span<const double> values) noexcept;

/** Weighted median; weights must be >= 0. Returns nullopt when total weight is zero. */
std::optional<double> weightedMedian (std::span<const double> values, std::span<const double> weights);

/** Percentile in [0, 100] with linear interpolation. Empty input returns nullopt. */
std::optional<double> percentile (std::vector<double> values, double pct);

/** Slope of least-squares line y = a + b x. Returns nullopt with fewer than two points. */
std::optional<double> linearSlope (std::span<const double> x, std::span<const double> y);

struct Periodicity
{
    double rateHz = 0.0;
    double strength = 0.0;  // normalised autocorrelation at the chosen lag, 0..1
    double amplitude = 0.0; // sqrt(2) * standard deviation of the detrended series (sine amplitude)
};

/**
    Finds the strongest periodic component of a (uniformly sampled) series between
    minHz and maxHz using normalised autocorrelation after removing a moving-average
    trend. Used for vibrato (pitch contour) and tremolo (dB envelope). Returns nullopt
    when the series is too short for the requested range.
*/
std::optional<Periodicity> findPeriodicity (std::span<const double> series,
                                            double frameRateHz,
                                            double minHz,
                                            double maxHz);

} // namespace osp::stats
