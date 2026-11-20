// andor_arp_core on its own: it builds and plays without JUCE or anything else from OSP.

#include "PatternCorpus.h"

#include "engine/Arpeggiator.h"
#include "engine/PatternCapture.h"
#include "model/ArpCoreVersion.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace osp;

TEST_CASE ("ARP core: a version of its own", "[unit][arp-core]")
{
    CHECK (std::string (arp::coreVersion)
           == std::to_string (arp::coreVersionMajor) + "." + std::to_string (arp::coreVersionMinor) + "." + std::to_string (arp::coreVersionPatch));
}

TEST_CASE ("ARP core: the 18 styles and every factory pattern play on their own", "[unit][arp-core]")
{
    for (int p = 0; p <= arp::patternCount; ++p)
    {
        Arpeggiator a;
        a.prepare (44100.0);
        Arpeggiator::Settings s;
        s.enabled = true;
        const bool custom = p == arp::patternCount;
        s.pattern = custom ? ArpPattern::up : static_cast<ArpPattern> (p);
        s.custom = custom;
        if (custom)
            a.setCustomPattern (arppattern::compile (arppattern::factoryPatterns()[2].pattern));
        HostTiming t;
        t.valid = t.playing = true;
        t.bpm = 128.0;
        int ons = 0, offs = 0;
        for (int b = 0; b < 400; ++b)
        {
            t.ppq = b * 512 / (44100.0 * 60.0 / t.bpm);
            a.beginBlock (s, t, 512);
            if (b == 0)
                for (int n : { 60, 64, 67 })
                    a.noteOn (0, n, 90, 1);
            if (b == 300)
                for (int n : { 60, 64, 67 })
                    a.noteOff (0, n, 1);
            a.endBlock();
            for (int i = 0; i < a.numEvents(); ++i)
                (a.event (i).kind == Arpeggiator::Event::Kind::noteOn ? ons : offs) += 1;
        }
        INFO ("style " << p);
        CHECK (ons >= 12);   // 3.5 s of 1/8 at 128 BPM: about 15 steps
        CHECK (ons == offs);   // every note ends: nothing hangs after the keys are released
        CHECK (a.soundingCount() == 0);
    }
}

TEST_CASE ("ARP core: Capture turns played notes into a pattern without a host framework", "[unit][arp-core]")
{
    PatternCapture capture;
    capture.start (48000.0);
    const int step = 6000;   // a sixteenth at 120 BPM
    int clock = 0;
    for (int n : { 60, 62, 64, 67 })
    {
        capture.push (0, n, 100, 120.0);
        capture.advance (step / 2);
        capture.push (0, n, 0, 120.0);
        capture.advance (step / 2);
        clock += step;
    }
    capture.stop();
    capture.poll();
    REQUIRE (capture.notes().size() == 4);
    const auto pattern = arppattern::interpret (capture.notes(), {});
    CHECK (pattern.valid());
    CHECK (pattern.notes() == 4);
    CHECK (pattern.highestRank() == 3);
}
