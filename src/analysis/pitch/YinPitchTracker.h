#pragma once

#include "core/Fft.h"

#include <complex>
#include <cstdint>
#include <vector>

namespace osp
{

struct YinFrame
{
    double hz = 0.0;            ///< 0 when the frame is silent
    double aperiodicity = 1.0;  ///< YIN cumulative-mean-normalised difference at the chosen lag (0 = perfectly periodic)
};

/**
    YIN fundamental-frequency estimator (de Cheveigné & Kawahara 2002), with the
    difference function computed by FFT cross-correlation so long windows (low F0
    limits) stay cheap.

    Not real-time: owns FFT scratch buffers. One instance per thread.
*/
class YinPitchTracker
{
public:
    YinPitchTracker (double sampleRate, double minHz, double maxHz, double threshold);

    /** Analyses the frame centred on `centreSample`. Samples outside the signal are treated as zero. */
    YinFrame analyse (const float* signal, std::int64_t numSamples, std::int64_t centreSample);

    int windowSize() const noexcept { return window; }

private:
    double sampleRate;
    double threshold;
    int minLag;
    int maxLag;
    int window;
    int frameLength;
    Fft fft;
    std::vector<double> frame;
    std::vector<std::complex<double>> spectrum;
    std::vector<double> energyPrefix;
    std::vector<double> difference;
    std::vector<double> cmnd;
};

} // namespace osp
