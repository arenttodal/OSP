#include "analysis/phrase/PhraseAnalysis.h"

#include "analysis/pitch/YinPitchTracker.h"
#include "core/PitchMath.h"

#include <algorithm>
#include <map>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <array>
#include <numbers>
#include <numeric>

namespace osp::phrase
{

const char* toString (PitchBackend b) noexcept
{
    switch (b)
    {
        case PitchBackend::yin: return "yin";
        case PitchBackend::yinViterbi: return "yin-viterbi";
    }
    return "yin-viterbi";
}

const char* toString (Detection d) noexcept
{
    switch (d)
    {
        case Detection::fewer: return "fewer";
        case Detection::more:  return "more";
        case Detection::normal: break;
    }
    return "normal";
}

std::string AnalysisSettings::digest() const
{
    char buffer[256];
    std::snprintf (buffer, sizeof (buffer), "%s|%.1f-%.1f|h%.4f|hp%.0f|s%.1f|f%.1f|n%.3f|b%.3f|p%.2f/%.3f|r%.1f/%.1f/%.1f/%.1f|a%.2f|t%d|d%s|m%.3f",
                   toString (backend), minHz, maxHz, hopSeconds, highPassHz, silenceDb, absoluteFloorDb, minNoteSeconds,
                   bridgeGapSeconds, pitchChangeSemitones, pitchHoldSeconds, rearticulationDb, attackRiseDb, unvoicedDipDb, softDipDb, a4Hz, autoTuning ? 1 : 0,
                   toString (detection), minPieceSeconds);
    return buffer;
}

AnalysisSettings AnalysisSettings::effective() const
{
    auto e = *this;
    // A semitone step is always a new note, so FEWER never loosens the pitch threshold much:
    // it mostly asks for deeper re-articulations, sharper attacks and longer-held moves.
    if (detection == Detection::fewer)
    {
        e.pitchChangeSemitones *= 1.1;
        e.pitchHoldSeconds *= 1.5;
        e.rearticulationDb *= 1.5;
        e.attackRiseDb *= 1.3;
        e.unvoicedDipDb *= 1.5;
        e.softDipDb *= 1.5;
    }
    else if (detection == Detection::more)
    {
        e.pitchChangeSemitones *= 0.85;
        e.pitchHoldSeconds *= 0.75;
        e.rearticulationDb *= 0.65;
        // MORE splits at every dip the level rule finds, as before Phase 4B (re-attacks without
        // a sharp rise: legato repeats, re-pressed organ keys), and at softer attacks.
        e.attackRiseDb *= 0.75;
        e.unvoicedDipDb = 0.5 * e.rearticulationDb;
        e.softDipDb = e.rearticulationDb;
    }
    return e;
}

namespace
{
    double median (std::vector<double> v)
    {
        if (v.empty())
            return 0.0;
        const auto mid = v.size() / 2;
        std::nth_element (v.begin(), v.begin() + static_cast<std::ptrdiff_t> (mid), v.end());
        double m = v[mid];
        if (v.size() % 2 == 0)
        {
            const double lower = *std::max_element (v.begin(), v.begin() + static_cast<std::ptrdiff_t> (mid));
            m = 0.5 * (m + lower);
        }
        return m;
    }

    /** Weighted median: the value where the cumulative weight first reaches half the total. */
    double weightedMedian (std::vector<std::pair<double, double>> valueWeight)
    {
        if (valueWeight.empty())
            return 0.0;
        std::sort (valueWeight.begin(), valueWeight.end());
        double total = 0.0;
        for (const auto& [v, w] : valueWeight)
            total += w;
        if (total <= 0.0)
            return valueWeight[valueWeight.size() / 2].first;
        double running = 0.0;
        for (const auto& [v, w] : valueWeight)
        {
            running += w;
            if (running >= 0.5 * total)
                return v;
        }
        return valueWeight.back().first;
    }

    /** RBJ high-pass biquad (Q = 0.7071), applied in place. */
    void highPass (std::vector<float>& x, double sampleRate, double cutoffHz)
    {
        if (cutoffHz <= 0.0 || cutoffHz >= 0.45 * sampleRate)
            return;
        const double w0 = 2.0 * 3.141592653589793 * cutoffHz / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * 0.7071067811865476);
        const double cosw = std::cos (w0);
        const double a0 = 1.0 + alpha;
        const double b0 = (1.0 + cosw) / 2.0 / a0, b1 = -(1.0 + cosw) / a0, b2 = b0;
        const double a1 = -2.0 * cosw / a0, a2 = (1.0 - alpha) / a0;
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
        for (auto& s : x)
        {
            const double in = s;
            const double y = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = y;
            s = static_cast<float> (y);
        }
    }

    int hopSamplesFor (const AnalysisSettings& s, double sampleRate)
    {
        return std::max (16, static_cast<int> (std::lround (s.hopSeconds * sampleRate)));
    }

    std::size_t frameCount (const Preprocessed& pre, int hop)
    {
        return pre.analysedFrames <= 0 ? 0 : static_cast<std::size_t> ((pre.analysedFrames + hop - 1) / hop);
    }

    //==========================================================================
    class YinTracker final : public PitchTracker
    {
    public:
        const char* name() const noexcept override { return "yin"; }
        int version() const noexcept override { return 1; }

        bool track (const Preprocessed& pre, const AnalysisSettings& s, PitchContour& out, const std::atomic<bool>* cancel,
                    std::atomic<float>* progress) override
        {
            const int hop = hopSamplesFor (s, pre.sampleRate);
            const auto count = frameCount (pre, hop);
            out.hopSeconds = hop / pre.sampleRate;
            out.frames.assign (count, {});
            if (count == 0)
                return true;
            YinPitchTracker yin (pre.sampleRate, s.minHz, s.maxHz, 0.15);
            for (std::size_t i = 0; i < count; ++i)
            {
                if ((i & 255) == 0)
                {
                    if (cancel != nullptr && cancel->load (std::memory_order_relaxed))
                        return false;
                    if (progress != nullptr)
                        progress->store (static_cast<float> (i) / static_cast<float> (count), std::memory_order_relaxed);
                }
                auto& f = out.frames[i];
                f.levelDb = pre.levelDb[i];
                const auto r = yin.analyse (pre.mono.data(), pre.analysedFrames, static_cast<std::int64_t> (i) * hop);
                f.confidence = r.hz > 0.0 ? std::clamp (1.0 - r.aperiodicity, 0.0, 1.0) : 0.0;
                f.voiced = r.hz > 0.0 && f.confidence >= 0.65 && f.levelDb >= pre.silenceThresholdDb;
                if (f.voiced)
                {
                    f.hz = r.hz;
                    f.midi = hzToMidi (r.hz, s.a4Hz);
                }
            }
            foldOctaves (out);
            return true;
        }

        /** YIN's typical failure is a single-frame octave slip: fold frames that sit an octave
            from the median of their voiced neighbourhood back onto it. */
        static void foldOctaves (PitchContour& c)
        {
            const auto n = c.frames.size();
            std::vector<double> window;
            std::vector<double> folded (n, 0.0);
            for (std::size_t i = 0; i < n; ++i)
            {
                folded[i] = c.frames[i].midi;
                if (! c.frames[i].voiced)
                    continue;
                window.clear();
                for (std::size_t k = (i >= 10 ? i - 10 : 0); k < std::min (n, i + 11); ++k)
                    if (c.frames[k].voiced)
                        window.push_back (c.frames[k].midi);
                const double med = median (window);
                const double d = c.frames[i].midi - med;
                if (std::abs (d - 12.0) < 1.0)
                    folded[i] -= 12.0;
                else if (std::abs (d + 12.0) < 1.0)
                    folded[i] += 12.0;
            }
            for (std::size_t i = 0; i < n; ++i)
                if (c.frames[i].voiced && folded[i] != c.frames[i].midi)
                {
                    c.frames[i].hz *= semitonesToRatio (folded[i] - c.frames[i].midi);
                    c.frames[i].midi = folded[i];
                }
        }
    };

    //==========================================================================
    /**
        pYIN-inspired (Mauch & Dixon 2014), simplified: every YIN dip that is the *first* dip
        under some threshold of a range collects that threshold's weight, so a frame yields
        several pitch candidates with probabilities instead of one hard choice. A Viterbi pass
        then picks the path that is cheapest overall: staying near the previous pitch is cheap,
        jumps cost per semitone, and switching voicing costs a fixed amount. Octave slips that
        last a few frames are therefore not worth taking, while real note changes (one jump
        each) are.
    */
    class YinViterbiTracker final : public PitchTracker
    {
    public:
        const char* name() const noexcept override { return "yin-viterbi"; }
        int version() const noexcept override { return 1; }

        static constexpr int maxCandidates = 6;
        static constexpr int numThresholds = 30;

        struct Candidate
        {
            double midi = 0.0, hz = 0.0, probability = 0.0;
        };
        struct Frame
        {
            std::array<Candidate, maxCandidates> c {};
            int count = 0;
            double voicedProbability = 0.0;
        };

