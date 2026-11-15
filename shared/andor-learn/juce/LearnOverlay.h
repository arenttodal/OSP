#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>
#include <vector>

namespace andor::learn
{

/**
    The target outline: paint-only, transparent, never intercepting a click (every press goes to
    the real control under it). A luminous but restrained green line (about 2 px) with a soft
    glow, a few pixels outside the exact control, its corners matched to the control's size;
    nothing is drawn over the control itself. Green reads on the warm ceramic and on the dark
    displays alike; the lesson text always names the control too (colour is never the only cue).
    When the note cannot sit right beside its target, a thin connector joins them.
*/
class HighlightLayer final : public juce::Component
{
public:
    HighlightLayer();
    /** The rectangle to outline (nothing: none). `quiet`: Info's hover outline (thinner). */
    void setTarget (std::optional<juce::Rectangle<int>> target, bool quiet);
    /** A connector from the note to the target (nothing: none). */
    void setConnector (std::optional<juce::Line<float>> line);
    std::optional<juce::Rectangle<int>> target() const noexcept { return ring; }
    std::optional<juce::Line<float>> connector() const noexcept { return link; }
    void paint (juce::Graphics&) override;

    /** The outline's colour and its padding around the control (reference px). */
    static juce::Colour colour() noexcept { return juce::Colour (0xff2fbf6c); }
    static constexpr float padding = 5.0f;

private:
    std::optional<juce::Rectangle<int>> ring;
    std::optional<juce::Line<float>> link;
    bool quietRing = false;
};

/**
    The small ceramic note (the macro popovers' material: warm cream, a hairline, a short soft
    shadow) used for a lesson's step, an Info explanation and the invitation. A small label and
    the step count, a title, two or three short sentences, an optional status line (a check once
    the task is done), and at most one primary key with a quiet Back beside it; a thin cross
    closes it. It takes its own clicks only (the shadow margin passes through). Keys are
    focusable (Tab, Return, Space).
*/
class Note final : public juce::Component
{
public:
    struct Action
    {
        juce::String id, label;
        bool primary = false;
    };
    struct Content
    {
        juce::String meta;     ///< the small label ("ARPEGGIATOR")
        juce::String step;     ///< "STEP 5 OF 9" (right-aligned; empty: none)
        juce::String title, body;
        juce::String status;   ///< a short status line ("OSC 1 now plays sustained notes.")
        bool done = false;     ///< the status is a completed task: a check before it
        std::vector<Action> actions;
    };

    Note();
    ~Note() override;

    void setContent (Content);
    /** (The earlier form: meta, title, body, keys, and "Done" as the status when `done`.) */
    void setContent (const juce::String& meta, const juce::String& title, const juce::String& body, std::vector<Action> actions, bool done);
    /** A key, or "close" for the cross. */
    std::function<void (const juce::String& actionId)> onAction;

    static constexpr int margin = 14;    ///< the shadow's room around the card
    static constexpr int cardWidth = 320;
    /** The component's size for its content (card plus margin). */
    juce::Point<int> preferredSize() const;
    juce::Rectangle<int> card() const { return getLocalBounds().reduced (margin); }
    juce::Rectangle<float> closeArea() const;

    const juce::String& titleText() const noexcept { return content.title; }
    const juce::String& bodyText() const noexcept { return content.body; }
    const juce::String& metaText() const noexcept { return content.meta; }
    const juce::String& stepText() const noexcept { return content.step; }
    const juce::String& statusText() const noexcept { return content.status; }
    bool showsDone() const noexcept { return content.done; }
    juce::StringArray actionIds() const;
    juce::StringArray actionLabels() const;
    /** A key by its action ID (tests: its click). */
    juce::Button* button (const juce::String& actionId) const;
    /** Presses a key as its click does (tests and accessibility). */
    bool trigger (const juce::String& actionId);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool hitTest (int x, int y) override { return card().contains (x, y); }
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    class Key;
    juce::TextLayout layoutText (const juce::String& text, juce::Font font, juce::Colour colour, float width) const;
    int textHeight (const juce::String& text, juce::Font font) const;
    Content content;
    bool closeHover = false;
    std::vector<std::unique_ptr<Key>> keys;
};

/**
    One Info region: transparent, over a registered area only (never a full-window blocker);
    hovering outlines the area, a click (or Return when focused) explains it. Exists only while
    Info is on.
*/
class HitRegion final : public juce::Component
{
public:
    explicit HitRegion (juce::String targetId);
    const juce::String& targetId() const noexcept { return id; }
    std::function<void (const juce::String&)> onHover;   ///< empty: left
    std::function<void (const juce::String&)> onChoose;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;

private:
    juce::String id;
};

} // namespace andor::learn
