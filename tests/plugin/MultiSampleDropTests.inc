// Automatic multi-sample mapping, the drop (docs/research/multisample-mapping): several loose
// files dropped together are ONE multi-sample sound in the source they land on - on an empty
// instrument, beside the layers, or on card A, B or C (only that card changes). A single file
// is one sound, as before. Included at the end of PluginProcessorTests.cpp.

namespace
{
    juce::Array<juce::File> pizzSet (const juce::File& dir, const juce::String& prefix, int seed)
    {
        juce::Array<juce::File> files;
        files.add (writeSource (dir, prefix + "_C4_01.wav", testsignals::pluck (midiToHz (60), 1.0, 48000.0, static_cast<std::uint64_t> (seed))));
        files.add (writeSource (dir, prefix + "_C4_02.wav", testsignals::pluck (midiToHz (60), 1.0, 48000.0, static_cast<std::uint64_t> (seed + 1))));
        files.add (writeSource (dir, prefix + "_G4_01.wav", testsignals::pluck (midiToHz (67), 1.0, 48000.0, static_cast<std::uint64_t> (seed + 2))));
        return files;
    }

    juce::StringArray pathsOf (const juce::Array<juce::File>& files)
    {
        juce::StringArray paths;
        for (const auto& f : files)
            paths.add (f.getFullPathName());
        return paths;
    }

    /** Drops `files` on card `layer` (its centre, in the editor's coordinates). */
    void dropOnCard (osp::plugin::OspAudioProcessorEditor& ui, int layer, const juce::Array<juce::File>& files)
    {
        auto& card = ui.engineCard (layer);
        const auto centre = ui.getLocalArea (&card, card.getLocalBounds()).getCentre();
        ui.filesDropped (pathsOf (files), centre.x, centre.y);
    }

    void settle (OspAudioProcessor& p, osp::plugin::OspAudioProcessorEditor& ui)
    {
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        ui.refreshNow();
    }

    juce::StringArray memberNames (OspAudioProcessor& p, int layer)
    {
        juce::StringArray names;
        if (const auto instrument = p.currentInstrument (layer); instrument != nullptr && instrument->set != nullptr)
            for (const auto& m : instrument->set->members)
                names.add (juce::String (m.filename));
        names.sort (true);
        return names;
    }
}

TEST_CASE ("multisample drop: loose files dropped on an empty instrument become one multi-sample sound", "[plugin][multisample]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
    REQUIRE (ui != nullptr);
    const auto files = pizzSet (tmp.dir, "Violin_Pizz", 7);
    ui->filesDropped (pathsOf (files), e->getWidth() / 2, e->getHeight() / 4);
    settle (p, *ui);
    CHECK (p.slotCount() == 1);
    const auto instrument = p.currentInstrument (0);
    REQUIRE (instrument != nullptr);
    REQUIRE (instrument->set != nullptr);
    CHECK (instrument->set->members.size() == 3);
    CHECK (instrument->set->groups.size() == 2);
    // Every key plays (the whole keyboard is filled from the two roots).
    for (int note = 0; note < 128; ++note)
        CHECK (instrument->set->groupFor (note) >= 0);
}

TEST_CASE ("multisample drop: loose files dropped on card A, B or C replace only that card's sound", "[plugin][multisample]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
    REQUIRE (ui != nullptr);
    juce::Array<juce::File> three;
    three.add (writeSource (tmp.dir, "one.wav", testsignals::vowel (midiToHz (57), 1.0, 48000.0, 1)));
    three.add (writeSource (tmp.dir, "two.wav", testsignals::vowel (midiToHz (60), 1.0, 48000.0, 2)));
    three.add (writeSource (tmp.dir, "three.wav", testsignals::vowel (midiToHz (64), 1.0, 48000.0, 3)));
    p.addLayers (three);
    settle (p, *ui);
    REQUIRE (p.slotCount() == 3);
    const char* prefixes[3] { "Violin_Pizz", "Cello_Pizz", "Voice" };
    for (int target = 0; target < 3; ++target)
    {
        INFO ("card " << target);
        std::array<std::shared_ptr<const osp::plugin::LoadedInstrument>, 3> before;
        for (int l = 0; l < 3; ++l)
            before[static_cast<std::size_t> (l)] = p.currentInstrument (l);
        dropOnCard (*ui, target, pizzSet (tmp.dir, prefixes[target], 20 + 10 * target));
        settle (p, *ui);
        CHECK (p.slotCount() == 3);
        CHECK (memberNames (p, target) == juce::StringArray { juce::String (prefixes[target]) + "_C4_01.wav", juce::String (prefixes[target]) + "_C4_02.wav",
                                                              juce::String (prefixes[target]) + "_G4_01.wav" });
        for (int l = 0; l < 3; ++l)
            if (l != target)
                CHECK (p.currentInstrument (l) == before[static_cast<std::size_t> (l)]);   // the others untouched
    }
}

