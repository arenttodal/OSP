// The unified engine's Phase 4 in ANDOR OSP (docs/research/unified-andor/phase-4-report.md): a
// recorded phrase analysed onto a layer - imported by hash, analysed on a background job (its
// stored analysis reused), published playable at once and finished in the background; a
// recording that does not suit stays one ordinary sample; the session saves the analysed atlas
// with its corrections, never the audio. Included after UnifiedPhase3Tests.inc (its helpers).
// The vowels' keys are ones the analysis reads correctly: OSP's test vowel has a fundamental
// about 30 dB under its formant, and on lower keys the analysis can lock onto a harmonic (a
// documented limitation, phase-4-report.md section 5; those notes carry a doubt).

#include "analysis/phrase/PhraseEdits.h"
#include "core/Prng.h"

#include <thread>

namespace
{
    /** Waits for every load stage and delivers them (Stage A, then Stage B). */
    void settleAnalysis (OspAudioProcessor& p)
    {
        REQUIRE (p.waitForLoads (60000));
        p.pollLoads();
        REQUIRE (p.waitForLoads (60000));
        p.pollLoads();
    }

    int phase4AtlasFiles (const juce::File& folder)
    {
        return folder.getNumberOfChildFiles (juce::File::findFiles, "*.andoratlas");
    }
}

TEST_CASE ("plugin: a recorded phrase is analysed onto a layer, each note on its own key, then finished", "[plugin][phase4][analysis]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const auto rec = phraseRecording (tmp.dir, "sung.wav", { 69, 74, 77, 81 });
    OspAudioProcessor p;
    p.analyseRecording (rec.file);
    settleAnalysis (p);
    REQUIRE (p.loadState() == OspAudioProcessor::LoadState::ready);
    const auto instrument = p.currentInstrument();
    REQUIRE (instrument != nullptr);
    REQUIRE (instrument->atlas != nullptr);
    REQUIRE (instrument->set != nullptr);
    const auto status = p.atlasStatus();
    REQUIRE (status != nullptr);
    CHECK (status->analysed);
    CHECK_FALSE (status->fromCache);
    CHECK (status->suitability == "monophonic");
    CHECK (status->notes == 4);
    CHECK_FALSE (status->playableOnly);   // Stage B arrived
    CHECK_FALSE (status->partial);
    CHECK (instrument->model->stage == InstrumentModel::Stage::complete);
    // The atlas is an ordinary schema-2 atlas of this recording.
    CHECK (instrument->atlas->schemaVersion == atlas::currentSchemaVersion);
    CHECK (instrument->atlas->analysis.version == phrase::analysisVersion);
    CHECK (instrument->atlas->sources.front().contentHash == rec.atlas.sources.front().contentHash);
    std::set<int> keys;
    for (const auto& n : instrument->atlas->notes)
        if (n.included)
            keys.insert (n.rootMidi);
    CHECK (keys == std::set<int> { 69, 74, 77, 81 });
    for (int key : { 69, 74, 77, 81 })
        CHECK (heldLevelDb (p, key) > -50.0);
    CHECK (p.loadState (0) != OspAudioProcessor::LoadState::failed);
    // The recording is in the store by hash, the analysis beside it.
    CHECK (phase4AtlasFiles (p.phraseAnalysisFolder()) == 1);
}

