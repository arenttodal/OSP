#include "ArpSection.h"

#include "OspLookAndFeel.h"

namespace osp::plugin
{

namespace
{
    juce::RangedAudioParameter& parameter (OspAudioProcessor& p, const juce::String& id)
    {
        auto* ranged = p.parameters.getParameter (id);
        jassert (ranged != nullptr);
        return *ranged;
    }

    const juce::String middleDot = juce::String::fromUTF8 ("\xc2\xb7");
}

//==============================================================================
// The control by the keyboard

ArpControl::ArpControl (OspAudioProcessor& p)
    : ospProcessor (p),
      enabledAttachment (parameter (p, "arp.enabled"), [this] (float v) {
          enabled = v >= 0.5f;
          repaint();
      }, &p.undoManager)
{
    setTitle ("Arpeggiator");
    setTooltip ("The light switches the arpeggiator on and off; the arrow shows its settings");
    enabledAttachment.sendInitialUpdate();
    refresh();
}

void ArpControl::setExpanded (bool open)
{
    if (expanded != open)
    {
        expanded = open;
        repaint();
    }
}

void ArpControl::refresh()
{
    const int pattern = juce::jlimit (0, arp::patternCount - 1, juce::roundToInt (ospProcessor.parameterValue ("arp.pattern")));
    const int rate = juce::jlimit (0, arp::rateCount - 1, juce::roundToInt (ospProcessor.parameterValue ("arp.rate")));
    const auto text = "ARP  /  " + OspAudioProcessor::arpPatternNames()[pattern] + " " + middleDot + " " + OspAudioProcessor::arpRateNames()[rate];
    if (text != status)
    {
        status = text;
        repaint();
    }
}

juce::Rectangle<float> ArpControl::lightArea() const
{
    const auto r = getLocalBounds().toFloat();
    return { r.getRight() - 262.0f, r.getY(), 26.0f, r.getHeight() };
}

juce::Rectangle<float> ArpControl::chevronArea() const
{
    const auto r = getLocalBounds().toFloat();
    return { r.getRight() - 26.0f, r.getY(), 26.0f, r.getHeight() };
}

void ArpControl::paint (juce::Graphics& g)
{
    using namespace design;
    // The light: the arpeggiator's amber when on, a dark unlit lens when off.
    const auto light = lightArea();
    const auto lens = juce::Rectangle<float> (11.0f, 11.0f).withCentre (light.getCentre());
    if (enabled)
        draw::led (g, lens.getCentre(), 10.0f, colour::arp.withMultipliedBrightness (hover == 1 ? 1.25f : 1.12f), 0.85f);
    else
    {
        g.setColour (colour::housingBottom.darker (hover == 1 ? 0.18f : 0.1f));
        g.fillEllipse (lens);
        g.setColour (colour::edgeShade);
        g.drawEllipse (lens, 1.0f);
    }

    // The state: subdued while off.
    const auto text = juce::Rectangle<float> (light.getRight() + 6.0f, 0.0f, chevronArea().getX() - light.getRight() - 10.0f, static_cast<float> (getHeight()));
    g.setFont (type::controlLabel (1.0f));
    g.setColour (enabled ? colour::text.withAlpha (0.88f) : colour::textMicro);
    g.drawText (status, text, juce::Justification::centredLeft, true);

    // The chevron: points down while the editor is hidden, up while it is shown.
    const auto c = chevronArea().getCentre();
    juce::Path chevron;
    const float w = 5.0f, h = 3.0f * (expanded ? -1.0f : 1.0f);
    chevron.startNewSubPath (c.x - w, c.y - 0.5f * h);
    chevron.lineTo (c.x, c.y + 0.5f * h);
    chevron.lineTo (c.x + w, c.y - 0.5f * h);
    g.setColour (hover == 2 || expanded ? colour::text : colour::textSecondary);
    g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void ArpControl::mouseMove (const juce::MouseEvent& e)
{
    const int now = lightArea().contains (e.position) ? 1 : (chevronArea().contains (e.position) ? 2 : 0);
    if (now != hover)
    {
        hover = now;
        setMouseCursor (hover != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ArpControl::mouseExit (const juce::MouseEvent&)
{
    hover = 0;
    repaint();
}

void ArpControl::toggleEnabled()
{
    enabledAttachment.setValueAsCompleteGesture (enabled ? 0.0f : 1.0f);
}

void ArpControl::mouseDown (const juce::MouseEvent& e)
{
    if (lightArea().contains (e.position))
        toggleEnabled();
    else if (chevronArea().contains (e.position) && onToggleEditor != nullptr)
        onToggleEditor();
}

//==============================================================================

SmallLinkButton::SmallLinkButton (const juce::String& caption) : juce::Button (caption)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SmallLinkButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace design;
    const bool open = getToggleState();
    g.setFont (type::controlLabel (0.86f));
    g.setColour (open || down ? colour::text : (highlighted ? colour::text.withAlpha (0.8f) : colour::textMicro));
    g.drawText (getButtonText(), getLocalBounds().toFloat(), juce::Justification::centredRight, false);
}

//==============================================================================
// The inline editor

ArpInlinePanel::GateSlider::GateSlider() : juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox)
{
    setSliderSnapsToMousePosition (false);
    setDoubleClickReturnValue (true, 75.0);
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    setTitle ("Arp gate");
}

void ArpInlinePanel::GateSlider::paint (juce::Graphics& g)
{
    using namespace design;
    const auto r = getLocalBounds().toFloat().reduced (2.0f, 0.0f);
    const float y = r.getCentreY();
    const auto range = getNormalisableRange();
    const float pos = static_cast<float> (range.convertTo0to1 (getValue()));
    const float x = r.getX() + pos * r.getWidth();
    // The track, 100 % marked (beyond it notes overlap: legato).
    g.setColour (colour::knobTrack);
    g.fillRoundedRectangle (r.withHeight (3.0f).withCentre ({ r.getCentreX(), y }), 1.5f);
    const float full = r.getX() + static_cast<float> (range.convertTo0to1 (100.0)) * r.getWidth();
    g.setColour (colour::divider);
    g.fillRect (juce::Rectangle<float> (1.0f, 9.0f).withCentre ({ full, y }));
    const auto accent = dimmed ? colour::textMicro : colour::arp;
    g.setColour (accent);
    g.fillRoundedRectangle (juce::Rectangle<float> (r.getX(), y - 1.5f, x - r.getX(), 3.0f), 1.5f);
    const auto thumb = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ x, y });
    g.setColour (juce::Colour (0x22302418));
    g.fillEllipse (thumb.translated (0.0f, 1.0f));
    g.setGradientFill (juce::ColourGradient (colour::knobCapTop, 0.0f, thumb.getY(), colour::knobCapBottom, 0.0f, thumb.getBottom(), false));
    g.fillEllipse (thumb);
    g.setColour (isMouseOverOrDragging() ? colour::text.withAlpha (0.6f) : colour::knobRim);
    g.drawEllipse (thumb, 1.0f);
}

ArpInlinePanel::ArpInlinePanel (OspAudioProcessor& p)
    : ospProcessor (p),
      enabledAttachment (parameter (p, "arp.enabled"), [this] (float v) {
          enabled = v >= 0.5f;
          gate.dimmed = ! enabled;
          gate.repaint();
          repaint();
      }, &p.undoManager),
      patternAttachment (parameter (p, "arp.pattern"), [this] (float v) {
          pattern = juce::jlimit (0, arp::patternCount - 1, juce::roundToInt (v));
          updatePreview();
          repaint();
      }, &p.undoManager),
      rateAttachment (parameter (p, "arp.rate"), [this] (float v) {
          rate = juce::jlimit (0, arp::rateCount - 1, juce::roundToInt (v));
          repaint();
      }, &p.undoManager),
      octavesAttachment (parameter (p, "arp.octaves"), [this] (float v) {
          octaves = juce::jlimit (arp::minOctaves, arp::maxOctaves, juce::roundToInt (v));
          updatePreview();
          repaint();
      }, &p.undoManager)
{
    setTitle ("Arpeggiator settings");
    addAndMakeVisible (gate);
    gateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters, "arp.gate", gate);
    gate.onValueChange = [this] { repaint (gateLabel.getSmallestIntegerContainer().expanded (4)); };
    enabledAttachment.sendInitialUpdate();
    patternAttachment.sendInitialUpdate();
    rateAttachment.sendInitialUpdate();
    octavesAttachment.sendInitialUpdate();
    shown.current = -1;
}

