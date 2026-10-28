#include "AndorTestUtil.h"

#include "andor/dsp/OfflineRender.h"
#include "andor/dsp/Wavetable.h"
#include "andor/grain/GrainEngine.h"
#include "andor/grain/KeyDna.h"
#include "core/PitchMath.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <complex>

using namespace osp;
using namespace osp::andortest;
using Catch::Approx;

namespace
{
    constexpr double sr = 48000.0;

    /** A harmonic tone (fundamental `hz`) with a slow level wobble: something to granulate. */
    AudioData tone (double hz, double seconds, int channels = 2)
    {
        auto a = AudioData::allocate (channels, static_cast<std::int64_t> (seconds * sr), sr);
        for (std::size_t i = 0; i < a.channels[0].size(); ++i)
        {
            const double t = static_cast<double> (i) / sr;
            double s = 0.0;
            for (int k = 1; k <= 6; ++k)
                s += std::sin (andor::twoPi * hz * k * t) / k;
            for (auto& ch : a.channels)
                ch[i] = static_cast<float> (0.3 * s * (0.8 + 0.2 * std::sin (andor::twoPi * 0.7 * t)));
        }
        return a;
    }

    std::shared_ptr<grain::GrainModel> modelOf (const AudioData& audio, double root)
    {
        auto src = std::make_shared<const PlaybackSource> (audio, root, grain::grainPadding());
        return grain::buildGrainModel (src, root, 1);
    }

    AudioData play (const grain::GrainModel* model, const MidiSequence& seq, const grain::GrainParams& p, double tail = 1.0, int block = 256)
    {
        grain::GrainEngine e;
        e.prepare (sr, block);
        e.setModel (model);
        e.setParams (p);
        return andor::renderOffline (e, seq, sr, block, tail);
    }

    double cents (double a, double b) { return 1200.0 * std::log2 (b / a); }
}

TEST_CASE ("Grain: windows, normalised domains and quality limits", "[grain][unit]")
{
    for (auto p : { grain::DnaParam::size, grain::DnaParam::density, grain::DnaParam::position, grain::DnaParam::positionScatter,
                    grain::DnaParam::pitchScatter, grain::DnaParam::tone, grain::DnaParam::spread })
        for (double n : { 0.0, 0.25, 0.5, 0.9, 1.0 })
            CHECK (grain::toNormalised (p, grain::fromNormalised (p, n)) == Approx (n).margin (1.0e-9));
    CHECK (grain::fromNormalised (grain::DnaParam::size, 0.0) == Approx (0.005));
    CHECK (grain::fromNormalised (grain::DnaParam::size, 1.0) == Approx (1.0));
    CHECK (grain::fromNormalised (grain::DnaParam::density, 1.0) == Approx (200.0));
    CHECK (grain::limitsFor (grain::Quality::eco).voices == 8);
    CHECK (grain::limitsFor (grain::Quality::standard).voices == 12);
    CHECK (grain::limitsFor (grain::Quality::high).voices == 16);
}

TEST_CASE ("Grain: grains play the source at the key's pitch", "[grain][integration]")
{
    const auto model = modelOf (tone (220.0, 3.0), 57.0);   // A3
    grain::GrainParams p;
    p.sizeSeconds = 0.08;
    p.density = 40.0;
    p.positionScatter = 0.2;
    p.spread = 0.0;
    for (int key : { 57, 69, 45, 64 })
    {
        INFO (key);
        MidiSequence seq;
        andor::addNote (seq, 0.0, 1.0, key);
        const auto out = play (model.get(), seq, p, 0.5);
        CHECK (allFinite (out));
        CHECK (std::abs (cents (midiToHz (key), pitchOf (out, 0.2, 0.9))) < 8.0);
    }
}

