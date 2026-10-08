// DRIVE: the shared saturation stage (DriveProcessor). Bypass is exact, the stage is
// near-transparent when barely engaged, bounded, DC-free, alias-poor (oversampled), smooth
// under automation and circuit changes, stereo-coherent and sample-rate consistent.

#include "audio/utility/TestSignals.h"
#include "core/Prng.h"
#include "engine/DriveProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <cstdio>
#include <numbers>
#include <vector>

using namespace osp;

namespace
{
    constexpr std::array<DriveMode, 3> allModes { DriveMode::tube, DriveMode::tape, DriveMode::crunch };
    const char* nameOf (DriveMode m) { return m == DriveMode::tube ? "TUBE" : (m == DriveMode::tape ? "TAPE" : "CRUNCH"); }

    struct Stereo
    {
        std::vector<float> l, r;
    };

    /** Runs a mono or stereo signal through a fresh processor; `during` may change settings per sample. */
    Stereo run (const std::vector<float>& inL, const std::vector<float>& inR, double rate, DriveProcessor::Settings s,
                const std::function<void (DriveProcessor&, std::size_t)>& during = {})
    {
        DriveProcessor d;
        d.setSettings (s);
        d.prepare (rate);
        Stereo out { inL, inR };
        for (std::size_t i = 0; i < inL.size(); ++i)
        {
            if (during)
                during (d, i);
            d.process (out.l[i], out.r[i]);
        }
        return out;
    }
    Stereo run (const std::vector<float>& in, double rate, DriveProcessor::Settings s,
                const std::function<void (DriveProcessor&, std::size_t)>& during = {})
    {
        return run (in, in, rate, s, during);
    }

    DriveProcessor::Settings settings (DriveMode mode, double amount, double tone = 0.5, double body = 0.5)
    {
        DriveProcessor::Settings s;
        s.mode = mode;
        s.amount = amount;
        s.tone = tone;
        s.body = body;
        return s;
    }

    double rms (const std::vector<float>& x, std::size_t from = 0, std::size_t to = 0)
    {
        to = to == 0 ? x.size() : std::min (to, x.size());
        double e = 0.0;
        for (std::size_t i = from; i < to; ++i)
            e += static_cast<double> (x[i]) * x[i];
        return std::sqrt (e / static_cast<double> (std::max<std::size_t> (1, to - from)));
    }

