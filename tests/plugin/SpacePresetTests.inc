// SPACE's presets (shared/andor-ui/SpacePresets): named settings of SPACE's own controls by
// category, in the tabs' place under the display. They only set the parameters SPACE already
// has: every session made before plays as it did; the name is worked out from the values.

#include "SpacePresets.h"

TEST_CASE ("plugin: SPACE presets set SPACE's controls and are recognised by their values", "[plugin][space][presets]")
{
    namespace sp = osp::plugin::spacepresets;
    OspAudioProcessor p;
    const auto& all = sp::all();
    REQUIRE (all.size() >= 30);
    // Every category has presets, every name is its own.
    juce::StringArray names;
    for (const auto& category : sp::categories())
    {
        int n = 0;
        for (const auto& preset : all)
            n += category == preset.category ? 1 : 0;
        INFO (category);
        CHECK (n >= 4);
    }
    for (const auto& preset : all)
    {
        CHECK_FALSE (names.contains (preset.name));
        names.add (preset.name);
    }
    // A new instrument is "Concert Hall" (SPACE's defaults).
    REQUIRE (sp::matching (p.parameters) >= 0);
    CHECK (juce::String (all[static_cast<std::size_t> (sp::matching (p.parameters))].name) == "Concert Hall");
    // Each preset, once chosen, is the one the values match; its engine is the one it names.
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        sp::apply (p.parameters, all[i], &p.undo());
        INFO (all[i].name);
        CHECK (sp::matching (p.parameters) == static_cast<int> (i));
        const juce::String category (all[i].category);
        const auto described = sp::describe (p.parameters);
        if (category == "Abyss" || category == "Vintage" || category == "Bloom")
            CHECK (described.startsWith (category.toUpperCase()));
        else
            CHECK (described == juce::StringArray { "ROOM", "HALL", "PLATE", "SPRING" }[sp::categories().indexOf (category)]);
    }
    // Edited, it is none of them: the bar shows the last one with " *" while SPACE is of its
    // kind, else what is set.
    p.setParameterValue ("space.decay", 7.77f);
    CHECK (sp::matching (p.parameters) == -1);
    CHECK (sp::barText (p.parameters, static_cast<int> (all.size()) - 1) == juce::String (all.back().name) + " *");
    p.setParameterValue ("space.engine", 1.0f);   // (another engine: no longer that preset at all)
    CHECK (sp::barText (p.parameters, static_cast<int> (all.size()) - 1) == sp::describe (p.parameters));
    // Choosing one again changes nothing (its values are already there); another replaces it
    // whole (no value of the one before is left: every SPACE parameter is set).
    sp::apply (p.parameters, all[0], &p.undo());
    sp::apply (p.parameters, all[0], &p.undo());
    CHECK (sp::matching (p.parameters) == 0);
    sp::apply (p.parameters, all.back(), &p.undo());
    CHECK (sp::matching (p.parameters) == static_cast<int> (all.size()) - 1);
}

TEST_CASE ("plugin: SPACE's popover shows the presets bar in the tabs' place; the variants are in the header's list", "[plugin][space][presets]")
{
    namespace sp = osp::plugin::spacepresets;
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    std::function<juce::Component* (juce::Component&, const juce::String&)> find = [&] (juce::Component& c, const juce::String& title) -> juce::Component* {
        if (c.getTitle() == title)
            return &c;
        for (auto* child : c.getChildren())
            if (auto* f = find (*child, title))
                return f;
        return nullptr;
    };
    for (int engine = 0; engine < 4; ++engine)
    {
        p.setParameterValue ("space.engine", static_cast<float> (engine));
        ui->openPopup (4);
        auto* panel = ui->openPopupPanel();
        REQUIRE (panel != nullptr);
        INFO ("engine " << engine);
        auto* bar = find (*panel, "SPACE presets");
        REQUIRE (bar != nullptr);
        CHECK (bar->isVisible());
        CHECK (bar->getHeight() > 10);
        ui->closePopup();
    }
    // A session from before (no preset in it): it plays as it did; the bar names what is set.
    p.setParameterValue ("space.engine", 1.0f);
    p.setParameterValue ("space.abyss.variant", 1.0f);
    p.setParameterValue ("space.decay", 5.55f);
    CHECK (sp::matching (p.parameters) == -1);
    CHECK (sp::describe (p.parameters) == "ABYSS DARK");
}