TEST_CASE ("plugin: the same recording is analysed once; another layer reuses it", "[plugin][phase4][analysis][cache]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const auto rec = phraseRecording (tmp.dir, "bowed.wav", { 69, 72, 76, 79 });
    OspAudioProcessor p;
    p.analyseRecording (rec.file, 0);
    settleAnalysis (p);
    const auto first = p.currentInstrument (0);
    REQUIRE (first != nullptr);
    REQUIRE (first->atlas != nullptr);
    // A copy under another name: the same audio, so the same analysis.
    const auto copy = tmp.dir.getChildFile ("bowed copy.wav");
    REQUIRE (rec.file.copyFileTo (copy));
    p.analyseRecording (copy, 1);
    settleAnalysis (p);
    const auto status = p.atlasStatus (1);
    REQUIRE (status != nullptr);
    CHECK (status->fromCache);
    const auto second = p.currentInstrument (1);
    REQUIRE (second->atlas != nullptr);
    REQUIRE (second->atlas->notes.size() == first->atlas->notes.size());
    for (std::size_t i = 0; i < first->atlas->notes.size(); ++i)
    {
        CHECK (second->atlas->notes[i].id == first->atlas->notes[i].id);
        CHECK (second->atlas->notes[i].attackStart == first->atlas->notes[i].attackStart);
    }
    CHECK (second->atlas->sources.front().fileName == "bowed copy.wav");
    CHECK (phase4AtlasFiles (p.phraseAnalysisFolder()) == 1);
    // Other settings are another analysis.
    phrase::AnalysisSettings fewer;
    fewer.detection = phrase::Detection::fewer;
    p.analyseRecording (rec.file, 2, fewer);
    settleAnalysis (p);
    REQUIRE (p.atlasStatus (2) != nullptr);
    CHECK_FALSE (p.atlasStatus (2)->fromCache);
    CHECK (phase4AtlasFiles (p.phraseAnalysisFolder()) == 2);
}

TEST_CASE ("plugin: a recording that does not suit the analysis stays one playable sample, and says why", "[plugin][phase4][analysis]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    // A chord: three vowels at once (polyphonic), 2 s.
    auto chord = AudioData::allocate (2, 96000, 48000.0);
    for (int key : { 48, 55, 64 })
    {
        const auto v = testsignals::vowel (midiToHz (key), 2.0, 48000.0, static_cast<std::uint64_t> (key), 0.3, 2);
        for (int c = 0; c < 2; ++c)
            for (std::size_t i = 0; i < chord.channels[static_cast<std::size_t> (c)].size(); ++i)
                chord.channels[static_cast<std::size_t> (c)][i] += v.channels[static_cast<std::size_t> (c)][i];
    }
    const auto file = writeSource (tmp.dir, "chord.wav", chord);
    OspAudioProcessor p;
    p.analyseRecording (file);
    settleAnalysis (p);
    const auto instrument = p.currentInstrument();
    REQUIRE (instrument != nullptr);
    CHECK (instrument->atlas == nullptr);
    CHECK (instrument->set == nullptr);
    CHECK (instrument->model != nullptr);
    CHECK (p.statusMessage().contains ("one sample"));
    CHECK (heldLevelDb (p, 60) > -50.0);   // the recording plays, as recorded

    // Noise: no pitched notes at all, the same outcome.
    auto noise = AudioData::allocate (1, 48000, 48000.0);
    Prng rng (7);
    for (auto& x : noise.channels[0])
        x = static_cast<float> (0.2 * (rng.nextDouble() * 2.0 - 1.0));
    p.analyseRecording (writeSource (tmp.dir, "noise.wav", noise));
    settleAnalysis (p);
    REQUIRE (p.currentInstrument() != nullptr);
    CHECK (p.currentInstrument()->atlas == nullptr);
    CHECK (p.statusMessage().contains ("one sample"));
}

TEST_CASE ("plugin: an analysed instrument saves its atlas and the musician's corrections, not the audio", "[plugin][phase4][analysis][state]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const auto rec = phraseRecording (tmp.dir, "phrase.wav", { 69, 74, 77, 81 });
    OspAudioProcessor p;
    p.analyseRecording (rec.file);
    settleAnalysis (p);
    REQUIRE (p.currentInstrument()->atlas != nullptr);
    // The musician corrects one note (moved to another key) and leaves one out.
    auto corrected = *p.currentInstrument()->atlas;
    REQUIRE (corrected.notes.size() >= 3);
    const auto movedId = corrected.notes[1].id, outId = corrected.notes[2].id;
    REQUIRE (phrase::edit::assignKey (corrected, movedId, 86));
    REQUIRE (phrase::edit::setIncluded (corrected, outId, false));
    p.loadAtlas (corrected);
    settleAnalysis (p);
    REQUIRE (p.currentInstrument()->atlas != nullptr);
    CHECK (heldLevelDb (p, 86) > -50.0);

    juce::MemoryBlock state;
    p.getStateInformation (state);
    CHECK (state.getSize() < 200000);   // the atlas, not the audio
    OspAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    settleAnalysis (restored);
    const auto back = restored.currentInstrument();
    REQUIRE (back != nullptr);
    REQUIRE (back->atlas != nullptr);
    const auto* moved = back->atlas->findNote (movedId);
    const auto* out = back->atlas->findNote (outId);
    REQUIRE (moved != nullptr);
    REQUIRE (out != nullptr);
    CHECK (moved->assignedKey == 86);
    CHECK (moved->corrections.key);
    CHECK_FALSE (out->included);
    CHECK (out->corrections.included);
    CHECK (back->atlas->analysis.version == phrase::analysisVersion);
    CHECK (back->atlas->analysis.suitability == corrected.analysis.suitability);
    for (std::size_t i = 0; i < corrected.notes.size(); ++i)
    {
        CHECK (back->atlas->notes[i].id == corrected.notes[i].id);
        CHECK (back->atlas->notes[i].diagnostics == corrected.notes[i].diagnostics);
    }
    CHECK (heldLevelDb (restored, 86) > -50.0);
}

