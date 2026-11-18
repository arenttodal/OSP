#include "OspLookAndFeel.h"

#include "Design.h"
#include "MenuIcons.h"

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
    setColour (juce::PopupMenu::backgroundColourId, popupSurface());
    setColour (juce::PopupMenu::textColourId, juce::Colour (0xff2f2c28));
    setColour (juce::PopupMenu::headerTextColourId, juce::Colour (0xff8a847a));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, popupHighlight());
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xff2f2c28));
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
        design::CachedShadow (juce::Colour (0x141e1c18), 2, { 0, 1 }).drawForPath (g, shape);
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
    design::CachedShadow (juce::Colour (0x261e1c18), mini ? 3 : 5, { 0, mini ? 1 : 2 }).drawForPath (g, disc);
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
    style.sprite = spriteFor (slider, *sprites);   // a rendered cap, under the current knob skin
    style.graphite = static_cast<bool> (props["graphite"]);
    if (static_cast<bool> (props["spectral"]))
    {
        style.spectral = true;
        style.spectralLift = slider.isMouseOverOrDragging() ? 1.06f : 1.0f;
    }
    if (props.contains ("thinArc"))
    {
        // A macro: its identity colour on a thin ring (90 %, a touch more on hover).
        style.arcWidth = static_cast<float> (props["thinArc"]);
        style.arc = slider.isMouseOverOrDragging() ? style.arc.withMultipliedBrightness (1.07f) : style.arc.withAlpha (0.9f);
    }
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
        style.pointer = props.contains ("arc") ? style.arc : design::colour::accent;
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
    design::CachedShadow (juce::Colour (0x4a302418), 5, { 0, 2 }).drawForPath (g, shape);
    design::CachedShadow (juce::Colour (0x22302418), 1, { 0, 1 }).drawForPath (g, shape);
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
    if (static_cast<bool> (slider.getProperties()["volume"]))
        return 7;   // VolumeSlider::thumbRadius: its track runs between the thumb's extremes
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

namespace
{
    bool translucentMenusForced = false;

    /** The card's ink colours: graphite text, a softer graphite for labels and shortcuts. */
    const juce::Colour menuInk { 0xff2f2c28 };
    const juce::Colour menuInkSoft { 0xff8a847a };
    const juce::Colour menuRule { 0xffe6ddcf };
    const juce::Colour menuRim { 0x24504434 };

    int shadowMarginFor (const juce::PopupMenu::Options& options)
    {
        // A menu inside a component is always drawn over it (translucent); a desktop window
        // only where the platform composites windows.
        return options.getParentComponent() != nullptr || OspLookAndFeel::translucentMenus() ? OspLookAndFeel::popupShadowMargin : 0;
    }

    /** The card: where there is a margin, rounded, on a soft contact and ambient shadow; else
        square, filling the window, with a hairline. */
    void drawMenuCard (juce::Graphics& g, int width, int height, int margin)
    {
        const auto bounds = juce::Rectangle<int> (width, height).toFloat();
        if (margin <= 0)
        {
            g.fillAll (OspLookAndFeel::popupSurface());
            g.setColour (menuRim);
            g.drawRect (bounds, 1.0f);
            return;
        }
        const auto card = bounds.reduced (static_cast<float> (margin));
        juce::Path shape;
        shape.addRoundedRectangle (card, OspLookAndFeel::popupRadius);
        juce::DropShadow (juce::Colours::black.withAlpha (0.10f), 6, { 0, 3 }).drawForPath (g, shape);   // ambient
        juce::DropShadow (juce::Colours::black.withAlpha (0.15f), 3, { 0, 1 }).drawForPath (g, shape);   // contact
        g.setColour (OspLookAndFeel::popupSurface());
        g.fillPath (shape);
        g.setColour (menuRim);
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }
}

bool OspLookAndFeel::translucentMenus() noexcept
{
    return translucentMenusForced || juce::Desktop::canUseSemiTransparentWindows();
}

void OspLookAndFeel::setTranslucentMenusForTests (bool forced) noexcept
{
    translucentMenusForced = forced;
}

