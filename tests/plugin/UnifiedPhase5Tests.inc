// The unified engine's Phase 5 in ANDOR OSP (docs/research/unified-andor/phase-5-report.md): MAP
// NOTES turns an ordinary recording into an instrument of its notes; the NOTE MAP edits it
// (one undo step per edit, published like a load: playable at once, held notes keep sounding);
// re-detection keeps the musician's corrections; KEY DNA's editor is undoable and its curves are
// saved. An ordinary sample is untouched by all of it. Included after UnifiedPhase4Tests.inc
// (its helpers: phraseRecording, settleAnalysis, heldLevelDb, renderHeld).

#include "NoteMapEditor.h"
#include "KeyDnaEditor.h"

namespace
{
    /** Every edit, analysis and stage delivered (queued edits start as the one before lands). */
    void settleEdits (OspAudioProcessor& p)
    {
        for (int i = 0; i < 40; ++i)
        {
            REQUIRE (p.waitForLoads (60000));
            p.pollLoads();
            bool idle = true;
            for (int l = 0; l < OspAudioProcessor::numLayers; ++l)
                idle = idle && p.atlasActivity (l) == osp::plugin::InstrumentHost::AtlasActivity::none;
            if (idle)
            {
                REQUIRE (p.waitForLoads (60000));
                p.pollLoads();
                return;
            }
        }
        FAIL ("the edits never settled");
    }

    /** A phrase loaded as an ordinary sample on layer 0 (what a drop does). */
    AtlasRecording ordinaryPhrase (OspAudioProcessor& p, const juce::File& dir, std::vector<int> keys)
    {
        auto rec = phraseRecording (dir, "phrase.wav", std::move (keys));
        p.addLayers ({ rec.file });
        REQUIRE (p.waitForLoads (60000));
        p.pollLoads();
        REQUIRE (p.waitForLoads (60000));
        p.pollLoads();
        return rec;
    }

    std::set<int> includedKeys (const atlas::NoteAtlas& a)
    {
        std::set<int> keys;
        for (const auto& n : a.notes)
            if (n.included)
                keys.insert (n.playKey());
        return keys;
    }
}

TEST_CASE ("plugin phase 5: an ordinary sample stays an ordinary sample (workflow A)", "[plugin][phase5][notemap]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    const auto rec = ordinaryPhrase (p, tmp.dir, { 69, 74, 77 });
    const auto instrument = p.currentInstrument (0);
    REQUIRE (instrument != nullptr);
    // Nothing analysed by itself: no atlas, no activity, no NOTES key; MAP NOTES is offered.
    CHECK (instrument->atlas == nullptr);
    CHECK (instrument->set == nullptr);
    CHECK (p.atlasActivity (0) == osp::plugin::InstrumentHost::AtlasActivity::none);
    CHECK (p.canMapNotes (0));
    ui->refreshNow();
    auto& card = ui->engineCard (0);
    CHECK_FALSE (card.notesToggle().isVisible());
    CHECK_FALSE (card.isNotesOpen());
    CHECK (osp::plugin::NoteMapEditor::statusOf (p, 0).isEmpty());
    // The keyboard shows no map.
    p.setEditLayer (0);
    ui->refreshNow();
    CHECK (heldLevelDb (p, 60) > -50.0);
}

TEST_CASE ("plugin phase 5: MAP NOTES analyses the layer's recording, playable at once, one undo step (workflow B)", "[plugin][phase5][notemap]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    ordinaryPhrase (p, tmp.dir, { 69, 74, 77, 81 });
    const auto ordinary = p.currentInstrument (0);
    REQUIRE (ordinary != nullptr);
    p.mapNotes (0);
    CHECK (p.atlasActivity (0) == osp::plugin::InstrumentHost::AtlasActivity::analysing);   // said at once
    ui->refreshNow();
    settleEdits (p);
    ui->refreshNow();
    const auto mapped = p.currentInstrument (0);
    REQUIRE (mapped != nullptr);
    REQUIRE (mapped->atlas != nullptr);
    CHECK (includedKeys (*mapped->atlas) == std::set<int> { 69, 74, 77, 81 });
    for (int key : { 69, 74, 77, 81 })
        CHECK (heldLevelDb (p, key) > -50.0);
    // The card says what it is; the NOTES key is there.
    auto& card = ui->engineCard (0);
    CHECK (card.notesToggle().isVisible());
    const auto status = osp::plugin::NoteMapEditor::statusOf (p, 0);
    CHECK ((status == "Ready" || status == "Needs review"));
    // One undo step: back to the ordinary sample, and forward again.
    CHECK (p.undo().getUndoDescription().startsWith ("Map notes"));
    REQUIRE (p.undo().undo());
    p.pollLoads();
    REQUIRE (p.currentInstrument (0) != nullptr);
    CHECK (p.currentInstrument (0)->atlas == nullptr);
    CHECK (p.currentInstrument (0)->loadId == ordinary->loadId);
    REQUIRE (p.undo().redo());
    p.pollLoads();
    REQUIRE (p.currentInstrument (0)->atlas != nullptr);
    // "Play as one sample" brings the recording back as one ordinary sample (undoable).
    p.playAsOneSample (0);
    settleEdits (p);
    REQUIRE (p.currentInstrument (0) != nullptr);
    CHECK (p.currentInstrument (0)->atlas == nullptr);
    CHECK (p.currentInstrument (0)->contentHash == ordinary->contentHash);
}

