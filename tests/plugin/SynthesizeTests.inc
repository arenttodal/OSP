// SYNTHESIZE in ANDOR OSP (Phase S1, docs/research/synthesize): the per-layer host parameter
// (appended, 0 by default), its persistence and older sessions, and the oscillator's life inside
// the plugin's voices: pedal, stealing, all-notes-off, a One Shot that ends while held, mute and
// solo, A/B/C independence and an Atlas key left silent. The DSP itself (tuning, aliasing, blend
// laws, loudness) is tested headless in tests/unit/SynthesizeTests.cpp.
// Included after UnifiedPhase5Tests.inc (its helpers: loadLayers, renderHeld, audioHash, chord8,
// phraseRecording, settleEdits, heldLevelDb).

namespace
{
    juce::String synthId (int layer) { return osp::plugin::InstrumentHost::synthesizeId (layer); }

    /** Renders `seconds` in 256-sample blocks; `events (block)` adds the MIDI of each block. */
    AudioData renderBlocks (OspAudioProcessor& p, double seconds, const std::function<void (int, juce::MidiBuffer&)>& events)
    {
        const double rate = 48000.0;
        const int block = 256, blocks = static_cast<int> (seconds * rate) / block;
        auto audio = AudioData::allocate (2, static_cast<std::int64_t> (blocks) * block, rate);
        juce::AudioBuffer<float> buffer (2, block);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            events (b, midi);
            buffer.clear();
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + block,
                           audio.channels[static_cast<std::size_t> (ch)].begin() + static_cast<std::ptrdiff_t> (b) * block);
        }
        return audio;
    }

    double rmsDb (const AudioData& a, double fromSeconds, double toSeconds)
    {
        const auto from = static_cast<std::size_t> (fromSeconds * a.sampleRate), to = std::min (a.channels[0].size(), static_cast<std::size_t> (toSeconds * a.sampleRate));
        double sum = 0.0;
        for (const auto& ch : a.channels)
            for (std::size_t i = from; i < to; ++i)
                sum += static_cast<double> (ch[i]) * ch[i];
        return 10.0 * std::log10 (sum / static_cast<double> (std::max<std::size_t> (1, 2 * (to - from))) + 1.0e-30);
    }

    bool finite (const AudioData& a)
    {
        for (const auto& ch : a.channels)
            for (float x : ch)
                if (! std::isfinite (x))
                    return false;
        return true;
    }
}

TEST_CASE ("plugin synthesize: one parameter per layer, appended, 0 by default; saved and reopened", "[plugin][synthesize]")
{
    TempDir tmp;
    juce::MemoryBlock state;
    {
        OspAudioProcessor p;
        loadLayers (p, tmp.dir, 3);
        for (int l = 0; l < 3; ++l)
        {
            auto* param = p.parameters.getParameter (synthId (l));
            REQUIRE (param != nullptr);
            CHECK (param->getVersionHint() == 22);
            CHECK (p.parameterValue (synthId (l)) == 0.0f);
            CHECK (param->getCurrentValueAsText() == "0 %");
        }
        p.setParameterValue (synthId (0), 35.0f);
        p.setParameterValue (synthId (2), 80.0f);
        p.getStateInformation (state);
    }
    OspAudioProcessor reopened;
    reopened.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (reopened.waitForLoads (30000));
    reopened.pollLoads();
    CHECK (reopened.parameterValue (synthId (0)) == Approx (35.0f));
    CHECK (reopened.parameterValue (synthId (1)) == 0.0f);
    CHECK (reopened.parameterValue (synthId (2)) == Approx (80.0f));

    // A session from before SYNTHESIZE (no such parameters in it) opens with it off.
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (xml != nullptr);
    for (auto* child = xml->getFirstChildElement(); child != nullptr;)
    {
        auto* next = child->getNextElement();
        if (child->getStringAttribute ("id").endsWith (".synthesize"))
            xml->removeChildElement (child, true);
        child = next;
    }
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    reopened.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    REQUIRE (reopened.waitForLoads (30000));
    reopened.pollLoads();
    for (int l = 0; l < 3; ++l)
        CHECK (reopened.parameterValue (synthId (l)) == 0.0f);
}