ArpInlinePanel::~ArpInlinePanel() = default;

void ArpInlinePanel::updatePreview()
{
    // While nothing is held the display shows the pattern's shape on a C major chord: the
    // same scheduler, run here on its own (no audio, no host).
    Arpeggiator demo;
    demo.prepare (48000.0);
    Arpeggiator::Settings s;
    s.enabled = true;
    s.pattern = static_cast<ArpPattern> (pattern);
    s.octaves = octaves;
    HostTiming none;
    demo.beginBlock (s, none, 1);
    for (int note : { 60, 64, 67 })
        demo.noteOn (0, note, 100, 1);
    demo.display (preview);
}

void ArpInlinePanel::refresh()
{
    const auto now = ospProcessor.arpView();
    if (now.active != shown.active || now.current != shown.current || now.low != shown.low || now.high != shown.high)
    {
        shown = now;
        repaint (well.getSmallestIntegerContainer().expanded (2));
    }
}

void ArpInlinePanel::resized()
{
    // Panel coordinates (reference px): the title and the display on the left, the four
    // settings to the right, each a label over its value.
    const auto r = getLocalBounds().toFloat();
    title = { 24.0f, 8.0f, 300.0f, 22.0f };
    well = { 24.0f, 38.0f, 600.0f, r.getHeight() - 54.0f };
    const float labelY = 36.0f, valueY = 58.0f, valueH = 34.0f;
    float x = 672.0f;
    patternLabel = { x, labelY, 150.0f, 18.0f };
    patternValue = { x - 8.0f, valueY, 150.0f, valueH };
    x += 170.0f;
    rateLabel = { x, labelY, 110.0f, 18.0f };
    rateValue = { x, valueY, 104.0f, valueH };
    x += 136.0f;
    gateLabel = { x, labelY, 190.0f, 18.0f };
    gateValue = { x, valueY, 190.0f, valueH };
    x += 222.0f;
    octavesLabel = { x, labelY, 160.0f, 18.0f };
    octavesRow = { x, valueY + 2.0f, std::min (160.0f, r.getRight() - 24.0f - x), valueH - 4.0f };
    gate.setBounds (gateValue.getSmallestIntegerContainer());
}

