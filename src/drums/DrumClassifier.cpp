#include "drums/DrumClassifier.h"

#include "core/Fft.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <map>

namespace osp::drums
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    double sig (double x, double mid, double width) { return 1.0 / (1.0 + std::exp (-(x - mid) / width)); }

    std::vector<float> monoRegion (const AudioData& audio, std::int64_t start, std::int64_t end)
    {
        const auto n = audio.numFrames();
        start = std::clamp<std::int64_t> (start, 0, n);
        end = end <= 0 ? n : std::clamp<std::int64_t> (end, start, n);
        std::vector<float> x (static_cast<std::size_t> (end - start), 0.0f);
        const auto channels = audio.numChannels();
        for (const auto& c : audio.channels)
            for (std::int64_t i = start; i < end; ++i)
                x[static_cast<std::size_t> (i - start)] += c[static_cast<std::size_t> (i)] / static_cast<float> (std::max (1, channels));
        return x;
    }

    /** Power spectrum of `length` samples from `from` (Hann, zero-padded to the FFT size). */
    std::vector<double> powerSpectrum (const std::vector<float>& x, std::size_t from, std::size_t length, int order)
    {
        const Fft fft (order);
        const auto n = static_cast<std::size_t> (fft.size());
        std::vector<std::complex<double>> buf (n);
        length = std::min (length, n);
        for (std::size_t i = 0; i < length; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * pi * static_cast<double> (i) / static_cast<double> (std::max<std::size_t> (1, length - 1)));
            buf[i] = { from + i < x.size() ? x[from + i] * w : 0.0, 0.0 };
        }
        fft.forward (buf.data());
        std::vector<double> p (n / 2);
        for (std::size_t k = 0; k < p.size(); ++k)
            p[k] = std::norm (buf[k]);
        return p;
    }

    double centroid (const std::vector<double>& p, double binHz, double lo, double hi)
    {
        double num = 0.0, den = 0.0;
        for (std::size_t k = 1; k < p.size(); ++k)
        {
            const double f = static_cast<double> (k) * binHz;
            if (f < lo || f > hi)
                continue;
            num += f * p[k];
            den += p[k];
        }
        return den > 0.0 ? num / den : 0.0;
    }

    // ---- names ------------------------------------------------------------------------

    std::vector<std::string> tokens (const std::string& filename)
    {
        // Without the extension; camelCase and digits split; lower case.
        std::string name = filename;
        if (const auto dot = name.find_last_of ('.'); dot != std::string::npos && dot > 0)
            name = name.substr (0, dot);
        std::vector<std::string> out;
        std::string current;
        auto flush = [&] {
            if (! current.empty())
                out.push_back (current);
            current.clear();
        };
        for (std::size_t i = 0; i < name.size(); ++i)
        {
            const auto c = static_cast<unsigned char> (name[i]);
            if (! std::isalnum (c))
            {
                flush();
                continue;
            }
            const bool upper = std::isupper (c) != 0;
            const bool digit = std::isdigit (c) != 0;
            if (! current.empty())
            {
                const auto prev = static_cast<unsigned char> (name[i - 1]);
                const bool boundary = (upper && std::islower (prev)) || (digit != (std::isdigit (prev) != 0));
                if (boundary)
                    flush();
            }
            current += static_cast<char> (std::tolower (c));
        }
        flush();
        return out;
    }

    struct Word
    {
        const char* text;
        DrumCategory category;
    };

    // Whole words (also as a prefix of a longer word: "kicks", "snares").
    const Word words[] = {
        { "kick", DrumCategory::kick }, { "kik", DrumCategory::kick }, { "bassdrum", DrumCategory::kick }, { "kck", DrumCategory::kick },
        { "snare", DrumCategory::snare }, { "snr", DrumCategory::snare },
        { "clap", DrumCategory::clap }, { "handclap", DrumCategory::clap }, { "clp", DrumCategory::clap },
        { "tom", DrumCategory::tom }, { "floortom", DrumCategory::tom },
        { "rim", DrumCategory::rim }, { "rimshot", DrumCategory::rim }, { "sidestick", DrumCategory::rim }, { "stick", DrumCategory::rim },
        { "crash", DrumCategory::cymbal }, { "ride", DrumCategory::cymbal }, { "cymbal", DrumCategory::cymbal }, { "cym", DrumCategory::cymbal },
        { "splash", DrumCategory::cymbal }, { "china", DrumCategory::cymbal },
        { "perc", DrumCategory::percussion }, { "shaker", DrumCategory::percussion }, { "shake", DrumCategory::percussion },
        { "tamb", DrumCategory::percussion }, { "tambourine", DrumCategory::percussion }, { "conga", DrumCategory::percussion },
        { "bongo", DrumCategory::percussion }, { "cowbell", DrumCategory::percussion }, { "clave", DrumCategory::percussion },
        { "block", DrumCategory::percussion }, { "triangle", DrumCategory::percussion }, { "guiro", DrumCategory::percussion },
        { "cabasa", DrumCategory::percussion }, { "maraca", DrumCategory::percussion }, { "timbale", DrumCategory::percussion },
        { "agogo", DrumCategory::percussion },
    };

    // Abbreviations: only as a whole token ("bd", never inside "abdomen").
    const Word abbreviations[] = {
        { "bd", DrumCategory::kick }, { "kd", DrumCategory::kick },
        { "sd", DrumCategory::snare }, { "sn", DrumCategory::snare },
        { "cp", DrumCategory::clap },
        { "ch", DrumCategory::closedHat }, { "chh", DrumCategory::closedHat }, { "hhc", DrumCategory::closedHat },
        { "oh", DrumCategory::openHat }, { "ohh", DrumCategory::openHat }, { "hho", DrumCategory::openHat },
        { "lt", DrumCategory::tom }, { "mt", DrumCategory::tom }, { "ht", DrumCategory::tom }, { "ft", DrumCategory::tom },
        { "rs", DrumCategory::rim },
        { "cy", DrumCategory::cymbal }, { "cr", DrumCategory::cymbal }, { "rd", DrumCategory::cymbal },
    };

    // ---- grouping ---------------------------------------------------------------------

    int kitOrder (DrumCategory c)
    {
        switch (c)
        {
            case DrumCategory::kick: return 0;
            case DrumCategory::snare: return 1;
            case DrumCategory::closedHat: return 2;
            case DrumCategory::openHat: return 3;
            case DrumCategory::clap: return 4;
            case DrumCategory::tom: return 5;
            case DrumCategory::rim: return 6;
            case DrumCategory::percussion: return 7;
            case DrumCategory::cymbal: return 8;
            case DrumCategory::unknown: break;
        }
        return 9;
    }
} // namespace

