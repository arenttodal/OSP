#include "analysis/pitch/PitchAnalyzer.h"

#include "analysis/pitch/YinPitchTracker.h"
#include "core/PitchMath.h"
#include "core/Statistics.h"

#include <algorithm>
#include <cmath>

namespace osp
{

PitchAnalysis PitchAnalyzer::analyse (const AnalysisFrames& frames, const AnalysisOptions& options, PitchFrames& out)
{
    PitchAnalysis result;
    result.trackHz.hopSeconds = frames.hopSeconds;
    result.trackConfidence.hopSeconds = frames.hopSeconds;

    const auto numFrames = static_cast<std::size_t> (frames.numFrames);
    out.hz.assign (numFrames, 0.0);
    out.periodicity.assign (numFrames, 0.0);
    out.voiced.assign (numFrames, false);
    result.trackHz.values.assign (numFrames, 0.0f);
    result.trackConfidence.values.assign (numFrames, 0.0f);

    if (numFrames == 0 || ! frames.anyActive())
        return result;

    const double maxHz = std::min (options.maxF0Hz, 0.45 * frames.sampleRate);
    YinPitchTracker tracker (frames.sampleRate, options.minF0Hz, maxHz, options.yinThreshold);
    const auto numSamples = static_cast<std::int64_t> (frames.mono.size());

    for (std::size_t f = 0; f < numFrames; ++f)
    {
        if (! frames.active[f])
            continue;
        const auto frame = tracker.analyse (frames.mono.data(), numSamples, frames.frameCentre (static_cast<int> (f)));
        out.hz[f] = frame.hz;
        out.periodicity[f] = frame.hz > 0.0 ? 1.0 - frame.aperiodicity : 0.0;
        out.voiced[f] = frame.hz > 0.0 && out.periodicity[f] >= voicedThreshold;

        result.trackHz.values[f] = out.voiced[f] ? static_cast<float> (frame.hz) : 0.0f;
        result.trackConfidence.values[f] = static_cast<float> (out.periodicity[f]);
    }

    // Weighted median over voiced frames in the MIDI (log) domain.
    std::vector<double> midi;
    std::vector<double> weights;
    double activeWeight = 0.0;
    for (std::size_t f = 0; f < numFrames; ++f)
    {
        if (! frames.active[f])
            continue;
        activeWeight += frames.rms[f];
        if (out.voiced[f])
        {
            midi.push_back (hzToMidi (out.hz[f]));
            weights.push_back (out.periodicity[f] * frames.rms[f]);
        }
    }

    const auto median = stats::weightedMedian (midi, weights);
    if (! median || activeWeight <= 0.0)
        return result;

    // Agreement and stability around the median.
    double agreeWeight = 0.0;
    double voicedWeight = 0.0;
    double periodicitySum = 0.0;
    double deviationSum = 0.0;
    std::vector<double> nearCents;
    for (std::size_t f = 0, v = 0; f < numFrames; ++f)
    {
        if (! frames.active[f] || ! out.voiced[f])
            continue;
        const double cents = 100.0 * (midi[v] - *median);
        voicedWeight += frames.rms[f];
        if (std::abs (cents) <= agreementCents)
        {
            agreeWeight += frames.rms[f];
            periodicitySum += out.periodicity[f] * frames.rms[f];
            deviationSum += cents * cents * frames.rms[f];
            nearCents.push_back (cents);
        }
        ++v;
    }

    const double agreement = agreeWeight / activeWeight;
    const double meanPeriodicity = agreeWeight > 0.0 ? periodicitySum / agreeWeight : 0.0;

    result.voicedFraction = voicedWeight / activeWeight;
    result.confidence = std::clamp (agreement * meanPeriodicity, 0.0, 1.0);
    result.stabilityCents = agreeWeight > 0.0 ? std::sqrt (deviationSum / agreeWeight) : 0.0;
    if (const auto lo = stats::percentile (nearCents, 5.0))
        result.rangeCents = *stats::percentile (nearCents, 95.0) - *lo;

    result.fundamentalHz = midiToHz (*median);
    result.midiNote = static_cast<int> (std::lround (*median));
    result.centsOffset = 100.0 * (*median - result.midiNote);
    result.noteName = midiNoteName (result.midiNote);

    if (result.confidence >= highConfidence)
        result.confidenceLevel = "high";
    else if (result.confidence >= moderateConfidence)
        result.confidenceLevel = "moderate";
    else
        result.confidenceLevel = "low";

    // A low-confidence estimate is still reported (it can seed a UI suggestion) but is
    // not marked as detected, so callers never treat it as certain.
    result.detected = result.confidence >= moderateConfidence;

    // Vibrato: periodicity of the cents contour over the longest run of agreeing voiced frames.
    std::vector<double> run;
    std::vector<double> bestRun;
    for (std::size_t f = 0; f <= numFrames; ++f)
    {
        bool ok = false;
        if (f < numFrames && out.voiced[f])
            ok = std::abs (1200.0 * std::log2 (out.hz[f] / result.fundamentalHz)) <= agreementCents;
        if (ok)
            run.push_back (1200.0 * std::log2 (out.hz[f] / result.fundamentalHz));
        else
        {
            if (run.size() > bestRun.size())
                bestRun = run;
            run.clear();
        }
    }

    if (const auto p = stats::findPeriodicity (bestRun, 1.0 / frames.hopSeconds, 3.0, 9.0))
        if (p->strength >= 0.5 && p->amplitude >= 3.0)
            result.vibrato = Modulation { p->rateHz, p->amplitude, p->strength };

    return result;
}

} // namespace osp
