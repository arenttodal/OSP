// The built-in starting states are whole states: .ospstate files embedded from
// apps/plugin/Resources/StartingStates ("NN Name.ospstate": the order and the name). Choosing one
// applies all of it (settings only: the sounds stay), as a starting state from the Library does,
// and a new instance opens on the first, so the header and the values agree.
// Re-bake the files from the current built-ins: OSP_BAKE_STATES=<folder> osp_plugin_tests "[bake-starting-states]".
// Included at the end of PluginProcessorTests.cpp.

TEST_CASE ("built-in states: bake the built-ins as .ospstate files (OSP_BAKE_STATES)", "[.][bake-starting-states]")
{
    const char* dir = std::getenv ("OSP_BAKE_STATES");
    if (dir == nullptr)
        return;
    const juce::File out (juce::String::fromUTF8 (dir));
    out.createDirectory();
    const int count = OspAudioProcessor().getNumPrograms();
    for (int i = 0; i < count; ++i)
    {
        OspAudioProcessor p;
        p.setCurrentProgram (i);
        const auto name = juce::String (i + 1).paddedLeft ('0', 2) + " " + p.getProgramName (i);
        REQUIRE (p.saveStartingState (out.getChildFile (name + ".ospstate")));
    }
}

namespace
{
    /** Turns a new instance's first-state opening on for one scope (the test runner keeps it off). */
    struct ScopedFirstState
    {
        ScopedFirstState() { OspAudioProcessor::setNewInstancesOpenOnFirstState (true); }
        ~ScopedFirstState() { OspAudioProcessor::setNewInstancesOpenOnFirstState (false); }
    };
}

TEST_CASE ("built-in states: seven embedded whole states, in file order, named by their files", "[plugin][built-in-states]")
{
    OspAudioProcessor p;
    REQUIRE (p.getNumPrograms() == 7);
    const juce::StringArray names { "Natural", "Alive", "Floating", "Broken", "Frozen", "Dream", "Wide" };
    for (int i = 0; i < 7; ++i)
        CHECK (p.getProgramName (i) == names[i]);
    // The header's list and the Library's tiles read the same names.
    const auto list = p.presetList();
    REQUIRE (list.size() >= 7);
    for (int i = 0; i < 7; ++i)
    {
        CHECK (list[static_cast<std::size_t> (i)].name == names[i]);
        CHECK (list[static_cast<std::size_t> (i)].program == i);
    }
}

TEST_CASE ("built-in states: choosing one applies all of it - every setting, not only the macros - and the sounds stay", "[plugin][built-in-states]")
{
    TempDir tmp;
    OspAudioProcessor p;
    juce::Array<juce::File> files;
    files.add (writeSource (tmp.dir, "pad.wav", testsignals::vowel (midiToHz (60), 1.0, 48000.0, 3)));
    p.addLayers (files);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    REQUIRE (p.isLayerOccupied (0));
    // A setting outside the old eight (LIFE, DYNAMICS, CHARACTER, MOTION, SPACE, REIMAGINED,
    // attack, release): the old states left it as it was; a whole state sets it.
    auto* blend = p.apvts().getParameter ("ab.blend");
    REQUIRE (blend != nullptr);
    blend->setValueNotifyingHost (0.9f);
    auto* tune = p.apvts().getParameter ("layerA.tune");
    REQUIRE (tune != nullptr);
    tune->setValueNotifyingHost (tune->convertTo0to1 (5.0f));
    p.setCurrentProgram (1);   // Alive
    CHECK (blend->getValue() == Catch::Approx (blend->getDefaultValue()).margin (1.0e-4));
    CHECK (p.parameterValue ("layerA.tune") == Catch::Approx (0.0f).margin (1.0e-3));
    // Its own values (as the table had them: LIFE 70).
    CHECK (p.parameterValue ("life") == Catch::Approx (70.0f).margin (0.01f));
    CHECK (p.presetDisplayName() == "Alive");
    CHECK (p.activeBuiltInState() == 1);
    // Settings only: the loaded sound plays on.
    CHECK (p.isLayerOccupied (0));
    // Every built-in sets the table's eight the same way as before.
    const float life[] = { 45, 70, 35, 85, 10, 40, 40 };
    for (int i = 0; i < 7; ++i)
    {
        p.setCurrentProgram (i);
        CHECK (p.parameterValue ("life") == Catch::Approx (life[i]).margin (0.01f));
    }
}

TEST_CASE ("built-in states: a new instance opens on the first, whole; a session restored after it wins", "[plugin][built-in-states]")
{
    juce::MemoryBlock alive;
    {
        OspAudioProcessor reference;   // (opening off: defaults)
        reference.setCurrentProgram (1);
        reference.getStateInformation (alive);
    }
    const ScopedFirstState opening;
    OspAudioProcessor p;
    CHECK (p.presetDisplayName() == "Natural");
    CHECK (p.activeBuiltInState() == 0);
    CHECK (p.parameterValue ("life") == Catch::Approx (45.0f).margin (0.01f));   // the header and the values agree
    CHECK_FALSE (p.undo().canUndo());                                             // opening is not an edit
    // The host restores a session next: it replaces the first state entirely.
    p.setStateInformation (alive.getData(), static_cast<int> (alive.getSize()));
    CHECK (p.parameterValue ("life") == Catch::Approx (70.0f).margin (0.01f));
    CHECK (p.presetDisplayName() == "Alive");
}

TEST_CASE ("built-in states: a preset saved while on a starting state becomes the one shown and stepped from", "[plugin][built-in-states][preset-bar]")
{
    TempDir tmp;
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedFirstState opening;
    OspAudioProcessor p;
    REQUIRE (p.presetDisplayName() == "Natural");
    const auto file = tmp.dir.getChildFile ("My Pad.osppreset");
    REQUIRE (p.savePreset (file));
    CHECK (p.presetDisplayName() == "My Pad");   // the saved preset, not the state it began from
    CHECK (p.activeBuiltInState() < 0);
    CHECK (p.canSaveInPlace());                   // SAVE writes to it again
    // Choosing a starting state afterwards shows that state again.
    p.setCurrentProgram (1);
    CHECK (p.presetDisplayName() == "Alive");
}
