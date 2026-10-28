#pragma once

#include "ShapingPopups.h"

namespace osp::plugin
{

class OspAudioProcessor;

// ANDOR OSP's own popover contents (the shared MiniPanel frame and the Advanced popover are
// in shared/andor-ui).

/** Which macro a popup belongs to (the order of the macro row). */
enum class MacroPopup { life, drive, character, movement, space, echo };

/** The parameters behind a macro's popup (the "customised" dot compares them to their defaults). */
const juce::StringArray& popupParameterIds (MacroPopup macro);
/** The macro popups (MacroPopups.cpp): each with its own visualisation of what it does. */
std::unique_ptr<MiniPanel> createMacroPopup (MacroPopup macro, OspAudioProcessor& processor);
/** REIMAGINED (one per layer): the mode, its picture and its own two or three settings. */
std::unique_ptr<MiniPanel> createReimaginedPopup (OspAudioProcessor& processor, int layer);

} // namespace osp::plugin
