#include "LearnActionBridge.h"

namespace andor::learn
{

TouchWatcher::TouchWatcher (juce::String targetId, std::function<void (const LearnEvent&)> sink, std::function<void (bool)> gestureActive)
    : id (std::move (targetId)), emit (std::move (sink)), pressed (std::move (gestureActive))
{
}

TouchWatcher::~TouchWatcher()
{
    watch (nullptr);
}

void TouchWatcher::watch (juce::Component* c)
{
    if (c == component.getComponent())
        return;
    if (auto* old = component.getComponent())
        old->removeMouseListener (this);
    component = c;
    if (c != nullptr)
        c->addMouseListener (this, true);
}

void TouchWatcher::report()
{
    if (emit == nullptr)
        return;
    LearnEvent e;
    e.actionId = actions::touched (id);
    e.targetId = id;
    e.origin = Origin::userPointer;
    emit (e);
}

// (The control handles the gesture first: its listeners are told after it, so the value it
// wrote is there when the postcondition reads it.)
void TouchWatcher::mouseDown (const juce::MouseEvent&)
{
    if (pressed != nullptr)
        pressed (true);   // the step's baseline holds still while the knob is held
}

void TouchWatcher::mouseUp (const juce::MouseEvent&)
{
    report();
    if (pressed != nullptr)
        pressed (false);
}
void TouchWatcher::mouseDoubleClick (const juce::MouseEvent&) { report(); }
void TouchWatcher::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) { report(); }

//==============================================================================
ParameterGestureBridge::ParameterGestureBridge (juce::AudioProcessor& p, std::function<void (const LearnEvent&)> sink, std::function<void (bool)> gestureActive)
    : processor (p), emit (std::move (sink)), pressed (std::move (gestureActive))
{
    for (auto* parameter : processor.getParameters())
        parameter->addListener (this);
}

ParameterGestureBridge::~ParameterGestureBridge()
{
    for (auto* parameter : processor.getParameters())
        parameter->removeListener (this);
}

void ParameterGestureBridge::parameterGestureChanged (int parameterIndex, bool starting)
{
    // (A host thread never begins a plugin's gesture; only the editor's controls do.)
    if (! enabled || ! juce::MessageManager::existsAndIsCurrentThread())
        return;
    const auto& all = processor.getParameters();
    if (parameterIndex < 0 || parameterIndex >= all.size())
        return;
    auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (all[parameterIndex]);
    if (withId == nullptr)
        return;
    if (starting)
    {
        if (open++ == 0 && pressed != nullptr)
            pressed (true);
        return;
    }
    LearnEvent e;
    e.actionId = actions::parameterEdited (withId->paramID);
    e.parameterId = withId->paramID;
    e.origin = Origin::userPointer;
    if (emit != nullptr)
        emit (e);
    open = std::max (0, open - 1);
    if (open == 0 && pressed != nullptr)
        pressed (false);
}

} // namespace andor::learn
