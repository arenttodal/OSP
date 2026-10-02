#include "ShapingPopups.h"

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
    const bool hover = opensPopup && isMouseOver();
    const auto font = fonts::label (twoWay ? 12.0f : 13.0f);
    g.setFont (font);
    g.setColour (hover || open ? juce::Colours::black : palette::text);

    auto r = getLocalBounds().toFloat();
    if (twoWay)
    {
        // ORIGINAL <-> REIMAGINED with a drawn arrow (the typeface has no arrow glyph).
        const juce::String left = "ORIGINAL", right = "REIMAGINED";
        const float arrow = 16.0f;
        const float wl = juce::GlyphArrangement::getStringWidth (font, left);
        const float wr = juce::GlyphArrangement::getStringWidth (font, right);
        float x = r.getCentreX() - (wl + wr + arrow + 8.0f) * 0.5f;
        g.drawText (left, juce::Rectangle<float> (x, r.getY(), wl + 2.0f, r.getHeight()), juce::Justification::centredLeft, false);
        x += wl + 4.0f;
        const float y = r.getCentreY();
        juce::Path p;
        p.startNewSubPath (x + 1.0f, y);
        p.lineTo (x + arrow - 1.0f, y);
        p.startNewSubPath (x + 4.0f, y - 3.0f);
        p.lineTo (x + 1.0f, y);
        p.lineTo (x + 4.0f, y + 3.0f);
        p.startNewSubPath (x + arrow - 4.0f, y - 3.0f);
        p.lineTo (x + arrow - 1.0f, y);
        p.lineTo (x + arrow - 4.0f, y + 3.0f);
        g.strokePath (p, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        x += arrow + 4.0f;
        g.drawText (right, juce::Rectangle<float> (x, r.getY(), wr + 2.0f, r.getHeight()), juce::Justification::centredLeft, false);
        return;
    }

    const float w = juce::GlyphArrangement::getStringWidth (font, text);
    const auto textArea = juce::Rectangle<float> (r.getCentreX() - w * 0.5f, r.getY(), w + 2.0f, r.getHeight());
    g.drawText (text, textArea, juce::Justification::centredLeft, false);
    if (customised)
    {
        g.setColour (palette::accent);
        g.fillEllipse (textArea.getRight() + 3.0f, r.getCentreY() - 2.0f, 4.0f, 4.0f);
    }
    if (hover || open)
    {
        g.setColour (open ? palette::accent : palette::text.withAlpha (0.6f));
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
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const int count = std::max (1, items.size());
    const float w = r.getWidth() / static_cast<float> (count);
    g.setColour (palette::surface.withAlpha (0.45f));
    g.fillRoundedRectangle (r, 3.0f);

    g.setFont (fonts::label (count > 4 ? 9.5f : 10.0f));
    for (int i = 0; i < count; ++i)
    {
        auto cell = juce::Rectangle<float> (r.getX() + w * static_cast<float> (i), r.getY(), w, r.getHeight());
        if (i == index)
        {
            juce::Path active;
            active.addRoundedRectangle (cell.getX(), cell.getY(), cell.getWidth(), cell.getHeight(), 3.0f, 3.0f,
                                        i == 0, i == count - 1, i == 0, i == count - 1);
            g.setGradientFill (juce::ColourGradient (palette::accent.brighter (0.08f), 0.0f, cell.getY(),
                                                     palette::accent.darker (0.08f), 0.0f, cell.getBottom(), false));
            g.fillPath (active);
            g.setColour (juce::Colours::white);
        }
        else
        {
            if (i > 0 && i != index + 1)
            {
                g.setColour (palette::border.withAlpha (0.8f));
                g.drawLine (cell.getX(), cell.getY() + 3.0f, cell.getX(), cell.getBottom() - 3.0f, 1.0f);
            }
            g.setColour (palette::text.withAlpha (0.8f));
        }
        g.drawText (items[i], cell, juce::Justification::centred, false);
    }
    g.setColour (palette::border);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
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

void MiniKnob::resized()
{
    auto r = getLocalBounds();
    if (horizontal)
    {
        r.removeFromLeft (56);
        slider.setBounds (r.removeFromLeft (r.getHeight()).reduced (1));
        return;
    }
    r.removeFromTop (12);
    r.removeFromBottom (14);
    slider.setBounds (r.withSizeKeepingCentre (r.getHeight(), r.getHeight()));
}

void MiniKnob::paint (juce::Graphics& g)
{
    const auto value = formatter != nullptr ? formatter (slider.getValue()) : juce::String (slider.getValue(), 1);
    auto r = getLocalBounds();
    const auto captionColour = onDark ? palette::displayText : palette::textDim;
    const auto valueColour = onDark ? palette::housing : palette::text;
    g.setColour (captionColour);
    g.setFont (fonts::label (9.5f));
    if (horizontal)
    {
        g.drawText (caption, r.removeFromLeft (56), juce::Justification::centredLeft, false);
        r.removeFromLeft (r.getHeight() + 6);
        g.setColour (valueColour);
        g.setFont (fonts::make (13.0f, fonts::Weight::medium));
        g.drawText (value, r, juce::Justification::centredLeft, false);
        return;
    }
    g.drawText (caption, r.removeFromTop (12), juce::Justification::centred, false);
    g.setColour (valueColour);
    g.setFont (fonts::make (11.5f, fonts::Weight::medium));
    g.drawText (value, r.removeFromBottom (14), juce::Justification::centred, false);
}

//==============================================================================
MiniPanel::MiniPanel (juce::String t) : title (std::move (t))
{
    setTitle (title);
    setInterceptsMouseClicks (true, true);
    setWantsKeyboardFocus (false);
}

void MiniPanel::paint (juce::Graphics& g)
{
    const auto r = card().toFloat();
    juce::Path shape;
    shape.addRoundedRectangle (r, 8.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.35f), 14, { 0, 5 }).drawForPath (g, shape);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff5efe4), 0.0f, r.getY(), juce::Colour (0xffeae1d0), 0.0f, r.getBottom(), false));
    g.fillPath (shape);
    g.setColour (palette::border);
    g.strokePath (shape, juce::PathStrokeType (1.0f));
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.drawLine (r.getX() + 8.0f, r.getY() + 1.5f, r.getRight() - 8.0f, r.getY() + 1.5f, 1.0f);

    g.setColour (palette::text);
    g.setFont (fonts::label (11.0f));
    g.drawText (title, card().reduced (12, 8).removeFromTop (14), juce::Justification::centredLeft, false);
}

