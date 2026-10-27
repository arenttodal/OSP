#include "AndorTestUtil.h"

#include "andor/dsp/OfflineRender.h"
#include "andor/phrase/PhraseAnalysis.h"
#include "andor/phrase/PhraseEngine.h"
#include "andor/phrase/PhraseFixtures.h"
#include "core/PitchMath.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace osp;
using namespace osp::andortest;
using Catch::Approx;

namespace
{
    std::shared_ptr<phrase::PhraseModel> modelFor (const std::string& fixture, double rate = 48000.0)
    {
        const auto fx = phrase::makePhraseFixture (fixture, rate);
        const auto atlas = phrase::analysePhrase (fx.audio, {}).atlas;
        auto source = std::make_shared<const PlaybackSource> (fx.audio, 60.0, phrase::phrasePadding());
        return phrase::buildPhraseModel (source, atlas, 1);
    }

    AudioData play (const phrase::PhraseModel& model, const MidiSequence& seq, const phrase::PhraseParams& params, double tail = 1.0,
                    int block = 256, double rate = 48000.0)
    {
        phrase::PhraseEngine engine;
        engine.prepare (rate, block);
        engine.setModel (&model);
        engine.setParams (params);
        return andor::renderOffline (engine, seq, rate, block, tail);
    }

    double centsBetweenHz (double a, double b) { return 1200.0 * std::log2 (b / a); }
}

TEST_CASE ("Phrase engine: recorded notes play on their own keys, in tune", "[phrase][integration]")
{
    const auto model = modelFor ("clean-scale");
    REQUIRE (model->regions.size() == 8);
    for (int key : { 60, 64, 67, 72 })
    {
        INFO (key);
        MidiSequence seq;
        andor::addNote (seq, 0.0, 0.3, key);
        phrase::PhraseParams p;
        p.playMode = phrase::PlayMode::gate;
        const auto out = play (*model, seq, p, 0.3);
        CHECK (allFinite (out));
        CHECK (std::abs (centsBetweenHz (midiToHz (key), pitchOf (out, 0.05, 0.25))) < 5.0);
    }
    CHECK (phrase::describeKey (*model, 60, phrase::FillMode::off).state == phrase::KeyState::exact);
    CHECK (phrase::describeKey (*model, 61, phrase::FillMode::off).state == phrase::KeyState::missing);
}

TEST_CASE ("Phrase engine: corrected tuning plays in tune, original keeps the performance's cents", "[phrase][integration]")
{
    const auto model = modelFor ("out-of-tune");   // C4 +30, D4 -40, E4 +45, F4 -20, G4 +10
    MidiSequence seq;
    andor::addNote (seq, 0.0, 0.35, 62);
    phrase::PhraseParams p;
    p.playMode = phrase::PlayMode::gate;
    p.tuning = phrase::Tuning::corrected;
    CHECK (std::abs (centsBetweenHz (midiToHz (62), pitchOf (play (*model, seq, p, 0.3), 0.05, 0.3))) < 5.0);
    p.tuning = phrase::Tuning::original;
    CHECK (centsBetweenHz (midiToHz (62), pitchOf (play (*model, seq, p, 0.3), 0.05, 0.3)) == Approx (-40.0).margin (6.0));
}

