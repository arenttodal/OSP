#pragma once

#include "Design.h"
#include "OspLookAndFeel.h"

#include "drums/DrumKitEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

/**
    ANDOR Drums' drawing vocabulary, all taken from OSP's design system (Design.h palette,
    OspLookAndFeel fonts): one place for category colours, panels, badges and note names so
    the pads, the KEY MAP lanes and the editors always agree.
*/
namespace osp::drumsplugin::look
{

namespace colour = plugin::design::colour;
namespace type = plugin::type;
using Weight = plugin::fonts::Weight;

/** Category accents: OSP's macro identities, no new palette. */
inline juce::Colour category (drums::DrumCategory c)
{
    switch (c)
    {
        case drums::DrumCategory::kick: return colour::macro (0);
        case drums::DrumCategory::snare: return juce::Colour (0xff6f8fae);   // OSP's layer B blue, muted
        case drums::DrumCategory::closedHat:
        case drums::DrumCategory::openHat: return colour::macro (2);
        case drums::DrumCategory::clap: return colour::macro (1);
        case drums::DrumCategory::tom: return colour::macro (4);
        case drums::DrumCategory::rim: return juce::Colour (0xff9a958c);
        case drums::DrumCategory::cymbal:
        case drums::DrumCategory::percussion: return colour::macro (5);
        case drums::DrumCategory::unknown: break;
    }
    return colour::textMicro;
}

inline const char* categoryName (drums::DrumCategory c)
{
    static const char* const names[] = { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Tom", "Rim", "Cymbal", "Percussion", "Unknown" };
    return names[static_cast<int> (c)];
}

/** MIDI notes are authoritative; names follow OSP's convention (MIDI 60 = C4, 36 = C2). */
inline juce::String noteName (int note)
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (note < 0 || note > 127)
        return "-";
    return juce::String (names[note % 12]) + juce::String (note / 12 - 1);
}
inline juce::String noteWithNumber (int note) { return noteName (note) + " (" + juce::String (note) + ")"; }
inline bool isBlackKey (int note) { const int k = note % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }

inline void panel (juce::Graphics& g, juce::Rectangle<float> r, float radius = 10.0f)
{
    g.setColour (juce::Colours::black.withAlpha (0.05f));
    g.fillRoundedRectangle (r.translated (0.0f, 1.5f), radius);
    g.setGradientFill (juce::ColourGradient (colour::panelTop, r.getX(), r.getY(), colour::panelBottom, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (colour::hairline);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

inline void panelTitle (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, const juce::String& scope = {})
{
    g.setColour (colour::text);
    g.setFont (type::panelHeader());
    g.drawText (title, r, juce::Justification::centredLeft);
    if (scope.isNotEmpty())
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (type::panelHeader(), title);
        g.setColour (colour::textMicro);
        g.setFont (type::micro (12.0f));
        g.drawText (scope, r.withTrimmedLeft (w + 10), juce::Justification::centredLeft);
    }
}

/** The pad number in a small ivory square (the pad's handle for dragging). */
inline void badge (juce::Graphics& g, juce::Rectangle<float> r, int number, juce::Colour fill, juce::Colour text)
{
    g.setColour (fill);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (text);
    g.setFont (plugin::fonts::make (r.getHeight() * 0.62f, Weight::semibold));
    g.drawText (juce::String (number), r, juce::Justification::centred);
}

/** A sound's overview (min/max per bucket) drawn into `r`. */
inline void waveform (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<float>& lo, const std::vector<float>& hi, juce::Colour c,
                      float startFraction = 0.0f)
{
    if (hi.empty() || r.getWidth() < 2.0f)
        return;
    const int n = static_cast<int> (hi.size());
    const int columns = std::max (1, static_cast<int> (r.getWidth()));
    const float mid = r.getCentreY(), half = r.getHeight() * 0.46f;
    juce::Path wave;
    for (int x = 0; x < columns; ++x)
    {
        const auto b = static_cast<std::size_t> (std::min (n - 1, x * n / columns));
        const float top = std::min (1.0f, hi[b]), bottom = std::max (-1.0f, lo[b]);
        wave.addRectangle (r.getX() + static_cast<float> (x), mid - half * top, 1.0f, std::max (0.7f, half * (top - bottom)));
    }
    if (startFraction > 0.0f)
    {
        const float sx = r.getX() + r.getWidth() * startFraction;
        g.setColour (c.withAlpha (0.35f));
        g.saveState();
        g.reduceClipRegion (r.withRight (sx).toNearestInt());
        g.fillPath (wave);
        g.restoreState();
        g.setColour (c);
        g.saveState();
        g.reduceClipRegion (r.withLeft (sx).toNearestInt());
        g.fillPath (wave);
        g.restoreState();
    }
    else
    {
        g.setColour (c);
        g.fillPath (wave);
    }
}

/** A text button used as a segment of a two-way switch (PAD | EXPANDED, 2 x 4 | 3 x 3). */
inline void makeSegment (juce::TextButton& b, int group)
{
    b.setClickingTogglesState (true);
    b.setRadioGroupId (group);
}

} // namespace osp::drumsplugin::look
