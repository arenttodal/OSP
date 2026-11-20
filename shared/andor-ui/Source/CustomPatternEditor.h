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
    Custom ARP Patterns (docs/research/custom-arp, CP3; redesigned in R2): the editor for the
    pattern of one card, opened from the Custom Patterns browser (+ New Pattern, Edit) or the
    pencil in the ARP panel's pattern field.

    One editing surface: the PATTERN grid, the adaptive pattern itself (events on the step grid,
    each a relative rank, 1 = the lowest note the gesture used, or a rest). Click a note to
    select it, drag it up or down for its rank, drag its edge for its length (the pattern's
    length stays), double-click a rest for a note; right-click for every event action. The
    toolbar under it holds the frequent edits (Split, Make Rest, Add Step, Transform ▾, and Merge
    / Extend when they apply), Undo and Redo.

    The original take (RECORD from the keys or the host, or IMPORT a MIDI file) is a collapsible
    strip above: read only, the source TIMING and MAPPING interpret. Changing TIMING rebuilds the
    pattern from the take and asks first when that would replace manual edits; every change, a
    rebuild included, is one undo step that restores the controls with the pattern.

    PREVIEW plays the working pattern on the card while editing. APPLY PATTERN keeps it on the
    card (CUSTOM on, in the session and presets). Save to Library (the ··· menu) keeps it in
    ~/Documents/ANDOR/Patterns for other projects. CANCEL, Escape, the close icon or a click
    outside put the card back exactly as it was, after asking when there are changes that are
    neither applied nor saved (the host asks the panel: dismissRequested). While RECORD is armed
    the arpeggiator steps aside so the keys sound as played; it comes back on STOP, CANCEL or
    closing.
*/
class CustomPatternEditor final : public MiniPanel, public juce::TooltipClient, private juce::Timer
{
public:
    /** `fresh`: an empty pattern (+ New Pattern); else the card's own (Edit). */
    CustomPatternEditor (InstrumentHost& host, int card, bool fresh = false);
    ~CustomPatternEditor() override;

    static constexpr int width = 700, height = 424;   ///< the card with the take folded
    static constexpr int sourceViewHeight = 62;        ///< what the open take adds
    juce::Point<int> cardSize() const override;
    bool compact() const override { return true; }
    void paint (juce::Graphics&) override;
    void refreshContent() override;
    void settleNow() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String getTooltip() override;
    /** Escape, a click outside, the anchor again: closes now (true), or keeps the panel open and
        asks (a dialog: Discard changes?) or closes what is open inside it first. */
    bool dismissRequested() override;

    /** The actions (a click on a key, a menu item, a key press; tests drive these). */
    enum class Action
    {
        record, import, preview, timingSteps, timingHalf, timingQuarter, timingEighth, contour, degrees,
        split, merge, extend, rest, remove, insertStep, deleteStep, reverse, mirror, rotate, shorter, longer,
        undo, redo, clear, cancel, use, save, rename, duplicate, deleteFromLibrary, library,
        // R2: the menus and the panel's own keys
        transform, overflow, timingMenu, gridMenu, mappingMenu, toggleSource, help, close, saveCopy, browse, showFolder
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
    /** A document from the library (Browse Patterns...): one undo step. */
    void loadDocument (const arppattern::Document& document);
    /** Where the library keeps user patterns (CP4; tests point it elsewhere). */
    static void setLibraryFolderForTests (const juce::File& folder);
    static juce::File libraryFolder();
    /** The working document differs from the card's and from what was last saved to the library. */
    bool hasUnsavedChanges() const;
    /** The pattern differs from what TIMING would rebuild from the take (a rebuild asks first). */
    bool hasManualEdits() const;
    bool sourceExpanded() const noexcept { return sourceOpen; }
    bool helpShown() const noexcept { return helpOpen; }

    // -- the panel's dialog (Discard changes?, Rebuild?, Rename...) ------------------------------
    bool dialogShown() const noexcept { return dialog != nullptr; }
    juce::String dialogTitle() const { return dialog != nullptr ? dialog->title : juce::String(); }
    /** Its text field (Rename..., Save Copy..., Duplicate...), or null. */
    juce::TextEditor* dialogField() noexcept { return dialog != nullptr ? dialog->field.get() : nullptr; }
    /** The dialog's buttons: confirm (Discard, Rebuild, Rename ...) or cancel (Keep Editing ...). */
    void answerDialog (bool confirm);

    // -- direct manipulation (tests: the same as the mouse) -----------------------------------
    /** Drags the end (or the start) of event `index` to `tick` (pattern ticks, snapped as the
        mouse snaps); one undo step. False: refused, nothing changed. */
    bool dragEdge (int index, bool end, int tick);
    /** A double-click on a rest at `tick`, rank `rank`: a note there, one step long at most. */
    bool addNoteAt (int tick, int rank);
    /** The event menu's choices for event `index`, as shown (tests). */
    juce::StringArray contextActions (int index) const;
    /** Runs the event menu's item with this text on event `index`. */
    bool performContext (int index, const juce::String& item);
    /** The ··· menu's items, as shown (only the valid ones). */
    juce::StringArray overflowItems() const;

