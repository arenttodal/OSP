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
    // The inset is small: the longer styles in short (the list and the host show them whole).
    static const juce::StringArray shortNames { "UP", "DOWN", "UP/DOWN", "PLAYED", "RANDOM", "CHORD", "DN/UP", "UP&DN", "DN&UP", "CONV",
                                                "DIV", "CON/DIV", "PINKY", "PINKY UD", "THUMB", "THUMB UD", "RND OTH", "RND 1" };
    const auto text = shortNames[pattern] + "  " + middleDot + "  " + OspAudioProcessor::arpRateNames()[rate];
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
    g.drawFittedText (status, inset.getSmallestIntegerContainer(), juce::Justification::centred, 1, 0.8f);
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

namespace
{
    /** Reference geometry of the list (panel coordinates). */
    constexpr float listRowHeight = 106.0f / static_cast<float> (ArpInlinePanel::visibleRows);
}

const std::array<int, arp::patternCount>& ArpInlinePanel::listOrder()
{
    // Families together, as in Live: the straight walks, the inward and outward ones, the
    // finger patterns, then the order played, the chord and the random styles.
    using P = ArpPattern;
    static const std::array<int, arp::patternCount> order = [] {
        std::array<int, arp::patternCount> o {};
        const P styles[] { P::up, P::down, P::upDown, P::downUp, P::upAndDown, P::downAndUp, P::converge, P::diverge, P::convergeDiverge,
                           P::pinkyUp, P::pinkyUpDown, P::thumbUp, P::thumbUpDown, P::played, P::chord, P::random, P::randomOther, P::randomOnce };
        static_assert (sizeof (styles) / sizeof (styles[0]) == static_cast<std::size_t> (arp::patternCount));
        for (std::size_t i = 0; i < o.size(); ++i)
            o[i] = static_cast<int> (styles[i]);
        return o;
    }();
    return order;
}

const std::vector<int>& ArpInlinePanel::rateOrder()
{
    // Slowest to fastest (a step's length: 1.5, 1, 0.75, 0.67, 0.5, 0.375, 0.33, 0.25, 0.17,
    // 0.125 quarter notes). The parameter keeps its saved order; only the knob walks this one.
    static const std::vector<int> order = [] {
        std::vector<int> o;
        for (int i = 0; i < arp::rateCount; ++i)
            o.push_back (i);
        std::stable_sort (o.begin(), o.end(), [] (int x, int y) {
            return arp::rateQuarters (static_cast<ArpRate> (x)) > arp::rateQuarters (static_cast<ArpRate> (y));
        });
        return o;
    }();
    return order;
}

juce::String ArpInlinePanel::patternLabel (int index)
{
    static const juce::StringArray labels { "Up", "Down", "Up/Down", "Played", "Random", "Chord", "Down/Up", "Up & Down", "Down & Up",
                                            "Converge", "Diverge", "Con & Diverge", "Pinky Up", "Pinky Up/Down", "Thumb Up",
                                            "Thumb Up/Down", "Random Other", "Random Once" };
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
          revealSelected();
          repaint();
      }, &p.undoManager),
      octavesAttachment (parameter (p, "arp.octaves"), [this] (float v) {
          octaveCount = juce::jlimit (arp::minOctaves, arp::maxOctaves, juce::roundToInt (v));
          updatePreview();
          repaint();
      }, &p.undoManager),
      rate (p.parameters, "arp.rate", "RATE", [] (double v) {
          const auto& order = rateOrder();
          const int position = juce::jlimit (0, static_cast<int> (order.size()) - 1, juce::roundToInt (v));
          return OspAudioProcessor::arpRateNames()[order[static_cast<std::size_t> (position)]];
      }),
      gate (p.parameters, "arp.gate", "GATE", [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; }),
      swing (p.parameters, "arp.swing", "SWING", [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; })
{
    setTitle ("Arpeggiator settings");
    rate.setChoiceOrder (parameter (p, "arp.rate"), rateOrder(), &p.undoManager);
    rate.slider.setTooltip ("Step length, from 1/4 dotted (slowest) to 1/32 (fastest)");
    for (auto* knob : { &rate, &gate, &swing })
    {
        knob->setArcColour (design::colour::accent);
        addAndMakeVisible (*knob);
    }
    enabledAttachment.sendInitialUpdate();
    patternAttachment.sendInitialUpdate();
    octavesAttachment.sendInitialUpdate();
    shown.current = -1;
    updatePreview();
}

ArpInlinePanel::~ArpInlinePanel() = default;

