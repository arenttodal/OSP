// Pack authoring (Gates 4-6): a pack draft (a collection and its details, kept across sessions
// without exporting); a customer build never publishes factory content; a pack made of a sound,
// a preset and a Starting State installs on a clean machine where every item loads.

#include "library/Archive.h"
#include "library/Packs.h"

namespace
{
    template <typename T>
    T* findTitledIn (juce::Component& root, const juce::String& title)
    {
        for (auto* child : root.getChildren())
        {
            if (auto* t = dynamic_cast<T*> (child); t != nullptr && child->getTitle() == title)
                return t;
            if (auto* t = findTitledIn<T> (*child, title))
                return t;
        }
        return nullptr;
    }
}

TEST_CASE ("packs: Create content pack starts a draft; Save Draft keeps its details without exporting, the version unchanged", "[plugin][library][pack-authoring]")
{
    LibraryRig rig;
    rig.panel->createPackDraft();
    REQUIRE (rig.panel->settle());
    REQUIRE (rig.panel->openSheetName() == "publish");
    auto* sheet = rig.panel->sheet();
    REQUIRE (sheet != nullptr);
    // The draft's details.
    for (const auto& [title, text] : std::initializer_list<std::pair<const char*, const char*>> { { "Pack id", "com.example.strings" }, { "Pack name", "Night Strings" }, { "Creator", "Arn" } })
    {
        auto* field = findTitledIn<juce::TextEditor> (*sheet, title);
        REQUIRE (field != nullptr);
        field->setText (text, true);
    }
    // The customer build: no factory option on the sheet.
    REQUIRE_FALSE (osp::plugin::LibraryPanel::factoryAuthoring());
    for (auto* child : sheet->getChildren())
        if (auto* toggle = dynamic_cast<juce::ToggleButton*> (child))
            CHECK_FALSE (toggle->isVisible());
    auto* draft = findTitledIn<juce::TextButton> (*sheet, "Save the pack's details as a draft");
    REQUIRE (draft != nullptr);
    draft->onClick();
    CHECK (rig.panel->openSheetName().isEmpty());
    CHECK (rig.panel->statusText().contains ("Draft saved"));
    // Nothing exported; the draft's collection holds its content.
    std::string collection;
    rig.p.libraryService().runAndWait ([&] (osp::library::Catalog& c) {
        for (const auto& col : c.collections())
            if (col.name == "New pack")
                collection = col.id;
        if (! collection.empty())
            c.addToCollection (collection, rig.idPluck);
    });
    REQUIRE_FALSE (collection.empty());
    // Reopened (another session): the details are back, the version unchanged.
    rig.panel->openPublishSheet (collection, "New pack");
    REQUIRE (rig.panel->openSheetName() == "publish");
    CHECK (findTitledIn<juce::TextEditor> (*rig.panel->sheet(), "Pack id")->getText() == "com.example.strings");
    CHECK (findTitledIn<juce::TextEditor> (*rig.panel->sheet(), "Pack name")->getText() == "Night Strings");
    CHECK (findTitledIn<juce::TextEditor> (*rig.panel->sheet(), "Version")->getText() == "1.0.0");
    rig.panel->closeSheet();
    // The main menu reaches it: CONTENT holds Create content pack...
    juce::StringArray texts;
    const auto menu = rig.ui->mainMenu();
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        texts.add (it.getItem().text);
    CHECK (texts.contains (juce::String::fromUTF8 ("Create content pack\xe2\x80\xa6")));
}

TEST_CASE ("packs: a customer build publishes an ordinary pack even when asked for a factory one", "[plugin][library][pack-authoring][factory]")
{
    LibraryRig rig;
    REQUIRE_FALSE (osp::plugin::LibraryPanel::factoryAuthoring());
    std::string collection;
    rig.p.libraryService().runAndWait ([&] (osp::library::Catalog& c) {
        collection = *c.createCollection ("Core Try");
        c.addToCollection (collection, rig.idDrone);
    });
    osp::library::PackExport fields;
    fields.id = "com.evenant.osp.core";
    fields.name = "OSP Core";
    fields.factory = true;
    fields.confirmUnknownRights = true;
    const auto out = rig.tmp.dir.getChildFile ("try.osppack");
    rig.panel->exportCollection (collection, fields, out);
    REQUIRE (rig.panel->settle (30000));
    INFO (rig.panel->statusText());
    REQUIRE (out.existsAsFile());
    CHECK (rig.panel->statusText().contains ("Saved the pack "));
    CHECK_FALSE (rig.panel->statusText().contains ("factory"));
    std::string why;
    const auto manifest = osp::library::parsePackManifest (*osp::library::ArchiveReader (out.getFullPathName().toStdString()).readText ("manifest.json", 1 << 20), why);
    REQUIRE (manifest.has_value());
    CHECK_FALSE (manifest->factory);
}

