#include "RackControls.h"
#include "OspLookAndFeel.h"

#include <cmath>

namespace osp::plugin::rackui
{

namespace palette
{
    namespace
    {
        // The approved active segment (MOVEMENT): its saturation and brightness carry to the
        // other modules' hues, so every module's chosen segment is equally quiet.
        juce::Colour inHue (juce::Colour approved, juce::Colour accent)
        {
            const float approvedHue = segmentActive.getHue();
            if (std::abs (accent.getHue() - approvedHue) < 0.06f)
                return approved;
            return juce::Colour::fromHSV (accent.getHue(), approved.getSaturation(), approved.getBrightness(), 1.0f);
        }
    }
    juce::Colour activeFill (juce::Colour accent) { return inHue (segmentActive, accent); }
    juce::Colour activeText (juce::Colour accent) { return inHue (segmentActiveText, accent); }
}

namespace
{
    juce::Font labelFont (bool active) { return fonts::make (9.0f, active ? fonts::Weight::bold : fonts::Weight::regular, 0.02f); }

    void focusRing (juce::Graphics& g, juce::Rectangle<float> r, float radius)
    {
        g.setColour (palette::focus);
        g.drawRoundedRectangle (r.expanded (3.0f), radius + 3.0f, 2.0f);
    }

    /** The recessed slot (approved: 20 x 61, radius 11, a 2 px rim, a dark channel). */
    void paintTrack (juce::Graphics& g, juce::Rectangle<float> t)
    {
        // The rim's light catch below, the rim, then the dark channel shaded across.
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillRoundedRectangle (t.translated (0.0f, 1.0f), 11.0f);
        g.setColour (palette::trackEdge);
        g.fillRoundedRectangle (t, 11.0f);
        const auto inner = t.reduced (2.0f);
        juce::ColourGradient across (palette::trackDark, inner.getX(), 0.0f, palette::trackDark, inner.getRight(), 0.0f, false);
        across.addColour (0.5, palette::trackMid);
        g.setGradientFill (across);
        g.fillRoundedRectangle (inner, 9.0f);
        // Its inner shadow (inset 1 2 5 px): darker at the top and the left.
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xb5050505), 0.0f, inner.getY(), juce::Colour (0x00050505), 0.0f, inner.getY() + 8.0f, false));
        g.fillRoundedRectangle (inner, 9.0f);
    }

    /** The raised ivory actuator (approved: 17 x 19, radius 9, lit from the top left). */
    void paintActuator (juce::Graphics& g, juce::Rectangle<float> a)
    {
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillRoundedRectangle (a.translated (0.0f, 1.5f).expanded (0.5f), 9.0f);
        // 145 degrees: from the top left to the bottom right.
        juce::ColourGradient face (palette::actuatorHi, a.getX(), a.getY(), palette::actuatorLo, a.getRight(), a.getBottom(), false);
        face.addColour (0.73, palette::actuatorMid);
        g.setGradientFill (face);
        g.fillRoundedRectangle (a, 9.0f);
        g.setColour (palette::actuatorEdge);
        g.drawRoundedRectangle (a.reduced (0.5f), 8.5f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawLine (a.getX() + 5.0f, a.getY() + 1.5f, a.getRight() - 5.0f, a.getY() + 1.5f, 1.0f);
    }
}

//==============================================================================
VerticalSwitch::VerticalSwitch (juce::RangedAudioParameter& p, std::vector<Position> pos, juce::UndoManager* undo, Style s)
    : parameter (p), positions (std::move (pos)), undoManager (undo), style (s),
      attachment (p, [this] (float plain) {
          const int now = indexForValue (plain);
          if (now != shown)
          {
              shown = now;
              repaint();
              if (onChange != nullptr)
                  onChange (shown);
          }
      }, nullptr)   // (the control opens its own, named undo transaction: choose())
{
    jassert (positions.size() == 2 || positions.size() == 3);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);   // (the focus ring is the keyboard's only)
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle (p.getName (64));
    attachment.sendInitialUpdate();
}

int VerticalSwitch::indexForValue (float plain) const
{
    int best = 0;
    float distance = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < positions.size(); ++i)
        if (const float d = std::abs (positions[i].value - plain); d < distance)
        {
            distance = d;
            best = static_cast<int> (i);
        }
    return best;
}

void VerticalSwitch::choose (int index)
{
    index = juce::jlimit (0, positionCount() - 1, index);
    if (index == shown)
        return;
    if (undoManager != nullptr)
        undoManager->beginNewTransaction (parameter.getName (64));
    attachment.setValueAsCompleteGesture (positions[static_cast<std::size_t> (index)].value);
}

