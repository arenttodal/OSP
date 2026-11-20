// Custom ARP Patterns in the plugins (CP2): CUSTOM per card (version hint 36), recall, older
// sessions, the lock-free hand-over of a card's pattern, the processor's audio (CUSTOM on without
// a pattern is the selected style, sample for sample), the unlinked lanes through the shared
// CardArpLanes, and no allocation on the audio thread.

namespace customtests
{
    arp::CustomPattern withRests()
    {
        // C, rest, G E (halves), rest: two notes and a split over four 1/8 steps.
        arp::CustomPattern p;
        p.maxRank = 2;
        p.mapping = arp::CustomMapping::contour;
        const std::pair<int, int> events[] { { 0, 960 }, { -1, 960 }, { 2, 480 }, { 1, 480 }, { -1, 960 } };
        for (const auto& [rank, ticks] : events)
            p.events[static_cast<std::size_t> (p.count++)] = { static_cast<std::int16_t> (std::max (0, rank)), rank < 0, 0, ticks };
        return p;
    }
}

TEST_CASE ("plugin custom arp: CUSTOM per card, off by default, recalled; an older session opens with it off", "[plugin][arp][arp-custom]")
{
    OspAudioProcessor p;
    for (const char* id : { "arp.custom", "arp.cardB.custom", "arp.cardC.custom" })
        CHECK (arpValue (p, id) == 0.0f);
    p.setParameterValue ("arp.custom", 1.0f);
    p.setParameterValue ("arp.cardC.custom", 1.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    OspAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (arpValue (restored, "arp.custom") == 1.0f);
    CHECK (arpValue (restored, "arp.cardB.custom") == 0.0f);
    CHECK (arpValue (restored, "arp.cardC.custom") == 1.0f);

    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (xml != nullptr);
    for (auto* child = xml->getFirstChildElement(); child != nullptr;)
    {
        auto* next = child->getNextElement();
        if (child->getStringAttribute ("id").endsWith ("custom"))
            xml->removeChildElement (child, true);
        child = next;
    }
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    OspAudioProcessor older;
    older.setParameterValue ("arp.custom", 1.0f);
    older.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    CHECK (arpValue (older, "arp.custom") == 0.0f);
    CHECK (arpValue (older, "arp.cardC.custom") == 0.0f);
}

TEST_CASE ("plugin custom arp: a card's pattern is handed over and read back; invalid or none clears it", "[plugin][arp][arp-custom]")
{
    OspAudioProcessor p;
    const auto pattern = customtests::withRests();
    for (int card = 0; card < 3; ++card)
        CHECK (p.customArpPattern (card) == nullptr);
    p.setCustomArpPattern (1, &pattern);
    REQUIRE (p.customArpPattern (1) != nullptr);
    CHECK (*p.customArpPattern (1) == pattern);
    CHECK (p.customArpPattern (0) == nullptr);
    arp::CustomPattern invalid;
    p.setCustomArpPattern (1, &invalid);
    CHECK (p.customArpPattern (1) == nullptr);
    p.setCustomArpPattern (2, &pattern);
    p.setCustomArpPattern (2, nullptr);
    CHECK (p.customArpPattern (2) == nullptr);
    p.setCustomArpPattern (7, &pattern);   // no such card: ignored
}

TEST_CASE ("plugin custom arp: CUSTOM on without a pattern plays the style sample for sample; with one, the pattern plays", "[plugin][arp][arp-custom]")
{
    TempDir tmp;
    const auto pattern = customtests::withRests();
    auto render = [&] (bool custom, const arp::CustomPattern* withPattern, plugin::InstrumentHost::ArpView* view) {
        OspAudioProcessor p;
        cardarp::loadTwo (p, tmp.dir);
        arpOn (p, 0, 1, 60.0f, 1);   // UP, 1/8
        p.setParameterValue ("arp.custom", custom ? 1.0f : 0.0f);
        if (withPattern != nullptr)
            p.setCustomArpPattern (0, withPattern);
        auto run = renderArp (p, 48000.0, 256, 1.0, [] (int pos, int n, juce::MidiBuffer& m) {
            for (int note : { 60, 64, 67 })
                at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)));
        }, nullptr, 1.0e9, true);
        if (view != nullptr)
            *view = p.arpView();
        return run;
    };
    const auto off = render (false, nullptr, nullptr);
    REQUIRE (off.playPeak > 1.0e-3);
    const auto onWithout = render (true, nullptr, nullptr);
    CHECK (onWithout.audio == off.audio);
    CHECK (onWithout.noteOns == off.noteOns);
    const auto offWith = render (false, &pattern, nullptr);
    CHECK (offWith.audio == off.audio);

    plugin::InstrumentHost::ArpView view;
    const auto custom = render (true, &pattern, &view);
    CHECK (custom.finite);
    CHECK (custom.audio != off.audio);
    // One second at 120 BPM is 8 eighths: UP plays 8 notes, the pattern two cycles of three.
    CHECK (custom.noteOns * 4 == off.noteOns * 3);
    // The display: C, nothing, G (the split's first half is the step's lowest .. E its highest), nothing.
    REQUIRE (view.active);
    CHECK (view.low[0] == 60);
    CHECK (view.low[1] == -1);
    CHECK (view.low[2] == 64);
    CHECK (view.high[2] == 67);
    CHECK (view.low[3] == -1);
}

