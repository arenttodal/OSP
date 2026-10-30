#pragma once

#include "EqEditor.h"
#include "NoteMapEditor.h"
#include "OspLookAndFeel.h"
#include "InstrumentHost.h"
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
    enum class Kind { link, reverse, loop, follow, dots, chevronDown, chevronLeft, chevronRight, heart, heartFilled, plus, cassette };
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
    SourceDisplay();
    /** At most this many read heads are drawn (the loudest notes): past a few, more lines
        are noise rather than information, and every one costs a repaint. */
    static constexpr int maxHeadsShown = 8;
    /** Past this many moving strips, one repaint of the plot is cheaper than the strips. */
    static constexpr int maxStrips = 12;
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
    /** A generated source (a synthesizer's oscillators): one period of its waveform (-1..1),
        drawn across the display in place of a recording. Empty: the recording, as before. */
    void setGeneratedCycle (std::vector<float> cycle);
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
    /** What lies behind the display (its rounded corners show it) is painted from a picture
        of its parents taken once, so the display can be opaque: a moving read head then
        repaints only the display, never the housing and card behind it. Taken again when
        the display moves, resizes, changes scale or skin, or the card's focus changes. */
    void invalidateBackdrop() noexcept { backdropKey = {}; }
    int headsShown() const noexcept { return static_cast<int> (heads.size()); }
    /** Visual tests: the cached pictures (backdrop, waveform) in JUCE's own renderer. The
        native image (CoreGraphics on macOS) is not bit-repeatable: two renders of the same state
        differed by a channel level now and then. On Linux the native image is this renderer
        already. Off in the plug-in (a native cache is drawn per frame without a conversion). */
    static inline bool softwareCaches = false;

    /** Where the recording is drawn: the time window() maps across this. */
    juce::Rectangle<float> plotArea() const;

    /** The part of the recording shown (seconds into the file): from where notes start
        reading (the analysed onset) to where the sound ends (its trailing silence left
        out) plus a short margin, so the sound always fills the display. */
    juce::Range<double> window() const;

private:
    void paintStatic (juce::Graphics&);
    void paintGenerated (juce::Graphics&);
    void paintNoteRegions (juce::Graphics&, juce::Rectangle<float> plot);
    /** Paints, into `g` (this display's coordinates), what its ancestors paint behind it:
        each ancestor's own paint and the siblings below the branch that leads here. */
    void paintBackdrop (juce::Graphics&);
    void moved() override { invalidateBackdrop(); }
    void parentHierarchyChanged() override { invalidateBackdrop(); }
    /** x of a time in the file (seconds), and of a fraction of the whole file (0..1). */
    float xAtSeconds (double seconds, juce::Rectangle<float> plot) const;
    float xAtFraction (double fraction, juce::Rectangle<float> plot) const;
    /** Seconds after the start of the file where a note starts reading (START applied). */
    double startSeconds() const;

    std::shared_ptr<const LoadedInstrument> instrument;
    std::vector<float> generatedCycle;   ///< a generated source's period (empty: none)
    int layer = 0;
    bool focused = true, loading = false;
    View view;
    std::vector<Dot> grains, heads;
    juce::String dropLabel;
    int bottomInset = 0, overlayBand = 0;
    juce::Image cache;
    bool cacheDirty = true;
    juce::Image backdrop;
    juce::String backdropKey;
};

/** A layer knob: caption above, value below; LINK passes the user's changes on. */
class LayerKnob final : public juce::Component
{
public:
    LayerKnob (InstrumentHost&, int layer, const juce::String& control, const juce::String& caption);
    /** A product's own control in a layer knob's place (a generator's card): `parameterId` is
        any parameter; it never moves with LINK. */
    LayerKnob (InstrumentHost&, int layer, const juce::String& caption, const juce::String& parameterId, bool productControl);
    void setCompact (bool compact);
    bool isCompact() const noexcept { return compact; }
    void paint (juce::Graphics&) override;
    void resized() override;