TEST_CASE ("Grain: reads never leave the chosen region", "[grain][unit]")
{
    // Inside the region: silence. Outside: a loud constant. Any read beyond the region (except
    // the interpolator's two neighbours under a window that is already ~0) would be heard.
    auto audio = AudioData::allocate (2, static_cast<std::int64_t> (2.0 * sr), sr);
    for (auto& ch : audio.channels)
        for (std::size_t i = 0; i < ch.size(); ++i)
            ch[i] = (i < 48000 || i >= 52800) ? 1.0f : 0.0f;   // region: 1.0 s .. 1.1 s
    atlas::NoteAtlas a;
    a.sources.push_back ({ "src0", "", "", "", "", "", sr, 2, audio.numFrames() });
    atlas::NoteEvent n;
    n.id = "n0000";
    n.sourceId = "src0";
    n.sourceStart = n.attackStart = 48000;
    n.sustain = { 48000, 52800 };
    n.releaseStart = n.sourceEnd = 52800;
    n.midiPitch = 60.0;
    n.rootMidi = 60;
    n.included = true;
    a.notes.push_back (n);
    auto src = std::make_shared<const PlaybackSource> (audio, 60.0, grain::grainPadding());
    const auto model = grain::buildGrainModel (src, std::make_shared<const atlas::NoteAtlas> (a), 60.0, 1);
    REQUIRE (model->regions.size() == 1);

    grain::GrainParams p;
    p.policy = grain::SourcePolicy::fixedNote;
    p.sizeSeconds = 1.0;          // far longer than the 100 ms region: grains shorten
    p.density = 120.0;
    p.positionScatter = 1.0;
    p.pitchScatter = 12.0;
    p.reverseProbability = 0.5;
    p.toneHz = 20000.0;
    for (double pos : { 0.0, 1.0 })
        for (int key : { 36, 60, 96 })
        {
            INFO (pos << " " << key);
            p.position = pos;
            MidiSequence seq;
            andor::addNote (seq, 0.0, 0.5, key);
            const auto out = play (model.get(), seq, p, 0.3);
            REQUIRE (allFinite (out));
            float peak = 0.0f;
            for (const auto& ch : out.channels)
                for (float s : ch)
                    peak = std::max (peak, std::abs (s));
            CHECK (peak < 0.02f);
        }
}

TEST_CASE ("Grain: stereo stays coherent and mono-compatible", "[grain][unit]")
{
    const auto model = modelOf (tone (330.0, 2.0, 1), 64.0);   // mono source
    MidiSequence seq;
    andor::addNote (seq, 0.0, 1.0, 64);
    grain::GrainParams p;
    p.spread = 0.0;
    p.triggerVariation = 0.0;   // (it would vary SPREAD a little per note)
    auto out = play (model.get(), seq, p, 0.3);
    CHECK (out.channels[0] == out.channels[1]);   // no spread: identical channels

    p.spread = 1.0;
    out = play (model.get(), seq, p, 0.3);
    CHECK (out.channels[0] != out.channels[1]);
    // Panning only weights the same reads: the mono sum keeps the signal (no cancellation).
    AudioData mono = AudioData::allocate (1, out.numFrames(), sr);
    for (std::size_t i = 0; i < mono.channels[0].size(); ++i)
        mono.channels[0][i] = 0.5f * (out.channels[0][i] + out.channels[1][i]);
    CHECK (rmsOf (mono, 0.2, 0.9) > 0.5 * rmsOf (out, 0.2, 0.9));
}

TEST_CASE ("Grain: odd sources and extremes are safe", "[grain][unit]")
{
    grain::GrainParams p;
    MidiSequence seq;
    for (int k = 0; k < 16; ++k)
        andor::addNote (seq, 0.01 * k, 0.6, 40 + 3 * k);

    SECTION ("silent source")
    {
        const auto model = modelOf (AudioData::allocate (2, 48000, sr), 60.0);
        const auto out = play (model.get(), seq, p, 0.5);
        CHECK (allFinite (out));
        CHECK (rmsOf (out, 0.0, 1.0) == 0.0);
    }
    SECTION ("a 5 ms source, long grains, extreme pitch and position")
    {
        const auto model = modelOf (tone (440.0, 0.005), 69.0);
        p.sizeSeconds = 1.0;
        p.pitchSemitones = 24.0;
        p.position = 1.0;
        p.positionScatter = 1.0;
        p.reverseProbability = 1.0;
        CHECK (allFinite (play (model.get(), seq, p, 0.5)));
    }
    SECTION ("no source: the oscillator still plays, grains are silent")
    {
        const auto model = grain::buildGrainModel (nullptr, 60.0, 1);
        p.oscMix = 0.0;
        CHECK (rmsOf (play (model.get(), seq, p, 0.5), 0.0, 0.6) == 0.0);
        p.oscMix = 1.0;
        CHECK (rmsOf (play (model.get(), seq, p, 0.5), 0.1, 0.6) > 0.01);
    }
    SECTION ("no model at all")
    {
        p.oscMix = 0.5;
        CHECK (allFinite (play (nullptr, seq, p, 0.5)));
    }
}