        bool track (const Preprocessed& pre, const AnalysisSettings& s, PitchContour& out, const std::atomic<bool>* cancel,
                    std::atomic<float>* progress) override
        {
            const int hop = hopSamplesFor (s, pre.sampleRate);
            const auto count = frameCount (pre, hop);
            out.hopSeconds = hop / pre.sampleRate;
            out.frames.assign (count, {});
            if (count == 0)
                return true;

            YinPitchTracker yin (pre.sampleRate, s.minHz, s.maxHz, 0.15);
            const int minLag = yin.minLagValue();
            const int maxLag = yin.maxLagValue();
            std::vector<Frame> frames (count);
            std::vector<int> dips;
            for (std::size_t i = 0; i < count; ++i)
            {
                if ((i & 255) == 0)
                {
                    if (cancel != nullptr && cancel->load (std::memory_order_relaxed))
                        return false;
                    if (progress != nullptr)
                        progress->store (0.9f * static_cast<float> (i) / static_cast<float> (count), std::memory_order_relaxed);
                }
                out.frames[i].levelDb = pre.levelDb[i];
                // Far below the silence threshold there is nothing to track.
                if (pre.levelDb[i] < pre.silenceThresholdDb - 6.0f)
                    continue;
                yin.analyse (pre.mono.data(), pre.analysedFrames, static_cast<std::int64_t> (i) * hop);
                if (yin.lastFrameSilent())
                    continue;
                const auto& cmnd = yin.lastCmnd();

                dips.clear();
                int globalMin = minLag;
                for (int tau = minLag; tau < maxLag; ++tau)
                {
                    if (cmnd[static_cast<std::size_t> (tau)] < cmnd[static_cast<std::size_t> (globalMin)])
                        globalMin = tau;
                    const bool leftOk = tau == minLag || cmnd[static_cast<std::size_t> (tau)] < cmnd[static_cast<std::size_t> (tau - 1)];
                    if (leftOk && cmnd[static_cast<std::size_t> (tau)] <= cmnd[static_cast<std::size_t> (tau + 1)] && cmnd[static_cast<std::size_t> (tau)] < 0.9)
                        dips.push_back (tau);
                }

                // Probability mass per dip from the threshold range 0.02..0.6 (uniform prior).
                std::array<double, 64> mass {};
                double orphan = 0.0;
                for (int k = 0; k < numThresholds; ++k)
                {
                    const double threshold = 0.02 + 0.58 * k / (numThresholds - 1);
                    bool found = false;
                    for (std::size_t d = 0; d < dips.size() && d < mass.size(); ++d)
                        if (cmnd[static_cast<std::size_t> (dips[d])] < threshold)
                        {
                            mass[d] += 1.0 / numThresholds;
                            found = true;
                            break;
                        }
                    if (! found)
                        orphan += 1.0 / numThresholds;
                }

                // Level near the silence threshold lowers the voicing belief smoothly.
                const double levelWeight = std::clamp ((pre.levelDb[i] - pre.silenceThresholdDb + 6.0) / 12.0, 0.0, 1.0);
                auto& fr = frames[i];
                auto addCandidate = [&] (int tau, double p)
                {
                    if (p <= 0.0)
                        return;
                    double refined = tau;
                    if (tau > 1 && tau < maxLag)
                    {
                        const double y0 = cmnd[static_cast<std::size_t> (tau - 1)], y1 = cmnd[static_cast<std::size_t> (tau)],
                                     y2 = cmnd[static_cast<std::size_t> (tau + 1)];
                        const double den = y0 - 2.0 * y1 + y2;
                        if (std::abs (den) > 1.0e-12)
                            refined = tau + std::clamp (0.5 * (y0 - y2) / den, -0.5, 0.5);
                    }
                    Candidate cand;
                    cand.hz = pre.sampleRate / refined;
                    cand.midi = hzToMidi (cand.hz, s.a4Hz);
                    cand.probability = p * levelWeight;
                    if (fr.count < maxCandidates)
                        fr.c[static_cast<std::size_t> (fr.count++)] = cand;
                    else
                    {
                        // Replace the weakest.
                        auto weakest = std::min_element (fr.c.begin(), fr.c.end(), [] (const Candidate& a, const Candidate& b) { return a.probability < b.probability; });
                        if (weakest->probability < cand.probability)
                            *weakest = cand;
                    }
                };
                for (std::size_t d = 0; d < dips.size() && d < mass.size(); ++d)
                    addCandidate (dips[d], mass[d]);
                // pYIN: thresholds without any dip give a small chance to the global minimum.
                addCandidate (globalMin, 0.01 * orphan);
                for (int k = 0; k < fr.count; ++k)
                    fr.voicedProbability += fr.c[static_cast<std::size_t> (k)].probability;
                fr.voicedProbability = std::min (fr.voicedProbability, 0.999);
            }

            decode (frames, out, pre);
            if (progress != nullptr)
                progress->store (1.0f, std::memory_order_relaxed);
            return true;
        }

        static void decode (const std::vector<Frame>& frames, PitchContour& out, const Preprocessed& pre)
        {
            // States per frame: candidates 0..count-1, then the unvoiced state (index count).
            const auto n = frames.size();
            constexpr double switchCost = 2.0;
            constexpr double eps = 1.0e-6;
            auto emission = [&] (const Frame& f, int state)
            {
                return state < f.count ? -std::log (std::max (f.c[static_cast<std::size_t> (state)].probability, eps))
                                       : -std::log (std::max (1.0 - f.voicedProbability, eps));
            };
            auto transition = [] (const Frame& a, int sa, const Frame& b, int sb)
            {
                const bool va = sa < a.count, vb = sb < b.count;
                if (va != vb)
                    return switchCost;
                if (! va)
                    return 0.0;
                const double d = std::abs (a.c[static_cast<std::size_t> (sa)].midi - b.c[static_cast<std::size_t> (sb)].midi);
                return d <= 0.5 ? 0.0 : 0.6 * (d - 0.5);
            };

            std::vector<std::array<double, maxCandidates + 1>> cost (n);
            std::vector<std::array<int, maxCandidates + 1>> back (n);
            for (int sIdx = 0; sIdx <= frames[0].count; ++sIdx)
                cost[0][static_cast<std::size_t> (sIdx)] = emission (frames[0], sIdx);
            for (std::size_t i = 1; i < n; ++i)
            {
                const auto& prev = frames[i - 1];
                const auto& cur = frames[i];
                for (int sb = 0; sb <= cur.count; ++sb)
                {
                    double best = 1.0e300;
                    int arg = prev.count;
                    for (int sa = 0; sa <= prev.count; ++sa)
                    {
                        const double c = cost[i - 1][static_cast<std::size_t> (sa)] + transition (prev, sa, cur, sb);
                        if (c < best)
                        {
                            best = c;
                            arg = sa;
                        }
                    }
                    cost[i][static_cast<std::size_t> (sb)] = best + emission (cur, sb);
                    back[i][static_cast<std::size_t> (sb)] = arg;
                }
            }
            int state = 0;
            {
                double best = 1.0e300;
                for (int sIdx = 0; sIdx <= frames[n - 1].count; ++sIdx)
                    if (cost[n - 1][static_cast<std::size_t> (sIdx)] < best)
                    {
                        best = cost[n - 1][static_cast<std::size_t> (sIdx)];
                        state = sIdx;
                    }
            }
            for (std::size_t i = n; i-- > 0;)
            {
                const auto& f = frames[i];
                auto& o = out.frames[i];
                if (state < f.count)
                {
                    const auto& c = f.c[static_cast<std::size_t> (state)];
                    o.hz = c.hz;
                    o.midi = c.midi;
                    o.confidence = std::clamp (c.probability, 0.0, 1.0);
                    o.voiced = o.levelDb >= pre.silenceThresholdDb && o.confidence >= 0.2;
                    if (! o.voiced)
                        o.hz = o.midi = 0.0;
                }
                if (i > 0)
                    state = back[i][static_cast<std::size_t> (state)];
            }
        }
    };

    //==========================================================================
    /** Pitch with unvoiced frames filled by linear interpolation inside a run (for smoothing). */
    std::vector<double> filledPitch (const PitchContour& c, std::size_t begin, std::size_t end)
    {
        std::vector<double> m (end - begin, 0.0);
        std::ptrdiff_t lastVoiced = -1;
        for (std::size_t i = begin; i < end; ++i)
        {
            const auto local = static_cast<std::ptrdiff_t> (i - begin);
            if (c.frames[i].voiced)
            {
                m[static_cast<std::size_t> (local)] = c.frames[i].midi;
                if (lastVoiced >= 0 && local - lastVoiced > 1)
                {
                    const double a = m[static_cast<std::size_t> (lastVoiced)], b = c.frames[i].midi;
                    for (auto k = lastVoiced + 1; k < local; ++k)
                        m[static_cast<std::size_t> (k)] = a + (b - a) * static_cast<double> (k - lastVoiced) / static_cast<double> (local - lastVoiced);
                }
                else if (lastVoiced < 0)
                    for (std::ptrdiff_t k = 0; k < local; ++k)
                        m[static_cast<std::size_t> (k)] = c.frames[i].midi;
                lastVoiced = local;
            }
        }
        if (lastVoiced >= 0)
            for (auto k = static_cast<std::size_t> (lastVoiced + 1); k < m.size(); ++k)
                m[k] = m[static_cast<std::size_t> (lastVoiced)];
        return m;
    }

