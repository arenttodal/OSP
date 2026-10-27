// The engine's fast numerics (docs/reports/cpu-three-layers.md, step 2) against the plain
// forms they replaced: each differs by float rounding only. The reference forms are written
// out here as they were, so a later change to the fast ones is measured against the original.

#include "audio/pitch/SincInterpolator.h"
#include "core/Prng.h"
#include "engine/CharacterFilter.h"
#include "engine/FastMath.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <vector>

using namespace osp;

TEST_CASE ("fast numerics: the lane-summed sinc read matches the kernel and apply", "[unit][numerics]")
{
    SincInterpolator sinc (16);
    sinc.prepareStretchTables();
    const int pad = sinc.maxReach() + 8, frames = 4000;
    Prng rng (7);
    std::vector<float> left (static_cast<std::size_t> (frames + 2 * pad), 0.0f), right = left;
    for (int i = 0; i < frames; ++i)
    {
        left[static_cast<std::size_t> (pad + i)] = static_cast<float> (rng.bipolar());
        right[static_cast<std::size_t> (pad + i)] = static_cast<float> (rng.bipolar());
    }
    const float* l0 = left.data() + pad;
    const float* r0 = right.data() + pad;
    SincInterpolator::Kernel kernel, scratch;
    double worst = 0.0;
    for (double increment : { 0.37, 1.0, 1.06, 1.5, 2.9, 4.0, 5.5, 12.0 })
    {
        SincInterpolator::LevelCache a, b;
        for (int n = 0; n < 500; ++n)
        {
            const double pos = 300.0 + rng.nextDouble() * 3300.0;
            sinc.computeKernelFast (pos, increment, kernel, a);
            const float wantL = SincInterpolator::apply (kernel, l0), wantR = SincInterpolator::apply (kernel, r0);
            float gotL = 0.0f, gotR = 0.0f, monoL = 0.0f, monoR = 0.0f;
            sinc.readFast (pos, increment, b, scratch, l0, r0, gotL, gotR);
            sinc.readFast (pos, increment, b, scratch, l0, nullptr, monoL, monoR);
            worst = std::max ({ worst, std::abs (static_cast<double> (gotL - wantL)), std::abs (static_cast<double> (gotR - wantR)),
                                std::abs (static_cast<double> (monoL - wantL)) });
            REQUIRE (monoR == monoL);
        }
    }
    std::cout << "  sinc read: worst difference " << worst << " (taps of full-scale noise)\n";
    CHECK (worst < 2.0e-6);   // float rounding of a sum of 32-512 products
}

TEST_CASE ("fast numerics: the rational tanh is within 4e-7 of tanh, odd and bounded", "[unit][numerics]")
{
    double worst = 0.0;
    float previous = -2.0f;
    for (int i = -200000; i <= 200000; ++i)
    {
        const auto x = static_cast<float> (i) * 1.0e-4f;
        const float y = fastTanh (x);
        worst = std::max (worst, std::abs (static_cast<double> (y) - std::tanh (static_cast<double> (x))));
        if (y < previous - 4.0e-7f)   // monotonic but for wobbles within its error near +-1
            FAIL ("tanh falls at " << x);
        previous = y;
        if (i > 0)
            REQUIRE (fastTanh (-x) == -y);
    }
    std::cout << "  tanh: worst difference " << worst << "\n";
    CHECK (worst < 4.0e-7);
    CHECK (fastTanh (0.0f) == 0.0f);
    CHECK (std::abs (fastTanh (1.0e6f)) <= 1.0f + 1.0e-6f);
    CHECK (std::abs (fastTanh (-1.0e6f)) <= 1.0f + 1.0e-6f);
}

TEST_CASE ("fast numerics: Hermite tap weights are the c0..c3 form regrouped", "[unit][numerics]")
{
    Prng rng (11);
    double worst = 0.0;
    for (int n = 0; n < 20000; ++n)
    {
        float x[4];
        for (auto& v : x)
            v = static_cast<float> (rng.bipolar());
        const auto t = static_cast<float> (rng.nextDouble());
        const float c1 = 0.5f * (x[2] - x[0]);
        const float c2 = x[0] - 2.5f * x[1] + 2.0f * x[2] - 0.5f * x[3];
        const float c3 = 0.5f * (x[3] - x[0]) + 1.5f * (x[1] - x[2]);
        const float want = ((c3 * t + c2) * t + c1) * t + x[1];
        const float got = (hermiteWeights (t) * simd::Float4::load (x)).sum();
        worst = std::max (worst, std::abs (static_cast<double> (got - want)));
    }
    std::cout << "  hermite: worst difference " << worst << "\n";
    CHECK (worst < 1.0e-6);
}

