// MOVEMENT v2: SHAPER - host-synchronised rhythmic patterns on volume and/or filter.
#include "audio/utility/TestSignals.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "engine/MovementBus.h"
#include "engine/RhythmicShaper.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace osp;
using Catch::Approx;

namespace
{
    constexpr double rate = 48000.0;

    std::shared_ptr<InstrumentModel> toneModel (bool saw)
    {
        auto audio = saw ? testsignals::saw (220.0, 8.0, rate, 0.3) : testsignals::sine (220.0, 8.0, rate, 0.4, 1);
        testsignals::applyFades (audio, 0.01, 0.3);
        return instrument::buildComplete (audio, test::analyse (audio), {}, false);
    }

    EngineSettings shaperSettings (ShaperTarget target, int pattern = 0, double smooth = 0.0, double amount = 1.0)
    {
        EngineSettings s;
        s.macros.life = 0.0;
        s.macros.space = 0.0;
        s.macros.reimagined = 0.0;
        s.macros.motion = amount;
        s.shaping = Shaping::neutral();
        s.shaping.movementMode = MovementMode::shaper;
        s.shaping.shaper.pattern = pattern;
        s.shaping.shaper.rate = ShaperRate::sixteenth;
        s.shaping.shaper.target = target;
        s.shaping.shaper.smooth = smooth;
        return s;
    }

    /** Holds a note and renders with the host transport running from `startPpq` at 120 BPM. */
    std::vector<float> render (const InstrumentModel& model, const EngineSettings& s, double seconds, double startPpq, int block = 256,
                               bool playing = true)
    {
        InstrumentEngine engine;
        engine.prepare (rate, block, s);
        engine.setModel (&model);
        std::vector<float> out, l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block));
        const auto total = static_cast<int> (seconds * rate);
        for (int done = 0; done < total; done += block)
        {
            HostTiming t;
            t.valid = playing;
            t.playing = playing;
            t.bpm = 120.0;
            t.ppq = startPpq + done * 120.0 / (60.0 * rate);
            engine.setHostTiming (t);
            if (done == 0)
                engine.noteOn (57, 100, 1);
            const int n = std::min (block, total - done);
            float* ch[2] = { l.data(), r.data() };
            engine.render (ch, 2, n);
            out.insert (out.end(), l.begin(), l.begin() + n);
        }
        return out;
    }

    std::vector<double> envelopeDb (const std::vector<float>& x, int window)
    {
        std::vector<double> e;
        for (std::size_t i = 0; i + static_cast<std::size_t> (window) <= x.size(); i += static_cast<std::size_t> (window))
        {
            double sum = 0.0;
            for (std::size_t j = i; j < i + static_cast<std::size_t> (window); ++j)
                sum += static_cast<double> (x[j]) * x[j];
            e.push_back (10.0 * std::log10 (sum / window + 1e-20));
        }
        return e;
    }

    double correlation (const std::vector<double>& a, const std::vector<double>& b)
    {
        double ma = 0, mb = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            ma += a[i];
            mb += b[i];
        }
        ma /= static_cast<double> (a.size());
        mb /= static_cast<double> (b.size());
        double ab = 0, aa = 0, bb = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            ab += (a[i] - ma) * (b[i] - mb);
            aa += (a[i] - ma) * (a[i] - ma);
            bb += (b[i] - mb) * (b[i] - mb);
        }
        return ab / std::sqrt (aa * bb + 1e-30);
    }

    double sd (const std::vector<double>& v)
    {
        double m = 0, var = 0;
        for (double x : v) m += x;
        m /= static_cast<double> (v.size());
        for (double x : v) var += (x - m) * (x - m);
        return std::sqrt (var / static_cast<double> (v.size()));
    }
}