juce::Point<int> VerticalSwitch::preferredSize (int count, Style s)
{
    if (s == Style::mini)
        return { 16, 26 };
    return count == 2 ? juce::Point<int> (44, 83) : juce::Point<int> (66, 79);
}

juce::Rectangle<float> VerticalSwitch::trackBounds() const
{
    const auto r = getLocalBounds().toFloat();
    if (style == Style::mini)
        return r.withSizeKeepingCentre (16.0f, 26.0f);
    if (positionCount() == 2)
        return { r.getCentreX() - 10.0f, r.getCentreY() - 30.5f, 20.0f, 61.0f };
    // Three: the slot and the label column (8 px apart) centred together.
    const float total = 20.0f + 8.0f + 38.0f;
    return { r.getCentreX() - 0.5f * total, r.getCentreY() - 30.5f, 20.0f, 61.0f };
}

juce::Rectangle<float> VerticalSwitch::actuatorBounds (int index) const
{
    const auto t = trackBounds();
    if (style == Style::mini)
        return { t.getX() + 3.0f, t.getY() + 3.0f + (index == 0 ? 0.0f : 9.0f), 10.0f, 12.0f };
    const float travel = 39.0f * static_cast<float> (index) / static_cast<float> (positionCount() - 1);
    return { t.getX() + 2.0f, t.getY() + 2.0f + travel, 17.0f, 19.0f };
}

juce::Rectangle<float> VerticalSwitch::labelBounds (int index) const
{
    if (style == Style::mini)
        return {};
    const auto r = getLocalBounds().toFloat();
    const auto t = trackBounds();
    if (positionCount() == 2)
        return index == 0 ? juce::Rectangle<float> (r.getX(), t.getY() - 11.0f, r.getWidth(), 10.0f)
                          : juce::Rectangle<float> (r.getX(), t.getBottom() + 1.0f, r.getWidth(), 10.0f);
    // Three: on the right, spread over 75 px around the slot's centre (as approved).
    const float lineHeight = 10.8f, column = 75.0f;
    const float top = t.getCentreY() - 0.5f * column;
    return { t.getRight() + 8.0f, top + static_cast<float> (index) * 0.5f * (column - lineHeight), 38.0f, lineHeight };
}

int VerticalSwitch::indexAt (juce::Point<float> p) const
{
    for (int i = 0; i < positionCount(); ++i)
        if (labelBounds (i).expanded (2.0f, 1.0f).contains (p))
            return i;
    const auto t = trackBounds();
    const float y = juce::jlimit (0.0f, 1.0f, (p.y - t.getY()) / t.getHeight());
    return juce::roundToInt (y * static_cast<float> (positionCount() - 1));
}

void VerticalSwitch::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;
    if (style == Style::mini)
    {
        choose (shown == 0 ? 1 : 0);   // two positions: a click throws it
        return;
    }
    choose (indexAt (e.position));
}

void VerticalSwitch::mouseDrag (const juce::MouseEvent& e)
{
    if (! isEnabled() || style == Style::mini)
        return;
    // The actuator follows the pointer only from detent to detent.
    const auto t = trackBounds();
    const float y = juce::jlimit (0.0f, 1.0f, (e.position.y - t.getY()) / t.getHeight());
    choose (juce::roundToInt (y * static_cast<float> (positionCount() - 1)));
}

bool VerticalSwitch::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::upKey)
        choose (shown - 1);
    else if (key == juce::KeyPress::downKey)
        choose (shown + 1);
    else if (key == juce::KeyPress::homeKey)
        choose (0);
    else if (key == juce::KeyPress::endKey)
        choose (positionCount() - 1);
    else if (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey)
        choose ((shown + 1) % positionCount());
    else
        return false;
    return true;
}

