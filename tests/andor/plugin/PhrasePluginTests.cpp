#define CATCH_CONFIG_RUNNER
#include "PluginTestUtil.h"

#include "PhraseEditor.h"
#include "PhraseProcessor.h"

#include "andor/dsp/OfflineRender.h"
#include "andor/io/NoteAtlasJson.h"
#include "andor/phrase/PhraseEdits.h"
#include "andor/phrase/PhraseFixtures.h"
#include "andor/AndorTestUtil.h"
#include "core/PitchMath.h"
#include "io/AudioFileIO.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

ANDOR_DEFINE_ALLOCATION_COUNTER

#include <thread>

using namespace osp;
using namespace osp::andorlab;
using Catch::Approx;

namespace
{
    constexpr double sr = 48000.0;

    juce::File writeFixture (const juce::File& dir, const std::string& name)
    {
        const auto fx = phrase::makePhraseFixture (name);
        const auto file = dir.getChildFile (juce::String (name) + ".wav");
        std::string error;
        REQUIRE (io::writeAudioFile (file.getFullPathName().toStdString(), fx.audio, io::SampleFormat::pcm24, error));
        return file;
    }

    MidiSequence melody()
    {
        MidiSequence seq;
        const int keys[] = { 62, 64, 66, 69, 67, 66, 64, 62 };
        for (int i = 0; i < 8; ++i)
            andor::addNote (seq, 0.3 * i, 0.28, keys[i], 90);
        andor::addNote (seq, 2.5, 1.0, 62, 80);
        andor::addNote (seq, 2.5, 1.0, 66, 80);
        andor::addNote (seq, 2.5, 1.0, 69, 80);
        return seq;
    }

    AudioData toAudio (const juce::AudioBuffer<float>& b, double rate = sr)
    {
        auto a = AudioData::allocate (2, b.getNumSamples(), rate);
        for (int ch = 0; ch < 2; ++ch)
            std::copy (b.getReadPointer (ch), b.getReadPointer (ch) + b.getNumSamples(), a.channels[static_cast<std::size_t> (ch)].begin());
        return a;
    }
}

TEST_CASE ("ANDOR Phrase plugin: its own identity", "[phrase-plugin]")
{
    PhraseProcessor p;
    CHECK (p.getName() == "ANDOR Phrase");
    CHECK (juce::String (JucePlugin_Name) == "ANDOR Phrase");
    CHECK (juce::String (JUCE_STRINGIFY (JucePlugin_CFBundleIdentifier)) == "com.osp.andorphrase");
    CHECK (JucePlugin_PluginCode != 'Osp1');   // ANDOR/OSP
    CHECK (JucePlugin_PluginCode != 'Adrm');   // ANDOR Drums
    CHECK (JucePlugin_PluginCode != 'Agrn');   // ANDOR Grain
    CHECK (JucePlugin_PluginCode == 'Aphr');
    CHECK (JucePlugin_IsSynth == 1);
    CHECK (PhraseProcessor::presetFolder().getFileName() == "Phrase Presets");
    CHECK (juce::String (PhraseProcessor::stateTag) == "ANDOR_PHRASE_STATE");
}

TEST_CASE ("ANDOR Phrase plugin: drop a phrase, analyse, play it chromatically and in chords", "[phrase-plugin]")
{
    plugintest::IsolatedFolders folders;
    const auto file = writeFixture (folders.root, "vocal-phrase");
    PhraseProcessor p;
    p.loadFile (file);
    CHECK (p.isBusy());
    REQUIRE (p.waitForLoad (30000));
    REQUIRE (p.hasSource());
    CHECK (p.document().atlas.notes.size() == 8);
    CHECK (p.document().atlas.sources[0].fileName == "vocal-phrase.wav");
    CHECK (p.status().contains ("notes"));

    const auto out = plugintest::renderThrough (p, melody(), sr, 256, 4.5);
    REQUIRE (plugintest::finite (out));
    CHECK (plugintest::rms (out, sr, 0.05, 0.25) > 0.01);
    const auto audio = toAudio (out);
    CHECK (std::abs (1200.0 * std::log2 (andortest::pitchOf (audio, 0.05, 0.25) / midiToHz (62))) < 6.0);
    CHECK (plugintest::rms (out, sr, 2.6, 3.4) > plugintest::rms (out, sr, 0.05, 0.25));   // chord louder than one note
}

