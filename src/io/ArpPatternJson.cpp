#include "io/ArpPatternJson.h"

#include "io/JsonUtil.h"
#include "model/ArpPatternCodec.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace osp
{

namespace
{
    // The document's meaning lives in model/ArpPatternCodec (pure C++, shared with ANDOR ARP's Max
    // external and plugin). Here juce::var is only translated, so OSP's text stays JUCE's.
    juce::var toVar (const arppattern::JsonValue& v)
    {
        using T = arppattern::JsonValue::Type;
        switch (v.type)
        {
            case T::null:    return {};
            case T::boolean: return v.boolean;
            case T::number:
                if (v.integer && std::abs (v.number) <= 2147483647.0)
                    return static_cast<int> (v.number);
                if (v.integer && std::abs (v.number) < 9.0e18)
                    return static_cast<juce::int64> (v.number);
                return v.number;
            case T::string:  return juce::String::fromUTF8 (v.text.c_str());
            case T::array:
            {
                juce::Array<juce::var> items;
                for (const auto& i : v.items)
                    items.add (toVar (i));
                return items;
            }
            case T::object:
            {
                auto object = json::object();
                for (const auto& [key, value] : v.members)
                    object.getDynamicObject()->setProperty (juce::Identifier (juce::String::fromUTF8 (key.c_str())), toVar (value));
                return object;
            }
        }
        return {};
    }

    arppattern::JsonValue fromVar (const juce::var& v)
    {
        using J = arppattern::JsonValue;
        if (v.isBool())
            return J::makeBool (static_cast<bool> (v));
        if (v.isInt() || v.isInt64())
            return J::makeInt (static_cast<juce::int64> (v));
        if (v.isDouble())
            return J::makeDouble (static_cast<double> (v));
        if (v.isString())
            return J::makeString (v.toString().toStdString());
        if (const auto* items = v.getArray())
        {
            auto out = J::makeArray();
            for (const auto& i : *items)
                out.append (fromVar (i));
            return out;
        }
        if (auto* object = v.getDynamicObject())
        {
            auto out = J::makeObject();
            for (const auto& p : object->getProperties())
                out.members.emplace_back (p.name.toString().toStdString(), fromVar (p.value));
            return out;
        }
        return {};
    }
}

juce::var arpPatternToJson (const arppattern::Document& d)
{
    return toVar (arppattern::documentToJson (d));
}

std::optional<arppattern::Document> arpPatternFromJson (const juce::var& root, std::string& error)
{
    return arppattern::documentFromJson (fromVar (root), error);
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
    juce::File file (path.string());
    juce::FileInputStream in (file);
    if (! in.openedOk())
    {
        error = "cannot open " + path.filename().string();
        return phrases;
    }
    juce::MidiFile midi;
    int fileType = 0;
    if (! midi.readFrom (in, true, &fileType))
    {
        error = path.filename().string() + " is not a Standard MIDI File";
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
        error = path.filename().string() + " has no notes";
    return phrases;
}

} // namespace osp
