#pragma once

#include "InstrumentHost.h"
#include "MainSections.h"
#include "ShapingPopups.h"
#include "engine/SatelliteMotion.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

namespace osp::plugin
{

/**
    SATELLITE's panel (ANDOR ANALOG's, docs/features/SATELLITE_RHYTHM_ARCHITECTURE.md in its
    repository; shared here so every ANDOR instrument with per-card arpeggiators shows the same
    one), opened from the SATELLITE key on the per-card arpeggiator's title row. The SUB / SHAPER
    DNA popovers' language: a compact bone card, one dark display, a few keys and knobs. A / B / C
    choose whose settings it shows (choosing never switches a card); the light on the right
    switches the shown card.

    The product names its parameters (`idOf (card, field)`, fields: enabled, mode, sync, division,
    rate, depth, width, phase, offset, retrigger - the shared schema's ranges) and reports where
    the audio has a card (InstrumentHost::satelliteView).
*/
namespace satview
{
    using IdOf = std::function<juce::String (int card, const char* field)>;
    satellite::Settings settingsOf (InstrumentHost& host, const IdOf& idOf, int card);
    juce::Colour cardColour (int card) noexcept;
}

/**
    The path, from the card's real settings: PAN's track, ORBIT's circle (its far half quieter),
    BOUNCE's two places. The marker is where the audio has the card now (the engine's own
    telemetry, not an animation); with nothing moving it rests, hollow, where the path starts.
*/
class SatelliteDisplay final : public juce::Component
{
public:
    SatelliteDisplay (InstrumentHost& host, satview::IdOf idOf, int card);
    void setCard (int card);
    void paint (juce::Graphics&) override;
    /** Repaints when the path or the marker moved (the panel's refresh). */
    void refresh();
    /** Where the marker is drawn (tests: the live place, in the display's coordinates). */
    juce::Point<float> markerAt() const noexcept { return marker; }
    bool markerLive() const noexcept { return live; }

private:
    InstrumentHost& host;
    satview::IdOf idOf;
    int card = 0;
    juce::String drawnFor;
    juce::Point<float> marker;
    bool live = false;
};

class SatellitePanel final : public MiniPanel
{
public:
    SatellitePanel (InstrumentHost& host, satview::IdOf idOf, int card);
    ~SatellitePanel() override;

    static constexpr int width = 420;
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

    SatelliteDisplay& display() noexcept { return *pathDisplay; }
    MacroSwitchLamp& lamp() noexcept { return *onLamp; }
    SegmentedControl& modeControl() noexcept { return *mode; }
    SegmentedControl& timingControl() noexcept { return *timing; }
    SegmentedControl& bounceControl() noexcept { return *bounce; }
    MiniKnob& divisionKnob() noexcept { return *division; }
    MiniKnob& rateKnob() noexcept { return *rate; }
    MiniKnob& depthKnob() noexcept { return *depth; }
    MiniKnob& widthKnob() noexcept { return *spread; }
    MiniKnob& phaseKnob() noexcept { return *phase; }
    MiniKnob& offsetKnob() noexcept { return *offset; }

protected:
    void layoutContent (juce::Rectangle<int> area) override;

private:
    void build();
    InstrumentHost& host;
    satview::IdOf idOf;
    int cardIndex = 0;
    std::unique_ptr<MacroSwitchLamp> onLamp;
    std::unique_ptr<SatelliteDisplay> pathDisplay;
    std::unique_ptr<SegmentedControl> mode, timing, bounce;
    std::unique_ptr<MiniKnob> division, rate, depth, spread, phase, offset;
    std::array<juce::Rectangle<int>, 3> chips {};
    juce::Rectangle<int> keysRow, bounceCaption;
};

} // namespace osp::plugin
