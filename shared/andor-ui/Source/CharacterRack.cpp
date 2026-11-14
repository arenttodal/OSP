#include "CharacterRack.h"

#include "Design.h"
#include "FxIdentity.h"
#include "MacroPopups.h"
#include "ParameterRack.h"
#include "ShapingPopups.h"

#include "engine/Shaping.h"

#include <algorithm>
#include <array>
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

    juce::String middleDot() { return juce::String::fromUTF8 (" \xc2\xb7 "); }

    juce::String noteName (int note)
    {
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1);
    }

    /** The filter's response, following what the filter does (as ANDOR ANALOG's FILTER graph
        follows its Synth::FilterTap). LIVE: the most recent sounding voice's cutoff from the
        engine's CharacterTaps - CHARACTER, its routes, velocity, drift and the envelope as the
        voice uses them. PREVIEW: when CHARACTER, its range or its envelope is edited with nothing
        playing, a silent notional C4 at velocity 100 through the same AD envelope (never
        heard). IDLE: the knobs' position, the envelope's reach dashed. The drawn cutoff eases
        towards the truth (drawing only), so a fast attack still reads as a jump. */
    class CharacterGraph final : public GraphFrame
    {
    public:
        CharacterGraph (InstrumentHost& h, juce::Colour inkColour) : host (h), ink (inkColour) {}

        CharacterGraphState state() const
        {
            CharacterGraphState out;
            out.mode = shownMode;
            out.note = shownNote;
            out.velocity = shownVelocity;
            out.stage = shownStage;
            out.baseHz = std::exp2 (baseOct);
            out.nowHz = std::exp2 (nowOct);
            out.drawnHz = std::exp2 (drawnOct);
            out.reachHz = std::exp2 (reachOct);
            out.peakHz = std::exp2 (peakOct);
            return out;
        }

        void refresh() override
        {
            const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
            const double dt = lastRefresh > 0.0 ? std::clamp (now - lastRefresh, 0.0, 0.2) : 0.0;
            lastRefresh = now;
            advance (dt);
            const std::array<float, 7> settings { valueOf (host, "character.type"), valueOf (host, "character"), valueOf (host, "character.min"),
                                                  valueOf (host, "character.max"), valueOf (host, "character.resonance"), valueOf (host, "character.envAmount"),
                                                  static_cast<float> (shownMode) };
            if (settings != shownSettings)
            {
                shownSettings = settings;
                repaint();
            }
            else if (shownMode != CharacterGraphState::Mode::idle || settling)
                repaint();   // (moving: every frame; idle: only when something changed)
        }

        /** Tests: one refresh as if `seconds` had passed (no message loop). */
        void advanceBy (double seconds)
        {
            lastRefresh = juce::Time::getMillisecondCounterHiRes() * 0.001;
            advance (seconds);
        }

    protected:
        void paintGraph (juce::Graphics& g, juce::Rectangle<float> area) override
        {
            if (! advancedOnce)
                advance (0.0);   // (a first paint: the state now)
            const int type = std::clamp (juce::roundToInt (valueOf (host, "character.type")), 0, 4);
            const double resonance = 0.01 * valueOf (host, "character.resonance");
            const double knobTilt = (0.01 * valueOf (host, "character") - 0.5) * 24.0;
            const bool isTilt = type == static_cast<int> (FilterType::tilt);
            const bool idle = shownMode == CharacterGraphState::Mode::idle;
            const bool preview = shownMode == CharacterGraphState::Mode::preview;
            const auto drawInk = preview ? ink.withMultipliedSaturation (0.7f) : ink;

            const auto header = juce::Rectangle<float> (area.getX() + 9.0f, area.getY() + 4.0f, area.getWidth() - 18.0f, 12.0f);
            auto plot = area.reduced (12.0f, 0.0f).withTrimmedTop (22.0f).withTrimmedBottom (30.0f);
            constexpr double topDb = 18.0, bottomDb = -42.0;
            auto xAt = [&] (double f) { return plot.getX() + static_cast<float> (std::log (std::clamp (f, 20.0, 20000.0) / 20.0) / std::log (1000.0)) * plot.getWidth(); };
            auto yAt = [&] (double db) { return plot.getY() + static_cast<float> ((topDb - std::clamp (db, bottomDb, topDb)) / (topDb - bottomDb)) * plot.getHeight(); };
            auto hzText = [] (double hz) { return format::hertz (hz); };
            // TILT has no cutoff: the envelope, velocity and drift lean it (3 dB an octave, as
            // the voice does), around the knob's tilt.
            auto tiltAt = [&] (double octaves) { return knobTilt + 3.0 * (octaves - restOct); };

            // The header: the type, and what is shown (LIVE / PREVIEW / the settings).
            auto* typeParam = host.apvts().getParameter ("character.type");
            g.setFont (fonts::make (10.0f, fonts::Weight::regular, 0.02f));
            g.setColour (juce::Colour (0xd9c2c7c5));
            g.drawText (typeParam != nullptr ? typeParam->getCurrentValueAsText().toUpperCase() : juce::String(), header, juce::Justification::centredLeft, false);
            juce::String state;
            if (shownMode == CharacterGraphState::Mode::live)
                state = "LIVE " + noteName (shownNote) + middleDot() + "VEL " + juce::String (shownVelocity);
            else if (preview)
                state = "PREVIEW C4" + middleDot() + "VEL 100" + middleDot() + "SILENT";
            else
                state = isTilt ? "TILT " + juce::String (knobTilt, 1) + " dB"
                               : "CUTOFF " + hzText (std::exp2 (drawnOct)) + middleDot() + "RESO " + juce::String (juce::roundToInt (100.0 * resonance)) + " %";
            g.drawText (state, header, juce::Justification::centredRight, false);

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

            auto curve = [&] (double octaves) {
                const double cutoff = std::exp2 (octaves);
                const double tilt = tiltAt (octaves);
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
            const bool reaches = std::abs (reachOct - baseOct) > 0.01;
            // 1. The envelope's reach, dashed (where a note's filter sweeps to at its top).
            if (reaches)
            {
                juce::Path dashed;
                const float dashes[] = { 3.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, curve (reachOct), dashes, 2);
                g.setColour (drawInk.withAlpha (0.4f));
                g.fillPath (dashed);
            }
            // 2. The base (before the envelope), muted under the moving one; at rest it IS the curve.
            if (! idle)
            {
                g.setColour (juce::Colour (0xffd9dcd8).withAlpha (0.3f));
                g.strokePath (curve (baseOct), juce::PathStrokeType (1.0f));
            }
            // 3. Now: the response at the cutoff the filter uses (LIVE), or the preview's.
            const auto response = curve (drawnOct);
            juce::Path under (response);
            under.lineTo (plot.getRight(), plot.getBottom());
            under.lineTo (plot.getX(), plot.getBottom());
            under.closeSubPath();
            g.setColour (drawInk.withAlpha (idle ? 0.12f : 0.18f));
            g.fillPath (under);
            if (! idle)
            {
                g.setColour (drawInk.withAlpha (0.25f));
                g.strokePath (response, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (drawInk);
            g.strokePath (response, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // The cutoff (TILT: its pivot).
            const double drawnHz = std::exp2 (drawnOct);
            const double markerHz = isTilt ? 800.0 : drawnHz;
            const float mx = xAt (markerHz), my = yAt (characterResponseDb (type, markerHz, drawnHz, resonance, tiltAt (drawnOct)));
            g.setColour (juce::Colour (0xff1c2528));
            g.fillEllipse (mx - 4.0f, my - 4.0f, 8.0f, 8.0f);
            g.setColour (drawInk);
            g.drawEllipse (mx - 4.0f, my - 4.0f, 8.0f, 8.0f, 1.5f);

            // Foot: the cutoffs (BASE, NOW, the envelope's PEAK or REACH) and the stage (A, D).
            if (! isTilt)
            {
                juce::String figures = "BASE " + hzText (std::exp2 (baseOct));
                if (! idle)
                    figures << "   NOW " << hzText (drawnHz);
                if (reaches)
                    figures << "   " << (shownMode == CharacterGraphState::Mode::live ? "PEAK " + hzText (std::exp2 (peakOct)) : "REACH " + hzText (std::exp2 (reachOct)));
                g.setFont (fonts::make (9.0f, fonts::Weight::medium, 0.06f));
                g.setColour (juce::Colour (0xcc9aa09e));
                g.drawText (figures, juce::Rectangle<float> (area.getX() + 9.0f, area.getBottom() - 15.0f, area.getWidth() - 60.0f, 12.0f), juce::Justification::centredLeft, false);
            }
            if (! idle)
            {
                static const char* letters[] { "A", "D" };
                const float w = 11.0f;
                float x = area.getRight() - 9.0f - 2.0f * w;
                g.setFont (fonts::make (8.5f, fonts::Weight::semibold, 0.04f));
                for (int i = 0; i < 2; ++i, x += w)
                {
                    g.setColour (shownStage == i + 1 ? drawInk : juce::Colour (0xffd9dcd8).withAlpha (0.35f));
                    g.drawText (letters[i], juce::Rectangle<float> (x, area.getBottom() - 15.0f, w, 12.0f), juce::Justification::centred, false);
                }
            }
        }

    private:
        InstrumentHost& host;
        juce::Colour ink;
        std::array<float, 7> shownSettings {};
        CharacterGraphState::Mode shownMode = CharacterGraphState::Mode::idle;
        int shownNote = 60, shownVelocity = 100, shownStage = 0;
        double baseOct = 10.0, nowOct = 10.0, reachOct = 10.0, peakOct = 10.0, restOct = 10.0, drawnOct = 10.0;
        double lastRefresh = 0.0, previewTime = 0.0, previewEnv = 0.0;
        bool advancedOnce = false, settling = false, previewing = false, previewAttacking = true;
        int trackedSlot = -1;
        std::uint64_t trackedOrder = 0;
        std::array<float, 6> watched {};
        bool watching = false;

        static constexpr double lowOct = 4.321928094887362, highOct = 14.287712379549449;   // log2 20 Hz, log2 20 kHz
        static double clampOct (double o) { return std::clamp (o, lowOct, highOct); }

        void startPreview()
        {
            previewing = true;
            previewTime = 0.0;
            previewEnv = 0.0;
            previewAttacking = true;
        }

        void advance (double dt)
        {
            advancedOnce = true;
            Shaping shaping;
            shaping.filterMinHz = valueOf (host, "character.min");
            shaping.filterMaxHz = valueOf (host, "character.max");
            restOct = shaping::cutoffOctaves (shaping, 0.01 * valueOf (host, "character"));
            const double depth = shaping::envelopeOctaves (0.01 * valueOf (host, "character.envAmount"));
            const double attack = std::max (0.0005, 0.001 * valueOf (host, "character.envAttack"));
            const double decay = std::max (0.02, 0.001 * valueOf (host, "character.envDecay"));

            // A live voice: the most recent note-on that sounds through CHARACTER. A newer note
            // takes over; the one followed is kept through its release; when it ends, the next.
            CharacterTap::Snapshot tap, best, tracked;
            bool found = false, trackedAlive = false;
            int bestSlot = -1;
            if (const auto* engine = host.telemetryEngine(); engine != nullptr)
                for (int slot = 0; slot < InstrumentEngine::characterTapCount(); ++slot)
                    if (engine->characterTap (slot).read (tap) && tap.active)
                    {
                        if (slot == trackedSlot && tap.order == trackedOrder)
                        {
                            tracked = tap;
                            trackedAlive = true;
                        }
                        if (! found || tap.order > best.order)
                        {
                            best = tap;
                            bestSlot = slot;
                            found = true;
                        }
                    }
            if (found && trackedAlive && best.order <= trackedOrder)
                best = tracked;   // (nothing newer: keep following it, released or not)
            else if (found)
            {
                trackedSlot = bestSlot;
                trackedOrder = best.order;
            }

            // What starts a preview (with nothing playing): CHARACTER, its range, its envelope.
            const std::array<float, 6> watch { valueOf (host, "character"), valueOf (host, "character.min"), valueOf (host, "character.max"),
                                               valueOf (host, "character.envAmount"), valueOf (host, "character.envAttack"), valueOf (host, "character.envDecay") };
            if (watch != watched)
            {
                const bool first = ! watching;
                watched = watch;
                watching = true;
                if (! first && ! found && std::abs (depth) > 0.01)
                    startPreview();   // (restarts from its note-on: the new values, never the old run)
            }

            if (found)
            {
                previewing = false;
                shownMode = CharacterGraphState::Mode::live;
                baseOct = clampOct (best.baseOctaves);
                nowOct = clampOct (best.cutoffOctaves);
                reachOct = clampOct (best.baseOctaves + best.depthOctaves);
                peakOct = clampOct (best.baseOctaves + best.envPeak * best.depthOctaves);
                shownNote = best.note;
                shownVelocity = juce::roundToInt (best.velocity * 127.0f);
                shownStage = best.stage;
            }
            else if (previewing)
            {
                // The voice's own AD envelope: a linear rise over ATTACK, then back to rest
                // over DECAY (exponential, ~1 % left at its end).
                double left = dt;
                if (previewAttacking && left > 0.0)
                {
                    const double rise = std::min (left, (1.0 - previewEnv) * attack);
                    previewEnv = std::min (1.0, previewEnv + rise / attack);
                    left -= rise;
                    if (previewEnv >= 1.0)
                        previewAttacking = false;
                }
                if (! previewAttacking && left > 0.0)
                    previewEnv *= std::exp (-4.6 * left / decay);
                previewTime += dt;
                shownMode = CharacterGraphState::Mode::preview;
                baseOct = clampOct (restOct);
                nowOct = clampOct (restOct + previewEnv * depth);
                reachOct = peakOct = clampOct (restOct + depth);
                shownNote = 60;
                shownVelocity = 100;
                shownStage = previewAttacking ? 1 : (previewEnv > 0.01 ? 2 : 0);
                if ((! previewAttacking && previewEnv <= 0.01) || previewTime > 12.0)
                    previewing = false;
            }
            else
            {
                shownMode = CharacterGraphState::Mode::idle;
                baseOct = nowOct = clampOct (restOct);
                reachOct = peakOct = clampOct (restOct + depth);
                shownStage = 0;
            }
            // Drawing only: ease the drawn cutoff (about 45 ms); settle when it arrives.
            if (dt <= 0.0)
                drawnOct = nowOct;
            else
                drawnOct += (1.0 - std::exp (-dt / 0.045)) * (nowOct - drawnOct);
            settling = std::abs (drawnOct - nowOct) > 0.003;
            if (! settling)
                drawnOct = nowOct;
        }
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
            characterGraph = graph.get();
            setGraph (std::move (graph));
        }

        CharacterGraph* characterGraph = nullptr;

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

std::optional<CharacterGraphState> characterGraphState (rack::Module& module)
{
    if (auto* rack = dynamic_cast<CharacterRack*> (&module); rack != nullptr && rack->characterGraph != nullptr)
        return rack->characterGraph->state();
    return std::nullopt;
}

void advanceCharacterGraph (rack::Module& module, double seconds)
{
    if (auto* rack = dynamic_cast<CharacterRack*> (&module); rack != nullptr && rack->characterGraph != nullptr)
        rack->characterGraph->advanceBy (seconds);
}

} // namespace osp::plugin
