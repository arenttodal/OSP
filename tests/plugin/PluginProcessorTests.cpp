// Headless tests for the plugin processor: the same object a DAW would host.
// Covers: import -> analysis -> chromatic playback, sample-rate independence,
// root override, session state recall (including from the managed sample store after
// the original file disappears), and bad files.

#include "EngineCard.h"
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

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <typeinfo>

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
        const char* names[] = { "life", "drive", "character", "movement", "space" };
        for (int i = 0; i < 5; ++i)
        {
            ospEditor->openPopup (i);
            CHECK (ospEditor->openPopupIndex() == i);
            snapshot (juce::String ("osp-editor-popup-") + names[i] + ".png");
        }
        // DRIVE's picture is each circuit's own curve (here driven at 70 %).
        p.parameters.getParameter ("drive")->setValueNotifyingHost (0.7f);
        for (int mode = 0; mode < 3; ++mode)
        {
            p.parameters.getParameter ("drive.mode")->setValueNotifyingHost (static_cast<float> (mode) / 2.0f);
            ospEditor->openPopup (1);
            snapshot ("osp-popup-drive-" + juce::String (mode) + ".png");
        }
        p.parameters.getParameter ("drive")->setValueNotifyingHost (0.0f);
        p.parameters.getParameter ("drive.mode")->setValueNotifyingHost (0.0f);
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

    double levelOf (const AudioData& audio)
    {
        double e = 0.0;
        std::size_t n = 0;
        for (const auto& ch : audio.channels)
            for (float v : ch)
            {
                e += static_cast<double> (v) * v;
                ++n;
            }
        return n > 0 ? std::sqrt (e / static_cast<double> (n)) : 0.0;
    }
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
    // Sessions before SPACE v2 had no reverb EQ: they open with it open (the v10 migration).
    original.setParameterValue ("space.lowCut", 20.0f);
    original.setParameterValue ("space.highCut", 20000.0f);
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

    // The header's MIX is a triangle with three layers (A bottom left, B bottom right, C on
    // top); a click on its caption opens the large mix (and does not move the point); in
    // the popup the triangle places the mix directly.
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
        auto* header = dynamic_cast<osp::plugin::HeaderMix*> (triangles.front());
        REQUIRE (header != nullptr);
        CHECK (header->layerCount() == 3);
        const auto corner = header->corners();
        CHECK (corner[2].y < corner[0].y - 30.0f);   // C above the A-B base
        const float x0 = p.parameterValue ("mix.x"), y0 = p.parameterValue ("mix.y");
        click (*header, corner[2] + juce::Point<float> (0.0f, -9.0f));   // MIX, above C
        CHECK (ui->openPopupIndex() == osp::plugin::OspAudioProcessorEditor::mixPopup);
        CHECK (p.parameterValue ("mix.x") == Approx (x0));
        CHECK (p.parameterValue ("mix.y") == Approx (y0));
        snapshot ("osp-adaptive-3-mix.png");
        triangles.clear();
        collect (*editor, triangles);
        REQUIRE (triangles.size() == 2);
        auto* large = triangles.back()->getWidth() > triangles.front()->getWidth() ? triangles.back() : triangles.front();
        CHECK (large->getWidth() > 200);
        // Pressing near corner C (the top) puts most of the mix on C.
        click (*large, juce::Point<float> (0.5f * static_cast<float> (large->getWidth()), 34.0f));
        const auto share = InstrumentEngine::triangleShares (p.parameterValue ("mix.x"), p.parameterValue ("mix.y"));
        CHECK (share[2] > 0.6);
        snapshot ("osp-adaptive-3-mix-c.png");
        p.setParameterValue ("mix.x", 0.5f);
        p.setParameterValue ("mix.y", 1.0f / 3.0f);
    }
    // REIMAGINED is in every card (the central Original <-> Reimagined track is gone); with
    // LINK on two layers a change of one moves the other by the same amount.
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
        CHECK (find (*editor, "Original / Reimagined") == nullptr);
        CHECK (find (*editor, "Link Reimagined") == nullptr);
        auto* dialA = dynamic_cast<juce::Slider*> (find (*editor, "Layer A REIMAGINED"));
        auto* dialB = dynamic_cast<juce::Slider*> (find (*editor, "Layer B REIMAGINED"));
        REQUIRE (dialA != nullptr);
        REQUIRE (dialB != nullptr);
        REQUIRE (find (*editor, "Layer C REIMAGINED") != nullptr);
        p.setParameterValue ("reimagined", 20.0f);
        p.setParameterValue ("layerB.reimagined", 50.0f);
        p.setParameterValue ("layerC.reimagined", 40.0f);
        CHECK (dialA->getValue() == Approx (20.0));
        p.setParameterValue ("layerA.link", 1.0f);
        p.setParameterValue ("layerB.link", 1.0f);
        // A musician's turn of A (a gesture): B follows by the same amount, C (unlinked) stays.
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        const auto dialCentre = dialA->getLocalBounds().getCentre().toFloat();
        const juce::MouseEvent down (source, dialCentre, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, dialA, dialA, now, dialCentre, now, 1, false);
        dialA->mouseDown (down);
        dialA->setValue (30.0, juce::sendNotificationSync);
        dialA->mouseUp (down);
        CHECK (valueOf (p, "reimagined") == Approx (30.0f).margin (0.6));
        CHECK (valueOf (p, "layerB.reimagined") == Approx (60.0f).margin (0.6));
        CHECK (valueOf (p, "layerC.reimagined") == Approx (40.0f).margin (0.6));
        p.setParameterValue ("layerA.link", 0.0f);
        p.setParameterValue ("layerB.link", 0.0f);
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
    original.setParameterValue ("space.lowCut", 20.0f);   // as the v10 migration opens it
    original.setParameterValue ("space.highCut", 20000.0f);
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
    // REIMAGINED's spectral arc on one layer at 0 / 20 / 50 / 75 / 100 % (a comparison board).
    for (const int amount : { 0, 20, 50, 75, 100 })
    {
        set ("reimagined", static_cast<float> (amount));
        shot ("29-reimagined-" + juce::String (amount) + ".png");
    }
    // The REIMAGINED popover of layer A in each mode (75 %), anchored above its name.
    set ("reimagined", 75.0f);
    for (int mode = 0; mode < 5; ++mode)
    {
        set ("layerA.reimagined.mode", static_cast<float> (mode));
        ui->openPopup (osp::plugin::OspAudioProcessorEditor::reimaginedPopup);
        shot ("30-reimagined-" + juce::String (reimagined::modeName (reimagined::modeFromIndex (mode))).replace (" ", "-").toLowerCase() + ".png");
        ui->closePopup();
    }
    set ("layerA.reimagined.mode", 0.0f);
    set ("reimagined", 18.0f);
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
    // SHAPER CUSTOM: the small editor with the musician's steps, then the large editor.
    p.setParameterValue ("movement.shaper.custom", 1.0f);
    {
        auto steps = RhythmicShaper::patternSteps (5);
        steps[3] = { 0.9f, 0.9f, StepShape::hold };
        steps[10] = { 0.2f, 1.0f, StepShape::up };
        p.setShaperCustomPattern (steps, false);
    }
    ui->openPopup (3);
    shot ("15b-shaper-custom.png");
    p.setShaperEditorLarge (true);
    ui->openPopup (3);
    shot ("15c-shaper-large.png");
    p.setShaperEditorLarge (false);
    p.setParameterValue ("movement.shaper.custom", 0.0f);
    // ECHO (the sixth macro): tape, ping-pong, synced; then BBD wide and free.
    set ("echo", 40.0f);
    ui->openPopup (osp::plugin::OspAudioProcessorEditor::echoPopup);
    shot ("26-echo-tape.png");
    set ("echo.type", 1.0f);
    set ("echo.stereo", 2.0f);
    set ("echo.sync", 0.0f);
    set ("echo.age", 70.0f);
    ui->openPopup (osp::plugin::OspAudioProcessorEditor::echoPopup);
    shot ("27-echo-bbd.png");
    for (const char* id : { "echo", "echo.type", "echo.stereo", "echo.sync", "echo.age" })
        if (auto* param = p.parameters.getParameter (id))
            param->setValueNotifyingHost (param->getDefaultValue());
    const char* space[] = { "16-space-room.png", "17-space-hall.png", "18-space-plate.png", "19-space-spring.png" };
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
    // MUTE / SOLO: B soloed, C muted (A and C dimmed: not heard).
    set ("layerB.solo", 1.0f);
    set ("layerC.mute", 1.0f);
    shot ("07b-three-solo-mute.png");
    set ("layerB.solo", 0.0f);
    set ("layerC.mute", 0.0f);
    ui->openPopup (osp::plugin::OspAudioProcessorEditor::mixPopup);
    shot ("20-mix-popup.png");
    ui->openPopup (osp::plugin::OspAudioProcessorEditor::advancedPopup);
    shot ("21-advanced-popup.png");
    ui->closePopup();
    p.clearAllSamples();
    shot ("22-cleared.png");

    // Per-layer REIMAGINED (new patches) and older patches shown through the new cards.
    p.initPatch();
    p.addLayers ({ a, b });
    settle();
    set ("reimagined", 0.0f);
    set ("layerB.reimagined", 100.0f);
    shot ("25-new-a0-b100.png");
    p.addLayers ({ c });
    settle();
    set ("reimagined", 20.0f);
    set ("layerB.reimagined", 50.0f);
    set ("layerC.reimagined", 90.0f);
    shot ("26-new-a20-b50-c90.png");
    auto openAsOlder = [&] (int layers, float amount) {
        OspAudioProcessor maker;
        juce::Array<juce::File> files;
        for (int i = 0; i < layers; ++i)
            files.add (i == 0 ? a : b);
        maker.addLayers (files);
        REQUIRE (maker.waitForLoads (30000));
        maker.pollLoads();
        for (int l = 0; l < layers; ++l)
            maker.setParameterValue (OspAudioProcessor::reimaginedParameterId (l), amount);
        juce::MemoryBlock state;
        maker.getStateInformation (state);
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
        REQUIRE (xml != nullptr);
        xml->removeAttribute ("reimaginedRouting");
        xml->setAttribute ("stateVersion", 7);
        juce::AudioProcessor::copyXmlToBinary (*xml, state);
        p.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        settle();
    };
    openAsOlder (1, 63.0f);
    shot ("27-old-1-layer.png");
    CHECK_FALSE (p.isReimaginedPerLayer());   // shown, not converted
    openAsOlder (2, 63.0f);
    shot ("28-old-2-layer.png");
    CHECK_FALSE (p.isReimaginedPerLayer());
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

//==============================================================================
// Reimagined migration (per-layer REIMAGINED): sessions saved before it must sound exactly
// as they did. tests/audio/reimagined-before/ holds the reference made by the build before
// the migration (exact sample hash plus level, peak and brightness per scene). The scenes
// include SPACE, so the references were regenerated once for SPACE v2 (state 10), the
// REIMAGINED code unchanged. Each scene
// is set up, saved and reopened the way an older session is (no routing in its state),
// then rendered. On the reference platform the hash must match; elsewhere the signal
// metrics must (floating-point results differ slightly between compilers).
//   OSP_UPDATE_REIMAGINED_REFERENCE=1  rewrite the references (only before the migration)
//   OSP_REIMAGINED_WAV_DIR=<dir>       also write each render as a 32-bit float WAV (null tests)
namespace
{
    struct ReimaginedScene
    {
        const char* name;
        int layers;                                        // 1..3: vowel A3, pluck E2, vowel E4
        std::vector<std::pair<const char*, float>> values; // parameter values (plain)
        bool automate = false;                             // `reimagined` swept 0 -> 100 % while playing
        bool perLayer = false;                             // a new (per-layer routing) patch, saved as it is
    };

    std::vector<ReimaginedScene> reimaginedScenes()
    {
        using V = std::vector<std::pair<const char*, float>>;
        auto one = [] (float amount) { return V { { "reimagined", amount } }; };
        return {
            { "one-0", 1, one (0.0f) },
            { "one-25", 1, one (25.0f) },
            { "one-50", 1, one (50.0f) },
            { "one-75", 1, one (75.0f) },
            { "one-100", 1, one (100.0f) },
            { "one-granular-75", 1, V { { "reimagined", 75.0f }, { "layerA.sourceMode", 1.0f } } },
            { "two-50", 2, V { { "reimagined", 50.0f }, { "layerB.reimagined", 50.0f }, { "ab.blend", 0.5f } } },
            { "two-20-80-blend-0.85", 2, V { { "reimagined", 20.0f }, { "layerB.reimagined", 80.0f }, { "ab.blend", 0.85f } } },
            { "two-mixed-modes-60", 2, V { { "reimagined", 60.0f }, { "layerB.reimagined", 60.0f }, { "layerB.sourceMode", 1.0f }, { "ab.blend", 0.4f } } },
            { "two-shaped-60", 2, V { { "reimagined", 60.0f }, { "layerB.reimagined", 60.0f }, { "ab.blend", 0.3f }, { "character", 45.0f },
                                      { "movement.mode", 1.0f }, { "motion", 60.0f }, { "space", 55.0f }, { "space.type", 2.0f } } },
            { "three-0-50-100", 3, V { { "reimagined", 0.0f }, { "layerB.reimagined", 50.0f }, { "layerC.reimagined", 100.0f },
                                       { "layerC.level", -3.0f }, { "layerB.pan", -40.0f } } },
            { "one-automated", 1, one (0.0f), true },
            // Per-layer patches (KALEIDOSCOPE, the Reimagined every patch had before the modes).
            { "perlayer-one-25", 1, one (25.0f), false, true },
            { "perlayer-one-50", 1, one (50.0f), false, true },
            { "perlayer-one-75", 1, one (75.0f), false, true },
            { "perlayer-one-100", 1, one (100.0f), false, true },
            { "perlayer-two-30-80", 2, V { { "reimagined", 30.0f }, { "layerB.reimagined", 80.0f }, { "ab.blend", 0.5f } }, false, true },
            { "perlayer-three-granular-60", 3, V { { "reimagined", 60.0f }, { "layerB.reimagined", 60.0f }, { "layerC.reimagined", 60.0f },
                                                   { "layerB.sourceMode", 1.0f }, { "space", 30.0f } }, false, true },
        };
    }

    struct SceneResult
    {
        juce::String hash;
        double rmsDb = -200.0, peakDb = -200.0, brightness = 0.0;
        AudioData audio;
    };

