#pragma once

#include "InstrumentHost.h"
#include "MainSections.h"
#include "ShapingPopups.h"

#include <array>
#include <functional>
#include <memory>

namespace osp::plugin
{

class EqValueField;

/**
    STRUM's popover (docs/research/arp-strum), opened from the STRUM key beside SATELLITE on the
    ARP title row: SATELLITE's language (a compact bone card, one graphite display, a few keys and
    knobs). A / B / C choose whose settings it shows (choosing never switches a card); the light on
    the right switches the shown card.

    The display draws the sweep from the card's real settings: the chord's notes (the keys held
    now, else a C major seventh drawn as a preview, marked PREVIEW) placed along the spread in
    their strum order, and the spread itself. When the step is shorter than the spread asked
    for, it says how much plays (the arpeggiator never lets a strum reach the next step).
*/
class StrumDisplay final : public juce::Component
{
public:
    using KeyHeld = std::function<bool (int note)>;
    StrumDisplay (InstrumentHost& host, int card, KeyHeld held);
    void setCard (int card);
    void paint (juce::Graphics&) override;
    /** Repaints when the settings or the held chord changed (the panel's refresh). */
    void refresh();

    /** What is drawn (tests): the chord (ascending), whether it is the preview, the spread
        asked for and the one that plays, in ms, and the order the notes start in. */
    struct Model
    {
        std::vector<int> chord;
        bool preview = true;
        bool applies = true;     ///< the card's pattern makes chords (CHORD)
        double requestedMs = 0.0, playedMs = 0.0;
        std::vector<int> order;  ///< indices into `chord`, first to last note-on
        int direction = 0;
    };
    Model model() const;

private:
    InstrumentHost& host;
    int card = 0;
    KeyHeld held;
    juce::String drawnFor;
};

class StrumPanel final : public MiniPanel
{
public:
    StrumPanel (InstrumentHost& host, int card, StrumDisplay::KeyHeld held);
    ~StrumPanel() override;

    static constexpr int width = 400;
    bool compact() const override { return true; }
    juce::Point<int> cardSize() const override;
    void paint (juce::Graphics&) override;
    void refreshContent() override;
    void settleNow() override { refreshContent(); }
    void mouseDown (const juce::MouseEvent&) override;

    /** The card shown (0 A .. 2 C): its settings; never switches it. */
    void showCard (int card);
    int shownCard() const noexcept { return cardIndex; }
    juce::Rectangle<int> cardChipArea (int c) const { return chips[static_cast<std::size_t> (juce::jlimit (0, 2, c))]; }
    bool showsCards() const noexcept { return cards; }

    StrumDisplay& display() noexcept { return *sweep; }
    MacroSwitchLamp& lamp() noexcept { return *onLamp; }
    SegmentedControl& directionControl() noexcept { return *direction; }
    SegmentedControl& timingControl() noexcept { return *timing; }
    MiniKnob& spreadKnob() noexcept { return *ms; }
    MiniKnob& divisionKnob() noexcept { return *division; }
    EqValueField& spreadField() noexcept { return *msField; }

protected:
    void layoutContent (juce::Rectangle<int> area) override;

private:
    void build();
    juce::String id (const char* field) const;
    InstrumentHost& host;
    int cardIndex = 0;
    bool cards = false;
    StrumDisplay::KeyHeld held;
    std::unique_ptr<MacroSwitchLamp> onLamp;
    std::unique_ptr<StrumDisplay> sweep;
    std::unique_ptr<SegmentedControl> direction, timing;
    std::unique_ptr<MiniKnob> ms, division;
    std::unique_ptr<EqValueField> msField;
    std::array<juce::Rectangle<int>, 3> chips {};
};

} // namespace osp::plugin
