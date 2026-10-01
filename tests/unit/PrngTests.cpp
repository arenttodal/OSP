#include "core/Prng.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>
#include <vector>

using Catch::Approx;
using osp::Prng;

TEST_CASE ("PRNG: same seed gives the exact same sequence", "[unit][prng]")
{
    Prng a (1234), b (1234);
    for (int i = 0; i < 10000; ++i)
        REQUIRE (a.nextU64() == b.nextU64());

    a.reseed (99);
    b.reseed (99);
    for (int i = 0; i < 1000; ++i)
    {
        REQUIRE (a.nextDouble() == b.nextDouble());
        REQUIRE (a.gaussian() == b.gaussian());
    }
}

TEST_CASE ("PRNG: algorithm is frozen (golden values)", "[unit][prng]")
{
    // If this fails, the generator changed and every stored seed now produces different
    // performances. Do not update these values casually: it breaks DAW-session recall.
    Prng rng (0);
    CHECK (rng.nextU64() == 0x99EC5F36CB75F2B4ull);
    CHECK (rng.nextU64() == 0xBF6E1F784956452Aull);
    CHECK (Prng::deriveSeed (1, 2, 3) == Prng::deriveSeed (1, 2, 3));
    CHECK (Prng::deriveSeed (1, 2, 3) != Prng::deriveSeed (1, 3, 2));
}

TEST_CASE ("PRNG: different seeds diverge", "[unit][prng]")
{
    Prng a (1), b (2);
    int equal = 0;
    for (int i = 0; i < 1000; ++i)
        equal += a.nextU64() == b.nextU64() ? 1 : 0;
    CHECK (equal == 0);

    std::set<std::uint64_t> seeds;
    for (std::uint64_t note = 0; note < 128; ++note)
        for (std::uint64_t counter = 0; counter < 16; ++counter)
            seeds.insert (Prng::deriveSeed (42, counter, note));
    CHECK (seeds.size() == 128 * 16);
}

TEST_CASE ("PRNG: distributions are sane", "[unit][prng]")
{
    Prng rng (7);
    const int n = 200000;
    double sum = 0.0, sumSq = 0.0, gSum = 0.0, gSq = 0.0;
    double lo = 1.0, hi = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double u = rng.nextDouble();
        lo = std::min (lo, u);
        hi = std::max (hi, u);
        sum += u;
        sumSq += u * u;
        const double g = rng.gaussian();
        gSum += g;
        gSq += g * g;
    }
    CHECK (lo >= 0.0);
    CHECK (hi < 1.0);
    CHECK (sum / n == Approx (0.5).margin (0.005));
    CHECK (sumSq / n - (sum / n) * (sum / n) == Approx (1.0 / 12.0).margin (0.002));
    CHECK (gSum / n == Approx (0.0).margin (0.01));
    CHECK (gSq / n == Approx (1.0).margin (0.02));

    for (int i = 0; i < 1000; ++i)
    {
        const double v = rng.uniform (-3.0, 5.0);
        REQUIRE (v >= -3.0);
        REQUIRE (v < 5.0);
        REQUIRE (rng.nextBelow (7) < 7u);
    }
}
