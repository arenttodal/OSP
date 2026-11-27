// ZONES (owner, 26 Nov 2026): a multi-sample layer's zone editor. Included at the end of
// PluginProcessorTests.cpp.

using osp::plugin::ZoneEditor;
using osp::plugin::EngineCard;

namespace
{
    juce::MouseEvent zoneMouse (juce::Component& c, juce::Point<float> at, juce::Point<float> down)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (source, at, {}, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, down, now, 1, false);
    }

    double peakOf (const AudioData& a)
    {
        double peak = 0.0;
        for (const auto& ch : a.channels)
            for (float s : ch)
                peak = std::max (peak, static_cast<double> (std::abs (s)));
        return peak;
    }

    void settleSet (OspAudioProcessor& p)
    {
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
    }
}

TEST_CASE ("plugin zones: the map follows the set; edits make zones of their own, saved and recalled; AUTO forgets them", "[plugin][zones][gui]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    juce::Array<juce::File> files;
    files.add (writeSource (tmp.dir, "low A2.wav", testsignals::vowel (midiToHz (45), 1.5, 48000.0, 1)));
    files.add (writeSource (tmp.dir, "mid A3.wav", testsignals::vowel (midiToHz (57), 1.5, 48000.0, 2)));
    files.add (writeSource (tmp.dir, "high A4.wav", testsignals::saw (midiToHz (69), 1.5, 48000.0, 0.3, 2)));
    OspAudioProcessor p;
    p.loadFiles (files);
    settleSet (p);
    REQUIRE (p.currentInstrument() != nullptr);
    REQUIRE (p.currentInstrument()->set != nullptr);
    REQUIRE (p.currentInstrument()->set->groups.size() == 3);
    CHECK_FALSE (p.currentInstrument()->set->zoned);

    ZoneEditor z (p, 0);
    z.setSize (900, 260);
    REQUIRE (z.zones().size() == 3);
    CHECK_FALSE (z.isManual());
    // Automatic: the nearest sample, so the zones meet and every key plays.
    CHECK (z.zones()[0].low == 0);
    CHECK (z.zones()[0].high + 1 == z.zones()[1].low);
    CHECK (z.zones()[1].high + 1 == z.zones()[2].low);
    CHECK (z.zones()[2].high == 127);
    CHECK (peakOf (playNote (p, 51, 48000.0, 0.3)) > 0.01);

    // Drag the low zone's right edge 4 keys left: the map becomes the musician's own.
    const int edgeKey = z.zones()[0].high;
    const auto r = z.zoneBounds (0);
    REQUIRE_FALSE (r.isEmpty());
    const juce::Point<float> edge (r.getRight() - 2.0f, r.getCentreY());
    const float keyW = z.xForKey (1) - z.xForKey (0);
    z.mouseDown (zoneMouse (z, edge, edge));
    CHECK (z.selectedZone() == 0);
    z.mouseDrag (zoneMouse (z, edge.translated (-4.0f * keyW, 0.0f), edge));
    z.mouseUp (zoneMouse (z, edge.translated (-4.0f * keyW, 0.0f), edge));
    settleSet (p);
    z.refresh();
    CHECK (z.isManual());
    const auto set = p.currentInstrument()->set;
    REQUIRE (set->zoned);
    for (const auto& m : set->members)
        CHECK (m.hasKeyRange());
    CHECK (z.zones()[0].high == edgeKey - 4);
    // The keys left between the zones are silent; FILL GAPS gives them back.
    CHECK (peakOf (playNote (p, edgeKey - 1, 48000.0, 0.3)) < 1.0e-6);
    CHECK (peakOf (playNote (p, edgeKey - 5, 48000.0, 0.3)) > 0.01);
    REQUIRE (z.perform ("fill"));
    settleSet (p);
    z.refresh();
    CHECK (peakOf (playNote (p, edgeKey - 1, 48000.0, 0.3)) > 0.01);
    CHECK (z.zones()[0].low == 0);

    // Saved with the session, recalled exactly.
    REQUIRE (z.perform ("manual") == false);   // already manual
    juce::MemoryBlock state;
    p.getStateInformation (state);
    {
        OspAudioProcessor q;
        q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        settleSet (q);
        REQUIRE (q.currentInstrument() != nullptr);
        REQUIRE (q.currentInstrument()->set != nullptr);
        CHECK (q.currentInstrument()->set->zoned);
        int ranged = 0;
        for (const auto& a : q.currentInstrument()->assignments)
            ranged += a.keyLow && a.keyHigh ? 1 : 0;
        CHECK (ranged == 3);
    }

    // AUTO: back to the nearest sample (no ranges), the rest of the corrections kept.
    REQUIRE (z.perform ("auto"));
    settleSet (p);
    z.refresh();
    CHECK_FALSE (z.isManual());
    CHECK_FALSE (p.currentInstrument()->set->zoned);

    // A drag up moves a zone to a new, harder velocity row.
    const auto mid = z.zoneBounds (1);
    const auto at = mid.getCentre();
    z.mouseDown (zoneMouse (z, at, at));
    z.mouseDrag (zoneMouse (z, juce::Point<float> (at.x, z.laneArea().getY() - 10.0f), at));
    z.mouseUp (zoneMouse (z, juce::Point<float> (at.x, z.laneArea().getY() - 10.0f), at));
    settleSet (p);
    z.refresh();
    CHECK (z.rowCount() == 2);
    bool moved = false;
    for (const auto& m : p.currentInstrument()->set->members)
        moved = moved || (m.filename == "mid A3.wav" && m.layer == 1);
    CHECK (moved);
}

