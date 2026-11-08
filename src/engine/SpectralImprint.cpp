#include "engine/SpectralImprint.h"

#include "core/Fft.h"

#include <algorithm>
#include <complex>
#include <numbers>

namespace osp::imprint
{

namespace
{
    double gridHz (int p) { return gridMinHz * std::exp2 (p * gridStepOctaves); }

    /** The profile's treatment of one envelope (gridPoints dB, any offset): the broad tilt
        mostly removed (a straight line in log frequency fitted over 100 Hz .. 8 kHz, 35 % of
        it kept as broad colour), the floor at 45 dB under the peak, bounded, mean 0. */
    std::vector<float> treat (const std::vector<double>& db)
    {
        constexpr double keepTilt = 0.35, floorDb = 45.0;
        std::vector<double> v (db);
        const double peak = *std::max_element (v.begin(), v.end());
        for (auto& x : v)
            x = std::max (x, peak - floorDb);
        double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0, n = 0.0;
        for (int p = 0; p < gridPoints; ++p)
        {
            const double hz = gridHz (p);
            if (hz < 100.0 || hz > 8000.0)
                continue;
            const double x = p, y = v[static_cast<std::size_t> (p)];
            sx += x; sy += y; sxx += x * x; sxy += x * y; n += 1.0;
        }
        const double slope = n > 2.0 ? (n * sxy - sx * sy) / std::max (1.0e-9, n * sxx - sx * sx) : 0.0;
        const double intercept = n > 0.0 ? (sy - slope * sx) / n : 0.0;
        std::vector<float> out (static_cast<std::size_t> (gridPoints));
        double mean = 0.0;
        for (int p = 0; p < gridPoints; ++p)
        {
            const double line = intercept + slope * p;
            const double value = (v[static_cast<std::size_t> (p)] - line) + keepTilt * (line - intercept - slope * 0.5 * (gridPoints - 1));
            out[static_cast<std::size_t> (p)] = static_cast<float> (value);
            mean += value;
        }
        mean /= gridPoints;
        for (auto& x : out)
            x = static_cast<float> (std::clamp (static_cast<double> (x) - mean, -maxBandDb, maxBandDb));
        return out;
    }

    /** A bell's response (dB) at `hz`: the analog prototype at the pre-warped frequency (the
        TPT filter is exactly its bilinear transform). */
    double bellDb (double hz, double centreG, double q, double gainDb, double sampleRate)
    {
        const double w = std::tan (std::numbers::pi * std::min (hz, 0.49 * sampleRate) / sampleRate) / centreG;
        const double a = std::pow (10.0, gainDb / 40.0);
        const std::complex<double> s (0.0, w);
        const auto h = (s * s + s * (a / q) + 1.0) / (s * s + s / (a * q) + 1.0);
        return 20.0 * std::log10 (std::abs (h));
    }
}

//==============================================================================
Profile analyse (const AudioData& audio, double f0Hz)
{
    Profile profile;
    profile.referenceHz = f0Hz > 0.0 ? f0Hz : 0.0;
    if (audio.isEmpty())
        return profile;
    const auto mono = audio.mixToMono();
    const double rate = audio.sampleRate;
    const Fft fft (11);   // 2048
    const int n = fft.size(), hop = n / 4;
    // The cepstral lifter sits under the harmonic spacing, so harmonics are smoothed away and
    // the resonant body remains (as analysis/spectrum/SpectralEnvelope does, per frame here).
    const int cutoff = std::max (4, static_cast<int> (0.5 * rate / std::max (f0Hz > 0.0 ? f0Hz * 1.1 : 150.0, 60.0)));
    const double seconds = static_cast<double> (mono.size()) / rate;
    profile.frameSeconds = std::max (0.05, seconds / 120.0);   // at most 120 frames
    const int frames = std::max (1, static_cast<int> (std::ceil (seconds / profile.frameSeconds)));

    std::vector<double> window (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
        window[static_cast<std::size_t> (i)] = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / n);
    std::vector<std::complex<double>> buffer (static_cast<std::size_t> (n));
    std::vector<std::vector<double>> sum (static_cast<std::size_t> (frames), std::vector<double> (static_cast<std::size_t> (gridPoints), 0.0));
    std::vector<double> weight (static_cast<std::size_t> (frames), 0.0), energyOf (static_cast<std::size_t> (frames), 0.0);

    for (std::size_t start = 0;; start += static_cast<std::size_t> (hop))
    {
        double energy = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto index = start + static_cast<std::size_t> (i);
            const double x = index < mono.size() ? mono[index] * window[static_cast<std::size_t> (i)] : 0.0;
            buffer[static_cast<std::size_t> (i)] = { x, 0.0 };
            energy += x * x;
        }
        const double centre = (static_cast<double> (start) + 0.5 * n) / rate;
        const int f = std::clamp (static_cast<int> (centre / profile.frameSeconds), 0, frames - 1);
        if (energy > 1.0e-10)
        {
            fft.forward (buffer.data());
            for (auto& v : buffer)
                v = { std::log (std::abs (v) + 1.0e-9), 0.0 };
            fft.inverse (buffer.data());
            for (int q = cutoff; q <= n - cutoff; ++q)
                buffer[static_cast<std::size_t> (q)] = 0.0;
            fft.forward (buffer.data());
            const double w = std::sqrt (energy);
            for (int p = 0; p < gridPoints; ++p)
            {
                const double bin = std::min (gridHz (p) / rate * n, n / 2.0 - 1.0);
                const auto b0 = static_cast<std::size_t> (bin);
                const double frac = bin - static_cast<double> (b0);
                const double value = (1.0 - frac) * buffer[b0].real() + frac * buffer[b0 + 1].real();
                sum[static_cast<std::size_t> (f)][static_cast<std::size_t> (p)] += w * 20.0 / std::log (10.0) * value;
            }
            weight[static_cast<std::size_t> (f)] += w;
            energyOf[static_cast<std::size_t> (f)] += energy;
        }
        if (start + static_cast<std::size_t> (n) >= mono.size())
            break;
    }