TEST_CASE ("Grain: the global grain budget is never exceeded and shared fairly", "[grain][integration]")
{
    const auto model = modelOf (tone (220.0, 3.0), 57.0);
    grain::GrainParams p;
    p.quality = grain::Quality::high;
    p.sizeSeconds = 1.0;
    p.density = 200.0;   // 200 grains/s x 1 s = 200 overlapping per voice, far over budget
    grain::GrainEngine e;
    e.prepare (sr, 128);
    e.setModel (model.get());
    e.setParams (p);
    std::vector<float> l (128), r (128);

    e.noteOn (57, 1.0f);
    for (int b = 0; b < 400; ++b)
        e.render (l.data(), r.data(), 128);
    CHECK (e.activeGrains() <= e.grainBudget());
    // Alone, a voice may use the budget up to its own pool of grain slots.
    CHECK (e.activeGrains() >= std::min (e.grainBudget(), grain::GrainEngine::maxGrainsPerVoice) - 8);
    CHECK (e.deniedGrains() > 0);

    // A second note gets its fair half: the first voice stops growing, the second gets grains.
    e.noteOn (64, 1.0f);
    for (int b = 0; b < 400; ++b)
    {
        e.render (l.data(), r.data(), 128);
        CHECK (e.activeGrains() <= e.grainBudget());
    }
    grain::GrainEngine::VoiceView views[4];
    CHECK (e.collectVoices (views, 4) == 2);
    grain::GrainEngine::GrainView grains[512];
    const int n = e.collectGrains (grains, 512);
    int second = 0;
    for (int i = 0; i < n; ++i)
        second += grains[i].voice != grains[0].voice ? 1 : 0;
    CHECK (second > e.grainBudget() / 4);
    CHECK (e.peakGrains() <= e.grainBudget());

    for (auto q : { grain::Quality::eco, grain::Quality::standard })
    {
        p.quality = q;
        e.setParams (p);
        for (int k = 0; k < 16; ++k)
            e.noteOn (40 + k, 1.0f);
        for (int b = 0; b < 200; ++b)
        {
            e.render (l.data(), r.data(), 128);
            REQUIRE (e.activeGrains() <= grain::limitsFor (q).grainBudget + grain::limitsFor (grain::Quality::high).grainBudget);
        }
        for (int b = 0; b < 400; ++b)
            e.render (l.data(), r.data(), 128);
        CHECK (e.activeGrains() <= grain::limitsFor (q).grainBudget);
        e.panic();
    }
}