TEST_CASE ("plugin zones: the card's key reads ZONES on a set and opens the editor; the menu's Zones too", "[plugin][zones][gui]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    juce::Array<juce::File> files;
    files.add (writeSource (tmp.dir, "a.wav", testsignals::vowel (midiToHz (45), 1.0, 48000.0, 1)));
    files.add (writeSource (tmp.dir, "b.wav", testsignals::vowel (midiToHz (57), 1.0, 48000.0, 2)));
    OspAudioProcessor p;
    p.loadFiles (files);
    settleSet (p);
    EngineCard card (p, 0);
    card.setSize (900, 420);
    card.refresh();
    REQUIRE (card.notesToggle().isVisible());
    CHECK (card.notesToggle().showsZones());
    card.setNotesOpen (true);
    REQUIRE (card.zoneEditorComponent() != nullptr);
    CHECK (card.noteMapComponent() == nullptr);
    CHECK (card.zoneEditorComponent()->zones().size() == 2);
    card.setNotesOpen (false);
    CHECK (card.zoneEditorComponent() == nullptr);
}

TEST_CASE ("plugin zones: files dropped on ZONES join the set", "[plugin][zones]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    juce::Array<juce::File> files;
    files.add (writeSource (tmp.dir, "a.wav", testsignals::vowel (midiToHz (45), 1.0, 48000.0, 1)));
    files.add (writeSource (tmp.dir, "b.wav", testsignals::vowel (midiToHz (57), 1.0, 48000.0, 2)));
    OspAudioProcessor p;
    p.loadFiles (files);
    settleSet (p);
    ZoneEditor z (p, 0);
    z.setSize (900, 260);
    const auto extra = writeSource (tmp.dir, "c.wav", testsignals::saw (midiToHz (69), 1.0, 48000.0, 0.3, 2));
    REQUIRE (z.isInterestedInFileDrag ({ extra.getFullPathName() }));
    z.filesDropped ({ extra.getFullPathName() }, 10, 10);
    settleSet (p);
    z.refresh();
    REQUIRE (p.currentInstrument()->set != nullptr);
    CHECK (p.currentInstrument()->set->members.size() == 3);
    CHECK (z.zones().size() == 3);
}

