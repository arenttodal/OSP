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
