// The per-card arpeggiator in ANDOR OSP (version hint 27): the shared lanes
// (plugin::CardArpLanes) on OSP's processor, the engine's layer masks, and old sessions.

namespace cardarp
{
    double energyAt (const std::vector<float>& interleaved, double fromSeconds, double toSeconds, double hz)
    {
        // Goertzel on the left channel.
        const auto a = static_cast<std::size_t> (fromSeconds * 48000.0), b = static_cast<std::size_t> (toSeconds * 48000.0);
        const double w = 2.0 * std::cos (2.0 * juce::MathConstants<double>::pi * hz / 48000.0);
        double s1 = 0.0, s2 = 0.0;
        for (std::size_t i = a; i < b; ++i)
        {
            const double s0 = interleaved[2 * i] + w * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return (s1 * s1 + s2 * s2 - w * s1 * s2) / static_cast<double> (b - a);
    }

    /** Two sustained sine layers: A plays the key, B a fifth above it (its own TUNE). */
    void loadTwo (OspAudioProcessor& p, const juce::File& dir)
    {
        auto sine = [&] (const char* name) {
            auto audio = testsignals::sine (midiToHz (60), 3.0, 48000.0, 0.4, 1);
            testsignals::applyFades (audio, 0.01, 0.05);
            return writeSource (dir, name, audio);
        };
        const juce::File files[] { sine ("a.wav"), sine ("b.wav") };
        REQUIRE (p.addLayers (juce::Array<juce::File> (files, 2)) == 2);
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        p.setParameterValue ("ab.blend", 0.5f);
        p.setParameterValue ("layerB.tune", 7.0f);
        p.setParameterValue ("space", 0.0f);
        p.setParameterValue ("echo", 0.0f);
        p.setParameterValue ("life", 0.0f);
        p.setParameterValue ("release", 40.0f);
    }

    void holdC (int pos, int n, juce::MidiBuffer& m)
    {
        at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)));
    }
}

TEST_CASE ("plugin card arp: parameters, defaults, recall; an older session opens as the one arpeggiator", "[plugin][arp][card-arp]")
{
    OspAudioProcessor p;
    CHECK (p.cardArpeggiator());
    CHECK (p.cardArpNamespace().isEmpty());
    CHECK (arpValue (p, "arp.link") == 1.0f);
    for (const char* id : { "arp.cardA", "arp.cardB", "arp.cardC" })
        CHECK (arpValue (p, id) == 1.0f);
    for (const char* card : { "arp.cardB", "arp.cardC" })
    {
        CHECK (arpValue (p, juce::String (card) + ".pattern") == 0.0f);
        CHECK (arpValue (p, juce::String (card) + ".rate") == 1.0f);
        CHECK (arpValue (p, juce::String (card) + ".octaves") == 1.0f);
    }

    // Saved and recalled.
    p.setParameterValue ("arp.link", 0.0f);
    p.setParameterValue ("arp.cardB", 0.0f);
    p.setParameterValue ("arp.cardC.rate", 3.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    OspAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (arpValue (restored, "arp.link") == 0.0f);
    CHECK (arpValue (restored, "arp.cardB") == 0.0f);
    CHECK (arpValue (restored, "arp.cardC.rate") == 3.0f);

    // A session saved before them (no arp.link, arp.card*): LINK and every card.
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (xml != nullptr);
    for (auto* child = xml->getFirstChildElement(); child != nullptr;)
    {
        auto* next = child->getNextElement();
        if (child->getStringAttribute ("id").startsWith ("arp.link") || child->getStringAttribute ("id").startsWith ("arp.card"))
            xml->removeChildElement (child, true);
        child = next;
    }
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    OspAudioProcessor older;
    older.setParameterValue ("arp.link", 0.0f);   // whatever it held before, the old session wins
    older.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    CHECK (arpValue (older, "arp.link") == 1.0f);
    CHECK (arpValue (older, "arp.cardB") == 1.0f);
}

TEST_CASE ("plugin card arp: LINK with every card is the one arpeggiator, sample for sample", "[plugin][arp][card-arp]")
{
    // The lanes stay out of the way unless a card is left out or LINK is off: a session that
    // never touches them (every session before them) renders exactly as the arpeggiator did.
    TempDir tmp;
    auto render = [&] (bool visitLanes) {
        OspAudioProcessor p;
        cardarp::loadTwo (p, tmp.dir);
        arpOn (p, 2, 2, 60.0f, 2);
        p.prepareToPlay (48000.0, 256);
        // Lanes on and back before any note (or not): nothing sounded, so nothing changes.
        if (visitLanes)
            p.setParameterValue ("arp.link", 0.0f);
        renderArp (p, 48000.0, 256, 0.05, {}, nullptr, 1.0e9, false);
        if (visitLanes)
            p.setParameterValue ("arp.link", 1.0f);
        renderArp (p, 48000.0, 256, 0.05, {}, nullptr, 1.0e9, false);
        return renderArp (p, 48000.0, 256, 1.5, [] (int pos, int n, juce::MidiBuffer& m) {
            for (int note : { 60, 64, 67 })
                at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)));
            at (m, pos, n, 48000.0, 1.0, juce::MidiMessage::allNotesOff (1));
        }, nullptr, 1.0e9, false);
    };
    const auto plain = render (false);
    REQUIRE (plain.playPeak > 1.0e-3);
    const auto visited = render (true);
    CHECK (visited.noteOns == plain.noteOns);
    CHECK (visited.audio == plain.audio);
}