    std::vector<double> movingMedian (const std::vector<double>& x, int radius)
    {
        std::vector<double> out (x.size());
        std::vector<double> w;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            w.clear();
            const auto lo = i >= static_cast<std::size_t> (radius) ? i - static_cast<std::size_t> (radius) : 0;
            const auto hi = std::min (x.size(), i + static_cast<std::size_t> (radius) + 1);
            w.assign (x.begin() + static_cast<std::ptrdiff_t> (lo), x.begin() + static_cast<std::ptrdiff_t> (hi));
            out[i] = median (w);
        }
        return out;
    }

    std::vector<double> movingAverage (const std::vector<double>& x, int radius)
    {
        std::vector<double> prefix (x.size() + 1, 0.0);
        for (std::size_t i = 0; i < x.size(); ++i)
            prefix[i + 1] = prefix[i] + x[i];
        std::vector<double> out (x.size());
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const auto lo = i >= static_cast<std::size_t> (radius) ? i - static_cast<std::size_t> (radius) : 0;
            const auto hi = std::min (x.size(), i + static_cast<std::size_t> (radius) + 1);
            out[i] = (prefix[hi] - prefix[lo]) / static_cast<double> (hi - lo);
        }
        return out;
    }

    /** Splits [begin, end) at pitch changes. Returns the split frames (absolute). */
    std::vector<std::size_t> pitchChanges (const PitchContour& c, std::size_t begin, std::size_t end, const AnalysisSettings& s)
    {
        std::vector<std::size_t> splits;
        const auto len = end - begin;
        const double fps = 1.0 / c.hopSeconds;
        const int hold = std::max (2, static_cast<int> (std::lround (s.pitchHoldSeconds * fps)));
        const int confirm = std::max (4, static_cast<int> (std::lround (0.15 * fps)));     // 150 ms: vibrato returns, notes stay
        const int minNote = std::max (2, static_cast<int> (std::lround (s.minNoteSeconds * fps)));
        if (len < static_cast<std::size_t> (2 * minNote))
            return splits;

        const auto raw = filledPitch (c, begin, end);
        // Light smoothing only (a 107 ms note in a fast run must survive it); vibrato is told
        // apart from note changes by its shape below, not by blurring it away.
        const auto smooth = movingAverage (movingMedian (raw, 2), std::max (1, static_cast<int> (std::lround (0.02 * fps))));
        const auto vibratoWindow = static_cast<std::size_t> (std::lround (0.3 * fps));

        // Vibrato (and slow, wide vibrato above all) keeps the pitch moving: few frames are flat
        // and the direction keeps reversing. A step to a new note has plateaus.
        // Returns the cycle length in frames when [from, to) looks like vibrato, else 0.
        auto vibratoPeriod = [&] (std::size_t from, std::size_t to)
        {
            if (to <= from + 4)
                return 0;
            std::size_t flat = 0;
            int reversals = 0, direction = 0;
            double extreme = raw[from];
            for (auto k = from + 1; k + 1 < to; ++k)
            {
                const double slope = std::abs (raw[k + 1] - raw[k - 1]) / (2.0 * c.hopSeconds);   // semitones per second
                flat += slope < 3.0 ? 1 : 0;
                const double x = raw[k];
                if (direction >= 0 && x < extreme - 0.15)
                {
                    reversals += direction > 0 ? 1 : 0;
                    direction = -1;
                    extreme = x;
                }
                else if (direction <= 0 && x > extreme + 0.15)
                {
                    reversals += direction < 0 ? 1 : 0;
                    direction = 1;
                    extreme = x;
                }
                else if ((direction > 0 && x > extreme) || (direction < 0 && x < extreme))
                    extreme = x;
            }
            if (static_cast<double> (flat) >= 0.35 * static_cast<double> (to - from - 2) || reversals < 3)
                return 0;
            return static_cast<int> (std::clamp (2.0 * static_cast<double> (to - from) / reversals, 0.1 * fps, 0.34 * fps));
        };
        // Inside vibrato the mean over one cycle hardly moves; across a note change it steps.
        auto cycleMeanRange = [&] (std::size_t from, std::size_t to, int period)
        {
            const auto p = static_cast<std::size_t> (period);
            if (to < from + p + 1)
                return 1.0e9;
            double sum = 0.0;
            for (auto k = from; k < from + p; ++k)
                sum += raw[k];
            double lo = sum / static_cast<double> (p), hi = lo;
            for (auto k = from + p; k < to; ++k)
            {
                sum += raw[k] - raw[k - p];
                lo = std::min (lo, sum / static_cast<double> (p));
                hi = std::max (hi, sum / static_cast<double> (p));
            }
            return hi - lo;
        };

        std::size_t segStart = 0;
        int over = 0;
        double seededCentre = 0.0;   // after a split: the confirmed new pitch, until the note has its own history
        bool seeded = false;
        auto centreOf = [&] (std::size_t from, std::size_t to)
        {
            // Robust centre of the current note so far (its last 250 ms at most).
            const auto window = static_cast<std::size_t> (0.25 * fps);
            const auto lo = to > from + window ? to - window : from;
            return median (std::vector<double> (smooth.begin() + static_cast<std::ptrdiff_t> (lo), smooth.begin() + static_cast<std::ptrdiff_t> (std::max (to, lo + 1))));
        };
        for (std::size_t i = static_cast<std::size_t> (minNote); i < len; ++i)
        {
            if (i < segStart + static_cast<std::size_t> (minNote))
                continue;
            const double centre = seeded && i < segStart + static_cast<std::size_t> (confirm) ? seededCentre : centreOf (segStart, i);
            if (std::abs (smooth[i] - centre) > s.pitchChangeSemitones)
                ++over;
            else
                over = 0;
            if (over < hold)
                continue;

            const auto first = i - static_cast<std::size_t> (hold) + 1;
            const auto confirmEnd = std::min (len, first + static_cast<std::size_t> (confirm));
            const double newCentre = median (std::vector<double> (raw.begin() + static_cast<std::ptrdiff_t> (first), raw.begin() + static_cast<std::ptrdiff_t> (confirmEnd)));
            over = 0;
            if (std::abs (newCentre - centre) <= s.pitchChangeSemitones)
                continue;   // an excursion that came back (vibrato, scoop): same note
            {
                // Inside vibrato, compare the means over whole cycles (300 ms covers one at 3.3 Hz).
                const auto lo = first > segStart + vibratoWindow ? first - vibratoWindow : segStart;
                const auto hi = std::min (len, first + vibratoWindow);
                if (const int period = vibratoPeriod (lo, hi); period > 0 && cycleMeanRange (lo, hi, period) <= s.pitchChangeSemitones)
                    continue;
            }

            // The boundary is where the raw pitch crosses half-way between the two notes.
            const double mid = 0.5 * (centre + newCentre);
            const double dir = newCentre > centre ? 1.0 : -1.0;
            const auto searchFrom = first > static_cast<std::size_t> (2 * hold) ? first - static_cast<std::size_t> (2 * hold) : 0;
            std::size_t boundary = first;
            for (auto k = std::max (searchFrom, segStart + 1); k <= first; ++k)
                if ((raw[k] - mid) * dir > 0.0)
                {
                    boundary = k;
                    break;
                }
            if (boundary < segStart + static_cast<std::size_t> (minNote) || len - boundary < static_cast<std::size_t> (minNote))
                continue;
            splits.push_back (begin + boundary);
            segStart = boundary;
            seededCentre = newCentre;
            seeded = true;
        }
        return splits;
    }

    /** Splits at level dips that re-attack the note (a re-bow, a re-pluck, a new breath). A dip
        without a sharp rise or a breath (a bow change, a swell, beating) splits only when very
        deep; shallower ones that the plain level rule would have split go to `doubtful`. */
    std::vector<std::size_t> rearticulations (const PitchContour& c, std::size_t begin, std::size_t end, const AnalysisSettings& s,
                                              std::vector<std::size_t>& doubtful)
    {
        std::vector<std::size_t> splits;
        const double fps = 1.0 / c.hopSeconds;
        const auto minNote = static_cast<std::size_t> (std::max (2L, std::lround (s.minNoteSeconds * fps)));
        const auto reach = static_cast<std::size_t> (std::lround (0.15 * fps));
        if (end - begin < 2 * minNote)
            return splits;

        std::vector<double> level (end - begin);
        for (std::size_t i = begin; i < end; ++i)
            level[i - begin] = c.frames[i].levelDb;
        level = movingAverage (level, 2);

        struct Dip
        {
            std::size_t at;
            double depth;
            bool splits;   ///< a re-attack (sharp rise, breath, or very deep)
            bool sharp;    ///< the rise out of it is an attack
        };
        std::vector<Dip> dips;
        // The rise out of a dip, measured over 10 ms steps within 50 ms: a bow or a plectrum
        // brings the tone back within a few milliseconds, a bow change or a swell over tens.
        const auto step = std::max<std::size_t> (1, static_cast<std::size_t> (std::lround (0.01 * fps)));
        const auto riseWindow = static_cast<std::size_t> (std::lround (0.05 * fps));
        for (std::size_t i = minNote; i + minNote < level.size(); ++i)
        {
            bool isMin = true;
            for (std::size_t k = i - 3; k <= i + 3 && isMin; ++k)
                if (k != i && level[k] < level[i])
                    isMin = false;
            if (! isMin)
                continue;
            // Depth against the typical level either side (median over 150 ms), not the loudest
            // moment: breath and bow swells peak and sag around a held tone without re-attacking it,
            // while a re-articulation falls well under the sound on both sides.
            const auto lo = i > reach ? i - reach : 0;
            const auto hi = std::min (level.size(), i + reach + 1);
            const auto side = [&] (std::size_t from, std::size_t to)
            { return median (std::vector<double> (level.begin() + static_cast<std::ptrdiff_t> (from), level.begin() + static_cast<std::ptrdiff_t> (std::max (to, from + 1)))); };
            const double depth = std::min (side (lo, i > 3 ? i - 3 : i), side (std::min (i + 3, hi - 1), hi)) - level[i];
            bool unvoicedNear = false;
            for (std::size_t k = (i >= 3 ? i - 3 : 0); k <= std::min (level.size() - 1, i + 3); ++k)
                unvoicedNear = unvoicedNear || ! c.frames[begin + k].voiced;
            double rise = 0.0, rawRise = 0.0;
            for (auto k = i; k < std::min (i + riseWindow, level.size() - step); ++k)
            {
                rise = std::max (rise, level[k + step] - level[k]);
                rawRise = std::max (rawRise, static_cast<double> (c.frames[begin + k + step].levelDb - c.frames[begin + k].levelDb));
            }
            // A rise within a few milliseconds is smoothed away above; on the unsmoothed level it
            // marks a new attack when the dip is a full re-articulation deep.
            const bool sharp = rise >= s.attackRiseDb || (rawRise >= 1.4 * s.attackRiseDb && depth >= s.rearticulationDb);
            if ((sharp && depth >= 0.5 * s.rearticulationDb) || (unvoicedNear && depth >= s.unvoicedDipDb))
                dips.push_back ({ i, depth, true, sharp });
            else if (depth >= std::min (s.rearticulationDb, s.softDipDb))
                dips.push_back ({ i, depth, depth >= s.softDipDb, false });
        }
        // A re-pluck on a decaying or steady tone need not dip first: the level jumps within a few
        // milliseconds (on the unsmoothed level; a bow change or a swell takes tens) and stays up.
        {
            const auto ctx = static_cast<std::size_t> (std::lround (0.05 * fps));
            const auto levelOver = [&] (std::size_t from, std::size_t to)
            { return median (std::vector<double> (level.begin() + static_cast<std::ptrdiff_t> (from), level.begin() + static_cast<std::ptrdiff_t> (to))); };
            const auto raw = [&] (std::size_t k) { return static_cast<double> (c.frames[begin + k].levelDb); };
            for (std::size_t i = std::max (minNote, ctx + 8); i + std::max (minNote, ctx + step + 2) < level.size(); ++i)
            {
                if (raw (i + step) - raw (i) < 1.5 * s.attackRiseDb)
                    continue;
                auto at = i;
                for (auto k = i - 6; k <= i; ++k)
                    if (raw (k) < raw (at))
                        at = k;
                const double stepUp = levelOver (at + step + 2, std::min (level.size(), at + step + 2 + ctx)) - levelOver (at - ctx - 2, at - 2);
                if (stepUp >= 0.5 * s.rearticulationDb)
                    dips.push_back ({ at, stepUp, true, true });
                i += step + 4;
            }
        }
        std::sort (dips.begin(), dips.end(), [] (const Dip& a, const Dip& b) { return a.at < b.at; });
        const auto regular = [&] (const std::vector<Dip>& train, double from, double to, double maxCv)
        {
            if (train.size() < 2)
                return false;
            std::vector<double> spacing;
            for (std::size_t k = 1; k < train.size(); ++k)
                spacing.push_back (static_cast<double> (train[k].at - train[k - 1].at) * c.hopSeconds);
            double m = 0.0;
            for (double x : spacing)
                m += x;
            m /= static_cast<double> (spacing.size());
            double var = 0.0;
            for (double x : spacing)
                var += (x - m) * (x - m);
            const double cv = std::sqrt (var / static_cast<double> (spacing.size())) / m;
            return m >= from && m <= to && cv < maxCv;
        };
        // Tremolo is a train of evenly spaced dips (3-10 Hz); a re-articulation is a single
        // event. Four or more dips at a regular spacing are the tremolo and do not split.
        {
            std::vector<Dip> splitting;
            for (const auto& d : dips)
                if (d.splits)
                    splitting.push_back (d);
            if (splitting.size() >= 4 && regular (splitting, 0.1, 0.34, 0.2))
                dips.clear();
        }
        // Beating between two strings (or a slow tremolo) is a slower, very regular train of
        // dips; it is part of the sound, not a place to split or to doubt. Only a sharp attack
        // inside the train still splits. Repeated notes in a steady tempo are regular too, but
        // each holds its level between the dips; beating never settles (under half the time
        // within 1.5 dB of its peak).
        {
            std::vector<Dip> train;
            for (const auto& d : dips)
                if (d.depth >= s.rearticulationDb)
                    train.push_back (d);
            double settled = 0.0;
            for (std::size_t k = 1; k < train.size(); ++k)
            {
                const auto from = level.begin() + static_cast<std::ptrdiff_t> (train[k - 1].at);
                const auto to = level.begin() + static_cast<std::ptrdiff_t> (train[k].at + 1);
                const double peak = *std::max_element (from, to);
                settled += static_cast<double> (std::count_if (from, to, [&] (double l) { return l >= peak - 1.5; }))
                           / static_cast<double> (to - from) / static_cast<double> (train.size() - 1);
            }
            if (train.size() >= 3 && regular (train, 0.25, 0.8, 0.15) && settled < 0.45)
                dips.erase (std::remove_if (dips.begin(), dips.end(), [] (const Dip& d) { return ! d.sharp; }), dips.end());
        }
        std::sort (dips.begin(), dips.end(), [] (const Dip& a, const Dip& b) { return a.splits != b.splits ? a.splits : a.depth > b.depth; });
        std::vector<std::size_t> accepted, doubts;
        const auto farFrom = [&] (std::size_t at, const std::vector<std::size_t>& others)
        {
            bool far = at >= minNote && level.size() - at >= minNote;
            for (auto a : others)
                far = far && (at > a ? at - a : a - at) >= minNote;
            return far;
        };
        for (const auto& d : dips)
        {
            if (d.splits && farFrom (d.at, accepted))
                accepted.push_back (d.at);
            else if (! d.splits && farFrom (d.at, accepted) && farFrom (d.at, doubts))
                doubts.push_back (d.at);
        }
        std::sort (accepted.begin(), accepted.end());
        std::sort (doubts.begin(), doubts.end());
        for (auto a : accepted)
            splits.push_back (begin + a);
        for (auto d : doubts)
            doubtful.push_back (begin + d);
        return splits;
    }
    /** What the consolidation rules need to know about a candidate note. */
    struct PieceInfo
    {
        double centre = 0.0;      ///< median pitch of its voiced frames (MIDI)
        double head = 0.0;        ///< median pitch of its first quarter
        double tail = 0.0;        ///< ... and of its last quarter
        double levelDb = -120.0;  ///< median level
        double confidence = 0.0;  ///< mean over voiced frames
        bool plateau = false;     ///< most voiced frames within 0.3 semitone of the centre
        bool weak = false;        ///< barely voiced or barely periodic
        double seconds = 0.0;
    };

    PieceInfo describePiece (const PitchContour& c, const Segment& seg)
    {
        PieceInfo p;
        std::vector<double> pitch, level;
        double conf = 0.0;
        for (auto k = seg.begin; k < seg.end; ++k)
        {
            level.push_back (c.frames[k].levelDb);
            if (c.frames[k].voiced)
            {
                pitch.push_back (c.frames[k].midi);
                conf += c.frames[k].confidence;
            }
        }
        const auto len = seg.end - seg.begin;
        p.seconds = static_cast<double> (len) * c.hopSeconds;
        p.levelDb = median (level);
        if (pitch.empty())
        {
            p.weak = true;
            return p;
        }
        p.centre = median (pitch);
        const auto quarter = std::max<std::size_t> (1, pitch.size() / 4);
        p.head = median (std::vector<double> (pitch.begin(), pitch.begin() + static_cast<std::ptrdiff_t> (quarter)));
        p.tail = median (std::vector<double> (pitch.end() - static_cast<std::ptrdiff_t> (quarter), pitch.end()));
        p.confidence = conf / static_cast<double> (pitch.size());
        std::size_t near = 0;
        for (double m : pitch)
            near += std::abs (m - p.centre) < 0.3 ? 1 : 0;
        p.plateau = near * 2 > pitch.size();
        p.weak = static_cast<double> (pitch.size()) < 0.3 * static_cast<double> (len) || p.confidence < 0.5;
        return p;
    }

    /** Distance in semitones with octave slips folded away (the tracker's classic error). */
    double foldedDistance (double a, double b)
    {
        const double d = std::abs (a - b);
        return std::min (d, std::abs (d - 12.0));
    }

    /**
        Pieces that are not notes of their own join the note they belong to (audio is never thrown
        away; it becomes that note's attack or release, and KEEP ATTACK decides how much of a scoop
        is heard):
          - a scoop or slide into a note at the start of a phrase, and a fall or octave slip in its
            breathy tail at the end;
          - MIN NOTE: anything shorter than `minPieceSeconds` joined to a neighbour that has no
            pitch of its own: a fragment of the same note, a slide without a plateau, an octave
            slip, a weak scrap. Short notes with their own steady pitch (runs, grace notes) stay.
          - a short scrap after a breath-length gap at the same pitch is that note's tail.
    */
    void consolidate (std::vector<Segment>& segs, const PitchContour& c, const AnalysisSettings& s)
    {
        const double fps = 1.0 / c.hopSeconds;
        const auto mergeInto = [&] (std::size_t from, std::size_t into)
        {
            auto& a = segs[into];
            if (segs[from].begin < a.begin)
                a.pitchCut = segs[from].pitchCut;
            a.begin = std::min (a.begin, segs[from].begin);
            a.end = std::max (a.end, segs[from].end);
            segs.erase (segs.begin() + static_cast<std::ptrdiff_t> (from));
        };

        for (int pass = 0; pass < 64; ++pass)
        {
            bool changed = false;
            for (std::size_t i = 0; i < segs.size() && ! changed; ++i)
            {
                const auto& seg = segs[i];
                const bool gluedPrev = i > 0 && segs[i - 1].end == seg.begin;
                const bool gluedNext = i + 1 < segs.size() && segs[i + 1].begin == seg.end;
                const auto p = describePiece (c, seg);

                // Scoop into the first note of a phrase: short, travelling towards the note, or a
                // brief approach (the boundary sits half-way up the slide, so the piece left over
                // can look flat) leaning into a much longer note close by.
                if (gluedNext && ! gluedPrev && p.seconds <= 0.25)
                {
                    const auto next = describePiece (c, segs[i + 1]);
                    const bool travels = std::abs (p.tail - next.centre) + 0.5 <= std::abs (p.head - next.centre);
                    const bool slide = travels && ! p.plateau && std::abs (p.centre - next.centre) <= 3.0;
                    const bool scoop = travels && std::abs (p.centre - next.centre) <= 2.5;
                    const bool approach = p.seconds <= 0.2 && next.seconds >= 3.0 * p.seconds && std::abs (p.centre - next.centre) <= 2.5;
                    const bool slip = std::abs (std::abs (p.centre - next.centre) - 12.0) < 0.7;
                    if (slide || scoop || approach || slip || p.weak)
                    {
                        mergeInto (i, i + 1);
                        changed = true;
                        continue;
                    }
                }
                // Fall, octave slip or fading scrap at the end of a phrase.
                if (gluedPrev && ! gluedNext && p.seconds <= 0.4)
                {
                    const auto prev = describePiece (c, segs[i - 1]);
                    const bool falls = std::abs (p.tail - prev.centre) >= std::abs (p.head - prev.centre) + 0.5 && ! p.plateau;
                    const bool slip = std::abs (std::abs (p.centre - prev.centre) - 12.0) < 0.7;
                    const bool fading = p.levelDb <= prev.levelDb - 10.0;
                    if (falls || slip || fading || p.weak)
                    {
                        mergeInto (i, i - 1);
                        changed = true;
                        continue;
                    }
                }
                // MIN NOTE.
                if ((gluedPrev || gluedNext) && p.seconds < s.minPieceSeconds)
                {
                    const auto prev = gluedPrev ? describePiece (c, segs[i - 1]) : PieceInfo {};
                    const auto next = gluedNext ? describePiece (c, segs[i + 1]) : PieceInfo {};
                    const double dPrev = gluedPrev ? foldedDistance (p.centre, prev.centre) : 1.0e9;
                    const double dNext = gluedNext ? foldedDistance (p.centre, next.centre) : 1.0e9;
                    const bool samePitch = std::min (dPrev, dNext) < 0.5;
                    if (p.weak || ! p.plateau || samePitch)
                    {
                        mergeInto (i, dPrev <= dNext ? i - 1 : i + 1);
                        changed = true;
                        continue;
                    }
                }
                // An octave slip or a weak scrap across a tiny gap (a voicing dropout) belongs to the
                // note on the other side of the gap.
                if (! gluedPrev && ! gluedNext && p.seconds < s.minPieceSeconds)
                {
                    const auto gapFrames = static_cast<std::size_t> (std::lround (0.08 * fps));
                    const bool nearPrev = i > 0 && seg.begin - segs[i - 1].end <= gapFrames;
                    const bool nearNext = i + 1 < segs.size() && segs[i + 1].begin - seg.end <= gapFrames;
                    const auto slipOf = [&] (std::size_t j)
                    {
                        const auto other = describePiece (c, segs[j]);
                        return std::abs (std::abs (p.centre - other.centre) - 12.0) < 0.7 && other.seconds > p.seconds;
                    };
                    if (nearNext && (slipOf (i + 1) || p.weak))
                    {
                        mergeInto (i, i + 1);
                        changed = true;
                        continue;
                    }
                    if (nearPrev && (slipOf (i - 1) || p.weak))
                    {
                        mergeInto (i, i - 1);
                        changed = true;
                        continue;
                    }
                }
                // A scrap after a breath-length gap at the same pitch: the tail of the note before.
                if (! gluedPrev && ! gluedNext && i > 0 && p.seconds < s.minPieceSeconds
                    && static_cast<double> (seg.begin - segs[i - 1].end) <= 0.1 * fps)
                {
                    const auto prev = describePiece (c, segs[i - 1]);
                    if (foldedDistance (p.centre, prev.centre) < 0.5 && (p.weak || p.levelDb <= prev.levelDb - 6.0))
                    {
                        mergeInto (i, i - 1);
                        changed = true;
                        continue;
                    }
                }
            }
            if (! changed)
                break;
        }
    }
}