void ArpInlinePanel::updatePreview()
{
    // While nothing is held the display shows the pattern on a C major chord: the same
    // scheduler, run here on its own (no audio, no host).
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
    updateField();
}

void ArpInlinePanel::updateField()
{
    // The picture: the style itself on four notes over one octave, so every shape reads.
    const int want = hoverRow >= 0 ? listOrder()[static_cast<std::size_t> (hoverRow)] : pattern;
    if (want == fieldShown)
        return;
    fieldShown = want;
    Arpeggiator demo;
    demo.prepare (48000.0);
    Arpeggiator::Settings s;
    s.enabled = true;
    s.pattern = static_cast<ArpPattern> (want);
    s.octaves = 1;
    HostTiming none;
    demo.beginBlock (s, none, 1);
    for (int note : { 60, 64, 67, 71 })
        demo.noteOn (0, note, 100, 1);
    demo.display (fieldSteps);
    repaint (field.getSmallestIntegerContainer().expanded (2));
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
    // Panel coordinates (reference px, panel 1378 x 177): the title and the collapse button on
    // the top row; the numbered steps on the left; PATTERN's picture (level with the steps) and
    // its list; then RATE, the OCTAVES keys, GATE and SWING, captions level with each other.
    const auto r = getLocalBounds().toFloat();
    title = { 26.0f, 14.0f, 300.0f, 26.0f };
    collapse = juce::Rectangle<float> (30.0f, 30.0f).withCentre ({ r.getRight() - 34.0f, 28.0f });
    numbers = { 57.0f, 50.0f, 480.0f, 18.0f };
    steps = { 57.0f, 72.0f, 480.0f, 84.0f };
    patternCaption = { 562.0f, 50.0f, 150.0f, 18.0f };
    field = { 562.0f, 72.0f, 150.0f, 84.0f };
    list = { 722.0f, 50.0f, 156.0f, 106.0f };
    const float knobTop = 58.0f, knobH = 94.0f, knobW = 100.0f;
    rate.setBounds (juce::Rectangle<float> (knobW, knobH).withCentre ({ 940.0f, knobTop + 0.5f * knobH }).getSmallestIntegerContainer());
    octaveCaption = { 995.0f, knobTop, 80.0f, 0.22f * knobH };
    octaveKeys = { 1012.0f, knobTop + 0.22f * knobH + 2.0f, 46.0f, 156.0f - (knobTop + 0.22f * knobH + 2.0f) };
    gate.setBounds (juce::Rectangle<float> (knobW, knobH).withCentre ({ 1140.0f, knobTop + 0.5f * knobH }).getSmallestIntegerContainer());
    swing.setBounds (juce::Rectangle<float> (knobW, knobH).withCentre ({ 1255.0f, knobTop + 0.5f * knobH }).getSmallestIntegerContainer());
    revealSelected();
}

juce::Rectangle<float> ArpInlinePanel::octaveKeyArea (int octaves) const
{
    // 1 on top to 4 at the bottom, read like a list.
    const int i = juce::jlimit (arp::minOctaves, arp::maxOctaves, octaves) - arp::minOctaves;
    constexpr float gap = 2.5f;
    const float h = (octaveKeys.getHeight() - 3.0f * gap) / 4.0f;
    return { octaveKeys.getX(), octaveKeys.getY() + static_cast<float> (i) * (h + gap), octaveKeys.getWidth(), h };
}

juce::Rectangle<float> ArpInlinePanel::patternRowArea (int which) const
{
    const auto& order = listOrder();
    const auto it = std::find (order.begin(), order.end(), which);
    if (it == order.end())
        return {};
    const int row = static_cast<int> (std::distance (order.begin(), it)) - firstRow;
    if (row < 0 || row >= visibleRows)
        return {};
    return { list.getX(), list.getY() + listRowHeight * static_cast<float> (row), list.getWidth() - 8.0f, listRowHeight };
}

int ArpInlinePanel::rowAt (juce::Point<float> p) const
{
    if (! list.contains (p) || p.x > list.getRight() - 8.0f)
        return -1;
    const int row = firstRow + static_cast<int> ((p.y - list.getY()) / listRowHeight);
    return row >= 0 && row < arp::patternCount ? row : -1;
}

void ArpInlinePanel::scrollTo (int first)
{
    const int next = juce::jlimit (0, arp::patternCount - visibleRows, first);
    if (next != firstRow)
    {
        firstRow = next;
        repaint (list.getSmallestIntegerContainer().expanded (2));
    }
}

