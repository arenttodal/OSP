#include "engine/EchoDelay.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace osp;
using Catch::Approx;

namespace
{
    constexpr double rate = 48000.0;

    struct Stereo
    {
        std::vector<float> l, r;
    };

    EchoDelay::Settings ascend (AscendInterval interval, AscendMotion motion)
    {
        EchoDelay::Settings s;
        s.type = EchoType::ascend;
        s.sync = false;
        s.timeMs = 300.0;
        s.feedback = 0.6;
        s.tone = 0.6;
        s.age = 0.0;
        s.stereo = EchoStereo::mono;
        s.interval = interval;
        s.motion = motion;
        return s;
    }

    Stereo run (const EchoDelay::Settings& s, const std::vector<float>& in, double seconds)
    {
        EchoDelay echo;
        echo.prepare (rate);
        echo.setSettings (s);
        echo.reset();
        const auto n = static_cast<std::size_t> (seconds * rate);
        Stereo out { std::vector<float> (n), std::vector<float> (n) };
        for (std::size_t i = 0; i < n; ++i)
        {
            const float x = i < in.size() ? in[i] : 0.0f;
            echo.process (x, x, out.l[i], out.r[i]);
        }
        return out;
    }

    std::vector<float> sineBurst (double hz, double seconds, float level = 0.5f)
    {
        std::vector<float> x (static_cast<std::size_t> (seconds * rate));
        const auto fade = static_cast<std::size_t> (0.005 * rate);
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const double edge = std::min (1.0, std::min (static_cast<double> (i), static_cast<double> (x.size() - 1 - i)) / static_cast<double> (fade));
            x[i] = level * static_cast<float> (edge * std::sin (2.0 * std::numbers::pi * hz * static_cast<double> (i) / rate));
        }
        return x;
    }

    /** The pitch (Hz) in [from, from + length) seconds, from the zero crossings (a dual-head
        shifter's output is the interval's pitch under a grain-rate shimmer that a spectrum
        smears into side lines; the crossings count the carrier). */
    double dominant (const std::vector<float>& x, double from, double length)
    {
        const auto a = static_cast<std::size_t> (from * rate), n = static_cast<std::size_t> (length * rate);
        std::size_t first = 0, last = 0;
        int crossings = 0;
        for (std::size_t i = a + 1; i < a + n; ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                if (crossings == 0)
                    first = i;
                last = i;
                ++crossings;
            }
        return crossings < 2 ? 0.0 : (crossings - 1) * rate / static_cast<double> (last - first);
    }

    double rms (const std::vector<float>& x, double from, double length)
    {
        const auto a = static_cast<std::size_t> (from * rate), n = static_cast<std::size_t> (length * rate);
        double sum = 0.0;
        for (std::size_t i = 0; i < n; ++i)
            sum += static_cast<double> (x[a + i]) * x[a + i];
        return std::sqrt (sum / static_cast<double> (n));
    }

    /** Repeat k (1-based) of a 300 ms echo: its steady middle, after the shifter's latency. */
    double repeatStart (int k) { return k * 0.3 + 0.05 * k; }
}

TEST_CASE ("echo ascend: RISE's first repeat is an octave up, the second two", "[unit][echo][ascend]")
{
    const auto out = run (ascend (AscendInterval::octaveUp, AscendMotion::rise), sineBurst (330.0, 0.2), 1.2);
    CHECK (dominant (out.l, repeatStart (1), 0.08) == Approx (660.0).epsilon (0.01));
    CHECK (dominant (out.l, repeatStart (2), 0.08) == Approx (1320.0).epsilon (0.01));
}

TEST_CASE ("echo ascend: STEADY keeps every repeat at the interval; LATE starts at the played pitch", "[unit][echo][ascend]")
{
    const auto steady = run (ascend (AscendInterval::octaveUp, AscendMotion::steady), sineBurst (330.0, 0.2), 1.2);
    CHECK (dominant (steady.l, repeatStart (1), 0.08) == Approx (660.0).epsilon (0.01));
    CHECK (dominant (steady.l, 2 * 0.3 + 0.05, 0.08) == Approx (660.0).epsilon (0.01));
    const auto late = run (ascend (AscendInterval::octaveUp, AscendMotion::late), sineBurst (330.0, 0.2), 1.2);
    CHECK (dominant (late.l, 0.3 + 0.03, 0.08) == Approx (330.0).epsilon (0.01));
    CHECK (dominant (late.l, 0.6 + 0.06, 0.08) == Approx (660.0).epsilon (0.01));
}

