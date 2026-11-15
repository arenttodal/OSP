#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>
#include <vector>

namespace andor::learn
{

/**
    The focus ring: paint-only, transparent, never intercepting a click (every press goes to
    the real control under it). A thin accent stroke and a soft halo around one rectangle in
    the content's coordinates; nothing else is covered.
*/
class HighlightLayer final : public juce::Component
{
public:
    HighlightLayer();
    /** The rectangle to ring (nothing: none). `quiet`: Learn mode's hover outline. */
    void setTarget (std::optional<juce::Rectangle<int>> target, bool quiet);
    std::optional<juce::Rectangle<int>> target() const noexcept { return ring; }
    void paint (juce::Graphics&) override;

private:
    std::optional<juce::Rectangle<int>> ring;
    bool quietRing = false;
};

/**
    The small ceramic note (the macro popovers' material: warm cream, a hairline, a short soft
    shadow) used for a lesson's step, a Learn mode explanation and the invitation. A meta line,
    a title, one or two sentences, and a row of keys; a thin cross closes it. It takes its own
    clicks only (the shadow margin passes through). Keys are focusable (Tab, Return, Space).
*/
class Note final : public juce::Component
{
public:
    struct Action
    {
        juce::String id, label;
        bool primary = false;
    };

    Note();
    ~Note() override;

    void setContent (const juce::String& meta, const juce::String& title, const juce::String& body, std::vector<Action> actions, bool done);
    /** A key, or "close" for the cross. */
    std::function<void (const juce::String& actionId)> onAction;

    static constexpr int margin = 14;    ///< the shadow's room around the card
    static constexpr int cardWidth = 300;
    /** The component's size for its content (card plus margin). */
    juce::Point<int> preferredSize() const;
    juce::Rectangle<int> card() const { return getLocalBounds().reduced (margin); }
    juce::Rectangle<float> closeArea() const;

    const juce::String& titleText() const noexcept { return title; }
    const juce::String& bodyText() const noexcept { return body; }
    const juce::String& metaText() const noexcept { return meta; }
    bool showsDone() const noexcept { return done; }
    juce::StringArray actionIds() const;
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
    juce::TextLayout layoutBody (float width) const;
    int bodyHeight() const;
    juce::String meta, title, body;
    bool done = false, closeHover = false;
    std::vector<std::unique_ptr<Key>> keys;
    std::vector<Action> actions;
};

/**
    One Learn mode region: transparent, over a registered area only (never a full-window
    blocker); hovering outlines the area, a click (or Return when focused) explains it. Exists
    only while Learn mode is on.
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
