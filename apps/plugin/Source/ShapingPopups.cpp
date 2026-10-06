#include "ShapingPopups.h"

#include "Design.h"

#include "EngineCard.h"

#include "engine/Shaping.h"

namespace osp::plugin
{

//==============================================================================
namespace format
{
    juce::String percent (double v) { return juce::String (juce::roundToInt (v)); }
    juce::String bipolar (double v)
    {
        const int i = juce::roundToInt (v);
        return (i > 0 ? "+" : "") + juce::String (i);
    }
    juce::String milliseconds (double ms)
    {
        if (ms < 10.0)
            return juce::String (ms, 1) + " ms";
        if (ms < 1000.0)
            return juce::String (juce::roundToInt (ms)) + " ms";
        return juce::String (ms / 1000.0, ms < 10000.0 ? 2 : 1) + " s";
    }
    juce::String hertz (double hz)
    {
        if (hz < 1.0)
            return juce::String (hz, 2) + " Hz";
        if (hz < 10.0)
            return juce::String (hz, 1) + " Hz";
        if (hz < 1000.0)
            return juce::String (juce::roundToInt (hz)) + " Hz";
        return juce::String (hz / 1000.0, hz < 10000.0 ? 2 : 1) + " kHz";
    }
}

//==============================================================================
MacroLabel::MacroLabel (juce::String t, bool popup, bool arrow)
    : text (std::move (t)), opensPopup (popup), twoWay (arrow)
{
    setMouseCursor (opensPopup ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    setTitle (text);
}

void MacroLabel::setCustomised (bool c)
{
    if (customised != c)
    {
        customised = c;
        repaint();
    }
}

void MacroLabel::setOpen (bool o)
{
    if (open != o)
    {
        open = o;
        repaint();
    }
}

void MacroLabel::mouseUp (const juce::MouseEvent& e)
{
    if (opensPopup && onClick != nullptr && getLocalBounds().contains (e.getPosition()))
        onClick();
}

void MacroLabel::paint (juce::Graphics& g)
{
    // Reference: a bold, dark name (about 16 px at the canonical size); its settings in use
    // are an accent dot after it.
    const bool hover = opensPopup && isMouseOver();
    auto r = getLocalBounds().toFloat();
    const auto font = fonts::make (0.75f * r.getHeight(), fonts::Weight::bold, 0.04f);
    g.setFont (font);
    g.setColour (hover || open ? juce::Colours::black : design::colour::text);
    const float w = juce::GlyphArrangement::getStringWidth (font, text);
    const auto textArea = juce::Rectangle<float> (r.getCentreX() - w * 0.5f, r.getY(), w + 2.0f, r.getHeight());
    g.drawText (text, textArea, juce::Justification::centredLeft, false);
    if (customised)
        design::draw::led (g, { textArea.getRight() + 0.42f * r.getHeight(), r.getCentreY() }, 0.3f * r.getHeight(), juce::Colour (0xffff8a3c), 0.6f);
    if (hover || open)
    {
        g.setColour (open ? palette::accent : palette::text.withAlpha (0.5f));
        g.fillRect (textArea.getX(), r.getCentreY() + font.getHeight() * 0.5f + 1.0f, w, 1.2f);
    }
}

//==============================================================================
SegmentedControl::SegmentedControl (juce::RangedAudioParameter& parameter, juce::StringArray labels)
    : items (std::move (labels)),
      attachment (parameter, [this] (float value) {
          index = juce::jlimit (0, items.size() - 1, juce::roundToInt (value));
          repaint();
          if (onChange != nullptr)
              onChange (index);
      })
{
    attachment.sendInitialUpdate();
    setTitle (parameter.getName (64));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SegmentedControl::mouseDown (const juce::MouseEvent& e)
{
    const int count = std::max (1, items.size());
    const int clicked = juce::jlimit (0, count - 1, e.x * count / std::max (1, getWidth()));
    if (clicked != index)
        attachment.setValueAsCompleteGesture (static_cast<float> (clicked));
}

void SegmentedControl::paint (juce::Graphics& g)
{
    using namespace palette;
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const int count = std::max (1, items.size());
    const float w = r.getWidth() / static_cast<float> (count);
    OspLookAndFeel::drawCard (g, r, 5.0f, false, false);
    g.setFont (fonts::make (count > 4 ? 10.0f : 10.5f, fonts::Weight::medium, 0.06f));
    for (int i = 0; i < count; ++i)
    {
        auto cell = juce::Rectangle<float> (r.getX() + w * static_cast<float> (i), r.getY(), w, r.getHeight());
        if (i == index)
        {
            // Chosen: set into the panel, the accent in its text and rim.
            const auto inset = cell.reduced (2.0f);
            g.setColour (recessed.interpolatedWith (accent, 0.1f));
            g.fillRoundedRectangle (inset, 4.0f);
            g.setColour (accent.withAlpha (0.55f));
            g.drawRoundedRectangle (inset, 4.0f, 1.0f);
            g.setColour (accent.darker (0.2f));
        }
        else
        {
            if (i > 0 && i != index + 1)
            {
                g.setColour (hairline);
                g.drawLine (cell.getX(), cell.getY() + 5.0f, cell.getX(), cell.getBottom() - 5.0f, 1.0f);
            }
            g.setColour (text.withAlpha (0.72f));
        }
        g.drawText (items[i], cell, juce::Justification::centred, false);
    }
}

//==============================================================================
MiniKnob::MiniKnob (juce::AudioProcessorValueTreeState& state, const juce::String& id, juce::String c, Formatter f, bool h)
    : caption (std::move (c)), formatter (std::move (f)), horizontal (h)
{
    slider.getProperties().set ("mini", true);
    slider.setRotaryParameters (OspLookAndFeel::rotaryStart, OspLookAndFeel::rotaryEnd, true);
    slider.setTitle (caption);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);
    if (auto* p = state.getParameter (id))
    {
        // Double-click resets this setting (and only this one).
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        slider.setTooltip (p->getName (64));
    }
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void MiniKnob::setCaption (juce::String c)
{
    caption = std::move (c);
    slider.setTitle (caption);
    repaint();
}

void MiniKnob::setFormatter (Formatter f)
{
    formatter = std::move (f);
    repaint();
}

// Proportions from the reference's envelope knobs (100 x 87): caption on top, the knob at
// 48 % of the height, its value underneath. Horizontal: caption, knob, value in a row.
void MiniKnob::resized()
{
    const auto r = getLocalBounds().toFloat();
    const float h = r.getHeight();
    if (horizontal)
    {
        const float side = h;
        slider.setBounds (juce::Rectangle<float> (side, side).withCentre ({ r.getX() + 0.5f * r.getWidth() - 0.05f * r.getWidth(), r.getCentreY() }).getSmallestIntegerContainer());
        return;
    }
    const float side = (compact ? 0.5f : 0.55f) * h;
    slider.setBounds (juce::Rectangle<float> (side, side).withCentre ({ r.getCentreX(), r.getY() + 0.48f * h }).getSmallestIntegerContainer());
}

void MiniKnob::paint (juce::Graphics& g)
{
    const auto value = formatter != nullptr ? formatter (slider.getValue()) : juce::String (slider.getValue(), 1);
    const auto r = getLocalBounds().toFloat();
    const float h = r.getHeight();
    const auto captionColour = onDark ? design::colour::wellText.brighter (0.3f) : design::colour::text;
    const auto valueColour = onDark ? juce::Colour (0xfff2eee6) : design::colour::text;
    if (horizontal)
    {
        g.setColour (onDark ? captionColour : design::colour::textSecondary.darker (0.2f));
        g.setFont (fonts::make (0.42f * h, fonts::Weight::semibold, 0.08f));
        g.drawText (caption, r.withRight (static_cast<float> (slider.getX()) - 0.25f * h), juce::Justification::centredRight, false);
        g.setColour (valueColour);
        g.setFont (fonts::make (0.5f * h, fonts::Weight::semibold));
        g.drawText (value, r.withLeft (static_cast<float> (slider.getRight()) + 0.25f * h), juce::Justification::centredLeft, false);
        return;
    }
    g.setColour (captionColour);
    g.setFont (fonts::make ((compact ? 0.14f : 0.155f) * h, fonts::Weight::bold, 0.04f));
    g.drawText (caption, juce::Rectangle<float> (r.getX(), r.getY(), r.getWidth(), 0.2f * h), juce::Justification::centred, false);
    g.setColour (valueColour);
    g.setFont (fonts::make ((compact ? 0.17f : 0.19f) * h, fonts::Weight::bold));
    g.drawText (value, juce::Rectangle<float> (r.getX(), r.getY() + 0.78f * h, r.getWidth(), 0.22f * h), juce::Justification::centred, false);
}

//==============================================================================
ValueSelector::ValueSelector (juce::RangedAudioParameter& p, juce::String captionText)
    : parameter (p), caption (std::move (captionText)),
      attachment (p, [this] (float v) {
          index = juce::roundToInt (v);
          repaint();
      })
{
    attachment.sendInitialUpdate();
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle (caption);
}

void ValueSelector::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (palette::textDim);
    g.setFont (fonts::label (9.5f));
    g.drawText (caption, r.removeFromTop (12), juce::Justification::centred, false);
    const auto box = r.reduced (2, 1).toFloat();
    OspLookAndFeel::drawCard (g, box, 5.0f, false, hasKeyboardFocus (false) || isMouseOver());
    g.setColour (palette::text);
    g.setFont (fonts::make (12.0f, fonts::Weight::semibold, 0.04f));
    auto text = box.reduced (8.0f, 0.0f);
    icons::draw (g, icons::Kind::chevronDown, text.removeFromRight (10.0f), palette::text.withAlpha (0.6f), 1.3f);
    g.drawText (parameter.getAllValueStrings()[index], text, juce::Justification::centred, false);
}

void ValueSelector::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition()))
        return;
    juce::PopupMenu menu;
    const auto values = parameter.getAllValueStrings();
    for (int i = 0; i < values.size(); ++i)
    {
        // The choice arrives later, from the message loop: by then this selector may be
        // gone (popup closed, mode changed), so never call into it unchecked.
        juce::Component::SafePointer<ValueSelector> safe (this);
        menu.addItem (values[i], true, i == index, [safe, i] { if (safe != nullptr) safe->choose (i); });
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()).withDeletionCheck (*this));
}

