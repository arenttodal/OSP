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

// Needs a display (run under xvfb-run on headless Linux). Hidden by default:
//   OSP_SNAPSHOT_DIR=/tmp xvfb-run ./osp_plugin_tests "[ui]"
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
