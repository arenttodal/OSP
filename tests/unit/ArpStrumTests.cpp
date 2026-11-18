// ARP STRUM (docs/research/arp-strum): a chord step's notes one after another over a total
// spread, inside the one arpeggiator's scheduler. The ST0 timing fixtures, plus the safety rules:
// the disabled path and zero spread are the chord as before, the spread never reaches the next
// step, every note's gate runs from its own onset, and no waiting note survives a release, a
// panic, a transport restart, a jump or the arpeggiator switching off.

#include "engine/Arpeggiator.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <vector>

using namespace osp;

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
        std::uint64_t seed = 7;
        Arpeggiator::Settings settings;
        std::vector<In> inputs;
        std::int64_t length = 48000;
        /** Before each block: settings changes, host jumps, a restart. */
        std::function<void (Arpeggiator&, Arpeggiator::Settings&, double& ppq, std::int64_t now)> perBlock;
        int maxPendingSeen = 0;

        std::vector<Ev> render()
        {
            Arpeggiator arp;
            arp.setSeed (seed);
            arp.prepare (sampleRate);
            auto s = settings;
            auto in = inputs;
            std::stable_sort (in.begin(), in.end(), [] (const In& a, const In& b) { return a.time < b.time; });
            std::vector<Ev> out;
            std::size_t next = 0;
            double ppq = 0.0;
            for (std::int64_t now = 0; now < length;)
            {
                const int n = static_cast<int> (std::min<std::int64_t> (block, length - now));
                if (perBlock)
                    perBlock (arp, s, ppq, now);
                HostTiming t;
                t.valid = t.playing = host;
                t.bpm = bpm;
                t.ppq = ppq;
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
                maxPendingSeen = std::max (maxPendingSeen, arp.pendingStrumCount());
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

    Arpeggiator::Settings strumChord (double ms, StrumDirection d = StrumDirection::up)
    {
        Arpeggiator::Settings s;
        s.enabled = true;
        s.pattern = ArpPattern::chord;
        s.rate = ArpRate::quarter;
        s.gate = 0.5;
        s.strum = true;
        s.strumDirection = d;
        s.strumMs = ms;
        return s;
    }

    std::vector<Ev> ons (const std::vector<Ev>& events)
    {
        std::vector<Ev> r;
        for (const auto& e : events)
            if (e.kind == Kind::noteOn)
                r.push_back (e);
        return r;
    }

    /** Every note-on has a later note-off of its pitch before the next note-on of it, and
        nothing is left sounding at the end (given the render runs past the last gate). */
    bool balanced (const std::vector<Ev>& events)
    {
        std::map<int, int> sounding;
        for (const auto& e : events)
        {
            if (e.kind == Kind::noteOn)
            {
                if (sounding[e.note] > 0)
                    return false;   // a retrigger without its note-off first
                ++sounding[e.note];
            }
            else if (sounding[e.note] > 0)
                --sounding[e.note];
        }
        return std::all_of (sounding.begin(), sounding.end(), [] (const auto& kv) { return kv.second == 0; });
    }

    /** The onsets of the first chord step, relative to its first note. */
    std::vector<std::int64_t> firstStrum (const std::vector<Ev>& events, int notes)
    {
        const auto o = ons (events);
        std::vector<std::int64_t> r;
        for (int i = 0; i < notes && i < static_cast<int> (o.size()); ++i)
            r.push_back (o[static_cast<std::size_t> (i)].time - o[0].time);
        return r;
    }
}

TEST_CASE ("STRUM: the total spread is shared by the chord's notes, whatever their number", "[unit][arp][strum]")
{
    {
        Run r;
        r.settings = strumChord (30.0);
        r.inputs = chord (100, { 60, 64, 67, 71 });
        const auto e = r.render();
        CHECK (firstStrum (e, 4) == std::vector<std::int64_t> { 0, 480, 960, 1440 });   // 30 ms at 48 kHz
        const auto o = ons (e);
        CHECK (o[0].note == 60);
        CHECK (o[3].note == 71);
        CHECK (o[0].time == 100);   // the first note on the step itself
    }
    {
        Run r;
        r.settings = strumChord (30.0);
        r.inputs = chord (0, { 48, 52, 55, 60, 64, 67 });
        CHECK (firstStrum (r.render(), 6) == std::vector<std::int64_t> { 0, 288, 576, 864, 1152, 1440 });
    }
    {
        // One note: on the step; nothing held: nothing.
        Run r;
        r.settings = strumChord (30.0);
        r.inputs = chord (0, { 60 });
        const auto o = ons (r.render());
        REQUIRE (! o.empty());
        CHECK (o[0].time == 0);
        Run none;
        none.settings = strumChord (30.0);
        CHECK (none.render().empty());
    }
}

TEST_CASE ("STRUM: off, zero spread, or another pattern - exactly the chord as before (order, times, velocities)", "[unit][arp][strum]")
{
    auto reference = [] (const Arpeggiator::Settings& base) {
        Run r;
        r.settings = base;
        r.settings.strum = false;
        r.inputs = chord (37, { 60, 64, 67, 72 }, 90);
        return r.render();
    };
    auto withStrum = [] (Arpeggiator::Settings s) {
        Run r;
        r.settings = s;
        r.inputs = chord (37, { 60, 64, 67, 72 }, 90);
        return r.render();
    };
    for (const auto d : { StrumDirection::up, StrumDirection::down, StrumDirection::random, StrumDirection::alternate })
    {
        auto s = strumChord (0.0, d);   // zero spread
        s.octaves = 3;
        CHECK (withStrum (s) == reference (s));
        auto sync = s;
        sync.strumSync = true;
        sync.strumQuarters = 0.0;
        CHECK (withStrum (sync) == reference (sync));
    }
    // STRUM on, a pattern without chords: untouched.
    for (const auto p : { ArpPattern::up, ArpPattern::random, ArpPattern::thumbUpDown })
    {
        auto s = strumChord (40.0);
        s.pattern = p;
        CHECK (withStrum (s) == reference (s));
    }
}

TEST_CASE ("STRUM: DOWN, ALTERNATE and seeded RANDOM orders", "[unit][arp][strum]")
{
    const auto notes = { 60, 64, 67, 71 };
    {
        Run r;
        r.settings = strumChord (30.0, StrumDirection::down);
        r.inputs = chord (0, notes);
        const auto o = ons (r.render());
        CHECK (std::vector<int> { o[0].note, o[1].note, o[2].note, o[3].note } == std::vector<int> { 71, 67, 64, 60 });
    }
    {
        // ALTERNATE: up, down, up from the phrase's start.
        Run r;
        r.settings = strumChord (30.0, StrumDirection::alternate);
        r.inputs = chord (0, notes);
        r.length = 48000 * 2;   // 4 quarter steps at 120 BPM
        const auto o = ons (r.render());
        REQUIRE (o.size() >= 12);
        CHECK (o[0].note == 60);
        CHECK (o[4].note == 71);
        CHECK (o[8].note == 60);
    }
    {
        // RANDOM: every note once per chord, the same strums for the same seed.
        Run a, b, c;
        for (auto* r : { &a, &b, &c })
        {
            r->settings = strumChord (30.0, StrumDirection::random);
            r->inputs = chord (0, { 48, 52, 55, 60, 64, 67, 71 });
            r->length = 48000 * 3;
        }
        c.seed = 99;
        const auto ea = a.render(), eb = b.render(), ec = c.render();
        CHECK (ea == eb);
        const auto o = ons (ea);
        for (std::size_t chordStart = 0; chordStart + 7 <= o.size(); chordStart += 7)
        {
            std::set<int> seen;
            for (std::size_t i = chordStart; i < chordStart + 7; ++i)
                seen.insert (o[i].note);
            CHECK (seen.size() == 7);
        }
        CHECK (ea != ec);   // another seed: other strums (seven notes: 5040 orders)
    }
}

TEST_CASE ("STRUM: SYNC spreads by note value at the tempo; FREE by milliseconds at every sample rate", "[unit][arp][strum]")
{
    {
        // 1/32 at 120 BPM: 62.5 ms = 3000 samples at 48 kHz (host playing, quarter steps).
        Run r;
        r.host = true;
        r.settings = strumChord (0.0);
        r.settings.strumSync = true;
        r.settings.strumQuarters = 0.125;
        r.inputs = chord (0, { 60, 64, 67 });
        const auto f = firstStrum (r.render(), 3);
        CHECK (f == std::vector<std::int64_t> { 0, 1500, 3000 });
    }
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        Run r;
        r.sampleRate = rate;
        r.settings = strumChord (40.0);
        r.inputs = chord (0, { 60, 64, 67, 71, 74 });
        const auto f = firstStrum (r.render(), 5);
        REQUIRE (f.size() == 5);
        for (int i = 0; i < 5; ++i)
            CHECK (std::abs (static_cast<double> (f[static_cast<std::size_t> (i)]) / rate * 1000.0 - 10.0 * i) <= 1000.0 / rate);   // within a sample
    }
}