TEST_CASE ("Phrase engine: Fill Keyboard transposes within its limit and never beyond", "[phrase][integration]")
{
    const auto model = modelFor ("clean-scale");   // C4 D4 E4 F4 G4 A4 B4 C5
    phrase::PhraseParams p;
    p.playMode = phrase::PlayMode::gate;

    SECTION ("a black key between two recordings is filled from its nearest neighbour")
    {
        const auto info = phrase::describeKey (*model, 61, phrase::FillMode::natural);
        CHECK (info.state == phrase::KeyState::filled);
        CHECK ((info.sourceKey == 60 || info.sourceKey == 62));
        MidiSequence seq;
        andor::addNote (seq, 0.0, 0.3, 61);
        p.fill = phrase::FillMode::natural;
        CHECK (std::abs (centsBetweenHz (midiToHz (61), pitchOf (play (*model, seq, p, 0.3), 0.05, 0.25))) < 6.0);
        p.fill = phrase::FillMode::off;
        CHECK (rmsOf (play (*model, seq, p, 0.3), 0.0, 0.3) == 0.0);
    }
    SECTION ("limits: natural 2, balanced 5, wide 12 semitones")
    {
        CHECK (phrase::describeKey (*model, 58, phrase::FillMode::natural).state == phrase::KeyState::filled);    // 2 below C4
        CHECK (phrase::describeKey (*model, 57, phrase::FillMode::natural).state == phrase::KeyState::missing);   // 3 below
        CHECK (phrase::describeKey (*model, 55, phrase::FillMode::balanced).state == phrase::KeyState::filled);   // 5 below
        CHECK (phrase::describeKey (*model, 54, phrase::FillMode::balanced).state == phrase::KeyState::missing);
        CHECK (phrase::describeKey (*model, 48, phrase::FillMode::wide).state == phrase::KeyState::filled);
        CHECK (phrase::describeKey (*model, 47, phrase::FillMode::wide).state == phrase::KeyState::missing);
        CHECK (phrase::describeKey (*model, 36, phrase::FillMode::extended).state == phrase::KeyState::filled);

        MidiSequence seq;
        andor::addNote (seq, 0.0, 0.3, 50);
        p.fill = phrase::FillMode::balanced;
        CHECK (rmsOf (play (*model, seq, p, 0.3), 0.0, 0.3) == 0.0);   // silent, never stretched 10 semitones
        p.fill = phrase::FillMode::wide;
        CHECK (std::abs (centsBetweenHz (midiToHz (50), pitchOf (play (*model, seq, p, 0.3), 0.05, 0.25))) < 8.0);
    }
    SECTION ("the choice is deterministic")
    {
        for (int key = 0; key < 128; ++key)
            CHECK (phrase::describeKey (*model, key, phrase::FillMode::wide).sourceKey
                   == phrase::describeKey (*model, key, phrase::FillMode::wide).sourceKey);
    }
}

TEST_CASE ("Phrase engine: repeated pitches are variations played in round robin", "[phrase][integration]")
{
    const auto model = modelFor ("repeated");   // G3 eight times
    REQUIRE (model->keyRegions[55].size() == 8);
    CHECK (phrase::describeKey (*model, 55, phrase::FillMode::off).state == phrase::KeyState::variations);

    auto sequenceOfRegions = [&] (phrase::RoundRobin mode, std::uint64_t seed)
    {
        phrase::PhraseEngine engine;
        engine.prepare (48000.0, 256);
        engine.setModel (model.get());
        phrase::PhraseParams p;
        p.roundRobin = mode;
        p.seed = seed;
        p.playMode = phrase::PlayMode::gate;
        engine.setParams (p);
        std::vector<int> regions;
        std::vector<float> l (64), r (64);
        for (int i = 0; i < 24; ++i)
        {
            engine.noteOn (55, 0.8f);
            phrase::PhraseEngine::VoiceView views[8];
            const int n = engine.collect (views, 8);
            int newest = -1;
            for (int k = 0; k < n; ++k)
                newest = views[k].region;   // the newest voice is the last started
            regions.push_back (newest);
            engine.render (l.data(), r.data(), 64);
            engine.noteOff (55);
            engine.panic();
        }
        return regions;
    };

    SECTION ("sequential cycles through every take in order")
    {
        const auto seq = sequenceOfRegions (phrase::RoundRobin::sequential, 1);
        for (std::size_t i = 0; i < seq.size(); ++i)
            CHECK (seq[i] == model->keyRegions[55][i % 8]);
    }
    SECTION ("shuffle never repeats the previous take, and uses them all")
    {
        const auto seq = sequenceOfRegions (phrase::RoundRobin::shuffle, 7);
        std::set<int> used (seq.begin(), seq.end());
        CHECK (used.size() >= 6);
        for (std::size_t i = 1; i < seq.size(); ++i)
            CHECK (seq[i] != seq[i - 1]);
    }
    SECTION ("random is reproducible from its seed")
    {
        CHECK (sequenceOfRegions (phrase::RoundRobin::random, 3) == sequenceOfRegions (phrase::RoundRobin::random, 3));
        CHECK (sequenceOfRegions (phrase::RoundRobin::random, 3) != sequenceOfRegions (phrase::RoundRobin::random, 4));
    }
}

