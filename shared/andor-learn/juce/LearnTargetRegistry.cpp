#include "LearnTargetRegistry.h"

namespace andor::learn
{

void TargetRegistry::add (Target t)
{
    jassert (t.id.isNotEmpty() && t.component != nullptr);
    for (auto& existing : targets)
        if (existing.id == t.id)
        {
            existing = std::move (t);   // a product may refine a shell target
            return;
        }
    targets.push_back (std::move (t));
}

void TargetRegistry::addState (const juce::String& id, std::function<bool()> query)
{
    states[id] = std::move (query);
}

void TargetRegistry::addValue (const juce::String& id, std::function<float()> read)
{
    values[id] = std::move (read);
}

std::optional<float> TargetRegistry::value (const juce::String& id) const
{
    const auto it = values.find (id);
    if (it == values.end() || it->second == nullptr)
        return std::nullopt;
    return it->second();
}

const Target* TargetRegistry::find (const juce::String& id) const
{
    for (const auto& t : targets)
        if (t.id == id)
            return &t;
    return nullptr;
}

bool TargetRegistry::state (const juce::String& id) const
{
    const auto it = states.find (id);
    return it != states.end() && it->second != nullptr && it->second();
}

bool TargetRegistry::shownWithin (const juce::Component& c, const juce::Component& space)
{
    for (const auto* p = &c; p != nullptr; p = p->getParentComponent())
    {
        if (! p->isVisible())
            return false;
        if (p == &space)
            return true;
    }
    return false;   // not inside `space`
}

std::optional<juce::Rectangle<int>> TargetRegistry::boundsIn (const juce::String& id, juce::Component& space) const
{
    const auto* t = find (id);
    if (t == nullptr)
        return std::nullopt;
    auto* c = t->component();
    // (`space` itself may be the component of a region it computes: the cards' union.)
    if (c == nullptr || (c == &space && t->area == nullptr) || ! shownWithin (*c, space))
        return std::nullopt;
    if (t->ready != nullptr && ! t->ready())
        return std::nullopt;
    const auto local = t->area != nullptr ? t->area (*c) : c->getLocalBounds();
    if (local.isEmpty())
        return std::nullopt;
    const auto r = space.getLocalArea (c, local);
    if (r.isEmpty())
        return std::nullopt;
    return r;
}

std::optional<juce::Rectangle<int>> TargetRegistry::containerIn (const juce::String& id, juce::Component& space) const
{
    // The nearest enclosing panel noticeably larger than the control (the ARP panel around one
    // of its keys, the rack around a knob or a segment, the card around its LEVEL), and smaller
    // than half of `space` (never a whole section).
    const auto target = boundsIn (id, space);
    const auto* t = find (id);
    if (! target.has_value() || t == nullptr)
        return std::nullopt;
    auto* c = t->component();
    if (c == nullptr || ! shownWithin (*c, space))
        return std::nullopt;
    const auto targetArea = static_cast<double> (target->getWidth()) * target->getHeight();
    const auto spaceArea = static_cast<double> (space.getWidth()) * space.getHeight();
    for (auto* p = t->area != nullptr ? c : c->getParentComponent(); p != nullptr && p != &space; p = p->getParentComponent())
    {
        const auto r = space.getLocalArea (p, p->getLocalBounds());
        const auto area = static_cast<double> (r.getWidth()) * r.getHeight();
        if (area >= 0.5 * spaceArea)
            return std::nullopt;
        if (area >= 6.0 * targetArea)
            return r;
    }
    return std::nullopt;
}

bool TargetRegistry::reveal (const juce::String& id) const
{
    const auto* t = find (id);
    if (t == nullptr || t->reveal == nullptr)
        return false;
    return t->reveal();
}

} // namespace andor::learn
