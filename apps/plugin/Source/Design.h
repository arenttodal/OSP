#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace osp::plugin::design
{

/**
    The approved reference images (design/reference/, PNG) are the visual source of truth.
    The instrument is laid out once, in their coordinate system (1448 x 1086 px), and the
    editor scales that whole layout uniformly to the window. Every position, size and colour
    below is measured from the references (design/reference/palette.md); change them only
    against an overlay (scripts/visual-review.sh), never by eye.
*/
constexpr float width = 1448.0f;
constexpr float height = 1086.0f;

//==============================================================================
namespace colour
{
    // The surround and the housing (one physical instrument on a warm surface).
    const juce::Colour backdropTop { 0xffd6ccc2 };
    const juce::Colour backdropBottom { 0xffe3dacd };
    const juce::Colour housingTop { 0xffeae3d6 };
    const juce::Colour housingBottom { 0xffded5c8 };
    const juce::Colour housingRim { 0xffc9bfb1 };

    // Cards (sources) and panels (blend, macros): a lit top, a warmer, darker foot.
    const juce::Colour cardTop { 0xfff5eee4 };
    const juce::Colour cardBottom { 0xffe9e2d6 };
    const juce::Colour panelTop { 0xfff2ebe0 };
    const juce::Colour panelBottom { 0xffe6dfd2 };
    const juce::Colour edgeLight { 0xfffffbf2 };      ///< 1 px light along a raised top edge
    const juce::Colour edgeShade { 0xffc8c0b2 };      ///< 1 px shade along a raised bottom edge
    const juce::Colour hairline { 0xffd8d0c3 };
    const juce::Colour divider { 0xffcbc2b4 };

    // Dark displays (waveforms, envelope, popup wells).
    const juce::Colour wellA { 0xff1f2324 };
    const juce::Colour wellB { 0xff1c2325 };
    const juce::Colour wellRim { 0xff111415 };
    const juce::Colour wellGrid { 0xff2e3434 };
    const juce::Colour wellText { 0xff8d8c86 };

    // Type.
    const juce::Colour text { 0xff23211e };
    const juce::Colour textSecondary { 0xff5d5850 };
    const juce::Colour textMicro { 0xff8a847b };

    // The one live accent (active states, arcs) and its gradient for filled elements.
    const juce::Colour accent { 0xffe8692a };
    const juce::Colour accentTop { 0xfff36a2c };
    const juce::Colour accentBottom { 0xffd3461a };
    const juce::Colour accentSoft { 0xfff2c8b0 };      ///< an active modifier's fill

    /** The five macros' identities, one natural mineral palette at about the same lightness
        (CIE L* 56-58): LIFE rust, DYNAMICS ochre, CHARACTER moss, MOVEMENT mineral blue,
        SPACE muted mauve. They mark only the macro's thin value arc, its small light, and
        the accents of its popover; panels, text and values stay neutral. */
    enum class Macro { life, dynamics, character, movement, space };
    inline juce::Colour macro (Macro m)
    {
        static const std::array<juce::Colour, 5> colours { juce::Colour (0xffbe7552), juce::Colour (0xffb5843a), juce::Colour (0xff7b8d69),
                                                           juce::Colour (0xff73899c), juce::Colour (0xff96839d) };
        return colours[static_cast<std::size_t> (m)];
    }
    inline juce::Colour macro (int index) { return macro (static_cast<Macro> (juce::jlimit (0, 4, index))); }
    /** The same identity lifted for lines on the graphite displays (equal weight on dark). */
    inline juce::Colour macroOnDark (Macro m) { return macro (m).withMultipliedBrightness (1.32f).withMultipliedSaturation (1.08f); }

    /** REIMAGINED's identity: a restrained spectral continuum along its travel (0..1),
        rust -> amber -> muted gold -> moss -> mineral teal -> dusty blue -> mauve,
        interpolated in OKLab (no neon steps). The same on every layer. */
    juce::Colour reimagined (float position);

    // Layer identities: A warm amber/terracotta, B cool slate-blue, C a quiet sage.
    struct Identity
    {
        juce::Colour badgeTop, badgeBottom, led, arc, thumb, wave;
    };
    inline const Identity& identity (int layer)
    {
        static const std::array<Identity, 3> ids { {
            { juce::Colour (0xffe9844a), juce::Colour (0xffa9420e), juce::Colour (0xfffc9f4f), juce::Colour (0xffe8692a), juce::Colour (0xffef8640), juce::Colour (0xffeeb070) },
            { juce::Colour (0xff8297aa), juce::Colour (0xff324659), juce::Colour (0xff8cc4ef), juce::Colour (0xff5d93c4), juce::Colour (0xff69a9d5), juce::Colour (0xffb4c6dc) },
            { juce::Colour (0xff9db4a0), juce::Colour (0xff4b6b55), juce::Colour (0xffa9d6ae), juce::Colour (0xff6f9e7a), juce::Colour (0xff7fb38a), juce::Colour (0xffbad3b9) },
        } };
        return ids[static_cast<std::size_t> (juce::jlimit (0, 2, layer))];
    }

    // Controls.
    const juce::Colour knobCapTop { 0xfff8f3ea };
    const juce::Colour knobCapBottom { 0xffd9d0c2 };
    const juce::Colour knobRim { 0xffb9ad9b };
    const juce::Colour knobTrack { 0xffcfc6b8 };
    const juce::Colour knobPointer { 0xff141210 };
    const juce::Colour tick { 0xff4a4640 };
    const juce::Colour buttonTop { 0xfff8f4ec };
    const juce::Colour buttonBottom { 0xffe9e3d8 };

    // Keyboard.
    const juce::Colour whiteKeyTop { 0xfffbf8f2 };
    const juce::Colour whiteKeyBottom { 0xffefe9de };
    const juce::Colour blackKeyTop { 0xff3a3936 };
    const juce::Colour blackKeyBottom { 0xff1e1e1d };
    const juce::Colour keyLine { 0xffd2cabd };
}

//==============================================================================
/** Positions in reference pixels (the canonical 1448 x 1086 canvas). */
namespace layout
{
    using R = juce::Rectangle<float>;
    const R housing { 25.0f, 19.0f, 1398.0f, 1053.0f };
    constexpr float housingRadius = 26.0f;

    // Header
    const juce::Point<float> logo { 51.0f, 79.0f };            ///< baseline of OSP/2-OSP
    const R logoSubtitle { 51.0f, 88.0f, 300.0f, 14.0f };
    const R presetBar { 501.0f, 45.0f, 446.0f, 48.0f };
    /** MIX between the preset and VOLUME (two or three layers; nothing for one). */
    const R headerMix { 966.0f, 23.0f, 164.0f, 88.0f };
    /** Master volume: a thin utility slider (VOLUME and its value above the track). */
    const R volume { 1150.0f, 47.0f, 194.0f, 44.0f };
    const R menu { 1366.0f, 50.0f, 28.0f, 36.0f };

    // Sources: one, two or three cards across the band (the mix lives in the header, so
    // the cards take most of the old mix band's room; the rest is air above the macros).
    const R sources { 35.0f, 127.0f, 1379.0f, 525.0f };
    constexpr float cardGap = 13.0f;
    constexpr float cardRadius = 16.0f;

    constexpr float panelRadius = 13.0f;

    // Macros and envelope
    const R macroPanel { 35.0f, 702.0f, 1378.0f, 196.0f };

    // Keyboard row and footer
    const R pitchWheel { 42.0f, 913.0f, 36.0f, 88.0f };
    const R modWheel { 93.0f, 913.0f, 36.0f, 88.0f };
    const R keyboard { 148.0f, 913.0f, 1262.0f, 101.0f };
    const R status { 48.0f, 1038.0f, 600.0f, 24.0f };
    const R advanced { 1253.0f, 1025.0f, 160.0f, 40.0f };

    // Popups float centred over the instrument at the reference's place (SPACE: 607 x 610
    // at (420, 236)); taller ones grow upwards from the same foot.
    constexpr float popupTop = 236.0f;
    constexpr float popupWidth = 607.0f;
    constexpr float popupFoot = 846.0f;
}

//==============================================================================
/** Drawing shared by every section: one material system, not per-component styling. */
/**
    juce::DropShadow, with its blur computed once. juce::DropShadow::drawForPath renders the
    path into an alpha mask, box-blurs it and fills it with the colour - on every paint, also
    for the small regions a moving element dirties (the housing's 26 px shadow alone covers
    the whole instrument). This keeps the blurred mask, keyed by the path's exact outline,
    its sub-pixel phase, the radius and the offset, and repeats only the final fill: the
    same pixels, without the blur. Message thread only (the cache is not shared).
*/
class CachedShadow
{
public:
    CachedShadow (juce::Colour c, int r, juce::Point<int> o) noexcept : colour (c), radius (r), offset (o) {}
    void drawForPath (juce::Graphics&, const juce::Path&) const;
    /** Masks held (tests, diagnostics). */
    static int cachedMaskCount();

private:
    juce::Colour colour;
    int radius;
    juce::Point<int> offset;
};

namespace draw
{
    /** The housing: the instrument's body on its surround, with a broad quiet shadow. */
    void housing (juce::Graphics&, juce::Rectangle<float> bounds);
    /** A raised card or panel: soft contact shadow, vertical light, 1 px lit top edge. */
    void raised (juce::Graphics&, juce::Rectangle<float> r, float radius, juce::Colour top, juce::Colour bottom, float shadow = 1.0f);
    /** A recessed dark display: inner shadow under its top edge, a faint lower reflection. */
    void well (juce::Graphics&, juce::Rectangle<float> r, float radius, juce::Colour fill);
    /** A small raised control face (buttons, the mode selector, the preset bar). */
    void button (juce::Graphics&, juce::Rectangle<float> r, float radius, bool active, bool hover, juce::Colour activeColour);
    /** The instrument's one knob (macros, layer controls, envelope, volume, popups), drawn
        around `centre` from its body radius: tick dots, a grey track with the value arc in
        `arc` (from the start, or from the centre when bipolar), a contact shadow, a bevelled
        rim, a lit cap and a dark pointer. `position` 0..1 along the travel. */
    struct KnobStyle
    {
        juce::Colour arc = colour::accent;
        bool bipolar = false;
        bool ticks = true;
        int tickCount = 9;
        float tickRadius = 1.37f;                      ///< tick circle, in body radii
        float arcRadius = 1.13f;                       ///< track and value arc, in body radii
        juce::Colour pointer = colour::knobPointer;    ///< popups: the accent
        float pointerFrom = 0.36f, pointerTo = 0.8f;   ///< in body radii
        float arcWidth = 0.0f;                         ///< > 0: a thin ring of this width (macros), flat colour
        bool spectral = false;                         ///< REIMAGINED: the value arc travels the spectral continuum
        float spectralLift = 1.0f;                     ///< hover: a few percent brighter
        float startAngle = -2.356f, endAngle = 2.356f;
        bool enabled = true;
    };
    void knob (juce::Graphics&, juce::Point<float> centre, float bodyRadius, float position, const KnobStyle& style);
    /** A glowing indicator dot (layer identity, macro LEDs). */
    void led (juce::Graphics&, juce::Point<float> centre, float diameter, juce::Colour colour, float glow = 1.0f);
}

} // namespace osp::plugin::design
