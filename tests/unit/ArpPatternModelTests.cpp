// Custom ARP Patterns, CP3/CP4 model (src/model/ArpPatternModel, src/io/ArpPatternJson): the
// capture's interpretation and the editing operations agree with the CP1 lab; every edit keeps
// the pattern's length unless it says otherwise; nothing is truncated silently; undo and redo
// restore exact data; the pattern file round-trips and refuses what it cannot read; MIDI import
// keeps tracks, channels, tempo, PPQ, velocity-0 note-offs and overlapping notes.

#include "io/ArpPatternJson.h"
#include "model/ArpPatternModel.h"
#include "research/ArpPatternLab.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <filesystem>

using namespace osp;
namespace ap = osp::arppattern;
namespace lab = osp::research::arplab;
using Catch::Approx;

namespace
{
    std::vector<ap::CapturedNote> capture (const lab::Gesture& g)
    {
        std::vector<ap::CapturedNote> c;
        for (const auto& n : g.notes)
            c.push_back ({ n.onset, n.duration, n.pitch, n.velocity });
        return c;
    }

    ap::Pattern fromLab (const lab::RhythmPattern& p)
    {
        ap::Pattern r;
        r.maxRank = p.maxRank;
        for (const auto& e : p.events)
            r.events.push_back ({ e.rank, e.rest, e.duration, e.velocity });
        return r;
    }

    ap::Pattern simple (std::initializer_list<std::pair<int, int>> rankTicks, int maxRank)
    {
        ap::Pattern p;
        p.maxRank = maxRank;
        for (const auto& [r, t] : rankTicks)
            p.events.push_back ({ std::max (0, r), r < 0, t, 0 });
        return p;
    }

    struct TempFile
    {
        std::filesystem::path path;
        explicit TempFile (const std::string& name) : path (std::filesystem::temp_directory_path() / ("osp-arp-" + name)) {}
        ~TempFile() { std::error_code ec; std::filesystem::remove (path, ec); }
    };

    constexpr int T = arp::ticksPerStep;
}

TEST_CASE ("arp pattern model: a capture becomes the lab's pattern (rhythm and steps)", "[unit][arp-pattern]")
{
    for (const auto& g : lab::referenceGestures())
    {
        INFO (g.name);
        ap::Interpretation rhythm;
        rhythm.timing = ap::Interpretation::Timing::rhythm;
        rhythm.quantumTicks = arp::minEventTicks;
        rhythm.stepQuarters = 0.25;
        rhythm.lengthQuarters = g.lengthQuarters;
        ap::InterpretReport report;
        const auto a = ap::interpret (capture (g), rhythm, &report);
        const auto b = fromLab (lab::stageB (g.notes, 0.25, arp::minEventTicks, g.lengthQuarters));
        CHECK (a.events == b.events);
        CHECK (a.maxRank == b.maxRank);
        CHECK (report.capturedNotes == static_cast<int> (g.notes.size()));
        CHECK (report.placedNotes + report.chordNotesDropped + report.beyondLength + report.beyondCapacity == report.capturedNotes);

        ap::Interpretation steps;
        steps.timing = ap::Interpretation::Timing::steps;
        const auto s = ap::interpret (capture (g), steps);
        CHECK (s.events == fromLab (lab::stageA (lab::extractContour (g.notes))).events);
    }
}

TEST_CASE ("arp pattern model: the capture's origin is its first note; a chord's dropped notes and late notes are reported", "[unit][arp-pattern]")
{
    // Two bars late, a chord (C E G) then two notes, the last beyond a one-beat loop.
    std::vector<ap::CapturedNote> c { { 8.0, 0.2, 60, 90 }, { 8.0, 0.2, 64, 90 }, { 8.01, 0.2, 67, 90 }, { 8.5, 0.2, 62, 90 }, { 9.5, 0.2, 65, 90 } };
    ap::Interpretation how;
    how.lengthQuarters = 1.0;
    ap::InterpretReport report;
    const auto p = ap::interpret (c, how, &report);
    CHECK (report.chordAttacks == 1);
    CHECK (report.chordNotesDropped == 2);
    CHECK (report.beyondLength == 1);
    CHECK (report.placedNotes == 2);
    REQUIRE_FALSE (p.events.empty());
    CHECK (p.onsetOf (0) == 0);              // the origin: the chord, at 0
    CHECK (p.events[0].rank == 2);           // the chord's G (highest), among the chosen pitches 62 65 67
    CHECK (p.lengthTicks() == 4 * T);        // one beat of 1/16 steps
}

