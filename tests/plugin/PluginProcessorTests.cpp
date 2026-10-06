// Headless tests for the plugin processor: the same object a DAW would host.
// Covers: import -> analysis -> chromatic playback, sample-rate independence,
// root override, session state recall (including from the managed sample store after
// the original file disappears), and bad files.

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "analysis/Analyzer.h"
#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "engine/InstrumentEngine.h"
#include "io/AudioFileIO.h"

#define CATCH_CONFIG_RUNNER
#include <catch2/catch_approx.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

using Catch::Approx;
using namespace osp;
using osp::plugin::OspAudioProcessor;

namespace
{
    struct TempDir
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("osp-plugin-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        TempDir() { dir.createDirectory(); }
        ~TempDir() { dir.deleteRecursively(); }
    };

    juce::File writeSource (const juce::File& dir, const juce::String& name, const AudioData& audio)
    {
        const auto file = dir.getChildFile (name);
        std::string error;
        REQUIRE (io::writeAudioFile (std::filesystem::path (file.getFullPathName().toStdString()), audio, io::SampleFormat::pcm24, error));
        return file;
    }

    void loadAndWait (OspAudioProcessor& p, const juce::File& file)
    {
        p.loadFile (file);
        REQUIRE (p.waitForLoads (20000));
        p.pollLoads();
    }

    /** Plays one note through processBlock and returns the rendered audio. */
    AudioData playNote (OspAudioProcessor& p, int note, double sampleRate, double seconds, int blockSize = 256)
    {
        p.prepareToPlay (sampleRate, blockSize);
        const auto total = static_cast<int> (seconds * sampleRate);
        AudioData out = AudioData::allocate (2, total, sampleRate);
        juce::AudioBuffer<float> buffer (2, blockSize);
        for (int pos = 0; pos < total; pos += blockSize)
        {
            juce::MidiBuffer midi;
            if (pos == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)), 0);
            buffer.clear();
            p.processBlock (buffer, midi);
            const int n = std::min (blockSize, total - pos);
            for (int ch = 0; ch < 2; ++ch)
                std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + n, out.channels[static_cast<std::size_t> (ch)].begin() + pos);
        }
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        p.processBlock (buffer, off);
        return out;
    }

    double pitchOf (const AudioData& audio) { return Analyzer::analyse (audio).pitch.fundamentalHz; }
}

TEST_CASE ("plugin: a dropped sample becomes a chromatic instrument", "[plugin]")
{
    TempDir tmp;
    const auto file = writeSource (tmp.dir, "vowel.wav", testsignals::vowel (midiToHz (57), 3.0, 44100.0, 3));

    OspAudioProcessor p;
    CHECK (p.loadState() == OspAudioProcessor::LoadState::empty);
    loadAndWait (p, file);
    REQUIRE (p.loadState() == OspAudioProcessor::LoadState::ready);

    const auto instrument = p.currentInstrument();
    REQUIRE (instrument != nullptr);
    CHECK (instrument->analysis.pitch.noteName == "A3");
    CHECK (instrument->rootOrigin == "analysis");
    CHECK_FALSE (instrument->character.empty());
    CHECK (juce::File (instrument->storedPath).existsAsFile());

    for (int offset : { -12, 0, 7, 12 })
    {
        const auto out = playNote (p, 57 + offset, 48000.0, 1.0);
        CHECK (pitchOf (out) == Approx (midiToHz (57 + offset)).epsilon (0.006));
    }

    // Host sample-rate changes must not change pitch.
    for (double rate : { 44100.0, 88200.0, 96000.0 })
        CHECK (pitchOf (playNote (p, 69, rate, 0.8)) == Approx (440.0).epsilon (0.006));
}

TEST_CASE ("plugin: manual root correction", "[plugin]")
{
    TempDir tmp;
    const auto file = writeSource (tmp.dir, "saw.wav", testsignals::saw (midiToHz (50), 2.0, 48000.0));
    OspAudioProcessor p;
    loadAndWait (p, file);

    // Tell the instrument the sample is a G3 (55) instead of D3 (50): playing G3 now gives the raw sample pitch.
    p.setRootOverride (55.0);
    CHECK (p.effectiveRootMidi() == Approx (55.0));
    CHECK (pitchOf (playNote (p, 55, 48000.0, 0.8)) == Approx (midiToHz (50)).epsilon (0.006));

    p.setRootOverride (std::nullopt);
    CHECK (pitchOf (playNote (p, 55, 48000.0, 0.8)) == Approx (midiToHz (55)).epsilon (0.006));
}

TEST_CASE ("plugin: session state recalls sample, root and parameters", "[plugin]")
{
    TempDir tmp;
    auto file = writeSource (tmp.dir, "pluck.wav", testsignals::pluck (midiToHz (45), 2.0, 48000.0, 5));

    juce::MemoryBlock state;
    AudioData before;
    {
        OspAudioProcessor p;
        loadAndWait (p, file);
        p.setRootOverride (47.0);
        p.parameters.getParameter ("release")->setValueNotifyingHost (0.7f);
        before = playNote (p, 60, 48000.0, 0.5);
        p.getStateInformation (state);
    }

    // The original file disappears: recall must come from the managed sample store.
    REQUIRE (file.deleteFile());

    OspAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (restored.waitForLoads (20000));
    restored.pollLoads();
    REQUIRE (restored.loadState() == OspAudioProcessor::LoadState::ready);
    REQUIRE (restored.currentInstrument() != nullptr);
    CHECK (restored.currentInstrument()->filename == "pluck.wav");
    CHECK (restored.rootOverride().value_or (-1.0) == Approx (47.0));
    CHECK (restored.parameters.getParameter ("release")->getValue() == Approx (0.7f));

    // Identical recall => identical audio (deterministic playback).
    const auto after = playNote (restored, 60, 48000.0, 0.5);
    REQUIRE (after.numFrames() == before.numFrames());
    double diff = 0.0;
    for (std::size_t ch = 0; ch < 2; ++ch)
        for (std::size_t i = 0; i < after.channels[ch].size(); ++i)
            diff = std::max (diff, static_cast<double> (std::abs (after.channels[ch][i] - before.channels[ch][i])));
    CHECK (diff < 1.0e-12); // bit-identical
}

TEST_CASE ("plugin: bad files fail cleanly and keep the previous instrument", "[plugin]")
{
    TempDir tmp;
    const auto good = writeSource (tmp.dir, "good.wav", testsignals::sine (220.0, 1.0, 48000.0));
    const auto bad = tmp.dir.getChildFile ("bad.wav");
    bad.replaceWithText ("this is not audio");

    OspAudioProcessor p;
    loadAndWait (p, bad);
    CHECK (p.loadState() == OspAudioProcessor::LoadState::failed);
    CHECK (p.statusMessage().isNotEmpty());

    loadAndWait (p, good);
    CHECK (p.loadState() == OspAudioProcessor::LoadState::ready);
    const auto generation = p.currentInstrument()->generation;

    loadAndWait (p, bad);
    CHECK (p.loadState() == OspAudioProcessor::LoadState::ready); // still playable
    CHECK (p.currentInstrument()->generation == generation);
    CHECK (pitchOf (playNote (p, 57, 48000.0, 0.5)) == Approx (220.0).epsilon (0.006));
}

TEST_CASE ("plugin: a sample with a slow start speaks immediately", "[plugin]")
{
    // 0.6 s of silence before a quiet tone (like the organ's bellows pre-roll).
    TempDir tmp;
    auto audio = testsignals::sine (220.0, 2.0, 48000.0, 0.01, 2);
    for (auto& ch : audio.channels)
        std::fill (ch.begin(), ch.begin() + 28800, 0.0f);
    const auto file = writeSource (tmp.dir, "late.wav", audio);

    OspAudioProcessor p;
    loadAndWait (p, file);
    REQUIRE (p.currentInstrument() != nullptr);
    CHECK (p.currentInstrument()->startSeconds == Approx (0.57).margin (0.02));
    CHECK (p.currentInstrument()->playbackGainDb > 20.0);

    const auto out = playNote (p, 57, 48000.0, 0.2);
    float early = 0.0f;
    for (std::size_t i = 2400; i < 4800; ++i) // 50-100 ms after note-on
        early = std::max (early, std::abs (out.channels[0][i]));
    CHECK (early > 0.01f);
}

TEST_CASE ("plugin: an unpitched sample is still playable", "[plugin]")
{
    TempDir tmp;
    const auto noise = writeSource (tmp.dir, "noise.wav", testsignals::whiteNoise (1.0, 48000.0, 0.3, 2, 2));
    OspAudioProcessor p;
    loadAndWait (p, noise);
    REQUIRE (p.loadState() == OspAudioProcessor::LoadState::ready);
    CHECK_FALSE (p.currentInstrument()->analysis.pitch.detected);
    CHECK (p.currentInstrument()->rootOrigin == "fallback");
    const auto out = playNote (p, 60, 48000.0, 0.3);
    double peak = 0.0;
    for (float s : out.channels[0])
        peak = std::max (peak, static_cast<double> (std::abs (s)));
    CHECK (peak > 0.01);
}

TEST_CASE ("plugin: a dropped set becomes one multi-sample instrument and recalls identically", "[plugin]")
{
    TempDir tmp;
    juce::Array<juce::File> files;
    files.add (writeSource (tmp.dir, "low A2.wav", testsignals::vowel (midiToHz (45), 1.5, 48000.0, 1)));
    files.add (writeSource (tmp.dir, "mid take1.wav", testsignals::vowel (midiToHz (57), 1.5, 48000.0, 2)));
    files.add (writeSource (tmp.dir, "mid take2.wav", testsignals::vowel (midiToHz (57), 1.5, 48000.0, 3)));
    // (a saw: the synthetic vowel at A4 is pitch-tracked on its 2nd harmonic, see STATUS)
    files.add (writeSource (tmp.dir, "high A4.wav", testsignals::saw (midiToHz (69), 1.5, 48000.0, 0.3, 2)));

    juce::MemoryBlock state;
    AudioData before;
    {
        OspAudioProcessor p;
        p.loadFiles (files);
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        const auto instrument = p.currentInstrument();
        REQUIRE (instrument != nullptr);
        REQUIRE (instrument->set != nullptr);
        CHECK (instrument->set->groups.size() == 3);
        CHECK (instrument->memberFiles.size() == 4);
        // Each register plays from its own recording, in tune.
        CHECK (pitchOf (playNote (p, 47, 48000.0, 0.8)) == Approx (midiToHz (47)).epsilon (0.006));
        CHECK (pitchOf (playNote (p, 70, 48000.0, 0.8)) == Approx (midiToHz (70)).epsilon (0.006));

        // A correction from the Samples inspector rebuilds the set.
        p.reassignSample ("mid take2.wav", SampleRole::velocityLayer, 1);
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        const auto reassigned = p.currentInstrument();
        REQUIRE (reassigned->set != nullptr);
        bool found = false;
        for (const auto& m : reassigned->set->members)
            if (m.filename == "mid take2.wav")
            {
                found = true;
                CHECK (m.userAssigned);
                CHECK (m.layer == 1);
            }
        CHECK (found);

        // A root correction retunes that file: call the low A2 file "A1" and A2 now plays an octave up.
        p.reassignSample ("low A2.wav", SampleRole::pitchAnchor, 0, 33.0);
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        CHECK (pitchOf (playNote (p, 33, 48000.0, 0.8)) == Approx (midiToHz (45)).epsilon (0.006));
        p.reassignSample ("low A2.wav", SampleRole::pitchAnchor, 0, 45.0);
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        before = playNote (p, 60, 48000.0, 0.6);
        p.getStateInformation (state);
    }

    OspAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (restored.waitForLoads (30000));
    restored.pollLoads();
    REQUIRE (restored.currentInstrument() != nullptr);
    REQUIRE (restored.currentInstrument()->set != nullptr);
    CHECK (restored.currentInstrument()->assignments.size() == 2);
    const auto after = playNote (restored, 60, 48000.0, 0.6);
    REQUIRE (after.numFrames() == before.numFrames());
    double diff = 0.0;
    for (std::size_t ch = 0; ch < 2; ++ch)
        for (std::size_t i = 0; i < after.channels[ch].size(); ++i)
            diff = std::max (diff, static_cast<double> (std::abs (after.channels[ch][i] - before.channels[ch][i])));
    CHECK (diff < 1.0e-12);
}

