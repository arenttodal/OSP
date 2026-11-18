// ARP STRUM in the plugins (ST3): per-card parameters (version hint 35), recall, older
// sessions, the processor's audio (zero spread is the chord as before, sample for sample),
// LINK and unlinked lanes through the shared CardArpLanes, and no allocation.

namespace strumtests
{
    const char* const fields[] { "on", "direction", "sync", "ms", "division" };

    /** CardArpLanes' host, as the processors provide it: lane A is this arpeggiator with
        `base` (card A's settings, STRUM included). */
    struct LaneHost final : plugin::CardArpLanes::Host
    {
        Arpeggiator arp;
        Arpeggiator::Settings base;
        juce::MidiBuffer out;
        bool routing = false;
        plugin::CardArpLanes* lanes = nullptr;

        Arpeggiator& mainArpeggiator() noexcept override { return arp; }
        Arpeggiator::Settings arpSettings() noexcept override
        {
            auto s = base;
            s.enabled = base.enabled && (lanes == nullptr || ! lanes->forceArpOff());
            return s;
        }
        const juce::MidiBuffer& runArpeggiator (const juce::MidiBuffer& midi, int numSamples, const HostTiming& timing) noexcept override
        {
            out.clear();
            arp.beginBlock (arpSettings(), timing, numSamples);
            for (const auto m : midi)
            {
                const auto msg = m.getMessage();
                if (msg.isNoteOn())
                    arp.noteOn (m.samplePosition, msg.getNoteNumber(), msg.getVelocity(), msg.getChannel());
                else if (msg.isNoteOff())
                    arp.noteOff (m.samplePosition, msg.getNoteNumber(), msg.getChannel());
            }
            arp.endBlock();
            for (int i = 0; i < arp.numEvents(); ++i)
            {
                const auto& e = arp.event (i);
                if (e.kind == Arpeggiator::Event::Kind::noteOn)
                    out.addEvent (juce::MidiMessage::noteOn (e.channel, e.note, e.velocity), e.offset);
                else if (e.kind == Arpeggiator::Event::Kind::noteOff)
                    out.addEvent (juce::MidiMessage::noteOff (e.channel, e.note), e.offset);
            }
            return out;
        }
        void setCardRouting (bool on) noexcept override { routing = on; }
        void publishArpDisplay() noexcept override {}
    };

    struct NoteOn
    {
        std::int64_t time;
        int note, channel;
    };

    /** Holds a C major seventh from sample 0 for `samples`, returns every note-on with its channel. */
    std::vector<NoteOn> runLanes (LaneHost& host, plugin::CardArpLanes& lanes, const plugin::CardArpLanes::Controls& c, int samples)
    {
        host.lanes = &lanes;
        lanes.prepare (48000.0);
        host.arp.prepare (48000.0);
        std::vector<NoteOn> ons;
        HostTiming t;
        for (int pos = 0; pos < samples; pos += 256)
        {
            juce::MidiBuffer in;
            if (pos == 0)
                for (const int n : { 60, 64, 67, 71 })
                    in.addEvent (juce::MidiMessage::noteOn (1, n, static_cast<juce::uint8> (100)), 0);
            const auto& out = lanes.process (host, in, 256, t, c);
            for (const auto m : out)
                if (m.getMessage().isNoteOn())
                    ons.push_back ({ pos + m.samplePosition, m.getMessage().getNoteNumber(), m.getMessage().getChannel() });
        }
        return ons;
    }

    Arpeggiator::Settings chordBase()
    {
        Arpeggiator::Settings s;
        s.enabled = true;
        s.pattern = ArpPattern::chord;
        s.rate = ArpRate::quarter;
        s.gate = 0.5;
        return s;
    }
}

