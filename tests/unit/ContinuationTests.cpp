#include "analysis/continuation/ContinuationAnalyzer.h"
#include "audio/utility/TestSignals.h"
#include "support/TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace osp;

namespace
{
    AudioData sustainedVowel (double seconds = 4.0, double fadeOut = 0.0)
    {
        auto audio = testsignals::vowel (220.0, seconds, 48000.0, 3);
        testsignals::applyFades (audio, 0.05, fadeOut);
        return audio;
    }
}

TEST_CASE ("continuation: sustained vowel gets a stable region and compatible jumps", "[unit][continuation]")
{
    const auto audio = sustainedVowel();
    const auto analysis = test::analyse (audio);
    const auto c = analyseContinuation (audio, analysis);

    REQUIRE (c.canSustain);
    REQUIRE (c.hasJumps());
    CHECK (c.jumps.size() >= 4);
    CHECK (c.sustainStartFrame > 0.0);
    CHECK (c.sustainEndFrame > c.sustainStartFrame + 0.5 * audio.sampleRate);

    // Invariant that keeps the random walk alive: every destination lies before the
    // backstop jump with room for a full segment.
    const auto& backstop = c.jumps[static_cast<std::size_t> (c.backstop)];
    for (const auto& j : c.jumps)
    {
        CHECK (j.toFrame <= backstop.fromFrame - c.minSegmentFrames);
        CHECK (j.crossfadeFrames > 0.0);
        CHECK (j.fromFrame + j.crossfadeFrames <= static_cast<double> (audio.numFrames()));
        CHECK (j.toFrame + j.crossfadeFrames <= static_cast<double> (audio.numFrames()));
    }
    // Periodic material aligns well; the synthetic vowel's vibrato keeps waveform
    // correlation over a crossfade window near 0.7 (real vocals measure 0.75-0.8).
    double meanR = 0.0;
    for (const auto& j : c.jumps)
        meanR += j.correlation;
    CHECK (meanR / static_cast<double> (c.jumps.size()) > 0.6);

    CHECK (c.bestLoop >= 0);
    CHECK (c.naiveLoop.fromFrame > c.naiveLoop.toFrame);
}

TEST_CASE ("continuation: a natural ending gives release-graft exits", "[unit][continuation]")
{
    const auto audio = sustainedVowel (4.0, 1.0);
    const auto analysis = test::analyse (audio);
    const auto c = analyseContinuation (audio, analysis);
    REQUIRE (c.canSustain);
    CHECK (c.hasRelease);
    CHECK (c.tailSeconds > 0.3);
    REQUIRE_FALSE (c.graftExits.empty());
    for (const auto& e : c.graftExits)
    {
        CHECK (e.fromFrame >= c.sustainStartFrame - 1.0);
        CHECK (e.toFrame <= c.releaseFrame + 1.0);
    }
}

TEST_CASE ("continuation: decaying plucks stay one-shot", "[unit][continuation]")
{
    const auto audio = testsignals::pluck (196.0, 3.0, 48000.0, 5);
    const auto c = analyseContinuation (audio, test::analyse (audio));
    CHECK_FALSE (c.canSustain);
    CHECK (c.reason == "decaying source");
}

TEST_CASE ("continuation: odd input never fails", "[unit][continuation]")
{
    for (const auto& audio : { testsignals::silence (2.0, 48000.0), testsignals::sine (440.0, 0.05, 48000.0),
                               testsignals::whiteNoise (3.0, 48000.0, 0.3, 9), testsignals::impulse (1.0, 48000.0) })
    {
        const auto c = analyseContinuation (audio, test::analyse (audio));
        CHECK_FALSE (c.reason.empty());
        if (c.canSustain)
            CHECK (c.hasJumps());
    }
}

TEST_CASE ("continuation: crossfade gains keep power for uncorrelated and amplitude for identical signals", "[unit][continuation]")
{
    float out = 0.0f, in = 0.0f;
    crossfadeGains (0.5f, 1.0f, out, in);
    CHECK (std::abs (out + in - 1.0f) < 1.0e-5f);
    crossfadeGains (0.5f, 0.0f, out, in);
    CHECK (std::abs (out * out + in * in - 1.0f) < 1.0e-5f);
    crossfadeGains (0.0f, 0.3f, out, in);
    CHECK (out == 1.0f);
    CHECK (in == 0.0f);
    crossfadeGains (1.0f, 0.3f, out, in);
    CHECK (out == 0.0f);
    CHECK (in == 1.0f);
}

TEST_CASE ("continuation: per-layer refinement keeps the jump set", "[unit][continuation]")
{
    const auto audio = sustainedVowel();
    const auto c = analyseContinuation (audio, test::analyse (audio));
    REQUIRE (c.hasJumps());
    const auto layer = testsignals::vowel (440.0, 4.0, 48000.0, 3);
    const auto r = refineContinuationForLayer (c, layer, 440.0);
    REQUIRE (r.jumps.size() == c.jumps.size());
    for (std::size_t i = 0; i < r.jumps.size(); ++i)
    {
        CHECK (r.jumps[i].fromFrame == c.jumps[i].fromFrame);
        CHECK (std::abs (r.jumps[i].toFrame - c.jumps[i].toFrame) <= 0.025 * 48000.0 + 1.0);
    }
}
