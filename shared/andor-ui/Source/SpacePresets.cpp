#include "SpacePresets.h"

namespace osp::plugin::spacepresets
{

namespace
{
    // Shorthands for the rooms' and the engines' values.
    using Values = std::vector<std::pair<const char*, float>>;

    /** A room: its type (0 ROOM, 1 HALL, 2 PLATE, 3 SPRING) and its controls. */
    Values room (int type, float decay, float size, float damp, float preDelay, float mod, float width, float lowCut, float highCut)
    {
        return { { "space.engine", 0.0f }, { "space.type", static_cast<float> (type) }, { "space.decay", decay }, { "space.size", size },
                 { "space.damping", damp }, { "space.preDelay", preDelay }, { "space.modulation", mod }, { "space.width", width },
                 { "space.lowCut", lowCut }, { "space.highCut", highCut } };
    }

    /** ABYSS: its variant (0 DEEP, 1 DARK, 2 INVERSE, 3 ENDLESS), GRAVITY, RESONANCE. */
    Values abyss (int variant, float decay, float size, float damp, float mod, float gravity, float resonance, float preDelay = 8.0f)
    {
        return { { "space.engine", 1.0f }, { "space.abyss.variant", static_cast<float> (variant) }, { "space.decay", decay }, { "space.size", size },
                 { "space.damping", damp }, { "space.modulation", mod }, { "space.abyss.gravity", gravity }, { "space.abyss.resonance", resonance },
                 { "space.preDelay", preDelay } };
    }

    /** VINTAGE: its variant (0 HALL, 1 PLATE, 2 CHAMBER, 3 ROOM), ERA (0 EARLY, 1 LATER), DIFFUSION. */
    Values vintage (int variant, int era, float decay, float size, float damp, float diffusion, float mod)
    {
        return { { "space.engine", 2.0f }, { "space.vintage.variant", static_cast<float> (variant) }, { "space.vintage.era", static_cast<float> (era) },
                 { "space.decay", decay }, { "space.size", size }, { "space.damping", damp }, { "space.vintage.diffusion", diffusion },
                 { "space.modulation", mod } };
    }

    /** BLOOM: its variant (0 CLEAN, 1 VINTAGE, 2 TAPE, 3 WASH), synced TIME (a division), DIFFUSION. */
    Values bloom (int variant, int division, float decay, float damp, float diffusion, float mod)
    {
        return { { "space.engine", 3.0f }, { "space.bloom.variant", static_cast<float> (variant) }, { "space.bloom.sync", 1.0f },
                 { "space.bloom.division", static_cast<float> (division) }, { "space.decay", decay }, { "space.damping", damp },
                 { "space.bloom.diffusion", diffusion }, { "space.modulation", mod } };
    }

