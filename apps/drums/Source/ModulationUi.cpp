#include "ModulationUi.h"

#include "DrumsLook.h"

#include <cmath>

namespace osp::drumsplugin
{

namespace
{
    namespace cc = ceramic::colour;

    juce::String secondsText (double s)
    {
        return s < 1.0 ? juce::String (s * 1000.0, s < 0.01 ? 1 : 0) + " ms" : juce::String (s, 2) + " s";
    }

    void styleCombo (juce::ComboBox& c)
    {
        c.setColour (juce::ComboBox::backgroundColourId, cc::cardTop);
        c.setColour (juce::ComboBox::textColourId, cc::text);
        c.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xffcfc4b4));
    }

    void styleKnob (juce::Slider& s, juce::Colour arc)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        s.getProperties().set ("arc", static_cast<juce::int64> (arc.getARGB()));
    }

    juce::Font labelFont() { return plugin::fonts::make (11.5f, look::Weight::medium, 0.08f); }

    const char* envNames[] = { "ATTACK", "DECAY", "SUSTAIN", "RELEASE" };
} // namespace

DepthSlider::DepthSlider()
{
    setSliderStyle (juce::Slider::LinearHorizontal);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRange (-1.0, 1.0, 0.0001);
    setDoubleClickReturnValue (true, 0.0);
}

void DepthSlider::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (6.0f, 0.0f);
    const float cy = b.getCentreY();
    const auto rail = juce::Rectangle<float> (b.getX(), cy - 1.6f, b.getWidth(), 3.2f);
    g.setColour (cc::faderRail.withAlpha (isEnabled() ? 0.9f : 0.4f));
    g.fillRoundedRectangle (rail, 1.6f);
    const float centre = b.getCentreX();
    const float x = b.getX() + b.getWidth() * static_cast<float> ((getValue() + 1.0) * 0.5);
    g.setColour (cc::textMicro.withAlpha (0.6f));
    g.fillRect (juce::Rectangle<float> (1.0f, 9.0f).withCentre ({ centre, cy }));   // 0
    if (isEnabled())
    {
        g.setColour (cc::accent);
        g.fillRoundedRectangle (juce::Rectangle<float> (std::min (x, centre), cy - 1.6f, std::abs (x - centre), 3.2f), 1.6f);
    }
    auto cap = juce::Rectangle<float> (10.0f, 16.0f).withCentre ({ x, cy });
    ceramic::contactShadow (g, cap, 3.0f, isMouseOverOrDragging() ? 1.3f : 1.0f);
    g.setColour (juce::Colour (0xfff4f0ea));
    g.fillRoundedRectangle (cap, 3.0f);
    g.setColour (isEnabled() ? cc::accent.withAlpha (0.8f) : cc::faderCap);
    g.drawRoundedRectangle (cap.reduced (0.5f), 3.0f, 1.0f);
}