TEST_CASE ("STRUM: block sizes never change the events (waiting notes cross block boundaries)", "[unit][arp][strum]")
{
    std::vector<Ev> reference;
    for (const int block : { 1024, 512, 128, 64, 32, 7 })
    {
        Run r;
        r.block = block;
        r.settings = strumChord (37.0, StrumDirection::alternate);
        r.settings.rate = ArpRate::eighth;
        r.inputs = chord (13, { 55, 60, 64, 67, 72 });
        r.length = 48000 * 2;
        const auto e = r.render();
        if (reference.empty())
            reference = e;
        else
            CHECK (e == reference);
    }
}

TEST_CASE ("STRUM: the spread never reaches the next step (fast rates, swing, rate modulation); the setting is kept", "[unit][arp][strum]")
{
    auto check = [] (Run r, int chordNotes) {
        const auto o = ons (r.render());
        REQUIRE (o.size() >= static_cast<std::size_t> (chordNotes * 3));
        // Chord steps start where the lowest note plays first (UP); each strum ends before the next starts.
        for (std::size_t i = 0; i + chordNotes < o.size(); i += static_cast<std::size_t> (chordNotes))
            CHECK (o[i + static_cast<std::size_t> (chordNotes) - 1].time < o[i + static_cast<std::size_t> (chordNotes)].time);
    };
    {
        // 1/16 at 120 BPM is 125 ms; 250 ms asked: played as the step less 1 ms.
        Run r;
        r.settings = strumChord (250.0);
        r.settings.rate = ArpRate::sixteenth;
        r.inputs = chord (0, { 60, 64, 67, 71 });
        const auto f = firstStrum (r.render(), 4);
        CHECK (f.back() == 6000 - 48);
        check (r, 4);
        Arpeggiator arp;
        arp.prepare (48000.0);
        HostTiming t;
        arp.beginBlock (r.settings, t, 512);
        arp.noteOn (0, 60, 100, 1);
        arp.noteOn (0, 64, 100, 1);
        arp.endBlock();
        CHECK (arp.lastStrumRequested() == 12000);
        CHECK (arp.lastStrumSpread() == 6000 - 48);
    }
    {
        // SWING 100 %: an odd step plays half a step late, so the even step before it has more
        // room and the odd one less; neither strum may overlap the next.
        Run r;
        r.settings = strumChord (200.0);
        r.settings.rate = ArpRate::sixteenth;
        r.settings.swing = 1.0;
        r.inputs = chord (0, { 60, 64, 67 });
        r.length = 48000 * 2;
        check (r, 3);
    }
    {
        // ARP RATE modulated up to 2x mid-phrase.
        Run r;
        r.settings = strumChord (100.0);
        r.settings.rate = ArpRate::eighth;
        r.inputs = chord (0, { 60, 64, 67 });
        r.length = 48000 * 2;
        r.perBlock = [] (Arpeggiator&, Arpeggiator::Settings& s, double&, std::int64_t now) { s.rateMultiplier = now < 24000 ? 1.0 : 2.0; };
        check (r, 3);
    }
}

