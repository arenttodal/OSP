// Custom ARP Patterns, CP1 lab (src/research/ArpPatternLab): contour extraction and the rank
// mappings. The lab is a prototype; these pin the spec's worked examples and edge cases so the
// listening renders rest on exact behaviour.

#include "research/ArpPatternLab.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace osp::research::arplab;

namespace
{
    std::vector<CapturedNote> sixteenths (std::initializer_list<int> pitches)
    {
        std::vector<CapturedNote> r;
        double t = 0.0;
        for (const int p : pitches)
        {
            r.push_back ({ t, 0.2, p, 100 });
            t += 0.25;
        }
        return r;
    }
}

TEST_CASE ("arp lab: ranks keep order, repeats and anchors (the spec's C3 G3 E3 C3 A3 G3 C3)", "[unit][arp][arp-lab]")
{
    const auto c = extractContour (sixteenths ({ 48, 55, 52, 48, 57, 55, 48 }));
    CHECK (c.pitches == std::vector<int> { 48, 52, 55, 57 });
    CHECK (c.ranks == std::vector<int> { 0, 2, 1, 0, 3, 2, 0 });
    CHECK (c.maxRank == 3);
    CHECK (c.chordAttacks == 0);
}

TEST_CASE ("arp lab: a chord attack is one step, by policy; legato overlaps stay separate steps", "[unit][arp][arp-lab]")
{
    std::vector<CapturedNote> notes { { 0.0, 0.2, 60, 100 }, { 0.25, 0.2, 55, 90 }, { 0.26, 0.2, 64, 90 }, { 0.255, 0.2, 59, 90 }, { 0.5, 0.2, 67, 100 } };
    const auto high = extractContour (notes, ChordPolicy::highest);
    REQUIRE (high.ranks.size() == 3);
    CHECK (high.pitches == std::vector<int> { 60, 64, 67 });
    CHECK (high.chordAttacks == 1);
    CHECK (high.notesDropped == 2);
    const auto low = extractContour (notes, ChordPolicy::lowest);
    CHECK (low.pitches == std::vector<int> { 55, 60, 67 });
    CHECK (low.ranks == std::vector<int> { 1, 0, 2 });

    // Each note starts before the last one ends: still one step each.
    std::vector<CapturedNote> legato { { 0.0, 0.6, 60, 100 }, { 0.25, 0.6, 64, 100 }, { 0.5, 0.6, 62, 100 } };
    CHECK (extractContour (legato).ranks == std::vector<int> { 0, 2, 1 });
}

TEST_CASE ("arp lab: the mappings - zero denominators, empty pools, clamping, exact rounding", "[unit][arp][arp-lab]")
{
    // Empty pool: nothing.
    for (const auto m : { Mapping::adaptiveContour, Mapping::degreeCycle, Mapping::contourFit })
        CHECK (mapRank (m, 2, 3, 0) == -1);
    // One captured pitch (R = 0): the lowest note.
    CHECK (mapRank (Mapping::adaptiveContour, 0, 0, 5) == 0);
    CHECK (mapRank (Mapping::contourFit, 0, 0, 5) == 0);
    // One held note: every rank on it.
    for (int r = 0; r <= 3; ++r)
        CHECK (mapRank (Mapping::adaptiveContour, r, 3, 1) == 0);
    // round (r / R * (N - 1)): R = 3, N = 3 -> 0, 1 (0.67 up), 1 (1.33 down), 2.
    CHECK (mapRank (Mapping::adaptiveContour, 0, 3, 3) == 0);
    CHECK (mapRank (Mapping::adaptiveContour, 1, 3, 3) == 1);
    CHECK (mapRank (Mapping::adaptiveContour, 2, 3, 3) == 1);
    CHECK (mapRank (Mapping::adaptiveContour, 3, 3, 3) == 2);
    // Half rounds up: R = 2, N = 2 -> rank 1 is 0.5 -> 1.
    CHECK (mapRank (Mapping::adaptiveContour, 1, 2, 2) == 1);
    // Degree cycle wraps.
    CHECK (mapRank (Mapping::degreeCycle, 4, 4, 3) == 1);
    // Contour fit: the ranks themselves while the pool is large enough, else adaptive.
    CHECK (mapRank (Mapping::contourFit, 1, 1, 12) == 1);
    CHECK (mapRank (Mapping::adaptiveContour, 1, 1, 12) == 11);
    CHECK (mapRank (Mapping::contourFit, 3, 3, 3) == mapRank (Mapping::adaptiveContour, 3, 3, 3));
}