PadModEditor::PadModEditor (DrumsProcessor& p) : proc (p)
{
    // ENV: a hit envelope (times in seconds, SUSTAIN 0..1).
    for (std::size_t i = 0; i < envKnob.size(); ++i)
    {
        auto& k = envKnob[i];
        styleKnob (k, cc::accent);
        if (i == 2)
        {
            k.setRange (0.0, 1.0, 0.001);
            k.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
        }
        else
        {
            k.setRange (i == 0 ? 0.0 : 0.005, i == 0 ? 2.0 : 5.0, 0.0001);
            k.setSkewFactorFromMidPoint (i == 0 ? 0.05 : 0.3);
            k.textFromValueFunction = [] (double v) { return secondsText (v); };
        }
        k.onDragStart = [this] { gesture = true; };
        k.onValueChange = [this, i] {
            const double v = envKnob[i].getValue();
            edit ([i, v] (drums::PadModulation& m) {
                if (i == 0) m.env.attackSeconds = v;
                else if (i == 1) m.env.decaySeconds = v;
                else if (i == 2) m.env.sustain = v;
                else m.env.releaseSeconds = v;
            }, gesture || ! envKnob[i].isMouseButtonDown());
            gesture = false;
        };
        addChildComponent (k);
    }

    // LFO
    styleKnob (rate, cc::accent);
    rate.setRange (0.05, 30.0, 0.001);
    rate.setSkewFactorFromMidPoint (3.0);
    rate.textFromValueFunction = [] (double v) { return juce::String (v, v < 10.0 ? 2 : 1) + " Hz"; };
    rate.onDragStart = [this] { gesture = true; };
    rate.onValueChange = [this] {
        const double v = rate.getValue();
        edit ([v] (drums::PadModulation& m) { m.lfo.rateHz = v; }, gesture || ! rate.isMouseButtonDown());
        gesture = false;
    };
    addChildComponent (rate);
    const char* shapes[] = { "Sine", "Triangle", "Ramp up", "Ramp down", "Pulse", "Random" };
    for (int i = 0; i < 6; ++i)
        shape.addItem (shapes[i], i + 1);
    styleCombo (shape);
    shape.onChange = [this] {
        const int v = shape.getSelectedItemIndex();
        if (v >= 0)
            edit ([v] (drums::PadModulation& m) { m.lfo.shape = static_cast<mod::LfoShape> (v); }, true);
    };
    addChildComponent (shape);
    for (int i = 0; i < mod::syncDivisionCount; ++i)
        division.addItem (mod::syncName (i), i + 1);
    styleCombo (division);
    division.onChange = [this] {
        const int v = division.getSelectedItemIndex();
        if (v >= 0)
            edit ([v] (drums::PadModulation& m) { m.lfo.division = v; }, true);
    };
    addChildComponent (division);
    sync.setClickingTogglesState (true);
    sync.setTooltip ("In time with the host's tempo");
    sync.onClick = [this] {
        const bool on = sync.getToggleState();
        edit ([on] (drums::PadModulation& m) { m.lfo.sync = on; }, true);
    };
    addChildComponent (sync);
    retrig.setClickingTogglesState (true);
    retrig.setTooltip ("On: every hit starts its own LFO from the beginning. Off: one LFO runs freely for the pad");
    retrig.onClick = [this] {
        const bool on = retrig.getToggleState();
        edit ([on] (drums::PadModulation& m) {
            m.lfo.mode = on ? mod::LfoMode::retrigger : mod::LfoMode::free;
            m.lfo.scope = on ? mod::Scope::poly : mod::Scope::global;
        }, true);
    };
    addChildComponent (retrig);

    // VELOCITY's curve
    for (std::size_t i = 0; i < velShape.size(); ++i)
    {
        look::makeSegment (velShape[i], 31);
        velShape[i].onClick = [this, i] {
            if (velShape[i].getToggleState())
                edit ([i] (drums::PadModulation& m) { m.velocityShape = i == 1 ? drums::VelocityShape::linear : (i == 0 ? drums::VelocityShape::soft : drums::VelocityShape::hard); }, true);
        };
        addChildComponent (velShape[i]);
    }

    // CTRL: which controller's routes are shown
    for (auto* b : { &wheel, &pressure })
    {
        look::makeSegment (*b, 32);
        addChildComponent (*b);
    }
    wheel.setTooltip ("The mod wheel (CC 1)");
    pressure.setTooltip ("Channel aftertouch");
    wheel.onClick = [this] { if (wheel.getToggleState()) setSource (drums::ModSource::wheel); };
    pressure.onClick = [this] { if (pressure.getToggleState()) setSource (drums::ModSource::pressure); };

    // Routes
    for (int s = 0; s < drums::modSlotsPerSource; ++s)
    {
        auto& box = target[static_cast<std::size_t> (s)];
        for (int t = 0; t < drums::modTargetCount; ++t)
            box.addItem (t == 0 ? juce::String ("Off") : juce::String (drums::modTargetName (static_cast<drums::ModTarget> (t))), t + 1);
        styleCombo (box);
        box.onChange = [this, s] {
            const int t = target[static_cast<std::size_t> (s)].getSelectedItemIndex();
            if (t < 0)
                return;
            const auto src = shown;
            edit ([src, s, t] (drums::PadModulation& m) {
                auto& slot = m.slot (src, s);
                const auto was = slot.target;
                slot.target = static_cast<drums::ModTarget> (t);
                // A new target starts at a gentle, audible depth rather than nothing.
                if (slot.target != drums::ModTarget::none && was == drums::ModTarget::none && ! (std::abs (slot.depth) > 0.0))
                    slot.depth = slot.target == drums::ModTarget::pitch ? 1.0 / 24.0 : 0.25;
            }, true);
        };
        addAndMakeVisible (box);

        auto& d = depth[static_cast<std::size_t> (s)];
        d.setTooltip ("How far this source moves the target (double-click: none)");
        d.onDragStart = [this] { gesture = true; };
        d.onValueChange = [this, s] {
            const double v = depth[static_cast<std::size_t> (s)].getValue();
            const auto src = shown;
            edit ([src, s, v] (drums::PadModulation& m) { m.slot (src, s).depth = v; }, gesture || ! depth[static_cast<std::size_t> (s)].isMouseButtonDown());
            gesture = false;
            repaint (rowArea (s));
        };
        addAndMakeVisible (d);
    }
    showFor (shown);
}

