#include "analysis/continuation/ContinuationAnalyzer.h"

#include "core/Fft.h"

#include <algorithm>
#include <array>
#include <complex>
#include <cstring>
#include <numbers>

namespace osp
{

const char* toString (ContinuationStrategy strategy) noexcept
{
    switch (strategy)
    {
        case ContinuationStrategy::off: return "off";
        case ContinuationStrategy::naiveLoop: return "naive-loop";
        case ContinuationStrategy::bestLoop: return "best-loop";
        case ContinuationStrategy::multiLoop: return "multi-loop";
        case ContinuationStrategy::multiLoopMovement: return "multi-loop-movement";
    }
    return "off";
}

bool parseContinuationStrategy (const std::string& text, ContinuationStrategy& out) noexcept
{
    for (auto s : { ContinuationStrategy::off, ContinuationStrategy::naiveLoop, ContinuationStrategy::bestLoop,
                    ContinuationStrategy::multiLoop, ContinuationStrategy::multiLoopMovement })
        if (text == toString (s))
        {
            out = s;
            return true;
        }
    return false;
}

namespace
{
    constexpr int numBands = 32;

    struct Point
    {
        double frame = 0.0;
        float bands[numBands] {};
        float rmsDb = -120.0f;
        float rmsSlope = 0.0f;   // dB per second
        float cents = 0.0f;      // F0 re 440 Hz, cents
        float centsSlope = 0.0f; // cents per second
        bool voiced = false;
    };

    struct Scales
    {
        double rmsSlope = 20.0;
        double centsSlope = 60.0;
    };

    double toDb (double power) { return 10.0 * std::log10 (power + 1.0e-12); }

    /** RMS (dB) of the mono mix in windows of `window` frames every `hop` frames, centred. */
    std::vector<double> rmsSeries (const std::vector<float>& mono, int hop, int window)
    {
        std::vector<double> out;
        const auto n = static_cast<std::int64_t> (mono.size());
        for (std::int64_t c = 0; c < n; c += hop)
        {
            const auto a = std::max<std::int64_t> (0, c - window / 2);
            const auto b = std::min<std::int64_t> (n, c + window / 2);
            double sum = 0.0;
            for (auto i = a; i < b; ++i)
                sum += static_cast<double> (mono[static_cast<std::size_t> (i)]) * mono[static_cast<std::size_t> (i)];
            out.push_back (toDb (b > a ? sum / static_cast<double> (b - a) : 0.0));
        }
        return out;
    }

    double sampleSeries (const std::vector<double>& s, double index)
    {
        if (s.empty())
            return 0.0;
        index = std::clamp (index, 0.0, static_cast<double> (s.size() - 1));
        const auto i = static_cast<std::size_t> (index);
        const auto j = std::min (i + 1, s.size() - 1);
        const double f = index - static_cast<double> (i);
        return s[i] + (s[j] - s[i]) * f;
    }

    /** F0 (Hz) from the pitch track at a time; 0 when unvoiced or not available. */
    double f0At (const PitchAnalysis& pitch, double seconds)
    {
        const auto& t = pitch.trackHz;
        if (t.values.empty() || t.hopSeconds <= 0.0)
            return 0.0;
        const auto i = static_cast<std::int64_t> (std::llround (seconds / t.hopSeconds));
        if (i < 0 || i >= static_cast<std::int64_t> (t.values.size()))
            return 0.0;
        return t.values[static_cast<std::size_t> (i)];
    }

    class FeatureExtractor
    {
    public:
        FeatureExtractor (const std::vector<float>& m, double sr, const PitchAnalysis& p)
            : mono (m), sampleRate (sr), pitch (p), fft (sr > 60000.0 ? 12 : 11)
        {
            const int n = fft.size();
            window.resize (static_cast<std::size_t> (n));
            for (int i = 0; i < n; ++i)
                window[static_cast<std::size_t> (i)] = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / n);
            buffer.resize (static_cast<std::size_t> (n));

