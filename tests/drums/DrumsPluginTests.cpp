// Headless tests of the ANDOR Drums plugin processor: the object a DAW hosts. Identity
// (never OSP's), Milestone 1 through processBlock (two pads, independent notes, the shared
// engine), keyboard modes, session recall, and the functional editor.

#include "DrumsEditor.h"
#include "DrumsProcessor.h"

#include "audio/utility/TestSignals.h"
#include "io/AudioFileIO.h"

#define CATCH_CONFIG_RUNNER
#include <catch2/catch_approx.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>

using namespace osp;
using osp::drumsplugin::DrumsProcessor;
using Catch::Approx;

namespace
{
    constexpr double rate = 48000.0;

    struct TempDir
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("andor-drums-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        TempDir() { dir.createDirectory(); }
        ~TempDir() { dir.deleteRecursively(); }
    };

    juce::File writeDrum (const juce::File& dir, const juce::String& name, double hz, double seconds)
    {
        auto audio = testsignals::sine (hz, seconds, rate, 0.5, 1);
        auto& x = audio.channels[0];
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] *= static_cast<float> (std::pow (10.0, -2.0 * static_cast<double> (i) / static_cast<double> (x.size())));
        testsignals::applyFades (audio, 0.002, 0.02);
        const auto file = dir.getChildFile (name);
        std::string error;
        REQUIRE (io::writeAudioFile (std::filesystem::path (file.getFullPathName().toStdString()), audio, io::SampleFormat::pcm24, error));
        return file;
    }

    /** Renders `seconds` through processBlock with the given note-ons at the start. */
    std::vector<float> play (DrumsProcessor& p, std::initializer_list<int> notes, double seconds, int block = 256)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, block);
        const auto total = static_cast<int> (seconds * rate);
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            if (pos == 0)
                for (int n : notes)
                    midi.addEvent (juce::MidiMessage::noteOn (10, n, static_cast<juce::uint8> (100)), 0);
            buffer.clear();
            p.processBlock (buffer, midi);
            const int n = std::min (block, total - pos);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + n);
        }
        return out;
    }

    double rmsOf (const std::vector<float>& x)
    {
        double s = 0.0;
        for (float v : x)
            s += static_cast<double> (v) * v;
        return x.empty() ? 0.0 : std::sqrt (s / static_cast<double> (x.size()));
    }

    double frequencyOf (const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        int first = -1, last = -1, crossings = 0;
        for (std::size_t i = from + 1; i < std::min (to, x.size()); ++i)
            if (x[i - 1] <= 0.0f && x[i] > 0.0f)
            {
                if (first < 0)
                    first = static_cast<int> (i);
                last = static_cast<int> (i);
                ++crossings;
            }
        return crossings > 1 ? rate * (crossings - 1) / static_cast<double> (last - first) : 0.0;
    }

    /** Points the sample store somewhere else for one scope (restored afterwards). */
    struct ScopedStore
    {
        juce::String previous { std::getenv ("OSP_SAMPLE_STORE") };
        explicit ScopedStore (const juce::File& dir) { set (dir.getFullPathName()); }
        ~ScopedStore() { set (previous); }
        static void set (const juce::String& value)
        {
#if JUCE_WINDOWS
            _putenv_s ("OSP_SAMPLE_STORE", value.toRawUTF8());
#else
            setenv ("OSP_SAMPLE_STORE", value.toRawUTF8(), 1);
#endif
        }
    };

    void loadKit (DrumsProcessor& p, const TempDir& tmp)
    {
        p.loadPad (0, writeDrum (tmp.dir, "Kick_01.wav", 110.0, 1.0));
        p.loadPad (1, writeDrum (tmp.dir, "Snare_01.wav", 880.0, 0.6));
        REQUIRE (p.waitForLoads());
        REQUIRE (p.padInfo (0).sound != nullptr);
        REQUIRE (p.padInfo (1).sound != nullptr);
    }
} // namespace

