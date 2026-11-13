#include "CharacterRack.h"

#include "Design.h"
#include "FxIdentity.h"
#include "MacroPopups.h"
#include "ParameterRack.h"
#include "ShapingPopups.h"

#include "engine/Shaping.h"

#include <cmath>

namespace osp::plugin
{

using rack::GraphFrame;
using rack::ParameterRack;

namespace
{
    float valueOf (InstrumentHost& host, const char* id) { return host.parameterValue (id); }

    // The values as CHARACTER's popover writes them.
    MiniKnob::Formatter percentText() { return [] (double v) { return format::percent (v) + " %"; }; }
    MiniKnob::Formatter bipolarText() { return [] (double v) { return format::bipolar (v) + " %"; }; }
    MiniKnob::Formatter hertzText() { return [] (double v) { return format::hertz (v); }; }
    MiniKnob::Formatter msText() { return [] (double v) { return format::milliseconds (v); }; }

    /** The filter's response as the CHARACTER popover draws it (its cutoff from CHARACTER
        between MIN and MAX; the envelope's peak as a dashed ghost), in the rack's glass. */
    class CharacterGraph final : public GraphFrame
    {
    public:
        CharacterGraph (InstrumentHost& h, juce::Colour inkColour) : host (h), ink (inkColour) {}

        void refresh() override
        {
            const std::array<float, 6> now { valueOf (host, "character.type"), valueOf (host, "character"), valueOf (host, "character.min"),
                                             valueOf (host, "character.max"), valueOf (host, "character.resonance"), valueOf (host, "character.envAmount") };
            if (now != shown)
            {
                shown = now;
                repaint();
            }
        }