void PadModEditor::edit (const std::function<void (drums::PadModulation&)>& change, bool newTransaction)
{
    if (updating)
        return;
    auto next = current();
    change (next);
    proc.setPadModulation (proc.selectedPad, next, newTransaction);
    repaint();
}

void PadModEditor::setSource (drums::ModSource source)
{
    shown = source;
    showFor (source);
    refresh();
    resized();
    repaint();
}

void PadModEditor::showFor (drums::ModSource s)
{
    for (auto& k : envKnob)
        k.setVisible (s == drums::ModSource::env);
    for (auto* c : std::initializer_list<juce::Component*> { &rate, &shape, &sync, &retrig, &division })
        c->setVisible (s == drums::ModSource::lfo);
    for (auto& b : velShape)
        b.setVisible (s == drums::ModSource::velocity);
    const bool ctrl = s == drums::ModSource::wheel || s == drums::ModSource::pressure;
    wheel.setVisible (ctrl);
    pressure.setVisible (ctrl);
}

void PadModEditor::refresh()
{
    const juce::ScopedValueSetter<bool> busy (updating, true);
    const auto& m = current();
    envKnob[0].setValue (m.env.attackSeconds, juce::dontSendNotification);
    envKnob[1].setValue (m.env.decaySeconds, juce::dontSendNotification);
    envKnob[2].setValue (m.env.sustain, juce::dontSendNotification);
    envKnob[3].setValue (m.env.releaseSeconds, juce::dontSendNotification);
    rate.setValue (m.lfo.rateHz, juce::dontSendNotification);
    shape.setSelectedItemIndex (std::min (static_cast<int> (m.lfo.shape), 5), juce::dontSendNotification);
    division.setSelectedItemIndex (m.lfo.division, juce::dontSendNotification);
    sync.setToggleState (m.lfo.sync, juce::dontSendNotification);
    retrig.setToggleState (m.lfo.mode != mod::LfoMode::free, juce::dontSendNotification);
    rate.setEnabled (! m.lfo.sync);
    division.setEnabled (m.lfo.sync);
    const int vel = m.velocityShape == drums::VelocityShape::soft ? 0 : (m.velocityShape == drums::VelocityShape::linear ? 1 : 2);
    for (std::size_t i = 0; i < velShape.size(); ++i)
        velShape[i].setToggleState (static_cast<int> (i) == vel, juce::dontSendNotification);
    wheel.setToggleState (shown == drums::ModSource::wheel, juce::dontSendNotification);
    pressure.setToggleState (shown == drums::ModSource::pressure, juce::dontSendNotification);
    for (int s = 0; s < drums::modSlotsPerSource; ++s)
    {
        const auto& slot = m.slot (shown, s);
        target[static_cast<std::size_t> (s)].setSelectedItemIndex (static_cast<int> (slot.target), juce::dontSendNotification);
        auto& d = depth[static_cast<std::size_t> (s)];
        d.setValue (slot.depth, juce::dontSendNotification);
        d.setEnabled (slot.target != drums::ModTarget::none);
    }
    for (auto& k : envKnob)
        k.updateText();
    rate.updateText();
    repaint();
}

juce::Rectangle<int> PadModEditor::sourceArea() const { return getLocalBounds().withWidth (getWidth() * 47 / 100); }
juce::Rectangle<int> PadModEditor::routesArea() const { return getLocalBounds().withTrimmedLeft (sourceArea().getWidth() + 14); }

juce::Rectangle<int> PadModEditor::rowArea (int slot) const
{
    auto r = routesArea().withTrimmedTop (22);
    const int h = r.getHeight() / drums::modSlotsPerSource;
    return r.withY (r.getY() + slot * h).withHeight (h);
}