TEST_CASE ("plugin phase 5: a dropped phrase asks \"Identify notes?\" at once; one note, a recalled session or NO asks nothing", "[plugin][phase5][notemap][gui]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    // A phrase of three notes: asked as soon as the ordinary load is in, before any analysis.
    ordinaryPhrase (p, tmp.dir, { 69, 74, 77 });
    CHECK (p.mapNotesPrompt (0) == 3);
    CHECK (p.atlasActivity (0) == osp::plugin::InstrumentHost::AtlasActivity::none);
    ui->refreshNow();
    auto& card = ui->engineCard (0);
    REQUIRE (card.mapNotesPromptComponent() != nullptr);
    CHECK (card.mapNotesPromptComponent()->isVisible());
    CHECK (card.display().getBounds().contains (card.mapNotesPromptComponent()->getBounds()));
    // MAP NOTES: the analysis starts and the prompt is gone; the result is the mapped phrase.
    card.answerMapNotesPrompt (true);
    CHECK (card.mapNotesPromptComponent() == nullptr);
    CHECK (p.mapNotesPrompt (0) == 0);
    settleEdits (p);
    ui->refreshNow();
    REQUIRE (p.currentInstrument (0) != nullptr);
    REQUIRE (p.currentInstrument (0)->atlas != nullptr);
    CHECK (includedKeys (*p.currentInstrument (0)->atlas) == std::set<int> { 69, 74, 77 });
    CHECK (card.mapNotesPromptComponent() == nullptr);
    // Back to one sample (undo, or Play as one sample) asks nothing again.
    REQUIRE (p.undo().undo());
    p.pollLoads();
    ui->refreshNow();
    CHECK (p.mapNotesPrompt (0) == 0);
    CHECK (card.mapNotesPromptComponent() == nullptr);

    // NO keeps it one sample, and the prompt does not come back.
    {
        OspAudioProcessor q;
        std::unique_ptr<juce::AudioProcessorEditor> qe (q.createEditorIfNeeded());
        auto* qui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (qe.get());
        REQUIRE (qui != nullptr);
        auto rec = phraseRecording (tmp.dir, "second.wav", { 62, 65, 69, 72 });
        q.addLayers ({ rec.file });
        REQUIRE (q.waitForLoads (60000));
        q.pollLoads();
        CHECK (q.mapNotesPrompt (0) == 4);
        qui->refreshNow();
        auto& qcard = qui->engineCard (0);
        REQUIRE (qcard.mapNotesPromptComponent() != nullptr);
        qcard.answerMapNotesPrompt (false);
        qui->refreshNow();
        CHECK (qcard.mapNotesPromptComponent() == nullptr);
        CHECK (q.mapNotesPrompt (0) == 0);
        CHECK (q.currentInstrument (0)->atlas == nullptr);
        CHECK (q.atlasActivity (0) == osp::plugin::InstrumentHost::AtlasActivity::none);
        CHECK (q.canMapNotes (0));   // still on the menu

        // A recalled session holding a phrase never asks.
        juce::MemoryBlock state;
        q.getStateInformation (state);
        OspAudioProcessor restored;
        restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        REQUIRE (restored.waitForLoads (60000));
        restored.pollLoads();
        REQUIRE (restored.waitForLoads (60000));
        restored.pollLoads();
        REQUIRE (restored.currentInstrument (0) != nullptr);
        CHECK (restored.mapNotesPrompt (0) == 0);
    }

    // One held note asks nothing: the card is drawn as before.
    {
        OspAudioProcessor one;
        std::unique_ptr<juce::AudioProcessorEditor> oe (one.createEditorIfNeeded());
        auto* oui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (oe.get());
        REQUIRE (oui != nullptr);
        auto rec = phraseRecording (tmp.dir, "one.wav", { 69 });
        one.addLayers ({ rec.file });
        REQUIRE (one.waitForLoads (60000));
        one.pollLoads();
        CHECK (one.mapNotesPrompt (0) == 0);
        oui->refreshNow();
        CHECK (oui->engineCard (0).mapNotesPromptComponent() == nullptr);
    }
}

TEST_CASE ("plugin phase 5: a played note lights its own region, its read head inside it; keys and regions wear their pitch's colour", "[plugin][phase5][notemap][gui]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    ordinaryPhrase (p, tmp.dir, { 69, 74, 77 });
    p.mapNotes (0);
    settleEdits (p);
    p.setEditLayer (0);
    ui->refreshNow();
    const auto instrument = p.currentInstrument (0);
    REQUIRE (instrument != nullptr);
    REQUIRE (instrument->atlas != nullptr);
    const auto& atlas = *instrument->atlas;
    auto& display = ui->engineCard (0).display();
    CHECK (display.litNoteIndices().empty());

    // Hold 74: its region lights, and only its; the read head stays inside it.
    std::vector<int> lit;
    bool headsInside = true;
    int headsSeen = 0;
    const double total = instrument->durationSeconds * atlas.sources.front().sampleRate;
    renderHeld (p, { 74 }, 1.2, [&] (int pos) {
        if (pos < 4800 || pos > 40000)
            return;
        ui->refreshNow();
        for (const int i : display.litNoteIndices())
            if (std::find (lit.begin(), lit.end(), i) == lit.end())
                lit.push_back (i);
        const auto& snapshot = p.grainSnapshot (0);
        for (int h = 0; h < snapshot.playheads.load(); ++h, ++headsSeen)
        {
            const double frame = snapshot.playheadPosition[static_cast<std::size_t> (h)].load() * total;
            bool inside = false;
            for (const auto& n : atlas.notes)
                inside = inside || (n.playKey() == 74 && frame >= static_cast<double> (n.attackStart) - 0.02 * total && frame <= static_cast<double> (n.sourceEnd) + 0.02 * total);
            headsInside = headsInside && inside;
        }
    });
    CHECK (headsSeen > 0);
    CHECK (headsInside);
    REQUIRE (lit.size() == 1);
    CHECK (atlas.notes[static_cast<std::size_t> (lit.front())].playKey() == 74);

    // Colours by pitch: the keyboard's light strip and the regions share them.
    const auto& keys = ui->keyboardTints();
    CHECK (keys[74].state == osp::plugin::OspKeyboard::KeyTint::State::recorded);
    CHECK (keys[74].colour == osp::plugin::design::colour::forPitch (74));
    CHECK (keys[69].colour == osp::plugin::design::colour::forPitch (69));
    CHECK (osp::plugin::design::colour::forPitch (62) == osp::plugin::design::colour::forPitch (74));   // every D
    CHECK (osp::plugin::design::colour::forPitch (74) != osp::plugin::design::colour::forPitch (75));
}