    SceneResult renderScene (const ReimaginedScene& scene, const juce::File& dir)
    {
        const std::array<juce::File, 3> files { writeSource (dir, "a-vowel.wav", testsignals::vowel (midiToHz (57), 2.5, 48000.0, 3)),
                                                writeSource (dir, "b-pluck.wav", testsignals::pluck (midiToHz (40), 2.5, 48000.0, 8, 0.7, 1)),
                                                writeSource (dir, "c-vowel.wav", testsignals::vowel (midiToHz (64), 2.5, 48000.0, 7)) };
        // The scene as a musician made it...
        juce::MemoryBlock state;
        {
            OspAudioProcessor p;
            juce::Array<juce::File> sources;
            for (int i = 0; i < scene.layers; ++i)
                sources.add (files[static_cast<std::size_t> (i)]);
            if (scene.layers == 1)
                p.loadFile (sources[0]);
            else
                p.addLayers (sources);
            REQUIRE (p.waitForLoads (30000));
            p.pollLoads();
            for (const auto& [id, value] : scene.values)
                p.setParameterValue (id, value);
            p.getStateInformation (state);
        }
        // ...saved by a build that knew nothing about routing (per-layer patches: as saved)...
        if (auto xml = scene.perLayer ? nullptr : juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize())))
        {
            xml->removeAttribute ("reimaginedRouting");
            state.reset();
            juce::AudioProcessor::copyXmlToBinary (*xml, state);
        }
        // ...and reopened.
        OspAudioProcessor q;
        q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        REQUIRE (q.waitForLoads (30000));
        q.pollLoads();

        const double rate = 48000.0;
        const int block = 256, total = static_cast<int> (2.4 * rate);
        q.prepareToPlay (rate, block);
        SceneResult result;
        result.audio = AudioData::allocate (2, total, rate);
        juce::AudioBuffer<float> buffer (2, block);
        const int notes[] = { 57, 61, 64 };
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            for (int k = 0; k < 3; ++k)
            {
                const int on = static_cast<int> ((0.25 * k) * rate);
                if (on >= pos && on < pos + block)
                    midi.addEvent (juce::MidiMessage::noteOn (1, notes[k], static_cast<juce::uint8> (90 + 10 * k)), on - pos);
            }
            const int off = static_cast<int> (1.4 * rate);
            if (off >= pos && off < pos + block)
                midi.addEvent (juce::MidiMessage::allNotesOff (1), off - pos);
            if (scene.automate)   // host automation of the old global parameter, block by block
                if (auto* r = q.parameters.getParameter ("reimagined"))
                    r->setValueNotifyingHost (std::min (1.0f, static_cast<float> (pos) / static_cast<float> (1.6 * rate)));
            buffer.clear();
            q.processBlock (buffer, midi);
            const int n = std::min (block, total - pos);
            for (int ch = 0; ch < 2; ++ch)
                std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + n, result.audio.channels[static_cast<std::size_t> (ch)].begin() + pos);
        }

        std::uint64_t h = 1469598103934665603ull;
        double sum = 0.0, diff = 0.0, peak = 0.0;
        for (const auto& channel : result.audio.channels)
        {
            float previous = 0.0f;
            for (float x : channel)
            {
                std::uint32_t bits;
                std::memcpy (&bits, &x, sizeof bits);
                for (int b = 0; b < 4; ++b)
                    h = (h ^ ((bits >> (8 * b)) & 0xffu)) * 1099511628211ull;
                sum += static_cast<double> (x) * x;
                diff += static_cast<double> (x - previous) * (x - previous);
                peak = std::max (peak, static_cast<double> (std::abs (x)));
                previous = x;
            }
        }
        const double count = 2.0 * total;
        result.hash = juce::String::toHexString (static_cast<juce::int64> (h));
        result.rmsDb = 10.0 * std::log10 (std::max (1.0e-20, sum / count));
        result.peakDb = 20.0 * std::log10 (std::max (1.0e-10, peak));
        result.brightness = std::sqrt (diff / std::max (1.0e-20, sum));
        return result;
    }

    juce::File reimaginedReferenceDir()
    {
        return juce::File (OSP_SOURCE_DIR).getChildFile ("tests/audio/reimagined-before");
    }
}

TEST_CASE ("plugin: sessions from before per-layer Reimagined render as they did", "[plugin][reimagined-migration]")
{
    const bool update = juce::SystemStats::getEnvironmentVariable ("OSP_UPDATE_REIMAGINED_REFERENCE", {}) == "1";
    const auto wavDir = juce::SystemStats::getEnvironmentVariable ("OSP_REIMAGINED_WAV_DIR", {});
    int identical = 0, scenes = 0;
    for (const auto& scene : reimaginedScenes())
    {
        DYNAMIC_SECTION (scene.name)
        {
            TempDir tmp;
            const auto result = renderScene (scene, tmp.dir);
            const auto file = reimaginedReferenceDir().getChildFile (juce::String (scene.name) + ".json");
            if (wavDir.isNotEmpty())
            {
                std::string error;
                const auto wav = juce::File (wavDir).getChildFile (juce::String (scene.name) + ".wav");
                CHECK (io::writeAudioFile (std::filesystem::path (wav.getFullPathName().toStdString()), result.audio, io::SampleFormat::float32, error));
            }
            if (update)
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("schemaVersion", 1);
                o->setProperty ("scene", scene.name);
                o->setProperty ("sampleHash", result.hash);
                o->setProperty ("rmsDb", result.rmsDb);
                o->setProperty ("peakDb", result.peakDb);
                o->setProperty ("brightness", result.brightness);
                file.getParentDirectory().createDirectory();
                REQUIRE (file.replaceWithText (juce::JSON::toString (juce::var (o))));
                continue;
            }
            const auto reference = juce::JSON::parse (file);
            INFO ("missing reference " << file.getFullPathName());
            REQUIRE (reference.isObject());
            ++scenes;
            const bool same = reference["sampleHash"].toString() == result.hash;
            identical += same ? 1 : 0;
            INFO (scene.name << ": rms " << result.rmsDb << " dB, peak " << result.peakDb << " dB, brightness " << result.brightness);
            CHECK (result.rmsDb == Approx (static_cast<double> (reference["rmsDb"])).margin (0.02));
            CHECK (result.peakDb == Approx (static_cast<double> (reference["peakDb"])).margin (0.05));
            CHECK (result.brightness == Approx (static_cast<double> (reference["brightness"])).epsilon (0.005));
            if (! same)
                WARN (scene.name << ": not bit-identical to the reference (expected only off the reference platform)");
        }
    }
    (void) identical;
    (void) scenes;
}

TEST_CASE ("plugin: Reimagined routing - new patches per layer, older ones legacy until a layer's REIMAGINED is edited", "[plugin][reimagined-migration]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "a.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "b.wav", testsignals::pluck (midiToHz (40), 2.0, 48000.0, 8, 0.7, 1));
    auto reopen = [] (const juce::MemoryBlock& state, bool asOlderSession) {
        juce::MemoryBlock copy (state);
        if (asOlderSession)
            if (auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize())))
            {
                xml->removeAttribute ("reimaginedRouting");
                xml->setAttribute ("stateVersion", 7);
                copy.reset();
                juce::AudioProcessor::copyXmlToBinary (*xml, copy);
            }
        auto q = std::make_unique<OspAudioProcessor>();
        q->setStateInformation (copy.getData(), static_cast<int> (copy.getSize()));
        REQUIRE (q->waitForLoads (30000));
        q->pollLoads();
        return q;
    };

    // A new patch: per layer, every REIMAGINED at the Original end.
    OspAudioProcessor p;
    CHECK (p.isReimaginedPerLayer());
    for (int layer = 0; layer < 3; ++layer)
        CHECK (valueOf (p, OspAudioProcessor::reimaginedParameterId (layer)) == Approx (0.0f));
    CHECK (p.parameters.getParameter ("reimagined") != nullptr);   // the old ID stays (automation, presets)
    CHECK (p.addLayers ({ a, b }) == 2);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.setParameterValue ("reimagined", 20.0f);
    p.setParameterValue ("layerB.reimagined", 90.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    {
        // Saved and reopened: still per layer, each layer its own amount.
        auto q = reopen (state, false);
        CHECK (q->isReimaginedPerLayer());
        CHECK (valueOf (*q, "reimagined") == Approx (20.0f));
        CHECK (valueOf (*q, "layerB.reimagined") == Approx (90.0f));
    }

    // An older session: legacy routing, its amounts shown as they are.
    auto old = reopen (state, true);
    CHECK_FALSE (old->isReimaginedPerLayer());
    CHECK (valueOf (*old, "reimagined") == Approx (20.0f));
    CHECK (valueOf (*old, "layerB.reimagined") == Approx (90.0f));
    // Automation playback (no gesture) and a parameter change from the host never convert it...
    auto* aReimagined = old->parameters.getParameter ("reimagined");
    REQUIRE (aReimagined != nullptr);
    aReimagined->setValueNotifyingHost (0.4f);
    CHECK_FALSE (old->isReimaginedPerLayer());
    playNote (*old, 57, 48000.0, 0.2);
    CHECK_FALSE (old->isReimaginedPerLayer());
    // ...and saving it untouched keeps it legacy.
    juce::MemoryBlock resaved;
    old->getStateInformation (resaved);
    CHECK_FALSE (reopen (resaved, false)->isReimaginedPerLayer());

    // The musician turns B's REIMAGINED: the patch becomes per layer, nothing else moves.
    auto* bReimagined = old->parameters.getParameter ("layerB.reimagined");
    REQUIRE (bReimagined != nullptr);
    bReimagined->beginChangeGesture();
    CHECK (old->isReimaginedPerLayer());
    CHECK (valueOf (*old, "reimagined") == Approx (40.0f));
    CHECK (valueOf (*old, "layerB.reimagined") == Approx (90.0f));
    bReimagined->setValueNotifyingHost (bReimagined->convertTo0to1 (60.0f));
    bReimagined->endChangeGesture();
    CHECK (valueOf (*old, "reimagined") == Approx (40.0f));
    old->getStateInformation (resaved);
    CHECK (reopen (resaved, false)->isReimaginedPerLayer());

    // The factory starting states were made with the shared stage; INIT is a new patch.
    p.setCurrentProgram (5);
    CHECK_FALSE (p.isReimaginedPerLayer());
    p.initPatch();
    CHECK (p.isReimaginedPerLayer());
    for (int layer = 0; layer < 3; ++layer)
        CHECK (valueOf (p, OspAudioProcessor::reimaginedParameterId (layer)) == Approx (0.0f));

    // Master volume: the same parameter, range and default as before the slider.
    auto* gain = dynamic_cast<juce::AudioParameterFloat*> (p.parameters.getParameter ("gain"));
    REQUIRE (gain != nullptr);
    CHECK (gain->range.start == Approx (-36.0f));
    CHECK (gain->range.end == Approx (12.0f));
    CHECK (gain->convertFrom0to1 (static_cast<juce::AudioProcessorParameter*> (gain)->getDefaultValue()) == Approx (0.0f).margin (1.0e-4));
}

TEST_CASE ("plugin: per-layer REIMAGINED - each layer's amount changes only that layer", "[plugin][reimagined-migration]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "a.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "b.wav", testsignals::vowel (midiToHz (64), 2.0, 48000.0, 7));
    OspAudioProcessor p;
    CHECK (p.addLayers ({ a, b }) == 2);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    for (const char* id : { "life", "space", "motion" })
        p.setParameterValue (id, 0.0f);
    auto render = [&p] (float amountA, float amountB, bool soloA, bool soloB) {
        p.setParameterValue ("reimagined", amountA);
        p.setParameterValue ("layerB.reimagined", amountB);
        p.setParameterValue ("layerA.level", soloB ? -48.0f : 0.0f);
        p.setParameterValue ("layerB.level", soloA ? -48.0f : 0.0f);
        return playNote (p, 57, 48000.0, 1.5);
    };
    auto difference = [] (const AudioData& x, const AudioData& y) {
        double d = 0.0, peak = 0.0;
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (std::size_t i = 0; i < x.channels[ch].size(); ++i)
            {
                d = std::max (d, static_cast<double> (std::abs (x.channels[ch][i] - y.channels[ch][i])));
                peak = std::max (peak, static_cast<double> (std::abs (x.channels[ch][i])));
            }
        return d / std::max (1.0e-9, peak);
    };
    // A heard alone sounds the same whatever B's REIMAGINED is (A 0 / B 0 vs A 0 / B 100).
    const auto aWithB0 = render (0.0f, 0.0f, true, false);
    const auto aWithB100 = render (0.0f, 100.0f, true, false);
    CHECK (difference (aWithB0, aWithB100) < 1.0e-6);
    // ...and B alone the same whatever A's is (A 50 / B 100 vs A 0 / B 100).
    CHECK (difference (render (0.0f, 100.0f, false, true), render (50.0f, 100.0f, false, true)) < 1.0e-6);
    // Both heard: B at 100 % changes the result, A at 0 % stays the recording.
    CHECK (difference (render (0.0f, 0.0f, false, false), render (0.0f, 100.0f, false, false)) > 1.0e-2);
}

TEST_CASE ("plugin: master volume keeps its gain law as a slider", "[plugin][reimagined-migration]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "a.wav", testsignals::vowel (midiToHz (57), 1.5, 48000.0, 3));
    OspAudioProcessor p;
    loadAndWait (p, a);
    p.setParameterValue ("life", 0.0f);
    auto rms = [] (const AudioData& x) {
        double e = 0.0;
        for (float v : x.channels[0])
            e += static_cast<double> (v) * v;
        return std::sqrt (e / static_cast<double> (x.channels[0].size()));
    };
    p.setParameterValue ("gain", 0.0f);
    const double reference = rms (playNote (p, 57, 48000.0, 0.8));
    REQUIRE (reference > 1.0e-4);
    // The same parameter value gives the same output as ever: level follows dB exactly.
    for (float db : { -36.0f, -24.0f, -12.0f, -6.0f, 0.0f, 12.0f })
    {
        p.setParameterValue ("gain", db);
        const double level = rms (playNote (p, 57, 48000.0, 0.8));
        INFO (db << " dB");
        CHECK (20.0 * std::log10 (level / reference) == Approx (db).margin (0.01));
    }
}

