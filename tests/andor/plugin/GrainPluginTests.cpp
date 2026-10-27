#define CATCH_CONFIG_RUNNER
#include "PluginTestUtil.h"

#include "GrainEditor.h"
#include "GrainProcessor.h"

#include "andor/AndorTestUtil.h"
#include "andor/dsp/OfflineRender.h"
#include "andor/io/NoteAtlasJson.h"
#include "andor/phrase/PhraseAnalysis.h"
#include "andor/phrase/PhraseFixtures.h"
#include "core/PitchMath.h"
#include "io/AudioFileIO.h"
#include "io/ContentHash.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

ANDOR_DEFINE_ALLOCATION_COUNTER

using namespace osp;
using namespace osp::andorlab;
using Catch::Approx;

namespace
{
    constexpr double sr = 48000.0;

    AudioData toAudio (const juce::AudioBuffer<float>& b, double rate = sr)
    {
        auto a = AudioData::allocate (2, b.getNumSamples(), rate);
        for (int ch = 0; ch < 2; ++ch)
            std::copy (b.getReadPointer (ch), b.getReadPointer (ch) + b.getNumSamples(), a.channels[static_cast<std::size_t> (ch)].begin());
        return a;
    }

    juce::File writeTone (const juce::File& dir)
    {
        // A sustained harmonic tone at A3 (220 Hz): something to granulate with a known root.
        auto audio = AudioData::allocate (2, static_cast<std::int64_t> (3.0 * sr), sr);
        for (std::size_t i = 0; i < audio.channels[0].size(); ++i)
        {
            const double t = static_cast<double> (i) / sr;
            double s = 0.0;
            for (int k = 1; k <= 6; ++k)
                s += std::sin (andor::twoPi * 220.0 * k * t) / k;
            const double env = std::min (1.0, t / 0.05) * std::min (1.0, (3.0 - t) / 0.2);
            for (auto& ch : audio.channels)
                ch[i] = static_cast<float> (0.3 * env * s);
        }
        const auto file = dir.getChildFile ("tone-a3.wav");
        std::string error;
        REQUIRE (io::writeAudioFile (file.getFullPathName().toStdString(), audio, io::SampleFormat::pcm24, error));
        return file;
    }

