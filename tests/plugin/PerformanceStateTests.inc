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

TEST_CASE ("Performance Mode (OSP): one key; 1, 2 or 3 sources become pictures; the strip; editing areas hidden and back; the sound never changes", "[plugin][performance][ui]")
{
    namespace pv = osp::plugin::performance;
    TempDir tmp;
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    OspAudioProcessor p;
    p.prepareToPlay (48000.0, 512);
    const auto a = writeSource (tmp.dir, "vowel A3.wav", testsignals::vowel (midiToHz (57), 1.5, 48000.0, 3));
    loadAndWait (p, a);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    editor->setSize (1448, juce::roundToInt (ui->instrumentHeight()));
    ui->refreshNow();
    juce::StringArray before;
    for (auto* param : p.getParameters())
        before.add (juce::String (param->getValue(), 9));
    const float editHeight = ui->instrumentHeight();
    auto& key = ui->performanceKey();
    CHECK (key.isVisible());
    CHECK (key.getTitle() == "Enter Performance Mode");
    key.onClick();
    ui->refreshNow();
    REQUIRE (ui->performanceShown());
    CHECK (key.getTitle() == "Edit Instrument");
    CHECK (ui->instrumentHeight() <= editHeight);
    CHECK (ui->engineCard (0).inPerformance());
    CHECK (ui->engineCard (0).isVisible());
    CHECK_FALSE (ui->sourceViewToggle().isVisible());
    // OSP's six core macros, in the panel's order (LIFE ... ECHO, SPACE).
    CHECK (ui->performanceMacrosShown().size() == 6);
    const auto panel = osp::plugin::design::layout::macroPanel.translated (0.0f, -ui->sourceTrim()).toNearestInt();
    CHECK (panel.contains (ui->performanceFlap().getBounds()));
    // The picker: one change, applied at once; Reset brings the six back.
    ui->togglePerformancePicker();
    auto* picker = ui->performancePicker();
    REQUIRE (picker != nullptr);
    picker->toggle (ui->performanceMacrosShown()[0]);
    CHECK (ui->performanceMacrosShown().size() == 5);
    picker->reset();
    CHECK (ui->performanceMacrosShown().size() == 6);
    ui->togglePerformancePicker();
    // Two and three sources: each its column, pictures only.
    for (const char* name : { "pluck E4.wav", "vowel C4.wav" })
    {
        const auto file = writeSource (tmp.dir, name, testsignals::pluck (midiToHz (64), 1.0, 48000.0, 4));
        p.loadFile (file, p.slotCount());
        REQUIRE (p.waitForLoads (20000));
        p.pollLoads();
        ui->refreshNow();
    }
    int pictures = 0;
    for (int l = 0; l < 3; ++l)
        pictures += ui->engineCard (l).isVisible() && ui->engineCard (l).inPerformance() ? 1 : 0;
    CHECK (pictures == 3);
    // Back to Edit: as it was.
    key.onClick();
    ui->refreshNow();
    CHECK_FALSE (ui->performanceShown());
    CHECK_FALSE (ui->engineCard (0).inPerformance());
    CHECK (ui->sourceViewToggle().isVisible());
    CHECK_FALSE (ui->performanceFlap().isVisible());
    juce::StringArray after;
    for (auto* param : p.getParameters())
        after.add (juce::String (param->getValue(), 9));
    CHECK (after == before);
    p.setPerformanceView ({});
}

TEST_CASE ("Performance Mode (OSP) screenshots (OSP_SCREENSHOT_DIR)", "[.][screenshots][screenshots-performance]")
{
    const char* dir = std::getenv ("OSP_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    TempDir tmp;
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    OspAudioProcessor p;
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    auto shoot = [&] (const juce::String& name) {
        editor->setSize (1448, juce::roundToInt (ui->instrumentHeight()));
        ui->refreshNow();
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), stream);
    };
    ui->setPerformance (true);
    shoot ("osp-performance-0-empty");
    const char* names[] = { "vowel A3.wav", "pluck E4.wav", "vowel C4.wav" };
    for (int n = 0; n < 3; ++n)
    {
        const auto file = writeSource (tmp.dir, names[n], n == 1 ? testsignals::pluck (midiToHz (64), 1.0, 48000.0, 4) : testsignals::vowel (midiToHz (57 + 3 * n), 1.5, 48000.0, 3));
        p.loadFile (file, n);
        REQUIRE (p.waitForLoads (20000));
        p.pollLoads();
        ui->setPerformance (false);
        shoot ("osp-edit-" + juce::String (n + 1));
        ui->setPerformance (true);
        shoot ("osp-performance-" + juce::String (n + 1));
    }
    ui->togglePerformancePicker();
    shoot ("osp-performance-picker");
    ui->togglePerformancePicker();
    ui->setPerformance (false);
    p.setPerformanceView ({});
}
