// KEY DNA's profile (engine/KeyDna.h): neutral is nothing, the same settings give the same
// keys, Smooth <-> Individual behaves predictably, curves map onto the keyboard, every offset
// stays within its knob, and computing a profile allocates nothing.

#include "engine/KeyDna.h"
#include "support/AllocationCounter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>

using namespace osp;
using Catch::Approx;

namespace
{
    keydna::Settings expressive (std::uint64_t seed = 7)
    {
        keydna::Settings s;
        s.enabled = true;
        s.seed = seed;
        s.individuality = 0.5;
        for (auto& p : s.params)
            p.variation = 0.6;
        return s;
    }

    mod::CurveTable ramp()
    {
        mod::Curve c;
        c.count = 2;
        c.points[0] = { 0.0f, 0.0f, 0.0f };
        c.points[1] = { 1.0f, 1.0f, 0.0f };
        return keydna::curveFrom (c);
    }

    bool sameBits (const GranularParams& a, const GranularParams& b)
    {
        return std::memcmp (&a.position, &b.position, sizeof (double)) == 0 && std::memcmp (&a.sizeSeconds, &b.sizeSeconds, sizeof (double)) == 0
               && std::memcmp (&a.density, &b.density, sizeof (double)) == 0 && std::memcmp (&a.tuneSemitones, &b.tuneSemitones, sizeof (double)) == 0
               && std::memcmp (&a.spread, &b.spread, sizeof (double)) == 0 && a.reverse == b.reverse && a.follow == b.follow;
    }
}

TEST_CASE ("KEY DNA: neutral settings give no profile at all", "[unit][keydna]")
{
    keydna::Table table;
    keydna::Settings s;
    CHECK (s.isNeutral());
    table.compute (s);
    CHECK_FALSE (table.active());

    // On, but nothing to move: still neutral, still inactive.
    s.enabled = true;
    CHECK (s.isNeutral());
    table.compute (s);
    CHECK_FALSE (table.active());

    // Depths and variations set, but AMOUNT 0 or switched off: nothing.
    auto e = expressive();
    e.amount = 0.0;
    CHECK (e.isNeutral());
    table.compute (e);
    CHECK_FALSE (table.active());
    e.amount = 1.0;
    e.enabled = false;
    table.compute (e);
    CHECK_FALSE (table.active());
    for (int key = 0; key < 128; ++key)
    {
        const auto& o = table.at (key);
        CHECK_FALSE (o.active);
        CHECK (o.position == 0.0);
        CHECK (o.sizeOctaves == 0.0);
        CHECK (o.densityOctaves == 0.0);
        CHECK (o.spread == 0.0);
        CHECK (o.tuneSemitones == 0.0);
    }
}

TEST_CASE ("KEY DNA: a neutral profile is an exact identity on the granular settings", "[unit][keydna]")
{
    GranularParams base { 0.37, 0.123, 17.5, -3.25, 0.41 };
    base.reverse = true;
    auto p = base;
    keydna::apply (KeyDnaOffsets {}, p);
    CHECK (sameBits (p, base));
    // An active profile that leaves a parameter alone does not touch it (no clamp, no arithmetic).
    KeyDnaOffsets o;
    o.position = 0.1;
    o.active = true;
    p = base;
    keydna::apply (o, p);
    CHECK (p.position == Approx (0.47));
    CHECK (std::memcmp (&p.sizeSeconds, &base.sizeSeconds, sizeof (double)) == 0);
    CHECK (std::memcmp (&p.density, &base.density, sizeof (double)) == 0);
    CHECK (std::memcmp (&p.tuneSemitones, &base.tuneSemitones, sizeof (double)) == 0);
    CHECK (std::memcmp (&p.spread, &base.spread, sizeof (double)) == 0);
}

TEST_CASE ("KEY DNA: the same settings give every key the same identity; another seed another one", "[unit][keydna]")
{
    keydna::Table a, b, c;
    a.compute (expressive (7));
    b.compute (expressive (7));
    c.compute (expressive (8));
    REQUIRE (a.active());
    int differ = 0;
    for (int key = 0; key < 128; ++key)
        for (int p = 0; p < keydna::paramCount; ++p)
        {
            const auto param = static_cast<keydna::Param> (p);
            CHECK (a.offset (key, param) == b.offset (key, param));
            differ += a.offset (key, param) != c.offset (key, param) ? 1 : 0;
        }
    CHECK (differ > 600);   // nearly all of the 640 values

    // Recomputing in place (as the engine does) gives the same values again.
    a.compute (expressive (8));
    a.compute (expressive (7));
    for (int key = 0; key < 128; ++key)
        CHECK (a.at (key).sizeOctaves == b.at (key).sizeOctaves);

    // Different keys differ: C3, C4 and C5 each have their own profile.
    CHECK (a.at (48).position != a.at (60).position);
    CHECK (a.at (60).sizeOctaves != a.at (72).sizeOctaves);
    CHECK (a.at (48).densityOctaves != a.at (72).densityOctaves);
}