TEST_CASE ("plugin synthesize: 0 % is exactly the instrument without it, in One Shot and Granular", "[plugin][synthesize]")
{
    TempDir tmp;
    OspAudioProcessor p;
    loadLayers (p, tmp.dir, 2);
    for (const float mode : { 0.0f, 1.0f })
    {
        p.setParameterValue ("layerA.sourceMode", mode);
        const auto plain = audioHash (renderHeld (p, chord8, 1.5));
        p.setParameterValue (synthId (0), 60.0f);
        const auto blended = renderHeld (p, chord8, 1.5);
        CHECK (audioHash (blended) != plain);
        CHECK (finite (blended));
        // ... and back to 0: nothing of it is left behind.
        p.setParameterValue (synthId (0), 0.0f);
        CHECK (audioHash (renderHeld (p, chord8, 1.5)) == plain);
    }
}

TEST_CASE ("plugin synthesize: each layer's own; MUTE and SOLO silence its oscillator with it", "[plugin][synthesize]")
{
    TempDir tmp;
    OspAudioProcessor p;
    loadLayers (p, tmp.dir, 3);
    const std::vector<int> chord { 48, 55, 64 };
    // B muted: B's SYNTHESIZE is not heard at all.
    p.setParameterValue ("layerB.mute", 1.0f);
    const auto withoutB = audioHash (renderHeld (p, chord, 1.0));
    p.setParameterValue (synthId (1), 100.0f);
    CHECK (audioHash (renderHeld (p, chord, 1.0)) == withoutB);
    // A soloed: neither B's nor C's is heard; A's own is.
    p.setParameterValue ("layerB.mute", 0.0f);
    p.setParameterValue (synthId (2), 100.0f);
    p.setParameterValue ("layerA.solo", 1.0f);
    const auto soloA = audioHash (renderHeld (p, chord, 1.0));
    p.setParameterValue (synthId (1), 0.0f);
    p.setParameterValue (synthId (2), 0.0f);
    CHECK (audioHash (renderHeld (p, chord, 1.0)) == soloA);
    p.setParameterValue (synthId (0), 100.0f);
    CHECK (audioHash (renderHeld (p, chord, 1.0)) != soloA);
    // Each layer's amount reaches only its own voices: A's and C's differ from each other.
    p.setParameterValue ("layerA.solo", 0.0f);
    p.setParameterValue (synthId (0), 0.0f);
    p.setParameterValue (synthId (2), 70.0f);
    const auto onC = audioHash (renderHeld (p, chord, 1.0));
    p.setParameterValue (synthId (2), 0.0f);
    p.setParameterValue (synthId (0), 70.0f);
    CHECK (audioHash (renderHeld (p, chord, 1.0)) != onC);
}

TEST_CASE ("plugin synthesize: no oscillator outlives its voice (pedal, stealing, all notes off)", "[plugin][synthesize][lifecycle]")
{
    TempDir tmp;
    OspAudioProcessor p;
    loadLayers (p, tmp.dir, 3);
    for (int l = 0; l < 3; ++l)
        p.setParameterValue (synthId (l), 100.0f);
    p.prepareToPlay (48000.0, 256);

    // The sustain pedal holds it after the key is let go; letting the pedal go ends it.
    auto pedal = renderBlocks (p, 1.0, [] (int b, juce::MidiBuffer& m) {
        if (b == 0)
        {
            m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
            m.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 1);
        }
        if (b == 20)
            m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    });
    CHECK (p.activeVoices.load() == 1);
    CHECK (rmsDb (pedal, 0.8, 1.0) > -50.0);
    renderBlocks (p, 2.0, [] (int b, juce::MidiBuffer& m) {
        if (b == 0)
            m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);
    });
    CHECK (p.activeVoices.load() == 0);

    // Far more notes than voices (each steals), then ALL NOTES OFF: nothing keeps sounding.
    auto storm = renderBlocks (p, 4.0, [] (int b, juce::MidiBuffer& m) {
        if (b < 60)
            m.addEvent (juce::MidiMessage::noteOn (1, 30 + b, static_cast<juce::uint8> (60 + b % 60)), 0);
        if (b == 400)
            m.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    });
    CHECK (finite (storm));
    CHECK (p.activeVoices.load() == 0);
    // Repeated notes on one key, then panic (all sound off).
    renderBlocks (p, 2.5, [] (int b, juce::MidiBuffer& m) {
        if (b % 8 == 0 && b < 160)
            m.addEvent (juce::MidiMessage::noteOn (1, 64, static_cast<juce::uint8> (90)), 0);
        if (b % 8 == 4 && b < 160)
            m.addEvent (juce::MidiMessage::noteOff (1, 64), 0);
        if (b == 200)
            m.addEvent (juce::MidiMessage::allSoundOff (1), 0);
    });
    CHECK (p.activeVoices.load() == 0);
}

