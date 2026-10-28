#include "InstrumentFrame.h"
#include "Binding.h"

#include "CeramicSkin.h"
#include "OspLookAndFeel.h"

namespace osp::plugin
{

//==============================================================================
void OspKeyboard::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                 juce::Colour, juce::Colour)
{
    using namespace design;
    // Warm white keys with a rounded front, a hairline between them; a held key only warms.
    const auto key = area.withTrimmedRight (1.0f);
    juce::Path shape;
    shape.addRoundedRectangle (key.getX(), key.getY() - 4.0f, key.getWidth(), key.getHeight() + 4.0f, 3.5f, 3.5f, false, false, true, true);
    auto top = colour::whiteKeyTop, bottom = colour::whiteKeyBottom;
    if (isDown)
    {
        top = top.interpolatedWith (colour::accent, 0.12f);
        bottom = bottom.interpolatedWith (colour::accent, 0.16f);
    }
    else if (isOver)
        top = top.darker (0.02f);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, key.getY(), bottom, 0.0f, key.getBottom(), false));
    g.fillPath (shape);
    g.setColour (colour::keyLine);
    g.fillRect (juce::Rectangle<float> (area.getRight() - 1.0f, area.getY(), 1.0f, area.getHeight()));

    const auto label = getWhiteNoteText (note);
    if (label.isNotEmpty())
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::make (std::min (14.0f, area.getWidth() * 0.6f), fonts::Weight::regular, 0.04f));
        // The octave number may run a little past a narrow key: "C4" must stay whole.
        const auto line = area.withTrimmedBottom (7.0f).removeFromBottom (16.0f);
        g.drawText (label, line.withX (area.getX() + 0.2f * area.getWidth()).withWidth (std::max (area.getWidth(), 24.0f)), juce::Justification::centredLeft, false);
    }
}

void OspKeyboard::drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour)
{
    using namespace design;
    const auto r = area.reduced (0.8f, 0.0f).withTrimmedTop (-4.0f);
    juce::Path shape;
    shape.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 2.5f, 2.5f, false, false, true, true);
    design::CachedShadow (juce::Colour (0x55000000), 3, { 1, 2 }).drawForPath (g, shape);
    auto top = colour::blackKeyTop, bottom = colour::blackKeyBottom;
    if (isDown)
    {
        top = top.interpolatedWith (colour::accent, 0.2f);
        bottom = bottom.interpolatedWith (colour::accent, 0.14f);
    }
    else if (isOver)
        top = top.brighter (0.15f);
    g.setGradientFill (juce::ColourGradient (bottom, r.getX(), 0.0f, bottom, r.getRight(), 0.0f, false));
    g.fillPath (shape);
    // A rounded body: lighter down the middle, a lit front bevel.
    juce::ColourGradient body (top.withAlpha (0.0f), r.getX(), 0.0f, top.withAlpha (0.0f), r.getRight(), 0.0f, false);
    body.addColour (0.45, top);
    g.setGradientFill (body);
    g.fillRoundedRectangle (r.reduced (1.5f, 0.0f).withTrimmedBottom (0.12f * r.getHeight()), 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.fillRoundedRectangle (r.withTop (r.getBottom() - 0.12f * r.getHeight()).reduced (1.5f, 1.0f), 1.5f);
}

juce::String OspKeyboard::getWhiteNoteText (int note)
{
    // MIDI 60 = C4, as everywhere else in OSP.
    return note % 12 == 0 ? "C" + juce::String (note / 12 - 1) : juce::String();
}