DrumFeatures drumFeatures (const AudioData& audio, std::int64_t start, std::int64_t end)
{
    DrumFeatures f;
    const double sr = audio.sampleRate;
    const auto x = monoRegion (audio, start, end);
    if (x.size() < 64 || sr <= 0.0)
        return f;
    float peak = 0.0f;
    for (const float v : x)
        peak = std::max (peak, std::abs (v));
    if (peak < 1.0e-5f)
        return f;
    f.peakDbfs = 20.0 * std::log10 (peak);
    // Onset: the first sample above -26 dB of the peak.
    std::size_t onset = 0;
    while (onset < x.size() && std::abs (x[onset]) < 0.05f * peak)
        ++onset;
    // 1 ms energy from the onset.
    const auto block = static_cast<std::size_t> (std::max (8.0, std::round (0.001 * sr)));
    std::vector<double> db;
    for (std::size_t b = onset; b + block <= x.size(); b += block)
    {
        double sum = 0.0;
        for (std::size_t i = b; i < b + block; ++i)
            sum += static_cast<double> (x[i]) * x[i];
        db.push_back (10.0 * std::log10 (sum / static_cast<double> (block) + 1.0e-14));
    }
    if (db.empty())
        return f;
    const auto top = static_cast<std::size_t> (std::max_element (db.begin(), db.end()) - db.begin());
    {
        // Attack: to the peak of the 5 ms average (a noise burst's single loud millisecond
        // is not its attack; a shaker's slow swell is).
        double best = -1.0e9;
        std::size_t at = 0;
        for (std::size_t b = 0; b < std::min<std::size_t> (db.size(), 200); ++b)
        {
            double sum = 0.0;
            int n = 0;
            for (std::size_t k = b; k < std::min (db.size(), b + 5); ++k, ++n)
                sum += std::pow (10.0, db[k] / 10.0);
            const double v = sum / std::max (1, n);
            if (v > best * 1.0001)
            {
                best = v;
                at = b;
            }
        }
        f.attackSeconds = static_cast<double> (at) * 0.001;
    }
    // Duration: until 20 dB below the peak, for good (the decay's own length, not its tail).
    std::size_t endBlock = db.size();
    for (std::size_t b = top; b + 2 < db.size(); ++b)
        if (db[b] < db[top] - 20.0 && db[b + 1] < db[top] - 20.0 && db[b + 2] < db[top] - 20.0)
        {
            endBlock = b;
            break;
        }
    f.durationSeconds = static_cast<double> (endBlock) * 0.001;
    // Bursts: separate energy peaks in the first 50 ms of the bright part (a low kick's own
    // oscillation is not a burst), each rising 6 dB out of a dip, within 15 dB of the top.
    {
        std::vector<double> e;
        for (std::size_t b = onset; b + block <= x.size() && e.size() < 50; b += block)
        {
            double sum = 0.0;
            for (std::size_t i = std::max<std::size_t> (b, 1); i < b + block; ++i)
            {
                const double d = x[i] - x[i - 1];
                sum += d * d;
            }
            e.push_back (sum);
        }
        std::vector<double> sm (e.size());
        for (std::size_t b = 0; b < e.size(); ++b)
        {
            const double v = (e[b] + (b > 0 ? e[b - 1] : e[b]) + (b + 1 < e.size() ? e[b + 1] : e[b])) / 3.0;
            sm[b] = 10.0 * std::log10 (v / static_cast<double> (block) + 1.0e-14);
        }
        int count = 0;
        if (! sm.empty())
        {
            const double topDb = *std::max_element (sm.begin(), sm.end());
            double dip = sm[0] - 100.0;
            for (std::size_t b = 1; b + 1 < sm.size(); ++b)
            {
                if (sm[b] >= sm[b - 1] && sm[b] > sm[b + 1] && sm[b] - dip >= 6.0 && sm[b] > topDb - 15.0)
                {
                    ++count;
                    dip = sm[b];
                }
                dip = std::min (dip, sm[b]);
            }
        }
        f.bursts = std::max (1, count);
    }
    // Spectrum of the first 80 ms.
    const int order = Fft::orderForSize (static_cast<int> (std::lround (0.08 * sr)));
    const auto window = static_cast<std::size_t> (std::lround (0.08 * sr));
    const auto p = powerSpectrum (x, onset, window, order);
    const double binHz = sr / static_cast<double> (1 << order);
    const double edges[] = { 20.0, 80.0, 200.0, 800.0, 3000.0, 8000.0, 20000.0 };
    double total = 0.0;
    for (std::size_t k = 1; k < p.size(); ++k)
    {
        const double hz = static_cast<double> (k) * binHz;
        for (int b = 0; b < 6; ++b)
            if (hz >= edges[b] && hz < edges[b + 1])
            {
                f.bands[static_cast<std::size_t> (b)] += p[k];
                total += p[k];
            }
    }
    if (total > 0.0)
        for (auto& b : f.bands)
            b /= total;
    f.centroidHz = centroid (p, binHz, 30.0, 16000.0);
    {
        // Per octave (a bright or dark tilt is not tonality), weighted by each octave's energy.
        double weighted = 0.0, weight = 0.0;
        for (double lo = 125.0; lo < 16000.0; lo *= 2.0)
        {
            double logSum = 0.0, sum = 0.0;
            int count = 0;
            for (std::size_t k = 1; k < p.size(); ++k)
            {
                const double hz = static_cast<double> (k) * binHz;
                if (hz < lo || hz >= lo * 2.0)
                    continue;
                logSum += std::log (p[k] + 1.0e-20);
                sum += p[k];
                ++count;
            }
            if (count < 4 || sum <= 0.0)
                continue;
            weighted += sum * std::exp (logSum / count) / (sum / count);
            weight += sum;
        }
        f.flatness = weight > 0.0 ? weighted / weight : 0.0;
    }
    {
        double best = 0.0;
        std::size_t at = 0;
        std::vector<double> neighbourhood;
        for (std::size_t k = 1; k < p.size(); ++k)
        {
            const double hz = static_cast<double> (k) * binHz;
            if (hz >= 30.0 && hz <= 400.0 && p[k] > best)
            {
                best = p[k];
                at = k;
            }
            if (hz >= 30.0 && hz <= 1600.0)
                neighbourhood.push_back (10.0 * std::log10 (p[k] + 1.0e-20));
        }
        if (at > 0 && ! neighbourhood.empty())
        {
            std::nth_element (neighbourhood.begin(), neighbourhood.begin() + static_cast<std::ptrdiff_t> (neighbourhood.size() / 2), neighbourhood.end());
            f.lowPeakHz = static_cast<double> (at) * binHz;
            f.lowPeakProminence = 10.0 * std::log10 (best + 1.0e-20) - neighbourhood[neighbourhood.size() / 2];
        }
    }
    // The tail: 100 - 300 ms, if there is one (a bright one: open hats, cymbals).
    const auto tailFrom = onset + static_cast<std::size_t> (0.1 * sr);
    if (tailFrom + static_cast<std::size_t> (0.05 * sr) < x.size() && f.durationSeconds > 0.12)
    {
        const auto tail = powerSpectrum (x, tailFrom, static_cast<std::size_t> (0.2 * sr), Fft::orderForSize (static_cast<int> (0.2 * sr)));
        f.tailCentroidHz = centroid (tail, sr / static_cast<double> (1 << Fft::orderForSize (static_cast<int> (0.2 * sr))), 30.0, 16000.0);
    }
    f.valid = true;
    return f;
}