TEST_CASE ("plugin custom arp: unlinked, card B plays its own pattern while A plays its style", "[plugin][arp][arp-custom]")
{
    using namespace strumtests;
    const auto pattern = customtests::withRests();
    LaneHost host;
    host.base = chordBase();
    host.base.pattern = ArpPattern::up;
    host.base.rate = ArpRate::eighth;
    plugin::CardArpLanes lanes;
    plugin::CardArpLanes::Controls c;
    c.enabled = true;
    c.link = false;
    c.cards = 3;   // A and B
    c.pattern[0] = static_cast<int> (ArpPattern::down);
    c.rate[0] = static_cast<int> (ArpRate::eighth);
    c.strum[0].custom = true;
    host.lanes = &lanes;
    lanes.prepare (48000.0);
    host.arp.prepare (48000.0);
    lanes.setCustomPattern (1, &pattern);   // (after prepare: the lanes' arpeggiators exist)
    std::map<int, std::vector<int>> byChannel;
    HostTiming t;
    for (int pos = 0; pos < 47000; pos += 256)   // four eighths (12000 samples each at 120 BPM)
    {
        juce::MidiBuffer in;
        if (pos == 0)
            for (const int n : { 60, 64, 67, 71 })
                in.addEvent (juce::MidiMessage::noteOn (1, n, static_cast<juce::uint8> (100)), 0);
        for (const auto m : lanes.process (host, in, 256, t, c))
            if (m.getMessage().isNoteOn())
                byChannel[m.getMessage().getChannel()].push_back (m.getMessage().getNoteNumber());
    }
    // Channel = 1 + card mask: A 2 (UP over the seventh), B 3 (the pattern: C, -, G E, -).
    CHECK (byChannel[2] == std::vector<int> { 60, 64, 67, 71 });
    CHECK (byChannel[3] == std::vector<int> { 60, 67, 64 });
}

TEST_CASE ("plugin custom arp: no allocation on the audio thread while a pattern plays and changes", "[plugin][arp][arp-custom]")
{
    TempDir tmp;
    OspAudioProcessor p;
    cardarp::loadTwo (p, tmp.dir);
    arpOn (p, 0, 3, 120.0f, 4);   // 1/32, four octaves
    p.setParameterValue ("arp.custom", 1.0f);
    const auto pattern = customtests::withRests();
    p.setCustomArpPattern (0, &pattern);
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    int allocations = 0;
    for (int block = 0; block < 400; ++block)
    {
        juce::MidiBuffer midi;
        midi.ensureSize (64);
        if (block == 0)
            for (int note : { 48, 55, 60, 64, 67 })
                midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (90)), 0);
        if (block == 200)
            p.setCustomArpPattern (0, &pattern);   // (message thread side, between blocks)
        buffer.clear();
        test::AllocationScope scope;
        p.processBlock (buffer, midi);
        allocations += scope.count();
    }
    CHECK (allocations == 0);
}
