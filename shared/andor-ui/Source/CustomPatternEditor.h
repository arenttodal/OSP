#pragma once

#include "InstrumentHost.h"
#include "ShapingPopups.h"

#include "io/ArpPatternJson.h"
#include "model/ArpPatternModel.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>

namespace osp::plugin
{

/**
    Custom ARP Patterns (docs/research/custom-arp, CP3): the compact contextual editor, opened from
    the ARP panel's pattern list (Create Pattern…, Edit Pattern…) for the card being edited.

    RECORDED shows the original take (record from the keys or the host, or import a MIDI file):
    the explanation of where the pattern came from, read only. PATTERN is the adaptive pattern
    itself, the editing surface: events on the step grid, each a relative rank (1 = the lowest
    note the gesture used, never a note name) or a rest. SPLIT, MERGE, EXTEND, REST and DELETE act
    on the selected event and keep the pattern's length; + STEP / - STEP and LENGTH change it,
    explicitly. Every completed action is one undo step.

    PREVIEW plays the pattern on the card while editing (CUSTOM on); CANCEL, Escape or a click
    outside put the card back exactly as it was; USE PATTERN keeps it (CUSTOM on). While RECORD is
    armed the arpeggiator steps aside so the keys sound as played; it comes back on STOP, CANCEL
    or closing.
*/
class CustomPatternEditor final : public MiniPanel, private juce::Timer
{
public:
    /** `fresh`: an empty pattern (Create Pattern...); else the card's own (Edit Pattern...). */
    CustomPatternEditor (InstrumentHost& host, int card, bool fresh = false);
    ~CustomPatternEditor() override;

    static constexpr int width = 700, height = 470;
    juce::Point<int> cardSize() const override { return { width, height }; }
    bool compact() const override { return true; }
    void paint (juce::Graphics&) override;
    void refreshContent() override;
    void settleNow() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** The actions, one each as a click on its key (tests drive these). */
    enum class Action
    {
        record, import, preview, timingSteps, timingHalf, timingQuarter, timingEighth, contour, degrees,
        split, merge, extend, rest, remove, insertStep, deleteStep, reverse, mirror, rotate, shorter, longer,
        undo, redo, clear, cancel, use, save, rename, duplicate, deleteFromLibrary, library
    };
    void perform (Action);
    bool canPerform (Action) const;

    // -- state (tests) ----------------------------------------------------------------------
    const arppattern::Document& document() const noexcept { return doc; }
    int selected() const noexcept { return selection; }
    void select (int index);
    /** A drag on the grid: the selected note's rank (one undo step when committed). */
    void setSelectedRank (int rank);
    bool previewing() const noexcept { return previewOn; }
    bool recording() const noexcept;
    juce::String message() const { return status; }
    /** An imported MIDI file's phrases (the file chooser and parsing call this; with several,
        the musician picks one from a menu; `pick` chooses directly). */
    void importPhrases (const std::vector<MidiPhrase>& phrases, int pick = -1);
    /** A recorded take (what STOP does with the capture; tests call it directly). */
    void useTake (const std::vector<arppattern::CapturedNote>& notes, double bpm);
    /** A document from the library (Load). */
    void loadDocument (const arppattern::Document& document);
    /** Where the library keeps user patterns (CP4; tests point it elsewhere). */
    static void setLibraryFolderForTests (const juce::File& folder);
    static juce::File libraryFolder();

    // places (tests, snapshots)
    juce::Rectangle<int> keyArea (Action) const;
    juce::Rectangle<int> gridArea() const { return grid; }
    juce::Rectangle<int> recordedArea() const { return recorded; }
    /** An event's rectangle on the grid (empty if none). */
    juce::Rectangle<float> eventArea (int index) const;
    juce::TextEditor& nameField() noexcept { return *name; }

protected:
    void layoutContent (juce::Rectangle<int> area) override;

private:
    struct Key
    {
        Action action;
        juce::String label;
        juce::Rectangle<int> area;
        bool toggle = false;
    };

    void timerCallback() override;
    void commit (const arppattern::Pattern& next, const juce::String& what);
    void reinterpret();
    void publishPreview();
    void restoreCard();
    void finishTake();
    void describe (const arppattern::InterpretReport& report);
    bool keyLit (Action) const;
    int stepsOf (const arppattern::Pattern& p) const;
    double stepQuarters() const;
    void openLibraryMenu();
    void openImportChooser();
    void paintKeys (juce::Graphics&);
    void paintRecorded (juce::Graphics&, juce::Rectangle<float>);
    void paintGrid (juce::Graphics&, juce::Rectangle<float>);
    int rankAt (float y) const;
    int eventAt (juce::Point<float> p) const;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    InstrumentHost& host;
    int cardIndex = 0;
    arppattern::Document doc;
    arppattern::History history;
    int selection = -1;
    bool previewOn = false, recordingTake = false, used = false;
    // The card as it was when the editor opened (CANCEL puts it back).
    std::optional<arppattern::Document> before;
    float customBefore = 0.0f;
    bool cardChanged = false;
    juce::String status;
    std::vector<Key> keys;
    juce::Rectangle<int> nameRow, recorded, grid, editRow, actionRow, statusRow, lengthCaption;
    std::unique_ptr<juce::TextEditor> name;
    std::unique_ptr<juce::FileChooser> chooser;
    // A drag on the grid.
    int dragFrom = -1, dragRank = -1;
    bool dragged = false;
    juce::String drawnFor;
};

} // namespace osp::plugin