TEST_CASE ("ANDOR Drums has its own identity, never OSP's", "[drums-plugin]")
{
    DrumsProcessor p;
    CHECK (p.getName() == "ANDOR Drums");
#define ANDOR_STRINGIFY2(x) #x
#define ANDOR_STRINGIFY(x) ANDOR_STRINGIFY2 (x)
    CHECK (juce::String (ANDOR_STRINGIFY (JucePlugin_CFBundleIdentifier)) == "com.osp.andordrums");   // unquoted in the build
    CHECK (JucePlugin_PluginCode == 0x4164726d);     // 'Adrm' (OSP is 'Osp1' = 0x4f737031)
    CHECK (JucePlugin_PluginCode != 0x4f737031);
    CHECK (juce::String (JucePlugin_Name) != "OSP");
}

TEST_CASE ("Milestone 1 in the plugin: kick and snare, independent notes, played together", "[drums-plugin]")
{
    TempDir tmp;
    DrumsProcessor p;
    p.prepareToPlay (rate, 256);
    loadKit (p, tmp);

    const auto kick = play (p, { 36 }, 0.4);
    p.prepareToPlay (rate, 256);
    const auto snare = play (p, { 38 }, 0.4);
    p.prepareToPlay (rate, 256);
    const auto both = play (p, { 36, 38 }, 0.4);
    p.prepareToPlay (rate, 256);
    const auto nothing = play (p, { 60, 42 }, 0.2);   // unmapped / empty pad

    CHECK (frequencyOf (kick, 2400, 12000) == Approx (110.0).margin (1.0));
    CHECK (frequencyOf (snare, 2400, 12000) == Approx (880.0).margin (4.0));
    CHECK (rmsOf (nothing) == 0.0);
    double worst = 0.0;
    for (std::size_t i = 0; i < both.size(); ++i)
        worst = std::max (worst, static_cast<double> (std::abs (both[i] - (kick[i] + snare[i]))));
    CHECK (worst < 1.0e-5);
}

TEST_CASE ("Pads keep independent parameters; mute silences only its pad", "[drums-plugin]")
{
    TempDir tmp;
    DrumsProcessor p;
    p.prepareToPlay (rate, 256);
    loadKit (p, tmp);
    const auto snareBefore = play (p, { 38 }, 0.3);
    p.parameters().getParameter (DrumsProcessor::padParamId (0, "mute"))->setValueNotifyingHost (1.0f);
    p.parameters().getParameter (DrumsProcessor::padParamId (0, "tune"))->setValueNotifyingHost (0.75f);   // +12 st
    p.prepareToPlay (rate, 256);
    CHECK (rmsOf (play (p, { 36 }, 0.3)) < 1.0e-7);
    p.prepareToPlay (rate, 256);
    const auto snareAfter = play (p, { 38 }, 0.3);
    CHECK (snareAfter == snareBefore);
}

TEST_CASE ("EXPANDED through the plugin: the switch is a parameter; both maps survive", "[drums-plugin]")
{
    TempDir tmp;
    DrumsProcessor p;
    p.prepareToPlay (rate, 256);
    loadKit (p, tmp);
    auto map = p.keyMap();
    REQUIRE (drums::applyZonePreset (map, drums::ZonePreset::oneOctave, { 0, 1 }, 36).applied);
    p.setKeyMap (map);
    p.setKeyboardMode (drums::KeyboardMode::expanded);
    p.prepareToPlay (rate, 256);
    const auto up = play (p, { 43 }, 0.3);   // kick +7 semitones
    CHECK (frequencyOf (up, 2400, 12000) == Approx (110.0 * std::pow (2.0, 7.0 / 12.0)).epsilon (0.01));

    p.setKeyboardMode (drums::KeyboardMode::pad);
    p.prepareToPlay (rate, 256);
    CHECK (frequencyOf (play (p, { 36 }, 0.3), 2400, 12000) == Approx (110.0).margin (1.0));
    CHECK (p.keyMap() == map);   // the zones are still there
}

