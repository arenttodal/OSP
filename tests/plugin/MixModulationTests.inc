// MIX as a modulation destination: MIX (A <-> B) and MIX TOWARDS C through each route's second
// destination parameter (mod.routeN.destExt), which also brings KEY DNA AMOUNT to the routes.
// The header MIX takes drops and right-clicks; it draws the range and a live node only while
// a route moves it. Included at the end of PluginProcessorTests.cpp.

namespace
{
    juce::StringArray mixMenuItems (const juce::PopupMenu& menu)
    {
        juce::StringArray items;
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            items.add (it.getItem().text);
        return items;
    }

    /** Runs the processor long enough for the engine to have taken the routes. */
    void mixRun (OspAudioProcessor& p, int blocks)
    {
        juce::AudioBuffer<float> buffer (2, 256);
        for (int i = 0; i < blocks; ++i)
        {
            juce::MidiBuffer midi;
            if (i == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, static_cast<juce::uint8> (100)), 0);
            buffer.clear();
            p.processBlock (buffer, midi);
        }
    }
}

TEST_CASE ("plugin: MIX modulation - assigned through destExt, undone, saved; older sessions and dest automation unchanged", "[plugin][mod][mix]")
{
    namespace modui = osp::plugin::modui;
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    using D = mod::Dest;

    // The controls standing for the new destinations.
    CHECK (modui::destinationFor (modui::mixTargetId, false) == D::mixPosition);
    CHECK (modui::destinationsShownBy (modui::mixTargetId) == std::vector<D> { D::mixPosition, D::mixTowardsC });
    for (int l = 0; l < 3; ++l)
        CHECK (modui::destinationFor (OspAudioProcessor::keyDnaAmountId (l), false) == static_cast<D> (static_cast<int> (D::keyDnaAmountA) + l));
    // The second choice list: None, then the destinations after the frozen list, append only.
    const auto& names = OspAudioProcessor::modExtendedDestinationNames();
    REQUIRE (names.size() == mod::extendedChoiceCount);
    CHECK (names[0] == "None");
    CHECK (names[mod::extendedChoiceOf (D::mixPosition)] == "MIX");
    CHECK (names[mod::extendedChoiceOf (D::mixTowardsC)] == "MIX TOWARDS C");
    CHECK (mod::routeDestFromChoices (static_cast<int> (D::life), 0) == D::life);
    CHECK (mod::routeDestFromChoices (static_cast<int> (D::life), mod::extendedChoiceOf (D::mixPosition)) == D::mixPosition);

    const auto a = writeSource (tmp.dir, "first.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "second.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 4));
    OspAudioProcessor p;
    p.loadFile (a, 0);
    p.loadFile (b, 1);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.prepareToPlay (48000.0, 256);

    // Assigned like any destination: the frozen `dest` stays "none", destExt holds MIX.
    const auto made = p.assignModulation (mod::Source::lfo1, D::mixPosition, 50.0f);
    REQUIRE (made.slot >= 0);
    CHECK (p.routeDest (made.slot) == D::mixPosition);
    CHECK (juce::roundToInt (p.parameterValue (OspAudioProcessor::modRouteId (made.slot, "dest"))) == 0);
    CHECK (juce::roundToInt (p.parameterValue (OspAudioProcessor::modRouteId (made.slot, "destExt"))) == mod::extendedChoiceOf (D::mixPosition));
    CHECK (p.assignModulation (mod::Source::lfo1, D::mixPosition, 50.0f).existed);   // the same pairing is selected, not doubled
    // A POLY LFO (one per note) cannot move a shared stage. (VEL and NOTE can: a shared stage
    // follows the latest key, as every shared destination does.)
    p.setParameterValue (OspAudioProcessor::modLfoId (1, "scope"), 1.0f);
    CHECK (p.assignModulation (mod::Source::lfo2, D::mixPosition, 50.0f).slot == -1);
    // Heard: the engine says the mix is modulated.
    mixRun (p, 40);
    CHECK (p.liveMix().modulated);

    // Undo removes it, redo brings it back.
    p.undo().undo();
    CHECK (p.routeDest (made.slot) == D::none);
    CHECK (p.modulationRoutes().empty());
    p.undo().redo();
    CHECK (p.routeDest (made.slot) == D::mixPosition);

    // Saved with the session.
    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    OspAudioProcessor q;
    q.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK (q.routeDest (made.slot) == D::mixPosition);

    // Removing it clears both parameters; a frozen-list destination in the same slot is the
    // `dest` parameter's again (automation of `dest` keeps driving it).
    p.removeModulationRoute (made.slot);
    CHECK (juce::roundToInt (p.parameterValue (OspAudioProcessor::modRouteId (made.slot, "destExt"))) == 0);
    const int life = p.addModulationRoute (mod::Source::lfo1, D::life, 30.0f);
    REQUIRE (life >= 0);
    CHECK (p.routeDest (life) == D::life);
    p.setParameterValue (OspAudioProcessor::modRouteId (life, "dest"), static_cast<float> (static_cast<int> (D::space)));
    CHECK (p.routeDest (life) == D::space);

    // An older session (no destExt at all) opens with every route as it was.
    juce::MemoryBlock withLife;
    p.getStateInformation (withLife);
    auto xml = juce::AudioProcessor::getXmlFromBinary (withLife.getData(), static_cast<int> (withLife.getSize()));
    REQUIRE (xml != nullptr);
    std::vector<juce::XmlElement*> ext;
    for (auto* param : xml->getChildIterator())
        if (param->getStringAttribute ("id").endsWith (".destExt"))
            ext.push_back (param);
    CHECK (ext.size() == static_cast<std::size_t> (mod::maxRoutes));
    for (auto* param : ext)
        xml->removeChildElement (param, true);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    OspAudioProcessor older;
    older.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    CHECK (older.routeDest (life) == D::space);
    for (int r = 0; r < mod::maxRoutes; ++r)
        CHECK (juce::roundToInt (older.parameterValue (OspAudioProcessor::modRouteId (r, "destExt"))) == 0);
}

