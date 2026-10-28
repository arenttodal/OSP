// The unified engine's Phase 3 in ANDOR OSP (docs/research/unified-andor/phase-3-report.md): a
// Note Atlas on a layer - its notes become the layer's multi-sample instrument, built on the
// loader thread and published through the layer's ModelExchange. Its session state (v14) holds
// the atlas, never the audio; a session whose recordings are missing opens empty and failed,
// naming them. Older sessions keep Phase 2's shared grain budget. Included at the end of
// PluginProcessorTests.cpp (after UnifiedPhase2Tests.inc, whose helpers it uses).

#include "io/ContentHash.h"
#include "io/NoteAtlasJson.h"

namespace
{
    struct AtlasRecording
    {
        juce::File file;
        atlas::NoteAtlas atlas;
    };

    /** A recorded phrase: vowels on `keys`, 0.8 s each, 0.2 s of silence between, written as a
        WAV file; the atlas places each note by hand (no analysis) and names the file by its
        content hash, as an import would. */
    AtlasRecording phraseRecording (const juce::File& dir, const juce::String& name, std::vector<int> keys, int channels = 2,
                                    atlas::AssetOrigin origin = atlas::AssetOrigin::fileImport)
    {
        const double rate = 48000.0;
        const auto noteFrames = static_cast<std::int64_t> (0.8 * rate), gap = static_cast<std::int64_t> (0.2 * rate);
        const auto total = static_cast<std::int64_t> (keys.size()) * (noteFrames + gap);
        auto audio = AudioData::allocate (channels, total, rate);
        AtlasRecording r;
        r.atlas.atlasId = "phrase-" + name.toStdString();
        r.atlas.createdBy = "osp plugin tests";
        for (std::size_t k = 0; k < keys.size(); ++k)
        {
            const auto note = testsignals::vowel (midiToHz (keys[k]), 0.8, rate, 30 + k, 0.5, channels);
            const auto start = static_cast<std::int64_t> (k) * (noteFrames + gap);
            for (int c = 0; c < channels; ++c)
                std::copy (note.channels[static_cast<std::size_t> (c)].begin(), note.channels[static_cast<std::size_t> (c)].begin() + noteFrames,
                           audio.channels[static_cast<std::size_t> (c)].begin() + start);
            atlas::NoteEvent n;
            n.id = "n" + std::to_string (k);
            n.sourceId = "src0";
            n.sourceStart = n.attackStart = start;
            n.sourceEnd = start + noteFrames;
            n.sustain = { start + noteFrames / 5, start + noteFrames * 3 / 4 };
            n.releaseStart = n.sustain.end;
            n.fadeOutFrames = 480;
            n.rootMidi = keys[k];
            n.midiPitch = keys[k];
            n.pitchHz = midiToHz (keys[k]);
            n.confidence = 1.0;
            n.voicing = 1.0;
            r.atlas.notes.push_back (n);
        }
        r.file = writeSource (dir, name, audio);
        atlas::SourceAsset src;
        src.assetId = "src0";
        src.origin = origin;
        src.contentHash = io::contentId (*io::sha256OfFile (std::filesystem::path (r.file.getFullPathName().toStdString())));
        src.fileName = name.toStdString();
        if (origin == atlas::AssetOrigin::fileImport)
            src.lastKnownPath = r.file.getFullPathName().toStdString();
        else
            src.captureId = "capture-0001";
        src.sampleRate = rate;
        src.numChannels = channels;
        src.numFrames = total;
        r.atlas.sources.push_back (src);
        return r;
    }

    /** One held note's level (dBFS RMS of both channels, the first 0.4 s). */
    double heldLevelDb (OspAudioProcessor& p, int note)
    {
        const auto audio = renderHeld (p, { note }, 0.5);
        double sum = 0.0;
        std::int64_t n = 0;
        for (const auto& ch : audio.channels)
            for (std::size_t i = 0; i < static_cast<std::size_t> (0.4 * 48000.0); ++i, ++n)
                sum += static_cast<double> (ch[i]) * ch[i];
        return 10.0 * std::log10 (sum / static_cast<double> (std::max<std::int64_t> (1, n)) + 1.0e-30);
    }