TEST_CASE ("arp lab: the pool is the arpeggiator's (ascending, octave copies, nothing above 127)", "[unit][arp][arp-lab]")
{
    CHECK (notePool ({ 67, 60, 64 }, 2) == std::vector<int> { 60, 64, 67, 72, 76, 79 });
    CHECK (notePool ({ 120 }, 4) == std::vector<int> { 120 });
    CHECK (notePool ({}, 3).empty());
}

TEST_CASE ("arp lab: repeated captured notes stay repeated and the length is the captured length", "[unit][arp][arp-lab]")
{
    for (const auto& g : referenceGestures())
    {
        const auto c = extractContour (g.notes);
        for (const auto m : { Mapping::adaptiveContour, Mapping::degreeCycle, Mapping::contourFit })
            for (const auto& held : { std::vector<int> { 60 }, std::vector<int> { 60, 64, 67 }, std::vector<int> { 50, 53, 57, 60, 64, 67 } })
                for (int octaves = 1; octaves <= 4; ++octaves)
                {
                    INFO (g.name << " " << mappingName (m) << " octaves " << octaves);
                    const auto pool = notePool (held, octaves);
                    const auto played = interpret (c, pool, m);
                    REQUIRE (played.size() == c.ranks.size());
                    for (const int n : played)
                        CHECK (std::find (pool.begin(), pool.end(), n) != pool.end());   // never outside the held notes and their octaves
                    CHECK (measure (c, played).repeatsKept);
                }
    }
}

TEST_CASE ("arp lab rhythm: SPLIT, MERGE, EXTEND, REST and LENGTH keep or change the length only as stated", "[unit][arp][arp-lab]")
{
    RhythmPattern p;
    p.maxRank = 3;
    p.events = { { 3, false, ticksPerStep, 0 }, { 1, false, ticksPerStep, 0 }, { 0, true, ticksPerStep, 0 } };
    const int length = p.lengthTicks();

    // SPLIT: two halves, same rank, same total; recursively down to the shortest event.
    REQUIRE (edit::split (p, 0));
    CHECK (p.events.size() == 4);
    CHECK (p.events[0] == PatternEvent { 3, false, ticksPerStep / 2, 0 });
    CHECK (p.events[1] == PatternEvent { 3, false, ticksPerStep / 2, 0 });
    CHECK (p.lengthTicks() == length);
    {
        auto q = p;
        int splits = 0;
        while (edit::split (q, 0))
            ++splits;
        CHECK (q.events[0].duration >= minEventTicks);
        CHECK (q.events[0].duration / 2 < minEventTicks);
        CHECK (splits == 4);   // 480 -> 240 -> 120 -> 60 -> 30
        CHECK (q.lengthTicks() == length);
    }
    // A third of a step splits to sixths, then twelfths, ...: exact integers throughout.
    {
        RhythmPattern t;
        t.events = { { 0, false, ticksPerStep / 3, 0 } };
        CHECK (edit::split (t, 0));
        CHECK (t.events[0].duration == 160);
    }

    // MERGE: same rank, contiguous: one event of both durations.
    REQUIRE (edit::merge (p, 0));
    CHECK (p.events[0] == PatternEvent { 3, false, ticksPerStep, 0 });
    // Different ranks: refused, nothing changed.
    const auto before = p;
    CHECK_FALSE (edit::merge (p, 0));
    CHECK (p == before);

    // EXTEND: into the rest after, never over a note.
    CHECK_FALSE (edit::extend (p, 0, ticksPerStep / 2));        // followed by a note
    REQUIRE (edit::extend (p, 1, ticksPerStep / 2));            // half the rest
    CHECK (p.events[1].duration == ticksPerStep * 3 / 2);
    CHECK (p.events[2] == PatternEvent { 0, true, ticksPerStep / 2, 0 });
    REQUIRE (edit::extend (p, 1, ticksPerStep / 2));            // the rest of it
    CHECK (p.events.size() == 2);
    CHECK (p.lengthTicks() == length);

    // REST keeps the time; a rest becomes a note of a chosen rank; delete = rest.
    REQUIRE (edit::toRest (p, 0));
    CHECK (p.events[0].rest);
    CHECK (p.events[0].duration == ticksPerStep);
    REQUIRE (edit::toNote (p, 0, 2));
    CHECK ((! p.events[0].rest && p.events[0].rank == 2));
    REQUIRE (edit::remove (p, 1));
    CHECK (p.events[1].rest);
    CHECK (p.lengthTicks() == length);

    // LENGTH: longer adds silence; shorter only cuts trailing silence, never a note.
    REQUIRE (edit::setLength (p, length + ticksPerStep));
    CHECK (p.lengthTicks() == length + ticksPerStep);
    REQUIRE (edit::setLength (p, ticksPerStep * 2));
    CHECK (p.lengthTicks() == ticksPerStep * 2);
    CHECK_FALSE (edit::setLength (p, ticksPerStep / 2));        // would cut the note
    CHECK (p.lengthTicks() == ticksPerStep * 2);
}