TEST_CASE ("plugin phase 5: NOTE MAP edits are one undo step each, queued in order, and keep held notes sounding", "[plugin][phase5][notemap][realtime]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const auto rec = phraseRecording (tmp.dir, "edit.wav", { 69, 74, 77 });
    OspAudioProcessor p;
    p.loadAtlas (rec.atlas);
    settleEdits (p);
    REQUIRE (p.currentInstrument()->atlas != nullptr);
    p.undo().clearUndoHistory();
    const auto before = p.currentInstrument()->atlas;

    // A note held through an edit: Stage A then B of the edit arrive while it sounds.
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);
    midi.addEvent (juce::MidiMessage::noteOn (1, 69, static_cast<juce::uint8> (100)), 0);
    p.editAtlas (0, "Pitch", [] (atlas::NoteAtlas& a, const AudioData&) { return phrase::edit::setRoot (a, "n1", 76); });
    int allocations = 0;
    double quietest = 1.0;
    for (int b = 0; b < 140; ++b)
    {
        if (b % 6 == 0)
            p.pollLoads();
        {
            test::AllocationScope scope;
            buffer.clear();
            p.processBlock (buffer, midi);
            midi.clear();
            allocations += scope.count();
        }
        double sum = 0.0;
        for (int i = 0; i < 256; ++i)
            sum += static_cast<double> (buffer.getSample (0, i)) * buffer.getSample (0, i);
        if (b > 4 && b < 100)
            quietest = std::min (quietest, sum / 256.0);
    }
    settleEdits (p);
    CHECK (allocations == 0);
    CHECK (10.0 * std::log10 (quietest + 1.0e-30) > -60.0);
    const auto edited = p.currentInstrument()->atlas;
    REQUIRE (edited != nullptr);
    REQUIRE (edited->findNote ("n1") != nullptr);
    CHECK (edited->findNote ("n1")->rootMidi == 76);
    CHECK (edited->findNote ("n1")->corrections.pitch);
    CHECK (p.undo().getNumActionsInCurrentTransaction() >= 0);
    CHECK (p.undo().getUndoDescription() == "Pitch");

    // Three edits asked for at once: each edits the result of the one before.
    p.editAtlas (0, "Leave out", [] (atlas::NoteAtlas& a, const AudioData&) { return phrase::edit::setIncluded (a, "n0", false); });
    p.editAtlas (0, "Play on", [] (atlas::NoteAtlas& a, const AudioData&) { return phrase::edit::assignKey (a, "n2", 84); });
    p.editAtlas (0, "Fill off", [] (atlas::NoteAtlas& a, const AudioData&) {
        a.mapping.fill = false;
        return true;
    });
    // An edit that changes nothing publishes nothing and is no undo step.
    p.editAtlas (0, "Nothing", [] (atlas::NoteAtlas&, const AudioData&) { return false; });
    settleEdits (p);
    const auto last = p.currentInstrument()->atlas;
    REQUIRE (last != nullptr);
    CHECK_FALSE (last->findNote ("n0")->included);
    CHECK (last->findNote ("n2")->assignedKey == 84);
    CHECK_FALSE (last->mapping.fill);
    CHECK (last->findNote ("n1")->rootMidi == 76);   // the first edit is still there
    CHECK (p.undo().getUndoDescription() == "Fill off");
    CHECK (heldLevelDb (p, 84) > -50.0);
    CHECK (heldLevelDb (p, 90) < -100.0);   // FILL off: an unrecorded key is silent

    // Undo walks back one edit at a time (the instruments are still cached), redo forward.
    REQUIRE (p.undo().undo());
    settleEdits (p);
    CHECK (p.currentInstrument()->atlas->mapping.fill);
    REQUIRE (p.undo().undo());
    settleEdits (p);
    CHECK (p.currentInstrument()->atlas->findNote ("n2")->assignedKey == -1);
    REQUIRE (p.undo().undo());
    REQUIRE (p.undo().undo());
    settleEdits (p);
    CHECK (p.currentInstrument()->atlas->findNote ("n1")->rootMidi == before->findNote ("n1")->rootMidi);
    CHECK (p.currentInstrument()->atlas->findNote ("n0")->included);
    REQUIRE (p.undo().redo());
    settleEdits (p);
    CHECK (p.currentInstrument()->atlas->findNote ("n1")->rootMidi == 76);
    CHECK (p.loadState (0) == OspAudioProcessor::LoadState::ready);
}

TEST_CASE ("plugin phase 5: an evicted undo state is rebuilt from its atlas; the edit queue is bounded", "[plugin][phase5][notemap]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const auto rec = phraseRecording (tmp.dir, "many.wav", { 60, 64, 67 });
    OspAudioProcessor p;
    p.loadAtlas (rec.atlas);
    settleEdits (p);
    p.undo().clearUndoHistory();
    // Twelve edits, one after another: more than the eight instruments kept per layer.
    for (int i = 0; i < 12; ++i)
    {
        const int key = 70 + i;
        p.undo().beginNewTransaction();
        p.editAtlas (0, "Play on " + juce::String (key), [key] (atlas::NoteAtlas& a, const AudioData&) { return phrase::edit::assignKey (a, "n0", key); });
        settleEdits (p);
    }
    CHECK (p.currentInstrument()->atlas->findNote ("n0")->assignedKey == 81);
    for (int i = 0; i < 11; ++i)
        REQUIRE (p.undo().undo());
    settleEdits (p);
    // The first edit's state was evicted from the cache: it is prepared again from its atlas.
    CHECK (p.currentInstrument()->atlas->findNote ("n0")->assignedKey == 70);
    CHECK (heldLevelDb (p, 70) > -50.0);

    // A burst far faster than preparation: kept to the last 32 waiting, never unbounded.
    for (int i = 0; i < 100; ++i)
        p.editAtlas (0, "burst", [i] (atlas::NoteAtlas& a, const AudioData&) {
            a.mapping.maxTransposeSemitones = 1.0 + (i % 20);
            return true;
        });
    settleEdits (p);
    CHECK (p.currentInstrument()->atlas->mapping.maxTransposeSemitones == Approx (1.0 + (99 % 20)));
}