TEST_CASE ("fast numerics: the phasor follows sin of its phase for ten minutes", "[unit][numerics]")
{
    const double omega = 2.0 * std::numbers::pi * 0.45 / 48000.0, start = 1.234;
    Phasor p;
    p.reset (start, omega);
    double worst = 0.0;
    const long long steps = 10LL * 60 * 48000;
    for (long long n = 0; n < steps; ++n)
    {
        if (n % 9973 == 0)
            worst = std::max (worst, std::abs (p.sin() - std::sin (start + static_cast<double> (n) * omega)));
        p.advance();
    }
    std::cout << "  phasor: worst difference " << worst << " after ten minutes\n";
    CHECK (worst < 1.0e-8);
}

namespace
{
    // The LP24 ladder as it was written before its two channels shared one pass.
    struct ReferenceLadder
    {
        float G, k, comp, driveGain, driveBias, driveOut, s[4] {};

        static float softClip (float x)
        {
            x = std::clamp (x, -3.0f, 3.0f);
            const float x2 = x * x;
            return x * (27.0f + x2) / (27.0f + 9.0f * x2);
        }
        ReferenceLadder (double fc, double res, double drive, double rate)
        {
            const double gw = std::tan (std::numbers::pi * fc / rate);
            G = static_cast<float> (gw / (1.0 + gw));
            k = static_cast<float> (3.8 * std::pow (res, 0.85));
            comp = 1.0f + 0.5f * k;
            driveGain = static_cast<float> (std::pow (10.0, (2.0 + 18.0 * drive * drive) / 20.0) * 0.5);
            driveBias = static_cast<float> (0.25 * drive);
            driveOut = static_cast<float> (1.0 / std::pow (driveGain, 0.85));
        }
        float process (float in)
        {
            const float x = in * driveGain;
            const float beta = 1.0f - G;
            const float sigma = beta * (G * G * G * s[0] + G * G * s[1] + G * s[2] + s[3]);
            const float g4 = G * G * G * G;
            float u = (x - k * sigma) / (1.0f + k * g4);
            u = softClip (u + driveBias) - softClip (driveBias);
            float y = u;
            for (auto& stage : s)
            {
                const float v = (y - stage) * G;
                const float out = v + stage;
                stage = out + v;
                y = out;
            }
            return y * comp * driveOut;
        }
    };
}

TEST_CASE ("fast numerics: the two-lane ladder matches the ladder it replaced", "[unit][numerics][character]")
{
    for (const auto& [fc, res, drive] : { std::tuple { 800.0, 0.9, 0.5 }, std::tuple { 3000.0, 0.3, 0.0 }, std::tuple { 200.0, 0.85, 1.0 } })
    {
        CharacterFilter f;
        f.prepare (48000.0);
        f.setParameters (FilterType::lp24, fc, res, drive, 0.0);
        ReferenceLadder refL (fc, std::clamp (res, 0.0, 0.9), drive, 48000.0), refR = refL;
        Prng rng (3);
        double diff = 0.0, signal = 0.0;
        for (int i = 0; i < 48000; ++i)
        {
            float l = static_cast<float> (0.5 * rng.bipolar()), r = static_cast<float> (0.3 * std::sin (0.01 * i));
            const float wantL = refL.process (l), wantR = refR.process (r);
            f.process (l, r);
            diff += (l - wantL) * (l - wantL) + (r - wantR) * (r - wantR);
            signal += wantL * wantL + wantR * wantR;
        }
        const double relDb = 10.0 * std::log10 (std::max (diff, 1.0e-300) / signal);
        std::cout << "  ladder " << fc << " Hz, res " << res << ", drive " << drive << ": difference " << relDb << " dB below the output\n";
        CHECK (relDb < -110.0);
    }
}
