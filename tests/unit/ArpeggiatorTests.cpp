// ARPEGGIATOR: the note-event stage ahead of the voice engine. Patterns, octaves, gate and
// velocity; sample-accurate timing on the host's grid (tempo x sample rate x block size x
// rate) and free running without one; loops, jumps and tempo changes without double or
// missed steps; sustain pedal, All Notes Off and on/off hand-overs without stuck notes;
// RANDOM reproducible from its seed; the step display shows what plays.

#include "core/Prng.h"
#include "engine/Arpeggiator.h"
#include "research/RenderConfig.h"
#include "research/RenderSession.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace osp;

namespace
{
    using Kind = Arpeggiator::Event::Kind;

    /** An input event for the harness (absolute sample time). */
    struct Input
    {
        enum class Type
        {
            on,
            off,
            pedalDown,
            pedalUp,
            allOff
        };
        std::int64_t time = 0;
        Type type = Type::on;
        int note = 60, velocity = 100, channel = 1;
    };

    /** An output event at its absolute time, as the voice engine would receive it. */
    struct Out
    {
        std::int64_t time = 0;
        Kind kind = Kind::noteOn;
        int note = 0, velocity = 0, channel = 1;
        bool direct = false;   ///< passed through (the arpeggiator was off)
    };

    /** A host transport for the harness. */
    struct Transport
    {
        bool present = false, playing = false;
        double bpm = 120.0, ppq = 0.0;
    };

    /**
        Runs the arpeggiator the way the plugin does: per block, settings and host time first,
        then the block's events in order (note events go to the arpeggiator, and also straight
        through while it is off), then the arpeggiator's own events, merged by time.
    */
    struct Harness
    {
        Arpeggiator arp;
        double sampleRate = 48000.0;
        int blockSize = 256;
        Arpeggiator::Settings settings;
        Transport transport;
        std::int64_t now = 0;
        std::vector<Out> out;

        explicit Harness (double rate = 48000.0, int block = 256, std::uint64_t seed = 1) : sampleRate (rate), blockSize (block)
        {
            arp.setSeed (seed);
            arp.prepare (sampleRate);
            settings.enabled = true;
        }

        /** Renders `samples` samples; inputs are absolute times (any order inside a block is
            sorted). Settings changes between run() calls apply at the next block. */
        void run (std::int64_t samples, std::vector<Input> inputs = {}, const std::function<void (Harness&, std::int64_t)>& perBlock = {})
        {
            std::stable_sort (inputs.begin(), inputs.end(), [] (const Input& a, const Input& b) { return a.time < b.time; });
            std::size_t next = 0;
            const std::int64_t end = now + samples;
            while (now < end)
            {
                const int n = static_cast<int> (std::min<std::int64_t> (blockSize, end - now));
                if (perBlock)
                    perBlock (*this, now);
                HostTiming timing;
                timing.valid = transport.present;
                timing.playing = transport.playing;
                timing.bpm = transport.bpm;
                timing.ppq = transport.ppq;
                arp.beginBlock (settings, timing, n);
                const bool owns = arp.isEnabled();
                std::vector<Out> direct;
                while (next < inputs.size() && inputs[next].time < now + n)
                {
                    const auto& in = inputs[next++];
                    const int offset = static_cast<int> (std::max<std::int64_t> (0, in.time - now));
                    switch (in.type)
                    {
                        case Input::Type::on:
                            arp.noteOn (offset, in.note, in.velocity, in.channel);
                            if (! owns)
                                direct.push_back ({ now + offset, Kind::noteOn, in.note, in.velocity, in.channel, true });
                            break;
                        case Input::Type::off:
                            arp.noteOff (offset, in.note, in.channel);
                            if (! owns)
                                direct.push_back ({ now + offset, Kind::noteOff, in.note, 0, in.channel, true });
                            break;
                        case Input::Type::pedalDown:
                        case Input::Type::pedalUp:
                        {
                            const bool down = in.type == Input::Type::pedalDown;
                            arp.sustainPedal (offset, down);
                            if (! owns)
                                direct.push_back ({ now + offset, down ? Kind::sustainOn : Kind::sustainOff, 0, 0, 1, true });
                            break;
                        }
                        case Input::Type::allOff:
                            arp.allNotesOff (offset);
                            // All Notes Off always reaches the engine: model it as the end of everything.
                            direct.push_back ({ now + offset, Kind::sustainOff, -1, 0, 1, true });
                            break;
                    }
                }
                arp.endBlock();
                // Merge as MidiBuffer does: by time; pass-through first at equal times, then
                // the arpeggiator's events in the order it made them.
                std::vector<Out> block = direct;
                for (int i = 0; i < arp.numEvents(); ++i)
                {
                    const auto& e = arp.event (i);
                    REQUIRE (e.offset >= 0);
                    REQUIRE (e.offset < n);
                    block.push_back ({ now + e.offset, e.kind, e.note, e.velocity, e.channel, false });
                }
                std::stable_sort (block.begin(), block.end(), [] (const Out& a, const Out& b) { return a.time < b.time; });
                out.insert (out.end(), block.begin(), block.end());
                if (transport.present)
                    transport.ppq += n * transport.bpm / (60.0 * sampleRate);
                now += n;
            }
        }