void VerticalSwitch::paint (juce::Graphics& g)
{
    if (! isEnabled())
        g.beginTransparencyLayer (0.5f);
    const auto t = trackBounds();
    if (style == Style::mini)
    {
        // The header's lane switch: a dark pill, its small ivory thumb up (ON) or down.
        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (t.translated (0.0f, 1.0f), 9.0f);
        g.setColour (palette::miniEdge);
        g.fillRoundedRectangle (t, 9.0f);
        g.setColour (palette::miniBody);
        g.fillRoundedRectangle (t.reduced (1.0f), 8.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x88000000), 0.0f, t.getY() + 1.0f, juce::Colour (0x00000000), 0.0f, t.getY() + 6.0f, false));
        g.fillRoundedRectangle (t.reduced (1.0f), 8.0f);
        const auto thumb = actuatorBounds (shown);
        g.setColour (juce::Colours::black.withAlpha (0.33f));
        g.fillRoundedRectangle (thumb.translated (0.0f, 1.0f), 6.0f);
        g.setGradientFill (juce::ColourGradient (palette::miniThumbHi, 0.0f, thumb.getY(), palette::miniThumbLo, 0.0f, thumb.getBottom(), false));
        g.fillRoundedRectangle (thumb, 6.0f);
        if (hasKeyboardFocus (false))
            focusRing (g, t, 9.0f);
        if (! isEnabled())
            g.endTransparencyLayer();
        return;
    }
    paintTrack (g, t);
    paintActuator (g, actuatorBounds (shown));
    const bool two = positionCount() == 2;
    for (int i = 0; i < positionCount(); ++i)
    {
        const bool active = i == shown;
        g.setFont (labelFont (active));
        g.setColour (active ? palette::switchActive : (two ? palette::switchLabelTwo : palette::switchLabel));
        g.drawText (positions[static_cast<std::size_t> (i)].label, labelBounds (i), two ? juce::Justification::centred : juce::Justification::centredLeft, false);
    }
    if (hasKeyboardFocus (false))
        focusRing (g, t, 11.0f);
    if (! isEnabled())
        g.endTransparencyLayer();
}

std::unique_ptr<juce::AccessibilityHandler> VerticalSwitch::createAccessibilityHandler()
{
    // A slider-like value with the position's name ("Ping-Pong"), stepped by the arrow keys.
    // Its value spoken by name ("Ping-Pong"); a name sets it.
    struct Value final : juce::AccessibilityTextValueInterface
    {
        explicit Value (VerticalSwitch& s) : owner (s) {}
        bool isReadOnly() const override { return false; }
        juce::String getCurrentValueAsString() const override
        {
            const auto& p = owner.positions[static_cast<std::size_t> (owner.shown)];
            return p.spoken.isNotEmpty() ? p.spoken : p.label;
        }
        void setValueAsString (const juce::String& text) override
        {
            for (int i = 0; i < owner.positionCount(); ++i)
            {
                const auto& p = owner.positions[static_cast<std::size_t> (i)];
                if (text.equalsIgnoreCase (p.label) || text.equalsIgnoreCase (p.spoken))
                    owner.choose (i);
            }
        }
        VerticalSwitch& owner;
    };
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider,
                                                          juce::AccessibilityActions().addAction (juce::AccessibilityActionType::press, [this] { choose ((shown + 1) % positionCount()); }),
                                                          juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
}

//==============================================================================
SegmentedSwitch::SegmentedSwitch (juce::RangedAudioParameter& p, std::vector<Position> pos, juce::UndoManager* undo, juce::Colour accent)
    : parameter (p), positions (std::move (pos)), undoManager (undo), fill (palette::activeFill (accent)), text (palette::activeText (accent)),
      attachment (p, [this] (float plain) {
          const int now = indexForValue (plain);
          if (now != shown)
          {
              shown = now;
              repaint();
              if (onChange != nullptr)
                  onChange (shown);
          }
      }, nullptr)   // (the control opens its own, named undo transaction: choose())
{
    jassert (positions.size() >= 2 && positions.size() <= 4);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle (p.getName (64));
    attachment.sendInitialUpdate();
}

int SegmentedSwitch::indexForValue (float plain) const
{
    int best = 0;
    float distance = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < positions.size(); ++i)
        if (const float d = std::abs (positions[i].value - plain); d < distance)
        {
            distance = d;
            best = static_cast<int> (i);
        }
    return best;
}

void SegmentedSwitch::choose (int index)
{
    index = juce::jlimit (0, static_cast<int> (positions.size()) - 1, index);
    if (index == shown)
        return;
    if (undoManager != nullptr)
        undoManager->beginNewTransaction (parameter.getName (64));
    attachment.setValueAsCompleteGesture (positions[static_cast<std::size_t> (index)].value);
}

juce::Rectangle<float> SegmentedSwitch::segmentBounds (int index) const
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float w = r.getWidth() / static_cast<float> (positions.size());
    return { r.getX() + w * static_cast<float> (index), r.getY(), w, r.getHeight() };
}

void SegmentedSwitch::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled() || e.mods.isPopupMenu())
        return;
    for (int i = 0; i < static_cast<int> (positions.size()); ++i)
        if (segmentBounds (i).contains (e.position))
            choose (i);
}

