#include "analysis/transient/TransientSeparation.h"
#include "audio/utility/TestSignals.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "research/Experiment.h"
#include "research/RenderSession.h"
#include "support/TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

namespace
{
    // A decaying 220 Hz tone with a 4 ms noise "pick" at its start.
    AudioData pickedTone()
    {
        const double sr = 48000.0;
        auto audio = testsignals::sine (220.0, 2.0, sr, 0.5);
        const auto noise = testsignals::whiteNoise (0.004, sr, 0.6, 11);
        for (std::size_t i = 0; i < audio.channels[0].size(); ++i)
        {
            audio.channels[0][i] *= static_cast<float> (std::exp (-2.0 * static_cast<double> (i) / sr));
            if (i < noise.channels[0].size())
                audio.channels[0][i] += noise.channels[0][i];
        }
        return audio;
    }

    // How long the attack's high band stays within 20 dB of its peak (seconds).
    double clickSeconds (const AudioData& audio)
    {
        std::vector<double> e;
        const int hop = static_cast<int> (audio.sampleRate / 2000.0); // 0.5 ms
        double acc = 0.0, prev = 0.0;
        for (std::size_t i = 0; i < std::min<std::size_t> (audio.channels[0].size(), static_cast<std::size_t> (0.2 * audio.sampleRate)); ++i)
        {
            const double x = audio.channels[0][i];
            acc += (x - prev) * (x - prev);
            prev = x;
            if ((i + 1) % static_cast<std::size_t> (hop) == 0)
            {
                e.push_back (acc);
                acc = 0.0;
            }
        }
        const double peak = *std::max_element (e.begin(), e.end());
        int count = 0;
        for (double v : e)
            count += v > 0.01 * peak ? 1 : 0;
        return count * 0.0005;
    }
}

TEST_CASE ("transient separation: the pick is transient, the tone is body", "[unit][engine]")
{
    const auto audio = pickedTone();
    const auto separation = separateOnsetTransient (audio, 0.0);
    REQUIRE_FALSE (separation.transient.isEmpty());
    CHECK (separation.share > 0.02);
    // The transient holds the pick; 50 ms later, after the pick, it is close to silent.
    double pick = 0.0, later = 0.0, tone = 0.0;
    for (std::size_t i = 0; i < 192; ++i)
        pick += std::pow (separation.transient.channels[0][i], 2.0);
    for (std::size_t i = 2400; i < 4800; ++i)
    {
        later += std::pow (separation.transient.channels[0][i], 2.0);
        tone += std::pow (audio.channels[0][i], 2.0);
    }
    CHECK (later < 0.05 * tone);
    CHECK (pick / 192.0 > 10.0 * later / 2400.0);
    // A steady tone has (almost) no transient.
    const auto steady = separateOnsetTransient (testsignals::sine (220.0, 2.0, 48000.0, 0.5), 0.0);
    CHECK (steady.share < 0.01);
}

TEST_CASE ("engine: transient preservation keeps a transposed pick short", "[integration][engine]")
{
    const auto audio = pickedTone();
    const auto model = instrument::buildComplete (audio, test::analyse (audio), {}, false);
    REQUIRE (model->original.transient != nullptr);

    auto render = [&] (bool preserve, int note) {
        research::RenderConfig config;
        config.engineSettings.macros.life = 0.0;
        config.engineSettings.macros.space = 0.0;
        config.engineSettings.macros.reimagined = 0.0;
        config.engineSettings.transientPreservation = preserve;
        return research::renderInstrument (*model, hold (note, 1.0), config).audio;
    };
    const double original = clickSeconds (audio);
    const int root = static_cast<int> (std::lround (model->rootMidi));
    const double offDown = clickSeconds (render (false, root - 24));
    const double onDown = clickSeconds (render (true, root - 24));
    INFO ("pick: original " << original << " s, two octaves down " << offDown << " s -> " << onDown << " s");
    CHECK (offDown > 2.0 * original);
    CHECK (onDown < 0.5 * offDown);
    // Near the root nothing changes.
    const auto a = render (false, root + 1), b = render (true, root + 1);
    CHECK (a.channels[0] == b.channels[0]);
}