    juce::RangedAudioParameter* find (const juce::AudioProcessorValueTreeState& state, const juce::String& id)
    {
        return state.getParameter (id);
    }
}

const std::vector<Preset>& all()
{
    // (category, name, values). Divisions: 3 1/8, 5 1/8D, 6 1/4, 9 1/2.
    static const std::vector<Preset> presets {
        { "Rooms", "Wood Room", room (0, 0.6f, 30.0f, 50.0f, 4.0f, 15.0f, 80.0f, 120.0f, 7000.0f) },
        { "Rooms", "Live Room", room (0, 0.9f, 40.0f, 40.0f, 6.0f, 20.0f, 100.0f, 100.0f, 9000.0f) },
        { "Rooms", "Dry Booth", room (0, 0.35f, 15.0f, 60.0f, 2.0f, 10.0f, 60.0f, 150.0f, 6000.0f) },
        { "Rooms", "Stone Room", room (0, 1.2f, 50.0f, 25.0f, 8.0f, 20.0f, 100.0f, 90.0f, 10000.0f) },
        { "Rooms", "Soft Room", room (0, 0.7f, 35.0f, 55.0f, 5.0f, 20.0f, 90.0f, 100.0f, 6000.0f) },
        { "Rooms", "Bright Box", room (0, 0.5f, 25.0f, 15.0f, 3.0f, 15.0f, 100.0f, 200.0f, 14000.0f) },
        { "Halls", "Concert Hall", room (1, 3.0f, 50.0f, 40.0f, 8.0f, 40.0f, 100.0f, 100.0f, 5000.0f) },   // (SPACE's defaults: a new instrument shows it)
        { "Halls", "Warm Hall", room (1, 2.6f, 65.0f, 60.0f, 20.0f, 30.0f, 100.0f, 120.0f, 4000.0f) },
        { "Halls", "Bright Hall", room (1, 2.4f, 60.0f, 20.0f, 18.0f, 25.0f, 100.0f, 150.0f, 11000.0f) },
        { "Halls", "Small Hall", room (1, 1.6f, 45.0f, 40.0f, 12.0f, 25.0f, 100.0f, 120.0f, 7000.0f) },
        { "Halls", "Dark Hall", room (1, 3.5f, 75.0f, 75.0f, 30.0f, 30.0f, 100.0f, 90.0f, 2800.0f) },
        { "Halls", "Cathedral", room (1, 6.5f, 95.0f, 45.0f, 40.0f, 35.0f, 100.0f, 80.0f, 5000.0f) },
        { "Plates", "Vocal Plate", room (2, 1.8f, 50.0f, 30.0f, 12.0f, 25.0f, 100.0f, 150.0f, 9000.0f) },
        { "Plates", "Bright Plate", room (2, 1.5f, 45.0f, 10.0f, 8.0f, 20.0f, 100.0f, 200.0f, 14000.0f) },
        { "Plates", "Drum Plate", room (2, 1.0f, 35.0f, 40.0f, 4.0f, 15.0f, 100.0f, 180.0f, 10000.0f) },
        { "Plates", "Soft Plate", room (2, 2.2f, 55.0f, 60.0f, 16.0f, 30.0f, 100.0f, 120.0f, 5000.0f) },
        { "Plates", "Long Plate", room (2, 4.0f, 70.0f, 35.0f, 20.0f, 30.0f, 100.0f, 120.0f, 8000.0f) },
        { "Springs", "Classic Spring", room (3, 2.0f, 50.0f, 30.0f, 0.0f, 20.0f, 70.0f, 150.0f, 6000.0f) },
        { "Springs", "Short Spring", room (3, 1.0f, 35.0f, 35.0f, 0.0f, 15.0f, 60.0f, 200.0f, 7000.0f) },
        { "Springs", "Surf Spring", room (3, 3.0f, 60.0f, 20.0f, 0.0f, 25.0f, 80.0f, 150.0f, 8000.0f) },
        { "Springs", "Dark Spring", room (3, 2.2f, 50.0f, 65.0f, 0.0f, 20.0f, 70.0f, 120.0f, 3500.0f) },
        { "Abyss", "Deep Space", abyss (0, 6.0f, 70.0f, 40.0f, 40.0f, 0.0f, 0.0f) },
        { "Abyss", "Heavy Gravity", abyss (0, 6.0f, 75.0f, 45.0f, 35.0f, 80.0f, 0.0f) },
        { "Abyss", "Singing Void", abyss (0, 5.0f, 65.0f, 35.0f, 40.0f, 0.0f, 70.0f) },
        { "Abyss", "Dark Matter", abyss (1, 7.0f, 80.0f, 60.0f, 40.0f, 20.0f, 30.0f) },
        { "Abyss", "Rising Swell", abyss (2, 4.0f, 60.0f, 40.0f, 40.0f, -40.0f, 0.0f) },
        { "Abyss", "Endless Drift", abyss (3, 8.0f, 90.0f, 40.0f, 60.0f, 0.0f, 0.0f) },
        { "Vintage", "Early Hall", vintage (0, 0, 2.8f, 60.0f, 40.0f, 70.0f, 30.0f) },
        { "Vintage", "Grainy Hall", vintage (0, 0, 4.0f, 70.0f, 45.0f, 50.0f, 30.0f) },
        { "Vintage", "Later Plate", vintage (1, 1, 2.0f, 50.0f, 30.0f, 70.0f, 25.0f) },
        { "Vintage", "Chamber 80", vintage (2, 0, 1.8f, 45.0f, 40.0f, 60.0f, 20.0f) },
        { "Vintage", "Digital Room", vintage (3, 1, 0.8f, 30.0f, 40.0f, 60.0f, 15.0f) },
        { "Bloom", "Clean Bloom", bloom (0, 6, 3.0f, 40.0f, 60.0f, 30.0f) },
        { "Bloom", "Dotted Bloom", bloom (0, 5, 3.0f, 40.0f, 50.0f, 30.0f) },
        { "Bloom", "Tape Bloom", bloom (2, 5, 3.5f, 50.0f, 50.0f, 35.0f) },
        { "Bloom", "Bucket Bloom", bloom (1, 3, 2.5f, 60.0f, 45.0f, 30.0f) },
        { "Bloom", "Slow Bloom", bloom (0, 9, 5.0f, 40.0f, 70.0f, 35.0f) },
        { "Bloom", "Wash", bloom (3, 6, 6.0f, 45.0f, 90.0f, 40.0f) },
    };
    return presets;
}

const juce::StringArray& categories()
{
    static const juce::StringArray c { "Rooms", "Halls", "Plates", "Springs", "Abyss", "Vintage", "Bloom" };
    return c;
}

const juce::StringArray& parameterIds()
{
    static const juce::StringArray ids { "space.type", "space.decay", "space.preDelay", "space.size", "space.damping", "space.modulation",
                                         "space.width", "space.lowCut", "space.highCut", "space.engine", "space.abyss.gravity",
                                         "space.abyss.freeze", "space.abyss.resonance", "space.abyss.variant", "space.vintage.variant",
                                         "space.vintage.era", "space.vintage.diffusion", "space.bloom.variant", "space.bloom.sync",
                                         "space.bloom.division", "space.bloom.time", "space.bloom.diffusion" };
    return ids;
}

namespace
{
    /** The preset's value for a parameter, normalised (its default where it sets none). */
    float target (const juce::RangedAudioParameter& p, const Preset& preset)
    {
        for (const auto& [id, value] : preset.values)
            if (p.getParameterID() == id)
                return p.convertTo0to1 (value);
        return p.getDefaultValue();
    }
}

void apply (juce::AudioProcessorValueTreeState& state, const Preset& preset, juce::UndoManager* undo)
{
    if (undo != nullptr)
        undo->beginNewTransaction ("SPACE: " + juce::String (preset.name));
    // The engine first (the popover rebuilds for it), then the rest.
    juce::StringArray order { "space.engine" };
    for (const auto& id : parameterIds())
        if (id != "space.engine")
            order.add (id);
    for (const auto& id : order)
        if (auto* p = find (state, id))
        {
            const float v = target (*p, preset);
            if (std::abs (p->getValue() - v) > 1.0e-6f)
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (v);
                p->endChangeGesture();
            }
        }
}

