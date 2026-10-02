// Headless tests for the plugin processor: the same object a DAW would host.
// Covers: import -> analysis -> chromatic playback, sample-rate independence,
// root override, session state recall (including from the managed sample store after
// the original file disappears), and bad files.

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "analysis/Analyzer.h"
#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
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
    CHECK (p.parameters.getParameter ("space")->convertFrom0to1 (p.parameters.getParameter ("space")->getValue()) == Approx (75.0f));
    CHECK (p.parameters.getParameter ("release")->convertFrom0to1 (p.parameters.getParameter ("release")->getValue()) == Approx (3000.0f).margin (0.5));
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
