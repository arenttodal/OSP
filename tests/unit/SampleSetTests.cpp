#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "engine/SampleSetInference.h"
#include "support/TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace osp;

namespace
{
    struct Source
    {
        AudioData audio;
        AnalysisData analysis;
        std::string name;
    };

    Source make (int midi, double amplitude, const std::string& name, double seconds = 1.5, std::uint64_t seed = 1)
    {
        Source s;
        s.audio = testsignals::vowel (midiToHz (midi), seconds, 48000.0, seed, amplitude);
        s.analysis = test::analyse (s.audio);
        s.name = name;
        return s;
    }

    InstrumentSet build (std::vector<Source>& sources, const std::vector<SetAssignment>& assignments = {})
    {
        std::vector<instrument::SetSource> inputs;
        for (auto& s : sources)
            inputs.push_back ({ &s.audio, &s.analysis, s.name });
        return instrument::buildSet (inputs, {}, assignments, false);
    }

    const SetMember& byName (const InstrumentSet& set, const std::string& name)
    {
        for (const auto& m : set.members)
            if (m.filename == name)
                return m;
        FAIL ("no member " << name);
        return set.members.front();
    }
}

TEST_CASE ("sample set: dynamics words in file names", "[unit][sampleset]")
{
    CHECK (dynamicsRankFromName ("Violin_mf_C4.wav").value_or (-1) == 4.0);
    CHECK (dynamicsRankFromName ("pluck-FF-02.aif").value_or (-1) == 6.0);
    CHECK (dynamicsRankFromName ("take_p_3.wav").value_or (-1) == 2.0);
    CHECK (dynamicsRankFromName ("Soft hit.wav").value_or (-1) == 1.5);
    CHECK_FALSE (dynamicsRankFromName ("apple.wav").has_value());
    CHECK_FALSE (dynamicsRankFromName ("p.wav").has_value()); // a lone letter is not enough
}

TEST_CASE ("sample set: pitch groups, round robins and loudness layers are inferred", "[unit][sampleset]")
{
    std::vector<Source> sources;
    sources.push_back (make (57, 0.5, "a3.wav"));
    sources.push_back (make (60, 0.5, "c4 take1.wav", 1.5, 1));
    sources.push_back (make (60, 0.45, "c4 take2.wav", 1.5, 2));
    sources.push_back (make (60, 0.55, "c4 take3.wav", 1.5, 3));
    sources.push_back (make (64, 0.15, "e4 quiet.wav"));
    sources.push_back (make (64, 0.6, "e4 loud.wav"));
    const auto set = build (sources);

    REQUIRE (set.groups.size() == 3);
    CHECK (byName (set, "a3.wav").role == SampleRole::pitchAnchor);
    for (const auto* n : { "c4 take1.wav", "c4 take2.wav", "c4 take3.wav" })
        CHECK (byName (set, n).role == SampleRole::roundRobin);
    std::set<int> takes;
    for (const auto* n : { "c4 take1.wav", "c4 take2.wav", "c4 take3.wav" })
        takes.insert (byName (set, n).take);
    CHECK (takes.size() == 3);
    // "quiet"/"loud" are dynamics words: layers follow the names.
    CHECK (byName (set, "e4 quiet.wav").layer == 0);
    CHECK (byName (set, "e4 loud.wav").layer == 1);
    CHECK (set.groups[2].layers == 2);
    // One shared playback gain keeps the layers' natural level difference.
    CHECK (byName (set, "e4 quiet.wav").model->playback.gainDb == byName (set, "e4 loud.wav").model->playback.gainDb);
}

TEST_CASE ("sample set: unnamed takes split into layers only at a clear loudness gap", "[unit][sampleset]")
{
    std::vector<Source> sources;
    sources.push_back (make (62, 0.08, "x1.wav"));
    sources.push_back (make (62, 0.5, "x2.wav"));
    auto set = build (sources);
    REQUIRE (set.groups.size() == 1);
    CHECK (set.groups[0].layers == 2);
    CHECK (byName (set, "x1.wav").layer == 0);
    CHECK (byName (set, "x1.wav").confidence < 0.9); // inferred from loudness, not named

    // The user can say they are round robins after all.
    set = build (sources, { { "x1.wav", SampleRole::roundRobin, std::nullopt, 0 }, { "x2.wav", SampleRole::roundRobin, std::nullopt, 0 } });
    CHECK (byName (set, "x2.wav").layer == 0);
    CHECK (byName (set, "x2.wav").userAssigned);
}

TEST_CASE ("sample set: a much shorter take is an alternate articulation", "[unit][sampleset]")
{
    std::vector<Source> sources;
    // (0.6 s: shorter synthetic vowels are pitch-tracked on a harmonic, see STATUS known issues)
    sources.push_back (make (60, 0.5, "long1.wav", 2.4, 1));
    sources.push_back (make (60, 0.5, "long2.wav", 2.4, 2));
    sources.push_back (make (60, 0.5, "stab.wav", 0.6, 3));
    const auto set = build (sources);
    CHECK (byName (set, "stab.wav").role == SampleRole::articulation);
}

TEST_CASE ("sample set: the engine picks the nearest pitch, the velocity layer and never repeats a take", "[unit][sampleset]")
{
    std::vector<Source> sources;
    sources.push_back (make (48, 0.5, "c3.wav"));
    sources.push_back (make (60, 0.5, "c4 a.wav", 1.5, 1));
    sources.push_back (make (60, 0.5, "c4 b.wav", 1.5, 2));
    sources.push_back (make (60, 0.5, "c4 c.wav", 1.5, 3));
    sources.push_back (make (72, 0.12, "c5 pp.wav"));
    sources.push_back (make (72, 0.6, "c5 ff.wav"));
    const auto set = build (sources);
    InstrumentEngine engine;
    engine.prepare (48000.0, 256, {});
    engine.setInstrumentSet (&set);

    auto name = [&] (int index) { return set.members[static_cast<std::size_t> (index)].filename; };
    CHECK (name (engine.memberFor (50, 100, 0)) == "c3.wav");
    CHECK (name (engine.memberFor (73, 30, 0)) == "c5 pp.wav");
    CHECK (name (engine.memberFor (73, 120, 0)) == "c5 ff.wav");

    // Round robin through noteOn: consecutive C4 notes never reuse the previous take.
    std::string previous;
    std::vector<float> l (256), r (256);
    float* ch[2] = { l.data(), r.data() };
    for (int i = 0; i < 30; ++i)
    {
        const auto chosen = name (engine.memberFor (60, 100, engine.noteOnCount()));
        CHECK (chosen != previous);
        engine.noteOn (60, 100);
        engine.render (ch, 2, 256);
        engine.noteOff (60);
        engine.render (ch, 2, 256);
        previous = chosen;
    }
}
