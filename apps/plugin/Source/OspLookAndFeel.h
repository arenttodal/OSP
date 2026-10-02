#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace osp::plugin
{

/** The instrument's palette (UI redesign: warm housing, charcoal display, one orange accent). */
namespace palette
{
    const juce::Colour frame { 0xff26292c };        ///< around the housing
    const juce::Colour housing { 0xffe7ddcc };      ///< chassis
    const juce::Colour housingLight { 0xfff1eadd }; ///< raised controls, popups
    const juce::Colour surface { 0xffd9cdb3 };      ///< control surfaces, value boxes
    const juce::Colour border { 0xffb8ab92 };
    const juce::Colour display { 0xff1f2326 };      ///< waveform well
    const juce::Colour displayLine { 0xff343a40 };
    const juce::Colour displayText { 0xff8f9396 };
    const juce::Colour wave { 0xffdccdb1 };
    const juce::Colour accent { 0xfff26722 };       ///< active states and indicators only
    const juce::Colour text { 0xff2b2b2b };
    const juce::Colour textDim { 0xff6f685d };
    const juce::Colour ivory { 0xfff7f3ea };        ///< white keys
    const juce::Colour ebony { 0xff2a2b2d };        ///< black keys
}

/** Barlow (SIL OFL, embedded) so the instrument reads the same on every computer. */
namespace fonts
{
    enum class Weight { regular, medium, semibold };
    juce::Font make (float height, Weight weight = Weight::regular, float tracking = 0.0f);
    /** Uppercase labels: medium weight, a little letter-spacing. */
    inline juce::Font label (float height) { return make (height, Weight::medium, 0.08f); }
}

/**
    LookAndFeel of the redesigned instrument: rounded hardware-card buttons and combo
    boxes, cream knobs with a dot scale and an orange indicator, value boxes, light popup
    menus. Sliders flagged with the "mini" property draw the compact popup knob.
*/
class OspLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    OspLookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;

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
                          float endAngle, bool mini, bool enabled);
    /** A rounded raised card (buttons, popups, value boxes). */
    static void drawCard (juce::Graphics&, juce::Rectangle<float> bounds, float radius, bool pressed, bool hover);

    static constexpr float rotaryStart = juce::MathConstants<float>::pi * 1.25f;
    static constexpr float rotaryEnd = juce::MathConstants<float>::pi * 2.75f;
};

} // namespace osp::plugin