TEST_CASE ("shaper: every pattern is bounded, continuous and distinct", "[unit][shaper]")
{
    const int points = RhythmicShaper::steps * 400;
    std::vector<std::vector<float>> curves;
    for (int p = 0; p < RhythmicShaper::patternCount; ++p)
    {
        for (const double smooth : { 0.0, 0.5, 1.0 })
        {
            float previous = RhythmicShaper::evaluate (p, 0.0, smooth);
            float largest = 0.0f;
            std::vector<float> curve;
            for (int i = 1; i <= points; ++i)
            {
                const float v = RhythmicShaper::evaluate (p, static_cast<double> (i) / points, smooth);
                REQUIRE (v >= 0.0f);
                REQUIRE (v <= 1.0f);
                largest = std::max (largest, std::abs (v - previous));
                previous = v;
                curve.push_back (v);
            }
            INFO (RhythmicShaper::patternName (p) << " smooth " << smooth << ": largest step " << largest);
            // Never a jump (the cycle wraps continuously too); smooth patterns flow.
            CHECK (largest < (smooth == 0.0 ? 0.12f : 0.03f));
            if (smooth == 0.0)
                curves.push_back (curve);
        }
    }
    for (std::size_t a = 0; a < curves.size(); ++a)
        for (std::size_t b = a + 1; b < curves.size(); ++b)
        {
            double diff = 0.0;
            for (std::size_t i = 0; i < curves[a].size(); ++i)
                diff += std::abs (curves[a][i] - curves[b][i]);
            INFO (RhythmicShaper::patternName (static_cast<int> (a)) << " vs " << RhythmicShaper::patternName (static_cast<int> (b)));
            CHECK (diff / static_cast<double> (curves[a].size()) > 0.05);
        }
}

TEST_CASE ("shaper: VOL follows the host's bars from any start position", "[integration][shaper]")
{
    const auto model = toneModel (false);
    const int window = 48;   // 1 ms
    for (const double startPpq : { 0.0, 4.0, 16.0, 37.5 })
    {
        for (const int pattern : { 0, 3, 11 })   // PULSE, THREE, MACHINE
        {
            const auto plain = envelopeDb (render (*model, shaperSettings (ShaperTarget::volume, pattern, 0.0, 0.0), 3.0, startPpq), window);
            const auto shaped = envelopeDb (render (*model, shaperSettings (ShaperTarget::volume, pattern), 3.0, startPpq), window);
            // Expected: the pattern at each window's PPQ (120 BPM: 2 quarters per second).
            std::vector<double> measured, expected;
            for (std::size_t w = 300; w < shaped.size(); ++w)   // after the attack
            {
                const double ppq = startPpq + (static_cast<double> (w) + 0.5) * window * 2.0 / rate;
                const double phase = ppq / 4.0 - std::floor (ppq / 4.0);
                const double gain = RhythmicShaper::evaluate (pattern, phase, 0.0);
                measured.push_back (shaped[w] - plain[w]);
                expected.push_back (20.0 * std::log10 (std::max (1.0e-3, gain)));
            }
            INFO ("start " << startPpq << " pattern " << RhythmicShaper::patternName (pattern));
            CHECK (correlation (measured, expected) > 0.95);
        }
    }
}

TEST_CASE ("shaper: identical at any block size, exact bypass at zero depth", "[integration][shaper]")
{
    const auto model = toneModel (true);
    const auto a = render (*model, shaperSettings (ShaperTarget::both, 9, 0.3), 2.0, 13.25, 64);
    const auto b = render (*model, shaperSettings (ShaperTarget::both, 9, 0.3), 2.0, 13.25, 509);
    REQUIRE (a.size() == b.size());
    double diff = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        diff = std::max (diff, static_cast<double> (std::abs (a[i] - b[i])));
    CHECK (diff == 0.0);

    // MOVEMENT 0 in SHAPER sounds exactly like MOVEMENT 0 in DRIFT.
    auto off = shaperSettings (ShaperTarget::both, 9, 0.3, 0.0);
    const auto shaperOff = render (*model, off, 1.0, 0.0);
    off.shaping.movementMode = MovementMode::drift;
    const auto driftOff = render (*model, off, 1.0, 0.0);
    double bypass = 0.0;
    for (std::size_t i = 0; i < shaperOff.size(); ++i)
        bypass = std::max (bypass, static_cast<double> (std::abs (shaperOff[i] - driftOff[i])));
    CHECK (bypass == 0.0);
}

