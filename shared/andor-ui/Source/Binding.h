#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace osp::plugin::binding
{

/**
    Which parameters a control shows, as a property on the control ("andorParams": IDs
    separated by spaces; a trailing '*' stands for every ID with that prefix). It changes
    nothing in how a control looks or behaves: the ANDOR Instrument Shell reads it to disable
    the controls whose parameters a product's engine does not use (ANDOR OSP uses them all).
*/
inline const juce::Identifier property { "andorParams" };

inline void tag (juce::Component& control, const juce::String& ids)
{
    control.getProperties().set (property, ids);
}

inline juce::StringArray tagsOf (const juce::Component& control)
{
    return juce::StringArray::fromTokens (control.getProperties()[property].toString(), " ", "");
}

/** An ID or pattern ("arp.*") against another: equal, or one pattern covers the other. */
inline bool matches (const juce::String& a, const juce::String& b)
{
    const bool aAll = a.endsWithChar ('*'), bAll = b.endsWithChar ('*');
    const auto pa = aAll ? a.dropLastCharacters (1) : a;
    const auto pb = bAll ? b.dropLastCharacters (1) : b;
    if (! aAll && ! bAll)
        return pa == pb;
    if (aAll && bAll)
        return pa.startsWith (pb) || pb.startsWith (pa);
    return aAll ? pb.startsWith (pa) : pa.startsWith (pb);
}

/**
    The parameter a single control edits, for finding that exact control (ANDOR Learn points at
    it): "andorControl" (a rack's knob, switch or selector), "paramId" (a card dial, a macro
    knob, an arpeggiator knob) or a one-ID "andorParams" tag (an AUX strip control). Changes
    nothing in how the control looks or behaves.
*/
inline const juce::Identifier controlProperty { "andorControl" };

inline void markControl (juce::Component& control, const juce::String& parameterId)
{
    control.getProperties().set (controlProperty, parameterId);
}

/** Whether `c` is the control that edits `parameterId`. */
inline bool isControlFor (const juce::Component& c, const juce::String& parameterId)
{
    const auto& props = c.getProperties();
    if (props[controlProperty].toString() == parameterId || props["paramId"].toString() == parameterId)
        return true;
    const auto tags = props[property].toString();
    return tags == parameterId;
}

/** The visible control under `root` that edits `parameterId` (the first in paint order), or
    nullptr. A hidden one (a closed panel, another page) is never returned. */
inline juce::Component* findControl (juce::Component& root, const juce::String& parameterId)
{
    for (auto* child : root.getChildren())
    {
        if (! child->isVisible())
            continue;
        if (isControlFor (*child, parameterId))
            return child;
        if (auto* found = findControl (*child, parameterId))
            return found;
    }
    return nullptr;
}

/** Whether any of `tags` is among `bound`. */
inline bool anyBound (const juce::StringArray& tags, const juce::StringArray& bound)
{
    for (const auto& t : tags)
        for (const auto& b : bound)
            if (matches (t, b))
                return true;
    return false;
}

} // namespace osp::plugin::binding