TEST_CASE ("engine: long recordings skip register anchors and Natural plays as Tape", "[integration][engine]")
{
    const auto audio = testsignals::vowel (220.0, 3.0, 48000.0, 3);
    InstrumentBuildOptions options;
    options.anchorMaxSeconds = 2.0; // stands in for a recording over a minute
    const auto model = instrument::buildComplete (audio, test::analyse (audio), options, true);
    CHECK (model->stage == InstrumentModel::Stage::complete);
    CHECK (model->anchors.empty());

    research::RenderConfig tape, natural;
    natural.engineSettings.pitchCharacter = PitchCharacter::natural;
    const auto a = research::renderInstrument (*model, hold (69, 1.0), tape).audio;
    const auto b = research::renderInstrument (*model, hold (69, 1.0), natural).audio;
    CHECK (a.channels[0] == b.channels[0]);
}

TEST_CASE ("engine: odd sources build every stage and play without failing", "[integration][engine]")
{
    // Fail beautifully: whatever arrives must become a playable (if plain) instrument.
    struct Odd
    {
        const char* name;
        AudioData audio;
    };
    std::vector<Odd> sources;
    sources.push_back ({ "silence", testsignals::silence (2.0, 48000.0) });
    sources.push_back ({ "noise", testsignals::whiteNoise (2.0, 48000.0, 0.3, 4, 2) });
    sources.push_back ({ "impulse", testsignals::impulse (1.0, 48000.0) });
    sources.push_back ({ "10 ms tone", testsignals::sine (440.0, 0.01, 48000.0) });
    sources.push_back ({ "1 sample", AudioData::allocate (1, 1, 48000.0) });
    sources.push_back ({ "8 kHz voice", testsignals::vowel (220.0, 2.0, 8000.0, 2) });
    sources.push_back ({ "192 kHz saw", testsignals::saw (110.0, 1.0, 192000.0) });
    auto dc = AudioData::allocate (2, 48000, 48000.0);
    for (auto& ch : dc.channels)
        std::fill (ch.begin(), ch.end(), 0.5f);
    sources.push_back ({ "DC", dc });
    auto clipped = testsignals::sine (55.0, 2.0, 48000.0, 4.0); // way over full scale
    for (auto& x : clipped.channels[0])
        x = std::clamp (x, -1.0f, 1.0f);
    sources.push_back ({ "clipped square", clipped });
    sources.push_back ({ "sub-audio", testsignals::sine (8.0, 3.0, 48000.0) });
    sources.push_back ({ "ultrasonic", testsignals::sine (21000.0, 1.0, 48000.0) });

    for (const auto& odd : sources)
    {
        INFO (odd.name);
        const auto model = instrument::buildComplete (odd.audio, test::analyse (odd.audio), {}, true);
        REQUIRE (model != nullptr);
        for (auto pitch : { PitchCharacter::tape, PitchCharacter::natural })
        {
            research::RenderConfig config; // default macros, transient preservation on
            config.engineSettings.pitchCharacter = pitch;
            config.maxTailSeconds = 3.0;
            MidiSequence chord;
            for (int note : { 36, 60, 84, 100 })
            {
                chord.events.push_back ({ 0.0, MidiEvent::Type::noteOn, note, 110, 1 });
                chord.events.push_back ({ 1.5, MidiEvent::Type::noteOff, note, 0, 1 });
            }
            const auto out = research::renderInstrument (*model, chord, config).audio;
            bool finite = true;
            float peak = 0.0f;
            for (const auto& ch : out.channels)
                for (float x : ch)
                {
                    finite = finite && std::isfinite (x);
                    peak = std::max (peak, std::abs (x));
                }
            CHECK (finite);
            CHECK (peak < 8.0f);
        }
    }
}

