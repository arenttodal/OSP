#include "OspLookAndFeel.h"

#include "Design.h"

#include "BinaryData.h"

namespace osp::plugin
{

namespace fonts
{
    namespace
    {
        juce::Typeface::Ptr typeface (Weight weight)
        {
            static const juce::Typeface::Ptr light = juce::Typeface::createSystemTypefaceFor (BinaryData::InterLight_ttf, BinaryData::InterLight_ttfSize);
            static const juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
            static const juce::Typeface::Ptr medium = juce::Typeface::createSystemTypefaceFor (BinaryData::InterMedium_ttf, BinaryData::InterMedium_ttfSize);
            static const juce::Typeface::Ptr semibold = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
            static const juce::Typeface::Ptr bold = juce::Typeface::createSystemTypefaceFor (BinaryData::InterBold_ttf, BinaryData::InterBold_ttfSize);
            static const juce::Typeface::Ptr extrabold = juce::Typeface::createSystemTypefaceFor (BinaryData::InterExtraBold_ttf, BinaryData::InterExtraBold_ttfSize);
            static const juce::Typeface::Ptr displayBold = juce::Typeface::createSystemTypefaceFor (BinaryData::OutfitExtraBold_ttf, BinaryData::OutfitExtraBold_ttfSize);
            static const juce::Typeface::Ptr displayLight = juce::Typeface::createSystemTypefaceFor (BinaryData::OutfitExtraLight_ttf, BinaryData::OutfitExtraLight_ttfSize);
            switch (weight)
            {
                case Weight::light: return light;
                case Weight::medium: return medium;
                case Weight::semibold: return semibold;
                case Weight::bold: return bold;
                case Weight::extrabold: return extrabold;
                case Weight::displayBold: return displayBold;
                case Weight::displayLight: return displayLight;
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
    // The value shown while a knob turns: a small graphite bubble (its text colour is set
    // on each knob that shows one - JUCE reads it from the knob, see valueBubbleText).
    setColour (juce::BubbleComponent::backgroundColourId, graphite);
    setColour (juce::BubbleComponent::outlineColourId, graphite.darker (0.4f));
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
    // Every knob is the one instrument knob (design::draw::knob), sized to its bounds: with
    // ticks the body leaves room for them; "mini" knobs (envelope, popups) have none.
    const auto& props = slider.getProperties();
    const bool mini = static_cast<bool> (props["mini"]);
    design::draw::KnobStyle style;
    style.arc = props.contains ("arc") ? juce::Colour (static_cast<juce::uint32> (static_cast<juce::int64> (props["arc"]))) : design::colour::accent;
    style.bipolar = static_cast<bool> (props["bipolar"]);
    style.ticks = ! mini && ! static_cast<bool> (props["noTicks"]);
    style.startAngle = startAngle;
    style.endAngle = endAngle;
    style.enabled = slider.isEnabled();
    const auto r = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y), static_cast<float> (width), static_cast<float> (height));
    const float side = std::min (r.getWidth(), r.getHeight());
    if (static_cast<bool> (props["popup"]))
    {
        // The popups' knob (SPACE's DECAY): eleven ticks close around a ring arc, the
        // pointer in the accent on the cap.
        style.ticks = true;
        style.tickCount = 11;
        style.tickRadius = 1.29f;
        style.arcRadius = 1.06f;
        style.pointer = design::colour::accent;
        style.pointerFrom = 0.46f;
        style.pointerTo = 0.77f;
        design::draw::knob (g, r.getCentre(), 0.5f * side / 1.36f, sliderPos, style);
        return;
    }
    const float body = 0.5f * side / (style.ticks ? 1.42f : 1.2f);
    design::draw::knob (g, r.getCentre(), body, sliderPos, style);
}

void OspLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minPos,
                                       float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (! static_cast<bool> (slider.getProperties()["blend"]))
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, slider);
        return;
    }
    // The A/B blend: a thin recessed groove carrying only a hint of the two layers' colours
    // (meeting in a dusky middle); a small knob-like thumb with a dark centre.
    const auto& props = slider.getProperties();
    auto colourOf = [&props] (const char* key, juce::Colour fallback) {
        return props.contains (key) ? juce::Colour (static_cast<juce::uint32> (static_cast<juce::int64> (props[key]))) : fallback;
    };
    const auto left = colourOf ("leftColour", design::colour::accent), right = colourOf ("rightColour", juce::Colour (0xff6da7cc));
    const float cy = static_cast<float> (y) + static_cast<float> (height) * 0.5f;
    const auto track = juce::Rectangle<float> (static_cast<float> (x), cy - 3.0f, static_cast<float> (width), 6.0f);
    const auto groove = juce::Colour (0xffcfc5b6);
    juce::ColourGradient fill (left.interpolatedWith (groove, 0.35f), track.getX(), 0.0f, right.interpolatedWith (groove, 0.35f), track.getRight(), 0.0f, false);
    fill.addColour (0.5, left.interpolatedWith (right, 0.5f).interpolatedWith (groove, 0.45f).withMultipliedBrightness (0.8f));
    g.setGradientFill (fill);
    g.fillRoundedRectangle (track, 3.0f);
    // Recessed: shade along the top, light along the bottom lip.
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.3f), 0.0f, track.getY(), juce::Colours::black.withAlpha (0.0f), 0.0f, track.getCentreY() + 1.0f, false));
    g.fillRoundedRectangle (track, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.fillRect (track.reduced (3.0f, 0.0f).withY (track.getBottom() + 0.5f).withHeight (0.8f));

    const juce::Point<float> c (sliderPos, cy);
    const auto disc = juce::Rectangle<float> (26.0f, 26.0f).withCentre (c);
    juce::Path shape;
    shape.addEllipse (disc);
    juce::DropShadow (juce::Colour (0x4a302418), 5, { 0, 2 }).drawForPath (g, shape);
    juce::DropShadow (juce::Colour (0x22302418), 1, { 0, 1 }).drawForPath (g, shape);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffefbf6), c.x - 7.0f, disc.getY(), juce::Colour (0xffcfc5b6), c.x + 7.0f, disc.getBottom(), false));
    g.fillPath (shape);
    g.setColour (juce::Colour (0xffaea290));
    g.strokePath (shape, juce::PathStrokeType (0.9f));
    const auto inner = disc.reduced (3.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff8f3eb), c.x, inner.getY(), juce::Colour (0xffe5dccf), c.x, inner.getBottom(), false));
    g.fillEllipse (inner);
    g.setColour (juce::Colour (0xff221f1c));
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (c));
}

int OspLookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    if (static_cast<bool> (slider.getProperties()["blend"]))
        return 13;
    return LookAndFeel_V4::getSliderThumbRadius (slider);
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
    label->setFont (fonts::make (13.0f, fonts::Weight::regular, 0.02f));
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
    // The instrument's raised key (Advanced, Browse, Reseed...): pressed or on, accent-rimmed.
    const auto r = button.getLocalBounds().toFloat().reduced (1.0f, 1.5f);
    design::draw::button (g, r, std::min (9.0f, 0.22f * r.getHeight()), down || button.getToggleState(), highlighted, design::colour::accent);
}

juce::Font OspLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    if (static_cast<bool> (button.getProperties()["caps"]))
        return fonts::label (11.0f);
    return type::button (std::min (19.0f, static_cast<float> (buttonHeight) * 0.46f));
}

void OspLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    // Secondary navigation (Advanced, Browse...): regular, a little subdued.
    const auto colour = design::colour::text.withAlpha (0.85f);
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
    return fonts::make (13.5f, fonts::Weight::regular, 0.01f);
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