TEST_CASE ("plugin: starting states set the macros and keep the sound", "[plugin]")
{
    OspAudioProcessor p;
    REQUIRE (p.getNumPrograms() == 7);
    CHECK (p.getProgramName (0) == "Natural");
    p.setCurrentProgram (5); // Dream
    CHECK (p.getCurrentProgram() == 5);
    CHECK (p.parameters.getParameter ("space")->convertFrom0to1 (p.parameters.getParameter ("space")->getValue()) == Approx (60.0f));
    CHECK (p.parameters.getParameter ("release")->convertFrom0to1 (p.parameters.getParameter ("release")->getValue()) == Approx (4000.0f).margin (0.5));
}

TEST_CASE ("plugin: shaping settings are parameters that persist and shape the sound", "[plugin]")
{
    TempDir tmp;
    const auto file = writeSource (tmp.dir, "bright.wav", testsignals::saw (midiToHz (48), 2.0, 48000.0));
    auto setValue = [] (OspAudioProcessor& p, const juce::String& id, float value) {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    auto valueOf = [] (OspAudioProcessor& p, const juce::String& id) {
        auto* param = p.parameters.getParameter (id);
        return param->convertFrom0to1 (param->getValue());
    };
    auto highShare = [] (const AudioData& a) {
        // Energy in the first difference relative to the signal: a brightness proxy.
        double e = 0.0, d = 0.0;
        for (std::size_t i = 1; i < a.channels[0].size(); ++i)
        {
            e += a.channels[0][i] * a.channels[0][i];
            const double dx = a.channels[0][i] - a.channels[0][i - 1];
            d += dx * dx;
        }
        return d / std::max (1.0e-12, e);
    };

    // Every stable ID exists.
    OspAudioProcessor p;
    for (const auto& id : OspAudioProcessor::shapingIds())
        CHECK (p.parameters.getParameter (id) != nullptr);

    loadAndWait (p, file);
    const auto open = playNote (p, 48, 48000.0, 0.6);
    setValue (p, "character", 20.0f);   // the filter closes
    const auto closed = playNote (p, 48, 48000.0, 0.6);
    CHECK (highShare (closed) < 0.5 * highShare (open));

    setValue (p, "character.type", 2.0f);   // HP12
    setValue (p, "space.type", 3.0f);       // Spring
    setValue (p, "space.decay", 3.3f);
    setValue (p, "movement.mode", 1.0f);    // Tape
    setValue (p, "life.mode", 2.0f);        // Fray
    // LIFE's round robins and character (version hint 10): endless and AUTO by default.
    CHECK (valueOf (p, "life.takes") == Approx (0.0f));
    CHECK (valueOf (p, "life.character") == Approx (0.0f));
    setValue (p, "life.takes", 3.0f);       // 4 takes
    setValue (p, "life.takeOrder", 1.0f);   // Random
    setValue (p, "life.character", 1.0f);   // Pluck
    setValue (p, "life.takesSeed", 7.0f);
    {
        const auto played = playNote (p, 48, 48000.0, 0.3);
        float peak = 0.0f;
        for (float x : played.channels[0])
            peak = std::max (peak, std::abs (x));
        CHECK (peak > 0.001f);
    }
    juce::MemoryBlock state;
    p.getStateInformation (state);

    OspAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (restored.waitForLoads (20000));
    restored.pollLoads();
    CHECK (valueOf (restored, "character") == Approx (20.0f).margin (0.05));
    CHECK (valueOf (restored, "character.type") == Approx (2.0f));
    CHECK (valueOf (restored, "space.type") == Approx (3.0f));
    CHECK (valueOf (restored, "space.decay") == Approx (3.3f).margin (0.01));
    CHECK (valueOf (restored, "movement.mode") == Approx (1.0f));
    CHECK (valueOf (restored, "life.mode") == Approx (2.0f));
    CHECK (valueOf (restored, "life.takes") == Approx (3.0f));
    CHECK (valueOf (restored, "life.takeOrder") == Approx (1.0f));
    CHECK (valueOf (restored, "life.character") == Approx (1.0f));
    CHECK (valueOf (restored, "life.takesSeed") == Approx (7.0f));
}

namespace
{
    /** A host transport for tests: playing from a PPQ position at a tempo. */
    struct TestPlayHead final : juce::AudioPlayHead
    {
        double ppq = 0.0, bpm = 120.0;
        bool playing = true;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (playing);
            info.setPpqPosition (ppq);
            info.setBpm (bpm);
            info.setTimeSignature (TimeSignature { 4, 4 });
            return info;
        }
    };
}

TEST_CASE ("plugin: MOVEMENT modes keep their own settings, recall and migrate", "[plugin][movement]")
{
    auto setValue = [] (OspAudioProcessor& p, const juce::String& id, float value) {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    auto valueOf = [] (OspAudioProcessor& p, const juce::String& id) {
        auto* param = p.parameters.getParameter (id);
        return param->convertFrom0to1 (param->getValue());
    };
    OspAudioProcessor p;
    for (const auto* id : { "movement.drift.speed", "movement.drift.pitch", "movement.drift.tone", "movement.tape.wow", "movement.tape.flutter",
                            "movement.tape.wear", "movement.chorus.rate", "movement.chorus.width", "movement.chorus.stereo", "movement.pulse.rate",
                            "movement.pulse.shape", "movement.pulse.stereo", "movement.shaper.pattern", "movement.shaper.rate",
                            "movement.shaper.target", "movement.shaper.smooth" })
        CHECK (p.parameters.getParameter (id) != nullptr);
    CHECK (p.parameters.getParameter ("movement.paramA") == nullptr);

    // TAPE set up, then SHAPER, then back: TAPE's values are still there.
    setValue (p, "movement.mode", 1.0f);
    setValue (p, "movement.tape.wow", 20.0f);
    setValue (p, "movement.tape.flutter", 13.0f);
    setValue (p, "movement.tape.wear", 32.0f);
    setValue (p, "movement.mode", 4.0f);
    setValue (p, "movement.shaper.pattern", 2.0f);   // BREATH
    setValue (p, "movement.shaper.rate", 1.0f);      // 1/8
    setValue (p, "movement.shaper.target", 1.0f);    // FILTER
    setValue (p, "movement.shaper.smooth", 64.0f);
    setValue (p, "motion", 55.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);

    OspAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (valueOf (restored, "movement.mode") == Approx (4.0f));
    CHECK (valueOf (restored, "motion") == Approx (55.0f).margin (0.05));
    CHECK (valueOf (restored, "movement.tape.wow") == Approx (20.0f).margin (0.05));
    CHECK (valueOf (restored, "movement.tape.flutter") == Approx (13.0f).margin (0.05));
    CHECK (valueOf (restored, "movement.tape.wear") == Approx (32.0f).margin (0.05));
    CHECK (valueOf (restored, "movement.shaper.pattern") == Approx (2.0f));
    CHECK (valueOf (restored, "movement.shaper.rate") == Approx (1.0f));
    CHECK (valueOf (restored, "movement.shaper.target") == Approx (1.0f));
    CHECK (valueOf (restored, "movement.shaper.smooth") == Approx (64.0f).margin (0.05));

    // A session from before MOVEMENT v2: the shared knobs belonged to the selected mode (CHORUS).
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    auto tree = juce::ValueTree::fromXml (*xml);
    tree.setProperty ("stateVersion", 4, nullptr);
    for (auto child : tree)
        if (child["id"].toString() == "movement.mode")
            child.setProperty ("value", 2.0f, nullptr);
    for (auto [id, value] : { std::pair { "movement.paramA", 81.0f }, { "movement.paramB", 22.0f }, { "movement.paramC", 47.0f } })
    {
        juce::ValueTree old ("PARAM");
        old.setProperty ("id", id, nullptr);
        old.setProperty ("value", value, nullptr);
        tree.appendChild (old, nullptr);
    }
    juce::MemoryBlock oldState;
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), oldState);
    OspAudioProcessor migrated;
    migrated.setStateInformation (oldState.getData(), static_cast<int> (oldState.getSize()));
    CHECK (valueOf (migrated, "movement.mode") == Approx (2.0f));
    CHECK (valueOf (migrated, "movement.chorus.rate") == Approx (81.0f).margin (0.05));
    CHECK (valueOf (migrated, "movement.chorus.width") == Approx (22.0f).margin (0.05));
    CHECK (valueOf (migrated, "movement.chorus.stereo") == Approx (47.0f).margin (0.05));
    auto* tapeWow = migrated.parameters.getParameter ("movement.tape.wow");
    CHECK (tapeWow->getValue() == Approx (tapeWow->getDefaultValue()));   // other modes: defaults
}

TEST_CASE ("plugin: SHAPER follows the host's transport", "[plugin][movement]")
{
    TempDir tmp;
    const auto file = writeSource (tmp.dir, "pad.wav", testsignals::vowel (midiToHz (57), 4.0, 48000.0, 2));
    OspAudioProcessor p;
    loadAndWait (p, file);
    p.parameters.getParameter ("movement.mode")->setValueNotifyingHost (1.0f);   // SHAPER (last choice)
    p.parameters.getParameter ("motion")->setValueNotifyingHost (0.6f);
    TestPlayHead head;
    head.ppq = 37.5;
    p.setPlayHead (&head);
    p.prepareToPlay (48000.0, 480);
    juce::AudioBuffer<float> buffer (2, 480);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 57, static_cast<juce::uint8> (100)), 0);
    p.processBlock (buffer, midi);
    // 1/16: a cycle is 4 quarters; the block's last sample is at 37.5 + 479 / 24000 quarters.
    const double ppqAtEnd = 37.5 + 479.0 * 120.0 / (60.0 * 48000.0);
    CHECK (p.shaperPhase() == Approx (ppqAtEnd / 4.0 - std::floor (ppqAtEnd / 4.0)).margin (1.0e-5));
    // The host jumps (a loop back to bar 1): the pattern follows at the next block.
    head.ppq = 0.0;
    juce::MidiBuffer none;
    p.processBlock (buffer, none);
    CHECK (p.shaperPhase() == Approx (479.0 * 120.0 / (60.0 * 48000.0) / 4.0).margin (1.0e-5));
    p.setPlayHead (nullptr);
}

TEST_CASE ("plugin: sessions from before the shaping system open CHARACTER fully", "[plugin]")
{
    OspAudioProcessor p;
    juce::MemoryBlock state;
    p.getStateInformation (state);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (xml != nullptr);
    auto tree = juce::ValueTree::fromXml (*xml);
    tree.setProperty ("stateVersion", 2, nullptr);
    for (auto child : tree)
        if (child["id"].toString() == "character")
            child.setProperty ("value", 50.0f, nullptr);
        else if (child["id"].toString() == "character.drive")
            child.setProperty ("value", 77.0f, nullptr);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);

    OspAudioProcessor restored;
    restored.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    auto valueOf = [&restored] (const juce::String& id) {
        auto* param = restored.parameters.getParameter (id);
        return param->convertFrom0to1 (param->getValue());
    };
    CHECK (valueOf ("character") == Approx (100.0f));
    CHECK (valueOf ("character.drive") == Approx (12.0f).margin (0.01)); // settings come back at their defaults
}

