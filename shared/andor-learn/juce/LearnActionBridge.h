#pragma once

#include "ActionEvents.h"

#include <juce_audio_processors/juce_audio_processors.h>
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

/**
    Reports the musician's edits of a processor's parameters as user events: a change gesture
    (begin ... end) on a parameter. In a plugin only the editor's own controls begin gestures
    (an attachment wraps every user edit in one); host automation, preset and session recall,
    undo and the processor's own changes set values without one, so they report nothing.
    Gestures are only taken on the message thread. While a gesture runs the lesson's baseline
    holds (`gestureActive`); its end reports actions::parameterEdited (id) with the parameter,
    after which the step's postcondition is checked as for any other event.

    Attach it to a processor whose parameters outlive it (an editor's lifetime).
*/
class ParameterGestureBridge final : private juce::AudioProcessorParameter::Listener
{
public:
    ParameterGestureBridge (juce::AudioProcessor&, std::function<void (const LearnEvent&)> sink, std::function<void (bool)> gestureActive = {});
    ~ParameterGestureBridge() override;
    /** While false (a tutorial preparing its example), gestures are ignored. */
    void setEnabled (bool on) noexcept { enabled = on; }

private:
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int parameterIndex, bool starting) override;
    juce::AudioProcessor& processor;
    std::function<void (const LearnEvent&)> emit;
    std::function<void (bool)> pressed;
    int open = 0;
    bool enabled = true;
};

} // namespace andor::learn