void MiniPanel::resized()
{
    layoutContent (card().reduced (12, 8).withTrimmedTop (18));
}

//==============================================================================
namespace
{
    constexpr int popupWidth = 236;
    constexpr int segmentHeight = 20;
    constexpr int knobRowHeight = 57;

    /** One macro's popup: a type/mode selector over one or two rows of mini knobs. */
    class ShapingPopup final : public MiniPanel
    {
    public:
        ShapingPopup (juce::String popupTitle, juce::AudioProcessorValueTreeState& s) : MiniPanel (std::move (popupTitle)), state (s) {}

        SegmentedControl& selector (const juce::String& id, juce::StringArray items)
        {
            segments = std::make_unique<SegmentedControl> (*state.getParameter (id), std::move (items));
            addAndMakeVisible (*segments);
            return *segments;
        }
        MiniKnob& knob (int row, const juce::String& id, const juce::String& caption, MiniKnob::Formatter f, bool horizontal = false)
        {
            knobs.push_back (std::make_unique<MiniKnob> (state, id, caption, std::move (f), horizontal));
            addAndMakeVisible (*knobs.back());
            if (static_cast<int> (rows.size()) <= row)
                rows.resize (static_cast<std::size_t> (row + 1));
            rows[static_cast<std::size_t> (row)].push_back (knobs.back().get());
            horizontalRow = horizontal;
            return *knobs.back();
        }