TEST_CASE ("plugin: A/B layers load, blend and recall independently", "[plugin][layers]")
{
    TempDir tmp;
    const auto fileA = writeSource (tmp.dir, "low.wav", testsignals::vowel (midiToHz (45), 2.0, 48000.0, 3));
    const auto fileB = writeSource (tmp.dir, "high.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 4));
    auto setValue = [] (OspAudioProcessor& p, const juce::String& id, float value) {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    juce::MemoryBlock state;
    AudioData before;
    {
        OspAudioProcessor p;
        // Edit focus decides where a sample goes.
        CHECK (p.editLayer() == 0);
        p.loadFile (fileA);
        REQUIRE (p.waitForLoads (20000));
        p.pollLoads();
        p.setEditLayer (1);
        p.loadFile (fileB);
        REQUIRE (p.waitForLoads (20000));
        p.pollLoads();
        REQUIRE (p.currentInstrument (0) != nullptr);
        REQUIRE (p.currentInstrument (1) != nullptr);
        CHECK (p.currentInstrument (0)->filename == "low.wav");
        CHECK (p.currentInstrument (1)->filename == "high.wav");
        CHECK (p.currentInstrument()->filename == "high.wav"); // the edited layer
        // Each layer keeps its own root: A sounds where it was recorded, so does B.
        setValue (p, "ab.blend", 0.0f);
        CHECK (pitchOf (playNote (p, 52, 48000.0, 0.8)) == Approx (midiToHz (52)).epsilon (0.006));
        setValue (p, "ab.blend", 1.0f);
        CHECK (pitchOf (playNote (p, 52, 48000.0, 0.8)) == Approx (midiToHz (52)).epsilon (0.006));
        p.setRootOverride (50.0, 1);   // B is declared a D3: its A3 recording now sounds a fifth higher
        CHECK (pitchOf (playNote (p, 57, 48000.0, 0.8)) == Approx (midiToHz (64)).epsilon (0.006));
        p.setRootOverride (std::nullopt, 1);

        setValue (p, "ab.blend", 0.35f);
        setValue (p, "layerB.sourceMode", 1.0f);
        setValue (p, "layerB.granular.size", 220.0f);
        setValue (p, "layerB.granular.tune", 7.0f);
        setValue (p, "layerA.granular.spread", 55.0f);
        before = playNote (p, 60, 48000.0, 0.6);
        p.getStateInformation (state);
    }

    OspAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (restored.waitForLoads (20000));
    restored.pollLoads();
    REQUIRE (restored.currentInstrument (0) != nullptr);
    REQUIRE (restored.currentInstrument (1) != nullptr);
    CHECK (restored.currentInstrument (0)->filename == "low.wav");
    CHECK (restored.currentInstrument (1)->filename == "high.wav");
    CHECK (restored.editLayer() == 1);
    auto valueOf = [&restored] (const juce::String& id) {
        auto* param = restored.parameters.getParameter (id);
        return param->convertFrom0to1 (param->getValue());
    };
    CHECK (valueOf ("ab.blend") == Approx (0.35f).margin (1.0e-4));
    CHECK (valueOf ("layerB.sourceMode") == Approx (1.0f));
    CHECK (valueOf ("layerA.sourceMode") == Approx (0.0f));
    CHECK (valueOf ("layerB.granular.size") == Approx (220.0f).margin (0.01));
    CHECK (valueOf ("layerB.granular.tune") == Approx (7.0f).margin (0.01));
    CHECK (valueOf ("layerA.granular.spread") == Approx (55.0f).margin (0.01));
    // Same instrument, same performance: the same audio.
    const auto after = playNote (restored, 60, 48000.0, 0.6);
    REQUIRE (after.numFrames() == before.numFrames());
    double diff = 0.0;
    for (std::size_t ch = 0; ch < 2; ++ch)
        for (std::size_t i = 0; i < after.channels[ch].size(); ++i)
            diff = std::max (diff, static_cast<double> (std::abs (after.channels[ch][i] - before.channels[ch][i])));
    CHECK (diff < 1.0e-12);

    // Clearing a layer: the one that is left plays alone, wherever the blend is (one
    // sound is a whole instrument, never half of an A/B mix).
    restored.clearLayer (1);
    restored.pollLoads();
    CHECK (restored.currentInstrument (1) == nullptr);
    CHECK (restored.occupiedLayerCount() == 1);
    restored.parameters.getParameter ("ab.blend")->setValueNotifyingHost (1.0f);
    const auto alone = playNote (restored, 60, 48000.0, 0.3);
    restored.parameters.getParameter ("ab.blend")->setValueNotifyingHost (0.0f);
    const auto aloneAtA = playNote (restored, 60, 48000.0, 0.3);
    double peak = 0.0, difference = 0.0;
    for (std::size_t i = 0; i < alone.channels[0].size(); ++i)
    {
        peak = std::max (peak, static_cast<double> (std::abs (alone.channels[0][i])));
        difference = std::max (difference, static_cast<double> (std::abs (alone.channels[0][i] - aloneAtA.channels[0][i])));
    }
    CHECK (peak > 0.01);
    CHECK (difference < 1.0e-6);
}

TEST_CASE ("plugin: clearing a layer while it sounds lets the note finish safely", "[plugin][layers]")
{
    TempDir tmp;
    const auto file = writeSource (tmp.dir, "held.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 6));
    OspAudioProcessor p;
    loadAndWait (p, file);
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, 57, static_cast<juce::uint8> (100)), 0);
    p.processBlock (buffer, on);
    p.clearLayer (0);   // published while the note plays: the old instrument stays alive for it
    p.pollLoads();
    double peak = 0.0;
    for (int block = 0; block < 400; ++block)
    {
        juce::MidiBuffer midi;
        if (block == 10)
            midi.addEvent (juce::MidiMessage::noteOff (1, 57), 0);
        buffer.clear();
        p.processBlock (buffer, midi);
        p.pollLoads();   // garbage collection runs here too
        if (block > 300)
            peak = std::max (peak, static_cast<double> (buffer.getMagnitude (0, 256)));
    }
    CHECK (std::isfinite (peak));
    CHECK (peak < 1.0e-4);              // the released note has ended (only the reverb tail, below -80 dB)
    CHECK (p.currentInstrument (0) == nullptr);
    CHECK (p.activeVoices.load() == 0);
}

TEST_CASE ("plugin: a layer in Granular mode sustains past its recording", "[plugin][layers]")
{
    TempDir tmp;
    // Half a second, decaying: One Shot (without continuation) is over long before 2 s.
    auto audio = testsignals::pluck (midiToHz (57), 0.5, 48000.0, 9);
    const auto file = writeSource (tmp.dir, "short.wav", audio);
    OspAudioProcessor p;
    loadAndWait (p, file);
    p.parameters.getParameter ("sustain")->setValueNotifyingHost (0.0f);   // Recording: no continuation
    auto tailRms = [&p] {
        const auto out = playNote (p, 57, 48000.0, 2.5);
        double e = 0.0;
        for (std::size_t i = 96000; i < 120000; ++i)
            e += static_cast<double> (out.channels[0][i]) * out.channels[0][i];
        return std::sqrt (e / 24000.0);
    };
    const double oneShot = tailRms();
    p.parameters.getParameter ("layerA.sourceMode")->setValueNotifyingHost (1.0f);
    p.parameters.getParameter ("layerA.granular.position")->setValueNotifyingHost (0.1f);   // near the start, where it is loud
    const double granular = tailRms();
    INFO ("rms at 2-2.5 s: one shot " << oneShot << ", granular " << granular);
    CHECK (oneShot < 1.0e-4);
    CHECK (granular > 100.0 * std::max (oneShot, 1.0e-6));
}

TEST_CASE ("plugin: sessions from before the layers recall into layer A", "[plugin][layers]")
{
    TempDir tmp;
    const auto file = writeSource (tmp.dir, "old.wav", testsignals::vowel (midiToHz (50), 2.0, 48000.0, 5));
    OspAudioProcessor p;
    loadAndWait (p, file);
    p.parameters.getParameter ("layerA.sourceMode")->setValueNotifyingHost (1.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (xml != nullptr);
    auto tree = juce::ValueTree::fromXml (*xml);
    tree.setProperty ("stateVersion", 3, nullptr);
    tree.removeChild (tree.getChildWithName ("InstrumentB"), nullptr);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);

    OspAudioProcessor restored;
    restored.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    REQUIRE (restored.waitForLoads (20000));
    restored.pollLoads();
    REQUIRE (restored.currentInstrument (0) != nullptr);
    CHECK (restored.currentInstrument (0)->filename == "old.wav");
    CHECK (restored.currentInstrument (1) == nullptr);
    auto* mode = restored.parameters.getParameter ("layerA.sourceMode");
    CHECK (mode->getValue() == Approx (mode->getDefaultValue()));   // One Shot, as it was made
}

TEST_CASE ("plugin: presets and portable instruments travel to another computer", "[plugin]")
{
    TempDir tmp;
    const auto file = writeSource (tmp.dir, "travel.wav", testsignals::vowel (midiToHz (55), 2.0, 48000.0, 7));
    const auto preset = tmp.dir.getChildFile ("mine.osppreset");
    const auto package = tmp.dir.getChildFile ("mine.ospinstrument");
    AudioData before;
    {
        OspAudioProcessor p;
        loadAndWait (p, file);
        p.parameters.getParameter ("life")->setValueNotifyingHost (0.0f);
        p.parameters.getParameter ("character")->setValueNotifyingHost (0.8f);
        before = playNote (p, 60, 48000.0, 0.6);
        REQUIRE (p.savePreset (preset));
        juce::String error;
        REQUIRE (p.exportInstrument (package, error));
    }
    {
        // Preset on the same machine.
        OspAudioProcessor p;
        REQUIRE (p.loadPreset (preset));
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        CHECK (p.parameters.getParameter ("character")->getValue() == Approx (0.8f));
    }
    // "Another computer": a fresh, empty sample store and no original file.
    REQUIRE (file.deleteFile());
    const auto otherStore = tmp.dir.getChildFile ("other-store");
    const juce::String previousStore (std::getenv ("OSP_SAMPLE_STORE"));
#if JUCE_WINDOWS
    _putenv_s ("OSP_SAMPLE_STORE", otherStore.getFullPathName().toRawUTF8());
#else
    setenv ("OSP_SAMPLE_STORE", otherStore.getFullPathName().toRawUTF8(), 1);
#endif
    {
        OspAudioProcessor p;
        juce::String error;
        REQUIRE (p.importInstrument (package, error));
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        REQUIRE (p.currentInstrument() != nullptr);
        CHECK (p.currentInstrument()->filename == "travel.wav");
        const auto after = playNote (p, 60, 48000.0, 0.6);
        REQUIRE (after.numFrames() == before.numFrames());
        double diff = 0.0;
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (std::size_t i = 0; i < after.channels[ch].size(); ++i)
                diff = std::max (diff, static_cast<double> (std::abs (after.channels[ch][i] - before.channels[ch][i])));
        CHECK (diff < 1.0e-12); // the same instrument, bit for bit
    }
#if JUCE_WINDOWS
    _putenv_s ("OSP_SAMPLE_STORE", previousStore.toRawUTF8());
#else
    setenv ("OSP_SAMPLE_STORE", previousStore.toRawUTF8(), 1);
#endif
}

TEST_CASE ("plugin: undo and redo sample loads and root changes", "[plugin]")
{
    TempDir tmp;
    const auto first = writeSource (tmp.dir, "first.wav", testsignals::vowel (midiToHz (57), 1.5, 48000.0, 1));
    const auto second = writeSource (tmp.dir, "second.wav", testsignals::saw (midiToHz (50), 1.5, 48000.0));
    OspAudioProcessor p;
    loadAndWait (p, first);
    loadAndWait (p, second);
    REQUIRE (p.currentInstrument()->filename == "second.wav");
    REQUIRE (p.undoManager.canUndo());
    p.undoManager.undo();
    p.pollLoads();
    CHECK (p.currentInstrument()->filename == "first.wav");
    p.undoManager.redo();
    p.pollLoads();
    CHECK (p.currentInstrument()->filename == "second.wav");

    p.changeRootOverride (62.0);
    CHECK (p.rootOverride().value_or (-1.0) == Approx (62.0));
    p.undoManager.undo();
    CHECK_FALSE (p.rootOverride().has_value());
    p.undoManager.redo();
    CHECK (p.rootOverride().value_or (-1.0) == Approx (62.0));
}

// Needs a display (run under xvfb-run on headless Linux). Hidden by default:
//   OSP_SNAPSHOT_DIR=/tmp xvfb-run ./osp_plugin_tests "[ui]"
TEST_CASE ("plugin: engine C holds up at every host sample rate and block size", "[plugin]")
{
    TempDir tmp;
    const auto file = writeSource (tmp.dir, "pluck.wav", testsignals::pluck (midiToHz (55), 2.0, 44100.0, 4));
    const auto vowel = writeSource (tmp.dir, "vowel.wav", testsignals::vowel (midiToHz (57), 3.0, 48000.0, 3));

    // A chord far from the root on both sides, with the default macros (LIFE, SPACE, ...)
    // and a pitch-bend sweep, rendered with a fixed or a host-like varying block size.
    auto render = [] (OspAudioProcessor& p, double rate, int maxBlock, bool varying) {
        p.prepareToPlay (rate, maxBlock);
        const auto total = static_cast<int> (1.5 * rate);
        AudioData out = AudioData::allocate (2, total, rate);
        juce::AudioBuffer<float> buffer (2, maxBlock);
        juce::Random sizes (7);
        int pos = 0, block = 0;
        while (pos < total)
        {
            const int n = std::min (total - pos, varying ? 1 + sizes.nextInt (maxBlock) : maxBlock);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
            view.clear();
            juce::MidiBuffer midi;
            if (block == 0)
                for (int note : { 31, 55, 62, 79, 91 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (60 + note % 60)), 0);
            // Sample-accurate events, wherever the block boundaries fall.
            if (total / 2 >= pos && total / 2 < pos + n)
                midi.addEvent (juce::MidiMessage::pitchWheel (1, 12000), total / 2 - pos);
            if (3 * total / 4 >= pos && 3 * total / 4 < pos + n)
                midi.addEvent (juce::MidiMessage::allNotesOff (1), 3 * total / 4 - pos);
            p.processBlock (view, midi);
            for (int ch = 0; ch < 2; ++ch)
                std::copy (view.getReadPointer (ch), view.getReadPointer (ch) + n, out.channels[static_cast<std::size_t> (ch)].begin() + pos);
            pos += n;
            ++block;
        }
        return out;
    };

    for (const auto& source : { file, vowel })
    {
        OspAudioProcessor p;
        loadAndWait (p, source);
        for (double rate : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            std::vector<AudioData> fixedRenders;
            for (int block : { 1, 32, 128, 1024, 4096 })
            {
                if (block == 1 && rate > 50000.0)
                    continue; // slow, and covered at the lower rates
                const auto out = render (p, rate, block, false);
                INFO (source.getFileName() << " at " << rate << " Hz, block " << block);
                bool finite = true;
                float peak = 0.0f, worstStep = 0.0f;
                for (const auto& ch : out.channels)
                    for (std::size_t i = 1; i < ch.size(); ++i)
                    {
                        finite = finite && std::isfinite (ch[i]);
                        peak = std::max (peak, std::abs (ch[i]));
                        worstStep = std::max (worstStep, std::abs (ch[i] - ch[i - 1]));
                    }
                CHECK (finite);
                CHECK (peak > 0.01f);
                CHECK (peak < 2.0f);
                CHECK (worstStep < 0.5f); // no clicks or blow-ups
                fixedRenders.push_back (out);
            }
            // Same output whatever the block size, also when the host varies it.
            const auto varying = render (p, rate, 512, true);
            auto maxDiff = [&] (const AudioData& a) {
                float d = 0.0f;
                for (std::size_t ch = 0; ch < 2; ++ch)
                    for (std::size_t i = 0; i < a.channels[ch].size(); ++i)
                        d = std::max (d, std::abs (a.channels[ch][i] - fixedRenders.front().channels[ch][i]));
                return d;
            };
            INFO (source.getFileName() << " at " << rate << " Hz");
            for (std::size_t k = 1; k < fixedRenders.size(); ++k)
            {
                INFO ("fixed render " << k);
                CHECK (maxDiff (fixedRenders[k]) <= 0.0f);
            }
            CHECK (maxDiff (varying) <= 0.0f);
        }
    }
}

TEST_CASE ("plugin: the preset browser lists, opens and steps through presets", "[plugin]")
{
    TempDir tmp;
    const auto folder = tmp.dir.getChildFile ("Presets");
    OspAudioProcessor p;
    auto* life = p.parameters.getParameter ("life");
    REQUIRE (life != nullptr);
    for (int i : { 2, 10, 1 })
    {
        life->setValueNotifyingHost (static_cast<float> (i) / 20.0f);
        REQUIRE (p.savePreset (folder.getChildFile (i == 10 ? "Pads/Preset " + juce::String (i) : "Preset " + juce::String (i))
                                   .withFileExtension (OspAudioProcessor::presetExtension)));
    }
    folder.getChildFile ("notes.txt").replaceWithText ("not a preset");

    // Natural order, sub-folders included, other files ignored.
    const auto files = OspAudioProcessor::findFiles (folder, OspAudioProcessor::presetExtension);
    REQUIRE (files.size() == 3);
    CHECK (files[0].getFileNameWithoutExtension() == "Preset 10"); // "Pads/..." sorts first
    CHECK (files[1].getFileNameWithoutExtension() == "Preset 1");
    CHECK (files[2].getFileNameWithoutExtension() == "Preset 2");

    // Stepping follows the same order (sub-folders included), wraps around, and recalls settings.
    REQUIRE (p.loadPreset (files[1]));
    CHECK (life->getValue() == Approx (0.05f));
    REQUIRE (p.stepPreset (1, folder));
    CHECK (p.currentPresetFile().getFileNameWithoutExtension() == "Preset 2");
    CHECK (life->getValue() == Approx (0.1f));
    REQUIRE (p.stepPreset (1, folder));
    CHECK (p.currentPresetFile().getFileNameWithoutExtension() == "Preset 10");
    CHECK (life->getValue() == Approx (0.5f));
    REQUIRE (p.stepPreset (-1, folder));
    CHECK (p.currentPresetFile().getFileNameWithoutExtension() == "Preset 2");
    CHECK_FALSE (p.loadPreset (folder.getChildFile ("notes.txt")));
}

TEST_CASE ("plugin: editor builds, shows the instrument and can be snapshotted", "[.][ui]")
{
    TempDir tmp;
    const auto file = writeSource (tmp.dir, "Vowel A3.wav", testsignals::vowel (midiToHz (57), 3.0, 48000.0, 3));

    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    REQUIRE (editor != nullptr);

    auto snapshot = [&] (const juce::String& name) {
        if (const char* dir = std::getenv ("OSP_SNAPSHOT_DIR"))
        {
            if (auto* settled = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get()))
                settled->refreshNow();   // transitions finish (popups fade in, cards glide)
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream out (juce::File (dir).getChildFile (name));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    };

    snapshot ("osp-editor-empty.png");
    loadAndWait (p, file);
    if (auto* ospEditor = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get()))
        ospEditor->refreshNow();
    snapshot ("osp-editor-loaded.png");

    // Macro popups: one at a time, anchored to the macro, closed on request.
    if (auto* ospEditor = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get()))
    {
        const char* names[] = { "life", "dynamics", "character", "movement", "space" };
        for (int i = 0; i < 5; ++i)
        {
            ospEditor->openPopup (i);
            CHECK (ospEditor->openPopupIndex() == i);
            snapshot (juce::String ("osp-editor-popup-") + names[i] + ".png");
        }
        // Every SPACE type and MOVEMENT mode draws its own picture.
        for (int type = 0; type < 4; ++type)
        {
            p.parameters.getParameter ("space.type")->setValueNotifyingHost (static_cast<float> (type) / 3.0f);
            ospEditor->openPopup (4);
            snapshot ("osp-popup-space-" + juce::String (type) + ".png");
        }
        p.parameters.getParameter ("space.type")->setValueNotifyingHost (p.parameters.getParameter ("space.type")->getDefaultValue());
        for (int mode = 0; mode < 4; ++mode)
        {
            p.parameters.getParameter ("movement.mode")->setValueNotifyingHost (static_cast<float> (mode) / 4.0f);
            ospEditor->openPopup (3);
            snapshot ("osp-popup-movement-" + juce::String (mode) + ".png");
        }
        // MOVEMENT in SHAPER mode: pattern strip, PATTERN, RATE, TARGET, SMOOTH.
        p.parameters.getParameter ("movement.mode")->setValueNotifyingHost (1.0f);
        ospEditor->openPopup (3);
        snapshot ("osp-editor-popup-shaper.png");
        // A press in another window - the PATTERN or RATE menu - must not close the popup:
        // closing it deleted the selector whose menu choice was still to arrive (a host crash).
        {
            juce::Component menuWindow;
            ospEditor->mouseDownAnywhere (&menuWindow);
            CHECK (ospEditor->openPopupIndex() == 3);
            ospEditor->mouseDownAnywhere (nullptr);
            CHECK (ospEditor->openPopupIndex() == 3);
            // A press elsewhere in the editor still closes it.
            ospEditor->mouseDownAnywhere (ospEditor);
            CHECK (ospEditor->openPopupIndex() == -1);
            ospEditor->openPopup (3);
        }
        p.parameters.getParameter ("movement.mode")->setValueNotifyingHost (0.0f);
        ospEditor->closePopup();
        CHECK (ospEditor->openPopupIndex() == -1);

        // Layer B in Granular mode: tabs, blend, file name, mode switch, overlay.
        const auto second = writeSource (tmp.dir, "Lydian Cinema.wav", testsignals::vowel (midiToHz (48), 4.0, 48000.0, 8));
        p.setEditLayer (1);
        p.loadFile (second);
        REQUIRE (p.waitForLoads (20000));
        p.pollLoads();
        p.parameters.getParameter ("layerB.sourceMode")->setValueNotifyingHost (1.0f);
        p.parameters.getParameter ("ab.blend")->setValueNotifyingHost (0.6f);
        p.parameters.getParameter ("layerB.granular.spread")->setValueNotifyingHost (0.45f);
        // Hold a chord so the cloud is visible: the display shows the grains playing now.
        p.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> audio (2, 512);
        for (int block = 0; block < 40; ++block)
        {
            juce::MidiBuffer midi;
            if (block == 0)
                for (int note : { 48, 55, 64 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)), 0);
            audio.clear();
            p.processBlock (audio, midi);
        }
        ospEditor->refreshNow();
        CHECK (p.grainSnapshot (1).count.load() > 2);   // three notes, grains on each (LIFE varies their density)
        snapshot ("osp-editor-layer-b-granular.png");
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        p.processBlock (audio, off);
        p.setEditLayer (0);
        p.parameters.getParameter ("ab.blend")->setValueNotifyingHost (0.0f);
        // One Shot: a read head per playing note, following the recording.
        for (int block = 0; block < 120; ++block)
        {
            juce::MidiBuffer midi;
            if (block == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, static_cast<juce::uint8> (100)), 0);
            if (block == 50)
                midi.addEvent (juce::MidiMessage::noteOn (1, 64, static_cast<juce::uint8> (90)), 0);
            audio.clear();
            p.processBlock (audio, midi);
        }
        ospEditor->refreshNow();
        const auto& heads = p.grainSnapshot (0);
        REQUIRE (heads.playheads.load() == 2);
        // The first note has played longer, so its read head is further along the recording.
        const float earlier = std::max (heads.playheadPosition[0].load(), heads.playheadPosition[1].load());
        const float later = std::min (heads.playheadPosition[0].load(), heads.playheadPosition[1].load());
        CHECK (earlier > later);
        CHECK (later > 0.0f);
        snapshot ("osp-editor-layer-a.png");
        // Turning a macro shows its value in a graphite bubble with light text.
        {
            std::function<juce::Slider* (juce::Component&)> findMacro = [&] (juce::Component& c) -> juce::Slider* {
                for (auto* child : c.getChildren())
                {
                    if (auto* slider = dynamic_cast<juce::Slider*> (child); slider != nullptr && slider->getTitle() == "LIFE")
                        return slider;
                    if (auto* found = findMacro (*child))
                        return found;
                }
                return nullptr;
            };
            auto* macro = findMacro (*editor);
            REQUIRE (macro != nullptr);
            {
                const auto bubbleText = macro->findColour (juce::TooltipWindow::textColourId, true);
                const auto bubbleBack = editor->findColour (juce::BubbleComponent::backgroundColourId, true);
                CHECK (std::abs (bubbleText.getPerceivedBrightness() - bubbleBack.getPerceivedBrightness()) > 0.5f);
                auto source = juce::Desktop::getInstance().getMainMouseSource();
                const auto centre = macro->getLocalBounds().getCentre().toFloat();
                const juce::MouseEvent press (source, centre, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, macro, macro,
                                              juce::Time::getCurrentTime(), centre, juce::Time::getCurrentTime(), 1, false);
                macro->mouseDown (press);
                snapshot ("osp-editor-value-bubble.png");
                macro->mouseUp (press);
            }
        }
        juce::MidiBuffer stop;
        stop.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        p.processBlock (audio, stop);
    }
    p.editorBeingDeleted (editor.get());
    editor.reset();

    // The Advanced panel opens on request and stays open with the session.
    p.setAdvancedOpen (true);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    OspAudioProcessor reopened;
    reopened.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (reopened.advancedOpen());
    editor.reset (p.createEditorIfNeeded());
    if (auto* ospEditor = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get()))
        ospEditor->refreshNow();
    snapshot ("osp-editor-advanced.png");
    p.editorBeingDeleted (editor.get());
}