TEST_CASE ("arp pattern model: edits keep the length unless they say otherwise; undo and redo restore exact data", "[unit][arp-pattern]")
{
    auto p = simple ({ { 0, T }, { 1, T }, { -1, T }, { 2, T } }, 2);
    const int length = p.lengthTicks();
    ap::History h;
    h.reset (p);
    auto apply = [&] (bool ok) {
        CHECK (ok);
        h.push (p);
    };
    apply (ap::edit::split (p, 1));           // 0 | 1 1 | - | 2
    CHECK (p.lengthTicks() == length);
    CHECK (p.events[1].duration == T / 2);
    apply (ap::edit::merge (p, 1));           // back to 1
    CHECK (p.events[1].duration == T);
    apply (ap::edit::extend (p, 1, T / 2));   // 1 takes half the rest
    CHECK (p.events[1].duration == T + T / 2);
    CHECK (p.lengthTicks() == length);
    apply (ap::edit::toRest (p, 0));
    CHECK (p.events[0].rest);
    apply (ap::edit::setRank (p, 0, 5));      // a rest becomes a note; R grows
    CHECK (p.maxRank == 5);
    CHECK_FALSE (ap::edit::merge (p, 0));     // ranks 5 and 1: refused (nothing discarded)
    ap::edit::reverse (p);
    h.push (p);
    ap::edit::mirror (p);
    h.push (p);
    ap::edit::rotate (p, 1);
    h.push (p);
    CHECK (p.lengthTicks() == length);
    apply (ap::edit::insertStep (p, 0));      // explicit: + one step
    CHECK (p.lengthTicks() == length + T);
    apply (ap::edit::deleteEvent (p, 0));     // explicit: - that step
    CHECK (p.lengthTicks() == length);
    apply (ap::edit::setLength (p, length + 2 * T));
    CHECK (p.lengthTicks() == length + 2 * T);

    // Undo every step back to the start, then redo to the end: exact data each way.
    std::vector<ap::Pattern> forward;
    while (h.canUndo())
    {
        forward.push_back (h.current());
        h.undo();
    }
    CHECK (h.current() == simple ({ { 0, T }, { 1, T }, { -1, T }, { 2, T } }, 2));
    for (auto it = forward.rbegin(); it != forward.rend(); ++it)
        CHECK (h.redo() == *it);
    CHECK (h.current() == p);
    // A new edit after an undo drops the redo branch.
    h.undo();
    auto q = h.current();
    ap::edit::reverse (q);
    h.push (q);
    CHECK_FALSE (h.canRedo());
}

TEST_CASE ("arp pattern model: limits are enforced (minimum subdivision, capacity, LENGTH never removes notes)", "[unit][arp-pattern]")
{
    auto p = simple ({ { 0, arp::minEventTicks } }, 0);
    CHECK_FALSE (ap::edit::split (p, 0));                 // below the shortest event
    p = simple ({ { 0, T }, { 1, T } }, 1);
    CHECK_FALSE (ap::edit::setLength (p, T));             // would cut a note
    CHECK_FALSE (ap::edit::extend (p, 0, T));             // no rest after it
    CHECK_FALSE (ap::edit::deleteEvent (p, 5));            // no such event
    auto one = simple ({ { 0, T } }, 0);
    CHECK_FALSE (ap::edit::deleteEvent (one, 0));         // the last event stays
    // Capacity: 256 events compile; a 257th is refused by split, and an over-full pattern
    // compiles to an invalid one (never truncated).
    ap::Pattern full;
    for (int i = 0; i < arp::maxPatternEvents; ++i)
        full.events.push_back ({ 0, false, arp::minEventTicks * 2, 0 });
    CHECK (full.valid());
    CHECK_FALSE (ap::edit::split (full, 0));
    full.events.push_back ({ 0, false, arp::minEventTicks * 2, 0 });
    CHECK_FALSE (full.valid());
    CHECK (ap::compile (full).count == 0);
    // Steps: a capture of more than 128 steps is cut at the capacity and says so.
    std::vector<ap::CapturedNote> longTake;
    for (int i = 0; i < 140; ++i)
        longTake.push_back ({ i * 0.25, 0.2, 60 + i % 5, 100 });
    ap::Interpretation steps;
    steps.timing = ap::Interpretation::Timing::steps;
    ap::InterpretReport report;
    const auto cut = ap::interpret (longTake, steps, &report);
    CHECK (static_cast<int> (cut.events.size()) == arp::maxPatternSteps);
    CHECK (report.beyondCapacity == 12);
}