DrumGuess classifyName (const std::string& filename)
{
    DrumGuess g;
    const auto t = tokens (filename);
    std::map<DrumCategory, double> found;
    bool hat = false, open = false, closed = false;
    for (const auto& w : t)
    {
        if (w == "hat" || w == "hats" || w == "hihat" || w == "hihats" || w == "hh" || w == "hi")
            hat = true;
        if (w == "open" || w == "opened")
            open = true;
        if (w == "closed" || w == "close" || w == "cl")
            closed = true;
        for (const auto& word : words)
        {
            const std::string key = word.text;
            if (w == key || (key.size() >= 4 && w.rfind (key, 0) == 0))
                found[word.category] = std::max (found[word.category], 0.9);
        }
        for (const auto& a : abbreviations)
            if (w == a.text)
                found[a.category] = std::max (found[a.category], 0.7);
    }
    if (hat)
    {
        if (open)
            found[DrumCategory::openHat] = std::max (found[DrumCategory::openHat], 0.9);
        else
            found[DrumCategory::closedHat] = std::max (found[DrumCategory::closedHat], closed ? 0.9 : 0.7);
    }
    // A rimshot is how a snare is played: "snare rimshot" is a snare.
    if (found.count (DrumCategory::snare) > 0)
        found.erase (DrumCategory::rim);
    // "hi" alone ("hi tom", "hi conga") is not a hat.
    if (hat && found.count (DrumCategory::tom) > 0 && ! open && ! closed)
        found.erase (DrumCategory::closedHat);
    if (found.empty())
        return g;
    auto best = found.begin();
    for (auto it = found.begin(); it != found.end(); ++it)
        if (it->second > best->second)
            best = it;
    g.category = best->first;
    g.confidence = found.size() == 1 ? best->second : 0.5;   // the name names several things
    g.fromName = true;
    g.reason = found.size() == 1 ? "the file name" : "the file name (ambiguous)";
    return g;
}

