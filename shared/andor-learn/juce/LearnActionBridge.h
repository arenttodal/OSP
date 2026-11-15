#pragma once

#include "ActionEvents.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace andor::learn
{

/**
    Reports the musician's touches on one control (Target::watchTouches) as user events: a
    press released (a click or a drag), a wheel turn, a double-click. Mouse events come from the
    operating system through the control's own window, never from automation or presets; the
    lesson still checks the parameter afterwards (the postcondition), so a touch that changed
    nothing completes nothing. Attached as a mouse listener (the control itself is unchanged).
*/
class TouchWatcher final : public juce::MouseListener
{
public:
    TouchWatcher (juce::String targetId, std::function<void (const LearnEvent&)> sink, std::function<void (bool)> gestureActive = {});
    ~TouchWatcher() override;

    /** Watches `c` (and its children), or nothing; replaces what was watched. */
    void watch (juce::Component* c);
    juce::Component* watched() const noexcept { return component.getComponent(); }
    const juce::String& targetId() const noexcept { return id; }

    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void report();
    juce::String id;
    std::function<void (const LearnEvent&)> emit;
    std::function<void (bool)> pressed;
    juce::Component::SafePointer<juce::Component> component;
};

} // namespace andor::learn
