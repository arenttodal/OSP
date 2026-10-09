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

    /** The mockup's two step tones: the warm orange of what plays, the tan of what is to come. */
    const juce::Colour stepOrange { 0xffe9884d };
    const juce::Colour stepTan { 0xffa48b74 };

    void chevron (juce::Graphics& g, juce::Point<float> c, float w, float h, bool up, juce::Colour colour, float thickness)
    {
        juce::Path p;
        const float dy = up ? -0.5f * h : 0.5f * h;
        p.startNewSubPath (c.x - w, c.y - dy);
        p.lineTo (c.x, c.y + dy);
        p.lineTo (c.x + w, c.y - dy);
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

//==============================================================================
// The card by the keyboard

ArpControl::ArpControl (OspAudioProcessor& p)
    : ospProcessor (p),
      enabledAttachment (parameter (p, "arp.enabled"), [this] (float v) {
          enabled = v >= 0.5f;
          repaint();
      }, &p.undoManager)
{
    setTitle ("Arpeggiator");
    setTooltip ("The light switches the arpeggiator on and off; click the card for its settings");
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
    const auto text = OspAudioProcessor::arpPatternNames()[pattern] + "  " + middleDot + "  " + OspAudioProcessor::arpRateNames()[rate];
    if (text != status)
    {
        status = text;
        repaint();
    }
}

// Card proportions from the mockup (105 x 64 reference px): the top row (light, ARP, chevron)
// in the upper 55 %, the inset with pattern and rate below it.
juce::Rectangle<float> ArpControl::lightArea() const
{
    const auto r = getLocalBounds().toFloat();
    return { r.getX() + 4.0f, r.getY() + 3.0f, 26.0f, 0.5f * r.getHeight() - 2.0f };
}

juce::Rectangle<float> ArpControl::chevronArea() const
{
    const auto r = getLocalBounds().toFloat();
    return { r.getRight() - 26.0f, r.getY() + 3.0f, 22.0f, 0.5f * r.getHeight() - 2.0f };
}

void ArpControl::paint (juce::Graphics& g)
{
    using namespace design;
    const auto r = getLocalBounds().toFloat();
    draw::raised (g, r.reduced (1.0f), 10.0f, colour::panelTop, colour::panelBottom);

    // The light: an orange ring with a lit centre when on, a grey lens when off.
    const auto c = lightArea().getCentre();
    if (enabled)
    {
        g.setColour (colour::accent.withAlpha (hover == 1 ? 0.3f : 0.22f));
        g.fillEllipse (juce::Rectangle<float> (21.0f, 21.0f).withCentre (c));
        g.setColour (colour::accent);
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (c));
        g.setColour (juce::Colour (0xfffff1e2));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c));
    }
    else
    {
        const auto lens = juce::Rectangle<float> (13.0f, 13.0f).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8d8a85), lens.getX(), lens.getY(), juce::Colour (0xff5f5c58), lens.getRight(), lens.getBottom(), false));
        g.fillEllipse (lens);
        g.setColour (hover == 1 ? colour::text.withAlpha (0.5f) : colour::edgeShade);
        g.drawEllipse (lens.expanded (0.5f), 1.0f);
    }

    // ARP, then the chevron (down: the editor is hidden; up: it is shown).
    const auto top = lightArea();
    g.setFont (fonts::make (18.0f, fonts::Weight::semibold, 0.04f));
    g.setColour (colour::text.withAlpha (enabled ? 0.95f : 0.8f));
    g.drawText ("ARP", juce::Rectangle<float> (top.getRight() + 3.0f, top.getY(), 50.0f, top.getHeight()), juce::Justification::centredLeft, false);
    chevron (g, chevronArea().getCentre(), 4.5f, 3.0f, expanded, hover == 2 ? colour::text : colour::text.withAlpha (0.75f), 1.6f);

    // The inset: pattern and rate.
    const auto inset = juce::Rectangle<float> (r.getX() + 7.0f, r.getY() + 0.53f * r.getHeight(), r.getWidth() - 14.0f, 0.35f * r.getHeight());
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.drawRoundedRectangle (inset.translated (0.0f, 1.0f), 6.0f, 1.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe6ded1), 0.0f, inset.getY(), juce::Colour (0xffede6db), 0.0f, inset.getBottom(), false));
    g.fillRoundedRectangle (inset, 6.0f);
    g.setColour (colour::hairline.darker (0.06f));
    g.drawRoundedRectangle (inset.reduced (0.5f), 6.0f, 1.0f);
    g.setFont (fonts::make (12.5f, fonts::Weight::medium, 0.04f));
    g.setColour (enabled ? colour::textSecondary : colour::textMicro);
    g.drawText (status, inset, juce::Justification::centred, false);
}

