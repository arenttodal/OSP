#include "analysis/loudness/EnvelopeAnalyzer.h"

#include "core/PitchMath.h"
#include "core/Statistics.h"

#include <algorithm>
#include <cmath>

namespace osp
{

EnvelopeAnalysis EnvelopeAnalyzer::analyse (const AudioData& audio, const AnalysisFrames& frames)
{
    EnvelopeAnalysis result;
    result.rmsDb.hopSeconds = frames.hopSeconds;

    // Absolute sample peak over all channels.
    double peak = 0.0;
    for (const auto& ch : audio.channels)
        for (float s : ch)
            peak = std::max (peak, static_cast<double> (std::abs (s)));
    result.peakDbfs = gainToDb (peak);

    const auto numFrames = static_cast<std::size_t> (frames.numFrames);
    std::vector<double> db (numFrames);
    for (std::size_t f = 0; f < numFrames; ++f)
        db[f] = std::max (-120.0, gainToDb (frames.rms[f]));
    result.rmsDb.values.assign (db.begin(), db.end());

    result.maxRmsDbfs = gainToDb (frames.maxRms);
    const double duration = audio.durationSeconds();

    if (! frames.anyActive() || numFrames == 0)
    {
        result.leadingSilenceSeconds = duration;
        result.trailingSilenceSeconds = 0.0;
        return result;
    }

    // Silence bounds at sample resolution (-60 dB re peak), on the mono analysis signal.
    const double gate = std::max (peak * 1.0e-3, 1.0e-6);
    const auto& mono = frames.mono;
    std::int64_t first = -1;
    std::int64_t last = -1;
    for (std::size_t i = 0; i < mono.size(); ++i)
        if (std::abs (mono[i]) >= gate) { first = static_cast<std::int64_t> (i); break; }
    for (std::size_t i = mono.size(); i-- > 0;)
        if (std::abs (mono[i]) >= gate) { last = static_cast<std::int64_t> (i); break; }
    if (first < 0)
    {
        // Peak exists only in a channel that cancels in the mix (e.g. out-of-phase stereo).
        first = 0;
        last = static_cast<std::int64_t> (mono.size()) - 1;
    }
    result.leadingSilenceSeconds = first / audio.sampleRate;
    result.trailingSilenceSeconds = std::max (0.0, duration - (last + 1) / audio.sampleRate);

    const double maxDb = result.maxRmsDbfs;
    std::size_t peakFrame = 0;
    for (std::size_t f = 0; f < numFrames; ++f)
        if (frames.rms[f] >= frames.maxRms) { peakFrame = f; break; }
    result.peakSeconds = frames.frameTime (static_cast<int> (peakFrame));

    std::size_t onsetFrame = peakFrame;
    for (std::size_t f = 0; f <= peakFrame; ++f)
        if (db[f] >= maxDb - 20.0) { onsetFrame = f; break; }
    result.onsetSeconds = frames.frameTime (static_cast<int> (onsetFrame));

    std::size_t nearPeakFrame = peakFrame;
    for (std::size_t f = onsetFrame; f <= peakFrame; ++f)
        if (db[f] >= maxDb - 3.0) { nearPeakFrame = f; break; }
    result.attackSeconds = frames.frameTime (static_cast<int> (nearPeakFrame)) - result.onsetSeconds;

    std::size_t lastLoud = peakFrame;
    for (std::size_t f = numFrames; f-- > peakFrame;)
        if (db[f] >= maxDb - 20.0) { lastLoud = f; break; }
    result.estimatedDecaySeconds = frames.frameTime (static_cast<int> (lastLoud)) - result.peakSeconds;

    // "Body" region: from the end of the attack (first frame within 3 dB of max) to the
    // end of the sounding material (-40 dB re max). Using the end of the attack rather
    // than the global maximum keeps tremolo / swelling sources from collapsing the region.
    std::size_t soundEnd = nearPeakFrame;
    for (std::size_t f = numFrames; f-- > nearPeakFrame;)
        if (frames.active[f]) { soundEnd = f; break; }

    std::vector<double> regionT;
    std::vector<double> regionDb;
    for (std::size_t f = nearPeakFrame; f <= soundEnd; ++f)
    {
        regionT.push_back (frames.frameTime (static_cast<int> (f)));
        regionDb.push_back (db[f] - maxDb);
    }

    if (const auto slope = stats::linearSlope (regionT, regionDb))
        result.decaySlopeDbPerSecond = *slope;
    if (const auto med = stats::percentile (regionDb, 50.0))
        result.sustainLevelDb = *med;

    // Fluctuation and tremolo on the detrended region (only meaningful for longer regions).
    if (regionDb.size() >= 20)
    {
        const double slope = result.decaySlopeDbPerSecond;
        std::vector<double> detrended (regionDb.size());
        const double meanDb = stats::mean (regionDb);
        const double meanT = stats::mean (regionT);
        for (std::size_t i = 0; i < regionDb.size(); ++i)
            detrended[i] = regionDb[i] - (meanDb + slope * (regionT[i] - meanT));
        result.sustainFluctuationDb = stats::standardDeviation (detrended);

        if (const auto p = stats::findPeriodicity (regionDb, 1.0 / frames.hopSeconds, 2.0, 15.0))
            if (p->strength >= 0.5 && p->amplitude >= 0.5)
                result.tremolo = Modulation { p->rateHz, 2.0 * p->amplitude, p->strength };
    }

    // Still sounding at the end? (no natural release in the recording)
    const std::size_t tailFrames = std::max<std::size_t> (1, numFrames / 20);
    double tailMax = -200.0;
    for (std::size_t f = numFrames - tailFrames; f < numFrames; ++f)
        tailMax = std::max (tailMax, db[f]);
    result.endsWhileSounding = tailMax >= maxDb - 12.0;

    // Secondary peaks: local maxima after the main peak rising >= 6 dB above the preceding trough.
    {
        // Light smoothing (5 frames) so ripple does not count as peaks.
        std::vector<double> smooth (numFrames);
        for (std::size_t f = 0; f < numFrames; ++f)
        {
            const std::size_t a = f >= 2 ? f - 2 : 0;
            const std::size_t b = std::min (numFrames - 1, f + 2);
            double s = 0.0;
            for (std::size_t k = a; k <= b; ++k)
                s += db[k];
            smooth[f] = s / static_cast<double> (b - a + 1);
        }

        double trough = smooth[peakFrame];
        bool rising = false;
        for (std::size_t f = peakFrame + 1; f + 1 < numFrames; ++f)
        {
            if (! frames.active[f])
                continue;
            trough = std::min (trough, smooth[f]);
            if (smooth[f] - trough >= 6.0)
                rising = true;
            if (rising && smooth[f] >= smooth[f - 1] && smooth[f] > smooth[f + 1])
            {
                ++result.secondaryPeakCount;
                trough = smooth[f];
                rising = false;
            }
        }
    }

    return result;
}

} // namespace osp