TEST_CASE ("plugin phase 5: DETECTION again keeps every correction and reports where it disagrees", "[plugin][phase5][notemap][analysis]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const auto rec = phraseRecording (tmp.dir, "sung.wav", { 69, 74, 77, 81 });
    OspAudioProcessor p;
    p.analyseRecording (rec.file);
    settleEdits (p);
    const auto analysed = p.currentInstrument()->atlas;
    REQUIRE (analysed != nullptr);
    REQUIRE (analysed->notes.size() >= 3);
    const auto corrected = analysed->notes[1].id, out = analysed->notes[2].id;
    p.editAtlas (0, "Play on", [corrected] (atlas::NoteAtlas& a, const AudioData&) { return phrase::edit::assignKey (a, corrected, 88); });
    p.editAtlas (0, "Leave out", [out] (atlas::NoteAtlas& a, const AudioData&) { return phrase::edit::setIncluded (a, out, false); });
    settleEdits (p);
    p.mapNotes (0, phrase::Detection::more);
    settleEdits (p);
    const auto again = p.currentInstrument()->atlas;
    REQUIRE (again != nullptr);
    CHECK (osp::plugin::InstrumentHost::detectionOf (*again) == phrase::Detection::more);
    REQUIRE (again->findNote (corrected) != nullptr);
    CHECK (again->findNote (corrected)->assignedKey == 88);
    REQUIRE (again->findNote (out) != nullptr);
    CHECK_FALSE (again->findNote (out)->included);
    CHECK (again->atlasId == analysed->atlasId);
    CHECK (p.redetectReport (0).keptCorrected == 2);
    CHECK (p.atlasReport (0).contains ("2 corrections kept"));
    CHECK (heldLevelDb (p, 88) > -50.0);
    // One undo step back to the earlier detection.
    CHECK (p.undo().getUndoDescription().startsWith ("Detect notes"));
    REQUIRE (p.undo().undo());
    settleEdits (p);
    CHECK (osp::plugin::InstrumentHost::detectionOf (*p.currentInstrument()->atlas) == phrase::Detection::normal);
}

TEST_CASE ("plugin phase 5: the NOTE MAP's controls edit the atlas (pitch, key, split, merge, include, fill)", "[plugin][phase5][notemap][gui]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const auto rec = phraseRecording (tmp.dir, "map.wav", { 64, 67, 71 });
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    p.loadAtlas (rec.atlas);
    settleEdits (p);
    editor->setSize (1448, 1086);
    ui->refreshNow();
    auto& card = ui->engineCard (0);
    REQUIRE (card.notesToggle().isVisible());
    card.onNotesToggle (0, true);
    REQUIRE (card.isNotesOpen());
    CHECK_FALSE (card.isEqOpen());
    auto* map = card.noteMapComponent();
    REQUIRE (map != nullptr);
    CHECK (map->getBounds() == card.display().getBounds());   // the display's place exactly
    map->refresh();
    // Nothing selected: the map's own controls.
    CHECK (map->actions().contains ("detect.more"));
    CHECK (map->actions().contains ("fill"));
    CHECK_FALSE (map->actions().contains ("split"));
    // Every note is a region over the waveform.
    for (const auto* id : { "n0", "n1", "n2" })
        CHECK_FALSE (map->regionBounds (id).isEmpty());

    map->select ({ "n1" });
    CHECK (map->actions().contains ("pitch+"));
    REQUIRE (map->perform ("oct+"));
    settleEdits (p);
    map->refresh();
    CHECK (p.currentInstrument()->atlas->findNote ("n1")->rootMidi == 79);
    REQUIRE (map->perform ("key-"));
    settleEdits (p);
    map->refresh();
    CHECK (p.currentInstrument()->atlas->findNote ("n1")->assignedKey == 78);
    REQUIRE (map->perform ("key.auto"));
    settleEdits (p);
    map->refresh();
    CHECK (p.currentInstrument()->atlas->findNote ("n1")->assignedKey == -1);
    // Split it, then merge the halves back.
    REQUIRE (map->perform ("split"));
    settleEdits (p);
    map->refresh();
    CHECK (p.currentInstrument()->atlas->notes.size() == 4);
    CHECK (map->selection().size() == 2);
    REQUIRE (map->perform ("merge"));
    settleEdits (p);
    map->refresh();
    CHECK (p.currentInstrument()->atlas->notes.size() == 3);
    // Leave one out: its key falls silent (FILL takes a neighbour within reach instead).
    map->select ({ "n2" });
    REQUIRE (map->perform ("include"));
    settleEdits (p);
    map->refresh();
    CHECK_FALSE (p.currentInstrument()->atlas->findNote ("n2")->included);
    // The map's FILL: off, then on with less reach.
    map->select ({});
    REQUIRE (map->perform ("fill"));
    settleEdits (p);
    map->refresh();
    CHECK_FALSE (p.currentInstrument()->atlas->mapping.fill);
    // The keyboard shows the edited layer's map: recorded, filled, silent.
    const auto keys = osp::plugin::NoteMapEditor::keyMap (*p.currentInstrument()->atlas);
    CHECK (keys[64].state == osp::plugin::OspKeyboard::KeyTint::State::recorded);
    CHECK (keys[65].state == osp::plugin::OspKeyboard::KeyTint::State::silent);
    // A double-click inside a note splits it there; an audition never changes anything.
    const auto r = map->regionBounds ("n0");
    REQUIRE_FALSE (r.isEmpty());
    const auto count = p.currentInstrument()->atlas->notes.size();
    map->select ({ "n0" });
    REQUIRE (map->perform ("audition"));
    CHECK (p.currentInstrument()->atlas->notes.size() == count);
    // EQ from the NOTE MAP's tab: one page at a time.
    card.onEqToggle (0, true);
    CHECK (card.isEqOpen());
    CHECK_FALSE (card.isNotesOpen());
}