TEST_CASE ("Grain: polyphony, stealing, pedal, panic and release", "[grain][integration]")
{
    const auto model = modelOf (tone (220.0, 2.0), 57.0);
    grain::GrainParams p;
    p.releaseSeconds = 0.1;
    std::vector<float> l (256), r (256);
    for (auto [q, voices] : { std::pair { grain::Quality::eco, 8 }, std::pair { grain::Quality::standard, 12 }, std::pair { grain::Quality::high, 16 } })
    {
        grain::GrainEngine e;
        e.prepare (sr, 256);
        e.setModel (model.get());
        p.quality = q;
        e.setParams (p);
        for (int count : { 1, 4, 8, 12, 16 })
        {
            for (int k = 0; k < count; ++k)
                e.noteOn (48 + k, 0.8f);
            for (int b = 0; b < 10; ++b)
                e.render (l.data(), r.data(), 256);
            CHECK (e.activeVoices() == std::min (count, voices));
            e.allNotesOff();
            for (int b = 0; b < 40; ++b)
                e.render (l.data(), r.data(), 256);
            CHECK (e.activeVoices() == 0);
            CHECK (e.activeGrains() == 0);
        }
    }

    grain::GrainEngine e;
    e.prepare (sr, 256);
    e.setModel (model.get());
    p.quality = grain::Quality::standard;
    e.setParams (p);
    // Rapid triggering: 300 notes in a row never exceed the limit or leak grains.
    for (int i = 0; i < 300; ++i)
    {
        e.noteOn (36 + (i * 7) % 48, 0.9f);
        e.render (l.data(), r.data(), 64);
        if (i % 3 == 0)
            e.noteOff (36 + (i * 7) % 48);
        REQUIRE (e.playingVoices() <= 12);   // plus at most the spare slots fading out
        REQUIRE (e.activeVoices() <= grain::GrainEngine::maxVoices + 4);
    }
    e.panic();
    CHECK (e.activeVoices() == 0);
    CHECK (e.activeGrains() == 0);

    // Sustain pedal.
    e.setSustainPedal (true);
    e.noteOn (60, 1.0f);
    e.noteOff (60);
    for (int b = 0; b < 40; ++b)
        e.render (l.data(), r.data(), 256);
    CHECK (e.activeVoices() == 1);
    e.setSustainPedal (false);
    for (int b = 0; b < 40; ++b)
        e.render (l.data(), r.data(), 256);
    CHECK (e.activeVoices() == 0);
}

TEST_CASE ("Grain: the oscillator is band-limited", "[grain][unit]")
{
    andor::WavetableBank bank;
    bank.prepare (sr);
    for (int level = 0; level < andor::WavetableBank::numLevels; ++level)
        if (bank.harmonicsAt (level) > 1)   // the top level is a lone sine (fundamentals near Nyquist)
            CHECK (bank.harmonicsAt (level) * 20.0 * std::exp2 (level + 1) <= 0.45 * sr + 1.0);

    // A saw at C8 (4186 Hz): energy where its 6th harmonic would alias (48000 - 25116 Hz) must be tiny.
    andor::WavetableOscillator osc;
    const double f0 = midiToHz (108.0);
    const int n = 48000;
    std::vector<float> x (n);
    for (int i = 0; i < n; ++i)
        x[static_cast<std::size_t> (i)] = osc.next (bank, 2.0, f0, sr);
    auto magnitude = [&] (double hz)
    {
        std::complex<double> acc;
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (andor::twoPi * i / n);
            acc += w * static_cast<double> (x[static_cast<std::size_t> (i)]) * std::polar (1.0, -andor::twoPi * hz * i / sr);
        }
        return std::abs (acc);
    };
    const double fundamental = magnitude (f0);
    CHECK (magnitude (sr - 6.0 * f0) < fundamental * 1.0e-3);   // < -60 dB
    CHECK (magnitude (sr - 7.0 * f0) < fundamental * 1.0e-3);
    CHECK (magnitude (2.0 * f0) > fundamental * 0.3);           // the real harmonics are there
}

