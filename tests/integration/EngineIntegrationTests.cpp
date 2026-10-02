#include "audio/utility/TestSignals.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "research/Experiment.h"
#include "research/RenderSession.h"
#include "support/TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace osp;

namespace
{
    double rmsBetween (const AudioData& audio, double a, double b)
    {
        double sum = 0.0;
        std::size_t n = 0;
        const auto from = static_cast<std::size_t> (a * audio.sampleRate);
        const auto to = std::min (audio.channels[0].size(), static_cast<std::size_t> (b * audio.sampleRate));
        for (const auto& ch : audio.channels)
            for (std::size_t i = from; i < to; ++i)
            {
                sum += static_cast<double> (ch[i]) * ch[i];
                ++n;
            }
        return n > 0 ? std::sqrt (sum / static_cast<double> (n)) : 0.0;
    }

    MidiSequence hold (int note, double seconds)
    {
        MidiSequence s;
        s.events.push_back ({ 0.0, MidiEvent::Type::noteOn, note, 100, 1 });
        s.events.push_back ({ seconds, MidiEvent::Type::noteOff, note, 0, 1 });
        return s;
    }

    struct Fixture
    {
        AudioData audio;
        std::shared_ptr<InstrumentModel> model;

        explicit Fixture (double fadeOut = 0.8, bool anchors = false)
        {
            audio = testsignals::vowel (220.0, 3.0, 48000.0, 3);
            testsignals::applyFades (audio, 0.05, fadeOut);
            model = instrument::buildComplete (audio, test::analyse (audio), {}, anchors);
        }
    };
}

TEST_CASE ("engine: a 3 s vowel holds for 20 s without dropping out", "[integration][engine]")
{
    Fixture f;
    REQUIRE (f.model->original.continuation.canSustain);
    research::RenderConfig config;
    config.engineSettings.continuation = ContinuationStrategy::multiLoop;
    const auto out = research::renderInstrument (*f.model, hold (57, 20.0), config);

    const double early = rmsBetween (out.audio, 0.5, 1.5);
    const double late = rmsBetween (out.audio, 15.0, 19.0);
    REQUIRE (early > 0.0);
    CHECK (std::abs (20.0 * std::log10 (late / early)) < 3.0);
    CHECK (research::discontinuityDb (out.audio, 1.0, 19.5) < 18.0);
    std::size_t nonFinite = 0;
    for (const auto& ch : out.audio.channels)
        for (float s : ch)
            nonFinite += std::isfinite (s) ? 0 : 1;
    CHECK (nonFinite == 0);
}

TEST_CASE ("engine: multi-loop repeats less than a single loop", "[integration][engine]")
{
    Fixture f;
    research::RenderConfig config;
    config.engineSettings.continuation = ContinuationStrategy::naiveLoop;
    const auto naive = research::renderInstrument (*f.model, hold (57, 30.0), config);
    config.engineSettings.continuation = ContinuationStrategy::multiLoop;
    const auto multi = research::renderInstrument (*f.model, hold (57, 30.0), config);
    CHECK (research::repetitionScore (naive.audio, 3.0) > research::repetitionScore (multi.audio, 3.0));
}

TEST_CASE ("engine: release grafts the recording's own ending", "[integration][engine]")
{
    Fixture f (0.8);
    REQUIRE (f.model->original.continuation.hasRelease);
    InstrumentEngine engine;
    EngineSettings settings;
    settings.continuation = ContinuationStrategy::multiLoop;
    engine.prepare (48000.0, 256, settings);
    engine.setModel (f.model.get());
    engine.noteOn (57, 100);
    std::vector<float> l (256), r (256);
    float* channels[2] = { l.data(), r.data() };
    for (int i = 0; i < 48000 * 8 / 256; ++i)
        engine.render (channels, 2, 256);
    engine.noteOff (57);
    int blocksAfterRelease = 0;
    bool grafted = false;
    while (engine.activeVoiceCount() > 0 && blocksAfterRelease < 48000 * 5 / 256)
    {
        engine.render (channels, 2, 256);
        for (int v = 0; v < InstrumentEngine::voiceSlots(); ++v)
            grafted = grafted || engine.voiceAt (v).hasGrafted();
        ++blocksAfterRelease;
    }
    CHECK (grafted);
    CHECK (engine.activeVoiceCount() == 0); // the natural ending finished the voice
}