TEST_CASE ("plugin card arp: the cards in it arpeggiate, the others hold the keys", "[plugin][arp][card-arp]")
{
    TempDir tmp;
    const double a = midiToHz (60), b = midiToHz (67);

    SECTION ("PLAYS A: A steps, B holds the key between the steps")
    {
        OspAudioProcessor p;
        cardarp::loadTwo (p, tmp.dir);
        arpOn (p, 0, 1, 25.0f);   // UP, 1/8 (a step every 0.25 s), short gate
        p.setParameterValue ("arp.cardB", 0.0f);
        p.setParameterValue ("arp.cardC", 0.0f);
        const auto run = renderArp (p, 48000.0, 256, 1.0, cardarp::holdC);
        CHECK (run.noteOns == 5);   // A's four steps and B's held key
        // Between two steps (A's short note has ended): B alone.
        const double gapA = cardarp::energyAt (run.audio, 0.18, 0.24, a), gapB = cardarp::energyAt (run.audio, 0.18, 0.24, b);
        CHECK (gapB > 100.0 * gapA);
        // On a step: both.
        const double stepA = cardarp::energyAt (run.audio, 0.26, 0.30, a);
        CHECK (stepA > 100.0 * gapA);
        CHECK (cardarp::energyAt (run.audio, 0.26, 0.30, b) > 0.1 * gapB);
    }

    SECTION ("PLAYS B: the other way round")
    {
        OspAudioProcessor p;
        cardarp::loadTwo (p, tmp.dir);
        arpOn (p, 0, 1, 25.0f);
        p.setParameterValue ("arp.cardA", 0.0f);
        const auto run = renderArp (p, 48000.0, 256, 1.0, cardarp::holdC);
        CHECK (run.noteOns == 5);
        CHECK (cardarp::energyAt (run.audio, 0.18, 0.24, a) > 100.0 * cardarp::energyAt (run.audio, 0.18, 0.24, b));
    }

    SECTION ("unlinked: each card its own RATE")
    {
        OspAudioProcessor p;
        cardarp::loadTwo (p, tmp.dir);
        arpOn (p, 0, 1, 25.0f);                         // A: 1/8
        p.setParameterValue ("arp.link", 0.0f);
        p.setParameterValue ("arp.cardB.rate", 0.0f);   // B: 1/4
        p.setParameterValue ("arp.cardC", 0.0f);        // (no C loaded)
        const auto run = renderArp (p, 48000.0, 256, 1.0, cardarp::holdC);
        CHECK (run.noteOns == 7);   // A at 0, .25, .5, .75; B at 0, .5; the held key (card C, left out)
        // At 0.25 s only A steps.
        CHECK (cardarp::energyAt (run.audio, 0.26, 0.30, a) > 100.0 * cardarp::energyAt (run.audio, 0.26, 0.30, b));
        // The display follows the card the panel edits.
        p.setArpViewCard (1);
        renderArp (p, 48000.0, 256, 0.1, cardarp::holdC, nullptr, 1.0e9, false);
        CHECK (p.arpView().active);
    }

    SECTION ("no card in it: the keys as played (the arpeggiator does not step)")
    {
        OspAudioProcessor p;
        cardarp::loadTwo (p, tmp.dir);
        arpOn (p, 0, 1, 25.0f);
        for (const char* id : { "arp.cardA", "arp.cardB", "arp.cardC" })
            p.setParameterValue (id, 0.0f);
        const auto run = renderArp (p, 48000.0, 256, 1.0, cardarp::holdC);
        CHECK (run.noteOns == 1);
    }
}

