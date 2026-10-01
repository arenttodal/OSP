#pragma once

#include <string>
#include <vector>

namespace osp
{

/** Minimal timestamped MIDI event used by the renderer and fixtures. */
struct MidiEvent
{
    enum class Type
    {
        noteOn,
        noteOff,
        sustainPedal,  ///< value >= 64 = down
        allNotesOff
    };

    double timeSeconds = 0.0;
    Type type = Type::noteOn;
    int note = 60;
    int value = 100;   ///< velocity (1..127) or controller value
    int channel = 1;
};

/** A named, time-ordered event list. */
struct MidiSequence
{
    std::string name;
    std::vector<MidiEvent> events;

    double endTimeSeconds() const noexcept
    {
        return events.empty() ? 0.0 : events.back().timeSeconds;
    }

    /** Stable sort by time; at equal times note-offs/pedal-ups come before note-ons. */
    void sort();
};

} // namespace osp
