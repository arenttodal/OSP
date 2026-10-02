#include "OspLookAndFeel.h"

#include "BinaryData.h"

namespace osp::plugin
{

namespace fonts
{
    namespace
    {
        juce::Typeface::Ptr typeface (Weight weight)
        {
            static const juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowRegular_ttf, BinaryData::BarlowRegular_ttfSize);
            static const juce::Typeface::Ptr medium = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowMedium_ttf, BinaryData::BarlowMedium_ttfSize);
            static const juce::Typeface::Ptr semibold = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowSemiBold_ttf, BinaryData::BarlowSemiBold_ttfSize);
            switch (weight)
            {
                case Weight::medium: return medium;
                case Weight::semibold: return semibold;
                case Weight::regular: break;
            }
            return regular;
        }
    }

    juce::Font make (float height, Weight weight, float tracking)
    {
        return juce::Font (juce::FontOptions (typeface (weight)).withHeight (height).withKerningFactor (tracking));
    }
}

namespace
{
    juce::Point<float> onCircle (juce::Point<float> centre, float radius, float angle) noexcept
    {
        return centre + juce::Point<float> (std::sin (angle), -std::cos (angle)) * radius;
    }
}

OspLookAndFeel::OspLookAndFeel()
{
    using namespace palette;
    setColour (juce::ResizableWindow::backgroundColourId, housing);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextButton::buttonColourId, housingLight);
    setColour (juce::TextButton::buttonOnColourId, surface);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, accent);
    setColour (juce::ComboBox::backgroundColourId, housingLight);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::ComboBox::outlineColourId, border);
    setColour (juce::PopupMenu::backgroundColourId, housingLight);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.3f));
    setColour (juce::TextEditor::backgroundColourId, housingLight);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.3f));
    setColour (juce::TextEditor::highlightedTextColourId, text);
    setColour (juce::TextEditor::outlineColourId, border);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::CaretComponent::caretColourId, text);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, juce::Colours::white);
    setColour (juce::TooltipWindow::backgroundColourId, housingLight);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, border);
    setColour (juce::ScrollBar::thumbColourId, displayText);
}

juce::Typeface::Ptr OspLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    // Text whose font was not chosen explicitly still uses the instrument's typeface.
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return fonts::make (font.getHeight(), font.isBold() ? fonts::Weight::semibold : fonts::Weight::regular).getTypefacePtr();
    return LookAndFeel_V4::getTypefaceForFont (font);
}

//==============================================================================
void OspLookAndFeel::drawCard (juce::Graphics& g, juce::Rectangle<float> r, float radius, bool pressed, bool hover)
{
    using namespace palette;
    // Soft contact shadow, a gently domed face, a fine border and a top highlight.
    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    juce::DropShadow (juce::Colours::black.withAlpha (pressed ? 0.06f : 0.14f), pressed ? 1 : 3, { 0, 1 }).drawForPath (g, shape);
    auto top = pressed ? surface : housingLight;
    auto bottom = pressed ? surface.darker (0.04f) : juce::Colour (0xffe4d9c6);
    if (hover && ! pressed)
    {
        top = top.brighter (0.04f);
        bottom = bottom.brighter (0.04f);
    }
    g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bottom, r.getX(), r.getBottom(), false));
    g.fillPath (shape);
    g.setColour (border.withAlpha (0.9f));
    g.strokePath (shape, juce::PathStrokeType (1.0f));
    if (! pressed)
    {
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawLine (r.getX() + radius, r.getY() + 1.2f, r.getRight() - radius, r.getY() + 1.2f, 1.0f);
    }
}

void OspLookAndFeel::drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float angle, float startAngle,
                               float endAngle, bool mini, bool enabled)
{
    using namespace palette;
    const float side = std::min (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const float dotRing = side * 0.5f - (mini ? 1.0f : 2.0f);
    const float body = mini ? side * 0.5f - 3.0f : side * 0.5f - side * 0.11f;

    if (! mini)
    {
        // Dot scale: eleven positions between the end stops.
        g.setColour (textDim.withAlpha (0.75f));
        for (int i = 0; i <= 10; ++i)
        {
            const float a = startAngle + (endAngle - startAngle) * static_cast<float> (i) / 10.0f;
            const auto p = onCircle (centre, dotRing, a);
            const float d = (i == 0 || i == 5 || i == 10) ? 3.2f : 2.4f;
            g.fillEllipse (p.x - d * 0.5f, p.y - d * 0.5f, d, d);
        }
    }

    juce::Path disc;
    disc.addEllipse (centre.x - body, centre.y - body, 2.0f * body, 2.0f * body);
    juce::DropShadow (juce::Colours::black.withAlpha (mini ? 0.22f : 0.30f), mini ? 4 : 10, { 0, mini ? 2 : 4 }).drawForPath (g, disc);

    // Skirt: lit from above.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffcf9f3), centre.x, centre.y - body,
                                             juce::Colour (0xffcfc4b1), centre.x, centre.y + body, false));
    g.fillPath (disc);
    g.setColour (juce::Colour (0xff8f836e).withAlpha (0.55f));
    g.strokePath (disc, juce::PathStrokeType (1.0f));

    // Face: a slightly smaller, gently dished cap.
    const float face = body * (mini ? 0.82f : 0.86f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffece4d5), centre.x, centre.y - face,
                                             juce::Colour (0xfff8f4ec), centre.x, centre.y + face, false));
    g.fillEllipse (centre.x - face, centre.y - face, 2.0f * face, 2.0f * face);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawEllipse (centre.x - face, centre.y - face, 2.0f * face, 2.0f * face, 0.8f);

    // Indicator.
    const auto inner = onCircle (centre, face * (mini ? 0.25f : 0.32f), angle);
    const auto outer = onCircle (centre, face * 0.94f, angle);
    g.setColour ((mini ? text : accent).withAlpha (enabled ? 1.0f : 0.4f));
    g.drawLine ({ inner, outer }, mini ? 2.0f : 3.0f);
    g.fillEllipse (outer.x - (mini ? 1.0f : 1.5f), outer.y - (mini ? 1.0f : 1.5f), mini ? 2.0f : 3.0f, mini ? 2.0f : 3.0f);
}

void OspLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                       float startAngle, float endAngle, juce::Slider& slider)
{
    const bool mini = static_cast<bool> (slider.getProperties()["mini"]);
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const auto r = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y), static_cast<float> (width), static_cast<float> (height));
    const float side = std::min (r.getWidth(), r.getHeight());
    drawKnob (g, r.withSizeKeepingCentre (side, side), angle, startAngle, endAngle, mini, slider.isEnabled());
}

juce::Slider::SliderLayout OspLookAndFeel::getSliderLayout (juce::Slider& slider)
{
    auto layout = LookAndFeel_V4::getSliderLayout (slider);
    if (slider.getTextBoxPosition() == juce::Slider::TextBoxBelow)
        layout.sliderBounds.removeFromBottom (6);   // air between the knob and its value box
    return layout;
}

juce::Label* OspLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (fonts::make (13.0f, fonts::Weight::medium, 0.02f));
    label->setColour (juce::Label::textColourId, palette::text);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return label;
}

void OspLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr && ! label.isBeingEdited())
    {
        // Value box: a small recessed window under the knob.
        auto r = label.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (palette::surface.withAlpha (0.55f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (palette::border);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.06f));
        g.drawLine (r.getX() + 3.0f, r.getY() + 1.5f, r.getRight() - 3.0f, r.getY() + 1.5f, 1.0f);
        g.setColour (palette::text);
        g.setFont (label.getFont());
        g.drawText (label.getText(), label.getLocalBounds(), juce::Justification::centred, false);
        return;
    }
    LookAndFeel_V4::drawLabel (g, label);
}

//==============================================================================
void OspLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                           bool highlighted, bool down)
{
    const auto r = button.getLocalBounds().toFloat().reduced (1.0f, 1.5f);
    drawCard (g, r, 4.0f, down || button.getToggleState(), highlighted);
}

juce::Font OspLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    if (static_cast<bool> (button.getProperties()["caps"]))
        return fonts::label (11.0f);
    return fonts::make (std::min (13.5f, static_cast<float> (buttonHeight) * 0.55f), fonts::Weight::medium, 0.01f);
}

void OspLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto colour = button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
    g.setColour (colour.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.4f));
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.drawText (button.getButtonText(), button.getLocalBounds().reduced (4, 0), juce::Justification::centred, false);
}

void OspLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const float box = 14.0f;
    auto r = button.getLocalBounds().toFloat();
    const auto tick = juce::Rectangle<float> (r.getX() + 1.0f, r.getCentreY() - box * 0.5f, box, box);
    if (button.getToggleState())
    {
        g.setColour (palette::accent);
        g.fillRoundedRectangle (tick, 3.0f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (tick.withSizeKeepingCentre (5.0f, 5.0f));
    }
    else
        drawCard (g, tick, 3.0f, false, highlighted);
    g.setColour (button.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.4f));
    g.setFont (fonts::label (12.0f));
    g.drawText (button.getButtonText(), r.withTrimmedLeft (box + 8.0f), juce::Justification::centredLeft, false);
}

//==============================================================================
void OspLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool down, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height)).reduced (1.0f, 1.5f);
    drawCard (g, r, 4.0f, down, box.isMouseOver (true));
    // Chevron
    const float cx = r.getRight() - 13.0f, cy = r.getCentreY();
    juce::Path chevron;
    chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (palette::text.withAlpha (box.isEnabled() ? 0.85f : 0.35f));
    g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font OspLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return fonts::make (13.5f, fonts::Weight::medium, 0.01f);
}

void OspLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void OspLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (palette::housingLight);
    g.setColour (palette::border);
    g.drawRect (0, 0, width, height, 1);
}

juce::Font OspLookAndFeel::getPopupMenuFont()
{
    return fonts::make (14.0f, fonts::Weight::regular);
}

} // namespace osp::plugin