        std::vector<Out> ons() const
        {
            std::vector<Out> r;
            for (const auto& e : out)
                if (e.kind == Kind::noteOn && ! e.direct)
                    r.push_back (e);
            return r;
        }
        std::vector<int> onNotes() const
        {
            std::vector<int> r;
            for (const auto& e : ons())
                r.push_back (e.note);
            return r;
        }
    };

    Input on (std::int64_t t, int note, int velocity = 100, int channel = 1) { return { t, Input::Type::on, note, velocity, channel }; }
    Input off (std::int64_t t, int note, int channel = 1) { return { t, Input::Type::off, note, 0, channel }; }

    /**
        A voice engine stand-in with the real engine's rules: a note-off ends every voice of
        its pitch (held by the pedal while it is down), pedal up ends the pedal-held voices,
        All Notes Off ends everything. Returns false if any rule the arpeggiator relies on is
        broken (a generated pitch started while still sounding from the arpeggiator).
    */
    struct EngineModel
    {
        std::map<int, int> voices;   ///< pitch -> voices still keyed
        std::map<int, int> pedalHeld;
        bool pedal = false;
        bool retriggerViolation = false;
        std::map<int, bool> arpSounding;

        void apply (const Out& e)
        {
            switch (e.kind)
            {
                case Kind::noteOn:
                    if (! e.direct && arpSounding[e.note])
                        retriggerViolation = true;
                    if (! e.direct)
                        arpSounding[e.note] = true;
                    ++voices[e.note];
                    break;
                case Kind::noteOff:   // any note-off ends every voice of the pitch
                    arpSounding[e.note] = false;
                    if (pedal)
                        pedalHeld[e.note] += voices[e.note];
                    voices[e.note] = 0;
                    break;
                case Kind::sustainOn: pedal = true; break;
                case Kind::sustainOff:
                    pedal = false;
                    pedalHeld.clear();
                    if (e.note == -1)   // All Notes Off
                    {
                        voices.clear();
                        arpSounding.clear();
                    }
                    break;
            }
        }
        int sounding() const
        {
            int n = 0;
            for (const auto& [pitch, count] : voices)
                n += count;
            for (const auto& [pitch, count] : pedalHeld)
                n += count;
            return n;
        }
    };

    /** Every offset in a block: the note-offs come before the note-ons. */
    bool offsBeforeOns (const std::vector<Out>& events)
    {
        for (std::size_t i = 1; i < events.size(); ++i)
            if (events[i].time == events[i - 1].time && events[i].kind == Kind::noteOff && events[i - 1].kind == Kind::noteOn
                && ! events[i].direct && ! events[i - 1].direct)
                return false;
        return true;
    }

    std::vector<int> run (ArpPattern pattern, std::vector<int> notes, int steps, int octaves = 1, std::uint64_t seed = 1)
    {
        Harness h (48000.0, 256, seed);
        h.settings.pattern = pattern;
        h.settings.octaves = octaves;
        std::vector<Input> in;
        for (std::size_t i = 0; i < notes.size(); ++i)
            in.push_back (on (0, notes[i], 60 + static_cast<int> (i)));
        // 120 BPM, 1/8: a step every 12000 samples. Stop just before step `steps`.
        h.run (static_cast<std::int64_t> (steps) * 12000 - 100, in);
        return h.onNotes();
    }
}

TEST_CASE ("arp: rates and names", "[unit][arp]")
{
    CHECK (arp::rateQuarters (ArpRate::quarter) == 1.0);
    CHECK (arp::rateQuarters (ArpRate::eighth) == 0.5);
    CHECK (arp::rateQuarters (ArpRate::sixteenth) == 0.25);
    CHECK (arp::rateQuarters (ArpRate::thirtySecond) == 0.125);
    CHECK (arp::rateQuarters (ArpRate::quarterDotted) == 1.5);
    CHECK (arp::rateQuarters (ArpRate::eighthDotted) == 0.75);
    CHECK (arp::rateQuarters (ArpRate::sixteenthDotted) == 0.375);
    CHECK (std::abs (arp::rateQuarters (ArpRate::quarterTriplet) - 2.0 / 3.0) < 1.0e-12);
    CHECK (std::abs (arp::rateQuarters (ArpRate::eighthTriplet) - 1.0 / 3.0) < 1.0e-12);
    CHECK (std::abs (arp::rateQuarters (ArpRate::sixteenthTriplet) - 1.0 / 6.0) < 1.0e-12);
    CHECK (std::string (arp::rateName (ArpRate::eighthDotted)) == "1/8D");
    CHECK (std::string (arp::rateName (ArpRate::sixteenthTriplet)) == "1/16T");
    CHECK (std::string (arp::patternName (ArpPattern::upDown)) == "UP/DOWN");
    CHECK (std::string (arp::patternName (ArpPattern::played)) == "PLAYED");
}

