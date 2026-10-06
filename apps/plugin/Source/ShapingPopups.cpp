#include "ShapingPopups.h"

#include "Design.h"

#include "EngineCard.h"

#include "engine/Shaping.h"

namespace osp::plugin
{

//==============================================================================
namespace format
{
    juce::String percent (double v) { return juce::String (juce::roundToInt (v)); }
    juce::String bipolar (double v)
    {
        const int i = juce::roundToInt (v);
        return (i > 0 ? "+" : "") + juce::String (i);
    }
    juce::String milliseconds (double ms)
    {
        if (ms < 10.0)
            return juce::String (ms, 1) + " ms";
        if (ms < 1000.0)
            return juce::String (juce::roundToInt (ms)) + " ms";
        return juce::String (ms / 1000.0, ms < 10000.0 ? 2 : 1) + " s";
    }
    juce::String hertz (double hz)
    {
        if (hz < 1.0)
            return juce::String (hz, 2) + " Hz";
        if (hz < 10.0)
            return juce::String (hz, 1) + " Hz";
        if (hz < 1000.0)
            return juce::String (juce::roundToInt (hz)) + " Hz";
        return juce::String (hz / 1000.0, hz < 10000.0 ? 2 : 1) + " kHz";
    }
}

//==============================================================================
MacroLabel::MacroLabel (juce::String t, bool popup, bool arrow)
    : text (std::move (t)), opensPopup (popup), twoWay (arrow)
{
    setMouseCursor (opensPopup ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    setTitle (text);
}

void MacroLabel::setCustomised (bool c)
{
    if (customised != c)
    {
        customised = c;
        repaint();
    }
}

void MacroLabel::setOpen (bool o)
{
    if (open != o)
    {
        open = o;
        repaint();
    }
}

void MacroLabel::mouseUp (const juce::MouseEvent& e)
{
    if (opensPopup && onClick != nullptr && getLocalBounds().contains (e.getPosition()))
        onClick();
}

void MacroLabel::paint (juce::Graphics& g)
{
    // The macro's name, restrained (medium, slightly tracked); its settings in use are an
    // accent dot after it.
    const bool hover = opensPopup && isMouseOver();
    auto r = getLocalBounds().toFloat();
    const auto font = type::macroLabel (0.7f * r.getHeight());
    g.setFont (font);
    g.setColour (hover || open ? juce::Colours::black : design::colour::text);
    const float w = juce::GlyphArrangement::getStringWidth (font, text);
    const auto textArea = juce::Rectangle<float> (r.getCentreX() - w * 0.5f, r.getY(), w + 2.0f, r.getHeight());
    g.drawText (text, textArea, juce::Justification::centredLeft, false);
    if (customised)
        design::draw::led (g, { textArea.getRight() + 0.42f * r.getHeight(), r.getCentreY() }, 0.3f * r.getHeight(), juce::Colour (0xffff8a3c), 0.6f);
    if (hover || open)
    {
        g.setColour (open ? palette::accent : palette::text.withAlpha (0.5f));
        g.fillRect (textArea.getX(), r.getCentreY() + font.getHeight() * 0.5f + 1.0f, w, 1.2f);
    }
}

//==============================================================================
SegmentedControl::SegmentedControl (juce::RangedAudioParameter& parameter, juce::StringArray labels)
    : items (std::move (labels)),
      attachment (parameter, [this] (float value) {
          index = juce::jlimit (0, items.size() - 1, juce::roundToInt (value));
          repaint();
          if (onChange != nullptr)
              onChange (index);
      })
{
    attachment.sendInitialUpdate();
    setTitle (parameter.getName (64));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SegmentedControl::mouseDown (const juce::MouseEvent& e)
{
    const int count = std::max (1, items.size());
    const int clicked = juce::jlimit (0, count - 1, e.x * count / std::max (1, getWidth()));
    if (clicked != index)
        attachment.setValueAsCompleteGesture (static_cast<float> (clicked));
}

void SegmentedControl::paint (juce::Graphics& g)
{
    // The reference's tab bar (SPACE: 559 x 43): one raised strip with hairline dividers;
    // the chosen segment is a coral key standing a pixel proud of it, its text in cream.
    using namespace design;
    const auto r = getLocalBounds().toFloat().reduced (0.5f, 1.0f);
    const float h = r.getHeight();
    const int count = std::max (1, items.size());
    const float w = r.getWidth() / static_cast<float> (count);
    const float radius = std::max (3.0f, 0.13f * h);
    {
        juce::Path strip;
        strip.addRoundedRectangle (r, radius);
        juce::DropShadow (juce::Colour (0x22302418), 3, { 0, 1 }).drawForPath (g, strip);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffeee7da), 0.0f, r.getY(), juce::Colour (0xffdcd3c4), 0.0f, r.getBottom(), false));
        g.fillPath (strip);
        g.setColour (juce::Colour (0xffc4bbad));
        g.strokePath (strip, juce::PathStrokeType (1.0f));
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawHorizontalLine (juce::roundToInt (r.getY() + 1.0f), r.getX() + radius, r.getRight() - radius);
    }
    g.setFont (type::popupMode (juce::jlimit (9.0f, 17.5f, (count > 4 ? 0.37f : 0.42f) * h)));
    for (int i = 0; i < count; ++i)
    {
        auto cell = juce::Rectangle<float> (r.getX() + w * static_cast<float> (i), r.getY(), w, h);
        if (i > 0 && i != index && i != index + 1)
        {
            g.setColour (juce::Colour (0xffb5ac9f));
            g.drawVerticalLine (juce::roundToInt (cell.getX()), cell.getY() + 1.0f, cell.getBottom() - 1.0f);
        }
        if (i == index)
        {
            const auto key = cell.expanded (0.5f, 1.0f);
            juce::Path shape;
            shape.addRoundedRectangle (key, radius);
            juce::DropShadow (juce::Colour (0x40a0300a), 4, { 0, 1 }).drawForPath (g, shape);
            g.setGradientFill (juce::ColourGradient (colour::accentTop.brighter (0.06f), 0.0f, key.getY(), colour::accentBottom, 0.0f, key.getBottom(), false));
            g.fillPath (shape);
            g.setColour (juce::Colour (0xffffd9c4).withAlpha (0.8f));
            g.drawHorizontalLine (juce::roundToInt (key.getY() + 1.0f), key.getX() + radius, key.getRight() - radius);
            g.setColour (juce::Colour (0xff9e3312).withAlpha (0.6f));
            g.strokePath (shape, juce::PathStrokeType (1.0f));
            g.setColour (juce::Colour (0xfffff4ea));
        }
        else
        {
            g.setColour (colour::text.withAlpha (0.86f));
        }
        g.drawText (items[i], cell, juce::Justification::centred, false);
    }
}