TEST_CASE ("plugin card arp: changes mid-chord, the pedal and panic leave nothing hanging; MPE and MONO fall back", "[plugin][arp][card-arp]")
{
    TempDir tmp;
    SECTION ("cards and LINK toggled every few blocks while a chord and the pedal are held")
    {
        OspAudioProcessor p;
        cardarp::loadTwo (p, tmp.dir);
        arpOn (p, 5, 2, 150.0f, 2);
        int block = 0;
        const auto run = renderArp (p, 48000.0, 64, 3.0, [&] (int pos, int n, juce::MidiBuffer& m) {
            for (int note : { 60, 64, 67 })
                at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)));
            at (m, pos, n, 48000.0, 0.5, juce::MidiMessage::controllerEvent (1, 64, 127));
            at (m, pos, n, 48000.0, 1.0, juce::MidiMessage::noteOff (1, 64));
            at (m, pos, n, 48000.0, 2.0, juce::MidiMessage::controllerEvent (1, 64, 0));
            ++block;
            if (block % 23 == 0)
                p.setParameterValue ("arp.link", arpValue (p, "arp.link") > 0.5f ? 0.0f : 1.0f);
            if (block % 31 == 0)
                p.setParameterValue ("arp.cardB", arpValue (p, "arp.cardB") > 0.5f ? 0.0f : 1.0f);
            if (block % 47 == 0)
                p.setParameterValue ("arp.cardA", arpValue (p, "arp.cardA") > 0.5f ? 0.0f : 1.0f);
            if (block % 59 == 0)
                p.setParameterValue ("arp.enabled", arpValue (p, "arp.enabled") > 0.5f ? 0.0f : 1.0f);
        });
        CHECK (run.finite);
        CHECK (run.playPeak > 1.0e-3);
        // Let go of the keys: nothing is left sounding, whichever arrangement it ended in.
        renderArp (p, 48000.0, 64, 0.05, [] (int pos, int, juce::MidiBuffer& m) {
            if (pos == 0)
                for (int note : { 60, 67 })
                    m.addEvent (juce::MidiMessage::noteOff (1, note), 0);
        }, nullptr, 1.0e9, false);
        renderArp (p, 48000.0, 64, 3.0, {}, nullptr, 1.0e9, false);
        CHECK (p.activeVoices.load() == 0);
    }

    SECTION ("a held pass-through key under the pedal ends with the pedal")
    {
        OspAudioProcessor p;
        cardarp::loadTwo (p, tmp.dir);
        arpOn (p, 0, 1, 25.0f);
        p.setParameterValue ("arp.cardB", 0.0f);
        renderArp (p, 48000.0, 256, 2.5, [] (int pos, int n, juce::MidiBuffer& m) {
            at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)));
            at (m, pos, n, 48000.0, 0.1, juce::MidiMessage::controllerEvent (1, 64, 127));
            at (m, pos, n, 48000.0, 0.2, juce::MidiMessage::noteOff (1, 60));
            at (m, pos, n, 48000.0, 1.0, juce::MidiMessage::controllerEvent (1, 64, 0));
        });
        CHECK (p.activeVoices.load() == 0);
    }

    SECTION ("panic (All Notes Off)")
    {
        OspAudioProcessor p;
        cardarp::loadTwo (p, tmp.dir);
        arpOn (p, 0, 1, 25.0f);
        p.setParameterValue ("arp.cardA", 0.0f);
        renderArp (p, 48000.0, 256, 2.0, [] (int pos, int n, juce::MidiBuffer& m) {
            cardarp::holdC (pos, n, m);
            at (m, pos, n, 48000.0, 0.6, juce::MidiMessage::allNotesOff (1));
        });
        CHECK (p.activeVoices.load() == 0);
    }

    for (const char* mode : { "voiceMode", "mpe" })
    {
        // The channels name MPE's notes, MONO keeps one stack: the one arpeggiator, every card.
        CAPTURE (mode);
        OspAudioProcessor p;
        cardarp::loadTwo (p, tmp.dir);
        arpOn (p, 0, 1, 25.0f);
        p.setParameterValue ("arp.cardB", 0.0f);
        p.setParameterValue (mode, 1.0f);
        const auto run = renderArp (p, 48000.0, 256, 1.0, cardarp::holdC);
        CHECK (run.noteOns == 4);   // the steps only: no held key beside them
        // Back to poly: the lanes (their notes start over cleanly).
        p.setParameterValue (mode, 0.0f);
        const auto laned = renderArp (p, 48000.0, 256, 1.0, {}, nullptr, 1.0e9, false);
        CHECK (laned.finite);
        renderArp (p, 48000.0, 256, 0.05, [] (int pos, int, juce::MidiBuffer& m) {
            if (pos == 0)
                m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        }, nullptr, 1.0e9, false);
        renderArp (p, 48000.0, 256, 2.0, {}, nullptr, 1.0e9, false);
        CHECK (p.activeVoices.load() == 0);
    }
}

