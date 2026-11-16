// LOOP's fallback for short notes (docs/research/short-loop.md): a short sustained recording the
// continuation analysis cannot loop holds on one crossfaded loop over its body (Note Atlas's
// forced loop), when the layer's LOOP is on and its fallback allowed. Struck and plucked notes
// still play once; LOOP off plays once; the engine's default (fallback off) is the original.

#include "analysis/continuation/ContinuationAnalyzer.h"
#include "audio/utility/TestSignals.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "io/AudioFileIO.h"
#include "support/TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>

using namespace osp;

namespace
{
    constexpr double rate = 48000.0;

    /** A sustained tone of `seconds` (an organ's or a bowed note's kind), short fades. */
    AudioData sustained (double seconds)
    {
        auto a = testsignals::vibratoSine (220.0, 5.0, 10.0, seconds, rate, 0.4);
        testsignals::applyFades (a, 0.01, std::min (0.03, 0.2 * seconds));
        return a;
    }

    std::shared_ptr<const InstrumentModel> build (const AudioData& audio)
    {
        return instrument::buildComplete (audio, test::analyse (audio), {}, false);
    }

    /** Holds A3 for `seconds` and returns the level (dB) over the last 0.3 s it was held. */
    double heldLevelDb (const std::shared_ptr<const InstrumentModel>& model, bool loop, bool fallback, double seconds = 2.0)
    {
        EngineSettings s;
        s.macros.life = 0.0;
        s.macros.space = 0.0;
        s.macros.motion = 0.0;
        s.macros.reimagined = 0.0;
        s.shaping = Shaping::neutral();
        s.layer[0].loop = loop;
        s.layer[0].fallbackLoop = fallback;
        auto engine = std::make_unique<InstrumentEngine>();
        engine->prepare (rate, 256, s);
        engine->setModel (model.get(), 0);
        engine->noteOn (57, 100);
        std::vector<float> l (256), r (256), out;
        const auto total = static_cast<int> (seconds * rate);
        for (int done = 0; done < total; done += 256)
        {
            float* ch[2] = { l.data(), r.data() };
            engine->render (ch, 2, 256);
            out.insert (out.end(), l.begin(), l.end());
        }
        double sum = 0.0;
        const auto from = static_cast<std::size_t> ((seconds - 0.3) * rate);
        for (auto i = from; i < out.size(); ++i)
            sum += static_cast<double> (out[i]) * out[i];
        return 10.0 * std::log10 (std::max (1.0e-20, sum / static_cast<double> (out.size() - from)));
    }
} // namespace

TEST_CASE ("short loop: the fallback is made only for short notes the analysis cannot loop", "[unit][short-loop]")
{
    // The duration matrix: short sustained notes get a fallback; long ones loop on their own.
    for (const double seconds : { 0.3, 0.4, 0.6 })
    {
        INFO (seconds << " s");
        const auto model = build (sustained (seconds));
        REQUIRE (model != nullptr);
        CHECK_FALSE (model->original.continuation.canSustain);
        CHECK (model->original.fallbackContinuation.canSustain);
        CHECK (model->original.fallbackContinuation.jumps.size() == 1);
        // (the natural model's pedal regions are kept)
        CHECK (model->original.fallbackContinuation.pedal.valid == model->original.continuation.pedal.valid);
    }
    {
        const auto model = build (sustained (3.0));
        CHECK (model->original.continuation.canSustain);
        CHECK_FALSE (model->original.fallbackContinuation.canSustain);   // (none needed)
    }
    // Struck and plucked notes play once, short or long.
    for (const double seconds : { 0.4, 2.0 })
    {
        INFO ("pluck " << seconds << " s");
        const auto model = build (testsignals::pluck (220.0, seconds, rate, 7, 0.7));
        CHECK_FALSE (model->original.continuation.canSustain);
        CHECK_FALSE (model->original.fallbackContinuation.canSustain);
    }
    // Too short for any body (and silence): nothing.
    CHECK_FALSE (build (sustained (0.1))->original.fallbackContinuation.canSustain);
    CHECK_FALSE (build (testsignals::sine (220.0, 0.5, rate, 0.0))->original.fallbackContinuation.canSustain);
}

TEST_CASE ("short loop: a held short note sustains with LOOP and its fallback; plays once otherwise", "[unit][short-loop]")
{
    const auto model = build (sustained (0.4));
    REQUIRE (model->original.fallbackContinuation.canSustain);
    const double held = heldLevelDb (model, true, true);
    const double once = heldLevelDb (model, true, false);    // (the engine's default: as before)
    const double loopOff = heldLevelDb (model, false, true);
    INFO ("held " << held << " dB, without the fallback " << once << " dB, LOOP off " << loopOff << " dB");
    CHECK (held > -40.0);
    CHECK (once < held - 40.0);
    CHECK (loopOff < held - 40.0);
    // Long notes are untouched by the fallback (their own loop).
    const auto longModel = build (sustained (3.0));
    CHECK (std::abs (heldLevelDb (longModel, true, true, 4.0) - heldLevelDb (longModel, true, false, 4.0)) < 1.0e-9);
}

TEST_CASE ("short loop: renders of the duration matrix (OSP_SHORT_LOOP_RENDERS)", "[.][short-loop-renders]")
{
    // Each note held 3 s, with the fallback and without (as before): sustained tones of 0.3,
    // 0.4, 0.6 and 1.0 s, and a 0.4 s pluck (it plays once either way). Mono, 48 kHz.
    const char* dir = std::getenv ("OSP_SHORT_LOOP_RENDERS");
    REQUIRE (dir != nullptr);
    std::filesystem::create_directories (dir);
    auto render = [] (const std::shared_ptr<const InstrumentModel>& model, bool fallback) {
        EngineSettings s;
        s.macros.space = 0.15;
        s.layer[0].fallbackLoop = fallback;
        auto engine = std::make_unique<InstrumentEngine>();
        engine->prepare (rate, 256, s);
        engine->setModel (model.get(), 0);
        engine->noteOn (57, 100);
        AudioData out;
        out.sampleRate = rate;
        out.channels.assign (1, {});
        std::vector<float> l (256), r (256);
        for (int done = 0; done < static_cast<int> (4.0 * rate); done += 256)
        {
            if (done == static_cast<int> (3.0 * rate) / 256 * 256)
                engine->noteOff (57);
            float* ch[2] = { l.data(), r.data() };
            engine->render (ch, 2, 256);
            out.channels[0].insert (out.channels[0].end(), l.begin(), l.end());
        }
        return out;
    };
    struct Case
    {
        const char* name;
        AudioData audio;
    };
    std::vector<Case> cases { { "sustained-0.3s", sustained (0.3) }, { "sustained-0.4s", sustained (0.4) }, { "sustained-0.6s", sustained (0.6) },
                              { "sustained-1.0s", sustained (1.0) }, { "pluck-0.4s", testsignals::pluck (220.0, 0.4, rate, 7, 0.7) } };
    for (const auto& c : cases)
    {
        const auto model = build (c.audio);
        for (const bool fallback : { false, true })
        {
            std::string error;
            const auto path = std::filesystem::path (dir) / (std::string (c.name) + (fallback ? "-fallback.wav" : "-before.wav"));
            CHECK (io::writeAudioFile (path, render (model, fallback), io::SampleFormat::pcm24, error));
        }
    }
}