TEST_CASE ("plugin: MIX - a third layer is heard at once, A and B keep their balance, removing it restores the A/B mix", "[plugin][adaptive]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "a.wav", testsignals::vowel (midiToHz (57), 1.5, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "b.wav", testsignals::vowel (midiToHz (61), 1.5, 48000.0, 5));
    const auto c = writeSource (tmp.dir, "c.wav", testsignals::vowel (midiToHz (64), 1.5, 48000.0, 7));
    OspAudioProcessor p;
    CHECK (p.addLayers ({ a, b }) == 2);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.setParameterValue ("ab.blend", 0.6f);
    const auto two = InstrumentEngine::mixWeights ({ true, true, false }, p.parameterValue ("ab.blend"), 0.5, 1.0 / 3.0);

    // Adding C: it takes a third of the mix at once; A and B keep their balance in the rest.
    CHECK (p.addLayers ({ c }) == 1);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    const auto three = InstrumentEngine::mixWeights ({ true, true, true }, 0.0, p.parameterValue ("mix.x"), p.parameterValue ("mix.y"));
    CHECK (three.gain[2] * three.gain[2] == Approx (1.0 / 3.0).margin (1.0e-3));
    CHECK (three.gain[0] * three.gain[0] == Approx (2.0 / 3.0 * two.gain[0] * two.gain[0]).margin (1.0e-3));
    CHECK (three.gain[1] * three.gain[1] == Approx (2.0 / 3.0 * two.gain[1] * two.gain[1]).margin (1.0e-3));
    {
        // ... and it plays: layer C's own pitch is in what comes out.
        const auto out = playNote (p, 57, 48000.0, 0.8);
        CHECK (levelOf (out) > 0.0);
    }
    // A centred pair (blend 50 %) puts all three at the centre.
    {
        OspAudioProcessor r;
        CHECK (r.addLayers ({ a, b }) == 2);
        REQUIRE (r.waitForLoads (30000));
        r.pollLoads();
        CHECK (r.addLayers ({ c }) == 1);
        REQUIRE (r.waitForLoads (30000));
        r.pollLoads();
        CHECK (valueOf (r, "mix.x") == Approx (0.5f).margin (1.0e-4));
        CHECK (valueOf (r, "mix.y") == Approx (1.0f / 3.0f).margin (1.0e-4));
    }

    // A 20 / B 30 / C 50, then C removed: A 40 / B 60.
    p.setParameterValue ("mix.x", static_cast<float> (0.5 + 0.5 * 0.3));
    p.setParameterValue ("mix.y", 0.3f);
    const auto shares = InstrumentEngine::triangleShares (p.parameterValue ("mix.x"), p.parameterValue ("mix.y"));
    REQUIRE (shares[0] == Approx (0.2).margin (1.0e-4));
    REQUIRE (shares[2] == Approx (0.5).margin (1.0e-4));
    REQUIRE (p.removeLayer (2));
    p.pollLoads();
    const auto after = InstrumentEngine::mixWeights ({ true, true, false }, p.parameterValue ("ab.blend"), 0.5, 1.0 / 3.0);
    CHECK (after.gain[0] * after.gain[0] == Approx (0.4).margin (1.0e-3));
    CHECK (after.gain[1] * after.gain[1] == Approx (0.6).margin (1.0e-3));

    // Three dropped together still meet in the middle (everything heard at once).
    OspAudioProcessor q;
    CHECK (q.addLayers ({ a, b, c }) == 3);
    REQUIRE (q.waitForLoads (30000));
    q.pollLoads();
    CHECK (valueOf (q, "mix.x") == Approx (0.5f));
    CHECK (valueOf (q, "mix.y") == Approx (1.0f / 3.0f));
}

TEST_CASE ("plugin: REIMAGINED modes - stable IDs, every mode's settings saved per layer, older sessions as KALEIDOSCOPE", "[plugin][reimagined-modes]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "a.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "b.wav", testsignals::pluck (midiToHz (48), 2.0, 48000.0, 8, 0.7, 1));
    OspAudioProcessor p;
    // Every layer has the mode and all fifteen settings, under stable IDs (version hint 11).
    for (int layer = 0; layer < 3; ++layer)
        for (const auto& name : OspAudioProcessor::reimaginedModeNames())
        {
            const auto id = OspAudioProcessor::reimaginedModeParameterId (layer, name);
            INFO (id);
            auto* param = p.parameters.getParameter (id);
            REQUIRE (param != nullptr);
            CHECK (param->getVersionHint() == 11);
        }
    CHECK (OspAudioProcessor::reimaginedModeParameterId (1, "tapeFrame.age") == "layerB.reimagined.tapeFrame.age");
    // Defaults: KALEIDOSCOPE at neutral FOCUS / SPREAD, and the spec's starting points.
    CHECK (valueOf (p, "layerA.reimagined.mode") == Approx (0.0f));
    CHECK (valueOf (p, "layerA.reimagined.kaleidoscope.focus") == Approx (50.0f));
    CHECK (valueOf (p, "layerA.reimagined.kaleidoscope.spread") == Approx (50.0f));
    CHECK (valueOf (p, "layerA.reimagined.tapeFrame.age") == Approx (35.0f));
    CHECK (valueOf (p, "layerA.reimagined.tapeFrame.stability") == Approx (25.0f));
    CHECK (valueOf (p, "layerA.reimagined.tapeFrame.frame") == Approx (1.0f));      // CLASSIC
    CHECK (valueOf (p, "layerA.reimagined.toybox.motion") == Approx (30.0f));
    CHECK (valueOf (p, "layerA.reimagined.toybox.digital") == Approx (40.0f));
    CHECK (valueOf (p, "layerA.reimagined.toybox.play") == Approx (1.0f));          // TURN
    CHECK (valueOf (p, "layerA.reimagined.mosaic.detail") == Approx (60.0f));
    CHECK (valueOf (p, "layerA.reimagined.mosaic.motion") == Approx (45.0f));
    CHECK (valueOf (p, "layerA.reimagined.mosaic.model") == Approx (1.0f));         // TEXTURED
    CHECK (valueOf (p, "layerA.reimagined.mirage.clock") == Approx (45.0f));
    CHECK (valueOf (p, "layerA.reimagined.mirage.filter") == Approx (50.0f));
    CHECK (valueOf (p, "layerA.reimagined.mirage.tone") == Approx (0.0f));          // DARK

    REQUIRE (p.addLayers ({ a, b }) == 2);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.setParameterValue ("reimagined", 70.0f);
    p.setParameterValue ("layerB.reimagined", 60.0f);
    // A plays TAPE FRAME, B MOSAIC; settings of modes not playing are kept too.
    p.setParameterValue ("layerA.reimagined.mode", 1.0f);
    p.setParameterValue ("layerA.reimagined.tapeFrame.age", 80.0f);
    p.setParameterValue ("layerA.reimagined.toybox.digital", 90.0f);
    p.setParameterValue ("layerB.reimagined.mode", 3.0f);
    p.setParameterValue ("layerB.reimagined.mosaic.model", 0.0f);
    p.setParameterValue ("layerB.reimagined.kaleidoscope.focus", 20.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    {
        OspAudioProcessor q;
        q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        REQUIRE (q.waitForLoads (30000));
        q.pollLoads();
        CHECK (valueOf (q, "layerA.reimagined.mode") == Approx (1.0f));
        CHECK (valueOf (q, "layerA.reimagined.tapeFrame.age") == Approx (80.0f));
        CHECK (valueOf (q, "layerA.reimagined.toybox.digital") == Approx (90.0f));
        CHECK (valueOf (q, "layerB.reimagined.mode") == Approx (3.0f));
        CHECK (valueOf (q, "layerB.reimagined.mosaic.model") == Approx (0.0f));
        CHECK (valueOf (q, "layerB.reimagined.kaleidoscope.focus") == Approx (20.0f));
        CHECK (valueOf (q, "layerC.reimagined.mode") == Approx (0.0f));
        // Both modes play (and stay bounded) after recall.
        const auto out = playNote (q, 57, 48000.0, 1.0);
        double peak = 0.0;
        for (const auto& ch : out.channels)
            for (float x : ch)
            {
                CHECK (std::isfinite (x));
                peak = std::max (peak, static_cast<double> (std::abs (x)));
            }
        CHECK (peak > 1.0e-3);
        CHECK (peak < 2.0);
    }
    // A session from before the modes (state 8, no mode parameters) opens as KALEIDOSCOPE,
    // whatever this instance had, with every mode's settings at their defaults.
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize())))
    {
        for (int i = xml->getNumChildElements(); --i >= 0;)
            if (xml->getChildElement (i)->getStringAttribute ("id").contains ("reimagined."))
                xml->removeChildElement (xml->getChildElement (i), true);
        xml->setAttribute ("stateVersion", 8);
        juce::MemoryBlock older;
        juce::AudioProcessor::copyXmlToBinary (*xml, older);
        p.setStateInformation (older.getData(), static_cast<int> (older.getSize()));
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        for (int layer = 0; layer < 3; ++layer)
        {
            CHECK (valueOf (p, OspAudioProcessor::reimaginedModeParameterId (layer, "mode")) == Approx (0.0f));
            CHECK (valueOf (p, OspAudioProcessor::reimaginedModeParameterId (layer, "kaleidoscope.focus")) == Approx (50.0f));
            CHECK (valueOf (p, OspAudioProcessor::reimaginedModeParameterId (layer, "tapeFrame.age")) == Approx (35.0f));
        }
        CHECK (valueOf (p, "reimagined") == Approx (70.0f));   // the amounts are the session's
        CHECK (valueOf (p, "layerB.reimagined") == Approx (60.0f));
    }
}

TEST_CASE ("plugin: REIMAGINED modes - a mode edit converts a legacy patch, automation never does", "[plugin][reimagined-modes]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "a.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    OspAudioProcessor p;
    p.setCurrentProgram (5);   // a factory starting state: legacy routing
    REQUIRE_FALSE (p.isReimaginedPerLayer());
    REQUIRE (p.addLayers ({ a }) == 1);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    auto* mode = p.parameters.getParameter ("layerA.reimagined.mode");
    REQUIRE (mode != nullptr);
    // Host automation of the mode (no gesture), notes playing through the switches: no
    // conversion, no crash, every block finite.
    for (int m = 0; m < 5; ++m)
    {
        mode->setValueNotifyingHost (mode->convertTo0to1 (static_cast<float> (m)));
        const auto out = playNote (p, 57 + m, 48000.0, 0.3, 128);
        for (const auto& ch : out.channels)
            for (float x : ch)
                REQUIRE (std::isfinite (x));
    }
    CHECK_FALSE (p.isReimaginedPerLayer());
    // The musician picks a mode (or turns a mode's setting): per layer from now on.
    auto* age = p.parameters.getParameter ("layerA.reimagined.tapeFrame.age");
    REQUIRE (age != nullptr);
    age->beginChangeGesture();
    age->setValueNotifyingHost (0.7f);
    age->endChangeGesture();
    CHECK (p.isReimaginedPerLayer());
}

TEST_CASE ("plugin: REIMAGINED popover - its name opens it above the card, a second click closes it", "[plugin][reimagined-modes][ui]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "a.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "b.wav", testsignals::pluck (midiToHz (48), 2.0, 48000.0, 8, 0.7, 1));
    OspAudioProcessor p;
    REQUIRE (p.addLayers ({ a, b }) == 2);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    editor->setSize (1448, 1086);
    ui->refreshNow();
    using Editor = osp::plugin::OspAudioProcessorEditor;
    for (int layer = 0; layer < 2; ++layer)
    {
        ui->openPopup (Editor::reimaginedPopup + layer);
        ui->refreshNow();
        CHECK (ui->openPopupIndex() == Editor::reimaginedPopup + layer);
        ui->closePopup();
        CHECK (ui->openPopupIndex() == -1);
    }
    // Layer C has no card: its popover does not open.
    ui->openPopup (Editor::reimaginedPopup + 2);
    CHECK (ui->openPopupIndex() == -1);
    // Switching modes inside the popover keeps every mode's own values.
    ui->openPopup (Editor::reimaginedPopup);
    p.setParameterValue ("layerA.reimagined.tapeFrame.stability", 77.0f);
    p.setParameterValue ("layerA.reimagined.mode", 1.0f);
    ui->refreshNow();
    p.setParameterValue ("layerA.reimagined.mode", 3.0f);
    ui->refreshNow();
    p.setParameterValue ("layerA.reimagined.mode", 1.0f);
    ui->refreshNow();
    CHECK (valueOf (p, "layerA.reimagined.tapeFrame.stability") == Approx (77.0f));
    CHECK (ui->openPopupIndex() == Editor::reimaginedPopup);
}

