#include "analysis/stereo/StereoAnalyzer.h"

#include "core/Fft.h"
#include "core/PitchMath.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace osp
{

namespace
{
    struct BandEnergy
    {
        double mid[3] {};
        double side[3] {};
    };

    BandEnergy bandEnergies (const std::vector<float>& left, const std::vector<float>& right, std::size_t frames, double sampleRate)
    {
        BandEnergy energy;
        const Fft fft (12); // 4096
        const int size = fft.size();
        const int hop = size / 2;
        std::vector<double> window (static_cast<std::size_t> (size));
        for (int i = 0; i < size; ++i)
            window[static_cast<std::size_t> (i)] = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / size);

        std::vector<std::complex<double>> buffer (static_cast<std::size_t> (size));
        const double binHz = sampleRate / size;

        for (std::size_t start = 0; start < frames; start += static_cast<std::size_t> (hop))
        {
            // Pack mid (real) and side (imag) into one transform.
            for (int i = 0; i < size; ++i)
            {
                const std::size_t index = start + static_cast<std::size_t> (i);
                double m = 0.0, s = 0.0;
                if (index < frames)
                {
                    m = 0.5 * (left[index] + right[index]);
                    s = 0.5 * (left[index] - right[index]);
                }
                buffer[static_cast<std::size_t> (i)] = { m * window[static_cast<std::size_t> (i)], s * window[static_cast<std::size_t> (i)] };
            }
            fft.forward (buffer.data());

            for (int k = 1; k < size / 2; ++k)
            {
                const auto zk = buffer[static_cast<std::size_t> (k)];
                const auto zn = std::conj (buffer[static_cast<std::size_t> (size - k)]);
                const double pm = std::norm (0.5 * (zk + zn));
                const double ps = std::norm (std::complex<double> (0.0, -0.5) * (zk - zn));
                const double hz = k * binHz;
                const int band = hz < 300.0 ? 0 : (hz < 3000.0 ? 1 : 2);
                energy.mid[band] += pm;
                energy.side[band] += ps;
            }
        }
        return energy;
    }

    double widthOf (double mid, double side)
    {
        const double total = mid + side;
        return total > 1.0e-20 ? side / total : 0.0;
    }
}

StereoAnalysis StereoAnalyzer::analyse (const AudioData& audio, double maxAnalysisSeconds)
{
    StereoAnalysis result;
    if (audio.numChannels() < 2 || audio.isEmpty())
    {
        result.isMono = true;
        return result;
    }

    result.isMono = false;
    const auto& left = audio.channels[0];
    const auto& right = audio.channels[1];
    const std::size_t frames = std::min (left.size(), static_cast<std::size_t> (std::max (0.0, maxAnalysisSeconds * audio.sampleRate)));

    double ll = 0.0, rr = 0.0, lr = 0.0, mm = 0.0, ss = 0.0;
    bool identical = true;
    for (std::size_t i = 0; i < frames; ++i)
    {
        const double l = left[i];
        const double r = right[i];
        ll += l * l;
        rr += r * r;
        lr += l * r;
        const double m = 0.5 * (l + r);
        const double s = 0.5 * (l - r);
        mm += m * m;
        ss += s * s;
        identical = identical && left[i] == right[i];
    }

    result.isDualMono = identical;
    result.correlation = (ll > 0.0 && rr > 0.0) ? std::clamp (lr / std::sqrt (ll * rr), -1.0, 1.0) : 1.0;
    result.width = widthOf (mm, ss);
    result.sideToMidDb = std::max (-120.0, powerToDb (ss) - powerToDb (mm));
    result.balanceDb = (ll > 0.0 && rr > 0.0) ? std::clamp (powerToDb (ll) - powerToDb (rr), -120.0, 120.0) : 0.0;

    if (! identical)
    {
        const auto bands = bandEnergies (left, right, frames, audio.sampleRate);
        result.widthLow = widthOf (bands.mid[0], bands.side[0]);
        result.widthMid = widthOf (bands.mid[1], bands.side[1]);
        result.widthHigh = widthOf (bands.mid[2], bands.side[2]);
    }
    return result;
}

} // namespace osp