    juce::MemoryBlock withoutAtlasAudio (const juce::MemoryBlock& state, const std::string& hint)
    {
        // The session as another machine sees it: same atlas, the file's path pointing nowhere.
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
        REQUIRE (xml != nullptr);
        auto* atlasXml = xml->getChildByName ("Instrument")->getChildByName ("Atlas");
        REQUIRE (atlasXml != nullptr);
        std::string error;
        auto a = io::noteAtlasFromString (atlasXml->getStringAttribute ("json").toStdString(), error);
        REQUIRE (a.has_value());
        for (auto& s : a->sources)
            s.lastKnownPath = hint;
        atlasXml->setAttribute ("json", juce::String (io::noteAtlasToString (*a)));
        juce::MemoryBlock out;
        juce::AudioProcessor::copyXmlToBinary (*xml, out);
        return out;
    }
}

TEST_CASE ("plugin: a Note Atlas plays as the layer's instrument, each note on its own key", "[plugin][phase3][atlas]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const auto rec = phraseRecording (tmp.dir, "phrase.wav", { 57, 60, 64 });
    OspAudioProcessor p;
    p.loadAtlas (rec.atlas);
    settle (p);
    REQUIRE (p.loadState() == OspAudioProcessor::LoadState::ready);
    const auto instrument = p.currentInstrument();
    REQUIRE (instrument != nullptr);
    REQUIRE (instrument->set != nullptr);
    REQUIRE (instrument->atlas != nullptr);
    CHECK (instrument->set->members.size() == 3);
    CHECK (instrument->rootOrigin == "atlas");
    CHECK (instrument->atlasSources.size() == 1);
    CHECK (p.missingSources().empty());
    // The recorded keys sound; a key beyond FILL's 12 semitones does not (and steals nothing).
    for (int key : { 57, 60, 64, 70 })
        CHECK (heldLevelDb (p, key) > -50.0);
    CHECK (heldLevelDb (p, 90) < -100.0);
    // The Samples inspector's reassignment does not apply to an atlas (its places are its notes).
    const auto generation = instrument->generation;
    p.reassignSample (instrument->set->members[0].filename, SampleRole::roundRobin, 0);
    settle (p);
    CHECK (p.currentInstrument()->generation == generation);
}

TEST_CASE ("plugin: an atlas session saves the atlas, not the audio, and reopens exactly", "[plugin][phase3][atlas]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    auto rec = phraseRecording (tmp.dir, "phrase.wav", { 55, 62, 67 });
    rec.atlas.mapping.maxTransposeSemitones = 4.0;
    rec.atlas.notes[1].centsOffset = 30.0;
    rec.atlas.notes[1].corrections.pitch = true;
    juce::MemoryBlock state;
    juce::String original;
    {
        OspAudioProcessor p;
        p.loadAtlas (rec.atlas, 1);
        settle (p);
        p.setParameterValue ("layerB.sourceMode", 1.0f);   // granular from the atlas
        p.getStateInformation (state);
        original = audioHash (renderHeld (p, { 55, 60, 62, 67 }, 1.0));
    }
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
        REQUIRE (xml != nullptr);
        CHECK (xml->getIntAttribute ("stateVersion") == 14);
        CHECK (xml->getStringAttribute ("grainBudgetPolicy") == "bounded");
        const auto* b = xml->getChildByName ("InstrumentB");
        REQUIRE (b != nullptr);
        CHECK (b->getChildByName ("Set") == nullptr);
        const auto* atlasXml = b->getChildByName ("Atlas");
        REQUIRE (atlasXml != nullptr);
        std::string error;
        const auto saved = io::noteAtlasFromString (atlasXml->getStringAttribute ("json").toStdString(), error);
        REQUIRE (saved.has_value());
        CHECK (saved->notes.size() == 3);
        CHECK (saved->mapping == rec.atlas.mapping);
        CHECK (saved->notes[1].centsOffset == 30.0);
        CHECK (saved->notes[1].corrections.pitch);
        CHECK (saved->sources[0].contentHash == rec.atlas.sources[0].contentHash);
        // No audio in the session: it is a few kilobytes of JSON.
        CHECK (state.getSize() < 200000);
        // Presets and the Library see the recording's hash.
        const auto hashes = OspAudioProcessor::soundHashesInState (*xml);
        CHECK (std::find (hashes.begin(), hashes.end(), rec.atlas.sources[0].contentHash) != hashes.end());
    }
    // Reopened with the file moved away: found by its hash in the sample store.
    REQUIRE (rec.file.moveFileTo (tmp.dir.getChildFile ("elsewhere.wav")));
    OspAudioProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    settle (q);
    REQUIRE (q.loadState (1) == OspAudioProcessor::LoadState::ready);
    REQUIRE (q.currentInstrument (1) != nullptr);
    CHECK (q.currentInstrument (1)->atlas != nullptr);
    CHECK (q.isGrainBudgetBounded());
    CHECK (audioHash (renderHeld (q, { 55, 60, 62, 67 }, 1.0)) == original);
}

