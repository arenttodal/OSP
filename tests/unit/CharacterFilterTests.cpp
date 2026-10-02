#include "engine/CharacterFilter.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace osp;

namespace
{
    // Steady-state gain (dB) of a sine through the filter (left channel).
    double gainDb (FilterType type, double cutoff, double hz, double res = 0.1, double drive = 0.0, double tilt = 0.0, double amp = 0.1)
    {
        CharacterFilter f;
        f.prepare (48000.0);
        double in = 0.0, out = 0.0;
        for (int i = 0; i < 48000; ++i)
        {
            if (i % 32 == 0)
                f.setParameters (type, cutoff, res, drive, tilt);
            float l = static_cast<float> (amp * std::sin (2.0 * std::numbers::pi * hz * i / 48000.0)), r = l;
            const float x = l;
            f.process (l, r);
            if (i > 24000)
            {
                in += x * x;
                out += l * l;
            }
        }
        return 10.0 * std::log10 (out / in);
    }
}

TEST_CASE ("character filter: LP24 is a steep, warm low-pass", "[unit][character]")
{
    CHECK (std::abs (gainDb (FilterType::lp24, 2000.0, 200.0)) < 3.0);       // passband
    CHECK (gainDb (FilterType::lp24, 1000.0, 4000.0) < -30.0);               // two octaves up: ~24 dB/oct
    CHECK (gainDb (FilterType::lp24, 1000.0, 1000.0, 0.85) > gainDb (FilterType::lp24, 1000.0, 1000.0, 0.0) + 6.0); // resonance peak
}

TEST_CASE ("character filter: LP12, HP12, BP12 and TILT do what their names say", "[unit][character]")
{
    const double lp12 = gainDb (FilterType::lp12, 1000.0, 4000.0);
    CHECK (lp12 < -15.0);
    CHECK (lp12 > gainDb (FilterType::lp24, 1000.0, 4000.0) + 10.0);         // gentler than LP24
    CHECK (gainDb (FilterType::hp12, 1000.0, 100.0) < -25.0);
    CHECK (std::abs (gainDb (FilterType::hp12, 1000.0, 8000.0)) < 3.0);
    CHECK (gainDb (FilterType::bp12, 1000.0, 1000.0, 0.4) > gainDb (FilterType::bp12, 1000.0, 100.0, 0.4) + 12.0);
    CHECK (gainDb (FilterType::tilt, 0.0, 6000.0, 0.0, 0.0, 10.0) > 3.0);   // bright
    CHECK (gainDb (FilterType::tilt, 0.0, 6000.0, 0.0, 0.0, -10.0) < -3.0); // dark
}

TEST_CASE ("character filter: full resonance and drive stay bounded and finite", "[unit][character]")
{
    for (auto type : { FilterType::lp24, FilterType::lp12, FilterType::hp12, FilterType::bp12 })
    {
        CharacterFilter f;
        f.prepare (48000.0);
        float peak = 0.0f;
        bool finite = true;
        for (int i = 0; i < 96000; ++i)
        {
            if (i % 32 == 0) // sweep the cutoff fast across the range
                f.setParameters (type, 20.0 * std::pow (1000.0, 0.5 + 0.5 * std::sin (i * 0.0005)), 0.9, 1.0, 0.0);
            float l = (i % 400 < 200) ? 0.9f : -0.9f, r = -l; // loud square
            f.process (l, r);
            finite = finite && std::isfinite (l) && std::isfinite (r);
            peak = std::max ({ peak, std::abs (l), std::abs (r) });
        }
        INFO ("type " << static_cast<int> (type) << " peak " << peak);
        CHECK (finite);
        CHECK (peak < 4.0f);
    }
}

TEST_CASE ("character filter: switching type crossfades without a click", "[unit][character]")
{
    CharacterFilter f;
    f.prepare (48000.0);
    const FilterType order[] = { FilterType::lp24, FilterType::hp12, FilterType::tilt, FilterType::bp12, FilterType::lp12, FilterType::lp24 };
    float previous = 0.0f, worstStep = 0.0f;
    for (int i = 0; i < 6 * 9600; ++i)
    {
        if (i % 32 == 0)
            f.setParameters (order[i / 9600], 1200.0, 0.3, 0.2, 6.0);
        float l = static_cast<float> (0.3 * std::sin (2.0 * std::numbers::pi * 220.0 * i / 48000.0)), r = l;
        f.process (l, r);
        if (i > 100)
            worstStep = std::max (worstStep, std::abs (l - previous));
        previous = l;
    }
    // A 220 Hz sine of 0.3 moves at most ~0.0086 per sample; the switch adds little.
    CHECK (worstStep < 0.05f);
}
