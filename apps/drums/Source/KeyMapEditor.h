#pragma once

#include "CeramicSkin.h"
#include "DrumsProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <vector>

namespace osp::drumsplugin
{

/**
    KEY MAP: every pad's MIDI assignment at once, in the active performance mode's map.

      header     PAD | EXPANDED, Auto Map, LEARN, ALLOW OVERLAP, undo / redo, zoom, DONE
      lanes      one per visible pad (screen order), all on ONE note -> x mapping
      keyboard   equal-width semitone columns on the same mapping (exact alignment)
      inspector  the selected pad's note / range / root as numbers, LEARN, reset

    The editor never changes the map itself: every gesture ends in one validated command
    (drums::moveZone, resizeZone, setZoneRoot, setPadNote, ...) applied through
    DrumsProcessor::applyEdit, i.e. one undo step per gesture. A drag only previews; Escape
    cancels it. A result that collides with another pad is never applied silently: it offers
    Swap, Move the other out of the way, Layer (only with ALLOW OVERLAP) or Cancel.
*/
class KeyMapEditor final : public juce::Component, private juce::Timer, private juce::ScrollBar::Listener
{
public:
    explicit KeyMapEditor (DrumsProcessor&);
    ~KeyMapEditor() override;

    std::function<void()> onClose;
    std::function<void (int pad)> onSelect;
    std::function<void()> onModeChanged;   ///< PAD / EXPANDED switched here (the main bar follows)

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

    // View: one note -> x mapping for lanes and keyboard -----------------------------------
    float noteToX (double note) const noexcept;
    double xToNote (float x) const noexcept;
    void setView (double lowNote, double visibleNotes);
    double viewLow() const noexcept { return low; }
    double visibleNotes() const noexcept { return span; }
    void zoomBy (double factor, double anchorNote);
    void fitAssigned();
    void showFullRange() { setView (0.0, 128.0); }
    juce::Rectangle<float> timelineArea() const;
    juce::Rectangle<float> keyboardArea() const;
    juce::Rectangle<float> laneArea (int lane) const;
    int laneCount() const;
    int padOfLane (int lane) const;
    int laneOfPad (int pad) const;

    // Commands (the mouse and MIDI Learn use these; so do the tests) ----------------------
    enum class Gesture { none, move, resizeLow, resizeHigh, root, padNote, create };
    void beginGesture (int pad, Gesture gesture, int note);
    void gestureTo (int note);
    void cancelGesture();
    /** Ends the gesture. Applied at once when nothing is in the way; otherwise the result
        waits (pendingResolution) and, if `askUser`, a menu offers the ways out. */
    void endGesture (bool askUser = true);

    /** A proposed map that collides, waiting for the musician's choice. */
    struct Pending
    {
        drums::KeyMap candidate;
        int pad = 0;
        Gesture gesture = Gesture::none;
        int note = -1;                 ///< PAD mode: the note asked for
        int newLow = -1;               ///< EXPANDED move: the asked-for start
        std::vector<int> colliders;
        juce::String action;
    };
    const std::optional<Pending>& pendingResolution() const noexcept { return pending; }
    bool canSwap() const;
    bool resolveSwap();
    bool resolveMoveOthers();
    bool resolveLayer();
    void resolveCancel();

    // MIDI Learn
    enum class Learn { off, note, rangeLow, rangeHigh };
    void startLearn();
    void cancelLearn();
    void learnedNote (int note);
    Learn learnState() const noexcept { return learn; }

    // Auto Map (always previewed before it replaces anything)
    enum class AutoMap { standard = 1, chromatic, oneOctave, twoOctaves, fromSelectedRoot };
    bool previewAutoMap (AutoMap preset);
    void applyPreview();
    void cancelPreview();
    bool hasPreview() const noexcept { return preview.has_value(); }

    /** What the lanes show: the gesture's candidate, the Auto Map preview, or the map. */
    const drums::KeyMap& shownMap() const noexcept;
    juce::String statusText() const { return status; }
    void refreshFromProcessor();

private:
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    bool expanded() const;
    std::vector<int> layoutOrder() const;
    std::vector<int> collidersFor (const drums::KeyMap& candidate, int pad) const;
    void propose (const drums::KeyMap& candidate, int pad, Gesture gesture, int note, int newLow, const juce::String& action, bool askUser);
    bool commitDirect (const Pending& p);
    void commit (const drums::KeyMap& next, const juce::String& action);
    void showResolutionMenu();
    void updateInspector();
    void inspectorEdited();
    Gesture gestureAt (int pad, juce::Point<float> p, int note) const;
    void paintLanes (juce::Graphics&);
    void paintKeyboard (juce::Graphics&);
    void paintInspectorBackground (juce::Graphics&);
    void updateScrollBar();

    DrumsProcessor& proc;
    double low = 24.0, span = 72.0;

    // gesture
    Gesture gesture = Gesture::none;
    int gesturePad = -1, grabNote = 0, gestureNote = -1;
    drums::KeyMap gestureStart, candidate;
    bool clickOnly = false;
    std::optional<Pending> pending;
    std::optional<drums::KeyMap> preview;
    juce::String previewText;

    Learn learn = Learn::off;
    int learnLow = -1;
    juce::uint32 learnStarted = 0;
    juce::String status;
    std::array<bool, 128> held {};
    int lastShownNote = -1;

    // header
    ceramic::KeyButton padMode { "PAD" }, expandedMode { "EXPANDED" };
    juce::ComboBox autoMap;
    ceramic::KeyButton learnButton { "LEARN" }, overlapButton { "ALLOW OVERLAP" };
    ceramic::KeyButton undoButton { "UNDO" }, redoButton { "REDO" };
    ceramic::KeyButton zoomOut { "-" }, zoomIn { "+" }, fitButton { "FIT" }, allButton { "0-127" };
    ceramic::KeyButton doneButton { "DONE" };
    ceramic::KeyButton applyButton { "APPLY" }, cancelButton { "CANCEL" };
    juce::ScrollBar scroll { false };

    // inspector
    juce::Label noteLabel, lowLabel, highLabel, rootLabel;
    juce::Slider noteBox, lowBox, highBox, rootBox;
    ceramic::KeyButton resetButton { "RESET" }, clearButton { "CLEAR" };
    bool updatingInspector = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyMapEditor)
};

} // namespace osp::drumsplugin
