#pragma once

#include "ShapingPopups.h"

#include <functional>
#include <memory>
#include <vector>

namespace osp::plugin
{

/**
    The keyboard row's wheels' settings (shared by ANDOR OSP and every product built from the
    ANDOR Instrument Shell): a click on PITCH or MOD - the captions under the wheels, doors
    as a macro's name is - opens their popover above the caption.

    PITCH BEND: RANGE (bendRange, the parameter ANDOR OSP always had: its ID, range and
    automation unchanged), +-N semitones, a picture of the wheel's travel.
    MOD WHEEL: its VIBRATO (Vibrato.h) - ON, DEPTH, RATE, DELAY, RISE, CONTROL (MOD WHEEL /
    ALWAYS ON / ASSIGNED), AMOUNT (ASSIGNED's) and the sources that take part, with a
    picture of one note's pitch over time as the settings make it.
*/
struct VibratoSource
{
    juce::String label;         ///< "A", "OSC 1"
    juce::String parameterId;   ///< its bool parameter (vibrato.sourceA ...)
};

/** ANDOR OSP's sources: the three layers. */
std::vector<VibratoSource> defaultVibratoSources();

std::unique_ptr<MiniPanel> createPitchBendPopup (InstrumentHost& host);
/** `wheel`: where the mod wheel is now (0..1), for the picture while CONTROL is MOD WHEEL. */
std::unique_ptr<MiniPanel> createModWheelPopup (InstrumentHost& host, std::vector<VibratoSource> sources, std::function<float()> wheel);

/** The pitch bend popover's help (the specification's words), for tests. */
extern const char* const pitchBendHelp;

} // namespace osp::plugin