int main (int argc, char* argv[])
{
    // Isolated sample store per run so tests never touch the user's library.
    const auto store = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("osp-plugin-test-store-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
#if JUCE_WINDOWS
    _putenv_s ("OSP_SAMPLE_STORE", store.getFullPathName().toRawUTF8());
#else
    setenv ("OSP_SAMPLE_STORE", store.getFullPathName().toRawUTF8(), 1);
#endif

    juce::ScopedJuceInitialiser_GUI juceInit;
    const int result = Catch::Session().run (argc, argv);
    store.deleteRecursively();
    return result;
}

//==============================================================================
// Adaptive 1-3 layers

namespace
{
    double bin (const AudioData& audio, double hz, double from, double to)
    {
        const auto& x = audio.channels[0];
        const auto a = static_cast<std::size_t> (from * audio.sampleRate), b = static_cast<std::size_t> (to * audio.sampleRate);
        const double w = 2.0 * std::cos (2.0 * juce::MathConstants<double>::pi * hz / audio.sampleRate);
        double s1 = 0.0, s2 = 0.0;
        for (std::size_t i = a; i < b; ++i)
        {
            const double s0 = x[i] + w * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return (s1 * s1 + s2 * s2 - w * s1 * s2) / static_cast<double> (b - a);
    }

    float valueOf (OspAudioProcessor& p, const juce::String& id) { return p.parameterValue (id); }
}

TEST_CASE ("plugin: three dropped sounds become layers A, B, C, all heard, recalled with their controls", "[plugin][adaptive]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "low.wav", testsignals::vowel (midiToHz (57), 2.5, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "mid.wav", testsignals::vowel (midiToHz (61), 2.5, 48000.0, 5));
    const auto c = writeSource (tmp.dir, "high.wav", testsignals::vowel (midiToHz (64), 2.5, 48000.0, 7));
    OspAudioProcessor p;
    CHECK (p.occupiedLayerCount() == 0);
    CHECK (p.addLayers ({ a, b, c }) == 3);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    CHECK (p.occupiedLayerCount() == 3);
    CHECK (p.firstFreeLayer() == -1);
    CHECK (juce::String (p.currentInstrument (2)->filename) == "high.wav");
    // The newest layer is audible: the triangle sits at its centre.
    CHECK (valueOf (p, "mix.x") == Approx (0.5f));
    CHECK (valueOf (p, "mix.y") == Approx (1.0f / 3.0f));

    // Every layer sounds on the same note (each keeps its own root: A3, C#4, E4 recordings
    // all play A3 on key 57, so separate them with TUNE).
    p.setParameterValue ("layerB.tune", 4.0f);
    p.setParameterValue ("layerC.tune", 7.0f);
    const auto out = playNote (p, 57, 48000.0, 1.2);
    const double ea = bin (out, midiToHz (57), 0.3, 1.1), eb = bin (out, midiToHz (61), 0.3, 1.1), ec = bin (out, midiToHz (64), 0.3, 1.1);
    INFO ("A " << ea << " B " << eb << " C " << ec);
    CHECK (ea > 1.0e-6);
    CHECK (eb > 1.0e-6);
    CHECK (ec > 1.0e-6);

    // Layer controls and the mix are part of the session.
    p.setParameterValue ("layerC.level", -6.0f);
    p.setParameterValue ("layerC.pan", -40.0f);
    p.setParameterValue ("layerB.reverse", 1.0f);
    p.setParameterValue ("layerA.follow", 0.0f);
    p.setParameterValue ("layerC.sourceMode", 1.0f);
    p.setParameterValue ("mix.x", 0.8f);
    p.setParameterValue ("decay", 900.0f);
    p.setParameterValue ("sustainLevel", 60.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    OspAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (restored.waitForLoads (30000));
    restored.pollLoads();
    CHECK (restored.occupiedLayerCount() == 3);
    CHECK (juce::String (restored.currentInstrument (2)->filename) == "high.wav");
    CHECK (valueOf (restored, "layerC.level") == Approx (-6.0f));
    CHECK (valueOf (restored, "layerC.pan") == Approx (-40.0f));
    CHECK (valueOf (restored, "layerB.reverse") == Approx (1.0f));
    CHECK (valueOf (restored, "layerA.follow") == Approx (0.0f).margin (1.0e-4));
    CHECK (valueOf (restored, "layerC.sourceMode") == Approx (1.0f));
    CHECK (valueOf (restored, "layerB.tune") == Approx (4.0f));
    CHECK (valueOf (restored, "mix.x") == Approx (0.8f));
    CHECK (valueOf (restored, "decay") == Approx (900.0f).margin (0.1));
    CHECK (valueOf (restored, "sustainLevel") == Approx (60.0f));
    // Same session, same sound.
    const auto x = playNote (p, 60, 48000.0, 0.8), y = playNote (restored, 60, 48000.0, 0.8);
    double diff = 0.0;
    for (std::size_t i = 0; i < x.channels[0].size(); ++i)
        diff = std::max (diff, static_cast<double> (std::abs (x.channels[0][i] - y.channels[0][i])));
    CHECK (diff < 1.0e-6);
}

TEST_CASE ("plugin: removing a layer compacts the others with their whole state; it can be restored", "[plugin][adaptive]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "one.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "two.wav", testsignals::vowel (midiToHz (60), 2.0, 48000.0, 4));
    const auto c = writeSource (tmp.dir, "three.wav", testsignals::vowel (midiToHz (64), 2.0, 48000.0, 5));
    OspAudioProcessor p;
    p.addLayers ({ a, b, c });
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.setParameterValue ("layerC.tune", -5.0f);
    p.setParameterValue ("layerC.granular.spread", 70.0f);
    p.setParameterValue ("layerC.sourceMode", 1.0f);
    p.setParameterValue ("layerB.level", -9.0f);
    p.setRootOverride (64.0, 2);

    // Remove B while a note sounds: C becomes B, the note finishes safely.
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 0);
    p.processBlock (buffer, on);
    REQUIRE (p.removeLayer (1));
    p.pollLoads();
    for (int i = 0; i < 100; ++i)
    {
        juce::MidiBuffer none;
        p.processBlock (buffer, none);
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < buffer.getNumSamples(); ++s)
                REQUIRE (std::isfinite (buffer.getSample (ch, s)));
    }
    CHECK (p.occupiedLayerCount() == 2);
    CHECK (juce::String (p.currentInstrument (1)->filename) == "three.wav");
    CHECK (p.currentInstrument (2) == nullptr);
    CHECK (valueOf (p, "layerB.tune") == Approx (-5.0f));
    CHECK (valueOf (p, "layerB.granular.spread") == Approx (70.0f));
    CHECK (valueOf (p, "layerB.sourceMode") == Approx (1.0f));
    CHECK (valueOf (p, "layerB.level") == Approx (0.0f).margin (1.0e-4));   // C's level, not B's
    REQUIRE (p.rootOverride (1).has_value());
    CHECK (*p.rootOverride (1) == Approx (64.0));
    CHECK_FALSE (p.rootOverride (2).has_value());
    CHECK (valueOf (p, "layerC.sourceMode") == Approx (0.0f).margin (1.0e-4));   // the freed slot is neutral

    // Restore: back in its place, the others move up again.
    REQUIRE (p.canRestoreRemovedLayer());
    REQUIRE (p.restoreRemovedLayer());
    p.pollLoads();
    CHECK (p.occupiedLayerCount() == 3);
    CHECK (juce::String (p.currentInstrument (1)->filename) == "two.wav");
    CHECK (juce::String (p.currentInstrument (2)->filename) == "three.wav");
    CHECK (valueOf (p, "layerB.level") == Approx (-9.0f));
    CHECK (valueOf (p, "layerC.tune") == Approx (-5.0f));
    CHECK_FALSE (p.canRestoreRemovedLayer());

    // Removing the last layer of one leaves an empty instrument.
    REQUIRE (p.removeLayer (2));
    REQUIRE (p.removeLayer (1));
    REQUIRE (p.removeLayer (0));
    p.pollLoads();
    CHECK (p.occupiedLayerCount() == 0);
    const auto quiet = playNote (p, 60, 48000.0, 0.2);
    for (float v : quiet.channels[0])
        REQUIRE_FALSE (std::abs (v) > 0.0f);
}