TEST_CASE ("ANDOR Phrase plugin: sessions reopen with the analysis and edits, without re-analysing", "[phrase-plugin]")
{
    plugintest::IsolatedFolders folders;
    const auto file = writeFixture (folders.root, "vocal-phrase");
    juce::MemoryBlock saved;
    std::string savedAtlas;
    juce::AudioBuffer<float> before;
    {
        PhraseProcessor a;
        a.loadFile (file);
        REQUIRE (a.waitForLoad (30000));
        const auto first = a.document().atlas.notes[0].id;
        REQUIRE (a.edit ("Octave", [&] (atlas::NoteAtlas& at) { return phrase::edit::setRoot (at, first, at.findNote (first)->rootMidi + 12); }));
        const auto audio = a.document().source->audio;
        const auto second = a.document().atlas.notes[2].id;
        REQUIRE (a.edit ("Split", [&] (atlas::NoteAtlas& at) { return ! phrase::edit::split (at, second, (at.findNote (second)->attackStart + at.findNote (second)->sourceEnd) / 2, *audio).empty(); }));
        a.state.getParameter ("fill")->setValueNotifyingHost (a.state.getParameter ("fill")->convertTo0to1 (3.0f));
        a.state.getParameter ("sustainCharacter")->setValueNotifyingHost (0.3f);
        a.getStateInformation (saved);
        savedAtlas = io::noteAtlasToString (a.document().atlas);
        before = plugintest::renderThrough (a, melody(), sr, 256, 4.0);
    }
    // The original file is gone: the managed store still has the bytes.
    file.deleteFile();
    PhraseProcessor b;
    b.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (b.waitForLoad (30000));
    REQUIRE (b.hasSource());
    CHECK (io::noteAtlasToString (b.document().atlas) == savedAtlas);   // exactly the saved edits
    CHECK (b.document().atlas.notes[0].corrections.pitch);
    CHECK (juce::roundToInt (b.state.getParameter ("fill")->convertFrom0to1 (b.state.getParameter ("fill")->getValue())) == 3);
    const auto after = plugintest::renderThrough (b, melody(), sr, 256, 4.0);
    CHECK (plugintest::identical (before, after));   // the same instrument, sample for sample
}

TEST_CASE ("ANDOR Phrase plugin: a missing source keeps the atlas and can be relinked", "[phrase-plugin]")
{
    plugintest::IsolatedFolders folders;
    const auto file = writeFixture (folders.root, "clean-scale");
    juce::MemoryBlock saved;
    {
        PhraseProcessor a;
        a.loadFile (file);
        REQUIRE (a.waitForLoad (30000));
        a.getStateInformation (saved);
    }
    // Remove both the store copy and the original.
    const auto moved = folders.root.getChildFile ("moved.wav");
    REQUIRE (file.moveFileTo (moved));
    juce::File (juce::SystemStats::getEnvironmentVariable ("ANDOR_SAMPLE_STORE", {})).deleteRecursively();

    PhraseProcessor b;
    b.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (b.waitForLoad (30000));
    CHECK_FALSE (b.hasSource());
    CHECK (b.document().sourceMissing);
    CHECK (b.document().atlas.notes.size() == 8);
    CHECK (b.document().missingName == "clean-scale.wav");
    // Silence, not a crash, while the audio is missing.
    MidiSequence seq;
    andor::addNote (seq, 0.0, 0.5, 60);
    CHECK (plugintest::rms (plugintest::renderThrough (b, seq, sr, 128, 0.6), sr, 0.0, 0.6) < 1.0e-12);

    std::string error;
    REQUIRE (b.relink (moved, error));
    REQUIRE (b.waitForLoad (30000));
    CHECK (b.hasSource());
    CHECK_FALSE (b.document().sourceMissing);
    CHECK (b.document().atlas.notes.size() == 8);
    CHECK (plugintest::rms (plugintest::renderThrough (b, seq, sr, 128, 0.6), sr, 0.05, 0.4) > 0.01);

    // A file of another length is refused.
    auto other = phrase::makePhraseFixture ("staccato").audio;
    const auto wrong = folders.root.getChildFile ("wrong.wav");
    REQUIRE (io::writeAudioFile (wrong.getFullPathName().toStdString(), other, io::SampleFormat::pcm16, error));
    CHECK_FALSE (b.relink (wrong, error));
}