            // Log-spaced bands 60 Hz .. min(16 kHz, Nyquist).
            const double lo = 60.0;
            const double hi = std::min (16000.0, 0.48 * sampleRate);
            for (int b = 0; b <= numBands; ++b)
            {
                const double hz = lo * std::pow (hi / lo, static_cast<double> (b) / numBands);
                edges[static_cast<std::size_t> (b)] = std::clamp (static_cast<int> (hz * n / sampleRate), 1, n / 2);
            }
            fineHop = std::max (1, static_cast<int> (0.005 * sampleRate));
            fineRms = rmsSeries (mono, fineHop, std::max (2, static_cast<int> (0.03 * sampleRate)));
        }

        Point at (double frame)
        {
            Point p;
            p.frame = frame;
            const int n = fft.size();
            const auto centre = static_cast<std::int64_t> (frame);
            const auto total = static_cast<std::int64_t> (mono.size());
            double energy = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const auto idx = centre - n / 2 + i;
                const double x = (idx >= 0 && idx < total) ? mono[static_cast<std::size_t> (idx)] : 0.0;
                buffer[static_cast<std::size_t> (i)] = { x * window[static_cast<std::size_t> (i)], 0.0 };
                energy += x * x;
            }
            fft.forward (buffer.data());
            for (int b = 0; b < numBands; ++b)
            {
                double sum = 0.0;
                const int from = edges[static_cast<std::size_t> (b)];
                const int to = std::max (from + 1, edges[static_cast<std::size_t> (b + 1)]);
                for (int k = from; k < to; ++k)
                    sum += std::norm (buffer[static_cast<std::size_t> (k)]);
                p.bands[b] = static_cast<float> (toDb (sum / (to - from)));
            }
            p.rmsDb = static_cast<float> (toDb (energy / n));

            const double idx = frame / fineHop;
            const double d = 0.02 * sampleRate / fineHop; // +/- 20 ms
            p.rmsSlope = static_cast<float> ((sampleSeries (fineRms, idx + d) - sampleSeries (fineRms, idx - d)) / 0.04);

            const double t = frame / sampleRate;
            const double f = f0At (pitch, t);
            const double fa = f0At (pitch, t - 0.04);
            const double fb = f0At (pitch, t + 0.04);
            if (f > 0.0)
            {
                p.voiced = true;
                p.cents = static_cast<float> (1200.0 * std::log2 (f / 440.0));
                if (fa > 0.0 && fb > 0.0)
                    p.centsSlope = static_cast<float> (1200.0 * std::log2 (fb / fa) / 0.08);
            }
            return p;
        }

        const std::vector<double>& rms() const { return fineRms; }
        int rmsHop() const { return fineHop; }

    private:
        const std::vector<float>& mono;
        double sampleRate;
        const PitchAnalysis& pitch;
        Fft fft;
        std::vector<double> window;
        std::vector<std::complex<double>> buffer;
        std::array<int, numBands + 1> edges {};
        int fineHop = 1;
        std::vector<double> fineRms;
    };

    double distance (const Point& a, const Point& b, const Scales& s)
    {
        double spectral = 0.0;
        for (int i = 0; i < numBands; ++i)
            spectral += std::abs (static_cast<double> (a.bands[i]) - b.bands[i]);
        spectral /= numBands;

        double d = spectral / 4.0
                 + std::abs (static_cast<double> (a.rmsDb) - b.rmsDb) / 1.5
                 + std::abs (static_cast<double> (a.rmsSlope) - b.rmsSlope) / s.rmsSlope;
        if (a.voiced && b.voiced)
            d += std::abs (static_cast<double> (a.cents) - b.cents) / 12.0
               + std::abs (static_cast<double> (a.centsSlope) - b.centsSlope) / s.centsSlope;
        else if (a.voiced != b.voiced)
            d += 1.0;
        return d;
    }

    /** Normalised correlation of two multichannel windows (stride for coarse searches). */
    double windowCorrelation (const AudioData& audio, std::int64_t a, std::int64_t b, int length, int stride)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        const int channels = std::min (audio.numChannels(), 2);
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* x = audio.channels[static_cast<std::size_t> (ch)].data();
            for (int i = 0; i < length; i += stride)
            {
                const double p = x[a + i];
                const double q = x[b + i];
                ab += p * q;
                aa += p * p;
                bb += q * q;
            }
        }
        if (aa <= 1.0e-12 || bb <= 1.0e-12)
            return 0.0;
        return ab / std::sqrt (aa * bb);
    }

    /**
        Finds the destination offset (within +/- maxLag) whose crossfade window best
        matches the outgoing window. Returns the aligned destination and its correlation.
    */
    std::pair<std::int64_t, double> alignDestination (const AudioData& audio, std::int64_t from, std::int64_t to, int length,
                                                      int maxLag)
    {
        const auto frames = audio.numFrames();
        if (from < 0 || from + length > frames)
            return { to, 0.0 };
        auto valid = [&] (std::int64_t t) { return t >= 0 && t + length <= frames; };

        std::int64_t best = to;
        double bestR = valid (to) ? windowCorrelation (audio, from, to, length, 2) : -2.0;
        const int coarse = std::max (1, maxLag / 48);
        for (int lag = -maxLag; lag <= maxLag; lag += coarse)
        {
            const auto t = to + lag;
            if (! valid (t))
                continue;
            const double r = windowCorrelation (audio, from, t, length, 2);
            if (r > bestR)
            {
                bestR = r;
                best = t;
            }
        }
        const auto centre = best;
        bestR = -2.0;
        for (int lag = -coarse; lag <= coarse; ++lag)
        {
            const auto t = centre + lag;
            if (! valid (t))
                continue;
            const double r = windowCorrelation (audio, from, t, length, 1);
            if (r > bestR)
            {
                bestR = r;
                best = t;
            }
        }
        return { best, windowCorrelation (audio, from, best, length, 1) };
    }

    double crossfadeSecondsFor (const AnalysisData& a)
    {
        // Strongly periodic material matches well over a few periods; noisy or unstable
        // material needs a longer blend to hide the seam.
        const bool periodic = a.pitch.detected && a.spectral.periodicity > 0.6;
        if (! periodic)
            return 0.15;
        const double period = 1.0 / std::max (a.pitch.fundamentalHz, 30.0);
        return std::clamp (8.0 * period, 0.04, 0.12);
    }
}