TEST_CASE ("plugin: more than three dropped sounds load three and say so", "[plugin][adaptive]")
{
    TempDir tmp;
    juce::Array<juce::File> files;
    for (int i = 0; i < 5; ++i)
        files.add (writeSource (tmp.dir, "s" + juce::String (i) + ".wav", testsignals::vowel (midiToHz (55 + i), 1.0, 48000.0, static_cast<std::uint64_t> (i + 1))));
    OspAudioProcessor p;
    CHECK (p.addLayers (files) == 3);
    CHECK (p.statusMessage().contains ("left out"));
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    CHECK (p.occupiedLayerCount() == 3);
    CHECK (p.addLayers ({ files[4] }) == 0);   // full: replace a layer instead
}

TEST_CASE ("plugin: sessions from before the adaptive layers open as they were (v5 migration)", "[plugin][adaptive]")
{
    TempDir tmp;
    const auto file = writeSource (tmp.dir, "old.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    OspAudioProcessor original;
    loadAndWait (original, file);
    original.setParameterValue ("sustain", 0.0f);   // "Recording": the old global sustain off
    juce::MemoryBlock state;
    original.getStateInformation (state);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    auto tree = juce::ValueTree::fromXml (*xml);
    tree.setProperty ("stateVersion", 5, nullptr);
    // A v5 session knows nothing of the new parameters or of layer C.
    for (int i = tree.getNumChildren(); --i >= 0;)
    {
        const auto id = tree.getChild (i)["id"].toString();
        if (id.startsWith ("layerC.") || id == "mix.x" || id == "mix.y" || id == "decay" || id == "sustainLevel"
            || OspAudioProcessor::layerControlNames().contains (id.fromFirstOccurrenceOf (".", false, false)))
            tree.removeChild (i, nullptr);
    }
    tree.removeChild (tree.getChildWithName ("InstrumentC"), nullptr);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);

    // Opened in an instance whose new controls were moved: they return to neutral.
    OspAudioProcessor p;
    p.setParameterValue ("layerA.level", -20.0f);
    p.setParameterValue ("layerA.tune", 5.0f);
    p.setParameterValue ("layerC.sourceMode", 1.0f);
    p.setParameterValue ("sustainLevel", 10.0f);
    p.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    REQUIRE (p.waitForLoads (20000));
    p.pollLoads();
    CHECK (p.occupiedLayerCount() == 1);
    CHECK (valueOf (p, "layerA.level") == Approx (0.0f).margin (1.0e-4));
    CHECK (valueOf (p, "layerA.tune") == Approx (0.0f).margin (1.0e-4));
    CHECK (valueOf (p, "layerC.sourceMode") == Approx (0.0f).margin (1.0e-4));
    CHECK (valueOf (p, "sustainLevel") == Approx (100.0f));
    // The old global "Recording" sustain is now every layer's LOOP off; Sustain is back to Endless.
    for (const char* id : { "layerA.loop", "layerB.loop", "layerC.loop" })
        CHECK (valueOf (p, id) == Approx (0.0f).margin (1.0e-4));
    CHECK (valueOf (p, "sustain") == Approx (1.0f));
    // ...and it plays exactly like the original (Recording sustain, one layer).
    const auto x = playNote (original, 57, 48000.0, 2.5), y = playNote (p, 57, 48000.0, 2.5);
    double diff = 0.0;
    for (std::size_t i = 0; i < x.channels[0].size(); ++i)
        diff = std::max (diff, static_cast<double> (std::abs (x.channels[0][i] - y.channels[0][i])));
    CHECK (diff < 1.0e-6);
}

