// Custom ARP Patterns, CP2 (docs/research/custom-arp): the approved mapping (CONTOUR = the
// hybrid, DEGREES = degree-cycle) and variable-duration events inside the one arpeggiator. The
// engine must play exactly what the CP1 lab specified (its rhythmSequence is the reference), keep
// the 18 styles untouched, and keep the scheduler's safety rules: no note past its gate, no
// waiting note after a release, a panic or the pattern switching off, no allocation.

#include "engine/Arpeggiator.h"
#include "core/Prng.h"
#include "research/ArpPatternLab.h"
#include "research/RenderConfig.h"
#include "support/AllocationCounter.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace osp;
namespace lab = osp::research::arplab;

namespace
{
    using Kind = Arpeggiator::Event::Kind;

    struct Ev
    {
        std::int64_t time = 0;
        Kind kind = Kind::noteOn;
        int note = 0, velocity = 0;
        bool operator== (const Ev& o) const { return time == o.time && kind == o.kind && note == o.note && velocity == o.velocity; }
    };

    struct In
    {
        std::int64_t time = 0;
        enum class T { on, off, pedalDown, pedalUp, allOff } type = T::on;
        int note = 60, velocity = 100;
    };

    struct Run
    {
        double sampleRate = 48000.0;
        int block = 256;
        bool host = false;
        double bpm = 120.0;
        Arpeggiator::Settings settings;
        std::vector<In> inputs;
        std::int64_t length = 48000;
        const arp::CustomPattern* pattern = nullptr;
        std::function<void (Arpeggiator&, Arpeggiator::Settings&, std::int64_t now)> perBlock;
        int maxPending = 0;
        int allocations = 0;
        bool countAllocations = false;

        std::vector<Ev> render()
        {
            Arpeggiator arp;
            arp.setSeed (7);
            arp.prepare (sampleRate);
            if (pattern != nullptr)
                arp.setCustomPattern (*pattern);
            auto s = settings;
            auto in = inputs;
            std::stable_sort (in.begin(), in.end(), [] (const In& a, const In& b) { return a.time < b.time; });
            std::vector<Ev> out;
            out.reserve (4096);
            std::size_t next = 0;
            double ppq = 0.0;
            for (std::int64_t now = 0; now < length;)
            {
                const int n = static_cast<int> (std::min<std::int64_t> (block, length - now));
                if (perBlock)
                    perBlock (arp, s, now);
                HostTiming t;
                t.valid = t.playing = host;
                t.bpm = bpm;
                t.ppq = ppq;
                {
                    std::optional<test::AllocationScope> scope;
                    if (countAllocations)
                        scope.emplace();
                    arp.beginBlock (s, t, n);
                    while (next < in.size() && in[next].time < now + n)
                    {
                        const auto& e = in[next++];
                        const int off = static_cast<int> (std::max<std::int64_t> (0, e.time - now));
                        switch (e.type)
                        {
                            case In::T::on: arp.noteOn (off, e.note, e.velocity, 1); break;
                            case In::T::off: arp.noteOff (off, e.note, 1); break;
                            case In::T::pedalDown: arp.sustainPedal (off, true); break;
                            case In::T::pedalUp: arp.sustainPedal (off, false); break;
                            case In::T::allOff: arp.allNotesOff (off); break;
                        }
                    }
                    arp.endBlock();
                    if (scope)
                        allocations += scope->count();
                }
                maxPending = std::max (maxPending, arp.pendingStrumCount());
                for (int i = 0; i < arp.numEvents(); ++i)
                {
                    const auto& e = arp.event (i);
                    REQUIRE (e.offset >= 0);
                    REQUIRE (e.offset < n);
                    if (e.kind == Kind::noteOn || e.kind == Kind::noteOff)
                        out.push_back ({ now + e.offset, e.kind, e.note, e.velocity });
                }
                if (host)
                    ppq += n * bpm / (60.0 * sampleRate);
                now += n;
            }
            return out;
        }
    };

