// The bounded grain budget under stress (unified engine Phase 4): lowering the budget while
// grains play, dense sustained chords with new notes arriving, rapid voice stealing, fairness
// between A, B and C, a budget smaller than the streams, and sudden parameter changes. The
// shared policy (sessions before state 14) is not touched: its pinned Phase 2 renders are the
// [phase2-demos] goldens.

#include "engine/AtlasSet.h"
#include "engine/InstrumentEngine.h"
#include "research/AtlasFixtures.h"
#include "support/AllocationCounter.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <set>

using namespace osp;
using namespace osp::research;

namespace
{
    std::shared_ptr<const InstrumentSet> atlasSet (const char* name)
    {
        static std::map<std::string, std::shared_ptr<const InstrumentSet>> cache;
        auto& s = cache[name];
        if (s == nullptr)
        {
            auto r = atlasfixtures::build (atlasfixtures::make (name));
            REQUIRE (r.set != nullptr);
            s = r.set;
        }
        return s;
    }

    EngineSettings granular (int budget, int layers = 3)
    {
        EngineSettings s;
        s.macros.life = 0.0;
        s.macros.motion = 0.0;
        s.macros.space = 0.0;
        s.macros.reimagined = 0.0;
        for (int l = 0; l < layers; ++l)
        {
            s.sourceMode[static_cast<std::size_t> (l)] = SourceMode::granular;
            s.granular[static_cast<std::size_t> (l)] = { 0.5, 0.12, 40.0, 0.0, 0.5 };
        }
        s.grainBudget = budget;
        s.grainBudgetPolicy = GrainBudgetPolicy::bounded;
        return s;
    }

    struct Stress
    {
        InstrumentEngine engine;
        std::array<std::shared_ptr<const InstrumentSet>, 3> sets { atlasSet ("sung-four"), atlasSet ("bowed-phrase"), atlasSet ("legato") };
        std::vector<float> l = std::vector<float> (128), r = std::vector<float> (128);
        int most = 0;
        bool finite = true;
        double seconds = 0.0;
        explicit Stress (const EngineSettings& s, int layers = 3)
        {
            engine.prepare (48000.0, 128, s);
            for (int i = 0; i < layers; ++i)
                engine.setInstrumentSet (sets[static_cast<std::size_t> (i)].get(), i);
        }
        /** Renders `duration` s; `each` runs before every block with the time. Returns the most
            grains seen after `settle` s into this call. */
        int run (double duration, const std::function<void (double)>& each = {}, double settle = 0.0)
        {
            int peak = 0;
            const int blocks = static_cast<int> (duration * 48000.0 / 128);
            for (int b = 0; b < blocks; ++b)
            {
                const double t = b * 128 / 48000.0;
                if (each)
                    each (seconds + t);
                float* ch[2] = { l.data(), r.data() };
                engine.render (ch, 2, 128);
                for (int i = 0; i < 128; ++i)
                    finite = finite && std::isfinite (l[static_cast<std::size_t> (i)]) && std::isfinite (r[static_cast<std::size_t> (i)]);
                const int live = engine.liveGrainCount();
                most = std::max (most, live);
                if (t >= settle)
                    peak = std::max (peak, live);
            }
            seconds += duration;
            return peak;
        }
        /** Grains playing per layer (A, B, C), by which layer's set holds the voice's model. */
        std::array<int, 3> perLayer() const
        {
            std::array<int, 3> n {};
            for (int i = 0; i < InstrumentEngine::voiceSlots(); ++i)
            {
                const auto& v = engine.voiceAt (i);
                if (! v.isActive())
                    continue;
                for (std::size_t s = 0; s < sets.size(); ++s)
                    for (const auto& m : sets[s]->members)
                        if (m.model.get() == v.model())
                            n[s] += v.grainsPlaying();
            }
            return n;
        }
    };
}

TEST_CASE ("Grain budget stress: lowering the budget while grains play holds the new budget once they finish", "[integration][budget][stress]")
{
    Stress rig (granular (64));
    for (int key : { 55, 57, 59, 60, 62, 64, 65, 67 })
        rig.engine.noteOn (key, 100);
    rig.run (0.5);
    REQUIRE (rig.engine.liveGrainCount() > 8);
    rig.engine.setGrainBudget (8);
    // Grains already playing finish (no grain is cut: a click); new ones respect the budget.
    // A grain lasts 0.12 s (+ its jitter): after 0.25 s, never above 8.
    const int after = rig.run (1.5, {}, 0.25);
    CHECK (after <= 8);
    CHECK (after >= 4);
    CHECK (rig.finite);
    // And raising it again lets the streams fill it.
    rig.engine.setGrainBudget (64);
    CHECK (rig.run (0.5, {}, 0.25) > 8);
}