TEST_CASE ("plugin: the editor adapts to one, two and three sounds; drops replace or add", "[.][ui]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "Glass Resonance.wav", testsignals::vowel (midiToHz (48), 3.0, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "Evolving Texture.wav", testsignals::vowel (midiToHz (55), 3.5, 48000.0, 8));
    const auto c = writeSource (tmp.dir, "Warm Harmonics.wav", testsignals::pluck (midiToHz (52), 2.5, 48000.0, 5));
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    auto snapshot = [&] (const juce::String& name) {
        if (const char* dir = std::getenv ("OSP_SNAPSHOT_DIR"))
        {
            if (auto* settled = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get()))
                settled->refreshNow();   // transitions finish (popups fade in, cards glide)
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream out (juce::File (dir).getChildFile (name));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    };
    auto settle = [&] {
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        ui->refreshNow();
    };
    const auto centre = editor->getLocalBounds().getCentre().withY (200);
    const auto left = juce::Point<int> (editor->getWidth() / 5, 200), right = juce::Point<int> (editor->getWidth() * 4 / 5, 200);

    // Nothing loaded: one drop zone, no empty cards.
    ui->refreshNow();
    CHECK (ui->visibleCardCount() == 0);
    CHECK (ui->dropTargetAt (centre) == "drop");
    snapshot ("osp-adaptive-0.png");

    // One sound: one full-width card.
    p.addLayers ({ a });
    settle();
    CHECK (ui->visibleCardCount() == 1);
    CHECK (ui->dropTargetAt (left) == "replace A");
    CHECK (ui->dropTargetAt (right) == "replace A");
    snapshot ("osp-adaptive-1.png");
    // Dragging another sound over it: room for a second layer opens beside it.
    ui->previewDrag (true, right);
    CHECK (ui->dropTargetAt (right) == "add B");
    CHECK (ui->dropTargetAt (left) == "replace A");
    snapshot ("osp-adaptive-1-drag.png");
    ui->previewDrag (false);
    CHECK (ui->dropTargetAt (right) == "replace A");

    // Two: equal cards and the A/B blend (B Granular).
    p.addLayers ({ b });
    settle();
    p.setParameterValue ("layerB.sourceMode", 1.0f);
    p.setParameterValue ("layerA.level", -3.0f);
    p.setParameterValue ("layerB.level", -3.0f);
    p.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> audio (2, 512);
    for (int block = 0; block < 40; ++block)
    {
        juce::MidiBuffer midi;
        if (block == 0)
            for (int note : { 48, 55 })
                midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)), 0);
        audio.clear();
        p.processBlock (audio, midi);
    }
    ui->refreshNow();
    CHECK (ui->visibleCardCount() == 2);
    CHECK (ui->dropTargetAt (left) == "replace A");
    CHECK (ui->dropTargetAt (right) == "replace B");
    snapshot ("osp-adaptive-2.png");
    juce::MidiBuffer off;
    off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    p.processBlock (audio, off);

    // Three: three cards, the mix triangle; a drop can only replace.
    p.addLayers ({ c });
    settle();
    p.setParameterValue ("layerC.sourceMode", 1.0f);
    ui->refreshNow();
    CHECK (ui->visibleCardCount() == 3);
    ui->previewDrag (true, right);
    CHECK (ui->dropTargetAt (right) == "replace C");
    CHECK (ui->dropTargetAt (centre) == "replace B");
    ui->previewDrag (false);
    snapshot ("osp-adaptive-3.png");

    // A click on the small triangle opens the large mix (and does not move the point);
    // in the popup the triangle places the mix directly.
    {
        std::function<void (juce::Component&, std::vector<juce::Component*>&)> collect = [&] (juce::Component& parent, std::vector<juce::Component*>& found) {
            for (auto* child : parent.getChildren())
            {
                if (! child->isVisible())
                    continue;   // (headless: nothing is on screen, so visibility down the tree)
                if (child->getTitle() == "Layer mix")
                    found.push_back (child);
                collect (*child, found);
            }
        };
        auto click = [] (juce::Component& target, juce::Point<float> at) {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent e (source, at, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, at, now, 1, false);
            target.mouseDown (e);
            target.mouseUp (e);
        };
        std::vector<juce::Component*> triangles;
        collect (*editor, triangles);
        REQUIRE (triangles.size() == 1);
        const float x0 = p.parameterValue ("mix.x"), y0 = p.parameterValue ("mix.y");
        click (*triangles.front(), triangles.front()->getLocalBounds().toFloat().getBottomLeft() + juce::Point<float> (12.0f, -9.0f));
        CHECK (ui->openPopupIndex() == osp::plugin::OspAudioProcessorEditor::mixPopup);
        CHECK (p.parameterValue ("mix.x") == Approx (x0));
        CHECK (p.parameterValue ("mix.y") == Approx (y0));
        snapshot ("osp-adaptive-3-mix.png");
        triangles.clear();
        collect (*editor, triangles);
        REQUIRE (triangles.size() == 2);
        auto* large = triangles.back()->getWidth() > triangles.front()->getWidth() ? triangles.back() : triangles.front();
        CHECK (large->getWidth() > 200);
        // Pressing near corner C puts most of the mix on C.
        click (*large, large->getLocalBounds().toFloat().getBottomRight() + juce::Point<float> (-40.0f, -26.0f));
        const auto share = InstrumentEngine::triangleShares (p.parameterValue ("mix.x"), p.parameterValue ("mix.y"));
        CHECK (share[2] > 0.6);
        snapshot ("osp-adaptive-3-mix-c.png");
        p.setParameterValue ("mix.x", 0.5f);
        p.setParameterValue ("mix.y", 1.0f / 3.0f);
    }
    // Original <-> Reimagined: linked by default, a drag moves every layer's thumb by the
    // same amount (offsets kept); unlinked, only the dragged one.
    {
        ui->closePopup();
        ui->refreshNow();
        std::function<juce::Component* (juce::Component&, const juce::String&)> find = [&] (juce::Component& parent, const juce::String& title) -> juce::Component* {
            for (auto* child : parent.getChildren())
            {
                if (! child->isVisible())
                    continue;
                if (child->getTitle() == title)
                    return child;
                if (auto* found = find (*child, title))
                    return found;
            }
            return nullptr;
        };
        auto* track = find (*editor, "Original / Reimagined");
        REQUIRE (track != nullptr);
        CHECK (find (*editor, "Link Reimagined") != nullptr);
        CHECK (valueOf (p, "reimaginedLink") >= 0.5f);
        p.setParameterValue ("reimagined", 20.0f);
        p.setParameterValue ("layerB.reimagined", 60.0f);
        p.setParameterValue ("layerC.reimagined", 40.0f);
        auto xAt = [track] (float v) { return 11.0f + (static_cast<float> (track->getWidth()) - 22.0f) * v / 100.0f; };
        auto drag = [track] (float fromX, float toX) {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            const float y = 0.5f * static_cast<float> (track->getHeight());
            const juce::Point<float> from (fromX, y), to (toX, y);
            const juce::MouseEvent down (source, from, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, track, track, now, from, now, 1, false);
            const juce::MouseEvent move (source, to, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, track, track, now, from, now, 1, true);
            track->mouseDown (down);
            track->mouseDrag (move);
            track->mouseUp (move);
        };
        drag (xAt (60.0f), xAt (70.0f));
        CHECK (valueOf (p, "layerB.reimagined") == Approx (70.0f).margin (0.6));
        CHECK (valueOf (p, "reimagined") == Approx (30.0f).margin (0.6));
        CHECK (valueOf (p, "layerC.reimagined") == Approx (50.0f).margin (0.6));
        p.setParameterValue ("reimaginedLink", 0.0f);
        drag (xAt (30.0f), xAt (10.0f));
        CHECK (valueOf (p, "reimagined") == Approx (10.0f).margin (0.6));
        CHECK (valueOf (p, "layerB.reimagined") == Approx (70.0f).margin (0.6));
        CHECK (valueOf (p, "layerC.reimagined") == Approx (50.0f).margin (0.6));
        p.setParameterValue ("reimaginedLink", 1.0f);
    }
    // The smallest window still fits three full cards.
    editor->setSize (900, 720);
    snapshot ("osp-adaptive-3-small.png");
    editor->setSize (1060, 820);

    // Removing B: C moves into its place, two cards again (and the large mix closes).
    ui->openPopup (osp::plugin::OspAudioProcessorEditor::mixPopup);
    REQUIRE (p.removeLayer (1));
    p.pollLoads();
    ui->refreshNow();
    CHECK (ui->visibleCardCount() == 2);
    CHECK (ui->openPopupIndex() == -1);
    CHECK (juce::String (p.currentInstrument (1)->filename) == "Warm Harmonics.wav");

    // Every window size keeps the layout usable (smallest and largest).
    for (auto size : { juce::Point<int> (900, 720), juce::Point<int> (1800, 1300) })
    {
        editor->setSize (size.x, size.y);
        ui->refreshNow();
        CHECK (ui->visibleCardCount() == 2);
    }
    p.editorBeingDeleted (editor.get());
}

TEST_CASE ("plugin: LINK moves the other linked layers by the same amount, keeping their relationship", "[plugin][adaptive]")
{
    TempDir tmp;
    juce::Array<juce::File> files;
    for (int i = 0; i < 3; ++i)
        files.add (writeSource (tmp.dir, "l" + juce::String (i) + ".wav", testsignals::vowel (midiToHz (57 + i), 1.0, 48000.0, static_cast<std::uint64_t> (i + 2))));
    OspAudioProcessor p;
    p.addLayers (files);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.setParameterValue ("layerA.level", -3.0f);
    p.setParameterValue ("layerB.level", -7.0f);
    p.setParameterValue ("layerC.level", -1.0f);
    p.setParameterValue ("layerA.link", 1.0f);
    p.setParameterValue ("layerB.link", 1.0f);   // C is not linked

    // The musician turns A down by 2 dB: B follows by 2 dB, C stays.
    p.setParameterValue ("layerA.level", -5.0f);
    p.applyLinkedDelta (0, "level", -2.0f);
    CHECK (valueOf (p, "layerB.level") == Approx (-9.0f).margin (0.05));
    CHECK (valueOf (p, "layerC.level") == Approx (-1.0f).margin (0.05));
    // Other controls too, clamped at their ends.
    p.setParameterValue ("layerB.tune", 20.0f);
    p.applyLinkedDelta (0, "tune", 7.0f);
    CHECK (valueOf (p, "layerB.tune") == Approx (24.0f));
    p.applyLinkedDelta (0, "pan", -30.0f);
    CHECK (valueOf (p, "layerB.pan") == Approx (-30.0f));
    // An unlinked layer's changes move nobody.
    p.applyLinkedDelta (2, "level", 4.0f);
    CHECK (valueOf (p, "layerA.level") == Approx (-5.0f).margin (0.05));
    CHECK (valueOf (p, "layerB.level") == Approx (-9.0f).margin (0.05));
    // Only START, TUNE, PAN and LEVEL link.
    p.applyLinkedDelta (0, "sourceMode", 1.0f);
    CHECK (valueOf (p, "layerB.sourceMode") == Approx (0.0f).margin (1.0e-4));
}

TEST_CASE ("plugin: an A/B session from before the adaptive layers opens as two layers with its blend", "[plugin][adaptive]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "first.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "second.wav", testsignals::vowel (midiToHz (60), 2.0, 48000.0, 4));
    OspAudioProcessor original;
    original.loadFile (a, 0);
    original.loadFile (b, 1);
    REQUIRE (original.waitForLoads (30000));
    original.pollLoads();
    original.setParameterValue ("ab.blend", 0.35f);
    original.setParameterValue ("layerB.sourceMode", 1.0f);
    juce::MemoryBlock state;
    original.getStateInformation (state);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    auto tree = juce::ValueTree::fromXml (*xml);
    tree.setProperty ("stateVersion", 5, nullptr);
    for (int i = tree.getNumChildren(); --i >= 0;)
    {
        const auto id = tree.getChild (i)["id"].toString();
        if (id.startsWith ("layerC.") || id == "mix.x" || id == "mix.y" || id == "decay" || id == "sustainLevel"
            || OspAudioProcessor::layerControlNames().contains (id.fromFirstOccurrenceOf (".", false, false)))
            tree.removeChild (i, nullptr);
    }
    tree.removeChild (tree.getChildWithName ("InstrumentC"), nullptr);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);

    // Opened where a three-layer instrument was playing: C goes, A and B come back as they were.
    const auto c = writeSource (tmp.dir, "third.wav", testsignals::vowel (midiToHz (64), 2.0, 48000.0, 5));
    OspAudioProcessor p;
    p.addLayers ({ c, c, c });
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    CHECK (p.occupiedLayerCount() == 2);
    CHECK (juce::String (p.currentInstrument (0)->filename) == "first.wav");
    CHECK (juce::String (p.currentInstrument (1)->filename) == "second.wav");
    CHECK (p.currentInstrument (2) == nullptr);
    CHECK (valueOf (p, "ab.blend") == Approx (0.35f));
    CHECK (valueOf (p, "layerB.sourceMode") == Approx (1.0f));
    CHECK (valueOf (p, "layerC.sourceMode") == Approx (0.0f).margin (1.0e-4));
    // Same sound as the session it came from.
    const auto x = playNote (original, 57, 48000.0, 1.0), y = playNote (p, 57, 48000.0, 1.0);
    double diff = 0.0;
    for (std::size_t i = 0; i < x.channels[0].size(); ++i)
        diff = std::max (diff, static_cast<double> (std::abs (x.channels[0][i] - y.channels[0][i])));
    CHECK (diff < 1.0e-6);
}