juce::Rectangle<float> ArpInlinePanel::octaveCell (int octave) const
{
    const float w = octavesRow.getWidth() / static_cast<float> (arp::maxOctaves);
    return { octavesRow.getX() + w * static_cast<float> (octave - 1), octavesRow.getY(), w, octavesRow.getHeight() };
}

void ArpInlinePanel::paint (juce::Graphics& g)
{
    using namespace design;
    const auto r = getLocalBounds().toFloat();
    draw::raised (g, r, layout::panelRadius, colour::panelTop, colour::panelBottom);
    g.setColour (colour::text.withAlpha (0.9f));
    g.setFont (type::panelHeader());
    g.drawText ("ARPEGGIATOR", title, juce::Justification::centredLeft, false);
    // A quiet divider between the display and the settings, as between the macros and the envelope.
    g.setColour (colour::divider);
    g.fillRect (juce::Rectangle<float> (well.getRight() + 24.0f, 14.0f, 1.0f, r.getHeight() - 28.0f));
    paintDisplay (g);
    paintControls (g);
}

void ArpInlinePanel::paintDisplay (juce::Graphics& g)
{
    using namespace design;
    draw::well (g, well, 8.0f, colour::wellA);
    const auto inner = well.reduced (14.0f, 10.0f);
    constexpr int columns = Arpeggiator::displaySteps;
    const float cw = inner.getWidth() / static_cast<float> (columns);

    // Beats: every fourth column a faint line.
    g.setColour (colour::wellGrid);
    for (int c = 4; c < columns; c += 4)
        g.fillRect (juce::Rectangle<float> (1.0f, inner.getHeight()).withPosition (inner.getX() + cw * static_cast<float> (c), inner.getY()));

    // What to draw: the scheduler's steps while notes are held (and the arpeggiator is on),
    // otherwise the pattern's shape, faint.
    const bool live = shown.active && enabled;
    std::array<int, columns> low {}, high {};
    for (int c = 0; c < columns; ++c)
    {
        low[static_cast<std::size_t> (c)] = live ? shown.low[static_cast<std::size_t> (c)] : preview.low[static_cast<std::size_t> (c)];
        high[static_cast<std::size_t> (c)] = live ? shown.high[static_cast<std::size_t> (c)] : preview.high[static_cast<std::size_t> (c)];
    }
    int lo = 128, hi = -1;
    for (int c = 0; c < columns; ++c)
        if (low[static_cast<std::size_t> (c)] >= 0)
        {
            lo = std::min (lo, low[static_cast<std::size_t> (c)]);
            hi = std::max (hi, high[static_cast<std::size_t> (c)]);
        }
    const float barH = 5.0f;
    auto yOf = [&] (int note) {
        if (hi <= lo)
            return inner.getCentreY();
        const float t = static_cast<float> (note - lo) / static_cast<float> (hi - lo);
        return inner.getBottom() - barH * 0.5f - 2.0f - t * (inner.getHeight() - barH - 4.0f);
    };
    const int current = live ? shown.current : -1;
    for (int c = 0; c < columns; ++c)
    {
        const auto cell = juce::Rectangle<float> (inner.getX() + cw * static_cast<float> (c), inner.getY(), cw, inner.getHeight());
        const int l = low[static_cast<std::size_t> (c)], h = high[static_cast<std::size_t> (c)];
        if (l < 0)
        {
            g.setColour (colour::wellGrid.brighter (0.15f));
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ cell.getCentreX(), inner.getCentreY() }));
            continue;
        }
        float alpha = 0.34f;
        if (live)
            alpha = c == current ? 1.0f : (c < current ? 0.55f : 0.3f);
        if (! enabled)
            alpha *= 0.6f;
        const float w = cw * 0.56f;
        const float top = yOf (h) - barH * 0.5f, bottom = yOf (l) + barH * 0.5f;
        const auto bar = juce::Rectangle<float> (cell.getCentreX() - 0.5f * w, top, w, bottom - top);
        if (c == current)
        {
            // The step sounding: lit, with a soft glow in the well.
            g.setColour (colour::arpOnDark.withAlpha (0.18f));
            g.fillRoundedRectangle (bar.expanded (5.0f, 5.0f), 5.0f);
        }
        g.setColour (colour::arpOnDark.withAlpha (alpha));
        g.fillRoundedRectangle (bar, 2.5f);
    }
}