    std::vector<In> chord (std::int64_t at, std::initializer_list<int> notes, int velocity = 100)
    {
        std::vector<In> r;
        for (const int n : notes)
            r.push_back ({ at, In::T::on, n, velocity });
        return r;
    }

    Arpeggiator::Settings customSettings (double gate = 0.75, ArpRate rate = ArpRate::sixteenth)
    {
        Arpeggiator::Settings s;
        s.enabled = true;
        s.custom = true;
        s.rate = rate;
        s.gate = gate;
        return s;
    }

    arp::CustomPattern fromLab (const lab::RhythmPattern& p, arp::CustomMapping mapping = arp::CustomMapping::contour)
    {
        arp::CustomPattern c;
        c.maxRank = p.maxRank;
        c.mapping = mapping;
        for (const auto& e : p.events)
            c.events[static_cast<std::size_t> (c.count++)] = { static_cast<std::int16_t> (e.rank), e.rest, static_cast<std::uint8_t> (e.velocity), e.duration };
        return c;
    }

    arp::CustomPattern pattern (std::initializer_list<std::pair<int, int>> rankAndTicks, int maxRank, arp::CustomMapping mapping = arp::CustomMapping::contour)
    {
        arp::CustomPattern c;
        c.maxRank = maxRank;
        c.mapping = mapping;
        for (const auto& [rank, ticks] : rankAndTicks)
            c.events[static_cast<std::size_t> (c.count++)] = { static_cast<std::int16_t> (std::max (0, rank)), rank < 0, 0, ticks };
        return c;
    }

    std::vector<Ev> onsOf (const std::vector<Ev>& events)
    {
        std::vector<Ev> r;
        for (const auto& e : events)
            if (e.kind == Kind::noteOn)
                r.push_back (e);
        return r;
    }

    constexpr int T = arp::ticksPerStep;
}

TEST_CASE ("custom arp: the engine's CONTOUR and DEGREES pick the lab's notes for every chord, OCTAVES, rank and cycle", "[unit][arp-custom]")
{
    Prng prng (Prng::deriveSeed (0xC0FFEE, 2, 3));
    int checked = 0;
    for (int trial = 0; trial < 400; ++trial)
    {
        // Random chords: 1..7 notes anywhere (duplicates, wide voicings and the MIDI top included).
        std::vector<int> held;
        const int size = 1 + static_cast<int> (prng.nextBelow (7));
        const int low = static_cast<int> (prng.nextBelow (120));
        for (int i = 0; i < size; ++i)
            held.push_back (std::min (127, low + static_cast<int> (prng.nextBelow (trial % 3 == 0 ? 40 : 14))));
        auto sorted = held;
        std::sort (sorted.begin(), sorted.end());
        std::vector<arp::PoolNote> pool;
        for (const int n : sorted)
            pool.push_back ({ static_cast<std::uint8_t> (n), 100, 1 });
        for (int octaves = 1; octaves <= 4; ++octaves)
            for (int maxRank = 0; maxRank <= 9; ++maxRank)
            {
                arp::CustomPool plan;
                arp::planCustomPool (pool.data(), static_cast<int> (pool.size()), octaves, maxRank, plan);
                for (int rank = 0; rank <= maxRank; ++rank)
                    for (long long cycle = 0; cycle < 6; ++cycle)
                    {
                        CHECK (arp::customNote (plan, arp::CustomMapping::contour, rank, maxRank, cycle).note
                               == lab::noteFor (lab::Mapping::hybrid, rank, maxRank, held, octaves, cycle));
                        CHECK (arp::customNote (plan, arp::CustomMapping::degrees, rank, maxRank, cycle).note
                               == lab::noteFor (lab::Mapping::degreeCycle, rank, maxRank, held, octaves, cycle));
                        ++checked;
                    }
            }
    }
    CHECK (checked > 10000);
}