    // Each frame's envelope; silent frames take their nearest heard neighbour.
    const double loudest = *std::max_element (energyOf.begin(), energyOf.end());
    if (! (loudest > 1.0e-9))
        return profile;   // silent: not valid (the voice plays Analog)
    std::vector<int> heard;
    for (int f = 0; f < frames; ++f)
        if (energyOf[static_cast<std::size_t> (f)] > loudest * 1.0e-5 && weight[static_cast<std::size_t> (f)] > 0.0)
            heard.push_back (f);
    std::vector<double> levelDb (static_cast<std::size_t> (frames), -120.0);
    for (int f : heard)
        levelDb[static_cast<std::size_t> (f)] = 10.0 * std::log10 (energyOf[static_cast<std::size_t> (f)] / loudest);
    profile.framesDb.reserve (static_cast<std::size_t> (frames * gridPoints));
    for (int f = 0; f < frames; ++f)
    {
        int src = f;
        if (! std::binary_search (heard.begin(), heard.end(), f))
        {
            int best = heard.front();
            for (int h : heard)
                if (std::abs (h - f) < std::abs (best - f))
                    best = h;
            src = best;
        }
        std::vector<double> env (static_cast<std::size_t> (gridPoints));
        for (int p = 0; p < gridPoints; ++p)
            env[static_cast<std::size_t> (p)] = sum[static_cast<std::size_t> (src)][static_cast<std::size_t> (p)] / weight[static_cast<std::size_t> (src)];
        const auto row = treat (env);
        profile.framesDb.insert (profile.framesDb.end(), row.begin(), row.end());
    }

    // The attack runs to the loudest frame; the sustain is the longest run after it within
    // 12 dB of that peak (a pluck without one: its early body, up to 0.3 s).
    const int peak = static_cast<int> (std::max_element (levelDb.begin(), levelDb.end()) - levelDb.begin());
    profile.attackEnd = std::clamp (peak + 1, 1, frames);
    int bestBegin = profile.attackEnd, bestLength = 0;
    for (int f = profile.attackEnd; f < frames;)
    {
        if (levelDb[static_cast<std::size_t> (f)] < -12.0)
        {
            ++f;
            continue;
        }
        int e = f;
        while (e < frames && levelDb[static_cast<std::size_t> (e)] >= -12.0)
            ++e;
        if (e - f > bestLength)
        {
            bestBegin = f;
            bestLength = e - f;
        }
        f = e;
    }
    if (bestLength < 3)
    {
        bestBegin = std::min (profile.attackEnd, frames - 1);
        bestLength = std::max (1, std::min (frames - bestBegin, static_cast<int> (std::ceil (0.3 / profile.frameSeconds))));
    }
    profile.sustainBegin = bestBegin;
    profile.sustainEnd = bestBegin + bestLength;