bool SegmentedSwitch::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::upKey)
        choose (shown - 1);
    else if (key == juce::KeyPress::rightKey || key == juce::KeyPress::downKey)
        choose (shown + 1);
    else if (key == juce::KeyPress::homeKey)
        choose (0);
    else if (key == juce::KeyPress::endKey)
        choose (static_cast<int> (positions.size()) - 1);
    else
        return false;
    return true;
}

void SegmentedSwitch::paint (juce::Graphics& g)
{
    // Approved: 27 px, radius 4, a cream key with engraved dividers; the chosen segment tinted
    // and pressed in (inset 0 1 2 px).
    if (! isEnabled())
        g.beginTransparencyLayer (0.5f);
    const auto r = getLocalBounds().toFloat();
    g.setColour (palette::segmentEdge);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (palette::segmentFill);
    g.fillRoundedRectangle (r.reduced (1.0f), 3.0f);
    {
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path inside;
        inside.addRoundedRectangle (r.reduced (1.0f), 3.0f);
        g.reduceClipRegion (inside);
        const auto a = segmentBounds (shown);
        g.setColour (fill);
        g.fillRect (a);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x32647783), 0.0f, a.getY(), juce::Colour (0x00647783), 0.0f, a.getY() + 3.0f, false));
        g.fillRect (a);
    }
    g.setColour (palette::segmentDivider);
    for (int i = 1; i < static_cast<int> (positions.size()); ++i)
        g.fillRect (juce::Rectangle<float> (segmentBounds (i).getX() - 0.5f, r.getY() + 1.0f, 1.0f, r.getHeight() - 2.0f));
    for (int i = 0; i < static_cast<int> (positions.size()); ++i)
    {
        const bool active = i == shown;
        g.setFont (fonts::make (10.0f, active ? fonts::Weight::medium : fonts::Weight::regular, 0.0f));
        g.setColour (active ? text : palette::segmentText);
        g.drawText (positions[static_cast<std::size_t> (i)].label, segmentBounds (i), juce::Justification::centred, false);
    }
    if (hasKeyboardFocus (false))
        focusRing (g, r, 4.0f);
    if (! isEnabled())
        g.endTransparencyLayer();
}

std::unique_ptr<juce::AccessibilityHandler> SegmentedSwitch::createAccessibilityHandler()
{
    // Its value spoken by name ("Ping-Pong"); a name sets it.
    struct Value final : juce::AccessibilityTextValueInterface
    {
        explicit Value (SegmentedSwitch& s) : owner (s) {}
        bool isReadOnly() const override { return false; }
        juce::String getCurrentValueAsString() const override
        {
            const auto& p = owner.positions[static_cast<std::size_t> (owner.shown)];
            return p.spoken.isNotEmpty() ? p.spoken : p.label;
        }
        void setValueAsString (const juce::String& text) override
        {
            for (int i = 0; i < static_cast<int> (owner.positions.size()); ++i)
            {
                const auto& p = owner.positions[static_cast<std::size_t> (i)];
                if (text.equalsIgnoreCase (p.label) || text.equalsIgnoreCase (p.spoken))
                    owner.choose (i);
            }
        }
        SegmentedSwitch& owner;
    };
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider, juce::AccessibilityActions(),
                                                          juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
}

//==============================================================================
LaneKey::LaneKey (const juce::String& letter) : juce::Button (letter)
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void LaneKey::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    // Approved: 40 x 23, radius 4; the edited lane's key tinted; its own light (the lane plays).
    const auto r = getLocalBounds().toFloat().reduced (0.5f, 0.5f).withTrimmedBottom (1.0f);
    const bool edited = getToggleState();
    g.setColour (juce::Colour (0x21695f51));
    g.fillRoundedRectangle (r.translated (0.0f, 1.0f), 4.0f);
    g.setColour (edited ? palette::laneActiveFill : (highlighted || down ? palette::laneFill.darker (0.03f) : palette::laneFill));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (edited ? palette::laneActiveEdge : palette::laneEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 3.5f, 1.0f);
    const auto font = fonts::make (10.0f, edited ? fonts::Weight::medium : fonts::Weight::regular, 0.0f);
    const float tw = juce::GlyphArrangement::getStringWidth (font, getButtonText());
    const float left = r.getCentreX() - 0.5f * (5.0f + 4.0f + tw);
    const bool on = lit == nullptr || lit();
    const auto led = juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ left + 2.5f, r.getCentreY() });
    if (on)
    {
        g.setColour (palette::ledLive.withAlpha (0.45f));
        g.fillEllipse (led.expanded (1.5f));
    }
    g.setColour (on ? palette::ledLive : palette::ledOff);
    g.fillEllipse (led);
    g.setColour (edited ? palette::laneActiveText : palette::laneText);
    g.setFont (font);
    g.drawText (getButtonText(), juce::Rectangle<float> (left + 9.0f, r.getY(), tw + 2.0f, r.getHeight()), juce::Justification::centredLeft, false);
    if (hasKeyboardFocus (false))
        focusRing (g, r, 4.0f);
}

