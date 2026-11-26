#pragma once

#include "Design.h"
#include "InstrumentHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace osp::plugin
{

/**
    ZONES: a multi-sample layer's map over the keyboard (owner, 26 Nov 2026), in the display's
    place like NOTES. Each sample is a bar over the keys it plays, its root a dot; rows are
    velocity layers (the top row the hardest). Round-robin takes of one pitch and layer share a
    bar ("2 takes").

    - Click a bar: select it and hear it as recorded. Drag an edge to resize its keys, its body
      to move them, up or down to another velocity row (above the top row: a new, harder one).
    - AUTO: every key plays the nearest sample (the automatic mapping). The first edit, MANUAL
      or FILL GAPS turns the current map into zones of their own (MANUAL); a key no zone holds
      is silent. AUTO again forgets the ranges (velocity rows and roots stay as set).
    - FILL GAPS stretches each row's zones over the empty keys between them (and to both ends).
    - ROOT - / + / AUTO for the selected bar retune it a semitone (its pitch group follows).
    - Drop audio files on it (or + ADD SAMPLES) to add them to the set.

    Every edit replaces the set's corrections through InstrumentHost::setSetAssignments (the
    set is rebuilt from its analysed members; nothing is analysed again) and is saved with the
    session. Depends only on InstrumentHost.
*/
class ZoneEditor final : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    ZoneEditor (InstrumentHost& host, int layer);

    std::function<void()> onClose;

    /** Follows the layer's set (editor timer); rebuilds when a new set arrives. */
    void refresh();

    /** One bar: the members of a (pitch group, velocity layer), their keys and root. */
    struct Zone
    {
        int group = 0;
        int layer = 0;
        std::vector<int> members;   ///< indices into the set's members (takes)
        int low = 0, high = 127;    ///< keys, inclusive
        double rootMidi = 60.0;
        juce::String name;          ///< the first member's file name, without extension
    };

    // -- What tests drive --------------------------------------------------------------------
    const std::vector<Zone>& zones() const noexcept { return shown; }
    bool isManual() const noexcept { return manual; }
    int rowCount() const noexcept;
    int selectedZone() const noexcept { return selected; }
    void selectZone (int index);
    juce::Rectangle<float> zoneBounds (int index) const;
    juce::Rectangle<float> laneArea() const;
    float xForKey (int key) const;
    /** "auto", "manual", "fill", "add", "root-", "root+", "root.auto", "close". False when the
        action is not offered now. */
    bool perform (const juce::String& action);
    juce::Rectangle<float> actionArea (const juce::String& action) const;
    /** The corrections the editor would send for the zones as shown (tests). */
    std::vector<SetAssignment> assignmentsFor (const std::vector<Zone>& zones, bool withKeys) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }

private:
    enum class Drag { none, move, left, right };

    InstrumentHost& host;
    int layer;
    std::shared_ptr<const LoadedInstrument> instrument;
    std::uint64_t shownGeneration = 0;
    std::vector<Zone> shown;
    bool manual = false;
    bool pending = false;          ///< an edit sent, the rebuilt set not here yet (keeps the edit shown)
    int selected = -1;
    int lowKey = 21, highKey = 108;   ///< the keyboard drawn
    Drag drag = Drag::none;
    Zone dragStart;
    bool dragChanged = false;
    bool dropHover = false;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<float> titleArea, closeArea, autoArea, manualArea, fillArea, addArea, lanes, keyboard, side, rowLabels;
    juce::Rectangle<float> rootDown, rootUp, rootAuto;

    void rebuild();
    void commit (const std::vector<Zone>& zones, bool withKeys);
    int rowOfLayer (int velocityLayer) const;
    int layerOfY (float y) const;
    float keyWidth() const;
    std::optional<double> pinnedRoot (const std::string& filename) const;
    void paintKeyboard (juce::Graphics&) const;
    void paintSide (juce::Graphics&) const;
};

} // namespace osp::plugin
