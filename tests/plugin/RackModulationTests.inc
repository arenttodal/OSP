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
