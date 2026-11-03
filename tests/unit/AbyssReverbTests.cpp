#include "core/Prng.h"
#include "engine/AbyssReverb.h"
#include "engine/SpaceReverb.h"
#include "support/AllocationCounter.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <vector>

using namespace osp;

namespace
{
    constexpr double rate = 48000.0;

    AbyssReverb::Settings open()
    {
        AbyssReverb::Settings s;
        s.lowCutHz = 20.0;
        s.highCutHz = 20000.0;
        s.damping = 0.0;
        s.preDelayMs = 0.0;
        return s;
    }

    struct Stereo
    {
        std::vector<float> l, r;
    };

    /** `input(i)` into a fresh ABYSS for `seconds`; `during(i, reverb)` may change it as it plays. */
    template <typename Input, typename During>
    Stereo play (const AbyssReverb::Settings& s, double seconds, Input input, During during)
    {
        AbyssReverb reverb;
        reverb.prepare (rate);
        reverb.configure (s);
        reverb.reset();
        const auto n = static_cast<std::size_t> (seconds * rate);
        Stereo out { std::vector<float> (n), std::vector<float> (n) };
        for (std::size_t i = 0; i < n; ++i)
        {
            during (i, reverb);
            const float x = input (i);
            reverb.process (x, x, out.l[i], out.r[i]);
        }
        return out;
    }

    Stereo impulse (const AbyssReverb::Settings& s, double seconds)
    {
        return play (s, seconds, [] (std::size_t i) { return i == 0 ? 1.0f : 0.0f; }, [] (std::size_t, AbyssReverb&) {});
    }

    /** RT60 from the impulse response's Schroeder curve: the -5..-25 dB slope, extrapolated. */
    double rt60 (const std::vector<float>& x)
    {
        std::vector<double> edc (x.size());
        double acc = 0.0;
        for (std::size_t i = x.size(); i-- > 0;)
        {
            acc += static_cast<double> (x[i]) * x[i];
            edc[i] = acc;
        }
        const double total = edc[0];
        double t5 = -1.0, t25 = -1.0;
        for (std::size_t i = 0; i < edc.size(); ++i)
        {
            const double db = 10.0 * std::log10 (std::max (1.0e-30, edc[i] / total));
            if (t5 < 0.0 && db <= -5.0)
                t5 = static_cast<double> (i) / rate;
            if (t25 < 0.0 && db <= -25.0)
            {
                t25 = static_cast<double> (i) / rate;
                break;
            }
        }
        return t25 > t5 && t5 >= 0.0 ? 3.0 * (t25 - t5) : -1.0;
    }

    double energy (const std::vector<float>& x, double from, double to)
    {
        double e = 0.0;
        for (auto i = static_cast<std::size_t> (from * rate); i < static_cast<std::size_t> (to * rate) && i < x.size(); ++i)
            e += static_cast<double> (x[i]) * x[i];
        return e;
    }

    double peakTime (const std::vector<float>& x)
    {
        // Where the envelope (50 ms windows) is highest.
        const auto window = static_cast<std::size_t> (0.05 * rate);
        double best = -1.0, at = 0.0;
        for (std::size_t start = 0; start + window <= x.size(); start += window)
        {
            const double e = energy (x, static_cast<double> (start) / rate, static_cast<double> (start + window) / rate);
            if (e > best)
            {
                best = e;
                at = static_cast<double> (start) / rate;
            }
        }
        return at;
    }
}

TEST_CASE ("abyss: DECAY sets the reverberation time, GRAVITY lengthens it", "[unit][abyss]")
{
    for (const double decay : { 1.5, 4.0 })
    {
        auto s = open();
        s.decaySeconds = decay;
        const auto ir = impulse (s, 3.0 * decay + 2.0);
        const double t = rt60 (ir.l);
        INFO ("DECAY " << decay << " s: measured RT60 " << t << " s");
        CHECK (t > 0.75 * decay);
        CHECK (t < 1.35 * decay);
    }
    auto s = open();
    s.decaySeconds = 2.0;
    s.gravity = 1.0;
    const double heavy = rt60 (impulse (s, 12.0).l);
    INFO ("GRAVITY +100 %, DECAY 2 s: " << heavy << " s");
    CHECK (heavy > 2.0 * 2.0);
}