TEST_CASE ("plugin: an atlas session whose audio is missing opens empty and failed, naming it", "[plugin][phase3][atlas]")
{
    TempDir tmp;
    juce::MemoryBlock state;
    {
        const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store-a").getFullPathName());
        const auto rec = phraseRecording (tmp.dir, "take.wav", { 60, 64 });
        OspAudioProcessor p;
        p.loadAtlas (rec.atlas);
        settle (p);
        REQUIRE (p.currentInstrument() != nullptr);
        p.getStateInformation (state);
    }
    // Another machine: its own (empty) store, and the file's path does not exist there.
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store-b").getFullPathName());
    const auto moved = withoutAtlasAudio (state, tmp.dir.getChildFile ("nowhere/take.wav").getFullPathName().toStdString());
    OspAudioProcessor q;
    // A sound was playing on the layer before: the session must not look restored with it.
    loadLayers (q, tmp.dir, 1);
    REQUIRE (q.currentInstrument() != nullptr);
    q.setStateInformation (moved.getData(), static_cast<int> (moved.getSize()));
    settle (q);
    CHECK (q.currentInstrument() == nullptr);
    CHECK (q.loadState() == OspAudioProcessor::LoadState::failed);
    REQUIRE (q.missingSources().size() == 1);
    CHECK (q.missingSources().front().find ("take.wav") != std::string::npos);
    CHECK (heldLevelDb (q, 60) < -100.0);
    // A damaged atlas in the session is the same: nothing restored, and said so.
    auto xml = juce::AudioProcessor::getXmlFromBinary (moved.getData(), static_cast<int> (moved.getSize()));
    xml->getChildByName ("Instrument")->getChildByName ("Atlas")->setAttribute ("json", "{ not json");
    juce::MemoryBlock damaged;
    juce::AudioProcessor::copyXmlToBinary (*xml, damaged);
    OspAudioProcessor r;
    loadLayers (r, tmp.dir, 1);
    r.setStateInformation (damaged.getData(), static_cast<int> (damaged.getSize()));
    settle (r);
    CHECK (r.currentInstrument() == nullptr);
    CHECK (r.loadState() == OspAudioProcessor::LoadState::failed);
}

TEST_CASE ("plugin: a live-capture asset is found by its hash alone (no file path)", "[plugin][phase3][atlas]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    auto rec = phraseRecording (tmp.dir, "capture.wav", { 50, 57 }, 1, atlas::AssetOrigin::liveCapture);
    // The capture subsystem (future) finalises a recording into the store; here an import does.
    {
        OspAudioProcessor p;
        loadAndWait (p, rec.file);
    }
    REQUIRE (rec.file.deleteFile());
    OspAudioProcessor q;
    q.loadAtlas (rec.atlas);
    settle (q);
    REQUIRE (q.currentInstrument() != nullptr);
    CHECK (q.currentInstrument()->atlas->sources[0].origin == atlas::AssetOrigin::liveCapture);
    CHECK (q.currentInstrument()->atlas->sources[0].lastKnownPath.empty());
    CHECK (heldLevelDb (q, 57) > -50.0);
}

