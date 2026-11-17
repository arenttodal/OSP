#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace osp::plugin
{

class InstrumentHost;
class MiniPanel;

/**
    USER MACROS: what an editor asks of a product's six USER macros (names, assignments, the
    panel, the halos' ranges). ANDOR OSP's come from usermacros::descriptor (UserMacroEditor.h);
    the shell's products give theirs as ProductInfo::userMacros. Message thread only.
*/
struct UserMacroHooks
{
    std::array<juce::String, 6> parameterIds;
    std::function<juce::String (InstrumentHost&, int macro)> name;
    std::function<void (InstrumentHost&, int macro, const juce::String& name)> rename;
    /** Its active assignments (its lens lights while it has any). */
    std::function<int (InstrumentHost&, int macro)> activeCount;
    /** The parameter IDs it moves (shown as thin arcs while its name is hovered or its panel open). */
    std::function<juce::StringArray (InstrumentHost&, int macro)> targets;
    /** Whether a USER macro may move this parameter (continuous, real-time safe). */
    std::function<bool (InstrumentHost&, const juce::String& parameterId)> eligible;
    /** A neutral assignment (undoable; an existing one is kept). False: refused. */
    std::function<bool (InstrumentHost&, int macro, const juce::String& parameterId)> assign;
    /** Removes all its assignments (undoable). */
    std::function<void (InstrumentHost&, int macro)> clear;
    /** Its assignments' panel (`select`: the assignment shown first, may be empty). */
    std::function<std::unique_ptr<MiniPanel> (InstrumentHost&, int macro, const juce::String& select)> panel;
    /** A control's display name ("FILTER / RESONANCE") for the drag's label. */
    std::function<juce::String (InstrumentHost&, const juce::String& parameterId)> targetName;
    /** Changes whenever the names or assignments change (an edit, undo, a preset): the editor
        refreshes its names and lenses when it moves. */
    std::function<std::uint64_t (InstrumentHost&)> revision;

    // Destination-first editing (the halos; docs/MACRO_DESTINATION_FIRST.md). Positions are the
    // control's normalised value (its knob's travel).
    struct Range
    {
        int macro = -1;
        float atMin = 0.0f, atMax = 0.0f;   ///< the control at the macro's 0 % and 100 % (this assignment alone)
        float now = 0.0f, base = 0.0f, macroValue = 0.0f;
        bool enabled = true;
    };
    /** A control's assignments as the engine applies them (empty: none). */
    std::function<std::vector<Range> (InstrumentHost&, const juce::String& parameterId)> ranges;
    /** The macros' sum on a control now (normalised; modulation starts there). */
    std::function<float (InstrumentHost&, const juce::String& parameterId)> offsetNow;
    /** A range end dragged on the halo (phase 0 begin, 1 move, 2 commit: one undo step, 3 cancel). */
    std::function<void (InstrumentHost&, int macro, const juce::String& parameterId, int end, float position, int phase)> editRange;
    /** One end back to the control's own value (undoable). */
    std::function<void (InstrumentHost&, int macro, const juce::String& parameterId, int end)> resetEnd;
    /** A visible default range again, around the control's value (undoable). */
    std::function<void (InstrumentHost&, int macro, const juce::String& parameterId)> resetRange;
    /** Removes one assignment / all of a control's (undoable; the control keeps its own value). */
    std::function<void (InstrumentHost&, int macro, const juce::String& parameterId)> unassign;
    std::function<void (InstrumentHost&, const juce::String& parameterId)> unassignAll;
};

} // namespace osp::plugin
