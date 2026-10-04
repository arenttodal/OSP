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

    /** A stepped choice shown as its value (PATTERN, RATE): click for the list, arrows or the
        mouse wheel to step. Bound to a choice parameter. */
    class ValueSelector final : public juce::Component
    {
    public:
        ValueSelector (juce::RangedAudioParameter& p, juce::String captionText)
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

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds();
            g.setColour (palette::textDim);
            g.setFont (fonts::label (9.5f));
            g.drawText (caption, r.removeFromTop (12), juce::Justification::centred, false);
            const auto box = r.reduced (2, 1).toFloat();
            g.setColour (palette::surface.withAlpha (hasKeyboardFocus (false) || isMouseOver() ? 0.75f : 0.45f));
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (palette::border);
            g.drawRoundedRectangle (box, 3.0f, 1.0f);
            g.setColour (palette::text);
            g.setFont (fonts::make (11.0f, fonts::Weight::semibold, 0.04f));
            g.drawText (parameter.getAllValueStrings()[index], box, juce::Justification::centred, false);
        }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override { repaint(); }
        void mouseUp (const juce::MouseEvent& e) override
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
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
        {
            if (std::abs (wheel.deltaY) > 0.0f)
                step (wheel.deltaY > 0.0f ? -1 : 1);
        }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key.isKeyCode (juce::KeyPress::rightKey) || key.isKeyCode (juce::KeyPress::downKey))
                return step (1), true;
            if (key.isKeyCode (juce::KeyPress::leftKey) || key.isKeyCode (juce::KeyPress::upKey))
                return step (-1), true;
            return false;
        }

    private:
        void step (int delta) { choose (juce::jlimit (0, parameter.getAllValueStrings().size() - 1, index + delta)); }
        void choose (int i)
        {
            if (i != index)
                attachment.setValueAsCompleteGesture (static_cast<float> (i));
        }
        juce::RangedAudioParameter& parameter;
        juce::String caption;
        int index = 0;
        juce::ParameterAttachment attachment;
    };

    /** SHAPER's pattern at a glance: the curve the sound follows (with the current SMOOTH),
        sixteen step cells, and a thin playhead read from the DSP's own phase. */
    class PatternStrip final : public juce::Component
    {
    public:
        PatternStrip (OspAudioProcessor& p) : processor (p)
        {
            pattern = p.parameters.getRawParameterValue ("movement.shaper.pattern");
            smooth = p.parameters.getRawParameterValue ("movement.shaper.smooth");
        }
        /** From the popup's timer: repaints when the playhead or the pattern moved. */
        void refresh()
        {
            const float phase = processor.shaperPhase();
            const int p = juce::roundToInt (pattern->load());
            const float s = smooth->load();
            if (std::abs (phase - shownPhase) > 1.0e-4f || p != shownPattern || std::abs (s - shownSmooth) > 1.0e-3f)
            {
                shownPhase = phase;
                shownPattern = p;
                shownSmooth = s;
                repaint();
            }
        }
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (palette::display);
            g.fillRoundedRectangle (r, 4.0f);
            const auto plot = r.reduced (4.0f, 4.0f);
            const float cell = plot.getWidth() / RhythmicShaper::steps;
            const int current = shownPhase >= 0.0f ? std::min (RhythmicShaper::steps - 1, static_cast<int> (shownPhase * RhythmicShaper::steps)) : -1;
            for (int i = 0; i < RhythmicShaper::steps; ++i)
            {
                const auto c = juce::Rectangle<float> (plot.getX() + i * cell, plot.getY(), cell, plot.getHeight());
                if (i == current)
                {
                    g.setColour (palette::housing.withAlpha (0.10f));
                    g.fillRect (c);
                }
                if (i > 0)
                {
                    g.setColour (palette::displayLine.withAlpha (i % 4 == 0 ? 1.0f : 0.5f));
                    g.drawVerticalLine (juce::roundToInt (c.getX()), plot.getY(), plot.getBottom());
                }
            }
            // The curve itself (what the sound follows), filled.
            juce::Path curve;
            const int points = 160;
            curve.startNewSubPath (plot.getX(), plot.getBottom());
            for (int i = 0; i <= points; ++i)
            {
                const double x = static_cast<double> (i) / points;
                const float v = RhythmicShaper::evaluate (shownPattern, std::min (x, 0.99999), 0.01 * shownSmooth);
                curve.lineTo (plot.getX() + static_cast<float> (x) * plot.getWidth(), plot.getBottom() - v * plot.getHeight());
            }
            curve.lineTo (plot.getRight(), plot.getBottom());
            curve.closeSubPath();
            g.setColour (palette::wave.withAlpha (0.75f));
            g.fillPath (curve);
            if (shownPhase >= 0.0f)
            {
                const float x = plot.getX() + shownPhase * plot.getWidth();
                g.setColour (palette::accent);
                g.fillRect (juce::Rectangle<float> (x - 0.75f, r.getY() + 1.0f, 1.5f, r.getHeight() - 2.0f));
            }
        }

    private:
        OspAudioProcessor& processor;
        std::atomic<float>* pattern = nullptr;
        std::atomic<float>* smooth = nullptr;
        float shownPhase = -1.0f, shownSmooth = 30.0f;
        int shownPattern = 3;
    };

    /** MOVEMENT (v2): the mode, then that mode's own settings - three knobs, or for SHAPER the
        pattern strip, PATTERN, RATE, TARGET and SMOOTH. Switching modes keeps every mode's values. */
    class MovementPopup final : public MiniPanel, private juce::Timer
    {
    public:
        explicit MovementPopup (OspAudioProcessor& p) : MiniPanel ("MOVEMENT MODE"), processor (p)
        {
            modes = std::make_unique<SegmentedControl> (*p.parameters.getParameter ("movement.mode"),
                                                         juce::StringArray { "DRIFT", "TAPE", "CHORUS", "PULSE", "SHAPER" });
            modes->onChange = [this] (int mode) {
                build (mode);
                if (onSizeChanged != nullptr)
                    onSizeChanged();
            };
            addAndMakeVisible (*modes);
            build (modes->selected());
            startTimerHz (30);
        }

        juce::Point<int> cardSize() const override
        {
            if (shaperMode)
                return { 236, 8 + 18 + 20 + 8 + 34 + 6 + 32 + 4 + 32 + 4 + 34 + 4 };
            return { 236, 8 + 18 + 20 + 8 + 57 + 6 };
        }

    private:
        void timerCallback() override
        {
            if (strip != nullptr)
                strip->refresh();
        }

        void build (int mode)
        {
            knobs.clear();
            strip.reset();
            patternSelector.reset();
            rateSelector.reset();
            target.reset();
            shaperMode = mode == static_cast<int> (MovementMode::shaper);
            auto& state = processor.parameters;
            auto knob = [&] (const char* id, const char* caption, MiniKnob::Formatter f, bool horizontal = false) {
                knobs.push_back (std::make_unique<MiniKnob> (state, id, caption, std::move (f), horizontal));
                addAndMakeVisible (*knobs.back());
            };
            const auto percent = [] (double v) { return format::percent (v); };
            switch (static_cast<MovementMode> (mode))
            {
                case MovementMode::drift:
                    knob ("movement.drift.speed", "SPEED", [] (double v) { return format::hertz (shaping::driftSpeedHz (v * 0.01)); });
                    knob ("movement.drift.pitch", "PITCH", [] (double v) { return juce::String (shaping::driftPitchCents (v * 0.01), 1) + " c"; });
                    knob ("movement.drift.tone", "TONE", percent);
                    break;
                case MovementMode::tape:
                    knob ("movement.tape.wow", "WOW", [] (double v) { return format::hertz (shaping::tapeWowHz (v * 0.01)); });
                    knob ("movement.tape.flutter", "FLUTTER", [] (double v) { return format::hertz (shaping::tapeFlutterHz (v * 0.01)); });
                    knob ("movement.tape.wear", "WEAR", percent);
                    break;
                case MovementMode::chorus:
                    knob ("movement.chorus.rate", "RATE", [] (double v) { return format::hertz (shaping::chorusRateHz (v * 0.01)); });
                    knob ("movement.chorus.width", "WIDTH", [] (double v) { return format::milliseconds (shaping::chorusWidthMs (v * 0.01)); });
                    knob ("movement.chorus.stereo", "STEREO", percent);
                    break;
                case MovementMode::pulse:
                    knob ("movement.pulse.rate", "RATE", [] (double v) { return format::hertz (shaping::pulseRateHz (v * 0.01)); });
                    knob ("movement.pulse.shape", "SHAPE", percent);
                    knob ("movement.pulse.stereo", "STEREO", percent);
                    break;
                case MovementMode::shaper:
                    strip = std::make_unique<PatternStrip> (processor);
                    addAndMakeVisible (*strip);
                    patternSelector = std::make_unique<ValueSelector> (*state.getParameter ("movement.shaper.pattern"), "PATTERN");
                    rateSelector = std::make_unique<ValueSelector> (*state.getParameter ("movement.shaper.rate"), "RATE");
                    addAndMakeVisible (*patternSelector);
                    addAndMakeVisible (*rateSelector);
                    target = std::make_unique<SegmentedControl> (*state.getParameter ("movement.shaper.target"), juce::StringArray { "VOL", "FILTER", "BOTH" });
                    target->setTooltip ("What the pattern shapes: the volume, a low-pass filter, or both");
                    addAndMakeVisible (*target);
                    knob ("movement.shaper.smooth", "SMOOTH", percent, true);
                    strip->refresh();
                    break;
            }
            resized();
        }

        void layoutContent (juce::Rectangle<int> area) override
        {
            modes->setBounds (area.removeFromTop (20));
            area.removeFromTop (8);
            if (! shaperMode)
            {
                auto row = area.removeFromTop (57);
                const int cell = row.getWidth() / std::max (1, static_cast<int> (knobs.size()));
                for (auto& k : knobs)
                    k->setBounds (row.removeFromLeft (cell));
                return;
            }
            if (strip != nullptr)
                strip->setBounds (area.removeFromTop (34));
            area.removeFromTop (6);
            auto row = area.removeFromTop (32);
            const int half = row.getWidth() / 2;
            if (patternSelector != nullptr)
                patternSelector->setBounds (row.removeFromLeft (half).reduced (2, 0));
            if (rateSelector != nullptr)
                rateSelector->setBounds (row.reduced (2, 0));
            area.removeFromTop (4);
            if (target != nullptr)
                target->setBounds (area.removeFromTop (32).withTrimmedTop (12).reduced (2, 0));
            area.removeFromTop (4);
            if (! knobs.empty())
                knobs.front()->setBounds (area.removeFromTop (34).withSizeKeepingCentre (150, 34));
        }

        void paint (juce::Graphics& g) override
        {
            MiniPanel::paint (g);
            if (shaperMode && target != nullptr)
            {
                g.setColour (palette::textDim);
                g.setFont (fonts::label (9.5f));
                g.drawText ("TARGET", target->getBounds().withY (target->getY() - 12).withHeight (12), juce::Justification::centred, false);
            }
        }

        OspAudioProcessor& processor;
        std::unique_ptr<SegmentedControl> modes, target;
        std::vector<std::unique_ptr<MiniKnob>> knobs;
        std::unique_ptr<PatternStrip> strip;
        std::unique_ptr<ValueSelector> patternSelector, rateSelector;
        bool shaperMode = false;
    };

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
            return std::make_unique<MovementPopup> (processor);
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