    // places (tests, snapshots)
    juce::Rectangle<int> keyArea (Action) const;
    juce::Rectangle<int> gridArea() const { return grid; }
    juce::Rectangle<int> recordedArea() const { return recorded; }
    juce::Rectangle<int> sourceStripArea() const { return sourceStrip; }
    /** An event's rectangle on the grid (empty if none or scrolled out of view). */
    juce::Rectangle<float> eventArea (int index) const;
    juce::TextEditor& nameField() noexcept { return *name; }
    /** The first nominal step drawn (long patterns scroll) and how many steps fit. */
    int firstStepShown() const noexcept { return viewStep; }
    int stepsShown() const;

protected:
    void layoutContent (juce::Rectangle<int> area) override;

private:
    struct Key
    {
        Action action;
        juce::String label;
        juce::Rectangle<int> area;
    };
    /** The panel's own dialog: a title, a line, two buttons and maybe a name field. */
    struct Dialog
    {
        juce::String title, text, cancelLabel, confirmLabel;
        bool destructive = false;
        std::function<void()> onConfirm;
        std::unique_ptr<juce::TextEditor> field;
        juce::Rectangle<int> box, cancelArea, confirmArea;
    };
    enum class Drag
    {
        none, rank, resizeEnd, resizeStart
    };

    void timerCallback() override;
    void commit (const arppattern::Pattern& next, const juce::String& what);
    void commitDocument (const juce::String& what);
    void restore (const arppattern::Document& state);
    void reinterpret (const arppattern::Interpretation& how);
    void chooseTiming (arppattern::Interpretation::Timing timing, int quantum);
    void publishPreview();
    void restoreCard();
    void startTake();
    void finishTake();
    void requestClose();
    void describe (const arppattern::InterpretReport& report);
    void openImportChooser();
    void showDialog (const juce::String& heading, const juce::String& text, const juce::String& cancel, const juce::String& confirm, bool destructive,
                     std::function<void()> onConfirm, const juce::String& fieldText = juce::String::charToString (0));
    void closeDialog();
    void layoutDialog();
    void setSourceOpen (bool open);
    bool keyEnabled (Action) const;
    bool keyLit (Action) const;
    bool keyShown (Action) const;
    int stepsOf (const arppattern::Pattern& p) const;
    double stepQuarters() const;
    void showMenu (Action, juce::PopupMenu& menu, std::function<void (int)> chosen);
    void openTransformMenu();
    void openOverflowMenu();
    void openTimingMenu();
    void openGridMenu();
    void openMappingMenu();
    void openBrowseMenu();
    void openContextMenu (int index);
    // the library (the ··· menu)
    juce::File libraryFileOf (const arppattern::Document& d) const;
    juce::File freeLibraryFile (const juce::String& wanted) const;
    void saveToLibrary();
    void saveCopy (const juce::String& wanted, bool switchToCopy);
    void renameInLibrary (const juce::String& wanted);
    void deleteFromLibrary();
    // drawing
    void paintKey (juce::Graphics&, const Key&);
    void paintSelector (juce::Graphics&, juce::Rectangle<int> r, const juce::String& text, bool enabled, bool hover);
    void paintSource (juce::Graphics&);
    void paintRecorded (juce::Graphics&, juce::Rectangle<float>);
    void paintGrid (juce::Graphics&, juce::Rectangle<float>);
    void paintHelp (juce::Graphics&);
    void paintDialog (juce::Graphics&);
    void paintIcons (juce::Graphics&);
    // the grid's geometry
    juce::Rectangle<float> plotArea() const;
    int viewTicks() const;
    float xOfTick (int tick) const;
    int tickAt (float x) const;
    int rankAt (float y) const;
    int rowsShown() const;
    int eventAt (juce::Point<float> p) const;
    int snapTick (int tick) const;
    void revealSelection();
    void setViewStep (int step);
    juce::Rectangle<float> scrollTrack() const;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    InstrumentHost& host;
    int cardIndex = 0;
    arppattern::Document doc;
    arppattern::SnapshotHistory<arppattern::Document> history;
    int selection = -1;
    bool previewOn = false, recordingTake = false, used = false, discardConfirmed = false;
    // The card as it was when the editor opened (CANCEL puts it back), what opened, what was saved.
    std::optional<arppattern::Document> before;
    arppattern::Document opened;
    std::optional<arppattern::Document> saved;
    float customBefore = 0.0f;
    bool cardChanged = false;
    juce::String status;
    std::vector<Key> keys;
    juce::Rectangle<int> nameRow, sourceStrip, recorded, interpRow, grid, toolRow, footerRow, statusArea, lengthText;
    juce::Rectangle<int> timingCaption, gridCaption, mappingCaption, lengthCaption;
    std::unique_ptr<juce::TextEditor> name;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<Dialog> dialog;
    bool sourceOpen = false, helpOpen = false;
    // Gestures on the grid.
    Drag drag = Drag::none;
    int dragIndex = -1, dragRank = -1;
    bool dragged = false;
    arppattern::Pattern dragBase;
    int viewStep = 0;
    juce::Point<int> hoverAt { -1, -1 };
    float wheelPending = 0.0f;
    juce::String drawnFor;
};

} // namespace osp::plugin
