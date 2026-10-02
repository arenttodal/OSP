#include "engine/PerformanceEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace osp;

namespace
{
    struct Run
    {
        std::vector<NoteShape> shapes;
        std::vector<double> force;
    };

    Run perform (double interval, int notes, double life, std::uint64_t seed = 7, int note = 60)
    {
        PerformanceEngine engine;
        engine.reset (seed);
        PerformanceProfile profile;
        SourceCharacter character;
        character.transientTonal = 0.6;
        character.sustainedHarmonic = 0.3;
        Run run;
        for (int i = 0; i < notes; ++i)
        {
            NoteShape shape;
            engine.perform (shape, note, 100, i * interval, static_cast<std::uint64_t> (i), profile, character, life);
            run.shapes.push_back (shape);
            run.force.push_back (engine.force());
        }
        return run;
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
        return ab / std::sqrt (aa * bb);
    }

    double lag1 (const std::vector<double>& v)
    {
        return correlation (std::vector<double> (v.begin(), v.end() - 1), std::vector<double> (v.begin() + 1, v.end()));
    }
}

TEST_CASE ("performance: LIFE 0 repeats the recorded performance exactly", "[unit][performance]")
{
    const auto run = perform (0.5, 20, 0.0);
    for (const auto& s : run.shapes)
    {
        CHECK (s.gain == 1.0f);
        CHECK (s.brightnessDb == 0.0f);
        CHECK (s.transientDb == 0.0f);
        CHECK (s.pitchCents == 0.0);
        CHECK (s.pitchSettleCents == 0.0);
        CHECK (s.startOffsetSeconds == 0.0);
    }
}

TEST_CASE ("performance: same seed and events give the same performances", "[unit][performance]")
{
    const auto a = perform (0.37, 50, 0.6, 11);
    const auto b = perform (0.37, 50, 0.6, 11);
    const auto c = perform (0.37, 50, 0.6, 12);
    bool differs = false;
    for (std::size_t i = 0; i < a.shapes.size(); ++i)
    {
        CHECK (a.shapes[i].gain == b.shapes[i].gain);
        CHECK (a.shapes[i].brightnessDb == b.shapes[i].brightnessDb);
        CHECK (a.shapes[i].pitchCents == b.shapes[i].pitchCents);
        differs = differs || a.shapes[i].gain != c.shapes[i].gain;
    }
    CHECK (differs);
}

TEST_CASE ("performance: the player's state has memory in time", "[unit][performance]")
{
    // Notes 0.1 s apart share most of the slow state; notes 30 s apart are independent.
    CHECK (lag1 (perform (0.1, 400, 0.5).force) > 0.9);
    CHECK (std::abs (lag1 (perform (30.0, 400, 0.5).force)) < 0.2);
}

TEST_CASE ("performance: outputs are correlated through force, not independent", "[unit][performance]")
{
    const auto run = perform (3.0, 400, 0.5);
    std::vector<double> gainDb, bright, transient;
    for (const auto& s : run.shapes)
    {
        gainDb.push_back (20.0 * std::log10 (s.gain));
        bright.push_back (s.brightnessDb);
        transient.push_back (s.transientDb);
    }
    CHECK (correlation (gainDb, bright) > 0.3);
    CHECK (correlation (gainDb, transient) > 0.3);
}

TEST_CASE ("performance: fast repeats alternate the attack and drift less in pitch", "[unit][performance]")
{
    const auto fast = perform (0.12, 200, 0.5);
    const auto slow = perform (2.0, 200, 0.5);
    auto meanStep = [] (const Run& r, auto field) {
        double sum = 0.0;
        for (std::size_t i = 1; i < r.shapes.size(); ++i)
            sum += std::abs (field (r.shapes[i]) - field (r.shapes[i - 1]));
        return sum / static_cast<double> (r.shapes.size() - 1);
    };
    auto transient = [] (const NoteShape& s) { return static_cast<double> (s.transientDb); };
    auto pitch = [] (const NoteShape& s) { return s.pitchCents; };
    CHECK (meanStep (fast, transient) > meanStep (slow, transient));
    CHECK (meanStep (fast, pitch) < meanStep (slow, pitch));
}

TEST_CASE ("performance: LIFE 1 stays bounded", "[unit][performance]")
{
    const auto run = perform (0.25, 1000, 1.0);
    for (const auto& s : run.shapes)
    {
        REQUIRE (std::isfinite (s.gain));
        CHECK (std::abs (20.0 * std::log10 (s.gain)) < 15.0);
        CHECK (std::abs (s.brightnessDb) < 25.0f);
        CHECK (std::abs (s.pitchCents) < 60.0);
        CHECK (s.startOffsetSeconds < 0.05);
    }
}
