#include "MemoClassifier.h"

#include <algorithm>
#include <cmath>
#include <numeric>

// The library scan's rules (MemoClassifier.h). Everything is measured on a reduced copy of the
// recording (about 8 kHz: speech and hummed pitches live well below 4 kHz), 40 ms frames, then
// decided second by second and smoothed over three seconds.

namespace osp::memolab
{

namespace
{
    struct Frame
    {
        double db = -120.0;          ///< RMS, dBFS
        bool active = false;         ///< above the memo's sounding threshold
        bool voiced = false;         ///< one clear pitch (YIN)
        double aperiodicity = 1.0;   ///< YIN's normalised difference at the chosen lag (0: periodic)
        double hz = 0.0;             ///< the pitch when voiced
        double brightness = 0.0;     ///< first difference's energy / energy (0 dark .. 2 white)
        int run = 0;                 ///< length of the voiced run this frame is in (frames)
        bool steady = false;         ///< its pitch within 40 cents of a voiced neighbour's
        bool wandering = false;      ///< in a long run whose pitch moves (a voice, not a machine)
        bool falling = false;        ///< quieter than the frame before by more than 0.6 dB
    };

    constexpr int framesPerSecond = 25;   // 40 ms
    constexpr int longRun = 8;            // 0.32 s: longer than a spoken syllable

    double cents (double a, double b) { return 1200.0 * std::log2 (a / b); }

    double median (std::vector<double> v)
    {
        if (v.empty())
            return 0.0;
        const auto mid = v.begin() + static_cast<std::ptrdiff_t> (v.size() / 2);
        std::nth_element (v.begin(), mid, v.end());
        return *mid;
    }

    /** YIN on one frame: the period (samples, 0 when none) and its aperiodicity. */
    std::pair<double, double> yin (const float* x, int window, int minLag, int maxLag, std::vector<double>& d)
    {
        d.assign (static_cast<std::size_t> (maxLag + 1), 0.0);
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            double sum = 0.0;
            for (int i = 0; i < window; ++i)
            {
                const double diff = static_cast<double> (x[i]) - x[i + tau];
                sum += diff * diff;
            }
            d[static_cast<std::size_t> (tau)] = sum;
        }
        // Cumulative mean normalised difference.
        double running = 0.0;
        d[0] = 1.0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            running += d[static_cast<std::size_t> (tau)];
            d[static_cast<std::size_t> (tau)] = running > 0.0 ? d[static_cast<std::size_t> (tau)] * tau / running : 1.0;
        }
        int best = -1;
        for (int tau = minLag; tau <= maxLag; ++tau)
            if (d[static_cast<std::size_t> (tau)] < 0.15)
            {
                while (tau + 1 <= maxLag && d[static_cast<std::size_t> (tau + 1)] < d[static_cast<std::size_t> (tau)])
                    ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            best = minLag;
            for (int tau = minLag; tau <= maxLag; ++tau)
                if (d[static_cast<std::size_t> (tau)] < d[static_cast<std::size_t> (best)])
                    best = tau;
        }
        double period = best;
        if (best > minLag && best < maxLag)
        {
            const double a = d[static_cast<std::size_t> (best - 1)], b = d[static_cast<std::size_t> (best)], c = d[static_cast<std::size_t> (best + 1)];
            const double denom = a - 2.0 * b + c;
            if (std::abs (denom) > 1.0e-12)
                period += std::clamp (0.5 * (a - c) / denom, -0.5, 0.5);
        }
        return { period, d[static_cast<std::size_t> (best)] };
    }

    /** One second's class from its frames' features. */
    SoundClass classify (const std::vector<Frame>& frames, std::size_t begin, std::size_t end)
    {
        int active = 0, voiced = 0, steady = 0, inLong = 0, wandering = 0, falling = 0;
        double brightness = 0.0, aperiodicity = 0.0;
        std::vector<double> pitches;
        for (auto i = begin; i < end; ++i)
        {
            const auto& f = frames[i];
            if (! f.active)
                continue;
            ++active;
            brightness += f.brightness;
            aperiodicity += f.aperiodicity;
            falling += f.falling ? 1 : 0;
            if (! f.voiced)
                continue;
            ++voiced;
            steady += f.steady ? 1 : 0;
            pitches.push_back (f.hz);
            if (f.run >= longRun)
            {
                ++inLong;
                wandering += f.wandering ? 1 : 0;
            }
        }
        const auto n = static_cast<double> (end - begin);
        if (active < 0.25 * n)
            return SoundClass::quiet;
        const double activeShare = active / n;
        const double voicedShare = static_cast<double> (voiced) / active;
        brightness /= active;
        aperiodicity /= active;
        const double fallingShare = static_cast<double> (falling) / active;
        const double steadyShare = voiced > 0 ? static_cast<double> (steady) / voiced : 0.0;
        const double longShare = voiced > 0 ? static_cast<double> (inLong) / voiced : 0.0;
        const double wanderShare = inLong > 0 ? static_cast<double> (wandering) / inLong : 0.0;
        const double hz = median (pitches);

        // Held, steady pitches: a hum (dark, a voice's range, a voice's small wander, not
        // decaying like a struck string) or else a phrase (sung, played).
        if (voicedShare >= 0.55 && steadyShare >= 0.7 && longShare >= 0.6)
        {
            const bool voiceLike = hz >= 70.0 && hz <= 650.0 && wanderShare >= 0.5 && fallingShare < 0.45;
            return brightness < 0.15 && voiceLike ? SoundClass::hum : SoundClass::phrase;
        }
        // Speech: voiced in short runs with gaps between, the pitch gliding, a speaking voice's
        // range.
        if (voicedShare >= 0.25 && longShare < 0.45 && steadyShare < 0.6 && hz > 0.0 && hz < 400.0 && activeShare < 0.97)
            return SoundClass::talk;
        // Sounding all through, tonal, but rarely one clear pitch: several things at once
        // (strummed chords read about 0.5 aperiodic and 0.8-1.1 bright; white noise 1 and 2).
        if (activeShare >= 0.85 && aperiodicity < 0.65 && brightness < 1.3)
            return SoundClass::jam;
        if (voicedShare >= 0.4 && steadyShare >= 0.5 && longShare >= 0.4)
            return SoundClass::phrase;
        return SoundClass::noise;
    }

    bool musical (SoundClass c) { return c == SoundClass::hum || c == SoundClass::phrase || c == SoundClass::jam; }
}