//==============================================================================
Preprocessed preprocess (const AudioData& audio, const AnalysisSettings& s)
{
    Preprocessed pre;
    pre.sampleRate = audio.sampleRate;
    if (audio.isEmpty())
        return pre;
    const auto limit = static_cast<std::int64_t> (s.maxAnalysisSeconds * audio.sampleRate);
    pre.analysedFrames = std::min (audio.numFrames(), limit);
    pre.mono = audio.mixToMono();
    pre.mono.resize (static_cast<std::size_t> (pre.analysedFrames));
    highPass (pre.mono, audio.sampleRate, s.highPassHz);

    const int hop = hopSamplesFor (s, audio.sampleRate);
    const auto count = frameCount (pre, hop);
    std::vector<double> prefix (pre.mono.size() + 1, 0.0);
    for (std::size_t i = 0; i < pre.mono.size(); ++i)
        prefix[i + 1] = prefix[i] + static_cast<double> (pre.mono[i]) * pre.mono[i];
    pre.levelDb.resize (count);
    pre.peakDb = -120.0f;
    for (std::size_t f = 0; f < count; ++f)
    {
        const auto centre = static_cast<std::int64_t> (f) * hop;
        const auto lo = std::max<std::int64_t> (0, centre - hop);
        const auto hi = std::min<std::int64_t> (pre.analysedFrames, centre + hop);
        const double power = hi > lo ? (prefix[static_cast<std::size_t> (hi)] - prefix[static_cast<std::size_t> (lo)]) / static_cast<double> (hi - lo) : 0.0;
        pre.levelDb[f] = static_cast<float> (powerToDb (power, -120.0));
        pre.peakDb = std::max (pre.peakDb, pre.levelDb[f]);
    }
    // Recordings with a noise floor (room, hiss) never fall to the peak-relative threshold: the
    // quietest tenth of the (non-digital-silence) frames estimates the floor, and "sound" starts
    // 6 dB above it, but never higher than 30 dB under the peak (a phrase without any pause has
    // no floor to find, only quiet notes).
    std::vector<float> audible;
    for (auto l : pre.levelDb)
        if (l > -119.0f)
            audible.push_back (l);
    double noiseFloor = -120.0;
    if (audible.size() >= 10)
    {
        const auto tenth = audible.size() / 10;
        std::nth_element (audible.begin(), audible.begin() + static_cast<std::ptrdiff_t> (tenth), audible.end());
        noiseFloor = audible[tenth];
    }
    pre.noiseFloorDb = static_cast<float> (noiseFloor);
    pre.silenceThresholdDb = static_cast<float> (std::max ({ static_cast<double> (pre.peakDb) + s.silenceDb, s.absoluteFloorDb,
                                                             std::min (noiseFloor + 6.0, static_cast<double> (pre.peakDb) - 30.0) }));
    return pre;
}

