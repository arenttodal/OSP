// Rack modulation, steps 1-2 (docs/research/rack-modulation.md): the FILTER rack's controls
// (MIN, MAX, DRIVE, ENV AMOUNT, ENV ATTACK, ENV DECAY) and each layer's SYNTH page (DETUNE,
// TONE, IMPRINT, MOTION) are modulation destinations, read per voice. They are reached through
// the route's second destination parameter (mod.routeN.destExt, choices appended), so the
// frozen `dest` list and every earlier destExt choice keep their meaning.
// Included after DampenDefaultTests.inc (helpers from UnifiedPhase2Tests.inc and
// SynthesizeTests.inc: loadLayers, renderHeld, audioHash, chord8, finite, synthId).

namespace
{
    using RackDest = osp::mod::Dest;

    RackDest rackLayerDest (RackDest first, int layer) { return static_cast<RackDest> (static_cast<int> (first) + layer); }

    /** A FILTER with room to move: LP24, 200 Hz .. 8 kHz, the knob half way, some envelope. */
    void rackFilterSetup (OspAudioProcessor& p)
    {
        p.setParameterValue ("character.type", 0.0f);
        p.setParameterValue ("character.min", 200.0f);
        p.setParameterValue ("character.max", 8000.0f);
        p.setParameterValue ("character", 50.0f);
        p.setParameterValue ("character.envAmount", 40.0f);
        p.setParameterValue ("character.envAttack", 30.0f);
        p.setParameterValue ("character.envDecay", 400.0f);
        p.setParameterValue ("character.drive", 20.0f);
    }
}

TEST_CASE ("plugin rack modulation: the FILTER rack and SYNTH page controls are destinations, appended to destExt", "[plugin][mod][rackmod]")
{
    namespace modui = osp::plugin::modui;
    using D = mod::Dest;
    using osp::plugin::InstrumentHost;

    // The earlier extended choices keep their places (sessions store the index).
    CHECK (mod::extendedChoiceOf (D::keyDnaAmountA) == 1);
    CHECK (mod::extendedChoiceOf (D::mixPosition) == 4);
    CHECK (mod::extendedChoiceOf (D::synthAmountA) == 6);
    CHECK (mod::extendedChoiceOf (D::vibratoAmount) == 10);
    CHECK (mod::extendedChoiceOf (D::filterMin) == 11);
    CHECK (mod::extendedChoiceOf (D::synthMotionC) == 28);
    // The frozen `dest` list does not move either.
    CHECK (mod::hostDestCount == static_cast<int> (D::keyDnaAmountA));

    const std::vector<std::pair<juce::String, D>> filter { { "character.min", D::filterMin },           { "character.max", D::filterMax },
                                                           { "character.drive", D::filterDrive },       { "character.envAmount", D::filterEnvAmount },
                                                           { "character.envAttack", D::filterEnvAttack }, { "character.envDecay", D::filterEnvDecay } };
    for (const auto& [id, dest] : filter)
    {
        CHECK (modui::destinationFor (id, false) == dest);
        CHECK (modui::destinationFor (id, true) == dest);
        CHECK (modui::parameterFor (dest) == id);   // (its halo finds its knob)
        CHECK (mod::destInfo (dest).owner == mod::Owner::voice);
    }
    for (int l = 0; l < 3; ++l)
    {
        CHECK (modui::destinationFor (InstrumentHost::synthParameterId (l, "detune"), false) == rackLayerDest (D::synthDetuneA, l));
        CHECK (modui::destinationFor (InstrumentHost::synthParameterId (l, "tone"), false) == rackLayerDest (D::synthToneA, l));
        CHECK (modui::destinationFor (InstrumentHost::synthParameterId (l, "imprint"), false) == rackLayerDest (D::synthImprintA, l));
        CHECK (modui::destinationFor (InstrumentHost::synthParameterId (l, "motion"), true) == rackLayerDest (D::synthMotionA, l));
        // (not the switches: WAVE, OCTAVE, SHAPE, PITCH, MODE stay unmodulated)
        CHECK (modui::destinationFor (InstrumentHost::synthParameterId (l, "wave"), false) == D::none);
        CHECK (modui::destinationFor (InstrumentHost::synthParameterId (l, "mode"), false) == D::none);
    }

    const auto& names = OspAudioProcessor::modExtendedDestinationNames();
    REQUIRE (names.size() == mod::extendedChoiceCount);
    CHECK (names[mod::extendedChoiceOf (D::vibratoAmount)] == "VIBRATO AMOUNT");
    CHECK (names[mod::extendedChoiceOf (D::filterEnvDecay)] == "CHARACTER ENV DECAY");
    CHECK (names[mod::extendedChoiceOf (D::synthToneB)] == "SYNTH B TONE");

    // ANDOR OSP names its CHARACTER stage FILTER.
    OspAudioProcessor p;
    CHECK (p.destName (D::filterEnvDecay) == "FILTER ENV DECAY");
    CHECK (p.destName (D::filterMin) == "FILTER MIN");
    CHECK (p.destName (D::synthDetuneA) == "SYNTH A DETUNE");
}

