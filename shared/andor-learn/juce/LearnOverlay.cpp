#include "LearnOverlay.h"

#include "Design.h"
#include "OspLookAndFeel.h"

namespace andor::learn
{

namespace design = osp::plugin::design;
namespace fonts = osp::plugin::fonts;
namespace type = osp::plugin::type;

//==============================================================================
HighlightLayer::HighlightLayer()
{
    setInterceptsMouseClicks (false, false);
    setAccessible (false);
}

void HighlightLayer::setTarget (std::optional<juce::Rectangle<int>> target, bool quiet)
{
    if (target == ring && quiet == quietRing)
        return;
    auto dirty = ring.value_or (juce::Rectangle<int>()).expanded (12);
    ring = target;
    quietRing = quiet;
    dirty = dirty.getUnion (ring.value_or (juce::Rectangle<int>()).expanded (12));
    repaint (dirty);
}

void HighlightLayer::paint (juce::Graphics& g)
{
    if (! ring.has_value())
        return;
    // A modest ring just outside the control: a soft halo, then a 1.5 px accent line. The
    // control itself stays as it is (nothing drawn over it).
    const auto r = ring->toFloat().expanded (4.0f);
    const auto accent = design::colour::accent;
    juce::Path shape;
    shape.addRoundedRectangle (r, 9.0f);
    g.setColour (accent.withAlpha (quietRing ? 0.10f : 0.16f));
    g.strokePath (shape, juce::PathStrokeType (6.0f));
    g.setColour (accent.withAlpha (quietRing ? 0.55f : 0.95f));
    g.strokePath (shape, juce::PathStrokeType (quietRing ? 1.0f : 1.5f));
}

//==============================================================================
class Note::Key final : public juce::Button
{
public:
    Key (const Action& a) : juce::Button (a.label), primary (a.primary), id (a.id)
    {
        setWantsKeyboardFocus (true);
        setMouseClickGrabsKeyboardFocus (false);   // a click never takes the DAW's keys
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle (a.label);
    }
    const bool primary;
    const juce::String id;

    int preferredWidth() const
    {
        return juce::roundToInt (juce::GlyphArrangement::getStringWidth (type::popupLabel (11.5f), getButtonText())) + 22;
    }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
        {
            triggerClick();
            return true;
        }
        return juce::Button::keyPressed (key);
    }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const float radius = r.getHeight() * 0.5f;
        if (primary)
        {
            const auto top = design::colour::accentTop, bottom = design::colour::accentBottom;
            g.setGradientFill (juce::ColourGradient (down ? bottom : top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, radius);
            g.setColour (juce::Colours::white.withAlpha (over ? 1.0f : 0.95f));
        }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (down ? 0.35f : over ? 0.75f : 0.55f));
            g.fillRoundedRectangle (r, radius);
            g.setColour (design::colour::hairline.darker (over ? 0.12f : 0.0f));
            g.drawRoundedRectangle (r, radius, 1.0f);
            g.setColour (design::colour::text.withAlpha (0.85f));
        }
        g.setFont (type::popupLabel (11.5f));
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
        if (hasKeyboardFocus (false))
        {
            g.setColour (design::colour::accent.withAlpha (0.8f));
            g.drawRoundedRectangle (r.expanded (1.5f), radius + 1.5f, 1.0f);
        }
    }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
};

namespace
{
    constexpr float pad = 16.0f;
    constexpr float metaHeight = 16.0f, titleHeight = 22.0f, keyHeight = 26.0f, doneHeight = 18.0f;
    juce::Font bodyFont() { return fonts::make (12.5f, fonts::Weight::regular, 0.0f); }
} // namespace

Note::Note()
{
    setWantsKeyboardFocus (false);
    setTitle ("ANDOR Learn");
    setAccessible (true);
}

Note::~Note() = default;