    // The static imprint: the sustain's frames averaged by their energy, then treated.
    std::vector<double> mean (static_cast<std::size_t> (gridPoints), 0.0);
    double total = 0.0;
    for (int f = profile.sustainBegin; f < profile.sustainEnd; ++f)
    {
        const double w = std::pow (10.0, levelDb[static_cast<std::size_t> (f)] / 20.0);
        if (weight[static_cast<std::size_t> (f)] <= 0.0)
            continue;
        for (int p = 0; p < gridPoints; ++p)
            mean[static_cast<std::size_t> (p)] += w * sum[static_cast<std::size_t> (f)][static_cast<std::size_t> (p)] / weight[static_cast<std::size_t> (f)];
        total += w;
    }
    if (! (total > 0.0))
        return profile;
    for (auto& v : mean)
        v /= total;
    profile.staticDb = treat (mean);

    // The band values for the standard layout, averaged over each band's width.
    const auto bands = BandLayout::make (standardBands, true, 80.0, 12000.0);
    auto toBands = [&bands] (const float* row, std::vector<float>& out) {
        for (int b = 0; b < bands.count; ++b)
            out.push_back (Profile::over (row, bands.hz[static_cast<std::size_t> (b)], bands.octaves[static_cast<std::size_t> (b)]));
    };
    toBands (profile.staticDb.data(), profile.bandStaticDb);
    for (int f = 0; f < profile.frameCount(); ++f)
        toBands (profile.frame (f), profile.bandFramesDb);
    profile.confidence = static_cast<float> (std::clamp (static_cast<double> (heard.size()) / 4.0, 0.0, 1.0));
    profile.valid = true;
    return profile;
}

//==============================================================================
BandLayout BandLayout::make (int count, bool perceptual, double lowHz, double highHz)
{
    BandLayout layout;
    layout.count = std::clamp (count, 4, maxBands);
    // ERB-rate (Glasberg & Moore): E(f) = 21.4 log10 (1 + 0.00437 f).
    auto erb = [] (double hz) { return 21.4 * std::log10 (1.0 + 0.00437 * hz); };
    auto fromErb = [] (double e) { return (std::pow (10.0, e / 21.4) - 1.0) / 0.00437; };
    for (int b = 0; b < layout.count; ++b)
    {
        const double t = static_cast<double> (b) / (layout.count - 1);
        layout.hz[static_cast<std::size_t> (b)] = perceptual ? fromErb (erb (lowHz) + t * (erb (highHz) - erb (lowHz)))
                                                             : lowHz * std::pow (highHz / lowHz, t);
    }
    for (int b = 0; b < layout.count; ++b)
    {
        // Q from the distance to the neighbours (in octaves): adjacent bands cross near -3 dB.
        const double below = b > 0 ? std::log2 (layout.hz[static_cast<std::size_t> (b)] / layout.hz[static_cast<std::size_t> (b - 1)]) : 0.0;
        const double above = b + 1 < layout.count ? std::log2 (layout.hz[static_cast<std::size_t> (b + 1)] / layout.hz[static_cast<std::size_t> (b)]) : 0.0;
        const double octaves = std::max (0.1, b == 0 ? above : (b + 1 == layout.count ? below : 0.5 * (below + above)));
        const double r = std::exp2 (octaves);
        layout.q[static_cast<std::size_t> (b)] = std::sqrt (r) / (r - 1.0);
        layout.octaves[static_cast<std::size_t> (b)] = octaves;
    }
    return layout;
}

