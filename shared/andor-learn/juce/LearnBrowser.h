#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace andor::learn
{

/**
    The lesson browser (Learn > Tutorials...): one fixed-width ceramic panel, a quiet title and a
    short introduction, the Quick Start on its own under START HERE, then the lessons under
    LESSONS. Each row is a title with a one-line description beneath; on the right only what
    matters: the time for a new lesson, "Resume - Step 3 of 8" for one under way, a small check
    for one finished (a finished lesson can be played again). One hover / selection state.
    Arrow keys move, Return opens, Escape closes (through the controller), the wheel scrolls
    when the editor is too short for the whole list. It never resizes the plugin.
*/
class Browser final : public juce::Component
{
public:
    struct Row
    {
        juce::String lessonId, title, description;
        int minutes = 0;
        enum class Status { notStarted, inProgress, complete } status = Status::notStarted;
        int resumeStep = 0;    ///< in progress: the step it resumes at (0-based)
        int stepCount = 0;
        bool featured = false; ///< the Quick Start (START HERE)
    };

    Browser();
    void setRows (std::vector<Row>);
    const std::vector<Row>& rows() const noexcept { return items; }
    std::function<void (const juce::String& lessonId)> onChoose;
    std::function<void()> onClose;

    static constexpr int margin = 14, cardWidth = 420;
    /** The size for its rows, no taller than `maxHeight` (0: no limit); taller lists scroll. */
    juce::Point<int> preferredSize (int maxHeight = 0) const;
    /** Lays the panel out at a height (the controller's: the editor's room). */
    void setMaximumHeight (int maxHeight);
    juce::Rectangle<int> card() const { return getLocalBounds().reduced (margin); }
    /** A lesson's row (local coordinates, scrolled), or empty. */
    juce::Rectangle<float> rowArea (const juce::String& lessonId) const;
    juce::Rectangle<float> closeArea() const;
    /** What a row says on the right ("3 min", "Resume · Step 3 of 8", or nothing for the check). */
    static juce::String statusText (const Row&);
    int selectedRow() const noexcept { return focused; }
    float scrollOffset() const noexcept { return scroll; }

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override { return card().contains (x, y); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    struct Line
    {
        int row = -1;   ///< -1: a section heading
        juce::String text;
        juce::Rectangle<float> area;   ///< unscrolled
    };
    void layoutLines();
    float contentHeight() const;
    juce::Rectangle<float> listArea() const;
    int rowAt (juce::Point<float>) const;
    void ensureVisible (int row);
    void choose (int row);
    std::vector<Row> items;
    std::vector<Line> lines;
    int hover = -1, focused = -1, maxHeight = 0;
    float scroll = 0.0f;
    bool closeHover = false;
};

} // namespace andor::learn
