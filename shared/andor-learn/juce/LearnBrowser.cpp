#include "LearnBrowser.h"

#include "Design.h"
#include "OspLookAndFeel.h"

namespace andor::learn
{

namespace design = osp::plugin::design;
namespace fonts = osp::plugin::fonts;
namespace type = osp::plugin::type;

namespace
{
    constexpr float pad = 16.0f, headerHeight = 44.0f, rowHeight = 32.0f, groupHeight = 24.0f;
}

Browser::Browser()
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    setTitle ("Learn ANDOR");
}

juce::String Browser::statusText (Row::Status s)
{
    switch (s)
    {
        case Row::Status::notStarted: return "Not started";
        case Row::Status::inProgress: return "In progress";
        case Row::Status::complete: return "Done";
    }
    return {};
}

void Browser::setRows (std::vector<Row> r)
{
    items = std::move (r);
    hover = focused = -1;
    const auto size = preferredSize();
    setSize (size.x, size.y);
    layoutLines();
    repaint();
}

void Browser::layoutLines()
{
    lines.clear();
    float y = static_cast<float> (margin) + headerHeight;
    const float x = static_cast<float> (margin) + 8.0f, w = static_cast<float> (cardWidth) - 16.0f;
    juce::String group;
    for (int i = 0; i < static_cast<int> (items.size()); ++i)
    {
        const auto& item = items[static_cast<std::size_t> (i)];
        if (item.group != group)
        {
            group = item.group;
            if (group.isNotEmpty())
            {
                lines.push_back ({ -1, group.toUpperCase(), { x, y, w, groupHeight } });
                y += groupHeight;
            }
        }
        lines.push_back ({ i, item.title, { x, y, w, rowHeight } });
        y += rowHeight;
    }
}

juce::Point<int> Browser::preferredSize() const
{
    float h = headerHeight + pad - 4.0f;
    juce::String group;
    for (const auto& item : items)
    {
        if (item.group != group)
        {
            group = item.group;
            if (group.isNotEmpty())
                h += groupHeight;
        }
        h += rowHeight;
    }
    return { cardWidth + 2 * margin, juce::roundToInt (h) + 2 * margin };
}

juce::Rectangle<float> Browser::rowArea (const juce::String& lessonId) const
{
    for (const auto& l : lines)
        if (l.row >= 0 && items[static_cast<std::size_t> (l.row)].lessonId == lessonId)
            return l.area;
    return {};
}

juce::Rectangle<float> Browser::closeArea() const
{
    const auto c = card().toFloat();
    return { c.getRight() - 30.0f, c.getY() + 10.0f, 22.0f, 22.0f };
}

int Browser::rowAt (juce::Point<float> p) const
{
    for (const auto& l : lines)
        if (l.row >= 0 && l.area.contains (p))
            return l.row;
    return -1;
}

void Browser::paint (juce::Graphics& g)
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

    g.setColour (design::colour::text.withAlpha (0.85f));
    g.setFont (type::popupTitle (13.0f));
    g.drawText ("LEARN ANDOR", juce::Rectangle<float> (r.getX() + pad, r.getY() + 12.0f, r.getWidth() - 60.0f, 20.0f), juce::Justification::centredLeft, false);
    const auto c = closeArea().reduced (6.0f);
    g.setColour (design::colour::text.withAlpha (closeHover ? 0.85f : 0.45f));
    g.drawLine (c.getX(), c.getY(), c.getRight(), c.getBottom(), 1.2f);
    g.drawLine (c.getRight(), c.getY(), c.getX(), c.getBottom(), 1.2f);

    for (const auto& l : lines)
    {
        if (l.row < 0)
        {
            g.setColour (design::colour::textMicro);
            g.setFont (type::popupTitle (9.5f));
            g.drawText (l.text, l.area.withTrimmedLeft (8.0f).withTrimmedTop (6.0f), juce::Justification::centredLeft, false);
            continue;
        }
        const auto& item = items[static_cast<std::size_t> (l.row)];
        const auto area = l.area.reduced (0.0f, 2.0f);
        if (l.row == hover || l.row == focused)
        {
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.fillRoundedRectangle (area, 8.0f);
            if (l.row == focused && hasKeyboardFocus (false))
            {
                g.setColour (design::colour::accent.withAlpha (0.7f));
                g.drawRoundedRectangle (area, 8.0f, 1.0f);
            }
        }
        auto text = area.reduced (8.0f, 0.0f);
        // Right: the progress (a check once done) and the estimate before it.
        const auto status = text.removeFromRight (86.0f);
        if (item.status == Row::Status::complete)
        {
            const auto mark = juce::Rectangle<float> (status.getRight() - 12.0f, status.getCentreY() - 6.0f, 12.0f, 12.0f);
            juce::Path tick;
            tick.startNewSubPath (mark.getX() + 1.5f, mark.getCentreY());
            tick.lineTo (mark.getX() + 4.8f, mark.getBottom() - 2.0f);
            tick.lineTo (mark.getRight() - 1.0f, mark.getY() + 2.0f);
            g.setColour (design::colour::accent);
            g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.setColour (item.status == Row::Status::inProgress ? design::colour::accent : design::colour::textMicro);
            g.setFont (type::popupLabel (10.5f));
            g.drawText (statusText (item.status), status, juce::Justification::centredRight, false);
        }
        if (item.minutes > 0)
        {
            g.setColour (design::colour::textMicro);
            g.setFont (type::popupLabel (10.5f));
            g.drawText (juce::String (item.minutes) + " min", text.removeFromRight (44.0f), juce::Justification::centredRight, false);
        }
        g.setColour (design::colour::text);
        g.setFont (fonts::make (13.0f, fonts::Weight::regular, 0.0f));
        g.drawText (item.title, text, juce::Justification::centredLeft, true);
    }
}

void Browser::mouseMove (const juce::MouseEvent& e)
{
    const int row = rowAt (e.position);
    const bool overClose = closeArea().contains (e.position);
    if (row != hover || overClose != closeHover)
    {
        hover = row;
        closeHover = overClose;
        setMouseCursor (row >= 0 || overClose ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void Browser::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    closeHover = false;
    repaint();
}

void Browser::mouseUp (const juce::MouseEvent& e)
{
    juce::Component::SafePointer<Browser> safe (this);
    if (closeArea().contains (e.position))
    {
        juce::MessageManager::callAsync ([safe] {
            if (safe != nullptr && safe->onClose != nullptr)
                safe->onClose();
        });
        return;
    }
    if (const int row = rowAt (e.position); row >= 0)
    {
        const auto id = items[static_cast<std::size_t> (row)].lessonId;
        juce::MessageManager::callAsync ([safe, id] {
            if (safe != nullptr && safe->onChoose != nullptr)
                safe->onChoose (id);
        });
    }
}

bool Browser::keyPressed (const juce::KeyPress& key)
{
    const int n = static_cast<int> (items.size());
    if (n == 0)
        return false;
    if (key == juce::KeyPress::downKey || key == juce::KeyPress::upKey)
    {
        focused = juce::jlimit (0, n - 1, focused < 0 ? 0 : focused + (key == juce::KeyPress::downKey ? 1 : -1));
        repaint();
        return true;
    }
    if ((key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) && focused >= 0 && onChoose != nullptr)
    {
        onChoose (items[static_cast<std::size_t> (focused)].lessonId);
        return true;
    }
    return false;
}

} // namespace andor::learn
