#pragma once

#include <vector>

namespace osp
{

/**
    Long-term spectral envelope on a log-frequency grid (50 Hz .. 10 kHz, 10-cent steps),
    from energy-weighted, cepstrally smoothed STFT frames. The cepstral cut-off sits below
    the harmonic spacing of `maxF0Hz`, so harmonic structure is removed and what remains
    is the resonant "body" (formants). Values are dB, mean-removed.
*/
struct SpectralEnvelope
{
    static constexpr double minHz = 50.0;
    static constexpr double maxHz = 10000.0;
    static constexpr double stepCents = 10.0;
    std::vector<double> db;
};

SpectralEnvelope computeSpectralEnvelope (const std::vector<float>& mono, double sampleRate, double maxF0Hz);

/**
    Shift (semitones, positive = moved up) that best aligns envelope `b` to envelope `a`
    by normalised cross-correlation over +/- maxShift. Used as a guard-rail "formant
    movement" metric: resampling moves the envelope with the pitch, formant-preserving
    shifting keeps it near 0.
*/
double envelopeShiftSemitones (const SpectralEnvelope& a, const SpectralEnvelope& b, double maxShiftSemitones = 30.0);

} // namespace osp