TEST_CASE ("plugin rack modulation: each FILTER rack destination is heard per voice, undone, saved", "[plugin][mod][rackmod]")
{
    using D = mod::Dest;
    TempDir tmp;
    OspAudioProcessor p;
    loadLayers (p, tmp.dir, 1);
    rackFilterSetup (p);
    const auto plain = audioHash (renderHeld (p, chord8, 1.2));
    CHECK (audioHash (renderHeld (p, chord8, 1.2)) == plain);   // (deterministic: the comparison means something)

    for (const auto dest : { D::filterMin, D::filterMax, D::filterDrive, D::filterEnvAmount, D::filterEnvAttack, D::filterEnvDecay })
    {
        INFO (mod::destInfo (dest).id);
        // VEL: a per-voice source with a value from the first sample (the times are taken at note-on).
        CHECK (p.canModulate (mod::Source::vel, dest));
        CHECK (p.canModulate (mod::Source::env1, dest));
        const auto made = p.assignModulation (mod::Source::vel, dest, 80.0f);
        REQUIRE (made.slot >= 0);
        CHECK (p.routeDest (made.slot) == dest);
        CHECK (juce::roundToInt (p.parameterValue (OspAudioProcessor::modRouteId (made.slot, "dest"))) == 0);
        CHECK (juce::roundToInt (p.parameterValue (OspAudioProcessor::modRouteId (made.slot, "destExt"))) == mod::extendedChoiceOf (dest));
        const auto moved = renderHeld (p, chord8, 1.2);
        CHECK (audioHash (moved) != plain);
        CHECK (finite (moved));

        juce::MemoryBlock saved;
        p.getStateInformation (saved);
        {
            OspAudioProcessor q;
            q.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
            CHECK (q.routeDest (made.slot) == dest);
        }
        p.removeModulationRoute (made.slot);
        CHECK (p.routeDest (made.slot) == D::none);
        // The route gone: the voices compute exactly what they did before.
        CHECK (audioHash (renderHeld (p, chord8, 1.2)) == plain);
    }

    // A poly LFO (one per note) reaches the rack too, and the depth's sign matters.
    p.setParameterValue (OspAudioProcessor::modLfoId (0, "scope"), 1.0f);
    const auto up = p.assignModulation (mod::Source::lfo1, D::filterMax, 60.0f);
    REQUIRE (up.slot >= 0);
    const auto upHash = audioHash (renderHeld (p, chord8, 1.2));
    p.setParameterValue (OspAudioProcessor::modRouteId (up.slot, "depth"), -60.0f);
    CHECK (audioHash (renderHeld (p, chord8, 1.2)) != upHash);
}

