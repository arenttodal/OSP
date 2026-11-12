#pragma once

#include "CeramicSkin.h"
#include "Design.h"
#include "InstrumentHost.h"
#include "ShapingPopups.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

namespace osp::plugin
{

/**
    The arpeggiator's card beside the keyboard (the approved mockup): a power light, ARP and a
    chevron on top, the pattern and rate ("UP · 1/8") in a small inset below. The light
    switches arp.enabled and nothing else; anywhere else on the card shows or hides the inline
    editor (a view setting) and nothing else. The editor can be open while the arpeggiator is
    off and the other way round.
*/
class ArpControl final : public juce::Component, public juce::SettableTooltipClient, public skin::KeyFace
{
public:
    explicit ArpControl (InstrumentHost& processor);

    std::function<void()> onToggleEditor;
    void setExpanded (bool open);
    /** What a click on the light does: arp.enabled on <-> off (one host gesture). */
    void toggleEnabled();
    /** Follows the parameters (pattern, rate): call from the editor's timer. */
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** A second instrument in the same housing (opt-in, a product's): two rows of the same
        height, each a power light, its lowercase name and a chevron ("arp" above, `name`
        below), and no pattern / rate inset. The lower light switches `enabledParameterId`;
        the rest of the lower row calls onSecondRow. Empty name: ANDOR OSP's card, exactly. */
    void setSecondRow (const juce::String& name, const juce::String& enabledParameterId);
    std::function<void()> onSecondRow;
    /** The lower row's chevron points up while its popover is open. */
    void setSecondRowOpen (bool open);
    bool hasSecondRow() const noexcept { return secondName.isNotEmpty(); }
    bool isSecondRowOn() const noexcept { return secondEnabled; }
    /** The lower row (stacked only): its light, and the whole row. */
    juce::Rectangle<float> secondLightArea() const;
    juce::Rectangle<float> secondRowArea() const;

    juce::Rectangle<float> lightArea() const;
    juce::Rectangle<float> chevronArea() const;
    /** The inset's text, "UP · 1/8". */
    juce::String statusText() const { return status; }
    bool isOn() const noexcept { return enabled; }
    skin::KeyRole keyRole() const override { return skin::KeyRole::arp; }
    skin::KeyState keyState() const override { return hover != 0 ? skin::KeyState::hover : skin::KeyState::normal; }

private:
    InstrumentHost& ospProcessor;
    juce::ParameterAttachment enabledAttachment;
    bool enabled = false, expanded = false;
    juce::String status;
    int hover = 0;   ///< 1 the light, 2 the rest of the card (stacked: of the upper row), 3 the lower light, 4 the rest of the lower row
    juce::String secondName;
    std::unique_ptr<juce::ParameterAttachment> secondAttachment;
    bool secondEnabled = false, secondOpen = false;
    void paintLight (juce::Graphics&, juce::Point<float> centre, bool on, bool hovered, const char* onPiece, const char* offPiece);
    int hoverAt (juce::Point<float>) const;
};

/** "Advanced" and "MOD ›" as raised half cards under the arpeggiator's, pressed in while
    open. The type shrinks a little to fit the half width (never below 12 px); the chevron
    goes when it does not fit. */
class AdvancedCardButton final : public juce::Button, public skin::KeyFace
{
public:
    explicit AdvancedCardButton (const juce::String& caption);
    void buttonStateChanged() override { skin::repaintKey (*this); }
    skin::KeyRole keyRole() const override { return skin::KeyRole::advanced; }
    skin::KeyState keyState() const override
    {
        return getToggleState() || isDown() ? skin::KeyState::pressed : isOver() ? skin::KeyState::hover : skin::KeyState::normal;
    }
    /** The chevron's direction: 1 right, -1 left, 0 none. */
    void setChevron (int direction) { chevron = direction; repaint(); }
    /** A small light: the patch has active modulation (MOD), never a switch. */
    void setIndicator (bool on) { if (indicator != on) { indicator = on; repaint(); } }
    bool hasIndicator() const noexcept { return indicator; }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    int chevron = 1;
    bool indicator = false;
};

/**
    The arpeggiator's inline editor, between the macros and the keyboard when open: ARPEGGIATOR
    and a collapse button; a 16-step display, numbered, each step a bar as high as its note (the
    step sounding lit and marked, from the audio thread's own scheduler; while nothing is held,
    the pattern on a C major chord). Then PATTERN as a small picture of the style (after Live's
    Arpeggiator: the notes of a four-note chord joined over twelve steps) with the list of every
    style beside it, RATE as a knob over the divisions from slowest to fastest, OCTAVES as four
    stacked keys, and GATE and SWING as the instrument's small knobs. No power switch: the light
    by the keyboard is the only one.
*/
/**
    SATELLITE's key on the per-card arpeggiator's title row (opt-in: a product that answers
    InstrumentHost::satelliteParameterId; ANDOR ANALOG). Two targets, as SUB's row: the light
    switches the motion (some card on: all off, remembering which; none on: those again, or the
    card being edited), the name and its chevron open the panel - opening never switches it on,
    switching never opens it. A restrained light, no badge. Keyboard: Space switches, Return opens.
*/
class SatelliteKey final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SatelliteKey (InstrumentHost& host);