    protected:
        void paintGraph (juce::Graphics& g, juce::Rectangle<float> area) override
        {
            const int type = std::clamp (juce::roundToInt (valueOf (host, "character.type")), 0, 4);
            Shaping shaping;
            shaping.filterMinHz = valueOf (host, "character.min");
            shaping.filterMaxHz = valueOf (host, "character.max");
            const double position = 0.01 * valueOf (host, "character");
            const double fc = std::exp2 (shaping::cutoffOctaves (shaping, position));
            const double resonance = 0.01 * valueOf (host, "character.resonance");
            const double env = 0.01 * valueOf (host, "character.envAmount");
            const double tilt = (position - 0.5) * 24.0;
            const bool isTilt = type == static_cast<int> (FilterType::tilt);

            const auto header = juce::Rectangle<float> (area.getX() + 9.0f, area.getY() + 4.0f, area.getWidth() - 18.0f, 12.0f);
            auto plot = area.reduced (12.0f, 0.0f).withTrimmedTop (22.0f).withTrimmedBottom (18.0f);
            constexpr double topDb = 18.0, bottomDb = -42.0;
            auto xAt = [&] (double f) { return plot.getX() + static_cast<float> (std::log (f / 20.0) / std::log (1000.0)) * plot.getWidth(); };
            auto yAt = [&] (double db) { return plot.getY() + static_cast<float> ((topDb - std::clamp (db, bottomDb - 6.0, topDb)) / (topDb - bottomDb)) * plot.getHeight(); };

            // The header: the type, and where the filter sits.
            auto* typeParam = host.apvts().getParameter ("character.type");
            g.setFont (fonts::make (10.0f, fonts::Weight::regular, 0.02f));
            g.setColour (juce::Colour (0xd9c2c7c5));
            g.drawText (typeParam != nullptr ? typeParam->getCurrentValueAsText().toUpperCase() : juce::String(), header, juce::Justification::centredLeft, false);
            g.drawText (isTilt ? "TILT " + juce::String (tilt, 1) + " dB"
                               : "CUTOFF " + format::hertz (fc) + juce::String::fromUTF8 (" \xc2\xb7 RESO ")
                                     + juce::String (juce::roundToInt (100.0 * resonance)) + " %",
                        header, juce::Justification::centredRight, false);

            // Grid: decades, the 0 dB line.
            g.setFont (fonts::make (9.0f, fonts::Weight::medium, 0.06f));
            for (const auto& [f, label] : { std::pair { 100.0, "100 Hz" }, std::pair { 1000.0, "1 kHz" }, std::pair { 10000.0, "10 kHz" } })
            {
                g.setColour (juce::Colour (0x0dffffff));
                g.drawVerticalLine (juce::roundToInt (xAt (f)), plot.getY(), plot.getBottom());
                g.setColour (juce::Colour (0xcc9aa09e));
                g.drawText (label, juce::Rectangle<float> (xAt (f) - 30.0f, plot.getBottom() + 3.0f, 60.0f, 11.0f), juce::Justification::centred, false);
            }
            g.setColour (juce::Colour (0x12ffffff));
            g.drawHorizontalLine (juce::roundToInt (yAt (0.0)), plot.getX(), plot.getRight());

            auto curve = [&] (double cutoff) {
                juce::Path p;
                for (int i = 0; i <= 220; ++i)
                {
                    const double f = 20.0 * std::pow (1000.0, i / 220.0);
                    const float x = xAt (f), y = yAt (characterResponseDb (type, f, cutoff, resonance, tilt));
                    if (i == 0)
                        p.startNewSubPath (x, y);
                    else
                        p.lineTo (x, y);
                }
                return p;
            };
            // The envelope's peak, dashed (where a note's filter sweeps from).
            if (std::abs (env) > 0.01 && ! isTilt)
            {
                const double peak = std::clamp (fc * std::exp2 (shaping::envelopeOctaves (env)), 20.0, 20000.0);
                juce::Path dashed;
                const float dashes[] = { 3.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, curve (peak), dashes, 2);
                g.setColour (ink.withAlpha (0.4f));
                g.fillPath (dashed);
            }
            const auto response = curve (fc);
            juce::Path under (response);
            under.lineTo (plot.getRight(), plot.getBottom());
            under.lineTo (plot.getX(), plot.getBottom());
            under.closeSubPath();
            g.setColour (ink.withAlpha (0.12f));
            g.fillPath (under);
            g.setColour (ink);
            g.strokePath (response, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // The cutoff (TILT: its pivot).
            const double markerHz = isTilt ? 800.0 : fc;
            const float mx = xAt (markerHz), my = yAt (characterResponseDb (type, markerHz, fc, resonance, tilt));
            g.setColour (juce::Colour (0xff1c2528));
            g.fillEllipse (mx - 4.0f, my - 4.0f, 8.0f, 8.0f);
            g.setColour (ink);
            g.drawEllipse (mx - 4.0f, my - 4.0f, 8.0f, 8.0f, 1.5f);
        }

    private:
        InstrumentHost& host;
        juce::Colour ink;
        std::array<float, 6> shown {};
    };

    class CharacterRack final : public ParameterRack
    {
    public:
        explicit CharacterRack (InstrumentHost& h) : ParameterRack (h, "CHARACTER", design::colour::macro (design::colour::Macro::character), 0.68f)
        {
            // ANDOR ANALOG's FILTER rack's face: the voice filter's own sage enamel and its
            // resonant motif (synthIdentity), the graph's glass from the identity.
            const auto& identity = synthIdentity (SynthRack::voiceFilter);
            setFaceplate (faceplateFromIdentity (identity));
            setAccent (identity.motifInk);
            auto graph = std::make_unique<CharacterGraph> (h, identity.motifInk);
            graph->setGlass (identity.glassTop, identity.glassBottom, identity.edge);
            setGraph (std::move (graph));
        }

    protected:
        void build (int) override
        {
            selectorList ("character.type", "TYPE", 96, "The filter: LP24 (four-pole low-pass), LP12, HP12, BP12, or a TILT of the whole spectrum");
            knob ("character", "CHARACTER", percentText());
            knob ("character.min", "MIN", hertzText());
            knob ("character.max", "MAX", hertzText());
            knob ("character.resonance", "RESO", percentText());
            knob ("character.drive", "DRIVE", percentText());
            group ("ENVELOPE");
            knob ("character.envAmount", "ENV", bipolarText());
            knob ("character.envAttack", "ATTACK", msText());
            knob ("character.envDecay", "DECAY", msText());
        }
    };
}

std::unique_ptr<rack::Module> createCharacterRack (InstrumentHost& host)
{
    return std::make_unique<CharacterRack> (host);
}

} // namespace osp::plugin