TEST_CASE ("Grain budget stress: dense sustained chords while new notes arrive never pass the budget", "[integration][budget][stress]")
{
    auto s = granular (16);
    s.polyphony = 24;
    Stress rig (s);
    for (int key = 48; key < 60; ++key)
        rig.engine.noteOn (key, 100);
    int next = 60;
    std::deque<int> melody;
    const int peak = rig.run (2.0, [&] (double t) {
        // A new note every 50 ms over the held chord (a fast melody on a pad), four at a time.
        if (static_cast<int> (t / 0.05) != static_cast<int> ((t - 128 / 48000.0) / 0.05))
        {
            rig.engine.noteOn (next, 90);
            melody.push_back (next);
            if (melody.size() > 4)
            {
                rig.engine.noteOff (melody.front());
                melody.pop_front();
            }
            next = next < 72 ? next + 1 : 60;
        }
    });
    CHECK (peak <= 16);
    CHECK (rig.most <= 16);
    CHECK (rig.finite);
}

TEST_CASE ("Grain budget stress: rapid voice stealing stays within the budget and never allocates", "[integration][budget][stress]")
{
    auto s = granular (12, 1);
    s.polyphony = 8;
    Stress rig (s, 1);
    int key = 40;
    int allocations = 0;
    {
        test::AllocationScope scope;
        rig.run (1.5, [&] (double) {
            // A note-on every block (2.7 ms): eight voices stolen over and over.
            rig.engine.noteOn (key, 100);
            key = key < 80 ? key + 1 : 40;
        });
        allocations = scope.count();
    }
    CHECK (allocations == 0);
    CHECK (rig.most <= 12);
    CHECK (rig.finite);
}

TEST_CASE ("Grain budget stress: A, B and C share a tight budget fairly - none is starved", "[integration][budget][stress]")
{
    Stress rig (granular (12));
    for (int key : { 55, 57, 59, 60 })
        rig.engine.noteOn (key, 100);   // the same keys sound on all three layers
    std::array<double, 3> sum {};
    std::array<int, 3> starvedBlocks {};
    int blocks = 0;
    rig.run (2.0, [&] (double t) {
        if (t < 0.3)
            return;
        const auto n = rig.perLayer();
        ++blocks;
        for (std::size_t i = 0; i < 3; ++i)
        {
            sum[i] += n[i];
            starvedBlocks[i] += n[i] == 0 ? 1 : 0;
        }
    });
    CHECK (rig.most <= 12);
    for (std::size_t i = 0; i < 3; ++i)
    {
        INFO ("layer " << i << ": " << sum[i] / blocks << " grains on average, starved in " << starvedBlocks[i] << " of " << blocks << " blocks");
        CHECK (sum[i] / blocks >= 2.0);                 // about a third of 12 each
        CHECK (starvedBlocks[i] < blocks / 10);
    }
}

TEST_CASE ("Grain budget stress: a budget smaller than the streams holds, and every stream still sounds over time", "[integration][budget][stress]")
{
    auto s = granular (3);
    s.polyphony = 12;
    Stress rig (s);
    for (int key : { 55, 57, 59, 60 })
        rig.engine.noteOn (key, 100);   // 12 streams (4 keys x 3 layers), 3 grains
    std::array<int, 3> everHeard {};
    rig.run (2.0, [&] (double) {
        const auto n = rig.perLayer();
        for (std::size_t i = 0; i < 3; ++i)
            everHeard[i] += n[i] > 0 ? 1 : 0;
    });
    CHECK (rig.most <= 3);
    CHECK (rig.most >= 1);
    for (int heard : everHeard)
        CHECK (heard > 0);
    CHECK (rig.finite);
}

TEST_CASE ("Grain budget stress: sudden parameter changes while budget-limited stay bounded and finite", "[integration][budget][stress]")
{
    auto s = granular (10);
    s.polyphony = 16;
    Stress rig (s);
    for (int key : { 50, 55, 59, 62, 67 })
        rig.engine.noteOn (key, 100);
    int step = 0;
    const int peak = rig.run (2.0, [&] (double t) {
        if (static_cast<int> (t / 0.05) == step)
            return;
        step = static_cast<int> (t / 0.05);
        // Every 50 ms: size, density, position, spread, SCATTER, PAN and the budget jump.
        for (int l = 0; l < 3; ++l)
        {
            GranularParams p;
            p.sizeSeconds = (step % 3 == 0) ? 0.02 : (step % 3 == 1 ? 0.4 : 0.1);
            p.density = (step % 2 == 0) ? 40.0 : 4.0;
            p.position = (step % 5) / 4.0;
            p.spread = (step % 2 == 0) ? 1.0 : 0.0;
            p.scatterSemitones = (step % 4 == 0) ? 12.0 : 0.0;
            p.pan = (step % 3 == 0) ? 1.0 : 0.0;
            rig.engine.setGranular (l, p);
        }
        rig.engine.setGrainBudget (step % 4 == 0 ? 4 : 10);
    });
    CHECK (rig.most <= 10);
    CHECK (peak <= 10);
    CHECK (rig.finite);
}
