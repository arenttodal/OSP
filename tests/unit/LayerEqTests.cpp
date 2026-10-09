// The layer EQ: what is drawn is what is heard (measured gains against responseDb at four
// sample rates), off and all-bands-off are bit-exact, extremes stay finite and bounded,
// switching and automation are click-free, and the engine applies it per layer (modulation
// included) while sessions without it render exactly as before.

#include "audio/utility/TestSignals.h"
#include "core/Prng.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "engine/LayerEq.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace osp;

namespace
{
    /** Steady-state gain (dB) of the processor for a sine at `hz` (the last half of 0.5 s). */
    double measuredDb (const eq::Settings& settings, double hz, double sampleRate)
    {
        eq::Processor p;
        p.prepare (sampleRate);
        p.setTarget (settings);
        const int n = static_cast<int> (0.5 * sampleRate);
        std::vector<float> l (static_cast<std::size_t> (n)), r (static_cast<std::size_t> (n));
        for (int i = 0; i < n; ++i)
            l[static_cast<std::size_t> (i)] = r[static_cast<std::size_t> (i)] = static_cast<float> (0.25 * std::sin (2.0 * std::numbers::pi * hz * i / sampleRate));
        for (int done = 0; done < n; done += 512)
            p.process (l.data() + done, r.data() + done, std::min (512, n - done), false);
        double in = 0.0, out = 0.0;
        for (int i = n / 2; i < n; ++i)
        {
            const double x = 0.25 * std::sin (2.0 * std::numbers::pi * hz * i / sampleRate);
            in += x * x;
            out += static_cast<double> (l[static_cast<std::size_t> (i)]) * l[static_cast<std::size_t> (i)];
        }
        return 10.0 * std::log10 (out / in);
    }

    eq::Settings with (eq::Band band, double hz, double gainDb = 0.0, double q = 0.707, bool steep = false)
    {
        eq::Settings s;
        s.enabled = true;
        auto& b = s.bands[static_cast<std::size_t> (band)];
        b.enabled = true;
        b.frequencyHz = hz;
        b.gainDb = gainDb;
        b.q = q;
        b.steep = steep;
        return s;
    }
}