void ValueSelector::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (std::abs (wheel.deltaY) > 0.0f)
        step (wheel.deltaY > 0.0f ? -1 : 1);
}

bool ValueSelector::keyPressed (const juce::KeyPress& key)
{
    if (key.isKeyCode (juce::KeyPress::rightKey) || key.isKeyCode (juce::KeyPress::downKey))
        return step (1), true;
    if (key.isKeyCode (juce::KeyPress::leftKey) || key.isKeyCode (juce::KeyPress::upKey))
        return step (-1), true;
    return false;
}

void ValueSelector::step (int delta)
{
    choose (juce::jlimit (0, parameter.getAllValueStrings().size() - 1, index + delta));
}

void ValueSelector::choose (int i)
{
    if (i != index)
        attachment.setValueAsCompleteGesture (static_cast<float> (i));
}

//==============================================================================
MiniPanel::MiniPanel (juce::String t, juce::String s) : title (std::move (t)), subtitle (std::move (s))
{
    setTitle (title);
    setInterceptsMouseClicks (true, true);
    setWantsKeyboardFocus (false);
}

juce::Rectangle<float> MiniPanel::closeButton() const
{
    const auto c = card().toFloat();
    return { c.getRight() - 34.0f, c.getY() + 10.0f, 22.0f, 22.0f };
}

