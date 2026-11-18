// Scalable preset navigation (docs/research/preset-navigation): the quick preset menu groups
// presets into collections, categories and packs, one column, at most 20 rows a flyout then
// Browse all; categories come from the stored value, an older alias or a name prefix, never by
// moving files; the arrows walk the collection a preset was chosen from; the Library's category
// filter agrees with the menu; 50, 500 and 5,000 presets keep it compact.

#include "library/PresetCategory.h"

namespace
{
    struct NavEntry
    {
        juce::String text;
        bool header = false, ticked = false;
        const juce::PopupMenu* sub = nullptr;
        std::function<void()> action;
    };

    std::vector<NavEntry> navEntries (const juce::PopupMenu& menu)
    {
        std::vector<NavEntry> out;
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        {
            const auto& item = it.getItem();
            if (item.isSeparator)
                continue;
            out.push_back ({ item.text, item.isSectionHeader, item.isTicked, item.subMenu.get(), item.action });
        }
        return out;
    }

    const NavEntry* navFind (const std::vector<NavEntry>& entries, const juce::String& text)
    {
        for (const auto& e : entries)
            if (e.text == text)
                return &e;
        return nullptr;
    }

    /** A user preset with this name (and stored category), saved by the processor and indexed. */
    void savePresetAs (OspAudioProcessor& p, const juce::String& name, const juce::String& category = {})
    {
        OspAudioProcessor::presetFolder().createDirectory();
        const auto file = OspAudioProcessor::presetFolder().getChildFile (name + ".osppreset");
        REQUIRE (p.savePreset (file));
        if (category.isNotEmpty())
        {
            REQUIRE (p.libraryService().waitUntilIdle (10000));
            p.libraryService().runAndWait ([path = file.getFullPathName().toStdString(), category] (osp::library::Catalog& c) {
                if (const auto id = c.presetWithFile (path))
                    c.setCategory (*id, category.toStdString());
            });
        }
    }