const char* soundClassName (SoundClass c) noexcept
{
    switch (c)
    {
        case SoundClass::quiet:  return "Quiet";
        case SoundClass::talk:   return "Talk";
        case SoundClass::hum:    return "Hum";
        case SoundClass::phrase: return "Phrase";
        case SoundClass::jam:    return "Jam";
        case SoundClass::noise:  return "Noise";
    }
    return "Quiet";
}

const char* soundClassId (SoundClass c) noexcept
{
    switch (c)
    {
        case SoundClass::quiet:  return "quiet";
        case SoundClass::talk:   return "talk";
        case SoundClass::hum:    return "hum";
        case SoundClass::phrase: return "phrase";
        case SoundClass::jam:    return "jam";
        case SoundClass::noise:  return "noise";
    }
    return "quiet";
}

std::vector<SoundClass> MemoProfile::tags (float minShare) const
{
    std::vector<SoundClass> out;
    if (label == SoundClass::quiet)
        return { SoundClass::quiet };
    const float sounding = 1.0f - share[static_cast<std::size_t> (SoundClass::quiet)];
    if (sounding <= 0.0f)
        return { label };
    out.push_back (label);
    std::vector<int> others;
    for (int c = 0; c < soundClassCount; ++c)
        if (static_cast<SoundClass> (c) != label && static_cast<SoundClass> (c) != SoundClass::quiet
            && share[static_cast<std::size_t> (c)] / sounding >= minShare)
            others.push_back (c);
    std::sort (others.begin(), others.end(), [this] (int a, int b) { return share[static_cast<std::size_t> (a)] > share[static_cast<std::size_t> (b)]; });
    for (const int c : others)
        out.push_back (static_cast<SoundClass> (c));
    return out;
}

