// Recorded round robins with LOOP off (docs/research/note-atlas-refinements.md, Task D): the
// recorded take is chosen at note-on, whatever LOOP is; LOOP decides only whether it sustains.
// The take actually played is read from the voice (its member), never inferred from the output.
#include "engine/AtlasSet.h"
#include "engine/InstrumentEngine.h"
#include "research/AtlasFixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>
#include <vector>

using namespace osp;
using namespace osp::research;

namespace
{
    std::shared_ptr<const InstrumentSet> setOf (const char* name)
    {
        auto r = atlasfixtures::build (atlasfixtures::make (name));
        INFO (name << ": " << (r.report.errors.empty() ? std::string() : r.report.errors.front()));
        REQUIRE (r.set != nullptr);
        return r.set;
    }

    EngineSettings dry (bool loop)
    {
        EngineSettings s;
        s.macros.life = 0.0;
        s.macros.motion = 0.0;
        s.macros.space = 0.0;
        s.macros.reimagined = 0.0;
        s.layer[0].loop = loop;
        return s;
    }

    struct Rig
    {
        InstrumentEngine engine;
        std::vector<float> l = std::vector<float> (256), r = std::vector<float> (256);
        Rig (const InstrumentSet& set, bool loop)
        {
            engine.prepare (48000.0, 256, dry (loop));
            engine.setInstrumentSet (&set);
        }
        void run (double seconds)
        {
            for (int b = 0; b < static_cast<int> (seconds * 48000.0 / 256); ++b)
            {
                float* ch[2] = { l.data(), r.data() };
                engine.render (ch, 2, 256);
            }
        }
        /** The member the newest held voice plays (-1: none). */
        int heldMember (const InstrumentSet& set) const
        {
            for (int i = 0; i < InstrumentEngine::voiceSlots(); ++i)
            {
                const auto& v = engine.voiceAt (i);
                if (! v.isActive() || v.isFading() || v.isReleased())
                    continue;
                for (std::size_t m = 0; m < set.members.size(); ++m)
                    if (set.members[m].model.get() == v.model())
                        return static_cast<int> (m);
            }
            return -1;
        }
        /** `presses` short presses of `key`: the member each one played. */
        std::vector<int> presses (const InstrumentSet& set, int key, int count)
        {
            std::vector<int> out;
            for (int i = 0; i < count; ++i)
            {
                engine.noteOn (key, 100);
                run (0.03);
                out.push_back (heldMember (set));
                engine.noteOff (key);
                run (0.05);
            }
            return out;
        }
    };

    std::set<int> membersAtKey (const InstrumentSet& set, int rootMidi)
    {
        std::set<int> out;
        for (std::size_t m = 0; m < set.members.size(); ++m)
            if (set.members[m].model != nullptr && static_cast<int> (std::lround (set.members[m].model->rootMidi)) == rootMidi)
                out.insert (static_cast<int> (m));
        return out;
    }
} // namespace

TEST_CASE ("Atlas round robin: eight recorded takes of C2 all play with LOOP off, never the same twice in a row", "[integration][atlas][round-robin]")
{
    const auto set = setOf ("repeated-bass");
    const auto takes = membersAtKey (*set, 36);
    REQUIRE (takes.size() == 8);
    for (const bool loop : { false, true })
    {
        INFO ("LOOP " << (loop ? "on" : "off"));
        Rig rig (*set, loop);
        const auto played = rig.presses (*set, 36, 24);
        REQUIRE (played.size() == 24);
        for (const int m : played)
            CHECK (takes.count (m) == 1);
        for (std::size_t i = 1; i < played.size(); ++i)
            CHECK (played[i] != played[i - 1]);
        // With LOOP off every take is a candidate (none is excluded for not looping).
        if (! loop)
            CHECK (std::set<int> (played.begin(), played.end()) == takes);
        // Deterministic: the same presses play the same takes.
        Rig again (*set, loop);
        CHECK (again.presses (*set, 36, 24) == played);
    }
}

TEST_CASE ("Atlas round robin: short pizzicato takes too short to loop still play in turn with LOOP off", "[integration][atlas][round-robin]")
{
    const auto set = setOf ("pizzicato");
    const auto c3 = membersAtKey (*set, 48);
    REQUIRE (c3.size() == 3);
    Rig rig (*set, false);
    const auto played = rig.presses (*set, 48, 12);
    for (const int m : played)
        CHECK (c3.count (m) == 1);
    CHECK (std::set<int> (played.begin(), played.end()) == c3);
    for (std::size_t i = 1; i < played.size(); ++i)
        CHECK (played[i] != played[i - 1]);
    // G3's two takes alternate.
    const auto g3 = membersAtKey (*set, 55);
    REQUIRE (g3.size() == 2);
    const auto g = rig.presses (*set, 55, 6);
    CHECK (std::set<int> (g.begin(), g.end()) == g3);
}

TEST_CASE ("Atlas round robin: a filled key keeps its group's takes; a take never changes while its voice sounds", "[integration][atlas][round-robin]")
{
    const auto set = setOf ("repeated-bass");
    const auto takes = membersAtKey (*set, 36);
    Rig rig (*set, false);
    // C#2 has no recording: FILL plays C2's takes, in turn.
    const auto filled = rig.presses (*set, 37, 16);
    for (const int m : filled)
        CHECK (takes.count (m) == 1);
    CHECK (std::set<int> (filled.begin(), filled.end()).size() >= 6);
    for (std::size_t i = 1; i < filled.size(); ++i)
        CHECK (filled[i] != filled[i - 1]);
    // Held: the member stays the same block after block, whatever else is played.
    rig.engine.noteOn (36, 100);
    rig.run (0.02);
    const int held = rig.heldMember (*set);
    REQUIRE (held >= 0);
    for (int b = 0; b < 10; ++b)
    {
        rig.run (0.02);
        bool found = false;
        for (int i = 0; i < InstrumentEngine::voiceSlots(); ++i)
        {
            const auto& v = rig.engine.voiceAt (i);
            if (v.isActive() && ! v.isReleased() && v.note() == 36)
            {
                CHECK (v.model() == set->members[static_cast<std::size_t> (held)].model.get());
                found = true;
            }
        }
        if (! found)
            break;   // (the pluck ended: nothing left to check)
    }
}