void ArpControl::mouseMove (const juce::MouseEvent& e)
{
    const int now = lightArea().contains (e.position) ? 1 : 2;
    if (now != hover)
    {
        hover = now;
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
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
    else if (onToggleEditor != nullptr)
        onToggleEditor();
}

//==============================================================================

AdvancedCardButton::AdvancedCardButton (const juce::String& caption) : juce::Button (caption)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void AdvancedCardButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace design;
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool open = getToggleState();
    if (open || down)
    {
        // Pressed in (Advanced is open): a recessed face.
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe2d9cc), 0.0f, r.getY(), juce::Colour (0xffebe4d9), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (colour::hairline.darker (0.1f));
        g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, 1.0f);
    }
    else
        draw::raised (g, r, 9.0f, highlighted ? colour::panelTop.brighter (0.02f) : colour::panelTop, colour::panelBottom);
    // The largest type (15 down to 12 px) that fits with its chevron; without it if need be.
    const auto caption = getButtonText();
    float size = 15.0f;
    bool withChevron = chevron != 0;
    auto widthAt = [&caption] (float h) { return juce::GlyphArrangement::getStringWidth (fonts::make (h, fonts::Weight::medium, 0.01f), caption); };
    const float chevronRoom = r.getWidth() < 60.0f ? 10.0f : 14.0f;
    while (size > 12.0f && widthAt (size) + (withChevron ? chevronRoom : 0.0f) > r.getWidth() - 10.0f)
        size -= 0.5f;
    if (withChevron && widthAt (size) + chevronRoom > r.getWidth() - 6.0f)
        withChevron = false;
    g.setFont (fonts::make (size, fonts::Weight::medium, 0.01f));
    g.setColour (colour::text.withAlpha (highlighted || open ? 0.98f : 0.88f));
    const float textWidth = widthAt (size);
    const float total = textWidth + (withChevron ? chevronRoom : 0.0f);
    const float left = r.getCentreX() - 0.5f * total;
    g.drawText (caption, juce::Rectangle<float> (left, r.getY(), textWidth + 2.0f, r.getHeight()), juce::Justification::centredLeft, false);
    if (withChevron)
    {
        juce::Path p;
        const float x = left + textWidth + chevronRoom - 4.0f, y = r.getCentreY();
        const float d = chevron > 0 ? 1.0f : -1.0f;
        p.startNewSubPath (x - 2.0f * d, y - 4.5f);
        p.lineTo (x + 2.5f * d, y);
        p.lineTo (x - 2.0f * d, y + 4.5f);
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if (indicator)
        draw::led (g, { r.getX() + 6.5f, r.getY() + 6.5f }, 5.0f, colour::accent, 0.6f);
}

//==============================================================================
// The inline editor

juce::String ArpInlinePanel::patternLabel (int index)
{
    static const juce::StringArray labels { "Up", "Down", "Up/Down", "Played", "Random", "Chord" };
    return labels[juce::jlimit (0, labels.size() - 1, index)];
}

