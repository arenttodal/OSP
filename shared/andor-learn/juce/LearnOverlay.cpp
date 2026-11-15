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
    auto dirty = ring.value_or (juce::Rectangle<int>()).expanded (16);
    ring = target;
    quietRing = quiet;
    dirty = dirty.getUnion (ring.value_or (juce::Rectangle<int>()).expanded (16));
    repaint (dirty);
}

void HighlightLayer::setConnector (std::optional<juce::Line<float>> line)
{
    auto same = [] (const std::optional<juce::Line<float>>& a, const std::optional<juce::Line<float>>& b) {
        return a.has_value() == b.has_value() && (! a.has_value() || (a->getStart() == b->getStart() && a->getEnd() == b->getEnd()));
    };
    if (same (line, link))
        return;
    auto area = [] (const std::optional<juce::Line<float>>& l) {
        return l.has_value() ? juce::Rectangle<float> (l->getStart(), l->getEnd()).expanded (8.0f).getSmallestIntegerContainer() : juce::Rectangle<int>();
    };
    const auto dirty = area (link).getUnion (area (line));
    link = line;
    repaint (dirty);
}

void HighlightLayer::paint (juce::Graphics& g)
{
    if (! ring.has_value())
        return;
    // The exact control, outlined just outside its bounds: a soft green glow, then a 2 px line;
    // the corners follow the control's size (a round key keeps a round outline). The control
    // itself is left as it is.
    const auto green = colour();
    const auto r = ring->toFloat().expanded (padding);
    const float radius = std::min (9.0f, 0.5f * std::min (r.getWidth(), r.getHeight()));
    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    if (quietRing)
    {
        g.setColour (green.withAlpha (0.12f));
        g.strokePath (shape, juce::PathStrokeType (5.0f));
        g.setColour (green.withAlpha (0.7f));
        g.strokePath (shape, juce::PathStrokeType (1.0f));
        return;
    }
    g.setColour (green.withAlpha (0.10f));
    g.strokePath (shape, juce::PathStrokeType (10.0f));
    g.setColour (green.withAlpha (0.22f));
    g.strokePath (shape, juce::PathStrokeType (5.0f));
    g.setColour (green);
    g.strokePath (shape, juce::PathStrokeType (2.0f));
    if (link.has_value())
    {
        // The connector: a fine line from the note to the outline, a small dot where it lands.
        g.setColour (green.withAlpha (0.85f));
        g.drawLine (*link, 1.5f);
        const auto end = link->getEnd();
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (end));
    }
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
        return juce::roundToInt (juce::GlyphArrangement::getStringWidth (type::popupLabel (11.5f), getButtonText())) + 24;
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
    constexpr float metaHeight = 16.0f, titleHeight = 22.0f, keyHeight = 26.0f;
    juce::Font bodyFont() { return fonts::make (12.5f, fonts::Weight::regular, 0.0f); }
    juce::Font statusFont() { return fonts::make (11.5f, fonts::Weight::medium, 0.0f); }
    constexpr float tickRoom = 18.0f;
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
    Content c;
    c.meta = m;
    c.title = t;
    c.body = b;
    c.done = d;
    c.status = d ? juce::String ("Done.") : juce::String();
    c.actions = std::move (a);
    setContent (std::move (c));
}

void Note::setContent (Content c)
{
    content = std::move (c);
    keys.clear();
    for (const auto& action : content.actions)
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
    setDescription (content.title + ". " + content.body + (content.status.isNotEmpty() ? " " + content.status : juce::String()));
    const auto size = preferredSize();
    setSize (size.x, size.y);
    resized();
    repaint();
}

juce::TextLayout Note::layoutText (const juce::String& text, juce::Font font, juce::Colour colour, float width) const
{
    juce::AttributedString a;
    a.append (text, font, colour);
    a.setLineSpacing (3.0f);
    juce::TextLayout layout;
    layout.createLayout (a, width);
    return layout;
}

int Note::textHeight (const juce::String& text, juce::Font font) const
{
    if (text.isEmpty())
        return 0;
    return juce::roundToInt (std::ceil (layoutText (text, font, juce::Colours::black, static_cast<float> (cardWidth) - 2.0f * pad).getHeight()));
}

juce::Point<int> Note::preferredSize() const
{
    float h = pad;
    if (content.meta.isNotEmpty() || content.step.isNotEmpty())
        h += metaHeight + 2.0f;
    h += titleHeight + 4.0f + static_cast<float> (textHeight (content.body, bodyFont()));
    if (content.status.isNotEmpty())
    {
        juce::AttributedString a;
        a.append (content.status, statusFont());
        juce::TextLayout l;
        l.createLayout (a, static_cast<float> (cardWidth) - 2.0f * pad - (content.done ? tickRoom : 0.0f));
        h += 10.0f + std::ceil (l.getHeight());
    }
    if (! content.actions.empty())
        h += 14.0f + keyHeight;
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
    if (content.meta.isNotEmpty() || content.step.isNotEmpty())
    {
        // The lesson's small label on the left, the step count on the right (clear of the cross).
        g.setColour (design::colour::textMicro);
        g.setFont (type::popupTitle (10.0f));
        const auto line = juce::Rectangle<float> (x, y - 2.0f, w - 28.0f, metaHeight);
        g.drawText (content.meta, line, juce::Justification::centredLeft, true);
        g.drawText (content.step, line, juce::Justification::centredRight, false);
        y += metaHeight + 2.0f;
    }
    g.setColour (design::colour::text);
    g.setFont (fonts::make (15.0f, fonts::Weight::medium, 0.0f));
    const bool titleBesideClose = content.meta.isEmpty() && content.step.isEmpty();
    g.drawText (content.title, juce::Rectangle<float> (x, y, w - (titleBesideClose ? 28.0f : 0.0f), titleHeight), juce::Justification::centredLeft, true);
    y += titleHeight + 4.0f;
    if (content.body.isNotEmpty())
    {
        const auto layout = layoutText (content.body, bodyFont(), design::colour::textSecondary, w);
        layout.draw (g, { x, y, w, layout.getHeight() });
        y += static_cast<float> (textHeight (content.body, bodyFont()));
    }
    if (content.status.isNotEmpty())
    {
        // The status: a quiet line under the text - with a check once the task is done.
        y += 10.0f;
        float tx = x;
        if (content.done)
        {
            const auto mark = juce::Rectangle<float> (x, y + 2.0f, 12.0f, 12.0f);
            juce::Path tick;
            tick.startNewSubPath (mark.getX() + 1.5f, mark.getCentreY());
            tick.lineTo (mark.getX() + 4.8f, mark.getBottom() - 2.0f);
            tick.lineTo (mark.getRight() - 1.0f, mark.getY() + 2.0f);
            g.setColour (HighlightLayer::colour().darker (0.25f));
            g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            tx += tickRoom;
        }
        const auto layout = layoutText (content.status, statusFont(), content.done ? design::colour::text : design::colour::textSecondary, w - (tx - x));
        layout.draw (g, { tx, y, w - (tx - x), layout.getHeight() });
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
    for (const auto& a : content.actions)
        ids.add (a.id);
    return ids;
}

juce::StringArray Note::actionLabels() const
{
    juce::StringArray labels;
    for (const auto& a : content.actions)
        labels.add (a.label);
    return labels;
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