TEST_CASE ("arp: UP, DOWN, UP/DOWN, PLAYED and CHORD patterns", "[unit][arp]")
{
    CHECK (run (ArpPattern::up, { 64, 60, 67 }, 7) == std::vector<int> { 60, 64, 67, 60, 64, 67, 60 });
    CHECK (run (ArpPattern::down, { 64, 60, 67 }, 7) == std::vector<int> { 67, 64, 60, 67, 64, 60, 67 });
    // UP/DOWN without repeating the ends.
    CHECK (run (ArpPattern::upDown, { 64, 60, 67 }, 9) == std::vector<int> { 60, 64, 67, 64, 60, 64, 67, 64, 60 });
    CHECK (run (ArpPattern::upDown, { 60, 72, 64, 67 }, 7) == std::vector<int> { 60, 64, 67, 72, 67, 64, 60 });
    CHECK (run (ArpPattern::upDown, { 64, 60 }, 5) == std::vector<int> { 60, 64, 60, 64, 60 });
    CHECK (run (ArpPattern::upDown, { 62 }, 3) == std::vector<int> { 62, 62, 62 });
    CHECK (run (ArpPattern::played, { 67, 60, 64 }, 6) == std::vector<int> { 67, 60, 64, 67, 60, 64 });
    // CHORD: every held note on every step.
    CHECK (run (ArpPattern::chord, { 64, 60, 67 }, 2) == std::vector<int> { 60, 64, 67, 60, 64, 67 });
    // Octaves.
    CHECK (run (ArpPattern::up, { 60, 64 }, 7, 3) == std::vector<int> { 60, 64, 72, 76, 84, 88, 60 });
    CHECK (run (ArpPattern::down, { 60, 64 }, 4, 2) == std::vector<int> { 76, 72, 64, 60 });
    CHECK (run (ArpPattern::upDown, { 60, 64 }, 7, 2) == std::vector<int> { 60, 64, 72, 76, 72, 64, 60 });
    CHECK (run (ArpPattern::played, { 64, 60 }, 5, 2) == std::vector<int> { 64, 60, 76, 72, 64 });
    CHECK (run (ArpPattern::chord, { 60, 64 }, 3, 2) == std::vector<int> { 60, 64, 72, 76, 60, 64 });
    // MIDI range: octave copies above 127 are left out, nothing wraps or clamps onto 127.
    CHECK (run (ArpPattern::up, { 120, 124 }, 4, 4) == std::vector<int> { 120, 124, 120, 124 });
    CHECK (run (ArpPattern::up, { 110, 118 }, 4, 3) == std::vector<int> { 110, 118, 122, 110 });
    CHECK (run (ArpPattern::chord, { 118, 122 }, 3, 4) == std::vector<int> { 118, 122, 118, 122, 118, 122 });
    CHECK (run (ArpPattern::up, { 0, 127 }, 3, 2) == std::vector<int> { 0, 127, 12 });   // octave by octave
}

TEST_CASE ("arp: 0, 1, 2, 3, 4, 7, 10, 16, 64 and more notes", "[unit][arp]")
{
    for (int count : { 0, 1, 2, 3, 4, 7, 10, 16, 64, 70 })
    {
        CAPTURE (count);
        std::vector<int> notes;
        for (int i = 0; i < count; ++i)
            notes.push_back (30 + (i * 7) % 90);   // a spread, pressed out of order
        std::vector<int> unique = notes;
        std::sort (unique.begin(), unique.end());
        unique.erase (std::unique (unique.begin(), unique.end()), unique.end());
        if (unique.size() > static_cast<std::size_t> (Arpeggiator::maxHeld))
            unique.resize (Arpeggiator::maxHeld);   // beyond its capacity, extra notes are ignored
        const int steps = std::max (1, static_cast<int> (unique.size()) * 2);
        const auto played = run (ArpPattern::up, notes, steps);
        if (count == 0)
        {
            CHECK (played.empty());
            continue;
        }
        REQUIRE (played.size() == static_cast<std::size_t> (steps));
        // Capacity: with more than 64 notes held the first 64 pressed play.
        std::vector<int> pressed (notes.begin(), notes.begin() + std::min<std::size_t> (notes.size(), Arpeggiator::maxHeld));
        std::sort (pressed.begin(), pressed.end());
        pressed.erase (std::unique (pressed.begin(), pressed.end()), pressed.end());
        for (std::size_t i = 0; i < played.size(); ++i)
            CHECK (played[i] == pressed[i % pressed.size()]);
        for (auto pattern : { ArpPattern::down, ArpPattern::upDown, ArpPattern::played, ArpPattern::random, ArpPattern::chord })
            for (int octaves = 1; octaves <= 4; ++octaves)
            {
                const auto r = run (pattern, notes, 8, octaves);
                CHECK (! r.empty());
                for (int n : r)
                {
                    CHECK (n >= 0);
                    CHECK (n <= 127);
                }
            }
    }
}

TEST_CASE ("arp: velocities are preserved per note; a pressed-again note takes its new velocity", "[unit][arp]")
{
    Harness h;
    h.run (3 * 12000 - 100, { on (0, 60, 33), on (0, 64, 77), on (0, 67, 120) });
    const auto ons = h.ons();
    REQUIRE (ons.size() == 3);
    CHECK (ons[0].velocity == 33);
    CHECK (ons[1].velocity == 77);
    CHECK (ons[2].velocity == 120);
    // Pedal: 64 released (kept), then pressed again softer.
    Harness g;
    g.run (6 * 12000 - 100,
           { on (0, 60, 90), on (0, 64, 90), { 100, Input::Type::pedalDown }, off (200, 64), on (12000 + 50, 64, 20) });
    const auto later = g.ons();
    REQUIRE (later.size() == 6);
    CHECK (later[1].velocity == 90);
    CHECK (later[3].velocity == 20);
    CHECK (later[5].velocity == 20);
}