ArpInlinePanel::ArpInlinePanel (OspAudioProcessor& p)
    : ospProcessor (p),
      enabledAttachment (parameter (p, "arp.enabled"), [this] (float v) {
          enabled = v >= 0.5f;
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
      gate (p.parameters, "arp.gate", "GATE", [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; }),
      octaves (p.parameters, "arp.octaves", "OCTAVES", [] (double v) { return juce::String (juce::roundToInt (v)); }),
      swing (p.parameters, "arp.swing", "SWING", [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; })
{
    setTitle ("Arpeggiator settings");
    for (auto* knob : { &gate, &octaves, &swing })
    {
        knob->setArcColour (design::colour::accent);
        addAndMakeVisible (*knob);
    }
    // The preview follows OCTAVES too.
    octaves.slider.onValueChange = [this] {
        octaveCount = juce::jlimit (arp::minOctaves, arp::maxOctaves, juce::roundToInt (octaves.slider.getValue()));
        updatePreview();
        octaves.repaint();
        repaint (steps.getSmallestIntegerContainer().expanded (2));
    };
    octaveCount = juce::jlimit (arp::minOctaves, arp::maxOctaves, juce::roundToInt (octaves.slider.getValue()));
    enabledAttachment.sendInitialUpdate();
    patternAttachment.sendInitialUpdate();
    rateAttachment.sendInitialUpdate();
    shown.current = -1;
    updatePreview();
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
    s.octaves = octaveCount;
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
        repaint (numbers.getUnion (steps).getSmallestIntegerContainer().expanded (4));
    }
}

void ArpInlinePanel::resized()
{
    // Panel coordinates (reference px, panel 1378 x 177), from the mockup: the title and the
    // collapse button on the top row; the numbered steps over the left half; PATTERN and
    // RATE boxes, then GATE, OCTAVES and SWING knobs, captions level with the step numbers.
    const auto r = getLocalBounds().toFloat();
    title = { 26.0f, 14.0f, 300.0f, 26.0f };
    collapse = juce::Rectangle<float> (30.0f, 30.0f).withCentre ({ r.getRight() - 34.0f, 28.0f });
    numbers = { 57.0f, 50.0f, 720.0f, 18.0f };
    steps = { 57.0f, 72.0f, 720.0f, 84.0f };
    patternLabelArea = { 802.0f, 60.0f, 118.0f, 18.0f };
    patternBox = { 802.0f, 86.0f, 118.0f, 38.0f };
    rateLabelArea = { 936.0f, 60.0f, 98.0f, 18.0f };
    rateBox = { 936.0f, 86.0f, 98.0f, 38.0f };
    // The envelope's knob cells (caption, knob, value), a little larger.
    const float knobTop = 58.0f, knobH = 94.0f, knobW = 100.0f;
    gate.setBounds (juce::Rectangle<float> (knobW, knobH).withCentre ({ 1080.0f, knobTop + 0.5f * knobH }).getSmallestIntegerContainer());
    octaves.setBounds (juce::Rectangle<float> (knobW, knobH).withCentre ({ 1183.0f, knobTop + 0.5f * knobH }).getSmallestIntegerContainer());
    swing.setBounds (juce::Rectangle<float> (knobW, knobH).withCentre ({ 1290.0f, knobTop + 0.5f * knobH }).getSmallestIntegerContainer());
}

void ArpInlinePanel::paint (juce::Graphics& g)
{
    using namespace design;
    const auto r = getLocalBounds().toFloat();
    draw::raised (g, r, layout::panelRadius, colour::panelTop, colour::panelBottom);
    g.setColour (colour::text.withAlpha (0.9f));
    g.setFont (type::panelHeader());
    g.drawText ("ARPEGGIATOR", title, juce::Justification::centredLeft, false);

    // Collapse: a small round button with an up chevron.
    {
        const auto c = collapse;
        g.setColour (juce::Colour (0x1a302418));
        g.fillEllipse (c.translated (0.0f, 1.0f));
        g.setGradientFill (juce::ColourGradient (colour::buttonTop, 0.0f, c.getY(), colour::buttonBottom, 0.0f, c.getBottom(), false));
        g.fillEllipse (c);
        g.setColour (hover == 3 ? colour::text.withAlpha (0.45f) : colour::hairline.darker (0.12f));
        g.drawEllipse (c.reduced (0.5f), 1.0f);
        chevron (g, c.getCentre(), 5.0f, 3.2f, true, colour::text.withAlpha (hover == 3 ? 1.0f : 0.8f), 1.6f);
    }

    paintSteps (g);

    const float dim = enabled ? 1.0f : 0.55f;
    g.setFont (type::popupLabel (13.0f));
    g.setColour (colour::text.withAlpha (0.62f * dim));
    g.drawText ("PATTERN", patternLabelArea, juce::Justification::centredLeft, false);
    g.drawText ("RATE", rateLabelArea, juce::Justification::centredLeft, false);
    paintBox (g, patternBox, patternLabel (pattern), hover == 1);
    paintBox (g, rateBox, OspAudioProcessor::arpRateNames()[rate], hover == 2);
}

void ArpInlinePanel::paintBox (juce::Graphics& g, juce::Rectangle<float> box, const juce::String& text, bool hot)
{
    using namespace design;
    draw::button (g, box, 7.0f, false, hot, colour::accent);
    g.setFont (type::controlValue (0.92f));
    g.setColour (colour::text.withAlpha (enabled ? 0.92f : 0.55f));
    g.drawText (text, box.withTrimmedLeft (14.0f).withTrimmedRight (26.0f), juce::Justification::centredLeft, false);
    chevron (g, { box.getRight() - 16.0f, box.getCentreY() }, 4.0f, 2.6f, false, colour::textSecondary.withAlpha (enabled ? 1.0f : 0.6f), 1.4f);
}

void ArpInlinePanel::paintSteps (juce::Graphics& g)
{
    using namespace design;
    constexpr int columns = Arpeggiator::displaySteps;
    const float cw = steps.getWidth() / static_cast<float> (columns);

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
    const int current = live ? shown.current : -1;

    // Step numbers.
    g.setFont (type::popupLabel (12.5f));
    for (int c = 0; c < columns; ++c)
    {
        g.setColour (c == current ? colour::text : colour::text.withAlpha (0.62f));
        g.drawText (juce::String (c + 1), juce::Rectangle<float> (steps.getX() + cw * static_cast<float> (c), numbers.getY(), cw, numbers.getHeight()),
                    juce::Justification::centred, false);
    }

    // The strip: a shallow recess, one cell per step.
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawRoundedRectangle (steps.translated (0.0f, 1.0f), 5.0f, 1.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe2d9cc), 0.0f, steps.getY(), juce::Colour (0xffebe4d9), 0.0f, steps.getBottom(), false));
    g.fillRoundedRectangle (steps, 5.0f);
    g.setColour (colour::hairline.darker (0.08f));
    for (int c = 1; c < columns; ++c)
        g.fillRect (juce::Rectangle<float> (1.0f, steps.getHeight() - 2.0f).withPosition (steps.getX() + cw * static_cast<float> (c), steps.getY() + 1.0f));
    g.drawRoundedRectangle (steps.reduced (0.5f), 5.0f, 1.0f);

    // Each step a bar as high as its note (a chord: its top note) in the range shown.
    const auto inner = steps.reduced (0.0f, 4.0f);
    for (int c = 0; c < columns; ++c)
    {
        const int h = high[static_cast<std::size_t> (c)];
        if (low[static_cast<std::size_t> (c)] < 0)
            continue;
        const float t = hi > lo ? static_cast<float> (h - lo) / static_cast<float> (hi - lo) : 0.5f;
        const float barH = inner.getHeight() * (0.3f + 0.7f * t);
        const auto cell = juce::Rectangle<float> (steps.getX() + cw * static_cast<float> (c), inner.getY(), cw, inner.getHeight());
        const auto bar = juce::Rectangle<float> (cell.getX() + 4.0f, inner.getBottom() - barH, cw - 8.0f, barH);
        juce::Colour fill = stepTan;
        if (live)
            fill = c == current ? colour::accent : (c < current ? stepOrange.withAlpha (0.8f) : stepTan);
        else
            fill = (c % 2 == 0 ? stepTan : stepOrange).withAlpha (0.42f);
        if (! enabled)
            fill = fill.withMultipliedSaturation (0.6f).withMultipliedAlpha (0.8f);
        g.setGradientFill (juce::ColourGradient (fill.brighter (0.08f), 0.0f, bar.getY(), fill.darker (0.06f), 0.0f, bar.getBottom(), false));
        g.fillRect (bar);
    }

    // The step sounding: marked under the strip, like a playhead.
    if (current >= 0)
    {
        const auto mark = juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ steps.getX() + cw * (static_cast<float> (current) + 0.5f), steps.getBottom() - 1.0f });
        g.setColour (juce::Colour (0x33302418));
        g.fillEllipse (mark.translated (0.0f, 1.0f));
        g.setColour (juce::Colour (0xfffbf8f2));
        g.fillEllipse (mark);
        g.setColour (colour::text.withAlpha (0.55f));
        g.drawEllipse (mark.reduced (0.5f), 1.0f);
    }
}