TEST_CASE ("multisample drop: one file is one sound, as before", "[plugin][multisample]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
    REQUIRE (ui != nullptr);
    juce::Array<juce::File> one;
    one.add (writeSource (tmp.dir, "single.wav", testsignals::pluck (midiToHz (60), 1.0, 48000.0, 3)));
    ui->filesDropped (pathsOf (one), e->getWidth() / 2, e->getHeight() / 4);
    settle (p, *ui);
    CHECK (p.slotCount() == 1);
    REQUIRE (p.currentInstrument (0) != nullptr);
    CHECK (p.currentInstrument (0)->set == nullptr);
}

namespace
{
    /** Every "Set" tree in a saved state (one per multi-sample layer). */
    void forEachSet (juce::ValueTree tree, const std::function<void (juce::ValueTree)>& fn)
    {
        if (tree.hasType ("Set"))
            fn (tree);
        for (auto child : tree)
            forEachSet (child, fn);
    }

    juce::StringArray memberOrder (OspAudioProcessor& p, int layer)
    {
        juce::StringArray names;
        if (const auto instrument = p.currentInstrument (layer); instrument != nullptr && instrument->set != nullptr)
            for (const auto& m : instrument->set->members)
                names.add (juce::String (m.filename));
        return names;
    }
}

TEST_CASE ("multisample drop: files are taken in natural name order, whatever order they arrive in", "[plugin][multisample]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    juce::Array<juce::File> files;
    for (const auto* name : { "Pizz_C4_10.wav", "Pizz_C4_2.wav", "Pizz_C4_1.wav" })
        files.add (writeSource (tmp.dir, name, testsignals::pluck (midiToHz (60), 1.0, 48000.0, static_cast<std::uint64_t> (files.size() + 5))));
    juce::StringArray orders[2];
    for (int run = 0; run < 2; ++run)
    {
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
        REQUIRE (ui != nullptr);
        auto paths = pathsOf (files);
        if (run == 1)
            std::reverse (paths.begin(), paths.end());
        ui->filesDropped (paths, e->getWidth() / 2, e->getHeight() / 4);
        settle (p, *ui);
        orders[run] = memberOrder (p, 0);
        const auto instrument = p.currentInstrument (0);
        REQUIRE (instrument != nullptr);
        REQUIRE (instrument->set != nullptr);
        CHECK (instrument->set->cycleTakes);
        CHECK_FALSE (instrument->set->loudnessLayers);
        for (const auto& m : instrument->set->members)
            CHECK (m.take == memberOrder (p, 0).indexOf (juce::String (m.filename)));   // takes in name order
    }
    CHECK (orders[0] == juce::StringArray { "Pizz_C4_1.wav", "Pizz_C4_2.wav", "Pizz_C4_10.wav" });
    CHECK (orders[1] == orders[0]);
}

TEST_CASE ("multisample drop: the mapping is saved; a set saved before it keeps the original rules", "[plugin][multisample]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    juce::MemoryBlock state;
    juce::StringArray order;
    {
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
        REQUIRE (ui != nullptr);
        ui->filesDropped (pathsOf (pizzSet (tmp.dir, "Violin_Pizz", 3)), e->getWidth() / 2, e->getHeight() / 4);
        settle (p, *ui);
        order = memberOrder (p, 0);
        REQUIRE (order.size() == 3);
        // A correction in the Samples inspector keeps the policy.
        p.reassignSample (order[0].toStdString(), SampleRole::roundRobin, 0, std::nullopt);
        settle (p, *ui);
        REQUIRE (p.currentInstrument (0) != nullptr);
        REQUIRE (p.currentInstrument (0)->set != nullptr);
        CHECK (p.currentInstrument (0)->set->cycleTakes);
        CHECK_FALSE (p.currentInstrument (0)->set->loudnessLayers);
        CHECK (p.currentInstrument (0)->assignments.size() == 1);
        p.getStateInformation (state);
    }
    // Recalled as saved: the policy and the member order.
    {
        OspAudioProcessor restored;
        restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        REQUIRE (restored.waitForLoads (30000));
        restored.pollLoads();
        const auto instrument = restored.currentInstrument (0);
        REQUIRE (instrument != nullptr);
        REQUIRE (instrument->set != nullptr);
        CHECK (instrument->set->cycleTakes);
        CHECK_FALSE (instrument->set->loudnessLayers);
        CHECK (memberOrder (restored, 0) == order);
    }
    // The same session as saved before automatic mapping (no policy on the Set): the original rules.
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (xml != nullptr);
    auto tree = juce::ValueTree::fromXml (*xml);
    int sets = 0;
    forEachSet (tree, [&sets] (juce::ValueTree set) {
        ++sets;
        CHECK (set.hasProperty ("cycleTakes"));
        set.removeProperty ("cycleTakes", nullptr);
        set.removeProperty ("loudnessLayers", nullptr);
    });
    REQUIRE (sets == 1);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);
    OspAudioProcessor legacy;
    legacy.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    REQUIRE (legacy.waitForLoads (30000));
    legacy.pollLoads();
    const auto instrument = legacy.currentInstrument (0);
    REQUIRE (instrument != nullptr);
    REQUIRE (instrument->set != nullptr);
    CHECK_FALSE (instrument->set->cycleTakes);
    CHECK (instrument->set->loudnessLayers);
    CHECK (memberOrder (legacy, 0) == order);
}