TEST_CASE ("plugin: automating every new control while notes play stays smooth and finite", "[plugin][adaptive]")
{
    TempDir tmp;
    juce::Array<juce::File> files;
    for (int i = 0; i < 3; ++i)
        files.add (writeSource (tmp.dir, "auto" + juce::String (i) + ".wav", testsignals::vowel (midiToHz (55 + 2 * i), 2.0, 48000.0, static_cast<std::uint64_t> (i + 9))));
    OspAudioProcessor p;
    p.addLayers (files);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.setParameterValue ("layerB.sourceMode", 1.0f);
    p.prepareToPlay (48000.0, 128);
    juce::AudioBuffer<float> buffer (2, 128);
    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, 57, static_cast<juce::uint8> (100)), 0);
    on.addEvent (juce::MidiMessage::noteOn (1, 64, static_cast<juce::uint8> (90)), 0);
    juce::Random random (17);
    const char* automated[] = { "mix.x", "mix.y", "layerA.level", "layerB.level", "layerC.level", "layerA.pan", "layerB.pan", "layerC.pan",
                                "layerA.tune", "layerC.start", "layerB.granular.position", "layerB.granular.spread", "attack", "decay",
                                "sustainLevel", "release", "life", "character", "motion", "space", "reimagined", "movement.shaper.pattern" };
    float previous = 0.0f, largestStep = 0.0f, peak = 0.0f;
    for (int block = 0; block < 1500; ++block)
    {
        // A host automating everything at once, every block (about 2.7 ms), across the full ranges.
        if (block > 100)
            for (const auto* id : automated)
                if (random.nextFloat() < 0.15f)
                    if (auto* param = p.parameters.getParameter (id))
                        param->setValueNotifyingHost (random.nextFloat());
        if (block == 600)
            p.setParameterValue ("movement.mode", 4.0f);
        buffer.clear();
        juce::MidiBuffer midi;
        p.processBlock (buffer, block == 0 ? on : midi);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float v = buffer.getSample (0, i);
            REQUIRE (std::isfinite (v));
            peak = std::max (peak, std::abs (v));
            if (block > 100 && p.parameterValue ("layerA.tune") > -100.0f)
                largestStep = std::max (largestStep, std::abs (v - previous));
            previous = v;
        }
    }
    INFO ("peak " << peak << ", largest sample step " << largestStep);
    CHECK (peak > 0.0f);
    CHECK (peak < 4.0f);
    // No zipper or click: the largest sample-to-sample step stays a fraction of the level.
    CHECK (largestStep < 0.35f * peak);
}

TEST_CASE ("plugin: a sound replaced while it plays finishes, a bad file never stops the others", "[plugin][adaptive]")
{
    TempDir tmp;
    const auto good = writeSource (tmp.dir, "good.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    const auto other = writeSource (tmp.dir, "other.wav", testsignals::vowel (midiToHz (60), 2.0, 48000.0, 4));
    const auto broken = tmp.dir.getChildFile ("broken.wav");
    broken.replaceWithText ("RIFF this is not audio at all");
    const auto tiny = writeSource (tmp.dir, "tiny.wav", testsignals::sine (440.0, 0.02, 48000.0, 0.4, 1));

    OspAudioProcessor p;
    p.addLayers ({ good, broken, tiny });
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    CHECK (p.currentInstrument (0) != nullptr);
    CHECK (p.loadState (1) == OspAudioProcessor::LoadState::failed);
    CHECK_FALSE (p.isLayerOccupied (1));
    // The tiny sound, played as grains: no crash, no NaN.
    if (p.currentInstrument (2) != nullptr)
        p.setParameterValue ("layerC.sourceMode", 1.0f);

    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, 57, static_cast<juce::uint8> (100)), 0);
    p.processBlock (buffer, on);
    // Replace A while its note sounds: the note goes on with the old sound, safely.
    p.replaceLayer ({ other }, 0);
    for (int block = 0; block < 400; ++block)
    {
        if (block == 50)
            p.pollLoads();
        if (block == 100)
        {
            REQUIRE (p.waitForLoads (30000));
            p.pollLoads();
        }
        juce::MidiBuffer none;
        p.processBlock (buffer, none);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            REQUIRE (std::isfinite (buffer.getSample (0, i)));
    }
    CHECK (juce::String (p.currentInstrument (0)->filename) == "other.wav");
    CHECK_FALSE (p.rootOverride (0).has_value());
    juce::MidiBuffer off;
    off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    p.processBlock (buffer, off);
    const auto out = playNote (p, 60, 48000.0, 0.5);
    double energy = 0.0;
    for (float v : out.channels[0])
        energy += static_cast<double> (v) * v;
    CHECK (energy > 0.0);
}

TEST_CASE ("plugin: an instance closed while its sounds are still loading shuts down cleanly", "[plugin][adaptive]")
{
    // A host can close a plugin at any moment, also while the loader is still refining
    // a sound (each stage queues the next). The destructor must wait for that chain to
    // end: a stage that reports after the instance is gone writes into freed memory.
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "a.wav", testsignals::vowel (midiToHz (57), 3.0, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "b.wav", testsignals::vowel (midiToHz (61), 3.0, 48000.0, 5));
    const auto c = writeSource (tmp.dir, "c.wav", testsignals::vowel (midiToHz (64), 3.0, 48000.0, 7));
    juce::MemoryBlock state;
    {
        OspAudioProcessor p;
        p.addLayers ({ a, b, c });
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        p.getStateInformation (state);
    }
    // Closed at different points of the load: immediately, mid-analysis, between stages.
    for (int waitMs : { 0, 5, 40, 150, 400 })
    {
        {
            OspAudioProcessor p;
            p.addLayers ({ a, b, c });
            juce::Thread::sleep (waitMs);
        }
        {
            OspAudioProcessor p;
            p.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
            juce::Thread::sleep (waitMs);
        }
    }
    SUCCEED ("every instance closed without touching freed memory");
}

TEST_CASE ("plugin: Mono plays one note with legato and glide; sessions without it open in Poly", "[plugin][mono]")
{
    TempDir tmp;
    const auto file = writeSource (tmp.dir, "bass.wav", testsignals::vowel (midiToHz (45), 3.0, 48000.0, 3));
    OspAudioProcessor p;
    loadAndWait (p, file);
    p.setParameterValue ("voiceMode", 1.0f);
    p.setParameterValue ("glide", 120.0f);
    CHECK (valueOf (p, "glide") == Approx (120.0f).margin (0.5f));
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    auto block = [&] (std::initializer_list<juce::MidiMessage> events) {
        juce::MidiBuffer midi;
        for (const auto& e : events)
            midi.addEvent (e, 0);
        buffer.clear();
        p.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                REQUIRE (std::isfinite (buffer.getSample (ch, i)));
    };
    block ({ juce::MidiMessage::noteOn (1, 45, static_cast<juce::uint8> (100)) });
    for (int i = 0; i < 20; ++i)
        block ({});
    block ({ juce::MidiMessage::noteOn (1, 52, static_cast<juce::uint8> (100)), juce::MidiMessage::noteOn (1, 57, static_cast<juce::uint8> (100)) });
    for (int i = 0; i < 20; ++i)
        block ({});
    CHECK (p.activeVoices.load() == 1);   // three keys held, one note sounding
    block ({ juce::MidiMessage::noteOff (1, 57), juce::MidiMessage::noteOff (1, 52), juce::MidiMessage::noteOff (1, 45) });
    for (int i = 0; i < 200; ++i)
        block ({});
    CHECK (p.activeVoices.load() == 0);

    // Poly (the default) plays the chord.
    p.setParameterValue ("voiceMode", 0.0f);
    block ({ juce::MidiMessage::noteOn (1, 45, static_cast<juce::uint8> (100)), juce::MidiMessage::noteOn (1, 52, static_cast<juce::uint8> (100)) });
    block ({});
    CHECK (p.activeVoices.load() == 2);
    block ({ juce::MidiMessage::allNotesOff (1) });

    // A session saved before Mono existed (no voiceMode / glide) opens in Poly with no
    // glide, even in an instance that was set to Mono.
    juce::MemoryBlock state;
    p.getStateInformation (state);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (xml != nullptr);
    for (auto* child = xml->getFirstChildElement(); child != nullptr;)
    {
        auto* next = child->getNextElement();
        if (child->getStringAttribute ("id") == "voiceMode" || child->getStringAttribute ("id") == "glide")
            xml->removeChildElement (child, true);
        child = next;
    }
    juce::MemoryBlock older;
    juce::AudioProcessor::copyXmlToBinary (*xml, older);
    OspAudioProcessor q;
    q.setParameterValue ("voiceMode", 1.0f);
    q.setParameterValue ("glide", 800.0f);
    q.setStateInformation (older.getData(), static_cast<int> (older.getSize()));
    REQUIRE (q.waitForLoads (20000));
    q.pollLoads();
    CHECK (valueOf (q, "voiceMode") == Approx (0.0f).margin (1.0e-4));
    CHECK (valueOf (q, "glide") == Approx (0.0f).margin (1.0e-3));
    // ...and a session saved in Mono recalls it.
    p.setParameterValue ("voiceMode", 1.0f);
    p.setParameterValue ("glide", 300.0f);
    p.getStateInformation (state);
    OspAudioProcessor r;
    r.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (r.waitForLoads (20000));
    r.pollLoads();
    CHECK (valueOf (r, "voiceMode") == Approx (1.0f));
    CHECK (valueOf (r, "glide") == Approx (300.0f).margin (0.5f));
}