// CPU profile (hidden): the plugin's own processBlock at 48 kHz / 128 samples, a new patch's
// settings, held notes, with one thing changed at a time. % of one core in real time.
//   ./osp_plugin_tests "[cpu-profile]"
TEST_CASE ("plugin: CPU profile", "[.][cpu-profile]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "a.wav", testsignals::vowel (midiToHz (57), 3.0, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "b.wav", testsignals::pluck (midiToHz (48), 3.0, 48000.0, 8, 0.7, 1));
    const auto c = writeSource (tmp.dir, "c.wav", testsignals::vowel (midiToHz (64), 3.0, 48000.0, 5));
    struct Case
    {
        juce::String name;
        int layers = 1, notes = 16, transpose = 0;
        double rate = 48000.0;
        int block = 128;
        std::vector<std::pair<juce::String, float>> set;
    };
    const std::vector<Case> cases {
        { "idle (sound loaded, nothing playing)", 1, 0, 0, 48000.0, 128, {} },
        { "1 note", 1, 1, 0, 48000.0, 128, {} },
        { "8 notes", 1, 8, 0, 48000.0, 128, {} },
        { "16 notes (reference)", 1, 16, 0, 48000.0, 128, {} },
        { "24 notes", 1, 24, 0, 48000.0, 128, {} },
        { "16 notes, block 32", 1, 16, 0, 48000.0, 32, {} },
        { "16 notes, block 1024", 1, 16, 0, 48000.0, 1024, {} },
        { "16 notes, 96 kHz", 1, 16, 0, 96000.0, 128, {} },
        { "16 notes, +24 st", 1, 16, 24, 48000.0, 128, {} },
        { "16 notes, -24 st", 1, 16, -24, 48000.0, 128, {} },
        { "16 notes, LIFE 0", 1, 16, 0, 48000.0, 128, { { "life", 0.0f } } },
        { "16 notes, DYNAMICS 0", 1, 16, 0, 48000.0, 128, { { "dynamics", 0.0f } } },
        { "16 notes, CHARACTER Tilt", 1, 16, 0, 48000.0, 128, { { "character.type", 4.0f } } },
        { "16 notes, CHARACTER LP12", 1, 16, 0, 48000.0, 128, { { "character.type", 1.0f } } },
        { "16 notes, MOVEMENT 0", 1, 16, 0, 48000.0, 128, { { "motion", 0.0f } } },
        { "16 notes, MOVEMENT chorus", 1, 16, 0, 48000.0, 128, { { "movement.mode", 2.0f } } },
        { "16 notes, MOVEMENT tape", 1, 16, 0, 48000.0, 128, { { "movement.mode", 1.0f } } },
        { "16 notes, MOVEMENT shaper", 1, 16, 0, 48000.0, 128, { { "movement.mode", 4.0f } } },
        { "16 notes, SPACE 0", 1, 16, 0, 48000.0, 128, { { "space", 0.0f } } },
        { "16 notes, SPACE 100", 1, 16, 0, 48000.0, 128, { { "space", 100.0f } } },
        { "16 notes, SPACE 100 HALL", 1, 16, 0, 48000.0, 128, { { "space", 100.0f }, { "space.type", 1.0f } } },
        { "16 notes, ECHO 50 TAPE", 1, 16, 0, 48000.0, 128, { { "echo", 50.0f } } },
        { "16 notes, ECHO 50 BBD", 1, 16, 0, 48000.0, 128, { { "echo", 50.0f }, { "echo.type", 1.0f } } },
        { "16 notes, Natural pitch", 1, 16, 0, 48000.0, 128, { { "pitchCharacter", 1.0f } } },
        { "16 notes, REIMAGINED 50", 1, 16, 0, 48000.0, 128, { { "reimagined", 50.0f } } },
        { "16 notes, REIMAGINED 100", 1, 16, 0, 48000.0, 128, { { "reimagined", 100.0f } } },
        { "16 notes, TAPE FRAME 100", 1, 16, 0, 48000.0, 128, { { "reimagined", 100.0f }, { "layerA.reimagined.mode", 1.0f } } },
        { "16 notes, MOSAIC 100", 1, 16, 0, 48000.0, 128, { { "reimagined", 100.0f }, { "layerA.reimagined.mode", 3.0f } } },
        { "16 notes, Granular", 1, 16, 0, 48000.0, 128, { { "layerA.sourceMode", 1.0f } } },
        { "16 notes, 2 layers", 2, 16, 0, 48000.0, 128, {} },
        { "16 notes, 3 layers", 3, 16, 0, 48000.0, 128, {} },
        { "16 notes, 3 layers, REIMAGINED 100", 3, 16, 0, 48000.0, 128,
          { { "reimagined", 100.0f }, { "layerB.reimagined", 100.0f }, { "layerC.reimagined", 100.0f } } },
        { "16 notes, DRIVE 70 TUBE", 1, 16, 0, 48000.0, 128, { { "drive", 70.0f } } },
        { "16 notes, DRIVE 70 TAPE", 1, 16, 0, 48000.0, 128, { { "drive", 70.0f }, { "drive.mode", 1.0f } } },
        { "16 notes, DRIVE 70 CRUNCH", 1, 16, 0, 48000.0, 128, { { "drive", 70.0f }, { "drive.mode", 2.0f } } },
        { "16 notes, 3 layers granular + REIMAGINED + DRIVE + MOVEMENT + SPACE", 3, 16, 0, 48000.0, 128,
          { { "layerA.sourceMode", 1.0f }, { "layerB.sourceMode", 1.0f }, { "layerC.sourceMode", 1.0f },
            { "reimagined", 100.0f }, { "layerB.reimagined", 100.0f }, { "layerC.reimagined", 100.0f },
            { "drive", 70.0f }, { "motion", 60.0f }, { "movement.mode", 2.0f }, { "space", 60.0f } } },
        { "16 notes, ARP 1/16 UP", 1, 16, 0, 48000.0, 128, { { "arp.enabled", 1.0f }, { "arp.rate", 2.0f } } },
        { "4 notes, ARP 1/16 CHORD 2 octaves", 1, 4, 0, 48000.0, 128,
          { { "arp.enabled", 1.0f }, { "arp.rate", 2.0f }, { "arp.pattern", 5.0f }, { "arp.octaves", 2.0f } } },
        // A stress case: 16-note chords up to three octaves above the keys (the cost is the
        // engine's voices transposed far up, as "16 notes, +24 st" shows; the ARP stage is ~0).
        { "16 notes, ARP 1/32 CHORD 4 octaves gate 150 %", 1, 16, 0, 48000.0, 128,
          { { "arp.enabled", 1.0f }, { "arp.rate", 3.0f }, { "arp.pattern", 5.0f }, { "arp.octaves", 4.0f }, { "arp.gate", 150.0f } } },
        { "16 notes, 3 layers, ARP 1/16 RANDOM 3 octaves", 3, 16, 0, 48000.0, 128,
          { { "arp.enabled", 1.0f }, { "arp.rate", 2.0f }, { "arp.pattern", 4.0f }, { "arp.octaves", 3.0f } } },
    };
    // DRIVE (spec): every rate and block size, TUBE at 70 %; the same without DRIVE beside it.
    auto withDrive = cases;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 64, 128, 256, 512 })
            for (float drive : { 0.0f, 70.0f })
                withDrive.push_back ({ "16 notes, DRIVE " + juce::String (juce::roundToInt (drive)) + ", " + juce::String (rate / 1000.0, 1) + " kHz, block "
                                           + juce::String (block),
                                       1, 16, 0, rate, block, { { "drive", drive } } });
    const juce::String only (std::getenv ("OSP_CPU_CASE") != nullptr ? std::getenv ("OSP_CPU_CASE") : "");
    const double seconds = std::getenv ("OSP_CPU_SECONDS") != nullptr ? std::atof (std::getenv ("OSP_CPU_SECONDS")) : 4.0;
    for (const auto& cs : withDrive)
    {
        if (only.isNotEmpty() && ! cs.name.startsWith (only))
            continue;
        OspAudioProcessor p;
        std::vector<juce::File> files { a, b, c };
        files.resize (static_cast<std::size_t> (cs.layers));
        REQUIRE (p.addLayers (juce::Array<juce::File> (files.data(), static_cast<int> (files.size()))) == cs.layers);
        REQUIRE (p.waitForLoads (60000));
        p.pollLoads();
        for (const auto& [id, value] : cs.set)
            p.setParameterValue (id, value);
        p.prepareToPlay (cs.rate, cs.block);
        juce::AudioBuffer<float> buffer (2, cs.block);
        juce::MidiBuffer midi;
        for (int n = 0; n < cs.notes; ++n)
            midi.addEvent (juce::MidiMessage::noteOn (1, 45 + (n * 7) % 24 + cs.transpose, static_cast<juce::uint8> (90)), 0);
        const int blocks = static_cast<int> (seconds * cs.rate / cs.block);
        double worst = 0.0, total = 0.0;
        for (int i = 0; i < blocks; ++i)
        {
            buffer.clear();
            const auto t0 = std::chrono::steady_clock::now();
            p.processBlock (buffer, midi);
            const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
            midi.clear();
            if (i > 10)
            {
                total += us;
                worst = std::max (worst, us);
            }
        }
        const double budget = 1.0e6 * cs.block / cs.rate;
        const double mean = total / (blocks - 11);
        std::cout << cs.name << ": " << juce::String (100.0 * mean / budget, 1) << " % mean, worst block " << juce::String (100.0 * worst / budget, 0)
                  << " % (" << p.activeVoices.load() << " voices)\n";
    }
}

// UI paint cost (hidden): how long each part of the editor takes to paint, at 1x and 2x.
//   xvfb-run ./osp_plugin_tests "[ui-cpu]"
TEST_CASE ("plugin: UI paint cost", "[.][ui-cpu]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "a.wav", testsignals::vowel (midiToHz (57), 3.0, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "b.wav", testsignals::pluck (midiToHz (48), 3.0, 48000.0, 8, 0.7, 1));
    OspAudioProcessor p;
    REQUIRE (p.addLayers ({ a, b }) == 2);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    editor->setSize (1448, 1086);
    ui->refreshNow();
    auto timeOf = [] (juce::Component& c, float scale) {
        (void) c.createComponentSnapshot (c.getLocalBounds(), true, scale);   // warm caches
        const int runs = 20;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < runs; ++i)
            (void) c.createComponentSnapshot (c.getLocalBounds(), true, scale);
        return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / runs;
    };
    for (float scale : { 1.0f, 2.0f })
    {
        std::cout << "scale " << scale << ": whole editor " << juce::String (timeOf (*editor, scale), 2) << " ms\n";
        std::function<void (juce::Component&, int)> walk = [&] (juce::Component& c, int depth) {
            for (auto* child : c.getChildren())
            {
                if (! child->isVisible() || child->getWidth() * child->getHeight() < 2000)
                    continue;
                const double ms = timeOf (*child, scale);
                if (ms > 0.15)
                    std::cout << juce::String::repeatedString ("  ", depth) << child->getName() << " [" << typeid (*child).name() << "] "
                              << child->getWidth() << "x" << child->getHeight() << ": " << juce::String (ms, 2) << " ms\n";
                if (depth < 2)
                    walk (*child, depth + 1);
            }
        };
        walk (*editor, 1);
        // What a frame really costs: the region one moving element dirties, painted through
        // every component under it (the editor repaints that rectangle, parents included).
        auto region = [&] (const char* what, juce::Rectangle<int> r) {
            (void) editor->createComponentSnapshot (r, true, scale);
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < 20; ++i)
                (void) editor->createComponentSnapshot (r, true, scale);
            std::cout << "  frame region " << what << " " << r.toString() << ": "
                      << juce::String (std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / 20.0, 2) << " ms\n";
        };
        for (auto* child : editor->getChildren())
            for (auto* card : child->getChildren())
                if (auto* engineCard = dynamic_cast<osp::plugin::EngineCard*> (card); engineCard != nullptr && engineCard->isVisible())
                {
                    auto& display = engineCard->display();
                    region ("waveform display", editor->getLocalArea (&display, display.getLocalBounds()));
                    region ("one read head (2 px)", editor->getLocalArea (&display, display.getLocalBounds().withWidth (3)));
                    break;
                }
        region ("one macro knob", { 100, 750, 140, 140 });
        region ("keyboard", { 140, 900, 1270, 110 });
    }
}

// Null renders (hidden): every SPACE type through a phrase with a long silence in it
// (note, 3 s of nothing, notes again), written as float WAVs for before/after comparisons
// of optimisations that must not change the sound.
//   OSP_NULL_DIR=<dir> ./osp_plugin_tests "[null-audio]"
TEST_CASE ("plugin: null renders", "[.][null-audio]")
{
    const char* dir = std::getenv ("OSP_NULL_DIR");
    if (dir == nullptr)
        return;
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "a.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    for (int type = 0; type < 4; ++type)
        for (const float space : { 20.0f, 100.0f })
        {
            OspAudioProcessor p;
            REQUIRE (p.addLayers ({ a }) == 1);
            REQUIRE (p.waitForLoads (30000));
            p.pollLoads();
            p.setParameterValue ("space.type", static_cast<float> (type));
            p.setParameterValue ("space", space);
            p.setParameterValue ("movement.mode", static_cast<float> (type));   // a bus mode too
            p.prepareToPlay (48000.0, 256);
            const int total = static_cast<int> (9.0 * 48000.0);
            AudioData out = AudioData::allocate (2, total, 48000.0);
            juce::AudioBuffer<float> buffer (2, 256);
            for (int pos = 0; pos < total; pos += 256)
            {
                juce::MidiBuffer midi;
                const double t = pos / 48000.0;
                if (pos == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 57, static_cast<juce::uint8> (100)), 0);
                if (t >= 1.0 && t < 1.0 + 256.0 / 48000.0)
                    midi.addEvent (juce::MidiMessage::noteOff (1, 57), 0);
                if (t >= 5.0 && t < 5.0 + 256.0 / 48000.0)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (90)), 0);
                    midi.addEvent (juce::MidiMessage::noteOn (1, 64, static_cast<juce::uint8> (80)), 0);
                }
                if (t >= 6.5 && t < 6.5 + 256.0 / 48000.0)
                    midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                buffer.clear();
                p.processBlock (buffer, midi);
                const int n = std::min (256, total - pos);
                for (int ch = 0; ch < 2; ++ch)
                    std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + n, out.channels[static_cast<std::size_t> (ch)].begin() + pos);
            }
            std::string error;
            const auto name = "space-" + std::to_string (type) + "-" + std::to_string (static_cast<int> (space)) + ".wav";
            REQUIRE (io::writeAudioFile (std::filesystem::path (dir) / name, out, io::SampleFormat::float32, error));
        }
}

TEST_CASE ("plugin: SPACE v2, ECHO and SHAPER CUSTOM are saved and recalled; older sessions open the EQ, ECHO stays off", "[plugin][space]")
{
    OspAudioProcessor p;
    p.setParameterValue ("echo", 35.0f);
    p.setParameterValue ("echo.type", 1.0f);
    p.setParameterValue ("echo.stereo", 2.0f);
    p.setParameterValue ("space.size", 80.0f);
    p.setParameterValue ("space.lowCut", 300.0f);
    p.setParameterValue ("space.type", 1.0f);   // HALL
    p.setParameterValue ("movement.shaper.custom", 1.0f);
    auto steps = RhythmicShaper::patternSteps (9);
    steps[2] = { 0.3f, 0.8f, StepShape::up };
    steps[7] = { 0.6f, 0.6f, StepShape::hold };
    p.setShaperCustomPattern (steps, false);
    juce::MemoryBlock state;
    p.getStateInformation (state);

    OspAudioProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (valueOf (q, "echo") == Approx (35.0f));
    CHECK (valueOf (q, "echo.type") == Approx (1.0f));
    CHECK (valueOf (q, "echo.stereo") == Approx (2.0f));
    CHECK (valueOf (q, "space.size") == Approx (80.0f));
    CHECK (valueOf (q, "space.lowCut") == Approx (300.0f).epsilon (1.0e-3));
    CHECK (valueOf (q, "space.type") == Approx (1.0f));
    CHECK (valueOf (q, "movement.shaper.custom") == Approx (1.0f));
    const auto recalled = q.shaperCustomPattern();
    for (std::size_t i = 0; i < steps.size(); ++i)
    {
        CHECK (recalled[i].start == Approx (steps[i].start).margin (1.0e-4));
        CHECK (recalled[i].end == Approx (steps[i].end).margin (1.0e-4));
        CHECK (recalled[i].shape == steps[i].shape);
    }
    // The pattern's text form reads back exactly, and nonsense is refused.
    CHECK (OspAudioProcessor::decodeShaperPattern (OspAudioProcessor::encodeShaperPattern (steps)).has_value());
    CHECK (! OspAudioProcessor::decodeShaperPattern ("v1:1,1,hold").has_value());
    CHECK (! OspAudioProcessor::decodeShaperPattern ("garbage").has_value());

    // A session from before SPACE v2 (state 9): no new parameters, no pattern.
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    auto tree = juce::ValueTree::fromXml (*xml);
    tree.setProperty ("stateVersion", 9, nullptr);
    tree.removeProperty ("shaperCustom", nullptr);
    for (int i = tree.getNumChildren(); --i >= 0;)
    {
        const auto id = tree.getChild (i)["id"].toString();
        if (id.startsWith ("echo") || id == "space.preDelay" || id == "space.size" || id == "space.damping" || id == "space.modulation"
            || id == "space.width" || id == "space.lowCut" || id == "space.highCut" || id == "movement.shaper.custom")
            tree.removeChild (i, nullptr);
    }
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);
    OspAudioProcessor r;
    r.setParameterValue ("echo", 80.0f);   // moved in this instance: the session must not inherit it
    r.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    CHECK (valueOf (r, "echo") == Approx (0.0f).margin (1.0e-4));
    CHECK (valueOf (r, "space.lowCut") == Approx (20.0f).margin (0.01));
    CHECK (valueOf (r, "space.highCut") == Approx (20000.0f).margin (1.0));
    CHECK (valueOf (r, "space.type") == Approx (1.0f));   // CHAMBER's index: HALL now
    CHECK (valueOf (r, "movement.shaper.custom") == Approx (0.0f).margin (1.0e-4));
    CHECK (r.shaperCustomPattern() == RhythmicShaper::patternSteps (ShaperParams().pattern));
}