    /** Magnitude of one frequency (Goertzel-style DFT with a Hann window). */
    double magnitude (const std::vector<float>& x, std::size_t from, std::size_t length, double hz, double rate)
    {
        std::complex<double> acc;
        double wsum = 0.0;
        for (std::size_t n = 0; n < length; ++n)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (n) / static_cast<double> (length - 1));
            acc += w * static_cast<double> (x[from + n]) * std::polar (1.0, -2.0 * std::numbers::pi * hz * static_cast<double> (n) / rate);
            wsum += w;
        }
        return 2.0 * std::abs (acc) / wsum;
    }

    std::vector<float> mono (const AudioData& a) { return a.channels[0]; }

    double maxStep (const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double m = 0.0;
        for (std::size_t i = std::max<std::size_t> (from, 1); i < std::min (to, x.size()); ++i)
            m = std::max (m, static_cast<double> (std::abs (x[i] - x[i - 1])));
        return m;
    }

    /** The share of energy that is not the tone's harmonics (aliasing and noise), in dB,
        by projecting out every harmonic below Nyquist (least squares on sines/cosines). */
    double inharmonicDb (const std::vector<float>& x, std::size_t from, std::size_t length, double f0, double rate)
    {
        std::vector<double> residual (length);
        for (std::size_t n = 0; n < length; ++n)
            residual[n] = x[from + n];
        double total = 0.0;
        for (double v : residual)
            total += v * v;
        // Fit each harmonic's sine and cosine (the window is an exact number of periods).
        for (int h = 1; h * f0 < 0.5 * rate; ++h)
        {
            double sc = 0.0, ss = 0.0;
            for (std::size_t n = 0; n < length; ++n)
            {
                const double ph = 2.0 * std::numbers::pi * h * f0 * static_cast<double> (n) / rate;
                sc += residual[n] * std::cos (ph);
                ss += residual[n] * std::sin (ph);
            }
            sc *= 2.0 / static_cast<double> (length);
            ss *= 2.0 / static_cast<double> (length);
            for (std::size_t n = 0; n < length; ++n)
            {
                const double ph = 2.0 * std::numbers::pi * h * f0 * static_cast<double> (n) / rate;
                residual[n] -= sc * std::cos (ph) + ss * std::sin (ph);
            }
        }
        // DC is harmonic 0.
        double mean = 0.0;
        for (double v : residual)
            mean += v;
        mean /= static_cast<double> (length);
        double left = 0.0;
        for (double v : residual)
            left += (v - mean) * (v - mean);
        return 10.0 * std::log10 (std::max (left, 1.0e-30) / std::max (total, 1.0e-30));
    }

    /** ITU-R BS.1770 K-weighting at 48 kHz, then RMS (a loudness estimate for calibration). */
    double kRms (const std::vector<float>& x, std::size_t from)
    {
        double z1 = 0, z2 = 0, w1 = 0, w2 = 0, e = 0;
        std::size_t n = 0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const double in = x[i];
            const double y1 = 1.53512485958697 * in + z1;
            z1 = -2.69169618940638 * in - (-1.69065929318241) * y1 + z2;
            z2 = 1.19839281085285 * in - 0.73248077421585 * y1;
            const double y2 = y1 + w1;
            w1 = -2.0 * y1 - (-1.99004745483398) * y2 + w2;
            w2 = y1 - 0.99007225036621 * y2;
            if (i >= from)
            {
                e += y2 * y2;
                ++n;
            }
        }
        return std::sqrt (e / static_cast<double> (std::max<std::size_t> (1, n)));
    }

    /** Program material at the reference level: a vowel, a saw and a pluck in a chord. */
    std::vector<float> program (double rate, double seconds)
    {
        const auto a = testsignals::vowel (220.0, seconds, rate, 3);
        const auto b = testsignals::saw (164.8, seconds, rate);
        const auto c = testsignals::pluck (329.6, seconds, rate, 5);
        std::vector<float> x (a.channels[0].size());
        double peak = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            x[i] = a.channels[0][i] + 0.5f * b.channels[0][i] + 0.6f * c.channels[0][std::min (i, c.channels[0].size() - 1)];
            peak = std::max (peak, static_cast<double> (std::abs (x[i])));
        }
        for (auto& v : x)
            v = static_cast<float> (v * DriveProcessor::referenceLevel / peak);
        return x;
    }
}

TEST_CASE ("drive: at 0 % the stage is not in the signal path (bit-exact)", "[unit][drive]")
{
    const auto in = program (48000.0, 0.5);
    for (auto mode : allModes)
    {
        const auto out = run (in, 48000.0, settings (mode, 0.0, 0.2, 0.9));
        CHECK (out.l == in);
        CHECK (out.r == in);
    }
    DriveProcessor d;
    d.prepare (48000.0);
    CHECK_FALSE (d.active());
}

TEST_CASE ("drive: the half-band design", "[unit][drive]")
{
    std::array<float, 16> c {};
    const int n1 = DriveProcessor::designHalfband (90.0, 0.04, c.data(), 16);
    CHECK (n1 == 8);
    for (int i = 0; i < n1; ++i)
    {
        CHECK (c[static_cast<std::size_t> (i)] > 0.0f);
        CHECK (c[static_cast<std::size_t> (i)] < 1.0f);
    }
    CHECK (DriveProcessor::designHalfband (80.0, 0.146, c.data(), 16) == 4);
}

TEST_CASE ("drive: barely engaged it is transparent", "[unit][drive]")
{
    // Flat (the oversampling, emphasis pair and smoothing colour nothing); the only change is
    // the saturator's own curvature, a fraction of a dB at this level.
    const double rate = 48000.0;
    for (auto mode : allModes)
    {
        CAPTURE (nameOf (mode));
        std::vector<double> gains;
        for (double hz : { 60.0, 1000.0, 8000.0, 16000.0, 19000.0 })
        {
            const auto in = mono (testsignals::sine (hz, 0.5, rate, 0.2));
            const auto out = run (in, rate, settings (mode, 1.0e-6));
            gains.push_back (20.0 * std::log10 (magnitude (out.l, 12000, 8192, hz, rate) / magnitude (in, 12000, 8192, hz, rate)));
            CHECK (std::abs (gains.back()) < 0.3);
        }
        CHECK (*std::max_element (gains.begin(), gains.end()) - *std::min_element (gains.begin(), gains.end()) < 0.1);
    }
}

