// Starting State authoring (Gate 3): Save as Starting State keeps every setting (macros, USER
// macros, effects and routing, modulation, performance) and nothing of the recordings (no file,
// hash, analysis, root or map); it carries its description; it applies onto other recordings,
// whose own analysis stands; saving one leaves the instrument as it was; the Library lists it
// as a Starting State and renames it as one.

TEST_CASE ("starting states: saved without audio or anything tied to it; the description travels; the instrument is unchanged", "[plugin][starting-state-authoring]")
{
    TempDir tmp;
    OspAudioProcessor p;
    juce::Array<juce::File> list;
    list.add (writeSource (tmp.dir, "Original Cello A2.wav", testsignals::vowel (midiToHz (45), 1.0, 48000.0, 3)));
    list.add (writeSource (tmp.dir, "Original Pluck E4.wav", testsignals::pluck (midiToHz (64), 1.0, 48000.0, 4)));
    p.addLayers (list);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.setRootOverride (std::optional<double> (50.0), 0);   // (a root that belongs to this recording)
    // Settings a starting state carries: a macro, a USER macro assignment, an effect route.
    p.setParameterValue ("life", 77.0f);
    osp::plugin::UserMacroState macros;
    macros.names[0] = "BLOOM";
    macros.maps[0].push_back ({ "layerA.level", {}, false, {} });
    p.setUserMacroAssignments (macros);
    auto route = p.fxRoute();
    route.custom = true;
    REQUIRE (osp::fx::insertEffect (route, {}, static_cast<int> (route.chain.size()), osp::fx::Node::effect ({ osp::fx::Type::eq, 0 })));
    REQUIRE (p.setFxRoute (route));
    // The instrument's identity before: a user preset.
    OspAudioProcessor::presetFolder().createDirectory();
    const auto preset = OspAudioProcessor::presetFolder().getChildFile ("Authoring Source.osppreset");
    REQUIRE (p.savePreset (preset));
    REQUIRE (p.loadPreset (preset));
    const auto nameBefore = p.presetDisplayName();
    REQUIRE (p.canSaveInPlace());

    const auto file = OspAudioProcessor::startingStateFolder().getChildFile ("Bloom Template.ospstate");
    file.deleteFile();
    REQUIRE (p.saveStartingState (file, "A slow bloom for sustained sources."));

    // The instrument is as it was: its name, its preset, what SAVE updates.
    CHECK (p.presetDisplayName() == nameBefore);
    CHECK (p.currentPresetFile() == preset);
    CHECK (p.canSaveInPlace());

    // The file: a Starting State by its own identity, its description, no audio and nothing of it.
    const auto xml = juce::XmlDocument::parse (file);
    REQUIRE (xml != nullptr);
    CHECK (xml->getIntAttribute ("startingState") == 1);
    CHECK (xml->getStringAttribute ("description") == "A slow bloom for sustained sources.");
    const auto text = file.loadFileAsString();
    for (const auto* gone : { "Original Cello", "Original Pluck", ".wav\"", "contentHash", "storedPath", "originalPath", "Atlas" })
    {
        INFO (gone);
        CHECK_FALSE (text.contains (gone));
    }

    // Applied onto other recordings: their sounds and their own analysis stay; the settings come.
    OspAudioProcessor q;
    juce::Array<juce::File> other;
    other.add (writeSource (tmp.dir, "New Voice C4.wav", testsignals::vowel (midiToHz (60), 1.0, 48000.0, 5)));
    q.addLayers (other);
    REQUIRE (q.waitForLoads (30000));
    q.pollLoads();
    REQUIRE (q.loadStartingState (file));
    REQUIRE (q.isLayerOccupied (0));
    CHECK (juce::String::fromUTF8 (q.currentInstrument (0)->filename.c_str()) == "New Voice C4.wav");
    CHECK_FALSE (q.rootOverride (0).has_value());              // (never the old recording's root)
    CHECK (q.parameterValue ("life") == Catch::Approx (77.0f).margin (0.01f));
    REQUIRE (q.userMacroAssignments() != nullptr);
    CHECK (q.userMacroAssignments()->displayName (0) == "BLOOM");
    CHECK (q.userMacroAssignments()->find (0, "layerA.level") >= 0);
    CHECK (osp::fx::contains (q.fxRoute(), { osp::fx::Type::eq, 0 }));
    // Two slots were saved: the second waits, empty, for a sound.
    CHECK (q.slotCount() == 2);
    CHECK (q.isKeptEmptySlot (1));

    // Renamed as content: the file keeps its identity and its description.
    juce::String error;
    const auto renamed = q.renamePresetFile (file, "Bloom", error);
    REQUIRE (renamed.has_value());
    const auto again = juce::XmlDocument::parse (*renamed);
    REQUIRE (again != nullptr);
    CHECK (again->getIntAttribute ("startingState") == 1);
    CHECK (again->getStringAttribute ("description") == "A slow bloom for sustained sources.");
    renamed->deleteFile();
    preset.deleteFile();
}

TEST_CASE ("starting states: the Library lists a user's state as a Starting State, apart from presets of the same name", "[plugin][starting-state-authoring][library]")
{
    LibraryRig rig;
    OspAudioProcessor::presetFolder().createDirectory();
    REQUIRE (rig.p.savePreset (OspAudioProcessor::presetFolder().getChildFile ("Glass Bloom.osppreset")));
    REQUIRE (rig.p.saveStartingState (OspAudioProcessor::startingStateFolder().getChildFile ("Glass Bloom.ospstate"), "Shared name, another type."));
    REQUIRE (rig.p.libraryService().waitUntilIdle (10000));
    rig.panel->setView (osp::plugin::LibraryPanel::View::templates);
    rig.panel->refresh();
    REQUIRE (rig.panel->settle());
    int states = 0;
    for (int i = 0; i < rig.panel->rowCount(); ++i)
        if (rig.panel->row (i).name == "Glass Bloom")
        {
            ++states;
            CHECK (rig.panel->row (i).type == osp::library::AssetType::templateState);
        }
    CHECK (states == 1);
    rig.panel->setView (osp::plugin::LibraryPanel::View::presets);
    rig.panel->refresh();
    REQUIRE (rig.panel->settle());
    int presets = 0;
    for (int i = 0; i < rig.panel->rowCount(); ++i)
        if (rig.panel->row (i).name == "Glass Bloom")
        {
            ++presets;
            CHECK (rig.panel->row (i).type == osp::library::AssetType::preset);
        }
    CHECK (presets == 1);
}