TEST_CASE ("Session recall: sounds, map, mode, parameters, names and view", "[drums-plugin]")
{
    TempDir tmp;
    juce::MemoryBlock state;
    std::vector<float> reference;
    drums::KeyMap map;
    {
        DrumsProcessor p;
        p.prepareToPlay (rate, 256);
        loadKit (p, tmp);
        map = p.keyMap();
        REQUIRE (drums::moveZone (map, 0, 0, drums::Collision::refuse).applied);
        REQUIRE (drums::setPadNote (map, 1, 40).applied);
        p.setKeyMap (map);
        p.setPadName (1, "Crack");
        p.setPadChoke (1, 3);
        p.nineLayout = true;
        p.selectedPad = 4;
        p.parameters().getParameter (DrumsProcessor::padParamId (1, "gain"))->setValueNotifyingHost (0.6f);
        p.parameters().getParameter ("kit.space")->setValueNotifyingHost (0.4f);
        p.prepareToPlay (rate, 256);
        reference = play (p, { 36, 40 }, 0.5);
        p.getStateInformation (state);
    }
    DrumsProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (q.waitForLoads());
    CHECK (q.padInfo (0).sound != nullptr);
    CHECK (q.keyMap() == map);
    CHECK (q.padDisplayName (1) == "Crack");
    CHECK (q.padInfo (1).chokeGroup == 3);
    CHECK (q.nineLayout);
    CHECK (q.selectedPad == 4);
    q.prepareToPlay (rate, 256);
    CHECK (play (q, { 36, 40 }, 0.5) == reference);   // bit-identical after reopening
}

TEST_CASE ("A missing sample is reported, never replaced", "[drums-plugin]")
{
    juce::MemoryBlock state;
    {
        TempDir tmp;
        const ScopedStore store (tmp.dir.getChildFile ("store-a"));
        DrumsProcessor p;
        p.loadPad (0, writeDrum (tmp.dir, "Gone.wav", 200.0, 0.3));
        REQUIRE (p.waitForLoads());
        p.getStateInformation (state);
    }   // the file and its store copy are gone now
    TempDir other;
    const ScopedStore store (other.dir.getChildFile ("store-b"));
    DrumsProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (q.waitForLoads());
    CHECK (q.padInfo (0).sound == nullptr);
    CHECK (q.padInfo (0).missingFilename == "Gone.wav");
    CHECK_FALSE (q.padInfo (0).error.empty());
    juce::MemoryBlock again;
    q.getStateInformation (again);   // still remembered for relinking
    const auto xml = juce::AudioProcessor::getXmlFromBinary (again.getData(), static_cast<int> (again.getSize()));
    REQUIRE (xml != nullptr);
    CHECK (xml->toString().contains ("Gone.wav"));
}

TEST_CASE ("An OSP session is ignored, not misread as a kit", "[drums-plugin]")
{
    DrumsProcessor p;
    p.setPadName (0, "Mine");
    juce::XmlElement osp ("OSP");
    osp.setAttribute ("stateVersion", 18);
    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary (osp, data);
    p.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    CHECK (p.padDisplayName (0) == "Mine");
}

TEST_CASE ("The functional editor opens, lays out both grids and snapshots", "[drums-plugin][ui]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    TempDir tmp;
    DrumsProcessor p;
    p.prepareToPlay (rate, 256);
    loadKit (p, tmp);
    p.loadPad (8, writeDrum (tmp.dir, "Crash.wav", 3000.0, 0.5));
    REQUIRE (p.waitForLoads());
    for (bool nine : { false, true })
    {
        p.nineLayout = nine;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        REQUIRE (editor != nullptr);
        CHECK (editor->getWidth() == 1120);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
        CHECK (image.isValid());
        if (const char* dir = std::getenv ("ANDOR_DRUMS_SNAPSHOT_DIR"); dir != nullptr && *dir != 0)
        {
            const auto file = juce::File (dir).getChildFile (nine ? "drums-3x3.png" : "drums-2x4.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }
}

int main (int argc, char* argv[])
{
    // An isolated sample store per run: tests never touch the user's samples.
    const auto store = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("andor-drums-test-store-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
#if JUCE_WINDOWS
    _putenv_s ("OSP_SAMPLE_STORE", store.getFullPathName().toRawUTF8());
#else
    setenv ("OSP_SAMPLE_STORE", store.getFullPathName().toRawUTF8(), 1);
#endif
    juce::MessageManager::getInstance();
    const int result = Catch::Session().run (argc, argv);
    store.deleteRecursively();
    juce::DeletedAtShutdown::deleteAll();
    juce::MessageManager::deleteInstance();
    return result;
}