        juce::Point<int> cardSize() const override
        {
            const int rowHeight = horizontalRow ? 42 : knobRowHeight;
            return { popupWidth, 8 + 18 + segmentHeight + 8 + static_cast<int> (rows.size()) * rowHeight + 6 };
        }

    private:
        void layoutContent (juce::Rectangle<int> area) override
        {
            if (segments != nullptr)
                segments->setBounds (area.removeFromTop (segmentHeight));
            area.removeFromTop (8);
            int columns = 1;
            for (const auto& row : rows)
                columns = std::max (columns, static_cast<int> (row.size()));
            const int cell = area.getWidth() / columns;
            for (const auto& row : rows)
            {
                auto line = area.removeFromTop (horizontalRow ? 42 : knobRowHeight);
                if (horizontalRow)
                {
                    row.front()->setBounds (line.reduced (0, 1));
                    continue;
                }
                // Shorter rows are centred under the longer one.
                line = line.withSizeKeepingCentre (cell * static_cast<int> (row.size()), line.getHeight());
                for (auto* k : row)
                    k->setBounds (line.removeFromLeft (cell));
            }
        }

        juce::AudioProcessorValueTreeState& state;
        std::unique_ptr<SegmentedControl> segments;
        std::vector<std::unique_ptr<MiniKnob>> knobs;
        std::vector<std::vector<MiniKnob*>> rows;
        bool horizontalRow = false;
    };

    MiniKnob::Formatter percentOf() { return [] (double v) { return format::percent (v); }; }

    /** MOVEMENT's three generic settings mean different things per mode (spec §32-38). */
    void relabelMovement (int mode, MiniKnob& a, MiniKnob& b, MiniKnob& c)
    {
        switch (static_cast<MovementMode> (mode))
        {
            case MovementMode::drift:
                a.setCaption ("SPEED");
                a.setFormatter ([] (double v) { return format::hertz (shaping::driftSpeedHz (v * 0.01)); });
                b.setCaption ("PITCH");
                b.setFormatter ([] (double v) { return juce::String (shaping::driftPitchCents (v * 0.01), 1) + " c"; });
                c.setCaption ("TONE");
                c.setFormatter (percentOf());
                break;
            case MovementMode::tape:
                a.setCaption ("WOW");
                a.setFormatter (percentOf());
                b.setCaption ("FLUTTER");
                b.setFormatter (percentOf());
                c.setCaption ("WEAR");
                c.setFormatter (percentOf());
                break;
            case MovementMode::chorus:
                a.setCaption ("RATE");
                a.setFormatter ([] (double v) { return format::hertz (shaping::chorusRateHz (v * 0.01)); });
                b.setCaption ("WIDTH");
                b.setFormatter ([] (double v) { return format::milliseconds (shaping::chorusWidthMs (v * 0.01)); });
                c.setCaption ("STEREO");
                c.setFormatter (percentOf());
                break;
            case MovementMode::pulse:
                a.setCaption ("RATE");
                a.setFormatter ([] (double v) { return format::hertz (shaping::pulseRateHz (v * 0.01)); });
                b.setCaption ("SHAPE");
                b.setFormatter (percentOf());
                c.setCaption ("STEREO");
                c.setFormatter (percentOf());
                break;
        }
    }

    juce::String parameterText (juce::RangedAudioParameter* p)
    {
        if (p == nullptr)
            return {};
        const auto label = p->getLabel();
        return p->getCurrentValueAsText() + (label.isNotEmpty() ? " " + label : juce::String());
    }

