// The card EQ draws the routing EQUALIZER rack's spectrum (EqSpectrum) behind its curve: what
// plays through the layer's EQ, from the engine's per-layer taps. Included at the end of
// PluginProcessorTests.cpp (after CharacterGraphTests.inc: setPlain, runBlocks).

TEST_CASE ("card eq: the layer's spectrum (the EQUALIZER rack's) shows behind the curve while it plays", "[plugin][card-eq-spectrum]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    OspAudioProcessor p;
    loadAndWait (p, writeSource (tmp.dir, "vowel.wav", testsignals::vowel (midiToHz (48), 2.0, 48000.0, 4)));
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
    REQUIRE (ui != nullptr);
    auto& card = ui->engineCard (0);
    card.onEqToggle (0, true);
    REQUIRE (card.isEqOpen());
    auto* eq = card.eqEditorComponent();
    REQUIRE (eq != nullptr);
    eq->refresh();
    CHECK (eq->spectrumPeakDb() <= osp::plugin::EqSpectrum::floorDb + 1.0f);   // nothing has played

    // A note: the spectrum rises (the EQ itself is off - its input and output are the layer).
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, 48, static_cast<juce::uint8> (100)), 0);
    runBlocks (p, buffer, 40, on);
    CHECK_FALSE (p.layerEqSettings (0).enabled);
    eq->refresh();
    const float playing = eq->spectrumPeakDb();
    CHECK (playing > -60.0f);
    // The same analyser as the routing page's EQUALIZER rack: the same taps would read the same.
    const auto* engine = p.telemetryEngine();
    REQUIRE (engine != nullptr);
    osp::plugin::EqSpectrum reference;
    reference.analyse (engine->layerEqSpectrum (0, false), engine->layerEqSpectrum (0, true));
    CHECK (reference.peakDb() == Approx (playing).margin (0.01));

    // Silence: it falls back to the floor (45 dB/s), never frozen.
    juce::MidiBuffer off;
    off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    runBlocks (p, buffer, 1500, off);
    for (int i = 0; i < 90; ++i)
        eq->refresh();
    CHECK (eq->spectrumPeakDb() < playing - 30.0f);
    p.releaseResources();
}

TEST_CASE ("card eq: spectrum screenshot (OSP_CARD_EQ_SHOTS)", "[.][card-eq-spectrum-shots]")
{
    const char* dir = std::getenv ("OSP_CARD_EQ_SHOTS");
    if (dir == nullptr)
        return;
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedLook ceramic (skin::Look::ceramic);
    OspAudioProcessor p;
    loadAndWait (p, writeSource (tmp.dir, "vowel.wav", testsignals::vowel (midiToHz (48), 2.0, 48000.0, 4)));
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
    REQUIRE (ui != nullptr);
    setPlain (p, "layerA.eq.enabled", 1.0f);
    setPlain (p, "layerA.eq.bell.enabled", 1.0f);
    setPlain (p, "layerA.eq.bell.frequency", 233.0f);
    setPlain (p, "layerA.eq.bell.gain", -7.3f);
    auto& card = ui->engineCard (0);
    card.onEqToggle (0, true);
    auto* eq = card.eqEditorComponent();
    REQUIRE (eq != nullptr);
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, 48, static_cast<juce::uint8> (100)), 0);
    runBlocks (p, buffer, 60, on);
    ui->refreshNow();
    eq->refresh();
    const auto area = ui->getLocalArea (&card, card.getLocalBounds());
    auto image = e->createComponentSnapshot (area, true, 2.0f);
    juce::FileOutputStream out (juce::File (dir).getChildFile ("card-eq-spectrum.png"));
    out.setPosition (0);
    out.truncate();
    juce::PNGImageFormat().writeImageToStream (image, out);
    p.releaseResources();
}
