#pragma once

#include "ShapingPopups.h"

namespace osp::plugin
{

// ANDOR OSP's macro popovers (LIFE, DRIVE, CHARACTER, MOVEMENT, SPACE, ECHO) and its REIMAGINED
// popover, shared so a sister product shows exactly the same ones. They need only the
// InstrumentHost (its parameters, the shaper's custom pattern and a few live displays).

/** Which macro a popup belongs to (the order of the macro row). */
enum class MacroPopup { life, drive, character, movement, space, echo };

/** The parameters behind a macro's popup (the "customised" dot compares them to their defaults). */
const juce::StringArray& popupParameterIds (MacroPopup macro);
/** The macro popups (MacroPopups.cpp): each with its own visualisation of what it does. */
std::unique_ptr<MiniPanel> createMacroPopup (MacroPopup macro, InstrumentHost& host);
/** REIMAGINED (one per layer): the mode, its picture and its own two or three settings. */
std::unique_ptr<MiniPanel> createReimaginedPopup (InstrumentHost& host, int layer);

} // namespace osp::plugin
