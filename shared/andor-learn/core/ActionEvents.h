#pragma once

#include "LessonTypes.h"

/**
    The action IDs the shared interface reports (LearnEvent::actionId). A control reports one
    only from the handler that commits a musician's gesture (a click, a drag, a key), after its
    attachment wrote the parameter: never from a parameter callback, so automation, presets,
    undo and the tutorial's own navigation report nothing.
*/
namespace andor::learn::actions
{
    // The arpeggiator (shared/andor-ui ArpSection).
    inline constexpr const char* arpPanelOpened = "arp.panel.opened";
    inline constexpr const char* arpPanelClosed = "arp.panel.closed";
    inline constexpr const char* arpEnabledChanged = "arp.enabled.changed";
    inline constexpr const char* arpPatternChanged = "arp.pattern.changed";
    inline constexpr const char* arpRateChanged = "arp.rate.changed";
    inline constexpr const char* arpOctavesChanged = "arp.octaves.changed";
    inline constexpr const char* arpCardToggled = "arp.card.toggled";
    inline constexpr const char* arpLinkChanged = "arp.link.changed";
    inline constexpr const char* arpEditSelected = "arp.edit.selected";
    // The shell.
    inline constexpr const char* sourceViewToggled = "sourceView.toggled";
    inline constexpr const char* rackToggled = "rack.toggled";
    /** A control watched for touches (Target::watchTouches): "<targetId>.touched" after a
        press, drag, wheel or double-click on it. */
    inline juce::String touched (const juce::String& targetId) { return targetId + ".touched"; }
} // namespace andor::learn::actions