    /** The card the panel opens on (the one being edited). */
    std::function<int()> editedCard;
    bool anyOn() const;
    bool isOpen() const;
    /** The light's part (local): a click there switches, anywhere else opens. */
    juce::Rectangle<int> lightArea() const { return getLocalBounds().withWidth (lightPartWidth); }
    /** Its width: the light, the name and the chevron at even spacing (the name's own width). */
    int preferredWidth() const;
    void toggle();
    void openPanel();
    /** Repaints when the light or the panel changed (the editor's refresh). */
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    // Its spacing (px): the light's centre, the name's start, the gap to the chevron, the
    // chevron's half width, the right margin. Light to name and name to chevron: one rhythm.
    static constexpr float lightCentreX = 9.0f, nameX = 21.0f, chevronGap = 9.0f, chevronHalf = 4.0f, rightMargin = 7.0f;
    static constexpr int lightPartWidth = 17;   ///< (to midway between the light and the name)
    juce::String name() const { return "SATELLITE"; }
    InstrumentHost& host;
    int remembered = 0;   ///< the cards switched off by the light (bit per card)
    bool shownOn = false, shownOpen = false;
};

class ArpInlinePanel final : public juce::Component, public skin::SurfaceFace
{
public:
    explicit ArpInlinePanel (InstrumentHost& processor);
    ~ArpInlinePanel() override;
    float surfaceRadius() const override { return design::layout::panelRadius; }
    bool surfaceIsCard() const override { return false; }

    /** The collapse button (top right): hides the editor (a view setting). */
    std::function<void()> onCollapse;
    /** A right-click on OCTAVES (ARP OCTAVES as a modulation destination). */
    std::function<void()> onOctavesMenu;

    /** Pulls the scheduler's step display and the parameters (editor timer, ~30 Hz). */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** The styles in the list's order (musical families, not the parameter's saved order). */
    static const std::array<int, arp::patternCount>& listOrder();
    /** RATE's divisions from slowest to fastest, as parameter choice indices. */
    static const std::vector<int>& rateOrder();
    /** The pattern as the list shows it ("Up", "Pinky Up/Down", ...). */
    static juce::String patternLabel (int pattern);

    /** One host gesture each, as a click does (the card being edited: A unless unlinked). */
    void choosePattern (int pattern);
    void chooseOctaves (int octaves);

