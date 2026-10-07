#pragma once

#include "OspLookAndFeel.h"
#include "PluginProcessor.h"
#include "ShapingPopups.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

namespace osp::plugin
{

/** How much room a layer's card has: one layer (Hero), two (Dual) or three (Triple). */
enum class EngineLayoutDensity { hero, dual, triple };

/** Small line icons for the source modifiers and the header (drawn, not glyphs). */
namespace icons
{
    enum class Kind { link, reverse, loop, follow, dots, chevronDown, chevronLeft, chevronRight, heart, heartFilled, plus };
    void draw (juce::Graphics&, Kind, juce::Rectangle<float> area, juce::Colour colour, float stroke = 1.5f);
}

/**
    One layer's sound display: the recording as spectral colour on graphite (each moment
    coloured by its spectral centroid, warm when dark, cool when bright, leaning towards
    the layer's identity), its loudness contour as a faint ghost, a quiet time grid, and on
    top what is happening: START, the loop region, One Shot read heads, Granular POS /
    SPREAD and the grains playing. The static part is cached; overlays move at 30 Hz.
*/
class SourceDisplay final : public juce::Component
{
public:
    struct View
    {
        bool granular = false;
        float start = 0.0f;        ///< START 0..1
        float position = 0.5f;     ///< effective grain POS (START applied), 0..1
        float spread = 0.2f;
        bool loop = true;
        bool reverse = false;
        bool operator== (const View&) const = default;
    };
    struct Dot
    {
        float position = 0.0f, level = 0.0f, lane = 0.5f;
    };

    void setInstrument (std::shared_ptr<const LoadedInstrument>);
    void setLayer (int layer);
    void setFocused (bool focused);
    void setLoading (bool loading);
    void setView (const View&);
    void setGrains (const Dot* dots, int count);
    void setPlayheads (const Dot* heads, int count);
    /** While a file is dragged over this layer: "REPLACE A" (empty: none). */
    void setDropLabel (const juce::String& label);
    /** Room kept free at the bottom (the granular strip sits there). */
    void setBottomInset (int pixels);
    /** A short display: the granular strip floats over the waveform on a translucent band of this height. */
    void setOverlayBand (int pixels);

    void paint (juce::Graphics&) override;
    void resized() override { cacheDirty = true; }

    /** Where the recording is drawn: the time window() maps across this. */
    juce::Rectangle<float> plotArea() const;

    /** The part of the recording shown (seconds into the file): from where notes start
        reading (the analysed onset) to where the sound ends (its trailing silence left
        out) plus a short margin, so the sound always fills the display. */
    juce::Range<double> window() const;

private:
    void paintStatic (juce::Graphics&);
    /** x of a time in the file (seconds), and of a fraction of the whole file (0..1). */
    float xAtSeconds (double seconds, juce::Rectangle<float> plot) const;
    float xAtFraction (double fraction, juce::Rectangle<float> plot) const;
    /** Seconds after the start of the file where a note starts reading (START applied). */
    double startSeconds() const;

    std::shared_ptr<const LoadedInstrument> instrument;
    int layer = 0;
    bool focused = true, loading = false;
    View view;
    std::vector<Dot> grains, heads;
    juce::String dropLabel;
    int bottomInset = 0, overlayBand = 0;
    juce::Image cache;
    bool cacheDirty = true;
};

/** A layer knob: caption above, value below; LINK passes the user's changes on. */
class LayerKnob final : public juce::Component
{
public:
    LayerKnob (OspAudioProcessor&, int layer, const juce::String& control, const juce::String& caption);
    void setCompact (bool compact);
    void paint (juce::Graphics&) override;
    void resized() override;

