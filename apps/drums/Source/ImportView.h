#pragma once

#include "CeramicSkin.h"
#include "DrumsProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <set>

namespace osp::drumsplugin
{

/**
    IMPORT SAMPLES (spec 6.3): the plan for several files before anything changes.

    One card per group (the files one pad would play as round robins): its destination
    (a pad, or the tray), a conflict when that pad already plays something, its category,
    and its files with a confidence dot (green sure, amber likely, grey unsure: hover for
    why). Click a file to hear it (shift: select several). Right-click a file to give it
    its own pad or move it to another group; "Group selected" joins the selection on one
    pad. Files that could not be loaded and duplicates are listed below, never dropped.

    Import, Cancel, Reset suggestions. Every correction is one undo step.
*/
class ImportView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ImportView (DrumsProcessor&);

    std::function<void()> onClose;
    std::function<void (const juce::String& title, const juce::String& message)> onReport;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void refresh();

    // Geometry and actions (also for tests)
    juce::Rectangle<float> cardRect (int group) const;
    juce::Rectangle<float> padLabelRect (int group) const;
    juce::Rectangle<float> categoryRect (int group) const;
    juce::Rectangle<float> fileRect (int group, int row) const;
    std::set<int> selection;   ///< selected items (session indices)
    void splitItem (int item);
    void moveItem (int item, int toGroup);
    void groupSelection();
    void setGroupPad (int group, int pad);
    void setGroupCategory (int group, drums::DrumCategory category);
    static constexpr int rowsShown = 4;

private:
    void showPadMenu (int group);
    void showCategoryMenu (int group);
    void showItemMenu (int group, int item);
    juce::String tooltipFor (juce::Point<float>) const;

    DrumsProcessor& proc;
    ceramic::KeyButton importButton { "Import", 0.8f }, cancel { "Cancel", 0.8f }, reset { "Reset suggestions", 0.7f }, join { "Group selected", 0.7f };
    int scrollRow = 0;
};

} // namespace osp::drumsplugin