int matching (const juce::AudioProcessorValueTreeState& state)
{
    const auto& presets = all();
    for (std::size_t i = 0; i < presets.size(); ++i)
    {
        bool same = true;
        for (const auto& id : parameterIds())
            if (const auto* p = find (state, id))
                if (std::abs (p->getValue() - target (*p, presets[i])) > 2.0e-3f)
                {
                    same = false;
                    break;
                }
        if (same)
            return static_cast<int> (i);
    }
    return -1;
}

bool sameKind (const juce::AudioProcessorValueTreeState& state, const Preset& preset)
{
    auto now = [&state] (const char* id) {
        const auto* p = find (state, id);
        return p != nullptr ? juce::roundToInt (p->convertFrom0to1 (p->getValue())) : 0;
    };
    auto of = [&state, &preset] (const char* id) {
        for (const auto& [key, value] : preset.values)
            if (juce::String (key) == id)
                return juce::roundToInt (value);
        const auto* p = find (state, id);
        return p != nullptr ? juce::roundToInt (p->convertFrom0to1 (p->getDefaultValue())) : 0;
    };
    const int engine = of ("space.engine");
    if (now ("space.engine") != engine)
        return false;
    static const char* kinds[] { "space.type", "space.abyss.variant", "space.vintage.variant", "space.bloom.variant" };
    const char* kind = kinds[juce::jlimit (0, 3, engine)];
    return now (kind) == of (kind);
}

juce::String barText (const juce::AudioProcessorValueTreeState& state, int last)
{
    const auto& presets = all();
    if (const int m = matching (state); m >= 0)
        return presets[static_cast<std::size_t> (m)].name;
    if (last >= 0 && last < static_cast<int> (presets.size()) && sameKind (state, presets[static_cast<std::size_t> (last)]))
        return juce::String (presets[static_cast<std::size_t> (last)].name) + " *";
    return describe (state);
}

juce::String describe (const juce::AudioProcessorValueTreeState& state)
{
    auto index = [&state] (const char* id) {
        const auto* p = find (state, id);
        return p != nullptr ? juce::roundToInt (p->convertFrom0to1 (p->getValue())) : 0;
    };
    static const char* rooms[] { "ROOM", "HALL", "PLATE", "SPRING" };
    static const char* abyssVariants[] { "DEEP", "DARK", "INVERSE", "ENDLESS" };
    static const char* vintageVariants[] { "HALL", "PLATE", "CHAMBER", "ROOM" };
    static const char* bloomVariants[] { "CLEAN", "VINTAGE", "TAPE", "WASH" };
    switch (juce::jlimit (0, 3, index ("space.engine")))
    {
        case 1: return juce::String ("ABYSS ") + abyssVariants[juce::jlimit (0, 3, index ("space.abyss.variant"))];
        case 2: return juce::String ("VINTAGE ") + vintageVariants[juce::jlimit (0, 3, index ("space.vintage.variant"))];
        case 3: return juce::String ("BLOOM ") + bloomVariants[juce::jlimit (0, 3, index ("space.bloom.variant"))];
        default: return rooms[juce::jlimit (0, 3, index ("space.type"))];
    }
}

} // namespace osp::plugin::spacepresets
