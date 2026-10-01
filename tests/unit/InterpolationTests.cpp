#include "audio/pitch/SincInterpolator.h"
#include "model/PlaybackSource.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using Catch::Approx;
using namespace osp;

namespace
{
    AudioData sineSource (double cyclesPerSample, int frames)
    {
        auto audio = AudioData::allocate (1, frames, 48000.0);
        for (int i = 0; i < frames; ++i)
            audio.channels[0][static_cast<std::size_t> (i)] = static_cast<float> (std::sin (2.0 * std::numbers::pi * cyclesPerSample * i));
        return audio;
    }

    /** Reads `count` samples starting at `start` with the given increment. */
    std::vector<double> read (const SincInterpolator& sinc, const PlaybackSource& src, double start, double increment, int count)
    {
        std::vector<double> out;
        SincInterpolator::Kernel kernel;
        double pos = start;
        for (int i = 0; i < count; ++i, pos += increment)
        {
            sinc.computeKernel (pos, increment, kernel);
            out.push_back (SincInterpolator::apply (kernel, src.channelData (0)));
        }
        return out;
    }
}

TEST_CASE ("interpolation: DC is preserved at any position and increment", "[unit][interpolation]")
{
    const SincInterpolator sinc (16);
    auto audio = AudioData::allocate (1, 4000, 48000.0);
    for (auto& s : audio.channels[0])
        s = 0.75f;
    const PlaybackSource src (audio, 60.0, 2 * sinc.maxReach() + 4);

    for (double inc : { 0.25, 0.5, 1.0, 1.4983, 2.0, 4.0 })
        for (double v : read (sinc, src, 1000.123, inc, 200))
            REQUIRE (v == Approx (0.75).margin (1e-4));
}

TEST_CASE ("interpolation: integer positions at unity rate reproduce the source", "[unit][interpolation]")
{
    const SincInterpolator sinc (16);
    const auto audio = sineSource (0.013, 4000);
    const PlaybackSource src (audio, 60.0, 2 * sinc.maxReach() + 4);
    const auto out = read (sinc, src, 500.0, 1.0, 1000);
    for (int i = 0; i < 1000; ++i)
        REQUIRE (out[static_cast<std::size_t> (i)] == Approx (audio.channels[0][static_cast<std::size_t> (500 + i)]).margin (2e-3));
}

TEST_CASE ("interpolation: fractional reads follow the band-limited signal", "[unit][interpolation]")
{
    const SincInterpolator sinc (16);
    const double f = 0.05; // cycles per sample, well inside the passband
    const auto audio = sineSource (f, 8000);
    const PlaybackSource src (audio, 60.0, 2 * sinc.maxReach() + 4);

    const double start = 1000.37;
    const auto out = read (sinc, src, start, 0.5, 2000); // one octave down
    double worst = 0.0;
    for (int i = 0; i < 2000; ++i)
    {
        const double expected = std::sin (2.0 * std::numbers::pi * f * (start + 0.5 * i));
        worst = std::max (worst, std::abs (out[static_cast<std::size_t> (i)] - expected));
    }
    CHECK (worst < 2e-3);
}

TEST_CASE ("interpolation: reading faster suppresses content that would alias", "[unit][interpolation]")
{
    const SincInterpolator sinc (16);
    // 0.4 cycles/sample read at increment 2 would alias to 0.8 -> 0.2 cycles/output-sample.
    const auto audio = sineSource (0.4, 20000);
    const PlaybackSource src (audio, 60.0, 2 * sinc.maxReach() + 4);
    const auto out = read (sinc, src, 2000.0, 2.0, 4000);
    double sum = 0.0;
    for (double v : out)
        sum += v * v;
    const double rmsDb = 10.0 * std::log10 (sum / out.size() + 1e-30);
    CHECK (rmsDb < -50.0); // source RMS is -3 dB

    // In-band content survives the same read.
    const auto lowAudio = sineSource (0.1, 20000);
    const PlaybackSource lowSrc (lowAudio, 60.0, 2 * sinc.maxReach() + 4);
    const auto lowOut = read (sinc, lowSrc, 2000.0, 2.0, 4000);
    double lowSum = 0.0;
    for (double v : lowOut)
        lowSum += v * v;
    CHECK (10.0 * std::log10 (lowSum / lowOut.size()) == Approx (-3.01).margin (0.1));
}

TEST_CASE ("interpolation: reads beyond the ends see silence", "[unit][interpolation]")
{
    const SincInterpolator sinc (8);
    auto audio = AudioData::allocate (1, 100, 48000.0);
    for (auto& s : audio.channels[0])
        s = 1.0f;
    const PlaybackSource src (audio, 60.0, 2 * sinc.maxReach() + 4);
    const auto before = read (sinc, src, -static_cast<double> (sinc.reachFor (1.0)) - 1.0, 1.0, 1);
    const auto after = read (sinc, src, 100.0 + sinc.reachFor (4.0) + 0.5, 4.0, 1);
    CHECK (before[0] == Approx (0.0).margin (1e-6));
    CHECK (after[0] == Approx (0.0).margin (1e-6));
}
