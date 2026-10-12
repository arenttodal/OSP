#pragma once

#include "DrumsProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

namespace osp::drumsplugin
{

class PadGrid;

/**
    One pad cell. Its size comes only from the grid (every cell identical); nothing it shows
    (a long name, layers, selection, notes) can change it.

    Interaction (one consistent pattern):
      - the waveform is the pad's striking surface: press = play (louder towards the top) and select
      - the header and footer select without playing
      - the number badge is the handle: drag it onto another pad to swap their places
      - S / M in the header solo and mute; the menu (or a right-click) has the rest
      - double-click the name to rename; drop audio files anywhere on the pad to load them
*/
class PadView final : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      public juce::DragAndDropTarget
{
public:
    PadView (PadGrid& grid, int slot);
    int slot() const noexcept { return slotIndex; }
    int pad() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    /** Display rate: repaints only when something shown changed. */
    void refresh();
    void showMenu();
    void startRename();

    // Regions (local coordinates), shared by painting and hit testing.
    juce::Rectangle<float> headerArea() const;
    juce::Rectangle<float> badgeArea() const;
    juce::Rectangle<float> soloArea() const;
    juce::Rectangle<float> muteArea() const;
    juce::Rectangle<float> waveArea() const;
    juce::Rectangle<float> footerArea() const;
    juce::Rectangle<float> menuArea() const;

private:
    enum class Hover { none, wave, badge, solo, mute, menu, other };
    Hover hitTest (juce::Point<float>) const;
    bool paramOn (const char* name) const;
    void toggleParam (const char* name);

    PadGrid& grid;
    int slotIndex;
    bool fileOver = false, padOver = false, striking = false;
    Hover hover = Hover::none;
    std::uint32_t lastHits = 0;
    int lastVoices = 0;
    float flash = 0.0f;
    std::unique_ptr<juce::TextEditor> renameEditor;
};

/** The pad grid: 2 x 4 (8 cells) or 3 x 3 (9 cells), equal cells, constant gaps. */
class PadGrid final : public juce::Component, public juce::DragAndDropContainer
{
public:
    explicit PadGrid (DrumsProcessor&);

    void resized() override;
    void refresh();
    void repaintAll();
    void layoutChanged() { resized(); }

    DrumsProcessor& processor() noexcept { return proc; }
    std::function<void (int pad)> onSelect;      ///< a pad was selected (the editor follows)
    std::function<void (int pad)> onLoadRequest; ///< "Load sample..." from a pad's menu

    int visibleCells() const noexcept { return proc.visiblePads(); }
    /** For tests: the cell of a screen slot. */
    PadView& cell (int slot) { return *cells[static_cast<std::size_t> (slot)]; }
    static constexpr float gap = 10.0f;

private:
    DrumsProcessor& proc;
    std::array<std::unique_ptr<PadView>, DrumsProcessor::numPads> cells;
};

} // namespace osp::drumsplugin
