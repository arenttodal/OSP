// ANDOR Drums: per-pad modulation (ENV, LFO, VELOCITY, WHEEL, PRESSURE -> PITCH, LEVEL, PAN,
// FILTER, START, DECAY) on OSP's modulation system, one pad's engine at a time.
#include "audio/utility/TestSignals.h"
#include "drums/DrumKitEngine.h"
#include "engine/InstrumentBuilder.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <string>

using namespace osp;
using namespace osp::drums;
using Catch::Approx;

namespace
{
    constexpr double rate = 48000.0;

    std::shared_ptr<InstrumentModel> decaying (AudioData audio, double decayDb = 30.0)
    {
        auto& x = audio.channels[0];
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] *= static_cast<float> (std::pow (10.0, -decayDb / 20.0 * static_cast<double> (i) / static_cast<double> (x.size())));
        testsignals::applyFades (audio, 0.002, 0.02);
        return instrument::buildComplete (audio, test::analyse (audio), {}, false);
    }
    const std::shared_ptr<InstrumentModel>& tone() { static auto m = decaying (testsignals::sine (110.0, 1.0, rate, 0.5, 1)); return m; }
    const std::shared_ptr<InstrumentModel>& bright() { static auto m = decaying (testsignals::saw (220.0, 1.0, rate, 0.4, 1)); return m; }

    KitSettings quietKit (bool withFilter = false)
    {
        KitSettings kit;
        kit.macros.life = 0.0;
        kit.macros.dynamics = 0.0;
        if (! withFilter)
            kit.shaping = Shaping::neutral();   // no CHARACTER filter (the plugin's kit has one)
        kit.volumeDb = 0.0;
        return kit;
    }

    struct Rig
    {
        DrumKitEngine engine;
        std::vector<float> l, r;
        explicit Rig (const InstrumentModel* model = tone().get(), bool withFilter = false)
        {
            engine.prepare (rate, 256, quietKit (withFilter), {});
            engine.setPadModel (0, model);
            l.resize (256);
            r.resize (256);
        }
        std::vector<float> run (double seconds, std::vector<float>* right = nullptr)
        {
            std::vector<float> out;
            const auto total = static_cast<int> (seconds * rate);
            for (int done = 0; done < total; done += 256)
            {
                const int n = std::min (256, total - done);
                float* ch[2] = { l.data(), r.data() };
                engine.render (ch, 2, n);
                out.insert (out.end(), l.begin(), l.begin() + n);
                if (right != nullptr)
                    right->insert (right->end(), r.begin(), r.begin() + n);
            }
            return out;
        }
        std::vector<float> hit (int velocity, double seconds = 0.3, std::vector<float>* right = nullptr)
        {
            engine.noteOn (36, velocity);
            return run (seconds, right);
        }
    };

    double rmsOf (const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        to = std::min (to, x.size());
        double s = 0.0;
        for (std::size_t i = from; i < to; ++i)
            s += static_cast<double> (x[i]) * x[i];
        return to > from ? std::sqrt (s / static_cast<double> (to - from)) : 0.0;
    }

    double brightness (const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double d = 0.0, s = 0.0;
        for (std::size_t i = std::max<std::size_t> (from, 1); i < std::min (to, x.size()); ++i)
        {
            const double diff = static_cast<double> (x[i]) - x[i - 1];
            d += diff * diff;
            s += static_cast<double> (x[i]) * x[i];
        }
        return s > 0.0 ? std::sqrt (d / s) : 0.0;
    }

    double frequencyOf (const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        int first = -1, last = -1, crossings = 0;
        for (std::size_t i = from + 1; i < std::min (to, x.size()); ++i)
            if (x[i - 1] <= 0.0f && x[i] > 0.0f)
            {
                if (first < 0)
                    first = static_cast<int> (i);
                last = static_cast<int> (i);
                ++crossings;
            }
        return crossings > 1 ? rate * (crossings - 1) / static_cast<double> (last - first) : 0.0;
    }

    std::size_t at (double seconds) { return static_cast<std::size_t> (seconds * rate); }
} // namespace

TEST_CASE ("Pad modulation: none set, or every slot empty, plays bit-identically", "[drums][padmod]")
{
    Rig plain, empty;
    PadModulation none;
    CHECK_FALSE (none.active());
    empty.engine.setPadModulation (0, none);
    CHECK (plain.hit (100) == empty.hit (100));

    // A slot with a target but no depth does nothing either.
    Rig zero;
    PadModulation m;
    m.slot (ModSource::env, 0) = { ModTarget::pitch, 0.0 };
    CHECK_FALSE (m.active());
    zero.engine.setPadModulation (0, m);
    Rig plain2;
    CHECK (plain2.hit (100) == zero.hit (100));
}

TEST_CASE ("Pad modulation: ENV -> PITCH drops a kick's pitch to its own", "[drums][padmod]")
{
    Rig drop;
    drop.engine.setPadModulation (0, modPreset (ModPreset::kickPitchDrop));
    const auto x = drop.hit (110, 0.4);
    const double early = frequencyOf (x, at (0.002), at (0.02));
    const double late = frequencyOf (x, at (0.2), at (0.38));
    INFO ("early " << early << " Hz, late " << late << " Hz");
    CHECK (early > 110.0 * std::exp2 (4.0 / 12.0));   // well above, falling from +12 st
    CHECK (late == Approx (110.0).margin (1.0));      // back at the drum's own pitch
}