TEST_CASE ("arp: free running - the first step at once, then every step to the sample; the gate", "[unit][arp]")
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 1, 32, 64, 128, 256, 512, 4096 })
            for (double gate : { 0.10, 0.5, 0.75, 1.0 })
            {
                CAPTURE (rate, block, gate);
                Harness h (rate, block);
                h.settings.rate = ArpRate::sixteenthTriplet;
                h.settings.gate = gate;
                const std::int64_t start = 1234;
                const double step = (1.0 / 6.0) * rate * 60.0 / 120.0;
                h.run (static_cast<std::int64_t> (step * 20), { on (start, 60), on (start, 67) });
                const auto ons = h.ons();
                REQUIRE (ons.size() >= 18);
                for (std::size_t k = 0; k < ons.size(); ++k)
                {
                    const auto expected = start + static_cast<std::int64_t> (std::ceil (k * step - 1.0e-6));
                    CHECK (ons[k].time == expected);
                }
                // Each note's off at its gate.
                const auto gateSamples = std::max<std::int64_t> (1, std::llround (gate * step));
                std::map<int, std::int64_t> started;
                for (const auto& e : h.out)
                {
                    if (e.kind == Kind::noteOn)
                        started[e.note] = e.time;
                    else if (e.kind == Kind::noteOff)
                        CHECK (e.time - started[e.note] == gateSamples);
                }
                CHECK (offsBeforeOns (h.out));
            }
}

TEST_CASE ("arp: on the host's grid - 60/90/120/174 BPM x 44.1/48/96 kHz x 32..512 blocks x every rate", "[unit][arp]")
{
    int cases = 0;
    for (double bpm : { 60.0, 90.0, 120.0, 174.0 })
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (int block : { 32, 64, 128, 256, 512 })
                for (int r = 0; r < arp::rateCount; ++r)
                {
                    CAPTURE (bpm, rate, block, r);
                    Harness h (rate, block);
                    h.settings.rate = static_cast<ArpRate> (r);
                    h.settings.gate = 0.5;
                    h.transport = { true, true, bpm, 0.0 };
                    const double spq = rate * 60.0 / bpm;
                    const double q = arp::rateQuarters (h.settings.rate);
                    // The chord arrives a little after the downbeat: it waits for the next step.
                    const auto pressed = static_cast<std::int64_t> (0.3 * q * spq);
                    const auto total = static_cast<std::int64_t> (8.0 * spq);
                    h.run (total, { on (pressed, 60), on (pressed, 64), on (pressed, 67) });
                    const auto ons = h.ons();
                    std::vector<std::int64_t> expected;
                    for (int k = 1;; ++k)
                    {
                        const auto t = static_cast<std::int64_t> (std::ceil (k * q * spq - 1.0e-6));
                        if (t >= total)
                            break;
                        expected.push_back (t);
                    }
                    REQUIRE (ons.size() == expected.size());   // no double, no missed step
                    bool exact = true;
                    for (std::size_t k = 0; k < ons.size(); ++k)
                        exact = exact && ons[k].time == expected[k];
                    CHECK (exact);
                    CHECK (offsBeforeOns (h.out));
                    ++cases;
                }
    CHECK (cases == 4 * 3 * 5 * arp::rateCount);
}

TEST_CASE ("arp: a note on the grid plays on that sample; between steps it waits for the next", "[unit][arp]")
{
    Harness h (48000.0, 512);
    h.transport = { true, true, 120.0, 0.0 };
    // 1/8 at 120 BPM: 12000 samples. A note pressed exactly on step 2's sample plays there.
    h.run (60000, { on (24000, 60), off (30000, 60), on (30001, 62), off (40000, 62) });
    const auto ons = h.ons();
    REQUIRE (ons.size() == 2);
    CHECK (ons[0].time == 24000);
    CHECK (ons[0].note == 60);
    CHECK (ons[1].time == 36000);
    CHECK (ons[1].note == 62);
}