int OspLookAndFeel::popupSideInset (const juce::Component& item) noexcept
{
    for (auto* c = item.getParentComponent(); c != nullptr; c = c->getParentComponent())
        if (c->getName() == "menu")   // (JUCE's menu window)
            return c->getParentComponent() != nullptr || translucentMenus() ? popupShadowMargin : 0;
    return 0;
}

void OspLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    drawMenuCard (g, width, height, translucentMenus() ? popupShadowMargin : 0);
}

void OspLookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width, int height, const juce::PopupMenu::Options& options)
{
    drawMenuCard (g, width, height, shadowMarginFor (options));
}

int OspLookAndFeel::getPopupMenuBorderSize()
{
    return popupPadding + (translucentMenus() ? popupShadowMargin : 0);
}

int OspLookAndFeel::getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options& options)
{
    // JUCE places a submenu's window at its row's right edge: with the padding equal to the
    // margin, the two cards meet edge to edge and the submenu's first row lines up with its row.
    return popupPadding + shadowMarginFor (options);
}

void OspLookAndFeel::preparePopupMenuWindow (juce::Component& window)
{
    const bool translucent = window.getParentComponent() != nullptr || translucentMenus();
    // JUCE makes a menu window opaque when its background colour is; the card's corners and
    // shadow need a translucent one (this re-creates the window where it is on the desktop).
    if (translucent)
        window.setOpaque (false);
    if (! translucent)
        return;
    // A submenu: JUCE opens its window at its parent window's edge, so the two cards would sit
    // two shadow margins apart. Moved to meet it, overlapping it a little (its own shadow falls
    // on the parent): as one surface opening out of another.
    constexpr int overlap = 3;
    const auto bounds = window.getBounds();
    auto consider = [&] (juce::Component* other) {
        if (other == nullptr || other == &window || other->getName() != "menu" || ! other->isVisible())
            return false;
        // (Within a few pixels: a scaled menu's edges are rounded in its own units.)
        const auto o = other->getBounds();
        constexpr int slack = 3;
        if (std::abs (o.getRight() - bounds.getX()) <= slack)
            window.setTopLeftPosition (o.getRight() - 2 * popupShadowMargin - overlap, bounds.getY());
        else if (std::abs (o.getX() - bounds.getRight()) <= slack)
            window.setTopLeftPosition (o.getX() - bounds.getWidth() + 2 * popupShadowMargin + overlap, bounds.getY());
        else
            return false;
        return true;
    };
    bool others = false;
    auto visit = [&] (juce::Component* c) {
        others = others || (c != nullptr && c != &window && c->getName() == "menu" && c->isVisible());
        return consider (c);
    };
    if (auto* parent = window.getParentComponent())
    {
        for (auto* child : parent->getChildren())
            if (visit (child))
                return;
    }
    else
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = desktop.getNumComponents(); --i >= 0;)
            if (visit (desktop.getComponent (i)))
                return;
    }
    // A menu of its own (no other open): JUCE puts its window's corner at its anchor's (under
    // it, its left edge); the card sits a shadow margin inside. Moved so the card's left edge is
    // the anchor's and its top 3 px under it.
    if (! others)
        window.setTopLeftPosition (bounds.getX() - popupShadowMargin, bounds.getY() - (popupShadowMargin - 3));
}

int OspLookAndFeel::getMenuWindowFlags()
{
    // The card draws its own shadow; the system's would square it off.
    return translucentMenus() ? 0 : juce::ComponentPeer::windowHasDropShadow;
}

juce::Font OspLookAndFeel::getPopupMenuFont()
{
    return fonts::make (13.5f, fonts::Weight::regular);
}

void OspLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = popupSeparatorHeight;
        return;
    }
    idealHeight = standardMenuItemHeight > 0 ? std::max (standardMenuItemHeight, popupRowHeight) : popupRowHeight;
    // Text after the icon column (the wider gutter: an icon menu's labels must fit too), and room
    // after it for a chevron, a tick or a shortcut.
    idealWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (getPopupMenuFont(), text)) + popupIconGutter + 34;
}