    /** REIMAGINED: its name is the door to the mode popover (hover darkens and underlines it). */
    std::function<void()> onLabelClick;
    void setLabelOpen (bool open);
    /** Where the name is drawn (this component's coordinates). */
    juce::Rectangle<float> labelBounds() const;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    class Dial;
    std::unique_ptr<Dial> dial;

private:
    OspAudioProcessor& processor;
    juce::RangedAudioParameter* parameter = nullptr;
    juce::String caption;
    bool compact = false;
    bool creative = false;   ///< REIMAGINED: the same knob, a coral arc and a small coral light by its name
    bool labelHover = false, labelOpen = false;
    juce::Font labelFont() const;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** A source modifier toggle (LINK, REVERSE, LOOP, FOLLOW) bound to a layer's bool parameter. */
class ModifierButton final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModifierButton (juce::RangedAudioParameter& parameter, icons::Kind icon, juce::Colour onColour);
    void setSuppressed (bool suppressed, const juce::String& why);
    bool isOn() const noexcept { return on; }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    icons::Kind icon;
    juce::Colour onColour;
    juce::String tip;
    bool on = false, suppressed = false;
    juce::ParameterAttachment attachment;
};

/** ONE SHOT / GRANULAR as a compact selector with a menu. */
class ModeSelector final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ModeSelector (juce::RangedAudioParameter& parameter);
    std::function<void (int)> onChange;
    int mode() const noexcept { return current; }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    void choose (int mode);
    int current = 0;
    juce::ParameterAttachment attachment;
};

/**
    One source layer, the same component for A, B and C (adaptive layout): header (layer,
    root, file, mode, menu), the sound display, START TUNE PAN LEVEL and the modifiers
    LINK REVERSE LOOP FOLLOW; in Granular mode POS SIZE DENS TUNE SPREAD over the display.
    Hero / Dual / Triple change sizes and arrangement, never what is there.
*/
class EngineCard final : public juce::Component
{
public:
    EngineCard (OspAudioProcessor& processor, int layer);
    ~EngineCard() override;

    int layer() const noexcept { return layerIndex; }
    void setDensity (EngineLayoutDensity density);
    void setFocused (bool focused);
    bool isFocused() const noexcept { return focused; }

    /** Pulls the layer's state (instrument, loading, mode, grains) from the processor (UI timer). */
    void refresh();
    SourceDisplay& display() noexcept { return sourceDisplay; }

    std::function<void (int)> onFocus;                 ///< the card was clicked
    std::function<void (int, juce::Component&)> onMenu; ///< its menu button
    std::function<void (int)> onReimagined;            ///< its REIMAGINED name (the mode popover)
    /** The REIMAGINED name, in this card's coordinates (the popover's anchor). */
    juce::Rectangle<int> reimaginedLabelBounds() const;
    /** Whether the REIMAGINED name is the door of a component (a click on it toggles the popover). */
    bool isReimaginedLabel (const juce::Component* c) const noexcept { return c != nullptr && c == knobs[4].get(); }
    void setReimaginedOpen (bool open);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    /** Shows / hides the granular controls over the display (they fade). */
    void showGranularControls (bool show);

private:
    void updateMode();
    bool granularShown = false;

    OspAudioProcessor& processor;
    const int layerIndex;
    EngineLayoutDensity density = EngineLayoutDensity::hero;
    bool focused = true;
    std::uint64_t shownGeneration = ~0ull;
    bool shownLoading = false;

    SourceDisplay sourceDisplay;
    std::unique_ptr<ModeSelector> mode;
    struct MenuDots final : juce::Button
    {
        MenuDots() : juce::Button ("Layer menu") {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    } menuButton;
    std::array<std::unique_ptr<LayerKnob>, 5> knobs;   ///< START TUNE PAN LEVEL REIMAGINED
    std::array<std::unique_ptr<ModifierButton>, 4> modifiers;
    std::array<std::unique_ptr<MiniKnob>, 5> granularKnobs;
    juce::Rectangle<int> badgeArea, textArea, dividerArea;
    juce::Point<float> ledCentre;
    juce::String rootText, fileText;
};

} // namespace osp::plugin
