#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace osp::plugin
{

/**
    A part of the interface that owns the files dropped on it (opt-in; ANDOR ANALOG's D / AUX
    strip and rack). The shell's editor receives every drop (its overlays sit above the parts);
    it gives a drop to the owner under the pointer - the topmost part there, or one of its
    parents, that is a DropOwner - and to nothing else: the drop is consumed, it never reaches a
    card's loader too. Where no owner is under the pointer, the editor's own rules apply as
    before. A product without owners (ANDOR OSP) is unchanged.
*/
class DropOwner
{
public:
    virtual ~DropOwner() = default;
    /** Files are being dragged over it (`over` true) or have left it: show (or clear) where they
        would go. Any files: an owner also says when it cannot take them. */
    virtual void dropHover (const juce::StringArray& files, bool over) = 0;
    /** Dropped on it at `local` (its own coordinates): load them, or say why not. */
    virtual void dropFiles (const juce::StringArray& files, juce::Point<int> local) = 0;
};

} // namespace osp::plugin