TEST_CASE ("plugin: the header MIX as a destination - its menu, its drop, the range and live node only while routed", "[plugin][mod][mix]")
{
    namespace modui = osp::plugin::modui;
    using osp::plugin::HeaderMix;
    using osp::plugin::ModulationOverlay;
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const auto a = writeSource (tmp.dir, "first.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "second.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 4));
    const auto c = writeSource (tmp.dir, "third.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 5));
    OspAudioProcessor p;
    p.loadFile (a, 0);
    p.loadFile (b, 1);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.prepareToPlay (48000.0, 256);

    p.setParameterValue ("ab.blend", 0.5f);
    juce::Component root;
    root.setSize (1086, 760);
    ModulationOverlay overlay (p, root);
    HeaderMix mix (p);
    root.addAndMakeVisible (mix);
    mix.setBounds (380, 20, 160, 80);
    mix.setLayers ({ true, true, false });
    mix.finishAnimations();

    // Two layers: MIX only; the sources a shared stage can take (a POLY LFO cannot).
    p.setParameterValue (OspAudioProcessor::modLfoId (1, "scope"), 1.0f);
    overlay.refresh();
    auto items = mixMenuItems (overlay.contextMenu (modui::mixTargetId));
    CHECK (items.contains ("Assign LFO 1"));
    CHECK (items.contains ("Assign MOD WHEEL"));
    CHECK_FALSE (items.contains ("Assign LFO 2"));
    for (const auto& item : items)
        CHECK_FALSE (item.contains ("towards C"));

    // Nothing routed: nothing new is drawn.
    mix.refreshModulation();
    CHECK_FALSE (mix.showsModulation());
    CHECK (mix.modulationRange().isEmpty());

    // A drop on the MIX (in its parent's coordinates) makes the route.
    const auto centre = mix.getPosition().toFloat() + mix.nodePosition();
    CHECK (mix.acceptsModulationDrop (centre));
    CHECK_FALSE (mix.acceptsModulationDrop ({ 10.0f, 600.0f }));
    const int slot = overlay.dropOn (0, modui::mixTargetId, mix.getBounds().toFloat());
    REQUIRE (slot >= 0);
    CHECK (p.routeDest (slot) == mod::Dest::mixPosition);
    CHECK (overlay.dropOn (1, modui::mixTargetId, mix.getBounds().toFloat()) == -1);   // the POLY LFO: refused, says why
    CHECK (overlay.currentNotice().isNotEmpty());

    // Routed and playing: the range (a bipolar LFO at 50 % from the middle: the whole line)
    // and the live node.
    mixRun (p, 40);
    mix.refreshModulation();
    CHECK (mix.showsModulation());
    CHECK (mix.modulationRange().getStart() == Approx (0.0f).margin (1.0e-3f));
    CHECK (mix.modulationRange().getEnd() == Approx (1.0f).margin (1.0e-3f));
    const auto c0 = mix.corners()[0], c1 = mix.corners()[1];
    const auto live = mix.liveNodePosition();
    CHECK (live.x >= c0.x - 0.01f);
    CHECK (live.x <= c1.x + 0.01f);
    overlay.refresh();
    items = mixMenuItems (overlay.contextMenu (modui::mixTargetId));
    CHECK (items.contains ("Edit LFO 1"));
    CHECK (items.contains ("Remove Assignment"));
    // Removed: back to the plain MIX.
    p.removeModulationRoute (slot);
    mixRun (p, 4);
    mix.refreshModulation();
    CHECK_FALSE (mix.showsModulation());

    // Three layers: MIX (A <-> B) and MIX towards C; the routes popover's + ADD lists both.
    p.loadFile (c, 2);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    overlay.refresh();
    items = mixMenuItems (overlay.contextMenu (modui::mixTargetId));
    CHECK (items.contains (juce::String::fromUTF8 ("Assign LFO 1 (A \xe2\x86\x94 B)")));
    CHECK (items.contains ("Assign LFO 1 (towards C)"));
    osp::plugin::ModRoutesPopup routes (p, 0);
    const auto added = mixMenuItems (routes.addMenu());
    CHECK (added.contains ("MIX"));
    CHECK (added.contains ("MIX TOWARDS C"));
    CHECK (added.contains ("KEY DNA A AMOUNT"));
}

