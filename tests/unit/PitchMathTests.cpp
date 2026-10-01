#include "core/PitchMath.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace osp;

TEST_CASE ("MIDI note <-> frequency", "[unit][pitch]")
{
    CHECK (midiToHz (69) == Approx (440.0));
    CHECK (midiToHz (60) == Approx (261.6255653).epsilon (1e-9));
    CHECK (midiToHz (57) == Approx (220.0));
    CHECK (midiToHz (81) == Approx (880.0));
    CHECK (midiToHz (21) == Approx (27.5));
    CHECK (midiToHz (69, 442.0) == Approx (442.0));

    for (double note = 0.0; note <= 127.0; note += 0.37)
        CHECK (hzToMidi (midiToHz (note)) == Approx (note).margin (1e-9));
}

TEST_CASE ("cents and ratio conversions", "[unit][pitch]")
{
    CHECK (centsToRatio (1200.0) == Approx (2.0));
    CHECK (centsToRatio (-1200.0) == Approx (0.5));
    CHECK (ratioToCents (2.0) == Approx (1200.0));
    CHECK (ratioToCents (std::exp2 (1.0 / 12.0)) == Approx (100.0));
    CHECK (semitonesToRatio (12.0) == Approx (2.0));
    CHECK (semitonesToRatio (-24.0) == Approx (0.25));
    CHECK (ratioToSemitones (1.5) == Approx (7.01955).margin (1e-4));
    CHECK (centsBetween (440.0, 880.0) == Approx (1200.0));
    CHECK (centsBetween (440.0, 440.0 * centsToRatio (13.0)) == Approx (13.0));
    CHECK (gainToDb (1.0) == Approx (0.0));
    CHECK (gainToDb (0.5) == Approx (-6.0206).margin (1e-4));
    CHECK (gainToDb (0.0) == Approx (-200.0));
    CHECK (dbToGain (-6.0206) == Approx (0.5).margin (1e-5));
}

TEST_CASE ("note names and note parsing", "[unit][pitch]")
{
    CHECK (midiNoteName (60) == "C4");
    CHECK (midiNoteName (69) == "A4");
    CHECK (midiNoteName (61) == "C#4");
    CHECK (midiNoteName (0) == "C-1");
    CHECK (midiNoteName (127) == "G9");

    CHECK (parseNoteSpec ("C4").value() == Approx (60));
    CHECK (parseNoteSpec ("A3").value() == Approx (57));
    CHECK (parseNoteSpec ("f#2").value() == Approx (42));
    CHECK (parseNoteSpec ("Bb2").value() == Approx (46));
    CHECK (parseNoteSpec ("C-1").value() == Approx (0));
    CHECK (parseNoteSpec (" 57.25 ").value() == Approx (57.25));
    CHECK_FALSE (parseNoteSpec ("H4").has_value());
    CHECK_FALSE (parseNoteSpec ("C").has_value());
    CHECK_FALSE (parseNoteSpec ("abc").has_value());
    CHECK_FALSE (parseNoteSpec ("").has_value());
    CHECK_FALSE (parseNoteSpec ("500").has_value());
}