//==============================================================================
MiniKnob::MiniKnob (juce::AudioProcessorValueTreeState& state, const juce::String& id, juce::String c, Formatter f, bool h)
    : caption (std::move (c)), formatter (std::move (f)), horizontal (h)
{
    slider.getProperties().set ("mini", true);
    slider.setRotaryParameters (OspLookAndFeel::rotaryStart, OspLookAndFeel::rotaryEnd, true);
    slider.setTitle (caption);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);
    if (auto* p = state.getParameter (id))
    {
        // Double-click resets this setting (and only this one).
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        slider.setTooltip (p->getName (64));
    }
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void MiniKnob::setCaption (juce::String c)
{
    caption = std::move (c);
    slider.setTitle (caption);
    repaint();
}

void MiniKnob::setFormatter (Formatter f)
{
    formatter = std::move (f);
    repaint();
}

// Proportions from the reference's envelope knobs (100 x 87): caption on top, the knob at
// 48 % of the height, its value underneath. Horizontal: caption, knob, value in a row.
void MiniKnob::setBoxed (bool shouldBeBoxed)
{
    boxed = shouldBeBoxed;
    slider.getProperties().set ("popup", boxed);
    resized();
    repaint();
}

juce::Rectangle<float> MiniKnob::valueBox() const
{
    // SPACE's DECAY cell (160 high): its value in a recessed box 99 x 31 under the knob.
    const auto r = getLocalBounds().toFloat();
    const float h = r.getHeight();
    return juce::Rectangle<float> (0.62f * h, 0.194f * h).withCentre ({ r.getCentreX(), r.getY() + 0.9125f * h });
}

