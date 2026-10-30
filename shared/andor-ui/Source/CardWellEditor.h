#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace osp::plugin
{

/**
    A product's own editor in a source card's dark well (opt-in: EngineCard::setWellEditor).
    It takes exactly the waveform display's place, never the header or the knobs below, and
    the card shows it or the waveform, never both, and never with the EQ or NOTE MAP (which
    take the well while they are open). ANDOR OSP has none: its cards are unchanged.

    Its view preferences (pinned, Quick or Detailed, the Detailed section) are interface
    state, not sound: the shell keeps them (ShellProcessor::wellViewState) and gives them
    back whenever the editor is made again. They are never parameters, never in a preset.
*/
class CardWellEditor : public juce::Component
{
public:
    struct ViewState
    {
        bool pinned = false;      ///< stays open: restored after the EQ, and when the window opens
        bool detailed = false;    ///< Detailed rather than Quick
        int section = 0;          ///< Detailed: the section shown

        bool operator== (const ViewState& o) const noexcept { return pinned == o.pinned && detailed == o.detailed && section == o.section; }
        bool operator!= (const ViewState& o) const noexcept { return ! operator== (o); }
    };

    /** The editor's own return to the waveform (its WAVE key): the shell's toggle. */
    std::function<void()> onShowWaveform;
    /** The musician changed a view preference (pin, Quick / Detailed, section). */
    std::function<void()> onViewStateChanged;

    const ViewState& viewState() const noexcept { return view; }
    /** From the shell: shows the state without reporting it back. */
    void setViewState (const ViewState& state)
    {
        if (state == view)
            return;
        view = state;
        viewStateChanged();
    }

    /** The editor's timer (message thread, while shown): follow the parameters and repaint
        what changed. Never anything heavy. */
    virtual void refresh() {}

protected:
    /** The view changed (from the shell, or `setView` below): show it. */
    virtual void viewStateChanged() {}
    /** From the editor's own controls: change the view and tell the shell. */
    void setView (const ViewState& state)
    {
        if (state == view)
            return;
        view = state;
        viewStateChanged();
        if (onViewStateChanged != nullptr)
            onViewStateChanged();
    }

private:
    ViewState view;
};

} // namespace osp::plugin