TEST_CASE ("plugin strum: parameters per card, off by default, recalled; an older session opens with STRUM off", "[plugin][arp][strum]")
{
    OspAudioProcessor p;
    for (const char* card : { "arp.strum.", "arp.cardB.strum.", "arp.cardC.strum." })
    {
        CHECK (arpValue (p, juce::String (card) + "on") == 0.0f);
        CHECK (arpValue (p, juce::String (card) + "direction") == 0.0f);   // UP
        CHECK (arpValue (p, juce::String (card) + "sync") == 0.0f);        // FREE
        CHECK (arpValue (p, juce::String (card) + "ms") == Approx (20.0f));
        CHECK (arpValue (p, juce::String (card) + "division") == 1.0f);    // 1/32
    }
    p.setParameterValue ("arp.strum.on", 1.0f);
    p.setParameterValue ("arp.strum.direction", 3.0f);
    p.setParameterValue ("arp.strum.ms", 75.0f);
    p.setParameterValue ("arp.cardC.strum.sync", 1.0f);
    p.setParameterValue ("arp.cardC.strum.division", 4.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    OspAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (arpValue (restored, "arp.strum.on") == 1.0f);
    CHECK (arpValue (restored, "arp.strum.direction") == 3.0f);
    CHECK (arpValue (restored, "arp.strum.ms") == Approx (75.0f).margin (0.1));
    CHECK (arpValue (restored, "arp.cardC.strum.sync") == 1.0f);
    CHECK (arpValue (restored, "arp.cardC.strum.division") == 4.0f);

    // A session saved before STRUM (no *.strum.*): every card off, whatever was set before.
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (xml != nullptr);
    for (auto* child = xml->getFirstChildElement(); child != nullptr;)
    {
        auto* next = child->getNextElement();
        if (child->getStringAttribute ("id").contains (".strum."))
            xml->removeChildElement (child, true);
        child = next;
    }
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    OspAudioProcessor older;
    older.setParameterValue ("arp.strum.on", 1.0f);
    older.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    CHECK (arpValue (older, "arp.strum.on") == 0.0f);
    CHECK (arpValue (older, "arp.cardC.strum.sync") == 0.0f);

    // Host automation: a normalised value lands on the same choice it always will (append only).
    auto* direction = dynamic_cast<juce::AudioParameterChoice*> (p.parameters.getParameter ("arp.strum.direction"));
    REQUIRE (direction != nullptr);
    CHECK (direction->choices == juce::StringArray { "UP", "DOWN", "RANDOM", "ALTERNATE" });
    auto* division = dynamic_cast<juce::AudioParameterChoice*> (p.parameters.getParameter ("arp.strum.division"));
    REQUIRE (division != nullptr);
    CHECK (division->choices[1] == "1/32");
}

TEST_CASE ("plugin strum: off and zero spread render the chord exactly as before; a spread changes it", "[plugin][arp][strum]")
{
    TempDir tmp;
    auto render = [&] (bool strum, float ms) {
        OspAudioProcessor p;
        cardarp::loadTwo (p, tmp.dir);
        arpOn (p, 5, 1, 60.0f, 2);   // CHORD, 1/8, two octaves
        p.setParameterValue ("arp.strum.on", strum ? 1.0f : 0.0f);
        p.setParameterValue ("arp.strum.ms", ms);
        return renderArp (p, 48000.0, 256, 1.2, [] (int pos, int n, juce::MidiBuffer& m) {
            for (int note : { 60, 64, 67 })
                at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)));
            at (m, pos, n, 48000.0, 1.0, juce::MidiMessage::allNotesOff (1));
        }, nullptr, 1.0e9, true);
    };
    const auto off = render (false, 20.0f);
    REQUIRE (off.playPeak > 1.0e-3);
    const auto zero = render (true, 0.0f);
    CHECK (zero.noteOns == off.noteOns);
    CHECK (zero.audio == off.audio);
    const auto strummed = render (true, 40.0f);
    CHECK (strummed.noteOns == off.noteOns);   // the same notes, later
    CHECK (strummed.finite);
    CHECK (strummed.audio != off.audio);
}