TEST_CASE ("ANDOR Phrase plugin: the analysis cache, undo/redo, foreign states", "[phrase-plugin]")
{
    plugintest::IsolatedFolders folders;
    const auto file = writeFixture (folders.root, "repeated");
    PhraseProcessor p;
    p.loadFile (file);
    REQUIRE (p.waitForLoad (30000));
    const auto store = juce::File (juce::SystemStats::getEnvironmentVariable ("ANDOR_SAMPLE_STORE", {}));
    CHECK (store.findChildFiles (juce::File::findFiles, false, "*.andoratlas").size() == 1);
    const auto analysed = io::noteAtlasToString (p.document().atlas);
    p.loadFile (file);   // the second time comes from the cache
    REQUIRE (p.waitForLoad (30000));
    CHECK (io::noteAtlasToString (p.document().atlas) == analysed);

    const auto id = p.document().atlas.notes[0].id;
    REQUIRE (p.edit ("Exclude", [&] (atlas::NoteAtlas& a) { return phrase::edit::setIncluded (a, id, false); }));
    CHECK_FALSE (p.document().atlas.findNote (id)->included);
    REQUIRE (p.model()->keyRegions[55].size() == 7);
    REQUIRE (p.undoManager().undo());
    CHECK (p.document().atlas.findNote (id)->included);
    CHECK (p.model()->keyRegions[55].size() == 8);
    REQUIRE (p.undoManager().redo());
    CHECK_FALSE (p.document().atlas.findNote (id)->included);
    CHECK_FALSE (p.edit ("Nothing", [] (atlas::NoteAtlas&) { return false; }));

    // Another product's state, garbage and an empty block are ignored without harm.
    juce::ValueTree grainState ("ANDOR_GRAIN_STATE");
    juce::MemoryBlock foreign;
    p.copyXmlToBinary (*grainState.createXml(), foreign);
    p.setStateInformation (foreign.getData(), static_cast<int> (foreign.getSize()));
    p.setStateInformation ("garbage", 7);
    p.setStateInformation (nullptr, 0);
    CHECK (p.hasSource());
    CHECK (p.document().atlas.notes.size() == 8);
}