    /** Advanced: what a musician rarely needs, out of the way (spec §13). */
    class AdvancedPopup final : public MiniPanel
    {
    public:
        explicit AdvancedPopup (OspAudioProcessor& p) : MiniPanel ("ADVANCED"), processor (p)
        {
            auto& state = processor.parameters;
            auto add = [this, &state] (const char* id, const char* caption, MiniKnob::Formatter f) {
                knobs.push_back (std::make_unique<MiniKnob> (state, id, caption, std::move (f)));
                addAndMakeVisible (*knobs.back());
            };
            auto* velocity = state.getParameter ("velocityRange");
            auto* fine = state.getParameter ("fineTune");
            auto* bend = state.getParameter ("bendRange");
            auto* gain = state.getParameter ("gain");
            add ("velocityRange", "VELOCITY", [velocity] (double) { return parameterText (velocity); });
            add ("fineTune", "FINE", [fine] (double) { return parameterText (fine); });
            add ("bendRange", "BEND", [bend] (double v) { juce::ignoreUnused (bend); return juce::String (juce::roundToInt (v)) + " st"; });
            add ("gain", "OUTPUT", [gain] (double) { return parameterText (gain); });

            pitch = std::make_unique<SegmentedControl> (*state.getParameter ("pitchCharacter"), juce::StringArray { "TAPE", "NATURAL" });
            sustain = std::make_unique<SegmentedControl> (*state.getParameter ("sustain"), juce::StringArray { "RECORDING", "ENDLESS" });
            pitch->setTooltip ("Pitch character: tape-like resampling or natural (formants kept)");
            sustain->setTooltip ("Sustain: the recording's own length, or endless continuation");
            addAndMakeVisible (*pitch);
            addAndMakeVisible (*sustain);

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

        juce::Point<int> cardSize() const override { return { 332, 8 + 18 + knobRowHeight + 8 + 12 + segmentHeight + 8 + 24 + 8 }; }

        void paint (juce::Graphics& g) override
        {
            MiniPanel::paint (g);
            g.setColour (palette::textDim);
            g.setFont (fonts::label (9.5f));
            g.drawText ("PITCH", pitchCaption, juce::Justification::centredLeft, false);
            g.drawText ("SUSTAIN", sustainCaption, juce::Justification::centredLeft, false);
            if (auto* seed = processor.parameters.getParameter ("seed"))
                g.drawText ("SEED " + seed->getCurrentValueAsText(), seedCaption, juce::Justification::centredRight, false);
        }

    private:
        void layoutContent (juce::Rectangle<int> area) override
        {
            auto row = area.removeFromTop (knobRowHeight);
            const int cell = row.getWidth() / static_cast<int> (knobs.size());
            for (auto& k : knobs)
                k->setBounds (row.removeFromLeft (cell));
            area.removeFromTop (8);
            auto captions = area.removeFromTop (12);
            auto choices = area.removeFromTop (segmentHeight);
            const int half = (choices.getWidth() - 10) / 2;
            pitchCaption = captions.removeFromLeft (half);
            pitch->setBounds (choices.removeFromLeft (half));
            choices.removeFromLeft (10);
            captions.removeFromLeft (10);
            sustainCaption = captions;
            sustain->setBounds (choices);
            area.removeFromTop (8);
            auto last = area.removeFromTop (24);
            mpe.setBounds (last.removeFromLeft (80));
            reseed.setBounds (last.removeFromRight (90));
            last.removeFromRight (8);
            seedCaption = last;
        }

        OspAudioProcessor& processor;
        std::vector<std::unique_ptr<MiniKnob>> knobs;
        std::unique_ptr<SegmentedControl> pitch, sustain;
        juce::ToggleButton mpe { "MPE" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mpeAttachment;
        juce::TextButton reseed { "Reseed" };
        juce::Rectangle<int> pitchCaption, sustainCaption, seedCaption;
    };
}

const juce::StringArray& popupParameterIds (MacroPopup macro)
{
    static const juce::StringArray life { "life.mode", "life.pitch", "life.tone", "life.attack" };
    static const juce::StringArray dynamics { "dynamics.curve", "attack", "release", "dynamics.tone" };
    static const juce::StringArray character { "character.type", "character.min", "character.max", "character.resonance",
                                               "character.drive", "character.envAmount", "character.envAttack", "character.envDecay" };
    static const juce::StringArray movement { "movement.mode", "movement.paramA", "movement.paramB", "movement.paramC" };
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

std::unique_ptr<MiniPanel> createMacroPopup (MacroPopup macro, OspAudioProcessor& processor)
{
    auto& state = processor.parameters;
    auto ms = [] (double v) { return format::milliseconds (v); };
    switch (macro)
    {
        case MacroPopup::life:
        {
            auto popup = std::make_unique<ShapingPopup> ("LIFE MODE", state);
            popup->selector ("life.mode", { "NATURAL", "LOOSE", "FRAY" });
            popup->knob (0, "life.pitch", "PITCH", [] (double v) { return juce::String (v, 1) + " c"; });
            popup->knob (0, "life.tone", "TONE", percentOf());
            popup->knob (0, "life.attack", "ATTACK", percentOf());
            return popup;
        }
        case MacroPopup::dynamics:
        {
            auto popup = std::make_unique<ShapingPopup> ("VELOCITY CURVE", state);
            popup->selector ("dynamics.curve", { "SOFT", "LINEAR", "HARD" });
            popup->knob (0, "attack", "ATTACK", ms);
            popup->knob (0, "release", "RELEASE", ms);
            popup->knob (0, "dynamics.tone", "TONE", percentOf());
            return popup;
        }
        case MacroPopup::character:
        {
            auto popup = std::make_unique<ShapingPopup> ("FILTER TYPE", state);
            popup->selector ("character.type", { "LP24", "LP12", "HP12", "BP12", "TILT" });
            popup->knob (0, "character.min", "MIN", [] (double v) { return format::hertz (v); });
            popup->knob (0, "character.max", "MAX", [] (double v) { return format::hertz (v); });
            popup->knob (0, "character.resonance", "RES", percentOf());
            popup->knob (0, "character.drive", "DRIVE", percentOf());
            popup->knob (1, "character.envAmount", "ENV", [] (double v) { return format::bipolar (v); });
            popup->knob (1, "character.envAttack", "ATTACK", ms);
            popup->knob (1, "character.envDecay", "DECAY", ms);
            return popup;
        }
        case MacroPopup::movement:
        {
            auto popup = std::make_unique<ShapingPopup> ("MOVEMENT MODE", state);
            auto& modes = popup->selector ("movement.mode", { "DRIFT", "TAPE", "CHORUS", "PULSE" });
            auto& a = popup->knob (0, "movement.paramA", "A", percentOf());
            auto& b = popup->knob (0, "movement.paramB", "B", percentOf());
            auto& c = popup->knob (0, "movement.paramC", "C", percentOf());
            modes.onChange = [&a, &b, &c] (int mode) { relabelMovement (mode, a, b, c); };
            relabelMovement (modes.selected(), a, b, c);
            return popup;
        }
        case MacroPopup::space:
            break;
    }

    auto popup = std::make_unique<ShapingPopup> ("SPACE TYPE", state);
    auto& types = popup->selector ("space.type", { "ROOM", "CHAMBER", "PLATE", "SPRING" });
    auto* typeParam = state.getParameter ("space.type");
    // The readout shows the decay the chosen type actually uses (each type has its own range).
    auto& decay = popup->knob (0, "space.decay", "DECAY", [typeParam] (double v) {
        double lo = 0.2, hi = 8.0;
        if (typeParam != nullptr)
            shaping::decayRange (static_cast<SpaceType> (juce::roundToInt (typeParam->convertFrom0to1 (typeParam->getValue()))), lo, hi);
        return juce::String (std::clamp (v, lo, hi), 1) + " s";
    }, true);
    types.onChange = [&decay] (int) { decay.repaint(); };
    return popup;
}

std::unique_ptr<MiniPanel> createAdvancedPopup (OspAudioProcessor& processor)
{
    return std::make_unique<AdvancedPopup> (processor);
}

} // namespace osp::plugin