TEST_CASE ("drive: silence in, silence out; huge input stays bounded and finite", "[unit][drive]")
{
    for (auto mode : allModes)
        for (double amount : { 0.3, 1.0 })
        {
            CAPTURE (nameOf (mode), amount);
            const auto quiet = run (std::vector<float> (24000, 0.0f), 48000.0, settings (mode, amount));
            CHECK (rms (quiet.l) == 0.0);
            std::vector<float> loud (24000);
            for (std::size_t i = 0; i < loud.size(); ++i)
                loud[i] = (i / 37) % 2 == 0 ? 8.0f : -8.0f;   // +18 dBFS square
            const auto out = run (loud, 48000.0, settings (mode, amount, 1.0, 1.0));
            float peak = 0.0f;
            bool ok = true;
            for (float v : out.l)
            {
                ok = ok && std::isfinite (v);
                peak = std::max (peak, std::abs (v));
            }
            CHECK (ok);
            CHECK (peak < 16.0f);
        }
}

TEST_CASE ("drive: no runaway DC, the bass fundamental is kept", "[unit][drive]")
{
    const double rate = 48000.0;
    for (auto mode : allModes)
    {
        CAPTURE (nameOf (mode));
        const auto in = mono (testsignals::sine (41.2, 3.0, rate, DriveProcessor::referenceLevel));   // E1
        const auto out = run (in, rate, settings (mode, 1.0, 0.5, 1.0));
        double mean = 0.0;
        for (std::size_t i = 72000; i < out.l.size(); ++i)
            mean += out.l[i];
        mean /= static_cast<double> (out.l.size() - 72000);
        CHECK (std::abs (mean) < 0.02 * rms (out.l, 72000));
        const double fundamentalDb = 20.0 * std::log10 (magnitude (out.l, 72000, 65536, 41.2, rate) / magnitude (in, 72000, 65536, 41.2, rate));
        CHECK (fundamentalDb > -6.0);
        CHECK (fundamentalDb < 6.0);
    }
}

TEST_CASE ("drive: oversampling keeps aliasing far down", "[unit][drive]")
{
    // A high note driven hard: everything that is not one of its harmonics is aliasing.
    const double rate = 48000.0, f0 = 48000.0 / 7.0;   // ~6857 Hz, an exact number of periods per window
    for (auto mode : allModes)
    {
        CAPTURE (nameOf (mode));
        const auto in = mono (testsignals::sine (f0, 1.0, rate, DriveProcessor::referenceLevel));
        const auto out = run (in, rate, settings (mode, 1.0));
        const double alias = inharmonicDb (out.l, 24000, 7 * 2000, f0, rate);
        std::printf ("%-7s aliasing at 100 %%, %.0f Hz: %.1f dB\n", nameOf (mode), f0, alias);
        CHECK (alias < -50.0);
    }
}