void MiniKnob::resized()
{
    const auto r = getLocalBounds().toFloat();
    const float h = r.getHeight();
    if (boxed)
    {
        // Caption on top, the knob (body radius 0.2625 h) centred at 46 %, the value box.
        const float side = 2.0f * 1.36f * 0.2625f * h;
        slider.setBounds (juce::Rectangle<float> (side, side).withCentre ({ r.getCentreX(), r.getY() + 0.4625f * h }).getSmallestIntegerContainer());
        return;
    }
    if (horizontal)
    {
        const float side = h;
        slider.setBounds (juce::Rectangle<float> (side, side).withCentre ({ r.getX() + 0.5f * r.getWidth() - 0.05f * r.getWidth(), r.getCentreY() }).getSmallestIntegerContainer());
        return;
    }
    const float side = (compact ? 0.5f : 0.55f) * h;
    slider.setBounds (juce::Rectangle<float> (side, side).withCentre ({ r.getCentreX(), r.getY() + 0.48f * h }).getSmallestIntegerContainer());
}

void MiniKnob::paint (juce::Graphics& g)
{
    const auto value = formatter != nullptr ? formatter (slider.getValue()) : juce::String (slider.getValue(), 1);
    const auto r = getLocalBounds().toFloat();
    const float h = r.getHeight();
    const auto captionColour = onDark ? design::colour::wellText.brighter (0.3f) : design::colour::text;
    const auto valueColour = onDark ? juce::Colour (0xfff2eee6) : design::colour::text;
    if (boxed)
    {
        g.setColour (design::colour::text.withAlpha (0.85f));
        g.setFont (type::popupLabel (0.115f * h));
        g.drawText (caption, juce::Rectangle<float> (r.getX() - 20.0f, r.getY() - 0.01f * h, r.getWidth() + 40.0f, 0.15f * h), juce::Justification::centred, false);
        const auto box = valueBox();
        const float radius = 0.17f * box.getHeight();
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawRoundedRectangle (box.translated (0.0f, 1.0f), radius, 1.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd9cebf), 0.0f, box.getY(), juce::Colour (0xffe3d9cd), 0.0f, box.getY() + 0.4f * box.getHeight(), false));
        g.fillRoundedRectangle (box, radius);
        g.setColour (juce::Colour (0xffbcb1a3));
        g.drawRoundedRectangle (box.reduced (0.5f), radius, 1.0f);
        g.setColour (design::colour::text);
        g.setFont (type::popupValue (0.13f * h));
        g.drawText (value, box.translated (0.0f, 0.5f), juce::Justification::centred, false);
        return;
    }
    if (horizontal)
    {
        g.setColour (onDark ? captionColour : design::colour::textSecondary.darker (0.2f));
        g.setFont (type::popupLabel (0.42f * h));
        g.drawText (caption, r.withRight (static_cast<float> (slider.getX()) - 0.25f * h), juce::Justification::centredRight, false);
        g.setColour (valueColour);
        g.setFont (type::popupValue (0.5f * h));
        g.drawText (value, r.withLeft (static_cast<float> (slider.getRight()) + 0.25f * h), juce::Justification::centredLeft, false);
        return;
    }
    // Small knob cells (envelope, popovers): a light caption, the value a step stronger.
    g.setColour (onDark ? captionColour : design::colour::text.withAlpha (0.78f));
    g.setFont (type::popupLabel (std::max (9.0f, (compact ? 0.14f : 0.155f) * h)));
    g.drawText (caption, juce::Rectangle<float> (r.getX(), r.getY(), r.getWidth(), 0.2f * h), juce::Justification::centred, false);
    g.setColour (onDark ? valueColour : design::colour::text.withAlpha (0.96f));
    g.setFont (type::popupValue (std::max (9.5f, (compact ? 0.17f : 0.19f) * h)));
    g.drawText (value, juce::Rectangle<float> (r.getX(), r.getY() + 0.78f * h, r.getWidth(), 0.22f * h), juce::Justification::centred, false);
}