TEST_CASE ("KEY DNA: curves, stable per-key profiles and predictable individuality", "[grain][unit]")
{
    grain::DnaCurve c;
    CHECK (c.evaluate (60.0f) == 0.0f);
    c.addPoint (36.0f, 0.8f);
    c.addPoint (96.0f, -0.6f);
    c.addPoint (60.0f, 0.0f);
    REQUIRE (c.count == 3);
    CHECK (c.points[1].key == 60.0f);
    CHECK (c.evaluate (0.0f) == Approx (0.8f));     // flat outside
    CHECK (c.evaluate (127.0f) == Approx (-0.6f));
    CHECK (c.evaluate (48.0f) == Approx (0.4f));
    c.addPoint (60.2f, 0.5f);                        // replaces the point at 60
    CHECK (c.count == 3);
    CHECK (c.evaluate (60.0f) == Approx (0.5f).margin (0.01));
    c.removePoint (1);
    CHECK (c.count == 2);

    grain::KeyDnaSettings s;
    s.variationAmount = 1.0;
    s.seed = 42;
    grain::KeyDnaTable a, b;
    a.compute (s);
    b.compute (s);
    for (int key = 0; key < 128; ++key)
        for (int p = 0; p < grain::numDnaParams; ++p)
        {
            const auto dp = static_cast<grain::DnaParam> (p);
            CHECK (a.offset (key, dp) == b.offset (key, dp));
            CHECK (std::abs (a.variation (key, dp)) <= 1.0f);
        }
    s.seed = 43;
    b.compute (s);
    int differ = 0;
    for (int key = 0; key < 128; ++key)
        differ += a.variation (key, grain::DnaParam::size) != b.variation (key, grain::DnaParam::size) ? 1 : 0;
    CHECK (differ > 120);

    // Smooth -> Individual lowers the neighbour correlation monotonically.
    double previous = 2.0;
    for (double ind : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        s.individuality = ind;
        a.compute (s);
        double mean = 0.0;
        for (int p = 0; p < grain::numDnaParams; ++p)
            mean += a.neighbourCorrelation (static_cast<grain::DnaParam> (p));
        mean /= grain::numDnaParams;
        INFO (ind << " -> " << mean);
        CHECK (mean < previous);
        previous = mean;
        if (ind == 0.0)
            CHECK (mean > 0.9);
        if (ind == 1.0)
            CHECK (std::abs (mean) < 0.25);
    }
}

TEST_CASE ("KEY DNA in the engine: curves change character across the keyboard", "[grain][integration]")
{
    const auto model = modelOf (tone (220.0, 3.0), 57.0);
    grain::GrainParams p;
    p.sizeSeconds = 0.1;
    p.dna.curves[static_cast<std::size_t> (grain::DnaParam::size)].addPoint (24.0f, 1.0f);   // low: long grains
    p.dna.curves[static_cast<std::size_t> (grain::DnaParam::size)].addPoint (96.0f, -1.0f);  // high: short grains
    p.dna.curves[static_cast<std::size_t> (grain::DnaParam::position)].addPoint (24.0f, -0.6f);
    p.dna.curves[static_cast<std::size_t> (grain::DnaParam::position)].addPoint (96.0f, 0.6f);
    grain::GrainEngine e;
    e.prepare (sr, 128);
    e.setModel (model.get());
    e.setParams (p);
    CHECK (e.profileValue (36, grain::DnaParam::size) > 2.0 * e.profileValue (60, grain::DnaParam::size));
    CHECK (e.profileValue (84, grain::DnaParam::size) < 0.5 * e.profileValue (60, grain::DnaParam::size));
    CHECK (e.profileValue (84, grain::DnaParam::position) > e.profileValue (36, grain::DnaParam::position));
    p.dna.curveAmount = 0.0;
    e.setParams (p);
    CHECK (e.profileValue (36, grain::DnaParam::size) == Approx (0.1).epsilon (1.0e-6));

    // The voice uses its key's profile (effective values reported for the display).
    p.dna.curveAmount = 1.0;
    p.triggerVariation = 0.0;
    e.setParams (p);
    e.noteOn (36, 1.0f);
    e.noteOn (84, 1.0f);
    std::vector<float> l (128), r (128);
    e.render (l.data(), r.data(), 128);
    grain::GrainEngine::VoiceView views[2];
    REQUIRE (e.collectVoices (views, 2) == 2);
    const auto& low = views[0].key == 36 ? views[0] : views[1];
    const auto& high = views[0].key == 36 ? views[1] : views[0];
    CHECK (low.effective[0] > high.effective[0] + 0.3f);
}