TEST_CASE ("plugin card arp: no allocation on the audio thread, whatever the arrangement", "[plugin][arp][card-arp]")
{
    TempDir tmp;
    OspAudioProcessor p;
    cardarp::loadTwo (p, tmp.dir);
    arpOn (p, 2, 2, 75.0f, 2);
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);
    auto feed = [&] (int round) {
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 48 + 3 * round, static_cast<juce::uint8> (90)), 0);
        midi.addEvent (juce::MidiMessage::noteOn (1, 55 + 3 * round, static_cast<juce::uint8> (90)), 17);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, round % 2 == 0 ? 127 : 0), 40);
        midi.addEvent (juce::MidiMessage::noteOff (1, 48 + 3 * (round - 1)), 80);
    };
    auto arrange = [&] (int round) {
        p.setParameterValue ("arp.link", round % 2 == 0 ? 0.0f : 1.0f);
        p.setParameterValue ("arp.cardA", round % 3 == 0 ? 0.0f : 1.0f);
        p.setParameterValue ("arp.cardB", round % 4 == 1 ? 0.0f : 1.0f);
    };
    for (int round = 0; round < 12; ++round)   // warm up every path once, outside the count
    {
        arrange (round);
        feed (round);
        for (int i = 0; i < 3; ++i)
        {
            buffer.clear();
            p.processBlock (buffer, midi);
            midi.clear();
        }
    }
    int allocations = 0;
    for (int round = 0; round < 12; ++round)
    {
        arrange (round);
        feed (round);
        test::AllocationScope scope;
        for (int i = 0; i < 3; ++i)
        {
            buffer.clear();
            p.processBlock (buffer, midi);
            midi.clear();
        }
        allocations += scope.count();
    }
    CHECK (allocations == 0);
}
