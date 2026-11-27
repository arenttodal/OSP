#pragma once

#include "Design.h"
#include "OspLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cmath>
#include <functional>
#include <optional>

namespace osp::plugin
{

/**
    The source menu's OCTAVE row (owner, 27 Nov): -2 -1 0 +1 +2 shifts the keys that reach the
    layer (its `layerX.octave` parameter, InstrumentEngine's LayerSettings::noteShift): -1 plays
    every key as the key an octave lower would - its own sample, note or zone, its own pitch -
    with nothing about the recording changed (root, tuning, speed). A choice applies at once
    (undoable) and closes the menu.
*/
class SourceOctaveRow final : public juce::PopupMenu::CustomComponent
{
public:
    static constexpr int itemId = 0x0c7a0e;   ///< (unique in the source menu; the row acts itself)
    static constexpr std::array<int, 5> octaves { -2, -1, 0, 1, 2 };

    SourceOctaveRow (std::optional<int> lit, juce::Colour accentColour, std::function<void (int)> choose)
        : CustomComponent (false), current (lit), accent (accentColour), onChoose (std::move (choose))
    {
        setTitle ("Octave");
    }

    std::optional<int> lit() const noexcept { return current; }
    /** What a click on an octave does (tests call it): applies it, then closes the menu. */
    void choose (int octave)
    {
        if (onChoose != nullptr)
            onChoose (octave);
        current = octave;
        if (findParentComponentOfClass<juce::Component>() != nullptr && isShowing())
            triggerMenuItem();
    }

    void getIdealSize (int& w, int& h) override
    {
        w = 300;
        h = 34;
    }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced (14.0f, 5.0f);
        auto r = area;
        g.setColour (palette::textDim);
        g.setFont (fonts::label (10.0f));
        g.drawText ("OCTAVE", r.removeFromLeft (64.0f), juce::Justification::centredLeft, false);
        strip = r;
        const float w = r.getWidth() / static_cast<float> (octaves.size());
        const float radius = 4.0f;
        juce::Path bar;
        bar.addRoundedRectangle (r, radius);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffeee7da), 0.0f, r.getY(), juce::Colour (0xffdcd3c4), 0.0f, r.getBottom(), false));
        g.fillPath (bar);
        g.setColour (juce::Colour (0xffc4bbad));
        g.strokePath (bar, juce::PathStrokeType (1.0f));
        g.setFont (fonts::label (11.0f));
        const auto cream = juce::Colour (0xfff6f0e6);
        for (std::size_t i = 0; i < octaves.size(); ++i)
        {
            const auto cell = juce::Rectangle<float> (r.getX() + w * static_cast<float> (i), r.getY(), w, r.getHeight());
            const bool on = current && *current == octaves[i];
            const bool over = hover == static_cast<int> (i);
            if (on)
            {
                juce::Path key;
                key.addRoundedRectangle (cell.expanded (0.5f, 1.0f), radius);
                g.setGradientFill (juce::ColourGradient (accent.interpolatedWith (cream, 0.84f), 0.0f, cell.getY(), accent.interpolatedWith (cream, 0.74f), 0.0f, cell.getBottom(), false));
                g.fillPath (key);
                g.setColour (accent.withAlpha (0.55f));
                g.strokePath (key, juce::PathStrokeType (1.0f));
            }
            else
            {
                if (over)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.35f));
                    g.fillRoundedRectangle (cell.reduced (1.0f), radius);
                }
                if (i > 0 && ! (current && (*current == octaves[i - 1])))
                {
                    g.setColour (juce::Colour (0xffb5ac9f));
                    g.drawVerticalLine (juce::roundToInt (cell.getX()), cell.getY() + 1.0f, cell.getBottom() - 1.0f);
                }
            }
            g.setColour (on ? accent.darker (0.45f) : design::colour::text.withAlpha (0.86f));
            const int o = octaves[i];
            g.drawText (o > 0 ? "+" + juce::String (o) : (o < 0 ? juce::String::fromUTF8 ("\xe2\x88\x92") + juce::String (-o) : juce::String ("0")), cell, juce::Justification::centred, false);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (cellAt (e.position)); }
    void mouseExit (const juce::MouseEvent&) override { setHover (-1); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (const int i = cellAt (e.position); i >= 0)
            choose (octaves[static_cast<std::size_t> (i)]);
    }

private:
    int cellAt (juce::Point<float> p) const
    {
        if (! strip.contains (p))
            return -1;
        const int i = static_cast<int> ((p.x - strip.getX()) / (strip.getWidth() / static_cast<float> (octaves.size())));
        return juce::jlimit (0, static_cast<int> (octaves.size()) - 1, i);
    }
    void setHover (int i)
    {
        if (i != hover)
        {
            hover = i;
            repaint();
        }
    }

    std::optional<int> current;
    juce::Colour accent;
    std::function<void (int)> onChoose;
    juce::Rectangle<float> strip;
    int hover = -1;
};

} // namespace osp::plugin