void MiniPanel::mouseUp (const juce::MouseEvent& e)
{
    if (closeButton().contains (e.position) && onClose != nullptr)
        onClose();
}

void MiniPanel::paint (juce::Graphics& g)
{
    using namespace palette;
    const auto r = card().toFloat();
    juce::Path shape;
    shape.addRoundedRectangle (r, 10.0f);
    // Floats a little above the instrument: a soft, short shadow (no glass, no glow).
    juce::DropShadow (juce::Colour (0x261e1c18), 12, { 0, 4 }).drawForPath (g, shape);
    g.setColour (juce::Colour (0xfff3f1ea));
    g.fillPath (shape);
    g.setColour (hairline);
    g.strokePath (shape, juce::PathStrokeType (1.0f));

    auto header = card().reduced (16, 10).removeFromTop (headerHeight()).toFloat();
    g.setColour (text);
    g.setFont (fonts::make (15.0f, fonts::Weight::semibold, 0.08f));
    g.drawText (title, header.removeFromTop (20.0f), juce::Justification::centredLeft, false);
    if (subtitle.isNotEmpty())
    {
        g.setColour (textDim);
        g.setFont (fonts::make (9.5f, fonts::Weight::medium, 0.22f));
        g.drawText (subtitle, header.removeFromTop (14.0f), juce::Justification::centredLeft, false);
    }
    const auto x = closeButton();
    if (isMouseOver() && x.contains (getMouseXYRelative().toFloat()))
    {
        g.setColour (recessed);
        g.fillRoundedRectangle (x, 5.0f);
    }
    g.setColour (text.withAlpha (0.75f));
    const auto cross = x.reduced (6.5f);
    g.drawLine (cross.getX(), cross.getY(), cross.getRight(), cross.getBottom(), 1.4f);
    g.drawLine (cross.getRight(), cross.getY(), cross.getX(), cross.getBottom(), 1.4f);
}

