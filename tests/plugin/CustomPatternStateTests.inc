// Custom ARP Patterns, CP4 (docs/research/custom-arp): each card's pattern document is embedded
// in the session's state and in every preset (and so in a portable instrument, whose preset.xml
// is the same state), so a session never depends on the pattern library: deleting the library
// file, renaming it or opening the session on another computer changes nothing. A state without
// patterns (an older session, a starting state) clears every card's. An unreadable document is
// skipped (that card has none), reported, and the rest of the state loads.

namespace customstate
{
    using Editor = osp::plugin::CustomPatternEditor;

    osp::arppattern::Document documentFrom (const std::vector<int>& ranks, const std::string& name)
    {
        osp::arppattern::Document d;
        d.id = osp::newArpPatternId();
        d.name = name;
        d.capture = customui::fiveUp();
        d.captureBpm = 97.5;
        for (const int r : ranks)
            d.pattern.events.push_back ({ r, r < 0, 960, 100 });
        d.pattern.maxRank = 5;
        REQUIRE (d.pattern.valid());
        return d;
    }

    // stateOf (p) is PluginProcessorTests.cpp's helper.
}

TEST_CASE ("custom arp state: each card's whole document is saved with the session and recalled exactly", "[plugin][arp-custom][arp-custom-state]")
{
    using namespace customstate;
    const auto a = documentFrom ({ 1, 3, -1, 5, 2 }, "Rise and fall");
    const auto c = documentFrom ({ 4, 4, 1 }, "Pedal");
    juce::MemoryBlock saved;
    {
        OspAudioProcessor p;
        p.setCustomArpDocument (0, &a);
        p.setCustomArpDocument (2, &c);
        p.setParameterValue ("arp.custom", 1.0f);
        saved = stateOf (p);
    }
    OspAudioProcessor q;
    q.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (q.customArpDocument (0) != nullptr);
    REQUIRE (q.customArpDocument (2) != nullptr);
    CHECK (q.customArpDocument (1) == nullptr);
    CHECK (*q.customArpDocument (0) == a);   // the take, its tempo, the interpretation, the pattern
    CHECK (*q.customArpDocument (2) == c);
    // ... and handed to the audio thread, ready to play.
    REQUIRE (q.customArpPattern (0) != nullptr);
    CHECK (*q.customArpPattern (0) == osp::arppattern::compile (a.pattern));
    CHECK (*q.customArpPattern (2) == osp::arppattern::compile (c.pattern));
    CHECK (q.customArpPattern (1) == nullptr);
    CHECK (q.parameterValue ("arp.custom") == 1.0f);

    // Saving again writes the same patterns (nothing drifts through a round trip). The whole
    // state is not compared byte for byte: OSP's ValueTree reorders unrelated top-level attributes
    // when it loads.
    const auto patternsOf = [] (const juce::MemoryBlock& m)
    {
        const auto xml = juce::AudioProcessor::getXmlFromBinary (m.getData(), static_cast<int> (m.getSize()));
        REQUIRE (xml != nullptr);
        const auto* e = xml->getChildByName ("CustomPatterns");
        REQUIRE (e != nullptr);
        return e->toString();
    };
    CHECK (patternsOf (stateOf (q)) == patternsOf (saved));
}

TEST_CASE ("custom arp state: a state without patterns (an older session) clears every card's", "[plugin][arp-custom][arp-custom-state]")
{
    using namespace customstate;
    juce::MemoryBlock older;
    {
        OspAudioProcessor fresh;
        older = stateOf (fresh);
    }
    {
        const auto xml = juce::AudioProcessor::getXmlFromBinary (older.getData(), static_cast<int> (older.getSize()));
        REQUIRE (xml != nullptr);
        CHECK (xml->getChildByName ("CustomPatterns") == nullptr);   // nothing written when no card has one
    }
    OspAudioProcessor p;
    const auto d = documentFrom ({ 1, 2, 3 }, "Gone");
    p.setCustomArpDocument (1, &d);
    p.setStateInformation (older.getData(), static_cast<int> (older.getSize()));
    CHECK (p.customArpDocument (1) == nullptr);
    CHECK (p.customArpPattern (1) == nullptr);
}

TEST_CASE ("custom arp state: an unreadable document is skipped and the rest of the state loads", "[plugin][arp-custom][arp-custom-state]")
{
    using namespace customstate;
    juce::MemoryBlock saved;
    float gate = 0.0f;
    {
        OspAudioProcessor p;
        const auto a = documentFrom ({ 1, 2 }, "A"), b = documentFrom ({ 2, 1 }, "B");
        p.setCustomArpDocument (0, &a);
        p.setCustomArpDocument (1, &b);
        p.setParameterValue ("arp.gate", 0.3f);
        gate = p.parameterValue ("arp.gate");
        saved = stateOf (p);
    }
    auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (xml != nullptr);
    auto* patterns = xml->getChildByName ("CustomPatterns");
    REQUIRE (patterns != nullptr);
    for (auto* e : patterns->getChildWithTagNameIterator ("Card"))
        if (e->getIntAttribute ("index") == 0)
            e->setAttribute ("json", "{ \"kind\": \"andor.arp-pattern\", \"schemaVersion\": 99 }");   // a newer schema
    juce::MemoryBlock edited;
    juce::AudioProcessor::copyXmlToBinary (*xml, edited);

    OspAudioProcessor q;
    q.setStateInformation (edited.getData(), static_cast<int> (edited.getSize()));
    CHECK (q.customArpDocument (0) == nullptr);
    REQUIRE (q.customArpDocument (1) != nullptr);
    CHECK (q.customArpDocument (1)->name == "B");
    CHECK (q.parameterValue ("arp.gate") == Catch::Approx (gate).margin (1.0e-4));
}

TEST_CASE ("custom arp state: presets embed the pattern; deleting the library file never breaks the session or the preset", "[plugin][arp-custom][arp-custom-state]")
{
    using namespace customstate;
    customui::TempFolder library;
    const auto presetDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("osp-preset-" + juce::Uuid().toDashedString());
    presetDir.createDirectory();
    const auto preset = presetDir.getChildFile ("Custom.osppreset");

    juce::MemoryBlock session;
    osp::arppattern::Document used;
    {
        OspAudioProcessor p;
        {
            Editor editor (p, 0, true);
            editor.useTake (customui::fiveUp(), 120.0);
            editor.nameField().setText ("Library tune", true);
            editor.perform (Editor::Action::save);
            editor.perform (Editor::Action::use);
        }
        REQUIRE (p.customArpDocument (0) != nullptr);
        used = *p.customArpDocument (0);
        REQUIRE (p.savePreset (preset));
        session = stateOf (p);
    }
    // The library file goes (deleted, or this is another computer).
    const auto files = library.dir.findChildFiles (juce::File::findFiles, false, "*" + juce::String (osp::arpPatternExtension));
    REQUIRE (files.size() == 1);
    REQUIRE (files[0].deleteFile());

    OspAudioProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    REQUIRE (q.customArpDocument (0) != nullptr);
    CHECK (*q.customArpDocument (0) == used);
    q.setCustomArpDocument (0, nullptr);
    REQUIRE (q.loadPreset (preset));
    REQUIRE (q.customArpDocument (0) != nullptr);
    CHECK (*q.customArpDocument (0) == used);
    CHECK (q.parameterValue ("arp.custom") == 1.0f);
    presetDir.deleteRecursively();
}