TEST_CASE ("plugin: saved SHAPER patterns read back; a CUSTOM edit is one undo step", "[plugin][shaper]")
{
    TempDir tmp;
    auto steps = RhythmicShaper::patternSteps (2);
    steps[0] = { 0.0f, 1.0f, StepShape::soft };
    const auto file = tmp.dir.getChildFile (juce::String ("mine") + OspAudioProcessor::shaperPatternExtension);
    REQUIRE (OspAudioProcessor::writeShaperPatternFile (file, steps));
    const auto back = OspAudioProcessor::readShaperPatternFile (file);
    REQUIRE (back.has_value());
    CHECK (*back == steps);
    CHECK (! OspAudioProcessor::readShaperPatternFile (tmp.dir.getChildFile ("missing.ospshaper")).has_value());

    OspAudioProcessor p;
    const auto before = p.shaperCustomPattern();
    p.undoManager.beginNewTransaction();
    p.setShaperCustomPattern (steps);
    CHECK (p.shaperCustomPattern() == steps);
    p.undoManager.undo();
    CHECK (p.shaperCustomPattern() == before);
    p.undoManager.redo();
    CHECK (p.shaperCustomPattern() == steps);
}


TEST_CASE ("plugin: MUTE and SOLO - solo wins, mute silences, both recall; an emptied layer drops its solo", "[plugin][adaptive]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "a.wav", testsignals::vowel (midiToHz (57), 1.5, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "b.wav", testsignals::vowel (midiToHz (64), 1.5, 48000.0, 5));
    OspAudioProcessor p;
    CHECK (p.addLayers ({ a, b }) == 2);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    const double both = levelOf (playNote (p, 57, 48000.0, 0.8));
    REQUIRE (both > 0.0);
    CHECK (p.isLayerHeard (0));
    CHECK (p.isLayerHeard (1));

    p.setParameterValue ("layerB.mute", 1.0f);
    CHECK (! p.isLayerHeard (1));
    const double onlyA = levelOf (playNote (p, 57, 48000.0, 0.8));
    CHECK (onlyA < both);
    CHECK (onlyA > 0.0);

    // SOLO B: B is heard though muted? No - solo decides: only soloed layers are heard.
    p.setParameterValue ("layerB.mute", 0.0f);
    p.setParameterValue ("layerB.solo", 1.0f);
    CHECK (! p.isLayerHeard (0));
    CHECK (p.isLayerHeard (1));
    p.setParameterValue ("layerA.mute", 1.0f);
    p.soloOnly (0);   // alt-click on A's S: A alone, even though it is muted
    CHECK (p.isLayerHeard (0));
    CHECK (! p.isLayerHeard (1));

    // Saved and recalled with the session.
    juce::MemoryBlock state;
    p.getStateInformation (state);
    OspAudioProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (valueOf (q, "layerA.solo") == Approx (1.0f));
    CHECK (valueOf (q, "layerA.mute") == Approx (1.0f));
    CHECK (valueOf (q, "layerB.solo") == Approx (0.0f).margin (1.0e-4));

    // Removing the soloed layer: the others are heard again.
    p.clearLayer (0);
    CHECK (valueOf (p, "layerA.solo") == Approx (0.0f).margin (1.0e-4));
    CHECK (p.isLayerHeard (1));
}

TEST_CASE ("plugin: LOOP and REVERSE with REIMAGINED (measurement)", "[.][loop-reverse-plugin]")
{
    TempDir tmp;
    const auto file = writeSource (tmp.dir, "vowel.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    auto rmsOf = [] (const AudioData& x, double from, double to) {
        double e = 0.0;
        const auto a = static_cast<std::size_t> (from * 48000), b = std::min (x.channels[0].size(), static_cast<std::size_t> (to * 48000));
        for (std::size_t i = a; i < b; ++i)
            e += 0.5 * (x.channels[0][i] * x.channels[0][i] + x.channels[1][i] * x.channels[1][i]);
        return std::sqrt (e / std::max<std::size_t> (1, b - a));
    };
    for (int mode = 0; mode < 5; ++mode)
        for (float amount : { 0.0f, 50.0f, 100.0f })
            for (int fresh = 0; fresh < 2; ++fresh)
            {
                OspAudioProcessor p;
                if (fresh == 1)
                    p.initPatch();
                loadAndWait (p, file);
                p.setParameterValue ("space", 0.0f);
                p.setParameterValue ("release", 50.0f);
                p.setParameterValue ("reimagined", amount);
                p.setParameterValue ("layerA.reimagined.mode", static_cast<float> (mode));
                double tail[2] {};
                for (int loop = 0; loop < 2; ++loop)
                {
                    p.setParameterValue ("layerA.loop", static_cast<float> (loop));
                    const auto out = playNote (p, 57, 48000.0, 6.0);
                    tail[loop] = rmsOf (out, 3.0, 5.5) / std::max (1.0e-9, rmsOf (out, 0.0, 1.0));
                }
                std::printf ("mode %d %3.0f%% %s routing %s: tail loop off %.3f on %.3f\n", mode, amount, fresh ? "init  " : "legacy",
                             p.isReimaginedPerLayer() ? "perLayer" : "legacy", tail[0], tail[1]);
            }
}

// DRIVE-00 / DRIVE-10: presets made before DRIVE must sound exactly the same after it.
// Run once with the build from before DRIVE (OSP_DRIVE_BASELINE=write) and again with the
// new build (=compare), the same OSP_DRIVE_BASELINE_DIR: the old build's saved states are
// loaded by the new one, rendered, and compared sample by sample with the old renders.
TEST_CASE ("plugin: sessions from before DRIVE render identically (baseline)", "[.][drive-baseline]")
{
    const auto* modeText = std::getenv ("OSP_DRIVE_BASELINE");
    const auto* dirText = std::getenv ("OSP_DRIVE_BASELINE_DIR");
    if (modeText == nullptr || dirText == nullptr)
    {
        WARN ("set OSP_DRIVE_BASELINE=write|compare and OSP_DRIVE_BASELINE_DIR");
        return;
    }
    const bool write = std::strcmp (modeText, "write") == 0;
    const juce::File dir (dirText);
    dir.createDirectory();
    const auto vowelFile = dir.getChildFile ("vowel.wav"), sawFile = dir.getChildFile ("saw.wav"), pluckFile = dir.getChildFile ("pluck.wav");
    if (write)
    {
        writeSource (dir, "vowel.wav", testsignals::vowel (midiToHz (57), 2.5, 48000.0, 3));
        writeSource (dir, "saw.wav", testsignals::saw (midiToHz (48), 2.0, 48000.0));
        writeSource (dir, "pluck.wav", testsignals::pluck (midiToHz (60), 2.0, 48000.0, 5));
    }

    struct Scene
    {
        const char* name;
        std::function<void (OspAudioProcessor&)> setup;
        bool automateDynamics = false;
    };
    const std::vector<Scene> scenes {
        { "one-shot", [&] (OspAudioProcessor& p) { loadAndWait (p, vowelFile); } },
        { "granular", [&] (OspAudioProcessor& p) {
              loadAndWait (p, vowelFile);
              p.setParameterValue ("layerA.sourceMode", 1.0f);
          } },
        { "two-layer", [&] (OspAudioProcessor& p) {
              loadAndWait (p, vowelFile);
              p.loadFile (sawFile, 1);
              REQUIRE (p.waitForLoads (20000));
              p.pollLoads();
              p.setParameterValue ("ab.blend", 0.4f);
          } },
        { "three-layer", [&] (OspAudioProcessor& p) {
              loadAndWait (p, vowelFile);
              for (int layer : { 1, 2 })
              {
                  p.loadFile (layer == 1 ? sawFile : pluckFile, layer);
                  REQUIRE (p.waitForLoads (20000));
                  p.pollLoads();
              }
          } },
        { "reimagined", [&] (OspAudioProcessor& p) {
              loadAndWait (p, vowelFile);
              p.setParameterValue ("reimagined", 90.0f);
              p.setParameterValue ("layerA.reimagined.mode", 1.0f);   // TAPE FRAME
          } },
        { "dynamics-automation", [&] (OspAudioProcessor& p) {
              loadAndWait (p, pluckFile);
              p.setParameterValue ("dynamics.curve", 2.0f);
          },
          true },
        { "character-drive", [&] (OspAudioProcessor& p) {
              loadAndWait (p, sawFile);
              p.setParameterValue ("character.drive", 80.0f);
              p.setParameterValue ("character.resonance", 40.0f);
          } },
        { "shaper", [&] (OspAudioProcessor& p) {
              loadAndWait (p, vowelFile);
              p.setParameterValue ("movement.mode", 4.0f);
              p.setParameterValue ("motion", 80.0f);
          } },
        { "space-echo", [&] (OspAudioProcessor& p) {
              loadAndWait (p, pluckFile);
              p.setParameterValue ("space", 60.0f);
              p.setParameterValue ("echo", 50.0f);
          } },
    };

    auto render = [] (OspAudioProcessor& p, bool automate) {
        constexpr double rate = 48000.0;
        constexpr int block = 256;
        p.prepareToPlay (rate, block);
        const int total = static_cast<int> (4.0 * rate);
        std::vector<float> out;
        out.reserve (static_cast<std::size_t> (2 * total));
        juce::AudioBuffer<float> buffer (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            if (pos == 0)
                for (int note : { 48, 52, 55 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (60 + note % 7 * 9)), 0);
            if (pos == static_cast<int> (1.0 * rate) / block * block)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (120)), 0);
            if (pos == static_cast<int> (2.5 * rate) / block * block)
                midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            if (automate)   // DYNAMICS moved by the host while notes play
                p.setParameterValue ("dynamics", static_cast<float> (100.0 * (0.5 + 0.5 * std::sin (pos / rate * 3.0))));
            buffer.clear();
            p.processBlock (buffer, midi);
            for (int i = 0; i < block && pos + i < total; ++i)
            {
                out.push_back (buffer.getSample (0, i));
                out.push_back (buffer.getSample (1, i));
            }
        }
        return out;
    };

    for (const auto& scene : scenes)
    {
        CAPTURE (scene.name);
        const auto audioFile = dir.getChildFile (juce::String (scene.name) + ".f32");
        const auto stateFile = dir.getChildFile (juce::String (scene.name) + ".state");
        if (write)
        {
            OspAudioProcessor p;
            scene.setup (p);
            juce::MemoryBlock state;
            p.getStateInformation (state);
            REQUIRE (stateFile.replaceWithData (state.getData(), state.getSize()));
            OspAudioProcessor fresh;   // render from the saved state, exactly as compare will
            fresh.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
            REQUIRE (fresh.waitForLoads (20000));
            fresh.pollLoads();
            const auto audio = render (fresh, scene.automateDynamics);
            REQUIRE (audioFile.replaceWithData (audio.data(), audio.size() * sizeof (float)));
            std::printf ("wrote %s (%zu samples)\n", scene.name, audio.size() / 2);
            continue;
        }
        juce::MemoryBlock state, reference;
        REQUIRE (stateFile.loadFileAsData (state));
        REQUIRE (audioFile.loadFileAsData (reference));
        OspAudioProcessor p;
        p.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        REQUIRE (p.waitForLoads (20000));
        p.pollLoads();
        const auto audio = render (p, scene.automateDynamics);
        const auto* ref = static_cast<const float*> (reference.getData());
        REQUIRE (reference.getSize() == audio.size() * sizeof (float));
        double worst = 0.0, peak = 0.0;
        for (std::size_t i = 0; i < audio.size(); ++i)
        {
            worst = std::max (worst, static_cast<double> (std::abs (audio[i] - ref[i])));
            peak = std::max (peak, static_cast<double> (std::abs (ref[i])));
        }
        std::printf ("%-20s peak %.4f  largest difference %.3g\n", scene.name, peak, worst);
        CHECK (peak > 1.0e-3);
        CHECK (worst <= 0.0);   // bit-identical: DRIVE at 0 % is not in the signal path
    }
}

TEST_CASE ("plugin: DRIVE is saved and recalled; older sessions open it off; DYNAMICS keeps its IDs and its sound", "[plugin][drive]")
{
    // DYNAMICS' parameters are unchanged (IDs, defaults): host automation of old sessions still lands.
    {
        OspAudioProcessor fresh;
        for (const char* id : { "dynamics", "dynamics.curve", "dynamics.tone", "velocityRange" })
            REQUIRE (fresh.parameters.getParameter (id) != nullptr);
        CHECK (valueOf (fresh, "dynamics") == Approx (65.0f));
        CHECK (valueOf (fresh, "dynamics.curve") == Approx (1.0f));
        CHECK (valueOf (fresh, "drive") == Approx (0.0f));          // new patches start clean
        CHECK (valueOf (fresh, "drive.mode") == Approx (0.0f));     // TUBE
        CHECK (valueOf (fresh, "drive.tone") == Approx (50.0f));
        CHECK (valueOf (fresh, "drive.body") == Approx (50.0f));
    }

    OspAudioProcessor p;
    p.setParameterValue ("drive", 60.0f);
    p.setParameterValue ("drive.mode", 2.0f);
    p.setParameterValue ("drive.tone", 30.0f);
    p.setParameterValue ("drive.body", 80.0f);
    p.setParameterValue ("dynamics", 40.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    OspAudioProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (valueOf (q, "drive") == Approx (60.0f));
    CHECK (valueOf (q, "drive.mode") == Approx (2.0f));
    CHECK (valueOf (q, "drive.tone") == Approx (30.0f));
    CHECK (valueOf (q, "drive.body") == Approx (80.0f));
    CHECK (valueOf (q, "dynamics") == Approx (40.0f));

    // A session from before DRIVE: no drive.* at all. It opens with DRIVE off (bypassed), even in
    // an instance where DRIVE had been turned up, and its DYNAMICS as it was saved.
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    auto tree = juce::ValueTree::fromXml (*xml);
    for (int i = tree.getNumChildren(); --i >= 0;)
        if (tree.getChild (i)["id"].toString().startsWith ("drive"))
            tree.removeChild (i, nullptr);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);
    OspAudioProcessor r;
    r.setParameterValue ("drive", 90.0f);
    r.setParameterValue ("drive.mode", 1.0f);
    r.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    CHECK (valueOf (r, "drive") == Approx (0.0f).margin (1.0e-4));
    CHECK (valueOf (r, "drive.mode") == Approx (0.0f).margin (1.0e-4));
    CHECK (valueOf (r, "drive.body") == Approx (50.0f));
    CHECK (valueOf (r, "dynamics") == Approx (40.0f));
}