std::unique_ptr<PitchTracker> makePitchTracker (PitchBackend backend)
{
    if (backend == PitchBackend::yin)
        return std::make_unique<YinTracker>();
    return std::make_unique<YinViterbiTracker>();
}

std::vector<Segment> segment (const PitchContour& c, const Preprocessed& pre, const AnalysisSettings& settings)
{
    const auto s = settings.effective();
    std::vector<Segment> result;
    const auto n = c.frames.size();
    if (n == 0)
        return result;
    const double fps = 1.0 / c.hopSeconds;
    const auto bridge = static_cast<std::size_t> (std::lround (s.bridgeGapSeconds * fps));
    const auto minNote = static_cast<std::size_t> (std::max (2L, std::lround (s.minNoteSeconds * fps)));

    // Voiced runs; short dropouts that stay above the silence threshold and keep the pitch
    // are bridged (a breathy frame or two inside a sung note).
    struct Run
    {
        std::size_t begin, end;
        bool pitchCut = false;   ///< a short loud gap that only the pitch jump kept open
    };
    std::vector<Run> runs;
    for (std::size_t i = 0; i < n;)
    {
        if (! c.frames[i].voiced)
        {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < n && c.frames[j].voiced)
            ++j;
        if (! runs.empty())
        {
            auto& last = runs.back();
            const auto gap = i - last.end;
            bool loud = true;
            for (auto k = last.end; k < i; ++k)
                loud = loud && c.frames[k].levelDb >= pre.silenceThresholdDb;
            const bool continuous = std::abs (c.frames[last.end - 1].midi - c.frames[i].midi) < 1.0;
            // Longer dropouts too, when the sound stays near the note's own level and the pitch
            // comes back unchanged: a breathy tone sinking into its air noise for a moment is the
            // same note (breath between two notes is much quieter than either).
            bool sustained = false;
            if (! (gap <= bridge) && gap <= static_cast<std::size_t> (std::lround (0.25 * fps)) && loud)
            {
                const auto around = [&] (std::size_t from, std::size_t to)
                {
                    std::vector<double> l;
                    for (auto k = from; k < to; ++k)
                        l.push_back (c.frames[k].levelDb);
                    return median (l);
                };
                const auto w = static_cast<std::size_t> (std::lround (0.1 * fps));
                const double noteLevel = std::min (around (last.end > last.begin + w ? last.end - w : last.begin, last.end), around (i, std::min (j, i + w)));
                double gapLow = 1.0e9;
                for (auto k = last.end; k < i; ++k)
                    gapLow = std::min (gapLow, static_cast<double> (c.frames[k].levelDb));
                sustained = gapLow >= noteLevel - 10.0 && std::abs (c.frames[last.end - 1].midi - c.frames[i].midi) < 0.5;
            }
            if ((gap <= bridge && loud && continuous) || sustained)
            {
                last.end = j;
                i = j;
                continue;
            }
            runs.push_back ({ i, j, gap <= bridge && loud });
            i = j;
            continue;
        }
        runs.push_back ({ i, j });
        i = j;
    }

    std::vector<std::size_t> doubtful;
    for (const auto& run : runs)
    {
        std::vector<std::size_t> cuts = pitchChanges (c, run.begin, run.end, s);
        std::vector<std::size_t> bounds { run.begin };
        bounds.insert (bounds.end(), cuts.begin(), cuts.end());
        bounds.push_back (run.end);
        for (std::size_t b = 0; b + 1 < bounds.size(); ++b)
        {
            std::vector<std::size_t> sub { bounds[b] };
            const auto dips = rearticulations (c, bounds[b], bounds[b + 1], s, doubtful);
            sub.insert (sub.end(), dips.begin(), dips.end());
            sub.push_back (bounds[b + 1]);
            for (std::size_t k = 0; k + 1 < sub.size(); ++k)
            {
                Segment seg;
                seg.begin = sub[k];
                seg.end = sub[k + 1];
                seg.legatoIn = k > 0 || b > 0;
                seg.pitchCut = k == 0 && (b > 0 || run.pitchCut);
                seg.legatoOut = k + 2 < sub.size() || b + 2 < bounds.size();
                result.push_back (seg);
            }
        }
    }

    // A short piece between two legato neighbours whose pitch travels more than a semitone is
    // the glide between them, not a note: its frames go to the neighbours, split where the
    // pitch crosses half-way between their centres.
    {
        const auto glideMax = static_cast<std::size_t> (std::lround (0.12 * fps));
        auto centre = [&] (const Segment& seg)
        {
            std::vector<double> m;
            for (auto k = seg.begin; k < seg.end; ++k)
                if (c.frames[k].voiced)
                    m.push_back (c.frames[k].midi);
            return median (m);
        };
        for (std::size_t i = 1; i + 1 < result.size(); ++i)
        {
            auto& seg = result[i];
            if (seg.end - seg.begin > glideMax || result[i - 1].end != seg.begin || result[i + 1].begin != seg.end)
                continue;
            double lo = 1.0e9, hi = -1.0e9;
            for (auto k = seg.begin; k < seg.end; ++k)
                if (c.frames[k].voiced)
                {
                    lo = std::min (lo, c.frames[k].midi);
                    hi = std::max (hi, c.frames[k].midi);
                }
            if (hi - lo < 1.0)
                continue;
            // A real short note (a fast run) has a plateau: most of its frames near its centre.
            const double own = centre (seg);
            std::size_t near = 0, voicedFrames = 0;
            for (auto k = seg.begin; k < seg.end; ++k)
                if (c.frames[k].voiced)
                {
                    ++voicedFrames;
                    near += std::abs (c.frames[k].midi - own) < 0.3 ? 1 : 0;
                }
            if (near * 2 > voicedFrames)
                continue;
            const double a = centre (result[i - 1]), b = centre (result[i + 1]);
            const double mid = 0.5 * (a + b);
            auto cut = seg.begin + (seg.end - seg.begin) / 2;
            for (auto k = seg.begin; k < seg.end; ++k)
                if (c.frames[k].voiced && (c.frames[k].midi - mid) * (b - a) > 0.0)
                {
                    cut = k;
                    break;
                }
            result[i - 1].end = cut;
            result[i + 1].begin = cut;
            result[i + 1].pitchCut = true;
            seg.end = seg.begin;   // emptied; removed below
        }
        result.erase (std::remove_if (result.begin(), result.end(), [] (const Segment& sg) { return sg.end <= sg.begin; }), result.end());
    }

    // Too short or barely voiced: not a note. A short piece glued to a neighbour is merged
    // into it instead of dropped (its audio belongs to that note's attack or tail).
    std::vector<Segment> kept;
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        auto seg = result[i];
        std::size_t voiced = 0;
        for (auto k = seg.begin; k < seg.end; ++k)
            voiced += c.frames[k].voiced ? 1 : 0;
        const bool tooShort = seg.end - seg.begin < minNote;
        const bool weak = static_cast<double> (voiced) < 0.3 * static_cast<double> (seg.end - seg.begin);
        if (! tooShort && ! weak)
        {
            kept.push_back (seg);
            continue;
        }
        if (tooShort && seg.legatoIn && ! kept.empty() && kept.back().end == seg.begin)
        {
            kept.back().end = seg.end;
            kept.back().legatoOut = seg.legatoOut;
        }
        else if (tooShort && seg.legatoOut && i + 1 < result.size())
        {
            result[i + 1].begin = seg.begin;
            result[i + 1].legatoIn = seg.legatoIn;
            result[i + 1].pitchCut = seg.pitchCut;
        }
    }

    consolidate (kept, c, s);
    for (std::size_t i = 0; i < kept.size(); ++i)
    {
        kept[i].legatoIn = i > 0 && kept[i - 1].end == kept[i].begin;
        kept[i].legatoOut = i + 1 < kept.size() && kept[i + 1].begin == kept[i].end;
        for (auto d : doubtful)
            if (d >= kept[i].begin + minNote && d + minNote <= kept[i].end)
                kept[i].possibleRepeats.push_back (d);
    }
    return kept;
}

