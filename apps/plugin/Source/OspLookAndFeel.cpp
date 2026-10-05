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
            static const juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
            static const juce::Typeface::Ptr medium = juce::Typeface::createSystemTypefaceFor (BinaryData::InterMedium_ttf, BinaryData::InterMedium_ttfSize);
            static const juce::Typeface::Ptr semibold = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
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
    setColour (juce::PopupMenu::highlightedBackgroundColourId, recessed);
    setColour (juce::PopupMenu::highlightedTextColourId, text);
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
    // Restrained physical depth: a raised face with a hairline and a 1 px contact shadow;
    // pressed or on, it sits in the housing instead.
    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    if (! pressed)
        juce::DropShadow (juce::Colour (0x141e1c18), 2, { 0, 1 }).drawForPath (g, shape);
    g.setColour (pressed ? recessed : (hover ? raised.brighter (0.4f) : raised));
    g.fillPath (shape);
    if (pressed)
    {
        // Inset: a faint shade under the top edge.
        g.setColour (juce::Colour (0x0f1e1c18));
        g.drawLine (r.getX() + radius, r.getY() + 1.0f, r.getRight() - radius, r.getY() + 1.0f, 1.0f);
    }
    g.setColour (hairline);
    g.strokePath (shape, juce::PathStrokeType (1.0f));
}

void OspLookAndFeel::drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float angle, float startAngle,
                               float endAngle, bool mini, bool enabled, juce::Colour arcColour, bool bipolar)
{
    using namespace palette;
    const float side = std::min (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const float ring = side * 0.5f - (mini ? 1.5f : 2.0f);
    const float body = ring - (mini ? 3.5f : side * 0.1f);
    const float alpha = enabled ? 1.0f : 0.4f;

    if (! mini)
    {
        // A quiet scale: eleven small ticks outside the arc.
        g.setColour (textDim.withAlpha (0.45f * alpha));
        for (int i = 0; i <= 10; ++i)
        {
            const float a = startAngle + (endAngle - startAngle) * static_cast<float> (i) / 10.0f;
            const auto p = onCircle (centre, ring + 1.5f, a);
            const float d = (i == 0 || i == 5 || i == 10) ? 2.2f : 1.6f;
            g.fillEllipse (p.x - d * 0.5f, p.y - d * 0.5f, d, d);
        }
    }

    // Track and value arc: the only colour on the control.
    const float arcRadius = ring - (mini ? 0.5f : 2.5f);
    const float thickness = mini ? 1.6f : 2.2f;
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (hairline.withAlpha (alpha));
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float from = bipolar ? 0.5f * (startAngle + endAngle) : startAngle;
    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, std::min (from, angle), std::max (from, angle), true);
        g.setColour (arcColour.withAlpha (alpha));
        g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Body: warm light grey, lit from above, a soft contact shadow.
    juce::Path disc;
    disc.addEllipse (centre.x - body, centre.y - body, 2.0f * body, 2.0f * body);
    juce::DropShadow (juce::Colour (0x261e1c18), mini ? 3 : 5, { 0, mini ? 1 : 2 }).drawForPath (g, disc);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffdfcf9), centre.x, centre.y - body,
                                             juce::Colour (0xffe6e2d9), centre.x, centre.y + body, false));
    g.fillPath (disc);
    g.setColour (juce::Colour (0xffcdc8bd));
    g.strokePath (disc, juce::PathStrokeType (0.8f));

    // Indicator: charcoal.
    const auto inner = onCircle (centre, body * 0.32f, angle);
    const auto outer = onCircle (centre, body * 0.86f, angle);
    g.setColour (text.withAlpha (0.9f * alpha));
    g.drawLine ({ inner, outer }, mini ? 1.6f : 2.2f);
}

void OspLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                       float startAngle, float endAngle, juce::Slider& slider)
{
    const auto& props = slider.getProperties();
    const bool mini = static_cast<bool> (props["mini"]);
    const auto arc = props.contains ("arc") ? juce::Colour (static_cast<juce::uint32> (static_cast<juce::int64> (props["arc"]))) : palette::accent;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const auto r = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y), static_cast<float> (width), static_cast<float> (height));
    const float side = std::min (r.getWidth(), r.getHeight());
    drawKnob (g, r.withSizeKeepingCentre (side, side), angle, startAngle, endAngle, mini, slider.isEnabled(), arc, static_cast<bool> (props["bipolar"]));
}

void OspLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minPos,
                                       float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (! static_cast<bool> (slider.getProperties()["blend"]))
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, slider);
        return;
    }
    // Slim mix sliders: a neutral track (or the two layers' colours meeting at the thumb), a
    // small raised thumb with a coloured centre.
    const auto& props = slider.getProperties();
    const float cy = static_cast<float> (y) + static_cast<float> (height) * 0.5f;
    const float thickness = props.contains ("thin") ? 2.0f : 4.0f;
    const auto track = juce::Rectangle<float> (static_cast<float> (x), cy - 0.5f * thickness, static_cast<float> (width), thickness);
    g.setColour (palette::recessed.darker (0.06f));
    g.fillRoundedRectangle (track, 0.5f * thickness);
    const auto left = props.contains ("leftColour") ? juce::Colour (static_cast<juce::uint32> (static_cast<juce::int64> (props["leftColour"]))) : palette::accent;
    const auto right = props.contains ("rightColour") ? juce::Colour (static_cast<juce::uint32> (static_cast<juce::int64> (props["rightColour"]))) : juce::Colours::transparentBlack;
    if (! right.isTransparent())
    {
        // Two layers: their colours meet along the track; the thumb alone says where.
        g.setGradientFill (juce::ColourGradient (left.withAlpha (0.9f), track.getX(), 0.0f, right.withAlpha (0.9f), track.getRight(), 0.0f, false));
        g.fillRoundedRectangle (track, 0.5f * thickness);
    }
    else
    {
        g.setColour (left.withAlpha (0.85f));
        g.fillRoundedRectangle (track.withRight (sliderPos), 0.5f * thickness);
    }
    const float size = props.contains ("thin") ? 12.0f : 16.0f;
    const auto thumb = juce::Rectangle<float> (sliderPos - 0.5f * size, cy - 0.5f * size, size, size);
    juce::Path disc;
    disc.addEllipse (thumb);
    juce::DropShadow (juce::Colour (0x331e1c18), 3, { 0, 1 }).drawForPath (g, disc);
    g.setColour (palette::raised);
    g.fillPath (disc);
    g.setColour (palette::hairline.darker (0.1f));
    g.strokePath (disc, juce::PathStrokeType (0.8f));
    g.setColour (props.contains ("thin") ? palette::accent : palette::text.withAlpha (0.8f));
    g.fillEllipse (thumb.withSizeKeepingCentre (size * 0.3f, size * 0.3f));
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
        // Value: plain text under the knob (no boxes).
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
    g.fillAll (palette::raised);
    g.setColour (palette::hairline);
    g.drawRect (0, 0, width, height, 1);
}

juce::Font OspLookAndFeel::getPopupMenuFont()
{
    return fonts::make (13.5f, fonts::Weight::regular);
}

} // namespace osp::plugin