TEST_CASE ("plugin rack modulation: the SYNTH page's DETUNE, TONE, IMPRINT and MOTION are heard per voice", "[plugin][mod][rackmod][synthesize]")
{
    using D = mod::Dest;
    using osp::plugin::InstrumentHost;
    TempDir tmp;
    OspAudioProcessor p;
    loadLayers (p, tmp.dir, 1);
    p.setParameterValue (synthId (0), 100.0f);
    p.setParameterValue (InstrumentHost::synthParameterId (0, "detune"), 10.0f);
    p.setParameterValue (InstrumentHost::synthParameterId (0, "tone"), 50.0f);
    const auto plain = audioHash (renderHeld (p, chord8, 1.2));

    for (const auto dest : { D::synthDetuneA, D::synthToneA })
    {
        INFO (mod::destInfo (dest).id);
        const auto made = p.assignModulation (mod::Source::vel, dest, 70.0f);
        REQUIRE (made.slot >= 0);
        CHECK (juce::roundToInt (p.parameterValue (OspAudioProcessor::modRouteId (made.slot, "destExt"))) == mod::extendedChoiceOf (dest));
        const auto moved = renderHeld (p, chord8, 1.2);
        CHECK (audioHash (moved) != plain);
        CHECK (finite (moved));
        p.removeModulationRoute (made.slot);
        CHECK (audioHash (renderHeld (p, chord8, 1.2)) == plain);
    }
    // Another layer's destination does nothing to layer A.
    {
        const auto made = p.assignModulation (mod::Source::vel, D::synthToneB, 70.0f);
        REQUIRE (made.slot >= 0);
        CHECK (audioHash (renderHeld (p, chord8, 1.2)) == plain);
        p.removeModulationRoute (made.slot);
    }

    // IMPRINT and MOTION: in Imprint mode (as their knobs), from IMPRINT 40 % and MOTION 0.
    p.setParameterValue (InstrumentHost::synthParameterId (0, "mode"), 1.0f);
    p.setParameterValue (InstrumentHost::synthParameterId (0, "imprint"), 40.0f);
    p.setParameterValue (InstrumentHost::synthParameterId (0, "motion"), 0.0f);
    const auto imprintPlain = audioHash (renderHeld (p, chord8, 1.2));
    for (const auto dest : { D::synthImprintA, D::synthMotionA })
    {
        INFO (mod::destInfo (dest).id);
        const auto made = p.assignModulation (mod::Source::vel, dest, 70.0f);
        REQUIRE (made.slot >= 0);
        const auto moved = renderHeld (p, chord8, 1.2);
        CHECK (audioHash (moved) != imprintPlain);
        CHECK (finite (moved));
        p.removeModulationRoute (made.slot);
    }
    // Analog mode: IMPRINT routes are not heard (the knob is not either).
    p.setParameterValue (InstrumentHost::synthParameterId (0, "mode"), 0.0f);
    const auto analogPlain = audioHash (renderHeld (p, chord8, 1.2));
    const auto made = p.assignModulation (mod::Source::vel, D::synthImprintA, 70.0f);
    REQUIRE (made.slot >= 0);
    CHECK (audioHash (renderHeld (p, chord8, 1.2)) == analogPlain);
}

TEST_CASE ("plugin rack modulation: a right-click on a FILTER rack knob offers the sources", "[plugin][mod][rackmod][ui]")
{
    using osp::plugin::ModulationOverlay;
    OspAudioProcessor p;
    juce::Component root;
    root.setSize (1086, 760);
    ModulationOverlay overlay (p, root);
    auto items = [] (const juce::PopupMenu& menu) {
        juce::StringArray list;
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            list.add (it.getItem().text);
        return list;
    };
    for (const char* id : { "character.envDecay", "character.envAttack", "character.min", "character.max", "character.drive", "character.envAmount" })
    {
        INFO (id);
        const auto list = items (overlay.contextMenu (id));
        CHECK (list.contains ("MODULATION"));
        CHECK (list.contains ("Assign LFO 1"));
        CHECK (list.contains ("Assign VEL"));
        CHECK (list.contains ("Assign MOD WHEEL"));
    }
    const auto synth = items (overlay.contextMenu (osp::plugin::InstrumentHost::synthParameterId (1, "detune")));
    CHECK (synth.contains ("Assign LFO 1"));
}

// Rack modulation steps 3-4: any other control the instrument reads every block (the SPACE,
// ECHO and MOVEMENT racks, the FX library's effects) through a parameter route.

namespace
{
    juce::StringArray rackMenuItems (const juce::PopupMenu& menu)
    {
        juce::StringArray list;
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            list.add (it.getItem().text);
        return list;
    }
}