TEST_CASE ("arp: loops and jumps resynchronise without catching up; tempo changes follow the grid", "[unit][arp]")
{
    SECTION ("a loop back to bar 1")
    {
        Harness h (48000.0, 480);
        h.transport = { true, true, 120.0, 0.0 };
        h.run (48000 * 2 + 240, { on (0, 60), on (0, 64) });   // 4 quarters + half a block
        const auto before = h.ons().size();
        CHECK (before == 9);   // steps at 0, 0.5 .. 4.0 quarters
        h.transport.ppq = 0.0;   // the host loops back
        const auto loopStart = h.now;
        h.run (12000 * 2);
        const auto ons = h.ons();
        REQUIRE (ons.size() == before + 2);
        CHECK (ons[before].time == loopStart);          // the boundary at the loop start plays
        CHECK (ons[before + 1].time == loopStart + 12000);
    }
    SECTION ("a jump forward plays the next boundary, never the ones skipped")
    {
        Harness h (48000.0, 256);
        h.transport = { true, true, 120.0, 0.0 };
        h.run (12000 * 2 + 100, { on (0, 60) });
        const auto before = h.ons().size();
        h.transport.ppq = 100.1;   // locate far ahead, between steps
        const auto jumpAt = h.now;
        h.run (12000 * 2);
        const auto ons = h.ons();
        REQUIRE (ons.size() == before + 2);
        // 100.5 quarters is 0.4 quarters after the jump: 9600 samples.
        CHECK (std::abs (ons[before].time - (jumpAt + 9600)) <= 1);
    }
    SECTION ("a tempo change")
    {
        Harness h (48000.0, 128);
        h.transport = { true, true, 120.0, 0.0 };
        h.run (48000, { on (0, 60) });   // 2 quarters at 120: steps at 0, 12000, 24000, 36000
        CHECK (h.ons().size() == 4);
        h.transport.bpm = 60.0;          // now a quarter is 48000 samples, a step 24000
        const auto changeAt = h.now;
        h.run (48000);
        const auto ons = h.ons();
        REQUIRE (ons.size() == 6);
        CHECK (std::abs (ons[4].time - changeAt) <= 1);           // 2.0 quarters
        CHECK (std::abs (ons[5].time - (changeAt + 24000)) <= 1); // 2.5 quarters
    }
    SECTION ("the transport stops: free running continues at the host's last tempo, then picks up the grid again")
    {
        Harness h (48000.0, 256);
        h.transport = { true, true, 90.0, 0.0 };
        const double step = 0.5 * 48000.0 * 60.0 / 90.0;   // 16000
        h.run (static_cast<std::int64_t> (step * 2 + 300), { on (0, 60) });
        REQUIRE (h.ons().size() == 3);
        h.transport.playing = false;
        h.run (static_cast<std::int64_t> (step * 3));
        const auto ons = h.ons();
        REQUIRE (ons.size() == 6);
        for (std::size_t k = 3; k < ons.size(); ++k)
            CHECK (std::abs (ons[k].time - static_cast<std::int64_t> (k * step)) <= 1);
        h.transport.playing = true;
        h.transport.ppq = 10.0;
        const auto restart = h.now;
        h.run (static_cast<std::int64_t> (step * 2));
        CHECK (h.ons()[6].time == restart);   // 10.0 is on the grid
    }
    SECTION ("no host: 120 BPM")
    {
        Harness h (48000.0, 256);
        h.run (12000 * 3 - 10, { on (0, 60) });
        const auto ons = h.ons();
        REQUIRE (ons.size() == 3);
        CHECK (ons[2].time == 24000);
    }
}

TEST_CASE ("arp: gate above 100 % ends a pitch right before it plays again; nothing hangs", "[unit][arp]")
{
    for (auto pattern : { ArpPattern::up, ArpPattern::chord, ArpPattern::random, ArpPattern::upDown })
        for (double gate : { 0.1, 0.75, 1.0, 1.25, 1.5 })
        {
            CAPTURE (static_cast<int> (pattern), gate);
            Harness h (48000.0, 64);
            h.settings.pattern = pattern;
            h.settings.gate = gate;
            h.run (12000 * 10, { on (0, 60), on (5, 61), off (12000 * 6, 60), off (12000 * 6, 61) });
            h.run (48000);
            EngineModel engine;
            for (const auto& e : h.out)
                engine.apply (e);
            CHECK_FALSE (engine.retriggerViolation);
            CHECK (engine.sounding() == 0);
            CHECK (h.arp.soundingCount() == 0);
            CHECK (offsBeforeOns (h.out));
        }
}

TEST_CASE ("arp: the sustain pedal keeps released notes in the pattern; generated notes still end", "[unit][arp]")
{
    Harness h;
    h.run (12000 * 10,
           { on (0, 60), on (0, 64), on (0, 67), { 6000, Input::Type::pedalDown }, off (7000, 60), off (7000, 64), off (7000, 67),
             { 12000 * 6 + 6000, Input::Type::pedalUp } });
    const auto ons = h.ons();
    // Steps 0..6 play (the pedal holds C E G), nothing after the pedal is released.
    REQUIRE (ons.size() == 7);
    CHECK (h.onNotes() == std::vector<int> { 60, 64, 67, 60, 64, 67, 60 });
    for (const auto& e : h.out)
        CHECK (e.kind != Kind::sustainOn);   // the pedal belongs to the arpeggiator while it is on
    EngineModel engine;
    for (const auto& e : h.out)
        engine.apply (e);
    CHECK (engine.sounding() == 0);
    // Gate note-offs went out while the pedal was down.
    int offsWhilePedal = 0;
    for (const auto& e : h.out)
        if (e.kind == Kind::noteOff && e.time > 7000 && e.time < 12000 * 6 + 6000)
            ++offsWhilePedal;
    CHECK (offsWhilePedal >= 5);
}