void OspLookAndFeel::getIdealPopupMenuSectionHeaderSizeWithOptions (const juce::String& text, int, int& idealWidth, int& idealHeight, const juce::PopupMenu::Options&)
{
    idealHeight = popupHeaderHeight;
    idealWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (fonts::make (10.5f, fonts::Weight::semibold, 0.12f), text.toUpperCase())) + 2 * 14 + 8;
}

void OspLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
{
    // A quiet label in small capitals near the bottom of its row, over its group (the groups'
    // rules are the menus' own separators: a header cannot know whether it is first).
    g.setColour (menuInkSoft);
    g.setFont (fonts::make (10.5f, fonts::Weight::semibold, 0.12f));
    g.drawFittedText (sectionName.toUpperCase(), area.withTrimmedLeft (14).withTrimmedRight (12).withTrimmedTop (area.getHeight() / 3),
                      juce::Justification::centredLeft, 1, 0.9f);
}

void OspLookAndFeel::drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area, bool isHighlighted, const juce::PopupMenu::Item& item,
                                                   const juce::PopupMenu::Options& options)
{
    const auto colour = item.colour != juce::Colour() ? &item.colour : nullptr;
    const bool hasSubMenu = item.subMenu != nullptr && (item.itemID == 0 || item.subMenu->getNumItems() > 0);
    drawPopupMenuItem (g, area.reduced (shadowMarginFor (options), 0), item.isSeparator, item.isEnabled, isHighlighted, item.isTicked, hasSubMenu, item.text,
                       item.shortcutKeyDescription, item.image.get(), colour);
}

void OspLookAndFeel::drawPopupMenuSectionHeaderWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName,
                                                            const juce::PopupMenu::Options& options)
{
    drawPopupMenuSectionHeader (g, area.reduced (shadowMarginFor (options), 0), sectionName);
}

void OspLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                                        bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText, const juce::Drawable* icon,
                                        const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (menuRule);
        g.fillRect (juce::Rectangle<int> (area.getX() + 12, area.getCentreY(), area.getWidth() - 24, 1));
        return;
    }
    if (isHighlighted && isActive)
    {
        g.setColour (popupHighlight());
        g.fillRoundedRectangle (area.toFloat().reduced (4.0f, 1.0f), 5.0f);
    }
    const auto ink = (textColour != nullptr ? *textColour : menuInk).withMultipliedAlpha (isActive ? 1.0f : 0.38f);
    auto drawTick = [&] (juce::Point<float> c) {
        juce::Path tick;
        tick.startNewSubPath (c.x - 4.0f, c.y);
        tick.lineTo (c.x - 1.0f, c.y + 3.0f);
        tick.lineTo (c.x + 4.5f, c.y - 3.5f);
        g.setColour (ink);
        g.strokePath (tick, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    const auto rowY = static_cast<float> (area.getY()), rowH = static_cast<float> (area.getHeight());
    auto textArea = area.withTrimmedRight (12);
    if (icon != nullptr)
    {
        // An icon menu: the icon in its own column, the label after it, a tick at the right.
        const auto box = juce::Rectangle<float> (static_cast<float> (area.getX()) + 10.0f, rowY, 15.0f, rowH).withSizeKeepingCentre (15.0f, 15.0f);
        if (auto* glyph = dynamic_cast<const menuicons::Glyph*> (icon))
            menuicons::draw (g, glyph->glyphKind(), box, ink.withMultipliedAlpha (textColour != nullptr ? 1.0f : 0.86f));
        else
            icon->drawWithin (g, box, juce::RectanglePlacement::centred, isActive ? 1.0f : 0.4f);
        textArea.removeFromLeft (popupIconGutter);
        if (isTicked)
        {
            drawTick ({ static_cast<float> (textArea.getRight()) - 6.0f, rowY + rowH * 0.5f });
            textArea.removeFromRight (18);
        }
    }
    else
    {
        if (isTicked)
            drawTick ({ static_cast<float> (area.getX()) + 13.0f, rowY + rowH * 0.5f });
        textArea.removeFromLeft (popupGutter);
    }
    if (hasSubMenu)
    {
        const auto chevronBox = textArea.removeFromRight (10).toFloat().withSizeKeepingCentre (6.0f, 9.0f);
        juce::Path chevron;
        chevron.startNewSubPath (chevronBox.getX() + 1.0f, chevronBox.getY());
        chevron.lineTo (chevronBox.getRight() - 1.0f, chevronBox.getCentreY());
        chevron.lineTo (chevronBox.getX() + 1.0f, chevronBox.getBottom());
        g.setColour (ink.withMultipliedAlpha (0.6f));
        g.strokePath (chevron, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        textArea.removeFromRight (6);
    }
    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (menuInkSoft.withMultipliedAlpha (isActive ? 1.0f : 0.6f));
        g.setFont (fonts::make (11.5f, fonts::Weight::regular, 0.02f));
        g.drawText (shortcutKeyText, textArea, juce::Justification::centredRight, false);
        textArea.removeFromRight (juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), shortcutKeyText)) + 12);
    }
    g.setColour (ink);
    g.setFont (getPopupMenuFont());
    g.drawFittedText (text, textArea, juce::Justification::centredLeft, 1, 0.9f);
}

