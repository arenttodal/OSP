#include "audio/sampler/BaselineSampler.h"
#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using Catch::Approx;
using namespace osp;

namespace
{
    struct Rig
    {
        BaselineSampler sampler;
        AudioData sourceAudio;
        std::unique_ptr<PlaybackSource> source;
        double rate;

        Rig (AudioData audio, double rootMidi, double outputRate, SamplerSettings settings = {})
            : sourceAudio (std::move (audio)), rate (outputRate)
        {
            sampler.prepare (outputRate, 256, settings);
            source = std::make_unique<PlaybackSource> (sourceAudio, rootMidi, sampler.requiredSourcePadding());
            sampler.setSource (source.get());
        }

        AudioData render (double seconds)
        {
            const auto frames = static_cast<std::int64_t> (seconds * rate);
            auto out = AudioData::allocate (2, frames, rate);
            for (std::int64_t pos = 0; pos < frames; pos += 256)
            {
                const int n = static_cast<int> (std::min<std::int64_t> (256, frames - pos));
                float* ch[2] = { out.channels[0].data() + pos, out.channels[1].data() + pos };
                sampler.render (ch, 2, n);
            }
            return out;
        }
    };

    double detectedHz (const AudioData& audio)
    {
        return test::analyse (audio).pitch.fundamentalHz;
    }
}

TEST_CASE ("sampler: plays the root at the source pitch and transposes chromatically", "[unit][sampler]")
{
    for (int offset : { -12, -7, 0, 5, 12 })
    {
        Rig rig (testsignals::sine (220.0, 3.0, 48000.0), 57.0, 48000.0);
        rig.sampler.noteOn (57 + offset, 100);
        const auto out = rig.render (1.0);
        CHECK (detectedHz (out) == Approx (220.0 * semitonesToRatio (offset)).epsilon (0.002));
    }
}

