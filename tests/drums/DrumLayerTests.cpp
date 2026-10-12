// ANDOR Drums M3: several samples on one pad - the picker's orders and velocity ranges,
// and the engine playing each layer at its own natural pitch, level, START and END.
#include "audio/utility/TestSignals.h"
#include "drums/DrumKitEngine.h"
#include "engine/InstrumentBuilder.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>

using namespace osp;
using namespace osp::drums;
using Catch::Approx;

namespace
{
    constexpr double rate = 48000.0;

    std::shared_ptr<InstrumentModel> tone (double hz, double seconds = 1.0)
    {
        auto audio = testsignals::sine (hz, seconds, rate, 0.5, 1);
        auto& x = audio.channels[0];
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] *= static_cast<float> (std::pow (10.0, -30.0 / 20.0 * static_cast<double> (i) / static_cast<double> (x.size())));
        testsignals::applyFades (audio, 0.002, 0.02);
        return instrument::buildComplete (audio, test::analyse (audio), {}, false);
    }
    const std::shared_ptr<InstrumentModel>& low() { static auto m = tone (110.0); return m; }
    const std::shared_ptr<InstrumentModel>& high() { static auto m = tone (330.0); return m; }

    LayerPlan plan (int count, LayerMode mode = LayerMode::roundRobin, RoundRobinOrder order = RoundRobinOrder::sequential)
    {
        LayerPlan p;
        p.count = count;
        p.mode = mode;
        p.order = order;
        return p;
    }

    std::vector<int> picks (LayerPicker& picker, const LayerPlan& p, int hits, int velocity = 100, std::uint64_t seed = 7)
    {
        std::vector<int> out;
        std::array<int, maxLayers> chosen {};
        for (int h = 0; h < hits; ++h)
        {
            REQUIRE (picker.pick (p, velocity, seed, static_cast<std::uint64_t> (h), chosen) == 1);
            out.push_back (chosen[0]);
        }
        return out;
    }

    KitSettings quietKit()
    {
        KitSettings kit;
        kit.macros.life = 0.0;
        kit.macros.dynamics = 0.0;
        kit.shaping = Shaping::neutral();
        kit.volumeDb = 0.0;
        return kit;
    }

    struct Rig
    {
        DrumKitEngine engine;
        std::vector<float> l, r;
        Rig()
        {
            engine.prepare (rate, 256, quietKit(), {});
            l.resize (256);
            r.resize (256);
        }
        std::vector<float> run (double seconds)
        {
            std::vector<float> out;
            const auto total = static_cast<int> (seconds * rate);
            for (int done = 0; done < total; done += 256)
            {
                const int n = std::min (256, total - done);
                float* ch[2] = { l.data(), r.data() };
                engine.render (ch, 2, n);
                out.insert (out.end(), l.begin(), l.begin() + n);
            }
            return out;
        }
        void layers (const LayerPlan& p, std::initializer_list<const InstrumentModel*> models)
        {
            std::array<const InstrumentModel*, maxLayers> m {};
            std::size_t i = 0;
            for (const auto* x : models)
                m[i++] = x;
            engine.setPadLayers (0, m, p);
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
} // namespace

TEST_CASE ("Layer picker: sequential cycles through the layers in order", "[drums][layers]")
{
    LayerPicker picker;
    CHECK (picks (picker, plan (3), 7) == std::vector<int> { 0, 1, 2, 0, 1, 2, 0 });
}

TEST_CASE ("Layer picker: random never repeats a layer twice in a row and is reproducible", "[drums][layers]")
{
    LayerPicker a, b, c;
    const auto first = picks (a, plan (4, LayerMode::roundRobin, RoundRobinOrder::random), 200);
    for (std::size_t i = 1; i < first.size(); ++i)
        CHECK (first[i] != first[i - 1]);
    std::set<int> seen (first.begin(), first.end());
    CHECK (seen.size() == 4);
    CHECK (picks (b, plan (4, LayerMode::roundRobin, RoundRobinOrder::random), 200) == first);   // same seed: same hits
    CHECK (picks (c, plan (4, LayerMode::roundRobin, RoundRobinOrder::random), 200, 100, 8) != first);
}

TEST_CASE ("Layer picker: shuffle plays every layer once per bag, never twice in a row", "[drums][layers]")
{
    LayerPicker picker;
    const int n = 5;
    const auto seq = picks (picker, plan (n, LayerMode::roundRobin, RoundRobinOrder::shuffle), n * 20);
    for (int bag = 0; bag < 20; ++bag)
    {
        std::set<int> inBag (seq.begin() + bag * n, seq.begin() + (bag + 1) * n);
        CHECK (static_cast<int> (inBag.size()) == n);
    }
    for (std::size_t i = 1; i < seq.size(); ++i)
        CHECK (seq[i] != seq[i - 1]);
}

TEST_CASE ("Layer picker: velocity ranges choose the layer; the nearest range if none holds the hit", "[drums][layers]")
{
    auto p = plan (3, LayerMode::velocity);
    spreadVelocityRanges (p, 0, 3);
    CHECK (p.layer[0].velocityLow == 1);
    CHECK (p.layer[2].velocityHigh == 127);
    CHECK (p.layer[1].velocityLow == p.layer[0].velocityHigh + 1);
    LayerPicker picker;
    std::array<int, maxLayers> out {};
    for (int v : { 1, 20, 42 })
    {
        REQUIRE (picker.pick (p, v, 1, 0, out) == 1);
        CHECK (out[0] == 0);
    }
    REQUIRE (picker.pick (p, 64, 1, 1, out) == 1);
    CHECK (out[0] == 1);
    REQUIRE (picker.pick (p, 127, 1, 2, out) == 1);
    CHECK (out[0] == 2);
    // A gap: 1..40 and 90..127 only. 60 is nearer the soft layer, 80 the hard one.
    auto gap = plan (2, LayerMode::velocity);
    gap.layer[0].velocityHigh = 40;
    gap.layer[1].velocityLow = 90;
    REQUIRE (picker.pick (gap, 60, 1, 3, out) == 1);
    CHECK (out[0] == 0);
    REQUIRE (picker.pick (gap, 80, 1, 4, out) == 1);
    CHECK (out[0] == 1);
}

TEST_CASE ("Layer picker: stack plays every layer in range", "[drums][layers]")
{
    auto p = plan (3, LayerMode::stack);
    p.layer[2].velocityLow = 100;   // the third only on hard hits
    LayerPicker picker;
    std::array<int, maxLayers> out {};
    CHECK (picker.pick (p, 80, 1, 0, out) == 2);
    CHECK (picker.pick (p, 110, 1, 1, out) == 3);
    CHECK (picker.pick (plan (0), 100, 1, 2, out) == 0);
}

TEST_CASE ("Layers on a pad: each plays at its own natural pitch, alternating", "[drums][layers][engine]")
{
    Rig seq;
    seq.layers (plan (2), { low().get(), high().get() });
    seq.engine.noteOn (36, 100);
    CHECK (seq.engine.lastLayerPlayed (0) == 0);
    CHECK (frequencyOf (seq.run (0.25), 2400, 12000) == Approx (110.0).margin (1.5));
    seq.engine.noteOn (36, 100);
    CHECK (seq.engine.lastLayerPlayed (0) == 1);
    seq.engine.noteOn (36, 100);
    CHECK (seq.engine.lastLayerPlayed (0) == 0);

    // The second layer, alone, at 330 Hz on the pad's own key: its own root, not the first's.
    Rig alone;
    auto vel = plan (2, LayerMode::velocity);
    vel.layer[0].velocityHigh = 63;
    vel.layer[1].velocityLow = 64;
    alone.layers (vel, { low().get(), high().get() });
    alone.engine.noteOn (36, 120);
    CHECK (alone.engine.lastLayerPlayed (0) == 1);
    CHECK (frequencyOf (alone.run (0.25), 2400, 12000) == Approx (330.0).margin (3.0));
    // In EXPANDED a key above the root moves every layer by the same semitones.
    alone.engine.reset();
    alone.engine.setKeyboardMode (KeyboardMode::expanded);
    const auto& zone = alone.engine.keyMap().zone[0];
    const int up = std::min (7, zone.high - zone.rootNote());
    REQUIRE (up > 0);
    alone.engine.noteOn (zone.rootNote() + up, 120);
    CHECK (frequencyOf (alone.run (0.25), 2400, 12000) == Approx (330.0 * std::pow (2.0, up / 12.0)).margin (6.0));
}

TEST_CASE ("Layers on a pad: one layer at its defaults sounds exactly as setPadModel", "[drums][layers][engine]")
{
    Rig a, b;
    a.engine.setPadModel (0, low().get());
    b.layers (plan (1), { low().get() });
    a.engine.noteOn (36, 100);
    b.engine.noteOn (36, 100);
    const auto x = a.run (0.3), y = b.run (0.3);
    REQUIRE (x.size() == y.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i)
        worst = std::max (worst, static_cast<double> (std::abs (x[i] - y[i])));
    CHECK (worst == 0.0);
}

TEST_CASE ("Layers on a pad: stack sounds every layer; layer level and END apply per hit", "[drums][layers][engine]")
{
    Rig stack;
    stack.layers (plan (2, LayerMode::stack), { low().get(), high().get() });
    stack.engine.noteOn (36, 100);
    CHECK (stack.engine.activeVoiceCount (0) == 2);

    // -6 dB on the layer halves its level.
    Rig loud, soft;
    loud.layers (plan (1), { low().get() });
    auto quieter = plan (1);
    quieter.layer[0].gainDb = -6.0206;
    soft.layers (quieter, { low().get() });
    loud.engine.noteOn (36, 100);
    soft.engine.noteOn (36, 100);
    const auto a = loud.run (0.3), b = soft.run (0.3);
    CHECK (rmsOf (b, 2400, 12000) / rmsOf (a, 2400, 12000) == Approx (0.5).margin (0.01));

    // END at 0.2 of a 1 s sound: silent from about 0.2 s on, still sounding before.
    Rig cut;
    auto shortened = plan (1);
    shortened.layer[0].end = 0.2;
    cut.layers (shortened, { low().get() });
    cut.engine.noteOn (36, 100);
    const auto c = cut.run (0.5);
    CHECK (rmsOf (c, 2400, 7200) > 0.01);                   // 50..150 ms
    CHECK (rmsOf (c, static_cast<std::size_t> (0.24 * rate), c.size()) < 1.0e-5);
    CHECK (cut.engine.activeVoiceCount (0) == 0);
}

TEST_CASE ("Layers on a pad: the same seed picks the same layers (random, shuffle)", "[drums][layers][engine]")
{
    for (auto order : { RoundRobinOrder::random, RoundRobinOrder::shuffle })
    {
        auto sequence = [order] (std::uint64_t seed) {
            DrumKitEngine e;
            auto kit = quietKit();
            kit.seed = seed;
            e.prepare (rate, 256, kit, {});
            std::array<const InstrumentModel*, maxLayers> m {};
            m[0] = low().get();
            m[1] = high().get();
            m[2] = low().get();
            m[3] = high().get();
            e.setPadLayers (0, m, plan (4, LayerMode::roundRobin, order));
            std::vector<int> s;
            for (int i = 0; i < 40; ++i)
            {
                e.noteOn (36, 100);
                s.push_back (e.lastLayerPlayed (0));
            }
            return s;
        };
        CHECK (sequence (3) == sequence (3));
        CHECK (sequence (3) != sequence (4));
    }
}
