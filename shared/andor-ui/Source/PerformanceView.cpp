#include "PerformanceView.h"

#include <algorithm>

namespace osp::plugin::performance
{

juce::String userId (int slot)
{
    return "user:" + juce::String (slot);
}

int userSlotOf (const juce::String& id)
{
    if (! id.startsWith ("user:"))
        return -1;
    const auto digits = id.fromFirstOccurrenceOf (":", false, false);
    if (digits.isEmpty() || ! digits.containsOnly ("0123456789"))
        return -1;
    return digits.getIntValue();
}

bool Catalog::knows (const juce::String& id) const
{
    if (const int slot = userSlotOf (id); slot >= 0)
        return slot < userSlots;
    return core.contains (id);
}

std::unique_ptr<juce::XmlElement> State::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("Performance");
    xml->setAttribute ("version", schemaVersion);
    xml->setAttribute ("mode", performance ? "performance" : "edit");
    xml->setAttribute ("customised", customised ? 1 : 0);
    if (customised)
        for (const auto& id : macros)
            xml->createNewChildElement ("Macro")->setAttribute ("id", id);
    return xml;
}

std::optional<State> State::fromXml (const juce::XmlElement* xml)
{
    if (xml == nullptr)
        return std::nullopt;
    State s;
    s.performance = xml->getStringAttribute ("mode") == "performance";
    s.customised = xml->getIntAttribute ("customised", 0) != 0;
    if (s.customised)
        for (const auto* m : xml->getChildWithTagNameIterator ("Macro"))
            if (const auto id = m->getStringAttribute ("id").trim(); id.isNotEmpty())
                s.macros.add (id);
    if (s.customised && s.macros.isEmpty())
        s.customised = false;   // (a malformed selection: the default)
    return s;
}

juce::StringArray defaultSelection (const Catalog& catalog, const AssignedFn& assigned)
{
    juce::StringArray out;
    for (const auto& id : catalog.core)
        if (out.size() < maxMacros)
            out.addIfNotAlreadyThere (id);
    int users = 0;
    for (int slot = 0; slot < catalog.userSlots && out.size() < maxMacros && users < 2; ++slot)
        if (assigned != nullptr && assigned (slot))
        {
            out.add (userId (slot));
            ++users;
        }
    return out;
}

juce::StringArray sanitise (const juce::StringArray& ids, const Catalog& catalog, const AssignedFn& assigned)
{
    juce::StringArray out;
    for (const auto& id : ids)
        if (out.size() < maxMacros && catalog.knows (id))
            out.addIfNotAlreadyThere (id);
    return out.isEmpty() ? defaultSelection (catalog, assigned) : out;
}

juce::StringArray selection (const State& state, const Catalog& catalog, const AssignedFn& assigned)
{
    return state.customised ? sanitise (state.macros, catalog, assigned) : defaultSelection (catalog, assigned);
}

juce::StringArray visible (const State& state, const Catalog& catalog, const AssignedFn& assigned)
{
    juce::StringArray out;
    for (const auto& id : selection (state, catalog, assigned))
        if (const int slot = userSlotOf (id); slot < 0 || (assigned != nullptr && assigned (slot)))
            out.add (id);
    if (out.isEmpty() && ! catalog.core.isEmpty())
        out.add (catalog.core[0]);
    return out;
}

std::optional<State> readPreference (const juce::File& interfaceFile)
{
    if (const auto xml = juce::XmlDocument::parse (interfaceFile); xml != nullptr && xml->hasTagName ("Interface"))
        return State::fromXml (xml->getChildByName ("Performance"));
    return std::nullopt;
}

void writePreference (const juce::File& interfaceFile, const State& state)
{
    juce::XmlElement xml ("Interface");
    if (const auto old = juce::XmlDocument::parse (interfaceFile); old != nullptr && old->hasTagName ("Interface"))
        xml = *old;
    if (auto* previous = xml.getChildByName ("Performance"))
        xml.removeChildElement (previous, true);
    xml.addChildElement (state.toXml().release());
    interfaceFile.getParentDirectory().createDirectory();
    xml.writeTo (interfaceFile);
}

std::vector<float> stripCentres (float left, float width, int count)
{
    std::vector<float> out;
    if (count <= 0)
        return out;
    const float slot = width / static_cast<float> (count);
    for (int i = 0; i < count; ++i)
        out.push_back (left + slot * (static_cast<float> (i) + 0.5f));
    return out;
}

} // namespace osp::plugin::performance
