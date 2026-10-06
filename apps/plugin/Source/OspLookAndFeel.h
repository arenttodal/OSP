#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace osp::plugin
{

/**
    The instrument's palette (adaptive-layer redesign: "neutral instrument, colourful
    sound"). Warm natural neutrals for the housing and controls; colour only where it
    means something: the live coral accent, the layers' identities and the spectral
    colours of the sound itself (amber -> coral -> rose -> lavender -> mineral blue).
*/
namespace palette
{
    // Surfaces
    const juce::Colour housing { 0xfff2f0ea };      ///< main housing
    const juce::Colour raised { 0xfff8f7f3 };       ///< cards, buttons, popups
    const juce::Colour recessed { 0xffe8e6df };     ///< tracks, wells on the housing
    const juce::Colour hairline { 0xffd5d1c8 };
    const juce::Colour text { 0xff272622 };
    const juce::Colour textDim { 0xff77746d };
    const juce::Colour graphite { 0xff34332f };     ///< the sound displays (colour reads as light on it)
    const juce::Colour accent { 0xffe1774f };       ///< live position, focus, active state: small areas only

    // The sound's spectral colours (usually at reduced opacity).
    const juce::Colour amber { 0xffd79a43 };
    const juce::Colour gold { 0xffdab66d };
    const juce::Colour coral { 0xffd47a62 };
    const juce::Colour rose { 0xffc48791 };
    const juce::Colour lavender { 0xffaaa0bb };
    const juce::Colour mineral { 0xff829cac };
    const juce::Colour sage { 0xff8fa898 };         ///< layer C's neutral bridge

    // Names the older components use, mapped onto the new system.
    const juce::Colour frame { 0xffe7e4dc };
    const juce::Colour housingLight = raised;
    const juce::Colour surface = recessed;
    const juce::Colour border = hairline;
    const juce::Colour display = graphite;
    const juce::Colour displayLine { 0xff4a4844 };
    const juce::Colour displayText { 0xffa29e95 };
    const juce::Colour valueBubbleText { 0xfff4f1ea };   ///< the value shown on a turning knob (on graphite)
    const juce::Colour wave { 0xffded7c8 };
    const juce::Colour ivory { 0xfffbfaf6 };        ///< white keys: warm white
    const juce::Colour ebony { 0xff3b3a36 };        ///< black keys: soft graphite

    /** A layer's identity: A warm (amber/coral), B cool (lavender/blue-grey), C a quiet sage. */
    inline juce::Colour layer (int index)
    {
        switch (index)
        {
            case 1: return juce::Colour (0xff8f9bb4);
            case 2: return juce::Colour (0xff86a596);
            default: break;
        }
        return juce::Colour (0xffd9895a);
    }

    /** The spectral gradient at t (0 = warm/low .. 1 = cool/high). */
    inline juce::Colour spectrum (float t)
    {
        static const juce::Colour stops[] = { amber, gold, coral, rose, lavender, mineral };
        const float x = juce::jlimit (0.0f, 1.0f, t) * 5.0f;
        const int i = juce::jmin (4, static_cast<int> (x));
        return stops[i].interpolatedWith (stops[i + 1], x - static_cast<float> (i));
    }
}

/** Inter (SIL OFL, embedded, tabular figures by default) so the instrument reads the same everywhere. */
namespace fonts
{
    /** Inter weights, and Outfit (displayBold / displayLight) for the OSP/2-OSP identity. */
    enum class Weight { regular, medium, semibold, bold, extrabold, displayBold, displayLight };
    juce::Font make (float height, Weight weight = Weight::regular, float tracking = 0.0f);
    /** Uppercase labels: medium weight, a little letter-spacing (not too much). */
    inline juce::Font label (float height) { return make (height, Weight::medium, 0.05f); }
}

/**
    LookAndFeel of the instrument: flat raised buttons with a hairline and a restrained
    contact shadow, neutral knobs with a charcoal indicator and a thin value arc in the
    layer's (or the global accent) colour, light popup menus. Slider properties:
    "mini" (compact knob, no scale), "arc" (ARGB of the value arc), "bipolar" (the arc
    starts at the centre).
*/
class OspLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    OspLookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool down, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    juce::Font getPopupMenuFont() override;
    void drawLabel (juce::Graphics&, juce::Label&) override;

    /** The knob itself (also used where no Slider exists, e.g. snapshots). */
    static void drawKnob (juce::Graphics&, juce::Rectangle<float> bounds, float angle, float startAngle,
                          float endAngle, bool mini, bool enabled, juce::Colour arc = palette::accent, bool bipolar = false);
    /** A rounded raised card (buttons, popups, value boxes). */
    static void drawCard (juce::Graphics&, juce::Rectangle<float> bounds, float radius, bool pressed, bool hover);

    static constexpr float rotaryStart = juce::MathConstants<float>::pi * 1.25f;
    static constexpr float rotaryEnd = juce::MathConstants<float>::pi * 2.75f;
};

} // namespace osp::plugin
