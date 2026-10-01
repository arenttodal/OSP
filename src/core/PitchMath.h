#pragma once

#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace osp
{

/** MIDI note 69 = A4 = 440 Hz. MIDI note 60 = C4 (middle C, "Yamaha/Logic" octave naming). */
inline double midiToHz (double midiNote, double a4Hz = 440.0) noexcept
{
    return a4Hz * std::exp2 ((midiNote - 69.0) / 12.0);
}

inline double hzToMidi (double hz, double a4Hz = 440.0) noexcept
{
    return 69.0 + 12.0 * std::log2 (hz / a4Hz);
}

inline double semitonesToRatio (double semitones) noexcept { return std::exp2 (semitones / 12.0); }
inline double ratioToSemitones (double ratio) noexcept { return 12.0 * std::log2 (ratio); }
inline double centsToRatio (double cents) noexcept { return std::exp2 (cents / 1200.0); }
inline double ratioToCents (double ratio) noexcept { return 1200.0 * std::log2 (ratio); }

/** Signed cents from reference to measured frequency. */
inline double centsBetween (double referenceHz, double measuredHz) noexcept
{
    return ratioToCents (measuredHz / referenceHz);
}

inline double gainToDb (double gain, double floorDb = -200.0) noexcept
{
    return gain > 0.0 ? std::fmax (20.0 * std::log10 (gain), floorDb) : floorDb;
}

inline double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }

inline double powerToDb (double power, double floorDb = -200.0) noexcept
{
    return power > 0.0 ? std::fmax (10.0 * std::log10 (power), floorDb) : floorDb;
}

/** "C4", "F#3", "A-1". Uses sharps. */
std::string midiNoteName (int midiNote);

/**
    Parses a note specification: a note name ("C4", "F#3", "Bb2", "c#-1") or a numeric
    MIDI value ("60", "57.25"). Returns std::nullopt if the text is not understood.
*/
std::optional<double> parseNoteSpec (std::string_view text);

} // namespace osp
