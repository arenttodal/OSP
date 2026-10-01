#pragma once

#include "midi/MidiEvent.h"

#include <filesystem>
#include <optional>
#include <string>

namespace osp::io
{

/**
    Reads a Standard MIDI File (type 0 or 1) into a time-ordered sequence in seconds
    (tempo map applied, all tracks merged). Note-ons, note-offs, CC64 and CC123 are kept.
*/
std::optional<MidiSequence> readMidiFile (const std::filesystem::path& path, std::string& error);

/** Writes a type-1 SMF at 120 BPM, 960 PPQ (1 tick = 1/1920 s, so fixture timing is exact). */
bool writeMidiFile (const std::filesystem::path& path, const MidiSequence& sequence, std::string& error);

} // namespace osp::io