DrumGuess classifyFeatures (const DrumFeatures& f)
{
    DrumGuess g;
    if (! f.valid)
    {
        g.reason = "no sound";
        return g;
    }
    const double low = f.low(), high = f.high(), mid = f.bands[3], sub = f.bands[0];
    const double dur = f.durationSeconds, c = f.centroidHz, flat = f.flatness;
    std::array<double, 10> score {};
    auto at = [&score] (DrumCategory k) -> double& { return score[static_cast<std::size_t> (k)]; };
    at (DrumCategory::kick) = sig (low, 0.5, 0.08) * sig (-c, -700.0, 150.0) * sig (dur, 0.08, 0.03) * (1.0 - 0.6 * sig (f.lowPeakHz, 110.0, 12.0) * (1.0 - sig (sub, 0.35, 0.08)));
    at (DrumCategory::tom) = sig (f.bands[1] + f.bands[2], 0.55, 0.1) * sig (f.lowPeakProminence, 12.0, 3.0) * sig (f.lowPeakHz, 95.0, 12.0)
                             * (1.0 - sig (flat, 0.3, 0.07)) * sig (dur, 0.12, 0.04) * (1.0 - sig (sub, 0.4, 0.08));
    // A snare: a noisy crack over the body (centroid in the middle), one attack.
    at (DrumCategory::snare) = sig (mid + high, 0.3, 0.08) * sig (flat, 0.12, 0.04) * (1.0 - sig (flat, 0.45, 0.05)) * sig (c, 1200.0, 400.0)
                               * (1.0 - sig (c, 7000.0, 800.0)) * sig (dur, 0.06, 0.02) * (f.bursts <= 1 ? 1.0 : 0.5);
    // A clap: very noisy, several bursts in its first tens of milliseconds.
    at (DrumCategory::clap) = sig (flat, 0.4, 0.05) * sig (static_cast<double> (f.bursts), 1.5, 0.25) * sig (c, 900.0, 300.0) * (1.0 - sig (c, 8000.0, 800.0))
                              * (1.0 - sig (f.attackSeconds, 0.045, 0.01));
    // Hats and cymbals: bright noise; how long it rings tells them apart.
    const double bright = sig (high, 0.55, 0.08) * sig (c, 4500.0, 700.0) * (1.0 - sig (f.attackSeconds, 0.015, 0.004));
    at (DrumCategory::closedHat) = bright * (1.0 - sig (dur, 0.18, 0.04));
    at (DrumCategory::openHat) = bright * sig (dur, 0.18, 0.04) * (1.0 - sig (dur, 1.4, 0.15));
    at (DrumCategory::cymbal) = sig (high, 0.45, 0.1) * sig (dur, 1.4, 0.15) * sig (std::max (c, f.tailCentroidHz), 3500.0, 700.0);
    // A rim: very short, tonal, in the middle.
    at (DrumCategory::rim) = (1.0 - sig (dur, 0.09, 0.02)) * sig (c, 600.0, 200.0) * (1.0 - sig (c, 5000.0, 800.0)) * (1.0 - sig (flat, 0.2, 0.06))
                             * (1.0 - sig (low, 0.4, 0.08));
    // Percussion: a weak catch-all, stronger for soft attacks (shakers) and mid tones (congas).
    at (DrumCategory::percussion) = 0.2 + 0.6 * sig (f.attackSeconds, 0.015, 0.004) * sig (high, 0.35, 0.1)
                                    + 0.45 * sig (f.lowPeakHz, 320.0, 25.0) * sig (f.lowPeakProminence, 12.0, 3.0) * (1.0 - sig (flat, 0.2, 0.06));

    int best = 0, second = -1;
    for (int k = 1; k < 9; ++k)
        if (score[static_cast<std::size_t> (k)] > score[static_cast<std::size_t> (best)])
            best = k;
    for (int k = 0; k < 9; ++k)
        if (k != best && (second < 0 || score[static_cast<std::size_t> (k)] > score[static_cast<std::size_t> (second)]))
            second = k;
    const double b = score[static_cast<std::size_t> (best)], s = score[static_cast<std::size_t> (second)];
    g.category = static_cast<DrumCategory> (best);
    g.confidence = std::clamp (0.5 * std::min (1.0, b / 0.6) + 0.5 * (b - s) / std::max (1.0e-9, b), 0.0, 1.0);
    g.reason = "the sound";
    // Strong sub-bass and strong highs together: two instruments in one hit (kick + hat).
    if (sub >= 0.25 && high >= 0.2)
    {
        g.layered = true;
        g.confidence *= 0.7;
        g.reason = "the sound (several instruments at once)";
    }
    return g;
}

