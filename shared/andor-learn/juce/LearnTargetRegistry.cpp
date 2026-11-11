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

bool TargetRegistry::reveal (const juce::String& id) const
{
    const auto* t = find (id);
    if (t == nullptr || t->reveal == nullptr)
        return false;
    return t->reveal();
}

} // namespace andor::learn
