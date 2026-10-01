#include "analysis/spectrum/SpectralAnalyzer.h"

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
    /** Hann-windowed STFT producing an amplitude spectrum scaled so a sine of amplitude A peaks near A. */
    class Stft
    {
    public:
        explicit Stft (double sampleRate)
            : fft (Fft::orderForSize (static_cast<int> (std::ceil (0.040 * sampleRate)))),
              size (fft.size()),
              window (static_cast<std::size_t> (size)),
              buffer (static_cast<std::size_t> (size)),
              amplitude (static_cast<std::size_t> (size / 2 + 1))
        {
            double sum = 0.0;
            for (int i = 0; i < size; ++i)
            {
                window[static_cast<std::size_t> (i)] = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / size);
                sum += window[static_cast<std::size_t> (i)];
            }
            scale = 2.0 / sum;
        }

        int fftSize() const noexcept { return size; }
        int numBins() const noexcept { return size / 2 + 1; }

        const std::vector<double>& analyse (const std::vector<float>& signal, std::int64_t centre)
        {
            const auto n = static_cast<std::int64_t> (signal.size());
            const std::int64_t start = centre - size / 2;
            for (int i = 0; i < size; ++i)
            {
                const auto index = start + i;
                const double x = (index >= 0 && index < n) ? signal[static_cast<std::size_t> (index)] : 0.0;
                buffer[static_cast<std::size_t> (i)] = { x * window[static_cast<std::size_t> (i)], 0.0 };
            }
            fft.forward (buffer.data());
            for (int k = 0; k <= size / 2; ++k)
                amplitude[static_cast<std::size_t> (k)] = std::abs (buffer[static_cast<std::size_t> (k)]) * scale;
            return amplitude;
        }

    private:
        Fft fft;
        int size;
        double scale = 1.0;
        std::vector<double> window;
        std::vector<std::complex<double>> buffer;
        std::vector<double> amplitude;
    };
}