DrumGuess classify (const std::string& filename, const DrumFeatures& features)
{
    const auto byName = classifyName (filename);
    const auto bySound = classifyFeatures (features);
    DrumGuess g;
    if (byName.confidence >= 0.6)
    {
        g = byName;
        if (bySound.category == byName.category)
        {
            g.confidence = std::max (byName.confidence, bySound.confidence);
            g.reason = "the file name and the sound agree";
        }
        else if (bySound.confidence >= 0.75)
        {
            g.confidence = 0.55;
            g.reason = std::string ("the file name (the sound suggests ") + toString (bySound.category) + ")";
        }
        else
            g.confidence = byName.confidence * 0.9;
    }
    else
        g = bySound;
    g.layered = bySound.layered;
    if (g.confidence < 0.45)
    {
        g.reason = std::string ("unsure (closest: ") + toString (g.category) + ")";
        g.category = DrumCategory::unknown;
    }
    return g;
}

double similarity (const DrumFeatures& a, const DrumFeatures& b) noexcept
{
    if (! a.valid || ! b.valid)
        return 0.0;
    auto v = [] (const DrumFeatures& f) {
        return std::array<double, 6> { std::log2 (std::max (0.01, f.durationSeconds)) / 1.5, std::log2 (std::max (20.0, f.centroidHz)) / 1.0,
                                       f.low() * 3.0, f.high() * 3.0, f.flatness * 3.0, std::log2 (f.attackSeconds + 0.001) / 2.0 };
    };
    const auto x = v (a), y = v (b);
    double d2 = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i)
        d2 += (x[i] - y[i]) * (x[i] - y[i]);
    return 1.0 / (1.0 + d2);
}