TEST_CASE ("ANDOR Phrase plugin: DETECTION and MIN NOTE re-analyse, keep corrections, undo, recall", "[phrase-plugin]")
{
    plugintest::IsolatedFolders folders;
    const auto file = writeFixture (folders.root, "repeated");   // 4 separate G3s, then 4 re-articulated legato
    PhraseProcessor p;
    p.loadFile (file);
    REQUIRE (p.waitForLoad (30000));
    REQUIRE (p.document().atlas.notes.size() == 8);
    CHECK (p.analysedSettings().digest() == p.analysisSettings().digest());

    // A correction made before trying a setting survives it.
    const auto first = p.document().atlas.notes[0].id;
    REQUIRE (p.edit ("Pitch", [&] (atlas::NoteAtlas& a) { return phrase::edit::setRoot (a, first, 56); }));

    auto set = [&] (const char* id, float value)
    {
        auto* param = p.state.getParameter (id);
        REQUIRE (param != nullptr);
        CHECK_FALSE (param->isAutomatable());
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    set ("detection", 0.0f);   // FEWER: the 12 dB re-articulations no longer split
    p.applyAnalysisSettingsNow();
    REQUIRE (p.waitForLoad (30000));
    CHECK (p.document().atlas.notes.size() == 5);
    CHECK (p.document().atlas.findNote (first)->rootMidi == 56);
    CHECK (p.undoManager().getUndoDescription() == "Detection: fewer notes");
    CHECK (p.analysedSettings().detection == phrase::Detection::fewer);

    // UNDO takes the notes and the control back, with nothing left to re-analyse.
    REQUIRE (p.undoManager().undo());
    CHECK (p.document().atlas.notes.size() == 8);
    CHECK (p.analysisSettings().detection == phrase::Detection::normal);
    CHECK (p.analysedSettings().digest() == p.analysisSettings().digest());
    CHECK_FALSE (p.isBusy());
    REQUIRE (p.undoManager().redo());
    CHECK (p.document().atlas.notes.size() == 5);
    CHECK (p.analysisSettings().detection == phrase::Detection::fewer);

    // MIN NOTE.
    set ("detection", 1.0f);
    set ("minNote", 0.45f);
    p.applyAnalysisSettingsNow();
    REQUIRE (p.waitForLoad (30000));
    CHECK (p.document().atlas.notes.size() == 6);

    // The session reopens with these settings and these notes, without analysing again.
    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    PhraseProcessor q;
    q.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (q.waitForLoad (30000));
    CHECK (q.document().atlas.notes.size() == 6);
    CHECK (q.analysisSettings().minPieceSeconds == Approx (0.45).margin (0.001));
    CHECK (q.analysedSettings().digest() == q.analysisSettings().digest());
}

TEST_CASE ("ANDOR Phrase plugin: presets and the exported Note Atlas", "[phrase-plugin]")
{
    plugintest::IsolatedFolders folders;
    const auto file = writeFixture (folders.root, "legato");
    PhraseProcessor p;
    p.loadFile (file);
    REQUIRE (p.waitForLoad (30000));
    p.state.getParameter ("playMode")->setValueNotifyingHost (0.0f);
    std::string error;
    const auto preset = PhraseProcessor::presetFolder().getChildFile ("Legato.andorphrase");
    REQUIRE (p.savePreset (preset, error));
    CHECK (preset.existsAsFile());
    CHECK (preset.getParentDirectory().getParentDirectory() == juce::File (juce::SystemStats::getEnvironmentVariable ("ANDOR_DOCUMENTS_DIR", {})));

    PhraseProcessor q;
    REQUIRE (q.loadPreset (preset, error));
    REQUIRE (q.waitForLoad (30000));
    CHECK (q.document().atlas.notes.size() == 6);
    CHECK (q.state.getParameter ("playMode")->getValue() < 1.0e-6f);

    const auto exported = folders.root.getChildFile ("legato.andoratlas");
    REQUIRE (p.exportAtlas (exported, error));
    const auto atlas = io::readNoteAtlas (exported.getFullPathName().toStdString(), error);
    REQUIRE (atlas);
    CHECK (atlas->notes.size() == 6);
    CHECK (atlas->createdBy.rfind ("ANDOR Phrase", 0) == 0);
    CHECK (atlas->sources[0].relativePath == "legato.wav");
    const auto resolved = io::resolveSource (atlas->sources[0], exported.getParentDirectory().getFullPathName().toStdString());
    REQUIRE (resolved);
    CHECK (resolved->match == atlas::RelinkResult::exact);
}

TEST_CASE ("ANDOR Phrase plugin: host block sizes and sample rates", "[phrase-plugin]")
{
    plugintest::IsolatedFolders folders;
    PhraseProcessor p;
    p.loadFile (writeFixture (folders.root, "vibrato"));
    REQUIRE (p.waitForLoad (30000));
    // E4 alone first (pitch measured over whole vibrato cycles), then a second note.
    MidiSequence seq;
    andor::addNote (seq, 0.0, 1.3, 64);
    andor::addNote (seq, 1.3, 0.4, 67);
    for (double rate : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int block : { 1, 32, 333, 1024, 4096 })
        {
            INFO (rate << " / " << block);
            if (block == 1 && rate > 50000.0)
                continue;   // (slow; covered at the lower rates)
            const auto out = plugintest::renderThrough (p, seq, rate, block, 1.8);
            REQUIRE (plugintest::finite (out));
            CHECK (plugintest::rms (out, rate, 0.1, 1.2) > 0.01);
            const auto pitch = andortest::pitchOf (toAudio (out, rate), 0.2, 1.2);
            CHECK (std::abs (1200.0 * std::log2 (pitch / midiToHz (64))) < 15.0);
        }
}

TEST_CASE ("ANDOR Phrase plugin: edits and loads while the audio thread plays", "[phrase-plugin]")
{
    plugintest::IsolatedFolders folders;
    PhraseProcessor p;
    p.loadFile (writeFixture (folders.root, "vocal-phrase"));
    REQUIRE (p.waitForLoad (30000));
    p.setPlayConfigDetails (0, 2, sr, 128);
    p.prepareToPlay (sr, 128);
    std::atomic<bool> stop { false };
    std::atomic<bool> bad { false };
    std::thread audio ([&]
                       {
                           juce::AudioBuffer<float> buffer (2, 128);
                           int i = 0;
                           while (! stop)
                           {
                               juce::MidiBuffer midi;
                               if (i % 20 == 0)
                                   midi.addEvent (juce::MidiMessage::noteOn (1, 60 + (i / 20) % 12, 0.8f), 0);
                               if (i % 20 == 10)
                                   midi.addEvent (juce::MidiMessage::allNotesOff (1), 5);
                               buffer.clear();
                               p.processBlock (buffer, midi);
                               for (int s = 0; s < 128; ++s)
                                   if (! std::isfinite (buffer.getSample (0, s)))
                                       bad = true;
                               ++i;
                           }
                       });
    const auto audioData = p.document().source->audio;
    for (int round = 0; round < 6; ++round)
    {
        const auto id = p.document().atlas.notes[static_cast<std::size_t> (round % 4)].id;
        p.edit ("Split", [&] (atlas::NoteAtlas& a)
                {
                    const auto* n = a.findNote (id);
                    return ! phrase::edit::split (a, id, (n->attackStart + n->sourceEnd) / 2, *audioData).empty();
                });
        p.undoManager().undo();
        p.edit ("Key", [&] (atlas::NoteAtlas& a) { return phrase::edit::assignKey (a, id, 72); });
        p.pollLoader();
    }
    p.loadFile (writeFixture (folders.root, "clean-scale"));
    REQUIRE (p.waitForLoad (30000));
    stop = true;
    audio.join();
    CHECK_FALSE (bad);
    CHECK (p.document().atlas.notes.size() == 8);
}

TEST_CASE ("ANDOR Phrase plugin: the editor", "[phrase-plugin][editor]")
{
    plugintest::IsolatedFolders folders;
    PhraseProcessor p;
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        editor->setSize (PhraseEditor::designWidth, PhraseEditor::designHeight);
        plugintest::screenshot (*editor, "phrase-01-default.png");
        p.loadFile (writeFixture (folders.root, "vocal-phrase"));
        REQUIRE (p.waitForLoad (30000));
        auto* pe = dynamic_cast<PhraseEditor*> (editor.get());
        REQUIRE (pe != nullptr);
        // Play a chord so the read heads and sounding keys show.
        MidiSequence seq;
        andor::addNote (seq, 0.0, 2.0, 64);
        andor::addNote (seq, 0.0, 2.0, 69);
        plugintest::renderThrough (p, seq, sr, 512, 0.8);
        pe->refreshNow();
        plugintest::screenshot (*editor, "phrase-02-analysed.png");

        const auto id = p.document().atlas.notes[2].id;
        pe->select (id);
        pe->waveform().cursor = (p.document().atlas.notes[2].attackStart + p.document().atlas.notes[2].sourceEnd) / 2;
        plugintest::screenshot (*editor, "phrase-03-note-correction.png");

        p.state.getParameter ("fill")->setValueNotifyingHost (p.state.getParameter ("fill")->convertTo0to1 (3.0f));
        pe->select (p.document().atlas.notes[0].id);
        editor->repaint();
        plugintest::screenshot (*editor, "phrase-04-keyboard-mapping.png");
        editor->setSize (PhraseEditor::designWidth * 3 / 4, PhraseEditor::designHeight * 3 / 4);   // resizable, scaled
        plugintest::screenshot (*editor, "phrase-05-small.png");
        p.editorBeingDeleted (editor.get());
    }
    CHECK (true);
}