SpectralAnalysis SpectralAnalyzer::analyse (const AnalysisFrames& frames, const PitchFrames& pitch,
                                            std::vector<double>& fluxOut)
{
    SpectralAnalysis result;
    const auto numFrames = static_cast<std::size_t> (frames.numFrames);
    result.centroidHz.hopSeconds = frames.hopSeconds;
    result.flux.hopSeconds = frames.hopSeconds;
    result.flatness.hopSeconds = frames.hopSeconds;
    result.centroidHz.values.assign (numFrames, 0.0f);
    result.flux.values.assign (numFrames, 0.0f);
    result.flatness.values.assign (numFrames, 0.0f);
    fluxOut.assign (numFrames, 0.0);

    if (numFrames == 0 || frames.sampleRate <= 0.0)
        return result;

    Stft stft (frames.sampleRate);
    result.fftSize = stft.fftSize();
    const int bins = stft.numBins();
    const double binHz = frames.sampleRate / stft.fftSize();
    const double floorDb = gainToDb (frames.maxRms) - 80.0;

    std::vector<double> previousDb (static_cast<std::size_t> (bins), floorDb);
    std::vector<double> centroids;
    std::vector<double> centroidWeights;

    double weightSum = 0.0;
    double centroidSum = 0.0, rolloffSum = 0.0, flatnessSum = 0.0, fluxSum = 0.0;
    double periodicitySum = 0.0, harmonicSum = 0.0, highSum = 0.0, lowSum = 0.0;

    for (std::size_t f = 0; f < numFrames; ++f)
    {
        const auto& amp = stft.analyse (frames.mono, frames.frameCentre (static_cast<int> (f)));

        double total = 0.0, weighted = 0.0, logSum = 0.0, high = 0.0, low = 0.0, flux = 0.0;
        for (int k = 1; k < bins; ++k)
        {
            const double p = amp[static_cast<std::size_t> (k)] * amp[static_cast<std::size_t> (k)];
            const double hz = k * binHz;
            total += p;
            weighted += p * hz;
            logSum += std::log (p + 1.0e-20);
            if (hz >= 4000.0) high += p;
            if (hz < 250.0) low += p;

            const double db = std::max (floorDb, gainToDb (amp[static_cast<std::size_t> (k)]));
            flux += std::max (0.0, db - previousDb[static_cast<std::size_t> (k)]);
            previousDb[static_cast<std::size_t> (k)] = db;
        }
        flux /= (bins - 1);
        fluxOut[f] = flux;
        result.flux.values[f] = static_cast<float> (flux);

        if (total <= 1.0e-20)
            continue;

        const double centroid = weighted / total;
        const double flatness = std::exp (logSum / (bins - 1)) / (total / (bins - 1) + 1.0e-20);

        double cumulative = 0.0;
        double rolloff = 0.0;
        for (int k = 1; k < bins; ++k)
        {
            cumulative += amp[static_cast<std::size_t> (k)] * amp[static_cast<std::size_t> (k)];
            if (cumulative >= 0.85 * total)
            {
                rolloff = k * binHz;
                break;
            }
        }

        // Harmonic energy ratio for voiced frames (bands of +/-3 %, at least +/-2 bins, up to 10 kHz).
        double harmonicRatio = 0.0;
        if (pitch.voiced[f] && pitch.hz[f] > 0.0)
        {
            const double limitHz = std::min (10000.0, 0.5 * frames.sampleRate);
            const int limitBin = std::min (bins - 1, static_cast<int> (limitHz / binHz));
            double inBand = 0.0, all = 0.0;
            std::vector<bool> marked (static_cast<std::size_t> (limitBin + 1), false);
            for (double h = pitch.hz[f]; h < limitHz; h += pitch.hz[f])
            {
                const double half = std::max (0.03 * h, 2.0 * binHz);
                const int a = std::max (1, static_cast<int> (std::floor ((h - half) / binHz)));
                const int b = std::min (limitBin, static_cast<int> (std::ceil ((h + half) / binHz)));
                for (int k = a; k <= b; ++k)
                    marked[static_cast<std::size_t> (k)] = true;
            }
            for (int k = 1; k <= limitBin; ++k)
            {
                const double p = amp[static_cast<std::size_t> (k)] * amp[static_cast<std::size_t> (k)];
                all += p;
                if (marked[static_cast<std::size_t> (k)])
                    inBand += p;
            }
            harmonicRatio = all > 0.0 ? inBand / all : 0.0;
        }

        result.centroidHz.values[f] = static_cast<float> (centroid);
        result.flatness.values[f] = static_cast<float> (flatness);

        if (! frames.active[f])
            continue;

        const double w = frames.rms[f];
        weightSum += w;
        centroidSum += w * centroid;
        rolloffSum += w * rolloff;
        flatnessSum += w * flatness;
        fluxSum += w * flux;
        periodicitySum += w * pitch.periodicity[f];
        harmonicSum += w * harmonicRatio;
        highSum += w * (high / total);
        lowSum += w * (low / total);
        centroids.push_back (centroid);
        centroidWeights.push_back (w);
    }

    if (weightSum <= 0.0)
        return result;

    result.meanCentroidHz = centroidSum / weightSum;
    result.meanRolloffHz = rolloffSum / weightSum;
    result.meanFlatness = flatnessSum / weightSum;
    result.meanFlux = fluxSum / weightSum;
    result.periodicity = periodicitySum / weightSum;
    result.harmonicEnergyRatio = harmonicSum / weightSum;
    result.highFrequencyEnergyRatio = highSum / weightSum;
    result.lowFrequencyEnergyRatio = lowSum / weightSum;

    double var = 0.0;
    for (std::size_t i = 0; i < centroids.size(); ++i)
        var += centroidWeights[i] * (centroids[i] - result.meanCentroidHz) * (centroids[i] - result.meanCentroidHz);
    result.centroidStdHz = std::sqrt (var / weightSum);

    return result;
}

double SpectralAnalyzer::meanCentroid (const std::vector<float>& mono, double sampleRate, double* stdOut)
{
    if (mono.empty() || sampleRate <= 0.0)
        return 0.0;

    Stft stft (sampleRate);
    const auto hop = static_cast<std::int64_t> (std::max (1.0, 0.010 * sampleRate));
    const double binHz = sampleRate / stft.fftSize();
    std::vector<double> centroids, weights;

    for (std::int64_t centre = 0; centre < static_cast<std::int64_t> (mono.size()); centre += hop)
    {
        const auto& amp = stft.analyse (mono, centre);
        double total = 0.0, weighted = 0.0;
        for (int k = 1; k < stft.numBins(); ++k)
        {
            const double p = amp[static_cast<std::size_t> (k)] * amp[static_cast<std::size_t> (k)];
            total += p;
            weighted += p * k * binHz;
        }
        if (total > 1.0e-14)
        {
            centroids.push_back (weighted / total);
            weights.push_back (std::sqrt (total));
        }
    }

    double wSum = 0.0, cSum = 0.0;
    for (std::size_t i = 0; i < centroids.size(); ++i)
    {
        wSum += weights[i];
        cSum += weights[i] * centroids[i];
    }
    if (wSum <= 0.0)
        return 0.0;
    const double mean = cSum / wSum;
    if (stdOut != nullptr)
    {
        double var = 0.0;
        for (std::size_t i = 0; i < centroids.size(); ++i)
            var += weights[i] * (centroids[i] - mean) * (centroids[i] - mean);
        *stdOut = std::sqrt (var / wSum);
    }
    return mean;
}

} // namespace osp