TEST_CASE ("plugin zones: a picture of ZONES in the editor (OSP_ZONES_SHOT=<file.png>)", "[.][zones-shot]")
{
    const char* path = std::getenv ("OSP_ZONES_SHOT");
    if (path == nullptr)
        return;
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    juce::Array<juce::File> files;
    const std::pair<const char*, int> takes[] = { { "Bowl C2 soft.wav", 36 }, { "Bowl C2 hard.wav", 36 }, { "Bowl G2 soft.wav", 43 }, { "Bowl G2 hard.wav", 43 },
                                                  { "Bowl D3 soft.wav", 50 }, { "Bowl D3 hard.wav", 50 }, { "Bowl A3 hard.wav", 57 }, { "Bowl E4 soft.wav", 64 },
                                                  { "Bowl E4 hard.wav", 64 }, { "Bowl C#5 hard.wav", 73 }, { "Bowl C#5 hard 2.wav", 73 }, { "Bowl G5 soft.wav", 79 } };
    std::uint64_t seed = 1;
    for (const auto& [name, key] : takes)
        files.add (writeSource (tmp.dir, name, testsignals::vowel (midiToHz (key), 1.0, 48000.0, seed++)));
    OspAudioProcessor p;
    p.loadFiles (files);
    settleSet (p);
    osp::plugin::OspAudioProcessorEditor editor (p);
    editor.setSize (1240, 820);
    auto& card = editor.engineCard (0);
    card.setNotesOpen (true);
    REQUIRE (card.zoneEditorComponent() != nullptr);
    auto& z = *card.zoneEditorComponent();
    z.perform ("manual");
    settleSet (p);
    z.refresh();
    for (int i = 0; i < static_cast<int> (z.zones().size()); ++i)
        if (z.zones()[static_cast<std::size_t> (i)].name.contains ("C#5"))
            z.selectZone (i);
    auto image = card.createComponentSnapshot (card.getLocalBounds(), true, 2.0f);
    juce::File out (path);
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat().writeImageToStream (image, stream);
}

TEST_CASE ("plugin zones: every zone edit is one undo step (the editor's arrows and the instrument's history), redo too", "[plugin][zones][undo]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    juce::Array<juce::File> files;
    files.add (writeSource (tmp.dir, "low A2.wav", testsignals::vowel (midiToHz (45), 1.0, 48000.0, 1)));
    files.add (writeSource (tmp.dir, "mid A3.wav", testsignals::vowel (midiToHz (57), 1.0, 48000.0, 2)));
    OspAudioProcessor p;
    p.loadFiles (files);
    settleSet (p);
    ZoneEditor z (p, 0);
    z.setSize (900, 260);
    REQUIRE (z.zones().size() == 2);
    const int high0 = z.zones()[0].high;
    auto shrink = [&z] (int keys) {
        const auto r = z.zoneBounds (0);
        const juce::Point<float> edge (r.getRight() - 2.0f, r.getCentreY());
        const float keyW = z.xForKey (1) - z.xForKey (0);
        z.mouseDown (zoneMouse (z, edge, edge));
        z.mouseDrag (zoneMouse (z, edge.translated (-static_cast<float> (keys) * keyW, 0.0f), edge));
        z.mouseUp (zoneMouse (z, edge.translated (-static_cast<float> (keys) * keyW, 0.0f), edge));
    };
    shrink (3);
    settleSet (p);
    z.refresh();
    REQUIRE (z.isManual());
    CHECK (z.zones()[0].high == high0 - 3);
    CHECK (p.undo().getUndoDescription() == "Zones");

    // Undo: the automatic map again; redo: the edit again.
    REQUIRE (z.perform ("undo"));
    settleSet (p);
    z.refresh();
    CHECK_FALSE (z.isManual());
    CHECK_FALSE (p.currentInstrument()->set->zoned);
    CHECK (z.zones()[0].high == high0);
    REQUIRE (z.perform ("redo"));
    settleSet (p);
    z.refresh();
    CHECK (z.isManual());
    CHECK (z.zones()[0].high == high0 - 3);

    // Two edits in quick succession (the first not built yet): one undo takes back only the last.
    shrink (2);
    shrink (2);   // (the editor shows the first edit at once, so this starts from it)
    settleSet (p);
    z.refresh();
    CHECK (z.zones()[0].high == high0 - 7);
    REQUIRE (z.perform ("undo"));
    settleSet (p);
    z.refresh();
    CHECK (z.zones()[0].high == high0 - 5);
}