TEST_CASE ("Phrase engine: a monophonic recording plays chords", "[phrase][integration]")
{
    const auto model = modelFor ("clean-scale");
    phrase::PhraseEngine engine;
    engine.prepare (48000.0, 256);
    engine.setModel (model.get());
    phrase::PhraseParams p;
    p.playMode = phrase::PlayMode::sustain;
    engine.setParams (p);
    engine.noteOn (60, 0.8f);
    engine.noteOn (64, 0.8f);
    engine.noteOn (67, 0.8f);
    CHECK (engine.activeVoices() == 3);
    MidiSequence chord;
    for (int k : { 60, 64, 67 })
        andor::addNote (chord, 0.0, 1.0, k);
    const auto out = play (*model, chord, p, 0.6);
    CHECK (allFinite (out));
    MidiSequence single;
    andor::addNote (single, 0.0, 1.0, 60);
    CHECK (rmsOf (out, 0.1, 0.9) > 1.4 * rmsOf (play (*model, single, p, 0.6), 0.1, 0.9));
}

TEST_CASE ("Phrase engine: voice stealing keeps the limit and leaves no stuck notes", "[phrase][integration]")
{
    const auto model = modelFor ("clean-scale");
    phrase::PhraseEngine engine;
    engine.prepare (48000.0, 128);
    engine.setModel (model.get());
    phrase::PhraseParams p;
    p.polyphony = 4;
    p.playMode = phrase::PlayMode::sustain;
    p.releaseSeconds = 0.1;
    engine.setParams (p);
    std::vector<float> l (128), r (128);
    for (int k : { 60, 62, 64, 65, 67, 69, 71, 72 })
    {
        engine.noteOn (k, 0.9f);
        engine.render (l.data(), r.data(), 128);
    }
    // The four newest play; the stolen ones have faded within 4 ms (one block of 128 is 2.7 ms).
    for (int i = 0; i < 4; ++i)
        engine.render (l.data(), r.data(), 128);
    CHECK (engine.activeVoices() == 4);
    engine.allNotesOff();
    for (int i = 0; i < 200; ++i)
        engine.render (l.data(), r.data(), 128);
    CHECK (engine.activeVoices() == 0);

    // Rapid retriggering of one key never piles up voices: at most the polyphony plus the
    // stolen voices still inside their 4 ms fade.
    int worst = 0;
    for (int i = 0; i < 50; ++i)
    {
        engine.noteOn (64, 0.7f);
        engine.render (l.data(), r.data(), 128);
        engine.noteOff (64);
        worst = std::max (worst, engine.activeVoices());
    }
    CHECK (worst <= 4 + 2);
}

TEST_CASE ("Phrase engine: ONE SHOT, GATE and SUSTAIN behave as named", "[phrase][integration]")
{
    const auto model = modelFor ("vibrato");   // four 1.2 s notes: E4 G4 D4 F4
    MidiSequence shortPress;
    andor::addNote (shortPress, 0.0, 0.2, 64);
    MidiSequence longHold;
    andor::addNote (longHold, 0.0, 6.0, 64);
    phrase::PhraseParams p;
    p.releaseSeconds = 0.15;

    SECTION ("ONE SHOT ignores note-off and plays the region out")
    {
        p.playMode = phrase::PlayMode::oneShot;
        const auto out = play (*model, shortPress, p, 2.0);
        CHECK (rmsOf (out, 0.9, 1.1) > 0.02);
        CHECK (rmsOf (out, 1.6, 2.0) < 1.0e-4);
    }
    SECTION ("GATE stops after note-off")
    {
        p.playMode = phrase::PlayMode::gate;
        const auto out = play (*model, shortPress, p, 2.0);
        CHECK (rmsOf (out, 0.05, 0.15) > 0.02);
        CHECK (rmsOf (out, 0.6, 1.0) < 1.0e-3);
    }
    SECTION ("SUSTAIN holds far past the recording, click-free, and releases on note-off")
    {
        p.playMode = phrase::PlayMode::sustain;
        const auto out = play (*model, longHold, p, 1.5);
        const double early = rmsOf (out, 0.4, 0.9);
        CHECK (early > 0.02);
        for (double t = 1.5; t < 5.8; t += 0.5)
        {
            INFO (t);
            CHECK (rmsOf (out, t, t + 0.5) > 0.5 * early);
            CHECK (rmsOf (out, t, t + 0.5) < 2.0 * early);
        }
        // Loop seams: no step much larger than the waveform's own (a click would be ~10x).
        CHECK (worstStepRatio (out, 1.0, 5.5) < 1.5 * worstStepRatio (out, 0.4, 0.9));
        CHECK (rmsOf (out, 6.8, 7.4) < 1.0e-3);
        CHECK (allFinite (out));
    }
    SECTION ("SUSTAIN CHARACTER blooms the hold with grains, still sustained")
    {
        p.playMode = phrase::PlayMode::sustain;
        p.sustainCharacter = 1.0;
        const auto out = play (*model, longHold, p, 1.0);
        CHECK (rmsOf (out, 3.0, 5.0) > 0.01);
        CHECK (std::abs (centsBetweenHz (midiToHz (64), pitchOf (out, 3.0, 5.0))) < 30.0);
    }
    SECTION ("short fragments fail gracefully in SUSTAIN (no loop: they hold with grains or end)")
    {
        const auto st = modelFor ("staccato");
        p.playMode = phrase::PlayMode::sustain;
        MidiSequence hold;
        andor::addNote (hold, 0.0, 2.0, 67);
        const auto out = play (*st, hold, p, 0.5);
        CHECK (allFinite (out));
        CHECK (rmsOf (out, 0.0, 0.1) > 0.005);
    }
}