void ArpInlinePanel::revealSelected()
{
    // The chosen style stays in view (it may change from automation or a preset).
    const auto& order = listOrder();
    const int row = static_cast<int> (std::distance (order.begin(), std::find (order.begin(), order.end(), pattern)));
    if (row < firstRow)
        scrollTo (row);
    else if (row >= firstRow + visibleRows)
        scrollTo (row - visibleRows + 1);
}

void ArpInlinePanel::choosePattern (int which)
{
    patternAttachment.setValueAsCompleteGesture (static_cast<float> (juce::jlimit (0, arp::patternCount - 1, which)));
}

void ArpInlinePanel::chooseOctaves (int octaves)
{
    octavesAttachment.setValueAsCompleteGesture (static_cast<float> (juce::jlimit (arp::minOctaves, arp::maxOctaves, octaves)));
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
    paintField (g);
    paintList (g);
    paintOctaves (g);
}

namespace
{
    /** The shallow recess the step strip and the picture share. */
    void recess (juce::Graphics& g, juce::Rectangle<float> area)
    {
        using namespace design;
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawRoundedRectangle (area.translated (0.0f, 1.0f), 5.0f, 1.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe2d9cc), 0.0f, area.getY(), juce::Colour (0xffebe4d9), 0.0f, area.getBottom(), false));
        g.fillRoundedRectangle (area, 5.0f);
        g.setColour (colour::hairline.darker (0.08f));
        g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);
    }
}

void ArpInlinePanel::paintField (juce::Graphics& g)
{
    using namespace design;
    const float dim = enabled ? 1.0f : 0.55f;
    g.setFont (type::popupLabel (13.0f));
    g.setColour (colour::text.withAlpha (0.62f * dim));
    g.drawText ("PATTERN", patternCaption, juce::Justification::centredLeft, false);
    recess (g, field);

    // Four faint lines, one per note of the chord (C E G B), and twelve steps across.
    constexpr int columns = 12;
    const auto inner = field.reduced (10.0f, 11.0f);
    const float cw = inner.getWidth() / static_cast<float> (columns - 1);
    const std::array<int, 4> notes { 60, 64, 67, 71 };
    auto yOf = [&inner] (int note) { return inner.getBottom() - inner.getHeight() * static_cast<float> (note - 60) / 11.0f; };
    g.setColour (colour::hairline.darker (0.02f).withAlpha (0.7f));
    for (int n : notes)
        g.fillRect (juce::Rectangle<float> (inner.getX() - 4.0f, yOf (n) - 0.5f, inner.getWidth() + 8.0f, 1.0f));

    const auto ink = (fieldShown == pattern ? colour::accent : stepTan).withMultipliedAlpha (dim);
    const bool chord = fieldShown == static_cast<int> (ArpPattern::chord);
    juce::Path line;
    bool started = false;   // (a path holding only its first point still reads as empty)
    for (int c = 0; c < columns; ++c)
    {
        const int low = fieldSteps.low[static_cast<std::size_t> (c)], high = fieldSteps.high[static_cast<std::size_t> (c)];
        if (low < 0)
            continue;
        const float x = inner.getX() + cw * static_cast<float> (c);
        if (chord)
        {
            // Every note at once: a column of dots, joined.
            g.setColour (ink.withAlpha (0.45f * dim));
            g.fillRect (juce::Rectangle<float> (x - 1.0f, yOf (high), 2.0f, yOf (low) - yOf (high)));
            g.setColour (ink);
            for (int n : notes)
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ x, yOf (n) }));
            continue;
        }
        const juce::Point<float> at (x, yOf (high));
        if (! started)
            line.startNewSubPath (at);
        else
            line.lineTo (at);
        started = true;
    }
    if (started && ! chord)
    {
        g.setColour (ink.withAlpha (0.55f * dim));
        g.strokePath (line, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (ink);
        for (int c = 0; c < columns; ++c)
            if (fieldSteps.high[static_cast<std::size_t> (c)] >= 0)
                g.fillEllipse (juce::Rectangle<float> (6.5f, 6.5f).withCentre ({ inner.getX() + cw * static_cast<float> (c), yOf (fieldSteps.high[static_cast<std::size_t> (c)]) }));
    }
}