    /** REIMAGINED (and a product's control given a door): its name opens a popover (hover
        darkens and underlines it). */
    std::function<void()> onLabelClick;
    /** The name is a door (REIMAGINED's always is). */
    void setLabelDoor (bool door) { labelDoor = door; }
    void setLabelOpen (bool open);
    /** Where the name is drawn (this component's coordinates). */
    juce::Rectangle<float> labelBounds() const;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    class Dial;
    std::unique_ptr<Dial> dial;

    /** The knob's design is 125 units tall (name, dial, value); the component is this many
        units taller so the name can sit higher above the dial's tick dots, as far above them
        as the value sits below the lowest ones. The card grows the bounds upwards by as much,
        so the dial and the value stay where they were. */
    static constexpr float labelLift = 15.1f;
    static constexpr float designUnits = 125.0f + labelLift;

private:
    InstrumentHost& processor;
    juce::RangedAudioParameter* parameter = nullptr;
    juce::String caption;
    bool compact = false;
    bool creative = false;   ///< REIMAGINED: the same knob, a coral arc and a small coral light by its name
    bool labelHover = false, labelOpen = false;
    bool labelDoor = false;   ///< the name opens something (REIMAGINED; a product's door)
    juce::Font labelFont() const;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** A source modifier toggle (LINK, REVERSE, LOOP, FOLLOW) bound to a layer's bool parameter. */
class ModifierButton final : public juce::Component, public juce::SettableTooltipClient, public skin::KeyFace
{
public:
    ModifierButton (juce::RangedAudioParameter& parameter, icons::Kind icon, juce::Colour onColour);
    void setSuppressed (bool suppressed, const juce::String& why);
    bool isOn() const noexcept { return on; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { skin::repaintKey (*this); }
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { skin::repaintKey (*this); }
    void mouseExit (const juce::MouseEvent&) override { skin::repaintKey (*this); }
    skin::KeyRole keyRole() const override { return skin::KeyRole::modifier; }
    skin::KeyState keyState() const override;

private:
    icons::Kind icon;
    juce::Colour onColour;
    juce::String tip;
    bool on = false, suppressed = false;
    juce::ParameterAttachment attachment;
};

/** A layer's MUTE or SOLO: a small key beside its badge, bound to its bool parameter.
    SOLO lights in the layer's colour, MUTE in graphite; alt-click on SOLO solos it alone. */
class LayerStateButton final : public juce::Component, public juce::SettableTooltipClient, public skin::KeyFace
{
public:
    LayerStateButton (InstrumentHost& processor, int layer, bool solo);
    bool isOn() const noexcept { return on; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { skin::repaintKey (*this); }
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { skin::repaintKey (*this); }
    void mouseExit (const juce::MouseEvent&) override { skin::repaintKey (*this); }
    skin::KeyRole keyRole() const override { return skin::KeyRole::layerState; }
    skin::KeyState keyState() const override;

private:
    InstrumentHost& processor;
    int layer;
    bool solo, on = false;
    juce::ParameterAttachment attachment;
};

/** "NOTES" in a source card's header (unified engine Phase 5): only while the layer plays a
    Note Atlas; opens its NOTE MAP in the display's place, as EQ opens the EQ. Styled as EQ. */
class NotesButton final : public juce::Button, public skin::KeyFace
{
public:
    NotesButton() : juce::Button ("NOTES")
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("This layer's notes: where each one is, its pitch and key (it opens over the waveform)");
        setTitle ("NOTES");
    }
    void setNeedsReview (bool on) { if (review != on) { review = on; repaint(); } }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void buttonStateChanged() override { skin::repaintKey (*this); }
    skin::KeyRole keyRole() const override { return skin::KeyRole::eq; }
    skin::KeyState keyState() const override
    {
        return getToggleState() || isDown() ? skin::KeyState::pressed : isOver() ? skin::KeyState::hover : skin::KeyState::normal;
    }

private:
    bool review = false;
};

/** "KEY DNA" at the end of the Granular hover strip (Phase 5): opens the layer's KEY DNA
    editor; a small light while the layer's KEY DNA is on. */
class KeyDnaKey final : public juce::Component, public juce::SettableTooltipClient
{
public:
    KeyDnaKey() { setMouseCursor (juce::MouseCursor::PointingHandCursor); setTooltip ("KEY DNA: how this layer's grains differ from key to key"); setTitle ("KEY DNA"); }
    std::function<void()> onClick;
    void setLit (bool on) { if (lit != on) { lit = on; repaint(); } }
    void setColour (juce::Colour c) { accent = c; }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (onClick != nullptr && getLocalBounds().contains (e.getPosition())) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    bool lit = false;
    juce::Colour accent;
};

/** ONE SHOT / GRANULAR as a compact selector with a menu. */
class ModeSelector final : public juce::Component, public juce::SettableTooltipClient, public skin::KeyFace
{
public:
    explicit ModeSelector (juce::RangedAudioParameter& parameter);
    std::function<void (int)> onChange;
    int mode() const noexcept { return current; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { skin::repaintKey (*this); }
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { skin::repaintKey (*this); }
    void mouseExit (const juce::MouseEvent&) override { skin::repaintKey (*this); }
    skin::KeyRole keyRole() const override { return skin::KeyRole::mode; }
    skin::KeyState keyState() const override { return isMouseButtonDown() ? skin::KeyState::pressed : isMouseOver() ? skin::KeyState::hover : skin::KeyState::normal; }

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
class EngineCard final : public juce::Component, public skin::SurfaceFace
{
public:
    float surfaceRadius() const override { return design::layout::cardRadius; }
    bool surfaceIsCard() const override { return true; }
    EngineCard (InstrumentHost& processor, int layer);
    ~EngineCard() override;

    int layer() const noexcept { return layerIndex; }
    void setDensity (EngineLayoutDensity density);
    void setFocused (bool focused);
    bool isFocused() const noexcept { return focused; }

    /** Pulls the layer's state (instrument, loading, mode, grains) from the processor (UI timer). */
    void refresh();
    /** The granular view (POS, SPREAD) and the playing grains; START in percent (0 for a
        generator, whose grains have no start). */
    void refreshGrains (float startPercent);
    SourceDisplay& display() noexcept { return sourceDisplay; }

    std::function<void (int)> onFocus;                 ///< the card was clicked
    std::function<void (int, juce::Component&)> onMenu; ///< its menu button
    std::function<void (int)> onReimagined;            ///< its REIMAGINED name (the mode popover)
    std::function<void (int)> onKeyDna;                ///< the KEY DNA key in the Granular strip
    KeyDnaKey& keyDnaToggle() noexcept { return keyDnaKey; }
    /** The REIMAGINED name, in this card's coordinates (the popover's anchor). */
    juce::Rectangle<int> reimaginedLabelBounds() const;
    /** Whether the REIMAGINED name is the door of a component (a click on it toggles the popover). */
    bool isReimaginedLabel (const juce::Component* c) const noexcept { return c != nullptr && c == knobs[4].get(); }
    void setReimaginedOpen (bool open);

    /** A generator's card: START's knob becomes one of the product's own controls (its
        parameter and name); a click on the name calls `onCaptionClick` (null: none). The card
        lays it out exactly where START was. */
    void replaceStartKnob (const juce::String& parameterId, const juce::String& caption, std::function<void()> onCaptionClick);
    juce::Rectangle<int> startLabelBounds() const;
    bool isStartLabel (const juce::Component* c) const noexcept { return c != nullptr && c == knobs[0].get(); }
    void setStartOpen (bool open) { knobs[0]->setLabelOpen (open); }

    /** A generator's heading (its name beside M and S) as a door: a click, Return or Space on
        the name calls `onHeadingClick`. Opt-in: without it the name is plain text, exactly as
        before. The target covers the name's glyphs only (never the EQ, mode or menu). */
    void setHeadingDoor (std::function<void()> onHeadingClick);
    /** The name's glyphs, in this card's coordinates (the popover's anchor); empty: no door. */
    juce::Rectangle<int> headingBounds() const;
    bool isHeading (const juce::Component* c) const noexcept;
    bool hasHeadingDoor() const noexcept { return headingDoor != nullptr; }
    void setHeadingOpen (bool open);

    /** The layer's EQ over its waveform (a view setting: the EQ keeps playing either way). */
    std::function<void (int, bool)> onEqToggle;        ///< its EQ button (the editor keeps one open at a time)
    void setEqOpen (bool open);
    bool isEqOpen() const noexcept { return eqEditor != nullptr; }
    EqEditor* eqEditorComponent() noexcept { return eqEditor.get(); }
    EqButton& eqToggle() noexcept { return eqButton; }

    /** The layer's NOTE MAP (a Note Atlas layer only), in the display's place; the EQ and the
        NOTE MAP are never open together. */
    std::function<void (int, bool)> onNotesToggle;
    void setNotesOpen (bool open);
    bool isNotesOpen() const noexcept { return noteMap != nullptr; }
    NoteMapEditor* noteMapComponent() noexcept { return noteMap.get(); }
    NotesButton& notesToggle() noexcept { return notesButton; }
    /** Tests: the "Map notes?" prompt when it is shown (null otherwise), and its answer. */
    juce::Component* mapNotesPromptComponent() noexcept { return mapPrompt.get(); }
    void answerMapNotesPrompt (bool mapNotes);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    /** The sample's details (full file name, length, rate, channels, root), shown in a small
        card when the pointer rests on the A / B / C badge (the header no longer spells the
        file name out). */
    juce::StringArray sourceInfoLines() const;
    void showSourceInfo (bool show);
    bool isSourceInfoShown() const noexcept { return infoCard != nullptr && infoCard->isVisible(); }
    juce::Rectangle<int> badgeBounds() const noexcept { return badgeArea; }

    /** Shows / hides the granular controls over the display (they fade). */
    void showGranularControls (bool show, bool immediately = false);   ///< immediately: no fade (a source being dragged)
    bool granularControlsShown() const noexcept { return granularShown; }
    /** The layer plays Granular (its POS SIZE DENS TUNE SPREAD exist to be shown). */
    bool isGranular() const noexcept;

private:
    void updateMode();
    bool granularShown = false;

    InstrumentHost& processor;
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
    LayerStateButton muteButton, soloButton;
    bool shownHeard = true;
    std::array<std::unique_ptr<MiniKnob>, 5> granularKnobs;
    KeyDnaKey keyDnaKey;
    EqButton eqButton { layerIndex };
    std::unique_ptr<EqEditor> eqEditor;
    NotesButton notesButton;
    std::unique_ptr<NoteMapEditor> noteMap;
    bool atlasLayer = false;
    juce::Rectangle<int> badgeArea, textArea, dividerArea;
    juce::String rootText, fileText;
    struct HeadingDoor;
    std::unique_ptr<HeadingDoor> headingDoor;   ///< (null on every card that did not ask for one)
    void placeHeadingDoor();
    bool generated = false;               ///< the layer's sound is made by the instrument itself
    std::uint64_t shownGeneratedRevision = ~0ull;
    struct InfoCard final : juce::Component
    {
        juce::StringArray lines;
        void paint (juce::Graphics&) override;
    };
    std::unique_ptr<InfoCard> infoCard;
    /** "Map notes?" over the display (Phase 5 review): a dropped recording with several notes
        asks once, at once, whether to become an instrument of its notes. Exists only while the
        host asks (InstrumentHost::mapNotesPrompt), so an ordinary card is drawn as before. */
    struct MapPrompt final : juce::Component
    {
        int notes = 0;
        std::function<void (bool)> onAnswer;   ///< true: MAP NOTES; false: keep it one sample
        juce::Rectangle<float> yesArea, noArea;
        int hover = 0;                         ///< 1 MAP NOTES, 2 NO
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
    };
    std::unique_ptr<MapPrompt> mapPrompt;
    void updateMapPrompt();
    void placeMapPrompt();
    struct Delay final : juce::Timer
    {
        std::function<void()> fire;
        void timerCallback() override { stopTimer(); if (fire != nullptr) fire(); }
    };
    Delay infoDelay;
    bool overBadge = false;
};

} // namespace osp::plugin