SharedMenuLook::Holder::Holder()
{
    juce::LookAndFeel::setDefaultLookAndFeel (&look);
}

SharedMenuLook::Holder::~Holder()
{
    // Nothing may still draw with the look when it goes: open menus close, the default returns.
    juce::PopupMenu::dismissAllActiveMenus();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void drawDropdownField (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool hot, bool focused, bool enabled)
{
    // As the racks' MODE field (ValueSelector, RackStyle::mode): drop, rim, light face.
    const float radius = 5.0f;
    g.setColour (juce::Colour (0x65c5bcae));
    g.fillRoundedRectangle (r.translated (0.0f, 1.0f), radius);
    g.setColour (juce::Colour (hot && enabled ? 0xffc8bfb2 : 0xffd3cbbf));
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colour (hot && enabled ? 0xfffaf7f1 : 0xfff6f2eb));
    g.fillRoundedRectangle (r.reduced (1.0f), radius - 1.0f);
    const auto ink = juce::Colour (0xff66615e).withMultipliedAlpha (enabled ? 1.0f : 0.45f);
    auto area = r.reduced (10.0f, 0.0f);
    const auto chevronBox = area.removeFromRight (10.0f).withSizeKeepingCentre (8.0f, 8.0f);
    juce::Path chevron;
    chevron.startNewSubPath (chevronBox.getX(), chevronBox.getY() + 2.5f);
    chevron.lineTo (chevronBox.getCentreX(), chevronBox.getBottom() - 1.5f);
    chevron.lineTo (chevronBox.getRight(), chevronBox.getY() + 2.5f);
    g.setColour (ink.withMultipliedAlpha (0.8f));
    g.strokePath (chevron, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (ink);
    g.setFont (fonts::make (11.0f, fonts::Weight::regular, 0.04f));
    g.drawText (text.toUpperCase(), area.withTrimmedRight (4.0f), juce::Justification::centred, true);
    if (focused)
    {
        g.setColour (juce::Colour (0xff90adbf));
        g.drawRoundedRectangle (r.expanded (2.0f), radius + 2.0f, 2.0f);
    }
}

void styleTextField (juce::TextEditor& field, TextSurface surface)
{
    // The foreground always follows the field's own surface (never forced dark everywhere).
    const bool dark = surface == TextSurface::dark;
    const auto ground = dark ? juce::Colour (0xff1c2122) : palette::raised;
    const auto ink = dark ? palette::valueBubbleText : palette::text;
    field.setColour (juce::TextEditor::backgroundColourId, ground);
    field.setColour (juce::TextEditor::textColourId, ink);
    field.setColour (juce::TextEditor::highlightColourId, palette::sage.withAlpha (dark ? 0.55f : 0.45f));
    field.setColour (juce::TextEditor::highlightedTextColourId, ink);
    field.setColour (juce::TextEditor::outlineColourId, dark ? palette::displayLine : palette::hairline);
    field.setColour (juce::TextEditor::focusedOutlineColourId, palette::accent);
    field.setColour (juce::CaretComponent::caretColourId, ink);
    field.applyColourToAllText (ink, true);
}

} // namespace osp::plugin
