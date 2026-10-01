#include "audio/utility/TestSignals.h"
#include "model/PlaybackPreparation.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace osp;

namespace
{
    /** `silence` seconds of nothing, then a sine at `amplitude`. */
    AudioData delayedTone (double silence, double amplitude)
    {
        auto audio = testsignals::sine (220.0, silence + 1.0, 48000.0, amplitude);
        for (std::size_t i = 0; i < static_cast<std::size_t> (silence * 48000.0); ++i)
            audio.channels[0][i] = 0.0f;
        return audio;
    }
}

TEST_CASE ("playback preparation: defaults leave the recording untouched", "[unit][playback]")
{
    const auto prep = preparePlayback (test::analyse (delayedTone (0.5, 0.01)), PlaybackOptions {});
    CHECK (prep.startSeconds == 0.0);
    CHECK (prep.gainDb == 0.0);
}

TEST_CASE ("playback preparation: start just before the onset", "[unit][playback]")
{
    PlaybackOptions options;
    options.startAtOnset = true;
    const auto prep = preparePlayback (test::analyse (delayedTone (0.5, 0.5)), options);
    CHECK (prep.startSeconds == Approx (0.5 - options.onsetPrerollSeconds).margin (0.015));

    const auto immediate = preparePlayback (test::analyse (delayedTone (0.0, 0.5)), options);
    CHECK (immediate.startSeconds == 0.0);
}

TEST_CASE ("playback preparation: level matching is peak-limited and bounded", "[unit][playback]")
{
    PlaybackOptions options;
    options.normaliseLevel = true;

    // A sine's max RMS is 3 dB below its peak. Quiet source (-40 dBFS peak): boost to -16 dBFS RMS.
    const auto quiet = preparePlayback (test::analyse (delayedTone (0.0, 0.01)), options);
    CHECK (quiet.gainDb == Approx (-16.0 - (-43.01)).margin (0.2));

    // Loud source: the -1 dBFS peak ceiling wins over the RMS target.
    const auto loud = preparePlayback (test::analyse (delayedTone (0.0, 0.99)), options);
    CHECK (loud.gainDb <= -1.0 - (-0.087) + 0.01);

    // Silence: no gain at all (never amplify nothing).
    CHECK (preparePlayback (test::analyse (testsignals::silence (1.0, 48000.0)), options).gainDb == 0.0);
}