TEST_CASE ("drive: automation, engaging and circuit changes do not click", "[unit][drive]")
{
    const double rate = 48000.0;
    const auto in = mono (testsignals::sine (220.0, 4.0, rate, DriveProcessor::referenceLevel));
    for (auto mode : allModes)
    {
        CAPTURE (nameOf (mode));
        // Reference: the largest step of the steadily driven tone.
        const auto steady = run (in, rate, settings (mode, 1.0));
        const double normal = maxStep (steady.l, 48000, 96000);
        // DRIVE swept 0 -> 100 -> 0 over 2 s, and fast random automation.
        Prng rng (7);
        const auto swept = run (in, rate, settings (mode, 0.0), [&] (DriveProcessor& d, std::size_t i) {
            const double t = static_cast<double> (i) / rate;
            auto s = settings (mode, t < 2.0 ? 1.0 - std::abs (t - 1.0) : (i / 64 % 2 == 0 ? rng.nextDouble() : 0.0));
            d.setSettings (s);
        });
        CHECK (maxStep (swept.l, 0, swept.l.size()) < 1.6 * normal + 0.01);
        // Circuits switched every 150 ms at 70 %, TONE and BODY moving.
        const auto switched = run (in, rate, settings (mode, 0.7), [&] (DriveProcessor& d, std::size_t i) {
            const auto m = allModes[(static_cast<std::size_t> (mode) + i / 7200) % 3];
            d.setSettings (settings (m, 0.7, 0.5 + 0.5 * std::sin (i * 1.0e-4), 0.5 + 0.5 * std::cos (i * 1.3e-4)));
        });
        CHECK (maxStep (switched.l, 0, switched.l.size()) < 1.6 * normal + 0.01);
    }
}

TEST_CASE ("drive: stereo is kept, deterministic, the same at every sample rate", "[unit][drive]")
{
    for (auto mode : allModes)
    {
        CAPTURE (nameOf (mode));
        const auto in = program (48000.0, 1.0);
        // Identical channels stay identical; one silent channel stays silent.
        const auto both = run (in, 48000.0, settings (mode, 0.8));
        CHECK (both.l == both.r);
        const auto left = run (in, std::vector<float> (in.size(), 0.0f), 48000.0, settings (mode, 0.8));
        CHECK (rms (left.r) == 0.0);
        CHECK (run (in, 48000.0, settings (mode, 0.8)).l == both.l);

        // The same tone at 44.1, 48 and 96 kHz: level and harmonics within a dB.
        std::vector<double> level, h2, h3;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            const auto tone = mono (testsignals::sine (220.0, 1.0, rate, DriveProcessor::referenceLevel));
            const auto out = run (tone, rate, settings (mode, 0.6));
            const auto from = static_cast<std::size_t> (0.5 * rate);
            const auto length = static_cast<std::size_t> (0.4 * rate);
            level.push_back (20.0 * std::log10 (rms (out.l, from)));
            h2.push_back (20.0 * std::log10 (magnitude (out.l, from, length, 440.0, rate) + 1.0e-9));
            h3.push_back (20.0 * std::log10 (magnitude (out.l, from, length, 660.0, rate) + 1.0e-9));
        }
        for (std::size_t i = 1; i < level.size(); ++i)
        {
            CHECK (std::abs (level[i] - level[0]) < 0.5);
            CHECK (std::abs (h2[i] - h2[0]) < 1.5);
            CHECK (std::abs (h3[i] - h3[0]) < 1.5);
        }
    }
}

TEST_CASE ("drive: the transfer curve the popover draws", "[unit][drive]")
{
    for (auto mode : allModes)
    {
        CAPTURE (nameOf (mode));
        // At 0 % it is the identity; driven, it is monotonic and bends (compresses) its peaks.
        CHECK (DriveProcessor::transfer (settings (mode, 0.0), 0.5) == Catch::Approx (0.5).margin (1.0e-3));
        double last = -1.0e9;
        for (int i = -40; i <= 40; ++i)
        {
            const double y = DriveProcessor::transfer (settings (mode, 1.0), i / 20.0);
            CHECK (y >= last - 1.0e-6);
            last = y;
        }
        const double small = DriveProcessor::transfer (settings (mode, 1.0), 0.05) / 0.05;
        const double big = DriveProcessor::transfer (settings (mode, 1.0), 2.0) / 2.0;
        CHECK (big < 0.5 * small);
    }
}

