#pragma once

#include "Design.h"
#include "InstrumentFrame.h"
#include "InstrumentHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace osp::plugin
{

/**
    NOTE MAP (unified engine Phase 5): a layer's Note Atlas over its recording, in the display's
    place beside EQ | DAMPEN (docs/research/unified-andor/08-gui-proposals.md). The notes are
    regions over the waveform; the inspector at the right edits the selected ones.

    - Click a note: select it and hear it as recorded (shift: a range, cmd / ctrl: one more).
      Drag a note's edge to move its start or end; double-click inside it to split it there.
      A dashed line inside a note is a place it may be two notes: its handle splits it there.
    - The wheel zooms around the pointer (shift, or a horizontal wheel: scrolls); dragging the
      background scrolls.
    - Inspector: pitch (semitone, octave, fine tune), the key it plays on, its velocity layer,
      included or left out, split, merge, audition, why it sustains or plays as recorded, its
      doubts. Nothing selected: DETECTION (FEWER | NORMAL | MORE, a background re-detection
      that keeps every correction), FILL KEYBOARD and its reach, tuning, the analysis's notes.

    Every edit is one undoable step through InstrumentHost::editAtlas (the atlas is the only
    model: there is no copy here). The edit runs where the recording's audio is (a split or a
    merge re-describes the notes from it); the instrument is playable again at once and held
    notes keep sounding. Depends only on InstrumentHost.
*/
class NoteMapEditor final : public juce::Component, public juce::SettableTooltipClient
{
public:
    NoteMapEditor (InstrumentHost& host, int layer);

    std::function<void()> onClose;
    std::function<void (bool dampen)> onShowEq;   ///< the EQ (or DAMPEN) tab: the EQ page instead

    /** Follows the layer's atlas, its preparation and the held keys (editor timer); repaints
        only on change. */
    void refresh();

    // -- What tests (and the keyboard) drive --------------------------------------------------
    const std::vector<std::string>& selection() const noexcept { return selected; }
    void select (std::vector<std::string> ids);
    juce::Rectangle<float> graphArea() const;
    juce::Rectangle<float> inspectorArea() const { return inspector; }
    /** Where a note's region is drawn (empty when it is outside the view). */
    juce::Rectangle<float> regionBounds (const std::string& noteId) const;
    float xForFrame (std::int64_t frame) const;
    std::int64_t frameForX (float x) const;
    juce::Range<std::int64_t> viewRange() const noexcept { return view; }
    void setViewRange (std::int64_t start, std::int64_t end);
    /** The inspector's controls by action name ("pitch+", "oct-", "fine+", "key+", "key.auto",
        "layer+", "include", "audition", "split", "merge", "repeat.split", "repeat.keep",
        "conflict.keep", "conflict.redescribe", "detect.fewer", "detect.normal", "detect.more",
        "fill", "fill+", "fill-", "tuning"). False when the action is not offered now. */
    bool perform (const juce::String& action);
    juce::StringArray actions() const;
    juce::Rectangle<float> actionArea (const juce::String& action) const;
    /** Repeat markers of a note (source frames), as drawn. */
    std::vector<std::int64_t> repeatMarkers (const std::string& noteId) const;
    /** The status word in the top row: Analyzing, Preparing, Playable, Enhancing, Ready, Needs review, Failed. */
    juce::String statusText() const;
    static juce::String statusOf (const InstrumentHost& host, int layer);
    /** The keyboard's picture of an atlas as its built `set` plays it with the layer's LOOP:
        recorded keys (their takes, in their pitch's colour), keys FILL reaches (pale, in the
        colour of the note that fills them), silent keys. */
    static std::array<OspKeyboard::KeyTint, 128> keyMap (const atlas::NoteAtlas& atlas, const InstrumentSet* set = nullptr, bool loopOn = false);
    /** Notes the built set leaves out now: with LOOP on, those that cannot loop (once the
        loops are analysed). Their ids; empty when every note plays. */
    static std::vector<std::string> notesLeftOut (const InstrumentSet* set, bool loopOn);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct Cell
    {
        juce::String action, text;
        juce::Rectangle<float> area;
        bool on = false, enabled = true, value = false;   ///< value: shows a number, not clickable
    };
    struct Row
    {
        juce::String label;
        std::vector<Cell> cells;
        juce::String note;            ///< a line of explanation instead of cells
        float y = 0.0f, height = 0.0f;
    };
    enum class Edge { none, start, end };
    struct Hit
    {
        std::string noteId;
        Edge edge = Edge::none;
        std::optional<std::int64_t> repeat;   ///< a repeat marker's handle
    };

    const atlas::NoteEvent* note (const std::string& id) const;
    std::vector<const atlas::NoteEvent*> shownNotes() const;
    std::vector<const atlas::NoteEvent*> selectedNotes() const;
    Hit hitTest (juce::Point<float> p) const;
    void rebuildInspector();
    void layoutRows();
    void defaultView();
    void edit (const juce::String& name, InstrumentHost::AtlasEdit change);
    void audition (const std::vector<const atlas::NoteEvent*>& notes);
    void split (const std::string& id, std::int64_t frame);
    void mergeSelection();
    void commitBoundary();
    juce::String sustainText (const atlas::NoteEvent& n) const;
    juce::String doubtText (const atlas::NoteEvent& n) const;
    bool isConflict (const std::string& id) const;
    double sampleRate() const;
    std::int64_t sourceFrames() const;
    void paintWave (juce::Graphics&, juce::Rectangle<float> graph);
    void paintRegions (juce::Graphics&, juce::Rectangle<float> graph);
    void paintInspector (juce::Graphics&);

    InstrumentHost& host;
    int layer;
    std::shared_ptr<const LoadedInstrument> instrument;
    std::shared_ptr<const atlas::NoteAtlas> atlas;
    InstrumentHost::AtlasActivity activity = InstrumentHost::AtlasActivity::none;
    InstrumentHost::LoadState loadState = InstrumentHost::LoadState::empty;
    std::vector<std::string> conflicts;
    juce::String report;
    bool loopOn = true;
    std::vector<std::string> selected;
    std::string anchor;   ///< shift-click extends from here
    juce::Range<std::int64_t> view;
    bool viewSet = false;

    // A gesture in the graph.
    Hit pressed;
    bool dragged = false, panning = false;
    juce::Point<float> pressAt;
    std::int64_t panFrom = 0;
    std::int64_t dragFrame = 0;
    /** A boundary moved and committed, shown until the edited atlas arrives. */
    struct Pending
    {
        std::string noteId;
        Edge edge = Edge::none;
        std::int64_t frame = 0;
    };
    std::optional<Pending> pending;
    Hit hover;
    juce::String hoverAction;   ///< the inspector cell under the pointer

    std::vector<Row> rows;
    juce::Rectangle<float> inspector, header, eqTab, dampenTab, titleArea, statusArea, undoArea, redoArea, closeArea;
    juce::String shownStatus;
};

} // namespace osp::plugin
