#include "drums/BreakSlicer.h"

#include "core/Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>

namespace osp::drums
{

namespace
{
    constexpr double pi = 3.14159265358979323846;
    // Bands: kick, body (toms, snare body), snare/clap crack, hats and cymbals.
    constexpr double bandEdgesHz[] = { 30.0, 150.0, 800.0, 4000.0, 18000.0 };
    constexpr int bands = 4;

    std::vector<float> mono (const AudioData& audio)
    {
        const auto frames = static_cast<std::size_t> (audio.numFrames());
        std::vector<float> m (frames, 0.0f);
        const auto channels = audio.numChannels();
        if (channels == 0)
            return m;
        for (const auto& c : audio.channels)
            for (std::size_t i = 0; i < frames; ++i)
                m[i] += c[i] / static_cast<float> (channels);
        return m;
    }

    float percentile (std::vector<float> v, double p)
    {
        if (v.empty())
            return 0.0f;
        const auto k = std::min (v.size() - 1, static_cast<std::size_t> (std::floor (p * static_cast<double> (v.size() - 1))));
        std::nth_element (v.begin(), v.begin() + static_cast<std::ptrdiff_t> (k), v.end());
        return v[k];
    }

    struct Frames
    {
        int size = 1024, hop = 256;
    };

    Frames framesFor (double sampleRate)
    {
        Frames f;
        f.size = 1 << Fft::orderForSize (static_cast<int> (std::lround (0.021 * sampleRate)));   // ~21 ms
        f.hop = std::max (1, f.size / 4);
        return f;
    }

    struct Attack
    {
        std::int64_t start = -1;   ///< frames (-1: none)
        float rise = 0.0f;         ///< the steepest 3 ms rise, dB
    };

    /** 1 ms energy in dB, plain or pre-emphasised (first difference: transients stand out
        over the low tails they ride on). */
    struct AttackEnvelope
    {
        int block = 48;
        std::vector<float> db;

        AttackEnvelope (const std::vector<float>& x, double sampleRate, bool preEmphasis)
        {
            block = std::max (8, static_cast<int> (std::lround (0.001 * sampleRate)));
            const auto n = x.size() / static_cast<std::size_t> (block);
            db.resize (n);
            for (std::size_t b = 0; b < n; ++b)
            {
                double sum = 0.0;
                for (std::size_t i = b * static_cast<std::size_t> (block); i < (b + 1) * static_cast<std::size_t> (block); ++i)
                {
                    const double d = preEmphasis ? x[i] - (i > 0 ? 0.97 * x[i - 1] : 0.0) : x[i];
                    sum += d * d;
                }
                db[b] = static_cast<float> (10.0 * std::log10 (sum / block + 1.0e-14));
            }
            // Nothing below the recording's own floor: in digital silence or a bit-crushed
            // noise floor, single quantisation steps would look like huge attacks.
            const float floor = std::max (-100.0f, percentile (db, 0.2));
            for (auto& v : db)
                v = std::max (v, floor);
        }

        /** The steepest rise over 3 ms in [from, to) (frames), and where its attack begins:
            from the valley before it, the first block 30 % (in dB) on the way up. */
        Attack attack (std::int64_t from, std::int64_t to) const
        {
            Attack a;
            const auto b0 = std::clamp<std::int64_t> (from / block, 0, static_cast<std::int64_t> (db.size()));
            const auto b1 = std::clamp<std::int64_t> (to / block, 0, static_cast<std::int64_t> (db.size()));
            if (b1 - b0 < 4)
                return a;
            std::int64_t best = -1;
            for (auto b = b0 + 3; b < b1; ++b)
            {
                const float rise = db[static_cast<std::size_t> (b)] - db[static_cast<std::size_t> (b - 3)];
                if (best < 0 || rise > a.rise)
                {
                    a.rise = rise;
                    best = b;
                }
            }
            auto valley = best - 3;
            for (auto b = best - 3; b >= std::max (b0, best - 7); --b)
                if (db[static_cast<std::size_t> (b)] <= db[static_cast<std::size_t> (valley)])
                    valley = b;
            const float lo = db[static_cast<std::size_t> (valley)];
            const float hi = db[static_cast<std::size_t> (best)];
            auto start = valley;
            while (start < best && db[static_cast<std::size_t> (start)] < lo + 0.3f * (hi - lo))
                ++start;
            a.start = std::max<std::int64_t> (0, start - 1) * block;
            return a;
        }
    };

