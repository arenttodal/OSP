#pragma once

#include "midi/MidiEvent.h"

#include <optional>
#include <string>
#include <vector>

namespace osp::fixtures
{

/**
    Standard test sequences (docs/testing.md). All are defined relative to a reference
    note so they can be rendered at the source's detected root; the static .mid files in
    research/midi/ are the same sequences written with reference C4 (60).

    THESE DEFINITIONS ARE FROZEN. Changing them invalidates every comparison against
    earlier renders; add a new fixture instead.
*/

inline constexpr int fixtureVersion = 1;

MidiSequence repetition (int reference);         ///< 8 x reference, velocity 90
MidiSequence dynamics (int reference);           ///< reference at 20 40 60 80 100 127
MidiSequence registerSweep (int reference);      ///< reference -24 -12 0 +12 +24
MidiSequence melody (int reference);             ///< steps, thirds, 4th/5th, repetition, velocity variation
MidiSequence chords (int reference);             ///< major, minor, sus4 (close) + major, minor (wide)
MidiSequence longHold (int reference);           ///< 60 s single note
MidiSequence longChord (int reference);          ///< 60 s four-note chord
MidiSequence repeatedSustains (int reference);   ///< 5-10 s notes with short pauses

/** All fixture names, in canonical order. */
const std::vector<std::string>& allNames();

/** Builds a fixture by name ("repetition", "register", "long-hold", ...). */
std::optional<MidiSequence> byName (const std::string& name, int reference);

/** Test profiles: quick, standard, sustain, full. Returns nullopt for an unknown profile. */
std::optional<std::vector<std::string>> profile (const std::string& name);

} // namespace osp::fixtures
