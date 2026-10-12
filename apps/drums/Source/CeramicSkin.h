#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

/**
    "09 Warm Ceramic + Teak" (docs/design/warm-ceramic-teak-style-guide.md) drawn in code for
    ANDOR Drums: the same displayed colours, radii and light (one soft key light, top-left;
    soft contact shadows down and slightly right) as the guide. The approved Blender macro
    knob is not redrawn here: it is the shared sprite (SpriteKnobRenderer). Everything here
    is message-thread painting; the expensive parts (teak grain, shadows) are cached images.
*/
namespace osp::drumsplugin::ceramic
{

// §11 colour summary (displayed)
namespace colour
{
    const juce::Colour backdrop { 0xffb7a89c };
    const juce::Colour teakLight { 0xffc28c68 };
    const juce::Colour teakDark { 0xffb98664 };
    const juce::Colour housingTop { 0xffe9e2d4 };
    const juce::Colour housingBottom { 0xffe0d9cb };
    const juce::Colour cardTop { 0xfffcf5eb };
    const juce::Colour cardMiddle { 0xfff9f2e8 };
    const juce::Colour cardBottom { 0xfff7f0e5 };
    const juce::Colour panelTop { 0xfff3ede1 };
    const juce::Colour panelBottom { 0xfff2ebdf };
    const juce::Colour keybedFloor { 0xffefeadf };
    const juce::Colour keyNormal { 0xfff2ece4 };
    const juce::Colour keyOn { 0xfff8e0cf };
    const juce::Colour presetBar { 0xfffaf4eb };
    const juce::Colour modeSelector { 0xfff8f2ea };
    const juce::Colour faderCap { 0xffc8c4bd };
    const juce::Colour faderRail { 0xffb1a491 };
    const juce::Colour ledOff { 0xff5a5752 };
    const juce::Colour accent { 0xffe8692a };
    const juce::Colour text { 0xff23211e };
    const juce::Colour textSecondary { 0xff5d5850 };
    const juce::Colour textMicro { 0xff8a847b };
    const juce::Colour well { 0xff1f2324 };
    const juce::Colour wellRim { 0xff111415 };
}

/** Soft contact shadow under a rounded body (down and slightly right, as the key light). */
void contactShadow (juce::Graphics&, juce::Rectangle<float> body, float radius, float height = 1.0f);
/** Oiled teak cheek (§3): straight grain along the length, rounded oiled edges. Cached. */
void teakCheek (juce::Graphics&, juce::Rectangle<float> r);
/** The housing slab (§2): 7 px corners, a rounded 2 mm edge, a contact shadow. */
void housing (juce::Graphics&, juce::Rectangle<float> r);
/** Satin ceramic ivory card (§4): a raised slab with a soft edge and a faint sheen. */
void card (juce::Graphics&, juce::Rectangle<float> r, float radius = 12.0f, float height = 1.0f);
/** The macro panel's satin ceramic (§4). */
void panel (juce::Graphics&, juce::Rectangle<float> r, float radius = 10.0f);
/** A recess (the keybed's, §4): a floor 1.2 mm down behind a 0.4 mm lip. */
void recess (juce::Graphics&, juce::Rectangle<float> r, float radius = 6.0f, juce::Colour floor = colour::keybedFloor);
/** A dark display in its 1.6 px ceramic lip (§4); returns the well's inside. */
juce::Rectangle<float> display (juce::Graphics&, juce::Rectangle<float> r, float radius = 8.0f, juce::Colour well = colour::well);
/** A glass lens LED (§9): lit in `c`, or a dim tinted lens. */
void lens (juce::Graphics&, juce::Point<float> centre, float diameter, juce::Colour c, bool lit);
/** A ceramic tile badge (§6), e.g. a pad's number. */
void badge (juce::Graphics&, juce::Rectangle<float> r, const juce::String& text, juce::Colour face, juce::Colour ink, float fontHeight);
/** B1 refined flat tab underline (§7): 2 px accent bar with round ends. */
void tabUnderline (juce::Graphics&, float x, float y, float width);

/**
    A1 soft ivory key (§5): a shallow physical key. Normal / hover (lifts 16 % to white) /
    pressed (sinks: shorter shadow) / on (sinks, warm peach face; the text keeps the accent).
    `role` scales the key height (modifiers 1.0 ... layer state 0.6).
*/
class KeyButton : public juce::TextButton
{
public:
    explicit KeyButton (const juce::String& label = {}, float roleScale = 0.8f) : juce::TextButton (label), role (roleScale) {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    float role;
    float corner = 6.0f;
    float fontHeight = 13.5f;
    /** Draws an icon instead of the text (in the text's colour). */
    std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)> icon;
};

/** A small check box in the ceramic style (a lit key with a check when on). */
class CheckKey : public juce::ToggleButton
{
public:
    using juce::ToggleButton::ToggleButton;
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

/** A pill switch (REVERSE). */
class PillSwitch : public juce::ToggleButton
{
public:
    using juce::ToggleButton::ToggleButton;
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

/**
    The volume fader (§8): an ivory C1 cap with three grip grooves on a T3 brushed rail;
    the accent fill runs along the rail to the cap. A LinearHorizontal slider drawn here.
*/
class Fader : public juce::Slider
{
public:
    Fader();
    void paint (juce::Graphics&) override;
};

/** A stereo level meter (two slim bars in a recess), fed lock-free peaks at 30 Hz. */
class Meter : public juce::Component
{
public:
    void setLevels (float left, float right);
    void paint (juce::Graphics&) override;

private:
    float l = 0.0f, r = 0.0f;
};

// Icons, drawn in a colour into a box.
void iconUndo (juce::Graphics&, juce::Rectangle<float>, juce::Colour, bool redo);
void iconChevron (juce::Graphics&, juce::Rectangle<float>, juce::Colour, bool right);
void iconPlay (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
void iconMenu (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
void iconPower (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
void iconKeys (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
void iconGear (juce::Graphics&, juce::Rectangle<float>, juce::Colour);

} // namespace osp::drumsplugin::ceramic
