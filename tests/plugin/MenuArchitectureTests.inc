// Menu architecture: the main menu holds global actions only (INSTRUMENT, SOURCES, CONTENT,
// PREFERENCES, SUPPORT); every source's actions are on its own card's menu, addressed by its
// letter; SAVE updates a writable user preset and otherwise saves a new one (factory and pack
// presets and starting states are never overwritten). docs/research/menu-content/.

#include "SourceOctaveRow.h"
#include "LibraryPlaces.h"

namespace
{
    struct MenuEntry
    {
        juce::String text, shortcut;
        bool header = false, enabled = true;
        std::function<void()> action;
    };

    std::vector<MenuEntry> entriesOf (const juce::PopupMenu& menu)
    {
        std::vector<MenuEntry> out;
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        {
            const auto& item = it.getItem();
            if (item.isSeparator)
                continue;
            // (a source menu's identity, a custom item, opens it as its header did)
            out.push_back ({ item.text, item.shortcutKeyDescription, item.isSectionHeader || item.customComponent != nullptr, item.isEnabled, item.action });
        }
        return out;
    }

    const MenuEntry* entryStarting (const std::vector<MenuEntry>& entries, const juce::String& prefix)
    {
        for (const auto& e : entries)
            if (! e.header && e.text.startsWith (prefix))
                return &e;
        return nullptr;
    }

    struct MenuRig
    {
        TempDir tmp;
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor { p.createEditorIfNeeded() };
        osp::plugin::OspAudioProcessorEditor& ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor&> (*editor);

        void loadTwo()
        {
            juce::Array<juce::File> list;
            list.add (writeSource (tmp.dir, "Cello Sustain A2.wav", testsignals::vowel (midiToHz (45), 1.0, 48000.0, 3)));
            list.add (writeSource (tmp.dir, "Glass Pluck E4.wav", testsignals::pluck (midiToHz (64), 1.0, 48000.0, 4)));
            p.addLayers (list);
            REQUIRE (p.waitForLoads (30000));
            p.pollLoads();
            ui.refreshNow();
        }

        juce::Button* saveKey()
        {
            juce::Button* found = nullptr;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
                for (auto* child : c.getChildren())
                {
                    if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && b->getName() == "Save preset")
                        found = b;
                    walk (*child);
                }
            };
            walk (*editor);
            return found;
        }
    };
}

TEST_CASE ("menus: the main menu holds global actions only, in five groups; no source is singled out", "[plugin][menus]")
{
    MenuRig rig;
    rig.loadTwo();
    const auto entries = entriesOf (rig.ui.mainMenu());
    juce::StringArray headers, items;
    for (const auto& e : entries)
        (e.header ? headers : items).add (e.text);
    CHECK (headers == juce::StringArray { "INSTRUMENT", "SOURCES", "CONTENT", "PREFERENCES", "SUPPORT" });
    for (const auto* expected : { "New instrument", "Save instrument", "Save as", "Open instrument", "Open Library", "Undo", "Redo", "Add source", "Clear all sources",
                                  "Save as Starting State", "Export portable instrument", "Create content pack", "Import pack", "Manage packs", "Interface size", "Library settings",
                                  "Tutorials", "About ANDOR OSP" })
    {
        INFO (expected);
        CHECK (entryStarting (entries, expected) != nullptr);
    }
    // Nothing of one source's: no Layer section, no Replace / Root / Map notes / Samples / Remove
    // layer, no Load example aimed at A; no duplicate browsing of presets or instruments.
    for (const auto& text : items)
    {
        INFO (text);
        for (const auto* gone : { "Layer", "Replace", "Root", "Map notes", "Samples", "Remove layer", "Load example", "Presets", "Instruments", "Open preset file", "Learn" })
            CHECK_FALSE (text.startsWith (gone));
    }
    for (const auto& h : headers)
        CHECK_FALSE (h.containsIgnoreCase ("layer"));
    // The shortcuts OSP really handles, shown at the right; no new ones a host might want.
    CHECK (entryStarting (entries, "Open Library")->shortcut.isNotEmpty());
    CHECK (entryStarting (entries, "Undo")->shortcut.isNotEmpty());
    CHECK (entryStarting (entries, "Redo")->shortcut.isNotEmpty());
    CHECK (entryStarting (entries, "Save instrument")->shortcut.isEmpty());
    // No development command (looks, knob skins) in the customer menu.
    for (const auto& text : items)
        CHECK_FALSE (text.containsIgnoreCase ("knob"));
}