void PadModEditor::resized()
{
    auto src = sourceArea();
    auto display = src.removeFromTop (src.getHeight() * 40 / 100);
    juce::ignoreUnused (display);
    src.removeFromTop (4);
    // ENV: four knobs in a row, their names drawn above
    {
        auto row = src.withTrimmedTop (14);
        const int w = row.getWidth() / 4;
        for (auto& k : envKnob)
            k.setBounds (row.removeFromLeft (w).reduced (2, 0));
    }
    // LFO: RATE knob, then shape / division and the two switches
    {
        auto row = src.withTrimmedTop (14);
        rate.setBounds (row.removeFromLeft (row.getWidth() / 3).reduced (2, 0));
        row.removeFromLeft (6);
        auto top = row.removeFromTop (row.getHeight() / 2).reduced (0, 3);
        shape.setBounds (top.removeFromLeft (top.getWidth() / 2 - 3));
        top.removeFromLeft (6);
        division.setBounds (top);
        auto bottom = row.reduced (0, 3);
        sync.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2 - 3));
        bottom.removeFromLeft (6);
        retrig.setBounds (bottom);
    }
    // VELOCITY: the curve's three choices
    {
        auto row = src.withSizeKeepingCentre (src.getWidth(), 28);
        const int w = row.getWidth() / 3;
        for (auto& b : velShape)
            b.setBounds (row.removeFromLeft (w).reduced (2, 0));
    }
    // CTRL: which controller
    {
        auto row = src.withSizeKeepingCentre (src.getWidth(), 28);
        wheel.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2, 0));
        pressure.setBounds (row.reduced (2, 0));
    }
    for (int s = 0; s < drums::modSlotsPerSource; ++s)
    {
        auto r = rowArea (s).reduced (0, 4);
        target[static_cast<std::size_t> (s)].setBounds (r.removeFromLeft (96).withSizeKeepingCentre (96, 24));
        r.removeFromLeft (8);
        r.removeFromRight (72);   // the depth's words
        depth[static_cast<std::size_t> (s)].setBounds (r.withSizeKeepingCentre (r.getWidth(), 22));
    }
}

void PadModEditor::paint (juce::Graphics& g)
{
    auto src = sourceArea();
    const auto display = src.removeFromTop (src.getHeight() * 40 / 100).toFloat();
    const auto inner = ceramic::display (g, display, 6.0f);
    if (shown == drums::ModSource::env)
        paintEnv (g, inner.reduced (8.0f, 6.0f));
    else if (shown == drums::ModSource::lfo)
        paintLfo (g, inner.reduced (8.0f, 6.0f));
    else if (shown == drums::ModSource::velocity)
        paintVelocity (g, inner.reduced (8.0f, 6.0f));
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.setFont (plugin::fonts::make (12.0f, look::Weight::medium, 0.06f));
        g.drawFittedText (shown == drums::ModSource::wheel ? "MOD WHEEL (CC 1): every hit follows it as it moves"
                                                            : "AFTERTOUCH: press into the key or pad",
                          inner.reduced (10.0f, 4.0f).toNearestInt(), juce::Justification::centred, 2);
    }
    src.removeFromTop (4);
    g.setColour (cc::textSecondary);
    g.setFont (labelFont());
    if (shown == drums::ModSource::env)
    {
        auto row = src.removeFromTop (14);
        const int w = row.getWidth() / 4;
        for (auto* name : envNames)
            g.drawText (name, row.removeFromLeft (w), juce::Justification::centred);
    }
    else if (shown == drums::ModSource::lfo)
    {
        auto row = src.removeFromTop (14);
        g.drawText (current().lfo.sync ? "RATE (SYNC)" : "RATE", row.removeFromLeft (row.getWidth() / 3), juce::Justification::centred);
        g.drawText ("SHAPE / TIME", row.withTrimmedLeft (6), juce::Justification::centredLeft);
    }
    else if (shown == drums::ModSource::velocity)
    {
        g.drawFittedText ("The hardest hit plays the pad as set; softer hits move by the depth.",
                          src.removeFromBottom (30), juce::Justification::centred, 2);
    }

    // Routes: "TO", then each slot's depth in words.
    auto routes = routesArea();
    g.setColour (cc::text);
    g.setFont (plugin::fonts::make (12.0f, look::Weight::semibold, 0.08f));
    g.drawText (juce::String (drums::modSourceName (shown)) + "  TO", routes.removeFromTop (20), juce::Justification::centredLeft);
    for (int s = 0; s < drums::modSlotsPerSource; ++s)
    {
        const auto& slot = current().slot (shown, s);
        char text[32] {};
        drums::describeDepth (slot.target, slot.depth, text, static_cast<int> (sizeof (text)));
        g.setColour (slot.target == drums::ModTarget::none ? cc::textMicro : cc::accent);
        g.setFont (plugin::fonts::make (12.0f, look::Weight::semibold, 0.04f));
        g.drawText (slot.target == drums::ModTarget::none ? juce::String ("off") : juce::String (text), rowArea (s).removeFromRight (70),
                    juce::Justification::centredRight);
    }
}