TEST_CASE ("custom arp: the engine plays the lab's rhythmic patterns to the sample (onsets, pitches, gates, swing)", "[unit][arp-custom]")
{
    // The reference gestures as Stage B patterns (captured onsets on a 1/16 grid) and as their
    // edited forms; over a triad (4 octaves: CONTOUR climbs) and a seventh (2 octaves).
    std::vector<std::pair<std::string, lab::RhythmPattern>> patterns;
    for (const auto& g : lab::referenceGestures())
    {
        patterns.push_back ({ g.name + " A", lab::stageA (lab::extractContour (g.notes)) });
        patterns.push_back ({ g.name + " B", lab::stageB (g.notes, 0.25, arp::minEventTicks, g.lengthQuarters) });
    }
    {
        auto p = lab::stageA (lab::extractContour (lab::referenceGestures()[0].notes));
        lab::edit::split (p, 1);
        lab::edit::split (p, 1);
        lab::edit::toRest (p, 3);
        patterns.push_back ({ "edited", p });
    }
    struct Chord { std::vector<int> notes; int octaves; };
    const std::vector<Chord> chords { { { 60, 64, 67 }, 4 }, { { 57, 60, 64, 67 }, 2 }, { { 62 }, 3 } };
    int compared = 0;
    for (const auto& [name, rhythm] : patterns)
    {
        if (! rhythm.valid() || rhythm.notes() == 0)
            continue;
        for (const auto mapping : { lab::Mapping::hybrid, lab::Mapping::degreeCycle })
            for (const auto& c : chords)
                for (const double swing : { 0.0, 0.4 })
                {
                    INFO (name << " " << lab::mappingName (mapping) << " swing " << swing << " notes " << c.notes.size());
                    const double bpm = 120.0, sr = 48000.0;
                    const auto reference = lab::rhythmSequence (rhythm, { { "chord", c.notes, c.octaves, 2 } }, mapping, bpm, 0.75, swing);

                    Run run;
                    run.sampleRate = sr;
                    run.settings = customSettings();
                    run.settings.octaves = c.octaves;
                    run.settings.swing = swing;
                    const auto custom = fromLab (rhythm, mapping == lab::Mapping::hybrid ? arp::CustomMapping::contour : arp::CustomMapping::degrees);
                    REQUIRE (custom.valid());
                    run.pattern = &custom;
                    for (const int n : c.notes)
                        run.inputs.push_back ({ 0, In::T::on, n, 100 });
                    const auto twoBars = static_cast<std::int64_t> (2 * 4 * 60.0 / bpm * sr);   // 2 bars of 4/4
                    run.length = twoBars;
                    const auto played = run.render();

                    // Note-ons: the same pitches at the same samples (the lab's exact time, rounded).
                    std::vector<std::pair<std::int64_t, int>> want, got;
                    for (const auto& e : reference.events)
                    {
                        const auto at = static_cast<std::int64_t> (std::ceil (e.timeSeconds * sr - 1.0e-6));
                        if (at >= twoBars)
                            continue;
                        if (e.type == MidiEvent::Type::noteOn)
                            want.push_back ({ at, e.note });
                    }
                    for (const auto& e : played)
                        if (e.kind == Kind::noteOn)
                            got.push_back ({ e.time, e.note });
                    REQUIRE (got.size() == want.size());
                    for (std::size_t i = 0; i < want.size(); ++i)
                    {
                        CHECK (got[i].second == want[i].second);
                        CHECK (std::llabs (got[i].first - want[i].first) <= 1);
                    }
                    ++compared;
                }
    }
    CHECK (compared > 50);
}