//==============================================================================
ValueSelector::ValueSelector (juce::RangedAudioParameter& p, juce::String captionText)
    : parameter (p), caption (std::move (captionText)),
      attachment (p, [this] (float v) {
          const int now = juce::roundToInt (v);
          const bool moved = now != index;
          index = now;
          repaint();
          if (moved && onChange != nullptr)
              onChange (index);
      })
{
    attachment.sendInitialUpdate();
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle (caption);
}

void ValueSelector::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float h = r.getHeight();
    const auto valueText = parameter.getAllValueStrings()[index];
    if (plain)
    {
        // A popover's mode, right-aligned beside its title: quiet type and a small chevron.
        const bool hot = isMouseOver() || hasKeyboardFocus (false);
        const auto colour = design::colour::text.withAlpha (hot ? 0.85f : 0.55f);
        icons::draw (g, icons::Kind::chevronDown, r.removeFromRight (0.5f * h).withSizeKeepingCentre (0.42f * h, 0.42f * h), colour, 1.2f);
        r.removeFromRight (3.0f);
        g.setColour (colour);
        g.setFont (type::popupMode (0.55f * h));
        g.drawText (valueText.toUpperCase(), r, juce::Justification::centredRight, true);
        return;
    }
    // Caption above (a third of the height) when there is one, the value on a raised key.
    if (caption.isNotEmpty())
    {
        g.setColour (design::colour::textSecondary);
        g.setFont (fonts::make (0.27f * h, fonts::Weight::regular, 0.05f));
        g.drawText (caption, r.removeFromTop (0.36f * h), juce::Justification::centred, false);
    }
    const auto box = r.reduced (1.0f, 1.0f);
    design::draw::button (g, box, 0.2f * box.getHeight(), false, hasKeyboardFocus (false) || isMouseOver(), design::colour::accent);
    g.setColour (design::colour::text.withAlpha (0.9f));
    g.setFont (type::popupValue (0.46f * box.getHeight()));
    auto text = box.reduced (0.3f * box.getHeight(), 0.0f);
    icons::draw (g, icons::Kind::chevronDown, text.removeFromRight (0.32f * box.getHeight()), design::colour::text.withAlpha (0.5f), 1.2f);
    g.drawText (valueText.toUpperCase(), text, juce::Justification::centred, false);
}

void ValueSelector::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition()))
        return;
    juce::PopupMenu menu;
    const auto values = parameter.getAllValueStrings();
    for (int i = 0; i < values.size(); ++i)
    {
        // The choice arrives later, from the message loop: by then this selector may be
        // gone (popup closed, mode changed), so never call into it unchecked.
        juce::Component::SafePointer<ValueSelector> safe (this);
        menu.addItem (values[i], true, i == index, [safe, i] { if (safe != nullptr) safe->choose (i); });
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()).withDeletionCheck (*this));
}

void ValueSelector::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (std::abs (wheel.deltaY) > 0.0f)
        step (wheel.deltaY > 0.0f ? -1 : 1);
}

bool ValueSelector::keyPressed (const juce::KeyPress& key)
{
    if (key.isKeyCode (juce::KeyPress::rightKey) || key.isKeyCode (juce::KeyPress::downKey))
        return step (1), true;
    if (key.isKeyCode (juce::KeyPress::leftKey) || key.isKeyCode (juce::KeyPress::upKey))
        return step (-1), true;
    return false;
}

void ValueSelector::step (int delta)
{
    choose (juce::jlimit (0, parameter.getAllValueStrings().size() - 1, index + delta));
}

void ValueSelector::choose (int i)
{
    if (i != index)
        attachment.setValueAsCompleteGesture (static_cast<float> (i));
}

