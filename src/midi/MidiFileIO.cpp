#include "midi/MidiFileIO.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

namespace osp::io
{

namespace
{
    juce::File toJuceFile (const std::filesystem::path& path)
    {
        std::error_code ec;
        const auto absolute = std::filesystem::absolute (path, ec);
        return juce::File (juce::String::fromUTF8 ((ec ? path : absolute).string().c_str()));
    }

    constexpr int ticksPerQuarter = 960;
    constexpr double ticksPerSecond = ticksPerQuarter * 2.0; // 120 BPM
}

std::optional<MidiSequence> readMidiFile (const std::filesystem::path& path, std::string& error)
{
    const auto file = toJuceFile (path);
    if (! file.existsAsFile())
    {
        error = "MIDI file not found: " + path.string();
        return std::nullopt;
    }

    juce::FileInputStream stream (file);
    juce::MidiFile midi;
    if (! stream.openedOk() || ! midi.readFrom (stream))
    {
        error = "could not parse MIDI file: " + path.string();
        return std::nullopt;
    }
    if (midi.getTimeFormat() <= 0)
    {
        error = "SMPTE-timed MIDI files are not supported: " + path.string();
        return std::nullopt;
    }
    midi.convertTimestampTicksToSeconds();

    MidiSequence sequence;
    sequence.name = path.stem().string();

    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        const auto* track = midi.getTrack (t);
        for (int i = 0; i < track->getNumEvents(); ++i)
        {
            const auto& m = track->getEventPointer (i)->message;
            const double time = m.getTimeStamp();
            if (m.isNoteOn())
                sequence.events.push_back ({ time, MidiEvent::Type::noteOn, m.getNoteNumber(), m.getVelocity(), m.getChannel() });
            else if (m.isNoteOff())
                sequence.events.push_back ({ time, MidiEvent::Type::noteOff, m.getNoteNumber(), 0, m.getChannel() });
            else if (m.isSustainPedalOn() || m.isSustainPedalOff())
                sequence.events.push_back ({ time, MidiEvent::Type::sustainPedal, 0, m.getControllerValue(), m.getChannel() });
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                sequence.events.push_back ({ time, MidiEvent::Type::allNotesOff, 0, 0, m.getChannel() });
        }
    }

    sequence.sort();
    return sequence;
}

bool writeMidiFile (const std::filesystem::path& path, const MidiSequence& sequence, std::string& error)
{
    juce::MidiMessageSequence tempoTrack;
    tempoTrack.addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0.0);
    tempoTrack.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    auto name = juce::MidiMessage::textMetaEvent (3, juce::String::fromUTF8 (sequence.name.c_str()));
    tempoTrack.addEvent (name, 0.0);

    juce::MidiMessageSequence notes;
    double lastTick = 0.0;
    for (const auto& e : sequence.events)
    {
        const double tick = std::round (e.timeSeconds * ticksPerSecond);
        lastTick = std::max (lastTick, tick);
        const int channel = std::clamp (e.channel, 1, 16);
        switch (e.type)
        {
            case MidiEvent::Type::noteOn:
                notes.addEvent (juce::MidiMessage::noteOn (channel, e.note, static_cast<juce::uint8> (std::clamp (e.value, 1, 127))), tick);
                break;
            case MidiEvent::Type::noteOff:
                notes.addEvent (juce::MidiMessage::noteOff (channel, e.note), tick);
                break;
            case MidiEvent::Type::sustainPedal:
                notes.addEvent (juce::MidiMessage::controllerEvent (channel, 64, std::clamp (e.value, 0, 127)), tick);
                break;
            case MidiEvent::Type::allNotesOff:
                notes.addEvent (juce::MidiMessage::allNotesOff (channel), tick);
                break;
        }
    }
    notes.updateMatchedPairs();
    tempoTrack.addEvent (juce::MidiMessage::endOfTrack(), lastTick);
    notes.addEvent (juce::MidiMessage::endOfTrack(), lastTick);

    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ticksPerQuarter);
    midi.addTrack (tempoTrack);
    midi.addTrack (notes);

    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories (path.parent_path(), ec);

    auto file = toJuceFile (path);
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk() || ! midi.writeTo (out, 1))
    {
        error = "cannot write MIDI file: " + path.string();
        return false;
    }
    return true;
}

} // namespace osp::io
