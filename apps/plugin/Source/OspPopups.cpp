#include "OspPopups.h"

#include "Design.h"
#include "EngineCard.h"
#include "PluginProcessor.h"

#include "engine/Shaping.h"

namespace osp::plugin
{

namespace
{
    juce::String parameterText (juce::RangedAudioParameter* p)
    {
        if (p == nullptr)
            return {};
        const auto label = p->getLabel();
        return p->getCurrentValueAsText() + (label.isNotEmpty() ? " " + label : juce::String());
    }

    /** Advanced (spec §13): what a musician rarely needs, out of the way. */
    class AdvancedPopup final : public MiniPanel
    {
    public:
        explicit AdvancedPopup (OspAudioProcessor& p) : MiniPanel ("ADVANCED", "TUNING, VOICES & DYNAMICS"), processor (p)
        {
            auto& state = processor.parameters;
            auto add = [this, &state] (const char* id, const char* caption, MiniKnob::Formatter f) {
                knobs.push_back (std::make_unique<MiniKnob> (state, id, caption, std::move (f)));
                knobs.back()->setBoxed (true);
                addAndMakeVisible (*knobs.back());
            };
            // OUTPUT is the header's VOLUME, sustain each layer's LOOP: Advanced keeps what a
            // musician rarely needs, and DYNAMICS (how touch changes the sound) since DRIVE took
            // its place on the panel.
            auto* fine = state.getParameter ("fineTune");
            add ("fineTune", "FINE", [fine] (double) { return parameterText (fine); });
            add ("bendRange", "BEND", [] (double v) { return juce::String (juce::roundToInt (v)) + " st"; });
            add ("glide", "GLIDE", [] (double ms) { return ms < 0.5 ? juce::String ("Off") : format::milliseconds (ms); });
            glide = knobs.back().get();

            // Mono: one note at a time, legato (no restart while a key is held), last key
            // wins, GLIDE slides between notes. For basses and leads.
            voices = std::make_unique<SegmentedControl> (*state.getParameter ("voiceMode"), juce::StringArray { "POLY", "MONO" });
            voices->setTooltip ("Mono: one note at a time; a key played while another is held slides to it (legato) over GLIDE");
            voices->onChange = [this] (int) { updateGlide(); };
            addAndMakeVisible (*voices);
            updateGlide();

            pitch = std::make_unique<SegmentedControl> (*state.getParameter ("pitchCharacter"), juce::StringArray { "TAPE", "NATURAL" });
            pitch->setTooltip ("Pitch character: tape-like resampling or natural (formants kept)");
            addAndMakeVisible (*pitch);

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

            // DYNAMICS: the same parameters as ever (its amount, curve, range and tone).
            curve = std::make_unique<SegmentedControl> (*state.getParameter ("dynamics.curve"), juce::StringArray { "SOFT", "LINEAR", "HARD" });
            curve->setTooltip ("Velocity curve: soft reaches loud easily, hard needs a firm touch");
            addAndMakeVisible (*curve);
            auto addDynamics = [this, &state] (const char* id, const char* caption, MiniKnob::Formatter f) {
                dynamicsKnobs.push_back (std::make_unique<MiniKnob> (state, id, caption, std::move (f)));
                dynamicsKnobs.back()->setBoxed (true);
                addAndMakeVisible (*dynamicsKnobs.back());
            };
            auto percent = [] (double v) { return format::percent (v) + " %"; };
            addDynamics ("dynamics", "AMOUNT", percent);
            addDynamics ("velocityRange", "RANGE", [] (double v) { return juce::String (v, 0) + " dB"; });
            addDynamics ("dynamics.tone", "TONE", percent);
        }

        // Laid out at 0.69 of the reference's unit; the editor scales it up to match the macro popups.
        float unit() const override { return 0.69f; }
        juce::Point<int> cardSize() const override
        {
            return { 300, headerHeight() + knobRow + 8 + 12 + 24 + 8 + 12 + 24 + 10 + 12 + 24 + 6 + knobRow + 10 + 26 + juce::roundToInt (26.0f * unit()) };
        }

        void paint (juce::Graphics& g) override
        {
            MiniPanel::paint (g);
            g.setColour (palette::textDim);
            g.setFont (fonts::label (9.5f));
            g.drawText ("VOICES", voicesCaption, juce::Justification::centredLeft, false);
            g.drawText ("PITCH CHARACTER", pitchCaption, juce::Justification::centredLeft, false);
            g.drawText ("DYNAMICS", dynamicsCaption, juce::Justification::centredLeft, false);
            if (auto* seed = processor.parameters.getParameter ("seed"))
                g.drawText ("SEED " + seed->getCurrentValueAsText(), seedCaption, juce::Justification::centredRight, false);
        }

