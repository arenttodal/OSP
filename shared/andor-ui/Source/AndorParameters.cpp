#include "AndorParameters.h"
#include "InstrumentHost.h"
#include "ValueFormat.h"

#include "engine/PerformanceEngine.h"
#include "engine/RhythmicShaper.h"
#include "engine/Shaping.h"

namespace osp::plugin
{

const juce::StringArray& InstrumentHost::modDestinationNames()
{
    // The registry's order (append only: sessions store the index).
    static const juce::StringArray names = [] {
        juce::StringArray n;
        n.add ("None");
        for (int d = 1; d < mod::hostChoiceCount(); ++d)   // (a product's own after ANDOR OSP's)
            n.add (mod::destInfo (mod::destFromChoice (d)).name);
        return n;
    }();
    return names;
}

const juce::StringArray& InstrumentHost::modExtendedDestinationNames()
{
    // mod::extendedDests' order (append only).
    static const juce::StringArray names = [] {
        juce::StringArray n;
        n.add ("None");
        for (const auto d : mod::extendedDests)
            n.add (mod::destInfo (d).name);
        return n;
    }();
    return names;
}

mod::Dest InstrumentHost::routeDest (int slot) const
{
    if (slot < 0 || slot >= mod::maxRoutes)
        return mod::Dest::none;
    return mod::routeDestFromChoices (juce::roundToInt (parameterValue (modRouteId (slot, "dest"))), juce::roundToInt (parameterValue (modRouteId (slot, "destExt"))));
}

void InstrumentHost::setRouteDest (int slot, mod::Dest dest)
{
    // The frozen list when it holds the destination, else the second parameter (and the
    // first one cleared): one of the two is set, never both.
    const int choice = mod::choiceOf (dest);
    const int extended = choice < 0 ? mod::extendedChoiceOf (dest) : 0;
    setParameterValue (modRouteId (slot, "dest"), static_cast<float> (std::max (0, choice)));
    setParameterValue (modRouteId (slot, "destExt"), static_cast<float> (extended));
}

juce::AudioProcessorValueTreeState::ParameterLayout InstrumentHost::createParameterLayout()
{
    using Range = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto ms = juce::AudioParameterFloatAttributes().withLabel ("ms");
    auto db = juce::AudioParameterFloatAttributes().withLabel ("dB");

    Range attackRange (0.0f, 2000.0f, 0.1f);
    attackRange.setSkewForCentre (50.0f);
    Range releaseRange (20.0f, 15000.0f, 0.1f);
    releaseRange.setSkewForCentre (700.0f);

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::attack, 1 }, "Attack", attackRange, 2.0f, ms));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::release, 1 }, "Release", releaseRange, 700.0f, ms));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::gain, 1 }, "Output", Range (-36.0f, 12.0f, 0.1f), 0.0f, db));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::velocityRange, 1 }, "Velocity Range",
                                                             Range (0.0f, 48.0f, 0.1f), 30.0f, db));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::fineTune, 1 }, "Fine Tune",
                                                             Range (-100.0f, 100.0f, 0.1f), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("cents")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::bendRange, 1 }, "Bend Range",
                                                             Range (0.0f, 24.0f, 1.0f), 2.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("st")));

    // Musician-facing macros (spec §11, §12), 0..100 %.
    auto percent = juce::AudioParameterFloatAttributes().withLabel ("%");
    const Range unit (0.0f, 100.0f, 0.1f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::life, 2 }, "Life", unit, 50.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::dynamics, 2 }, "Dynamics", unit, 65.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::character, 2 }, "Character", unit, 90.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::motion, 2 }, "Movement", unit, 12.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::space, 2 }, "Space", unit, 10.0f, percent));
    // Original <-> Reimagined: layer A's REIMAGINED (and, in sessions from before the layers,
    // the instrument's one amount; the ID never changes). New patches start at the Original end.
    const auto reimaginedText = juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) {
        return juce::String (juce::roundToInt (v)) + " %";
    });
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::reimagined, 2 }, "A Reimagined", unit, 0.0f, reimaginedText));
    // Advanced (spec §13).
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::pitchCharacter, 2 }, "Pitch Character",
                                                              juce::StringArray { "Tape", "Natural" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::sustain, 2 }, "Sustain",
                                                              juce::StringArray { "Recording", "Endless" }, 1));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ids::seed, 2 }, "Variation Seed", 1, 9999, 1));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::mpe, 3 }, "MPE", false));

    // Shaping system v1.0 (version hint 4): the popups behind the macros.
    const Shaping d;
    auto hz = juce::AudioParameterFloatAttributes().withLabel ("Hz");
    auto cents = juce::AudioParameterFloatAttributes().withLabel ("cents");
    auto seconds = juce::AudioParameterFloatAttributes().withLabel ("s");
    auto choice = [&layout] (const juce::String& id, const juce::String& name, juce::StringArray items, int def) {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 4 }, name, items, def));
    };
    auto number = [&layout] (const juce::String& id, const juce::String& name, Range range, float def, juce::AudioParameterFloatAttributes attr) {
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 4 }, name, range, def, attr));
    };
    auto skewed = [] (float lo, float hi, float centre) {
        Range r (lo, hi, 0.0f);
        r.setSkewForCentre (centre);
        return r;
    };
    choice (ids::lifeMode, "Life Mode", { "Natural", "Loose", "Fray" }, 0);
    number (ids::lifePitch, "Life Pitch", skewed (0.0f, 15.0f, 4.0f), static_cast<float> (d.lifePitchCents), cents);
    number (ids::lifeTone, "Life Tone", unit, static_cast<float> (100.0 * d.lifeTone), percent);
    number (ids::lifeAttack, "Life Attack", unit, static_cast<float> (100.0 * d.lifeAttack), percent);
    choice (ids::dynamicsCurve, "Dynamics Curve", { "Soft", "Linear", "Hard" }, 1);
    number (ids::dynamicsTone, "Dynamics Tone", unit, static_cast<float> (100.0 * d.dynamicsTone), percent);
    choice (ids::characterType, "Character Filter", { "LP24", "LP12", "HP12", "BP12", "Tilt" }, 0);
    number (ids::characterMin, "Character Min", skewed (20.0f, 20000.0f, 632.0f), static_cast<float> (d.filterMinHz), hz);
    number (ids::characterMax, "Character Max", skewed (20.0f, 20000.0f, 632.0f), static_cast<float> (d.filterMaxHz), hz);
    number (ids::characterResonance, "Character Resonance", Range (0.0f, 90.0f, 0.0f), static_cast<float> (100.0 * d.resonance), percent);
    number (ids::characterDrive, "Character Drive", unit, static_cast<float> (100.0 * d.drive), percent);
    number (ids::characterEnvAmount, "Character Envelope", Range (-100.0f, 100.0f, 0.0f), static_cast<float> (100.0 * d.envAmount), percent);
    number (ids::characterEnvAttack, "Character Env Attack", skewed (0.0f, 2000.0f, 60.0f), static_cast<float> (1000.0 * d.envAttackSeconds), ms);
    number (ids::characterEnvDecay, "Character Env Decay", skewed (20.0f, 12000.0f, 700.0f), static_cast<float> (1000.0 * d.envDecaySeconds), ms);
    choice (ids::movementMode, "Movement Mode", { "Drift", "Tape", "Chorus", "Pulse", "Shaper" }, 0);
    choice (ids::spaceType, "Space Type", { "Room", "Hall", "Plate", "Spring" }, 2);   // HALL took CHAMBER's place (v12)
    number (ids::spaceDecay, "Space Decay", skewed (0.2f, 8.0f, 1.8f), static_cast<float> (d.spaceDecaySeconds), seconds);

    // MOVEMENT v2 (version hint 6): every mode's own settings.
    auto movementNumber = [&layout, &unit, &percent] (const juce::String& id, const juce::String& name, double value) {
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 6 }, name, unit, static_cast<float> (100.0 * value), percent));
    };
    movementNumber (ids::driftSpeed, "Drift Speed", d.driftSpeed);
    movementNumber (ids::driftPitch, "Drift Pitch", d.driftPitch);
    movementNumber (ids::driftTone, "Drift Tone", d.driftTone);
    movementNumber (ids::tapeWow, "Tape Wow", d.tapeWow);
    movementNumber (ids::tapeFlutter, "Tape Flutter", d.tapeFlutter);
    movementNumber (ids::tapeWear, "Tape Wear", d.tapeWear);
    movementNumber (ids::chorusRate, "Chorus Rate", d.chorusRate);
    movementNumber (ids::chorusWidth, "Chorus Width", d.chorusWidth);
    movementNumber (ids::chorusStereo, "Chorus Stereo", d.chorusStereo);
    movementNumber (ids::pulseRate, "Pulse Rate", d.pulseRate);
    movementNumber (ids::pulseShape, "Pulse Shape", d.pulseShape);
    movementNumber (ids::pulseStereo, "Pulse Stereo", d.pulseStereo);
    {
        juce::StringArray patterns;
        for (int i = 0; i < RhythmicShaper::patternCount; ++i)
            patterns.add (RhythmicShaper::patternName (i));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::shaperPattern, 6 }, "Shaper Pattern", patterns, d.shaper.pattern));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::shaperRate, 6 }, "Shaper Rate",
                                                                  juce::StringArray { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, static_cast<int> (d.shaper.rate)));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::shaperTarget, 6 }, "Shaper Target",
                                                                  juce::StringArray { "Vol", "Filter", "Both" }, static_cast<int> (d.shaper.target)));
        movementNumber (ids::shaperSmooth, "Shaper Smooth", d.shaper.smooth);
    }

    // A/B layers (version hint 5): the blend, then each layer's source mode and GRANULAR settings.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::blend, 5 }, "A/B Blend", Range (0.0f, 1.0f, 0.0f), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
                                                                 return v <= 0.0f ? juce::String ("A") : (v >= 1.0f ? juce::String ("B") : "A " + juce::String (juce::roundToInt (100.0f * (1.0f - v))) + " / B " + juce::String (juce::roundToInt (100.0f * v)));
                                                             })));
    const GranularParams g;
    for (int layer = 0; layer < numLayers; ++layer)
    {
        const auto name = "Layer " + layerName (layer) + " ";
        // Layer C arrived with the adaptive layers (version hint 7); A and B keep theirs.
        auto id = [layer] (const char* n) { return juce::ParameterID { layerParameterId (layer, n), layer < 2 ? 5 : 7 }; };
        layout.add (std::make_unique<juce::AudioParameterChoice> (id ("sourceMode"), name + "Source Mode", juce::StringArray { "One Shot", "Granular" }, 0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("granular.position"), name + "Grain Position", unit, static_cast<float> (100.0 * g.position), percent));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("granular.size"), name + "Grain Size", skewed (20.0f, 400.0f, 120.0f), static_cast<float> (1000.0 * g.sizeSeconds), ms));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("granular.density"), name + "Grain Density", skewed (4.0f, 40.0f, 14.0f), static_cast<float> (g.density),
                                                                 juce::AudioParameterFloatAttributes().withLabel ("/s")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("granular.tune"), name + "Grain Tune", Range (-12.0f, 12.0f, 0.0f), static_cast<float> (g.tuneSemitones),
                                                                 juce::AudioParameterFloatAttributes().withLabel ("st")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("granular.spread"), name + "Grain Spread", unit, static_cast<float> (100.0 * g.spread), percent));
    }

    // Adaptive 1-3 layers (version hint 7): every layer's START, TUNE, PAN, LEVEL and its
    // source modifiers; the three-layer mix; the envelope's decay and sustain.
    const LayerSettings defaults;
    for (int layer = 0; layer < numLayers; ++layer)
    {
        const auto name = "Layer " + layerName (layer) + " ";
        auto id = [layer] (const char* n) { return juce::ParameterID { layerParameterId (layer, n), 7 }; };
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("start"), name + "Start", unit, 0.0f,
                                                                 juce::AudioParameterFloatAttributes().withLabel ("%")
                                                                     .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("tune"), name + "Tune", Range (-24.0f, 24.0f, 0.01f), 0.0f,
                                                                 juce::AudioParameterFloatAttributes().withLabel ("st")
                                                                     .withStringFromValueFunction ([] (float v, int) { return format::semitones (v); })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("pan"), name + "Pan", Range (-100.0f, 100.0f, 0.1f), 0.0f,
                                                                 juce::AudioParameterFloatAttributes()
                                                                     .withStringFromValueFunction ([] (float v, int) { return format::pan (v); })
                                                                     .withValueFromStringFunction ([] (const juce::String& t) { return format::panFromText (t); })));
        Range levelRange (static_cast<float> (LayerSettings::minLevelDb), 6.0f, 0.1f);
        levelRange.setSkewForCentre (-12.0f);   // the knob's travel goes to the useful range
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("level"), name + "Level", levelRange, 0.0f,
                                                                 juce::AudioParameterFloatAttributes().withLabel ("dB")
                                                                     .withStringFromValueFunction ([] (float v, int) { return format::levelDb (v); })));
        layout.add (std::make_unique<juce::AudioParameterBool> (id ("link"), name + "Link", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (id ("reverse"), name + "Reverse", defaults.reverse));
        layout.add (std::make_unique<juce::AudioParameterBool> (id ("loop"), name + "Loop", defaults.loop));
        layout.add (std::make_unique<juce::AudioParameterBool> (id ("follow"), name + "Follow", defaults.follow));
        // Per-layer Original <-> Reimagined (version hint 8): A's is the instrument's `reimagined`.
        if (layer > 0)
            layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { reimaginedParameterId (layer), 8 }, layerName (layer) + " Reimagined",
                                                                     unit, 0.0f, reimaginedText));
    }
    // REIMAGINED modes (version hint 11): every layer's mode and each mode's own settings,
    // all remembered while another mode plays. The defaults are KALEIDOSCOPE at neutral
    // FOCUS / SPREAD: the Reimagined every older session was made with.
    {
        const ReimaginedSettings rd;
        const auto percentText = juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) {
            return juce::String (juce::roundToInt (v)) + " %";
        });
        for (int layer = 0; layer < numLayers; ++layer)
        {
            const auto prefix = layerName (layer) + " ";
            auto id = [layer] (const char* n) { return juce::ParameterID { reimaginedModeParameterId (layer, n), 11 }; };
            auto modeNumber = [&] (const char* n, const juce::String& name, double value) {
                layout.add (std::make_unique<juce::AudioParameterFloat> (id (n), prefix + name, unit, static_cast<float> (100.0 * value), percentText));
            };
            auto modeChoice = [&] (const char* n, const juce::String& name, const juce::StringArray& options, int value) {
                layout.add (std::make_unique<juce::AudioParameterChoice> (id (n), prefix + name, options, value));
            };
            modeChoice ("mode", "Reimagined Mode", juce::StringArray { "Kaleidoscope", "Tape Frame", "Toybox", "Mosaic", "Mirage" }, 0);
            modeNumber ("kaleidoscope.focus", "Kaleidoscope Focus", rd.kaleidoscope.focus);
            modeNumber ("kaleidoscope.spread", "Kaleidoscope Spread", rd.kaleidoscope.spread);
            modeNumber ("tapeFrame.age", "Tape Frame Age", rd.tapeFrame.age);
            modeNumber ("tapeFrame.stability", "Tape Frame Stability", rd.tapeFrame.stability);
            modeChoice ("tapeFrame.frame", "Tape Frame Length", juce::StringArray { "Short", "Classic", "Long" }, static_cast<int> (rd.tapeFrame.frame));
            modeNumber ("toybox.motion", "Toybox Motion", rd.toybox.motion);
            modeNumber ("toybox.digital", "Toybox Digital", rd.toybox.digital);
            modeChoice ("toybox.play", "Toybox Play", juce::StringArray { "Fwd", "Turn", "Chaos" }, static_cast<int> (rd.toybox.play));
            modeNumber ("mosaic.detail", "Mosaic Detail", rd.mosaic.detail);
            modeNumber ("mosaic.motion", "Mosaic Motion", rd.mosaic.motion);
            modeChoice ("mosaic.model", "Mosaic Model", juce::StringArray { "Pure", "Textured" }, static_cast<int> (rd.mosaic.model));
            modeNumber ("mirage.clock", "Mirage Clock", rd.mirage.clock);
            modeNumber ("mirage.filter", "Mirage Filter", rd.mirage.filter);
            modeChoice ("mirage.tone", "Mirage Tone", juce::StringArray { "Dark", "Open" }, static_cast<int> (rd.mirage.tone));
        }
    }
    // The layers' Original <-> Reimagined thumbs move together (keeping their offsets) while
    // linked; a UI behaviour, stored with the session (version hint 9).
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "reimaginedLink", 9 }, "Reimagined Link", true));
    // LIFE's round robins and variation character (version hint 10). The defaults (AUTO,
    // endless takes) are LIFE as it was, so older sessions sound the same.
    {
        juce::StringArray takes { juce::String::fromUTF8 ("\xe2\x88\x9e") };
        for (int n = 2; n <= PerformanceEngine::maxTakes; ++n)
            takes.add (juce::String (n));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::lifeCharacter, 10 }, "Life Character",
                                                                  juce::StringArray { "Auto", "Pluck", "Synth", "Drum" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::lifeTakes, 10 }, "Life Takes", takes, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::lifeTakeOrder, 10 }, "Life Take Order",
                                                                  juce::StringArray { "Cycle", "Random" }, 0));
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ids::lifeTakesSeed, 10 }, "Life Takes Variation", 0, 999, 0));
    }
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::mixX, 7 }, "Mix X", Range (0.0f, 1.0f, 0.0f), 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::mixY, 7 }, "Mix Y", Range (0.0f, 1.0f, 0.0f), 1.0f / 3.0f));
    {
        Range decayRange (1.0f, 20000.0f, 0.1f);
        decayRange.setSkewForCentre (600.0f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::decay, 7 }, "Decay", decayRange, 600.0f, ms));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::sustainLevel, 7 }, "Sustain Level", unit, 100.0f, percent));
    }
    // Mono with legato and glide (basses and leads), version hint 8.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::voiceMode, 8 }, "Voice Mode",
                                                              juce::StringArray { "Poly", "Mono" }, 0));
    {
        Range glideRange (0.0f, 3000.0f, 0.1f);
        glideRange.setSkewForCentre (250.0f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::glide, 8 }, "Glide", glideRange, 0.0f, ms));
    }
    // MUTE and SOLO per layer (version hint 13).
    for (int layer = 0; layer < numLayers; ++layer)
    {
        const auto name = "Layer " + layerName (layer) + " ";
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { layerParameterId (layer, "mute"), 13 }, name + "Mute", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { layerParameterId (layer, "solo"), 13 }, name + "Solo", false));
    }
    {
        // SPACE v2, ECHO and SHAPER CUSTOM (version hint 12).
        auto id12 = [] (const juce::String& id) { return juce::ParameterID { id, 12 }; };
        auto skewedRange = [] (float lo, float hi, float centre) {
            Range r (lo, hi, 0.0f);
            r.setSkewForCentre (centre);
            return r;
        };
        auto hz12 = juce::AudioParameterFloatAttributes().withLabel ("Hz");
        auto ms12 = juce::AudioParameterFloatAttributes().withLabel ("ms");
        const Shaping d12;
        layout.add (std::make_unique<juce::AudioParameterFloat> (id12 (ids::spacePreDelay), "Space Pre-Delay", skewedRange (0.0f, 250.0f, 40.0f),
                                                                 static_cast<float> (d12.spacePreDelayMs), ms12));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id12 (ids::spaceSize), "Space Size", unit, static_cast<float> (100.0 * d12.spaceSize), percent));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id12 (ids::spaceDamping), "Space Damping", unit, static_cast<float> (100.0 * d12.spaceDamping), percent));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id12 (ids::spaceModulation), "Space Modulation", unit, static_cast<float> (100.0 * d12.spaceModulation), percent));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id12 (ids::spaceWidth), "Space Width", unit, static_cast<float> (100.0 * d12.spaceWidth), percent));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id12 (ids::spaceLowCut), "Space Low Cut", skewedRange (20.0f, 2000.0f, 200.0f),
                                                                 static_cast<float> (d12.spaceLowCutHz), hz12));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id12 (ids::spaceHighCut), "Space High Cut", skewedRange (1000.0f, 20000.0f, 5000.0f),
                                                                 static_cast<float> (d12.spaceHighCutHz), hz12));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id12 (ids::echo), "Echo", unit, 0.0f, percent));
        layout.add (std::make_unique<juce::AudioParameterChoice> (id12 (ids::echoType), "Echo Type", juce::StringArray { "Tape", "BBD" }, static_cast<int> (d12.echoType)));
        layout.add (std::make_unique<juce::AudioParameterBool> (id12 (ids::echoSync), "Echo Sync", d12.echoSync));
        juce::StringArray divisions;
        for (int i = 0; i < shaping::echoDivisionCount; ++i)
            divisions.add (shaping::echoDivisionName (i));
        layout.add (std::make_unique<juce::AudioParameterChoice> (id12 (ids::echoDivision), "Echo Division", divisions, d12.echoDivision));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id12 (ids::echoTime), "Echo Time", skewedRange (20.0f, 1500.0f, 300.0f),
                                                                 static_cast<float> (d12.echoTimeMs), ms12));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id12 (ids::echoFeedback), "Echo Feedback", unit, static_cast<float> (100.0 * d12.echoFeedback), percent));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id12 (ids::echoTone), "Echo Tone", unit, static_cast<float> (100.0 * d12.echoTone), percent));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id12 (ids::echoAge), "Echo Age", unit, static_cast<float> (100.0 * d12.echoAge), percent));
        layout.add (std::make_unique<juce::AudioParameterChoice> (id12 (ids::echoStereo), "Echo Stereo", juce::StringArray { "Mono", "Ping-Pong", "Wide" },
                                                                  static_cast<int> (d12.echoStereo)));
        layout.add (std::make_unique<juce::AudioParameterBool> (id12 (ids::shaperCustom), "Shaper Custom", false));
    }
    {
        // DRIVE (version hint 14). 0 % = bypassed: sessions from before it sound exactly the same.
        auto id14 = [] (const juce::String& id) { return juce::ParameterID { id, 14 }; };
        const Shaping d14;
        layout.add (std::make_unique<juce::AudioParameterFloat> (id14 (ids::drive), "Drive", unit, 0.0f, percent));
        layout.add (std::make_unique<juce::AudioParameterChoice> (id14 (ids::driveMode), "Drive Mode", juce::StringArray { "Tube", "Tape", "Crunch" },
                                                                  static_cast<int> (d14.driveMode)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id14 (ids::driveTone), "Drive Tone", unit, static_cast<float> (100.0 * d14.driveTone), percent));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id14 (ids::driveBody), "Drive Body", unit, static_cast<float> (100.0 * d14.driveBody), percent));
    }
    {
        // ARPEGGIATOR (version hint 15). Off: the MIDI stream is not touched, so sessions from
        // before it (which open with it off) play exactly as they did.
        auto id15 = [] (const juce::String& id) { return juce::ParameterID { id, 15 }; };
        layout.add (std::make_unique<juce::AudioParameterBool> (id15 (ids::arpEnabled), "Arp", false));
        layout.add (std::make_unique<juce::AudioParameterChoice> (id15 (ids::arpPattern), "Arp Pattern", arpPatternNames(), 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (id15 (ids::arpRate), "Arp Rate", arpRateNames(), 1));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id15 (ids::arpGate), "Arp Gate", juce::NormalisableRange<float> (10.0f, 150.0f, 1.0f), 75.0f,
                                                                 percent));
        layout.add (std::make_unique<juce::AudioParameterInt> (id15 (ids::arpOctaves), "Arp Octaves", 1, 4, 1));
        // SWING came after (hint 16); 0 % is straight, as every session before it played.
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::arpSwing, 16 }, "Arp Swing",
                                                                 juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, percent));
        // VELOCITY (hint 19, an ARP destination): the generated notes' velocities scaled;
        // 100 % plays them as the keys were played, as every session before it did.
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::arpVelocity, 19 }, "Arp Velocity",
                                                                 juce::NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f, percent));
    }
    {
        // DAMPEN (hint 19), per layer: off by default, so sessions from before (and new
        // layers) sound exactly as they did.
        auto logRange = [] (float lo, float hi) {
            return juce::NormalisableRange<float> (
                lo, hi, [] (float start, float end, float x) { return start * std::pow (end / start, x); },
                [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); },
                [] (float start, float end, float v) { return juce::jlimit (start, end, v); });
        };
        const auto hz = juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) {
            return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
        });
        for (int layer = 0; layer < numLayers; ++layer)
        {
            const auto n = "Layer " + layerName (layer) + " Dampen ";
            auto id = [layer] (const char* name) { return juce::ParameterID { dampenParameterId (layer, name), 19 }; };
            layout.add (std::make_unique<juce::AudioParameterBool> (id ("enabled"), n + "On", false));
            layout.add (std::make_unique<juce::AudioParameterChoice> (id ("mode"), n + "Mode", juce::StringArray { "Soft Shelf", "Low-Pass" }, 0));
            layout.add (std::make_unique<juce::AudioParameterFloat> (id ("amount"), n + "Amount", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent));
            layout.add (std::make_unique<juce::AudioParameterFloat> (id ("frequency"), n + "Frequency",
                                                                     logRange (static_cast<float> (dampen::minShelfHz), static_cast<float> (dampen::maxShelfHz)), 3000.0f, hz));
            layout.add (std::make_unique<juce::AudioParameterFloat> (id ("cutoff"), n + "Min Cutoff",
                                                                     logRange (static_cast<float> (dampen::minCutoffLowHz), static_cast<float> (dampen::minCutoffHighHz)), 2000.0f, hz));
        }
    }
    {
        // MODULATION (version hint 17): two LFOs, two envelopes and 16 route slots. A route
        // slot is a stable set of parameters (automatable depth, recalled with the session);
        // sessions from before it have every slot empty, so nothing moves.
        auto id17 = [] (const juce::String& id) { return juce::ParameterID { id, 17 }; };
        const juce::StringArray shapes { "Sine", "Triangle", "Ramp Up", "Ramp Down", "Pulse", "Random", "Custom" };
        juce::StringArray divisions;
        for (int i = 0; i < mod::syncDivisionCount; ++i)
            divisions.add (mod::syncName (i));
        for (int i = 0; i < 2; ++i)
        {
            const auto n = "LFO " + juce::String (i + 1) + " ";
            layout.add (std::make_unique<juce::AudioParameterChoice> (id17 (modLfoId (i, "shape")), n + "Shape", shapes, 0));
            layout.add (std::make_unique<juce::AudioParameterFloat> (id17 (modLfoId (i, "rate")), n + "Rate", skewed (0.01f, 30.0f, 1.5f), 1.0f, hz));
            layout.add (std::make_unique<juce::AudioParameterBool> (id17 (modLfoId (i, "sync")), n + "Sync", false));
            layout.add (std::make_unique<juce::AudioParameterChoice> (id17 (modLfoId (i, "division")), n + "Division", divisions, 3));
            layout.add (std::make_unique<juce::AudioParameterFloat> (id17 (modLfoId (i, "phase")), n + "Phase", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, percent));
            layout.add (std::make_unique<juce::AudioParameterChoice> (id17 (modLfoId (i, "polarity")), n + "Polarity", juce::StringArray { "Bipolar", "Unipolar" }, 0));
            layout.add (std::make_unique<juce::AudioParameterChoice> (id17 (modLfoId (i, "mode")), n + "Mode", juce::StringArray { "Free", "Retrigger", "One Shot" }, 0));
            layout.add (std::make_unique<juce::AudioParameterChoice> (id17 (modLfoId (i, "scope")), n + "Scope", juce::StringArray { "Global", "Poly" }, 0));
        }
        for (int i = 0; i < 2; ++i)
        {
            const auto n = "ENV " + juce::String (i + 1) + " ";
            layout.add (std::make_unique<juce::AudioParameterChoice> (id17 (modEnvId (i, "mode")), n + "Mode", juce::StringArray { "ADSR", "One Shot" }, 0));
            layout.add (std::make_unique<juce::AudioParameterFloat> (id17 (modEnvId (i, "attack")), n + "Attack", skewed (0.0f, 10000.0f, 400.0f), 10.0f, ms));
            layout.add (std::make_unique<juce::AudioParameterFloat> (id17 (modEnvId (i, "decay")), n + "Decay", skewed (1.0f, 10000.0f, 600.0f), 300.0f, ms));
            layout.add (std::make_unique<juce::AudioParameterFloat> (id17 (modEnvId (i, "sustain")), n + "Sustain", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f, percent));
            layout.add (std::make_unique<juce::AudioParameterFloat> (id17 (modEnvId (i, "release")), n + "Release", skewed (1.0f, 10000.0f, 600.0f), 400.0f, ms));
            layout.add (std::make_unique<juce::AudioParameterFloat> (id17 (modEnvId (i, "curve")), n + "Curve", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, percent));
            layout.add (std::make_unique<juce::AudioParameterFloat> (id17 (modEnvId (i, "length")), n + "Length", skewed (10.0f, 20000.0f, 1000.0f), 1000.0f, ms));
        }
        juce::StringArray sources { "None" };
        for (int i = 1; i <= mod::sourceCount; ++i)
            sources.add (mod::sourceName (static_cast<mod::Source> (i)));
        for (int r = 0; r < mod::maxRoutes; ++r)
        {
            const auto n = "Mod " + juce::String (r + 1) + " ";
            layout.add (std::make_unique<juce::AudioParameterChoice> (id17 (modRouteId (r, "source")), n + "Source", sources, 0));
            layout.add (std::make_unique<juce::AudioParameterChoice> (id17 (modRouteId (r, "dest")), n + "Destination", modDestinationNames(), 0));
            // 0.01 % steps: 100 % is a destination's whole range (TUNE: 48 semitones).
            layout.add (std::make_unique<juce::AudioParameterFloat> (id17 (modRouteId (r, "depth")), n + "Depth", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.01f), 0.0f, percent));
            layout.add (std::make_unique<juce::AudioParameterBool> (id17 (modRouteId (r, "enabled")), n + "On", true));
        }
    }
    {
        // EQ (version hint 18): each layer's five bands. Every band and the EQ itself off by
        // default: sessions from before open with the layers untouched.
        auto id18 = [] (const juce::String& id) { return juce::ParameterID { id, 18 }; };
        auto logRange = [] (float lo, float hi) {
            return juce::NormalisableRange<float> (
                lo, hi, [] (float start, float end, float x) { return start * std::pow (end / start, x); },
                [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); },
                [] (float start, float end, float v) { return juce::jlimit (start, end, v); });
        };
        const auto frequency = juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) {
            return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
        });
        const auto gain = juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) {
            return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB";
        });
        const auto qText = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); });
        const juce::StringArray slopes { "12 dB/oct", "24 dB/oct" };
        for (int layer = 0; layer < numLayers; ++layer)
        {
            const auto n = "Layer " + layerName (layer) + " EQ ";
            auto id = [layer, &id18] (const char* name) { return id18 (eqParameterId (layer, name)); };
            layout.add (std::make_unique<juce::AudioParameterBool> (id ("enabled"), n + "On", false));
            for (int b = 0; b < eq::bandCount; ++b)
            {
                const auto band = static_cast<eq::Band> (b);
                const juce::String key = eqBandKey (band);
                const auto range = eq::frequencyRange (band);
                const auto bn = n + juce::String (eq::bandName (band)).toLowerCase() + " ";
                layout.add (std::make_unique<juce::AudioParameterBool> (id18 (eqParameterId (layer, key + ".enabled")), bn + "On", false));
                layout.add (std::make_unique<juce::AudioParameterFloat> (id18 (eqParameterId (layer, key + ".frequency")), bn + "Frequency",
                                                                         logRange (static_cast<float> (range.minHz), static_cast<float> (range.maxHz)),
                                                                         static_cast<float> (range.defaultHz), frequency));
                if (eq::hasGain (band))
                {
                    layout.add (std::make_unique<juce::AudioParameterFloat> (id18 (eqParameterId (layer, key + ".gain")), bn + "Gain",
                                                                             juce::NormalisableRange<float> (-18.0f, 18.0f, 0.01f), 0.0f, gain));
                    const bool bell = band == eq::Band::bell;
                    auto qRange = logRange (static_cast<float> (bell ? eq::minBellQ : eq::minShelfQ), static_cast<float> (bell ? eq::maxBellQ : eq::maxShelfQ));
                    layout.add (std::make_unique<juce::AudioParameterFloat> (id18 (eqParameterId (layer, key + ".q")), bn + "Q", qRange, bell ? 1.0f : 0.707f, qText));
                }
                else
                    layout.add (std::make_unique<juce::AudioParameterChoice> (id18 (eqParameterId (layer, key + ".slope")), bn + "Slope", slopes, 0));
            }
        }
    }
    {
        // Unified engine, Phase 2 (version hint 20), appended so nothing before moves. Each
        // layer's new grain options (neutral: SCATTER 0, PAN 0, HANN - exactly the Granulator as
        // it was) and its KEY DNA AMOUNT (live on sounding notes; heard only once the layer has
        // a KEY DNA profile, which lives in the session state). Then the grain budget.
        auto id20 = [] (const juce::String& id) { return juce::ParameterID { id, 20 }; };
        const auto semis = juce::AudioParameterFloatAttributes().withLabel ("st").withStringFromValueFunction ([] (float v, int) {
            return v <= 0.0f ? juce::String ("Off") : juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String (v, 1) + " st";
        });
        for (int layer = 0; layer < numLayers; ++layer)
        {
            const auto name = "Layer " + layerName (layer) + " ";
            layout.add (std::make_unique<juce::AudioParameterFloat> (id20 (layerParameterId (layer, "granular.scatter")), name + "Grain Scatter",
                                                                     Range (0.0f, 12.0f, 0.01f), 0.0f, semis));
            layout.add (std::make_unique<juce::AudioParameterFloat> (id20 (layerParameterId (layer, "granular.pan")), name + "Grain Pan", unit, 0.0f, percent));
            layout.add (std::make_unique<juce::AudioParameterChoice> (id20 (layerParameterId (layer, "granular.window")), name + "Grain Window",
                                                                      juce::StringArray { "Hann", "Soft", "Percussive" }, 0));
            layout.add (std::make_unique<juce::AudioParameterFloat> (id20 (layerParameterId (layer, "keyDna.amount")), name + "KEY DNA Amount", unit, 100.0f, percent));
        }
        juce::StringArray budgets;
        for (int grains : grainBudgetChoices)
            budgets.add (grains == 0 ? juce::String ("Unlimited") : juce::String (grains) + " grains");
        layout.add (std::make_unique<juce::AudioParameterChoice> (id20 (ids::grainBudget), "Grain Budget", budgets, newSessionGrainBudgetIndex));
    }
    {
        // MIX modulation (version hint 21), appended so nothing before moves: each route's
        // second destination, for what the frozen `dest` list cannot hold (KEY DNA AMOUNT, the
        // MIX). None by default, so every session from before plays its routes as it did.
        auto id21 = [] (const juce::String& id) { return juce::ParameterID { id, 21 }; };
        for (int r = 0; r < mod::maxRoutes; ++r)
            layout.add (std::make_unique<juce::AudioParameterChoice> (id21 (modRouteId (r, "destExt")), "Mod " + juce::String (r + 1) + " Destination+",
                                                                      modExtendedDestinationNames(), 0));
    }
    {
        // SYNTHESIZE (Phase S1, version hint 22), appended so nothing before moves: each
        // source's analog oscillator blended into its own voices. 0 by default (the voice is
        // bit-identical to playing without it), so every session from before plays as it did.
        auto id22 = [] (const juce::String& id) { return juce::ParameterID { id, 22 }; };
        for (int layer = 0; layer < numLayers; ++layer)
            layout.add (std::make_unique<juce::AudioParameterFloat> (id22 (layerParameterId (layer, "synthesize")), "Layer " + layerName (layer) + " Synthesize",
                                                                     unit, 0.0f, reimaginedText));
    }
    return layout;
}

} // namespace osp::plugin