//==============================================================================
void Shaper::prepare (double rate, int count, bool perceptual) noexcept
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    bandLayout = BandLayout::make (count, perceptual, 80.0, std::min (12000.0, 0.42 * sampleRate));
    bands = bandLayout.count;
    standard = perceptual && bands == standardBands && 0.42 * sampleRate >= 12000.0;
    for (int b = 0; b < bands; ++b)
        g[static_cast<std::size_t> (b)] = std::tan (std::numbers::pi * bandLayout.hz[static_cast<std::size_t> (b)] / sampleRate);

    // The bands' interaction: each band at a prototype gain, read at every centre. Solving it
    // gives the gains that make the cascade meet a target at the centres (the accurate
    // graphic-equaliser method). Inverted once here.
    constexpr double prototype = 12.0;
    std::array<double, BandLayout::maxBands * BandLayout::maxBands> m {}, inv {};   // (no heap: a voice prepares it)
    for (int i = 0; i < bands; ++i)
        for (int j = 0; j < bands; ++j)
            m[static_cast<std::size_t> (i * bands + j)] = bellDb (bandLayout.hz[static_cast<std::size_t> (i)], g[static_cast<std::size_t> (j)],
                                                                 bandLayout.q[static_cast<std::size_t> (j)], prototype, sampleRate) / prototype;
    for (int i = 0; i < bands; ++i)
        inv[static_cast<std::size_t> (i * bands + i)] = 1.0;
    for (int c = 0; c < bands; ++c)
    {
        int pivot = c;
        for (int r = c + 1; r < bands; ++r)
            if (std::abs (m[static_cast<std::size_t> (r * bands + c)]) > std::abs (m[static_cast<std::size_t> (pivot * bands + c)]))
                pivot = r;
        for (int k = 0; k < bands; ++k)
        {
            std::swap (m[static_cast<std::size_t> (c * bands + k)], m[static_cast<std::size_t> (pivot * bands + k)]);
            std::swap (inv[static_cast<std::size_t> (c * bands + k)], inv[static_cast<std::size_t> (pivot * bands + k)]);
        }
        const double d = m[static_cast<std::size_t> (c * bands + c)];
        for (int k = 0; k < bands; ++k)
        {
            m[static_cast<std::size_t> (c * bands + k)] /= d;
            inv[static_cast<std::size_t> (c * bands + k)] /= d;
        }
        for (int r = 0; r < bands; ++r)
            if (r != c)
            {
                const double f = m[static_cast<std::size_t> (r * bands + c)];
                for (int k = 0; k < bands; ++k)
                {
                    m[static_cast<std::size_t> (r * bands + k)] -= f * m[static_cast<std::size_t> (c * bands + k)];
                    inv[static_cast<std::size_t> (r * bands + k)] -= f * inv[static_cast<std::size_t> (c * bands + k)];
                }
            }
    }
    solve = inv;
    reset();
}

void Shaper::reset() noexcept
{
    for (auto& s : state)
        s = {};
    gainDb.fill (0.0);
    targetDb.fill (0.0);
    level = levelTarget = 1.0f;
    levelStep = 0.0f;
    energyIn = energyOut = 0.0f;
    powerIn = powerOut = measured = 0.0;
    engaged = false;
    fresh = true;
    updateCoefficients();
}

