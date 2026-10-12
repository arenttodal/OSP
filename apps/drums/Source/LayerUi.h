#pragma once

#include "DrumsProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

/**
    ANDOR Drums' sample layers in the editor (spec §3.2, §3.5): the choice offered when
    files land on a pad, the selected pad's layer list, and a velocity range bar. Every
    edit goes through the processor's undoable layer functions.
*/
namespace osp::drumsplugin
{

/** What a drop does when nobody chooses: one file replaces, several become round robins. */
LayerDrop defaultLayerDrop (int fileCount) noexcept;

/** Whether a drop asks what to do: only with Cmd (Ctrl on Windows) held, by default. */
enum class DropAsk
{
    fromModifiers,   ///< ask while Cmd is held as the files land
    never,
    always
};

/** The rule: a drop asks only with Cmd (Ctrl on Windows) held, unless `ask` decides. */
bool dropAsks (DropAsk ask, juce::ModifierKeys modifiers) noexcept;

/**
    Files dropped (or chosen) for a pad. Without asking, the default just happens: one file
    replaces the pad's sound, several become round robins (or `preferred`). Asking (Cmd held
    while dropping) opens a menu: round robin, velocity layers, stack, replace, slicing a
    long file as a break, sorting several into the kit; the default is ticked. Without a
    window to show it in (headless), the default is applied.
*/
void offerLayerDrop (DrumsProcessor& proc, int pad, const juce::Array<juce::File>& files, juce::Component* target,
                     std::optional<LayerDrop> preferred = std::nullopt, DropAsk ask = DropAsk::fromModifiers);

bool isAudioFileName (const juce::String& path);

/** "Duplicate to": the first empty pad, then every other pad (replacing what it holds).
    `onDone` gets the copy's pad (to select it). */
juce::PopupMenu duplicateMenu (DrumsProcessor& proc, int pad, std::function<void (int)> onDone);

/**
    The selected pad's layers, one row each: play (hold to audition), number, name and
    its velocity range or level. Click selects (the waveform shows it), drag reorders,
    double-click renames, right-click has the rest; drop files here to add layers. The
    layer the last hit played is marked.
*/
class LayerList final : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer
{
public:
    explicit LayerList (DrumsProcessor&);
    ~LayerList() override;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;

    /** The selection or the layers changed elsewhere (keeps the selection visible). */
    void refresh();
    std::function<void()> onSelectionChanged;

    static constexpr float rowHeight = 26.0f;
    int rowAt (float y) const;
    juce::Rectangle<float> rowRect (int row) const;
    void showMenu (int layer);
    void startRename (int layer);

private:
    void timerCallback() override;
    int visibleRows() const;
    int pad() const noexcept;
    int layerCount() const;
    void select (int layer);

    DrumsProcessor& proc;
    int scroll = 0;
    int hover = -1;
    int dragFrom = -1, dragTo = -1;
    int auditioning = -1;
    int shownLastPlayed = -1;
    bool fileOver = false;
    std::unique_ptr<juce::TextEditor> renameEditor;
    std::unique_ptr<juce::FileChooser> chooser;
};

/**
    A velocity range 1..127 as a bar: drag either end (or the middle to move both). Calls
    onChange (low, high, gestureStart) while dragging.
*/
class VelocityRange final : public juce::Component
{
public:
    void setRange (int low, int high, juce::Colour colour);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    std::function<void (int low, int high, bool gestureStart)> onChange;
    int low() const noexcept { return lo; }
    int high() const noexcept { return hi; }

private:
    juce::Rectangle<float> bar() const;
    float xOf (int velocity) const;
    int velocityAt (float x) const;
    int lo = 1, hi = 127;
    juce::Colour fill { 0xffe8692a };
    enum class Grab { none, low, high, both } grab = Grab::none;
    int grabOffset = 0;
};

} // namespace osp::drumsplugin