TEST_CASE ("abyss: GRAVITY below 0 turns the envelope over (it swells)", "[unit][abyss]")
{
    auto s = open();
    s.decaySeconds = 3.0;
    const double natural = peakTime (impulse (s, 4.0).l);
    s.gravity = -1.0;
    const double inverse = peakTime (impulse (s, 4.0).l);
    INFO ("envelope peak: natural " << natural << " s, inverse " << inverse << " s");
    CHECK (natural < 0.2);
    CHECK (inverse > 0.5);
}

TEST_CASE ("abyss: RESONANCE lets the space ring longer", "[unit][abyss]")
{
    auto s = open();
    s.decaySeconds = 2.0;
    const double plain = rt60 (impulse (s, 10.0).l);
    s.resonance = 1.0;
    const double ringing = rt60 (impulse (s, 10.0).l);
    INFO ("RT60 plain " << plain << " s, resonant " << ringing << " s");
    CHECK (ringing > 1.4 * plain);
}

TEST_CASE ("abyss: FREEZE holds the space and lets it go again", "[unit][abyss]")
{
    auto s = open();
    s.decaySeconds = 2.0;
    Prng rng { 7 };
    const auto in2s = static_cast<std::size_t> (2.0 * rate);
    const auto out = play (
        s, 30.0, [&] (std::size_t i) { return i < in2s ? static_cast<float> (0.3 * rng.bipolar()) : 0.0f; },
        [&] (std::size_t i, AbyssReverb& r) {
            if (i == in2s - 2400 || i == static_cast<std::size_t> (22.0 * rate))
            {
                auto t = r.settings();
                t.freeze = i < in2s;   // on just before the input stops, off at 22 s
                r.tune (t);
            }
        });
    const double early = energy (out.l, 4.0, 6.0), late = energy (out.l, 19.0, 21.0), released = energy (out.l, 28.0, 30.0);
    const double heldDb = 10.0 * std::log10 (late / early), releasedDb = 10.0 * std::log10 (released / late);
    INFO ("held 4..6 s -> 19..21 s: " << heldDb << " dB; released 6 s later: " << releasedDb << " dB");
    CHECK (std::abs (heldDb) < 1.5);
    CHECK (releasedDb < -40.0);
}

TEST_CASE ("abyss: never runs away (extremes, FREEZE under loud input) and is deterministic", "[unit][abyss]")
{
    auto s = open();
    s.decaySeconds = 8.0;
    s.size = 1.0;
    s.gravity = 1.0;
    s.resonance = 1.0;
    s.modulation = 1.0;
    for (const bool freeze : { false, true })
    {
        Prng rng { 3 };
        AbyssReverb reverb;
        reverb.prepare (rate);
        reverb.configure (s);
        reverb.reset();
        float peak = 0.0f;
        bool finite = true;
        for (int i = 0; i < static_cast<int> (40.0 * rate); ++i)
        {
            if (freeze && i == static_cast<int> (5.0 * rate))
            {
                auto t = reverb.settings();   // (5 s of loud input in, then held while it goes on)
                t.freeze = true;
                reverb.tune (t);
            }
            const auto x = static_cast<float> (0.9 * rng.bipolar());
            float l = 0.0f, r = 0.0f;
            reverb.process (x, x, l, r);
            finite = finite && std::isfinite (l) && std::isfinite (r);
            peak = std::max ({ peak, std::abs (l), std::abs (r) });
        }
        INFO ("FREEZE " << freeze << ": peak " << peak);
        CHECK (finite);
        CHECK (reverb.guardEvents() == 0u);
        CHECK (peak < 4.0f);
    }
    auto noise = [] (std::uint64_t seed) {
        return [rng = Prng { seed }] (std::size_t) mutable { return static_cast<float> (0.3 * rng.bipolar()); };
    };
    s = open();
    s.gravity = -0.6;
    const auto a = play (s, 3.0, noise (11), [] (std::size_t, AbyssReverb&) {});
    const auto b = play (s, 3.0, noise (11), [] (std::size_t, AbyssReverb&) {});
    CHECK (a.l == b.l);
    CHECK (a.r == b.r);
}