TEST_CASE ("engine: transient mixing makes velocity act on the pick, not the whole note", "[integration][engine]")
{
    const auto audio = pickedTone();
    const auto model = instrument::buildComplete (audio, test::analyse (audio), {}, false);
    REQUIRE (model->original.transientShare >= 0.02);
    const int root = static_cast<int> (std::lround (model->rootMidi));

    // Pick energy (first 4 ms) over early body energy (20-60 ms), in dB.
    auto pickToBody = [&] (bool mixing, int velocity) {
        research::RenderConfig config;
        config.engineSettings.macros.life = 0.0;
        config.engineSettings.macros.space = 0.0;
        config.engineSettings.macros.reimagined = 0.0;
        config.engineSettings.macros.dynamics = 1.0;
        config.engineSettings.transientMixing = mixing;
        MidiSequence s;
        s.events.push_back ({ 0.0, MidiEvent::Type::noteOn, root, velocity, 1 });
        s.events.push_back ({ 0.5, MidiEvent::Type::noteOff, root, 0, 1 });
        const auto out = research::renderInstrument (*model, s, config).audio;
        return 20.0 * std::log10 (rmsBetween (out, 0.0, 0.004) / std::max (rmsBetween (out, 0.02, 0.06), 1.0e-9));
    };
    // Above the recorded velocity nothing else changes the attack (soft notes also soften it).
    const double gainSpread = pickToBody (false, 127) - pickToBody (false, 100);
    const double mixSpread = pickToBody (true, 127) - pickToBody (true, 100);
    INFO ("pick/body change from velocity 100 to 127: attack gain " << gainSpread << " dB, transient mixing " << mixSpread << " dB");
    CHECK (mixSpread > gainSpread + 2.0);
}

TEST_CASE ("engine: Reimagined is clearly audible", "[integration][engine]")
{
    // Lab, continuum-1: 0..100 % sounded alike. Towards Reimagined the sound must move:
    // the doubling head makes a steady tone beat (level fluctuation over 20 ms frames).
    Fixture f;
    auto fluctuation = [&] (double reimagined) {
        research::RenderConfig config;
        config.engineSettings.macros.life = 0.0;
        config.engineSettings.macros.space = 0.0;
        config.engineSettings.macros.motion = 0.0;
        config.engineSettings.macros.reimagined = reimagined;
        const auto out = research::renderInstrument (*f.model, hold (57, 4.0), config).audio;
        std::vector<double> db;
        for (std::size_t i = static_cast<std::size_t> (0.5 * out.sampleRate); i + 960 < static_cast<std::size_t> (3.5 * out.sampleRate); i += 960)
        {
            double e = 0.0;
            for (std::size_t j = i; j < i + 960; ++j)
                e += static_cast<double> (out.channels[0][j]) * out.channels[0][j];
            db.push_back (10.0 * std::log10 (e / 960.0 + 1e-20));
        }
        double mean = 0.0, var = 0.0;
        for (double v : db) mean += v;
        mean /= static_cast<double> (db.size());
        for (double v : db) var += (v - mean) * (v - mean);
        return std::sqrt (var / static_cast<double> (db.size()));
    };
    const double original = fluctuation (0.0), reimagined = fluctuation (1.0);
    INFO ("level fluctuation: original " << original << " dB, reimagined " << reimagined << " dB");
    CHECK (reimagined > original + 0.5);
}