TEST_CASE ("arp lab rhythm: an empty cycle, reverse, mirror and rotate", "[unit][arp][arp-lab]")
{
    RhythmPattern empty;
    empty.events = { { 0, true, ticksPerStep * 4, 0 } };
    CHECK (empty.valid());
    CHECK (empty.notes() == 0);
    CHECK (rhythmSequence (empty, listeningProgression(), Mapping::contourFit).events.empty());

    RhythmPattern p;
    p.maxRank = 2;
    p.events = { { 0, false, 480, 0 }, { 2, false, 960, 0 }, { 0, true, 480, 0 }, { 1, false, 960, 0 } };
    auto r = p;
    edit::reverse (r);
    CHECK (r.events.front() == p.events.back());
    CHECK (r.lengthTicks() == p.lengthTicks());
    auto m = p;
    edit::mirror (m);
    CHECK (m.events[0].rank == 2);
    CHECK (m.events[1].rank == 0);
    CHECK (m.events[2].rest);
    auto t = p;
    edit::rotate (t, 1);
    CHECK (t.events[0] == p.events[1]);
    edit::rotate (t, -1);
    CHECK (t == p);
}

TEST_CASE ("arp lab rhythm: Stage B keeps rests and the loop's trailing silence; RATE scales, SWING follows the nominal grid", "[unit][arp][arp-lab]")
{
    // D . F A on a 1/16 grid (quarter = 4 steps), loop of one quarter plus a silent quarter.
    const std::vector<CapturedNote> notes { { 0.0, 0.2, 62, 100 }, { 0.5, 0.2, 65, 100 }, { 0.75, 0.2, 69, 100 } };
    const auto p = stageB (notes, 0.25, ticksPerStep / 4, 2.0);
    REQUIRE (p.events.size() == 5);
    CHECK (p.events[0] == PatternEvent { 0, false, ticksPerStep, 0 });       // D: its captured sixteenth (0.2 rounded up)
    CHECK (p.events[1] == PatternEvent { 0, true, ticksPerStep, 0 });        // the empty step kept
    CHECK (p.events[2] == PatternEvent { 1, false, ticksPerStep, 0 });       // F to A (no silence between)
    CHECK (p.events[3] == PatternEvent { 2, false, ticksPerStep, 0 });
    CHECK (p.events[4] == PatternEvent { 0, true, 4 * ticksPerStep, 0 });    // the loop's trailing silence
    CHECK (p.lengthTicks() == 8 * ticksPerStep);
    // Staccato sixteenths with small gaps stay notes (articulation is GATE's), no rests.
    const std::vector<CapturedNote> staccato { { 0.0, 0.1, 60, 100 }, { 0.25, 0.1, 62, 100 }, { 0.5, 0.1, 64, 100 } };
    const auto s = stageB (staccato, 0.25, ticksPerStep / 4, 0.75);
    CHECK (s.events.size() == 3);
    CHECK (s.notes() == 3);

    // SWING 0: identity. SWING 1: odd steps start half a step late; a subdivision inside an
    // odd step keeps its relative place between the swung step's start and the next step.
    CHECK (swungTick (960 + 480, 0.0) == 1440.0);
    CHECK (swungTick (960, 1.0) == 1440.0);
    CHECK (swungTick (1920, 1.0) == 1920.0);
    CHECK (swungTick (960 + 480, 1.0) == 1680.0);   // halfway between 1440 and 1920
    CHECK (swungTick (480, 1.0) == 720.0);          // halfway between 0 and 1440
}

TEST_CASE ("arp lab: fit-octave-cycles keeps the shape in the held notes and climbs an octave per cycle", "[unit][arp][arp-lab]")
{
    const auto c = extractContour (sixteenths ({ 64, 64, 71, 64 }));   // ranks 0 0 1 0
    const auto line = playLine (c, { 60, 64, 67 }, 3, Mapping::fitOctaveCycles);
    CHECK (line == std::vector<int> { 60, 60, 64, 60, 72, 72, 76, 72, 84, 84, 88, 84 });
    // Near the top the copies fold back below 128.
    for (const int n : playLine (c, { 120, 124 }, 4, Mapping::fitOctaveCycles))
        CHECK (n <= 127);
    // Other mappings: one pattern length over the octave pool.
    CHECK (playLine (c, { 60, 64, 67 }, 3, Mapping::adaptiveContour).size() == 4);
}