TEST_CASE ("eq: the drawn response is the heard response (every band, 44.1 - 96 kHz)", "[unit][eq]")
{
    std::vector<eq::Settings> cases {
        with (eq::Band::highPass, 90.0), with (eq::Band::highPass, 90.0, 0.0, 0.707, true),
        with (eq::Band::lowShelf, 200.0, 6.0), with (eq::Band::bell, 3200.0, -4.0, 2.0),
        with (eq::Band::bell, 1000.0, 18.0, 12.0), with (eq::Band::highShelf, 5000.0, -9.0, 1.2),
        with (eq::Band::lowPass, 12000.0), with (eq::Band::lowPass, 2000.0, 0.0, 0.707, true),
    };
    // Every band at once.
    auto all = with (eq::Band::highPass, 60.0);
    all.bands[1] = { true, 180.0, 4.0, 0.707, false };
    all.bands[2] = { true, 2500.0, -6.0, 3.0, false };
    all.bands[3] = { true, 8000.0, 3.0, 0.707, false };
    all.bands[4] = { true, 16000.0, 0.0, 0.707, true };
    cases.push_back (all);
    for (double sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        for (const auto& s : cases)
            for (double hz : { 50.0, 90.0, 200.0, 1000.0, 3200.0, 7000.0, 15000.0 })
            {
                const double drawn = eq::responseDb (s, hz, sampleRate);
                if (drawn < -40.0)
                    continue;   // deep in a stop band the measurement is the noise floor
                INFO ("rate " << sampleRate << " hz " << hz);
                CHECK (measuredDb (s, hz, sampleRate) == Catch::Approx (drawn).margin (0.05));
            }
    // The reference points: a bell is its gain at its centre, a shelf half its gain at its corner,
    // a 12 / 24 dB/oct pass -3 dB at its cutoff.
    CHECK (eq::responseDb (with (eq::Band::bell, 3200.0, -4.0, 2.0), 3200.0, 48000.0) == Catch::Approx (-4.0).margin (1.0e-6));
    CHECK (eq::responseDb (with (eq::Band::lowShelf, 200.0, 6.0), 200.0, 48000.0) == Catch::Approx (3.0).margin (0.05));
    CHECK (eq::responseDb (with (eq::Band::highPass, 90.0), 90.0, 48000.0) == Catch::Approx (-3.01).margin (0.02));
    CHECK (eq::responseDb (with (eq::Band::highPass, 90.0, 0.0, 0.707, true), 90.0, 48000.0) == Catch::Approx (-3.01).margin (0.02));
    CHECK (eq::responseDb (with (eq::Band::highPass, 90.0, 0.0, 0.707, true), 45.0, 48000.0) < -23.0);   // two octaves of 24 dB/oct
}

TEST_CASE ("eq: off, or on with every band off, never touches the signal", "[unit][eq]")
{
    for (bool enabled : { false, true })
    {
        eq::Settings s;
        s.enabled = enabled;
        eq::Processor p;
        p.prepare (48000.0);
        p.setTarget (s);
        CHECK_FALSE (p.active());
        CHECK_FALSE (s.audible());
        CHECK (eq::responseDb (s, 1000.0, 48000.0) == 0.0);
    }
    // Off again after use: once faded it is out of the path.
    eq::Processor p;
    p.prepare (48000.0);
    p.setTarget (with (eq::Band::bell, 1000.0, 6.0));
    std::vector<float> l (4800, 0.1f), r (4800, 0.1f);
    p.process (l.data(), r.data(), 4800, false);
    CHECK (p.active());
    p.setTarget (eq::Settings {});
    p.process (l.data(), r.data(), 4800, false);   // the 10 ms fade
    CHECK_FALSE (p.active());
}

TEST_CASE ("eq: extremes and rapid automation stay finite and bounded", "[unit][eq]")
{
    for (double sampleRate : { 44100.0, 96000.0 })
    {
        eq::Processor p;
        p.prepare (sampleRate);
        Prng rng (Prng::deriveSeed (7, 0, 0));
        std::vector<float> l (64), r (64);
        float peak = 0.0f;
        bool finite = true;
        double phase = 0.0;
        for (int block = 0; block < 3000; ++block)
        {
            // New random settings every block: every band, any frequency (also near Nyquist),
            // full gain, the extreme Qs, both slopes, the switches.
            eq::Settings s;
            s.enabled = rng.nextDouble() > 0.05;
            for (int b = 0; b < eq::bandCount; ++b)
            {
                auto& band = s.bands[static_cast<std::size_t> (b)];
                const auto range = eq::frequencyRange (static_cast<eq::Band> (b));
                band.enabled = rng.nextDouble() > 0.2;
                band.frequencyHz = range.minHz * std::pow (range.maxHz / range.minHz, rng.nextDouble());
                band.gainDb = rng.nextDouble() > 0.5 ? eq::maxGainDb : -eq::maxGainDb;
                band.q = rng.nextDouble() > 0.5 ? eq::maxBellQ : eq::minBellQ;
                band.steep = rng.nextDouble() > 0.5;
            }
            p.setTarget (s);
            for (std::size_t i = 0; i < l.size(); ++i)
            {
                phase += 2.0 * std::numbers::pi * (100.0 + 9000.0 * rng.nextDouble()) / sampleRate;
                l[i] = r[i] = static_cast<float> (0.25 * std::sin (phase));
            }
            p.process (l.data(), r.data(), static_cast<int> (l.size()), false);
            for (std::size_t i = 0; i < l.size(); ++i)
            {
                finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                peak = std::max (peak, std::abs (l[i]));
                CHECK (l[i] == r[i]);   // identical channels stay identical (stereo coherent)
            }
        }
        CHECK (finite);
        CHECK (peak < 0.25f * 8.0f * 4.0f);   // at most +18 dB on three bands at once, plus resonance
    }
}

TEST_CASE ("eq: switching a band and moving it are click-free", "[unit][eq]")
{
    // A 220 Hz sine through a bell switched on and its frequency jumped: the output's
    // sample-to-sample step never jumps past what the sine itself does (plus a little).
    eq::Processor p;
    p.prepare (48000.0);
    auto s = with (eq::Band::bell, 220.0, 12.0, 1.0);
    s.bands[2].enabled = false;
    p.setTarget (s);
    const double sampleRate = 48000.0;
    std::vector<float> out;
    for (int block = 0; block < 300; ++block)
    {
        if (block == 50)
            s.bands[2].enabled = true;
        if (block == 150)
            s.bands[2].frequencyHz = 8000.0;   // a jump of five octaves: it glides
        if (block == 220)
            s.bands[2].gainDb = -18.0;
        p.setTarget (s);
        std::vector<float> l (128), r (128);
        for (int i = 0; i < 128; ++i)
            l[static_cast<std::size_t> (i)] = r[static_cast<std::size_t> (i)]
                = static_cast<float> (0.2 * std::sin (2.0 * std::numbers::pi * 220.0 * (block * 128 + i) / sampleRate));
        p.process (l.data(), r.data(), 128, false);
        out.insert (out.end(), l.begin(), l.end());
    }
    double worst = 0.0;
    for (std::size_t i = 2; i < out.size(); ++i)
    {
        // Second difference: a click is a kink, a sine's is tiny (w^2 x amplitude).
        const double d2 = std::abs (static_cast<double> (out[i]) - 2.0 * out[i - 1] + out[i - 2]);
        worst = std::max (worst, d2);
    }
    const double w = 2.0 * std::numbers::pi * 220.0 / sampleRate;
    // Within twice the loudest (+12 dB) sine's own curvature: the level gliding as the bell
    // slides off the sine bends it a little; a click (as a direct-form biquad redesigned under a
    // moving gain gave: 8 x) would not pass.
    CHECK (worst < 0.2 * 4.0 * w * w * 2.0);
}

TEST_CASE ("eq: the engine filters each layer; without it the engine is unchanged; LFO routes move it", "[unit][eq]")
{
    constexpr double rate = 48000.0;
    auto audio = testsignals::saw (220.0, 2.0, rate);
    testsignals::applyFades (audio, 0.01, 0.05);
    const auto model = instrument::buildComplete (audio, test::analyse (audio), {}, false);
    EngineSettings settings;
    settings.macros.life = 0.0;
    settings.macros.space = 0.0;
    settings.macros.motion = 0.0;
    settings.shaping = Shaping::neutral();
    auto run = [&] (const EngineSettings& es, const mod::Settings* m = nullptr) {
        InstrumentEngine e;
        e.prepare (rate, 256, es);
        e.setModel (model.get());
        if (m != nullptr)
            e.setModulation (*m);
        std::vector<float> l (256), r (256), out;
        e.noteOn (57, 100);
        for (int done = 0; done < static_cast<int> (rate); done += 256)
        {
            float* ch[2] = { l.data(), r.data() };
            e.render (ch, 2, 256);
            out.insert (out.end(), l.begin(), l.end());
        }
        return out;
    };
    const auto plain = run (settings);
    auto withOff = settings;
    withOff.layer[0].eq.enabled = true;   // on, but every band off: untouched
    CHECK (run (withOff) == plain);
    auto lowPassed = settings;
    lowPassed.layer[0].eq = with (eq::Band::lowPass, 1000.0, 0.0, 0.707, true);
    const auto dark = run (lowPassed);
    CHECK (dark != plain);
    // A low pass darkens the saw: its first difference (the high partials) shrinks against the
    // signal.
    auto brightness = [] (const std::vector<float>& x) {
        double d = 0.0, e = 0.0;
        for (std::size_t i = 1; i < x.size(); ++i)
        {
            d += std::abs (x[i] - x[i - 1]);
            e += std::abs (x[i]);
        }
        return d / std::max (1.0e-9, e);
    };
    CHECK (brightness (dark) < 0.85 * brightness (plain));
    // Layer B's EQ does not touch layer A.
    auto other = settings;
    other.layer[1].eq = with (eq::Band::lowPass, 1000.0, 0.0, 0.707, true);
    CHECK (run (other) == plain);
    // A global LFO on layer A's bell frequency moves the sound; the same LFO on B's does not.
    auto bell = settings;
    bell.layer[0].eq = with (eq::Band::bell, 1000.0, 12.0, 2.0);
    const auto still = run (bell);
    mod::Settings m;
    m.lfo[0].rateHz = 3.0;
    m.routes[0] = { mod::Source::lfo1, mod::Dest::eqBellFrequencyA, 0.8, true };
    const auto swept = run (bell, &m);
    CHECK (swept != still);
    m.routes[0].dest = mod::Dest::eqBellFrequencyB;
    const auto elsewhere = run (bell, &m);   // control-rate chunks: the same within rounding
    double worst = 0.0;
    for (std::size_t i = 0; i < still.size(); ++i)
        worst = std::max (worst, static_cast<double> (std::abs (elsewhere[i] - still[i])));
    CHECK (worst < 1.0e-4);
    // EQ routes are layer stages: a per-voice source cannot reach them.
    mod::Settings probe;
    CHECK_FALSE (mod::compatible (probe, mod::Source::env1, mod::Dest::eqBellGainA));
    CHECK (mod::compatible (probe, mod::Source::lfo1, mod::Dest::eqBellGainA));
}