void MiniPanel::resized()
{
    layoutContent (card().reduced (16, 10).withTrimmedTop (headerHeight() + 6));
}

//==============================================================================
namespace
{
    juce::String parameterText (juce::RangedAudioParameter* p)
    {
        if (p == nullptr)
            return {};
        const auto label = p->getLabel();
        return p->getCurrentValueAsText() + (label.isNotEmpty() ? " " + label : juce::String());
    }

    /** Advanced (spec §13): what a musician rarely needs, out of the way. */
    class AdvancedPopup final : public MiniPanel
    {
    public:
        explicit AdvancedPopup (OspAudioProcessor& p) : MiniPanel ("ADVANCED", "TUNING, BEND, VOICES, PITCH, MPE"), processor (p)
        {
            auto& state = processor.parameters;
            auto add = [this, &state] (const char* id, const char* caption, MiniKnob::Formatter f) {
                knobs.push_back (std::make_unique<MiniKnob> (state, id, caption, std::move (f)));
                addAndMakeVisible (*knobs.back());
            };
            // OUTPUT is the header's VOLUME, sustain each layer's LOOP, the velocity range
            // DYNAMICS' RANGE: Advanced keeps what a musician rarely needs.
            auto* fine = state.getParameter ("fineTune");
            add ("fineTune", "FINE", [fine] (double) { return parameterText (fine); });
            add ("bendRange", "BEND", [] (double v) { return juce::String (juce::roundToInt (v)) + " st"; });
            add ("glide", "GLIDE", [] (double ms) { return ms < 0.5 ? juce::String ("Off") : format::milliseconds (ms); });
            glide = knobs.back().get();

            // Mono: one note at a time, legato (no restart while a key is held), last key
            // wins, GLIDE slides between notes. For basses and leads.
            voices = std::make_unique<SegmentedControl> (*state.getParameter ("voiceMode"), juce::StringArray { "POLY", "MONO" });
            voices->setTooltip ("Mono: one note at a time; a key played while another is held slides to it (legato) over GLIDE");
            voices->onChange = [this] (int) { updateGlide(); };
            addAndMakeVisible (*voices);
            updateGlide();

            pitch = std::make_unique<SegmentedControl> (*state.getParameter ("pitchCharacter"), juce::StringArray { "TAPE", "NATURAL" });
            pitch->setTooltip ("Pitch character: tape-like resampling or natural (formants kept)");
            addAndMakeVisible (*pitch);

            mpe.setTooltip ("MPE controllers: per-note pitch bend (+/-48 st), pressure and slide");
            mpeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, "mpe", mpe);
            addAndMakeVisible (mpe);
            reseed.setTooltip ("New variation seed: repeated notes vary differently");
            reseed.onClick = [this] {
                if (auto* seed = processor.parameters.getParameter ("seed"))
                {
                    const auto range = processor.parameters.getParameterRange ("seed");
                    const float next = std::fmod (range.convertFrom0to1 (seed->getValue()), 9999.0f) + 1.0f;
                    seed->setValueNotifyingHost (range.convertTo0to1 (next));
                    repaint();
                }
            };
            addAndMakeVisible (reseed);
        }

