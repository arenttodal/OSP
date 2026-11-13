// Automatic multi-sample mapping (docs/research/multisample-mapping): several one-shot
// recordings dropped together become one instrument - every MIDI key 0-127 plays the nearest
// recorded root (ties: the lower), takes of one pitch are real round robins, octaves stay
// apart. Fixtures: synthetic vowels at known pitches, each take its own seed (and so its own
// recording), told apart by file name through InstrumentEngine::memberFor.

#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "engine/SampleSetInference.h"
#include "support/TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>
#include <string>
#include <vector>

using namespace osp;

namespace
{
    struct Take
    {
        AudioData audio;
        AnalysisData analysis;
        std::string name;
    };

    Take take (int midi, const std::string& name, std::uint64_t seed, double amplitude = 0.5, double seconds = 1.2)
    {
        Take t;
        t.audio = testsignals::vowel (midiToHz (midi), seconds, 48000.0, seed, amplitude);
        t.analysis = test::analyse (t.audio);
        t.name = name;
        return t;
    }

    /** Files imported together (the drop and the chooser): automatic mapping. */
    InstrumentSet build (std::vector<Take>& takes, const SetInferenceOptions& policy = SetInferenceOptions::automatic())
    {
        std::vector<instrument::SetSource> inputs;
        for (auto& t : takes)
            inputs.push_back ({ &t.audio, &t.analysis, t.name });
        return instrument::buildSet (inputs, {}, {}, false, policy);
    }

    /** Plays `notes` one after another (each started and released) and returns the member names. */
    std::vector<std::string> play (InstrumentEngine& engine, const InstrumentSet& set, const std::vector<int>& notes, int velocity = 100)
    {
        std::vector<std::string> names;
        std::vector<float> l (256), r (256);
        float* ch[2] = { l.data(), r.data() };
        for (const int note : notes)
        {
            const int index = engine.memberFor (note, velocity, engine.noteOnCount());
            names.push_back (index >= 0 ? set.members[static_cast<std::size_t> (index)].filename : std::string ("-"));
            engine.noteOn (note, velocity);
            engine.render (ch, 2, 256);
            engine.noteOff (note);
            engine.render (ch, 2, 256);
        }
        return names;
    }

    double rootOf (const InstrumentSet& set, int note)
    {
        const int g = set.groupFor (note);
        return g >= 0 ? set.groups[static_cast<std::size_t> (g)].rootMidi : -1.0;
    }
}

TEST_CASE ("multisample: one recording plays on every key 0-127", "[unit][multisample]")
{
    std::vector<Take> takes;
    takes.push_back (take (60, "Pizz_C4.wav", 1));
    const auto set = build (takes);
    REQUIRE (set.isValid());
    REQUIRE (set.groups.size() == 1);
    for (int note = 0; note < 128; ++note)
        CHECK (set.groupFor (note) == 0);
}

TEST_CASE ("multisample: several pitches - nearest root, ties to the lower, the edges reach 0 and 127", "[unit][multisample]")
{
    std::vector<Take> takes;
    for (const auto& [midi, name] : std::vector<std::pair<int, std::string>> { { 48, "Pizz_C3.wav" }, { 55, "Pizz_G3.wav" }, { 62, "Pizz_D4.wav" }, { 69, "Pizz_A4.wav" } })
        takes.push_back (take (midi, name, static_cast<std::uint64_t> (midi)));
    const auto set = build (takes);
    REQUIRE (set.groups.size() == 4);
    CHECK (std::abs (set.groups[0].rootMidi - 48.0) < 0.5);
    CHECK (std::abs (set.groups[3].rootMidi - 69.0) < 0.5);
    for (int note = 0; note < 128; ++note)
    {
        // The expected root: the nearest of 48 55 62 69, a tie to the lower one.
        int expected = 48;
        for (const int r : { 55, 62, 69 })
            if (std::abs (r - note) < std::abs (expected - note))
                expected = r;
        INFO ("note " << note);
        CHECK (std::abs (rootOf (set, note) - expected) < 0.5);
    }
    // A tie between C4 and D4 (C#4 is a semitone from each): the lower.
    std::vector<Take> pair;
    pair.push_back (take (60, "C4.wav", 3));
    pair.push_back (take (62, "D4.wav", 4));
    const auto tie = build (pair);
    CHECK (std::abs (rootOf (tie, 61) - 60.0) < 0.5);
}