atlas::NoteEvent describe (const Segment& seg, const Segment* previous, const Segment* next, const PitchContour& c, const Preprocessed& pre,
                           const AnalysisSettings& s)
{
    atlas::NoteEvent note;
    const double fps = 1.0 / c.hopSeconds;
    const int hop = hopSamplesFor (s, pre.sampleRate);
    const auto len = seg.end - seg.begin;
    const auto totalFrames = pre.analysedFrames;

    // --- 4: robust centre pitch on the body (glides at both ends left out) ---
    // Octave slips (often in an absorbed breathy tail) are folded back first, so they never
    // read as pitch spread or vibrato.
    std::vector<double> folded (len, 0.0);
    {
        std::vector<double> m;
        for (auto i = seg.begin; i < seg.end; ++i)
            if (c.frames[i].voiced)
                m.push_back (c.frames[i].midi);
        const double rough = median (m);
        for (auto i = seg.begin; i < seg.end; ++i)
        {
            double x = c.frames[i].midi;
            while (c.frames[i].voiced && x - rough > 6.0)
                x -= 12.0;
            while (c.frames[i].voiced && rough - x > 6.0)
                x += 12.0;
            folded[i - seg.begin] = x;
        }
    }
    const auto pitchAt = [&] (std::size_t i) { return folded[i - seg.begin]; };
    const auto trim = len > 30 ? std::min<std::size_t> (static_cast<std::size_t> (0.2 * static_cast<double> (len)), 12) : 0;
    std::vector<std::pair<double, double>> weighted;
    double confSum = 0.0;
    std::size_t voicedCount = 0;
    for (auto i = seg.begin; i < seg.end; ++i)
        if (c.frames[i].voiced)
        {
            ++voicedCount;
            confSum += c.frames[i].confidence;
        }
    for (auto i = seg.begin + trim; i < seg.end - trim; ++i)
        if (c.frames[i].voiced)
            weighted.emplace_back (pitchAt (i), std::max (1.0e-6, c.frames[i].confidence * dbToGain (c.frames[i].levelDb)));
    if (weighted.empty())
        for (auto i = seg.begin; i < seg.end; ++i)
            if (c.frames[i].voiced)
                weighted.emplace_back (pitchAt (i), std::max (1.0e-6, c.frames[i].confidence * dbToGain (c.frames[i].levelDb)));

    const double centre = weightedMedian (weighted);
    std::vector<std::pair<double, double>> deviations;
    for (const auto& [m, w] : weighted)
        deviations.emplace_back (std::abs (m - centre), w);
    const double stability = weightedMedian (deviations) * 1.4826 * 100.0;

    // Expressive deviation kept apart: residual around a slow trend -> vibrato rate and depth.
    {
        std::vector<double> body;
        for (auto i = seg.begin + trim; i < seg.end - trim; ++i)
            if (c.frames[i].voiced)
                body.push_back (pitchAt (i));
        if (body.size() > static_cast<std::size_t> (0.25 * fps))
        {
            const auto trend = movingAverage (body, static_cast<int> (std::lround (0.1 * fps)));
            double sumSq = 0.0;
            int crossings = 0;
            int sign = 0;
            for (std::size_t i = 0; i < body.size(); ++i)
            {
                const double r = body[i] - trend[i];
                sumSq += r * r;
                const int sgn = r > 0.05 ? 1 : (r < -0.05 ? -1 : 0);
                if (sgn != 0 && sign != 0 && sgn != sign)
                    ++crossings;
                if (sgn != 0)
                    sign = sgn;
            }
            const double seconds = static_cast<double> (body.size()) / fps;
            const double rate = crossings / (2.0 * seconds);
            const double depth = std::sqrt (2.0 * sumSq / static_cast<double> (body.size())) * 100.0;
            if (rate >= 3.0 && rate <= 10.0 && depth >= 10.0)
            {
                note.vibratoRateHz = rate;
                note.vibratoDepthCents = depth;
            }
        }
    }

    note.midiPitch = centre;
    note.pitchHz = centre > 0.0 ? midiToHz (centre, s.a4Hz) : 0.0;
    note.pitchStabilityCents = stability;
    note.voicing = len > 0 ? static_cast<double> (voicedCount) / static_cast<double> (len) : 0.0;
    const double meanConf = voicedCount > 0 ? confSum / static_cast<double> (voicedCount) : 0.0;
    // Vibrato is expressive, not instability: judge the spread left after removing it.
    const double residualSpread = std::max (0.0, stability - 0.7 * note.vibratoDepthCents);
    const double stabilityFactor = std::clamp (1.0 - std::max (0.0, residualSpread - 25.0) / 150.0, 0.2, 1.0);
    note.confidence = std::clamp (note.voicing * meanConf * stabilityFactor, 0.0, 1.0);

    // --- 5: boundaries ---
    auto toSample = [&] (std::size_t frame) { return std::clamp<std::int64_t> (static_cast<std::int64_t> (frame) * hop, 0, totalFrames); };
    const auto voicedStart = toSample (seg.begin);
    const auto prevEnd = previous != nullptr ? toSample (previous->end) : std::int64_t { 0 };

    // The louder part of the note's first 100 ms sets the attack-search floor.
    float notePeak = -120.0f;
    for (auto i = seg.begin; i < std::min (seg.end, seg.begin + static_cast<std::size_t> (0.1 * fps)); ++i)
        notePeak = std::max (notePeak, c.frames[i].levelDb);

    if (seg.legatoIn)
    {
        note.attackStart = voicedStart;
        note.sourceStart = voicedStart;
        note.fadeInFrames = std::min<std::int64_t> (64, hop);
    }
    else
    {
        // Pitch needs voicing, but consonants, bow noise and picks come first: walk back while
        // the level is still clearly above the floor (at most 120 ms, never into the previous note).
        // Reaching the recording's noise floor also ends the search (a hissy recording never
        // falls to the silence threshold).
        const auto limitFrame = std::max<std::ptrdiff_t> (previous != nullptr ? static_cast<std::ptrdiff_t> (previous->end) : 0,
                                                          static_cast<std::ptrdiff_t> (seg.begin) - static_cast<std::ptrdiff_t> (0.12 * fps));
        // When true silence comes before the note, the walk ends there; the noise-floor
        // estimate is only for hissy recordings (with digital silence between notes it is really
        // the quiet moments of breathy notes, and would stop a breathy attack too early).
        bool silenceBefore = false;
        for (auto f = limitFrame; f < static_cast<std::ptrdiff_t> (seg.begin); ++f)
            silenceBefore = silenceBefore || c.frames[static_cast<std::size_t> (f)].levelDb < pre.silenceThresholdDb;
        const float floor = silenceBefore ? std::max (pre.silenceThresholdDb, notePeak - 35.0f)
                                          : std::max ({ pre.silenceThresholdDb, notePeak - 35.0f, pre.noiseFloorDb + 3.0f });
        // It also stops in the valley before the onset: breath or a previous tail that rises
        // again further back is not this note's attack.
        auto k = static_cast<std::ptrdiff_t> (seg.begin);
        auto level = [&] (std::ptrdiff_t f) { return c.frames[static_cast<std::size_t> (f)].levelDb; };
        while (k > limitFrame && level (k - 1) > floor)
        {
            if (level (k) < notePeak - 12.0f && level (k - 1) > level (k) + 1.5f)
                break;
            --k;
        }
        const auto onset = toSample (static_cast<std::size_t> (k));
        note.attackStart = std::max (prevEnd, onset - hop);   // one hop of pre-roll
        note.sourceStart = std::max (prevEnd, note.attackStart - static_cast<std::int64_t> (0.02 * pre.sampleRate));
        const bool fromSilence = k > 0 && c.frames[static_cast<std::size_t> (k - 1)].levelDb < pre.silenceThresholdDb;
        note.fadeInFrames = fromSilence ? 32 : static_cast<std::int64_t> (0.002 * pre.sampleRate);
    }

    // Sustain: from the attack's peak once the pitch has settled, to the last settled frame.
    const auto peakSearchEnd = seg.begin + std::max<std::size_t> (1, std::min<std::size_t> (len / 3, static_cast<std::size_t> (0.12 * fps)));
    auto peakFrame = seg.begin;
    for (auto i = seg.begin; i < peakSearchEnd; ++i)
        if (c.frames[i].levelDb > c.frames[peakFrame].levelDb)
            peakFrame = i;
    const auto settleLimit = seg.begin + static_cast<std::size_t> (0.4 * static_cast<double> (len));
    auto s0 = std::min (std::max (peakFrame, seg.begin + std::min<std::size_t> (4, len / 4)), seg.end - 1);
    while (s0 < settleLimit && (! c.frames[s0].voiced || std::abs (c.frames[s0].midi - centre) > 0.35))
        ++s0;
    std::vector<double> sustainLevels;
    for (auto i = s0; i < seg.end; ++i)
        sustainLevels.push_back (c.frames[i].levelDb);
    const double sustainLevel = median (sustainLevels);
    auto s1 = seg.end - 1;
    while (s1 > s0 + 1 && (! c.frames[s1].voiced || std::abs (c.frames[s1].midi - centre) > 0.35 || c.frames[s1].levelDb < sustainLevel - 12.0))
        --s1;
    note.sustain = { std::max (note.attackStart, toSample (s0)), toSample (s1 + 1) };

    // Release: to the next note (legato) or until the tail falls into silence (<= 400 ms).
    if (seg.legatoOut)
        note.sourceEnd = toSample (seg.end);
    else
    {
        auto k = seg.end;
        const auto limit = std::min (c.frames.size(), seg.end + static_cast<std::size_t> (0.4 * fps));
        const auto nextLimit = next != nullptr ? next->begin : c.frames.size();
        float tailPeak = -120.0f;
        for (auto i = seg.begin; i < seg.end; ++i)
            tailPeak = std::max (tailPeak, c.frames[i].levelDb);
        const float tailFloor = std::max (pre.silenceThresholdDb, pre.noiseFloorDb + 3.0f);
        while (k < limit && k < nextLimit && c.frames[k].levelDb > tailFloor)
        {
            // Rising again after the tail has fallen well down: breath or the next sound.
            if (k + 1 < c.frames.size() && c.frames[k].levelDb < tailPeak - 12.0f && c.frames[k + 1].levelDb > c.frames[k].levelDb + 1.5f)
                break;
            ++k;
        }
        note.sourceEnd = toSample (k);
    }
    note.sourceEnd = std::max (note.sourceEnd, note.sustain.end);
    note.releaseStart = note.sustain.end;
    note.sustain.start = std::min (note.sustain.start, note.sustain.end);
    note.fadeOutFrames = std::clamp<std::int64_t> ((note.sourceEnd - note.releaseStart) / 2, 32, static_cast<std::int64_t> (0.01 * pre.sampleRate));
    note.fadeOutFrames = std::min (note.fadeOutFrames, std::max<std::int64_t> (1, (note.sourceEnd - note.attackStart) / 2));
    note.fadeInFrames = std::min (note.fadeInFrames, std::max<std::int64_t> (1, (note.sourceEnd - note.attackStart) / 4));

    // Levels.
    float peak = -120.0f;
    double power = 0.0;
    int powerCount = 0;
    for (auto i = static_cast<std::size_t> (note.attackStart / hop); i < std::min (c.frames.size(), static_cast<std::size_t> (note.sourceEnd / hop) + 1); ++i)
        peak = std::max (peak, c.frames[i].levelDb);
    for (auto i = s0; i <= s1 && i < c.frames.size(); ++i)
    {
        power += dbToGain (c.frames[i].levelDb) * dbToGain (c.frames[i].levelDb);
        ++powerCount;
    }
    note.peakDb = peak;
    note.rmsDb = powerCount > 0 ? powerToDb (power / powerCount, -120.0) : -120.0;

    const double seconds = static_cast<double> (note.sourceEnd - note.attackStart) / pre.sampleRate;
    if (note.voicing < 0.5 || meanConf < 0.45)
        note.articulation = atlas::Articulation::noisy;
    else if (seconds < 0.25)
        note.articulation = atlas::Articulation::short_;
    else if (seg.legatoIn || seg.legatoOut)
        note.articulation = atlas::Articulation::legato;
    else
        note.articulation = atlas::Articulation::sustained;

    note.included = centre > 0.0 && note.voicing >= 0.3;
    return note;
}