//==============================================================================
MiniPanel::MiniPanel (juce::String t, juce::String s) : title (std::move (t)), subtitle (std::move (s))
{
    setTitle (title);
    setInterceptsMouseClicks (true, true);
    setWantsKeyboardFocus (false);
}

juce::Rectangle<float> MiniPanel::closeButton() const
{
    const auto c = card().toFloat();
    const float u = unit();
    return juce::Rectangle<float> (30.0f * u, 30.0f * u).withCentre ({ c.getRight() - 33.5f * u, c.getY() + 35.0f * u });
}

void MiniPanel::mouseUp (const juce::MouseEvent& e)
{
    if (! compact() && closeButton().contains (e.position) && onClose != nullptr)
        onClose();
}

void MiniPanel::paint (juce::Graphics& g)
{
    // The reference's SPACE popup (607 x 610): a light raised card floating well above the
    // instrument (a broad soft shadow and a tight contact one), lit along its top edge;
    // the title in bold capitals, a widely tracked subtitle, a thin close cross.
    using namespace design;
    const float u = unit();
    const auto r = card().toFloat();
    juce::Path shape;
    if (compact())
    {
        // A small raised extension of the macro beneath it: warm cream, a hairline edge, a
        // short soft shadow (no window chrome, no backdrop).
        shape.addRoundedRectangle (r, 14.0f);
        juce::DropShadow (juce::Colour (0x1e1e1a16), 16, { 0, 5 }).drawForPath (g, shape);
        juce::DropShadow (juce::Colour (0x161e1a16), 2, { 0, 1 }).drawForPath (g, shape);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff8f4ec), 0.0f, r.getY(), juce::Colour (0xffeee7db), 0.0f, r.getBottom(), false));
        g.fillPath (shape);
        g.setColour (juce::Colour (0xffd3cabc));
        g.strokePath (shape, juce::PathStrokeType (1.0f));
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawHorizontalLine (juce::roundToInt (r.getY() + 1.0f), r.getX() + 14.0f, r.getRight() - 14.0f);
        g.setColour (colour::text.withAlpha (0.8f));
        g.setFont (type::popupTitle (13.0f));
        g.drawText (title, juce::Rectangle<float> (r.getX() + 13.0f, r.getY() + 7.0f, r.getWidth() * 0.5f, 20.0f), juce::Justification::centredLeft, false);
        return;
    }
    shape.addRoundedRectangle (r, 12.0f * u);
    juce::DropShadow (juce::Colour (0x5a281c10), juce::roundToInt (32.0f * u), { 3, juce::roundToInt (12.0f * u) }).drawForPath (g, shape);
    juce::DropShadow (juce::Colour (0x3a281c10), juce::roundToInt (4.0f * u), { 0, 1 }).drawForPath (g, shape);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff6f2ea), 0.0f, r.getY(), juce::Colour (0xffe5dccf), 0.0f, r.getBottom(), false));
    g.fillPath (shape);
    g.setColour (juce::Colour (0xffc9bfb1).withAlpha (0.8f));
    g.strokePath (shape, juce::PathStrokeType (1.0f));
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.drawHorizontalLine (juce::roundToInt (r.getY() + 1.0f), r.getX() + 12.0f * u, r.getRight() - 12.0f * u);

    const float x = r.getX() + 27.5f * u;
    g.setColour (colour::text);
    g.setFont (fonts::make (34.0f * u, fonts::Weight::medium, 0.0f));
    g.drawText (title, juce::Rectangle<float> (x, r.getY() + 13.0f * u, r.getWidth() - 100.0f * u, 46.0f * u), juce::Justification::centredLeft, false);
    if (subtitle.isNotEmpty())
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::make (16.0f * u, fonts::Weight::regular, 0.3f));
        g.drawText (subtitle, juce::Rectangle<float> (x + 1.0f * u, r.getY() + 54.0f * u, r.getWidth() - 60.0f * u, 22.0f * u), juce::Justification::centredLeft, false);
    }
    const auto box = closeButton();
    if (isMouseOver() && box.contains (getMouseXYRelative().toFloat()))
    {
        g.setColour (juce::Colour (0x18000000));
        g.fillRoundedRectangle (box, 6.0f * u);
    }
    g.setColour (colour::text.withAlpha (0.9f));
    const auto cross = juce::Rectangle<float> (17.0f * u, 17.0f * u).withCentre (box.getCentre());
    g.drawLine (cross.getX(), cross.getY(), cross.getRight(), cross.getBottom(), 2.0f * u);
    g.drawLine (cross.getRight(), cross.getY(), cross.getX(), cross.getBottom(), 2.0f * u);
}