TEST_CASE ("plugin: DRIVE changes the sound (and CC 27 moves it); at 0 it is untouched; DYNAMICS still acts", "[plugin][drive]")
{
    TempDir tmp;
    const auto file = writeSource (tmp.dir, "vowel.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    auto render = [] (OspAudioProcessor& p, int velocity, int cc27 = -1) {
        p.prepareToPlay (48000.0, 256);
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, 256);
        for (int pos = 0; pos < 48000; pos += 256)
        {
            juce::MidiBuffer midi;
            if (pos == 0)
            {
                if (cc27 >= 0)
                    midi.addEvent (juce::MidiMessage::controllerEvent (1, 27, cc27), 0);
                for (int note : { 48, 52, 55, 60 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (velocity)), 0);
            }
            buffer.clear();
            p.processBlock (buffer, midi);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 256);
        }
        return out;
    };
    auto difference = [] (const std::vector<float>& x, const std::vector<float>& y) {
        double d = 0.0, e = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            d += (x[i] - y[i]) * (x[i] - y[i]);
            e += x[i] * x[i];
        }
        return std::sqrt (d / std::max (e, 1.0e-12));
    };
    OspAudioProcessor p;
    loadAndWait (p, file);
    p.setParameterValue ("space", 0.0f);
    const auto clean = render (p, 100);
    CHECK (difference (clean, render (p, 100)) == Approx (0.0).margin (1.0e-9));   // deterministic, untouched at 0
    for (int mode = 0; mode < 3; ++mode)
    {
        CAPTURE (mode);
        p.setParameterValue ("drive.mode", static_cast<float> (mode));
        p.setParameterValue ("drive", 80.0f);
        const auto driven = render (p, 100);
        CHECK (difference (clean, driven) > 0.05);
        float peak = 0.0f;
        bool finite = true;
        for (float v : driven)
        {
            finite = finite && std::isfinite (v);
            peak = std::max (peak, std::abs (v));
        }
        CHECK (finite);
        CHECK (peak < 1.5f);
        p.setParameterValue ("drive", 0.0f);
    }
    p.setParameterValue ("drive.mode", 0.0f);
    // CC 27 is DRIVE's (CC 21 stays DYNAMICS').
    CHECK (difference (clean, render (p, 100, 100)) > 0.05);
    OspAudioProcessor fresh;
    loadAndWait (fresh, file);
    fresh.setParameterValue ("space", 0.0f);
    CHECK (difference (clean, render (fresh, 100)) == Approx (0.0).margin (1.0e-9));
    // DYNAMICS (in Advanced now) still shapes a soft note.
    fresh.setParameterValue ("dynamics", 0.0f);
    const auto flat = render (fresh, 30);
    fresh.setParameterValue ("dynamics", 100.0f);
    CHECK (difference (flat, render (fresh, 30)) > 0.05);
}

// ARP-0 / ARP release blocker: with the arpeggiator off, the instrument must be exactly the
// instrument it was. Run once with the build from before the arpeggiator
// (OSP_ARP_BASELINE=write) and again with the new one (=compare), the same OSP_ARP_BASELINE_DIR:
// the old build's states are loaded by the new one, played with the same MIDI (held notes,
// sustain pedal, pitch bend, mod wheel, pressure, repeated pitches, All Notes Off), with and
// without a running transport, and compared sample by sample.
TEST_CASE ("plugin: with ARP off sessions and MIDI render identically (baseline)", "[.][arp-baseline]")
{
    const auto* modeText = std::getenv ("OSP_ARP_BASELINE");
    const auto* dirText = std::getenv ("OSP_ARP_BASELINE_DIR");
    if (modeText == nullptr || dirText == nullptr)
    {
        WARN ("set OSP_ARP_BASELINE=write|compare and OSP_ARP_BASELINE_DIR");
        return;
    }
    const bool write = std::strcmp (modeText, "write") == 0;
    const juce::File dir (dirText);
    dir.createDirectory();
    const auto vowelFile = dir.getChildFile ("vowel.wav"), sawFile = dir.getChildFile ("saw.wav"), pluckFile = dir.getChildFile ("pluck.wav");
    if (write)
    {
        writeSource (dir, "vowel.wav", testsignals::vowel (midiToHz (57), 2.5, 48000.0, 3));
        writeSource (dir, "saw.wav", testsignals::saw (midiToHz (48), 2.0, 48000.0));
        writeSource (dir, "pluck.wav", testsignals::pluck (midiToHz (60), 2.0, 48000.0, 5));
    }

    struct Scene
    {
        const char* name;
        std::function<void (OspAudioProcessor&)> setup;
        bool transport = false;
    };
    std::vector<Scene> scenes {
        { "one-shot", [&] (OspAudioProcessor& p) { loadAndWait (p, vowelFile); } },
        { "transport", [&] (OspAudioProcessor& p) {
              loadAndWait (p, vowelFile);
              p.setParameterValue ("movement.mode", 4.0f);   // SHAPER follows the host
              p.setParameterValue ("motion", 70.0f);
          },
          true },
        { "reverse", [&] (OspAudioProcessor& p) {
              loadAndWait (p, pluckFile);
              p.setParameterValue ("layerA.reverse", 1.0f);
          } },
        { "granular", [&] (OspAudioProcessor& p) {
              loadAndWait (p, vowelFile);
              p.setParameterValue ("layerA.sourceMode", 1.0f);
          } },
        { "mono-glide", [&] (OspAudioProcessor& p) {
              loadAndWait (p, sawFile);
              p.setParameterValue ("voiceMode", 1.0f);
              p.setParameterValue ("glide", 120.0f);
          } },
        { "two-layer", [&] (OspAudioProcessor& p) {
              loadAndWait (p, vowelFile);
              p.loadFile (sawFile, 1);
              REQUIRE (p.waitForLoads (20000));
              p.pollLoads();
          } },
        { "three-layer", [&] (OspAudioProcessor& p) {
              loadAndWait (p, vowelFile);
              for (int layer : { 1, 2 })
              {
                  p.loadFile (layer == 1 ? sawFile : pluckFile, layer);
                  REQUIRE (p.waitForLoads (20000));
                  p.pollLoads();
              }
          } },
        { "fx", [&] (OspAudioProcessor& p) {
              loadAndWait (p, pluckFile);
              p.setParameterValue ("life", 80.0f);
              p.setParameterValue ("drive", 60.0f);
              p.setParameterValue ("character", 70.0f);
              p.setParameterValue ("motion", 60.0f);
              p.setParameterValue ("space", 60.0f);
              p.setParameterValue ("echo", 50.0f);
          } },
    };
    for (int mode = 0; mode < 5; ++mode)
    {
        static const char* names[] { "kaleidoscope", "tape-frame", "toybox", "mosaic", "mirage" };
        scenes.push_back ({ names[mode], [&, mode] (OspAudioProcessor& p) {
                               loadAndWait (p, vowelFile);
                               p.setParameterValue ("reimagined", 80.0f);
                               p.setParameterValue ("layerA.reimagined.mode", static_cast<float> (mode));
                           } });
    }

    auto render = [] (OspAudioProcessor& p, bool transport) {
        constexpr double rate = 48000.0;
        constexpr int block = 256;
        TestPlayHead head;
        head.bpm = 120.0;
        if (transport)
            p.setPlayHead (&head);
        p.prepareToPlay (rate, block);
        const int total = static_cast<int> (5.0 * rate);
        std::vector<float> out;
        out.reserve (static_cast<std::size_t> (2 * total));
        juce::AudioBuffer<float> buffer (2, block);
        auto at = [] (double seconds) { return static_cast<int> (seconds * rate); };
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            auto add = [&] (double seconds, const juce::MidiMessage& m) {
                const int t = at (seconds);
                if (t >= pos && t < pos + block)
                    midi.addEvent (m, t - pos);
            };
            add (0.0, juce::MidiMessage::noteOn (1, 48, static_cast<juce::uint8> (70)));
            add (0.01, juce::MidiMessage::noteOn (1, 52, static_cast<juce::uint8> (90)));
            add (0.02, juce::MidiMessage::noteOn (1, 55, static_cast<juce::uint8> (110)));
            add (0.3, juce::MidiMessage::controllerEvent (1, 1, 90));                  // mod wheel
            add (0.5, juce::MidiMessage::pitchWheel (1, 8192 + 3000));
            add (0.7, juce::MidiMessage::channelPressureChange (1, 80));
            add (0.8, juce::MidiMessage::controllerEvent (1, 64, 127));                // sustain down
            add (0.9, juce::MidiMessage::noteOff (1, 52));                             // held by the pedal
            add (1.0, juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (120)));
            add (1.2, juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (64))); // same pitch again
            add (1.3, juce::MidiMessage::noteOff (1, 60));
            add (1.5, juce::MidiMessage::pitchWheel (1, 8192));
            add (1.6, juce::MidiMessage::controllerEvent (1, 64, 0));                  // sustain up
            add (1.8, juce::MidiMessage::noteOff (1, 48));
            add (2.0, juce::MidiMessage::noteOn (1, 64, static_cast<juce::uint8> (100)));
            add (2.0, juce::MidiMessage::noteOff (1, 55));                             // same timestamp
            add (2.6, juce::MidiMessage::allNotesOff (1));
            add (3.0, juce::MidiMessage::noteOn (1, 57, static_cast<juce::uint8> (100)));
            add (3.4, juce::MidiMessage::noteOff (1, 57));
            buffer.clear();
            p.processBlock (buffer, midi);
            head.ppq += block * head.bpm / (60.0 * rate);
            for (int i = 0; i < block && pos + i < total; ++i)
            {
                out.push_back (buffer.getSample (0, i));
                out.push_back (buffer.getSample (1, i));
            }
        }
        p.setPlayHead (nullptr);
        return out;
    };

    for (const auto& scene : scenes)
    {
        CAPTURE (scene.name);
        const auto audioFile = dir.getChildFile (juce::String (scene.name) + ".f32");
        const auto stateFile = dir.getChildFile (juce::String (scene.name) + ".state");
        if (write)
        {
            OspAudioProcessor p;
            scene.setup (p);
            juce::MemoryBlock state;
            p.getStateInformation (state);
            REQUIRE (stateFile.replaceWithData (state.getData(), state.getSize()));
            OspAudioProcessor fresh;
            fresh.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
            REQUIRE (fresh.waitForLoads (20000));
            fresh.pollLoads();
            const auto audio = render (fresh, scene.transport);
            REQUIRE (audioFile.replaceWithData (audio.data(), audio.size() * sizeof (float)));
            std::printf ("wrote %s (%zu samples)\n", scene.name, audio.size() / 2);
            continue;
        }
        juce::MemoryBlock state, reference;
        REQUIRE (stateFile.loadFileAsData (state));
        REQUIRE (audioFile.loadFileAsData (reference));
        OspAudioProcessor p;
        p.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        REQUIRE (p.waitForLoads (20000));
        p.pollLoads();
        const auto audio = render (p, scene.transport);
        const auto* ref = static_cast<const float*> (reference.getData());
        REQUIRE (reference.getSize() == audio.size() * sizeof (float));
        double worst = 0.0, peak = 0.0;
        for (std::size_t i = 0; i < audio.size(); ++i)
        {
            worst = std::max (worst, static_cast<double> (std::abs (audio[i] - ref[i])));
            peak = std::max (peak, static_cast<double> (std::abs (ref[i])));
        }
        std::printf ("%-14s peak %.4f  largest difference %.3g\n", scene.name, peak, worst);
        CHECK (peak > 1.0e-3);
        CHECK (worst <= 0.0);   // bit-identical: the arpeggiator off is not in the MIDI path
    }
}

// ---------------------------------------------------------------------------------------------
// ARPEGGIATOR in the plugin: parameters and state, the note stream the engine plays, the host's
// transport, every mode and effect behind it, the lifecycle, and its cost.

namespace
{
    float arpValue (OspAudioProcessor& p, const juce::String& id)
    {
        auto* parameter = p.parameters.getParameter (id);
        REQUIRE (parameter != nullptr);
        return parameter->convertFrom0to1 (parameter->getValue());
    }

    struct ArpRun
    {
        std::vector<float> audio;   ///< interleaved stereo
        int noteOns = 0;            ///< note-ons the engine received
        double peak = 0.0, playPeak = 0.0;
        bool finite = true;
    };

    /** Renders `seconds`, `events` adds MIDI per block (block start, block size). */
    ArpRun renderArp (OspAudioProcessor& p, double rate, int block, double seconds,
                      const std::function<void (int, int, juce::MidiBuffer&)>& events, TestPlayHead* head = nullptr, double playUntil = 1.0e9,
                      bool prepare = true)
    {
        p.setPlayHead (head);
        if (prepare)
            p.prepareToPlay (rate, block);
        ArpRun run;
        const int startCount = p.velocityCount.load();
        const auto total = static_cast<int> (seconds * rate);
        juce::AudioBuffer<float> buffer (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
            juce::MidiBuffer midi;
            if (events)
                events (pos, n, midi);
            view.clear();
            p.processBlock (view, midi);
            for (int i = 0; i < n; ++i)
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float v = view.getSample (ch, i);
                    run.finite = run.finite && std::isfinite (v);
                    run.peak = std::max (run.peak, static_cast<double> (std::abs (v)));
                    if (pos + i < playUntil * rate)
                        run.playPeak = std::max (run.playPeak, static_cast<double> (std::abs (v)));
                    run.audio.push_back (v);
                }
            if (head != nullptr)
                head->ppq += n * head->bpm / (60.0 * rate);
        }
        p.setPlayHead (nullptr);
        run.noteOns = p.velocityCount.load() - startCount;
        return run;
    }

    /** Adds `m` if `seconds` falls in the block. */
    void at (juce::MidiBuffer& midi, int pos, int n, double rate, double seconds, const juce::MidiMessage& m)
    {
        const auto t = static_cast<int> (seconds * rate);
        if (t >= pos && t < pos + n)
            midi.addEvent (m, t - pos);
    }

    void arpOn (OspAudioProcessor& p, int pattern = 0, int rate = 1, float gate = 75.0f, int octaves = 1)
    {
        p.setParameterValue ("arp.enabled", 1.0f);
        p.setParameterValue ("arp.pattern", static_cast<float> (pattern));
        p.setParameterValue ("arp.rate", static_cast<float> (rate));
        p.setParameterValue ("arp.gate", gate);
        p.setParameterValue ("arp.octaves", static_cast<float> (octaves));
    }
}