TEST_CASE ("plugin phase 5: NOTE MAP corrections and KEY DNA curves survive the session; no audio in host state", "[plugin][phase5][notemap][state]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const auto rec = phraseRecording (tmp.dir, "keep.wav", { 62, 65, 69 });
    OspAudioProcessor p;
    p.loadAtlas (rec.atlas);
    settleEdits (p);
    p.editAtlas (0, "Pitch", [] (atlas::NoteAtlas& a, const AudioData&) { return phrase::edit::setRoot (a, "n2", 70); });
    p.editAtlas (0, "Reach", [] (atlas::NoteAtlas& a, const AudioData&) {
        a.mapping.maxTransposeSemitones = 5.0;
        return true;
    });
    settleEdits (p);
    // KEY DNA on layer 0 with a drawn SIZE curve (three points, one bent).
    auto dna = p.keyDnaSettings (0);
    dna.enabled = true;
    dna[keydna::Param::size].curveDepth = 0.6;
    p.setKeyDnaSettings (0, dna, "KEY DNA", true);
    mod::Curve drawn;
    drawn.count = 3;
    drawn.points[0] = { 0.0f, 0.2f, 0.0f };
    drawn.points[1] = { 0.5f, 0.9f, 0.4f };
    drawn.points[2] = { 1.0f, 0.3f, -0.2f };
    p.setKeyDnaCurve (0, keydna::Param::size, drawn, true);
    const auto settingsBefore = p.keyDnaSettings (0);

    juce::MemoryBlock state;
    p.getStateInformation (state);
    CHECK (state.getSize() < 200000);   // the atlas and the curves, never the audio
    OspAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    settleEdits (restored);
    const auto back = restored.currentInstrument (0);
    REQUIRE (back != nullptr);
    REQUIRE (back->atlas != nullptr);
    CHECK (back->atlas->findNote ("n2")->rootMidi == 70);
    CHECK (back->atlas->findNote ("n2")->corrections.pitch);
    CHECK (back->atlas->mapping.maxTransposeSemitones == Approx (5.0));
    CHECK (restored.keyDnaSettings (0) == settingsBefore);
    CHECK (restored.keyDnaCurve (0, keydna::Param::size) == p.keyDnaCurve (0, keydna::Param::size));
    CHECK (restored.keyDnaCurve (0, keydna::Param::size).count == 3);
    // A parameter without drawn points still opens with a curve to edit (flat).
    CHECK (restored.keyDnaCurve (0, keydna::Param::tune).count == 2);
}

TEST_CASE ("plugin phase 5: KEY DNA's editor is undoable, follows AMOUNT live and sits over the keys", "[plugin][phase5][keydna][gui]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const auto a = writeSource (tmp.dir, "pad.wav", testsignals::vowel (midiToHz (60), 2.0, 48000.0, 3));
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    p.addLayers ({ a });
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.setParameterValue ("layerA.sourceMode", 1.0f);
    editor->setSize (1448, 1086);
    ui->refreshNow();
    p.undo().clearUndoHistory();
    // The Granular strip's KEY DNA key opens the editor over the instrument, keyboard live.
    ui->openKeyDna (0);
    auto* dnaEditor = ui->keyDnaWindow();
    REQUIRE (dnaEditor != nullptr);
    CHECK (dnaEditor->isVisible());
    // The profile under each key is where the real key is.
    {
        auto& keyboard = ui->keyboardComponent();
        const auto key = keyboard.getRectangleForKey (60).translated (static_cast<float> (keyboard.getX()), 0.0f);
        CHECK (std::abs (dnaEditor->getX() + dnaEditor->xForNote (60) - key.getCentreX()) < 1.0f);
    }
    // VARIATION on SIZE: heard as it moves, one undo step when released.
    dnaEditor->showParameter (keydna::Param::size);
    dnaEditor->setValue ("variation", 0.4, false);
    dnaEditor->setValue ("variation", 0.7, false);
    CHECK (p.keyDnaSettings (0)[keydna::Param::size].variation == Approx (0.7));
    CHECK_FALSE (p.undo().canUndo());
    dnaEditor->setValue ("variation", 0.8, true);
    CHECK (p.keyDnaSettings (0).enabled);
    CHECK (p.undo().canUndo());
    REQUIRE (p.undo().undo());
    CHECK (p.keyDnaSettings (0)[keydna::Param::size].variation == Approx (0.0));
    CHECK_FALSE (p.keyDnaSettings (0).enabled);
    REQUIRE (p.undo().redo());
    CHECK (p.keyDnaSettings (0)[keydna::Param::size].variation == Approx (0.8));
    // The profile the editor shows is the processor's (the same computation), AMOUNT applied.
    dnaEditor->refresh();
    keydna::Table table;
    table.compute (p.keyDnaSettings (0));
    CHECK (dnaEditor->shownOffset (64) == Approx (table.offset (64, keydna::Param::size)));
    p.setParameterValue (osp::plugin::InstrumentHost::keyDnaAmountId (0), 50.0f);
    dnaEditor->refresh();
    CHECK (dnaEditor->shownOffset (64) == Approx (0.5 * table.offset (64, keydna::Param::size)).margin (1.0e-6));
    // RANDOMIZE: another seed, deterministic from this one.
    const auto seed = p.keyDnaSettings (0).seed;
    {
        const auto at = dnaEditor->controlArea ("randomize").getCentre();
        dnaEditor->mouseDown (mouseAt (*dnaEditor, at, at, false));
    }
    CHECK (p.keyDnaSettings (0).seed != seed);
    ui->closeKeyDna();
    CHECK (ui->keyDnaWindow() == nullptr);
}