TEST_CASE ("arp: All Notes Off ends everything at once", "[unit][arp]")
{
    Harness h;
    h.settings.gate = 1.5;
    h.run (12000 * 6, { on (0, 60), on (0, 64), { 100, Input::Type::pedalDown }, { 12000 * 2 + 500, Input::Type::allOff } });
    const auto ons = h.ons();
    CHECK (ons.size() == 3);
    bool offAtPanic = false;
    for (const auto& e : h.out)
        if (e.kind == Kind::noteOff && e.time == 12000 * 2 + 500)
            offAtPanic = true;
    CHECK (offAtPanic);
    CHECK (h.arp.heldCount() == 0);
    CHECK (h.arp.soundingCount() == 0);
    CHECK_FALSE (h.arp.pedalIsDown());
    CHECK_FALSE (h.arp.isRunning());
}

TEST_CASE ("arp: switching on and off mid-performance hands the notes over cleanly", "[unit][arp]")
{
    SECTION ("off: nothing at all is generated; on: the held keys end and the pattern starts")
    {
        Harness h;
        h.settings.enabled = false;
        h.run (24000, { on (0, 60, 80), on (10, 64, 90), on (20, 67, 100), off (15000, 67) });
        CHECK (h.ons().empty());
        for (const auto& e : h.out)
            CHECK (e.direct);
        CHECK (h.arp.heldCount() == 2);   // tracked all along
        h.out.clear();
        h.settings.enabled = true;
        const auto switchAt = h.now;
        h.run (12000 * 2 - 100);
        REQUIRE (h.out.size() >= 4);
        CHECK (h.out[0].kind == Kind::noteOff);
        CHECK (h.out[1].kind == Kind::noteOff);
        CHECK (h.out[0].time == switchAt);
        CHECK (h.onNotes() == std::vector<int> { 60, 64 });
        CHECK (h.ons()[0].time == switchAt);
        CHECK (h.ons()[0].velocity == 80);
    }
    SECTION ("on -> off: generated notes end, held keys sound as played, the pedal returns")
    {
        Harness h;
        h.settings.gate = 1.5;
        h.run (30000, { on (0, 60, 70), on (0, 64, 75), { 100, Input::Type::pedalDown }, off (200, 64) });
        h.out.clear();
        h.settings.enabled = false;
        h.run (24000);
        // Off(s) first, then the pedal, then the key still held (60) directly.
        REQUIRE (h.out.size() >= 3);
        std::size_t i = 0;
        while (i < h.out.size() && h.out[i].kind == Kind::noteOff)
            ++i;
        CHECK (i >= 1);
        REQUIRE (i + 1 < h.out.size());
        CHECK (h.out[i].kind == Kind::sustainOn);
        CHECK (h.out[i + 1].kind == Kind::noteOn);
        CHECK (h.out[i + 1].note == 60);
        CHECK (h.out[i + 1].velocity == 70);
        CHECK (h.out.size() == i + 2);   // nothing generated after
        CHECK (h.arp.soundingCount() == 0);
    }
    SECTION ("the pedal down when switching on: the engine's pedal is released")
    {
        Harness h;
        h.settings.enabled = false;
        h.run (1000, { on (0, 60), { 100, Input::Type::pedalDown }, off (200, 60) });
        h.out.clear();
        h.settings.enabled = true;
        h.run (12000);
        REQUIRE (! h.out.empty());
        CHECK (h.out[0].kind == Kind::sustainOff);
        CHECK (h.onNotes() == std::vector<int> { 60 });   // the pedal-held note is arpeggiated
    }
}

TEST_CASE ("arp: random performances never leave a note hanging (stress)", "[unit][arp]")
{
    for (int trial = 0; trial < 60; ++trial)
    {
        CAPTURE (trial);
        Prng prng (Prng::deriveSeed (77, static_cast<std::uint64_t> (trial)));
        const int block = std::array<int, 6> { 1, 17, 64, 256, 512, 2048 }[static_cast<std::size_t> (trial % 6)];
        Harness h (trial % 3 == 0 ? 44100.0 : (trial % 3 == 1 ? 48000.0 : 96000.0), block, static_cast<std::uint64_t> (trial));
        h.transport = { trial % 2 == 0, trial % 4 == 0, 60.0 + 120.0 * prng.nextDouble(), 0.0 };
        std::vector<Input> in;
        std::array<bool, 128> down {};
        const std::int64_t length = 48000 * 6;
        for (int i = 0; i < 160; ++i)
        {
            const auto t = static_cast<std::int64_t> (prng.nextBelow (static_cast<std::uint64_t> (length)));
            const int note = 40 + static_cast<int> (prng.nextBelow (40));
            const auto kind = prng.nextBelow (20);
            if (kind < 9)
                in.push_back (on (t, note, 1 + static_cast<int> (prng.nextBelow (127))));
            else if (kind < 18)
                in.push_back (off (t, note));
            else if (kind == 18)
                in.push_back ({ t, prng.nextBelow (2) == 0 ? Input::Type::pedalDown : Input::Type::pedalUp });
            else
                in.push_back ({ t, Input::Type::allOff });
        }
        (void) down;
        h.run (length, in, [&prng] (Harness& x, std::int64_t) {
            if (prng.nextBelow (40) == 0)
                x.settings.enabled = ! x.settings.enabled;
            if (prng.nextBelow (30) == 0)
                x.settings.pattern = static_cast<ArpPattern> (prng.nextBelow (arp::patternCount));
            if (prng.nextBelow (30) == 0)
                x.settings.rate = static_cast<ArpRate> (prng.nextBelow (arp::rateCount));
            if (prng.nextBelow (30) == 0)
                x.settings.gate = 0.1 + 1.4 * prng.nextDouble();
            if (prng.nextBelow (30) == 0)
                x.settings.octaves = 1 + static_cast<int> (prng.nextBelow (4));
            if (x.transport.present && prng.nextBelow (60) == 0)
                x.transport.ppq = 32.0 * prng.nextDouble();   // jumps
            if (x.transport.present && prng.nextBelow (60) == 0)
                x.transport.playing = ! x.transport.playing;
        });
        // The performer lets go of everything (and the pedal): silence follows.
        std::vector<Input> release;
        for (int n = 0; n < 128; ++n)
            release.push_back (off (h.now, n));
        release.push_back ({ h.now, Input::Type::pedalUp });
        h.run (48000 * 2, release);
        EngineModel engine;
        for (const auto& e : h.out)
            engine.apply (e);
        CHECK_FALSE (engine.retriggerViolation);
        CHECK (engine.sounding() == 0);
        CHECK (h.arp.soundingCount() == 0);
        CHECK (h.arp.heldCount() == 0);
        CHECK (offsBeforeOns (h.out));
    }
}