TEST_CASE ("plugin: ARP parameters, recall; older sessions open with it off; the inline editor is a view setting", "[plugin][arp]")
{
    OspAudioProcessor fresh;
    CHECK (arpValue (fresh, "arp.enabled") == Approx (0.0f));
    CHECK (arpValue (fresh, "arp.pattern") == Approx (0.0f));   // UP
    CHECK (arpValue (fresh, "arp.rate") == Approx (1.0f));      // 1/8
    CHECK (arpValue (fresh, "arp.gate") == Approx (75.0f));
    CHECK (arpValue (fresh, "arp.octaves") == Approx (1.0f));
    CHECK (OspAudioProcessor::arpPatternNames().joinIntoString (",") == "UP,DOWN,UP/DOWN,PLAYED,RANDOM,CHORD");
    CHECK (OspAudioProcessor::arpRateNames().joinIntoString (",") == "1/4,1/8,1/16,1/32,1/4D,1/8D,1/16D,1/4T,1/8T,1/16T");
    CHECK_FALSE (fresh.arpEditorExpanded());
    // The expanded state is not a parameter: hosts cannot automate it, presets do not carry it as one.
    for (auto* parameter : fresh.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            CHECK_FALSE (ranged->getParameterID().containsIgnoreCase ("expand"));

    OspAudioProcessor p;
    arpOn (p, 4, 8, 130.0f, 3);
    p.setArpEditorExpanded (true);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    OspAudioProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (arpValue (q, "arp.enabled") == Approx (1.0f));
    CHECK (arpValue (q, "arp.pattern") == Approx (4.0f));
    CHECK (arpValue (q, "arp.rate") == Approx (8.0f));
    CHECK (arpValue (q, "arp.gate") == Approx (130.0f));
    CHECK (arpValue (q, "arp.octaves") == Approx (3.0f));
    CHECK (q.arpEditorExpanded());

    // A session from before the arpeggiator (no arp.* at all, no arpExpanded) opens with it off,
    // even in an instance where it was on.
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (xml != nullptr);
    for (auto* child = xml->getFirstChildElement(); child != nullptr;)
    {
        auto* next = child->getNextElement();
        if (child->getStringAttribute ("id").startsWith ("arp."))
            xml->removeChildElement (child, true);
        child = next;
    }
    xml->removeAttribute ("arpExpanded");
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    q.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    CHECK (arpValue (q, "arp.enabled") == Approx (0.0f));
    CHECK (arpValue (q, "arp.pattern") == Approx (0.0f));
    CHECK (arpValue (q, "arp.rate") == Approx (1.0f));
    CHECK (arpValue (q, "arp.gate") == Approx (75.0f));
    CHECK (arpValue (q, "arp.octaves") == Approx (1.0f));
    CHECK_FALSE (q.arpEditorExpanded());

    // Expanding the editor changes nothing in the sound, with ARP on or off.
    TempDir tmp;
    const auto file = writeSource (tmp.dir, "pluck.wav", testsignals::pluck (midiToHz (60), 2.0, 48000.0, 5));
    for (bool enabled : { false, true })
    {
        std::array<std::vector<float>, 2> renders;
        for (int expanded = 0; expanded < 2; ++expanded)
        {
            OspAudioProcessor r;
            loadAndWait (r, file);
            if (enabled)
                arpOn (r);
            r.setArpEditorExpanded (expanded == 1);
            renders[static_cast<std::size_t> (expanded)] = renderArp (r, 48000.0, 256, 1.5, [] (int pos, int n, juce::MidiBuffer& m) {
                                                               at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (90)));
                                                               at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, 64, static_cast<juce::uint8> (90)));
                                                               at (m, pos, n, 48000.0, 0.8, juce::MidiMessage::allNotesOff (1));
                                                           }).audio;
        }
        CHECK (renders[0] == renders[1]);
    }
}

TEST_CASE ("plugin: ARP plays held notes as steps with their velocities; the screen keyboard, wheels and pedal", "[plugin][arp]")
{
    TempDir tmp;
    const auto file = writeSource (tmp.dir, "pluck.wav", testsignals::pluck (midiToHz (60), 2.0, 48000.0, 5));
    {
        OspAudioProcessor p;
        loadAndWait (p, file);
        arpOn (p);   // UP, 1/8, no host: 120 BPM, a step every 12000 samples
        const auto run = renderArp (p, 48000.0, 256, 1.0, [] (int pos, int n, juce::MidiBuffer& m) {
            at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, 67, static_cast<juce::uint8> (120)));
            at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (40)));
            at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, 64, static_cast<juce::uint8> (80)));
        });
        CHECK (run.noteOns == 4);   // steps at 0, 0.25, 0.5, 0.75 s
        const int count = p.velocityCount.load();
        std::vector<int> last;
        for (int i = count - 4; i < count; ++i)
            last.push_back (p.recentVelocity[static_cast<std::size_t> (i % OspAudioProcessor::velocityHistory)].load());
        CHECK (last == std::vector<int> { 40, 80, 120, 40 });   // C E G C, each with its own velocity
        CHECK (run.playPeak > 1.0e-3);
        const auto view = p.arpView();
        CHECK (view.active);
        CHECK (view.current == 3);
        CHECK (view.low[0] == 60);
        CHECK (view.low[1] == 64);
        CHECK (view.low[2] == 67);
    }
    {
        // The on-screen keyboard plays through the arpeggiator like a controller.
        OspAudioProcessor p;
        loadAndWait (p, file);
        arpOn (p);
        p.prepareToPlay (48000.0, 256);
        p.keyboardState.noteOn (1, 60, 0.8f);
        const auto run = renderArp (p, 48000.0, 256, 1.0, {}, nullptr, 1.0e9, false);
        CHECK (run.noteOns == 4);
        p.keyboardState.noteOff (1, 60, 0.0f);
        const auto after = renderArp (p, 48000.0, 256, 1.0, {}, nullptr, 1.0e9, false);
        CHECK (after.noteOns == 0);
    }
    {
        // Pitch bend passes by the arpeggiator: the stepped notes bend.
        auto pitchWith = [&] (int bend) {
            OspAudioProcessor p;
            loadAndWait (p, file);
            p.setParameterValue ("space", 0.0f);
            arpOn (p, 0, 0, 150.0f);   // 1/4, legato
            const auto run = renderArp (p, 48000.0, 256, 1.5, [bend] (int pos, int n, juce::MidiBuffer& m) {
                at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::pitchWheel (1, bend));
                at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)));
            });
            AudioData audio = AudioData::allocate (1, static_cast<int> (run.audio.size() / 2), 48000.0);
            for (std::size_t i = 0; i < run.audio.size() / 2; ++i)
                audio.channels[0][i] = run.audio[2 * i];
            return pitchOf (audio);
        };
        const double plain = pitchWith (8192), bent = pitchWith (16383);
        REQUIRE (plain > 0.0);
        CHECK (12.0 * std::log2 (bent / plain) == Approx (arpValue (*std::make_unique<OspAudioProcessor>(), "bendRange")).margin (0.3));
    }
    {
        // The sustain pedal keeps released keys in the pattern; releasing it stops the pattern.
        OspAudioProcessor p;
        loadAndWait (p, file);
        arpOn (p);
        const auto run = renderArp (p, 48000.0, 256, 3.0, [] (int pos, int n, juce::MidiBuffer& m) {
            at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)));
            at (m, pos, n, 48000.0, 0.1, juce::MidiMessage::controllerEvent (1, 64, 127));
            at (m, pos, n, 48000.0, 0.2, juce::MidiMessage::noteOff (1, 60));
            at (m, pos, n, 48000.0, 1.1, juce::MidiMessage::controllerEvent (1, 64, 0));
        });
        CHECK (run.noteOns == 5);   // 0, 0.25 .. 1.0 s; nothing after the pedal
        CHECK (p.activeVoices.load() == 0);
    }
}

TEST_CASE ("plugin: ARP on the host's grid; a bounce plays the same notes every time", "[plugin][arp]")
{
    TempDir tmp;
    const auto file = writeSource (tmp.dir, "pluck.wav", testsignals::pluck (midiToHz (60), 2.0, 48000.0, 5));
    {
        OspAudioProcessor p;
        loadAndWait (p, file);
        arpOn (p, 0, 2);   // 1/16 at 120: every 6000 samples
        TestPlayHead head;
        head.ppq = 0.0;
        // Pressed at 0.01 s, after the downbeat: the first step waits for 0.125 s (sample 6000).
        std::vector<int> onsPerBlock;
        int previous = p.velocityCount.load();
        renderArp (p, 48000.0, 480, 1.0, [&] (int pos, int n, juce::MidiBuffer& m) {
            onsPerBlock.push_back (p.velocityCount.load() - previous);
            previous = p.velocityCount.load();
            at (m, pos, n, 48000.0, 0.01, juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)));
            at (m, pos, n, 48000.0, 0.01, juce::MidiMessage::noteOn (1, 67, static_cast<juce::uint8> (100)));
        }, &head);
        // Each entry counted the block before it: entry b + 1 is block b.
        onsPerBlock.push_back (p.velocityCount.load() - previous);
        onsPerBlock.erase (onsPerBlock.begin());
        // A step lands in every 12.5th block of 480 samples: blocks 12, 25, 37, 50, ...
        std::vector<int> blocks;
        for (std::size_t b = 0; b < onsPerBlock.size(); ++b)
            for (int k = 0; k < onsPerBlock[b]; ++k)
                blocks.push_back (static_cast<int> (b));
        std::vector<int> expected;
        for (int step = 1; step * 6000 < 48000; ++step)
            expected.push_back (step * 6000 / 480);
        CHECK (blocks == expected);
    }
    // RANDOM: the same bounce in two instances, one of which played freely before.
    auto bounce = [&] (bool playFirst) {
        OspAudioProcessor p;
        loadAndWait (p, file);
        p.setParameterValue ("space", 0.0f);
        arpOn (p, 4, 2);
        if (playFirst)
            renderArp (p, 48000.0, 256, 2.0, [] (int pos, int n, juce::MidiBuffer& m) {
                at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, 50, static_cast<juce::uint8> (100)));
                at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, 55, static_cast<juce::uint8> (100)));
                at (m, pos, n, 48000.0, 1.0, juce::MidiMessage::allNotesOff (1));
            });
        TestPlayHead head;
        std::vector<int> played;
        renderArp (p, 48000.0, 256, 2.0, [&] (int pos, int n, juce::MidiBuffer& m) {
            const auto view = p.arpView();
            if (view.active && view.current >= 0)
            {
                const int step = view.low[static_cast<std::size_t> (view.current)] * 100 + view.current;   // the note and its column
                if (played.empty() || played.back() != step)
                    played.push_back (step);
            }
            for (int note : { 60, 62, 64, 67, 69 })
                at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)));
        }, &head);
        return played;
    };
    const auto first = bounce (false), second = bounce (true);
    CHECK (first.size() >= 14);
    CHECK (first == second);
}

TEST_CASE ("plugin: ARP with every REIMAGINED mode and routing and every effect: plays, finite, every note ends", "[plugin][arp]")
{
    TempDir tmp;
    const auto vowel = writeSource (tmp.dir, "vowel.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    struct Scene
    {
        juce::String name;
        bool init = false;   ///< a new patch (per-layer REIMAGINED); otherwise the legacy global routing
        std::vector<std::pair<juce::String, float>> set;
    };
    std::vector<Scene> scenes;
    static const char* modes[] { "KALEIDOSCOPE", "TAPE FRAME", "TOYBOX", "MOSAIC", "MIRAGE" };
    for (int mode = 0; mode < 5; ++mode)
        for (bool init : { false, true })
            scenes.push_back ({ juce::String (modes[mode]) + (init ? "" : " (legacy global)"), init,
                                { { "reimagined", 85.0f }, { "layerA.reimagined.mode", static_cast<float> (mode) } } });
    scenes.push_back ({ "LIFE", true, { { "life", 100.0f } } });
    for (int drive = 0; drive < 3; ++drive)
        scenes.push_back ({ "DRIVE " + juce::String (drive), true, { { "drive", 80.0f }, { "drive.mode", static_cast<float> (drive) } } });
    scenes.push_back ({ "DYNAMICS", true, { { "dynamics", 100.0f }, { "dynamics.curve", 2.0f } } });
    scenes.push_back ({ "CHARACTER", true, { { "character", 85.0f }, { "character.resonance", 60.0f }, { "character.drive", 60.0f } } });
    for (int movement = 0; movement < 5; ++movement)
        scenes.push_back ({ "MOVEMENT " + juce::String (movement), true, { { "motion", 85.0f }, { "movement.mode", static_cast<float> (movement) } } });
    scenes.push_back ({ "SPACE", true, { { "space", 80.0f } } });
    scenes.push_back ({ "ECHO", true, { { "echo", 60.0f } } });
    for (const auto& scene : scenes)
        for (int pattern : { 0, 4, 5 })
        {
            CAPTURE (scene.name, pattern);
            OspAudioProcessor p;
            if (scene.init)
                p.initPatch();
            loadAndWait (p, vowel);
            p.setParameterValue ("release", 60.0f);
            for (const auto& [id, value] : scene.set)
                p.setParameterValue (id, value);
            arpOn (p, pattern, 2, 140.0f, 3);
            const auto run = renderArp (p, 48000.0, 256, 2.0, [] (int pos, int n, juce::MidiBuffer& m) {
                for (int note : { 48, 55, 60, 64 })
                    at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (90)));
                for (int note : { 48, 55, 60, 64 })
                    at (m, pos, n, 48000.0, 1.0, juce::MidiMessage::noteOff (1, note));
            }, nullptr, 1.0);
            CHECK (run.finite);
            CHECK (run.playPeak > 1.0e-3);
            CHECK (run.peak < 4.0);
            CHECK (run.noteOns >= 8);
            // Everything ends: no voice is left keyed after the pattern stops and the tails fade.
            renderArp (p, 48000.0, 256, 6.0, {});
            CHECK (p.activeVoices.load() == 0);
        }
}

TEST_CASE ("plugin: ARP with REVERSE, LOOP, Granular and 1-3 layers", "[plugin][arp]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "a.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "b.wav", testsignals::saw (midiToHz (48), 2.0, 48000.0));
    const auto c = writeSource (tmp.dir, "c.wav", testsignals::pluck (midiToHz (60), 2.0, 48000.0, 5));
    for (int layers = 1; layers <= 3; ++layers)
        for (int reverse = 0; reverse < 2; ++reverse)
            for (int loop = 0; loop < 2; ++loop)
                for (int granular = 0; granular < 2; ++granular)
                {
                    CAPTURE (layers, reverse, loop, granular);
                    OspAudioProcessor p;
                    std::vector<juce::File> files { a, b, c };
                    files.resize (static_cast<std::size_t> (layers));
                    REQUIRE (p.addLayers (juce::Array<juce::File> (files.data(), layers)) == layers);
                    REQUIRE (p.waitForLoads (30000));
                    p.pollLoads();
                    p.setParameterValue ("release", 60.0f);
                    for (const char* layer : { "layerA", "layerB", "layerC" })
                    {
                        p.setParameterValue (juce::String (layer) + ".reverse", static_cast<float> (reverse));
                        p.setParameterValue (juce::String (layer) + ".loop", static_cast<float> (loop));
                        p.setParameterValue (juce::String (layer) + ".sourceMode", static_cast<float> (granular));
                    }
                    arpOn (p, 2, 7, 100.0f, 2);   // UP/DOWN, 1/4T
                    const auto run = renderArp (p, 48000.0, 128, 2.0, [] (int pos, int n, juce::MidiBuffer& m) {
                        for (int note : { 52, 57, 61 })
                            at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)));
                        at (m, pos, n, 48000.0, 1.2, juce::MidiMessage::allNotesOff (1));
                    }, nullptr, 1.2);
                    CHECK (run.finite);
                    CHECK (run.playPeak > 1.0e-3);
                    CHECK (run.peak < 4.0);
                    renderArp (p, 48000.0, 128, 5.0, {});
                    CHECK (p.activeVoices.load() == 0);
                }
}