    private:
        void layoutContent (juce::Rectangle<int> area) override
        {
            auto row = area.removeFromTop (knobRow);
            const int cell = row.getWidth() / static_cast<int> (knobs.size());
            for (auto& k : knobs)
                k->setBounds (row.removeFromLeft (cell));
            area.removeFromTop (8);
            voicesCaption = area.removeFromTop (12);
            voices->setBounds (area.removeFromTop (24));
            area.removeFromTop (8);
            pitchCaption = area.removeFromTop (12);
            pitch->setBounds (area.removeFromTop (24));
            area.removeFromTop (10);
            dynamicsCaption = area.removeFromTop (12);
            curve->setBounds (area.removeFromTop (24));
            area.removeFromTop (6);
            auto dynamicsRow = area.removeFromTop (knobRow);
            const int dynamicsCell = dynamicsRow.getWidth() / static_cast<int> (dynamicsKnobs.size());
            for (auto& k : dynamicsKnobs)
                k->setBounds (dynamicsRow.removeFromLeft (dynamicsCell));
            area.removeFromTop (10);
            auto last = area.removeFromTop (26);
            mpe.setBounds (last.removeFromLeft (80));
            reseed.setBounds (last.removeFromRight (90));
            last.removeFromRight (8);
            seedCaption = last;
        }

        static constexpr int knobRow = 104;   ///< the popups' boxed knob cells, at this panel's unit

        /** GLIDE only acts in Mono: dimmed (still adjustable) in Poly. */
        void updateGlide()
        {
            if (glide != nullptr && voices != nullptr)
                glide->setAlpha (voices->selected() == 1 ? 1.0f : 0.45f);
        }

        OspAudioProcessor& processor;
        std::vector<std::unique_ptr<MiniKnob>> knobs;
        MiniKnob* glide = nullptr;
        std::unique_ptr<SegmentedControl> voices, pitch, curve;
        std::vector<std::unique_ptr<MiniKnob>> dynamicsKnobs;
        juce::ToggleButton mpe { "MPE" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mpeAttachment;
        juce::TextButton reseed { "Reseed" };
        juce::Rectangle<int> voicesCaption, pitchCaption, dynamicsCaption, seedCaption;
    };
}

const juce::StringArray& popupParameterIds (MacroPopup macro)
{
    static const juce::StringArray life { "life.mode", "life.pitch", "life.tone", "life.attack" };
    static const juce::StringArray drive { "drive.mode", "drive.tone", "drive.body" };
    static const juce::StringArray character { "character.type", "character.min", "character.max", "character.resonance",
                                               "character.drive", "character.envAmount", "character.envAttack", "character.envDecay" };
    static const juce::StringArray movement { "movement.mode", "movement.drift.speed", "movement.drift.pitch", "movement.drift.tone",
                                              "movement.tape.wow", "movement.tape.flutter", "movement.tape.wear",
                                              "movement.chorus.rate", "movement.chorus.width", "movement.chorus.stereo",
                                              "movement.pulse.rate", "movement.pulse.shape", "movement.pulse.stereo",
                                              "movement.shaper.pattern", "movement.shaper.rate", "movement.shaper.target",
                                              "movement.shaper.smooth", "movement.shaper.custom" };
    static const juce::StringArray space { "space.type", "space.decay", "space.preDelay", "space.size", "space.damping",
                                           "space.modulation", "space.width", "space.lowCut", "space.highCut" };
    static const juce::StringArray echo { "echo.type", "echo.sync", "echo.division", "echo.time", "echo.feedback",
                                          "echo.tone", "echo.age", "echo.stereo" };
    switch (macro)
    {
        case MacroPopup::life: return life;
        case MacroPopup::drive: return drive;
        case MacroPopup::character: return character;
        case MacroPopup::movement: return movement;
        case MacroPopup::echo: return echo;
        case MacroPopup::space: break;
    }
    return space;
}

std::unique_ptr<MiniPanel> createAdvancedPopup (OspAudioProcessor& processor)
{
    return std::make_unique<AdvancedPopup> (processor);
}

} // namespace osp::plugin