TEST_CASE ("abyss: plays without allocating, and what it costs against HALL", "[unit][abyss][realtime]")
{
    AbyssReverb abyss;
    abyss.prepare (rate);
    auto s = open();
    s.gravity = -0.5;
    s.resonance = 0.4;
    abyss.configure (s);
    abyss.reset();
    SpaceReverb hall;
    hall.prepare (rate);
    SpaceReverb::Settings h;
    h.type = SpaceType::hall;
    hall.configure (h);
    hall.reset();
    Prng rng { 5 };
    const int n = static_cast<int> (5.0 * rate);
    std::vector<float> input (static_cast<std::size_t> (n));
    for (auto& x : input)
        x = static_cast<float> (0.3 * rng.bipolar());
    float l = 0.0f, r = 0.0f, sink = 0.0f;
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    {
        osp::test::AllocationScope scope;
        for (int i = 0; i < n; ++i)
        {
            abyss.process (input[static_cast<std::size_t> (i)], input[static_cast<std::size_t> (i)], l, r);
            sink += l;
            if (i == n / 2)
            {
                auto t = abyss.settings();
                t.freeze = true;
                t.gravity = 0.8;
                abyss.tune (t);   // (tune is real-time too)
            }
        }
        CHECK (scope.count() == 0);
    }
    const auto t1 = Clock::now();
    for (int i = 0; i < n; ++i)
    {
        hall.process (input[static_cast<std::size_t> (i)], input[static_cast<std::size_t> (i)], l, r);
        sink += l;
    }
    const auto t2 = Clock::now();
    const double abyssMs = std::chrono::duration<double, std::milli> (t1 - t0).count();
    const double hallMs = std::chrono::duration<double, std::milli> (t2 - t1).count();
    WARN ("CPU, 5 s at 48 kHz: ABYSS " << abyssMs << " ms, HALL " << hallMs << " ms (x" << abyssMs / std::max (1.0e-3, hallMs) << ")");
    CHECK (std::isfinite (sink));
}

TEST_CASE ("abyss: as loud as HALL at the same send (noise in, the tail out)", "[unit][abyss]")
{
    Prng rngA { 9 }, rngB { 9 };
    auto s = open();
    s.decaySeconds = 3.0;
    const auto abyss = play (s, 6.0, [&] (std::size_t) { return static_cast<float> (0.3 * rngA.bipolar()); }, [] (std::size_t, AbyssReverb&) {});
    SpaceReverb hall;
    hall.prepare (rate);
    SpaceReverb::Settings h;
    h.type = SpaceType::hall;
    h.decaySeconds = 3.0;
    h.lowCutHz = 20.0;
    h.highCutHz = 20000.0;
    h.damping = 0.0;
    hall.configure (h);
    hall.reset();
    std::vector<float> hl (abyss.l.size());
    for (auto& y : hl)
    {
        const auto x = static_cast<float> (0.3 * rngB.bipolar());
        float r = 0.0f;
        hall.process (x, x, y, r);
    }
    const double db = 10.0 * std::log10 (energy (abyss.l, 2.0, 6.0) / energy (hl, 2.0, 6.0));
    INFO ("ABYSS against HALL: " << db << " dB");
    CHECK (std::abs (db) < 3.0);
}