TEST_CASE ("menus: each source's menu names its source and acts on it, never on A by default", "[plugin][menus]")
{
    MenuRig rig;
    rig.loadTwo();
    REQUIRE (rig.p.isLayerOccupied (1));
    rig.p.setEditLayer (0);   // (A is the edited layer: B's menu must still mean B)
    auto* tuneA = rig.p.apvts().getParameter ("layerA.tune");
    auto* tuneB = rig.p.apvts().getParameter ("layerB.tune");
    tuneA->setValueNotifyingHost (tuneA->convertTo0to1 (5.0f));
    tuneB->setValueNotifyingHost (tuneB->convertTo0to1 (5.0f));
    const auto b = entriesOf (rig.ui.sourceMenu (1));
    REQUIRE_FALSE (b.empty());
    CHECK (b.front().header);
    CHECK (b.front().text.startsWith ("SOURCE B"));
    CHECK (b.front().text.contains ("Glass Pluck E4.wav"));
    juce::StringArray headers;
    for (const auto& e : b)
        if (e.header)
            headers.add (e.text.upToFirstOccurrenceOf (" ", false, false));
    CHECK (headers == juce::StringArray { "SOURCE", "AUDIO", "OCTAVE", "MANAGEMENT" });   // (OCTAVE: a custom row)
    CHECK (entryStarting (b, "Replace audio") != nullptr);
    CHECK (entryStarting (b, "Map notes") != nullptr);
    CHECK (entryStarting (b, "Root") != nullptr);
    // Reset source settings on B: B's controls only.
    const auto* reset = entryStarting (b, "Reset source settings");
    REQUIRE (reset != nullptr);
    reset->action();
    CHECK (rig.p.parameterValue ("layerB.tune") == Catch::Approx (0.0f).margin (1.0e-3));
    CHECK (rig.p.parameterValue ("layerA.tune") == Catch::Approx (5.0f).margin (1.0e-3));
    // A's menu names A.
    const auto a = entriesOf (rig.ui.sourceMenu (0));
    CHECK (a.front().text.startsWith ("SOURCE A"));
    CHECK (a.front().text.contains ("Cello Sustain A2.wav"));
    // The card's own menu key opens that source's menu (as a click does).
    juce::PopupMenu opened;
    rig.ui.showMenuForTest = [&opened] (juce::PopupMenu& m) { opened = m; };
    rig.ui.engineCard (1).onMenu (1, rig.ui.engineCard (1));
    rig.ui.showMenuForTest = nullptr;
    const auto viaCard = entriesOf (opened);
    REQUIRE_FALSE (viaCard.empty());
    CHECK (viaCard.front().text.startsWith ("SOURCE B"));
    // An empty source: what can fill it, nothing about audio.
    const auto c = entriesOf (rig.ui.sourceMenu (2));
    CHECK (c.front().text.startsWith ("SOURCE C"));
    CHECK (entryStarting (c, "Load audio") != nullptr);
    CHECK (entryStarting (c, "Map notes") == nullptr);
    CHECK (entryStarting (c, "Root") == nullptr);
}

TEST_CASE ("menus: SAVE updates a writable user preset; otherwise (new patch, starting state, factory or pack preset) it never overwrites", "[plugin][menus][save]")
{
    MenuRig rig;
    rig.loadTwo();
    // A new patch, or a built-in starting state: no identity to update.
    CHECK_FALSE (rig.p.canSaveInPlace());
    rig.p.setCurrentProgram (1);
    CHECK_FALSE (rig.p.canSaveInPlace());
    // A user preset: SAVE writes it in place.
    OspAudioProcessor::presetFolder().createDirectory();
    const auto file = OspAudioProcessor::presetFolder().getChildFile ("Menu Save Test.osppreset");
    file.deleteFile();
    REQUIRE (rig.p.savePreset (file));
    REQUIRE (rig.p.loadPreset (file));
    REQUIRE (rig.p.canSaveInPlace());
    auto* tuneB = rig.p.apvts().getParameter ("layerB.tune");
    tuneB->setValueNotifyingHost (tuneB->convertTo0to1 (7.0f));
    auto* key = rig.saveKey();
    REQUIRE (key != nullptr);
    key->onClick();   // (the SAVE key, as a click does)
    {
        OspAudioProcessor fresh;
        REQUIRE (fresh.loadPreset (file));
        CHECK (fresh.parameterValue ("layerB.tune") == Catch::Approx (7.0f).margin (1.0e-3));
    }
    // After a starting state is applied, SAVE no longer means that preset.
    rig.p.setCurrentProgram (0);
    CHECK_FALSE (rig.p.canSaveInPlace());
    // A factory or pack preset is read-only: SAVE goes to Save As.
    const auto places = osp::plugin::packPlaces();
    for (const auto& dir : { places.factoryDir, places.packsDir })
    {
        const auto folder = juce::File (juce::String::fromUTF8 (dir.string().c_str())).getChildFile ("osp.test.pack").getChildFile ("Presets");
        folder.createDirectory();
        const auto shipped = folder.getChildFile ("Shipped.osppreset");
        REQUIRE (file.copyFileTo (shipped));
        REQUIRE (rig.p.loadPreset (shipped));
        CHECK_FALSE (rig.p.canSaveInPlace());
        folder.getParentDirectory().deleteRecursively();
    }
    file.deleteFile();
}

