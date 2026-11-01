#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace osp::plugin
{

/**
    Controls inside a source card's dark well, revealed by the pointer (opt-in, a product's:
    ProductInfo::wellOverlay), in the spirit of ANDOR OSP's granular controls: the waveform
    stays where it is, the controls fade in over its lower part while the pointer is in the
    well, and fade out when it leaves.

    Two kinds of children:
    - the *persistent* ones (a model or source dropdown): always shown;
    - the *revealed* ones, added to revealed(): faded in on hover (about 140 ms), kept while
      one has keyboard focus or is being dragged, hidden again a moment after the pointer
      leaves (a small tolerance, so crossing between controls never flickers). Escape hides
      them until the pointer comes back.

    The overlay never takes clicks where it has no child: the card's own display below keeps
    them. Hover is presentation only: never a parameter, never in a preset.
*/
class WellOverlay : public juce::Component, private juce::Timer
{
public:
    WellOverlay();
    ~WellOverlay() override;

    /** The children that fade in and out (lay them out in resized()). */
    juce::Component& revealed() noexcept { return reveal; }
    bool isRevealed() const noexcept { return target > 0.5f; }
    /** Called when the controls should reflect their parameters again (model changed, ...). */
    virtual void refresh() {}
    /** About ten times a second while shown: dim a control a setting makes inactive, ... */
    virtual void tick() {}
    /** The band the revealed controls sit on (this component's coordinates): shaded under
        them while shown. Empty: no shading. */
    virtual juce::Rectangle<float> revealBand() const { return {}; }
    /** Room its persistent controls take at the well's top right (px): a recording's time
        ruler keeps its labels clear of them. */
    virtual int rulerReserve() const { return 0; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Tests: as if the pointer were in (or out of) the well, at once (no fade). */
    void setRevealedForTest (bool shown);

private:
    struct Reveal final : juce::Component
    {
        Reveal() { setInterceptsMouseClicks (false, true); }
    };
    void timerCallback() override;
    bool wantsReveal() const;
    Reveal reveal;
    float alpha = 0.0f, target = 0.0f;
    double lastInside = -1.0e9;
    bool dismissed = false, forced = false, forcedShown = false;
    int frames = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WellOverlay)
};

} // namespace osp::plugin