TEST_CASE ("plugin phase 5: an atlas, an ordinary sample and a Granular layer with KEY DNA play together (workflow D)", "[plugin][phase5][notemap][keydna]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const auto rec = phraseRecording (tmp.dir, "a.wav", { 57, 60, 64 });
    const auto b = writeSource (tmp.dir, "b.wav", testsignals::pluck (220.0, 2.0, 48000.0, 4));
    const auto c = writeSource (tmp.dir, "c.wav", testsignals::vowel (midiToHz (60), 2.0, 48000.0, 5));
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    p.addLayers ({ rec.file, b, c });
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.mapNotes (0);
    settleEdits (p);
    p.setParameterValue ("layerC.sourceMode", 1.0f);
    auto dna = p.keyDnaSettings (2);
    dna.enabled = true;
    dna[keydna::Param::tune].variation = 0.5;
    p.setKeyDnaSettings (2, dna, "KEY DNA", true);
    editor->setSize (1448, 1086);
    ui->refreshNow();
    REQUIRE (p.currentInstrument (0)->atlas != nullptr);
    CHECK (p.currentInstrument (1)->atlas == nullptr);
    CHECK (p.currentInstrument (2)->atlas == nullptr);
    // Each card shows only its own context.
    CHECK (ui->engineCard (0).notesToggle().isVisible());
    CHECK_FALSE (ui->engineCard (1).notesToggle().isVisible());
    CHECK_FALSE (ui->engineCard (2).notesToggle().isVisible());
    // A chord sounds on every layer; AMOUNT moves while it is held, the audio thread allocates nothing.
    p.prepareToPlay (48000.0, 256);
    int allocations = 0;
    const auto audio = renderHeld (p, { 57, 60, 64 }, 1.2, [&] (int pos) {
        if (pos == 48000 / 2)
        {
            p.setParameterValue (osp::plugin::InstrumentHost::keyDnaAmountId (2), 20.0f);
            p.pollLoads();
        }
    });
    {
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        midi.ensureSize (1024);
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (90)), 0);
        for (int i = 0; i < 40; ++i)
        {
            test::AllocationScope scope;
            buffer.clear();
            p.processBlock (buffer, midi);
            midi.clear();
            allocations += scope.count();
        }
    }
    CHECK (allocations == 0);
    double sum = 0.0;
    for (const auto x : audio.channels[0])
        sum += static_cast<double> (x) * x;
    CHECK (10.0 * std::log10 (sum / static_cast<double> (audio.channels[0].size()) + 1.0e-30) > -40.0);
    // The keyboard shows layer A's map when A is edited, nothing for B.
    p.setEditLayer (1);
    ui->refreshNow();
    CHECK (ui->keyboardTints()[60].state == osp::plugin::OspKeyboard::KeyTint::State::none);
    p.setEditLayer (0);
    ui->refreshNow();
    CHECK (ui->keyboardTints()[60].state == osp::plugin::OspKeyboard::KeyTint::State::recorded);
    CHECK (ui->keyboardTints()[61].state == osp::plugin::OspKeyboard::KeyTint::State::filled);
}