TEST_CASE ("Pad modulation: VELOCITY -> FILTER darkens soft hits; the hardest hit is the knob's", "[drums][padmod]")
{
    PadModulation m;
    m.slot (ModSource::velocity, 0) = { ModTarget::filter, 0.6 };
    Rig hard (bright().get(), true), soft (bright().get(), true), plain (bright().get(), true);
    hard.engine.setPadModulation (0, m);
    soft.engine.setPadModulation (0, m);
    const auto a = hard.hit (127), b = soft.hit (25), c = plain.hit (127);
    CHECK (a == c);   // velocity 127: no offset at all
    const double ba = brightness (a, at (0.01), at (0.2)), bb = brightness (b, at (0.01), at (0.2));
    INFO ("hard " << ba << ", soft " << bb);
    CHECK (bb < 0.8 * ba);
}

TEST_CASE ("Pad modulation: VELOCITY -> PITCH: soft hits lower by up to the depth", "[drums][padmod]")
{
    auto m = modPreset (ModPreset::velocityPitch);   // one semitone over the range
    Rig hard, soft;
    hard.engine.setPadModulation (0, m);
    soft.engine.setPadModulation (0, m);
    const double fh = frequencyOf (hard.hit (127), at (0.02), at (0.28));
    const double fs = frequencyOf (soft.hit (1), at (0.02), at (0.28));
    const double cents = 1200.0 * std::log2 (fh / fs);
    INFO (cents << " cents");
    CHECK (fh == Approx (110.0).margin (0.5));
    CHECK (cents == Approx (100.0 * 126.0 / 127.0).margin (6.0));
}

TEST_CASE ("Pad modulation: LFO -> PAN moves the hit between the speakers, the same every time", "[drums][padmod]")
{
    PadModulation m;
    m.lfo.rateHz = 8.0;
    m.slot (ModSource::lfo, 0) = { ModTarget::pan, 0.5 };
    Rig one, two;
    one.engine.setPadModulation (0, m);
    two.engine.setPadModulation (0, m);
    std::vector<float> r1, r2;
    const auto l1 = one.hit (100, 0.4, &r1), l2 = two.hit (100, 0.4, &r2);
    CHECK (l1 == l2);
    CHECK (r1 == r2);
    // The left/right balance swings: some stretches lean left, some right.
    double maxLean = -1.0, minLean = 1.0;
    for (double t = 0.02; t < 0.36; t += 0.02)
    {
        const double lv = rmsOf (l1, at (t), at (t + 0.02)), rv = rmsOf (r1, at (t), at (t + 0.02));
        const double lean = (rv - lv) / (rv + lv + 1.0e-12);
        maxLean = std::max (maxLean, lean);
        minLean = std::min (minLean, lean);
    }
    INFO ("lean " << minLean << " .. " << maxLean);
    CHECK (maxLean - minLean > 0.3);
}

TEST_CASE ("Pad modulation: WHEEL -> LEVEL and PRESSURE -> PITCH follow the controllers", "[drums][padmod]")
{
    PadModulation m;
    m.slot (ModSource::wheel, 0) = { ModTarget::level, -12.0 / 54.0 };   // wheel up: 12 dB down
    Rig down, up;
    down.engine.setPadModulation (0, m);
    up.engine.setPadModulation (0, m);
    up.engine.setModWheel (1.0f);
    up.run (0.05);     // the wheel is smoothed over ~10 ms
    down.run (0.05);
    const auto a = down.hit (100), b = up.hit (100);
    const double db = 20.0 * std::log10 (rmsOf (b, at (0.02), at (0.2)) / rmsOf (a, at (0.02), at (0.2)));
    CHECK (db == Approx (-12.0).margin (0.5));

    PadModulation p;
    p.slot (ModSource::pressure, 0) = { ModTarget::pitch, 0.5 };   // full pressure: +12 st
    Rig pressed;
    pressed.engine.setPadModulation (0, p);
    pressed.engine.setPressure (1.0f);
    pressed.run (0.05);
    CHECK (frequencyOf (pressed.hit (100), at (0.05), at (0.28)) == Approx (220.0).margin (2.0));
}

TEST_CASE ("Pad modulation: one pad's modulation never touches another pad", "[drums][padmod]")
{
    Rig a, b;
    a.engine.setPadModel (1, tone().get());
    b.engine.setPadModel (1, tone().get());
    a.engine.setPadModulation (0, modPreset (ModPreset::kickPitchDrop));
    a.engine.triggerPad (1, 100);
    b.engine.triggerPad (1, 100);
    CHECK (a.run (0.3) == b.run (0.3));
}

TEST_CASE ("Pad modulation: presets and depth words", "[drums][padmod]")
{
    for (int p = 1; p < modPresetCount; ++p)
    {
        const auto m = modPreset (static_cast<ModPreset> (p));
        INFO (modPresetName (static_cast<ModPreset> (p)));
        CHECK (m.active());
        const auto s = m.toSettings (1);
        int routes = 0;
        for (const auto& r : s.routes)
            routes += r.source != mod::Source::none ? 1 : 0;
        CHECK (routes >= 1);
        for (int i = 0; i < mod::maxRoutes; ++i)
            if (s.routes[static_cast<std::size_t> (i)].source != mod::Source::none)
                CHECK (mod::routeState (s, i) == mod::RouteState::active);
    }
    CHECK_FALSE (modPreset (ModPreset::none).active());

    char text[32];
    describeDepth (ModTarget::pitch, 0.5, text, sizeof (text));
    CHECK (std::string (text) == "+12.0 st");
    describeDepth (ModTarget::level, -12.0 / 54.0, text, sizeof (text));
    CHECK (std::string (text) == "-12.0 dB");
    describeDepth (ModTarget::filter, 0.45, text, sizeof (text));
    CHECK (std::string (text) == "+45 %");
    describeDepth (ModTarget::decay, 1.0, text, sizeof (text));
    CHECK (std::string (text) == "x 20.0");
}
