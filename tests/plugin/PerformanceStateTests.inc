//==============================================================================
// PERFORMANCE MODE R1, Gate 1: the shared presentation model (PerformanceView.h) and its
// persistence in ANDOR OSP - a view, never a parameter, never part of a preset.

TEST_CASE ("performance model: default selection, sanitising, what the strip shows; stable ids", "[plugin][performance][unit]")
{
    namespace pv = osp::plugin::performance;
    pv::Catalog catalog;
    catalog.core = { "life", "drive", "character", "motion", "space", "echo" };
    catalog.userSlots = 6;
    std::set<int> assigned;
    const pv::AssignedFn isAssigned = [&] (int slot) { return assigned.count (slot) > 0; };

    CHECK (pv::userId (3) == "user:3");
    CHECK (pv::userSlotOf ("user:3") == 3);
    CHECK (pv::userSlotOf ("drive") == -1);
    CHECK (pv::userSlotOf ("user:x") == -1);
    CHECK_FALSE (catalog.knows ("user:6"));
    CHECK_FALSE (catalog.knows ("nonsense"));

    // Default: the six core macros; then up to two ASSIGNED USER macros (never an unassigned one).
    CHECK (pv::defaultSelection (catalog, isAssigned) == catalog.core);
    assigned = { 1, 3, 4 };
    auto expected = catalog.core;
    expected.add ("user:1");
    expected.add ("user:3");
    CHECK (pv::defaultSelection (catalog, isAssigned) == expected);

    // A stored selection: unknown ids dropped, duplicates once, at most eight; empty -> default.
    CHECK (pv::sanitise ({ "drive", "drive", "gone", "user:9", "user:2", "space" }, catalog, isAssigned) == juce::StringArray ({ "drive", "user:2", "space" }));
    CHECK (pv::sanitise ({ "gone" }, catalog, isAssigned) == expected);
    CHECK (pv::sanitise ({ "life", "drive", "character", "motion", "space", "echo", "user:0", "user:1", "user:2" }, catalog, isAssigned).size() == pv::maxMacros);

    // What the strip shows: an unassigned USER macro is kept in the selection but not shown; it
    // comes back when assigned. Renaming changes nothing (the id is the slot).
    pv::State s;
    s.customised = true;
    s.macros = { "user:2", "drive", "user:1" };
    CHECK (pv::selection (s, catalog, isAssigned) == juce::StringArray ({ "user:2", "drive", "user:1" }));
    CHECK (pv::visible (s, catalog, isAssigned) == juce::StringArray ({ "drive", "user:1" }));
    assigned.insert (2);
    CHECK (pv::visible (s, catalog, isAssigned) == juce::StringArray ({ "user:2", "drive", "user:1" }));
    // Never empty: only unassigned USER macros chosen -> the first core macro.
    assigned.clear();
    s.macros = { "user:4" };
    CHECK (pv::visible (s, catalog, isAssigned) == juce::StringArray ({ "life" }));

    // XML: round trip; a malformed or empty selection reads as the default.
    pv::State p;
    p.performance = true;
    p.customised = true;
    p.macros = { "space", "user:0" };
    const auto xml = p.toXml();
    REQUIRE (pv::State::fromXml (xml.get()).has_value());
    CHECK (*pv::State::fromXml (xml.get()) == p);
    juce::XmlElement empty ("Performance");
    empty.setAttribute ("customised", 1);
    CHECK_FALSE (pv::State::fromXml (&empty)->customised);
    CHECK_FALSE (pv::State::fromXml (nullptr).has_value());

    // Strip layout: evenly spread centres.
    const auto centres = pv::stripCentres (0.0f, 800.0f, 8);
    REQUIRE (centres.size() == 8);
    CHECK (centres.front() == Catch::Approx (50.0f));
    CHECK (centres.back() == Catch::Approx (750.0f));
}

TEST_CASE ("performance view in OSP: Interface.xml keeps it (other entries kept); the session keeps each instance's; presets never carry it", "[plugin][performance][state]")
{
    namespace pv = osp::plugin::performance;
    TempDir tmp;
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    pv::State chosen;
    chosen.performance = true;
    chosen.customised = true;
    chosen.macros = { "space", "drive", "user:2" };
    {
        OspAudioProcessor p;
        CHECK_FALSE (p.performanceView().performance);   // (nothing chosen yet: Edit Mode)
        p.setMacroBank (1);                             // (another Interface.xml entry)
        p.setPerformanceView (chosen);
        CHECK (p.performanceView() == chosen);
    }
    // A new instance on this computer starts from the musician's last choice; the bank entry kept.
    {
        OspAudioProcessor p;
        CHECK (p.performanceView() == chosen);
        CHECK (p.macroBank() == 1);
    }
    // The session keeps this instance's view, whatever the computer's preference says later.
    juce::MemoryBlock session;
    pv::State other;
    other.performance = false;
    other.customised = true;
    other.macros = { "life" };
    {
        OspAudioProcessor p;
        p.setPerformanceView (other);
        p.getStateInformation (session);
        p.setPerformanceView (chosen);   // (the computer's preference moves on)
    }
    {
        OspAudioProcessor p;
        p.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
        CHECK (p.performanceView() == other);
        // Two instances: each its own.
        OspAudioProcessor q;
        CHECK (q.performanceView() == chosen);
        // A preset saved here carries no view; loading one changes none.
        const auto preset = tmp.dir.getChildFile ("Mine.osppreset");
        REQUIRE (p.savePreset (preset));
        const auto text = preset.loadFileAsString();
        CHECK_FALSE (text.contains ("<Performance"));
        q.setPerformanceView (chosen);
        REQUIRE (q.loadPreset (preset));
        CHECK (q.performanceView() == chosen);
    }
    // An old session (no view): this instance takes the computer's preference.
    {
        OspAudioProcessor p;
        juce::MemoryBlock old;
        {
            OspAudioProcessor q;
            q.getStateInformation (old);
        }
        auto xml = OspAudioProcessor::getXmlFromBinary (old.getData(), static_cast<int> (old.getSize()));
        REQUIRE (xml != nullptr);
        xml->removeChildElement (xml->getChildByName ("Performance"), true);
        juce::MemoryBlock stripped;
        OspAudioProcessor::copyXmlToBinary (*xml, stripped);
        p.setStateInformation (stripped.getData(), static_cast<int> (stripped.getSize()));
        CHECK (p.performanceView() == chosen);
    }
}