TEST_CASE ("plugin phase 5: screenshots of the new states (OSP_SNAPSHOT_DIR)", "[plugin][phase5][snapshots]")
{
    const char* dir = std::getenv ("OSP_SNAPSHOT_DIR");
    if (dir == nullptr)
        return;
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedLook ceramic (skin::Look::ceramic);
    auto save = [dir] (const juce::Image& image, const juce::String& name) {
        juce::FileOutputStream out (juce::File (dir).getChildFile (name));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
    };
    // Keys the analysis reads correctly on OSP's test vowel (UnifiedPhase4Tests.inc), one repeated (two takes).
    const auto rec = phraseRecording (tmp.dir, "phrase.wav", { 69, 74, 77, 81, 74, 79 });
    const auto pad = writeSource (tmp.dir, "pad.wav", testsignals::vowel (midiToHz (60), 2.4, 48000.0, 3));
    const auto pluck = writeSource (tmp.dir, "pluck.wav", testsignals::pluck (220.0, 2.4, 48000.0, 4));

    auto shoot = [&] (std::vector<juce::File> files, const std::function<void (OspAudioProcessor&, osp::plugin::OspAudioProcessorEditor&)>& setUp,
                      const juce::String& name) {
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
        REQUIRE (ui != nullptr);
        juce::Array<juce::File> list;
        for (const auto& f : files)
            list.add (f);
        p.addLayers (list);
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        e->setSize (1448, 1086);
        setUp (p, *ui);
        ui->refreshNow();
        ui->refreshNow();
        save (e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f), name + "@1x.png");
    };
    // 1 An ordinary one-shot: unchanged (no NOTES key, no regions).
    shoot ({ pad }, [] (OspAudioProcessor&, osp::plugin::OspAudioProcessorEditor&) {}, "p5-01-one-shot-unchanged");
    // 2 MAP NOTES just pressed: ANALYZING.
    shoot ({ rec.file }, [] (OspAudioProcessor& p, osp::plugin::OspAudioProcessorEditor&) { p.mapNotes (0); }, "p5-02-analyzing");
    // 3b Mapped, one note held: its region lit in its colour, the read head inside it; the
    // keyboard's light strip along the top of the keys.
    shoot ({ rec.file }, [] (OspAudioProcessor& p, osp::plugin::OspAudioProcessorEditor&) {
        p.mapNotes (0);
        settleEdits (p);
        p.setEditLayer (0);
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buffer (2, 256);
        for (int b = 0; b < 160; ++b)   // 0.85 s held: past the note's own length, looping
        {
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 77, static_cast<juce::uint8> (100)), 0);
            buffer.clear();
            p.processBlock (buffer, midi);
        }
    }, "p5-12-playing");
    // 2b A phrase just dropped: "Identify notes?" at once (the musical review).
    shoot ({ rec.file }, [] (OspAudioProcessor&, osp::plugin::OspAudioProcessorEditor&) {}, "p5-11-identify-notes");
    // 3 Mapped: regions over the waveform, NOTES key, keyboard tints.
    shoot ({ rec.file }, [] (OspAudioProcessor& p, osp::plugin::OspAudioProcessorEditor&) {
        p.mapNotes (0);
        settleEdits (p);
    }, "p5-03-mapped");
    // 4 The NOTE MAP open, nothing selected (hero), then a note selected.
    shoot ({ rec.file }, [] (OspAudioProcessor& p, osp::plugin::OspAudioProcessorEditor& ui) {
        p.mapNotes (0);
        settleEdits (p);
        ui.refreshNow();
        ui.engineCard (0).onNotesToggle (0, true);
    }, "p5-04-note-map");
    shoot ({ rec.file }, [] (OspAudioProcessor& p, osp::plugin::OspAudioProcessorEditor& ui) {
        p.mapNotes (0);
        settleEdits (p);
        ui.refreshNow();
        ui.engineCard (0).onNotesToggle (0, true);
        if (auto* map = ui.engineCard (0).noteMapComponent())
        {
            map->refresh();
            const auto a = p.currentInstrument (0)->atlas;
            if (a != nullptr && a->notes.size() > 2)
                map->select ({ a->notes[2].id });
        }
    }, "p5-05-note-selected");
    // 6 Dual and triple density.
    shoot ({ rec.file, pluck }, [] (OspAudioProcessor& p, osp::plugin::OspAudioProcessorEditor& ui) {
        p.mapNotes (0);
        settleEdits (p);
        ui.refreshNow();
        ui.engineCard (0).onNotesToggle (0, true);
        if (auto* map = ui.engineCard (0).noteMapComponent())
        {
            map->refresh();
            map->select ({ p.currentInstrument (0)->atlas->notes[1].id });
        }
    }, "p5-06-note-map-dual");
    shoot ({ rec.file, pluck, pad }, [] (OspAudioProcessor& p, osp::plugin::OspAudioProcessorEditor& ui) {
        p.mapNotes (0);
        settleEdits (p);
        ui.refreshNow();
        ui.engineCard (0).onNotesToggle (0, true);
        if (auto* map = ui.engineCard (0).noteMapComponent())
        {
            map->refresh();
            map->select ({ p.currentInstrument (0)->atlas->notes[1].id });
        }
    }, "p5-07-note-map-triple");
    // 8 Granular with the KEY DNA key in its strip, then the KEY DNA editor.
    shoot ({ pad }, [] (OspAudioProcessor& p, osp::plugin::OspAudioProcessorEditor& ui) {
        p.setParameterValue ("layerA.sourceMode", 1.0f);
        ui.refreshNow();
        ui.engineCard (0).showGranularControls (true, true);
    }, "p5-08-granular-strip");
    shoot ({ pad }, [] (OspAudioProcessor& p, osp::plugin::OspAudioProcessorEditor& ui) {
        p.setParameterValue ("layerA.sourceMode", 1.0f);
        auto dna = p.keyDnaSettings (0);
        dna.enabled = true;
        dna[keydna::Param::size].variation = 0.45;
        dna[keydna::Param::size].curveDepth = 0.7;
        p.setKeyDnaSettings (0, dna, "KEY DNA", true);
        mod::Curve c;
        c.count = 3;
        c.points[0] = { 0.0f, 0.85f, 0.0f };
        c.points[1] = { 0.55f, 0.45f, 0.3f };
        c.points[2] = { 1.0f, 0.15f, 0.0f };
        p.setKeyDnaCurve (0, keydna::Param::size, c, true);
        ui.refreshNow();
        ui.openKeyDna (0);
        if (auto* d = ui.keyDnaWindow())
            d->showParameter (keydna::Param::size);
        p.keyboardState.noteOn (1, 60, 0.8f);
        p.keyboardState.noteOn (1, 64, 0.8f);
        p.keyboardState.noteOn (1, 67, 0.8f);
    }, "p5-09-key-dna");
    // A real recording (local only: OSP_SNAPSHOT_RECORDING names it; nothing of it is kept).
    if (const char* real = std::getenv ("OSP_SNAPSHOT_RECORDING"); real != nullptr && juce::File (real).existsAsFile())
    {
        const juce::File recording (real);
        shoot ({ recording }, [] (OspAudioProcessor& p, osp::plugin::OspAudioProcessorEditor&) {
            p.mapNotes (0);
            settleEdits (p);
        }, "p5-real-mapped");
        shoot ({ recording }, [] (OspAudioProcessor& p, osp::plugin::OspAudioProcessorEditor& ui) {
            p.mapNotes (0);
            settleEdits (p);
            ui.refreshNow();
            ui.engineCard (0).onNotesToggle (0, true);
            if (auto* map = ui.engineCard (0).noteMapComponent())
            {
                map->refresh();
                const auto a = p.currentInstrument (0)->atlas;
                if (a != nullptr && a->notes.size() > 1)
                    map->select ({ a->notes[a->notes.size() / 2].id });
            }
        }, "p5-real-note-map");
    }
    // 10 A/B/C: A mapped, B one-shot, C Granular with KEY DNA.
    shoot ({ rec.file, pluck, pad }, [] (OspAudioProcessor& p, osp::plugin::OspAudioProcessorEditor&) {
        p.mapNotes (0);
        settleEdits (p);
        p.setParameterValue ("layerC.sourceMode", 1.0f);
        auto dna = p.keyDnaSettings (2);
        dna.enabled = true;
        dna[keydna::Param::tune].variation = 0.5;
        p.setKeyDnaSettings (2, dna, "KEY DNA", true);
        p.setEditLayer (0);
    }, "p5-10-abc");
}