TEST_CASE ("plugin rack modulation: rack and effect controls take parameter routes; the instrument's own keep theirs", "[plugin][mod][rackmod][parammod]")
{
    using D = mod::Dest;
    using osp::plugin::ModulationOverlay;
    OspAudioProcessor p;
    for (const char* id : { "space.decay", "space.size", "space.damping", "echo.feedback", "echo.time", "echo.tone", "fx.retro.1.mix", "fx.phaser.1.rate", "fx.phaser.2.depth" })
    {
        INFO (id);
        REQUIRE (p.parameters.getParameter (id) != nullptr);
        CHECK (p.parameterModulatable (id));
    }
    // Not: switches and choices, the routes themselves, the seed.
    for (const char* id : { "space.type", "echo.sync", "mod.route1.depth", "seed", "fx.phaser.1.on" })
    {
        INFO (id);
        CHECK_FALSE (p.parameterModulatable (id));
    }
    juce::Component root;
    root.setSize (1086, 760);
    ModulationOverlay overlay (p, root);
    CHECK (overlay.destFor ("space.decay", false) == D::parameter);
    CHECK (overlay.destFor ("fx.retro.1.mix", true) == D::parameter);
    CHECK (overlay.destFor ("space", false) == D::space);                     // (the instrument's own first)
    CHECK (overlay.destFor ("character.envDecay", true) == D::filterEnvDecay);

    // The choice the route parameter stores, after every earlier one.
    CHECK (mod::extendedChoiceOf (D::parameter) == 29);
    CHECK (mod::extendedChoiceOf (mod::paramRouteDest (7)) == 29);
    CHECK (OspAudioProcessor::modExtendedDestinationNames()[29] == "PARAMETER");
    CHECK (mod::routeDestFromChoices (0, 29, 5) == mod::paramRouteDest (5));
    CHECK (mod::destInfo (D::parameter).owner == mod::Owner::global);
}

TEST_CASE ("plugin rack modulation: an ECHO rack control follows an LFO; saved, undone, gone without a trace", "[plugin][mod][rackmod][parammod]")
{
    using D = mod::Dest;
    TempDir tmp;
    OspAudioProcessor p;
    loadLayers (p, tmp.dir, 1);
    p.setParameterValue ("echo", 60.0f);
    p.setParameterValue ("echo.feedback", 40.0f);
    p.setParameterValue (OspAudioProcessor::modLfoId (0, "rate"), 6.0f);
    const auto plain = audioHash (renderHeld (p, chord8, 1.5));
    CHECK (audioHash (renderHeld (p, chord8, 1.5)) == plain);

    // The rules: a shared stage takes no POLY LFO; the bare choice is not an assignment.
    p.setParameterValue (OspAudioProcessor::modLfoId (1, "scope"), 1.0f);
    CHECK_FALSE (p.canModulate (mod::Source::lfo2, D::parameter));
    CHECK (p.assignParameterModulation (mod::Source::lfo2, "echo.feedback").slot < 0);
    CHECK (p.assignModulation (mod::Source::lfo1, D::parameter).slot < 0);
    CHECK (p.assignParameterModulation (mod::Source::lfo1, "space.type").slot < 0);   // (a choice)

    const auto made = p.assignParameterModulation (mod::Source::lfo1, "echo.feedback", 60.0f);
    REQUIRE (made.slot >= 0);
    CHECK_FALSE (made.existed);
    CHECK (p.routeDest (made.slot) == mod::paramRouteDest (made.slot));
    CHECK (p.modRouteParameter (made.slot) == "echo.feedback");
    CHECK (juce::roundToInt (p.parameterValue (OspAudioProcessor::modRouteId (made.slot, "dest"))) == 0);
    CHECK (juce::roundToInt (p.parameterValue (OspAudioProcessor::modRouteId (made.slot, "destExt"))) == 29);
    CHECK (p.destName (p.routeDest (made.slot)) == "ECHO FEEDBACK");
    // The same pairing again selects it, never a second route.
    const auto again = p.assignParameterModulation (mod::Source::lfo1, "echo.feedback");
    CHECK (again.existed);
    CHECK (again.slot == made.slot);
    // Its route list entry names the control.
    bool listed = false;
    for (const auto& r : p.modulationRoutes())
        listed = listed || (r.slot == made.slot && r.parameter == "echo.feedback");
    CHECK (listed);

    const auto moved = renderHeld (p, chord8, 1.5);
    CHECK (audioHash (moved) != plain);
    CHECK (finite (moved));
    CHECK (p.userMacroOffsetOf ("echo.feedback") != 0.0f);   // (on top of the knob, which is never written)
    CHECK (p.parameterValue ("echo.feedback") == Catch::Approx (40.0f));

    // Saved and reopened: the same route to the same control.
    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    {
        OspAudioProcessor q;
        q.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        CHECK (q.routeDest (made.slot) == mod::paramRouteDest (made.slot));
        CHECK (q.modRouteParameter (made.slot) == "echo.feedback");
    }
    // A depth route on it works as on any route.
    const auto meta = p.assignDepthModulation (mod::Source::modWheel, made.slot);
    CHECK (meta.slot >= 0);

    // Removed: the control goes back to exactly what it was.
    p.removeModulationRoute (made.slot);
    CHECK (p.routeDest (made.slot) == D::none);
    CHECK (p.modRouteParameter (made.slot).isEmpty());
    CHECK (audioHash (renderHeld (p, chord8, 1.5)) == plain);
    CHECK (p.userMacroOffsetOf ("echo.feedback") == 0.0f);

    // Undo brings the route back with its control; undo again removes it.
    p.undo().undo();
    CHECK (p.modRouteParameter (made.slot) == "echo.feedback");
}