void Note::setContent (const juce::String& m, const juce::String& t, const juce::String& b, std::vector<Action> a, bool d)
{
    meta = m;
    title = t;
    body = b;
    done = d;
    actions = std::move (a);
    keys.clear();
    for (const auto& action : actions)
    {
        auto key = std::make_unique<Key> (action);
        const auto id = action.id;
        key->onClick = [this, id] {
            // (async: a key may close and delete this note)
            juce::Component::SafePointer<Note> safe (this);
            juce::MessageManager::callAsync ([safe, id] {
                if (safe != nullptr && safe->onAction != nullptr)
                    safe->onAction (id);
            });
        };
        addAndMakeVisible (*key);
        keys.push_back (std::move (key));
    }
    setDescription (title + ". " + body);
    const auto size = preferredSize();
    setSize (size.x, size.y);
    resized();
    repaint();
}

juce::TextLayout Note::layoutBody (float width) const
{
    juce::AttributedString text;
    text.append (body, bodyFont(), design::colour::textSecondary);
    text.setLineSpacing (2.0f);
    juce::TextLayout layout;
    layout.createLayout (text, width);
    return layout;
}

int Note::bodyHeight() const
{
    if (body.isEmpty())
        return 0;
    return juce::roundToInt (std::ceil (layoutBody (static_cast<float> (cardWidth) - 2.0f * pad).getHeight()));
}

juce::Point<int> Note::preferredSize() const
{
    float h = pad;
    if (meta.isNotEmpty())
        h += metaHeight;
    h += titleHeight + 4.0f + static_cast<float> (bodyHeight());
    if (done)
        h += doneHeight + 4.0f;
    if (! actions.empty())
        h += 12.0f + keyHeight;
    h += pad - 2.0f;
    return { cardWidth + 2 * margin, juce::roundToInt (h) + 2 * margin };
}

juce::Rectangle<float> Note::closeArea() const
{
    const auto c = card().toFloat();
    return { c.getRight() - 30.0f, c.getY() + 8.0f, 22.0f, 22.0f };
}

void Note::resized()
{
    // The keys on the bottom row, right-aligned, the primary last (the reading order ends on it).
    auto row = card().toFloat().reduced (pad, 0.0f);
    row = row.removeFromBottom (pad - 2.0f + keyHeight).removeFromTop (keyHeight);
    float x = row.getRight();
    for (auto it = keys.rbegin(); it != keys.rend(); ++it)
    {
        const float w = static_cast<float> ((*it)->preferredWidth());
        (*it)->setBounds (juce::Rectangle<float> (x - w, row.getY(), w, keyHeight).toNearestInt());
        x -= w + 8.0f;
    }
}

