#pragma once

#include "CeramicSkin.h"
#include "DrumsProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <set>

namespace osp::drumsplugin
{

/**
    SLICE BREAK (spec Part 7): the import plan for a break, shown over the selected-pad area
    (as KEY MAP is). Nothing touches the kit until Apply.

      header    SENSITIVITY, MIN GAP, the tempo, MIDI (drag into the DAW, or click to save),
                Cancel, Apply (new kit / fill empty pads / merge)
      waveform  the break with its slices, coloured by their destination pad; click a slice
                to hear it (shift: add to the selection), drag a marker to move a boundary,
                double-click to add one, right-click for the slice's menu
      tray      every slice as a chip (number, destination, category); drag chips onto a
                pad in the grid, or select and Assign; Delete removes the selected
                boundaries

    Every edit is one undo step (the processor's editBreakSlices).
*/
class BreakView final : public juce::Component
{
public:
    explicit BreakView (DrumsProcessor&);

    std::function<void()> onClose;
    std::function<void (const juce::String& title, const juce::String& message)> onReport;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** The processor's session changed (slices, plan, loading). */
    void refresh();

    // Geometry and actions (also for tests)
    juce::Rectangle<float> waveArea() const;
    juce::Rectangle<float> trayArea() const;
    juce::Rectangle<float> chipRect (int slice) const;
    float xOfFrame (std::int64_t frame) const;
    std::int64_t frameAtX (float x) const;
    int sliceAtX (float x) const;
    int markerNear (float x) const;   ///< slice whose start marker is within 6 px (-1: none)
    int chipAt (juce::Point<float>) const;
    std::set<int> selection;
    void assignSelection (int pad);   ///< -1: back to the tray
    void setSelectionCategory (drums::DrumCategory category);
    void removeSelectedBoundaries();
    void showApplyMenu();
    static constexpr const char* dragPrefix = "andor-drums-slices:";

private:
    void showSliceMenu (int slice);
    void showAssignMenu (juce::Component* target);
    void showCategoryMenu (juce::Component* target);
    void exportMidi();
    juce::Colour sliceColour (const drums::BreakSlice&) const;

    DrumsProcessor& proc;
    ceramic::Fader sensitivity, separation;
    ceramic::KeyButton midi { "MIDI", 0.7f }, cancel { "Cancel", 0.8f }, apply { "Apply...", 0.8f };
    ceramic::KeyButton assign { "Assign to...", 0.7f }, category { "Category...", 0.7f }, toTray { "To tray", 0.7f }, suggest { "Suggest plan", 0.7f };
    std::unique_ptr<juce::FileChooser> chooser;
    int draggingMarker = -1;
    bool markerMoved = false;
    int hoverMarker = -1;
    int trayScroll = 0;
    bool chipDragStarted = false;
};

} // namespace osp::drumsplugin
