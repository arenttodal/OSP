// Pedal Loop 1.0 in the plugin (docs/research/pedal-loop): the per-layer PEDAL LOOP parameter,
// its place in the playback-mode menu (no new control on the card), sessions, and the pedal
// through processBlock. Included at the end of PluginProcessorTests.cpp.

namespace
{
    /** One note under the sustain pedal (down first), its key up at 0.5 s, the pedal held to the end. */
    AudioData renderUnderPedal (OspAudioProcessor& p, int note, double seconds)
    {
        const double rate = 48000.0;
        const int block = 256, total = static_cast<int> (seconds * rate);
        p.prepareToPlay (rate, block);
        auto audio = AudioData::allocate (2, total, rate);
        juce::AudioBuffer<float> buffer (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            if (pos == 0)
            {
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
                midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)), 1);
            }
            const int up = static_cast<int> (0.5 * rate);
            if (up >= pos && up < pos + block)
                midi.addEvent (juce::MidiMessage::noteOff (1, note), up - pos);
            buffer.clear();
            p.processBlock (buffer, midi);
            const int n = std::min (block, total - pos);
            for (int ch = 0; ch < 2; ++ch)
                std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + n, audio.channels[static_cast<std::size_t> (ch)].begin() + pos);
        }
        return audio;
    }

    double tailDb (const AudioData& a, double from, double to)
    {
        double sum = 0.0;
        const auto i0 = static_cast<std::size_t> (from * a.sampleRate), i1 = static_cast<std::size_t> (to * a.sampleRate);
        for (auto i = i0; i < i1; ++i)
            sum += static_cast<double> (a.channels[0][i]) * a.channels[0][i];
        return 10.0 * std::log10 (std::max (1.0e-20, sum / static_cast<double> (i1 - i0)));
    }
}

TEST_CASE ("plugin pedal loop: the parameter, sessions, its section of the mode menu, and the pedal through processBlock", "[plugin][ui][pedal-loop]")
{
    using osp::plugin::InstrumentHost;
    TempDir tmp;
    OspAudioProcessor p;
    // A struck note (it plays once today, LOOP on or not).
    auto pluck = testsignals::pluck (midiToHz (57), 2.0, 48000.0, 3, 0.7, 1);
    testsignals::applyFades (pluck, 0.0, 0.8);
    loadAndWait (p, writeSource (tmp.dir, "pluck.wav", pluck));
    const auto id = [] (int l) { return InstrumentHost::pedalLoopParameterId (l); };
    for (int l = 0; l < 3; ++l)
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (p.parameters.getParameter (id (l)));
        REQUIRE (param != nullptr);
        CHECK (param->choices == juce::StringArray { "Normal", "Forward", "Ping-Pong" });
        CHECK (p.parameterValue (id (l)) == 0.0f);   // Normal: the pedal as it was
    }

    // Normal: the recording plays once under the pedal; the setting changed and back is the same sound.
    const auto normal = renderUnderPedal (p, 57, 4.0);
    CHECK (tailDb (normal, 3.0, 4.0) < -100.0);
    p.setParameterValue (id (0), 2.0f);
    const auto pingPong = renderUnderPedal (p, 57, 4.0);
    CHECK (tailDb (pingPong, 3.0, 4.0) > -60.0);   // held past the recording's 2 s
    p.setParameterValue (id (0), 0.0f);
    CHECK (audioHash (renderUnderPedal (p, 57, 4.0)) == audioHash (normal));

    // Sessions: saved and recalled; a session from before has no such parameter and opens Normal.
    p.setParameterValue (id (0), 1.0f);
    p.setParameterValue (id (2), 2.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    {
        OspAudioProcessor q;
        q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        CHECK (q.parameterValue (id (0)) == 1.0f);
        CHECK (q.parameterValue (id (1)) == 0.0f);
        CHECK (q.parameterValue (id (2)) == 2.0f);
    }
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
        REQUIRE (xml != nullptr);
        for (int l = 0; l < 3; ++l)
            for (auto* e = xml->getFirstChildElement(); e != nullptr;)
            {
                auto* next = e->getNextElement();
                if (e->getStringAttribute ("id") == id (l))
                    xml->removeChildElement (e, true);
                e = next;
            }
        juce::MemoryBlock older;
        juce::AudioProcessor::copyXmlToBinary (*xml, older);
        OspAudioProcessor q;
        q.setParameterValue (id (0), 2.0f);
        q.setStateInformation (older.getData(), static_cast<int> (older.getSize()));
        for (int l = 0; l < 3; ++l)
            CHECK (q.parameterValue (id (l)) == 0.0f);
    }
    p.setParameterValue (id (0), 0.0f);
    p.setParameterValue (id (2), 0.0f);

    // The mode menu: the modes, then PEDAL LOOP's three choices with their lines and a check.
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
    REQUIRE (ui != nullptr);
    e->setSize (1448, 1086);
    ui->refreshNow();
    auto* selector = ui->engineCard (0).modeSelector();
    REQUIRE (selector != nullptr);
    REQUIRE (selector->hasPedalLoop());
    auto items = [] (juce::PopupMenu menu) {
        std::vector<juce::PopupMenu::Item> out;
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            out.push_back (it.getItem());
        return out;
    };
    {
        const auto menu = items (selector->createMenu());
        REQUIRE (menu.size() == 7);
        CHECK (menu[0].text == "One Shot");
        CHECK (menu[1].text == "Granular");
        CHECK (menu[2].isSeparator);
        CHECK (menu[3].isSectionHeader);
        CHECK (menu[3].text == "PEDAL LOOP");
        CHECK (menu[4].text == "Normal");
        CHECK (menu[5].text == "Forward");
        CHECK (menu[6].text == "Ping-Pong");
        CHECK (menu[4].isTicked);
        CHECK (menu[6].isEnabled);
        CHECK (menu[4].customComponent->getDescription() == "Standard sustain pedal");
        CHECK (menu[5].customComponent->getDescription() == "Hold a forward loop");
        CHECK (menu[6].customComponent->getDescription() == "Hold a forward/reverse loop");
        // Choosing Ping-Pong: the parameter follows; one undo step; the playback mode untouched.
        p.parameters.copyState();   // (earlier changes settled into the tree first)
        p.undo().clearUndoHistory();
        p.undo().beginNewTransaction();
        menu[6].action();
        p.parameters.copyState();   // (the value tree takes the change, and the undo manager with it)
    }
    CHECK (p.parameterValue (id (0)) == 2.0f);
    CHECK (selector->pedalLoopMode() == 2);
    CHECK (selector->mode() == 0);
    REQUIRE (p.undo().undo());
    CHECK (p.parameterValue (id (0)) == 0.0f);
    // In Granular the section stays, inactive, its choice kept.
    p.setParameterValue (id (0), 1.0f);
    p.setParameterValue (InstrumentHost::layerParameterId (0, "sourceMode"), 1.0f);
    {
        const auto menu = items (selector->createMenu());
        REQUIRE (menu.size() == 7);
        CHECK (menu[5].isTicked);
        CHECK_FALSE (menu[4].isEnabled);
        CHECK_FALSE (menu[5].isEnabled);
        CHECK_FALSE (menu[6].isEnabled);
    }
    p.setParameterValue (InstrumentHost::layerParameterId (0, "sourceMode"), 0.0f);
    p.setParameterValue (id (0), 0.0f);
    // No new control on the card: the selector is the only key that knows the setting.
    CHECK (ui->engineCard (0).getNumChildComponents() == ui->engineCard (1).getNumChildComponents());

    // A recording with nothing to loop (a click): a muted line under the choices.
    {
        OspAudioProcessor q;
        loadAndWait (q, writeSource (tmp.dir, "click.wav", testsignals::impulse (0.5, 48000.0, 0.1, 0.9)));
        std::unique_ptr<juce::AudioProcessorEditor> qe (q.createEditorIfNeeded());
        auto* qui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (qe.get());
        REQUIRE (qui != nullptr);
        qe->setSize (1448, 1086);
        qui->refreshNow();
        const auto menu = items (qui->engineCard (0).modeSelector()->createMenu());
        REQUIRE (menu.size() == 8);
        CHECK (menu[7].text == "No suitable loop found for this recording");
        CHECK_FALSE (menu[7].isEnabled);
    }
}

