#include "audio/pitch/OctaveDecimator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace osp
{

namespace
{
    constexpr int halfTaps = 16;   ///< 33 taps

    double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 64; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < sum * 1.0e-16)
                break;
        }
        return sum;
    }

    /** The filter, normalised to unity gain at DC (built once; deterministic). */
    const std::array<float, 2 * halfTaps + 1>& taps()
    {
        static const auto table = [] {
            std::array<float, 2 * halfTaps + 1> h {};
            constexpr double cutoff = 0.25;   // of the input rate
            constexpr double beta = 10.0;     // ~ -100 dB
            const double pi = std::acos (-1.0);
            double sum = 0.0;
            std::array<double, 2 * halfTaps + 1> d {};
            for (int i = -halfTaps; i <= halfTaps; ++i)
            {
                const double x = static_cast<double> (i);
                const double sinc = i == 0 ? 2.0 * cutoff : std::sin (2.0 * pi * cutoff * x) / (pi * x);
                const double r = x / static_cast<double> (halfTaps);
                const double window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
                d[static_cast<std::size_t> (i + halfTaps)] = sinc * window;
                sum += sinc * window;
            }
            for (std::size_t i = 0; i < d.size(); ++i)
                h[i] = static_cast<float> (d[i] / sum);
            return h;
        }();
        return table;
    }
} // namespace

std::vector<float> decimateByTwo (const std::vector<float>& input)
{
    const auto& h = taps();
    const auto n = static_cast<std::ptrdiff_t> (input.size());
    std::vector<float> out (static_cast<std::size_t> ((n + 1) / 2), 0.0f);
    for (std::ptrdiff_t k = 0; k < static_cast<std::ptrdiff_t> (out.size()); ++k)
    {
        const std::ptrdiff_t centre = 2 * k;
        double acc = 0.0;
        for (int j = -halfTaps; j <= halfTaps; ++j)
        {
            const std::ptrdiff_t at = centre + j;
            if (at >= 0 && at < n)
                acc += static_cast<double> (h[static_cast<std::size_t> (j + halfTaps)]) * input[static_cast<std::size_t> (at)];
        }
        out[static_cast<std::size_t> (k)] = static_cast<float> (acc);
    }
    return out;
}

} // namespace osp