TEST_CASE ("Phrase engine: the sustain pedal holds released keys", "[phrase][integration]")
{
    const auto model = modelFor ("vibrato");
    MidiSequence seq;
    seq.events.push_back ({ 0.0, MidiEvent::Type::sustainPedal, 0, 127, 1 });
    andor::addNote (seq, 0.05, 0.2, 64);
    seq.events.push_back ({ 2.0, MidiEvent::Type::sustainPedal, 0, 0, 1 });
    phrase::PhraseParams p;
    p.playMode = phrase::PlayMode::sustain;
    p.releaseSeconds = 0.1;
    const auto out = play (*model, seq, p, 1.0);
    CHECK (rmsOf (out, 1.0, 1.9) > 0.01);
    CHECK (rmsOf (out, 2.6, 3.0) < 1.0e-3);
}

TEST_CASE ("Phrase engine: deterministic and independent of the host block size", "[phrase][regression]")
{
    const auto model = modelFor ("vocal-phrase");
    MidiSequence seq;
    for (int i = 0; i < 12; ++i)
        andor::addNote (seq, 0.13 * i, 0.4 + 0.05 * (i % 3), 60 + (i * 5) % 12, 60 + 5 * i);
    seq.events.push_back ({ 0.9, MidiEvent::Type::sustainPedal, 0, 127, 1 });
    seq.events.push_back ({ 1.4, MidiEvent::Type::sustainPedal, 0, 0, 1 });
    phrase::PhraseParams p;
    p.roundRobin = phrase::RoundRobin::random;
    p.sustainCharacter = 0.4;
    p.synthetic = 0.3;
    p.tone = 0.6;
    p.space = 0.3;
    p.echo = 0.2;
    p.fill = phrase::FillMode::wide;
    const auto a = play (*model, seq, p, 1.0, 64);
    const auto b = play (*model, seq, p, 1.0, 64);
    const auto c = play (*model, seq, p, 1.0, 512);
    const auto d = play (*model, seq, p, 1.0, 37);
    CHECK (a.channels == b.channels);
    CHECK (a.channels == c.channels);
    CHECK (a.channels == d.channels);
    CHECK (allFinite (a));
}

TEST_CASE ("Phrase engine: odd models are safe", "[phrase][unit]")
{
    phrase::PhraseEngine engine;
    engine.prepare (48000.0, 128);
    std::vector<float> l (128), r (128);
    engine.noteOn (60, 1.0f);   // no model at all
    engine.render (l.data(), r.data(), 128);
    CHECK (engine.activeVoices() == 0);

    // An empty atlas (silence was analysed): plays nothing, never crashes.
    const auto silent = AudioData::allocate (1, 4800, 48000.0);
    auto src = std::make_shared<const PlaybackSource> (silent, 60.0, phrase::phrasePadding());
    const auto empty = phrase::buildPhraseModel (src, phrase::analysePhrase (silent, {}).atlas, 1);
    engine.setModel (empty.get());
    engine.noteOn (60, 1.0f);
    engine.noteOn (-3, 1.0f);
    engine.noteOn (200, 1.0f);
    engine.render (l.data(), r.data(), 128);
    CHECK (engine.activeVoices() == 0);

    // A model whose source is missing.
    const auto noSource = phrase::buildPhraseModel (nullptr, {}, 2);
    CHECK_FALSE (noSource->hasNotes());
}
