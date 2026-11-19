#include "StrumPanel.h"

#include "AndorParameters.h"
#include "Binding.h"
#include "Design.h"
#include "EqEditor.h"
#include "MainSections.h"
#include "OspLookAndFeel.h"
#include "SatellitePanel.h"
#include "core/Prng.h"

#include <algorithm>
#include <cmath>

namespace osp::plugin
{

namespace
{
    MiniKnob::Formatter textOf (juce::RangedAudioParameter& p)
    {
        return [&p] (double v) { return p.getText (p.convertTo0to1 (static_cast<float> (v)), 0); };
    }

    juce::String laneSetting (InstrumentHost& host, int card, const char* name)
    {
        return ids::arpCardSetting (host.cardArpNamespace(), card, name);
    }
}

//==============================================================================
StrumDisplay::StrumDisplay (InstrumentHost& h, int c, KeyHeld k) : host (h), card (juce::jlimit (0, 2, c)), held (std::move (k))
{
    setInterceptsMouseClicks (false, false);
}

void StrumDisplay::setCard (int c)
{
    card = juce::jlimit (0, 2, c);
    drawnFor.clear();
    repaint();
}

StrumDisplay::Model StrumDisplay::model() const
{
    Model m;
    auto value = [this] (const juce::String& id) { return static_cast<double> (host.parameterValue (id)); };
    auto strum = [&] (const char* field) { return value (ids::arpStrum (host.cardArpNamespace(), card, field)); };
    // The chord: the keys held now, else the preview (a C major seventh).
    if (held != nullptr)
        for (int n = 0; n < 128 && m.chord.size() < 12; ++n)
            if (held (n))
                m.chord.push_back (n);
    m.preview = m.chord.size() < 2;
    if (m.preview)
        m.chord = { 60, 64, 67, 71 };
    m.applies = juce::roundToInt (value (laneSetting (host, card, "pattern"))) == static_cast<int> (ArpPattern::chord);
    m.direction = std::clamp (juce::roundToInt (strum ("direction")), 0, arp::strumDirectionCount - 1);
    const double bpm = std::max (5.0, static_cast<double> (host.hostTempo()));
    m.requestedMs = strum ("sync") >= 0.5 ? arp::strumDivisionQuarters (juce::roundToInt (strum ("division"))) * 60000.0 / bpm
                                          : std::clamp (strum ("ms"), 0.0, arp::maxStrumMs);
    // What plays: the arpeggiator ends a strum 1 ms before the next step (swing and ARP RATE
    // modulation can shorten it further while playing).
    const auto rate = static_cast<ArpRate> (std::clamp (juce::roundToInt (value (laneSetting (host, card, "rate"))), 0, arp::rateCount - 1));
    const double stepMs = arp::rateQuarters (rate) * 60000.0 / bpm;
    m.playedMs = std::clamp (std::min (m.requestedMs, stepMs - 1.0), 0.0, m.requestedMs);
    const int n = static_cast<int> (m.chord.size());
    for (int i = 0; i < n; ++i)
        m.order.push_back (i);
    if (m.direction == static_cast<int> (StrumDirection::down))
        std::reverse (m.order.begin(), m.order.end());
    else if (m.direction == static_cast<int> (StrumDirection::random))
    {
        // An illustration of one order (each chord gets its own when playing).
        Prng prng (Prng::deriveSeed (0x5354524d, static_cast<std::uint64_t> (n), 1));
        for (int i = n - 1; i > 0; --i)
            std::swap (m.order[static_cast<std::size_t> (i)], m.order[static_cast<std::size_t> (prng.nextBelow (static_cast<std::uint64_t> (i + 1)))]);
    }
    return m;
}

void StrumDisplay::refresh()
{
    const auto m = model();
    juce::String key;
    key << card << ":" << static_cast<int> (m.preview) << static_cast<int> (m.applies) << m.direction << ":" << juce::String (m.requestedMs, 2) << ":"
        << juce::String (m.playedMs, 2) << ":";
    for (const int note : m.chord)
        key << note << ",";
    key << static_cast<int> (host.parameterValue (ids::arpStrum (host.cardArpNamespace(), card, "on")) >= 0.5f);
    if (key != drawnFor)
    {
        drawnFor = key;
        repaint();
    }
}

void StrumDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    // The dark well (SATELLITE's graphite).
    g.setColour (juce::Colour (0xff1c2021));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    const auto m = model();
    const bool on = host.parameterValue (ids::arpStrum (host.cardArpNamespace(), card, "on")) >= 0.5f;
    const auto colour = satview::cardColour (card);
    const float alpha = on && m.applies ? 1.0f : 0.45f;
    const auto area = bounds.reduced (22.0f, 24.0f).withTrimmedBottom (2.0f);