void ArpInlinePanel::mouseMove (const juce::MouseEvent& e)
{
    int now = 0;
    if (patternBox.contains (e.position))
        now = 1;
    else if (rateBox.contains (e.position))
        now = 2;
    else if (collapse.contains (e.position))
        now = 3;
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
    if (patternBox.contains (e.position))
        showPatternMenu();
    else if (rateBox.contains (e.position))
        showRateMenu();
    else if (collapse.contains (e.position) && onCollapse != nullptr)
        onCollapse();
}

juce::PopupMenu ArpInlinePanel::patternMenu() const
{
    juce::PopupMenu menu;
    for (int i = 0; i < arp::patternCount; ++i)
        menu.addItem (i + 1, patternLabel (i), true, i == pattern);
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
    patternMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (patternBox.getSmallestIntegerContainer())).withDeletionCheck (*this),
                                 [safe] (int chosen) {
                                     if (safe != nullptr && chosen > 0)
                                         safe->patternAttachment.setValueAsCompleteGesture (static_cast<float> (chosen - 1));
                                 });
}

void ArpInlinePanel::showRateMenu()
{
    juce::Component::SafePointer<ArpInlinePanel> safe (this);
    rateMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (rateBox.getSmallestIntegerContainer())).withDeletionCheck (*this),
                              [safe] (int chosen) {
                                  if (safe != nullptr && chosen > 0)
                                      safe->rateAttachment.setValueAsCompleteGesture (static_cast<float> (chosen - 1));
                              });
}

} // namespace osp::plugin