TEST_CASE ("arp pattern model: compile and decompile round-trip; the file round-trips and refuses what it cannot read", "[unit][arp-pattern]")
{
    ap::Document d;
    d.id = newArpPatternId();
    d.name = "Five up · edited";
    d.description = "A five-note climb, the third split";
    d.capture = capture (lab::referenceGestures()[0]);
    d.captureBpm = 97.5;
    d.interpretation.timing = ap::Interpretation::Timing::rhythm;
    d.interpretation.quantumTicks = 120;
    d.interpretation.stepQuarters = 0.5;
    d.interpretation.chord = ap::ChordPolicy::lowest;
    d.interpretation.mapping = arp::CustomMapping::degrees;
    d.pattern = simple ({ { 0, T }, { 2, T / 2 }, { 2, T / 2 }, { -1, T }, { 4, 3 * T } }, 4);
    d.pattern.mapping = arp::CustomMapping::degrees;
    d.pattern.events[1].velocity = 110;
    CHECK (ap::decompile (ap::compile (d.pattern)) == d.pattern);

    std::string error;
    const auto again = arpPatternFromString (arpPatternToString (d), error);
    INFO (error);
    REQUIRE (again.has_value());
    CHECK (again->id == d.id);
    CHECK (again->name == d.name);
    CHECK (again->pattern == d.pattern);
    CHECK (again->interpretation == d.interpretation);
    CHECK (again->capture.size() == d.capture.size());
    CHECK (again->capture[2].onset == Approx (d.capture[2].onset).margin (1.0e-6));
    CHECK (again->captureBpm == Approx (97.5));

    TempFile file ("roundtrip.andorpattern");
    REQUIRE (writeArpPattern (file.path, d, error));
    const auto read = readArpPattern (file.path, error);
    REQUIRE (read.has_value());
    CHECK (read->pattern == d.pattern);

    // REST keeps the note's rank in memory, the file stores -1: the documents stay equal (a rest's
    // rank means nothing), so a session saved and recalled is the same pattern.
    auto rested = d;
    REQUIRE (ap::edit::toRest (rested.pattern, 2));
    REQUIRE (rested.pattern.events[2].rank == 2);
    const auto restedAgain = arpPatternFromString (arpPatternToString (rested), error);
    REQUIRE (restedAgain.has_value());
    CHECK (restedAgain->pattern.events[2].rank == 0);
    CHECK (*restedAgain == rested);
    CHECK (restedAgain->pattern != d.pattern);

    auto json = arpPatternToJson (d);
    json.getDynamicObject()->setProperty ("schemaVersion", 2);
    CHECK_FALSE (arpPatternFromJson (json, error).has_value());   // a newer schema: refused
    CHECK (error.find ("newer") != std::string::npos);
    json = arpPatternToJson (d);
    json.getDynamicObject()->setProperty ("kind", "something.else");
    CHECK_FALSE (arpPatternFromJson (json, error).has_value());
    auto broken = d;
    broken.pattern.events[0].duration = 1;
    CHECK_FALSE (arpPatternFromJson (arpPatternToJson (broken), error).has_value());
    CHECK_FALSE (arpPatternFromString ("{ not json", error).has_value());
    CHECK (newArpPatternId() != newArpPatternId());
}

TEST_CASE ("arp pattern model: MIDI import keeps tracks, channels, tempo, PPQ, velocity-0 note-offs and overlaps", "[unit][arp-pattern]")
{
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (480);
    juce::MidiMessageSequence conductor;
    conductor.addEvent (juce::MidiMessage::tempoMetaEvent (500000 * 120 / 90), 0);   // 90 BPM
    midi.addTrack (conductor);
    juce::MidiMessageSequence lead;
    lead.addEvent (juce::MidiMessage::textMetaEvent (3, "Lead"), 0);
    lead.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    lead.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 0), 240);          // velocity 0: off at 1/2 quarter
    lead.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 90), 480);
    lead.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 80), 600);          // overlap of one pitch
    lead.addEvent (juce::MidiMessage::noteOff (1, 64), 720);
    lead.addEvent (juce::MidiMessage::noteOff (1, 64), 960);
    lead.addEvent (juce::MidiMessage::controllerEvent (1, 7, 100), 10);                // ignored
    lead.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 120), 0);          // another channel
    lead.addEvent (juce::MidiMessage::noteOff (10, 36), 120);
    lead.updateMatchedPairs();
    midi.addTrack (lead);
    TempFile file ("phrase.mid");
    {
        juce::File f (file.path.string());
        f.deleteFile();
        juce::FileOutputStream out (f);
        REQUIRE (out.openedOk());
        REQUIRE (midi.writeTo (out));
    }
    std::string error;
    const auto phrases = readMidiPhrases (file.path, error);
    INFO (error);
    REQUIRE (phrases.size() == 2);
    const auto& melody = phrases[0].channel == 1 ? phrases[0] : phrases[1];
    const auto& drum = phrases[0].channel == 1 ? phrases[1] : phrases[0];
    CHECK (melody.name.find ("Lead") != std::string::npos);
    CHECK (melody.name.find ("channel 1") != std::string::npos);
    CHECK (melody.bpm == Approx (90.0).margin (0.01));
    REQUIRE (melody.notes.size() == 3);
    CHECK (melody.notes[0].onset == Approx (0.0));
    CHECK (melody.notes[0].duration == Approx (0.5));
    CHECK (melody.notes[1].onset == Approx (1.0));
    CHECK (melody.notes[2].onset == Approx (1.25));
    REQUIRE (drum.notes.size() == 1);
    CHECK (drum.notes[0].pitch == 36);

    TempFile bad ("bad.mid");
    {
        juce::File f (bad.path.string());
        f.replaceWithText ("not midi");
    }
    CHECK (readMidiPhrases (bad.path, error).empty());
    CHECK_FALSE (error.empty());
}
