#include "analysis/reimagined/ReimaginedAnalyzer.h"

#include "core/Fft.h"
#include "core/Prng.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace osp
{

namespace
{
    using TapeFrame = ReimaginedAnalysis::TapeFrame;
    using TapeSplice = ReimaginedAnalysis::TapeSplice;
    using Mosaic = ReimaginedAnalysis::Mosaic;
    using MosaicFrame = ReimaginedAnalysis::MosaicFrame;

    constexpr double longestFrameSeconds = 11.4 + 0.6;   // the LONG frame plus its run-out

    double soundEndFrame (const AudioData& audio, const AnalysisData& analysis, double start)
    {
        const auto frames = static_cast<double> (audio.numFrames());
        const double trailing = std::max (0.0, analysis.envelope.trailingSilenceSeconds) * audio.sampleRate;
        return std::clamp (frames - trailing, std::min (frames, start + 1.0), frames);
    }

    /** Where the stable body continues best after `from` (normalised correlation of a window). */
    double bestLanding (const std::vector<float>& mono, double from, double lo, double hi, int window, float& correlation)
    {
        const auto n = static_cast<std::int64_t> (mono.size());
        auto at = [&] (std::int64_t i) { return i >= 0 && i < n ? static_cast<double> (mono[static_cast<std::size_t> (i)]) : 0.0; };
        const auto a = static_cast<std::int64_t> (from);
        double best = lo, bestScore = -2.0;
        for (double cand = lo; cand <= hi; cand += 4.0)
        {
            const auto b = static_cast<std::int64_t> (cand);
            double xy = 0.0, xx = 0.0, yy = 0.0;
            for (int i = 0; i < window; i += 2)
            {
                const double x = at (a + i), y = at (b + i);
                xy += x * y;
                xx += x * x;
                yy += y * y;
            }
            const double score = xx > 1.0e-12 && yy > 1.0e-12 ? xy / std::sqrt (xx * yy) : -1.0;
            if (score > bestScore)
            {
                bestScore = score;
                best = cand;
            }
        }
        correlation = static_cast<float> (std::clamp (bestScore, 0.0, 1.0));
        return best;
    }

    TapeFrame buildTape (const AudioData& audio, const std::vector<float>& mono, const AnalysisData& analysis,
                         const ContinuationModel& cont, double start)
    {
        TapeFrame tape;
        const double sr = audio.sampleRate;
        tape.sampleRate = sr;
        const double end = soundEndFrame (audio, analysis, start);
        const double natural = end - start;
        const double maxLength = longestFrameSeconds * sr;
        if (natural < 0.02 * sr)
        {
            tape.reason = "too short for a tape";
            return tape;
        }

        Prng rng (Prng::deriveSeed (0x74617065ull, static_cast<std::uint64_t> (audio.numFrames()), static_cast<std::uint64_t> (start)));
        auto splice = [&] (double sourceFrame, double frameStart, double length, double fadeIn, float correlation) {
            TapeSplice s;
            s.sourceFrame = sourceFrame;
            s.frameStart = frameStart;
            s.length = std::max (1.0, length);
            s.fadeIn = fadeIn;
            s.correlation = correlation;
            // A real splice is never quite identical: +-0.35 dB per repeat (the first is the recording).
            s.gain = tape.splices.empty() ? 1.0f : static_cast<float> (std::pow (10.0, 0.35 * rng.bipolar() / 20.0));
            tape.splices.push_back (s);
        };

        if (! cont.hasJumps())
        {
            // Nothing stable to loop (a pluck, a hit): the tape is the recording, as long as it lasts.
            splice (start, 0.0, std::min (natural, maxLength), 0.0, 1.0f);
            tape.reason = "natural length";
        }
        else if (natural >= maxLength)
        {
            // Long recording: enough tape as it is. When the stable body only starts late
            // (a long evolving intro), the onset is spliced straight onto the body instead of
            // always cropping from the start.
            const double intro = cont.sustainStartFrame - start;
            const double body = cont.sustainEndFrame - cont.sustainStartFrame;
            if (intro > 0.35 * maxLength && body > 0.5 * maxLength)
            {
                const double attack = std::min (0.45 * sr, 0.5 * intro);
                const int fade = static_cast<int> (0.06 * sr);
                float correlation = 1.0f;
                const double landing = bestLanding (mono, start + attack, cont.sustainStartFrame, cont.sustainStartFrame + 0.25 * sr, fade, correlation);
                splice (start, 0.0, attack + fade, 0.0, 1.0f);
                splice (landing, attack, std::min (end - landing, maxLength - attack), fade, correlation);
                tape.reason = "onset spliced onto the stable body";
            }
            else
            {
                splice (start, 0.0, maxLength, 0.0, 1.0f);
                tape.reason = "natural length";
            }
        }
        else
        {
            // Short recording: lengthen it on tape by its own matched loops, a fixed walk
            // (the same tape for every note) that never repeats the last few splices.
            const double minSegment = std::max (cont.minSegmentFrames, 0.3 * sr);
            const int count = static_cast<int> (cont.jumps.size());
            std::array<int, 3> recent { -1, -1, -1 };
            int recentWrite = 0;
            double pos = start, t = 0.0, fadeIn = 0.0;
            float correlation = 1.0f;
            while (t < maxLength && tape.splices.size() < 160)
            {
                const double minFrom = pos + minSegment;
                auto weightOf = [&] (int i) {
                    const auto& j = cont.jumps[static_cast<std::size_t> (i)];
                    if (j.fromFrame < minFrom)
                        return 0.0;
                    double w = 0.05 + static_cast<double> (j.score);
                    for (int r : recent)
                        if (r == i)
                            w *= 0.1;
                    if (j.fromFrame > minFrom + 2.5 * sr)
                        w *= 0.2;
                    return w;
                };
                double total = 0.0;
                for (int i = 0; i < count; ++i)
                    total += weightOf (i);
                int chosen = -1;
                if (total > 0.0)
                {
                    double pick = rng.nextDouble() * total;
                    for (int i = 0; i < count && chosen < 0; ++i)
                    {
                        pick -= weightOf (i);
                        if (pick <= 0.0)
                            chosen = i;
                    }
                    if (chosen < 0)
                        chosen = count - 1;
                }
                else if (cont.backstop >= 0 && cont.jumps[static_cast<std::size_t> (cont.backstop)].fromFrame >= pos + 0.05 * sr)
                    chosen = cont.backstop;
                if (chosen < 0)
                    break;
                const auto& j = cont.jumps[static_cast<std::size_t> (chosen)];
                splice (pos, t, j.fromFrame + j.crossfadeFrames - pos, fadeIn, correlation);
                t += j.fromFrame - pos;
                pos = j.toFrame;
                fadeIn = j.crossfadeFrames;
                correlation = j.correlation;
                recent[static_cast<std::size_t> (recentWrite)] = chosen;
                recentWrite = (recentWrite + 1) % 3;
            }
            // The last piece plays on to the end of the tape (or of the recording).
            if (t < maxLength)
                splice (pos, t, std::min (end - pos, maxLength - t), fadeIn, correlation);
            tape.reason = "body loops";
        }

        const auto& last = tape.splices.back();
        tape.lengthFrames = std::min (maxLength, last.frameStart + last.length);
        const double settle = cont.canSustain ? cont.sustainStartFrame - start
                                              : (analysis.envelope.attackSeconds + 0.05) * sr;
        tape.bodyFrame = std::clamp (settle, 0.05 * sr, 0.5 * tape.lengthFrames);

        // Overview: where the tape is loud (the display draws the frame from it).
        float peak = 1.0e-9f;
        for (int b = 0; b < TapeFrame::overviewSize; ++b)
        {
            const double t0 = tape.lengthFrames * b / TapeFrame::overviewSize;
            const double t1 = tape.lengthFrames * (b + 1) / TapeFrame::overviewSize;
            double sum = 0.0;
            int n = 0;
            std::size_t s = 0;
            for (double t = t0; t < t1; t += 16.0)
            {
                while (s + 1 < tape.splices.size() && t >= tape.splices[s + 1].frameStart)
                    ++s;
                const auto i = static_cast<std::int64_t> (tape.splices[s].sourceFrame + (t - tape.splices[s].frameStart));
                const double x = i >= 0 && i < static_cast<std::int64_t> (mono.size()) ? mono[static_cast<std::size_t> (i)] : 0.0;
                sum += x * x;
                ++n;
            }
            tape.energy[static_cast<std::size_t> (b)] = n > 0 ? static_cast<float> (std::sqrt (sum / n)) : 0.0f;
            peak = std::max (peak, tape.energy[static_cast<std::size_t> (b)]);
        }
        for (auto& e : tape.energy)
            e /= peak;
        tape.ready = true;
        return tape;
    }

    Mosaic buildMosaic (const AudioData& audio, const std::vector<float>& mono, const AnalysisData& analysis,
                        const ContinuationModel& cont, double start)
    {
        Mosaic mosaic;
        const double sr = audio.sampleRate;
        const double f0 = analysis.pitch.fundamentalHz;
        if (! analysis.pitch.detected || ! (f0 > 25.0 && f0 < 2500.0))
        {
            mosaic.reason = "no stable pitch to rebuild";
            return mosaic;
        }
        const double end = soundEndFrame (audio, analysis, start);
        if (end - start < 0.06 * sr)
        {
            mosaic.reason = "too short to rebuild";
            return mosaic;
        }
        mosaic.fundamentalHz = f0;
        mosaic.sustains = cont.canSustain;

        // Long enough for four periods and to separate the partials (a few bins apart).
        const int order = Fft::orderForSize (static_cast<int> (std::clamp (4.5 * sr / f0, 2048.0, 16384.0)));
        const Fft fft (order);
        const int size = fft.size();
        std::vector<double> window (static_cast<std::size_t> (size));
        double windowSum = 0.0, windowPower = 0.0;
        for (int i = 0; i < size; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / size);
            window[static_cast<std::size_t> (i)] = w;
            windowSum += w;
            windowPower += w * w;
        }
        const double binHz = sr / size;

        const double duration = (end - start) / sr;
        const int count = std::clamp (static_cast<int> (std::lround (duration / 0.12)), 16, 32);
        const double half = 0.5 * size;
        // Centres from the onset (a quarter window in, so the first frame catches the attack)
        // to the end of the sound.
        const double first = start + 0.25 * size;
        const double last = std::max (first, end - half);
        std::vector<std::complex<double>> buffer (static_cast<std::size_t> (size));
        std::vector<double> power (static_cast<std::size_t> (size / 2 + 1));
        std::vector<bool> harmonicBin (static_cast<std::size_t> (size / 2 + 1));
        std::vector<double> inharmonicity;
        const auto& track = analysis.pitch.trackHz;

        for (int k = 0; k < count; ++k)
        {
            const double centre = count > 1 ? first + (last - first) * k / (count - 1) : first;
            MosaicFrame frame;
            frame.seconds = static_cast<float> ((centre - start) / sr);
            double energy = 0.0;
            for (int i = 0; i < size; ++i)
            {
                const auto index = static_cast<std::int64_t> (centre - half) + i;
                const double x = index >= 0 && index < static_cast<std::int64_t> (mono.size()) ? mono[static_cast<std::size_t> (index)] : 0.0;
                energy += x * x;
                buffer[static_cast<std::size_t> (i)] = { x * window[static_cast<std::size_t> (i)], 0.0 };
            }
            frame.level = static_cast<float> (std::sqrt (energy / size));
            fft.forward (buffer.data());
            double centroidSum = 0.0, powerSum = 0.0;
            for (int b = 0; b <= size / 2; ++b)
            {
                power[static_cast<std::size_t> (b)] = std::norm (buffer[static_cast<std::size_t> (b)]);
                centroidSum += b * binHz * power[static_cast<std::size_t> (b)];
                powerSum += power[static_cast<std::size_t> (b)];
            }
            frame.centroidHz = powerSum > 0.0 ? static_cast<float> (centroidSum / powerSum) : 0.0f;

            // This frame's own fundamental (the pitch track), within half a semitone of the median.
            double local = f0;
            if (track.hopSeconds > 0.0 && ! track.values.empty())
            {
                const auto ti = static_cast<std::size_t> (std::clamp (centre / sr / track.hopSeconds, 0.0, static_cast<double> (track.values.size() - 1)));
                const double hz = track.values[ti];
                if (hz > 0.0 && std::abs (std::log2 (hz / f0)) < 0.5 / 12.0)
                    local = hz;
            }
            frame.f0Ratio = static_cast<float> (local / f0);

            // Partials: the strongest bin near each harmonic (inharmonicity found on the way).
            std::fill (harmonicBin.begin(), harmonicBin.end(), false);
            double fitNum = 0.0, fitDen = 0.0;
            const double b0 = inharmonicity.empty() ? 0.0 : inharmonicity.back();
            for (int h = 1; h <= ReimaginedAnalysis::maxPartials; ++h)
            {
                const double expected = h * local * std::sqrt (1.0 + b0 * h * h);
                if (expected > std::min (0.45 * sr, 18000.0))
                    break;
                const int lo = std::max (1, static_cast<int> ((expected - 0.3 * local) / binHz));
                const int hi = std::min (size / 2 - 1, static_cast<int> ((expected + 0.3 * local) / binHz) + 1);
                int best = lo;
                for (int b = lo; b <= hi; ++b)
                    if (power[static_cast<std::size_t> (b)] > power[static_cast<std::size_t> (best)])
                        best = b;
                // Parabolic interpolation (dB) for the peak's frequency and height.
                const double a = 10.0 * std::log10 (power[static_cast<std::size_t> (best - 1)] + 1.0e-30);
                const double m = 10.0 * std::log10 (power[static_cast<std::size_t> (best)] + 1.0e-30);
                const double c = 10.0 * std::log10 (power[static_cast<std::size_t> (best + 1)] + 1.0e-30);
                const double denom = a - 2.0 * m + c;
                const double offset = std::abs (denom) > 1.0e-12 ? std::clamp (0.5 * (a - c) / denom, -0.5, 0.5) : 0.0;
                const double peakDb = m - 0.25 * (a - c) * offset;
                const double amplitude = 2.0 * std::sqrt (std::pow (10.0, peakDb / 10.0)) / windowSum;
                frame.partial[static_cast<std::size_t> (h - 1)] = static_cast<float> (amplitude);
                for (int b = std::max (0, best - 3); b <= std::min (size / 2, best + 3); ++b)
                    harmonicBin[static_cast<std::size_t> (b)] = true;
                // Strong upper partials tell how stretched the series is (strings, bells).
                const double measured = (best + offset) * binHz;
                if (h >= 3 && amplitude > 0.003 && frame.level > 1.0e-4)
                {
                    const double ratio = measured / (h * local);
                    fitNum += (ratio * ratio - 1.0) * h * h;
                    fitDen += static_cast<double> (h) * h * h * h;
                }
            }
            if (fitDen > 0.0)
                inharmonicity.push_back (std::clamp (fitNum / fitDen, 0.0, 2.0e-3));

            // Residual: what is left between the partials, as noise RMS per band.
            for (int band = 0; band < ReimaginedAnalysis::residualBands; ++band)
            {
                const double loHz = band == 0 ? 40.0 : ReimaginedAnalysis::residualEdgesHz[static_cast<std::size_t> (band - 1)];
                const double hiHz = std::min (ReimaginedAnalysis::residualEdgesHz[static_cast<std::size_t> (band)], 0.48 * sr);
                double sum = 0.0;
                int n = 0, all = 0;
                for (int b = static_cast<int> (loHz / binHz); b <= static_cast<int> (hiHz / binHz) && b <= size / 2; ++b)
                {
                    ++all;
                    if (harmonicBin[static_cast<std::size_t> (b)])
                        continue;
                    sum += power[static_cast<std::size_t> (b)];
                    ++n;
                }
                // Band-limited noise of variance v over B positive bins: E|X|^2 = v N sum(w^2) / 2B.
                const double variance = n > 0 ? (sum / n) * 2.0 * all / (size * windowPower) : 0.0;
                frame.residual[static_cast<std::size_t> (band)] = static_cast<float> (std::sqrt (std::max (0.0, variance)));
            }
            mosaic.frames.push_back (frame);
        }

        if (! inharmonicity.empty())
        {
            std::sort (inharmonicity.begin(), inharmonicity.end());
            const double b = inharmonicity[inharmonicity.size() / 2];
            mosaic.inharmonicity = b < 2.0e-5 ? 0.0 : b;
        }

        // The body: past the attack (the loudest frame or the stable region's start).
        int loudest = 0;
        for (int k = 0; k < count; ++k)
            if (mosaic.frames[static_cast<std::size_t> (k)].level > mosaic.frames[static_cast<std::size_t> (loudest)].level)
                loudest = k;
        const double attackEnd = (analysis.envelope.onsetSeconds + analysis.envelope.attackSeconds) - start / sr;
        int body = 0;
        while (body < count - 1 && mosaic.frames[static_cast<std::size_t> (body)].seconds < attackEnd)
            ++body;
        mosaic.bodyFrame = std::min (body, loudest);
        // A held note settles where the recording is steady and strong: in the stable region
        // if there is one, else just after the attack.
        int stable = mosaic.bodyFrame;
        if (cont.canSustain)
        {
            const double s0 = (cont.sustainStartFrame - start) / sr, s1 = (cont.sustainEndFrame - start) / sr;
            float best = -1.0f;
            for (int k = 0; k < count; ++k)
            {
                const auto& f = mosaic.frames[static_cast<std::size_t> (k)];
                if (f.seconds >= s0 && f.seconds <= s1 && f.level > best)
                {
                    best = f.level;
                    stable = k;
                }
            }
        }
        mosaic.stableFrame = stable;

        double harmonic = 0.0, noise = 0.0;
        for (int k = mosaic.bodyFrame; k < count; ++k)
        {
            const auto& f = mosaic.frames[static_cast<std::size_t> (k)];
            for (float p : f.partial)
                harmonic += 0.5 * p * p;
            for (float r : f.residual)
                noise += r * r;
        }
        mosaic.residualShare = harmonic + noise > 0.0 ? noise / (harmonic + noise) : 0.0;
        if (harmonic <= 1.0e-14)
        {
            mosaic.reason = "no harmonic energy";
            return mosaic;
        }
        mosaic.reason = "harmonic frames";
        mosaic.ready = true;
        return mosaic;
    }
}

ReimaginedAnalysis analyseReimagined (const AudioData& audio, const AnalysisData& analysis, const ContinuationModel& continuation,
                                      double startFrame)
{
    ReimaginedAnalysis result;
    if (audio.isEmpty())
    {
        result.tape.reason = result.mosaic.reason = "no audio";
        return result;
    }
    const auto mono = audio.mixToMono();
    const double start = std::clamp (startFrame, 0.0, static_cast<double> (std::max<std::int64_t> (0, audio.numFrames() - 1)));
    result.tape = buildTape (audio, mono, analysis, continuation, start);
    result.mosaic = buildMosaic (audio, mono, analysis, continuation, start);
    return result;
}

} // namespace osp