    /** The per-card arpeggiator (InstrumentHost::cardArpeggiator): on the title row, PLAYS
        A B C (the cards the arpeggio plays; the others hold the keys) and LINK; unlinked,
        EDIT A | B | C chooses whose PATTERN, RATE and OCTAVES the panel shows and sets
        (GATE, SWING and VELOCITY are shared). Absent elsewhere (ANDOR OSP: as before). */
    bool showsCards() const noexcept { return cards; }
    void toggleCard (int shown);
    void toggleLink();
    void setEditCard (int shown);
    /** The collapse key as the racks' borderless X (opt-in; ANDOR ANALOG's): the same size and
        stroke, 25 px from the right edge. Off: ANDOR OSP's round chevron key. */
    void setCrossClose (bool cross)
    {
        crossClose = cross;
        resized();
        repaint();
    }
    int editedCard() const noexcept { return editCard; }
    juce::Rectangle<float> cardChipArea (int shown) const { return cardChips[static_cast<std::size_t> (juce::jlimit (0, 2, shown))]; }
    juce::Rectangle<float> linkArea() const { return linkChip; }
    juce::Rectangle<float> editTabArea (int shown) const { return editTabs[static_cast<std::size_t> (juce::jlimit (0, 2, shown))]; }
    /** SATELLITE's key (a product's opt-in), or null. */
    SatelliteKey* satelliteKey() noexcept { return satellite.get(); }

    /** Places of the parts (tests, snapshots). */
    juce::Rectangle<float> displayArea() const { return steps; }
    juce::Rectangle<float> patternFieldArea() const { return field; }
    juce::Rectangle<float> patternListArea() const { return list; }
    /** A style's row in the list, or empty while it is scrolled out of view. */
    juce::Rectangle<float> patternRowArea (int pattern) const;
    juce::Rectangle<float> octaveKeyArea (int octaves) const;
    juce::Rectangle<float> collapseArea() const { return collapse; }
    MiniKnob& rateKnob() noexcept { return rate; }
    int firstVisibleRow() const noexcept { return firstRow; }
    /** The style the picture shows: the one under the pointer in the list, else the chosen one. */
    int fieldPattern() const noexcept { return fieldShown; }
    /** The column lit as sounding (-1: none), as last drawn. */
    int litColumn() const noexcept { return shown.current; }

    static constexpr int visibleRows = 6;

private:
    void paintSteps (juce::Graphics&);
    void paintField (juce::Graphics&);
    void paintList (juce::Graphics&);
    void paintOctaves (juce::Graphics&);
    void paintCards (juce::Graphics&);
    void showEditedCard();
    void updatePreview();
    void updateField();
    void scrollTo (int first);
    void revealSelected();
    int rowAt (juce::Point<float> p) const;

    InstrumentHost& ospProcessor;
    juce::ParameterAttachment enabledAttachment, patternAttachment, octavesAttachment;
    MiniKnob rate, gate, swing, velocity;
    bool enabled = false;
    int pattern = 0, octaveCount = 1;
    InstrumentHost::ArpView shown;
    // The per-card arpeggiator.
    bool cards = false, linked = true;
    bool crossClose = false;
    int editCard = 0;
    std::array<bool, 3> cardOn { true, true, true };
    std::array<int, 3> cardPattern {}, cardOctaves { 1, 1, 1 };
    std::vector<std::unique_ptr<juce::ParameterAttachment>> cardAttachments;   ///< LINK, the three cards
    std::array<std::unique_ptr<juce::ParameterAttachment>, 2> lanePattern, laneOctaves;   ///< B's and C's
    std::array<std::unique_ptr<MiniKnob>, 2> laneRate;
    std::array<juce::Rectangle<float>, 3> cardChips {}, editTabs {};
    std::unique_ptr<SatelliteKey> satellite;
    juce::Rectangle<float> playsCaption, linkChip, editCaption;
    Arpeggiator::Display preview;   ///< the pattern on a C major chord, while nothing is held
    Arpeggiator::Display fieldSteps; ///< the picture: the style on C E G B, one octave
    int fieldShown = -1;
    juce::Rectangle<float> title, collapse, numbers, steps, patternCaption, field, list, octaveCaption, octaveKeys;
    int firstRow = 0, hoverRow = -1;
    float wheelPending = 0.0f;
    int hover = 0;   ///< 3 collapse, 10 + n octave key n
};

} // namespace osp::plugin