TEST_CASE ("multisample: C3, C4 and C5 are three roots, never one pitch class", "[unit][multisample]")
{
    std::vector<Take> takes;
    takes.push_back (take (48, "C3.wav", 1));
    takes.push_back (take (60, "C4.wav", 2));
    takes.push_back (take (72, "C5.wav", 3));
    const auto set = build (takes);
    REQUIRE (set.groups.size() == 3);
    InstrumentEngine engine;
    engine.prepare (48000.0, 256, {});
    engine.setInstrumentSet (&set);
    const auto names = play (engine, set, { 48, 60, 72, 0, 127, 53, 55 });
    CHECK (names == std::vector<std::string> { "C3.wav", "C4.wav", "C5.wav", "C3.wav", "C5.wav", "C3.wav", "C4.wav" });
}

TEST_CASE ("multisample: same-pitch takes are real round robins that cycle in a fixed order", "[unit][multisample][round-robin]")
{
    std::vector<Take> takes;
    takes.push_back (take (60, "Pizz_C4_01.wav", 11));
    takes.push_back (take (60, "Pizz_C4_02.wav", 12));
    takes.push_back (take (60, "Pizz_C4_03.wav", 13));
    const auto set = build (takes);
    REQUIRE (set.groups.size() == 1);
    for (const auto& m : set.members)
        CHECK (m.role == SampleRole::roundRobin);
    InstrumentEngine engine;
    engine.prepare (48000.0, 256, {});
    engine.setInstrumentSet (&set);
    const auto names = play (engine, set, std::vector<int> (9, 60));
    // A strict cycle in file order from the first take: 01 02 03 01 02 03 ...
    for (std::size_t i = 0; i < names.size(); ++i)
        CHECK (names[i] == "Pizz_C4_0" + std::to_string (i % 3 + 1) + ".wav");
    // A newly loaded set starts its cycle again at the first take.
    const auto again = build (takes);
    engine.setInstrumentSet (&again);
    CHECK (play (engine, again, { 60 }) == std::vector<std::string> { "Pizz_C4_01.wav" });
}

TEST_CASE ("multisample: each root keeps its own cycle; a filled key cycles its root's takes", "[unit][multisample][round-robin]")
{
    std::vector<Take> takes;
    takes.push_back (take (60, "Pizz_C4_01.wav", 21));
    takes.push_back (take (60, "Pizz_C4_02.wav", 22));
    takes.push_back (take (60, "Pizz_C4_03.wav", 23));
    takes.push_back (take (67, "Pizz_G4_01.wav", 24));
    takes.push_back (take (67, "Pizz_G4_02.wav", 25));
    const auto set = build (takes);
    REQUIRE (set.groups.size() == 2);
    InstrumentEngine plain, mixed;
    for (auto* e : { &plain, &mixed })
    {
        e->prepare (48000.0, 256, {});
        e->setInstrumentSet (&set);
    }
    // C4 alone, and C4 with G4 notes between: the C4 sequence is the same (G4 does not advance it).
    const auto alone = play (plain, set, { 60, 60, 60, 60, 60, 60 });
    std::vector<std::string> interleaved;
    for (const auto& n : play (mixed, set, { 60, 67, 60, 67, 60, 67, 60, 60, 67, 60 }))
        if (n.find ("C4") != std::string::npos)
            interleaved.push_back (n);
    CHECK (alone == interleaved);
    // D4 (62) is nearer C4: it plays C4's takes, all three, not only the first (E4, 64, is
    // nearer G4 and plays G4's).
    InstrumentEngine filled;
    filled.prepare (48000.0, 256, {});
    filled.setInstrumentSet (&set);
    const auto d4 = play (filled, set, std::vector<int> (6, 62));
    CHECK (std::set<std::string> (d4.begin(), d4.end()) == std::set<std::string> { "Pizz_C4_01.wav", "Pizz_C4_02.wav", "Pizz_C4_03.wav" });
    const auto e4 = play (filled, set, std::vector<int> (4, 64));
    CHECK (std::set<std::string> (e4.begin(), e4.end()) == std::set<std::string> { "Pizz_G4_01.wav", "Pizz_G4_02.wav" });
    // Beyond the roots: below C4 its takes, above G4 G4's.
    const auto low = play (filled, set, { 0, 12, 30 }), high = play (filled, set, { 90, 110, 127 });
    for (const auto& n : low)
        CHECK (n.find ("C4") != std::string::npos);
    for (const auto& n : high)
        CHECK (n.find ("G4") != std::string::npos);
}