void ArpInlinePanel::paintList (juce::Graphics& g)
{
    using namespace design;
    const float dim = enabled ? 1.0f : 0.55f;
    const auto& order = listOrder();
    g.setFont (type::popupLabel (13.0f));
    for (int row = firstRow; row < firstRow + visibleRows && row < arp::patternCount; ++row)
    {
        const int which = order[static_cast<std::size_t> (row)];
        const auto area = patternRowArea (which);
        const bool chosen = which == pattern;
        if (chosen)
        {
            g.setColour (colour::accent.withAlpha (0.16f + 0.06f * dim));
            g.fillRoundedRectangle (area.reduced (0.0f, 1.0f), 5.0f);
        }
        else if (row == hoverRow)
        {
            g.setColour (colour::text.withAlpha (0.06f));
            g.fillRoundedRectangle (area.reduced (0.0f, 1.0f), 5.0f);
        }
        g.setColour (colour::text.withAlpha ((chosen ? 0.95f : (row == hoverRow ? 0.85f : 0.66f)) * dim));
        g.drawText (patternLabel (which), area.withTrimmedLeft (10.0f), juce::Justification::centredLeft, false);
    }
    // A slim scroll track: where the six rows sit in the eighteen.
    const auto track = juce::Rectangle<float> (list.getRight() - 4.0f, list.getY() + 2.0f, 3.0f, list.getHeight() - 4.0f);
    g.setColour (colour::hairline.withAlpha (0.8f));
    g.fillRoundedRectangle (track, 1.5f);
    const float share = static_cast<float> (visibleRows) / static_cast<float> (arp::patternCount);
    const float top = static_cast<float> (firstRow) / static_cast<float> (arp::patternCount);
    g.setColour (colour::textSecondary.withAlpha (0.55f * dim));
    g.fillRoundedRectangle (track.withY (track.getY() + top * track.getHeight()).withHeight (share * track.getHeight()), 1.5f);
}

void ArpInlinePanel::paintOctaves (juce::Graphics& g)
{
    using namespace design;
    const float dim = enabled ? 1.0f : 0.55f;
    g.setFont (type::popupLabel (13.0f));
    g.setColour (colour::text.withAlpha (0.62f * dim));
    g.drawText ("OCTAVES", octaveCaption, juce::Justification::centred, false);
    for (int o = arp::minOctaves; o <= arp::maxOctaves; ++o)
    {
        const auto key = octaveKeyArea (o);
        const bool on = o == octaveCount;
        draw::button (g, key, 5.0f, on, hover == 10 + o, colour::accent);
        g.setFont (type::controlValue (0.86f));
        g.setColour (on ? colour::accent.darker (0.35f).withMultipliedAlpha (dim) : colour::text.withAlpha (0.78f * dim));
        g.drawText (juce::String (o), key, juce::Justification::centred, false);
    }
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
    recess (g, steps);
    g.setColour (colour::hairline.darker (0.08f));
    for (int c = 1; c < columns; ++c)
        g.fillRect (juce::Rectangle<float> (1.0f, steps.getHeight() - 2.0f).withPosition (steps.getX() + cw * static_cast<float> (c), steps.getY() + 1.0f));

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
        const auto bar = juce::Rectangle<float> (cell.getX() + 3.0f, inner.getBottom() - barH, cw - 6.0f, barH);
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
    if (collapse.contains (e.position))
        now = 3;
    for (int o = arp::minOctaves; o <= arp::maxOctaves; ++o)
        if (octaveKeyArea (o).contains (e.position))
            now = 10 + o;
    const int row = rowAt (e.position);
    if (now != hover || row != hoverRow)
    {
        hover = now;
        hoverRow = row;
        setMouseCursor (hover != 0 || hoverRow >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        updateField();   // the picture previews the style under the pointer
        repaint (list.getUnion (octaveKeys).getUnion (collapse).getSmallestIntegerContainer().expanded (3));
    }
}

void ArpInlinePanel::mouseExit (const juce::MouseEvent&)
{
    if (hover != 0 || hoverRow >= 0)
    {
        hover = 0;
        hoverRow = -1;
        updateField();
        repaint();
    }
}

void ArpInlinePanel::mouseDown (const juce::MouseEvent& e)
{
    if (const int row = rowAt (e.position); row >= 0)
    {
        choosePattern (listOrder()[static_cast<std::size_t> (row)]);
        return;
    }
    for (int o = arp::minOctaves; o <= arp::maxOctaves; ++o)
        if (octaveKeyArea (o).contains (e.position))
        {
            chooseOctaves (o);
            return;
        }
    if (collapse.contains (e.position) && onCollapse != nullptr)
        onCollapse();
}

void ArpInlinePanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! list.contains (e.position))
        return;
    // One row per notch (trackpads add up their small deltas).
    wheelPending -= wheel.deltaY * 6.0f;
    const int rows = static_cast<int> (wheelPending);
    if (rows != 0)
    {
        wheelPending -= static_cast<float> (rows);
        scrollTo (firstRow + rows);
        hoverRow = rowAt (e.position);
        updateField();
    }
}

} // namespace osp::plugin
