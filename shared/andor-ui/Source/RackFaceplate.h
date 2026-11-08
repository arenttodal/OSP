#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace osp::plugin::rack
{

/** The boutique hardware faceplate (opt-in; ANDOR ANALOG's racks; ANDOR OSP's racks never set
    it): a module drawn as a small instrument in the rack's slot - a ceramic chassis with a
    hairline bezel and four screws, an enamel title rail (an LED for what plays, the title and a
    short subtitle, the module's parts at its right), a band of stripes at the body's left (the
    controls start after it) and the graph in a metal bezel on tinted glass. Interface only. */
struct Faceplate
{
    juce::Colour enamel;                    ///< the title rail
    std::array<juce::Colour, 3> stripes;    ///< the motif's bands, outermost first
    juce::Colour glow;                      ///< the LED and lit marks
    juce::Colour glassTop { 0xff1c2528 }, glassBottom { 0xff192124 };
    juce::String subtitle;                  ///< "DYNAMICS PROCESSOR"
};

namespace hardware
{
    // Its measures (reference pixels, module coordinates).
    constexpr int chassisInsetX = 12, chassisTop = 3, chassisBottomGap = 3, motifWidth = 38, motifGap = 14;
    constexpr int contentLeft = chassisInsetX + motifWidth + motifGap, contentRightMargin = chassisInsetX + 12;
    constexpr float chassisRadius = 7.0f;
    inline juce::Rectangle<int> chassis (juce::Rectangle<int> local)
    {
        return local.withTrimmedLeft (chassisInsetX).withTrimmedRight (chassisInsetX).withTrimmedTop (chassisTop).withTrimmedBottom (chassisBottomGap);
    }

    /** Draws the faceplate under a module's children: `titleOffset` is the rail's bottom, `lit`
        the LED, `graph` the glass (its bezel drawn around it; empty: none), the subtitle kept
        left of `subtitleLimit`. */
    void paint (juce::Graphics& g, juce::Rectangle<int> local, int titleOffset, const Faceplate& f, const juce::String& title, bool lit,
                juce::Rectangle<int> graph, float subtitleLimit);

    /** The rail's overflow key: three dots; `action` gets the key (to anchor a menu). */
    class OverflowKey final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit OverflowKey (std::function<void (juce::Component&)> action);
        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    private:
        std::function<void (juce::Component&)> action;
        bool hover = false;
    };
}

} // namespace osp::plugin::rack