TEST_CASE ("plugin: v13 sessions keep Phase 2's shared grain budget; v14 sessions are bounded", "[plugin][phase3][migration][budget]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    juce::MemoryBlock modern;
    {
        OspAudioProcessor p;
        CHECK (p.isGrainBudgetBounded());   // a new session
        loadLayers (p, tmp.dir, 3);
        heavyGranular (p);
        p.setParameterValue ("grainBudget", 7.0f);   // 64: the budget binds
        p.getStateInformation (modern);
    }
    auto xml = juce::AudioProcessor::getXmlFromBinary (modern.getData(), static_cast<int> (modern.getSize()));
    xml->setAttribute ("stateVersion", 13);
    xml->removeAttribute ("grainBudgetPolicy");
    juce::MemoryBlock v13;
    juce::AudioProcessor::copyXmlToBinary (*xml, v13);

    // 24 notes on three Granular layers: 72 grain streams, more than the 64-grain budget -
    // Phase 2's shared budget then plays at least one grain per stream (above the budget), the
    // bounded one never more than 64. (Each render on a fresh processor: LIFE's memory of
    // earlier notes would otherwise differ.)
    std::vector<int> chord24;
    for (int k = 0; k < 24; ++k)
        chord24.push_back (36 + 2 * k);
    OspAudioProcessor old;
    old.setStateInformation (v13.getData(), static_cast<int> (v13.getSize()));
    settle (old);
    CHECK_FALSE (old.isGrainBudgetBounded());
    CHECK (old.parameterValue ("grainBudget") == 7.0f);   // a v13 session's own budget is kept
    const auto shared = audioHash (renderHeld (old, chord24, 1.0));
    OspAudioProcessor now;
    now.setStateInformation (modern.getData(), static_cast<int> (modern.getSize()));
    settle (now);
    CHECK (now.isGrainBudgetBounded());
    CHECK (audioHash (renderHeld (now, chord24, 1.0)) != shared);
    // ...and the policy is the only difference.
    OspAudioProcessor asShared;
    asShared.setStateInformation (modern.getData(), static_cast<int> (modern.getSize()));
    settle (asShared);
    asShared.setGrainBudgetBounded (false);
    CHECK (audioHash (renderHeld (asShared, chord24, 1.0)) == shared);
    // Saved again, the old session stays shared.
    juce::MemoryBlock resaved;
    old.getStateInformation (resaved);
    auto again = juce::AudioProcessor::getXmlFromBinary (resaved.getData(), static_cast<int> (resaved.getSize()));
    CHECK (again->getStringAttribute ("grainBudgetPolicy") == "shared");
}

TEST_CASE ("plugin: atlases replace each other while notes sound, without allocating on the audio thread", "[plugin][phase3][atlas]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const std::array<AtlasRecording, 3> recs { phraseRecording (tmp.dir, "one.wav", { 57, 60, 64 }),
                                               phraseRecording (tmp.dir, "two.wav", { 48, 55, 62, 69 }, 1),
                                               phraseRecording (tmp.dir, "three.wav", { 60, 67 }) };
    OspAudioProcessor p;
    p.loadAtlas (recs[0].atlas, 0);
    p.loadAtlas (recs[2].atlas, 1);
    settle (p);
    p.setParameterValue ("layerB.sourceMode", 1.0f);
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);
    for (int note : { 55, 60, 64, 67 })
        midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)), 0);
    for (int b = 0; b < 8; ++b)
    {
        buffer.clear();
        p.processBlock (buffer, midi);
        midi.clear();
    }
    int allocations = 0;
    bool finite = true;
    for (int round = 0; round < 6; ++round)
    {
        // The message thread: a new atlas on a layer while its notes are held; the loader
        // builds it, the timer publishes it, the next block takes it.
        p.loadAtlas (recs[static_cast<std::size_t> ((round + 1) % 3)].atlas, round % 2);
        settle (p);
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 50 + round, static_cast<juce::uint8> (90)), 0);
        {
            test::AllocationScope scope;
            for (int b = 0; b < 16; ++b)
            {
                buffer.clear();
                p.processBlock (buffer, midi);
                midi.clear();
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        finite = finite && std::isfinite (buffer.getSample (ch, i));
            }
            allocations += scope.count();
        }
        p.pollLoads();   // retired instruments are freed here, on the message thread
    }
    CHECK (allocations == 0);
    CHECK (finite);
    CHECK (p.currentInstrument (0)->atlas != nullptr);
    CHECK (p.currentInstrument (1)->atlas != nullptr);
}

TEST_CASE ("plugin: a portable instrument carries an atlas's recordings", "[plugin][phase3][atlas]")
{
    TempDir tmp;
    const auto package = tmp.dir.getChildFile ("phrase.ospinstrument");
    {
        const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store-a").getFullPathName());
        const auto rec = phraseRecording (tmp.dir, "phrase.wav", { 57, 64 });
        OspAudioProcessor p;
        p.loadAtlas (rec.atlas);
        settle (p);
        juce::String error;
        REQUIRE (p.exportInstrument (package, error));
        REQUIRE (rec.file.deleteFile());
    }
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store-b").getFullPathName());
    OspAudioProcessor q;
    juce::String error;
    REQUIRE (q.importInstrument (package, error));
    settle (q);
    REQUIRE (q.currentInstrument() != nullptr);
    CHECK (q.currentInstrument()->atlas != nullptr);
    CHECK (heldLevelDb (q, 64) > -50.0);
}