TEST_CASE ("plugin rack modulation: SPACE rack and FX library controls are heard through parameter routes", "[plugin][mod][rackmod][parammod]")
{
    using namespace osp::fx;
    TempDir tmp;
    OspAudioProcessor p;
    loadLayers (p, tmp.dir, 1);
    p.setParameterValue ("space", 70.0f);
    p.setParameterValue (OspAudioProcessor::modLfoId (0, "rate"), 4.0f);
    {
        const auto plain = audioHash (renderHeld (p, chord8, 1.5));
        const auto made = p.assignParameterModulation (mod::Source::lfo1, "space.damping", 80.0f);
        REQUIRE (made.slot >= 0);
        const auto moved = renderHeld (p, chord8, 1.5);
        CHECK (audioHash (moved) != plain);
        CHECK (finite (moved));
        p.removeModulationRoute (made.slot);
        CHECK (audioHash (renderHeld (p, chord8, 1.5)) == plain);
    }
    // An effect of the FX library in a CUSTOM route.
    osp::fx::Graph g;
    g.custom = true;
    g.chain = { Node::effect ({ Type::phaser, 0 }) };
    REQUIRE (p.setFxRoute (g));
    p.setParameterValue ("fx.phaser.1.mix", 50.0f);
    const auto plain = audioHash (renderHeld (p, chord8, 1.5));
    const auto made = p.assignParameterModulation (mod::Source::env1, "fx.phaser.1.mix", 90.0f);
    REQUIRE (made.slot >= 0);
    const auto moved = renderHeld (p, chord8, 1.5);
    CHECK (audioHash (moved) != plain);
    CHECK (finite (moved));
    // Two routes on one control add up (and to the second control separately).
    const auto second = p.assignParameterModulation (mod::Source::modWheel, "fx.phaser.1.mix", 30.0f);
    CHECK (second.slot >= 0);
    CHECK (second.slot != made.slot);
    CHECK (finite (renderHeld (p, chord8, 1.0)));
}

TEST_CASE ("plugin rack modulation: a right-click on a rack control offers the sources a shared stage takes", "[plugin][mod][rackmod][parammod][ui]")
{
    using osp::plugin::ModulationOverlay;
    OspAudioProcessor p;
    juce::Component root;
    root.setSize (1086, 760);
    ModulationOverlay overlay (p, root);
    p.setParameterValue (OspAudioProcessor::modLfoId (1, "scope"), 1.0f);   // LFO 2 POLY
    auto items = rackMenuItems (overlay.contextMenu ("space.decay"));
    CHECK (items.contains ("MODULATION"));
    CHECK (items.contains ("Assign LFO 1"));
    CHECK (items.contains ("Assign MOD WHEEL"));
    CHECK (items.contains ("Assign ENV"));
    CHECK_FALSE (items.contains ("Assign LFO 2"));
    // Through the overlay's one path, then the menu offers to edit it.
    const int slot = overlay.assign (0, mod::Dest::parameter, "space.decay");
    REQUIRE (slot >= 0);
    CHECK (p.modRouteParameter (slot) == "space.decay");
    overlay.refresh();
    items = rackMenuItems (overlay.contextMenu ("space.decay"));
    CHECK (items.contains ("Edit LFO 1"));
    CHECK (items.contains ("Remove Assignment"));
    // Another control's menu does not show it.
    CHECK_FALSE (rackMenuItems (overlay.contextMenu ("space.size")).contains ("Edit LFO 1"));
    // A choice offers nothing.
    CHECK_FALSE (rackMenuItems (overlay.contextMenu ("space.type")).contains ("Assign LFO 1"));
}