TEST_CASE ("custom arp: CUSTOM off, or on without a pattern, plays the selected style exactly", "[unit][arp-custom]")
{
    for (int style = 0; style < arp::patternCount; ++style)
    {
        INFO ("style " << arp::patternName (static_cast<ArpPattern> (style)));
        Run plain;
        plain.settings.enabled = true;
        plain.settings.pattern = static_cast<ArpPattern> (style);
        plain.settings.octaves = 2;
        plain.inputs = chord (100, { 60, 64, 67, 71 });
        const auto reference = plain.render();

        Run onWithout = plain;
        onWithout.settings.custom = true;   // no pattern loaded
        CHECK (onWithout.render() == reference);

        const auto p = pattern ({ { 0, T }, { 2, T / 2 }, { 1, T / 2 } }, 2);
        Run offWith = plain;
        offWith.pattern = &p;               // loaded, CUSTOM off
        CHECK (offWith.render() == reference);
    }
}

TEST_CASE ("custom arp: rests play nothing, split events retrigger, merged events sustain, RATE scales the whole pattern", "[unit][arp-custom]")
{
    const double sr = 48000.0;
    const double step16 = 0.25 * 60.0 / 120.0 * sr;   // 6000 samples
    SECTION ("a rest advances time without an attack (and without repeating the last note)")
    {
        const auto p = pattern ({ { 0, T }, { -1, T }, { 1, T }, { -1, T } }, 1);
        Run run;
        run.settings = customSettings();
        run.pattern = &p;
        run.inputs = chord (0, { 60, 64 });
        run.length = static_cast<std::int64_t> (8 * step16);
        const auto ons = onsOf (run.render());
        REQUIRE (ons.size() == 4);   // two cycles of two notes
        CHECK (ons[0].time == 0);
        CHECK (ons[1].time == static_cast<std::int64_t> (2 * step16));
        CHECK (ons[2].time == static_cast<std::int64_t> (4 * step16));
        CHECK (ons[0].note == 60);
        CHECK (ons[1].note == 64);
    }
    SECTION ("a split note retriggers at each half; a merged note triggers once and lasts its whole duration at GATE")
    {
        const auto split = pattern ({ { 0, T / 2 }, { 0, T / 2 }, { 1, T } }, 1);
        const auto merged = pattern ({ { 0, T * 2 } }, 1);
        Run a;
        a.settings = customSettings (0.75);
        a.pattern = &split;
        a.inputs = chord (0, { 60, 64 });
        a.length = static_cast<std::int64_t> (2 * step16);
        const auto ea = a.render();
        const auto onsA = onsOf (ea);
        REQUIRE (onsA.size() == 3);
        CHECK (onsA[0].note == 60);
        CHECK (onsA[1].note == 60);
        CHECK (onsA[1].time == static_cast<std::int64_t> (step16 / 2));

        Run b;
        b.settings = customSettings (0.75);
        b.pattern = &merged;
        b.inputs = chord (0, { 60, 64 });
        b.length = static_cast<std::int64_t> (2 * step16);
        const auto eb = b.render();
        REQUIRE (onsOf (eb).size() == 1);   // no retrigger at the inner boundary
        const auto off = std::find_if (eb.begin(), eb.end(), [] (const Ev& e) { return e.kind == Kind::noteOff; });
        REQUIRE (off != eb.end());
        CHECK (off->time == static_cast<std::int64_t> (std::llround (0.75 * 2 * step16)));
    }
    SECTION ("ARP RATE 1/8 doubles every onset (the relations kept, nothing quantised to one value)")
    {
        const auto p = pattern ({ { 0, T }, { 1, T / 2 }, { 2, T / 2 }, { 1, 2 * T } }, 2);
        auto onsetsAt = [&] (ArpRate rate) {
            Run run;
            run.settings = customSettings (0.5, rate);
            run.pattern = &p;
            run.inputs = chord (0, { 60, 64, 67 });
            run.length = static_cast<std::int64_t> (16 * step16);
            std::vector<std::int64_t> t;
            for (const auto& e : onsOf (run.render()))
                t.push_back (e.time);
            return t;
        };
        const auto sixteenth = onsetsAt (ArpRate::sixteenth);
        const auto eighth = onsetsAt (ArpRate::eighth);
        REQUIRE (sixteenth.size() >= 4);
        REQUIRE (eighth.size() >= 4);
        for (std::size_t i = 0; i < 4; ++i)
            CHECK (eighth[i] == 2 * sixteenth[i]);
        CHECK (sixteenth[1] - sixteenth[0] == static_cast<std::int64_t> (step16));
        CHECK (sixteenth[2] - sixteenth[1] == static_cast<std::int64_t> (step16 / 2));
    }
}