// Visual QA (design/README.md): the canonical scenes at the reference size, 1448 x 1086,
// for overlay against design/reference/. Hidden; scripts/visual-review.sh runs it:
//   OSP_SNAPSHOT_DIR=design/current xvfb-run ./osp_plugin_tests "[canonical]"
// OSP_CANONICAL_A / OSP_CANONICAL_B may name the reference composition's own recordings
// (otherwise a sustained vowel and a struck, ringing tone stand in for them).
TEST_CASE ("plugin: canonical screenshots for visual review", "[.][canonical]")
{
    const char* dir = std::getenv ("OSP_SNAPSHOT_DIR");
    if (dir == nullptr)
        return;
    TempDir tmp;
    auto source = [&] (const char* env, const juce::String& name, const AudioData& fallback) {
        if (const char* path = std::getenv (env); path != nullptr && juce::File (path).existsAsFile())
        {
            // Under the reference's own file name (uploads can carry a prefix).
            const auto copy = tmp.dir.getChildFile (name);
            juce::File (path).copyFileTo (copy);
            return copy;
        }
        return writeSource (tmp.dir, name, fallback);
    };
    const auto a = source ("OSP_CANONICAL_A", "MMZT_one_shot_sailboat_C.wav", testsignals::vowel (midiToHz (60), 2.1, 48000.0, 3));
    const auto b = source ("OSP_CANONICAL_B", "MMZT_one_shot_reverb_kalimba_C.wav", testsignals::pluck (midiToHz (60), 3.4, 48000.0, 5));
    const auto c = source ("OSP_CANONICAL_C", "Warm Harmonics.wav", testsignals::pluck (midiToHz (52), 2.5, 48000.0, 9));

    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    editor->setSize (1448, 1086);
    auto shot = [&] (const juce::String& name) {
        ui->refreshNow();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream out (juce::File (dir).getChildFile (name));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
    };
    auto settle = [&] {
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        ui->refreshNow();
    };
    auto set = [&] (const char* id, float value) { p.setParameterValue (id, value); };
    // The reference composition's settings (main-2-layer.png).
    set ("life", 50.0f);
    set ("dynamics", 22.0f);
    set ("character", 78.0f);
    set ("motion", 72.0f);
    set ("space", 40.0f);
    set ("reimagined", 18.0f);
    set ("attack", 2.0f);
    set ("decay", 600.0f);
    set ("sustainLevel", 100.0f);
    set ("release", 700.0f);
    set ("layerA.start", 1.0f);
    set ("layerA.reverse", 1.0f);
    set ("layerB.loop", 0.0f);
    // LIFE, CHARACTER and SPACE have their own settings in use (their dots and lights).
    set ("life.pitch", 65.0f);
    set ("character.resonance", 35.0f);
    set ("space.decay", 3.2f);
    p.setScreenModWheel (0.44f);

    shot ("01-empty.png");
    p.addLayers ({ a });
    settle();
    shot ("02-one-oneshot.png");
    set ("layerA.sourceMode", 1.0f);
    shot ("03-one-granular.png");
    set ("layerA.sourceMode", 0.0f);
    p.addLayers ({ b });
    settle();
    set ("ab.blend", 0.44f);
    set ("layerA.start", 1.0f);
    set ("layerB.reimagined", 90.0f);
    shot ("04-two-oneshot.png");
    set ("layerB.sourceMode", 1.0f);
    set ("layerB.granular.position", 23.0f);
    set ("layerB.granular.spread", 21.0f);
    // Notes playing: read heads in A, grains in B.
    p.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> audio (2, 512);
    for (int block = 0; block < 60; ++block)
    {
        juce::MidiBuffer midi;
        if (block % 8 == 0 && block < 56)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60 + (block / 8) * 2, static_cast<juce::uint8> (100)), 0);
        if (block == 59)   // released (the reference shows no held keys); still sounding
            for (int n = 0; n < 7; ++n)
                midi.addEvent (juce::MidiMessage::noteOff (1, 60 + n * 2), 0);
        audio.clear();
        p.processBlock (audio, midi);
    }
    shot ("05-two-mixed.png");
    shot ("main-2-layer.png");
    {
        // As on a Retina display (type and strokes at twice the pixels), for review.
        ui->refreshNow();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        juce::FileOutputStream out (juce::File (dir).getChildFile ("main-2-layer@2x.png"));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
    }
    // The popups over the two-layer instrument.
    ui->openPopup (0);
    shot ("08-life-popup.png");
    p.setParameterValue ("life.takes", 3.0f);   // 4 takes (round robins), PLUCK
    p.setParameterValue ("life.character", 1.0f);
    ui->openPopup (0);
    shot ("08b-life-takes.png");
    p.setParameterValue ("life.takes", 0.0f);
    p.setParameterValue ("life.character", 0.0f);
    ui->openPopup (1);
    shot ("09-dynamics-popup.png");
    ui->openPopup (2);
    shot ("10-character-popup.png");
    const char* movement[] = { "11-movement-drift.png", "12-movement-tape.png", "13-movement-chorus.png", "14-movement-pulse.png", "15-movement-shaper.png" };
    for (int mode = 0; mode < 5; ++mode)
    {
        p.parameters.getParameter ("movement.mode")->setValueNotifyingHost (static_cast<float> (mode) / 4.0f);
        ui->openPopup (3);
        shot (movement[mode]);
    }
    const char* space[] = { "16-space-room.png", "17-space-chamber.png", "18-space-plate.png", "19-space-spring.png" };
    for (int type = 0; type < 4; ++type)
    {
        p.parameters.getParameter ("space.type")->setValueNotifyingHost (static_cast<float> (type) / 3.0f);
        ui->openPopup (4);
        shot (space[type]);
    }
    p.parameters.getParameter ("space.type")->setValueNotifyingHost (0.0f);
    ui->openPopup (4);
    shot ("popup-space.png");
    // DECAY lengthens and shortens the tail on a fixed time axis.
    for (const auto& [seconds, name] : { std::pair<float, const char*> { 0.4f, "23-space-decay-short.png" }, { 2.4f, "24-space-decay-long.png" } })
    {
        set ("space.decay", seconds);
        ui->openPopup (4);
        shot (name);
    }
    set ("space.decay", 3.2f);
    ui->closePopup();
    juce::MidiBuffer off;
    off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    p.processBlock (audio, off);
    set ("layerA.sourceMode", 1.0f);
    shot ("06-two-granular.png");
    set ("layerA.sourceMode", 0.0f);
    p.addLayers ({ c });
    settle();
    shot ("07-three.png");
    ui->openPopup (osp::plugin::OspAudioProcessorEditor::mixPopup);
    shot ("20-mix-popup.png");
    ui->openPopup (osp::plugin::OspAudioProcessorEditor::advancedPopup);
    shot ("21-advanced-popup.png");
    ui->closePopup();
    p.clearAllSamples();
    shot ("22-cleared.png");
    p.editorBeingDeleted (editor.get());
}

TEST_CASE ("plugin: per-layer Reimagined - each layer its own, older sessions give every layer the one amount", "[plugin][adaptive]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "a.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "b.wav", testsignals::vowel (midiToHz (61), 2.0, 48000.0, 5));
    const auto c = writeSource (tmp.dir, "c.wav", testsignals::vowel (midiToHz (64), 2.0, 48000.0, 7));
    OspAudioProcessor p;
    p.setParameterValue ("reimagined", 35.0f);
    CHECK (p.addLayers ({ a, b, c }) == 3);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    // A new layer starts as Reimagined as the instrument is.
    CHECK (valueOf (p, "layerB.reimagined") == Approx (35.0f));
    CHECK (valueOf (p, "layerC.reimagined") == Approx (35.0f));
    p.setParameterValue ("layerB.reimagined", 80.0f);
    p.setParameterValue ("layerC.reimagined", 5.0f);
    // Removing B: C (with its own amount) moves into B's place.
    REQUIRE (p.removeLayer (1));
    p.pollLoads();
    CHECK (valueOf (p, "layerB.reimagined") == Approx (5.0f));
    CHECK (valueOf (p, "reimagined") == Approx (35.0f));
    REQUIRE (p.restoreRemovedLayer());
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    CHECK (valueOf (p, "layerB.reimagined") == Approx (80.0f));
    CHECK (valueOf (p, "layerC.reimagined") == Approx (5.0f));

    // Recall.
    juce::MemoryBlock state;
    p.getStateInformation (state);
    OspAudioProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (q.waitForLoads (30000));
    q.pollLoads();
    CHECK (valueOf (q, "layerB.reimagined") == Approx (80.0f));

    // A session from before (stateVersion 6, one amount): every layer keeps it.
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (xml != nullptr);
    xml->setAttribute ("stateVersion", 6);
    for (auto* child = xml->getFirstChildElement(); child != nullptr;)
    {
        auto* next = child->getNextElement();
        const auto id = child->getStringAttribute ("id");
        if (id == "layerB.reimagined" || id == "layerC.reimagined")
            xml->removeChildElement (child, true);
        child = next;
    }
    juce::MemoryBlock older;
    juce::AudioProcessor::copyXmlToBinary (*xml, older);
    OspAudioProcessor r;
    r.setStateInformation (older.getData(), static_cast<int> (older.getSize()));
    REQUIRE (r.waitForLoads (30000));
    r.pollLoads();
    CHECK (valueOf (r, "layerB.reimagined") == Approx (35.0f));
    CHECK (valueOf (r, "layerC.reimagined") == Approx (35.0f));
}

TEST_CASE ("plugin: clear all samples keeps the slots and every setting; new sounds take them over", "[plugin][adaptive]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "a.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "b.wav", testsignals::vowel (midiToHz (61), 2.0, 48000.0, 5));
    const auto c = writeSource (tmp.dir, "c.wav", testsignals::vowel (midiToHz (64), 2.0, 48000.0, 7));
    OspAudioProcessor p;
    CHECK (p.addLayers ({ a, b, c }) == 3);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.setParameterValue ("mix.x", 0.2f);
    p.setParameterValue ("mix.y", 0.1f);
    p.setParameterValue ("layerB.tune", 7.0f);
    p.setParameterValue ("layerC.pan", -40.0f);
    p.setParameterValue ("life", 77.0f);

    p.clearAllSamples();
    CHECK (p.occupiedLayerCount() == 0);
    CHECK (p.slotCount() == 3);
    CHECK (p.isKeptEmptySlot (1));
    CHECK (p.parameterValue ("mix.x") == Approx (0.2f));
    CHECK (p.parameterValue ("layerB.tune") == Approx (7.0f));
    CHECK (p.parameterValue ("layerC.pan") == Approx (-40.0f));
    CHECK (p.parameterValue ("life") == Approx (77.0f));

    // A drop on B's empty card (a replace) and a plain add (into A) keep the slots' settings.
    p.replaceLayer ({ c }, 1);
    CHECK (p.addLayers ({ a }) == 1);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    CHECK (p.isLayerOccupied (0));
    CHECK (p.isLayerOccupied (1));
    CHECK (! p.isLayerOccupied (2));
    CHECK (p.slotCount() == 3);
    CHECK (p.parameterValue ("layerB.tune") == Approx (7.0f));
    CHECK (p.parameterValue ("mix.x") == Approx (0.2f));

    // The kept slots survive a session round trip; removing the empty one closes it.
    juce::MemoryBlock state;
    p.getStateInformation (state);
    OspAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (restored.waitForLoads (30000));
    restored.pollLoads();
    CHECK (restored.slotCount() == 3);
    CHECK (restored.keptSlots() == 3);
    REQUIRE (p.removeLayer (2));
    CHECK (p.slotCount() == 2);
}

TEST_CASE ("plugin: kept slots keep the mix law - a refilled layer plays at its share", "[plugin][adaptive]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "a.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    auto render = [] (OspAudioProcessor& p) {
        p.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> audio (2, 512);
        double energy = 0.0;
        for (int block = 0; block < 40; ++block)
        {
            juce::MidiBuffer midi;
            if (block == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, static_cast<juce::uint8> (100)), 0);
            audio.clear();
            p.processBlock (audio, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    energy += static_cast<double> (audio.getSample (ch, i)) * audio.getSample (ch, i);
        }
        return energy;
    };
    std::array<double, 2> energy {};
    for (int kept = 0; kept < 2; ++kept)
    {
        OspAudioProcessor p;
        p.setParameterValue ("life", 0.0f);
        CHECK (p.addLayers ({ a }) == 1);
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        if (kept == 1)
            p.setKeptSlots (3);   // A alone in three slots: its third of the triangle (centre)
        energy[static_cast<std::size_t> (kept)] = render (p);
    }
    REQUIRE (energy[0] > 0.0);
    CHECK (energy[1] / energy[0] == Approx (1.0 / 3.0).margin (0.03));
}

TEST_CASE ("plugin: INIT, Reset settings and saved starting states", "[plugin][adaptive]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "a.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "b.wav", testsignals::vowel (midiToHz (61), 2.0, 48000.0, 5));
    OspAudioProcessor p;
    CHECK (p.addLayers ({ a, b }) == 2);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.setParameterValue ("life", 81.0f);
    p.setParameterValue ("space.decay", 4.0f);
    p.setParameterValue ("layerB.pan", 30.0f);
    p.setParameterValue ("ab.blend", 0.8f);

    // A starting state: settings and slots, no audio.
    const auto file = tmp.dir.getChildFile ("Mine.ospstate");
    REQUIRE (p.saveStartingState (file));
    const auto xml = juce::XmlDocument::parse (file);
    REQUIRE (xml != nullptr);
    CHECK (xml->getChildByName ("Instrument") == nullptr);
    CHECK (xml->getChildByName ("InstrumentB") == nullptr);
    CHECK (xml->getIntAttribute ("keptSlots") == 2);

    // Reset settings: defaults, the sounds stay and still meet in the middle.
    p.resetSettings();
    CHECK (p.occupiedLayerCount() == 2);
    CHECK (p.parameterValue ("life") == Approx (p.parameters.getParameter ("life")->convertFrom0to1 (p.parameters.getParameter ("life")->getDefaultValue())));
    CHECK (p.parameterValue ("ab.blend") == Approx (0.5f));
    CHECK (p.presetDisplayName() == "Reset");

    // INIT: nothing at all.
    p.initPatch();
    CHECK (p.occupiedLayerCount() == 0);
    CHECK (p.slotCount() == 0);
    CHECK (p.presetDisplayName() == "INIT");

    // Opening the state brings back its settings and two empty slots waiting for sounds.
    REQUIRE (p.loadStartingState (file));
    CHECK (p.occupiedLayerCount() == 0);
    CHECK (p.slotCount() == 2);
    CHECK (p.parameterValue ("life") == Approx (81.0f));
    CHECK (p.parameterValue ("space.decay") == Approx (4.0f));
    CHECK (p.parameterValue ("layerB.pan") == Approx (30.0f));
    CHECK (p.parameterValue ("ab.blend") == Approx (0.8f));
    CHECK (p.presetDisplayName() == "Mine");
    CHECK (p.addLayers ({ a, b }) == 2);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    CHECK (p.parameterValue ("layerB.pan") == Approx (30.0f));
    CHECK (p.parameterValue ("ab.blend") == Approx (0.8f));
}