//==============================================================================
namespace frame
{

namespace
{
    juce::Rectangle<int> at (juce::Rectangle<float> r) { return r.getSmallestIntegerContainer(); }
    // Six across the narrower macro side (data order LIFE..SPACE, ECHO; ECHO sits before SPACE).
    constexpr std::array<float, macroCount> macroCentres { 128.0f, 274.0f, 420.0f, 566.0f, 858.0f, 712.0f };
}

std::array<MacroSpec, macroCount> ospMacroSpecs()
{
    return { { { "life", "LIFE", "Life: how differently each note is performed" },
               { "drive", "DRIVE", "Drive: saturation, from a little warmth to beautiful breakup (tube, tape or crunch)" },
               { "character", "CHARACTER", "Character: the tonal shape (filter)" },
               { "motion", "MOVEMENT", "Movement: how the sound changes through time" },
               { "space", "SPACE", "Space: the room it plays in" },
               { "echo", "ECHO", "Echo: repeats of the sound, from a tape echo or a bucket-brigade delay" } } };
}

void setUpMacro (MacroKnob& knob, int index, const MacroSpec& spec, juce::AudioProcessorValueTreeState& state, juce::Component& content)
{
    knob.label = std::make_unique<MacroLabel> (spec.name, true, false);
    knob.label->setAccent (design::colour::macro (index));
    // The macro's identity colour on a thin value ring (the knob itself stays neutral).
    knob.slider.getProperties().set ("arc", static_cast<juce::int64> (design::colour::macro (index).getARGB()));
    knob.slider.getProperties().set ("thinArc", 2.4);
    knob.slider.getProperties().set ("paramId", spec.parameterId);   // a modulation drop target
    binding::tag (knob.slider, spec.parameterId);
    binding::tag (*knob.label, spec.parameterId);
    // The rendered ivory cap (tools/blender-assets): LIFE is its pilot; the knob skin
    // (Cmd+Option+K, OSP_KNOB_SKIN) decides which macros show it. Looks only.
    knob.slider.getProperties().set ("sprite", "macro-ivory");
    knob.slider.getProperties().set ("spritePilot", index == 0);
    knob.label->setTooltip (spec.hint);
    content.addAndMakeVisible (*knob.label);
    knob.slider.setRotaryParameters (OspLookAndFeel::rotaryStart, OspLookAndFeel::rotaryEnd, true);
    knob.slider.setPopupDisplayEnabled (true, true, &content);
    knob.slider.setColour (juce::TooltipWindow::textColourId, palette::valueBubbleText);   // light on the graphite bubble
    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, spec.parameterId, knob.slider);
    if (auto* param = state.getParameter (spec.parameterId))
        knob.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue())); // resets the amount only
    knob.slider.setTitle (spec.name);
    // The value bubble: whole percent ("63 %"), set after the attachment (which installs the parameter's own text).
    knob.slider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; };
    knob.slider.updateText();
    content.addAndMakeVisible (knob.slider);
}

void setUpKeyboard (OspKeyboard& keyboard)
{
    keyboard.setAvailableRange (21, 108);
    keyboard.setOctaveForMiddleC (4); // MIDI 60 = C4, as everywhere else in OSP
    keyboard.setScrollButtonsVisible (false);
    keyboard.setBlackNoteLengthProportion (0.62f);
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, palette::hairline);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, palette::ivory);
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, palette::ebony);
}

void setUpStatusLabel (juce::Label& label)
{
    label.setFont (type::micro (16.0f));
    label.setColour (juce::Label::textColourId, design::colour::textMicro.darker (0.15f));
    label.setBorderSize ({ 0, 0, 0, 0 });
}