TEST_CASE ("plugin rack modulation: screenshots of rack halos (OSP_RACKMOD_SHOTS)", "[.][rackmod-shots]")
{
    // On demand: OSP_RACKMOD_SHOTS=<folder> osp_plugin_tests "[rackmod-shots]". The SPACE and
    // FILTER racks open, an LFO on SPACE DECAY (a parameter route) and on FILTER ENV DECAY.
    const char* dir = std::getenv ("OSP_RACKMOD_SHOTS");
    if (dir == nullptr)
        return;
    using Editor = osp::plugin::OspAudioProcessorEditor;
    using Kind = osp::plugin::rack::Kind;
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<Editor*> (e.get());
    REQUIRE (ui != nullptr);
    loadLayers (p, tmp.dir, 1);
    REQUIRE (p.assignParameterModulation (mod::Source::lfo1, "space.decay", 60.0f).slot >= 0);
    REQUIRE (p.assignParameterModulation (mod::Source::modWheel, "space.size", -40.0f).slot >= 0);
    REQUIRE (p.assignModulation (mod::Source::env1, mod::Dest::filterEnvDecay, 50.0f).slot >= 0);
    auto save = [dir, &e, ui] (const juce::String& name) {
        ui->refreshNow();
        ui->modulationOverlay().refresh();
        ui->modulationOverlay().repaint();
        juce::FileOutputStream out (juce::File (dir).getChildFile (name + ".png"));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f), out);
    };
    ui->toggleRackFromLabel (Kind::space);
    save ("rackmod-space");
    ui->toggleRackFromLabel (Kind::space);
    const auto filterKind = Editor::macroRackKind ("character");
    REQUIRE (filterKind.has_value());
    ui->toggleRackFromLabel (*filterKind);
    save ("rackmod-filter");
}

TEST_CASE ("plugin rack modulation: the racks lie under the halos (their knobs' routes show)", "[plugin][mod][rackmod][parammod][ui]")
{
    // The rack host was added after the overlay, so it hid every halo on a rack's knobs.
    using Editor = osp::plugin::OspAudioProcessorEditor;
    using Kind = osp::plugin::rack::Kind;
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<Editor*> (e.get());
    REQUIRE (ui != nullptr);
    auto& overlay = ui->modulationOverlay();
    auto* parent = overlay.getParentComponent();
    REQUIRE (parent != nullptr);
    REQUIRE (parent == ui->effectsRack().getParentComponent());
    CHECK (parent->getIndexOfChildComponent (&ui->effectsRack()) < parent->getIndexOfChildComponent (&overlay));
    REQUIRE (p.assignParameterModulation (mod::Source::lfo1, "space.decay", 60.0f).slot >= 0);
    ui->toggleRackFromLabel (Kind::space);
    ui->refreshNow();
    overlay.refresh();
    CHECK (parent->getIndexOfChildComponent (&ui->effectsRack()) < parent->getIndexOfChildComponent (&overlay));
    CHECK (overlay.haloRadius ("space.decay") > 0.0f);
    CHECK (overlay.haloCovers ("space.decay").empty());
    CHECK (overlay.haloRadius ("space.size") == 0.0f);   // (no route: no halo)
}

TEST_CASE ("plugin rack modulation: a USER macro and a parameter route on one control add up; the route gone, the macro's offset stays", "[plugin][mod][rackmod][parammod][usermacros]")
{
    namespace um = osp::plugin::usermacros;
    OspAudioProcessor p;
    loadPluck (p);
    setNormalised (p, "userMacro.1", 0.5f);
    renderNote (p, 4);
    REQUIRE (um::assign (p, 0, "echo.feedback"));
    setNormalised (p, "userMacro.1", 0.9f);
    renderNote (p, 60);   // (the macro's ~20 ms glide settles)
    const float macroOnly = p.userMacroOffsetOf ("echo.feedback");
    REQUIRE (macroOnly != 0.0f);
    // A MOD WHEEL route (a steady value) on top: the two add.
    const auto made = p.assignParameterModulation (mod::Source::modWheel, "echo.feedback", 20.0f);
    REQUIRE (made.slot >= 0);
    juce::AudioBuffer<float> buffer (2, 256);
    for (int b = 0; b < 40; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, 127), 0);   // the wheel all the way
        buffer.clear();
        p.processBlock (buffer, midi);
    }
    const float both = p.userMacroOffsetOf ("echo.feedback");
    CHECK (both == Approx (macroOnly + 0.2f).margin (0.02f));
    // The route removed: the macro's offset alone, exactly as before.
    p.removeModulationRoute (made.slot);
    for (int b = 0; b < 4; ++b)
    {
        juce::MidiBuffer midi;
        buffer.clear();
        p.processBlock (buffer, midi);
    }
    CHECK (p.userMacroOffsetOf ("echo.feedback") == Approx (macroOnly).margin (1.0e-6));
}