void Shaper::setTarget (const Profile& profile, double amount, double position, double motion, double pitchHz,
                        double formantFollow, int samplesToNext) noexcept
{
    amount = std::clamp (amount, 0.0, 1.0);
    motion = std::clamp (motion, 0.0, 1.0);
    std::array<double, BandLayout::maxBands> wanted {};
    if (profile.valid && amount > 0.0)
    {
        // Where the resonances sit for this pitch: fixed, or following the key.
        const double shift = profile.referenceHz > 0.0 && pitchHz > 0.0 && formantFollow != 0.0
                                 ? std::pow (pitchHz / profile.referenceHz, std::clamp (formantFollow, 0.0, 1.0)) : 1.0;
        const float* rest = profile.staticDb.data();
        const float* moving = nullptr;
        const float* movingNext = nullptr;
        float blend = 0.0f;
        if (motion > 0.0 && position >= 0.0 && profile.frameCount() > 0)
        {
            const double p = std::min (position, static_cast<double> (profile.frameCount() - 1));
            const int i = static_cast<int> (p);
            moving = profile.frame (i);
            movingNext = profile.frame (std::min (i + 1, profile.frameCount() - 1));
            blend = static_cast<float> (p - i);
        }
        if (standard && shift == 1.0 && ! profile.bandStaticDb.empty())
        {
            // The descriptor's own band values (no arithmetic beyond the blend).
            const float* restBands = profile.bandStaticDb.data();
            const float* a = moving != nullptr ? profile.bandFramesDb.data() + (moving - profile.framesDb.data()) / gridPoints * standardBands : nullptr;
            const float* c = movingNext != nullptr ? profile.bandFramesDb.data() + (movingNext - profile.framesDb.data()) / gridPoints * standardBands : nullptr;
            for (int b = 0; b < bands; ++b)
            {
                double db = restBands[b];
                if (a != nullptr)
                    db += motion * (a[b] + blend * (c[b] - a[b]) - db);
                wanted[static_cast<std::size_t> (b)] = amount * db;
            }
        }
        else
            for (int b = 0; b < bands; ++b)
            {
                // What the band can render: the profile averaged over the band's own width (a
                // narrower detail would only push the solved gains to their bounds).
                const double hz = bandLayout.hz[static_cast<std::size_t> (b)] / shift;
                const double width = bandLayout.octaves[static_cast<std::size_t> (b)];
                double db = Profile::over (rest, hz, width);
                if (moving != nullptr)
                {
                    const double a = Profile::over (moving, hz, width), c = Profile::over (movingNext, hz, width);
                    db += motion * (a + blend * (c - a) - db);
                }
                wanted[static_cast<std::size_t> (b)] = amount * db;
            }
    }
    // The bands' own gains for that target (their spill into each other compensated), bounded.
    for (int i = 0; i < bands; ++i)
    {
        double x = 0.0;
        for (int j = 0; j < bands; ++j)
            x += solve[static_cast<std::size_t> (i * bands + j)] * wanted[static_cast<std::size_t> (j)];
        targetDb[static_cast<std::size_t> (i)] = std::clamp (x, -1.5 * maxBandDb, 1.5 * maxBandDb);
    }

    // The level for the harmonics this pitch has: the saw's band power falls 3 dB per octave
    // above its fundamental (none below), and the oscillator's low-pass (about 6 x the pitch)
    // takes the top. The imprint then keeps that loudness.
    double plain = 0.0, coloured = 0.0;
    const double f0 = std::max (pitchHz, 20.0);
    for (int b = 0; b < bands; ++b)
    {
        const double hz = bandLayout.hz[static_cast<std::size_t> (b)];
        if (hz < 0.7 * f0)
            continue;
        // A band holds (its width x hz / f0) harmonics of amplitude f0 / hz: power ~ width x f0 / hz.
        const double cut = hz / std::clamp (6.0 * f0, 250.0, 12000.0);
        const double p = bandLayout.octaves[static_cast<std::size_t> (b)] * (f0 / hz) / (1.0 + cut * cut * cut * cut);
        plain += p;
        coloured += p * std::exp (0.23025850929940458 * wanted[static_cast<std::size_t> (b)]);   // 10^(dB/10)
    }
    const double estimate = coloured > 0.0 ? std::clamp (std::sqrt (plain / coloured), 0.25, 4.0) : 1.0;
    // Then what the cascade actually does to this oscillator, measured: the estimate gives way
    // to it over the first ~120 ms of sound.
    {
        const double a = 1.0 - std::exp (-static_cast<double> (std::max (1, samplesToNext)) / (0.12 * sampleRate));
        powerIn += a * (energyIn - powerIn);
        powerOut += a * (energyOut - powerOut);
        if (energyIn > 1.0e-12f)
            measured = std::min (1.0, measured + a);
        energyIn = energyOut = 0.0f;
    }
    const double actual = powerOut > 1.0e-15 ? std::clamp (std::sqrt (powerIn / powerOut), 0.25, 4.0) : estimate;
    levelTarget = static_cast<float> (estimate + measured * (actual - estimate));

    // About 25 ms to a new target (a first target is taken at once: no sweep at a note's start).
    const double k = fresh ? 1.0 : 1.0 - std::exp (-static_cast<double> (std::max (1, samplesToNext)) / (0.025 * sampleRate));
    bool any = false, moved = false;
    for (int b = 0; b < bands; ++b)
    {
        auto& gdb = gainDb[static_cast<std::size_t> (b)];
        const double before = gdb;
        gdb += k * (targetDb[static_cast<std::size_t> (b)] - gdb);
        if (std::abs (targetDb[static_cast<std::size_t> (b)] - gdb) < 1.0e-3)
            gdb = targetDb[static_cast<std::size_t> (b)];
        any = any || gdb != 0.0;
        moved = moved || gdb != before;
    }
    if (fresh)
    {
        level = levelTarget;
        levelStep = 0.0f;
    }
    else
        levelStep = (levelTarget - level) / static_cast<float> (std::max (1, samplesToNext));
    fresh = false;
    if (! any)
    {
        // No colour left: exactly the oscillator again.
        level = levelTarget = 1.0f;
        levelStep = 0.0f;
        powerIn = powerOut = measured = 0.0;
    }
    engaged = any;
    if (moved)
        updateCoefficients();   // (only when a band moved: a settled note costs nothing here)
}

void Shaper::updateCoefficients() noexcept
{
    for (int b = 0; b < bands; ++b)
    {
        const double a = std::exp (0.057564627324851145 * gainDb[static_cast<std::size_t> (b)]);   // 10^(dB/40)
        const double k = 1.0 / (a * bandLayout.q[static_cast<std::size_t> (b)]);
        const double gg = g[static_cast<std::size_t> (b)];
        const double a1 = 1.0 / (1.0 + gg * (gg + k));
        auto& c = coef[static_cast<std::size_t> (b)];
        c.a1 = static_cast<float> (a1);
        c.a2 = static_cast<float> (gg * a1);
        c.a3 = static_cast<float> (gg * gg * a1);
        c.boost = static_cast<float> ((a * a - 1.0) * k);
    }
}