void place (const Parts& parts, bool arpShown)
{
    using namespace design;
    auto set = [] (juce::Component* c, juce::Rectangle<int> r) {
        if (c != nullptr)
            c->setBounds (r);
    };

    // Header: identity left (painted), preset in the middle, volume and menu right.
    set (parts.presetBar, at (layout::presetBar));
    set (parts.savePreset, at (layout::savePreset));
    set (parts.volume, at (layout::volume));
    set (parts.menu, at (layout::menu));

    // The macros: name, knob and its light, at the reference's places.
    if (parts.macros != nullptr)
        for (std::size_t i = 0; i < parts.macros->size(); ++i)
        {
            auto& knob = (*parts.macros)[i];
            // The name stands clear above everything the knob's modulation halo can draw (its
            // outer route arcs, the selection glow and the handle reach 77 px above the knob's
            // centre): the name a little higher, the knob a little lower than before.
            if (knob.label != nullptr)
                knob.label->setBounds (at (juce::Rectangle<float> (150.0f, 22.0f).withCentre ({ macroCentres[i], 719.0f })));
            knob.slider.setBounds (at (juce::Rectangle<float> (132.0f, 132.0f).withCentre ({ macroCentres[i], 818.0f })));
        }
    // The envelope panel's tab row on the macros' title line; its display takes the extra height.
    set (parts.modulationPanel, at (juce::Rectangle<float> (layout::envelopeX, 688.0f, 1405.0f - layout::envelopeX, 206.0f)));

    // The arpeggiator's editor (when shown) takes the keyboard row's place; the row and the
    // footer move down by its height.
    set (parts.arpPanel, at (layout::arpPanel));
    const float shift = arpShown ? layout::arpShift : 0.0f;
    auto low = [&] (juce::Rectangle<float> r) { return at (r.translated (0.0f, shift)); };

    // Keyboard row and footer.
    set (parts.pitchWheel, low (layout::pitchWheel.withHeight (115.0f)));
    set (parts.modWheel, low (layout::modWheel.withHeight (115.0f)));
    if (parts.keyboard != nullptr)
    {
        parts.keyboard->setBounds (low (layout::keyboard));
        parts.keyboard->setKeyWidth (layout::keyboard.getWidth() / 52.0f);   // 88 keys = 52 white keys
    }
    set (parts.status, low (layout::status));
    set (parts.arpControl, low (layout::arpControl));
    set (parts.advanced, low (layout::advanced));
    set (parts.headerMix, at (layout::headerMix));
}

juce::Rectangle<int> sourceArea()
{
    return at (design::layout::sources);
}

juce::Rectangle<int> sourceColumn (juce::Rectangle<int> area, int columns, int index)
{
    const int gap = 12;
    const int width = columns > 0 ? (area.getWidth() - gap * (columns - 1)) / columns : area.getWidth();
    return { area.getX() + index * (width + gap), area.getY(), width, area.getHeight() };
}