TEST_CASE ("packs: a sound, a preset and a Starting State exported on one machine install on a clean one, where every item loads", "[plugin][library][pack-authoring][clean-install]")
{
    TempDir tmp;
    const auto pack = tmp.dir.getChildFile ("Night Strings 1.0.0.osppack");
    {
        const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("author/library").getFullPathName());
        const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("author/documents").getFullPathName());
        const ScopedEnvironment store ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("author/store").getFullPathName());
        OspAudioProcessor p;
        juce::Array<juce::File> list;
        list.add (writeSource (tmp.dir, "Night Cello A2.wav", testsignals::vowel (midiToHz (45), 1.0, 48000.0, 3)));
        p.addLayers (list);
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        OspAudioProcessor::presetFolder().createDirectory();
        REQUIRE (p.savePreset (OspAudioProcessor::presetFolder().getChildFile ("Night Bow.osppreset")));
        p.setParameterValue ("life", 66.0f);
        REQUIRE (p.saveStartingState (OspAudioProcessor::startingStateFolder().getChildFile ("Night Air.ospstate"), "Air for any source."));
        REQUIRE (p.libraryService().waitUntilIdle (10000));
        std::string collection;
        p.libraryService().runAndWait ([&] (osp::library::Catalog& c) {
            collection = *c.createCollection ("Night Strings");
            for (const auto type : { osp::library::AssetType::sound, osp::library::AssetType::preset, osp::library::AssetType::templateState })
            {
                osp::library::SearchQuery q;
                q.type = type;
                for (const auto& a : c.search (q))
                    c.addToCollection (collection, a.id);
            }
        });
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
        auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
        ui->openLibrary();
        auto* panel = ui->libraryWindow();
        REQUIRE (panel->settle());
        osp::library::PackExport fields;
        fields.id = "com.example.night";
        fields.name = "Night Strings";
        fields.confirmUnknownRights = true;
        panel->exportCollection (collection, fields, pack);
        REQUIRE (panel->settle (30000));
        INFO (panel->statusText());
        REQUIRE (pack.existsAsFile());
        ui->closeLibrary();
    }
    // A clean machine: nothing of the author's; the pack only.
    tmp.dir.getChildFile ("author").deleteRecursively();
    tmp.dir.getChildFile ("Night Cello A2.wav").deleteFile();   // (not even the original recording)
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("clean/library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("clean/documents").getFullPathName());
    const ScopedEnvironment store ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("clean/store").getFullPathName());
    OspAudioProcessor q;
    osp::library::PackResult installed;
    REQUIRE (q.libraryService().runAndWait ([&] (osp::library::Catalog& c) {
        installed = osp::library::installPack (c, pack.getFullPathName().toStdString(), osp::plugin::packPlaces());
    }, 30000));
    INFO (installed.error);
    REQUIRE (installed.ok);
    CHECK (installed.sounds == 1);
    CHECK (installed.presets == 1);
    CHECK (installed.templates == 1);
    // Every item loads: the preset with its recording (from the pack, into the store), the state.
    const auto folder = juce::File (juce::String::fromUTF8 (osp::plugin::packPlaces().packsDir.string().c_str())).getChildFile ("com.example.night");
    const auto presets = folder.getChildFile ("Presets").findChildFiles (juce::File::findFiles, true, "*.osppreset");
    const auto states = folder.getChildFile ("Templates").findChildFiles (juce::File::findFiles, true, "*.ospstate");
    REQUIRE (presets.size() == 1);
    REQUIRE (states.size() == 1);
    REQUIRE (q.loadPreset (presets[0]));
    REQUIRE (q.waitForLoads (30000));
    q.pollLoads();
    CHECK (q.isLayerOccupied (0));
    REQUIRE (q.currentInstrument (0) != nullptr);
    CHECK (q.loadStartingState (states[0]));
    CHECK (q.parameterValue ("life") == Catch::Approx (66.0f).margin (0.01f));
    CHECK (q.isLayerOccupied (0));   // (the state's settings, the sound stays)
    const auto stateXml = juce::XmlDocument::parse (states[0]);
    REQUIRE (stateXml != nullptr);
    CHECK (stateXml->getStringAttribute ("description") == "Air for any source.");
}