double Shaper::responseDb (double hz) const noexcept
{
    double db = 0.0;
    for (int b = 0; b < bands; ++b)
        db += bellDb (hz, g[static_cast<std::size_t> (b)], bandLayout.q[static_cast<std::size_t> (b)], gainDb[static_cast<std::size_t> (b)], sampleRate);
    return db + 20.0 * std::log10 (std::max (1.0e-6f, levelTarget));
}

//==============================================================================
void Vocoder::prepare (double rate, int count, bool perceptual)
{
    const auto layout = BandLayout::make (count, perceptual, 80.0, std::min (12000.0, 0.42 * rate));
    bands = layout.count;
    for (int b = 0; b < bands; ++b)
    {
        auto& band = bank[static_cast<std::size_t> (b)];
        band.g = std::tan (std::numbers::pi * layout.hz[static_cast<std::size_t> (b)] / rate);
        band.k = 1.0 / layout.q[static_cast<std::size_t> (b)];
        band.a1 = 1.0 / (1.0 + band.g * (band.g + band.k));
        band.a2 = band.g * band.a1;
        band.a3 = band.g * band.a2;
    }
    up = static_cast<float> (1.0 - std::exp (-1.0 / (0.003 * rate)));
    down = static_cast<float> (1.0 - std::exp (-1.0 / (0.030 * rate)));
    levelUp = static_cast<float> (1.0 - std::exp (-1.0 / (0.050 * rate)));
    reset();
}

void Vocoder::reset() noexcept
{
    for (auto& b : bank)
    {
        b.c1 = b.c2 = b.m1 = b.m2 = 0.0f;
        b.carrierEnv = b.contourEnv = 0.0f;
    }
    inLevel = outLevel = 0.0f;
}

float Vocoder::bandpass (const Band& b, float x, float& s1, float& s2) const noexcept
{
    const float v3 = x - s2;
    const float v1 = static_cast<float> (b.a1) * s1 + static_cast<float> (b.a2) * v3;
    const float v2 = s2 + static_cast<float> (b.a2) * s1 + static_cast<float> (b.a3) * v3;
    s1 = 2.0f * v1 - s1;
    s2 = 2.0f * v2 - s2;
    return static_cast<float> (b.k) * v1;   // unity at the centre
}

float Vocoder::process (float carrier, float contour, float amount) noexcept
{
    float sumContour = 0.0f, sumCarrier = 0.0f;
    std::array<float, BandLayout::maxBands> carried {};
    for (int i = 0; i < bands; ++i)
    {
        auto& b = bank[static_cast<std::size_t> (i)];
        const float c = bandpass (b, carrier, b.c1, b.c2);
        const float m = bandpass (b, contour, b.m1, b.m2);
        const float ac = std::abs (c), am = std::abs (m);
        b.carrierEnv += (ac > b.carrierEnv ? up : down) * (ac - b.carrierEnv);
        b.contourEnv += (am > b.contourEnv ? up : down) * (am - b.contourEnv);
        carried[static_cast<std::size_t> (i)] = c;
        sumContour += b.contourEnv;
        sumCarrier += b.carrierEnv;
    }
    // Each carrier band takes the contour band's share of the contour (floored: an empty
    // carrier band is never boosted without limit), then the whole is levelled to the carrier.
    const float floorEnv = 1.0e-5f + 0.02f * sumCarrier / static_cast<float> (std::max (1, bands));
    const float contourTotal = std::max (sumContour, 1.0e-9f);
    float y = 0.0f;
    for (int i = 0; i < bands; ++i)
    {
        const auto& b = bank[static_cast<std::size_t> (i)];
        const float share = b.contourEnv / contourTotal * sumCarrier;
        const float gain = std::min (share / (b.carrierEnv + floorEnv), 8.0f);
        y += gain * carried[static_cast<std::size_t> (i)];
    }
    inLevel += levelUp * (std::abs (carrier) - inLevel);
    outLevel += levelUp * (std::abs (y) - outLevel);
    const float norm = outLevel > 1.0e-7f ? std::clamp (inLevel / outLevel, 0.1f, 10.0f) : 1.0f;
    return carrier + amount * (y * norm - carrier);
}

} // namespace osp::imprint