TEST_CASE ("plugin: ARP lifecycle - switching mid-chord, pedal, panic, sample-rate changes, recall, Mono, odd blocks", "[plugin][arp]")
{
    TempDir tmp;
    const auto file = writeSource (tmp.dir, "pluck.wav", testsignals::pluck (midiToHz (60), 2.0, 48000.0, 5));
    auto settle = [] (OspAudioProcessor& p, double rate = 48000.0) {
        renderArp (p, rate, 256, 5.0, [] (int pos, int, juce::MidiBuffer& m) {
            if (pos == 0)
                m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);
        });
        return p.activeVoices.load();
    };
    SECTION ("switched on and off every few blocks while a chord and the pedal are held")
    {
        OspAudioProcessor p;
        loadAndWait (p, file);
        p.setParameterValue ("release", 60.0f);
        arpOn (p, 5, 3, 150.0f, 2);
        int block = 0;
        const auto run = renderArp (p, 48000.0, 64, 4.0, [&] (int pos, int n, juce::MidiBuffer& m) {
            for (int note : { 60, 64, 67 })
                at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)));
            at (m, pos, n, 48000.0, 0.5, juce::MidiMessage::controllerEvent (1, 64, 127));
            at (m, pos, n, 48000.0, 1.0, juce::MidiMessage::noteOff (1, 64));
            at (m, pos, n, 48000.0, 2.5, juce::MidiMessage::controllerEvent (1, 64, 0));
            if (++block % 37 == 0)
                p.setParameterValue ("arp.enabled", arpValue (p, "arp.enabled") > 0.5f ? 0.0f : 1.0f);
        });
        CHECK (run.finite);
        // Let go of the keys: nothing is left sounding, whichever state the switch ended in.
        renderArp (p, 48000.0, 64, 0.1, [] (int pos, int, juce::MidiBuffer& m) {
            if (pos == 0)
                for (int note : { 60, 67 })
                    m.addEvent (juce::MidiMessage::noteOff (1, note), 0);
        });
        CHECK (settle (p) == 0);
    }
    SECTION ("All Notes Off stops the pattern at once")
    {
        OspAudioProcessor p;
        loadAndWait (p, file);
        p.setParameterValue ("release", 60.0f);
        arpOn (p, 0, 3);
        renderArp (p, 48000.0, 256, 1.0, [] (int pos, int n, juce::MidiBuffer& m) {
            for (int note : { 60, 64 })
                at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)));
            at (m, pos, n, 48000.0, 0.5, juce::MidiMessage::allNotesOff (1));
        });
        const auto after = renderArp (p, 48000.0, 256, 2.0, {});
        CHECK (after.noteOns == 0);
        CHECK (p.activeVoices.load() == 0);
        CHECK_FALSE (p.arpView().active);
    }
    SECTION ("the host changes the sample rate mid-performance; a session is recalled while notes are held")
    {
        OspAudioProcessor p;
        loadAndWait (p, file);
        p.setParameterValue ("release", 60.0f);
        arpOn (p, 1, 2);
        renderArp (p, 44100.0, 512, 1.0, [] (int pos, int, juce::MidiBuffer& m) {
            if (pos == 0)
                m.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 0);
        });
        // prepareToPlay at a new rate (inside renderArp): the arpeggiator starts clean.
        const auto run = renderArp (p, 96000.0, 512, 1.0, {});
        CHECK (run.noteOns == 0);
        CHECK (settle (p, 96000.0) == 0);
        juce::MemoryBlock state;
        p.getStateInformation (state);
        renderArp (p, 48000.0, 256, 0.5, [] (int pos, int, juce::MidiBuffer& m) {
            if (pos == 0)
                m.addEvent (juce::MidiMessage::noteOn (1, 62, static_cast<juce::uint8> (100)), 0);
        });
        p.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        REQUIRE (p.waitForLoads (20000));
        p.pollLoads();
        renderArp (p, 48000.0, 256, 0.5, [] (int pos, int, juce::MidiBuffer& m) {
            if (pos == 0)
                m.addEvent (juce::MidiMessage::noteOff (1, 62), 0);
        });
        CHECK (settle (p) == 0);
    }
    SECTION ("Mono with glide follows the pattern legato")
    {
        OspAudioProcessor p;
        loadAndWait (p, file);
        p.setParameterValue ("release", 60.0f);
        p.setParameterValue ("voiceMode", 1.0f);
        p.setParameterValue ("glide", 40.0f);
        arpOn (p, 2, 2, 120.0f, 2);
        const auto run = renderArp (p, 48000.0, 256, 1.0, [] (int pos, int n, juce::MidiBuffer& m) {
            for (int note : { 48, 52, 55 })
                at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)));
            for (int note : { 48, 52, 55 })
                at (m, pos, n, 48000.0, 0.8, juce::MidiMessage::noteOff (1, note));
        });
        CHECK (run.finite);
        CHECK (run.noteOns >= 6);
        CHECK (p.activeVoices.load() <= 1);
        CHECK (settle (p) == 0);
    }
    SECTION ("blocks of 1 and 4096 samples, and empty blocks")
    {
        for (int block : { 1, 4096 })
        {
            OspAudioProcessor p;
            loadAndWait (p, file);
            p.setParameterValue ("release", 60.0f);
            arpOn (p, 0, 3);
            const auto run = renderArp (p, 48000.0, block, 0.5, [] (int pos, int n, juce::MidiBuffer& m) {
                at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)));
                at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, 64, static_cast<juce::uint8> (100)));
            });
            CHECK (run.noteOns == 8);   // 1/32 at 120 BPM: every 3000 samples
            juce::AudioBuffer<float> empty (2, 0);
            juce::MidiBuffer none;
            p.processBlock (empty, none);
            renderArp (p, 48000.0, block, 0.1, [] (int pos, int, juce::MidiBuffer& m) {
                if (pos == 0)
                    m.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            });
            CHECK (settle (p) == 0);
        }
    }
}

TEST_CASE ("plugin: ARP costs next to nothing on the audio thread", "[plugin][arp]")
{
    // No sound loaded: the block is the MIDI path alone (the arpeggiator at its busiest, 1/32
    // CHORD over four octaves with 16 notes held), against the same with ARP off.
    auto timeIt = [] (bool enabled) {
        OspAudioProcessor p;
        if (enabled)
            arpOn (p, 5, 3, 150.0f, 4);
        p.prepareToPlay (48000.0, 128);
        juce::AudioBuffer<float> buffer (2, 128);
        double total = 0.0;
        const int blocks = 48000 * 4 / 128;
        for (int i = 0; i < blocks; ++i)
        {
            juce::MidiBuffer midi;
            if (i == 0)
                for (int n = 0; n < 16; ++n)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 40 + n * 2, static_cast<juce::uint8> (90)), 0);
            buffer.clear();
            const auto t0 = std::chrono::steady_clock::now();
            p.processBlock (buffer, midi);
            total += std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
        }
        return total / blocks;
    };
    const double off = timeIt (false), on = timeIt (true);
    const double budget = 1.0e6 * 128 / 48000.0;
    std::printf ("ARP stage: off %.2f us, on %.2f us per 128-sample block (%.3f %% of real time)\n", off, on, 100.0 * (on - off) / budget);
    CHECK (on - off < 0.02 * budget);   // well under 2 % of one core
}

// ARP GUI (hidden, needs a display: xvfb-run): the control by the keyboard, the inline editor,
// the window growing and shrinking by the editor's height, and the screenshots for review
// (OSP_SNAPSHOT_DIR): 01 closed and off, 02 closed and on, 03 open and playing, 04 open and
// off, the PATTERN and RATE menus, and the open editor with one, two and three sounds.
TEST_CASE ("plugin: ARP keyboard control and inline editor", "[.][arp-ui]")
{
    TempDir tmp;
    const auto a = writeSource (tmp.dir, "Vowel A3.wav", testsignals::vowel (midiToHz (57), 3.0, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "Saw C3.wav", testsignals::saw (midiToHz (48), 2.0, 48000.0));
    const auto c = writeSource (tmp.dir, "Pluck C4.wav", testsignals::pluck (midiToHz (60), 2.0, 48000.0, 5));
    OspAudioProcessor p;
    loadAndWait (p, a);
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditorIfNeeded());
    auto* editor = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (base.get());
    REQUIRE (editor != nullptr);
    editor->refreshNow();
    auto snapshot = [&] (const juce::String& name) {
        if (const char* dir = std::getenv ("OSP_SNAPSHOT_DIR"))
        {
            editor->refreshNow();
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream out (juce::File (dir).getChildFile (name));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    };
    auto play = [&] (bool hold) {
        // A few blocks with a chord held: the scheduler's step display has something to show.
        p.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buffer (2, 512);
        for (int i = 0; i < 60; ++i)
        {
            juce::MidiBuffer midi;
            if (i == 0 && hold)
                for (int note : { 57, 60, 64, 69 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)), 0);
            buffer.clear();
            p.processBlock (buffer, midi);
        }
    };

    const int width = editor->getWidth(), height = editor->getHeight();
    CHECK_FALSE (editor->isArpExpanded());
    CHECK_FALSE (editor->arpInlinePanel().isVisible());
    CHECK (editor->arpControl().statusText().contains ("UP"));
    CHECK (editor->arpControl().statusText().contains ("1/8"));
    snapshot ("arp-01-closed-off.png");

    // The light switches the arpeggiator, nothing else.
    editor->arpControl().toggleEnabled();
    CHECK (p.parameterValue ("arp.enabled") > 0.5f);
    CHECK_FALSE (editor->isArpExpanded());
    CHECK (editor->getHeight() == height);
    snapshot ("arp-02-closed-on.png");

    // The chevron shows the editor, nothing else: the window grows by its height, keeps its width.
    const auto keyboardBefore = editor->arpControl().getBounds();
    editor->arpControl().onToggleEditor();
    CHECK (editor->isArpExpanded());
    CHECK (p.arpEditorExpanded());
    CHECK (p.parameterValue ("arp.enabled") > 0.5f);
    CHECK (editor->arpInlinePanel().isVisible());
    CHECK (editor->getWidth() == width);
    const float scale = static_cast<float> (width) / osp::plugin::design::width;
    CHECK (std::abs (editor->getHeight() - (height + osp::plugin::design::layout::arpShift * scale)) <= 1.5f);
    CHECK (editor->arpControl().getBounds().getY() == keyboardBefore.getY() + juce::roundToInt (osp::plugin::design::layout::arpShift));
    // No dead space: the panel sits between the macros and the keyboard row.
    CHECK (editor->arpInlinePanel().getBottom() < editor->arpControl().getY());
    play (true);
    editor->refreshNow();
    CHECK (p.arpView().active);
    CHECK (editor->arpInlinePanel().litColumn() == p.arpView().current);
    CHECK (editor->arpInlinePanel().litColumn() >= 0);
    snapshot ("arp-03-open-playing.png");

    // Off with the editor open: the editor stays, the light goes out.
    editor->arpControl().toggleEnabled();
    CHECK (p.parameterValue ("arp.enabled") < 0.5f);
    CHECK (editor->isArpExpanded());
    play (false);
    snapshot ("arp-04-open-off.png");
    editor->arpControl().toggleEnabled();
    play (true);

    // The menus: every pattern; the rates in three groups.
    CHECK (editor->arpInlinePanel().patternMenu().getNumItems() == 6);
    CHECK (editor->arpInlinePanel().rateMenu().getNumItems() == 13);   // 10 rates + 3 group headers
    if (const char* dir = std::getenv ("OSP_SNAPSHOT_DIR"))
        for (int which = 0; which < 2; ++which)
        {
            // The menus drawn by the editor's own look and feel, item by item, as JUCE draws
            // them in their window (a menu window cannot open under the test display server).
            const auto menu = which == 0 ? editor->arpInlinePanel().patternMenu() : editor->arpInlinePanel().rateMenu();
            auto& laf = editor->getLookAndFeel();
            struct Row
            {
                juce::String text;
                bool header = false, ticked = false;
                int height = 0;
            };
            std::vector<Row> rows;
            int width = 120, height = 8;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            {
                const auto& item = it.getItem();
                int w = 0, h = 0;
                laf.getIdealPopupMenuItemSize (item.text, false, -1, w, h);
                if (item.isSectionHeader)
                    h = std::max (h, 26);
                rows.push_back ({ item.text, item.isSectionHeader, item.isTicked, h });
                width = std::max (width, w + 40);
                height += h;
            }
            juce::Image image (juce::Image::ARGB, width, height + 8, true);
            juce::Graphics g (image);
            laf.drawPopupMenuBackground (g, width, height + 8);
            int y = 8;
            for (const auto& row : rows)
            {
                const juce::Rectangle<int> area (0, y, width, row.height);
                if (row.header)
                    laf.drawPopupMenuSectionHeader (g, area, row.text);
                else
                    laf.drawPopupMenuItem (g, area, false, true, false, row.ticked, false, row.text, {}, nullptr, nullptr);
                y += row.height;
            }
            juce::FileOutputStream out (juce::File (dir).getChildFile (which == 0 ? "arp-05-pattern-menu.png" : "arp-06-rate-menu.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    for (int pattern : { 2, 4, 5 })
    {
        p.setParameterValue ("arp.pattern", static_cast<float> (pattern));
        p.setParameterValue ("arp.octaves", 2.0f);
        play (true);
        snapshot ("arp-07-pattern-" + juce::String (pattern) + ".png");
    }
    snapshot ("arp-08-layers-1.png");
    for (const auto& file : { b, c })
    {
        p.addLayers (juce::Array<juce::File> { file });
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        snapshot (file == b ? "arp-08-layers-2.png" : "arp-08-layers-3.png");
    }

    // Saved with the session; a new editor opens it the way it was.
    base.reset();
    std::unique_ptr<juce::AudioProcessorEditor> again (p.createEditorIfNeeded());
    auto* reopened = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (again.get());
    REQUIRE (reopened != nullptr);
    CHECK (reopened->isArpExpanded());
    CHECK (std::abs (reopened->getHeight() - (height + osp::plugin::design::layout::arpShift * scale)) <= 1.5f);
    // Hidden again: back to the original window, nothing left over.
    reopened->arpControl().onToggleEditor();
    CHECK_FALSE (reopened->isArpExpanded());
    CHECK (reopened->getHeight() == height);
    CHECK (reopened->getWidth() == width);
    CHECK_FALSE (reopened->arpInlinePanel().isVisible());
    // The Advanced popover still opens from the small ADVANCED link.
    reopened->openPopup (osp::plugin::OspAudioProcessorEditor::advancedPopup);
    CHECK (reopened->openPopupIndex() == osp::plugin::OspAudioProcessorEditor::advancedPopup);
    reopened->closePopup();
}