ContinuationModel analyseContinuation (const AudioData& audio, const AnalysisData& analysis, const ContinuationOptions& options)
{
    ContinuationModel model;
    const double sr = audio.sampleRate;
    const auto frames = audio.numFrames();
    if (audio.isEmpty() || frames < static_cast<std::int64_t> (sr * 0.2))
    {
        model.reason = "too short";
        return model;
    }

    const auto mono = audio.mixToMono();
    FeatureExtractor features (mono, sr, analysis.pitch);
    const auto& rms = features.rms();
    const double rmsHopSec = features.rmsHop() / sr;
    const double maxRms = rms.empty() ? -120.0 : *std::max_element (rms.begin(), rms.end());
    if (maxRms < -90.0)
    {
        model.reason = "silent";
        return model;
    }

    // Sound end: last frame within 30 dB of the maximum.
    std::size_t lastSounding = 0;
    for (std::size_t i = 0; i < rms.size(); ++i)
        if (rms[i] > maxRms - 30.0)
            lastSounding = i;
    const double soundEnd = static_cast<double> (lastSounding) * rmsHopSec;

    const auto& env = analysis.envelope;
    const double xfadeSec = crossfadeSecondsFor (analysis);
    const double bodyStart = std::max (env.onsetSeconds + env.attackSeconds + 0.08, env.onsetSeconds + 0.12);

    // Median level over the body, then the last time the level is still within 4 dB of it.
    std::vector<double> body;
    for (std::size_t i = 0; i < rms.size(); ++i)
    {
        const double t = static_cast<double> (i) * rmsHopSec;
        if (t >= bodyStart && t <= soundEnd)
            body.push_back (rms[i]);
    }
    if (body.size() < 10)
    {
        model.reason = "no body after the attack";
        return model;
    }
    auto sorted = body;
    std::nth_element (sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t> (sorted.size() / 2), sorted.end());
    const double median = sorted[sorted.size() / 2];

    // Smooth over 100 ms so tremolo dips do not end the region.
    const int smooth = std::max (1, static_cast<int> (0.05 / rmsHopSec));
    auto smoothed = [&] (std::size_t i)
    {
        double sum = 0.0;
        int count = 0;
        for (int k = -smooth; k <= smooth; ++k)
        {
            const auto j = static_cast<std::int64_t> (i) + k;
            if (j >= 0 && j < static_cast<std::int64_t> (rms.size()))
            {
                sum += rms[static_cast<std::size_t> (j)];
                ++count;
            }
        }
        return sum / std::max (count, 1);
    };
    double regionEnd = bodyStart;
    for (std::size_t i = rms.size(); i-- > 0;)
    {
        const double t = static_cast<double> (i) * rmsHopSec;
        if (t < bodyStart)
            break;
        if (smoothed (i) >= median - 4.0)
        {
            regionEnd = t;
            break;
        }
    }

    const double duration = audio.durationSeconds();
    // Leave room for a crossfade read past the last jump and for the alignment search.
    regionEnd = std::min (regionEnd, duration - xfadeSec - 0.03);
    model.hasRelease = ! env.endsWhileSounding && soundEnd > regionEnd + 0.05;

    // Level slope over the region decides between "sustains" and "decays" (plucks).
    std::vector<double> xs, ys;
    for (std::size_t i = 0; i < rms.size(); ++i)
    {
        const double t = static_cast<double> (i) * rmsHopSec;
        if (t >= bodyStart && t <= regionEnd)
        {
            xs.push_back (t);
            ys.push_back (rms[i]);
        }
    }
    double slope = 0.0;
    if (xs.size() > 2)
    {
        double mx = 0.0, my = 0.0;
        for (std::size_t i = 0; i < xs.size(); ++i)
        {
            mx += xs[i];
            my += ys[i];
        }
        mx /= static_cast<double> (xs.size());
        my /= static_cast<double> (xs.size());
        double num = 0.0, den = 0.0;
        for (std::size_t i = 0; i < xs.size(); ++i)
        {
            num += (xs[i] - mx) * (ys[i] - my);
            den += (xs[i] - mx) * (xs[i] - mx);
        }
        slope = den > 0.0 ? num / den : 0.0;
        double var = 0.0;
        for (std::size_t i = 0; i < xs.size(); ++i)
        {
            const double r = ys[i] - (my + slope * (xs[i] - mx));
            var += r * r;
        }
        model.levelFluctuationDb = std::sqrt (var / static_cast<double> (xs.size()));
    }

    const double regionLength = regionEnd - bodyStart;
    model.sustainStartFrame = bodyStart * sr;
    model.sustainEndFrame = regionEnd * sr;
    model.minSegmentFrames = options.minSegmentSeconds * sr;
    model.pitchFluctuationCents = analysis.pitch.detected ? analysis.pitch.stabilityCents : 0.0;

    if (regionLength < options.minRegionSeconds)
    {
        model.reason = "stable region too short";
        return model;
    }
    // Struck and plucked sources (sharp attack, steady decay) are one-shot even when
    // they decay slowly: holding them forever would freeze the string, not play it.
    const bool struck = env.attackSeconds < 0.03 && slope < -2.5;
    if (slope < -options.maxDecayDbPerSecond || struck)
    {
        model.reason = "decaying source";
        return model;
    }

    // Feature grid over the region.
    const double gridHop = std::max (0.01, regionLength / 1200.0);
    std::vector<Point> grid;
    for (double t = bodyStart; t <= regionEnd; t += gridHop)
        grid.push_back (features.at (t * sr));

    Scales scales;
    {
        double rs = 0.0, cs = 0.0;
        int voiced = 0;
        for (const auto& p : grid)
        {
            rs += static_cast<double> (p.rmsSlope) * p.rmsSlope;
            if (p.voiced)
            {
                cs += static_cast<double> (p.centsSlope) * p.centsSlope;
                ++voiced;
            }
        }
        scales.rmsSlope = std::max (20.0, 0.5 * std::sqrt (rs / static_cast<double> (grid.size())));
        scales.centsSlope = std::max (60.0, voiced > 0 ? 0.5 * std::sqrt (cs / voiced) : 0.0);
    }

    // Best pairs by descriptor distance (both directions).
    struct Pair
    {
        int from, to;
        double d;
    };
    const int minGap = static_cast<int> (std::ceil (options.minLoopSeconds / gridHop));
    std::vector<Pair> pairs;
    const int n = static_cast<int> (grid.size());
    pairs.reserve (static_cast<std::size_t> (n) * 8);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            if (std::abs (i - j) >= minGap)
                pairs.push_back ({ i, j, distance (grid[static_cast<std::size_t> (i)], grid[static_cast<std::size_t> (j)], scales) });
    const std::size_t keep = std::min<std::size_t> (pairs.size(), 600);
    std::partial_sort (pairs.begin(), pairs.begin() + static_cast<std::ptrdiff_t> (keep), pairs.end(),
                       [] (const Pair& a, const Pair& b) { return a.d < b.d; });
    pairs.resize (keep);

    const int xfadeFrames = static_cast<int> (xfadeSec * sr);
    const double periodSec = analysis.pitch.detected ? 1.0 / std::max (analysis.pitch.fundamentalHz, 30.0) : 0.005;
    const int maxLag = static_cast<int> (std::clamp (1.2 * periodSec, 0.003, 0.025) * sr);

    std::vector<ContinuationJump> refined;
    refined.reserve (pairs.size());
    for (const auto& p : pairs)
    {
        const auto from = static_cast<std::int64_t> (grid[static_cast<std::size_t> (p.from)].frame);
        const auto to = static_cast<std::int64_t> (grid[static_cast<std::size_t> (p.to)].frame);
        const auto [aligned, r] = alignDestination (audio, from, to, xfadeFrames, maxLag);
        ContinuationJump j;
        j.fromFrame = static_cast<double> (from);
        j.toFrame = static_cast<double> (aligned);
        j.crossfadeFrames = xfadeFrames;
        j.correlation = static_cast<float> (std::clamp (r, 0.0, 1.0));
        j.score = static_cast<float> (j.correlation * j.correlation * std::exp (-p.d));
        refined.push_back (j);
    }
    std::sort (refined.begin(), refined.end(), [] (const auto& a, const auto& b) { return a.score > b.score; });

    // Backstop: a good backward jump late in the region so the walk can always continue.
    const double lateFrom = model.sustainStartFrame + 0.6 * (model.sustainEndFrame - model.sustainStartFrame);
    int backstopCandidate = -1;
    for (int i = 0; i < static_cast<int> (refined.size()); ++i)
    {
        const auto& j = refined[static_cast<std::size_t> (i)];
        if (j.toFrame < j.fromFrame - model.minSegmentFrames - 0.03 * sr && j.fromFrame >= lateFrom)
        {
            backstopCandidate = i;
            break; // sorted by score
        }
    }
    if (backstopCandidate < 0)
        for (int i = 0; i < static_cast<int> (refined.size()); ++i)
            if (refined[static_cast<std::size_t> (i)].toFrame < refined[static_cast<std::size_t> (i)].fromFrame - model.minSegmentFrames - 0.03 * sr)
            {
                backstopCandidate = i;
                break;
            }
    if (backstopCandidate < 0)
    {
        model.reason = "no compatible loop points";
        return model;
    }
    const auto backstopJump = refined[static_cast<std::size_t> (backstopCandidate)];
    // Margin for the per-layer re-alignment, which may move destinations by up to 25 ms.
    const double latestDestination = backstopJump.fromFrame - model.minSegmentFrames - 0.03 * sr;

    // Greedy diverse selection.
    std::vector<ContinuationJump> chosen { backstopJump };
    const double near = 0.15 * sr;
    for (const auto& j : refined)
    {
        if (static_cast<int> (chosen.size()) >= options.maxJumps)
            break;
        if (j.toFrame > latestDestination || j.correlation < 0.5f)
            continue;
        bool duplicate = false;
        for (const auto& c : chosen)
            if (std::abs (c.fromFrame - j.fromFrame) < near && std::abs (c.toFrame - j.toFrame) < near)
            {
                duplicate = true;
                break;
            }
        if (! duplicate)
            chosen.push_back (j);
    }
    std::sort (chosen.begin(), chosen.end(), [] (const auto& a, const auto& b) { return a.fromFrame < b.fromFrame; });
    model.jumps = chosen;
    for (int i = 0; i < static_cast<int> (chosen.size()); ++i)
        if (chosen[static_cast<std::size_t> (i)].fromFrame == backstopJump.fromFrame
            && chosen[static_cast<std::size_t> (i)].toFrame == backstopJump.toFrame)
            model.backstop = i;

    // Experiment B: the best long backward loop.
    double bestValue = -1.0;
    for (int i = 0; i < static_cast<int> (model.jumps.size()); ++i)
    {
        const auto& j = model.jumps[static_cast<std::size_t> (i)];
        if (j.toFrame >= j.fromFrame)
            continue;
        const double lengthSec = (j.fromFrame - j.toFrame) / sr;
        const double value = j.score * std::min (1.0, lengthSec / 1.5);
        if (value > bestValue)
        {
            bestValue = value;
            model.bestLoop = i;
        }
    }

    // Experiment A: the naive whole-region loop, snapped to rising zero crossings.
    auto zeroCrossingNear = [&] (double frame)
    {
        auto f = static_cast<std::int64_t> (frame);
        for (std::int64_t k = 0; k < static_cast<std::int64_t> (0.01 * sr); ++k)
            for (auto c : { f + k, f - k })
                if (c > 0 && c + 1 < frames && mono[static_cast<std::size_t> (c)] <= 0.0f && mono[static_cast<std::size_t> (c + 1)] > 0.0f)
                    return static_cast<double> (c);
        return frame;
    };
    model.naiveLoop.fromFrame = zeroCrossingNear (model.sustainEndFrame - 0.01 * sr);
    model.naiveLoop.toFrame = zeroCrossingNear (model.sustainStartFrame);
    model.naiveLoop.crossfadeFrames = 0.01 * sr;
    model.naiveLoop.correlation = 1.0f; // linear fade, as a plain sampler loop
    model.naiveLoop.score = 0.0f;

    model.canSustain = true;
    model.reason = "stable sustain";

    // Release grafting: exits from the region into the original ending.
    if (model.hasRelease)
    {
        model.releaseFrame = regionEnd * sr;
        model.tailSeconds = std::max (0.0, soundEnd - regionEnd);
        std::vector<Point> targets;
        for (double t = std::max (bodyStart, regionEnd - 0.4); t <= regionEnd; t += 0.02)
            targets.push_back (features.at (t * sr));
        const double exitSpacing = std::max (0.08, regionLength / options.maxGraftExits);
        for (double t = bodyStart; t <= regionEnd - 0.02; t += exitSpacing)
        {
            const auto& gp = grid[std::min (grid.size() - 1, static_cast<std::size_t> ((t - bodyStart) / gridHop))];
            const Point* best = nullptr;
            double bestD = 1.0e9;
            for (const auto& target : targets)
            {
                const double d = distance (gp, target, scales);
                if (d < bestD)
                {
                    bestD = d;
                    best = &target;
                }
            }
            if (best == nullptr)
                continue;
            const auto from = static_cast<std::int64_t> (gp.frame);
            const auto [aligned, r] = alignDestination (audio, from, static_cast<std::int64_t> (best->frame), xfadeFrames, maxLag);
            ContinuationJump exit;
            exit.fromFrame = static_cast<double> (from);
            exit.toFrame = static_cast<double> (aligned);
            exit.crossfadeFrames = xfadeFrames;
            exit.correlation = static_cast<float> (std::clamp (r, 0.0, 1.0));
            exit.score = static_cast<float> (exit.correlation * exit.correlation * std::exp (-bestD));
            model.graftExits.push_back (exit);
        }
    }
    return model;
}

ContinuationModel refineContinuationForLayer (const ContinuationModel& base, const AudioData& layer, double layerF0Hz)
{
    auto model = base;
    if (layer.isEmpty())
        return model;
    const double sr = layer.sampleRate;
    const double periodSec = layerF0Hz > 0.0 ? 1.0 / std::max (layerF0Hz, 30.0) : 0.005;
    const int maxLag = static_cast<int> (std::clamp (1.2 * periodSec, 0.003, 0.025) * sr);
    auto refine = [&] (ContinuationJump& j)
    {
        const auto [aligned, r] = alignDestination (layer, static_cast<std::int64_t> (j.fromFrame), static_cast<std::int64_t> (j.toFrame),
                                                    static_cast<int> (j.crossfadeFrames), maxLag);
        j.toFrame = static_cast<double> (aligned);
        j.correlation = static_cast<float> (std::clamp (r, 0.0, 1.0));
    };
    for (auto& j : model.jumps)
        refine (j);
    for (auto& e : model.graftExits)
        refine (e);
    return model;
}

} // namespace osp