atlas::NoteEvent analyseRange (const AudioData& audio, std::int64_t startFrame, std::int64_t endFrame, const AnalysisSettings& s)
{
    const auto frames = audio.numFrames();
    startFrame = std::clamp<std::int64_t> (startFrame, 0, std::max<std::int64_t> (0, frames - 1));
    endFrame = std::clamp<std::int64_t> (endFrame, startFrame + 1, frames);
    // Context either side lets the tracker settle; the note itself is exactly [start, end).
    const auto context = static_cast<std::int64_t> (0.05 * audio.sampleRate);
    const auto from = std::max<std::int64_t> (0, startFrame - context);
    const auto to = std::min (frames, endFrame + context);
    AudioData slice;
    slice.sampleRate = audio.sampleRate;
    for (const auto& ch : audio.channels)
        slice.channels.emplace_back (ch.begin() + from, ch.begin() + to);
    auto local = s;
    local.maxAnalysisSeconds = std::max (s.maxAnalysisSeconds, static_cast<double> (to - from) / audio.sampleRate + 1.0);

    const auto pre = preprocess (slice, local);
    PitchContour contour;
    makePitchTracker (local.backend)->track (pre, local, contour, nullptr, nullptr);
    const int hop = hopSamplesFor (local, audio.sampleRate);
    Segment seg;
    seg.begin = static_cast<std::size_t> ((startFrame - from) / hop);
    seg.end = std::max (seg.begin + 1, std::min (contour.frames.size(), static_cast<std::size_t> ((endFrame - from + hop - 1) / hop)));
    // The range is given by the user: its edges are joins, not onsets to search around.
    seg.legatoIn = seg.legatoOut = true;
    auto note = describe (seg, nullptr, nullptr, contour, pre, local);
    auto shift = [&] (std::int64_t& f) { f = std::clamp<std::int64_t> (f + from, startFrame, endFrame); };
    shift (note.sourceStart);
    shift (note.attackStart);
    shift (note.sustain.start);
    shift (note.sustain.end);
    shift (note.releaseStart);
    shift (note.sourceEnd);
    note.sourceStart = note.attackStart = startFrame;
    note.sourceEnd = endFrame;
    note.releaseStart = std::clamp (note.releaseStart, note.sustain.end, endFrame);
    note.origin = atlas::NoteOrigin::analysis;
    note.analysisMethod = toString (local.backend);
    note.analysisVersion = analysisVersion * 100 + 1;
    const auto r = atlas::roundPitch (note.midiPitch);
    note.rootMidi = r.key;
    note.centsOffset = r.cents;
    return note;
}

namespace
{
    /** OSP Phase 4: what describe() was unsure about, from the note's own frames and its joins. */
    atlas::Diagnostics diagnose (const atlas::NoteEvent& note, const Segment& seg, const atlas::NoteEvent* previous, const atlas::NoteEvent* next,
                                 const PitchContour& c, double sampleRate)
    {
        atlas::Diagnostics d;
        std::size_t voiced = 0, octaveAway = 0;
        for (auto i = seg.begin; i < seg.end && i < c.frames.size(); ++i)
            if (c.frames[i].voiced)
            {
                ++voiced;
                octaveAway += std::abs (std::abs (c.frames[i].midi - note.midiPitch) - 12.0) < 1.0 ? 1 : 0;
            }
        d.unvoiced = note.voicing < 0.5 || note.articulation == atlas::Articulation::noisy;
        d.possibleOctaveError = voiced > 0 && static_cast<double> (octaveAway) / static_cast<double> (voiced) > 0.2;
        const double residualSpread = std::max (0.0, note.pitchStabilityCents - 0.7 * note.vibratoDepthCents);
        d.uncertainPitch = note.confidence < 0.5 || residualSpread > 40.0 || std::abs (note.centsOffset) > 42.0;
        const double seconds = static_cast<double> (note.sourceEnd - note.attackStart) / sampleRate;
        auto closeJoin = [&] (const atlas::NoteEvent* other, bool joined) {
            return other != nullptr && joined && std::abs (other->midiPitch - note.midiPitch) < 1.0;
        };
        d.uncertainBoundary = seconds < 0.08 || closeJoin (previous, seg.legatoIn) || closeJoin (next, seg.legatoOut);
        // A dip the segmentation kept inside the note (no sharp rise, no breath) may still have
        // been a legato repeat: NOTE MAP offers the split there.
        const auto hop = static_cast<std::int64_t> (std::lround (c.hopSeconds * sampleRate));
        for (auto f : seg.possibleRepeats)
        {
            const auto frame = static_cast<std::int64_t> (f) * hop;
            if (frame > note.attackStart && frame < note.sourceEnd)
                d.repeatFrames.push_back (frame);
        }
        d.possibleRepeat = ! d.repeatFrames.empty();
        d.possibleSplit = (previous != nullptr && seg.legatoIn && previous->rootMidi == note.rootMidi)
                          || (next != nullptr && seg.legatoOut && next->rootMidi == note.rootMidi);
        // A short entry that slides legato into a longer, nearby note is the shape of a scoop or
        // an ornament (a real sung A4 entered from G4): it may be the start of that note.
        if (next != nullptr && seg.legatoOut && ! seg.legatoIn && std::abs (next->midiPitch - note.midiPitch) <= 3.0)
        {
            const double nextSeconds = static_cast<double> (next->sourceEnd - next->attackStart) / sampleRate;
            d.possibleSplit = d.possibleSplit || (seconds < 0.25 && seconds < 0.5 * nextSeconds);
        }
        return d;
    }

    /** OSP Phase 4: a note tracked on its 2nd-5th harmonic (a weak fundamental under a
        strong formant) leaves a spectral peak at f/2 .. f/5; a real high note has nothing
        there. Hann-weighted Goertzel over the note's middle (at most 0.5 s). */
    int subharmonicPeak (const atlas::NoteEvent& note, const Preprocessed& pre)
    {
        const double f = note.pitchHz > 0.0 ? note.pitchHz : midiToHz (note.midiPitch);
        auto begin = note.sustain.length() > 0 ? note.sustain.start : note.attackStart;
        auto end = note.sustain.length() > 0 ? note.sustain.end : note.sourceEnd;
        end = std::min ({ end, begin + static_cast<std::int64_t> (0.5 * pre.sampleRate), pre.analysedFrames });
        begin = std::max<std::int64_t> (0, begin);
        const auto n = end - begin;
        if (f <= 0.0 || n < static_cast<std::int64_t> (0.08 * pre.sampleRate))
            return 0;
        auto power = [&] (double hz) {
            const double w = 2.0 * std::numbers::pi * hz / pre.sampleRate, c = 2.0 * std::cos (w);
            double s1 = 0.0, s2 = 0.0;
            for (std::int64_t i = 0; i < n; ++i)
            {
                const double hann = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i) / static_cast<double> (n - 1));
                const double s0 = hann * pre.mono[static_cast<std::size_t> (begin + i)] + c * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            return 10.0 * std::log10 (std::max (1.0e-30, s1 * s1 + s2 * s2 - c * s1 * s2));
        };
        const double atPitch = power (f);
        for (int m : { 2, 3, 4, 5 })
        {
            const double sub = f / m;
            if (sub < 35.0)
                continue;
            const double peak = power (sub), around = 0.5 * (power (sub * 0.85) + power (sub * 1.15));
            if (peak > atPitch - 35.0 && peak > around + 15.0)
                return m;
        }
        return 0;
    }

    /** OSP Phase 4: a note whose tracked pitch is its m-th harmonic (subharmonicPeak found the
        fundamental's peak) and whose waveform repeats far better at m times the tracked period
        (YIN's dip there, median over the note, under a quarter of the tracked period's, which
        is itself poor) is relabelled to the fundamental. Both kinds of evidence over the whole
        note are needed: a join or a breathy onset alone never moves a note. */
    void repairHarmonicNotes (std::vector<atlas::NoteEvent>& notes, const Preprocessed& pre, const AnalysisSettings& s)
    {
        YinPitchTracker yin (pre.sampleRate, s.minHz, s.maxHz, 0.15);
        for (auto& note : notes)
        {
            const int m = subharmonicPeak (note, pre);
            if (m == 0)
                continue;
            const double f = note.pitchHz > 0.0 ? note.pitchHz : midiToHz (note.midiPitch);
            const double lag = pre.sampleRate / f;
            if (lag * m > yin.maxLagValue() - 2)
                continue;
            std::vector<double> here, there;
            const auto step = static_cast<std::int64_t> (0.02 * pre.sampleRate);
            const auto begin = note.sustain.length() > 0 ? note.sustain.start : note.attackStart;
            const auto end = std::min (note.sustain.length() > 0 ? note.sustain.end : note.sourceEnd, pre.analysedFrames);
            for (auto centre = begin; centre < end; centre += step)
            {
                yin.analyse (pre.mono.data(), pre.analysedFrames, centre);
                if (yin.lastFrameSilent())
                    continue;
                const auto& cmnd = yin.lastCmnd();
                auto dipNear = [&] (double l) {
                    double best = 1.0;
                    for (auto t = static_cast<int> (l * 0.97); t <= static_cast<int> (l * 1.03) + 1; ++t)
                        if (t >= yin.minLagValue() && t <= yin.maxLagValue())
                            best = std::min (best, cmnd[static_cast<std::size_t> (t)]);
                    return best;
                };
                here.push_back (dipNear (lag));
                there.push_back (dipNear (lag * m));
            }
            if (here.size() < 3)
                continue;
            auto median = [] (std::vector<double> v) {
                std::nth_element (v.begin(), v.begin() + static_cast<std::ptrdiff_t> (v.size() / 2), v.end());
                return v[v.size() / 2];
            };
            const double h = median (here), t = median (there);
            if (h > 0.1 && t < 0.25 * h)
            {
                note.pitchHz = f / m;
                note.midiPitch -= 12.0 * std::log2 (static_cast<double> (m));
            }
        }
    }

    /** OSP Phase 4: how much of the recording had a confident pitch, what no note covers, and
        whether a monophonic analysis suits it at all (a heuristic, reported, never hidden). */
    void summarise (atlas::NoteAtlas& a, const PitchContour& c, const Preprocessed& pre, double sampleRate, int hop)
    {
        auto& info = a.analysis;
        info.analysedSeconds = static_cast<double> (pre.analysedFrames) / sampleRate;
        std::size_t loud = 0, pitched = 0;
        std::vector<bool> covered (c.frames.size(), false);
        for (const auto& n : a.notes)
            if (n.included)
                for (auto f = static_cast<std::size_t> (std::max<std::int64_t> (0, n.attackStart / hop));
                     f < c.frames.size() && static_cast<std::int64_t> (f) * hop < n.sourceEnd; ++f)
                    covered[f] = true;
        const auto minRun = static_cast<std::size_t> (0.1 / c.hopSeconds);
        std::size_t runStart = 0, run = 0;
        auto closeRun = [&] (std::size_t end) {
            if (run >= minRun)
                info.uncovered.push_back ({ static_cast<std::int64_t> (runStart) * hop, static_cast<std::int64_t> (end) * hop });
            run = 0;
        };
        for (std::size_t i = 0; i < c.frames.size(); ++i)
        {
            const auto& f = c.frames[i];
            const bool isLoud = f.levelDb > pre.silenceThresholdDb + 10.0f;
            const bool isPitched = f.voiced && f.confidence >= 0.6;
            loud += isLoud ? 1 : 0;
            pitched += isLoud && isPitched ? 1 : 0;
            if (isLoud && isPitched && ! covered[i])
            {
                if (run == 0)
                    runStart = i;
                ++run;
            }
            else
                closeRun (i);
        }
        closeRun (c.frames.size());
        info.pitchedSeconds = static_cast<double> (pitched) * c.hopSeconds;
        const double share = loud > 0 ? static_cast<double> (pitched) / static_cast<double> (loud) : 0.0;
        info.suitability = loud == 0 ? "silent" : (share >= 0.6 ? "monophonic" : (share >= 0.35 ? "uncertain" : "unsuitable"));
    }
}