TEST_CASE ("STRUM: each note's gate runs from its own onset; a pitch still sounding ends before it plays again", "[unit][arp][strum]")
{
    Run r;
    r.settings = strumChord (30.0);
    r.settings.gate = 0.5;   // quarter at 120 BPM = 24000 samples: 12000 each
    r.inputs = chord (0, { 60, 64, 67, 71 });
    r.length = 48000 * 2;
    const auto e = r.render();
    std::map<int, std::int64_t> onAt;
    int checked = 0;
    for (const auto& ev : e)
    {
        if (ev.kind == Kind::noteOn)
            onAt[ev.note] = ev.time;
        else if (onAt.count (ev.note))
        {
            CHECK (ev.time - onAt[ev.note] == 12000);
            ++checked;
        }
    }
    CHECK (checked >= 8);
    CHECK (balanced (e));

    // Legato (gate 150 %): the next step's same pitches end first, never doubled.
    Run legato;
    legato.settings = strumChord (40.0, StrumDirection::alternate);
    legato.settings.gate = 1.5;
    legato.settings.octaves = 2;
    legato.inputs = chord (0, { 60, 64, 67 });
    for (const int n : { 60, 64, 67 })
        legato.inputs.push_back ({ 48000 * 2, In::T::off, n, 0 });   // released: the last gates run out before the end
    legato.length = 48000 * 3;
    CHECK (balanced (legato.render()));
}