    /** Each test its own Library, presets and store: nothing it saves reaches another test. */
    struct IsolatedPresets
    {
        TempDir tmp;
        ScopedEnvironment library { "OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName() };
        ScopedEnvironment documents { "OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName() };
        ScopedEnvironment store { "OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName() };
    };

    void clearPresets()
    {
        OspAudioProcessor::presetFolder().deleteRecursively();
        OspAudioProcessor::startingStateFolder().deleteRecursively();
    }
}

TEST_CASE ("preset navigation: the quick menu - global actions, COLLECTIONS, CATEGORIES (empty ones hidden), one column, the loaded one ticked", "[plugin][preset-nav]")
{
    IsolatedPresets isolated;
    clearPresets();
    OspAudioProcessor p;
    savePresetAs (p, "Felt Upright", "Keys");
    savePresetAs (p, "Arp - Glass Run");            // (a prefix: Arpeggios)
    savePresetAs (p, "Warm Hymn");                  // (nothing to go by: Uncategorized)
    savePresetAs (p, "Sub Weight", "Basses");       // (an older name: Bass)
    REQUIRE (p.libraryService().waitUntilIdle (10000));
    p.refreshPresetIndex();
    REQUIRE (p.loadPreset (OspAudioProcessor::presetFolder().getChildFile ("Felt Upright.osppreset")));
    osp::plugin::PresetBar bar (p);
    const auto menu = bar.presetMenu();
    const auto top = navEntries (menu);
    juce::StringArray headers;
    for (const auto& e : top)
        if (e.header)
            headers.add (e.text);
    CHECK (headers == juce::StringArray { "COLLECTIONS", "CATEGORIES" });   // (no packs installed)
    for (const auto* name : { "INIT", "Reset settings", "Starting States", "Favorites", "Recently Used", "All Presets" })
    {
        INFO (name);
        CHECK (navFind (top, name) != nullptr);
    }
    // Only the categories that hold presets, in the list's order.
    juce::StringArray categories;
    bool inCategories = false;
    for (const auto& e : top)
    {
        if (e.header)
            inCategories = e.text == "CATEGORIES";
        else if (inCategories)
            categories.add (e.text);
    }
    CHECK (categories == juce::StringArray { "Arpeggios", "Bass", "Keys", "Uncategorized" });
    // Keys: its preset, ticked (the loaded one); names kept whole.
    const auto keys = navEntries (*navFind (top, "Keys")->sub);
    REQUIRE (keys.size() == 1);
    CHECK (keys[0].text == "Felt Upright");
    CHECK (keys[0].ticked);
    CHECK (navEntries (*navFind (top, "Arpeggios")->sub)[0].text == "Arp - Glass Run");
    // Starting States: the built-ins and Save Starting State..., apart from presets.
    const auto states = navEntries (*navFind (top, "Starting States")->sub);
    CHECK (navFind (states, "Natural") != nullptr);
    CHECK (navFind (states, juce::String::fromUTF8 ("Save Starting State\xe2\x80\xa6")) != nullptr);
    CHECK (navFind (states, "Felt Upright") == nullptr);
    // All Presets: presets only, alphabetical.
    const auto all = navEntries (*navFind (top, "All Presets")->sub);
    juce::StringArray names;
    for (const auto& e : all)
        names.add (e.text);
    CHECK (names == juce::StringArray { "Arp - Glass Run", "Felt Upright", "Sub Weight", "Warm Hymn" });
    // Nothing renamed or moved: the files are where they were.
    CHECK (OspAudioProcessor::presetFolder().getChildFile ("Arp - Glass Run.osppreset").existsAsFile());
    clearPresets();
}

TEST_CASE ("preset navigation: the arrows walk the collection a preset was chosen from; Starting States only when chosen from them", "[plugin][preset-nav]")
{
    IsolatedPresets isolated;
    clearPresets();
    OspAudioProcessor p;
    for (const auto* name : { "Keys - Alpha", "Keys - Beta", "Keys - Gamma", "Pad - Delta", "Pad - Epsilon" })
        savePresetAs (p, name);
    REQUIRE (p.libraryService().waitUntilIdle (10000));
    p.refreshPresetIndex();
    osp::plugin::PresetBar bar (p);
    auto open = [&bar] (const juce::String& collection, const juce::String& name) {
        const auto menu = bar.presetMenu();
        const auto top = navEntries (menu);
        const auto* c = navFind (top, collection);
        REQUIRE (c != nullptr);
        REQUIRE (c->sub != nullptr);
        const auto items = navEntries (*c->sub);
        const auto* item = navFind (items, name);
        REQUIRE (item != nullptr);
        item->action();
    };
    // From Keys: Keys only, wrapping.
    open ("Keys", "Keys - Beta");
    CHECK (p.presetDisplayName() == "Keys - Beta");
    CHECK (p.browseContext() == "category:Keys");
    p.stepPresetList (1);
    CHECK (p.presetDisplayName() == "Keys - Gamma");
    p.stepPresetList (1);
    CHECK (p.presetDisplayName() == "Keys - Alpha");   // (wraps within Keys, never into Pads)
    p.stepPresetList (-1);
    CHECK (p.presetDisplayName() == "Keys - Gamma");
    // From All Presets: every preset, no Starting State.
    open ("All Presets", "Pad - Epsilon");
    CHECK (p.browseContext() == "all");
    p.stepPresetList (1);
    CHECK (p.presetDisplayName() == "Keys - Alpha");
    // A deleted item drops out: the walk goes on.
    // (as the Library's Move to Trash: the record goes to its trash; the file may stay)
    REQUIRE (p.libraryService().waitUntilIdle (10000));
    p.libraryService().runAndWait ([] (osp::library::Catalog& c) {
        if (const auto id = c.presetWithFile (OspAudioProcessor::presetFolder().getChildFile ("Keys - Beta.osppreset").getFullPathName().toStdString()))
            c.trash (*id);
    });
    p.refreshPresetIndex();
    p.stepPresetList (1);
    CHECK (p.presetDisplayName() == "Keys - Gamma");
    // From Starting States: the states only.
    open ("Starting States", "Alive");
    CHECK (p.browseContext() == "states");
    p.stepPresetList (1);
    CHECK (p.presetDisplayName() == "Floating");
    for (int i = 0; i < 20; ++i)
    {
        p.stepPresetList (1);
        CHECK_FALSE (p.presetDisplayName().startsWith ("Keys"));
    }
    clearPresets();
}

TEST_CASE ("preset navigation: Browse all opens the Library on that category, whose filter lists the same presets", "[plugin][preset-nav][library]")
{
    clearPresets();
    LibraryRig rig;
    for (int i = 1; i <= 24; ++i)
        savePresetAs (rig.p, "Keys - Felt " + juce::String (i).paddedLeft ('0', 2));
    savePresetAs (rig.p, "Mellow One", "keys");          // (stored, any case)
    savePresetAs (rig.p, "Pad - Wide");
    REQUIRE (rig.p.libraryService().waitUntilIdle (10000));
    rig.p.refreshPresetIndex();
    osp::plugin::PresetBar bar (rig.p);
    juce::String browsed;
    bar.onBrowseLibrary = [&browsed] (const juce::String& scope, bool) { browsed = scope; };
    const auto menu = bar.presetMenu();
    const auto top = navEntries (menu);
    const auto* keys = navFind (top, "Keys");
    REQUIRE (keys != nullptr);
    const auto items = navEntries (*keys->sub);
    // 25 Keys presets: 20 rows, then Browse all 25.
    CHECK (items.size() == static_cast<std::size_t> (osp::plugin::PresetBar::flyoutRows + 1));
    const auto& browse = items.back();
    CHECK (browse.text.startsWith ("Browse all 25"));
    browse.action();
    CHECK (browsed == "category:Keys");
    // The Library's Keys: the same 25 (the stored "keys" and the 24 prefixed ones), nothing else.
    rig.panel->setView (osp::plugin::LibraryPanel::View::presets);
    rig.panel->selectScope ("category:Keys");
    REQUIRE (rig.panel->settle());
    std::set<juce::String> library;
    for (int i = 0; i < rig.panel->rowCount(); ++i)
        library.insert (rig.panel->row (i).name);
    CHECK (library.size() == 25);
    CHECK (library.count ("Mellow One") == 1);
    CHECK (library.count ("Pad - Wide") == 0);
    std::set<juce::String> quick;
    for (const auto& e : rig.p.presetsIn ("category:Keys"))
        quick.insert (e.name);
    CHECK (quick == library);
    // Loading from the Library's Keys: the arrows walk Keys.
    rig.panel->selectRow (0);
    rig.panel->loadSelected();
    REQUIRE (rig.panel->settle());
    CHECK (rig.p.browseContext() == "category:Keys");
    clearPresets();
}

TEST_CASE ("preset navigation: 50, 500 and 5,000 presets - the menu stays compact, every flyout within 21 rows", "[plugin][preset-nav][scale]")
{
    IsolatedPresets isolated;
    for (const int count : { 50, 500, 5000 })
    {
        INFO (count << " presets");
        clearPresets();
        OspAudioProcessor p;
        OspAudioProcessor::presetFolder().createDirectory();
        const char* prefixes[] { "Keys", "Pad", "Lead", "Pluck", "Arp", "Synth", "Strings", "Untitled" };
        for (int i = 0; i < count; ++i)
            OspAudioProcessor::presetFolder().getChildFile (juce::String (prefixes[i % 8]) + " - Preset " + juce::String (i).paddedLeft ('0', 4) + ".osppreset")
                .replaceWithText ("<OSP stateVersion=\"17\"/>");
        p.refreshPresetIndex();
        osp::plugin::PresetBar bar (p);
        const auto started = juce::Time::getMillisecondCounterHiRes();
        const auto menu = bar.presetMenu();
        const double ms = juce::Time::getMillisecondCounterHiRes() - started;
        const auto top = navEntries (menu);
        std::size_t widest = 0;
        for (const auto& e : top)
            if (e.sub != nullptr)
                widest = std::max (widest, navEntries (*e.sub).size());
        UNSCOPED_INFO (count << " presets: " << top.size() << " top-level rows, widest flyout " << widest << ", built in " << ms << " ms");
        CHECK (top.size() <= 20);          // (global actions, 4 collections, at most 8 categories here)
        CHECK (widest <= 21);              // (20 rows and Browse all)
        CHECK (ms < 2000.0);
        std::cout << count << " presets: " << top.size() << " top-level rows, widest flyout " << widest << ", menu built in " << juce::String (ms, 1) << " ms" << std::endl;
    }
    clearPresets();
}

TEST_CASE ("preset navigation: before / after screenshots of the quick preset menu (OSP_PRESET_NAV_SHOTS)", "[.][preset-nav-shots]")
{
    const char* dir = std::getenv ("OSP_PRESET_NAV_SHOTS");
    if (dir == nullptr)
        return;
    const juce::File out (juce::String::fromUTF8 (dir));
    out.createDirectory();
    IsolatedPresets isolated;
    const ScopedLook ceramic (skin::Look::ceramic);
    OspAudioProcessor p;
    OspAudioProcessor::presetFolder().createDirectory();
    const char* names[] { "Keys - Felt Upright", "Keys - Glass Rhodes", "Keys - Night Celesta", "Keys - Toy Piano", "Pad - Slow Bloom", "Pad - Wide Air",
                          "Pad - Frozen Lake", "Lead - Saw Glide", "Lead - Whistle", "Pluck - Kalimba", "Pluck - Harp Run", "Arp - Glass Run", "Arp - Pulse Steps",
                          "Strings - Night Bow", "Strings - Tremolo", "Synth - Wire", "Bells - Tubular", "Voices - Ooh Choir", "Warm Hymn", "Morning Light",
                          "Sub Weight", "Dark Matter", "Brass Swell", "Cathedral" };
    for (const auto* name : names)
        savePresetAs (p, name);
    savePresetAs (p, "Low Felt", "Bass");
    REQUIRE (p.libraryService().waitUntilIdle (10000));
    for (const auto* fav : { "Keys - Felt Upright", "Pad - Slow Bloom", "Morning Light" })
        p.libraryService().runAndWait ([fav] (osp::library::Catalog& c) {
            if (const auto id = c.presetWithFile (OspAudioProcessor::presetFolder().getChildFile (juce::String (fav) + ".osppreset").getFullPathName().toStdString()))
                c.setFavourite (*id, true);
        });
    REQUIRE (p.loadPreset (OspAudioProcessor::presetFolder().getChildFile ("Keys - Glass Rhodes.osppreset")));
    p.refreshPresetIndex();

    struct Stage : juce::Component
    {
        void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xffe9e4da)); }
    } stage;
    osp::plugin::OspLookAndFeel look;
    stage.setLookAndFeel (&look);
    stage.setSize (1100, 1250);
    osp::plugin::PresetBar bar (p);
    stage.addAndMakeVisible (bar);
    bar.setBounds (40, 20, 420, 52);
    bar.refresh();
    auto shoot = [&] (juce::PopupMenu menu, juce::Rectangle<int> target, const juce::String& name, int columns) {
        juce::PopupMenu::dismissAllActiveMenus();
        auto options = juce::PopupMenu::Options().withParentComponent (&stage).withTargetScreenArea (target);
        if (columns > 0)
            options = options.withMaximumNumColumns (columns);
        menu.showMenuAsync (options);
        const auto image = stage.createComponentSnapshot (stage.getLocalBounds(), true, 2.0f);
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        juce::PopupMenu::dismissAllActiveMenus();
    };
    // Before: the flat list (the same build, grouping off).
    p.setGroupsPresets (false);
    shoot (bar.presetMenu(), bar.getBounds(), "before-01-quick-menu", 0);
    // After: grouped, one column; then the Keys flyout beside it.
    p.setGroupsPresets (true);
    const auto menu = bar.presetMenu();
    shoot (menu, bar.getBounds(), "after-01-quick-menu", 1);
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        if (it.getItem().text == "Keys" && it.getItem().subMenu != nullptr)
            shoot (*it.getItem().subMenu, juce::Rectangle<int> (480, 380, 10, 10), "after-02-keys-flyout", 1);
    stage.setLookAndFeel (nullptr);
}