AnalysisResult analysePhrase (const AudioData& audio, const atlas::SourceAsset& sourceIn, const AnalysisSettings& s,
                              const std::atomic<bool>* cancel, std::atomic<float>* progress)
{
    const auto started = std::chrono::steady_clock::now();
    AnalysisResult result;
    auto source = sourceIn;
    if (source.assetId.empty())
        source.assetId = "src0";
    source.sampleRate = audio.sampleRate;
    source.numChannels = std::min (audio.numChannels(), 2);
    source.numFrames = audio.numFrames();

    auto& a = result.atlas;
    a.sources.push_back (source);
    a.analysis.method = toString (s.backend);
    a.analysis.version = analysisVersion;
    a.analysis.a4Hz = s.a4Hz;
    a.analysis.settingsDigest = s.digest();
    a.atlasId = source.contentHash.empty() ? std::string ("atlas") : "atlas:" + source.contentHash.substr (std::min<std::size_t> (7, source.contentHash.size()), 16);

    if (audio.isEmpty())
    {
        result.ok = true;
        result.warnings.push_back ("the recording is empty: nothing to analyse");
        return result;
    }
    if (audio.durationSeconds() > s.maxAnalysisSeconds)
        result.warnings.push_back ("only the first " + std::to_string (static_cast<int> (s.maxAnalysisSeconds)) + " s were analysed");

    const auto pre = preprocess (audio, s);
    auto tracker = makePitchTracker (s.backend);
    if (! tracker->track (pre, s, result.contour, cancel, progress))
    {
        result.cancelled = true;
        result.error = "analysis cancelled";
        return result;
    }
    result.levelDb = pre.levelDb;

    auto segments = segment (result.contour, pre, s);
    const auto describeAll = [&]
    {
        a.notes.clear();
        for (std::size_t i = 0; i < segments.size(); ++i)
        {
            auto note = describe (segments[i], i > 0 ? &segments[i - 1] : nullptr, i + 1 < segments.size() ? &segments[i + 1] : nullptr,
                                  result.contour, pre, s);
            note.sourceId = source.assetId;
            note.origin = atlas::NoteOrigin::analysis;
            note.analysisMethod = tracker->name();
            note.analysisVersion = analysisVersion * 100 + tracker->version();
            note.id = a.nextNoteId();
            a.notes.push_back (std::move (note));
        }
    };
    describeAll();
    // The doubts need the rounded keys of the neighbours: after remapping (below), so roots are final.

    std::vector<double> tracked;
    for (const auto& n : a.notes)
        tracked.push_back (n.midiPitch);
    repairHarmonicNotes (a.notes, pre, s);
    // OSP Phase 4B: two joined pieces cut where the pitch moved, or that the repair put back on
    // one pitch, but that are described at the same pitch were one note: the tracker followed a
    // harmonic for a while (a weak fundamental: bowed harmonics, a low string), or the move was
    // only an excursion. They become one, described again.
    {
        const auto bridge = static_cast<std::size_t> (std::lround (s.bridgeGapSeconds / result.contour.hopSeconds));
        std::vector<Segment> joined;
        for (std::size_t i = 0; i < segments.size(); ++i)
        {
            const auto repaired = [&] (std::size_t k) { return std::abs (a.notes[k].midiPitch - tracked[k]) > 6.0; };
            const bool cut = segments[i].pitchCut && s.detection != Detection::more;
            if (i > 0 && segments[i].begin <= joined.back().end + (cut ? bridge : 0) && std::abs (a.notes[i].midiPitch - a.notes[i - 1].midiPitch) < 0.5
                && atlas::roundPitch (a.notes[i].midiPitch).key == atlas::roundPitch (a.notes[i - 1].midiPitch).key
                && (cut || repaired (i) || repaired (i - 1)))
            {
                // A small move between two takes of one key (not a harmonic the tracker jumped to)
                // may still have been two sung or played notes: offered as a split.
                if (std::abs (tracked[i] - tracked[i - 1]) < 6.0)
                    joined.back().possibleRepeats.push_back (segments[i].begin);
                joined.back().end = segments[i].end;
                joined.back().legatoOut = segments[i].legatoOut;
                joined.back().possibleRepeats.insert (joined.back().possibleRepeats.end(), segments[i].possibleRepeats.begin(),
                                                      segments[i].possibleRepeats.end());
            }
            else
                joined.push_back (segments[i]);
        }
        if (joined.size() < segments.size())
        {
            segments = std::move (joined);
            describeAll();
            repairHarmonicNotes (a.notes, pre, s);
        }
    }

    // --- 6: mapping ---
    if (s.autoTuning)
    {
        std::vector<std::pair<double, double>> residuals;
        for (const auto& n : a.notes)
            if (n.included && n.confidence >= 0.5)
                residuals.emplace_back ((n.midiPitch - std::round (n.midiPitch)) * 100.0, n.confidence);
        if (residuals.size() >= 3)
            a.analysis.tuningOffsetCents = weightedMedian (residuals);
    }
    atlas::remapNotes (a);
    atlas::repair (a);
    for (std::size_t i = 0; i < a.notes.size() && i < segments.size(); ++i)
        a.notes[i].diagnostics = diagnose (a.notes[i], segments[i], i > 0 ? &a.notes[i - 1] : nullptr,
                                           i + 1 < a.notes.size() ? &a.notes[i + 1] : nullptr, result.contour, audio.sampleRate);
    for (auto& n : a.notes)
        n.diagnostics.possibleOctaveError = n.diagnostics.possibleOctaveError || subharmonicPeak (n, pre) != 0;
    // A decaying tail or a breath that became its own region on a key that already has a full take
    // would play as a near-silent round robin. It stays in the atlas, off the keyboard, and says why.
    std::map<std::string, float> loudestOfKey;
    for (const auto& n : a.notes)
        if (n.included)
            loudestOfKey[atlas::groupOf (n)] = std::max (loudestOfKey.count (atlas::groupOf (n)) != 0 ? loudestOfKey[atlas::groupOf (n)] : -120.0f,
                                                         static_cast<float> (n.peakDb));
    int quietAlternates = 0;
    for (auto& n : a.notes)
        if (n.included && n.peakDb < loudestOfKey[atlas::groupOf (n)] - quietAlternateDb)
        {
            n.included = false;
            n.diagnostics.quietAlternate = true;
            ++quietAlternates;
        }
    if (quietAlternates > 0)
        result.warnings.push_back (std::to_string (quietAlternates) + " note(s) more than " + std::to_string (static_cast<int> (quietAlternateDb))
                                   + " dB quieter than another take of their key were left off the keyboard (kept in the atlas)");
    summarise (a, result.contour, pre, audio.sampleRate, hopSamplesFor (s, audio.sampleRate));
    if (a.analysis.suitability == "unsuitable" && ! a.notes.empty())
    {
        // No note of a recording judged unsuitable may look reliable.
        for (auto& n : a.notes)
            n.diagnostics.uncertainPitch = true;
        result.warnings.push_back ("this recording does not look monophonic and pitched (polyphonic, noisy or unpitched): its notes are unreliable");
    }
    if (! a.analysis.uncovered.empty())
        result.warnings.push_back (std::to_string (a.analysis.uncovered.size()) + " stretch(es) of pitched sound are in no note: possible missed notes");

    if (a.notes.empty())
        result.warnings.push_back (pre.peakDb < static_cast<float> (s.absoluteFloorDb) ? "the recording is silent: no notes found"
                                                                                       : "no pitched notes found (noise, or no stable pitch)");
    result.ok = true;
    result.analysisSeconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
    if (progress != nullptr)
        progress->store (1.0f, std::memory_order_relaxed);
    return result;
}

} // namespace osp::phrase