MemoProfile profileRecording (const float* mono, std::int64_t frames, double sampleRate, const volatile bool* cancelled)
{
    MemoProfile profile;
    if (mono == nullptr || frames <= 0 || sampleRate <= 0.0)
        return profile;
    profile.durationSeconds = static_cast<double> (frames) / sampleRate;

    // About 8 kHz by averaging (a box filter is enough for these features).
    const int factor = std::max (1, static_cast<int> (std::lround (sampleRate / 8000.0)));
    const double rate = sampleRate / factor;
    std::vector<float> x (static_cast<std::size_t> (frames / factor));
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        double sum = 0.0;
        for (int k = 0; k < factor; ++k)
            sum += mono[static_cast<std::int64_t> (i) * factor + k];
        x[i] = static_cast<float> (sum / factor);
    }

    const int frameLength = static_cast<int> (std::lround (rate / framesPerSecond));
    const int minLag = std::max (2, static_cast<int> (rate / 1000.0));   // up to 1 kHz
    const int maxLag = std::min (frameLength - 8, static_cast<int> (rate / 60.0));   // down to 60 Hz
    const int window = frameLength - maxLag;
    const auto count = x.size() / static_cast<std::size_t> (frameLength);
    std::vector<Frame> f (count);
    std::vector<double> scratch;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (cancelled != nullptr && *cancelled)
            return profile;
        const float* p = x.data() + i * static_cast<std::size_t> (frameLength);
        double energy = 0.0, diffEnergy = 0.0;
        for (int k = 0; k < frameLength; ++k)
        {
            energy += static_cast<double> (p[k]) * p[k];
            if (k > 0)
                diffEnergy += static_cast<double> (p[k] - p[k - 1]) * (p[k] - p[k - 1]);
        }
        f[i].db = 10.0 * std::log10 (energy / frameLength + 1.0e-12);
        f[i].brightness = energy > 0.0 ? diffEnergy / energy : 0.0;
        if (energy > 1.0e-9 && window > 16)
        {
            const auto [period, aperiodicity] = yin (p, window, minLag, maxLag, scratch);
            f[i].aperiodicity = aperiodicity;
            f[i].voiced = aperiodicity < 0.25 && period > 0.0;
            f[i].hz = f[i].voiced ? rate / period : 0.0;
        }
    }

    // What counts as sounding: within 40 dB of the memo's loud frames, and above -50 dBFS.
    std::vector<double> levels;
    for (const auto& fr : f)
        levels.push_back (fr.db);
    double loud = -120.0;
    if (! levels.empty())
    {
        std::sort (levels.begin(), levels.end());
        loud = levels[std::min (levels.size() - 1, static_cast<std::size_t> (0.95 * static_cast<double> (levels.size())))];
    }
    const double threshold = std::max (-50.0, loud - 40.0);
    for (std::size_t i = 0; i < count; ++i)
    {
        f[i].active = f[i].db >= threshold;
        f[i].voiced = f[i].voiced && f[i].active;
        f[i].falling = i > 0 && f[i].active && f[i].db < f[i - 1].db - 0.6;
    }
    // Voiced runs (a pitch jump of more than a semitone and a half ends one), their steadiness
    // and, for long runs, whether the pitch wanders like a voice.
    for (std::size_t i = 0; i < count;)
    {
        if (! f[i].voiced)
        {
            ++i;
            continue;
        }
        auto j = i + 1;
        while (j < count && f[j].voiced && std::abs (cents (f[j].hz, f[j - 1].hz)) < 150.0)
            ++j;
        const int run = static_cast<int> (j - i);
        std::vector<double> c;
        for (auto k = i; k < j; ++k)
        {
            f[k].run = run;
            const bool before = k > i && std::abs (cents (f[k].hz, f[k - 1].hz)) < 40.0;
            const bool after = k + 1 < j && std::abs (cents (f[k + 1].hz, f[k].hz)) < 40.0;
            f[k].steady = before || after;
            c.push_back (cents (f[k].hz, 440.0));
        }
        if (run >= longRun)
        {
            const double centre = median (c);
            std::vector<double> deviation;
            for (const double v : c)
                deviation.push_back (std::abs (v - centre));
            const bool wanders = median (deviation) >= 4.0;
            for (auto k = i; k < j; ++k)
                f[k].wandering = wanders;
        }
        i = j;
    }

    // Each second, then a three-second majority (a lone second between two of another kind
    // follows them).
    std::vector<SoundClass> raw;
    for (std::size_t s = 0; s * framesPerSecond < count; ++s)
    {
        const auto begin = s * framesPerSecond, end = std::min (count, begin + framesPerSecond);
        if (end - begin < framesPerSecond / 2 && ! raw.empty())
            break;   // a short tail belongs to the second before
        raw.push_back (classify (f, begin, end));
    }
    profile.seconds.resize (raw.size());
    for (std::size_t s = 0; s < raw.size(); ++s)
    {
        auto c = raw[s];
        if (s > 0 && s + 1 < raw.size() && raw[s - 1] == raw[s + 1] && raw[s] != raw[s - 1])
            c = raw[s - 1];
        profile.seconds[s] = static_cast<std::uint8_t> (c);
    }

    std::array<int, soundClassCount> counts {};
    for (const auto c : profile.seconds)
        ++counts[c];
    const auto total = static_cast<float> (std::max<std::size_t> (1, profile.seconds.size()));
    for (int c = 0; c < soundClassCount; ++c)
        profile.share[static_cast<std::size_t> (c)] = static_cast<float> (counts[static_cast<std::size_t> (c)]) / total;
    int best = -1;
    for (int c = 0; c < soundClassCount; ++c)
        if (static_cast<SoundClass> (c) != SoundClass::quiet && counts[static_cast<std::size_t> (c)] > 0
            && (best < 0 || counts[static_cast<std::size_t> (c)] > counts[static_cast<std::size_t> (best)]))
            best = c;
    const int sounding = static_cast<int> (profile.seconds.size()) - counts[static_cast<std::size_t> (SoundClass::quiet)];
    if (best < 0 || sounding <= 0)
    {
        profile.label = SoundClass::quiet;
        profile.confidence = profile.seconds.empty() ? 0.0f : 1.0f;
    }
    else
    {
        profile.label = static_cast<SoundClass> (best);
        profile.confidence = static_cast<float> (counts[static_cast<std::size_t> (best)]) / static_cast<float> (sounding);
    }
    for (std::size_t s = 0; s < profile.seconds.size(); ++s)
        if (musical (static_cast<SoundClass> (profile.seconds[s])))
        {
            profile.firstMusicSeconds = static_cast<double> (s);
            break;
        }
    return profile;
}

} // namespace osp::memolab