TEST_CASE ("KEY DNA: stable identity, per-trigger variation and per-grain randomness are separate", "[grain][integration]")
{
    const auto model = modelOf (tone (220.0, 3.0), 57.0);
    grain::GrainParams p;
    p.dna.variationAmount = 1.0;
    p.dna.individuality = 1.0;
    p.triggerVariation = 0.0;

    auto effectiveOf = [&] (int key, std::uint64_t eventsBefore)
    {
        grain::GrainEngine e;
        e.prepare (sr, 64);
        e.setModel (model.get());
        e.setParams (p);
        std::vector<float> l (64), r (64);
        for (std::uint64_t i = 0; i < eventsBefore; ++i)
        {
            e.noteOn (30, 1.0f);
            e.panic();
        }
        e.noteOn (key, 1.0f);
        e.render (l.data(), r.data(), 64);
        grain::GrainEngine::VoiceView v[1];
        e.collectVoices (v, 1);
        return v[0].effective;
    };
    // Without trigger variation, C4's profile is the same whatever happened before.
    CHECK (effectiveOf (60, 0) == effectiveOf (60, 5));
    // Neighbouring keys differ (Individual).
    CHECK (effectiveOf (60, 0) != effectiveOf (61, 0));
    // With trigger variation, repeated C4s differ a little around the same identity.
    p.triggerVariation = 1.0;
    const auto a = effectiveOf (60, 0), b = effectiveOf (60, 1);
    CHECK (a != b);
    for (int i = 0; i < grain::numDnaParams; ++i)
        CHECK (std::abs (a[static_cast<std::size_t> (i)] - b[static_cast<std::size_t> (i)]) <= 0.5f + 1.0e-6f);
}

TEST_CASE ("Grain: modulation moves parameters per voice", "[grain][integration]")
{
    const auto model = modelOf (tone (220.0, 3.0), 57.0);
    grain::GrainParams p;
    p.triggerVariation = 0.0;
    p.mod[0] = { grain::ModSource::velocity, grain::ModDest::size, 0.5 };
    p.mod[1] = { grain::ModSource::lfo1, grain::ModDest::position, 0.4 };
    p.lfo1Hz = 2.0;
    grain::GrainEngine e;
    e.prepare (sr, 64);
    e.setModel (model.get());
    e.setParams (p);
    e.noteOn (60, 1.0f);
    e.noteOn (64, 0.2f);
    std::vector<float> l (64), r (64);
    grain::GrainEngine::VoiceView v[2];
    float minPos = 1.0f, maxPos = 0.0f;
    for (int b = 0; b < 400; ++b)
    {
        e.render (l.data(), r.data(), 64);
        e.collectVoices (v, 2);
        const auto& loud = v[0].key == 60 ? v[0] : v[1];
        minPos = std::min (minPos, loud.effective[2]);
        maxPos = std::max (maxPos, loud.effective[2]);
    }
    const auto& loud = v[0].key == 60 ? v[0] : v[1];
    const auto& soft = v[0].key == 60 ? v[1] : v[0];
    CHECK (loud.effective[0] > soft.effective[0] + 0.3f);   // velocity -> size, per voice
    CHECK (maxPos - minPos > 0.5f);                          // the LFO sweeps position
}

TEST_CASE ("Grain: deterministic and independent of the host block size", "[grain][regression]")
{
    const auto model = modelOf (tone (220.0, 3.0), 57.0);
    MidiSequence seq;
    for (int i = 0; i < 10; ++i)
        andor::addNote (seq, 0.17 * i, 0.5, 48 + (i * 5) % 24, 50 + 7 * i);
    grain::GrainParams p;
    p.dna.variationAmount = 0.6;
    p.triggerVariation = 0.3;
    p.pitchScatter = 1.0;
    p.reverseProbability = 0.3;
    p.oscMix = 0.3;
    p.space = 0.25;
    p.mod[0] = { grain::ModSource::lfo2, grain::ModDest::density, 0.3 };
    const auto a = play (model.get(), seq, p, 0.8, 64);
    const auto b = play (model.get(), seq, p, 0.8, 64);
    const auto c = play (model.get(), seq, p, 0.8, 500);
    CHECK (a.channels == b.channels);
    CHECK (a.channels == c.channels);
    p.seed = 99;
    CHECK (play (model.get(), seq, p, 0.8, 64).channels != a.channels);
    CHECK (allFinite (a));
}