TEST_CASE ("shaper: VOL, FILTER and BOTH are audibly different", "[integration][shaper]")
{
    const auto model = toneModel (true);   // a bright saw: the filter has something to do
    struct Motion { double level, brightness; };
    auto measure = [&] (ShaperTarget target) {
        const auto x = render (*model, shaperSettings (target, 3, 0.2, 0.7), 3.0, 0.0);
        std::vector<double> level, bright;
        const std::size_t frame = 480;   // 10 ms
        for (std::size_t i = static_cast<std::size_t> (0.5 * rate); i + frame < x.size(); i += frame)
        {
            double e = 0.0, d = 0.0;
            for (std::size_t j = i + 1; j < i + frame; ++j)
            {
                e += static_cast<double> (x[j]) * x[j];
                const double dx = x[j] - x[j - 1];
                d += dx * dx;
            }
            level.push_back (10.0 * std::log10 (e + 1e-20));
            bright.push_back (10.0 * std::log10 (d / (e + 1e-20) + 1e-20));   // HF share
        }
        return Motion { sd (level), sd (bright) };
    };
    const auto vol = measure (ShaperTarget::volume), filter = measure (ShaperTarget::filter), both = measure (ShaperTarget::both);
    INFO ("level sd dB: vol " << vol.level << ", filter " << filter.level << ", both " << both.level);
    INFO ("brightness sd dB: vol " << vol.brightness << ", filter " << filter.brightness << ", both " << both.brightness);
    CHECK (vol.level > 1.2);                      // clearly rhythmic volume
    CHECK (vol.brightness < 0.4 * filter.brightness);   // ...with a (nearly) static tone
    CHECK (filter.brightness > 1.5);              // brightness moves
    CHECK (filter.level < 0.35 * vol.level);      // ...level much steadier
    CHECK (both.brightness > 0.8 * filter.brightness);
    CHECK (both.level > filter.level);            // quieter and darker together
    CHECK (both.level < vol.level);               // ...but not muted like VOL
}

TEST_CASE ("shaper: SMOOTH rounds the edges; nothing ever jumps", "[unit][shaper]")
{
    // A constant input shows the gain directly.
    auto edges = [] (double smooth) {
        RhythmicShaper shaper;
        shaper.prepare (rate);
        ShaperParams p;
        p.pattern = 0;   // PULSE
        p.target = ShaperTarget::volume;
        p.smooth = smooth;
        shaper.setParams (p);
        HostTiming t;
        t.valid = t.playing = true;
        t.bpm = 120.0;
        shaper.setTiming (t);
        float previous = 1.0f, largest = 0.0f;
        for (int i = 0; i < static_cast<int> (2.0 * rate); ++i)
        {
            shaper.tick();
            float l = 1.0f, r = 1.0f;
            shaper.process (l, r, 1.0);
            if (i > 0)
                largest = std::max (largest, std::abs (l - previous));
            previous = l;
        }
        return largest;
    };
    const float crisp = edges (0.0), rounded = edges (0.5), flowing = edges (1.0);
    INFO ("largest per-sample change: smooth 0 " << crisp << ", 50 % " << rounded << ", 100 % " << flowing);
    CHECK (crisp < 0.012f);       // anti-click: a 2 ms minimum transition
    CHECK (rounded < crisp);
    CHECK (flowing < rounded);
}