TEST_CASE ("plugin phase 5: KEY DNA keys drawn by hand - dragging a bar, one undo step, returned by Option-click, saved", "[plugin][phase5][keydna][gui]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const auto a = writeSource (tmp.dir, "pad.wav", testsignals::vowel (midiToHz (60), 2.0, 48000.0, 3));
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    p.addLayers ({ a });
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.setParameterValue ("layerA.sourceMode", 1.0f);
    editor->setSize (1448, 1086);
    ui->refreshNow();
    p.undo().clearUndoHistory();
    ui->openKeyDna (0);
    auto* dna = ui->keyDnaWindow();
    REQUIRE (dna != nullptr);
    dna->showParameter (keydna::Param::size);
    const auto graph = dna->graphArea();
    // Away from the flat curve (its middle): high on C4, then a stroke across to E4, lower.
    const juce::Point<float> c4 (dna->xForNote (60), graph.getY() + 0.2f * graph.getHeight());
    const juce::Point<float> e4 (dna->xForNote (64), graph.getY() + 0.35f * graph.getHeight());
    CHECK (dna->keyAt (c4) == 60);
    // A click alone draws nothing (a double-click still adds a curve point).
    dna->mouseDown (mouseAt (*dna, c4, c4, false));
    dna->mouseUp (mouseAt (*dna, c4, c4, false));
    CHECK_FALSE (p.keyDnaSettings (0)[keydna::Param::size].anyDrawn());
    CHECK_FALSE (p.undo().canUndo());
    // The drag: heard as it moves, one undo step on release; KEY DNA comes on.
    dna->mouseDown (mouseAt (*dna, c4, c4, false));
    dna->mouseDrag (mouseAt (*dna, c4 + juce::Point<float> (0.0f, 6.0f), c4, true));
    const float first = p.keyDnaSettings (0)[keydna::Param::size].drawn[60];
    CHECK (first == Approx (dna->unitAt (c4.y + 6.0f)));
    CHECK (first > 0.4f);
    CHECK_FALSE (p.undo().canUndo());
    dna->mouseDrag (mouseAt (*dna, e4, c4, true));
    dna->mouseUp (mouseAt (*dna, e4, c4, true));
    const auto drawn = p.keyDnaSettings (0);
    CHECK (drawn.enabled);
    for (const int k : { 60, 61, 62, 63, 64 })
        CHECK (drawn[keydna::Param::size].isDrawn (k));   // a fast stroke skips no key
    CHECK_FALSE (drawn[keydna::Param::size].isDrawn (65));
    CHECK (drawn[keydna::Param::size].drawn[64] == Approx (dna->unitAt (e4.y)));
    CHECK (drawn[keydna::Param::size].drawn[62] < drawn[keydna::Param::size].drawn[60]);
    dna->refresh();
    CHECK (dna->shownOffset (64) == Approx (dna->unitAt (e4.y)).margin (1.0e-5));   // the bar ends at the pointer
    REQUIRE (p.undo().undo());
    CHECK_FALSE (p.keyDnaSettings (0)[keydna::Param::size].anyDrawn());
    REQUIRE (p.undo().redo());
    CHECK (p.keyDnaSettings (0)[keydna::Param::size].isDrawn (62));
    dna->refresh();

    // Saved with the session (and reopens exactly); an older session has none.
    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    {
        OspAudioProcessor q;
        q.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        CHECK (q.keyDnaSettings (0)[keydna::Param::size].drawn == p.keyDnaSettings (0)[keydna::Param::size].drawn);
    }

    // Option-click on a drawn key returns it to the curve (one undo step).
    const juce::Point<float> d4 (dna->xForNote (62), graph.getY() + 0.8f * graph.getHeight());
    dna->mouseDown (mouseAt (*dna, d4, d4, false, 1, juce::ModifierKeys (juce::ModifierKeys::altModifier)));
    dna->mouseUp (mouseAt (*dna, d4, d4, false, 1, juce::ModifierKeys (juce::ModifierKeys::altModifier)));
    CHECK_FALSE (p.keyDnaSettings (0)[keydna::Param::size].isDrawn (62));
    CHECK (p.keyDnaSettings (0)[keydna::Param::size].isDrawn (61));
    REQUIRE (p.undo().undo());
    CHECK (p.keyDnaSettings (0)[keydna::Param::size].isDrawn (62));
    dna->refresh();   // (the editor's timer, in the plugin)

    // The curve still bends from its line, and Reset clears the drawn keys with the rest.
    const auto curveBefore = p.keyDnaCurve (0, keydna::Param::size);
    const juce::Point<float> onLine (dna->xForNote (84), graph.getCentreY());
    dna->mouseDown (mouseAt (*dna, onLine, onLine, false));
    dna->mouseDrag (mouseAt (*dna, onLine + juce::Point<float> (0.0f, -30.0f), onLine, true));
    dna->mouseUp (mouseAt (*dna, onLine + juce::Point<float> (0.0f, -30.0f), onLine, true));
    CHECK (p.keyDnaSettings (0)[keydna::Param::size].isDrawn (62));   // no key drawn by the bend
    CHECK_FALSE (p.keyDnaSettings (0)[keydna::Param::size].isDrawn (84));
    juce::ignoreUnused (curveBefore);
    {
        const auto at = dna->controlArea ("reset").getCentre();
        dna->mouseDown (mouseAt (*dna, at, at, false));
    }
    CHECK_FALSE (p.keyDnaSettings (0)[keydna::Param::size].anyDrawn());
    ui->closeKeyDna();
}