void Note::paint (juce::Graphics& g)
{
    const auto r = card().toFloat();
    juce::Path shape;
    shape.addRoundedRectangle (r, 14.0f);
    design::CachedShadow (juce::Colour (0x26281c10), 18, { 0, 6 }).drawForPath (g, shape);
    design::CachedShadow (juce::Colour (0x1c1e1a16), 2, { 0, 1 }).drawForPath (g, shape);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff8f4ec), 0.0f, r.getY(), juce::Colour (0xffeee7db), 0.0f, r.getBottom(), false));
    g.fillPath (shape);
    g.setColour (juce::Colour (0xffd3cabc));
    g.strokePath (shape, juce::PathStrokeType (1.0f));
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.drawHorizontalLine (juce::roundToInt (r.getY() + 1.0f), r.getX() + 14.0f, r.getRight() - 14.0f);

    float y = r.getY() + pad;
    const float x = r.getX() + pad, w = r.getWidth() - 2.0f * pad;
    if (meta.isNotEmpty())
    {
        g.setColour (design::colour::textMicro);
        g.setFont (type::popupTitle (10.0f));
        g.drawText (meta, juce::Rectangle<float> (x, y - 2.0f, w - 24.0f, metaHeight), juce::Justification::centredLeft, true);
        y += metaHeight;
    }
    g.setColour (design::colour::text);
    g.setFont (fonts::make (15.0f, fonts::Weight::medium, 0.0f));
    g.drawText (title, juce::Rectangle<float> (x, y, w - (meta.isEmpty() ? 24.0f : 0.0f), titleHeight), juce::Justification::centredLeft, true);
    y += titleHeight + 4.0f;
    if (body.isNotEmpty())
    {
        const auto layout = layoutBody (w);
        layout.draw (g, { x, y, w, layout.getHeight() });
        y += static_cast<float> (bodyHeight());
    }
    if (done)
    {
        // The task is done: a quiet check, the accent's colour.
        y += 4.0f;
        const auto mark = juce::Rectangle<float> (x, y + 3.0f, 12.0f, 12.0f);
        juce::Path tick;
        tick.startNewSubPath (mark.getX() + 1.5f, mark.getCentreY());
        tick.lineTo (mark.getX() + 4.8f, mark.getBottom() - 2.0f);
        tick.lineTo (mark.getRight() - 1.0f, mark.getY() + 2.0f);
        g.setColour (design::colour::accent);
        g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont (type::popupLabel (11.5f));
        g.drawText ("Done", juce::Rectangle<float> (x + 18.0f, y, w - 18.0f, doneHeight), juce::Justification::centredLeft, false);
    }
    // The close cross (thin, as the popovers').
    const auto c = closeArea().reduced (6.0f);
    g.setColour (design::colour::text.withAlpha (closeHover ? 0.85f : 0.45f));
    g.drawLine (c.getX(), c.getY(), c.getRight(), c.getBottom(), 1.2f);
    g.drawLine (c.getRight(), c.getY(), c.getX(), c.getBottom(), 1.2f);
}

void Note::mouseUp (const juce::MouseEvent& e)
{
    if (closeArea().contains (e.position) && onAction != nullptr)
    {
        juce::Component::SafePointer<Note> safe (this);
        juce::MessageManager::callAsync ([safe] {
            if (safe != nullptr && safe->onAction != nullptr)
                safe->onAction ("close");
        });
    }
}

void Note::mouseMove (const juce::MouseEvent& e)
{
    const bool over = closeArea().contains (e.position);
    if (over != closeHover)
    {
        closeHover = over;
        setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint (closeArea().toNearestInt().expanded (2));
    }
}

void Note::mouseExit (const juce::MouseEvent&)
{
    if (closeHover)
    {
        closeHover = false;
        repaint (closeArea().toNearestInt().expanded (2));
    }
}

juce::StringArray Note::actionIds() const
{
    juce::StringArray ids;
    for (const auto& a : actions)
        ids.add (a.id);
    return ids;
}

juce::Button* Note::button (const juce::String& actionId) const
{
    for (const auto& k : keys)
        if (k->id == actionId)
            return k.get();
    return nullptr;
}

bool Note::trigger (const juce::String& actionId)
{
    if (actionId != "close" && button (actionId) == nullptr)
        return false;
    if (onAction != nullptr)
        onAction (actionId);
    return true;
}

//==============================================================================
HitRegion::HitRegion (juce::String targetId) : id (std::move (targetId))
{
    setInterceptsMouseClicks (true, false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    setTitle ("Learn: " + id);
}

void HitRegion::mouseEnter (const juce::MouseEvent&)
{
    if (onHover != nullptr)
        onHover (id);
}

void HitRegion::mouseExit (const juce::MouseEvent&)
{
    if (onHover != nullptr)
        onHover ({});
}

void HitRegion::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() && onChoose != nullptr)
        onChoose (id);
}

bool HitRegion::keyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) && onChoose != nullptr)
    {
        onChoose (id);
        return true;
    }
    return false;
}

void HitRegion::focusGained (FocusChangeType)
{
    if (onHover != nullptr)
        onHover (id);
}

} // namespace andor::learn