void MiniPanel::resized()
{
    const float u = unit();
    auto area = card().toFloat();
    if (compact())
    {
        layoutContent (area.withTrimmedLeft (12.0f).withTrimmedRight (12.0f).withTrimmedTop (32.0f).withTrimmedBottom (10.0f).toNearestInt());
        return;
    }
    area = area.withTrimmedLeft (25.0f * u).withTrimmedRight (23.0f * u).withTrimmedTop (91.0f * u).withTrimmedBottom (26.0f * u);
    layoutContent (area.toNearestInt());
}

//==============================================================================
namespace
{
    juce::String parameterText (juce::RangedAudioParameter* p)
    {
        if (p == nullptr)
            return {};
        const auto label = p->getLabel();
        return p->getCurrentValueAsText() + (label.isNotEmpty() ? " " + label : juce::String());
    }

    /** Advanced (spec §13): what a musician rarely needs, out of the way. */
    class AdvancedPopup final : public MiniPanel
    {
    public:
        explicit AdvancedPopup (OspAudioProcessor& p) : MiniPanel ("ADVANCED", "TUNING, VOICES & PITCH"), processor (p)
        {
            auto& state = processor.parameters;
            auto add = [this, &state] (const char* id, const char* caption, MiniKnob::Formatter f) {
                knobs.push_back (std::make_unique<MiniKnob> (state, id, caption, std::move (f)));
                knobs.back()->setBoxed (true);
                addAndMakeVisible (*knobs.back());
            };
            // OUTPUT is the header's VOLUME, sustain each layer's LOOP, the velocity range
            // DYNAMICS' RANGE: Advanced keeps what a musician rarely needs.
            auto* fine = state.getParameter ("fineTune");
            add ("fineTune", "FINE", [fine] (double) { return parameterText (fine); });
            add ("bendRange", "BEND", [] (double v) { return juce::String (juce::roundToInt (v)) + " st"; });
            add ("glide", "GLIDE", [] (double ms) { return ms < 0.5 ? juce::String ("Off") : format::milliseconds (ms); });
            glide = knobs.back().get();

            // Mono: one note at a time, legato (no restart while a key is held), last key
            // wins, GLIDE slides between notes. For basses and leads.
            voices = std::make_unique<SegmentedControl> (*state.getParameter ("voiceMode"), juce::StringArray { "POLY", "MONO" });
            voices->setTooltip ("Mono: one note at a time; a key played while another is held slides to it (legato) over GLIDE");
            voices->onChange = [this] (int) { updateGlide(); };
            addAndMakeVisible (*voices);
            updateGlide();

            pitch = std::make_unique<SegmentedControl> (*state.getParameter ("pitchCharacter"), juce::StringArray { "TAPE", "NATURAL" });
            pitch->setTooltip ("Pitch character: tape-like resampling or natural (formants kept)");
            addAndMakeVisible (*pitch);

            mpe.setTooltip ("MPE controllers: per-note pitch bend (+/-48 st), pressure and slide");
            mpeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, "mpe", mpe);
            addAndMakeVisible (mpe);
            reseed.setTooltip ("New variation seed: repeated notes vary differently");
            reseed.onClick = [this] {
                if (auto* seed = processor.parameters.getParameter ("seed"))
                {
                    const auto range = processor.parameters.getParameterRange ("seed");
                    const float next = std::fmod (range.convertFrom0to1 (seed->getValue()), 9999.0f) + 1.0f;
                    seed->setValueNotifyingHost (range.convertTo0to1 (next));
                    repaint();
                }
            };
            addAndMakeVisible (reseed);
        }

