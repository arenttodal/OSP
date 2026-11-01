#include "WellOverlay.h"

namespace osp::plugin
{

namespace
{
    constexpr double fadeSeconds = 0.14;       // as the granular controls
    constexpr double exitToleranceSeconds = 0.18;
    constexpr int frameHz = 60;
}

WellOverlay::WellOverlay()
{
    setInterceptsMouseClicks (false, true);
    setWantsKeyboardFocus (false);
    addChildComponent (reveal);
    reveal.setAlpha (0.0f);
    startTimerHz (frameHz);
}

WellOverlay::~WellOverlay()
{
    stopTimer();
}

void WellOverlay::resized()
{
    reveal.setBounds (getLocalBounds());
}

void WellOverlay::paint (juce::Graphics& g)
{
    // Under the revealed controls, a soft dark band so their captions read over the waveform
    // (faded with them).
    if (alpha <= 0.001f)
        return;
    const auto band = revealBand();
    if (band.isEmpty())
        return;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x00101214), 0.0f, band.getY() - 18.0f, juce::Colour (0xd8101214).withMultipliedAlpha (alpha),
                                             0.0f, band.getY() + 14.0f, false));
    g.fillRect (band.withTop (band.getY() - 18.0f));
}

bool WellOverlay::wantsReveal() const
{
    if (forced)
        return forcedShown;
    if (dismissed)
        return false;
    // Keyboard focus inside, or a control being dragged, keeps them: they never vanish mid-gesture.
    if (reveal.hasKeyboardFocus (true))
        return true;
    if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        if (auto* under = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse(); under != nullptr && (under == &reveal || reveal.isParentOf (under)))
            return true;
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    return now - lastInside <= exitToleranceSeconds;
}

void WellOverlay::timerCallback()
{
    if (! isShowing())
        return;
    if (++frames % (frameHz / 10) == 0)
        tick();
    const auto mouse = getMouseXYRelative();
    const bool inside = getLocalBounds().contains (mouse);
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (inside)
        lastInside = now;
    else if (dismissed && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        dismissed = false;   // Escape holds until the pointer has left and comes back
    target = wantsReveal() ? 1.0f : 0.0f;
    if (juce::approximatelyEqual (alpha, target))
        return;
    const float step = static_cast<float> (1.0 / (fadeSeconds * frameHz));
    alpha = target > alpha ? std::min (target, alpha + step) : std::max (target, alpha - step);
    reveal.setVisible (alpha > 0.001f);
    reveal.setAlpha (alpha);
    repaint();
}

bool WellOverlay::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && isRevealed())
    {
        dismissed = true;   // hidden; nothing changed
        if (auto* focused = juce::Component::getCurrentlyFocusedComponent(); focused != nullptr && reveal.isParentOf (focused))
            focused->giveAwayKeyboardFocus();
        return true;
    }
    return false;
}

void WellOverlay::setRevealedForTest (bool shown)
{
    forced = true;
    forcedShown = shown;
    tick();   // (as the timer would: inactive controls dimmed)
    target = alpha = shown ? 1.0f : 0.0f;
    reveal.setVisible (shown);
    reveal.setAlpha (alpha);
    repaint();
}

} // namespace osp::plugin