TEST_CASE ("engine: renders are identical across block sizes and repeatable", "[integration][engine]")
{
    Fixture f;
    research::RenderConfig config;
    config.engineSettings.continuation = ContinuationStrategy::multiLoopMovement;
    config.blockSize = 32;
    const auto a = research::renderInstrument (*f.model, hold (60, 8.0), config);
    config.blockSize = 512;
    const auto b = research::renderInstrument (*f.model, hold (60, 8.0), config);
    const auto c = research::renderInstrument (*f.model, hold (60, 8.0), config);
    const auto frames = std::min (a.audio.numFrames(), b.audio.numFrames());
    double maxDiff = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (std::int64_t i = 0; i < frames; ++i)
            maxDiff = std::max (maxDiff, static_cast<double> (std::abs (a.audio.channels[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)]
                                                                        - b.audio.channels[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)])));
    CHECK (maxDiff == 0.0);
    CHECK (test::maxDifference (b.audio, c.audio) == 0.0);
}

TEST_CASE ("engine: different seeds walk differently", "[integration][engine]")
{
    Fixture f;
    research::RenderConfig config;
    config.engineSettings.continuation = ContinuationStrategy::multiLoop;
    config.engineSettings.seed = 1;
    const auto a = research::renderInstrument (*f.model, hold (57, 12.0), config);
    config.engineSettings.seed = 2;
    const auto b = research::renderInstrument (*f.model, hold (57, 12.0), config);
    CHECK (test::maxDifference (a.audio, b.audio) > 1.0e-3);
}

TEST_CASE ("engine: natural pitch character plays register anchors in tune", "[integration][engine]")
{
    Fixture f (0.8, true);
    REQUIRE (f.model->anchors.size() == 4);
    for (const auto& layer : f.model->anchors)
    {
        CHECK (layer.source->isValid());
        CHECK (layer.continuation.jumps.size() == f.model->original.continuation.jumps.size());
    }
    // Note 69 (A4, +12) should come from the +12 anchor and sound at 440 Hz.
    const auto& layer = f.model->layerFor (69.0, PitchCharacter::natural);
    CHECK (layer.offsetSemitones == 12.0);
    research::RenderConfig config;
    config.engineSettings.pitchCharacter = PitchCharacter::natural;
    const auto out = research::renderInstrument (*f.model, hold (69, 2.0), config);
    const auto analysis = test::analyse (out.audio);
    REQUIRE (analysis.pitch.detected);
    CHECK (std::abs (1200.0 * std::log2 (analysis.pitch.fundamentalHz / 440.0)) < 15.0);
}

TEST_CASE ("engine: one-shot sources play once and stop", "[integration][engine]")
{
    const auto audio = testsignals::pluck (196.0, 2.0, 48000.0, 5);
    const auto model = instrument::buildComplete (audio, test::analyse (audio), {}, false);
    CHECK_FALSE (model->original.continuation.canSustain);
    research::RenderConfig config;
    config.maxTailSeconds = 10.0;
    const auto out = research::renderInstrument (*model, hold (55, 10.0), config);
    CHECK (out.audio.durationSeconds() <= 10.5);
    CHECK (rmsBetween (out.audio, 4.0, 9.0) < 1.0e-6);
}

TEST_CASE ("engine: MPE bends only the note on its own channel", "[integration][engine]")
{
    Fixture f;
    auto render = [&] (bool mpe, int bendChannel) {
        InstrumentEngine engine;
        EngineSettings s;
        s.macros.life = 0.0;
        s.macros.space = 0.0;
        s.macros.reimagined = 0.0;
        engine.prepare (48000.0, 256, s);
        engine.setModel (f.model.get());
        engine.setMpe (mpe);
        engine.setChannelPitchBend (bendChannel, 2.0);
        engine.noteOn (57, 100, 2);
        AudioData out = AudioData::allocate (2, 48000, 48000.0);
        for (int pos = 0; pos < 48000; pos += 256)
        {
            float* ch[2] = { out.channels[0].data() + pos, out.channels[1].data() + pos };
            engine.render (ch, 2, std::min (256, 48000 - pos));
        }
        return test::analyse (out).pitch.fundamentalHz;
    };
    const double a3 = 220.0;
    CHECK (std::abs (1200.0 * std::log2 (render (true, 2) / (a3 * std::pow (2.0, 2.0 / 12.0)))) < 15.0); // its channel bends
    CHECK (std::abs (1200.0 * std::log2 (render (true, 3) / a3)) < 15.0);                                 // another channel does not
    CHECK (std::abs (1200.0 * std::log2 (render (false, 2) / a3)) < 15.0);                                // no MPE: channel bends ignored
}
