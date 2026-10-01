#include "analysis/spectrum/SpectralEnvelope.h"
#include "audio/pitch/OfflinePitchShifter.h"
#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace osp;

namespace
{
    /** Naive resampling (speed change) for test purposes: reads at `ratio` with linear interpolation. */
    AudioData resample (const AudioData& in, double ratio)
    {
        AudioData out;
        out.sampleRate = in.sampleRate;
        for (const auto& ch : in.channels)
        {
            std::vector<float> y;
            for (double pos = 0.0; pos + 1.0 < static_cast<double> (ch.size()); pos += ratio)
            {
                const auto i = static_cast<std::size_t> (pos);
                const auto f = static_cast<float> (pos - static_cast<double> (i));
                y.push_back (ch[i] + f * (ch[i + 1] - ch[i]));
            }
            out.channels.push_back (std::move (y));
        }
        return out;
    }
}

TEST_CASE ("stretch shifter: transposes and keeps duration", "[unit][pitch-engine]")
{
    const auto source = testsignals::saw (220.0, 2.0, 48000.0, 0.4, 2);
    for (double semis : { -12.0, 7.0, 12.0 })
    {
        StretchShiftOptions options;
        options.semitones = semis;
        const auto out = stretchShiftOffline (source, options);
        REQUIRE (out.numFrames() == source.numFrames());
        REQUIRE (out.numChannels() == 2);
        CHECK (test::analyse (out).pitch.fundamentalHz == Approx (220.0 * semitonesToRatio (semis)).epsilon (0.01));
    }
}

TEST_CASE ("stretch shifter: deterministic", "[unit][pitch-engine]")
{
    const auto source = testsignals::vowel (220.0, 1.5, 48000.0, 4);
    StretchShiftOptions options;
    options.semitones = 5.0;
    options.preserveFormants = true;
    options.formantBaseHz = 220.0;
    CHECK (test::maxDifference (stretchShiftOffline (source, options), stretchShiftOffline (source, options)) == 0.0);
}

TEST_CASE ("spectral envelope: tracks formant movement, ignores pitch", "[unit][pitch-engine]")
{
    // Same formants, different F0: envelope shift ~0.
    const auto low = testsignals::vowel (196.0, 2.0, 48000.0, 1, 1);
    const auto high = testsignals::vowel (294.0, 2.0, 48000.0, 1, 1);
    const auto eLow = computeSpectralEnvelope (low.mixToMono(), 48000.0, 300.0);
    const auto eHigh = computeSpectralEnvelope (high.mixToMono(), 48000.0, 300.0);
    CHECK (std::abs (envelopeShiftSemitones (eLow, eHigh)) < 1.5);

    // Resampling up an octave moves the formants up an octave too.
    const auto chipmunk = resample (low, 2.0);
    const auto eChip = computeSpectralEnvelope (chipmunk.mixToMono(), 48000.0, 400.0);
    CHECK (envelopeShiftSemitones (eLow, eChip) == Approx (12.0).margin (1.5));

    // Formant-preserving transposition keeps them close to where they were.
    StretchShiftOptions options;
    options.semitones = 12.0;
    options.preserveFormants = true;
    options.formantBaseHz = 196.0;
    const auto kept = stretchShiftOffline (low, options);
    const auto eKept = computeSpectralEnvelope (kept.mixToMono(), 48000.0, 400.0);
    CHECK (std::abs (envelopeShiftSemitones (eLow, eKept)) < 3.0);
}
