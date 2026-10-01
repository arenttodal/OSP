#include "analysis/pitch/YinPitchTracker.h"

#include <algorithm>
#include <cmath>

namespace osp
{

YinPitchTracker::YinPitchTracker (double rate, double minHz, double maxHz, double yinThreshold)
    : sampleRate (rate),
      threshold (yinThreshold),
      minLag (std::max (2, static_cast<int> (std::floor (rate / maxHz)))),
      maxLag (std::max (minLag + 2, static_cast<int> (std::ceil (rate / minHz)))),
      window (maxLag),
      frameLength (window + maxLag + 2),
      fft (Fft::orderForSize (frameLength + window))
{
    frame.resize (static_cast<std::size_t> (frameLength));
    spectrum.resize (static_cast<std::size_t> (fft.size()));
    energyPrefix.resize (static_cast<std::size_t> (frameLength + 1));
    difference.resize (static_cast<std::size_t> (maxLag + 2));
    cmnd.resize (static_cast<std::size_t> (maxLag + 2));
}

YinFrame YinPitchTracker::analyse (const float* signal, std::int64_t numSamples, std::int64_t centreSample)
{
    const std::int64_t start = centreSample - frameLength / 2;
    for (int i = 0; i < frameLength; ++i)
    {
        const auto index = start + i;
        frame[static_cast<std::size_t> (i)] = (index >= 0 && index < numSamples) ? signal[index] : 0.0;
    }

    energyPrefix[0] = 0.0;
    for (int i = 0; i < frameLength; ++i)
        energyPrefix[static_cast<std::size_t> (i + 1)] = energyPrefix[static_cast<std::size_t> (i)]
                                                         + frame[static_cast<std::size_t> (i)] * frame[static_cast<std::size_t> (i)];

    const double windowEnergy = energyPrefix[static_cast<std::size_t> (window)];
    if (windowEnergy < 1.0e-10 * window)
        return {};

    // Pack a = frame[0, window) (real) and b = frame[0, frameLength) (imag) into one complex FFT.
    const int n = fft.size();
    for (int i = 0; i < n; ++i)
    {
        const double a = i < window ? frame[static_cast<std::size_t> (i)] : 0.0;
        const double b = i < frameLength ? frame[static_cast<std::size_t> (i)] : 0.0;
        spectrum[static_cast<std::size_t> (i)] = { a, b };
    }
    fft.forward (spectrum.data());

    // Cross-spectrum conj(A) * B, using the conjugate symmetry of the real inputs.
    for (int k = 0; k <= n / 2; ++k)
    {
        const auto zk = spectrum[static_cast<std::size_t> (k)];
        const auto zn = std::conj (spectrum[static_cast<std::size_t> ((n - k) % n)]);
        const auto A = 0.5 * (zk + zn);
        const auto B = std::complex<double> (0.0, -0.5) * (zk - zn);
        const auto C = std::conj (A) * B;
        spectrum[static_cast<std::size_t> (k)] = C;
        if (k > 0 && k < n / 2)
            spectrum[static_cast<std::size_t> (n - k)] = std::conj (C);
    }
    fft.inverse (spectrum.data());

    // d(tau) = sum a^2 + sum b(tau..)^2 - 2 r(tau)
    for (int tau = 0; tau <= maxLag; ++tau)
    {
        const double shiftedEnergy = energyPrefix[static_cast<std::size_t> (tau + window)] - energyPrefix[static_cast<std::size_t> (tau)];
        const double r = spectrum[static_cast<std::size_t> (tau)].real();
        difference[static_cast<std::size_t> (tau)] = std::max (0.0, windowEnergy + shiftedEnergy - 2.0 * r);
    }

    // Cumulative mean normalised difference.
    cmnd[0] = 1.0;
    double running = 0.0;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        running += difference[static_cast<std::size_t> (tau)];
        cmnd[static_cast<std::size_t> (tau)] = running > 0.0 ? difference[static_cast<std::size_t> (tau)] * tau / running : 1.0;
    }

    // Absolute threshold: first dip below threshold, then follow it to its local minimum.
    int best = -1;
    for (int tau = minLag; tau < maxLag; ++tau)
    {
        if (cmnd[static_cast<std::size_t> (tau)] < threshold)
        {
            while (tau + 1 < maxLag && cmnd[static_cast<std::size_t> (tau + 1)] < cmnd[static_cast<std::size_t> (tau)])
                ++tau;
            best = tau;
            break;
        }
    }

    if (best < 0)
    {
        // No confident dip: report the global minimum with its (poor) aperiodicity.
        best = minLag;
        for (int tau = minLag + 1; tau < maxLag; ++tau)
            if (cmnd[static_cast<std::size_t> (tau)] < cmnd[static_cast<std::size_t> (best)])
                best = tau;
    }

    // Parabolic interpolation of the minimum.
    double refined = best;
    if (best > 1 && best < maxLag)
    {
        const double y0 = cmnd[static_cast<std::size_t> (best - 1)];
        const double y1 = cmnd[static_cast<std::size_t> (best)];
        const double y2 = cmnd[static_cast<std::size_t> (best + 1)];
        const double denom = y0 - 2.0 * y1 + y2;
        if (std::abs (denom) > 1.0e-12)
            refined = best + std::clamp (0.5 * (y0 - y2) / denom, -0.5, 0.5);
    }

    YinFrame result;
    result.hz = sampleRate / refined;
    result.aperiodicity = std::clamp (cmnd[static_cast<std::size_t> (best)], 0.0, 1.0);
    return result;
}

} // namespace osp
