#pragma once

#include "CeramicSkin.h"
#include "PerformanceView.h"
#include "ShapingPopups.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace osp::plugin::performance
{

/**
    The header's Edit / Performance key (PERFORMANCE MODE R1): a header key like SAVE and FX
    ROUTING (their face, size and ink), icon only. Its icon is always the action it offers: a
    gauge in Edit Mode ("Enter Performance Mode"), a pencil in Performance Mode ("Edit
    Instrument"). One stable component: switching only changes its icon.
*/
struct ModeKey final : juce::Button, skin::KeyFace
{
    ModeKey();
    /** Which view is shown (the icon then offers the other). */
    void setPerformance (bool performance);
    bool showsPerformance() const noexcept { return performance; }

    void paintButton (juce::Graphics&, bool over, bool down) override;
    void buttonStateChanged() override { skin::repaintKey (*this); }
    skin::KeyRole keyRole() const override { return skin::KeyRole::preset; }
    skin::KeyState keyState() const override
    {
        return isDown() ? skin::KeyState::pressed : isOver() ? skin::KeyState::hover : skin::KeyState::normal;
    }

private:
    bool performance = false;
};

/**
    The macro housing's flap (Performance Mode): a small tab on the housing's upper-right edge
    with a sliders icon - opens the choice of visible macros. Quiet: no text, no accent. Its
    hit area may be larger than what it draws; it never takes height from the housing.
*/
struct MacroFlap final : juce::Button
{
    MacroFlap();
    void paintButton (juce::Graphics&, bool over, bool down) override;
    /** The visible tab inside the component (the rest is hit area). */
    juce::Rectangle<float> tab() const;
};

/** One choice in the picker: a macro's identity, its name, and (USER macros) whether it is
    assigned now. */
struct PickerEntry
{
    juce::String id, name;
    bool user = false, assigned = true;
};

/**
    "Performance Macros": the flap's compact popover. Core and USER macros with a tick each,
    "N of 8", the chosen ones' order (up / down), Reset to Default. Every change is applied at
    once (onChange); it never touches a macro's value. At most eight, never none.
*/
class MacroPicker final : public MiniPanel
{
public:
    MacroPicker (std::vector<PickerEntry> entries, juce::StringArray chosen, juce::StringArray defaults);
    ~MacroPicker() override;

    /** The chosen ids in order, after a change. */
    std::function<void (const juce::StringArray& chosen, bool reset)> onChange;
    const juce::StringArray& chosen() const noexcept { return selection; }

    juce::Point<int> cardSize() const override;
    bool compact() const override { return true; }

    /** Tests and keyboard: tick / untick an entry; move a chosen one; reset. */
    void toggle (const juce::String& id);
    void move (const juce::String& id, int delta);
    void reset();

    void paint (juce::Graphics&) override;
    /** Escape closes it (as a click outside or the flap again). */
    bool keyPressed (const juce::KeyPress&) override;

protected:
    void layoutContent (juce::Rectangle<int> area) override;

private:
    class Row;
    void rebuild();
    void changed (bool wasReset);

    std::vector<PickerEntry> entries;
    juce::StringArray selection, defaults;
    std::vector<std::unique_ptr<Row>> rows;
    juce::Rectangle<int> resetArea;
    void mouseUp (const juce::MouseEvent&) override;
};

} // namespace osp::plugin::performance