    // Top line: the direction and the spread; the right: PREVIEW or HELD.
    static const char* const directionText[] { "UP", "DOWN", "RANDOM", "ALTERNATE" };
    g.setFont (fonts::label (9.5f));
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    juce::String left = directionText[m.direction];
    if (m.direction == static_cast<int> (StrumDirection::random))
        left << "  (a new order each chord)";
    else if (m.direction == static_cast<int> (StrumDirection::alternate))
        left << "  (up, then down)";
    g.drawText (left, juce::Rectangle<float> (bounds.getX() + 12.0f, bounds.getY() + 6.0f, bounds.getWidth() * 0.7f, 14.0f), juce::Justification::centredLeft, false);
    g.setColour (juce::Colours::white.withAlpha (m.preview ? 0.4f : 0.7f));
    g.drawText (m.preview ? "PREVIEW" : "HELD", juce::Rectangle<float> (bounds.getRight() - 92.0f, bounds.getY() + 6.0f, 80.0f, 14.0f), juce::Justification::centredRight, false);

    if (! m.applies)
    {
        g.setFont (fonts::label (11.0f));
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawText ("STRUM plays with the CHORD pattern", area, juce::Justification::centred, false);
        return;
    }

    // The time line: 0 at the left, the spread asked for at the right; the notes along what plays.
    const double shownMs = std::max (1.0, m.requestedMs);
    auto xOf = [&] (double ms) { return area.getX() + static_cast<float> (ms / shownMs) * area.getWidth(); };
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawLine (area.getX(), area.getBottom(), area.getRight(), area.getBottom(), 1.0f);
    const bool limited = m.playedMs + 0.5 < m.requestedMs;
    if (limited)
    {
        // The next step: the strum ends before it.
        const float x = xOf (m.playedMs + 1.0);
        g.setColour (juce::Colours::white.withAlpha (0.28f));
        for (float y = area.getY(); y < area.getBottom(); y += 6.0f)
            g.drawLine (x, y, x, std::min (y + 3.0f, area.getBottom()), 1.0f);
    }
    const int n = static_cast<int> (m.chord.size());
    const int lo = m.chord.front(), hi = std::max (m.chord.back(), lo + 1);
    auto yOf = [&] (int note) { return area.getBottom() - 6.0f - (area.getHeight() - 12.0f) * static_cast<float> (note - lo) / static_cast<float> (hi - lo); };
    juce::Path line;
    std::vector<juce::Point<float>> dots;
    for (int k = 0; k < n; ++k)
    {
        const double t = n > 1 ? m.playedMs * k / (n - 1) : 0.0;
        const juce::Point<float> p { xOf (t), yOf (m.chord[static_cast<std::size_t> (m.order[static_cast<std::size_t> (k)])]) };
        dots.push_back (p);
        if (k == 0)
            line.startNewSubPath (p);
        else
            line.lineTo (p);
    }
    g.setColour (colour.withAlpha (0.45f * alpha));
    g.strokePath (line, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    for (std::size_t k = 0; k < dots.size(); ++k)
    {
        const auto dot = juce::Rectangle<float> (8.0f, 8.0f).withCentre (dots[k]);
        if (m.preview)
        {
            g.setColour (juce::Colours::white.withAlpha (0.55f * alpha));
            g.drawEllipse (dot.reduced (0.6f), 1.3f);
        }
        else
        {
            g.setColour (colour.withAlpha (alpha).brighter (0.15f));
            g.fillEllipse (dot);
        }
        // Each note-on's moment, a short tick on the time line.
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.drawLine (dots[k].x, area.getBottom() - 3.0f, dots[k].x, area.getBottom() + 3.0f, 1.0f);
    }
    if (limited)
    {
        g.setFont (fonts::label (9.5f));
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawText ("plays " + juce::String (juce::roundToInt (m.playedMs)) + " ms: the step is shorter", bounds.withTrimmedTop (bounds.getHeight() - 18.0f).reduced (12.0f, 0.0f),
                    juce::Justification::centredLeft, false);
    }
}

//==============================================================================
StrumPanel::StrumPanel (InstrumentHost& h, int c, StrumDisplay::KeyHeld k)
    : MiniPanel ("STRUM"), host (h), cardIndex (juce::jlimit (0, 2, c)), cards (h.cardArpeggiator()), held (std::move (k))
{
    if (! cards)
        cardIndex = 0;
    sweep = std::make_unique<StrumDisplay> (host, cardIndex, held);
    addAndMakeVisible (*sweep);
    build();
}

StrumPanel::~StrumPanel() = default;

juce::String StrumPanel::id (const char* field) const
{
    return ids::arpStrum (host.cardArpNamespace(), cardIndex, field);
}

void StrumPanel::build()
{
    // The shown card's own controls (rebuilt when another card is shown: each binds its card).
    auto& state = host.apvts();
    const auto colour = satview::cardColour (cardIndex);
    onLamp = std::make_unique<MacroSwitchLamp> (*state.getParameter (id ("on")), colour, &host.undo());
    onLamp->setTooltip (cards ? juce::String ("Strum ") + static_cast<char> ('A' + cardIndex) + " on / off" : juce::String ("Strum on / off"));
    binding::tag (*onLamp, id ("on"));
    addAndMakeVisible (*onLamp);
    auto segments = [&] (const juce::String& pid, juce::StringArray items, const char* tooltip) {
        auto s = std::make_unique<SegmentedControl> (*state.getParameter (pid), std::move (items));
        s->setAccent (colour);
        s->setTooltip (tooltip);
        binding::tag (*s, pid);
        addAndMakeVisible (*s);
        return s;
    };
    direction = segments (id ("direction"), { "UP", "DOWN", "RANDOM", "ALT" },
                          "UP: lowest to highest. DOWN: highest to lowest. RANDOM: a new order each chord. ALT: up, then down");
    timing = segments (id ("sync"), { "FREE", "SYNC" }, "FREE: the spread in milliseconds. SYNC: a note value at the tempo");
    auto knob = [&] (const char* name, const char* caption) {
        auto* p = state.getParameter (id (name));
        auto k = std::make_unique<MiniKnob> (state, id (name), caption, textOf (*p));
        k->setArcColour (colour);
        binding::tag (k->slider, id (name));
        addAndMakeVisible (*k);
        return k;
    };
    ms = knob ("ms", "SPREAD");
    division = knob ("division", "SPREAD");
    ms->slider.setTooltip ("From the first note to the last, whatever the number of notes");
    division->slider.setTooltip ("From the first note to the last, as a note value at the tempo");
    msField = std::make_unique<EqValueField> (host, id ("ms"));
    msField->setColour (colour);
    addAndMakeVisible (*msField);
    sweep->setCard (cardIndex);
    refreshContent();
    resized();
}

void StrumPanel::showCard (int c)
{
    c = cards ? juce::jlimit (0, 2, c) : 0;
    if (c == cardIndex)
        return;
    cardIndex = c;
    for (auto* child : { static_cast<juce::Component*> (onLamp.get()), static_cast<juce::Component*> (direction.get()), static_cast<juce::Component*> (timing.get()),
                         static_cast<juce::Component*> (ms.get()), static_cast<juce::Component*> (division.get()), static_cast<juce::Component*> (msField.get()) })
        if (child != nullptr)
            removeChildComponent (child);
    build();
    repaint();
}

juce::Point<int> StrumPanel::cardSize() const
{
    // Header 32, the display 96, the keys 24, the knob 92, gaps.
    return { width, 32 + 96 + 10 + 24 + 8 + 92 + 10 };
}

void StrumPanel::layoutContent (juce::Rectangle<int> area)
{
    // The header: A / B / C after the title (with the per-card arpeggiator), the light at the right.
    const int headerY = area.getY() - 17;
    for (int c = 0; c < 3; ++c)
        chips[static_cast<std::size_t> (c)] = cards ? juce::Rectangle<int> (24, 20).withCentre ({ area.getX() + 108 + 28 * c, headerY }) : juce::Rectangle<int>();
    if (onLamp != nullptr)
        onLamp->setBounds (juce::Rectangle<int> (26, 26).withCentre ({ area.getRight() - 6, headerY }));
    sweep->setBounds (area.removeFromTop (96));
    area.removeFromTop (10);
    auto keys = area.removeFromTop (24);
    if (direction != nullptr)
    {
        direction->setBounds (keys.removeFromLeft (220));
        keys.removeFromLeft (12);
        timing->setBounds (keys);
    }
    area.removeFromTop (8);
    if (ms != nullptr)
    {
        auto knobs = area.removeFromTop (92);
        const auto knobArea = knobs.removeFromLeft (knobs.getWidth() / 3);
        ms->setBounds (knobArea);
        division->setBounds (knobArea);
        // The typed value beside the knob (drag it, or double-click to type milliseconds).
        msField->setBounds (juce::Rectangle<int> (86, 26).withCentre ({ knobs.getX() + 60, knobArea.getCentreY() - 6 }));
    }
}

void StrumPanel::refreshContent()
{
    if (ms == nullptr)
        return;
    const bool sync = host.parameterValue (id ("sync")) >= 0.5f;
    ms->setVisible (! sync);
    msField->setVisible (! sync);
    division->setVisible (sync);
    sweep->refresh();
    repaint (getLocalBounds().withHeight (40));
}

void StrumPanel::paint (juce::Graphics& g)
{
    MiniPanel::paint (g);
    // The value field sits on the bone card: a graphite slot under it, as on the EQ's displays.
    if (msField != nullptr && msField->isVisible())
    {
        g.setColour (juce::Colour (0xff1c2021));
        g.fillRoundedRectangle (msField->getBounds().toFloat().expanded (4.0f, 2.0f), 5.0f);
    }
    if (! cards)
        return;
    // The card chips (their identity colours; the shown one filled): showing, never switching.
    for (int c = 0; c < 3; ++c)
    {
        const auto chip = chips[static_cast<std::size_t> (c)].toFloat();
        const auto colour = satview::cardColour (c);
        const bool shown = c == cardIndex;
        const bool on = host.parameterValue (ids::arpStrum (host.cardArpNamespace(), c, "on")) >= 0.5f;
        g.setColour (shown ? colour.withAlpha (0.32f) : juce::Colours::transparentBlack);
        g.fillRoundedRectangle (chip, 4.0f);
        g.setColour (shown ? colour.darker (0.2f) : palette::hairline);
        g.drawRoundedRectangle (chip.reduced (0.5f), 4.0f, 1.0f);
        g.setFont (fonts::label (11.0f));
        g.setColour (palette::text.withAlpha (shown ? 0.95f : 0.6f));
        g.drawText (juce::String::charToString (static_cast<juce::juce_wchar> ('A' + c)), chip, juce::Justification::centred, false);
        if (on)
        {
            g.setColour (colour);
            g.fillEllipse (juce::Rectangle<float> (3.5f, 3.5f).withCentre ({ chip.getCentreX(), chip.getBottom() - 3.5f }));
        }
    }
}

void StrumPanel::mouseDown (const juce::MouseEvent& e)
{
    for (int c = 0; c < 3; ++c)
        if (cards && chips[static_cast<std::size_t> (c)].contains (e.getPosition()))
        {
            showCard (c);
            return;
        }
}

} // namespace osp::plugin