TEST_CASE ("multisample: eight takes of one pitch - one root, eight round robins, the whole keyboard", "[unit][multisample][round-robin]")
{
    std::vector<Take> takes;
    for (int i = 0; i < 8; ++i)
        takes.push_back (take (60, "Voice_C4_0" + std::to_string (i + 1) + ".wav", static_cast<std::uint64_t> (40 + i)));
    const auto set = build (takes);
    REQUIRE (set.groups.size() == 1);
    CHECK (set.groups[0].layers == 1);
    CHECK (set.groupFor (0) == 0);
    CHECK (set.groupFor (127) == 0);
    InstrumentEngine engine;
    engine.prepare (48000.0, 256, {});
    engine.setInstrumentSet (&set);
    const auto names = play (engine, set, std::vector<int> (16, 72));
    CHECK (std::set<std::string> (names.begin(), names.end()).size() == 8);
}

TEST_CASE ("multisample: dynamics words keep velocity layers; unlabeled louder and softer takes are round robins", "[unit][multisample][velocity]")
{
    {
        std::vector<Take> named;
        named.push_back (take (60, "C4_pp.wav", 51, 0.08));
        named.push_back (take (60, "C4_mf.wav", 52, 0.25));
        named.push_back (take (60, "C4_ff.wav", 53, 0.6));
        const auto set = build (named);
        REQUIRE (set.groups.size() == 1);
        CHECK (set.groups[0].layers == 3);
    }
    {
        // Three performances of one note, about 10 dB apart, no dynamics words: takes.
        std::vector<Take> plain;
        plain.push_back (take (60, "Pizz_C4_01.wav", 61, 0.6));
        plain.push_back (take (60, "Pizz_C4_02.wav", 62, 0.2));
        plain.push_back (take (60, "Pizz_C4_03.wav", 63, 0.07));
        const auto set = build (plain);
        REQUIRE (set.groups.size() == 1);
        CHECK (set.groups[0].layers == 1);
        for (const auto& m : set.members)
            CHECK (m.role == SampleRole::roundRobin);
    }
}

TEST_CASE ("multisample: plucked (pizzicato-like) recordings are rooted at their own pitch", "[unit][multisample][root]")
{
    // A sharp attack and a decaying pitched body: the root comes from the body.
    for (const int midi : { 43, 48, 55, 60, 62, 69, 76 })
    {
        AudioData audio = testsignals::pluck (midiToHz (midi), 1.2, 48000.0, static_cast<std::uint64_t> (midi));
        const auto analysis = test::analyse (audio);
        const auto model = instrument::buildComplete (audio, analysis, {}, false);
        REQUIRE (model != nullptr);
        INFO ("midi " << midi << " root " << model->rootMidi);
        CHECK (std::abs (model->rootMidi - midi) < 0.5);
    }
}

TEST_CASE ("multisample: a set saved before automatic mapping keeps the original rules", "[unit][multisample][legacy]")
{
    // The same three unlabeled performances under the original policy (what a session without a
    // stored policy restores): the loudness gaps still make velocity layers, and the takes of one
    // layer still alternate by the seeded never-repeat pick.
    std::vector<Take> plain;
    plain.push_back (take (60, "Pizz_C4_01.wav", 61, 0.6));
    plain.push_back (take (60, "Pizz_C4_02.wav", 62, 0.2));
    plain.push_back (take (60, "Pizz_C4_03.wav", 63, 0.07));
    const auto legacy = build (plain, {});
    CHECK_FALSE (legacy.cycleTakes);
    CHECK (legacy.loudnessLayers);
    REQUIRE (legacy.groups.size() == 1);
    CHECK (legacy.groups[0].layers == 3);
    const auto automatic = build (plain);
    CHECK (automatic.cycleTakes);
    CHECK_FALSE (automatic.loudnessLayers);
    // Takes of the new policy are numbered in file order.
    for (const auto& m : automatic.members)
        CHECK (m.take == std::stoi (m.filename.substr (8, 2)) - 1);

    // The legacy pick is unchanged: the same seed gives the same sequence as the random
    // never-repeat rule (two equal takes, so the pick must alternate).
    std::vector<Take> two;
    two.push_back (take (60, "A.wav", 71));
    two.push_back (take (60, "B.wav", 72));
    const auto pair = build (two, {});
    REQUIRE (pair.groups[0].layers == 1);
    InstrumentEngine engine;
    engine.prepare (48000.0, 256, {});
    engine.setInstrumentSet (&pair);
    const auto names = play (engine, pair, std::vector<int> (6, 60));
    for (std::size_t i = 1; i < names.size(); ++i)
        CHECK (names[i] != names[i - 1]);
}