TEST_CASE ("plugin pedal loop: screenshots of the mode menu with PEDAL LOOP (OSP_PEDAL_SHOTS=<dir>)", "[.pedal-shots]")
{
    const char* where = std::getenv ("OSP_PEDAL_SHOTS");
    REQUIRE (where != nullptr);
    const juce::File dir (where);
    dir.createDirectory();
    TempDir tmp;
    for (const auto& [name, size, granular] : std::vector<std::tuple<std::string, juce::Point<int>, bool>> {
             { "pedal-loop-menu-oneshot", { 1448, 1086 }, false }, { "pedal-loop-menu-granular", { 1448, 1086 }, true }, { "pedal-loop-menu-small", { 1086, 814 }, false } })
    {
        OspAudioProcessor p;
        auto pluck = testsignals::pluck (midiToHz (57), 2.0, 48000.0, 3, 0.7, 1);
        testsignals::applyFades (pluck, 0.0, 0.8);
        loadAndWait (p, writeSource (tmp.dir, "pluck.wav", pluck));
        p.setParameterValue (osp::plugin::InstrumentHost::pedalLoopParameterId (0), 2.0f);
        if (granular)
            p.setParameterValue (osp::plugin::InstrumentHost::layerParameterId (0, "sourceMode"), 1.0f);
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
        REQUIRE (ui != nullptr);
        e->setSize (size.x, size.y);
        ui->refreshNow();
        ui->refreshNow();
        auto* selector = ui->engineCard (0).modeSelector();
        REQUIRE (selector != nullptr);
        auto menu = selector->createMenuPicture();
        const auto below = e->getLocalArea (selector, selector->getLocalBounds()).getBottomLeft();
        menu->setTopLeftPosition (std::min (below.x, e->getWidth() - menu->getWidth() - 8), below.y - 3);
        e->addAndMakeVisible (*menu);
        for (const float scale : { 1.0f, 2.0f })
        {
            const auto image = e->createComponentSnapshot (e->getLocalBounds(), true, scale);
            juce::FileOutputStream out (dir.getChildFile (name + (scale > 1.5f ? "@2x.png" : "@1x.png")));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
        e->removeChildComponent (menu.get());
    }
}