TEST_CASE ("menus: main and source menu screenshots (OSP_MENU_SHOTS)", "[.][menu-source-shots]")
{
    const char* dir = std::getenv ("OSP_MENU_SHOTS");
    if (dir == nullptr)
        return;
    const juce::File out (juce::String::fromUTF8 (dir));
    MenuRig rig;
    rig.editor->setSize (1448, juce::roundToInt (rig.ui.instrumentHeight() * 1448.0f / osp::plugin::design::width));
    rig.loadTwo();
    {
        // The main menu as showMenu places it: under its key, right-aligned to it.
        auto menu = rig.ui.mainMenu();
        juce::Button* key = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c) {
            for (auto* child : c.getChildren())
            {
                if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && b->getTooltip().startsWith ("Instrument, sources"))
                    key = b;
                find (*child);
            }
        };
        find (*rig.editor);
        REQUIRE (key != nullptr);
        const int width = rig.ui.popupMenuWidth (menu);
        const auto at = rig.editor->getLocalArea (key, key->getLocalBounds());
        juce::PopupMenu::dismissAllActiveMenus();
        menu.showMenuAsync (juce::PopupMenu::Options().withParentComponent (rig.editor.get())
                                .withTargetScreenArea (juce::Rectangle<int> (at.getRight() - width, at.getY(), width, at.getHeight()))
                                .withMinimumWidth (width));
        const auto image = rig.editor->createComponentSnapshot (rig.editor->getLocalBounds(), true, 2.0f);
        const auto file = out.getChildFile ("after-01-main-menu.png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        juce::PopupMenu::dismissAllActiveMenus();
    }
    for (int layer : { 0, 1, 2 })
    {
        auto menu = rig.ui.sourceMenu (layer);
        auto& card = rig.ui.engineCard (layer == 2 ? 1 : layer);
        menuShot (*rig.editor, menu, card, out.getChildFile ("after-0" + juce::String (2 + layer) + "-source-" + osp::plugin::OspAudioProcessor::layerName (layer) + "-menu.png"));
    }
}

TEST_CASE ("menus: a source's OCTAVE row moves the detected root by octaves (Auto at 0), undoable, and shows a hand-set root's octave", "[plugin][menus][octave]")
{
    MenuRig rig;
    rig.loadTwo();
    REQUIRE (rig.p.isLayerOccupied (1));
    const auto loaded = rig.p.currentInstrument (1);
    REQUIRE (loaded != nullptr);
    REQUIRE (loaded->analysis.pitch.detected);
    const double detected = loaded->analysisRootMidi;
    auto rowOf = [&rig] (int layer) -> osp::plugin::SourceOctaveRow* {
        auto menu = rig.ui.sourceMenu (layer);
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            if (auto* row = dynamic_cast<osp::plugin::SourceOctaveRow*> (it.getItem().customComponent.get()))
            {
                static juce::PopupMenu kept;   // (keeps the row alive past this menu)
                kept = menu;
                return row;
            }
        return nullptr;
    };
    auto* row = rowOf (1);
    REQUIRE (row != nullptr);
    CHECK (row->lit() == std::optional<int> (0));   // Auto
    row->choose (-1);
    REQUIRE (rig.p.rootOverride (1).has_value());
    CHECK (*rig.p.rootOverride (1) == Catch::Approx (detected - 12.0));   // (its fine tuning kept)
    CHECK (rig.p.effectiveRootMidi (1) == Catch::Approx (detected - 12.0));
    CHECK (! rig.p.rootOverride (0).has_value());   // (B's row, B's root only)
    row = rowOf (1);
    REQUIRE (row != nullptr);
    CHECK (row->lit() == std::optional<int> (-1));
    row->choose (2);
    CHECK (*rig.p.rootOverride (1) == Catch::Approx (detected + 24.0));
    rig.p.undo().undo();
    CHECK (*rig.p.rootOverride (1) == Catch::Approx (detected - 12.0));
    rowOf (1)->choose (0);
    CHECK (! rig.p.rootOverride (1).has_value());   // 0: Auto again
    // A root set by hand from the list: the octave it is nearest (within a semitone), else none.
    using Row = osp::plugin::SourceOctaveRow;
    CHECK (Row::octaveOf (23.0, 35.05) == std::optional<int> (-1));   // (Cosmic Strings: B0 against a detected B1)
    CHECK (Row::octaveOf (30.0, 35.05) == std::nullopt);
    CHECK (Row::octaveOf (std::nullopt, 35.05) == std::optional<int> (0));
    CHECK (Row::overrideFor (0, 35.05) == std::nullopt);
}