TEST_CASE ("arp: RANDOM is reproducible from its seed, avoids immediate repeats and restarts with the transport", "[unit][arp]")
{
    const auto a = run (ArpPattern::random, { 60, 62, 64, 65, 67 }, 64, 1, 5);
    const auto b = run (ArpPattern::random, { 60, 62, 64, 65, 67 }, 64, 1, 5);
    const auto c = run (ArpPattern::random, { 60, 62, 64, 65, 67 }, 64, 1, 6);
    CHECK (a == b);
    CHECK (a != c);
    REQUIRE (a.size() == 64);
    for (std::size_t i = 1; i < a.size(); ++i)
        CHECK (a[i] != a[i - 1]);
    for (int n : { 60, 62, 64, 65, 67 })
        CHECK (std::count (a.begin(), a.end(), n) >= 5);
    CHECK (run (ArpPattern::random, { 60 }, 4) == std::vector<int> { 60, 60, 60, 60 });
    const auto two = run (ArpPattern::random, { 60, 72 }, 6);
    for (std::size_t i = 1; i < two.size(); ++i)
        CHECK (two[i] != two[i - 1]);

    // A bounce: the transport starts, the pattern and its choices start over.
    auto bounce = [] (Harness& h) {
        h.arp.restartPattern();
        h.transport = { true, true, 120.0, 0.0 };
        h.out.clear();
        h.run (12000 * 12 - 10, { on (h.now, 60), on (h.now, 64), on (h.now, 67), on (h.now, 71), off (h.now + 12000 * 11, 60),
                                   off (h.now + 12000 * 11, 64), off (h.now + 12000 * 11, 67), off (h.now + 12000 * 11, 71) });
        h.run (24000);
        return h.onNotes();
    };
    Harness h (48000.0, 256, 9);
    h.settings.pattern = ArpPattern::random;
    const auto first = bounce (h);
    h.transport.playing = false;
    h.run (12000 * 5, { on (h.now, 62), on (h.now, 69), off (h.now + 30000, 62), off (h.now + 30000, 69) });   // played in between
    const auto second = bounce (h);
    CHECK (first == second);
}

TEST_CASE ("arp: the display shows the page of steps, played and to come (RANDOM's real choices)", "[unit][arp]")
{
    for (auto pattern : { ArpPattern::up, ArpPattern::upDown, ArpPattern::random, ArpPattern::chord })
    {
        CAPTURE (static_cast<int> (pattern));
        Harness h (48000.0, 256, 3);
        h.settings.pattern = pattern;
        h.settings.octaves = 2;
        Arpeggiator::Display before;
        h.run (12000 * 3 - 50, { on (0, 60), on (0, 64), on (0, 67) });   // steps 0, 1, 2 played
        h.arp.display (before);
        CHECK (before.active);
        CHECK (before.current == 2);
        h.run (12000 * 14);   // through step 16: the rest of the page
        const auto ons = h.ons();
        // What the display promised is what played (lowest note of each step).
        std::vector<int> lowest;
        std::int64_t lastTime = -1;
        for (const auto& e : ons)
            if (e.time != lastTime)
            {
                lowest.push_back (e.note);
                lastTime = e.time;
            }
            else
                lowest.back() = std::min (lowest.back(), e.note);
        REQUIRE (lowest.size() >= 16);
        for (int j = 0; j < 16; ++j)
            CHECK (before.low[static_cast<std::size_t> (j)] == lowest[static_cast<std::size_t> (j)]);
    }
    Harness idle;
    Arpeggiator::Display d;
    idle.run (1000);
    idle.arp.display (d);
    CHECK_FALSE (d.active);
    CHECK (d.current == -1);
    for (auto v : d.low)
        CHECK (v == -1);
}