TEST_CASE ("shaper: pattern, rate, target and mode changes never click", "[unit][shaper]")
{
    MovementBus bus;
    bus.prepare (rate, 1);
    Shaping s = Shaping::neutral();
    s.movementMode = MovementMode::drift;
    bus.setTargets (s, 0.8);
    bus.reset();
    HostTiming t;
    t.valid = t.playing = true;
    t.bpm = 128.0;
    float previous = 0.5f, largest = 0.0f;
    const int seconds = 6;
    for (int i = 0; i < seconds * static_cast<int> (rate); ++i)
    {
        if (i % 512 == 0)
        {
            t.ppq = i * 128.0 / (60.0 * rate);
            bus.setTiming (t);
        }
        const double at = i / rate;
        auto change = [&] (double when, auto&& apply) {
            if (i == static_cast<int> (when * rate))
            {
                apply();
                bus.setTargets (s, 0.8);
            }
        };
        change (0.5, [&] { s.movementMode = MovementMode::shaper; });
        change (1.3, [&] { s.shaper.pattern = 11; });                       // MACHINE
        change (2.1, [&] { s.shaper.rate = ShaperRate::sixteenthTriplet; });
        change (2.9, [&] { s.shaper.target = ShaperTarget::volume; });
        change (3.7, [&] { s.shaper.pattern = 2; s.shaper.rate = ShaperRate::quarter; });
        change (4.5, [&] { s.movementMode = MovementMode::pulse; });
        change (5.2, [&] { s.movementMode = MovementMode::shaper; });
        float l = 0.5f, r = 0.5f;   // constant input: the output is the modulation
        bus.process (l, r);
        REQUIRE (std::isfinite (l));
        if (at > 0.01)
            largest = std::max (largest, std::abs (l - previous));
        previous = l;
    }
    INFO ("largest per-sample change " << largest);
    CHECK (largest < 0.01f);   // 0.5 x full swing over >= 2 ms
}

TEST_CASE ("shaper: with the transport stopped the pattern starts with the first note", "[integration][shaper]")
{
    const auto model = toneModel (false);
    auto phaseAfter = [&] (double seconds, int block) {
        InstrumentEngine engine;
        auto s = shaperSettings (ShaperTarget::volume, 3);
        engine.prepare (rate, block, s);
        engine.setModel (model.get());
        std::vector<float> l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block));
        float* ch[2] = { l.data(), r.data() };
        // Half a second of silence first: the clock waits for a note.
        const int idle = static_cast<int> (0.5 * rate);
        for (int done = 0; done < idle; done += block)
            engine.render (ch, 2, std::min (block, idle - done));
        CHECK (engine.shaperPhase() < 0.0f);
        engine.noteOn (60, 100, 1);
        const auto total = static_cast<int> (seconds * rate);
        for (int done = 0; done < total; done += block)
            engine.render (ch, 2, std::min (block, total - done));
        return engine.shaperPhase();
    };
    // 120 BPM fallback, 1/16: one cycle = 4 quarters = 2 s; after 0.75 s the pattern is 37.5 % in.
    CHECK (phaseAfter (0.75, 256) == Approx (0.375).margin (1.0e-3));
    CHECK (phaseAfter (0.75, 100) == Approx (0.375).margin (1.0e-3));
}

TEST_CASE ("shaper: the phase never drifts from the host over minutes", "[unit][shaper]")
{
    RhythmicShaper shaper;
    const double sr = 44100.0;
    shaper.prepare (sr);
    HostTiming t;
    t.valid = t.playing = true;
    t.bpm = 97.0;
    const int block = 441;
    double worst = 0.0;
    for (int b = 0; b < static_cast<int> (10.0 * 60.0 * sr / block); ++b)
    {
        t.ppq = static_cast<double> (b) * block * 97.0 / (60.0 * sr);
        shaper.setTiming (t);
        for (int i = 0; i < block; ++i)
            shaper.tick();
        const double expectedPpq = t.ppq + (block - 1) * 97.0 / (60.0 * sr);   // last sample's position
        const double expected = expectedPpq / 4.0 - std::floor (expectedPpq / 4.0);
        double d = std::abs (shaper.displayPhase() - expected);
        d = std::min (d, 1.0 - d);
        worst = std::max (worst, d);
    }
    CHECK (worst < 1.0e-5);
}