TEST_CASE ("plugin synthesize: a One Shot that ends while held keeps its oscillator until the key is let go", "[plugin][synthesize][lifecycle]")
{
    TempDir tmp;
    OspAudioProcessor p;
    // B's pluck alone (2.5 s), played once (LOOP off).
    const auto files = phase2Sources (tmp.dir);
    p.loadFile (files[1]);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.setParameterValue ("layerA.loop", 0.0f);
    p.prepareToPlay (48000.0, 256);
    auto held = [&p] {
        return renderBlocks (p, 4.0, [] (int b, juce::MidiBuffer& m) {
            if (b == 0)
                m.addEvent (juce::MidiMessage::noteOn (1, 52, static_cast<juce::uint8> (100)), 0);
        });
    };
    const auto plain = held();
    const int voicesPlain = p.activeVoices.load();
    renderBlocks (p, 2.0, [] (int b, juce::MidiBuffer& m) { if (b == 0) m.addEvent (juce::MidiMessage::allNotesOff (1), 0); });
    p.setParameterValue (synthId (0), 50.0f);
    const auto blended = held();
    CHECK (voicesPlain == 0);                    // the recording ended: so did its voice
    CHECK (p.activeVoices.load() == 1);          // with SYNTHESIZE, the held key still sounds
    CHECK (rmsDb (blended, 3.5, 4.0) > rmsDb (plain, 3.5, 4.0) + 20.0);
    renderBlocks (p, 2.0, [] (int b, juce::MidiBuffer& m) { if (b == 0) m.addEvent (juce::MidiMessage::noteOff (1, 52), 0); });
    CHECK (p.activeVoices.load() == 0);
}

TEST_CASE ("plugin synthesize: an Atlas key left silent stays silent; a mapped key plays it", "[plugin][synthesize][atlas]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const auto rec = phraseRecording (tmp.dir, "phrase.wav", { 69, 74, 77, 81 });
    OspAudioProcessor p;
    p.analyseRecording (rec.file, 0);
    settleEdits (p);
    REQUIRE (p.currentInstrument() != nullptr);
    REQUIRE (p.currentInstrument()->atlas != nullptr);
    p.editAtlas (0, "Fill off", [] (atlas::NoteAtlas& a, const AudioData&) {
        a.mapping.fill = false;
        return true;
    });
    settleEdits (p);
    p.setParameterValue (synthId (0), 100.0f);
    CHECK (heldLevelDb (p, 74) > -50.0);
    CHECK (heldLevelDb (p, 90) < -100.0);   // no voice there, so no oscillator either
}

TEST_CASE ("plugin synthesize: automation on held notes allocates nothing and repeats exactly", "[plugin][synthesize]")
{
    TempDir tmp;
    OspAudioProcessor p;
    loadLayers (p, tmp.dir, 2);
    p.setParameterValue ("layerB.sourceMode", 1.0f);
    auto* a = p.parameters.getParameter (synthId (0));
    auto* b = p.parameters.getParameter (synthId (1));
    REQUIRE (a != nullptr);
    REQUIRE (b != nullptr);
    auto automated = [&] {
        return renderHeld (p, chord8, 2.0, [&] (int pos) {
            const float t = static_cast<float> (pos) / 48000.0f;
            a->setValueNotifyingHost (0.5f + 0.5f * std::sin (6.0f * t));
            b->setValueNotifyingHost (std::min (1.0f, t));
        });
    };
    const auto first = automated(), second = automated();
    CHECK (audioHash (first) == audioHash (second));
    CHECK (finite (first));

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 0);
    for (int i = 0; i < 8; ++i)   // warm up every path once, outside the count
    {
        buffer.clear();
        p.processBlock (buffer, midi);
        midi.clear();
    }
    int allocations = 0;
    for (int round = 0; round < 6; ++round)
    {
        a->setValueNotifyingHost (static_cast<float> (round % 3) / 2.0f);
        b->setValueNotifyingHost (static_cast<float> ((round + 1) % 3) / 2.0f);
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 48 + 3 * round, static_cast<juce::uint8> (90)), 0);
        {
            test::AllocationScope scope;
            for (int i = 0; i < 4; ++i)
            {
                buffer.clear();
                p.processBlock (buffer, midi);
                midi.clear();
            }
            allocations += scope.count();
        }
    }
    CHECK (allocations == 0);
}
