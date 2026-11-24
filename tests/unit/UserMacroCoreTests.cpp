// USER MACROS (ported from ANDOR ANALOG with the core), gate 1: the pure core - linear maths (zero jump on assigning), sanitising, the
// bounded table and its lock-free hand-over (docs/USER_MACROS_V1_1.md).

#include "engine/usermacro/UserMacroCore.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <thread>

using namespace osp::usermacro;
using Catch::Approx;

namespace
{
    Mapping made (float macroNow, float targetNow)
    {
        Mapping m;
        m.anchorMacro = macroNow;
        m.anchorTarget = targetNow;
        m.low = m.high = targetNow;   // as made: no influence
        return m;
    }
}

TEST_CASE ("M06: a new assignment never jumps - its default range is a line through where the target is, at 0 %, 50 %, 73 %, 100 %", "[usermacro][unit]")
{
    for (const float a : { 0.0f, 0.5f, 0.73f, 0.95f, 1.0f })
        for (const float b0 : { 0.0f, 0.05f, 0.41f, 0.9f, 1.0f })
        {
            INFO ("made at " << a << ", target at " << b0);
            auto m = made (a, b0);
            for (double x = 0.0; x <= 1.0; x += 0.01)
                CHECK (offsetOf (m, x) == 0.0);   // L = H = b0: nothing anywhere
            const auto [low, high] = defaultRange (a, b0);
            m.low = low;
            m.high = high;
            CHECK (low >= 0.0f);
            CHECK (high <= 1.0f);
            CHECK (std::abs (high - low) >= 0.0999f);   // (always a range to see)
            const bool edgeCase = (b0 <= 0.0f || b0 >= 1.0f) && a > 0.0f && a < 1.0f;
            if (! edgeCase)
                CHECK (offsetOf (m, a) == Approx (0.0).margin (1.0e-6));   // no jump where it was made
            // Linear: equal macro steps, equal target steps, over the whole travel.
            const double step = offsetOf (m, 0.1) - offsetOf (m, 0.0);
            for (double x = 0.1; x < 0.95; x += 0.1)
                CHECK (offsetOf (m, x + 0.1) - offsetOf (m, x) == Approx (step).margin (1.0e-6));
        }
}

TEST_CASE ("M06b: the reported bug - assigned with the macro at 95 %, the target still moves evenly from 0 % to 100 %", "[usermacro][unit]")
{
    auto m = made (0.95f, 0.5f);
    m.low = 0.2f;
    m.high = 0.8f;
    CHECK (offsetOf (m, 0.0) == Approx (-0.3));
    CHECK (offsetOf (m, 0.5) == Approx (0.0).margin (1.0e-6));
    CHECK (offsetOf (m, 0.95) == Approx (0.27));
    CHECK (offsetOf (m, 1.0) == Approx (0.3));
    // Half the macro's travel moves the target half its range (not 5 %).
    CHECK ((offsetOf (m, 0.5) - offsetOf (m, 0.0)) / (offsetOf (m, 1.0) - offsetOf (m, 0.0)) == Approx (0.5));
}

TEST_CASE ("M07: the range - LOW at 0, HIGH at 1, linear between; inverted when LOW is above HIGH", "[usermacro][unit]")
{
    auto m = made (0.0f, 0.41f);
    m.high = 0.80f;
    CHECK (offsetOf (m, 0.0) == Approx (0.0));
    CHECK (offsetOf (m, 0.5) == Approx (0.195));
    CHECK (offsetOf (m, 1.0) == Approx (0.39));
    // Made at 73 %: the same straight line from LOW to HIGH.
    auto mid = made (0.73f, 0.5f);
    mid.low = 0.2f;
    mid.high = 0.9f;
    CHECK (offsetOf (mid, 0.0) == Approx (-0.3));
    CHECK (offsetOf (mid, 1.0) == Approx (0.4));
    CHECK (offsetOf (mid, 0.5) == Approx (0.05));
    // Inverted (SWAP): the same code.
    auto inv = made (0.0f, 0.6f);
    inv.high = 0.1f;
    CHECK (offsetOf (inv, 1.0) == Approx (-0.5));
    // EASE shapes the travel, the ends are the same.
    auto eased = m;
    eased.curve = Curve::ease;
    CHECK (offsetOf (eased, 1.0) == Approx (0.39));
    CHECK (offsetOf (eased, 0.25) < offsetOf (m, 0.25));
    // Bypassed: nothing.
    m.enabled = false;
    CHECK (offsetOf (m, 1.0) == 0.0);
    // Out-of-range macro values are clamped.
    m.enabled = true;
    CHECK (offsetOf (m, 7.0) == Approx (0.39));
    CHECK (offsetOf (m, -3.0) == Approx (0.0));
}

