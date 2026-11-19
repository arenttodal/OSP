#pragma once

#include "model/ArpPatternModel.h"

#include <juce_core/juce_core.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace osp
{

/**
    Custom ARP pattern files (docs/research/custom-arp, docs/analysis-schema.md): product-neutral
    JSON, the same in ANDOR OSP and every ANDOR shell product, in a pattern library file
    (`.andorpattern`), embedded in a session's or preset's state, or in a pack.

    { "schemaVersion": 1, "kind": "andor.arp-pattern", "id", "name", "description", "factory",
      "pattern": { "mapping": "contour" | "degrees", "maxRank", "events": [ [rank|-1, ticks, velocity?] ] },
      "interpretation": { "timing": "rhythm" | "steps", "quantumTicks", "stepQuarters",
                          "lengthQuarters", "chord": "highest" | "lowest" },
      "capture": { "bpm", "notes": [ [onsetQuarters, durationQuarters, pitch, velocity] ] } }

    Ticks are arp::ticksPerStep (960) per step. A file from a newer schema is refused rather than
    misread; unknown fields are ignored.
*/
juce::var arpPatternToJson (const arppattern::Document& document);
std::optional<arppattern::Document> arpPatternFromJson (const juce::var& json, std::string& error);

std::string arpPatternToString (const arppattern::Document& document);
std::optional<arppattern::Document> arpPatternFromString (const std::string& text, std::string& error);

bool writeArpPattern (const std::filesystem::path& path, const arppattern::Document& document, std::string& error);
std::optional<arppattern::Document> readArpPattern (const std::filesystem::path& path, std::string& error);

/** A new stable id (a UUID: on the message thread, never in a render or on the audio thread). */
std::string newArpPatternId();

inline constexpr const char* arpPatternExtension = ".andorpattern";

/**
    One track or channel of a Standard MIDI File, as notes in quarter notes from the file's start
    (the tempo map applied; PPQ positions; note-on velocity 0 ends a note; overlapping notes of one
    pitch end in order). Non-note events are ignored. Parse off the audio thread.
*/
struct MidiPhrase
{
    std::string name;    ///< "Track 2 · Lead", "Track 1 · channel 10"
    int track = 0, channel = 1;
    std::vector<arppattern::CapturedNote> notes;
    double bpm = 120.0;  ///< the tempo at the phrase's first note
};
/** Every track (and, within a track, every channel) that has notes. */
std::vector<MidiPhrase> readMidiPhrases (const std::filesystem::path& path, std::string& error);

} // namespace osp