void PadModEditor::paintEnv (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& e = current().env;
    auto tx = [] (double secs) { return std::log10 (1.0 + secs * 100.0); };
    const double hold = 0.15;
    const double total = tx (e.attackSeconds) + tx (e.decaySeconds) + tx (hold) + tx (e.releaseSeconds) + 1.0e-9;
    auto px = [&] (double t) { return area.getX() + static_cast<float> (t / total) * area.getWidth(); };
    auto py = [&] (double level) { return area.getBottom() - static_cast<float> (level) * area.getHeight(); };
    juce::Path p;
    double t = 0.0;
    p.startNewSubPath (px (0.0), py (0.0));
    t += tx (e.attackSeconds);
    p.lineTo (px (t), py (1.0));
    // The decay's curve (fast start when curve < 0), drawn in a few segments.
    const double t0 = t, dt = tx (e.decaySeconds);
    for (int i = 1; i <= 12; ++i)
    {
        const double x = i / 12.0;
        const double shaped = mod::shapeSegment (x, e.curve);
        p.lineTo (px (t0 + x * dt), py (1.0 + (e.sustain - 1.0) * shaped));
    }
    t += dt + tx (hold);
    p.lineTo (px (t), py (e.sustain));
    t += tx (e.releaseSeconds);
    p.lineTo (px (t), py (0.0));
    g.setColour (cc::accent);
    g.strokePath (p, juce::PathStrokeType (1.6f));
}

void PadModEditor::paintLfo (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& l = current().lfo;
    mod::CurveTable none {};
    juce::Path p;
    const int n = 96;
    for (int i = 0; i <= n; ++i)
    {
        const double phase = 2.0 * i / n;   // two cycles
        const double v = mod::lfoShapeValue (l.shape, phase - std::floor (phase), none, 7, static_cast<std::int64_t> (phase));
        const float x = area.getX() + area.getWidth() * static_cast<float> (i) / n;
        const float y = area.getCentreY() - static_cast<float> (v) * area.getHeight() * 0.45f;
        if (i == 0)
            p.startNewSubPath (x, y);
        else
            p.lineTo (x, y);
    }
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
    g.setColour (cc::accent);
    g.strokePath (p, juce::PathStrokeType (1.6f));
}

void PadModEditor::paintVelocity (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto preset = current().velocityShape == drums::VelocityShape::soft ? mod::CurvePreset::soft
                      : current().velocityShape == drums::VelocityShape::hard ? mod::CurvePreset::hard
                                                                             : mod::CurvePreset::linear;
    const auto table = mod::compileCurve (mod::presetCurve (preset));
    juce::Path p;
    for (int i = 0; i <= 64; ++i)
    {
        const double x = i / 64.0;
        const float px = area.getX() + area.getWidth() * static_cast<float> (x);
        const float py = area.getBottom() - area.getHeight() * static_cast<float> (mod::curveAt (table, x));
        if (i == 0)
            p.startNewSubPath (px, py);
        else
            p.lineTo (px, py);
    }
    g.setColour (cc::accent);
    g.strokePath (p, juce::PathStrokeType (1.6f));
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (plugin::fonts::make (10.5f, look::Weight::medium, 0.06f));
    g.drawText ("soft", area.removeFromBottom (12).removeFromLeft (40), juce::Justification::bottomLeft);
}

} // namespace osp::drumsplugin