// Calibration and voicing measurements (printed): loudness against DRIVE per circuit,
// harmonic profiles, aliasing and intermodulation. Not run by default.
TEST_CASE ("drive: measurements", "[.][drive-measure]")
{
    const double rate = 48000.0;
    const auto prog = program (rate, 3.0);
    const double dry = kRms (prog, 24000);
    std::printf ("loudness (K-weighted RMS against the clean chord, dB), tone 0.5, body 0 / 0.5 / 1\n");
    for (auto mode : allModes)
        for (double body : { 0.0, 0.5, 1.0 })
        {
            std::printf ("%-7s b%.1f", nameOf (mode), body);
            for (int a = 0; a <= 10; ++a)
            {
                const auto out = run (prog, rate, settings (mode, a / 10.0, 0.5, body));
                std::printf (" %+5.1f", 20.0 * std::log10 (kRms (out.l, 24000) / dry));
            }
            std::printf ("\n");
        }
    std::printf ("\nloudness at TONE / BODY extremes (dB), 65 %% and 100 %%: t0 t1 b0 b1\n");
    for (auto mode : allModes)
        for (double a : { 0.65, 1.0 })
        {
            std::printf ("%-7s %3.0f%% ", nameOf (mode), 100 * a);
            for (auto [t, b] : { std::pair { 0.0, 0.5 }, std::pair { 1.0, 0.5 }, std::pair { 0.5, 0.0 }, std::pair { 0.5, 1.0 } })
                std::printf (" %+5.1f", 20.0 * std::log10 (kRms (run (prog, rate, settings (mode, a, t, b)).l, 24000) / dry));
            std::printf ("\n");
        }
    std::printf ("\ntransients: a pluck's crest factor (peak / RMS, dB) at 0 / 40 / 65 / 100 %%\n");
    {
        auto pluck = testsignals::pluck (196.0, 1.5, rate, 9).channels[0];
        double pk = 0.0;
        for (float v : pluck)
            pk = std::max (pk, static_cast<double> (std::abs (v)));
        for (auto& v : pluck)
            v = static_cast<float> (v * DriveProcessor::referenceLevel / pk);
        for (auto mode : allModes)
        {
            std::printf ("%-7s", nameOf (mode));
            for (double a : { 0.0, 0.4, 0.65, 1.0 })
            {
                const auto out = run (pluck, rate, settings (mode, a));
                double peak = 0.0;
                for (float v : out.l)
                    peak = std::max (peak, static_cast<double> (std::abs (v)));
                std::printf (" %5.1f", 20.0 * std::log10 (peak / rms (out.l)));
            }
            std::printf ("\n");
        }
    }
    std::printf ("\nharmonics of a 220 Hz tone at the reference level (dB re fundamental): H2 H3 H4 H5 H7 H9\n");
    const auto tone = mono (testsignals::sine (220.0, 1.0, rate, DriveProcessor::referenceLevel));
    for (auto mode : allModes)
        for (double a : { 0.15, 0.4, 0.65, 1.0 })
        {
            const auto out = run (tone, rate, settings (mode, a));
            const double f = magnitude (out.l, 24000, 16384, 220.0, rate);
            std::printf ("%-7s %3.0f%%  ", nameOf (mode), 100 * a);
            for (int h : { 2, 3, 4, 5, 7, 9 })
                std::printf (" %6.1f", 20.0 * std::log10 (magnitude (out.l, 24000, 16384, 220.0 * h, rate) / f + 1.0e-12));
            std::printf ("   level %+5.1f dB\n", 20.0 * std::log10 (rms (out.l, 24000) / rms (tone, 24000)));
        }
    std::printf ("\nintermodulation: 50 Hz + 2 kHz (4:1), sidebands 2k+-50 re 2 kHz (dB), at 50 / 100 %%\n");
    std::vector<float> two (static_cast<std::size_t> (rate));
    for (std::size_t i = 0; i < two.size(); ++i)
        two[i] = static_cast<float> (DriveProcessor::referenceLevel * (0.8 * std::sin (2 * std::numbers::pi * 50 * i / rate) + 0.2 * std::sin (2 * std::numbers::pi * 2000 * i / rate)));
    for (auto mode : allModes)
        for (double a : { 0.5, 1.0 })
        {
            const auto out = run (two, rate, settings (mode, a));
            const double c = magnitude (out.l, 12000, 32768, 2000, rate);
            const double sb = magnitude (out.l, 12000, 32768, 2050, rate) + magnitude (out.l, 12000, 32768, 1950, rate);
            std::printf ("%-7s %3.0f%%  IMD %.1f dB\n", nameOf (mode), 100 * a, 20.0 * std::log10 (sb / c));
        }
}