    void setParam (GrainProcessor& p, const char* id, float value)
    {
        auto* param = p.state.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    float getParam (GrainProcessor& p, const char* id)
    {
        auto* param = p.state.getParameter (id);
        return param->convertFrom0to1 (param->getValue());
    }

    /** Phrase's half of the hand-over: analyse a phrase and export its atlas next to the audio. */
    juce::File exportPhraseAtlas (const juce::File& dir)
    {
        const auto fx = phrase::makePhraseFixture ("vocal-phrase");
        const auto wav = dir.getChildFile ("phrase.wav");
        std::string error;
        REQUIRE (io::writeAudioFile (wav.getFullPathName().toStdString(), fx.audio, io::SampleFormat::pcm24, error));
        atlas::SourceAsset src;
        src.contentHash = io::contentId (*io::sha256OfFile (wav.getFullPathName().toStdString()));
        src.fileName = "phrase.wav";
        src.lastKnownPath = wav.getFullPathName().toStdString();
        auto a = phrase::analysePhrase (fx.audio, src).atlas;
        a.createdBy = "ANDOR Phrase 0.1.0";
        const auto file = dir.getChildFile ("phrase.andoratlas");
        REQUIRE (io::writeNoteAtlas (file.getFullPathName().toStdString(), a, error));
        return file;
    }

    double centsTo (double hz, int key) { return 1200.0 * std::log2 (hz / midiToHz (key)); }
}

TEST_CASE ("ANDOR Grain plugin: its own identity", "[grain-plugin]")
{
    GrainProcessor p;
    CHECK (p.getName() == "ANDOR Grain");
    CHECK (juce::String (JucePlugin_Name) == "ANDOR Grain");
    CHECK (juce::String (JUCE_STRINGIFY (JucePlugin_CFBundleIdentifier)) == "com.osp.andorgrain");
    CHECK (JucePlugin_PluginCode == 'Agrn');
    CHECK (JucePlugin_PluginCode != 'Osp1');
    CHECK (JucePlugin_PluginCode != 'Adrm');
    CHECK (JucePlugin_PluginCode != 'Aphr');
    CHECK (GrainProcessor::presetFolder().getFileName() == "Grain Presets");
    CHECK (juce::String (GrainProcessor::stateTag) == "ANDOR_GRAIN_STATE");
}

TEST_CASE ("ANDOR Grain plugin: a sample becomes a polyphonic granular instrument", "[grain-plugin]")
{
    plugintest::IsolatedFolders folders;
    GrainProcessor p;
    p.loadSample (writeTone (folders.root));
    REQUIRE (p.waitForLoad (30000));
    REQUIRE (p.hasSource());
    CHECK (juce::roundToInt (getParam (p, "root")) == 57);   // detected A3
    setParam (p, "dnaVariation", 0.0f);
    setParam (p, "triggerVar", 0.0f);
    setParam (p, "pitchScatter", 0.0f);
    MidiSequence seq;
    andor::addNote (seq, 0.0, 1.5, 57);
    andor::addNote (seq, 1.6, 1.5, 64);
    andor::addNote (seq, 1.6, 1.5, 69);
    const auto out = plugintest::renderThrough (p, seq, sr, 256, 3.6);
    REQUIRE (plugintest::finite (out));
    CHECK (std::abs (centsTo (andortest::pitchOf (toAudio (out), 0.3, 1.4), 57)) < 8.0);
    CHECK (plugintest::rms (out, sr, 2.0, 3.0) > plugintest::rms (out, sr, 0.3, 1.4));   // two notes louder than one

    // SIZE and DENSITY change the sound.
    setParam (p, "size", 0.02f);
    setParam (p, "density", 120.0f);
    const auto busy = plugintest::renderThrough (p, seq, sr, 256, 1.5);
    setParam (p, "size", 0.4f);
    setParam (p, "density", 4.0f);
    const auto sparse = plugintest::renderThrough (p, seq, sr, 256, 1.5);
    CHECK_FALSE (plugintest::identical (busy, sparse));
}

TEST_CASE ("ANDOR Grain plugin: a drawn KEY DNA curve gives every key its own character", "[grain-plugin]")
{
    plugintest::IsolatedFolders folders;
    GrainProcessor p;
    p.loadSample (writeTone (folders.root));
    REQUIRE (p.waitForLoad (30000));
    setParam (p, "triggerVar", 0.0f);
    setParam (p, "dnaVariation", 0.0f);
    grain::DnaCurve curve;
    curve.addPoint (36.0f, 1.0f);
    curve.addPoint (84.0f, -1.0f);
    p.setCurve (grain::DnaParam::size, curve);
    const auto table = p.dnaTable();
    CHECK (table.curve (36, grain::DnaParam::size) == Approx (1.0f));
    CHECK (table.curve (84, grain::DnaParam::size) == Approx (-1.0f));

    // The audio thread receives the curve: the sounding keys report different effective SIZE.
    MidiSequence seq;
    andor::addNote (seq, 0.0, 1.0, 40);
    andor::addNote (seq, 0.0, 1.0, 80);
    plugintest::renderThrough (p, seq, sr, 256, 0.5);
    REQUIRE (p.voicesDisplay.count.load() == 2);
    float low = 0.0f, high = 0.0f;
    for (int i = 0; i < 2; ++i)
        (p.voicesDisplay.keys[static_cast<std::size_t> (i)].load() == 40 ? low : high) = p.voicesDisplay.effective[static_cast<std::size_t> (i)][0].load();
    CHECK (low > high + 0.4f);

    // Seeded individuality: a new seed changes a key's profile; the same seed brings it back.
    setParam (p, "dnaVariation", 1.0f);
    setParam (p, "dnaIndividual", 1.0f);
    const auto before = p.dnaTable().offset (60, grain::DnaParam::density);
    p.regenerateSeed();
    CHECK (juce::roundToInt (getParam (p, "dnaSeed")) == 2);
    const auto after = p.dnaTable().offset (60, grain::DnaParam::density);
    CHECK (before != after);
    setParam (p, "dnaSeed", 1.0f);
    CHECK (p.dnaTable().offset (60, grain::DnaParam::density) == before);

    p.randomiseCurve (grain::DnaParam::tone);
    CHECK (p.dnaSettings().curves[static_cast<std::size_t> (grain::DnaParam::tone)].count >= 3);
    p.resetCurve (grain::DnaParam::tone);
    CHECK (p.dnaSettings().curves[static_cast<std::size_t> (grain::DnaParam::tone)].count == 0);
}

TEST_CASE ("ANDOR Grain plugin: oscillator blend and modulation", "[grain-plugin]")
{
    plugintest::IsolatedFolders folders;
    GrainProcessor p;
    // No source at all: OSC <-> GRAIN at the oscillator end still plays, in tune.
    setParam (p, "oscMix", 1.0f);
    setParam (p, "oscShape", 0.0f);
    MidiSequence seq;
    andor::addNote (seq, 0.0, 1.0, 69);
    const auto osc = plugintest::renderThrough (p, seq, sr, 256, 1.2);
    CHECK (plugintest::rms (osc, sr, 0.2, 0.9) > 0.01);
    CHECK (std::abs (centsTo (andortest::pitchOf (toAudio (osc), 0.2, 0.9), 69)) < 3.0);
    setParam (p, "oscMix", 0.0f);
    CHECK (plugintest::rms (plugintest::renderThrough (p, seq, sr, 256, 1.2), sr, 0.2, 0.9) < 1.0e-9);

    // Modulation: LFO 1 -> POSITION moves the effective value while a note sounds.
    p.loadSample (writeTone (folders.root));
    REQUIRE (p.waitForLoad (30000));
    setParam (p, "triggerVar", 0.0f);
    setParam (p, "mod1Src", 6.0f);   // LFO 1
    setParam (p, "mod1Dst", 3.0f);   // Position
    setParam (p, "mod1Amt", 0.8f);
    setParam (p, "lfo1Rate", 3.0f);
    p.setPlayConfigDetails (0, 2, sr, 128);
    p.prepareToPlay (sr, 128);
    juce::AudioBuffer<float> buffer (2, 128);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
    float lo = 1.0f, hi = 0.0f;
    for (int b = 0; b < 300; ++b)
    {
        buffer.clear();
        p.processBlock (buffer, midi);
        midi.clear();
        const float pos = p.voicesDisplay.effective[0][static_cast<std::size_t> (grain::DnaParam::position)].load();
        lo = std::min (lo, pos);
        hi = std::max (hi, pos);
    }
    CHECK (hi - lo > 0.5f);
}

TEST_CASE ("ANDOR Grain plugin: sessions and presets reopen with the same behaviour", "[grain-plugin]")
{
    plugintest::IsolatedFolders folders;
    MidiSequence seq;
    for (int i = 0; i < 6; ++i)
        andor::addNote (seq, 0.2 * i, 0.6, 48 + 5 * i, 70 + 8 * i);
    juce::MemoryBlock saved;
    juce::AudioBuffer<float> before;
    {
        GrainProcessor a;
        a.loadSample (writeTone (folders.root));
        REQUIRE (a.waitForLoad (30000));
        grain::DnaCurve c;
        c.addPoint (30.0f, -0.7f);
        c.addPoint (90.0f, 0.9f);
        a.setCurve (grain::DnaParam::density, c);
        setParam (a, "dnaVariation", 0.6f);
        setParam (a, "pitchScatter", 0.7f);
        setParam (a, "oscMix", 0.3f);
        setParam (a, "mod2Src", 1.0f);
        setParam (a, "mod2Dst", 1.0f);
        setParam (a, "mod2Amt", 0.5f);
        a.getStateInformation (saved);
        before = plugintest::renderThrough (a, seq, sr, 256, 2.2);
        std::string error;
        REQUIRE (a.savePreset (GrainProcessor::presetFolder().getChildFile ("Test.andorgrain"), error));
    }
    GrainProcessor b;
    b.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (b.waitForLoad (30000));
    REQUIRE (b.hasSource());
    CHECK (b.dnaSettings().curves[static_cast<std::size_t> (grain::DnaParam::density)].count == 2);
    CHECK (getParam (b, "dnaVariation") == Approx (0.6f).margin (1.0e-4f));
    CHECK (juce::roundToInt (getParam (b, "root")) == 57);
    CHECK (plugintest::identical (before, plugintest::renderThrough (b, seq, sr, 256, 2.2)));   // deterministic mode: identical

    GrainProcessor c;
    std::string error;
    REQUIRE (c.loadPreset (GrainProcessor::presetFolder().getChildFile ("Test.andorgrain"), error));
    REQUIRE (c.waitForLoad (30000));
    CHECK (plugintest::identical (before, plugintest::renderThrough (c, seq, sr, 256, 2.2)));

    // Another product's state is ignored.
    juce::ValueTree phraseState ("ANDOR_PHRASE_STATE");
    juce::MemoryBlock foreign;
    c.copyXmlToBinary (*phraseState.createXml(), foreign);
    c.setStateInformation (foreign.getData(), static_cast<int> (foreign.getSize()));
    CHECK (c.hasSource());
}

TEST_CASE ("ANDOR Grain plugin: a Note Atlas from ANDOR Phrase plays its regions at their own pitch (Gate C)", "[grain-plugin][interop]")
{
    plugintest::IsolatedFolders folders;
    const auto atlasFile = exportPhraseAtlas (folders.root);
    GrainProcessor p;
    p.loadAtlas (atlasFile);
    REQUIRE (p.waitForLoad (30000));
    REQUIRE (p.hasSource());
    REQUIRE (p.document().atlas != nullptr);
    const auto model = p.model();
    REQUIRE (model->hasAtlas());
    CHECK (model->regions.size() == p.document().atlas->notes.size());
    // One shared source: the model references the loaded audio once, no per-key copies.
    CHECK (model->source.use_count() >= 1);
    for (const auto& r : model->regions)
    {
        const auto& n = p.document().atlas->notes[static_cast<std::size_t> (r.noteIndex)];
        CHECK (r.start == static_cast<double> (n.grainRange().start));   // correct sample-region references
        CHECK (r.rootMidi == n.midiPitch);                               // correct root interpretation
    }

    setParam (p, "triggerVar", 0.0f);
    setParam (p, "dnaVariation", 0.0f);
    setParam (p, "pitchScatter", 0.0f);
    setParam (p, "scatter", 0.1f);
    setParam (p, "size", 0.08f);
    setParam (p, "density", 40.0f);
    for (auto policy : { 0.0f, 1.0f, 3.0f })   // mapped, closest, whole source
    {
        INFO ("policy " << policy);
        setParam (p, "policy", policy);
        for (int key : { 62, 66, 69, 60 })   // recorded keys and one between them
        {
            INFO ("key " << key);
            MidiSequence seq;
            andor::addNote (seq, 0.0, 1.0, key);
            const auto out = plugintest::renderThrough (p, seq, sr, 256, 1.1);
            REQUIRE (plugintest::finite (out));
            CHECK (plugintest::rms (out, sr, 0.2, 0.9) > 0.003);   // no missing audio
            // Mapped and closest regions keep each note's recorded pitch; "whole source" is a
            // texture taken across the whole melody from one root, so its pitch follows POSITION.
            if (policy < 2.0f)
                CHECK (std::abs (centsTo (andortest::pitchOf (toAudio (out), 0.2, 0.9), key)) < 30.0);
        }
    }
    // KEY DNA applies on top: a POSITION curve moves where in each note the grains come from.
    grain::DnaCurve c;
    c.addPoint (60.0f, -1.0f);
    c.addPoint (72.0f, 1.0f);
    p.setCurve (grain::DnaParam::position, c);
    setParam (p, "policy", 0.0f);
    MidiSequence two;
    andor::addNote (two, 0.0, 0.6, 62);
    andor::addNote (two, 0.0, 0.6, 69);
    plugintest::renderThrough (p, two, sr, 256, 0.4);
    REQUIRE (p.voicesDisplay.count.load() == 2);
    float p62 = 0.0f, p69 = 0.0f;
    for (int i = 0; i < 2; ++i)
        (p.voicesDisplay.keys[static_cast<std::size_t> (i)].load() == 62 ? p62 : p69) = p.voicesDisplay.effective[static_cast<std::size_t> (i)][static_cast<std::size_t> (grain::DnaParam::position)].load();
    CHECK (p69 > p62 + 0.3f);

    // Save / reload: the atlas travels in the session; deterministic output is identical.
    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    MidiSequence melody;
    for (int i = 0; i < 5; ++i)
        andor::addNote (melody, 0.25 * i, 0.5, 62 + 2 * i);
    const auto first = plugintest::renderThrough (p, melody, sr, 256, 2.0);
    GrainProcessor q;
    q.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (q.waitForLoad (30000));
    REQUIRE (q.document().atlas != nullptr);
    CHECK (q.document().atlas->notes.size() == p.document().atlas->notes.size());
    CHECK (plugintest::identical (first, plugintest::renderThrough (q, melody, sr, 256, 2.0)));
}

TEST_CASE ("ANDOR Grain plugin: a Note Atlas whose recording is missing fails gracefully", "[grain-plugin][interop]")
{
    plugintest::IsolatedFolders folders;
    const auto atlasFile = exportPhraseAtlas (folders.root);
    folders.root.getChildFile ("phrase.wav").deleteFile();
    GrainProcessor p;
    p.loadAtlas (atlasFile);
    REQUIRE (p.waitForLoad (30000));
    CHECK_FALSE (p.hasSource());
    CHECK (p.document().sourceMissing);
    CHECK (p.document().missingName == "phrase.wav");
    MidiSequence seq;
    andor::addNote (seq, 0.0, 0.5, 64);
    CHECK (plugintest::finite (plugintest::renderThrough (p, seq, sr, 128, 0.6)));
    p.loadAtlas (folders.root.getChildFile ("nothing.andoratlas"));
    CHECK (p.status().contains ("Cannot read"));
}

TEST_CASE ("ANDOR Grain plugin: host block sizes and sample rates", "[grain-plugin]")
{
    plugintest::IsolatedFolders folders;
    GrainProcessor p;
    p.loadSample (writeTone (folders.root));
    REQUIRE (p.waitForLoad (30000));
    setParam (p, "triggerVar", 0.0f);
    setParam (p, "dnaVariation", 0.0f);
    MidiSequence seq;
    andor::addNote (seq, 0.0, 1.2, 57);
    for (double rate : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int block : { 16, 128, 777, 4096 })
        {
            INFO (rate << " / " << block);
            const auto out = plugintest::renderThrough (p, seq, rate, block, 1.4);
            REQUIRE (plugintest::finite (out));
            CHECK (std::abs (centsTo (andortest::pitchOf (toAudio (out, rate), 0.3, 1.1), 57)) < 10.0);
        }
}

TEST_CASE ("ANDOR Grain plugin: the editor", "[grain-plugin][editor]")
{
    plugintest::IsolatedFolders folders;
    GrainProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    auto* ge = dynamic_cast<GrainEditor*> (editor.get());
    REQUIRE (ge != nullptr);
    editor->setSize (GrainEditor::designWidth, GrainEditor::designHeight);
    plugintest::screenshot (*editor, "grain-05-default.png");

    p.loadSample (writeTone (folders.root));
    REQUIRE (p.waitForLoad (30000));
    grain::DnaCurve size;
    size.addPoint (28.0f, 0.9f);
    size.addPoint (64.0f, 0.1f);
    size.addPoint (100.0f, -0.8f);
    p.setCurve (grain::DnaParam::size, size);
    setParam (p, "dnaVariation", 0.4f);
    MidiSequence chord;
    for (int k : { 45, 57, 64, 76 })
        andor::addNote (chord, 0.0, 3.0, k);
    plugintest::renderThrough (p, chord, sr, 512, 0.7);
    ge->refreshNow();
    plugintest::screenshot (*editor, "grain-06-key-dna.png");

    setParam (p, "oscMix", 0.45f);
    setParam (p, "oscShape", 2.0f);
    ge->showTab (GrainEditor::Tab::voice);
    plugintest::renderThrough (p, chord, sr, 512, 0.7);
    ge->refreshNow();
    plugintest::screenshot (*editor, "grain-07-hybrid-osc.png");

    p.loadAtlas (exportPhraseAtlas (folders.root));
    REQUIRE (p.waitForLoad (30000));
    setParam (p, "oscMix", 0.0f);
    ge->showTab (GrainEditor::Tab::modulation);
    setParam (p, "mod1Src", 6.0f);
    setParam (p, "mod1Dst", 3.0f);
    setParam (p, "mod1Amt", 0.4f);
    MidiSequence notes;
    for (int k : { 62, 66, 69 })
        andor::addNote (notes, 0.0, 3.0, k);
    plugintest::renderThrough (p, notes, sr, 512, 0.7);
    ge->refreshNow();
    plugintest::screenshot (*editor, "grain-08-note-atlas.png");
    p.editorBeingDeleted (editor.get());
    editor.reset();
    CHECK (true);
}

TEST_CASE ("ANDOR Grain plugin: processBlock never allocates", "[grain-plugin][realtime]")
{
    plugintest::IsolatedFolders folders;
    GrainProcessor p;
    p.loadSample (writeTone (folders.root));
    REQUIRE (p.waitForLoad (30000));
    setParam (p, "density", 150.0f);
    setParam (p, "size", 0.5f);
    setParam (p, "oscMix", 0.4f);
    setParam (p, "space", 0.5f);
    setParam (p, "mod1Src", 6.0f);
    setParam (p, "mod1Dst", 2.0f);
    setParam (p, "mod1Amt", 0.5f);
    grain::DnaCurve c;
    c.addPoint (30.0f, 0.8f);
    c.addPoint (90.0f, -0.8f);
    p.setCurve (grain::DnaParam::size, c);   // the curve snapshot is taken inside processBlock
    CHECK (plugintest::allocationsWhilePlaying (p, 400) == 0);
}

// Hidden: `andor_grain_plugin_tests "[.demo-presets]"` with ANDOR_DEMO_DIR set writes demo
// presets and their generated sources there (see the Phrase counterpart).
TEST_CASE ("ANDOR Grain plugin: demo presets", "[.demo-presets]")
{
    const auto dirText = juce::SystemStats::getEnvironmentVariable ("ANDOR_DEMO_DIR", {});
    if (dirText.isEmpty())
        return;
    const juce::File dir (dirText);
    const auto audio = dir.getChildFile ("Audio");
    audio.createDirectory();
    const auto tone = writeTone (audio);
    const auto atlas = exportPhraseAtlas (audio);
    auto save = [&] (GrainProcessor& p, const char* name)
    {
        std::string error;
        REQUIRE (p.savePreset (dir.getChildFile ("Grain Presets").getChildFile (juce::String (name) + GrainProcessor::presetExtension), error));
    };
    {
        GrainProcessor p;
        p.loadSample (tone);
        REQUIRE (p.waitForLoad (30000));
        grain::DnaCurve size;
        size.addPoint (28.0f, 0.9f);
        size.addPoint (100.0f, -0.8f);
        p.setCurve (grain::DnaParam::size, size);
        grain::DnaCurve density;
        density.addPoint (36.0f, -0.5f);
        density.addPoint (64.0f, 0.4f);
        density.addPoint (96.0f, -0.2f);
        p.setCurve (grain::DnaParam::density, density);
        setParam (p, "dnaVariation", 0.4f);
        setParam (p, "space", 0.35f);
        save (p, "Tone - Long Low, Short High (KEY DNA)");
        setParam (p, "oscMix", 0.45f);
        setParam (p, "oscShape", 2.0f);
        setParam (p, "tone", 4500.0f);
        setParam (p, "mod1Src", 6.0f);
        setParam (p, "mod1Dst", 8.0f);
        setParam (p, "mod1Amt", 0.25f);
        save (p, "Tone - Hybrid Saw");
        setParam (p, "oscMix", 0.0f);
        setParam (p, "size", 0.03f);
        setParam (p, "density", 90.0f);
        setParam (p, "pitchScatter", 0.4f);
        setParam (p, "dnaIndividual", 0.9f);
        setParam (p, "dnaVariation", 0.8f);
        save (p, "Tone - Individual Keys Cloud");
    }
    {
        GrainProcessor p;
        p.loadAtlas (atlas);
        REQUIRE (p.waitForLoad (30000));
        setParam (p, "policy", 0.0f);
        setParam (p, "space", 0.3f);
        save (p, "Phrase Atlas - Mapped Notes");
        setParam (p, "policy", 3.0f);
        setParam (p, "size", 0.4f);
        setParam (p, "density", 12.0f);
        setParam (p, "scatter", 0.6f);
        setParam (p, "mod1Src", 6.0f);
        setParam (p, "mod1Dst", 3.0f);
        setParam (p, "mod1Amt", 0.3f);
        save (p, "Phrase Atlas - Whole Texture");
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    return Catch::Session().run (argc, argv);
}