TEST_CASE ("echo ascend: INTERVAL sets the shift (+7, +5, -12)", "[unit][echo][ascend]")
{
    const std::pair<AscendInterval, double> cases[] = {
        { AscendInterval::fifthUp, 440.0 * std::exp2 (7.0 / 12.0) },
        { AscendInterval::fourthUp, 440.0 * std::exp2 (5.0 / 12.0) },
        { AscendInterval::octaveDown, 220.0 },
    };
    for (const auto& [interval, expected] : cases)
    {
        const auto out = run (ascend (interval, AscendMotion::rise), sineBurst (440.0, 0.2), 0.8);
        INFO ("interval " << static_cast<int> (interval));
        CHECK (dominant (out.l, repeatStart (1), 0.08) == Approx (expected).epsilon (0.01));
    }
    CHECK (EchoDelay::intervalRatio (AscendInterval::octaveUp) == 2.0);
}

TEST_CASE ("echo ascend: CLOCK, the window and TONE darken; the repeat stays as loud as TAPE's", "[unit][echo][ascend]")
{
    CHECK (EchoDelay::clockHz (0.0) == Approx (13000.0));
    CHECK (EchoDelay::clockHz (1.0) == Approx (7000.0));
    CHECK (EchoDelay::shifterWindow (0.3) == Approx (0.0375));
    CHECK (EchoDelay::shifterWindow (0.1) == Approx (0.03));
    CHECK (EchoDelay::shifterWindow (1.0) == Approx (0.05));

    // A 3 kHz tone through STEADY: TONE's low-pass (2 kHz at 0, 5 kHz at 1) acts before the
    // shifter.
    auto level = [] (double tone, double age) {
        auto s = ascend (AscendInterval::octaveUp, AscendMotion::steady);
        s.tone = tone;
        s.age = age;
        const auto out = run (s, sineBurst (3000.0, 0.2), 0.6);
        return rms (out.l, repeatStart (1), 0.08);
    };
    CHECK (level (0.0, 0.0) < 0.8 * level (1.0, 0.0));

    // ASCEND's first repeat of a low tone (well below TONE) is within 3 dB of TAPE's.
    auto tape = ascend (AscendInterval::octaveUp, AscendMotion::steady);
    tape.type = EchoType::tape;
    tape.age = 0.0;
    const double tapeLevel = rms (run (tape, sineBurst (200.0, 0.2), 0.6).l, 0.33, 0.12);
    const double ascendLevel = rms (run (ascend (AscendInterval::octaveUp, AscendMotion::steady), sineBurst (200.0, 0.2), 0.6).l, repeatStart (1), 0.08);
    INFO ("tape " << tapeLevel << " ascend " << ascendLevel);
    CHECK (ascendLevel / tapeLevel == Approx (1.0).margin (0.3));
}

TEST_CASE ("echo ascend: FEEDBACK 100 % never overloads, in every motion, interval and stereo picture", "[unit][echo][ascend]")
{
    std::vector<float> noise (24000);
    Prng rng (11);
    for (auto& v : noise)
        v = 0.8f * static_cast<float> (rng.bipolar());
    for (int m = 0; m < 3; ++m)
        for (int i = 0; i < 4; ++i)
            for (int st = 0; st < 3; ++st)
            {
                auto s = ascend (static_cast<AscendInterval> (i), static_cast<AscendMotion> (m));
                s.feedback = 1.0;
                s.age = 1.0;
                s.stereo = static_cast<EchoStereo> (st);
                const auto out = run (s, noise, 6.0);
                float peak = 0.0f;
                for (const auto* ch : { &out.l, &out.r })
                    for (float v : *ch)
                    {
                        REQUIRE (std::isfinite (v));
                        peak = std::max (peak, std::abs (v));
                    }
                INFO ("motion " << m << " interval " << i << " stereo " << st << " peak " << peak);
                CHECK (peak < 2.0f);
            }
}

TEST_CASE ("echo ascend: the same input gives the same output (no randomness), and silence stays silent", "[unit][echo][ascend][regression]")
{
    const auto s = ascend (AscendInterval::octaveUp, AscendMotion::rise);
    const auto a = run (s, sineBurst (523.25, 0.15), 1.0), b = run (s, sineBurst (523.25, 0.15), 1.0);
    CHECK (a.l == b.l);
    CHECK (a.r == b.r);
    const auto quiet = run (s, {}, 0.5);
    for (float v : quiet.l)
        REQUIRE (v == 0.0f);
}
TEST_CASE ("echo ascend: spliced in phase, the octave is in tune whatever the note", "[unit][echo][ascend]")
{
    // A plain dual-head shifter drifts by up to a grain's rate (tens of cents) depending on
    // how the note's period meets the window; the aligned splice keeps every note in tune.
    for (double hz : { 196.0, 261.63, 293.66, 349.23, 466.16, 587.33, 739.99 })
    {
        const auto out = run (ascend (AscendInterval::octaveUp, AscendMotion::steady), sineBurst (hz, 0.2), 0.7);
        const double cents = 1200.0 * std::log2 (dominant (out.l, repeatStart (1), 0.08) / (2.0 * hz));
        INFO (hz << " Hz: " << cents << " cents");
        CHECK (std::abs (cents) < 8.0);
    }
}
