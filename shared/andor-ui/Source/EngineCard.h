#pragma once

#include "CardWellEditor.h"
#include "WellOverlay.h"
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
    /** The playback modes as symbols (the mode selector and its menu), drawn in a square `area`
        (16 px at the reference): One Shot, a start marker and a forward arrow; Granular, a small
        uneven cloud of grains. Equal visual weight at the same size and stroke. */
    void drawOneShotIcon (juce::Graphics&, juce::Rectangle<float> area, juce::Colour colour, float stroke = 1.4f);
    void drawGranularIcon (juce::Graphics&, juce::Rectangle<float> area, juce::Colour colour);
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
    void setGeneratedCycle (std::vector<float> cycle, bool periodic = true, const juce::String& placeholder = {});
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
    /** The ruler's labels keep clear of this many pixels at the top-right (a control sits there). */
    void setRulerReserve (int pixels);
    /** ... and of this many at the top-left (the modifiers' row in the well). */
    void setRulerLeftReserve (int pixels);
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

    /** Tests: the atlas notes (indices) lit now because a read head is inside them. */
    const std::vector<int>& litNoteIndices() const noexcept { return litNotes; }

private:
    juce::Rectangle<float> noteRegionBounds (const atlas::NoteEvent&, juce::Rectangle<float> plot) const;
    /** The atlas notes the read heads are in (a note sounding lights its region). */
    void updateLitNotes();
    std::vector<int> litNotes, litScratch;
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
    bool generatedPeriodic = true;       ///< a period (drawn twice), or an outline (drawn once)
    juce::String generatedPlaceholder;   ///< a stand-in's caption (its picture drawn faint), or empty
    int layer = 0;
    bool focused = true, loading = false;
    View view;
    std::vector<Dot> grains, heads;
    juce::String dropLabel;
    int bottomInset = 0, overlayBand = 0, rulerReserve = 0, rulerLeftReserve = 0;
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
    /** A small icon key in a dark well (EngineCard::setModifiersInWell): graphite, drawn by
        itself, and reachable from the keyboard (space or return toggles it). Opt-in. */
    void setInWell (bool inWell);
    bool isInWell() const noexcept { return inWell; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { skin::repaintKey (*this); }
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { skin::repaintKey (*this); }
    void mouseExit (const juce::MouseEvent&) override { skin::repaintKey (*this); }
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    skin::KeyRole keyRole() const override { return skin::KeyRole::modifier; }
    skin::KeyState keyState() const override;
    bool keyOnSurface() const override { return ! inWell; }
    /** One toggle (a click, or space / return), as a host gesture. */
    void toggle();

private:
    icons::Kind icon;
    juce::Colour onColour;
    juce::String tip;
    bool on = false, suppressed = false, inWell = false;
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

/** REC (live capture, a host with liveCapture()): opens the layer's CAPTURE page; its dot
    turns red and pulses while a take for this layer is being recorded. Narrow cards: the dot. */
class RecButton final : public juce::Button, public skin::KeyFace
{
public:
    RecButton() : juce::Button ("REC")
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Record or grab from the audio input into this layer (it opens over the waveform)");
        setTitle ("REC");
    }
    void setRecording (bool on, float pulse);
    bool isRecording() const noexcept { return recording; }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void buttonStateChanged() override { skin::repaintKey (*this); }
    skin::KeyRole keyRole() const override { return skin::KeyRole::eq; }
    skin::KeyState keyState() const override
    {
        return getToggleState() || isDown() ? skin::KeyState::pressed : isOver() ? skin::KeyState::hover : skin::KeyState::normal;
    }

private:
    bool recording = false;
    float glow = 0.0f;
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

/**
    SYNTHESIZE (Phase S1) in a source's well: a slim graphite fader in the ruler's top-right
    corner, in the KEY DNA key's hairline vocabulary, in two parts. The label ("SYNTH" and a
    small chevron) opens the SYNTH page with one click. The amount, on the right, is the fader:
    drag (up or right) or turn the wheel to set it, Alt for fine; double-click returns it to 0.
    While it is above 0 a small light in the layer's colour and a thin bar show how much; with
    the spectral imprint on (Imprint, IMPRINT above 0) the light is the imprint's small curve.
    It takes no room from the card: the well's ruler leaves its corner free
    (SourceDisplay::setRulerReserve).
*/
class SynthChip final : public juce::Slider
{
public:
    SynthChip (InstrumentHost& processor, int layer);
    void paint (juce::Graphics&) override;
    /** Right-click (or Ctrl-click): the development audition menu (blend law, envelope). A
        press on the label is the page's, not the fader's. */
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    /** A click on the label opens the SYNTH page. */
    void mouseUp (const juce::MouseEvent&) override;
    /** On the amount: back to 0. */
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    std::function<void()> onOpenPage;
    /** The label (opens the page) and the amount (the fader), in the chip's coordinates. */
    juce::Rectangle<float> labelArea() const;
    juce::Rectangle<float> amountArea() const;
    /** The amount's part (px): "100%" and a little room either side. */
    static constexpr float amountWidth = 38.0f;
    /** The audition menu's items, as shown (tests). */
    juce::PopupMenu auditionMenu();
    /** Repaints when the audition or the imprint changed (another source's menu, the page,
        automation): the UI timer. */
    void refreshAudition()
    {
        const int now = host.synthAuditionBlend() | (host.synthAuditionEnvelope() << 8) | (imprinting() ? 1 << 16 : 0);
        if (now != shownAudition)
        {
            shownAudition = now;
            repaint();
        }
    }
    void mouseEnter (const juce::MouseEvent& e) override { juce::Slider::mouseEnter (e); repaint(); }
    void mouseExit (const juce::MouseEvent& e) override
    {
        juce::Slider::mouseExit (e);
        overLabel = false;
        repaint();
    }
    /** The amount, 0..100 %. */
    float amount() const noexcept { return static_cast<float> (getValue()); }
    /** The spectral imprint colours this source's oscillator (Imprint, IMPRINT above 0). */
    bool imprinting() const;

private:
    InstrumentHost& host;
    int layer = 0;
    int shownAudition = 0;
    bool pressedLabel = false, overLabel = false;
    juce::Colour accent;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/**
    ONE SHOT / GRANULAR as a compact selector with a menu. ANDOR OSP's: the active mode's symbol
    and a quiet chevron in a small key (iconOnly()); its menu names both modes with their symbols,
    a line on what each does and a check by the active one. A product that names the first mode
    otherwise, or fits the key to the names (a generator's "Regular" / "Granular"), keeps the
    named key and plain menu, exactly as before.
*/
class ModeSelector final : public juce::Component, public juce::SettableTooltipClient, public skin::KeyFace
{
public:
    explicit ModeSelector (juce::RangedAudioParameter& parameter);
    std::function<void (int)> onChange;
    int mode() const noexcept { return current; }
    /** The first mode's name ("One Shot"; a product's generator may name it otherwise). */
    void setFirstModeName (const juce::String& name);
    /** Opt-in: the key only as wide as its longer name needs (a generator's "Regular" /
        "Granular"), its chevron closer to the name. Off: as before. */
    void setFitsName (bool fit);
    bool fitsName() const noexcept { return fitted; }
    /** The width that fits the longer name (px, at the reference 41 px height). */
    float fittedWidth() const;
    /** The symbol selector (ANDOR OSP's modes under their own names, not fitted to them). */
    bool iconOnly() const noexcept { return ! toggle && ! fitted && firstModeName == "One Shot"; }
    /** Opt-in (ANDOR ANALOG's oscillators): GRANULAR as an on / off key, the grain cloud alone
        (no chevron, no menu), lit in `litInk` while the layer plays grains; a click or Return /
        Space toggles it. Off: the selector with its menu. */
    void setToggle (bool on, juce::Colour litInk);
    bool isToggle() const noexcept { return toggle; }
    /** The symbol selector's width (px at the reference): a small key, not a button. */
    static constexpr float iconWidth = 42.0f, iconWidthNarrow = 40.0f;
    /** The tooltip of the named key (a product's); the symbol selector says its mode. */
    void setNamedTooltip (const juce::String& text);
    /** The modes' menu as the selector shows it (tests read its items). */
    juce::PopupMenu createMenu();
    /** Opens the menu below the selector (a click does this); `parent`: a component to hold it
        (the screenshots), else its own window as before. */
    void showMenu (juce::Component* parent = nullptr);
    /** The menu as it looks open, as a plain component (its background, border and the same
        items), for screenshots where no menu window can open (headless). */
    std::unique_ptr<juce::Component> createMenuPicture();
    /** A mode's name and what it does, as the menu says them. */
    juce::String modeName (int mode) const { return mode == 1 ? juce::String ("Granular") : firstModeName; }
    static juce::String modeDescription (int mode) { return mode == 1 ? "Resynthesize as a grain cloud" : "Play the original recording"; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { skin::repaintKey (*this); }
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { skin::repaintKey (*this); }
    void mouseExit (const juce::MouseEvent&) override { skin::repaintKey (*this); }
    bool keyPressed (const juce::KeyPress&) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    skin::KeyRole keyRole() const override { return skin::KeyRole::mode; }
    skin::KeyState keyState() const override
    {
        // (the toggle: pressed in while on, as EQ while open)
        return isMouseButtonDown() || (toggle && current == 1) ? skin::KeyState::pressed : isMouseOver() ? skin::KeyState::hover : skin::KeyState::normal;
    }
    /** The key's body: as tall as NOTES and EQ beside it (33 px), on their centre line; the
        whole 41 px stays the hit area. */
    juce::Rectangle<float> keyArea (juce::Rectangle<float> bounds) const override { return bounds.withSizeKeepingCentre (bounds.getWidth(), std::min (bounds.getHeight(), bodyHeight)); }
    static constexpr float bodyHeight = 33.0f;

private:
    void choose (int mode);
    /** The tooltip and accessible description after a change of mode or presentation. */
    void describe();
    int current = 0;
    juce::String firstModeName { "One Shot" };
    juce::String namedTooltip;
    bool fitted = false, toggle = false;
    juce::Colour toggleInk;
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
    /** SYNTHESIZE's control (null when the host has none). */
    SynthChip* synthesizeControl() noexcept { return synthChip.get(); }
    /** The playback-mode selector (null without the parameter). */
    ModeSelector* modeSelector() noexcept { return mode.get(); }

    /** A generator's card: START's knob becomes one of the product's own controls (its
        parameter and name); a click on the name calls `onCaptionClick` (null: none). The card
        lays it out exactly where START was. */
    void replaceStartKnob (const juce::String& parameterId, const juce::String& caption, std::function<void()> onCaptionClick);
    /** A product's control in any knob's place (slot 0..4: START TUNE PAN LEVEL REIMAGINED;
        ProductInfo::cardControls). An empty ID hides that knob, its place kept (the row's
        rhythm never moves). restoreKnob gives the slot its own layer control back. */
    void replaceKnob (int slot, const juce::String& parameterId, const juce::String& caption);
    void restoreKnob (int slot);
    /** A visible knob of the card's row bound to `parameterId` (tests, product overrides). */
    bool hasControl (const juce::String& parameterId) const;
    /** The four modifier keys (LINK REVERSE LOOP FOLLOW) shown, or hidden for a source they
        mean nothing to (a product's oscillator, its noise). Shown by default. */
    void setModifiersShown (bool shown);
    /** The modifiers as a compact row of icon keys at the top-left of the dark well instead
        of the keys under the knobs (a product's choice; ANDOR OSP keeps its keys). The card's
        own foot is then free, as on a card without them. */
    void setModifiersInWell (bool inWell);
    /** Which of the four apply to the source now (LINK REVERSE LOOP FOLLOW, bits 0..3): the
        others are not shown. All by default. */
    void setModifiersApplicable (int mask);
    /** How much shorter a narrow card is than the reference's with its modifiers in the well
        (reference pixels): the band under its knobs that it no longer keeps. */
    static constexpr float footTrim = 64.0f;
    /** Tests: modifier `index`'s key (0 LINK .. 3 FOLLOW), or nullptr. */
    ModifierButton* modifierKey (int index) const noexcept { return index >= 0 && index < 4 ? modifiers[static_cast<std::size_t> (index)].get() : nullptr; }
    /** Controls revealed in the well by the pointer (ProductInfo::wellOverlay); null: none.
        Hidden while the EQ, the note map or the in-card editor take the well. */
    void setWellOverlay (std::unique_ptr<WellOverlay> overlay);
    WellOverlay* wellOverlayComponent() const noexcept { return wellOverlay.get(); }
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
    /** The mode selector's first mode under another name (a generator's "Standard"). Opt-in:
        without it the card reads "One Shot", exactly as before. */
    void setFirstModeName (const juce::String& name);
    /** Opt-in: the mode selector only as wide as its names need (ModeSelector::setFitsName);
        the name beside it gets the room. Without it the card is exactly as before. */
    void setModeFitsName (bool fit);
    /** The mode as ModeSelector's GRANULAR on / off key, lit in the layer's colour (opt-in). */
    void setModeToggle (bool on);

    /** A product's own editor in the waveform's place (opt-in; see CardWellEditor). The card
        owns it and lays it out exactly over the display; shown, it replaces the waveform (the
        display stops drawing beneath it). The EQ and NOTE MAP take the well while open: they
        hide it, and the owner of the editor decides whether it returns (setWellEditorShown). */
    void setWellEditor (std::unique_ptr<CardWellEditor> editor);
    CardWellEditor* wellEditorComponent() noexcept { return wellEditor.get(); }
    void setWellEditorShown (bool shown);
    bool isWellEditorShown() const noexcept { return wellEditor != nullptr && wellEditor->isVisible(); }

    /** The layer's EQ over its waveform (a view setting: the EQ keeps playing either way). */
    std::function<void (int, bool)> onEqToggle;        ///< its EQ button (the editor keeps one open at a time)
    void setEqOpen (bool open);
    bool isEqOpen() const noexcept { return eqEditor != nullptr; }
    EqEditor* eqEditorComponent() noexcept { return eqEditor.get(); }
    EqButton& eqToggle() noexcept { return eqButton; }

    /** The layer's SYNTH page (from the SYNTH chip), in the display's place; one page at a
        time in the well (EQ, NOTE MAP, SYNTH). */
    void setSynthOpen (bool open);
    bool isSynthOpen() const noexcept { return synthEditor != nullptr; }
    SynthEditor* synthEditorComponent() noexcept { return synthEditor.get(); }

    /** The layer's CAPTURE page (its REC key; a host with liveCapture()), in the display's place. */
    void setCaptureOpen (bool open);
    bool isCaptureOpen() const noexcept { return captureEditor != nullptr; }
    CaptureEditor* captureEditorComponent() noexcept { return captureEditor.get(); }
    RecButton* recToggle() noexcept { return recButton.get(); }

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
    std::unique_ptr<SynthEditor> synthEditor;          ///< the SYNTH page, while open
    std::unique_ptr<RecButton> recButton;              ///< REC (a host with liveCapture(); null otherwise)
    std::unique_ptr<CaptureEditor> captureEditor;      ///< the CAPTURE page, while open
    std::unique_ptr<SynthChip> synthChip;              ///< SYNTHESIZE in the well's corner (a host with hasSynthesize(); null otherwise)
    std::array<std::unique_ptr<ModifierButton>, 4> modifiers;
    bool modifiersShown = true, modifiersInWell = false;
    int modifiersApplicable = 0xf;
    void updateModifiers();
    LayerStateButton muteButton, soloButton;
    bool shownHeard = true;
    std::array<std::unique_ptr<MiniKnob>, 5> granularKnobs;
    KeyDnaKey keyDnaKey;
    EqButton eqButton { layerIndex };
    std::unique_ptr<EqEditor> eqEditor;
    NotesButton notesButton;
    std::unique_ptr<NoteMapEditor> noteMap;
    std::unique_ptr<WellOverlay> wellOverlay;
    void updateWellOverlay();
    std::unique_ptr<CardWellEditor> wellEditor;   ///< (null on every card that did not ask for one)
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
