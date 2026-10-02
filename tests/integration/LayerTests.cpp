// A/B source layers and the Granular source mode, through the whole engine.
#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using namespace osp;
using Catch::Approx;

namespace
{
    constexpr double rate = 48000.0;

    std::shared_ptr<InstrumentModel> sineModel (double hz, double seconds = 2.0)
    {
        auto audio = testsignals::sine (hz, seconds, rate, 0.4, 1);
        testsignals::applyFades (audio, 0.01, 0.05);
        return instrument::buildComplete (audio, test::analyse (audio), {}, false);
    }

    EngineSettings quietSettings()
    {
        EngineSettings s;
        s.macros.life = 0.0;
        s.macros.space = 0.0;
        s.macros.motion = 0.0;
        s.macros.reimagined = 0.0;
        s.shaping = Shaping::neutral();
        s.adsr.releaseSeconds = 0.1;
        return s;
    }

    struct Rig
    {
        InstrumentEngine engine;
        std::vector<float> l, r;
        explicit Rig (const EngineSettings& s, int block = 256)
        {
            engine.prepare (rate, block, s);
            l.resize (static_cast<std::size_t> (block));
            r.resize (static_cast<std::size_t> (block));
        }
        /** Renders `seconds` and appends the left channel. */
        void run (double seconds, std::vector<float>& out, int block = 256)
        {
            const auto total = static_cast<int> (seconds * rate);
            for (int done = 0; done < total; done += block)
            {
                const int n = std::min (block, total - done);
                float* ch[2] = { l.data(), r.data() };
                engine.render (ch, 2, n);
                out.insert (out.end(), l.begin(), l.begin() + n);
            }
        }
    };