TEST_CASE ("abyss: the variants (DEEP, DARK, INVERSE, ENDLESS) keep their promises and their level", "[unit][abyss]")
{
    auto brightness = [] (const std::vector<float>& x, double from, double to) {
        double d = 0.0, e = 0.0;
        for (auto i = static_cast<std::size_t> (from * rate) + 1; i < static_cast<std::size_t> (to * rate) && i < x.size(); ++i)
        {
            const double v = static_cast<double> (x[i]) - x[i - 1];
            d += v * v;
            e += static_cast<double> (x[i]) * x[i];
        }
        return d / std::max (1.0e-30, e);
    };
    auto withVariant = [] (AbyssVariant v) {
        auto s = open();
        s.variant = v;
        return s;
    };
    CHECK (AbyssReverb::Settings {}.variant == AbyssVariant::deep);   // (DEEP is ABYSS as it was)

    // DARK: a much darker tail than DEEP (heard once the input stops: the loop's own colour).
    Prng a { 21 }, b { 21 };
    const auto oneSecond = static_cast<std::size_t> (rate);
    const auto deep = play (withVariant (AbyssVariant::deep), 4.0, [&] (std::size_t i) { return i < oneSecond ? static_cast<float> (0.3 * a.bipolar()) : 0.0f; }, [] (std::size_t, AbyssReverb&) {});
    const auto dark = play (withVariant (AbyssVariant::dark), 4.0, [&] (std::size_t i) { return i < oneSecond ? static_cast<float> (0.3 * b.bipolar()) : 0.0f; }, [] (std::size_t, AbyssReverb&) {});
    const double deepBright = brightness (deep.l, 2.0, 4.0), darkBright = brightness (dark.l, 2.0, 4.0);
    INFO ("brightness DEEP " << deepBright << ", DARK " << darkBright);
    CHECK (darkBright < 0.6 * deepBright);

    // INVERSE: it swells with GRAVITY at 0 (DEEP peaks at once).
    auto inverse = withVariant (AbyssVariant::inverse);
    inverse.decaySeconds = 3.0;
    const double swell = peakTime (impulse (inverse, 5.0).l);
    INFO ("INVERSE's envelope peaks at " << swell << " s");
    CHECK (swell > 0.8);

    // ENDLESS: rings far longer than DEEP at the same DECAY.
    auto endless = withVariant (AbyssVariant::endless);
    endless.decaySeconds = 1.0;
    auto plain = withVariant (AbyssVariant::deep);
    plain.decaySeconds = 1.0;
    const double endlessRt = rt60 (impulse (endless, 16.0).l), deepRt = rt60 (impulse (plain, 6.0).l);
    INFO ("RT60 at DECAY 1 s: DEEP " << deepRt << " s, ENDLESS " << endlessRt << " s");
    CHECK (endlessRt > 4.0 * deepRt);

    // Every variant: as loud as HALL at the same send (within 3 dB) and never running away.
    SpaceReverb hall;
    hall.prepare (rate);
    SpaceReverb::Settings h;
    h.type = SpaceType::hall;
    h.decaySeconds = 3.0;
    h.lowCutHz = 20.0;
    h.highCutHz = 20000.0;
    h.damping = 0.0;
    hall.configure (h);
    hall.reset();
    Prng hn { 9 };
    std::vector<float> hl (static_cast<std::size_t> (6.0 * rate));
    for (auto& y : hl)
    {
        const auto x = static_cast<float> (0.3 * hn.bipolar());
        float r = 0.0f;
        hall.process (x, x, y, r);
    }
    for (const auto v : { AbyssVariant::deep, AbyssVariant::dark, AbyssVariant::inverse, AbyssVariant::endless })
    {
        auto s = withVariant (v);
        s.decaySeconds = 3.0;
        Prng n { 9 };
        const auto out = play (s, 6.0, [&] (std::size_t) { return static_cast<float> (0.3 * n.bipolar()); }, [] (std::size_t, AbyssReverb&) {});
        const double db = 10.0 * std::log10 (energy (out.l, 2.0, 6.0) / energy (hl, 2.0, 6.0));
        INFO ("variant " << static_cast<int> (v) << " against HALL: " << db << " dB");
        CHECK (std::abs (db) < 3.0);
        auto loud = withVariant (v);
        loud.decaySeconds = 8.0;
        loud.size = 1.0;
        loud.resonance = 1.0;
        Prng m { 33 };
        float peak = 0.0f;
        bool finite = true;
        AbyssReverb reverb;
        reverb.prepare (rate);
        reverb.configure (loud);
        reverb.reset();
        for (int i = 0; i < static_cast<int> (20.0 * rate); ++i)
        {
            const auto x = static_cast<float> (0.9 * m.bipolar());
            float l = 0.0f, r = 0.0f;
            reverb.process (x, x, l, r);
            finite = finite && std::isfinite (l) && std::isfinite (r);
            peak = std::max ({ peak, std::abs (l), std::abs (r) });
        }
        CHECK (finite);
        CHECK (reverb.guardEvents() == 0u);
        CHECK (peak < 6.0f);
    }
}
