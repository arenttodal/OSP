// LOOP's fallback for short notes in ANDOR OSP (docs/research/short-loop.md): on for every
// sound, but off for the sounds of a session or preset saved before it (state version 16), so
// they play as they did; a sound loaded later has it. Included at the end of
// PluginProcessorTests.cpp.

namespace
{
    /** A short sustained tone (0.4 s): the analysis finds no loop; the fallback holds it. */
    AudioData shortSustained()
    {
        auto a = testsignals::vibratoSine (220.0, 5.0, 10.0, 0.4, 48000.0, 0.4);
        testsignals::applyFades (a, 0.01, 0.03);
        return a;
    }

    /** Holds A3 for 2 s and returns the level (dB) over the last 0.3 s. */
    double shortLoopHeldDb (OspAudioProcessor& p)
    {
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buffer (2, 256);
        double sum = 0.0;
        int counted = 0;
        const int blocks = static_cast<int> (2.0 * 48000.0 / 256.0);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, static_cast<juce::uint8> (100)), 0);
            buffer.clear();
            p.processBlock (buffer, midi);
            if (b >= blocks - static_cast<int> (0.3 * 48000.0 / 256.0))
                for (int i = 0; i < 256; ++i, ++counted)
                    sum += static_cast<double> (buffer.getSample (0, i)) * buffer.getSample (0, i);
        }
        p.releaseResources();
        return 10.0 * std::log10 (std::max (1.0e-20, sum / std::max (1, counted)));
    }
} // namespace

TEST_CASE ("short loop: a new session holds a short note; one saved before the fallback plays it once, until a sound is loaded", "[plugin][short-loop]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const auto file = writeSource (tmp.dir, "short.wav", shortSustained());
    OspAudioProcessor p;
    for (int l = 0; l < 3; ++l)
        CHECK (p.loopFallback (l));
    loadAndWait (p, file);
    REQUIRE (p.loadState (0) == OspAudioProcessor::LoadState::ready);
    const double held = shortLoopHeldDb (p);
    CHECK (held > -45.0);

    // Saved now: recalled as saved.
    juce::MemoryBlock state;
    p.getStateInformation (state);
    {
        OspAudioProcessor q;
        q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        REQUIRE (q.waitForLoads (20000));
        q.pollLoads();
        CHECK (q.loopFallback (0));
    }

    // The same session as an older build saved it (state version 15, no loopFallback): layer A's
    // sound plays once, as it did; the empty layers have the fallback.
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (xml != nullptr);
    xml->setAttribute ("stateVersion", 15);
    xml->removeAttribute ("loopFallback");
    xml->removeAttribute ("loopHold");   // (nor HOLD, which came later)
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    OspAudioProcessor q;
    q.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    REQUIRE (q.waitForLoads (20000));
    q.pollLoads();
    CHECK_FALSE (q.loopFallback (0));
    CHECK (q.loopFallback (1));
    CHECK (q.loopFallback (2));
    const double once = shortLoopHeldDb (q);
    CHECK (once < held - 40.0);
    // Saved again: it stays as it was.
    juce::MemoryBlock resaved;
    q.getStateInformation (resaved);
    {
        OspAudioProcessor r;
        r.setStateInformation (resaved.getData(), static_cast<int> (resaved.getSize()));
        REQUIRE (r.waitForLoads (20000));
        r.pollLoads();
        CHECK_FALSE (r.loopFallback (0));
    }
    // The musician loads a sound into the layer: the fallback holds it.
    loadAndWait (q, writeSource (tmp.dir, "short2.wav", shortSustained()));
    CHECK (q.loopFallback (0));
    CHECK (shortLoopHeldDb (q) > -45.0);
}

TEST_CASE ("LOOP's HOLD: on for new sessions and new sounds, off for a sound saved before it", "[plugin][short-loop][hold]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const auto file = writeSource (tmp.dir, "short.wav", shortSustained());
    OspAudioProcessor p;
    for (int l = 0; l < 3; ++l)
        CHECK (p.loopHold (l));
    loadAndWait (p, file);
    REQUIRE (p.loadState (0) == OspAudioProcessor::LoadState::ready);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    {
        OspAudioProcessor q;
        q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        REQUIRE (q.waitForLoads (20000));
        q.pollLoads();
        CHECK (q.loopHold (0));
    }
    // Saved before HOLD (no loopHold): the layer with a sound plays as it did, empty ones hold.
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (xml != nullptr);
    xml->removeAttribute ("loopHold");
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    OspAudioProcessor q;
    q.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    REQUIRE (q.waitForLoads (20000));
    q.pollLoads();
    CHECK_FALSE (q.loopHold (0));
    CHECK (q.loopHold (1));
    loadAndWait (q, writeSource (tmp.dir, "short2.wav", shortSustained()));
    CHECK (q.loopHold (0));
}

TEST_CASE ("DAMPEN is on after INIT (a new patch); a state without it plays as saved", "[plugin][dampen]")
{
    OspAudioProcessor p;
    p.initPatch();
    for (int l = 0; l < 3; ++l)
        CHECK (p.parameterValue (OspAudioProcessor::dampenParameterId (l, "enabled")) == 1.0f);
    // The parameter's own default is unchanged: a state that never stored it recalls it off.
    OspAudioProcessor fresh;
    CHECK (fresh.parameterValue (OspAudioProcessor::dampenParameterId (0, "enabled")) == 0.0f);
}