TEST_CASE ("sampler: output pitch is independent of source and output sample rates", "[unit][sampler]")
{
    for (double sourceRate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        for (double outputRate : { 44100.0, 48000.0, 96000.0 })
        {
            Rig rig (testsignals::sine (110.0, 2.0, sourceRate), 45.0, outputRate);
            rig.sampler.noteOn (57, 100); // one octave up
            const auto out = rig.render (0.8);
            CHECK (detectedHz (out) == Approx (220.0).epsilon (0.002));
        }
}

TEST_CASE ("sampler: fractional root corrects a detuned source", "[unit][sampler]")
{
    // Source is 30 cents sharp of A3; with the fractional root, A4 must come out at 440 Hz.
    const double hz = 220.0 * centsToRatio (30.0);
    Rig rig (testsignals::sine (hz, 3.0, 48000.0), hzToMidi (hz), 48000.0);
    rig.sampler.noteOn (69, 100);
    CHECK (detectedHz (rig.render (1.0)) == Approx (440.0).epsilon (0.002));
}

TEST_CASE ("sampler: velocity maps to gain over the configured range", "[unit][sampler]")
{
    SamplerSettings settings;
    settings.velocityRangeDb = 30.0;
    settings.adsr.attackSeconds = 0.0;

    auto levelFor = [&] (int velocity) {
        Rig rig (testsignals::sine (220.0, 2.0, 48000.0), 57.0, 48000.0, settings);
        rig.sampler.noteOn (57, velocity);
        return test::rms (rig.render (0.5));
    };
    const double loud = levelFor (127);
    CHECK (20.0 * std::log10 (levelFor (1) / loud) == Approx (-30.0 * (126.0 / 127.0)).margin (0.05));
    CHECK (20.0 * std::log10 (levelFor (64) / loud) == Approx (-30.0 * (63.0 / 127.0)).margin (0.05));
}

TEST_CASE ("sampler: polyphony is limited and stealing prefers released voices", "[unit][sampler][voices]")
{
    SamplerSettings settings;
    settings.polyphony = 4;
    Rig rig (testsignals::sine (220.0, 5.0, 48000.0), 57.0, 48000.0, settings);

    for (int n = 0; n < 4; ++n)
        rig.sampler.noteOn (60 + n, 100);
    rig.render (0.05);
    CHECK (rig.sampler.activeVoiceCount() == 4);

    rig.sampler.noteOff (61);                    // 61 is now releasing
    rig.sampler.noteOn (70, 100);                // must steal 61, not a held note
    rig.render (0.02);                           // steal fade (5 ms) completes
    CHECK (rig.sampler.activeVoiceCount() == 4);
    CHECK_FALSE (rig.sampler.isNoteActive (61));
    CHECK (rig.sampler.isNoteActive (60));
    CHECK (rig.sampler.isNoteActive (62));
    CHECK (rig.sampler.isNoteActive (63));
    CHECK (rig.sampler.isNoteActive (70));

    // With every voice held, the quietest/oldest held voice is stolen (60 was first).
    rig.sampler.noteOn (71, 100);
    rig.render (0.02);
    CHECK_FALSE (rig.sampler.isNoteActive (60));
    CHECK (rig.sampler.isNoteActive (71));
    rig.sampler.noteOff (71);

    rig.sampler.noteOff (60);
    rig.sampler.noteOff (62);
    rig.sampler.noteOff (63);
    rig.sampler.noteOff (70);
    rig.render (1.0);
    CHECK (rig.sampler.activeVoiceCount() == 0);

    // Hammering far more notes than the polyphony never exceeds polyphony + fading tails.
    for (int n = 0; n < 40; ++n)
    {
        rig.sampler.noteOn (40 + n, 100);
        rig.render (0.001);
        REQUIRE (rig.sampler.activeVoiceCount() <= 4 + 16);
    }
    rig.render (0.05);
    CHECK (rig.sampler.activeVoiceCount() == 4);
}

TEST_CASE ("sampler: sustain pedal holds released notes", "[unit][sampler][voices]")
{
    Rig rig (testsignals::sine (220.0, 5.0, 48000.0), 57.0, 48000.0);
    rig.sampler.setSustainPedal (true);
    rig.sampler.noteOn (60, 100);
    rig.sampler.noteOff (60);
    rig.render (0.5);
    CHECK (rig.sampler.activeVoiceCount() == 1);
    rig.sampler.setSustainPedal (false);
    rig.render (1.0);
    CHECK (rig.sampler.activeVoiceCount() == 0);
}

TEST_CASE ("sampler: voices end when the recording ends (no loop in the baseline)", "[unit][sampler]")
{
    Rig rig (testsignals::sine (220.0, 0.5, 48000.0), 57.0, 48000.0);
    rig.sampler.noteOn (57, 100);
    rig.render (0.4);
    CHECK (rig.sampler.activeVoiceCount() == 1);
    rig.render (0.2);
    CHECK (rig.sampler.activeVoiceCount() == 0);
}

TEST_CASE ("sampler: mono sources play centred, stereo sources keep their channels", "[unit][sampler]")
{
    Rig mono (testsignals::sine (220.0, 1.0, 48000.0, 0.5, 1), 57.0, 48000.0);
    mono.sampler.noteOn (57, 127);
    const auto m = mono.render (0.3);
    CHECK (test::maxDifference (AudioData { m.sampleRate, { m.channels[0] } }, AudioData { m.sampleRate, { m.channels[1] } }) == 0.0);

    auto stereoAudio = testsignals::sine (220.0, 1.0, 48000.0, 0.5, 2);
    for (auto& s : stereoAudio.channels[1])
        s = 0.0f; // hard-left source
    Rig stereo (stereoAudio, 57.0, 48000.0);
    stereo.sampler.noteOn (57, 127);
    const auto st = stereo.render (0.3);
    double right = 0.0;
    for (float s : st.channels[1])
        right = std::max (right, static_cast<double> (std::abs (s)));
    CHECK (right < 1e-6);
}

TEST_CASE ("sampler: randomised baseline is deterministic per seed", "[unit][sampler][prng]")
{
    SamplerSettings settings;
    settings.randomization.enabled = true;

    auto renderWithSeed = [&] (std::uint64_t seed) {
        settings.seed = seed;
        Rig rig (testsignals::saw (110.0, 2.0, 48000.0), 45.0, 48000.0, settings);
        for (int i = 0; i < 4; ++i)
        {
            rig.sampler.noteOn (45, 100);
            rig.render (0.1);
            rig.sampler.noteOff (45);
            rig.render (0.05);
        }
        return rig.render (0.3);
    };
    const auto a = renderWithSeed (1);
    const auto b = renderWithSeed (1);
    const auto c = renderWithSeed (2);
    CHECK (test::maxDifference (a, b) == 0.0);
    CHECK (test::maxDifference (a, c) > 1e-3);
}
