// Reads far above a recording (Cosmic Strings: a C0 root played around C4, 16x): engine C reads
// an octave-down copy of the source instead of widening the kernel with the speed. The copies
// must read as the original does, within the filters' precision.

#include "audio/pitch/OctaveDecimator.h"
#include "audio/pitch/SincInterpolator.h"
#include "engine/InstrumentEngine.h"
#include "engine/InstrumentVoice.h"
#include "model/PlaybackSource.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace osp;

namespace
{
    constexpr double rate = 48000.0;
    const double twoPi = 2.0 * std::acos (-1.0);

    AudioData tones (double seconds, std::initializer_list<double> hz)
    {
        AudioData a;
        a.sampleRate = rate;
        const auto n = static_cast<std::size_t> (seconds * rate);
        a.channels.assign (2, std::vector<float> (n, 0.0f));
        for (std::size_t i = 0; i < n; ++i)
        {
            double v = 0.0;
            for (const double f : hz)
                v += 0.2 * std::sin (twoPi * f * static_cast<double> (i) / rate);
            a.channels[0][i] = static_cast<float> (v);
            a.channels[1][i] = static_cast<float> (-v);
        }
        return a;
    }
}

TEST_CASE ("octave reads: the decimator keeps DC and the low band, rejects the top, and halves positions exactly", "[unit][octave-read]")
{
    std::vector<float> dc (4000, 0.5f);
    const auto halfDc = decimateByTwo (dc);
    CHECK (halfDc.size() == 2000);
    CHECK (halfDc[1000] == Catch::Approx (0.5f).epsilon (1.0e-5));
    // 1 kHz (well inside the band kept): the same sine at half the rate, in phase.
    std::vector<float> low (48000), high (48000);
    for (std::size_t i = 0; i < low.size(); ++i)
    {
        low[i] = static_cast<float> (std::sin (twoPi * 1000.0 * static_cast<double> (i) / rate));
        high[i] = static_cast<float> (std::sin (twoPi * 20000.0 * static_cast<double> (i) / rate));   // above 3/8 of the rate
    }
    const auto lowHalf = decimateByTwo (low);
    const auto highHalf = decimateByTwo (high);
    double lowErr = 0.0, highPeak = 0.0;
    for (std::size_t k = 100; k < lowHalf.size() - 100; ++k)
    {
        lowErr = std::max (lowErr, std::abs (static_cast<double> (lowHalf[k]) - std::sin (twoPi * 1000.0 * static_cast<double> (2 * k) / rate)));
        highPeak = std::max (highPeak, std::abs (static_cast<double> (highHalf[k])));
    }
    CHECK (lowErr < 1.0e-4);
    CHECK (highPeak < 1.0e-4);   // (-80 dB or better)
}

TEST_CASE ("octave reads: a source keeps its copies, padded; reading a copy far above the recording matches the original", "[unit][octave-read]")
{
    // Content below the fastest read's own Nyquist (48 kHz / 2 / 24 = 1 kHz): what such reads keep.
    const auto audio = tones (2.0, { 110.0, 330.0, 600.0 });
    const int pad = InstrumentEngine::requiredSourcePaddingFor (16);
    PlaybackSource source (audio, 60.0, pad);
    source.buildOctaveLevels (InstrumentVoice::octaveReadLevels);
    REQUIRE (source.octaveLevels() == InstrumentVoice::octaveReadLevels);
    CHECK (source.octaveData (0, 1) == source.channelData (1));
    // The padding reads as silence on both sides of every copy.
    for (int level = 1; level <= source.octaveLevels(); ++level)
    {
        CHECK (source.octaveData (level, 0)[-1] == 0.0f);
        CHECK (source.octaveData (level, 0)[-pad] == 0.0f);
    }
    SincInterpolator sinc (16);
    sinc.prepareStretchTables();
    SincInterpolator::LevelCache cacheA, cacheB;
    SincInterpolator::Kernel kernelA, kernelB;
    for (const double step : { 6.0, 12.0, 16.0, 24.0 })
    {
        INFO ("step " << step);
        const int level = std::min (source.octaveLevels(), static_cast<int> (std::ceil (std::log2 (step / SincInterpolator::maxTableStretch))));
        const double scale = std::ldexp (1.0, -level);
        double worst = 0.0;
        for (double pos = 2000.5; pos < 80000.0; pos += 977.37)
        {
            float l0 = 0.0f, r0 = 0.0f, l1 = 0.0f, r1 = 0.0f;
            sinc.readFast (pos, step, cacheA, kernelA, source.channelData (0), source.channelData (1), l0, r0);
            sinc.readFast (pos * scale, step * scale, cacheB, kernelB, source.octaveData (level, 0), source.octaveData (level, 1), l1, r1);
            worst = std::max ({ worst, std::abs (static_cast<double> (l0 - l1)), std::abs (static_cast<double> (r0 - r1)) });
        }
        CHECK (worst < 2.0e-3);   // (-54 dB of a full-scale tone: the two kernels' own small differences)
    }
}
