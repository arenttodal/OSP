// Adaptive 1-3 layer engine: the generalised mix, layer C, and every layer control
// (START, TUNE, PAN, LEVEL, REVERSE, LOOP, FOLLOW) through the whole engine.
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

    /** A sine that decays by 40 dB over its length (a struck, fading sound). */
    std::shared_ptr<InstrumentModel> decayingModel (double hz, double seconds)
    {
        auto audio = testsignals::sine (hz, seconds, rate, 0.5, 1);
        auto& x = audio.channels[0];
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] *= static_cast<float> (std::pow (10.0, -40.0 / 20.0 * static_cast<double> (i) / static_cast<double> (x.size())));
        testsignals::applyFades (audio, 0.005, 0.02);
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
        s.adsr.releaseSeconds = 0.05;
        s.velocityRangeDb = 0.0;
        return s;
    }

    struct Rig
    {
        InstrumentEngine engine;
        std::vector<float> l, r;
        explicit Rig (const EngineSettings& s, int block = 256)
        {
            engine.prepare (rate, block, s);
            for (int layer = 0; layer < EngineSettings::layers; ++layer)
                engine.setLayerSettings (layer, s.layer[static_cast<std::size_t> (layer)]);
            l.resize (static_cast<std::size_t> (block));
            r.resize (static_cast<std::size_t> (block));
        }
        void run (double seconds, std::vector<float>& outL, std::vector<float>* outR = nullptr, int block = 256)
        {
            const auto total = static_cast<int> (seconds * rate);
            for (int done = 0; done < total; done += block)
            {
                const int n = std::min (block, total - done);
                float* ch[2] = { l.data(), r.data() };
                engine.render (ch, 2, n);
                outL.insert (outL.end(), l.begin(), l.begin() + n);
                if (outR != nullptr)
                    outR->insert (outR->end(), r.begin(), r.begin() + n);
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
        return std::sqrt (e / static_cast<double> (std::max<std::size_t> (1, b - a)));
    }

    std::size_t at (double seconds) { return static_cast<std::size_t> (seconds * rate); }
}

TEST_CASE ("mix weights: one layer alone, two equal-power, three constant power", "[unit][layers]")
{
    // One occupied layer plays at unity wherever the controls are, whichever slot it is.
    for (int slot = 0; slot < 3; ++slot)
    {
        std::array<bool, 3> occupied {};
        occupied[static_cast<std::size_t> (slot)] = true;
        const auto w = InstrumentEngine::mixWeights (occupied, 0.8, 0.1, 0.9);
        for (int l = 0; l < 3; ++l)
            CHECK (w.gain[static_cast<std::size_t> (l)] == (l == slot ? 1.0 : 0.0));
    }
    // Two: exactly the A/B crossfade (cos / sin), also for A + C.
    for (double b : { 0.0, 0.25, 0.5, 1.0 })
    {
        const auto ab = InstrumentEngine::mixWeights ({ true, true, false }, b, 0.5, 0.5);
        CHECK (ab.gain[0] == std::cos (0.5 * std::numbers::pi * b));
        CHECK (ab.gain[1] == std::sin (0.5 * std::numbers::pi * b));
        CHECK (ab.gain[2] == 0.0);
        const auto ac = InstrumentEngine::mixWeights ({ true, false, true }, b, 0.5, 0.5);
        CHECK (ac.gain[0] == ab.gain[0]);
        CHECK (ac.gain[2] == ab.gain[1]);
    }
    // Three: constant power everywhere, a corner is one layer, the centre is all three equal.
    for (double x = 0.0; x <= 1.0; x += 0.125)
        for (double y = 0.0; y <= 1.0; y += 0.125)
        {
            const auto w = InstrumentEngine::mixWeights ({ true, true, true }, 0.0, x, y);
            const double power = w.gain[0] * w.gain[0] + w.gain[1] * w.gain[1] + w.gain[2] * w.gain[2];
            CHECK (power == Approx (1.0).margin (1.0e-12));
            for (double g : w.gain)
                CHECK (g >= 0.0);
        }
    auto corner = [] (double x, double y) { return InstrumentEngine::mixWeights ({ true, true, true }, 0.0, x, y).gain; };
    CHECK (corner (0.0, 0.0)[0] == Approx (1.0));
    CHECK (corner (0.5, 1.0)[1] == Approx (1.0));
    CHECK (corner (1.0, 0.0)[2] == Approx (1.0));
    const auto centre = corner (0.5, 1.0 / 3.0);
    for (double g : centre)
        CHECK (g == Approx (std::sqrt (1.0 / 3.0)).margin (1.0e-9));
    // Edge midpoint A-C: the two-layer equal-power middle.
    const auto edge = corner (0.5, 0.0);
    CHECK (edge[0] == Approx (std::sqrt (0.5)));
    CHECK (edge[2] == Approx (std::sqrt (0.5)));
    CHECK (edge[1] == Approx (0.0).margin (1.0e-12));
}

TEST_CASE ("three layers play together through the shared chain, weighted by the triangle", "[integration][layers]")
{
    // A, B and C all play A3 recordings, told apart by their TUNE: A 0, B +4, C +7 semitones.
    const auto a = sineModel (220.0), b = sineModel (220.0), c = sineModel (220.0);
    auto measure = [&] (double x, double y) {
        auto s = quietSettings();
        s.mixX = x;
        s.mixY = y;
        s.layer[1].tuneSemitones = 4.0;
        s.layer[2].tuneSemitones = 7.0;
        Rig rig (s);
        rig.engine.setModel (a.get(), 0);
        rig.engine.setModel (b.get(), 1);
        rig.engine.setModel (c.get(), 2);
        rig.engine.noteOn (57, 100, 1);
        std::vector<float> out;
        rig.run (1.0, out);
        CHECK (rig.engine.activeVoiceCount() == 3);
        CHECK (rig.engine.musicalVoiceCount() == 1);   // one played note, three layers
        return std::array<double, 3> { goertzel (out, at (0.25), at (0.75), 220.0), goertzel (out, at (0.25), at (0.75), midiToHz (61)),
                                       goertzel (out, at (0.25), at (0.75), midiToHz (64)) };
    };
    const auto onlyA = measure (0.0, 0.0), onlyB = measure (0.5, 1.0), onlyC = measure (1.0, 0.0);
    // (Below the bins' spectral leakage: the other layers are not rendered at all.)
    CHECK (onlyA[1] < 1.0e-3 * onlyA[0]);
    CHECK (onlyB[0] < 1.0e-3 * onlyB[1]);
    CHECK (onlyC[0] < 1.0e-3 * onlyC[2]);
    // At the centre each layer plays at a third of its solo power (constant total power).
    const auto centre = measure (0.5, 1.0 / 3.0);
    CHECK (centre[0] / onlyA[0] == Approx (1.0 / 3.0).margin (0.03));
    CHECK (centre[1] / onlyB[1] == Approx (1.0 / 3.0).margin (0.03));
    CHECK (centre[2] / onlyC[2] == Approx (1.0 / 3.0).margin (0.03));
}

TEST_CASE ("layers: one occupied slot plays at unity, an emptied layer's notes end", "[integration][layers]")
{
    const auto a = sineModel (220.0, 3.0);
    // Only C is loaded: it plays alone at unity (no half-mixed single sound).
    double solo = 0.0;
    {
        auto s = quietSettings();
        Rig rig (s);
        rig.engine.setModel (a.get(), 0);
        rig.engine.noteOn (57, 100, 1);
        std::vector<float> out;
        rig.run (0.6, out);
        solo = rms (out, at (0.2), at (0.6));
    }
    {
        auto s = quietSettings();
        s.blend = 0.7;   // whatever the controls say
        Rig rig (s);
        rig.engine.setModel (a.get(), 2);
        rig.engine.noteOn (57, 100, 1);
        std::vector<float> out;
        rig.run (0.6, out);
        CHECK (rms (out, at (0.2), at (0.6)) == Approx (solo).epsilon (1.0e-6));
    }
    // Emptying a layer while its note is held: it fades out and its voice ends (no crash,
    // no click: the gain glides for at least 10 ms).
    {
        auto s = quietSettings();
        s.blend = 0.5;
        Rig rig (s);
        rig.engine.setModel (a.get(), 0);
        rig.engine.setModel (a.get(), 1);
        rig.engine.noteOn (57, 100, 1);
        std::vector<float> out;
        rig.run (0.4, out);
        CHECK (rig.engine.activeVoiceCount (1) == 1);
        rig.engine.setModel (nullptr, 1);
        rig.run (0.1, out);
        CHECK (rig.engine.activeVoiceCount (1) == 0);
        CHECK (rig.engine.activeVoiceCount (0) == 1);
        double jump = 0.0;
        for (std::size_t i = at (0.4) - 64; i < at (0.45); ++i)
            jump = std::max (jump, static_cast<double> (std::abs (out[i] - out[i - 1])));
        CHECK (jump < 0.06);   // a 220 Hz sine at this level moves at most ~0.03 per sample
    }
}

TEST_CASE ("layers: a silent layer is not rendered, its held notes wait and come back", "[integration][layers][granular]")
{
    const auto a = sineModel (220.0, 2.0);
    auto s = quietSettings();
    s.blend = 0.0;   // B silent
    s.sourceMode[1] = SourceMode::granular;
    s.layer[1].tuneSemitones = 7.0;
    Rig rig (s);
    rig.engine.setModel (a.get(), 0);
    rig.engine.setModel (a.get(), 1);
    rig.engine.setGranular (1, s.granular[1]);
    rig.engine.noteOn (57, 100, 1);
    std::vector<float> out;
    rig.run (0.5, out);
    CHECK (rig.engine.activeVoiceCount (1) == 1);
    CHECK (rig.engine.voiceAt (1).grainCount() + rig.engine.voiceAt (0).grainCount() == 0);   // never rendered, no grains started
    // Fading B in: the held note sounds.
    rig.engine.setBlend (1.0);
    rig.run (0.5, out);
    CHECK (goertzel (out, at (0.8), at (1.0), midiToHz (64)) > 1.0e-4);
    // Back to A, release: B's waiting note ends.
    rig.engine.setBlend (0.0);
    rig.run (0.2, out);
    rig.engine.noteOff (57);
    rig.run (0.2, out);
    CHECK (rig.engine.activeVoiceCount (1) == 0);
}

TEST_CASE ("layer LEVEL and PAN trim and place a layer before the mix", "[integration][layers]")
{
    const auto a = sineModel (220.0, 2.0);
    auto render = [&] (double levelDb, double pan) {
        auto s = quietSettings();
        s.layer[0].levelDb = levelDb;
        s.layer[0].pan = pan;
        Rig rig (s);
        rig.engine.setModel (a.get(), 0);
        rig.engine.noteOn (57, 100, 1);
        std::vector<float> outL, outR;
        rig.run (0.6, outL, &outR);
        return std::pair { rms (outL, at (0.2), at (0.6)), rms (outR, at (0.2), at (0.6)) };
    };
    const auto [l0, r0] = render (0.0, 0.0);
    const auto [l6, r6] = render (-6.0, 0.0);
    CHECK (l6 / l0 == Approx (dbToGain (-6.0)).epsilon (0.01));
    CHECK (r6 / r0 == Approx (dbToGain (-6.0)).epsilon (0.01));
    const auto [lUp, rUp] = render (6.0, 0.0);
    CHECK (lUp / l0 == Approx (dbToGain (6.0)).epsilon (0.01));
    const auto [lMin, rMin] = render (LayerSettings::minLevelDb, 0.0);
    CHECK (lMin == 0.0);
    CHECK (rMin == 0.0);
    const auto [lLeft, rLeft] = render (0.0, -1.0);
    CHECK (lLeft == Approx (l0).epsilon (0.01));
    CHECK (rLeft < 1.0e-6);
    const auto [lHalf, rHalf] = render (0.0, 0.5);
    CHECK (lHalf / l0 == Approx (0.5).epsilon (0.01));
    CHECK (rHalf == Approx (r0).epsilon (0.01));
}

TEST_CASE ("layer TUNE transposes the layer on top of its root", "[integration][layers]")
{
    const auto a = sineModel (220.0, 2.0);
    for (double tune : { -12.0, 7.0, 24.0 })
    {
        auto s = quietSettings();
        s.layer[0].tuneSemitones = tune;
        Rig rig (s);
        rig.engine.setModel (a.get(), 0);
        rig.engine.noteOn (57, 100, 1);
        std::vector<float> out;
        rig.run (0.8, out);
        const double hz = 220.0 * std::exp2 (tune / 12.0);
        CHECK (goertzel (out, at (0.2), at (0.6), hz) > 20.0 * goertzel (out, at (0.2), at (0.6), 220.0 * std::exp2 ((tune + 1.0) / 12.0)));
    }
}

TEST_CASE ("layer START, REVERSE, LOOP and FOLLOW (One Shot)", "[integration][layers]")
{
    const auto fading = decayingModel (330.0, 2.0);
    auto render = [&] (LayerSettings layer, double seconds) {
        auto s = quietSettings();
        s.layer[0] = layer;
        Rig rig (s);
        rig.engine.setModel (fading.get(), 0);
        rig.engine.noteOn (64, 100, 1);
        std::vector<float> out;
        rig.run (seconds, out);
        return out;
    };
    // Where the sound ends (last 10 ms block above -60 dB re its loudest).
    auto soundEnd = [] (const std::vector<float>& x) {
        double peak = 1.0e-9;
        for (float v : x)
            peak = std::max (peak, static_cast<double> (std::abs (v)));
        std::size_t end = 0;
        for (std::size_t i = 0; i + 480 <= x.size(); i += 480)
            if (rms (x, i, i + 480) > 1.0e-3 * peak)
                end = i + 480;
        return static_cast<double> (end) / rate;
    };

    LayerSettings once;
    once.loop = false;
    const auto plain = render (once, 3.0);
    CHECK (soundEnd (plain) == Approx (2.0).margin (0.1));            // LOOP off: the recording once
    CHECK (rms (plain, at (0.05), at (0.25)) > 10.0 * rms (plain, at (1.7), at (1.9)));

    auto late = once;
    late.start = 0.5;
    const auto fromMiddle = render (late, 3.0);
    CHECK (soundEnd (fromMiddle) == Approx (1.0).margin (0.1));       // START 50 %: half of it is left
    CHECK (rms (fromMiddle, at (0.05), at (0.25)) == Approx (rms (plain, at (1.05), at (1.25))).epsilon (0.15));
    CHECK (std::abs (fromMiddle[0]) < 0.01f);                           // fades in: no click

    auto backwards = once;
    backwards.reverse = true;
    const auto reversed = render (backwards, 3.0);
    CHECK (rms (reversed, at (0.05), at (0.25)) < 0.2 * rms (reversed, at (1.6), at (1.8)));   // the decay, backwards: it swells
    CHECK (soundEnd (reversed) == Approx (2.0).margin (0.15));

    auto flat = once;
    flat.follow = false;
    const auto flattened = render (flat, 3.0);
    // FOLLOW off: the 40 dB decay is lifted (by up to 24 dB).
    const double fallFollow = rms (plain, at (0.1), at (0.3)) / rms (plain, at (1.6), at (1.8));
    const double fallFlat = rms (flattened, at (0.1), at (0.3)) / rms (flattened, at (1.6), at (1.8));
    CHECK (fallFlat < 0.25 * fallFollow);

}

TEST_CASE ("layer LOOP: on sustains a held note past the recording, off plays it once", "[integration][layers]")
{
    auto audio = testsignals::vowel (220.0, 2.0, rate, 3);
    const auto steady = instrument::buildComplete (audio, test::analyse (audio), {}, false);
    REQUIRE (steady->original.continuation.canSustain);
    for (bool loop : { true, false })
    {
        auto s = quietSettings();
        s.layer[0].loop = loop;
        Rig rig (s);
        rig.engine.setModel (steady.get(), 0);
        rig.engine.noteOn (57, 100, 1);
        std::vector<float> out;
        rig.run (4.0, out);
        INFO ("loop " << loop);
        if (loop)
            CHECK (rms (out, at (3.0), at (4.0)) > 0.5 * rms (out, at (0.5), at (1.0)));
        else
            CHECK (rms (out, at (3.0), at (4.0)) == 0.0);
    }
}

TEST_CASE ("REVERSE with LOOP keeps a held note going backwards; Granular REVERSE/FOLLOW are deterministic", "[integration][layers][granular]")
{
    auto vowel = testsignals::vowel (220.0, 2.0, rate, 3);
    const auto sustained = instrument::buildComplete (vowel, test::analyse (vowel), {}, false);
    REQUIRE (sustained->original.continuation.bestLoop >= 0);
    {
        auto s = quietSettings();
        s.layer[0].reverse = true;
        Rig rig (s);
        rig.engine.setModel (sustained.get(), 0);
        rig.engine.noteOn (57, 100, 1);
        std::vector<float> out;
        rig.run (5.0, out);
        CHECK (rms (out, at (4.0), at (5.0)) > 0.3 * rms (out, at (0.5), at (1.0)));
        CHECK (rig.engine.activeVoiceCount() == 1);
    }
    auto render = [&] (int block) {
        auto s = quietSettings();
        s.sourceMode[0] = SourceMode::granular;
        s.layer[0].reverse = true;
        s.layer[0].follow = false;
        s.layer[0].start = 0.2;
        Rig rig (s, block);
        rig.engine.setModel (sustained.get(), 0);
        rig.engine.setGranular (0, s.granular[0]);
        rig.engine.noteOn (57, 100, 1);
        std::vector<float> out;
        rig.run (1.5, out, nullptr, block);
        return out;
    };
    const auto a = render (256), b = render (37);
    REQUIRE (a.size() == b.size());
    CHECK (a == b);
    CHECK (rms (a, at (0.5), at (1.5)) > 1.0e-3);
}

TEST_CASE ("three layers in all eight One Shot / Granular combinations: playable, block-size independent", "[integration][layers][granular]")
{
    const auto a = sineModel (220.0, 1.5), b = sineModel (330.0, 1.5), c = sineModel (165.0, 1.5);
    for (int combo = 0; combo < 8; ++combo)
    {
        auto render = [&] (int block) {
            auto s = quietSettings();
            s.mixX = 0.5;
            s.mixY = 1.0 / 3.0;
            for (int l = 0; l < 3; ++l)
                s.sourceMode[static_cast<std::size_t> (l)] = (combo >> l) & 1 ? SourceMode::granular : SourceMode::oneShot;
            Rig rig (s, block);
            rig.engine.setModel (a.get(), 0);
            rig.engine.setModel (b.get(), 1);
            rig.engine.setModel (c.get(), 2);
            for (int l = 0; l < 3; ++l)
                rig.engine.setGranular (l, s.granular[static_cast<std::size_t> (l)]);
            rig.engine.noteOn (57, 100, 1);
            rig.engine.noteOn (64, 90, 1);
            std::vector<float> out;
            rig.run (0.8, out, nullptr, block);
            rig.engine.noteOff (57);
            rig.engine.noteOff (64);
            rig.run (0.6, out, nullptr, block);
            return std::pair { out, rig.engine.musicalVoiceCount() };
        };
        const auto [x, voicesAfter] = render (256);
        const auto [y, unused] = render (100);
        INFO ("combination " << combo);
        CHECK (rms (x, at (0.2), at (0.8)) > 1.0e-3);
        CHECK (x == y);
        CHECK (voicesAfter == 0);
        for (float v : x)
            REQUIRE (std::isfinite (v));
    }
}

TEST_CASE ("the instrument's envelope: decay to sustain shapes every layer, S = 0 ends notes", "[integration][layers][adsr]")
{
    auto vowel = testsignals::vowel (220.0, 2.0, rate, 3);
    const auto model = instrument::buildComplete (vowel, test::analyse (vowel), {}, false);
    auto render = [&] (double sustain, SourceMode modeB) {
        auto s = quietSettings();
        s.adsr.decaySeconds = 0.3;
        s.adsr.sustainLevel = sustain;
        s.blend = 0.5;
        s.sourceMode[1] = modeB;
        Rig rig (s);
        rig.engine.setModel (model.get(), 0);
        rig.engine.setModel (model.get(), 1);
        rig.engine.setGranular (1, s.granular[1]);
        rig.engine.noteOn (57, 100, 1);
        std::vector<float> out;
        rig.run (2.0, out);
        return std::pair { out, rig.engine.activeVoiceCount() };
    };
    for (auto mode : { SourceMode::oneShot, SourceMode::granular })
    {
        const auto [full, voicesFull] = render (1.0, mode);
        const auto [half, voicesHalf] = render (0.5, mode);
        const auto [none, voicesNone] = render (0.0, mode);
        INFO ("B " << (mode == SourceMode::granular ? "granular" : "one shot"));
        // After the decay the held note sits at the sustain level (amplitude ratio).
        CHECK (rms (half, at (1.0), at (1.8)) / rms (full, at (1.0), at (1.8)) == Approx (0.5).margin (0.06));
        CHECK (voicesFull == 2);
        CHECK (voicesHalf == 2);
        CHECK (voicesNone == 0);   // a pluck: it has ended although the key is held
        CHECK (rms (none, at (1.0), at (1.8)) == 0.0);
    }
}

TEST_CASE ("Granular layer modifiers: START moves the grains, FOLLOW off lifts quiet parts", "[integration][layers][granular]")
{
    const auto fading = decayingModel (330.0, 2.0);
    auto run = [&] (double start, bool follow, double position) {
        auto s = quietSettings();
        s.sourceMode[0] = SourceMode::granular;
        s.granular[0].position = position;
        s.granular[0].spread = 0.0;
        s.layer[0].start = start;
        s.layer[0].follow = follow;
        Rig rig (s);
        rig.engine.setModel (fading.get(), 0);
        rig.engine.setGranular (0, s.granular[0]);
        rig.engine.noteOn (64, 100, 1);
        std::vector<float> out;
        rig.run (0.6, out);
        const auto& snap = rig.engine.grainSnapshot (0);
        double sum = 0.0;
        const int count = snap.count.load();
        for (int i = 0; i < count; ++i)
            sum += snap.position[static_cast<std::size_t> (i)].load();
        return std::pair { count > 0 ? sum / count : -1.0, rms (out, at (0.2), at (0.6)) };
    };
    const auto [atPos, levelPos] = run (0.0, true, 0.2);
    const auto [shifted, levelShifted] = run (0.5, true, 0.2);
    CHECK (atPos == Approx (0.2).margin (0.06));
    CHECK (shifted == Approx (0.7).margin (0.06));   // START adds to POS
    // FOLLOW off: grains from the quiet end are lifted towards the loudest part's level.
    const auto [tail, quiet] = run (0.0, true, 0.85);
    const auto [tailLifted, lifted] = run (0.0, false, 0.85);
    CHECK (lifted > 4.0 * quiet);
    CHECK (levelPos > 0.0);
    CHECK (levelShifted > 0.0);
    CHECK (tail == Approx (tailLifted).margin (1.0e-6));
}
