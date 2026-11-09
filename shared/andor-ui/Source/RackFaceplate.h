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
    /** V3 (opt-in): the faceplate IS the rack's face - its enamel and ceramic reach the rack's
        own edges (the host draws no panel around it: Host::setFullBleed), narrower stripes,
        quieter screws, the rail's parts on one centre line. False: the V2 inset chassis. */
    bool fullBleed = false;
    /** V4 (opt-in): the TAPE modes (ECHO's tape echo, MOVEMENT's wow and flutter) as a light
        reel deck - the knobs left, two reels and the head in the middle, a small display right.
        False: the TAPE modes as every other mode. */
    bool tapeDeck = false;
    /** V5 PATTERNED HARDWARE (opt-in): the whole face is the enamel - its body as well as its
        rail (`enamel`, a step deeper) - with the device's own `motif` drawn behind the controls'
        left in place of the stripes, four small corner bolts, and the cells' text in `ink`
        (they read it from the module: rackui::inkFor). Graphs keep their own dark glass. */
    bool enamelBody = false;
    juce::Colour body;                                      ///< the body's enamel
    juce::Colour ink { 0xfff3eadb }, inkQuiet { 0xb8f3eadb };   ///< captions and values on it
    juce::Colour arc;                                       ///< knob arcs on it (transparent: the module's accent)
    std::function<void (juce::Graphics&, juce::Rectangle<float> zone)> motif;   ///< the device's motif (its zone: the body's left)
};

namespace hardware
{
    // The inset (V2) chassis' measures (reference pixels, module coordinates).
    constexpr int chassisInsetX = 12, chassisTop = 3, chassisBottomGap = 3, motifWidth = 38, motifGap = 14;
    constexpr int contentLeft = chassisInsetX + motifWidth + motifGap, contentRightMargin = chassisInsetX + 12;
    constexpr float chassisRadius = 7.0f;
    inline juce::Rectangle<int> chassis (juce::Rectangle<int> local)
    {
        return local.withTrimmedLeft (chassisInsetX).withTrimmedRight (chassisInsetX).withTrimmedTop (chassisTop).withTrimmedBottom (chassisBottomGap);
    }

    /** A faceplate's measures: the inset chassis (V2) or the full-bleed face (V3). */
    struct Measures
    {
        int insetX, top, bottomGap, motifWidth, motifGap, rightPad;
        float radius;
        int contentLeft() const noexcept { return insetX + motifWidth + motifGap; }
        int contentRightMargin() const noexcept { return insetX + rightPad; }
        juce::Rectangle<int> chassis (juce::Rectangle<int> local) const
        {
            return local.withTrimmedLeft (insetX).withTrimmedRight (insetX).withTrimmedTop (top).withTrimmedBottom (bottomGap);
        }
    };
    /** Full bleed: the stripes 22 px, 16 px to the content, 16 px right; the panels' radius. */
    constexpr Measures insetMeasures { chassisInsetX, chassisTop, chassisBottomGap, motifWidth, motifGap, 12, chassisRadius };
    constexpr Measures bleedMeasures { 0, 0, 0, 22, 16, 16, 13.0f };
    inline const Measures& measures (const Faceplate& f) noexcept { return f.fullBleed ? bleedMeasures : insetMeasures; }
    /** The rail's centre line (module coordinates): every part of the rail sits on it. */
    inline int railCentre (const Faceplate& f, int titleOffset) noexcept { return (measures (f).top + titleOffset) / 2; }

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
