// NOTE ATLAS refinements in ANDOR OSP (docs/research/note-atlas-refinements.md): recorded round
// robins with LOOP off, per-note FADE IN / FADE OUT, live NOTES highlighting and the footer's
// LIBRARY key. Included after UnifiedPhase5Tests.inc (its helpers: settleAnalysis, settleEdits).

namespace
{
    /** A bass phrase of eight plucked C2 takes (each its own pluck and level), 0.4 s each with
        0.1 s between: what a musician drops. */
    juce::File writeRepeatedBass (const juce::File& dir)
    {
        const double rate = 48000.0;
        const auto note = static_cast<std::int64_t> (0.4 * rate), gap = static_cast<std::int64_t> (0.1 * rate);
        auto audio = AudioData::allocate (2, 8 * (note + gap) + gap, rate);
        for (int t = 0; t < 8; ++t)
        {
            const auto take = testsignals::pluck (midiToHz (36), 0.4, rate, static_cast<std::uint64_t> (0xba55 + t), 0.4 + 0.03 * t, 2);
            const auto start = gap + t * (note + gap);
            for (int c = 0; c < 2; ++c)
                std::copy (take.channels[static_cast<std::size_t> (c)].begin(), take.channels[static_cast<std::size_t> (c)].begin() + note,
                           audio.channels[static_cast<std::size_t> (c)].begin() + start);
        }
        const auto file = dir.getChildFile ("repeated-bass.wav");
        std::string error;
        REQUIRE (io::writeAudioFile (std::filesystem::path (file.getFullPathName().toStdString()), audio, io::SampleFormat::float32, error));
        return file;
    }