        juce::Point<int> cardSize() const override { return { 300, 10 + headerHeight() + 6 + 58 + 8 + 12 + 24 + 8 + 12 + 24 + 10 + 26 + 12 }; }

        void paint (juce::Graphics& g) override
        {
            MiniPanel::paint (g);
            g.setColour (palette::textDim);
            g.setFont (fonts::label (9.5f));
            g.drawText ("VOICES", voicesCaption, juce::Justification::centredLeft, false);
            g.drawText ("PITCH CHARACTER", pitchCaption, juce::Justification::centredLeft, false);
            if (auto* seed = processor.parameters.getParameter ("seed"))
                g.drawText ("SEED " + seed->getCurrentValueAsText(), seedCaption, juce::Justification::centredRight, false);
        }

    private:
        void layoutContent (juce::Rectangle<int> area) override
        {
            auto row = area.removeFromTop (58);
            const int cell = row.getWidth() / static_cast<int> (knobs.size());
            for (auto& k : knobs)
                k->setBounds (row.removeFromLeft (cell));
            area.removeFromTop (8);
            voicesCaption = area.removeFromTop (12);
            voices->setBounds (area.removeFromTop (24));
            area.removeFromTop (8);
            pitchCaption = area.removeFromTop (12);
            pitch->setBounds (area.removeFromTop (24));
            area.removeFromTop (10);
            auto last = area.removeFromTop (26);
            mpe.setBounds (last.removeFromLeft (80));
            reseed.setBounds (last.removeFromRight (90));
            last.removeFromRight (8);
            seedCaption = last;
        }

        /** GLIDE only acts in Mono: dimmed (still adjustable) in Poly. */
        void updateGlide()
        {
            if (glide != nullptr && voices != nullptr)
                glide->setAlpha (voices->selected() == 1 ? 1.0f : 0.45f);
        }

        OspAudioProcessor& processor;
        std::vector<std::unique_ptr<MiniKnob>> knobs;
        MiniKnob* glide = nullptr;
        std::unique_ptr<SegmentedControl> voices, pitch;
        juce::ToggleButton mpe { "MPE" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mpeAttachment;
        juce::TextButton reseed { "Reseed" };
        juce::Rectangle<int> voicesCaption, pitchCaption, seedCaption;
    };
}

const juce::StringArray& popupParameterIds (MacroPopup macro)
{
    static const juce::StringArray life { "life.mode", "life.pitch", "life.tone", "life.attack" };
    static const juce::StringArray dynamics { "dynamics.curve", "velocityRange", "dynamics.tone" };
    static const juce::StringArray character { "character.type", "character.min", "character.max", "character.resonance",
                                               "character.drive", "character.envAmount", "character.envAttack", "character.envDecay" };
    static const juce::StringArray movement { "movement.mode", "movement.drift.speed", "movement.drift.pitch", "movement.drift.tone",
                                              "movement.tape.wow", "movement.tape.flutter", "movement.tape.wear",
                                              "movement.chorus.rate", "movement.chorus.width", "movement.chorus.stereo",
                                              "movement.pulse.rate", "movement.pulse.shape", "movement.pulse.stereo",
                                              "movement.shaper.pattern", "movement.shaper.rate", "movement.shaper.target",
                                              "movement.shaper.smooth" };
    static const juce::StringArray space { "space.type", "space.decay" };
    switch (macro)
    {
        case MacroPopup::life: return life;
        case MacroPopup::dynamics: return dynamics;
        case MacroPopup::character: return character;
        case MacroPopup::movement: return movement;
        case MacroPopup::space: break;
    }
    return space;
}

std::unique_ptr<MiniPanel> createAdvancedPopup (OspAudioProcessor& processor)
{
    return std::make_unique<AdvancedPopup> (processor);
}

} // namespace osp::plugin
