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

TEST_CASE ("sample set: velocity layers teach the dynamics between them", "[unit][sampleset]")
{
    // A soft take (quieter, darker) and a hard take (louder, brighter) of the same note.
    auto take = [] (double amplitude, double darkness, const std::string& name) {
        Source s;
        s.audio = testsignals::saw (midiToHz (55), 1.5, 48000.0, amplitude);
        float y = 0.0f;
        const auto a = static_cast<float> (darkness);
        for (auto& x : s.audio.channels[0])
            x = y = (1.0f - a) * x + a * y; // one-pole low-pass
        testsignals::applyFades (s.audio, 0.01, 0.2);
        s.analysis = test::analyse (s.audio);
        s.name = name;
        return s;
    };
    std::vector<Source> sources;
    sources.push_back (take (0.12, 0.9, "g3 soft.wav"));
    sources.push_back (take (0.5, 0.0, "g3 hard.wav"));
    const auto set = build (sources);
    REQUIRE (set.groups.size() == 1);
    REQUIRE (set.groups.front().layers == 2);
    REQUIRE (set.hasDynamicsModel);
    INFO ("step " << set.layerStepDb << " dB, " << set.layerStepBrightnessSt << " st, " << set.layerStepAttackMs << " ms");
    CHECK (set.layerStepDb > 6.0);
    CHECK (set.layerStepBrightnessSt > 3.0);

    // Either side of the layer boundary (velocity 63.5) the sound is nearly the same.
    auto play = [&] (int velocity) {
        InstrumentEngine engine;
        EngineSettings s;
        s.macros.life = 0.0;
        s.macros.space = 0.0;
        s.macros.reimagined = 0.0;
        s.macros.motion = 0.0;
        engine.prepare (48000.0, 256, s);
        engine.setInstrumentSet (&set);
        engine.noteOn (55, velocity);
        AudioData out = AudioData::allocate (2, 24000, 48000.0);
        for (int pos = 0; pos < 24000; pos += 256)
        {
            float* ch[2] = { out.channels[0].data() + pos, out.channels[1].data() + pos };
            engine.render (ch, 2, std::min (256, 24000 - pos));
        }
        return test::analyse (out);
    };
    const auto below = play (63), above = play (64), softest = play (10), hardest = play (127);
    const double jumpDb = above.envelope.maxRmsDbfs - below.envelope.maxRmsDbfs;
    const double jumpSt = 12.0 * std::log2 (above.spectral.meanCentroidHz / below.spectral.meanCentroidHz);
    const double rangeSt = 12.0 * std::log2 (hardest.spectral.meanCentroidHz / softest.spectral.meanCentroidHz);
    INFO ("boundary jump " << jumpDb << " dB, " << jumpSt << " st; full range " << rangeSt << " st");
    CHECK (std::abs (jumpDb) < 0.35 * set.layerStepDb);
    CHECK (std::abs (jumpSt) < 0.5 * set.layerStepBrightnessSt);
    CHECK (rangeSt > set.layerStepBrightnessSt); // velocity still spans more than the two recordings
}