    void setLayerParameter (OspAudioProcessor& p, int layer, const char* field, float value)
    {
        auto* param = p.parameters.getParameter (osp::plugin::InstrumentHost::layerParameterId (layer, field));
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    /** The member of the layer's set the newest held voice plays (-1: none). */
    int heldMember (const OspAudioProcessor& p, int layer, int key)
    {
        const auto instrument = p.currentInstrument (layer);
        if (instrument == nullptr || instrument->set == nullptr)
            return -1;
        const auto& set = *instrument->set;
        const auto* engine = p.telemetryEngine();
        for (int i = 0; i < InstrumentEngine::voiceSlots(); ++i)
        {
            const auto& v = engine->voiceAt (i);
            if (! v.isActive() || v.isFading() || v.isReleased() || v.note() != key)
                continue;
            for (std::size_t m = 0; m < set.members.size(); ++m)
                if (set.members[m].model.get() == v.model())
                    return static_cast<int> (m);
        }
        return -1;
    }

    /** `count` presses of `key` through processBlock: the member each one played. */
    std::vector<int> pressKey (OspAudioProcessor& p, int key, int count)
    {
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buffer (2, 256);
        std::vector<int> out;
        for (int i = 0; i < count; ++i)
        {
            juce::MidiBuffer on;
            on.addEvent (juce::MidiMessage::noteOn (1, key, static_cast<juce::uint8> (100)), 0);
            p.processBlock (buffer, on);
            juce::MidiBuffer none;
            for (int b = 0; b < 4; ++b)
                p.processBlock (buffer, none);
            out.push_back (heldMember (p, 0, key));
            juce::MidiBuffer off;
            off.addEvent (juce::MidiMessage::noteOff (1, key), 0);
            p.processBlock (buffer, off);
            for (int b = 0; b < 8; ++b)
                p.processBlock (buffer, none);
        }
        return out;
    }
} // namespace

TEST_CASE ("note atlas: a dropped bass phrase of eight C2 takes round-robins through every take with LOOP off, as with LOOP on", "[plugin][atlas][round-robin]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    OspAudioProcessor p;
    p.analyseRecording (writeRepeatedBass (tmp.dir), 0);
    settleAnalysis (p);
    const auto instrument = p.currentInstrument (0);
    REQUIRE (instrument != nullptr);
    REQUIRE (instrument->atlas != nullptr);
    REQUIRE (instrument->set != nullptr);
    std::set<int> c2;
    for (std::size_t m = 0; m < instrument->set->members.size(); ++m)
        if (std::lround (instrument->set->members[m].model->rootMidi) == 36)
            c2.insert (static_cast<int> (m));
    INFO (c2.size() << " C2 takes of " << instrument->set->members.size() << " members");
    REQUIRE (c2.size() == 8);
    for (const bool loop : { false, true })
    {
        INFO ("LOOP " << (loop ? "on" : "off"));
        setLayerParameter (p, 0, "loop", loop ? 1.0f : 0.0f);
        const auto played = pressKey (p, 36, 24);
        std::set<int> heard;
        for (const int m : played)
        {
            CHECK (c2.count (m) == 1);
            heard.insert (m);
        }
        for (std::size_t i = 1; i < played.size(); ++i)
            CHECK (played[i] != played[i - 1]);
        if (! loop)
            CHECK (heard == c2);   // every take, loopable or not
        else
            CHECK (heard.size() >= 2);
    }
}

TEST_CASE ("note atlas: FADE IN / FADE OUT under SPEED - steps, one undo step each, dimmed with LOOP on, saved with the session", "[plugin][atlas][fades][notemap]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const auto rec = phraseRecording (tmp.dir, "fades.wav", { 60, 64, 67 });
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    p.loadAtlas (rec.atlas);
    settleEdits (p);
    editor->setSize (1448, juce::roundToInt (ui->instrumentHeight()));
    setLayerParameter (p, 0, "loop", 0.0f);
    ui->refreshNow();
    auto& card = ui->engineCard (0);
    card.onNotesToggle (0, true);
    auto* map = card.noteMapComponent();
    REQUIRE (map != nullptr);
    map->refresh();
    // 0 ms: longer offered, shorter and Reset not; the rows sit under SPEED.
    CHECK (map->actions().contains ("fadein+"));
    CHECK (map->actions().contains ("fadeout+"));
    CHECK_FALSE (map->actions().contains ("fadein-"));
    CHECK_FALSE (map->actions().contains ("fadeout.reset"));
    CHECK (map->actionArea ("fadein+").getY() > map->actionArea ("speed+").getY());
    CHECK (map->actionArea ("fadeout+").getY() > map->actionArea ("fadein+").getY());
    CHECK (map->inspectorArea().contains (map->actionArea ("fadeout+")));
    // Steps: fine near zero; one undo step each, named by the value.
    p.undo().clearUndoHistory();
    for (int i = 0; i < 4; ++i)
    {
        REQUIRE (map->perform ("fadeout+"));
        settleEdits (p);
        map->refresh();
    }
    CHECK (p.currentInstrument()->atlas->mapping.fadeOutMs == Approx (5.0));   // 1, 2, 3, 5 ms
    CHECK (p.undo().getUndoDescription() == "Fade out 5 ms");
    REQUIRE (map->perform ("fadein+"));
    settleEdits (p);
    map->refresh();
    CHECK (p.currentInstrument()->atlas->mapping.fadeInMs == Approx (0.5));
    CHECK (p.undo().getUndoDescription() == "Fade in 0.5 ms");
    // The playing set follows.
    CHECK (p.currentInstrument()->set->noteFadeOutMs == Approx (5.0));
    CHECK (p.currentInstrument()->set->noteFadeInMs == Approx (0.5));
    // Undo takes back the fade only.
    REQUIRE (p.undo().undo());
    settleEdits (p);
    CHECK (p.currentInstrument()->atlas->mapping.fadeInMs == Approx (0.0));
    CHECK (p.currentInstrument()->atlas->mapping.fadeOutMs == Approx (5.0));
    // LOOP on: the rows stay where they are, their keys dimmed; the values are kept.
    map->refresh();
    const auto before = map->actionArea ("fadeout+");
    setLayerParameter (p, 0, "loop", 1.0f);
    ui->refreshNow();
    map->refresh();
    CHECK_FALSE (map->actions().contains ("fadeout-"));
    CHECK_FALSE (map->actions().contains ("fadeout+"));
    CHECK (map->actionArea ("fadeout+") == before);
    CHECK (p.currentInstrument()->atlas->mapping.fadeOutMs == Approx (5.0));
    setLayerParameter (p, 0, "loop", 0.0f);
    ui->refreshNow();
    map->refresh();
    CHECK (map->actions().contains ("fadeout-"));
    // The session keeps them; an older session (no fields) opens at 0 / 0.
    juce::MemoryBlock state;
    p.getStateInformation (state);
    OspAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    settleEdits (restored);
    REQUIRE (restored.currentInstrument (0) != nullptr);
    REQUIRE (restored.currentInstrument (0)->atlas != nullptr);
    CHECK (restored.currentInstrument (0)->atlas->mapping.fadeOutMs == Approx (5.0));
    CHECK (restored.currentInstrument (0)->set->noteFadeOutMs == Approx (5.0));
    {
        const auto mapping = io::noteAtlasToJson (*p.currentInstrument()->atlas)["mapping"];
        CHECK (mapping.hasProperty ("fadeOutMs"));
        CHECK_FALSE (mapping.hasProperty ("fadeInMs"));   // (0: not written, as SPEED at 100 %)
        const auto olderJson = io::noteAtlasToJson (rec.atlas);
        CHECK_FALSE (olderJson["mapping"].hasProperty ("fadeOutMs"));
        std::string error;
        const auto read = io::noteAtlasFromJson (olderJson, error);
        REQUIRE (read.has_value());
        CHECK (read->mapping.fadeInMs == 0.0);
        CHECK (read->mapping.fadeOutMs == 0.0);
    }
    // Reset.
    REQUIRE (map->perform ("fadeout.reset"));
    settleEdits (p);
    CHECK (p.currentInstrument()->atlas->mapping.fadeOutMs == Approx (0.0));
}

namespace
{
    void processKeys (OspAudioProcessor& p, std::initializer_list<int> on, std::initializer_list<int> off, int blocks)
    {
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        for (const int k : on)
            midi.addEvent (juce::MidiMessage::noteOn (1, k, static_cast<juce::uint8> (100)), 0);
        for (const int k : off)
            midi.addEvent (juce::MidiMessage::noteOff (1, k), 0);
        p.processBlock (buffer, midi);
        juce::MidiBuffer none;
        for (int b = 1; b < blocks; ++b)
            p.processBlock (buffer, none);
    }
}

TEST_CASE ("note atlas: NOTES lights the regions the voices really play - the round-robin take, every chord note, FILL's source - and never moves the selection", "[plugin][atlas][notemap][playback]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    p.analyseRecording (writeRepeatedBass (tmp.dir), 0);
    settleEdits (p);
    setLayerParameter (p, 0, "loop", 0.0f);
    p.prepareToPlay (48000.0, 256);
    ui->refreshNow();
    auto& card = ui->engineCard (0);
    card.onNotesToggle (0, true);
    auto* map = card.noteMapComponent();
    REQUIRE (map != nullptr);
    map->refresh();
    CHECK (map->soundingNotes().empty());
    const auto set = p.currentInstrument (0)->set;
    REQUIRE (set != nullptr);
    // The musician's editing selection: a note of their choosing; playing never moves it.
    const auto first = p.currentInstrument (0)->atlas->notes.front().id;
    map->select ({ first });
    // Round robin: each press lights the take that plays, and only it.
    std::set<std::string> lit;
    for (int press = 0; press < 6; ++press)
    {
        processKeys (p, { 36 }, {}, 3);
        map->refresh();
        const int member = heldMember (p, 0, 36);
        REQUIRE (member >= 0);
        REQUIRE (map->soundingNotes().size() == 1);
        CHECK (map->soundingNotes().front() == set->members[static_cast<std::size_t> (member)].regionId);
        // The read head is inside that region.
        REQUIRE (map->soundingHeads().size() == 1);
        const auto* n = [&]() -> const atlas::NoteEvent* {
            for (const auto& e : p.currentInstrument (0)->atlas->notes)
                if (e.id == map->soundingNotes().front())
                    return &e;
            return nullptr;
        }();
        REQUIRE (n != nullptr);
        CHECK (map->soundingHeads().front() >= n->attackStart);
        CHECK (map->soundingHeads().front() <= n->sourceEnd);
        lit.insert (map->soundingNotes().front());
        processKeys (p, {}, { 36 }, 110);   // released, and its 0.4 s pluck over
        map->refresh();
        CHECK (map->selection() == std::vector<std::string> { first });
    }
    CHECK (lit.size() >= 4);   // different takes, as heard
    // Note over: nothing stays lit.
    processKeys (p, {}, {}, 200);
    map->refresh();
    CHECK (map->soundingNotes().empty());
    // FILL: C#2 plays a C2 take; that region lights (the keyboard shows C#2 itself).
    processKeys (p, { 37 }, {}, 3);
    map->refresh();
    const int filled = heldMember (p, 0, 37);
    REQUIRE (filled >= 0);
    REQUIRE (map->soundingNotes().size() == 1);
    CHECK (map->soundingNotes().front() == set->members[static_cast<std::size_t> (filled)].regionId);
    processKeys (p, {}, { 37 }, 200);
    map->refresh();
    CHECK (map->soundingNotes().empty());
    CHECK (map->selection() == std::vector<std::string> { first });
}

TEST_CASE ("note atlas: a chord lights each of its notes' regions at once; another sound clears them", "[plugin][atlas][notemap][playback]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const auto rec = phraseRecording (tmp.dir, "chord.wav", { 60, 64, 67 });
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    p.loadAtlas (rec.atlas);
    settleEdits (p);
    p.prepareToPlay (48000.0, 256);
    ui->refreshNow();
    auto& card = ui->engineCard (0);
    card.onNotesToggle (0, true);
    auto* map = card.noteMapComponent();
    REQUIRE (map != nullptr);
    map->refresh();
    processKeys (p, { 60, 64, 67 }, {}, 4);
    map->refresh();
    std::set<std::string> ids (map->soundingNotes().begin(), map->soundingNotes().end());
    std::set<std::string> expected;
    for (const auto& n : p.currentInstrument (0)->atlas->notes)
        if (n.included)
            expected.insert (n.id);
    CHECK (ids == expected);
    CHECK (map->soundingHeads().size() == 3);
    // Another sound on the layer: no stale region.
    juce::String error;
    const auto other = osp::plugin::tutorial::files (osp::plugin::tutorial::Example::pluck, error);
    REQUIRE (other.size() == 1);
    p.loadFile (other[0], 0);
    REQUIRE (p.waitForLoads (60000));
    p.pollLoads();
    ui->refreshNow();
    if (auto* still = card.noteMapComponent())
    {
        still->refresh();
        CHECK (still->soundingNotes().empty());
    }
}