TEST_CASE ("ANDOR Phrase plugin: editor screenshot of a user recording", "[.recording-screenshot]")
{
    // Headless review of a reported recording (never committed):
    //   ANDOR_RECORDING=take.wav ANDOR_SCREENSHOT_DIR=out xvfb-run -a andor_phrase_plugin_tests "[.recording-screenshot]"
    //   (optional: ANDOR_DETECTION=0|1|2 for FEWER / NORMAL / MORE, ANDOR_MIN_NOTE=<seconds>)
    const auto recording = juce::SystemStats::getEnvironmentVariable ("ANDOR_RECORDING", {});
    if (recording.isEmpty())
        return;
    plugintest::IsolatedFolders folders;
    PhraseProcessor p;
    const auto detectionText = juce::SystemStats::getEnvironmentVariable ("ANDOR_DETECTION", "1");
    p.state.getParameter ("detection")->setValueNotifyingHost (p.state.getParameter ("detection")->convertTo0to1 (detectionText.getFloatValue()));
    const auto minNoteText = juce::SystemStats::getEnvironmentVariable ("ANDOR_MIN_NOTE", "0.25");
    p.state.getParameter ("minNote")->setValueNotifyingHost (p.state.getParameter ("minNote")->convertTo0to1 (minNoteText.getFloatValue()));
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    editor->setSize (PhraseEditor::designWidth, PhraseEditor::designHeight);
    p.loadFile (juce::File (recording));
    REQUIRE (p.waitForLoad (60000));
    dynamic_cast<PhraseEditor*> (editor.get())->refreshNow();
    plugintest::screenshot (*editor, (juce::File (recording).getFileNameWithoutExtension() + ".png").toStdString());
    p.editorBeingDeleted (editor.get());
}