TEST_CASE ("plugin: MIX modulation in the editor - drag onto the header MIX, the picture", "[.][ui][mix-ui]")
{
    namespace modui = osp::plugin::modui;
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "Vowel A3.wav", testsignals::vowel (midiToHz (57), 3.0, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "Saw C3.wav", testsignals::saw (midiToHz (48), 2.0, 48000.0));
    OspAudioProcessor p;
    p.loadFile (a, 0);
    p.loadFile (b, 1);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.prepareToPlay (48000.0, 256);
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditorIfNeeded());
    auto* editor = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (base.get());
    REQUIRE (editor != nullptr);
    editor->refreshNow();
    auto& mix = editor->headerMixControl();
    mix.finishAnimations();
    REQUIRE (mix.isVisible());
    const auto centre = mix.getPosition().toFloat() + mix.nodePosition();
    auto snapshot = [&] (const juce::String& name) {
        editor->refreshNow();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        if (const char* dir = std::getenv ("OSP_SNAPSHOT_DIR"))
        {
            const juce::File out = juce::File (dir).getChildFile (name);
            out.deleteFile();
            juce::FileOutputStream stream (out);
            juce::PNGImageFormat().writeImageToStream (image, stream);
        }
    };
    editor->dragModulation (0, centre);
    snapshot ("mix-01-dragging.png");
    const int slot = editor->dropModulation (0, centre);
    REQUIRE (slot >= 0);
    CHECK (p.routeDest (slot) == mod::Dest::mixPosition);
    p.setParameterValue (OspAudioProcessor::modLfoId (0, "rate"), 0.5f);
    for (int i = 0; i < 30; ++i)
    {
        mixRun (p, 4);
        editor->refreshNow();
    }
    CHECK (mix.showsModulation());
    snapshot ("mix-02-modulated.png");
}