    double goertzel (const std::vector<float>& x, std::size_t a, std::size_t b, double hz)
    {
        const double w = 2.0 * std::cos (2.0 * std::numbers::pi * hz / rate);
        double s1 = 0.0, s2 = 0.0;
        for (std::size_t i = a; i < b; ++i)
        {
            const double s0 = x[i] + w * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return (s1 * s1 + s2 * s2 - w * s1 * s2) / static_cast<double> (b - a);
    }

    double rms (const std::vector<float>& x, std::size_t a, std::size_t b)
    {
        double e = 0.0;
        for (std::size_t i = a; i < b; ++i)
            e += static_cast<double> (x[i]) * x[i];
        return std::sqrt (e / static_cast<double> (b - a));
    }

    double pitchOf (const std::vector<float>& x, std::size_t a, std::size_t b)
    {
        AudioData audio = AudioData::allocate (1, static_cast<std::int64_t> (b - a), rate);
        std::copy (x.begin() + static_cast<std::ptrdiff_t> (a), x.begin() + static_cast<std::ptrdiff_t> (b), audio.channels[0].begin());
        return test::analyse (audio).pitch.fundamentalHz;
    }
}

TEST_CASE ("layers: the A/B blend crossfades two samples with equal power", "[integration][layers]")
{
    // Both recordings play A3; layer B is shifted a fifth up by its own pitch offset (as a
    // root correction would), so the two layers can be told apart.
    const auto a = sineModel (220.0), b = sineModel (220.0);
    auto measure = [&] (double blend) {
        auto s = quietSettings();
        s.blend = blend;
        Rig rig (s);
        rig.engine.setModel (a.get(), 0);
        rig.engine.setModel (b.get(), 1);
        rig.engine.setLayerPitchOffsetSemitones (1, 7.0);
        rig.engine.noteOn (57, 100, 1);
        std::vector<float> out;
        rig.run (1.0, out);
        return std::pair { goertzel (out, 12000, 36000, 220.0), goertzel (out, 12000, 36000, midiToHz (64)) };
    };
    const auto [a0, b0] = measure (0.0);
    const auto [a1, b1] = measure (1.0);
    const auto [ah, bh] = measure (0.5);
    CHECK (b0 < 1.0e-4 * a0);     // full left: only A
    CHECK (a1 < 1.0e-4 * b1);     // full right: only B
    // Centre: each at half power (-3 dB).
    CHECK (ah / a0 == Approx (0.5).margin (0.03));
    CHECK (bh / b1 == Approx (0.5).margin (0.03));
}

TEST_CASE ("layers: an empty layer is silence, and one layer alone matches the single-layer engine", "[integration][layers]")
{
    const auto a = sineModel (220.0);
    auto s = quietSettings();
    s.blend = 1.0;   // only B, which is empty
    Rig rig (s);
    rig.engine.setModel (a.get(), 0);
    rig.engine.noteOn (57, 100, 1);
    std::vector<float> out;
    rig.run (0.5, out);
    CHECK (rms (out, 0, out.size()) < 1.0e-6);
}

TEST_CASE ("granular: a held note sustains indefinitely at the played pitch", "[integration][layers][granular]")
{
    const auto model = sineModel (220.0, 1.0);   // a one-second recording
    double oneShot = 0.0;
    {
        auto s = quietSettings();
        Rig rig (s);
        rig.engine.setModel (model.get(), 0);
        rig.engine.noteOn (69, 100, 1);
        std::vector<float> out;
        rig.run (0.4, out);
        oneShot = rms (out, static_cast<std::size_t> (0.1 * rate), static_cast<std::size_t> (0.4 * rate));
    }
    for (const auto tune : { 0.0, 7.0 })
    {
        auto s = quietSettings();
        s.continuation = ContinuationStrategy::off;   // one shot would end with the recording
        s.sourceMode[0] = SourceMode::granular;
        s.granular[0].tuneSemitones = tune;
        Rig rig (s);
        rig.engine.setModel (model.get(), 0);
        rig.engine.setGranular (0, s.granular[0]);
        rig.engine.noteOn (69, 100, 1);   // an octave above the recording's A3
        std::vector<float> out;
        rig.run (8.0, out);
        const double early = rms (out, static_cast<std::size_t> (0.5 * rate), static_cast<std::size_t> (1.0 * rate));
        const double late = rms (out, static_cast<std::size_t> (7.0 * rate), static_cast<std::size_t> (8.0 * rate));
        INFO ("tune " << tune << ": rms early " << early << ", late " << late << ", one shot " << oneShot);
        CHECK (late > 0.5 * early);
        // About as loud as the recording played through (within 6 dB).
        CHECK (early > 0.5 * oneShot);
        CHECK (early < 2.0 * oneShot);
        CHECK (pitchOf (out, static_cast<std::size_t> (6.0 * rate), static_cast<std::size_t> (7.0 * rate))
               == Approx (440.0 * std::exp2 (tune / 12.0)).epsilon (0.01));
    }
}

TEST_CASE ("granular: note-off stops new grains, playing grains finish", "[integration][layers][granular]")
{
    const auto model = sineModel (220.0, 1.0);
    auto s = quietSettings();
    s.sourceMode[0] = SourceMode::granular;
    s.granular[0].sizeSeconds = 0.3;
    s.adsr.releaseSeconds = 2.0;   // long release: the grains, not the envelope, end the note
    Rig rig (s);
    rig.engine.setModel (model.get(), 0);
    rig.engine.setGranular (0, s.granular[0]);
    rig.engine.noteOn (57, 100, 1);
    std::vector<float> out;
    rig.run (1.0, out);
    int playing = 0;
    for (int i = 0; i < InstrumentEngine::voiceSlots(); ++i)
        playing += rig.engine.voiceAt (i).grainCount();
    CHECK (playing >= 2);   // DENS 14 x SIZE 0.3 s: about four overlapping grains
    rig.engine.noteOff (57, 1);
    rig.run (0.05, out);
    CHECK (rig.engine.activeVoiceCount() == 1);   // still sounding: grains are finishing
    rig.run (0.35, out);
    CHECK (rig.engine.activeVoiceCount() == 0);   // all finished within one grain length
}

TEST_CASE ("granular: polyphonic, each layer has its own mode, block-size independent", "[integration][layers][granular]")
{
    const auto model = sineModel (220.0, 1.0);
    auto s = quietSettings();
    s.continuation = ContinuationStrategy::off;
    s.sourceMode[1] = SourceMode::granular;   // A one shot, B granular
    s.blend = 0.5;
    auto render = [&] (int block) {
        Rig rig (s, block);
        rig.engine.setModel (model.get(), 0);
        rig.engine.setModel (model.get(), 1);
        rig.engine.noteOn (57, 100, 1);
        rig.engine.noteOn (64, 100, 1);
        std::vector<float> out;
        rig.run (0.5, out, block);
        const int voicesEarly = rig.engine.activeVoiceCount();
        rig.run (2.5, out, block);
        return std::tuple { out, voicesEarly, rig.engine.activeVoiceCount (0), rig.engine.activeVoiceCount (1) };
    };
    const auto [out, early, layerA, layerB] = render (256);
    CHECK (early == 4);    // two notes x two layers
    CHECK (layerA == 0);   // one shot: the one-second recording has ended
    CHECK (layerB == 2);   // granular: both notes still sounding
    // Both notes are in the granular layer's sustain.
    const auto a = static_cast<std::size_t> (2.0 * rate), b = static_cast<std::size_t> (3.0 * rate);
    CHECK (goertzel (out, a, b, 220.0) > 1.0e-4);
    CHECK (goertzel (out, a, b, midiToHz (64)) > 1.0e-4);
    const auto [other, e2, la2, lb2] = render (37);
    REQUIRE (other.size() == out.size());
    double diff = 0.0;
    for (std::size_t i = 0; i < out.size(); ++i)
        diff = std::max (diff, static_cast<double> (std::abs (out[i] - other[i])));
    CHECK (diff == 0.0);
}
