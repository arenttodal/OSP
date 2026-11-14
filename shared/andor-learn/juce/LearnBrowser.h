#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace andor::learn
{

/**
    The topic chooser (Learn ANDOR...): the same ceramic note as a lesson's, a title and tidy
    one-line rows, each a lesson's name, a small time estimate and its progress (Not started,
    In progress, or a check). Grouped by topic; only lessons the product has are listed.
    Arrow keys move, Return starts, Escape closes (through the controller).
*/
class Browser final : public juce::Component
{
public:
    struct Row
    {
        juce::String lessonId, title, group;
        int minutes = 0;
        enum class Status { notStarted, inProgress, complete } status = Status::notStarted;
    };

    Browser();
    void setRows (std::vector<Row>);
    const std::vector<Row>& rows() const noexcept { return items; }
    std::function<void (const juce::String& lessonId)> onChoose;
    std::function<void()> onClose;

    static constexpr int margin = 14, cardWidth = 320;
    juce::Point<int> preferredSize() const;
    juce::Rectangle<int> card() const { return getLocalBounds().reduced (margin); }
    /** A lesson's row (local coordinates), or empty. */
    juce::Rectangle<float> rowArea (const juce::String& lessonId) const;
    juce::Rectangle<float> closeArea() const;
    static juce::String statusText (Row::Status);

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override { return card().contains (x, y); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct Line
    {
        int row = -1;   ///< -1: a group heading
        juce::String text;
        juce::Rectangle<float> area;
    };
    void layoutLines();
    int rowAt (juce::Point<float>) const;
    std::vector<Row> items;
    std::vector<Line> lines;
    int hover = -1, focused = -1;
    bool closeHover = false;
};

} // namespace andor::learn