TEST_CASE ("M07b: schema-1 assignments read as lines - the same line when made at 0 % or 100 %, the same ends otherwise", "[usermacro][unit]")
{
    // Made at 0 %: LOW was never used (the target sat at b0 there).
    auto zero = made (0.0f, 0.4f);
    zero.low = 0.15f;
    zero.high = 0.65f;
    const auto z = linearisedFromSchema1 (zero);
    CHECK (z.low == 0.4f);
    CHECK (offsetOf (z, 0.0) == Approx (0.0));
    CHECK (offsetOf (z, 0.5) == Approx (0.125));
    CHECK (offsetOf (z, 1.0) == Approx (0.25));
    // Made at 100 %: HIGH was never used.
    auto full = made (1.0f, 0.4f);
    full.low = 0.1f;
    full.high = 0.9f;
    const auto f = linearisedFromSchema1 (full);
    CHECK (f.high == 0.4f);
    CHECK (offsetOf (f, 0.0) == Approx (-0.3));
    CHECK (offsetOf (f, 1.0) == Approx (0.0));
    // Made part-way: the ends kept.
    auto part = made (0.95f, 0.5f);
    part.low = 0.25f;
    part.high = 0.75f;
    CHECK (linearisedFromSchema1 (part) == part);
}

TEST_CASE ("M08: two macros on one target add, then clamp once (the caller's job, shown here)", "[usermacro][unit]")
{
    auto a = made (0.0f, 0.5f);
    a.high = 0.9f;
    auto b = made (0.0f, 0.5f);
    b.high = 1.0f;
    const double base = 0.5;
    const double total = std::clamp (base + offsetOf (a, 1.0) + offsetOf (b, 1.0), 0.0, 1.0);
    CHECK (total == Approx (1.0));
    CHECK (offsetOf (a, 1.0) + offsetOf (b, 1.0) == Approx (0.9));
}

TEST_CASE ("M12 (core): bad numbers are made safe - NaN, infinities, out of range", "[usermacro][unit]")
{
    Mapping m;
    m.anchorMacro = std::numeric_limits<float>::quiet_NaN();
    m.anchorTarget = 3.0f;
    m.low = -std::numeric_limits<float>::infinity();
    m.high = 0.7f;
    const auto s = sanitised (m);
    CHECK (s.anchorMacro == 0.0f);
    CHECK (s.anchorTarget == 1.0f);
    CHECK (s.low == 1.0f);   // (a non-finite end: neutral)
    CHECK (s.high == 0.7f);
    for (double x = 0.0; x <= 1.0; x += 0.1)
        CHECK (std::isfinite (offsetOf (s, x)));
}

TEST_CASE ("USER MACROS: the triple buffer hands the latest table over, never torn, without waiting", "[usermacro][unit]")
{
    TripleBuffer<Snapshot> buffer;
    CHECK (buffer.read().count == 0);
    Snapshot s;
    s.count = 1;
    s.maps[0].index = 7;
    buffer.publish (s);
    CHECK (buffer.read().count == 1);
    CHECK (buffer.read().maps[0].index == 7);   // (read again: the same)
    // A writer far ahead of the reader: the reader sees the newest complete one.
    for (int i = 2; i <= 10; ++i)
    {
        s.count = i;
        s.maps[0].index = static_cast<std::int16_t> (i * 3);
        buffer.publish (s);
    }
    CHECK (buffer.read().count == 10);
    CHECK (buffer.read().maps[0].index == 30);
    // Two threads: every table the reader sees is one the writer wrote whole.
    std::atomic<bool> done { false };
    bool consistent = true;
    std::thread reader ([&] {
        while (! done.load())
        {
            const auto& t = buffer.read();
            consistent = consistent && t.maps[0].index == static_cast<std::int16_t> (t.count * 3) && t.maps[1].index == static_cast<std::int16_t> (t.count);
        }
    });
    for (int i = 11; i < 20000; ++i)
    {
        s.count = i;
        s.maps[0].index = static_cast<std::int16_t> (i * 3);
        s.maps[1].index = static_cast<std::int16_t> (i);
        buffer.publish (s);
    }
    done = true;
    reader.join();
    CHECK (consistent);
}