TEST_CASE ("STRUM: no waiting note survives a release, the pedal's end, All Notes Off, a restart, a jump or ARP off", "[unit][arp][strum]")
{
    auto settings = strumChord (100.0);   // a wide strum: 4800 samples
    {
        // A key released before its turn: its attack never comes; the others still play.
        Run r;
        r.settings = settings;
        r.inputs = chord (0, { 60, 64, 67, 71 });
        r.inputs.push_back ({ 2000, In::T::off, 67, 0 });   // E plays at 1600, G was due at 3200
        r.length = 12000;
        const auto o = ons (r.render());
        std::vector<int> played;
        for (const auto& e : o)
            played.push_back (e.note);
        CHECK (played == std::vector<int> { 60, 64, 71 });
    }
    {
        // Kept by the pedal: it still plays.
        Run r;
        r.settings = settings;
        r.inputs = chord (0, { 60, 64, 67, 71 });
        r.inputs.push_back ({ 500, In::T::pedalDown, 0, 0 });
        r.inputs.push_back ({ 2000, In::T::off, 67, 0 });
        r.length = 12000;
        CHECK (ons (r.render()).size() == 4);
    }
    {
        // The pedal lifted after the key: the waiting attack goes with it.
        Run r;
        r.settings = settings;
        r.inputs = chord (0, { 60, 64, 67, 71 });
        r.inputs.push_back ({ 100, In::T::pedalDown, 0, 0 });
        r.inputs.push_back ({ 200, In::T::off, 71, 0 });
        r.inputs.push_back ({ 2000, In::T::pedalUp, 0, 0 });   // B (due 4800) is no longer held
        r.length = 12000;
        std::vector<int> played;
        for (const auto& e : ons (r.render()))
            played.push_back (e.note);
        CHECK (played == std::vector<int> { 60, 64 , 67 });
    }
    // Everything released, All Notes Off, ARP off, a transport restart, a host jump: nothing more starts.
    struct Stop
    {
        const char* what;
        std::function<void (Run&)> apply;
    };
    const std::vector<Stop> stops {
        { "all released", [] (Run& r) { for (const int n : { 60, 64, 67, 71 }) r.inputs.push_back ({ 2000, In::T::off, n, 0 }); } },
        { "All Notes Off", [] (Run& r) { r.inputs.push_back ({ 2000, In::T::allOff, 0, 0 }); } },
        { "ARP off", [] (Run& r) { r.perBlock = [] (Arpeggiator&, Arpeggiator::Settings& s, double&, std::int64_t now) { if (now >= 2048) s.enabled = false; }; } },
        { "STRUM off", [] (Run& r) { r.perBlock = [] (Arpeggiator&, Arpeggiator::Settings& s, double&, std::int64_t now) { if (now >= 2048) s.strum = false; }; } },
        { "CHORD left", [] (Run& r) { r.perBlock = [] (Arpeggiator&, Arpeggiator::Settings& s, double&, std::int64_t now) { if (now >= 2048) s.pattern = ArpPattern::up; }; } },
        { "transport restart", [] (Run& r) { r.perBlock = [] (Arpeggiator& a, Arpeggiator::Settings&, double&, std::int64_t now) { if (now == 2048) a.restartPattern(); }; } },
        { "host jump", [] (Run& r) {
              r.host = true;
              r.perBlock = [] (Arpeggiator&, Arpeggiator::Settings&, double& ppq, std::int64_t now) { if (now == 2048) ppq += 8.0; };
          } },
    };
    for (const auto& stop : stops)
    {
        INFO (stop.what);
        Run r;
        r.settings = settings;
        r.block = 256;
        r.inputs = chord (0, { 60, 64, 67, 71 });
        r.length = 8000;   // past the cancelled notes' times (3200, 4800), before the next quarter step (24000)
        stop.apply (r);
        const auto e = r.render();
        // The chord's first two notes played (0, 1600); its last two (due 3200, 4800) never do.
        // (ARP off hands the held keys back at the stop; a restart starts a new phrase there:
        // both are notes at the stop itself, not the old strum's.)
        for (const auto& ev : ons (e))
        {
            CHECK (ev.time != 3200);
            CHECK (ev.time != 4800);
        }
        int beforeStop = 0;
        for (const auto& ev : ons (e))
            beforeStop += ev.time < 2048 ? 1 : 0;
        CHECK (beforeStop == 2);
    }
    // And over a long run with all of it: never more waiting than a chord, nothing left hanging.
    Run r;
    r.settings = strumChord (60.0, StrumDirection::random);
    r.settings.gate = 1.2;
    r.settings.octaves = 3;
    r.length = 48000 * 6;
    for (int k = 0; k < 10; ++k)
    {
        const std::int64_t t = 13000 * k;
        for (const int n : { 48 + k, 55 + k, 60 + k, 64 + k })
        {
            r.inputs.push_back ({ t, In::T::on, n, 100 });
            r.inputs.push_back ({ t + 9000, In::T::off, n, 0 });
        }
    }
    const auto e = r.render();
    CHECK (r.maxPendingSeen <= Arpeggiator::maxHeld);
    CHECK (balanced (e));
}

TEST_CASE ("STRUM: high notes and CHORD's octave climb stay inside MIDI", "[unit][arp][strum]")
{
    Run r;
    r.settings = strumChord (30.0);
    r.settings.octaves = 4;
    r.settings.rate = ArpRate::eighth;
    r.inputs = chord (0, { 100, 112, 120 });
    r.length = 48000 * 2;
    const auto e = r.render();
    for (const auto& ev : e)
        CHECK (ev.note <= 127);
    CHECK (balanced (e));
}