std::string nameStem (const std::string& filename)
{
    // Tokens without numbers and take / velocity / round-robin words.
    std::string stem;
    for (const auto& t : tokens (filename))
    {
        if (std::all_of (t.begin(), t.end(), [] (char c) { return std::isdigit (static_cast<unsigned char> (c)) != 0; }))
            continue;
        if (t == "rr" || t == "take" || t == "tk" || t == "v" || t == "vel" || t == "var" || t == "alt" || t == "copy" || t == "hit")
            continue;
        if (! stem.empty())
            stem += ' ';
        stem += t;
    }
    return stem;
}

std::vector<std::vector<int>> groupItems (const std::vector<GroupItem>& items)
{
    std::vector<std::vector<int>> groups;
    std::vector<int> groupOf (items.size(), -1);
    for (int i = 0; i < static_cast<int> (items.size()); ++i)
    {
        const auto& a = items[static_cast<std::size_t> (i)];
        int join = -1;
        for (int g = 0; g < static_cast<int> (groups.size()) && join < 0; ++g)
        {
            const auto& first = items[static_cast<std::size_t> (groups[static_cast<std::size_t> (g)].front())];
            if (first.guess.category != a.guess.category)
                continue;
            // Every member must sound alike (no chaining unrelated sounds through a middle one).
            bool all = true;
            const bool sameStem = ! nameStem (a.name).empty() && nameStem (a.name) == nameStem (first.name);
            for (int m : groups[static_cast<std::size_t> (g)])
            {
                const double sim = similarity (a.features, items[static_cast<std::size_t> (m)].features);
                const bool unknown = a.guess.category == DrumCategory::unknown;
                const bool ok = sameStem ? sim >= (unknown ? 0.6 : 0.35) : (! unknown && sim >= 0.8);
                all = all && ok;
            }
            if (all)
                join = g;
        }
        if (join < 0)
        {
            groups.push_back ({ i });
            groupOf[static_cast<std::size_t> (i)] = static_cast<int> (groups.size()) - 1;
        }
        else
        {
            groups[static_cast<std::size_t> (join)].push_back (i);
            groupOf[static_cast<std::size_t> (i)] = join;
        }
    }
    std::stable_sort (groups.begin(), groups.end(), [&items] (const auto& x, const auto& y) {
        return kitOrder (items[static_cast<std::size_t> (x.front())].guess.category) < kitOrder (items[static_cast<std::size_t> (y.front())].guess.category);
    });
    return groups;
}

std::vector<int> mapGroupsToPads (const std::vector<std::vector<int>>& groups, const std::vector<GroupItem>& items,
                                  const std::array<DrumCategory, maxPads>& padCategories, const std::vector<int>& freePads)
{
    std::vector<int> pad (groups.size(), -1);
    std::vector<bool> used (freePads.size(), false);
    // First a pad of the group's own category, then any free pad in order.
    for (std::size_t g = 0; g < groups.size(); ++g)
    {
        const auto c = items[static_cast<std::size_t> (groups[g].front())].guess.category;
        if (c == DrumCategory::unknown)
            continue;
        for (std::size_t k = 0; k < freePads.size(); ++k)
            if (! used[k] && padCategories[static_cast<std::size_t> (std::clamp (freePads[k], 0, maxPads - 1))] == c)
            {
                pad[g] = freePads[k];
                used[k] = true;
                break;
            }
    }
    for (std::size_t g = 0; g < groups.size(); ++g)
        if (pad[g] < 0)
            for (std::size_t k = 0; k < freePads.size(); ++k)
                if (! used[k])
                {
                    pad[g] = freePads[k];
                    used[k] = true;
                    break;
                }
    return pad;
}

} // namespace osp::drums