TEST_CASE ("custom arp: an odd-length pattern stays in phase; CONTOUR climbs an octave per cycle and comes home", "[unit][arp-custom]")
{
    // 5.5 steps: two notes, a dotted one and a rest; its cycle start drifts against the step grid.
    const auto p = pattern ({ { 0, T }, { 1, T + T / 2 }, { -1, 3 * T } }, 1);
    Run run;
    run.settings = customSettings();
    run.settings.octaves = 4;
    run.pattern = &p;
    run.inputs = chord (0, { 60, 64, 67 });
    const double step16 = 6000.0;
    run.length = static_cast<std::int64_t> (5.5 * 8 * step16);   // eight cycles
    const auto ons = onsOf (run.render());
    REQUIRE (ons.size() == 16);
    for (std::size_t c = 0; c < 8; ++c)
    {
        INFO ("cycle " << c);
        const auto start = static_cast<std::int64_t> (std::ceil (c * 5.5 * step16 - 1.0e-6));
        CHECK (std::llabs (ons[2 * c].time - start) <= 1);
        // C E, an octave higher per cycle over the four positions, then home.
        CHECK (ons[2 * c].note == 60 + 12 * static_cast<int> (c % 4));
        CHECK (ons[2 * c + 1].note == 64 + 12 * static_cast<int> (c % 4));
    }
}

TEST_CASE ("custom arp: block size and sample rate change nothing; no allocation; at most 32 notes wait in a step", "[unit][arp-custom]")
{
    // Thirty-seconds of a step (the minimum) alternating with longer notes: the busiest pattern.
    arp::CustomPattern busy;
    busy.maxRank = 3;
    for (int i = 0; i < 64; ++i)
        busy.events[static_cast<std::size_t> (busy.count++)] = { static_cast<std::int16_t> (i % 4), i % 7 == 3, 0, i < 32 ? arp::minEventTicks : T / 2 };
    REQUIRE (busy.valid());
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::vector<Ev> reference;
        for (const int block : { 1024, 7, 64, 333, 1 })
        {
            INFO ("sr " << sr << " block " << block);
            Run run;
            run.sampleRate = sr;
            run.block = block;
            run.settings = customSettings (1.2, ArpRate::eighth);
            run.settings.swing = 0.3;
            run.pattern = &busy;
            run.inputs = chord (17, { 48, 55, 60, 64 });
            run.inputs.push_back ({ static_cast<std::int64_t> (sr * 0.7), In::T::off, 55, 0 });
            run.length = static_cast<std::int64_t> (sr * 1.5);
            run.countAllocations = block == 1024;
            const auto out = run.render();
            if (block == 1024)
            {
                CHECK (run.allocations == 0);
                reference = out;
                CHECK (onsOf (out).size() == 36);   // 27 thirty-seconds (5 are rests) in the first step, then half steps
            }
            else
                CHECK (out == reference);
            CHECK (run.maxPending <= arp::ticksPerStep / arp::minEventTicks);
            // Every note-on has its note-off; nothing hangs.
            std::map<int, int> open;
            for (const auto& e : out)
                open[e.note] += e.kind == Kind::noteOn ? 1 : -1;
            for (const auto& [note, n] : open)
                CHECK (n >= 0);
        }
    }
}