TEST_CASE ("engine: the Reimagined far end remaps harmonics with grains", "[integration][engine]")
{
    // A pure 330 Hz tone has nothing an octave below or above. Towards the far end the
    // granular continuation adds octave-down and octave-up grains; below ~45 % it does not.
    auto audio = testsignals::sine (330.0, 6.0, 48000.0, 0.4, 1);
    testsignals::applyFades (audio, 0.01, 0.3);
    const auto model = instrument::buildComplete (audio, test::analyse (audio), {}, false);
    auto bandDb = [&] (double reimagined, double hz) {
        research::RenderConfig config;
        config.engineSettings.macros.life = 0.0;
        config.engineSettings.macros.space = 0.0;
        config.engineSettings.macros.motion = 0.0;
        config.engineSettings.macros.reimagined = reimagined;
        config.engineSettings.shaping = Shaping::neutral();
        MidiSequence s;
        s.events.push_back ({ 0.0, MidiEvent::Type::noteOn, 64, 100, 1 });
        s.events.push_back ({ 4.0, MidiEvent::Type::noteOff, 64, 0, 1 });
        const auto out = research::renderInstrument (*model, s, config).audio;
        // Goertzel power at hz over 1.5..3.5 s, relative to the whole signal.
        const auto a = static_cast<std::size_t> (1.5 * out.sampleRate), b = static_cast<std::size_t> (3.5 * out.sampleRate);
        const double w = 2.0 * std::cos (2.0 * 3.141592653589793 * hz / out.sampleRate);
        double s1 = 0.0, s2 = 0.0, total = 0.0;
        for (std::size_t i = a; i < b; ++i)
        {
            const double x = out.channels[0][i];
            const double s0 = x + w * s1 - s2;
            s2 = s1;
            s1 = s0;
            total += x * x;
        }
        const double power = (s1 * s1 + s2 * s2 - w * s1 * s2) / static_cast<double> (b - a);
        return 10.0 * std::log10 (power / total + 1e-30);
    };
    // The note plays at the source's pitch (E4 = 329.6 Hz).
    const double f0 = 329.63;
    const double subOriginal = bandDb (0.0, 0.5 * f0), subMiddle = bandDb (0.3, 0.5 * f0), subFar = bandDb (1.0, 0.5 * f0);
    const double octOriginal = bandDb (0.0, 2.0 * f0), octFar = bandDb (1.0, 2.0 * f0);
    INFO ("sub-octave " << subOriginal << " / " << subMiddle << " / " << subFar << " dB; octave " << octOriginal << " / " << octFar << " dB");
    CHECK (subFar > subOriginal + 15.0);
    CHECK (std::abs (subMiddle - subOriginal) < 3.0);
    CHECK (octFar > octOriginal + 10.0);
}

TEST_CASE ("engine: releasing early in a long recording stops promptly", "[integration][engine]")
{
    // Regression (Mac test): very long samples kept sounding after note-off. A release
    // before the stable region (or far from any exit into the ending) waited for the
    // ending at full level - seconds, or the rest of the file.
    // Like a long bowed violin take: a 10 s swell from -40 dB, a sustain, a 4 s ending.
    auto audio = testsignals::vowel (220.0, 30.0, 48000.0, 3);
    for (auto& ch : audio.channels)
        for (std::size_t i = 0; i < std::min<std::size_t> (ch.size(), 480000); ++i)
            ch[i] *= static_cast<float> (std::pow (10.0, -2.0 * (1.0 - static_cast<double> (i) / 480000.0)));
    for (auto& ch : audio.channels) // exponential 4 s ending, down to -60 dB
        for (std::size_t i = ch.size() - 192000; i < ch.size(); ++i)
            ch[i] *= static_cast<float> (std::pow (10.0, -3.0 * static_cast<double> (i - (ch.size() - 192000)) / 192000.0));
    const auto model = instrument::buildComplete (audio, test::analyse (audio), {}, false);
    const auto& c = model->original.continuation;
    REQUIRE (c.canSustain);
    INFO ("region end " << c.sustainEndFrame / 48000.0 << " s, ends while sounding " << model->analysis.envelope.endsWhileSounding << ", tail " << c.tailSeconds);
    REQUIRE (c.hasRelease);
    INFO ("stable region from " << c.sustainStartFrame / 48000.0 << " s");
    REQUIRE (c.sustainStartFrame / 48000.0 > 3.0); // the regression needs exits far from an early release
    for (double releaseAt : { 1.0, 12.0 })
    {
        INFO ("note-off after " << releaseAt << " s");
        InstrumentEngine engine;
        EngineSettings settings; // default release 250 ms: long enough to graft
        engine.prepare (48000.0, 256, settings);
        engine.setModel (model.get());
        engine.noteOn (57, 100);
        std::vector<float> l (256), r (256);
        float* channels[2] = { l.data(), r.data() };
        for (int i = 0; i < static_cast<int> (releaseAt * 48000.0 / 256.0); ++i)
            engine.render (channels, 2, 256);
        engine.noteOff (57);
        int blocks = 0;
        while (engine.activeVoiceCount() > 0 && blocks < 48000 * 30 / 256)
        {
            engine.render (channels, 2, 256);
            ++blocks;
        }
        // Release time, or a nearby graft plus the recording's 4 s ending - never more.
        CHECK (blocks * 256.0 / 48000.0 < (releaseAt < 5.0 ? 1.0 : 5.0));
    }
}
