#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace andor::learn
{

/**
    A semantic UI target ("arp.pattern", "source.card.B", "macro.0"): how to find the real
    control now (a locator, called each time: popovers and overlays are rebuilt), which part of
    it, and how to show it (navigation only: open a panel, select a tab; never a sound change).
    IDs are UI semantics only; they are not parameter IDs and not binding tags.
*/
struct Target
{
    using Locator = std::function<juce::Component* ()>;
    using Area = std::function<juce::Rectangle<int> (juce::Component&)>;
    Target (juce::String targetId, Locator locate, Area part = {}, std::function<bool()> show = {}, std::function<bool()> isReady = {},
            bool learnRegion = false, bool touches = false)
        : id (std::move (targetId)), component (std::move (locate)), area (std::move (part)), reveal (std::move (show)), ready (std::move (isReady)),
          inspectable (learnRegion), watchTouches (touches)
    {
    }

    juce::String id;
    /** The real component, or nullptr while it does not exist. */
    std::function<juce::Component* ()> component;
    /** A part of it in its own coordinates (null: all of it). Empty: not shown now. */
    std::function<juce::Rectangle<int> (juce::Component&)> area;
    /** Shows it (null: nothing to open). True if it is shown afterwards. */
    std::function<bool()> reveal;
    /** A further condition for pointing at it (null: shown with a size is enough). */
    std::function<bool()> ready;
    /** A Learn mode region (clicking it explains it). */
    bool inspectable = false;
    /** Report touches on the component as user events (a macro knob): "<id>.touched". */
    bool watchTouches = false;
};

class TargetRegistry
{
public:
    void add (Target);
    /** A named UI state a lesson can require ("arp.panel.open"). */
    void addState (const juce::String& id, std::function<bool()> query);

    const Target* find (const juce::String& id) const;
    bool contains (const juce::String& id) const { return find (id) != nullptr; }
    bool hasState (const juce::String& id) const { return states.count (id) != 0; }
    bool state (const juce::String& id) const;
    const std::vector<Target>& all() const noexcept { return targets; }

    /** The target's bounds in `space`'s coordinates (a SafePointer-free walk: called and used
        at once, never stored), or nothing while it is not shown inside `space`. */
    std::optional<juce::Rectangle<int>> boundsIn (const juce::String& id, juce::Component& space) const;
    bool ready (const juce::String& id, juce::Component& space) const { return boundsIn (id, space).has_value(); }
    bool reveal (const juce::String& id) const;

    /** `c` and each of its parents up to `space` are visible. */
    static bool shownWithin (const juce::Component& c, const juce::Component& space);

private:
    std::vector<Target> targets;
    std::map<juce::String, std::function<bool()>> states;
};

} // namespace andor::learn