TEST_CASE ("custom arp: a release drops a key's waiting notes; All Notes Off and CUSTOM off drop them all", "[unit][arp-custom]")
{
    const double step = 0.5 * 60.0 / 120.0 * 48000.0;   // 1/8: 12000 samples
    // Each step: a note on the beat, then three subdivisions waiting inside the step.
    const auto p = pattern ({ { 0, T / 4 }, { 1, T / 4 }, { 2, T / 4 }, { 1, T / 4 } }, 2, arp::CustomMapping::degrees);
    auto base = [&] {
        Run run;
        run.settings = customSettings (0.4, ArpRate::eighth);
        run.pattern = &p;
        run.inputs = chord (0, { 60, 64, 67 });
        run.length = static_cast<std::int64_t> (4 * step);
        return run;
    };
    SECTION ("releasing E drops its waiting note in this step; the others still play")
    {
        auto run = base();
        run.inputs.push_back ({ static_cast<std::int64_t> (step + 100), In::T::off, 64, 0 });
        const auto ons = onsOf (run.render());
        // In the second step (from 12000): C on the beat (before the release), E dropped, G at
        // 1/2, and the last subdivision's rank 1 maps to G now (E is no longer held: the pool is C G).
        std::vector<int> second;
        for (const auto& e : ons)
            if (e.time >= static_cast<std::int64_t> (step) && e.time < static_cast<std::int64_t> (2 * step))
                second.push_back (e.note);
        CHECK (std::find (second.begin(), second.end(), 64) == second.end());
        CHECK (std::find (second.begin(), second.end(), 67) != second.end());
    }
    SECTION ("All Notes Off: nothing waiting plays, nothing hangs")
    {
        auto run = base();
        run.inputs.push_back ({ static_cast<std::int64_t> (step + 100), In::T::allOff, 0, 0 });
        const auto out = run.render();
        for (const auto& e : out)
            if (e.kind == Kind::noteOn)
                CHECK (e.time <= static_cast<std::int64_t> (step + 100));
        std::map<int, int> open;
        for (const auto& e : out)
            open[e.note] += e.kind == Kind::noteOn ? 1 : -1;
        for (const auto& [note, n] : open)
            CHECK (n == 0);
    }
    SECTION ("CUSTOM switched off mid-step: its waiting notes never play")
    {
        auto run = base();
        run.block = 1000;
        run.perBlock = [&] (Arpeggiator&, Arpeggiator::Settings& s, std::int64_t now) {
            if (now >= static_cast<std::int64_t> (step + 1000))
                s.custom = false;
        };
        const auto ons = onsOf (run.render());
        for (const auto& e : ons)
            if (e.time > static_cast<std::int64_t> (step + 1000) && e.time < static_cast<std::int64_t> (2 * step))
                FAIL ("a custom subdivision played after CUSTOM went off");
    }
}

TEST_CASE ("custom arp: a new pattern takes over at the next step with the phase kept; STRUM does not apply to it", "[unit][arp-custom]")
{
    const double step = 6000.0;
    const auto first = pattern ({ { 0, T } }, 0);
    const auto second = pattern ({ { 1, T } }, 1);
    Run run;
    run.settings = customSettings();
    run.settings.strum = true;
    run.settings.pattern = ArpPattern::chord;   // STRUM would apply to CHORD, never to CUSTOM
    run.settings.strumMs = 50.0;
    run.pattern = &first;
    run.inputs = chord (0, { 60, 64 });
    run.block = 500;
    run.length = static_cast<std::int64_t> (6 * step);
    run.perBlock = [&] (Arpeggiator& a, Arpeggiator::Settings&, std::int64_t now) {
        if (now == static_cast<std::int64_t> (2 * step + 1000))
        {
            a.setCustomPattern (second);
            CHECK (a.customPattern() == first);   // not before the next step
        }
    };
    const auto ons = onsOf (run.render());
    REQUIRE (ons.size() == 6);
    for (std::size_t i = 0; i < 6; ++i)
    {
        CHECK (ons[i].time == static_cast<std::int64_t> (i * step));   // one note per step: never strummed
        CHECK (ons[i].note == (i < 3 ? 60 : 64));                      // the new pattern from step 3
    }
}