void paint (juce::Graphics& g, juce::Component& content, const Identity& identity, bool arpShown, const MacroRow& macros)
{
    using namespace design;
    draw::housing (g, content.getLocalBounds().toFloat());
    // Ceramic: the source cards and the arpeggiator's panel, drawn here so their shadows
    // fall on the housing.
    draw::raisedChildren (g, content);

    // Identity: ANDOR/OSP, One Shot Performer.
    {
        const auto bold = fonts::make (60.0f, fonts::Weight::displayBold).withHorizontalScale (1.07f);
        const auto light = fonts::make (60.0f, fonts::Weight::displayLight).withHorizontalScale (1.07f);
        juce::GlyphArrangement mark, rest;
        mark.addLineOfText (bold, identity.mark, layout::logo.x, layout::logo.y);
        const float w = mark.getBoundingBox (0, -1, true).getRight() - layout::logo.x + 1.0f;
        rest.addLineOfText (light, identity.product, layout::logo.x + w, layout::logo.y);
        g.setColour (colour::text);
        mark.draw (g);
        // The slash a shade lighter, as in the identity.
        rest.draw (g);
        // As written (not set in capitals), with a little air between the letters.
        g.setFont (fonts::make (15.0f, fonts::Weight::regular, 0.16f));
        g.setColour (colour::textSecondary);
        g.drawText (identity.tagline, layout::logoSubtitle.withHeight (17.0f), juce::Justification::centredLeft, false);
    }

    // The keyboard's frame: a quiet rim, the keys set into it. Ceramic: the keys sit in a
    // recess cut into the housing (rendered at the keybed's place: 2 px left and right, 3
    // above and 2 below the keys).
    const auto keys = layout::keyboard.translated (0.0f, arpShown ? layout::arpShift : 0.0f);
    if (auto* pieces = skin::active(); pieces != nullptr
        && pieces->draw (g, "keybed", juce::Rectangle<float> (keys.getX() - 2.0f, keys.getY() - 3.0f, keys.getWidth() + 4.0f, keys.getHeight() + 5.0f)))
    {
    }
    else
    {
        const auto bed = keys.expanded (2.0f);
        juce::Path frame;
        frame.addRoundedRectangle (bed, 6.0f);
        design::CachedShadow (juce::Colour (0x22302418), 3, { 0, 1 }).drawForPath (g, frame);
        g.setColour (juce::Colour (0xffd9d0c3));
        g.fillPath (frame);
        g.setColour (juce::Colour (0xffc4bbad));
        g.strokePath (frame, juce::PathStrokeType (1.0f));
    }

    // The macros and the envelope share one panel.
    {
        const auto r = layout::macroPanel;
        draw::raised (g, r, layout::panelRadius, colour::panelTop, colour::panelBottom);
        g.setColour (colour::divider);
        g.fillRect (juce::Rectangle<float> (layout::envelopeX - 10.0f, 695.0f, 1.0f, 190.0f));
        // No panel title: the six named knobs speak for themselves.
        // Under each macro a small light: lit when the macro's own settings are in use.
        // Ceramic: a rendered glass lens in the macro's colour (its glow stays drawn).
        static const char* const lensNames[] = { "life", "drive", "character", "motion", "space", "echo" };
        auto* pieces = skin::active();
        for (std::size_t i = 0; i < macros.size(); ++i)
        {
            const auto knob = macros[i].slider.getBounds().toFloat();
            const bool lit = macros[i].label != nullptr && macros[i].label->isCustomised();
            const auto identityColour = colour::macro (static_cast<int> (i));
            const juce::Point<float> centre { knob.getCentreX(), knob.getCentreY() + 65.0f };
            if (pieces != nullptr && i < std::size (lensNames))
            {
                if (lit)
                {
                    juce::ColourGradient halo (identityColour.withAlpha (0.3f), centre.x, centre.y, identityColour.withAlpha (0.0f), centre.x + 10.0f, centre.y, true);
                    g.setGradientFill (halo);
                    g.fillEllipse (juce::Rectangle<float> (20.0f, 20.0f).withCentre (centre));
                }
                if (pieces->draw (g, juce::String ("led-") + lensNames[i] + (lit ? "-lit" : "-unlit"), juce::Rectangle<float> (9.0f, 9.0f).withCentre (centre)))
                    continue;
            }
            draw::led (g, centre, 9.0f, lit ? identityColour.withMultipliedBrightness (1.2f) : identityColour.interpolatedWith (colour::panelBottom, 0.35f), lit ? 0.8f : 0.0f);
        }
    }

    // Ceramic: the keys on the housing (preset, ARP, Advanced), drawn here so their shadows
    // fall on it.
    skin::drawKeys (g, content);
}

float instrumentHeight (bool arpShown) noexcept
{
    return design::height + (arpShown ? design::layout::arpShift : 0.0f);
}

void fit (juce::Component& editor, juce::Component& content, float height)
{
    // Scaled uniformly to fit: if a host keeps the old window shape (it refused the resize),
    // the instrument still shows whole, with a margin, never cut off.
    const float w = design::width, h = height;
    const float k = std::min (static_cast<float> (editor.getWidth()) / w, static_cast<float> (editor.getHeight()) / h);
    const float x = 0.5f * (static_cast<float> (editor.getWidth()) - w * k);
    const float y = 0.5f * (static_cast<float> (editor.getHeight()) - h * k);
    content.setBounds (0, 0, static_cast<int> (w), static_cast<int> (h));
    content.setTransform (juce::AffineTransform::scale (k).translated (x, y));
}

void applyWindowShape (juce::AudioProcessorEditor& editor, float height, bool resize)
{
    // The window keeps the instrument's proportions: the same width range as always, the
    // height following the instrument (taller with the arpeggiator's editor shown).
    const float h = height;
    editor.setResizeLimits (869, juce::roundToInt (869.0f * h / design::width), 1810, juce::roundToInt (1810.0f * h / design::width));
    if (auto* windowShape = editor.getConstrainer())
        windowShape->setFixedAspectRatio (static_cast<double> (design::width / h));
    if (resize)
    {
        const int newHeight = juce::roundToInt (static_cast<float> (editor.getWidth()) * h / design::width);
        if (newHeight != editor.getHeight())
            editor.setSize (editor.getWidth(), newHeight);
        else
            editor.resized();
    }
}

} // namespace frame

} // namespace osp::plugin
