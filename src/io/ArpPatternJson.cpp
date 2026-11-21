#include "io/ArpPatternJson.h"

#include "io/JsonUtil.h"
#include "core/PathText.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <map>

namespace osp
{

namespace
{
    constexpr const char* kind = "andor.arp-pattern";

    const char* mappingId (arp::CustomMapping m) { return m == arp::CustomMapping::degrees ? "degrees" : "contour"; }
    arp::CustomMapping mappingFrom (const std::string& s) { return s == "degrees" ? arp::CustomMapping::degrees : arp::CustomMapping::contour; }

    juce::var number (double v) { return json::number (v, 6); }
}

juce::var arpPatternToJson (const arppattern::Document& d)
{
    auto root = json::object();
    json::set (root, "schemaVersion", arppattern::Document::schemaVersion);
    json::set (root, "kind", kind);
    json::set (root, "id", json::str (d.id));
    json::set (root, "name", json::str (d.name));
    json::set (root, "description", json::str (d.description));
    json::set (root, "factory", d.factory);

    auto pattern = json::object();
    json::set (pattern, "mapping", mappingId (d.pattern.mapping));
    json::set (pattern, "maxRank", d.pattern.maxRank);
    auto events = json::array();
    for (const auto& e : d.pattern.events)
    {
        juce::Array<juce::var> item { e.rest ? -1 : e.rank, e.duration };
        if (e.velocity > 0)
            item.add (e.velocity);
        events.append (juce::var (item));
    }
    json::set (pattern, "events", events);
    json::set (root, "pattern", pattern);

    const auto& in = d.interpretation;
    auto how = json::object();
    json::set (how, "timing", in.timing == arppattern::Interpretation::Timing::steps ? "steps" : "rhythm");
    json::set (how, "quantumTicks", in.quantumTicks);
    json::set (how, "stepQuarters", number (in.stepQuarters));
    json::set (how, "lengthQuarters", number (in.lengthQuarters));
    json::set (how, "chord", in.chord == arppattern::ChordPolicy::lowest ? "lowest" : "highest");
    json::set (how, "mapping", mappingId (in.mapping));
    json::set (root, "interpretation", how);

    auto capture = json::object();
    json::set (capture, "bpm", number (d.captureBpm));
    auto notes = json::array();
    for (const auto& n : d.capture)
        notes.append (juce::var (juce::Array<juce::var> { number (n.onset), number (n.duration), n.pitch, n.velocity }));
    json::set (capture, "notes", notes);
    json::set (root, "capture", capture);
    return root;
}

std::optional<arppattern::Document> arpPatternFromJson (const juce::var& root, std::string& error)
{
    if (! root.isObject())
    {
        error = "not a pattern (no JSON object)";
        return std::nullopt;
    }
    const int version = json::getInt (root, "schemaVersion", 0);
    if (version < 1 || version > arppattern::Document::schemaVersion)
    {
        error = version > arppattern::Document::schemaVersion ? "made by a newer version (pattern schema " + std::to_string (version) + ")"
                                                              : "not a pattern (no schemaVersion)";
        return std::nullopt;
    }
    if (json::getString (root, "kind") != kind)
    {
        error = "not an ANDOR ARP pattern";
        return std::nullopt;
    }
    arppattern::Document d;
    d.id = json::getString (root, "id");
    d.name = json::getString (root, "name");
    d.description = json::getString (root, "description");
    d.factory = json::getBool (root, "factory", false);

    const auto& pattern = root["pattern"];
    d.pattern.mapping = mappingFrom (json::getString (pattern, "mapping", "contour"));
    d.pattern.maxRank = std::clamp (json::getInt (pattern, "maxRank", 0), 0, arppattern::maxRankLimit);
    if (const auto* events = pattern["events"].getArray())
        for (const auto& e : *events)
        {
            const auto* item = e.getArray();
            if (item == nullptr || item->size() < 2)
            {
                error = "a pattern event is not [rank, ticks]";
                return std::nullopt;
            }
            const int rank = static_cast<int> ((*item)[0]);
            arppattern::Event ev { std::max (0, rank), rank < 0, static_cast<int> ((*item)[1]), item->size() > 2 ? static_cast<int> ((*item)[2]) : 0 };
            d.pattern.events.push_back (ev);
        }
    if (! d.pattern.valid())
    {
        error = "the pattern is outside the limits (1-256 events, 1-128 steps, each at least 1/32 step, one note at least)";
        return std::nullopt;
    }

    const auto& how = root["interpretation"];
    auto& in = d.interpretation;
    in.timing = json::getString (how, "timing", "rhythm") == "steps" ? arppattern::Interpretation::Timing::steps : arppattern::Interpretation::Timing::rhythm;
    in.quantumTicks = std::clamp (json::getInt (how, "quantumTicks", in.quantumTicks), arp::minEventTicks, arp::ticksPerStep);
    in.stepQuarters = std::clamp (json::getDouble (how, "stepQuarters", in.stepQuarters), 1.0 / 64.0, 4.0);
    in.lengthQuarters = std::clamp (json::getDouble (how, "lengthQuarters", 0.0), 0.0, 4.0 * arp::maxPatternSteps);
    in.chord = json::getString (how, "chord", "highest") == "lowest" ? arppattern::ChordPolicy::lowest : arppattern::ChordPolicy::highest;
    in.mapping = mappingFrom (json::getString (how, "mapping", mappingId (d.pattern.mapping)));

    const auto& capture = root["capture"];
    d.captureBpm = std::clamp (json::getDouble (capture, "bpm", 120.0), 5.0, 1000.0);
    if (const auto* notes = capture["notes"].getArray())
        for (const auto& n : *notes)
            if (const auto* item = n.getArray(); item != nullptr && item->size() >= 4 && static_cast<int> (d.capture.size()) < arppattern::maxCaptureNotes)
                d.capture.push_back ({ static_cast<double> ((*item)[0]), static_cast<double> ((*item)[1]), std::clamp (static_cast<int> ((*item)[2]), 0, 127),
                                       std::clamp (static_cast<int> ((*item)[3]), 1, 127) });
    return d;
}

std::string arpPatternToString (const arppattern::Document& d) { return json::toString (arpPatternToJson (d)); }

std::optional<arppattern::Document> arpPatternFromString (const std::string& text, std::string& error)
{
    const auto parsed = json::parse (text, error);
    if (! parsed)
        return std::nullopt;
    return arpPatternFromJson (*parsed, error);
}

bool writeArpPattern (const std::filesystem::path& path, const arppattern::Document& d, std::string& error)
{
    return json::writeFile (path, arpPatternToJson (d), error);
}

std::optional<arppattern::Document> readArpPattern (const std::filesystem::path& path, std::string& error)
{
    const auto parsed = json::readFile (path, error);
    if (! parsed)
        return std::nullopt;
    return arpPatternFromJson (*parsed, error);
}

std::string newArpPatternId()
{
    return juce::Uuid().toDashedString().toStdString();
}

std::vector<MidiPhrase> readMidiPhrases (const std::filesystem::path& path, std::string& error)
{
    std::vector<MidiPhrase> phrases;
    juce::File file (osp::utf8Of (path));
    juce::FileInputStream in (file);
    if (! in.openedOk())
    {
        error = "cannot open " + osp::utf8Of (path.filename());
        return phrases;
    }
    juce::MidiFile midi;
    int fileType = 0;
    if (! midi.readFrom (in, true, &fileType))
    {
        error = osp::utf8Of (path.filename()) + " is not a Standard MIDI File";
        return phrases;
    }
    const int ppq = midi.getTimeFormat();
    // Quarters from ticks: PPQ directly; an SMPTE file through seconds at 120 BPM.
    juce::MidiMessageSequence tempos;
    midi.findAllTempoEvents (tempos);
    auto bpmAt = [&tempos] (double tick) {
        double bpm = 120.0;
        for (int i = 0; i < tempos.getNumEvents(); ++i)
        {
            const auto* e = tempos.getEventPointer (i);
            if (e->message.getTimeStamp() > tick)
                break;
            if (const double spq = e->message.getTempoSecondsPerQuarterNote(); spq > 0.0)
                bpm = 60.0 / spq;
        }
        return std::clamp (bpm, 5.0, 1000.0);
    };
    if (ppq <= 0)
        midi.convertTimestampTicksToSeconds();
    auto toQuarters = [ppq] (double t) { return ppq > 0 ? t / ppq : t * 2.0; };

    for (int track = 0; track < midi.getNumTracks(); ++track)
    {
        juce::MidiMessageSequence seq (*midi.getTrack (track));
        seq.updateMatchedPairs();
        juce::String trackName;
        std::map<int, std::vector<arppattern::CapturedNote>> byChannel;
        std::map<int, double> firstTick;
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            const auto* e = seq.getEventPointer (i);
            const auto& m = e->message;
            if (m.isTrackNameEvent() && trackName.isEmpty())
                trackName = m.getTextFromTextMetaEvent();
            if (! m.isNoteOn())   // (a note-on of velocity 0 is a note-off: isNoteOn is false)
                continue;
            const double on = m.getTimeStamp();
            double off = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() : on + (ppq > 0 ? ppq / 4.0 : 0.125);
            off = std::max (off, on);
            auto& list = byChannel[m.getChannel()];
            if (static_cast<int> (list.size()) >= arppattern::maxCaptureNotes)
                continue;
            if (! firstTick.count (m.getChannel()))
                firstTick[m.getChannel()] = on;
            list.push_back ({ toQuarters (on), std::max (1.0 / 64.0, toQuarters (off) - toQuarters (on)), m.getNoteNumber(), std::max (1, static_cast<int> (m.getVelocity())) });
        }
        for (auto& [channel, notes] : byChannel)
        {
            MidiPhrase p;
            p.track = track;
            p.channel = channel;
            p.name = "Track " + std::to_string (track + 1) + (trackName.isNotEmpty() ? " · " + trackName.toStdString() : std::string())
                   + (byChannel.size() > 1 ? " · channel " + std::to_string (channel) : std::string());
            p.bpm = ppq > 0 ? bpmAt (firstTick[channel]) : 120.0;
            std::stable_sort (notes.begin(), notes.end(), [] (const auto& a, const auto& b) { return a.onset < b.onset; });
            p.notes = std::move (notes);
            phrases.push_back (std::move (p));
        }
    }
    if (phrases.empty() && error.empty())
        error = osp::utf8Of (path.filename()) + " has no notes";
    return phrases;
}

} // namespace osp