    /** The clearer of the two views of an attack: a dull or bit-crushed hit shows in the
        plain energy, a hat over a kick's tail in the pre-emphasised one. */
    struct Attacks
    {
        AttackEnvelope plain, bright;
        Attacks (const std::vector<float>& x, double sampleRate) : plain (x, sampleRate, false), bright (x, sampleRate, true) {}
        Attack find (std::int64_t from, std::int64_t to) const
        {
            const auto a = plain.attack (from, to);
            const auto b = bright.attack (from, to);
            return b.rise > a.rise ? b : a;
        }
    };

    std::int64_t decayEnd (const std::vector<float>& x, std::int64_t start, std::int64_t limit, double sampleRate, double maxSeconds)
    {
        const auto n = static_cast<std::int64_t> (x.size());
        const auto stop = std::min ({ limit, n, start + static_cast<std::int64_t> (maxSeconds * sampleRate) });
        const auto win = std::max<std::int64_t> (1, static_cast<std::int64_t> (0.01 * sampleRate));
        float peak = -200.0f;
        int quiet = 0;
        for (auto p = start; p + win <= stop; p += win)
        {
            double sum = 0.0;
            for (auto i = p; i < p + win; ++i)
                sum += static_cast<double> (x[static_cast<std::size_t> (i)]) * x[static_cast<std::size_t> (i)];
            const auto d = static_cast<float> (10.0 * std::log10 (sum / static_cast<double> (win) + 1.0e-14));
            peak = std::max (peak, d);
            quiet = d < peak - 50.0f || d < -90.0f ? quiet + 1 : 0;
            if (quiet >= 3)
                return p + win;
        }
        return stop;
    }
} // namespace

OnsetCurve onsetCurve (const AudioData& audio)
{
    OnsetCurve curve;
    const auto x = mono (audio);
    const double sr = audio.sampleRate;
    if (x.empty() || sr <= 0.0)
        return curve;
    const auto f = framesFor (sr);
    curve.hopSeconds = f.hop / sr;
    const Fft fft (Fft::orderForSize (f.size));
    std::vector<double> window (static_cast<std::size_t> (f.size));
    for (int i = 0; i < f.size; ++i)
        window[static_cast<std::size_t> (i)] = 0.5 - 0.5 * std::cos (2.0 * pi * i / f.size);
    const int bins = f.size / 2;
    std::array<int, bands + 1> edge {};
    for (int b = 0; b <= bands; ++b)
        edge[static_cast<std::size_t> (b)] = std::clamp (static_cast<int> (std::lround (bandEdgesHz[b] * f.size / sr)), 1, bins);

    const auto frames = static_cast<int> (x.size() / static_cast<std::size_t> (f.hop)) + 1;
    std::vector<std::complex<double>> buffer (static_cast<std::size_t> (f.size));
    // Lag 2: a hit enters the window over a few hops, so two hops see most of its rise
    // (steady noise changes no more over two hops than over one).
    std::vector<float> older (static_cast<std::size_t> (bins), 0.0f), previous (static_cast<std::size_t> (bins), 0.0f),
        current (static_cast<std::size_t> (bins));
    std::array<std::vector<float>, bands> flux;
    for (auto& b : flux)
        b.assign (static_cast<std::size_t> (frames), 0.0f);
    for (int t = 0; t < frames; ++t)
    {
        // The window ends at the hop: a hit shows up as soon as it enters.
        const std::int64_t end = static_cast<std::int64_t> (t) * f.hop;
        for (int i = 0; i < f.size; ++i)
        {
            const auto at = end - f.size + i;
            const double v = at >= 0 && at < static_cast<std::int64_t> (x.size()) ? x[static_cast<std::size_t> (at)] : 0.0;
            buffer[static_cast<std::size_t> (i)] = { v * window[static_cast<std::size_t> (i)], 0.0 };
        }
        fft.forward (buffer.data());
        for (int k = 0; k < bins; ++k)
            current[static_cast<std::size_t> (k)] = static_cast<float> (std::log1p (10.0 * std::abs (buffer[static_cast<std::size_t> (k)])));
        // Positive change against the previous frame's local maximum (vibrato and slow
        // movement do not count as attacks).
        for (int b = 0; b < bands; ++b)
        {
            double sum = 0.0;
            for (int k = edge[static_cast<std::size_t> (b)]; k < edge[static_cast<std::size_t> (b + 1)]; ++k)
            {
                const float prev = std::max ({ older[static_cast<std::size_t> (std::max (0, k - 1))], older[static_cast<std::size_t> (k)],
                                               older[static_cast<std::size_t> (std::min (bins - 1, k + 1))] });
                sum += std::max (0.0f, current[static_cast<std::size_t> (k)] - prev);
            }
            flux[static_cast<std::size_t> (b)][static_cast<std::size_t> (t)] = static_cast<float> (sum / std::max (1, edge[static_cast<std::size_t> (b + 1)] - edge[static_cast<std::size_t> (b)]));
        }
        std::swap (older, previous);
        std::swap (previous, current);
    }
    // Each band above its own running median (a cymbal's wash or a noise floor is not an
    // attack), then against its own strong hits; then the strongest band (any one
    // instrument is enough) blended with the mean (several at once is stronger evidence).
    const int half = std::max (2, static_cast<int> (std::lround (0.1 * sr / f.hop)));
    std::vector<float> local;
    for (auto& band : flux)
    {
        std::vector<float> excess (band.size());
        for (int t = 0; t < frames; ++t)
        {
            local.assign (band.begin() + std::max (0, t - half), band.begin() + std::min (frames, t + half + 1));
            excess[static_cast<std::size_t> (t)] = std::max (0.0f, band[static_cast<std::size_t> (t)] - percentile (local, 0.5));
        }
        band = std::move (excess);
    }
    std::array<float, bands> scale {};
    for (int b = 0; b < bands; ++b)
        scale[static_cast<std::size_t> (b)] = std::max (1.0e-6f, percentile (flux[static_cast<std::size_t> (b)], 0.995));
    curve.value.assign (static_cast<std::size_t> (frames), 0.0f);
    for (int t = 0; t < frames; ++t)
    {
        float mx = 0.0f, sum = 0.0f;
        for (int b = 0; b < bands; ++b)
        {
            const float v = flux[static_cast<std::size_t> (b)][static_cast<std::size_t> (t)] / scale[static_cast<std::size_t> (b)];
            mx = std::max (mx, v);
            sum += v;
        }
        curve.value[static_cast<std::size_t> (t)] = 0.7f * mx + 0.3f * sum / bands;
    }
    return curve;
}

std::vector<Onset> detectBreakOnsets (const AudioData& audio, const SliceSettings& settings)
{
    std::vector<Onset> out;
    const auto curve = onsetCurve (audio);
    const double sr = audio.sampleRate;
    if (curve.value.empty())
        return out;
    const auto& c = curve.value;
    const auto n = static_cast<int> (c.size());
    const auto hop = curve.hopSeconds * sr;
    const double s = std::clamp (settings.sensitivity, 0.0, 1.0);
    const int half = std::max (2, static_cast<int> (std::lround (0.12 / curve.hopSeconds)));
    const float delta = static_cast<float> (0.20 - 0.17 * s);
    const float floor = static_cast<float> (0.07 - 0.05 * s);
    const int minGap = std::max (1, static_cast<int> (std::lround (settings.minSeparationSeconds / curve.hopSeconds)));

    struct Peak
    {
        int t;
        float v;
    };
    std::vector<Peak> peaks;
    std::vector<float> local;
    for (int t = 1; t < n - 1; ++t)
    {
        const float v = c[static_cast<std::size_t> (t)];
        if (v < floor || v < c[static_cast<std::size_t> (t - 1)] || v < c[static_cast<std::size_t> (t + 1)])
            continue;
        if (t >= 2 && v < c[static_cast<std::size_t> (t - 2)])
            continue;
        local.assign (c.begin() + std::max (0, t - half), c.begin() + std::min (n, t + half + 1));
        const float threshold = percentile (local, 0.5) + delta;
        if (v < threshold)
            continue;
        if (! peaks.empty() && t - peaks.back().t < minGap)
        {
            if (v > peaks.back().v)
                peaks.back() = { t, v };
            continue;
        }
        peaks.push_back ({ t, v });
    }

    // Align each to its attack's start in the waveform.
    const auto x = mono (audio);
    const Attacks attacks (x, sr);
    std::int64_t last = -1;
    for (const auto& p : peaks)
    {
        const auto end = static_cast<std::int64_t> (p.t * hop);   // the frame's window ends here
        const auto size = static_cast<std::int64_t> (framesFor (sr).size);
        auto from = std::max<std::int64_t> (last + 1, end - size - static_cast<std::int64_t> (0.005 * sr));
        const auto found = attacks.find (from, end + static_cast<std::int64_t> (0.004 * sr));
        // A real attack rises out of what was sounding (steady noise and washes do not).
        if (found.start < 0 || found.rise < static_cast<float> (2.5 + 3.0 * (1.0 - s)))
            continue;
        const auto start = found.start;
        if (last >= 0 && start - last < static_cast<std::int64_t> (settings.minSeparationSeconds * sr * 0.5))
            continue;
        out.push_back ({ start, std::clamp (static_cast<double> (p.v), 0.0, 1.0) });
        last = start;
    }
    return out;
}

std::vector<BreakSlice> sliceBreak (const AudioData& audio, const SliceSettings& settings)
{
    std::vector<BreakSlice> slices;
    const auto onsets = detectBreakOnsets (audio, settings);
    const auto x = mono (audio);
    const auto total = static_cast<std::int64_t> (x.size());
    const auto pre = static_cast<std::int64_t> (std::lround (settings.preRollSeconds * audio.sampleRate));
    const auto maxLength = static_cast<std::int64_t> (settings.maxSliceSeconds * audio.sampleRate);
    for (std::size_t i = 0; i < onsets.size(); ++i)
    {
        BreakSlice s;
        s.onset = onsets[i].frame;
        s.start = std::max<std::int64_t> (0, s.onset - pre);
        if (! slices.empty())
            s.start = std::max (s.start, slices.back().start + 1);
        slices.push_back (s);
    }
    for (std::size_t i = 0; i < slices.size(); ++i)
    {
        auto& s = slices[i];
        s.end = i + 1 < slices.size() ? slices[i + 1].start : decayEnd (x, s.start, total, audio.sampleRate, settings.maxSliceSeconds);
        s.end = std::min (s.end, s.start + maxLength);
    }
    // Strength: each hit's attack level against the loudest (a velocity for the groove).
    std::vector<double> level (slices.size(), -200.0);
    double loudest = -200.0;
    for (std::size_t i = 0; i < slices.size(); ++i)
    {
        float peak = 0.0f;
        const auto to = std::min (slices[i].end, slices[i].start + static_cast<std::int64_t> (0.03 * audio.sampleRate));
        for (auto k = slices[i].start; k < to; ++k)
            peak = std::max (peak, std::abs (x[static_cast<std::size_t> (k)]));
        level[i] = 20.0 * std::log10 (peak + 1.0e-9);
        loudest = std::max (loudest, level[i]);
    }
    for (std::size_t i = 0; i < slices.size(); ++i)
        slices[i].strength = std::clamp (1.0 + (level[i] - loudest) / 36.0, 0.05, 1.0);
    return slices;
}

int addSliceAt (std::vector<BreakSlice>& slices, const AudioData& audio, std::int64_t frame, const SliceSettings& settings)
{
    const auto x = mono (audio);
    const auto total = static_cast<std::int64_t> (x.size());
    if (frame < 0 || frame >= total)
        return -1;
    const double sr = audio.sampleRate;
    const Attacks attacks (x, sr);
    // A click near an attack lands on the attack's start.
    const auto near = attacks.find (frame - static_cast<std::int64_t> (0.015 * sr), frame + static_cast<std::int64_t> (0.015 * sr));
    const auto onset = near.start >= 0 && near.rise >= 6.0f ? near.start : frame;
    const auto start = std::max<std::int64_t> (0, onset - static_cast<std::int64_t> (std::lround (settings.preRollSeconds * sr)));
    const auto minDistance = static_cast<std::int64_t> (0.005 * sr);
    for (const auto& s : slices)
        if (std::abs (s.start - start) < minDistance)
            return -1;
    BreakSlice added;
    added.start = start;
    added.onset = onset;
    added.manual = true;
    added.strength = 0.5;
    const auto it = std::upper_bound (slices.begin(), slices.end(), start, [] (std::int64_t f, const BreakSlice& s) { return f < s.start; });
    const auto index = static_cast<int> (it - slices.begin());
    if (index > 0 && slices[static_cast<std::size_t> (index - 1)].end > start)
    {
        added.end = slices[static_cast<std::size_t> (index - 1)].end;
        slices[static_cast<std::size_t> (index - 1)].end = start;
    }
    else
        added.end = index < static_cast<int> (slices.size()) ? slices[static_cast<std::size_t> (index)].start
                                                              : decayEnd (x, start, total, sr, settings.maxSliceSeconds);
    slices.insert (slices.begin() + index, added);
    return index;
}

bool removeSlice (std::vector<BreakSlice>& slices, int index)
{
    if (index < 0 || index >= static_cast<int> (slices.size()))
        return false;
    const auto i = static_cast<std::size_t> (index);
    if (index > 0 && slices[i - 1].end == slices[i].start)
        slices[i - 1].end = slices[i].end;
    slices.erase (slices.begin() + index);
    return true;
}

bool moveSliceStart (std::vector<BreakSlice>& slices, int index, std::int64_t frame, std::int64_t totalFrames)
{
    if (index < 0 || index >= static_cast<int> (slices.size()))
        return false;
    const auto i = static_cast<std::size_t> (index);
    const std::int64_t minLength = 64;
    const auto lo = index > 0 ? slices[i - 1].start + minLength : 0;
    const auto hi = std::min (slices[i].end, totalFrames) - minLength;
    if (hi < lo)
        return false;
    const auto at = std::clamp (frame, lo, hi);
    if (index > 0 && slices[i - 1].end == slices[i].start)
        slices[i - 1].end = at;
    slices[i].onset += at - slices[i].start;
    slices[i].start = at;
    slices[i].manual = true;
    return true;
}

double estimateTempo (const std::vector<BreakSlice>& slices, double sampleRate)
{
    if (slices.size() < 4 || sampleRate <= 0.0)
        return 0.0;
    const double first = static_cast<double> (slices.front().onset) / sampleRate;
    const double length = static_cast<double> (slices.back().end) / sampleRate - first;
    double bestBpm = 0.0, bestScore = -1.0;
    for (double bpm = 70.0; bpm <= 180.0; bpm += 0.25)
    {
        // How well the onsets sit on an 8th-note grid (phase free), weighted by strength ...
        const double period = 30.0 / bpm;
        double re = 0.0, im = 0.0, weight = 0.0;
        for (const auto& s : slices)
        {
            const double t = static_cast<double> (s.onset) / sampleRate - first;
            re += s.strength * std::cos (2.0 * pi * t / period);
            im += s.strength * std::sin (2.0 * pi * t / period);
            weight += s.strength;
        }
        double score = std::sqrt (re * re + im * im) / std::max (1.0e-9, weight);
        // ... and whether the break is a whole number of bars at that tempo.
        const double bars = length * bpm / 60.0 / 4.0;
        if (bars >= 0.75)
            score *= 1.0 + 0.5 * std::max (0.0, 1.0 - 4.0 * std::abs (bars - std::round (bars)));
        if (score > bestScore + 1.0e-9)
        {
            bestScore = score;
            bestBpm = bpm;
        }
    }
    return bestBpm;
}

} // namespace osp::drums