TEST_CASE ("plugin: Stage B arrives while a note sounds; the audio thread never allocates", "[plugin][phase4][analysis][realtime]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const auto rec = phraseRecording (tmp.dir, "held.wav", { 69, 74, 77 });
    OspAudioProcessor p;
    p.prepareToPlay (48000.0, 256);
    p.analyseRecording (rec.file);
    // Deliver as soon as Stage A (or, on a fast machine, already B) is there.
    for (int i = 0; i < 6000 && p.currentInstrument() == nullptr; ++i)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
        p.pollLoads();
    }
    REQUIRE (p.currentInstrument() != nullptr);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);
    midi.addEvent (juce::MidiMessage::noteOn (1, 74, static_cast<juce::uint8> (100)), 0);
    int allocations = 0;
    bool finite = true;
    double quietest = 0.0;
    for (int b = 0; b < 160; ++b)
    {
        if (b % 8 == 0)
            p.pollLoads();   // the timer, on the message thread: Stage B is published here
        {
            test::AllocationScope scope;
            buffer.clear();
            p.processBlock (buffer, midi);
            midi.clear();
            allocations += scope.count();
        }
        double sum = 0.0;
        for (int i = 0; i < 256; ++i)
        {
            finite = finite && std::isfinite (buffer.getSample (0, i));
            sum += static_cast<double> (buffer.getSample (0, i)) * buffer.getSample (0, i);
        }
        if (b > 4 && b < 100)   // the note's body (the region is 0.8 s)
            quietest = std::min (quietest == 0.0 ? 1.0 : quietest, sum / 256.0);
    }
    settleAnalysis (p);
    CHECK (allocations == 0);
    CHECK (finite);
    CHECK (10.0 * std::log10 (quietest + 1.0e-30) > -60.0);   // the held note never dropped out
    CHECK (p.currentInstrument()->model->stage == InstrumentModel::Stage::complete);
}

TEST_CASE ("plugin: a cancelled analysis leaves the layer as it was; capacity counts the other layers", "[plugin][phase4][analysis]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const auto rec = phraseRecording (tmp.dir, "long.wav", { 48, 50, 52, 53, 55, 57, 59, 60, 62, 64, 65, 67 });
    OspAudioProcessor p;
    CHECK (p.atlasCapacityFor (0) == OspAudioProcessor::atlasLayerSeconds);
    p.analyseRecording (rec.file, 0);
    p.cancelAnalysis (0);
    REQUIRE (p.waitForLoads (60000));
    p.pollLoads();
    // Cancelled before or after Stage A: either nothing, or a playable instrument - never a failure.
    CHECK (p.loadState (0) != OspAudioProcessor::LoadState::failed);

    p.analyseRecording (rec.file, 0);
    settleAnalysis (p);
    const auto status = p.atlasStatus (0);
    REQUIRE (status != nullptr);
    CHECK (status->regionSeconds > 5.0);
    CHECK (status->regionSeconds < 15.0);
    // What another layer may still hold: the session's policy minus this layer's atlas.
    CHECK (p.atlasCapacityFor (1) == Approx (std::min (OspAudioProcessor::atlasLayerSeconds,
                                                       OspAudioProcessor::atlasSessionSeconds - status->regionSeconds)));
}
