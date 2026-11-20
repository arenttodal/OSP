// MIDI LEARN (shared/andor-ui/Source/MidiLearn.h): an external controller turns a control
// (Values snap to each parameter's own step: a margin of 0.002.)
// itself; the audio thread only posts values; mappings live in the session, never presets.

namespace midilearn
{
    void sendCc (OspAudioProcessor& p, int number, int value)
    {
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::controllerEvent (1, number, value), 0);
        buffer.clear();
        p.processBlock (buffer, midi);
    }

    float normalised (OspAudioProcessor& p, const juce::String& id) { return p.apvts().getParameter (id)->getValue(); }
}

TEST_CASE ("midi learn: a learned controller turns the control itself, through the host", "[plugin][midi-learn]")
{
    using namespace midilearn;
    OspAudioProcessor p;
    p.prepareToPlay (48000.0, 256);
    auto* learn = p.midiLearn();
    REQUIRE (learn != nullptr);
    CHECK (learn->controllerFor ("life") == -1);

    learn->learn ("life");
    CHECK (learn->learningParameter() == "life");
    sendCc (p, 21, 100);
    learn->applyPending();   // (the message-thread timer)
    CHECK (learn->learningParameter().isEmpty());
    CHECK (learn->controllerFor ("life") == 21);
    CHECK (normalised (p, "life") == Approx (100.0f / 127.0f).margin (0.002));   // the knob jumped to the controller

    sendCc (p, 21, 0);
    learn->applyPending();
    CHECK (normalised (p, "life") == Approx (0.0f).margin (0.002));

    // An unmapped controller leaves the control alone; learning another control moves nothing else.
    sendCc (p, 22, 127);
    learn->applyPending();
    CHECK (normalised (p, "life") == Approx (0.0f).margin (0.002));

    // One controller turns one control: learning CC 21 for DRIVE takes it from LIFE.
    learn->learn ("drive");
    sendCc (p, 21, 64);
    learn->applyPending();
    CHECK (learn->controllerFor ("drive") == 21);
    CHECK (learn->controllerFor ("life") == -1);
    CHECK (normalised (p, "drive") == Approx (64.0f / 127.0f).margin (0.002));

    learn->forget ("drive");
    CHECK (learn->controllerFor ("drive") == -1);
    CHECK (learn->mappingCount() == 0);

    // Cancelling a learn assigns nothing.
    learn->learn ("life");
    learn->cancelLearn();
    sendCc (p, 30, 10);
    learn->applyPending();
    CHECK (learn->mappingCount() == 0);
}

TEST_CASE ("midi learn: mappings are saved with the session, never with presets", "[plugin][midi-learn]")
{
    using namespace midilearn;
    TempDir tmp;
    juce::MemoryBlock session;
    {
        OspAudioProcessor p;
        p.prepareToPlay (48000.0, 256);
        p.midiLearn()->assign (21, "life");
        p.midiLearn()->assign (74, "character");
        p.getStateInformation (session);
        // A preset is the sound: it carries no mapping.
        const auto preset = tmp.dir.getChildFile ("sound.osppreset");
        REQUIRE (p.savePreset (preset));
        const auto xml = juce::XmlDocument::parse (preset);
        REQUIRE (xml != nullptr);
        CHECK (xml->getChildByName ("MidiLearn") == nullptr);
    }
    OspAudioProcessor q;
    q.prepareToPlay (48000.0, 256);
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    CHECK (q.midiLearn()->controllerFor ("life") == 21);
    CHECK (q.midiLearn()->controllerFor ("character") == 74);
    // Loading a preset keeps the controller setup.
    REQUIRE (q.loadPreset (tmp.dir.getChildFile ("sound.osppreset")));
    CHECK (q.midiLearn()->controllerFor ("life") == 21);
    // The mappings never leak into the parameter state: a preset saved now has none either.
    const auto again = tmp.dir.getChildFile ("again.osppreset");
    REQUIRE (q.savePreset (again));
    const auto againXml = juce::XmlDocument::parse (again);
    REQUIRE (againXml != nullptr);
    CHECK (againXml->getChildByName ("MidiLearn") == nullptr);
    // An older session (no mappings) clears them.
    juce::MemoryBlock older;
    {
        OspAudioProcessor fresh;
        fresh.getStateInformation (older);
    }
    q.setStateInformation (older.getData(), static_cast<int> (older.getSize()));
    CHECK (q.midiLearn()->mappingCount() == 0);
}

TEST_CASE ("midi learn: no allocation on the audio thread", "[plugin][midi-learn]")
{
    OspAudioProcessor p;
    p.prepareToPlay (48000.0, 256);
    p.midiLearn()->assign (21, "life");
    juce::AudioBuffer<float> buffer (2, 256);
    int allocations = 0;
    for (int block = 0; block < 100; ++block)
    {
        juce::MidiBuffer midi;
        midi.ensureSize (64);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 21, block % 128), 0);
        if (block == 50)
            p.midiLearn()->learn ("drive");
        buffer.clear();
        test::AllocationScope scope;
        p.processBlock (buffer, midi);
        allocations += scope.count();
    }
    CHECK (allocations == 0);
}
