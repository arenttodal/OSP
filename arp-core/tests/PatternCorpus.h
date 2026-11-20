#pragma once

// The pattern corpus every ANDOR product must read the same way (the `.andorpattern` fixtures in
// arp-core/tests/fixtures are these documents as ANDOR OSP's JUCE codec writes them). Pure C++:
// included by andor_arp_core's own tests and by OSP's codec compatibility test.

#include "model/ArpFactoryPatterns.h"
#include "model/ArpPatternModel.h"

#include <string>
#include <vector>

namespace andor_arp_test
{

using namespace osp;

/** File-name stem of a corpus document ("factory.five-climb" -> "factory-five-climb"). */
inline std::string fixtureStem (const arppattern::Document& d)
{
    std::string s = d.id;
    for (auto& c : s)
        if (c == '.' || c == '/' || c == ' ')
            c = '-';
    return s;
}

inline std::vector<arppattern::Document> patternCorpus()
{
    std::vector<arppattern::Document> corpus = arppattern::factoryPatterns();

    // Everything a document can carry: a take (fractional quarters), DEGREES, STEPS timing,
    // lowest-note chords, rests, per-event velocity, subdivisions down to 1/32 step, a name and
    // description that need escaping and non-ASCII text.
    arppattern::Document edge;
    edge.id = "corpus.edge-1";
    edge.name = "Edge \"quoted\" \\ é – ✓ 🎹";
    edge.description = "line one\nline two\ttabbed";
    edge.captureBpm = 97.25;
    edge.capture = { { 0.0, 0.25, 60, 100 }, { 0.333333, 0.166667, 64, 90 }, { 0.5, 0.125, 67, 1 },
                     { 1.0, 1.5, 72, 127 }, { 2.75, 0.03125, 59, 64 } };
    edge.interpretation.timing = arppattern::Interpretation::Timing::steps;
    edge.interpretation.quantumTicks = arp::ticksPerStep / 8;
    // Six decimals: every ANDOR JSON number is saved so (1/3 reloads as 0.333333, an existing
    // property of the format that only moves where a take is re-read, never what plays).
    edge.interpretation.stepQuarters = 0.333333;
    edge.interpretation.lengthQuarters = 3.0;
    edge.interpretation.chord = arppattern::ChordPolicy::lowest;
    edge.interpretation.mapping = arp::CustomMapping::degrees;
    edge.pattern.mapping = arp::CustomMapping::degrees;
    edge.pattern.events = { { 0, false, arp::ticksPerStep, 0 }, { 0, true, arp::ticksPerStep / 2, 0 },
                            { 3, false, arp::minEventTicks, 127 }, { 2, false, arp::ticksPerStep - arp::minEventTicks, 1 },
                            { 0, true, 3 * arp::ticksPerStep, 0 }, { 5, false, 2 * arp::ticksPerStep + 40, 80 } };
    edge.pattern.maxRank = 7;   // R above the highest rank used: CONTOUR's scale is kept as stored
    corpus.push_back (edge);

    // A pattern interpreted from a take (RHYTHM), as Capture makes it.
    arppattern::Document captured;
    captured.id = "corpus.captured-rhythm";
    captured.name = "Captured";
    captured.captureBpm = 123.0;
    captured.capture = { { 0.0, 0.2, 48, 110 }, { 0.25, 0.2, 55, 70 }, { 0.5, 0.45, 52, 90 }, { 1.0, 0.1, 60, 100 },
                         { 1.125, 0.1, 59, 100 }, { 1.75, 0.2, 48, 120 } };
    captured.interpretation.stepQuarters = 0.25;
    captured.pattern = arppattern::interpret (captured.capture, captured.interpretation);
    corpus.push_back (captured);
    return corpus;
}

} // namespace andor_arp_test