void LaneKey::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onMenu != nullptr)
            onMenu();
        return;
    }
    juce::Button::mouseDown (e);
}

void LaneKey::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        juce::Button::mouseUp (e);
}

bool LaneKey::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress (juce::KeyPress::F10Key, juce::ModifierKeys::shiftModifier, 0) && onMenu != nullptr)
    {
        onMenu();
        return true;
    }
    return juce::Button::keyPressed (key);
}

//==============================================================================
int slotWidth (Slot slot, bool dense) noexcept
{
    switch (slot)
    {
        case Slot::slim: return dense ? 85 : 95;
        case Slot::wide: return dense ? 100 : 119;
        case Slot::twoKnobs: return dense ? 2 * 89 + 5 : 2 * 104 + 7;
        case Slot::knob: break;
    }
    return dense ? 89 : 104;
}

Cell::Cell (juce::String c, std::unique_ptr<juce::Component> k, Kind kd) : caption (std::move (c)), control (std::move (k)), kind (kd)
{
    addAndMakeVisible (*control);
}

Slot Cell::slot() const noexcept
{
    return kind == Kind::switch2 ? Slot::slim : (kind == Kind::list ? Slot::knob : Slot::wide);
}

void Cell::resized()
{
    const int w = getWidth();
    switch (kind)
    {
        case Kind::list: control->setBounds (juce::Rectangle<int> (juce::roundToInt (0.94f * static_cast<float> (w)), 25).withCentre ({ w / 2, controlCentre })); break;
        case Kind::segments: control->setBounds (juce::Rectangle<int> (w, 27).withCentre ({ w / 2, controlCentre })); break;
        case Kind::switch2:
        case Kind::switch3:
        {
            // The track's centre on the centreline (the switch keeps its track centred in itself).
            const auto size = VerticalSwitch::preferredSize (kind == Kind::switch2 ? 2 : 3, VerticalSwitch::Style::slot);
            control->setBounds (juce::Rectangle<int> (size.x, size.y).withCentre ({ w / 2, controlCentre }));
            jassert (std::abs (control->getY() + static_cast<VerticalSwitch&> (*control).trackBounds().getCentreY() - static_cast<float> (controlCentre)) < 1.0f);
            break;
        }
    }
}

void Cell::paint (juce::Graphics& g)
{
    // (a rack knob's caption: the same type, colour and place)
    g.setColour (quiet ? palette::quietCaption : palette::caption);
    g.setFont (fonts::make (11.0f, fonts::Weight::medium, 0.055f));
    g.drawText (caption, juce::Rectangle<float> (-6.0f, 0.0f, static_cast<float> (getWidth()) + 12.0f, static_cast<float> (captionHeight)), juce::Justification::centred, false);
}

//==============================================================================
void paintDetents (juce::Graphics& g, juce::Rectangle<float> knob, int steps, int chosen, juce::Colour accent)
{
    if (steps < 2)
        return;
    // As the approved rate study: a 2 x 5 px tick per step just outside the knob, the chosen
    // one 3 x 7 in the module's colour; over the knob's 270 degrees.
    const auto c = knob.getCentre();
    const float r = 0.5f * std::min (knob.getWidth(), knob.getHeight()) + 4.0f;
    for (int i = 0; i < steps; ++i)
    {
        const bool active = i == chosen;
        const float angle = juce::degreesToRadians (-135.0f + 270.0f * static_cast<float> (i) / static_cast<float> (steps - 1));
        const float len = active ? 7.0f : 5.0f, w = active ? 3.0f : 2.0f;
        juce::Path tick;
        tick.addRoundedRectangle (-0.5f * w, -(r + len), w, len, 0.5f * w);
        g.setColour (active ? accent.darker (0.15f) : juce::Colour (0xffaaa49a));
        g.fillPath (tick, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }
}

} // namespace osp::plugin::rackui