        // Laid out at 0.69 of the reference's unit; the editor scales it up to match the macro popups.
        float unit() const override { return 0.69f; }
        juce::Point<int> cardSize() const override { return { 300, headerHeight() + knobRow + 8 + 12 + 24 + 8 + 12 + 24 + 10 + 26 + juce::roundToInt (26.0f * unit()) }; }

        void paint (juce::Graphics& g) override
        {
            MiniPanel::paint (g);
            g.setColour (palette::textDim);
            g.setFont (fonts::label (9.5f));
            g.drawText ("VOICES", voicesCaption, juce::Justification::centredLeft, false);
            g.drawText ("PITCH CHARACTER", pitchCaption, juce::Justification::centredLeft, false);
            if (auto* seed = processor.parameters.getParameter ("seed"))
                g.drawText ("SEED " + seed->getCurrentValueAsText(), seedCaption, juce::Justification::centredRight, false);
        }

    private:
        void layoutContent (juce::Rectangle<int> area) override
        {
            auto row = area.removeFromTop (knobRow);
            const int cell = row.getWidth() / static_cast<int> (knobs.size());
            for (auto& k : knobs)
                k->setBounds (row.removeFromLeft (cell));
            area.removeFromTop (8);
            voicesCaption = area.removeFromTop (12);
            voices->setBounds (area.removeFromTop (24));
            area.removeFromTop (8);
            pitchCaption = area.removeFromTop (12);
            pitch->setBounds (area.removeFromTop (24));
            area.removeFromTop (10);
            auto last = area.removeFromTop (26);
            mpe.setBounds (last.removeFromLeft (80));
            reseed.setBounds (last.removeFromRight (90));
            last.removeFromRight (8);
            seedCaption = last;
        }

        static constexpr int knobRow = 104;   ///< the popups' boxed knob cells, at this panel's unit

        /** GLIDE only acts in Mono: dimmed (still adjustable) in Poly. */
        void updateGlide()
        {
            if (glide != nullptr && voices != nullptr)
                glide->setAlpha (voices->selected() == 1 ? 1.0f : 0.45f);
        }

        OspAudioProcessor& processor;
        std::vector<std::unique_ptr<MiniKnob>> knobs;
        MiniKnob* glide = nullptr;
        std::unique_ptr<SegmentedControl> voices, pitch;
        juce::ToggleButton mpe { "MPE" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mpeAttachment;
        juce::TextButton reseed { "Reseed" };
        juce::Rectangle<int> voicesCaption, pitchCaption, seedCaption;
    };
}

const juce::StringArray& popupParameterIds (MacroPopup macro)
{
    static const juce::StringArray life { "life.mode", "life.pitch", "life.tone", "life.attack" };
    static const juce::StringArray dynamics { "dynamics.curve", "velocityRange", "dynamics.tone" };
    static const juce::StringArray character { "character.type", "character.min", "character.max", "character.resonance",
                                               "character.drive", "character.envAmount", "character.envAttack", "character.envDecay" };
    static const juce::StringArray movement { "movement.mode", "movement.drift.speed", "movement.drift.pitch", "movement.drift.tone",
                                              "movement.tape.wow", "movement.tape.flutter", "movement.tape.wear",
                                              "movement.chorus.rate", "movement.chorus.width", "movement.chorus.stereo",
                                              "movement.pulse.rate", "movement.pulse.shape", "movement.pulse.stereo",
                                              "movement.shaper.pattern", "movement.shaper.rate", "movement.shaper.target",
                                              "movement.shaper.smooth" };
    static const juce::StringArray space { "space.type", "space.decay" };
    switch (macro)
    {
        case MacroPopup::life: return life;
        case MacroPopup::dynamics: return dynamics;
        case MacroPopup::character: return character;
        case MacroPopup::movement: return movement;
        case MacroPopup::space: break;
    }
    return space;
}

std::unique_ptr<MiniPanel> createAdvancedPopup (OspAudioProcessor& processor)
{
    return std::make_unique<AdvancedPopup> (processor);
}

} // namespace osp::plugin