TEST_CASE ("KEY DNA: Smooth relates neighbouring keys, Individual makes them independent, monotonically", "[unit][keydna]")
{
    double previous = 2.0;
    for (const double ind : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        auto s = expressive (11);
        s.individuality = ind;
        keydna::Table t;
        t.compute (s);
        double mean = 0.0;
        for (int p = 0; p < keydna::paramCount; ++p)
            mean += t.neighbourCorrelation (static_cast<keydna::Param> (p));
        mean /= keydna::paramCount;
        INFO ("individuality " << ind << ": neighbour correlation " << mean);
        CHECK (mean < previous);
        previous = mean;
        if (ind == 0.0)
            CHECK (mean > 0.9);
        if (ind == 1.0)
            CHECK (std::abs (mean) < 0.3);
    }
}

TEST_CASE ("KEY DNA: a curve maps onto the keyboard, scaled by its depth and AMOUNT", "[unit][keydna]")
{
    keydna::Settings s;
    s.enabled = true;
    s[keydna::Param::size].curve = ramp();
    s[keydna::Param::size].curveDepth = 1.0;
    keydna::Table t;
    t.compute (s);
    const double half = keydna::halfRange (keydna::Param::size);
    CHECK (t.at (0).sizeOctaves == Approx (-half));
    CHECK (t.at (127).sizeOctaves == Approx (half));
    CHECK (std::abs (t.at (64).sizeOctaves) < 0.02 * half);
    for (int key = 1; key < 128; ++key)
        CHECK (t.at (key).sizeOctaves >= t.at (key - 1).sizeOctaves);
    // Only SIZE moves.
    for (int key = 0; key < 128; ++key)
        CHECK (t.at (key).position == 0.0);

    // A negative depth inverts it; AMOUNT scales the whole profile.
    s[keydna::Param::size].curveDepth = -0.5;
    s.amount = 0.5;
    t.compute (s);
    CHECK (t.at (127).sizeOctaves == Approx (-0.25 * half));
}

TEST_CASE ("KEY DNA: every offset is bounded, and applied settings stay within the knobs", "[unit][keydna]")
{
    keydna::Settings s;
    s.enabled = true;
    s.seed = 3;
    s.individuality = 1.0;
    for (auto& p : s.params)
    {
        p.curve = ramp();
        p.curveDepth = 1.0;
        p.variation = 1.0;   // depth + variation could reach 2: clamped to 1
    }
    keydna::Table t;
    t.compute (s);
    for (int key = -5; key < 140; ++key)   // out-of-range keys read the nearest end
        for (int p = 0; p < keydna::paramCount; ++p)
        {
            const auto param = static_cast<keydna::Param> (p);
            CHECK (std::abs (t.offset (key, param)) <= 1.0f);
            CHECK (std::abs (t.individuality (key, param)) <= 1.0f);
        }
    for (const auto& base : { GranularParams { 0.0, 0.02, 4.0, -12.0, 0.0 }, GranularParams { 1.0, 0.4, 40.0, 12.0, 1.0 }, GranularParams {} })
        for (int key = 0; key < 128; ++key)
        {
            auto p = base;
            keydna::apply (t.at (key), p);
            CHECK (p.position >= 0.0);
            CHECK (p.position <= 1.0);
            CHECK (p.sizeSeconds >= 0.02);
            CHECK (p.sizeSeconds <= 0.4);
            CHECK (p.density >= 4.0);
            CHECK (p.density <= 40.0);
            CHECK (p.spread >= 0.0);
            CHECK (p.spread <= 1.0);
            CHECK (p.tuneSemitones >= -12.0);
            CHECK (p.tuneSemitones <= 12.0);
            CHECK (std::isfinite (p.sizeSeconds));
        }
}

TEST_CASE ("KEY DNA: computing a profile allocates nothing (the audio thread may do it)", "[unit][keydna]")
{
    keydna::Table t;
    const auto s = expressive (5);
    int allocations = 0;
    {
        test::AllocationScope scope;
        t.compute (s);
        t.compute (keydna::Settings {});
        allocations = scope.count();
    }
    CHECK (allocations == 0);
}