TEST_CASE ("arp: the same performance gives the same events every time", "[unit][arp]")
{
    auto perform = [] {
        Harness h (44100.0, 333, 42);
        h.settings.pattern = ArpPattern::random;
        h.settings.rate = ArpRate::sixteenthDotted;
        h.settings.octaves = 3;
        h.settings.gate = 1.2;
        h.transport = { true, true, 133.0, 3.7 };
        h.run (44100 * 4, { on (100, 50), on (3000, 57), on (9000, 62), off (50000, 57), on (60000, 66), off (120000, 50) });
        return h.out;
    };
    const auto a = perform(), b = perform();
    REQUIRE (a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        CHECK (a[i].time == b[i].time);
        CHECK (a[i].note == b[i].note);
        CHECK (a[i].kind == b[i].kind);
    }
}

TEST_CASE ("arp: research renders arpeggiate headless from a config's arp block", "[unit][arp]")
{
    const auto dir = std::filesystem::temp_directory_path() / "osp-arp-config-test";
    std::filesystem::create_directories (dir);
    const auto file = dir / "arp.json";
    {
        std::ofstream out (file);
        out << R"({ "schemaVersion": 1, "engine": "C", "sampleRate": 48000, "blockSize": 256,
                    "arp": { "pattern": "up/down", "rate": "1/16", "gate": 0.5, "octaves": 2, "bpm": 90 } })";
    }
    std::string error;
    const auto config = research::loadRenderConfig (file, error);
    std::filesystem::remove_all (dir);
    REQUIRE (config.has_value());
    CHECK (config->arp.settings.enabled);
    CHECK (config->arp.settings.pattern == ArpPattern::upDown);
    CHECK (config->arp.settings.rate == ArpRate::sixteenth);
    CHECK (config->arp.settings.gate == 0.5);
    CHECK (config->arp.settings.octaves == 2);
    CHECK (config->arp.bpm == 90.0);

    MidiSequence chord;
    chord.name = "chord";
    for (int note : { 60, 64, 67 })
        chord.events.push_back ({ 0.0, MidiEvent::Type::noteOn, note, 100, 1 });
    for (int note : { 60, 64, 67 })
        chord.events.push_back ({ 2.0, MidiEvent::Type::noteOff, note, 0, 1 });
    const auto a = research::arpeggiate (chord, *config, 48000.0);
    const auto b = research::arpeggiate (chord, *config, 48000.0);
    std::vector<int> notes;
    for (const auto& e : a.events)
        if (e.type == MidiEvent::Type::noteOn)
            notes.push_back (e.note);
    // 1/16 at 90 BPM: a step every 1/6 s, 12 steps in 2 s; UP/DOWN over two octaves.
    REQUIRE (notes.size() == 12);
    CHECK (std::vector<int> (notes.begin(), notes.begin() + 10) == std::vector<int> { 60, 64, 67, 72, 76, 79, 76, 72, 67, 64 });
    int ons = 0, offs = 0;
    for (const auto& e : a.events)
        (e.type == MidiEvent::Type::noteOn ? ons : offs) += 1;
    CHECK (ons == offs);   // every note ends
    REQUIRE (a.events.size() == b.events.size());
    for (std::size_t i = 0; i < a.events.size(); ++i)
        CHECK (a.events[i].timeSeconds == b.events[i].timeSeconds);
    // Off: the sequence is played as written.
    research::RenderConfig plain;
    CHECK (research::arpeggiate (chord, plain, 48000.0).events.size() == chord.events.size());
}

TEST_CASE ("arp: SWING delays every second step by up to half a step, on the grid and free running", "[unit][arp]")
{
    for (double swing : { 0.0, 0.15, 0.5, 1.0 })
        for (int block : { 32, 256, 512 })
            for (bool host : { false, true })
                for (int r : { 1, 2, 8 })   // 1/8, 1/16, 1/8T
                {
                    CAPTURE (swing, block, host, r);
                    Harness h (48000.0, block);
                    h.settings.rate = static_cast<ArpRate> (r);
                    h.settings.swing = swing;
                    h.settings.gate = 0.4;
                    h.transport = { host, host, 120.0, 0.0 };
                    const double step = arp::rateQuarters (h.settings.rate) * 24000.0;
                    h.run (static_cast<std::int64_t> (step * 12 - 10), { on (0, 60), on (0, 64), on (0, 67) });
                    const auto ons = h.ons();
                    REQUIRE (ons.size() == 12);
                    for (std::size_t k = 0; k < ons.size(); ++k)
                    {
                        const double late = k % 2 == 1 ? swing * 0.5 * step : 0.0;
                        CHECK (ons[k].time == static_cast<std::int64_t> (std::ceil (static_cast<double> (k) * step + late - 1.0e-6)));
                    }
                    CHECK (offsBeforeOns (h.out));
                    CHECK (h.onNotes()[3] == 60);   // the pattern itself is unchanged
                }
    // On the host's grid the swing sits on the beat, whenever the chord was pressed.
    Harness h (48000.0, 128);
    h.settings.swing = 0.5;
    h.transport = { true, true, 120.0, 0.0 };
    h.run (12000 * 6, { on (13000, 60) });   // pressed after step 1's boundary: first step is grid step 2 (on the beat)
    const auto ons = h.ons();
    REQUIRE (ons.size() >= 3);
    CHECK (ons[0].time == 24000);
    CHECK (ons[1].time == 36000 + 3000);
    CHECK (ons[2].time == 48000);
}