TEST_CASE ("ANDOR Phrase plugin: processBlock never allocates", "[phrase-plugin][realtime]")
{
    plugintest::IsolatedFolders folders;
    PhraseProcessor p;
    p.loadFile (writeFixture (folders.root, "vocal-phrase"));
    REQUIRE (p.waitForLoad (30000));
    p.state.getParameter ("space")->setValueNotifyingHost (0.5f);
    p.state.getParameter ("echo")->setValueNotifyingHost (0.5f);
    p.state.getParameter ("sustainCharacter")->setValueNotifyingHost (0.5f);
    p.state.getParameter ("fill")->setValueNotifyingHost (1.0f);
    CHECK (plugintest::allocationsWhilePlaying (p, 400) == 0);
}

// Hidden: `andor_phrase_plugin_tests "[.demo-presets]"` with ANDOR_DEMO_DIR set writes demo
// presets and their generated (rights-free) recordings there. Run it on the machine that will
// use them (presets find the audio by its path and fingerprint).
TEST_CASE ("ANDOR Phrase plugin: demo presets", "[.demo-presets]")
{
    const auto dirText = juce::SystemStats::getEnvironmentVariable ("ANDOR_DEMO_DIR", {});
    if (dirText.isEmpty())
        return;
    const juce::File dir (dirText);
    const auto audio = dir.getChildFile ("Audio");
    audio.createDirectory();
    struct Demo
    {
        const char* name;
        const char* fixture;
        std::vector<std::pair<const char*, float>> params;
    };
    const std::vector<Demo> demos {
        { "Sung Phrase - Sustain", "vocal-phrase", { { "playMode", 2.0f }, { "space", 0.25f }, { "release", 0.6f } } },
        { "Sung Phrase - Bloom", "vocal-phrase", { { "playMode", 2.0f }, { "sustainCharacter", 0.65f }, { "space", 0.45f }, { "attack", 0.08f }, { "release", 1.6f } } },
        { "Sung Phrase - Synthetic Half", "vocal-phrase", { { "synthetic", 0.5f }, { "tone", 0.7f }, { "echo", 0.25f } } },
        { "Legato Line - Gate", "legato", { { "playMode", 1.0f }, { "fill", 3.0f } } },
        { "Staccato - One Shot", "staccato", { { "playMode", 0.0f }, { "roundRobin", 1.0f }, { "space", 0.2f } } },
    };
    for (const auto& d : demos)
    {
        const auto file = audio.getChildFile (juce::String (d.fixture) + ".wav");
        if (! file.existsAsFile())
        {
            std::string error;
            REQUIRE (io::writeAudioFile (file.getFullPathName().toStdString(), phrase::makePhraseFixture (d.fixture).audio, io::SampleFormat::pcm24, error));
        }
        PhraseProcessor p;
        p.loadFile (file);
        REQUIRE (p.waitForLoad (30000));
        for (const auto& [id, value] : d.params)
            p.state.getParameter (id)->setValueNotifyingHost (p.state.getParameter (id)->convertTo0to1 (value));
        std::string error;
        REQUIRE (p.savePreset (dir.getChildFile ("Phrase Presets").getChildFile (juce::String (d.name) + PhraseProcessor::presetExtension), error));
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    return Catch::Session().run (argc, argv);
}