TEST_CASE ("plugin strum: LINK strums every lane as A; unlinked, each lane its own", "[plugin][arp][strum]")
{
    using namespace strumtests;
    // LINK, every card: the one arpeggiator, A's STRUM (UP, 30 ms).
    {
        LaneHost host;
        host.base = chordBase();
        host.base.strum = true;
        host.base.strumMs = 30.0;
        plugin::CardArpLanes lanes;
        plugin::CardArpLanes::Controls c;
        c.enabled = true;
        const auto ons = runLanes (host, lanes, c, 4096);
        REQUIRE (ons.size() >= 4);
        CHECK (ons[1].time - ons[0].time == 480);
        CHECK (ons[3].time - ons[0].time == 1440);
    }
    // Unlinked: A off, B DOWN 60 ms, C its own CHORD unstrummed.
    {
        LaneHost host;
        host.base = chordBase();
        plugin::CardArpLanes lanes;
        plugin::CardArpLanes::Controls c;
        c.enabled = true;
        c.link = false;
        c.pattern = { static_cast<int> (ArpPattern::chord), static_cast<int> (ArpPattern::chord) };
        c.rate = { 0, 0 };   // quarter
        c.strum[0].strum = true;
        c.strum[0].strumDirection = StrumDirection::down;
        c.strum[0].strumMs = 60.0;
        const auto ons = runLanes (host, lanes, c, 8192);
        std::map<int, std::vector<NoteOn>> byChannel;
        for (const auto& o : ons)
            byChannel[o.channel].push_back (o);
        // Channel = 1 + card mask: A 2, B 3, C 5.
        REQUIRE (byChannel[2].size() >= 4);
        REQUIRE (byChannel[3].size() >= 4);
        REQUIRE (byChannel[5].size() >= 4);
        for (int i = 0; i < 4; ++i)
        {
            CHECK (byChannel[2][static_cast<std::size_t> (i)].time == byChannel[2][0].time);   // A: together
            CHECK (byChannel[5][static_cast<std::size_t> (i)].time == byChannel[5][0].time);   // C: together
        }
        CHECK (byChannel[3][0].note == 71);                                // B: from the top
        CHECK (byChannel[3][3].note == 60);
        CHECK (byChannel[3][3].time - byChannel[3][0].time == 2880);       // 60 ms
    }
}

TEST_CASE ("plugin strum: no allocation on the audio thread while strumming in every arrangement", "[plugin][arp][strum]")
{
    TempDir tmp;
    OspAudioProcessor p;
    cardarp::loadTwo (p, tmp.dir);
    arpOn (p, 5, 2, 120.0f, 3);   // CHORD, 1/16, legato, three octaves
    for (const char* card : { "arp.strum.", "arp.cardB.strum.", "arp.cardC.strum." })
    {
        p.setParameterValue (juce::String (card) + "on", 1.0f);
        p.setParameterValue (juce::String (card) + "direction", 2.0f);   // RANDOM
        p.setParameterValue (juce::String (card) + "ms", 300.0f);        // limited by the step
    }
    p.setParameterValue ("arp.cardB.pattern", 5.0f);
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);
    auto round = [&] (int r) {
        p.setParameterValue ("arp.link", r % 2 == 0 ? 0.0f : 1.0f);
        p.setParameterValue ("arp.cardA", r % 3 == 0 ? 0.0f : 1.0f);
        p.setParameterValue ("arp.strum.sync", r % 4 == 1 ? 1.0f : 0.0f);
        midi.clear();
        for (int k = 0; k < 4; ++k)
            midi.addEvent (juce::MidiMessage::noteOn (1, 48 + 3 * r + 4 * k, static_cast<juce::uint8> (90)), 5 * k);
        midi.addEvent (juce::MidiMessage::noteOff (1, 48 + 3 * (r - 1)), 100);
    };
    for (int r = 0; r < 12; ++r)   // warm up every path once
    {
        round (r);
        for (int i = 0; i < 4; ++i)
        {
            buffer.clear();
            p.processBlock (buffer, midi);
            midi.clear();
        }
    }
    int allocations = 0;
    for (int r = 0; r < 12; ++r)
    {
        round (r);
        test::AllocationScope scope;
        for (int i = 0; i < 4; ++i)
        {
            buffer.clear();
            p.processBlock (buffer, midi);
            midi.clear();
        }
        allocations += scope.count();
    }
    CHECK (allocations == 0);
}