TEST_CASE ("custom arp: the display shows the custom pattern's notes; a copied arpeggiator keeps its pattern", "[unit][arp-custom]")
{
    const auto p = pattern ({ { 0, T }, { 2, T }, { 1, T } }, 2, arp::CustomMapping::degrees);
    Arpeggiator a;
    a.prepare (48000.0);
    a.setCustomPattern (p);
    auto s = customSettings();
    HostTiming t;
    a.beginBlock (s, t, 256);
    a.noteOn (0, 60, 100, 1);
    a.noteOn (0, 64, 100, 1);
    a.noteOn (0, 67, 100, 1);
    a.endBlock();
    Arpeggiator::Display d;
    a.display (d);
    CHECK (d.active);
    CHECK (d.low[0] == 60);
    CHECK (d.low[1] == 67);
    CHECK (d.low[2] == 64);
    CHECK (d.low[3] == 60);
    Arpeggiator copy = a;
    CHECK (copy.hasCustomPattern());
    CHECK (copy.customPattern() == p);
}

TEST_CASE ("custom arp: invalid patterns are refused (capacity, durations, ranks, empty)", "[unit][arp-custom]")
{
    arp::CustomPattern p;
    CHECK_FALSE (p.valid());                                   // no events
    p = pattern ({ { -1, T } }, 0);
    CHECK_FALSE (p.valid());                                   // only rests
    p = pattern ({ { 0, arp::minEventTicks - 1 } }, 0);
    CHECK_FALSE (p.valid());                                   // below the shortest event
    p = pattern ({ { 3, T } }, 2);
    CHECK_FALSE (p.valid());                                   // a rank above R
    p = pattern ({ { 0, arp::maxPatternSteps * T } }, 0);
    CHECK (p.valid());                                         // 128 steps: allowed
    p.events[0].duration += 1;
    CHECK_FALSE (p.valid());                                   // longer: refused
    Arpeggiator a;
    a.prepare (48000.0);
    a.setCustomPattern (p);
    CHECK_FALSE (a.hasCustomPattern());
}

TEST_CASE ("custom arp: the research configs carry their patterns into the renderer's arpeggiator", "[unit][arp-custom]")
{
    auto load = [] (const char* name) {
        std::string error;
        auto config = research::loadRenderConfig (std::filesystem::path (OSP_SOURCE_DIR) / "research" / "configs" / name, error);
        INFO (name << ": " << error);
        REQUIRE (config.has_value());
        return *config;
    };
    for (const char* name : { "custom-five-step-contour.json", "custom-five-step-degrees.json", "custom-repeated-subdivisions.json",
                              "custom-sixteenth-thirtysecond.json", "custom-sustained-rests.json", "custom-odd-split.json" })
    {
        INFO (name);
        const auto c = load (name);
        CHECK (c.arp.settings.custom);
        CHECK (c.arp.customPattern.valid());
    }
    const auto five = load ("custom-five-step-contour.json");
    CHECK (five.arp.customPattern.count == 5);
    CHECK (five.arp.customPattern.maxRank == 4);
    CHECK (five.arp.customPattern.mapping == arp::CustomMapping::contour);
    CHECK (load ("custom-five-step-degrees.json").arp.customPattern.mapping == arp::CustomMapping::degrees);
    const auto odd = load ("custom-odd-split.json");
    CHECK (odd.arp.customPattern.lengthTicks() == 5 * arp::ticksPerStep + arp::ticksPerStep / 2);   // 5.5 steps, the rest included
    CHECK (odd.arp.customPattern.events[1].duration == arp::ticksPerStep / 2);
    CHECK_FALSE (load ("custom-off-reference.json").arp.settings.custom);
}