void ArpInlinePanel::paintControls (juce::Graphics& g)
{
    using namespace design;
    const float dim = enabled ? 1.0f : 0.55f;
    auto label = [&] (const juce::String& text, juce::Rectangle<float> area, const juce::String& value = {}) {
        g.setFont (type::controlLabel (0.9f));
        g.setColour (colour::textSecondary.withMultipliedAlpha (dim));
        g.drawText (text, area, juce::Justification::centredLeft, false);
        if (value.isNotEmpty())
        {
            g.setColour (colour::text.withMultipliedAlpha (dim));
            g.setFont (type::controlValue (0.86f));
            g.drawText (value, area, juce::Justification::centredRight, false);
        }
    };

    // PATTERN: plain text; hovering lifts it, a click opens the menu.
    label ("PATTERN", patternLabel);
    if (hover == 1)
    {
        g.setColour (colour::text.withAlpha (0.06f));
        g.fillRoundedRectangle (patternValue, 6.0f);
    }
    g.setFont (type::controlValue (1.12f));
    g.setColour (colour::text.withMultipliedAlpha (dim));
    g.drawText (OspAudioProcessor::arpPatternNames()[pattern], patternValue.withTrimmedLeft (8.0f), juce::Justification::centredLeft, false);

    // RATE: a selector.
    label ("RATE", rateLabel);
    draw::button (g, rateValue, 7.0f, false, hover == 2, colour::arp);
    g.setFont (type::controlValue (1.0f));
    g.setColour (colour::text.withMultipliedAlpha (dim));
    g.drawText (OspAudioProcessor::arpRateNames()[rate], rateValue.withTrimmedLeft (12.0f).withTrimmedRight (22.0f), juce::Justification::centredLeft, false);
    {
        const auto c = juce::Point<float> (rateValue.getRight() - 15.0f, rateValue.getCentreY());
        juce::Path caret;
        caret.startNewSubPath (c.x - 4.0f, c.y - 2.0f);
        caret.lineTo (c.x, c.y + 2.0f);
        caret.lineTo (c.x + 4.0f, c.y - 2.0f);
        g.setColour (colour::textSecondary.withMultipliedAlpha (dim));
        g.strokePath (caret, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // GATE: its value beside its name, the slider below.
    label ("GATE", gateLabel, juce::String (juce::roundToInt (gate.getValue())) + " %");

    // OCTAVES: 1 2 3 4.
    label ("OCTAVES", octavesLabel);
    for (int o = arp::minOctaves; o <= arp::maxOctaves; ++o)
    {
        const auto cell = octaveCell (o).reduced (3.0f, 0.0f);
        const bool on = o == octaves;
        if (on)
        {
            // The chosen count: filled with the arpeggiator's amber (muted while it is off).
            const auto fill = enabled ? colour::arp : colour::textMicro.withAlpha (0.7f);
            g.setGradientFill (juce::ColourGradient (fill.brighter (0.12f), 0.0f, cell.getY(), fill.darker (0.12f), 0.0f, cell.getBottom(), false));
            g.fillRoundedRectangle (cell, 6.0f);
            g.setColour (fill.darker (0.35f).withAlpha (0.6f));
            g.drawRoundedRectangle (cell.reduced (0.5f), 6.0f, 1.0f);
        }
        else
            draw::button (g, cell, 6.0f, false, hover == 10 + o, colour::arp);
        g.setFont (type::controlValue (0.95f));
        g.setColour (on ? juce::Colours::white.withAlpha (0.96f) : colour::text.withMultipliedAlpha (dim));
        g.drawText (juce::String (o), cell, juce::Justification::centred, false);
    }
}

void ArpInlinePanel::mouseMove (const juce::MouseEvent& e)
{
    int now = 0;
    if (patternValue.contains (e.position))
        now = 1;
    else if (rateValue.contains (e.position))
        now = 2;
    else
        for (int o = arp::minOctaves; o <= arp::maxOctaves; ++o)
            if (octaveCell (o).contains (e.position))
                now = 10 + o;
    if (now != hover)
    {
        hover = now;
        setMouseCursor (hover != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ArpInlinePanel::mouseExit (const juce::MouseEvent&)
{
    if (hover != 0)
    {
        hover = 0;
        repaint();
    }
}

void ArpInlinePanel::mouseDown (const juce::MouseEvent& e)
{
    if (patternValue.contains (e.position))
        showPatternMenu();
    else if (rateValue.contains (e.position))
        showRateMenu();
    else
        for (int o = arp::minOctaves; o <= arp::maxOctaves; ++o)
            if (octaveCell (o).contains (e.position))
                octavesAttachment.setValueAsCompleteGesture (static_cast<float> (o));
}

juce::PopupMenu ArpInlinePanel::patternMenu() const
{
    juce::PopupMenu menu;
    for (int i = 0; i < arp::patternCount; ++i)
        menu.addItem (i + 1, OspAudioProcessor::arpPatternNames()[i], true, i == pattern);
    return menu;
}

juce::PopupMenu ArpInlinePanel::rateMenu() const
{
    juce::PopupMenu menu;
    const auto& names = OspAudioProcessor::arpRateNames();
    auto section = [&] (const char* header, std::initializer_list<ArpRate> rates) {
        menu.addSectionHeader (header);
        for (auto r : rates)
        {
            const int i = static_cast<int> (r);
            menu.addItem (i + 1, names[i], true, i == rate);
        }
    };
    section ("STRAIGHT", { ArpRate::quarter, ArpRate::eighth, ArpRate::sixteenth, ArpRate::thirtySecond });
    section ("DOTTED", { ArpRate::quarterDotted, ArpRate::eighthDotted, ArpRate::sixteenthDotted });
    section ("TRIPLET", { ArpRate::quarterTriplet, ArpRate::eighthTriplet, ArpRate::sixteenthTriplet });
    return menu;
}

void ArpInlinePanel::showPatternMenu()
{
    juce::Component::SafePointer<ArpInlinePanel> safe (this);
    patternMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (patternValue.getSmallestIntegerContainer())).withDeletionCheck (*this),
                                 [safe] (int chosen) {
                                     if (safe != nullptr && chosen > 0)
                                         safe->patternAttachment.setValueAsCompleteGesture (static_cast<float> (chosen - 1));
                                 });
}

void ArpInlinePanel::showRateMenu()
{
    juce::Component::SafePointer<ArpInlinePanel> safe (this);
    rateMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (rateValue.getSmallestIntegerContainer())).withDeletionCheck (*this),
                              [safe] (int chosen) {
                                  if (safe != nullptr && chosen > 0)
                                      safe->rateAttachment.setValueAsCompleteGesture (static_cast<float> (chosen - 1));
                              });
}

} // namespace osp::plugin
