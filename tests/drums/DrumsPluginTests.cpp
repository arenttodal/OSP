// Headless tests of the ANDOR Drums plugin processor: the object a DAW hosts. Identity
// (never OSP's), Milestone 1 through processBlock (two pads, independent notes, the shared
// engine), keyboard modes, session recall, and the functional editor.

#include "CeramicSkin.h"
#include "DrumsEditor.h"
#include "DrumsProcessor.h"

#include "audio/utility/TestSignals.h"
#include "core/Prng.h"
#include "io/AudioFileIO.h"

#define CATCH_CONFIG_RUNNER
#include <catch2/catch_approx.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <set>

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

    /** One hit of `note` at `velocity`, rendered for `seconds`. */
    std::vector<float> hit (DrumsProcessor& p, int note, int velocity, double seconds, int block = 256)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, block);
        const auto total = static_cast<int> (seconds * rate);
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            if (pos == 0)
                midi.addEvent (juce::MidiMessage::noteOn (10, note, static_cast<juce::uint8> (velocity)), 0);
            buffer.clear();
            p.processBlock (buffer, midi);
            const int n = std::min (block, total - pos);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + n);
        }
        return out;
    }

    juce::File writeDrumAt (const juce::File& dir, const juce::String& name, double hz, double seconds, double amplitude)
    {
        auto audio = testsignals::sine (hz, seconds, rate, amplitude, 1);
        auto& x = audio.channels[0];
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] *= static_cast<float> (std::pow (10.0, -2.0 * static_cast<double> (i) / static_cast<double> (x.size())));
        testsignals::applyFades (audio, 0.002, 0.02);
        const auto file = dir.getChildFile (name);
        std::string error;
        REQUIRE (io::writeAudioFile (std::filesystem::path (file.getFullPathName().toStdString()), audio, io::SampleFormat::pcm24, error));
        return file;
    }

    /** A small synthetic break: kick, hat, snare, hat ... on 8ths at 120 BPM (known onsets). */
    juce::File writeBreak (const juce::File& dir, const juce::String& name, int hits = 8)
    {
        AudioData audio;
        audio.sampleRate = rate;
        std::vector<float> x (static_cast<std::size_t> ((0.1 + hits * 0.25 + 0.5) * rate), 0.0f);
        Prng rng (11);
        for (int h = 0; h < hits; ++h)
        {
            const auto at = static_cast<std::size_t> ((0.1 + h * 0.25) * rate);
            const int kind = h % 4;   // kick, hat, snare, hat
            const auto length = static_cast<std::size_t> ((kind == 0 ? 0.22 : kind == 2 ? 0.18 : 0.05) * rate);
            double prev = 0.0;
            for (std::size_t i = 0; i < length && at + i < x.size(); ++i)
            {
                const double t = static_cast<double> (i) / rate;
                const double fade = std::min (1.0, static_cast<double> (length - i) / (0.01 * rate));
                const double n = rng.uniform (-1.0, 1.0);
                double v = 0.0;
                if (kind == 0)
                    v = 0.8 * std::sin (2.0 * 3.14159265358979 * 60.0 * t) * std::exp (-t / 0.08);
                else if (kind == 2)
                    v = 0.4 * (n - 0.6 * prev) * std::exp (-t / 0.05) + 0.3 * std::sin (2.0 * 3.14159265358979 * 200.0 * t) * std::exp (-t / 0.04);
                else
                    v = 0.25 * (n - prev) * std::exp (-t / 0.015);
                prev = n;
                x[at + i] += static_cast<float> (v * fade);
            }
        }
        audio.channels.push_back (std::move (x));
        const auto file = dir.getChildFile (name);
        std::string error;
        REQUIRE (io::writeAudioFile (std::filesystem::path (file.getFullPathName().toStdString()), audio, io::SampleFormat::pcm24, error));
        return file;
    }

    /** A drum one-shot of a kind (synthetic, seeded): kick, 808, snare, hat, open hat, clap,
        tom, crash, rim, shaker. */
    juce::File writeOneShot (const juce::File& dir, const juce::String& name, const juce::String& kind, std::uint64_t seed)
    {
        AudioData audio;
        audio.sampleRate = rate;
        const double seconds = kind == "crash" ? 2.5 : kind == "open" || kind == "808" ? 1.2 : 0.5;
        std::vector<float> x (static_cast<std::size_t> (seconds * rate), 0.0f);
        Prng rng (seed);
        const double level = 0.6 * rng.uniform (0.85, 1.0);   // takes differ a little
        double phase = 0.0, prev = 0.0, prev2 = 0.0, lp1 = 0.0, lp2 = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const double t = static_cast<double> (i) / rate;
            const double n = rng.uniform (-1.0, 1.0);
            lp1 += 0.6 * (n - lp1);
            lp2 += 0.6 * (lp1 - lp2);
            double v = 0.0;
            if (kind == "kick" || kind == "808")
            {
                phase += 2.0 * 3.14159265358979 * (50.0 + 100.0 * std::exp (-t / 0.03)) / rate;
                v = std::sin (phase) * std::exp (-t / (kind == "808" ? 0.6 : 0.15));
            }
            else if (kind == "snare")
                v = 1.2 * (lp2 - 0.7 * prev) * std::exp (-t / 0.1) + 0.5 * std::sin (2.0 * 3.14159265358979 * 200.0 * t) * std::exp (-t / 0.05);
            else if (kind == "hat" || kind == "open" || kind == "crash")
                v = 0.5 * (n - 2.0 * prev + prev2) * std::exp (-t / (kind == "hat" ? 0.03 : kind == "open" ? 0.3 : 1.2));
            else if (kind == "tom")
            {
                phase += 2.0 * 3.14159265358979 * 150.0 / rate;
                v = std::sin (phase) * std::exp (-t / 0.25);
            }
            else if (kind == "rim")
                v = std::sin (2.0 * 3.14159265358979 * 900.0 * t) * std::exp (-t / 0.012);
            else if (kind == "shaker")
                v = 0.5 * (n - prev) * std::min (1.0, t / 0.025) * std::exp (-t / 0.08);
            else if (kind == "clap")
                for (int b = 0; b < 3; ++b)
                    if (t - b * 0.01 >= 0.0)
                        v += lp2 * std::exp (-(t - b * 0.01) / (b == 2 ? 0.06 : 0.004));
            prev2 = prev;
            prev = kind == "snare" ? lp2 : n;
            x[i] = static_cast<float> (level * v * std::min (1.0, (seconds - t) / 0.02));
        }
        audio.channels.push_back (std::move (x));
        const auto file = dir.getChildFile (name);
        std::string error;
        REQUIRE (io::writeAudioFile (std::filesystem::path (file.getFullPathName().toStdString()), audio, io::SampleFormat::pcm24, error));
        return file;
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
    CHECK (rmsOf (nothing) < 1.0e-12);
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
        p.padLayout = DrumsProcessor::PadLayout::grid9;
        p.kitName = "Test Kit";
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
    CHECK (q.padLayout == DrumsProcessor::PadLayout::grid9);
    CHECK (q.kitName == "Test Kit");
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

namespace
{
    using osp::drumsplugin::DrumsEditor;
    using osp::drumsplugin::KeyMapEditor;

    void snapshot (juce::Component& c, const juce::String& name, float scale = 1.0f)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
        CHECK (image.isValid());
        if (const char* dir = std::getenv ("ANDOR_DRUMS_SNAPSHOT_DIR"); dir != nullptr && *dir != 0)
        {
            const auto file = juce::File (dir).getChildFile (name + ".png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }

    std::unique_ptr<DrumsEditor> openEditor (DrumsProcessor& p)
    {
        auto* e = dynamic_cast<DrumsEditor*> (p.createEditor());
        REQUIRE (e != nullptr);
        return std::unique_ptr<DrumsEditor> (e);
    }

    /** Every visible pad cell has exactly the same size; returns that size. */
    juce::Rectangle<int> uniformCells (DrumsEditor& editor, int expected)
    {
        auto& grid = editor.content().padGrid();
        int visible = 0;
        juce::Rectangle<int> first;
        for (int s = 0; s < 9; ++s)
        {
            auto& cell = grid.cell (s);
            if (! cell.isVisible())
                continue;
            ++visible;
            if (first.isEmpty())
                first = cell.getBounds();
            CHECK (cell.getWidth() == first.getWidth());
            CHECK (cell.getHeight() == first.getHeight());
        }
        CHECK (visible == expected);
        return first;
    }

    void loadNine (DrumsProcessor& p, const TempDir& tmp)
    {
        const char* names[] = { "Kick_01.wav", "Snare_01.wav", "HatClosed.wav", "HatOpen.wav", "Clap.wav", "Tom.wav", "Rim.wav", "Shaker.wav", "Crash.wav" };
        for (int pad = 0; pad < 9; ++pad)
            p.loadPad (pad, writeDrum (tmp.dir, names[pad], 100.0 * (pad + 1), 0.5));
        REQUIRE (p.waitForLoads());
    }

    void sendNotes (DrumsProcessor& p, std::initializer_list<std::pair<int, bool>> events)
    {
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        for (auto [note, on] : events)
            midi.addEvent (on ? juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)) : juce::MidiMessage::noteOff (1, note), 0);
        p.processBlock (buffer, midi);
    }
} // namespace

TEST_CASE ("Pads are equal cells in 2x4 and 3x3 at every size; content never resizes them", "[drums-plugin][ui]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    TempDir tmp;
    DrumsProcessor p;
    p.prepareToPlay (rate, 256);
    loadKit (p, tmp);
    auto editor = openEditor (p);
    CHECK (editor->getWidth() == juce::roundToInt (1536 * DrumsEditor::defaultScale));
    CHECK (p.padLayout == DrumsProcessor::PadLayout::row8);   // the mockup's single row by default

    const auto cell8 = uniformCells (*editor, 8);
    p.setPadName (0, "An extremely long kick drum name that will never fit on any pad");
    p.selectedPad = 3;
    editor->content().selectPad (5);
    CHECK (uniformCells (*editor, 8) == cell8);   // name and selection change nothing

    for (double scale : { DrumsEditor::minScale, 1.0, DrumsEditor::maxScale })
    {
        editor->setSize (juce::roundToInt (1536 * scale), juce::roundToInt (1024 * scale));
        uniformCells (*editor, 8);
        CHECK (std::abs (editor->content().padGrid().getScreenBounds().getWidth() - juce::roundToInt (1424 * editor->getWidth() / 1536.0)) <= 2);
    }
    // 2 x 4 and 3 x 3: equal cells too; the window grows taller instead of squeezing them.
    const int heightRow = editor->getHeight();
    editor->content().setLayout (DrumsProcessor::PadLayout::grid8);
    uniformCells (*editor, 8);
    CHECK (editor->getHeight() > heightRow);
    editor->content().setLayout (DrumsProcessor::PadLayout::grid9);
    const auto cell9 = uniformCells (*editor, 9);
    CHECK (cell9.getWidth() > cell8.getWidth());
}

TEST_CASE ("Pad 9 survives layout switches; swapping places keeps identity, sound and MIDI", "[drums-plugin][ui]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    TempDir tmp;
    DrumsProcessor p;
    p.prepareToPlay (rate, 256);
    loadNine (p, tmp);
    auto editor = openEditor (p);
    const auto crash = p.padInfo (8).sound;
    for (auto layout : { DrumsProcessor::PadLayout::grid9, DrumsProcessor::PadLayout::row8, DrumsProcessor::PadLayout::grid9, DrumsProcessor::PadLayout::grid8 })
    {
        editor->content().setLayout (layout);
        CHECK (p.padInfo (8).sound == crash);
        CHECK (p.keyMap().padNote[8] == 49);
    }
    // Drag pad 1 onto pad 2 = swap places (the command a drop issues).
    const auto kickBefore = play (p, { 36 }, 0.3);
    p.prepareToPlay (rate, 256);
    p.swapPadSlots (0, 1);
    CHECK (p.padAtSlot (0) == 1);
    CHECK (p.padAtSlot (1) == 0);
    CHECK (p.keyMap().padNote[0] == 36);   // the kick still answers 36
    CHECK (play (p, { 36 }, 0.3) == kickBefore);
    p.undoManager().undo();
    CHECK (p.padAtSlot (0) == 0);
    p.undoManager().redo();
    CHECK (p.padAtSlot (0) == 1);

    // Recall keeps the order (state 2); a state-1 kit opens in identity order.
    juce::MemoryBlock state;
    p.getStateInformation (state);
    DrumsProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (q.waitForLoads());
    CHECK (q.padAtSlot (0) == 1);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    xml->setAttribute ("schemaVersion", 1);
    xml->removeChildElement (xml->getChildByName ("Order"), true);
    juce::MemoryBlock v1;
    juce::AudioProcessor::copyXmlToBinary (*xml, v1);
    DrumsProcessor r;
    r.setStateInformation (v1.getData(), static_cast<int> (v1.getSize()));
    REQUIRE (r.waitForLoads());
    CHECK (r.padAtSlot (0) == 0);
    CHECK (r.padInfo (8).sound != nullptr);
}

TEST_CASE ("KEY MAP: move, resize and root are single undoable gestures that keep their shape", "[drums-plugin][ui][keymap]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    TempDir tmp;
    DrumsProcessor p;
    p.prepareToPlay (rate, 256);
    loadKit (p, tmp);
    auto editor = openEditor (p);
    editor->content().setKeyMapOpen (true);
    auto& km = editor->content().keyMapEditor();
    p.setKeyboardMode (drums::KeyboardMode::expanded);
    km.refreshFromProcessor();

    // Spec test 2: kick on 36..48, root 36.
    auto map = p.keyMap();
    REQUIRE (drums::applyZoneLayout (map, 13, { 0 }, 36).applied);   // 36..48 (13 keys inclusive)
    map.zone[1] = drums::Zone { 72, 83, drums::noNote };
    auto a = p.arrangement();
    a.map = map;
    p.applyEdit (a, "setup");
    const auto start = p.keyMap();

    // Spec test 3: drag the whole kick zone up; the width stays; undo restores.
    km.beginGesture (0, KeyMapEditor::Gesture::move, 40);
    km.gestureTo (52);
    CHECK (p.keyMap() == start);                  // nothing is written during the drag
    km.endGesture (false);
    CHECK (p.keyMap().zone[0] == drums::Zone { 48, 60, drums::noNote });
    CHECK_FALSE (km.pendingResolution().has_value());
    p.prepareToPlay (rate, 256);
    CHECK (frequencyOf (play (p, { 55 }, 0.3), 2400, 12000) == Approx (110.0 * std::pow (2.0, 7.0 / 12.0)).epsilon (0.01));
    p.undoManager().undo();
    CHECK (p.keyMap() == start);

    // Escape / cancel: nothing changes.
    km.beginGesture (0, KeyMapEditor::Gesture::move, 40);
    km.gestureTo (90);
    CHECK (km.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK (p.keyMap() == start);

    // Spec test 4: extend the snare's right edge; a drag into the kick is refused (pending).
    km.beginGesture (1, KeyMapEditor::Gesture::resizeHigh, 83);
    km.gestureTo (95);
    km.endGesture (false);
    CHECK (p.keyMap().zone[1] == drums::Zone { 72, 95, drums::noNote });
    km.beginGesture (1, KeyMapEditor::Gesture::resizeLow, 72);
    km.gestureTo (40);
    km.endGesture (false);
    REQUIRE (km.pendingResolution().has_value());
    CHECK (km.pendingResolution()->colliders == std::vector<int> { 0 });
    CHECK (p.keyMap().zone[1].low == 72);         // not applied
    km.resolveCancel();
    CHECK (p.keyMap().zone[1].low == 72);

    // ... or the kick moves out of the way, in one undo step.
    km.beginGesture (1, KeyMapEditor::Gesture::resizeLow, 72);
    km.gestureTo (40);
    km.endGesture (false);
    REQUIRE (km.resolveMoveOthers());
    CHECK (p.keyMap().zone[1].low == 40);
    CHECK_FALSE (p.keyMap().zone[0].overlaps (p.keyMap().zone[1]));
    CHECK (p.keyMap().zone[0].width() == 13);
    p.undoManager().undo();
    CHECK (p.keyMap().zone[1] == drums::Zone { 72, 95, drums::noNote });

    // Root: dragged inside the zone; the inspector's transposition follows.
    km.beginGesture (0, KeyMapEditor::Gesture::root, 36);
    km.gestureTo (42);
    km.endGesture (false);
    CHECK (p.keyMap().zone[0].rootNote() == 42);
    km.beginGesture (0, KeyMapEditor::Gesture::root, 42);
    km.gestureTo (100);   // clamped to the zone
    km.endGesture (false);
    CHECK (p.keyMap().zone[0].rootNote() == 48);

    // Swap: moving the kick exactly onto the snare exchanges them.
    p.undoManager().undo();
    p.undoManager().undo();
    km.beginGesture (0, KeyMapEditor::Gesture::move, 36);
    km.gestureTo (72);   // exactly onto the snare's start
    km.endGesture (false);
    REQUIRE (km.canSwap());
    REQUIRE (km.resolveSwap());
    CHECK (p.keyMap().zone[0] == drums::Zone { 72, 84, drums::noNote });
    CHECK (p.keyMap().zone[1] == drums::Zone { 36, 59, drums::noNote });

    // The PAD map was never touched by any of this.
    CHECK (p.keyMap().padNote == drums::defaultPadNotes());
}

TEST_CASE ("KEY MAP in PAD mode: click to assign, collisions swap or move, never silently layer", "[drums-plugin][ui][keymap]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    DrumsProcessor p;
    auto editor = openEditor (p);
    editor->content().setKeyMapOpen (true);
    auto& km = editor->content().keyMapEditor();
    REQUIRE (p.keyboardMode() == drums::KeyboardMode::pad);

    km.beginGesture (0, KeyMapEditor::Gesture::padNote, 36);
    km.gestureTo (35);
    km.endGesture (false);
    CHECK (p.keyMap().padNote[0] == 35);
    km.beginGesture (0, KeyMapEditor::Gesture::padNote, 35);
    km.gestureTo (38);   // the snare's note
    km.endGesture (false);
    REQUIRE (km.pendingResolution().has_value());
    REQUIRE (km.resolveSwap());
    CHECK (p.keyMap().padNote[0] == 38);
    CHECK (p.keyMap().padNote[1] == 35);
    km.beginGesture (2, KeyMapEditor::Gesture::padNote, 42);
    km.gestureTo (38);
    km.endGesture (false);
    REQUIRE (km.resolveMoveOthers());
    CHECK (p.keyMap().padNote[2] == 38);
    CHECK (p.keyMap().padNote[0] != 38);
    std::array<drums::Hit, drums::maxPads> hits {};
    for (int n = 0; n < 128; ++n)
        CHECK (drums::resolve (p.keyMap(), drums::KeyboardMode::pad, n, hits) <= 1);
    CHECK (p.keyMap().zone == drums::defaultZones());   // the EXPANDED map is untouched
}

TEST_CASE ("MIDI Learn: a note, a range, through processBlock; cancel leaves nothing behind", "[drums-plugin][ui][keymap]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    TempDir tmp;
    DrumsProcessor p;
    p.prepareToPlay (rate, 256);
    loadKit (p, tmp);
    auto editor = openEditor (p);
    editor->content().setKeyMapOpen (true);
    auto& km = editor->content().keyMapEditor();

    // Notes reach Learn only while it is armed.
    sendNotes (p, { { 70, true }, { 70, false } });
    CHECK (p.takeLearnedNotes().empty());

    editor->content().selectPad (2);   // the closed hat
    km.startLearn();
    CHECK (p.isLearnArmed());
    sendNotes (p, { { 50, true }, { 50, false } });
    for (int n : p.takeLearnedNotes())
        km.learnedNote (n);
    CHECK (p.keyMap().padNote[2] == 50);
    CHECK_FALSE (p.isLearnArmed());

    // Spec test 5: learn a range for the hat in EXPANDED, cancel, retry.
    p.setKeyboardMode (drums::KeyboardMode::expanded);
    km.refreshFromProcessor();
    km.startLearn();
    sendNotes (p, { { 120, true } });
    for (int n : p.takeLearnedNotes())
        km.learnedNote (n);
    CHECK (km.learnState() == KeyMapEditor::Learn::rangeHigh);
    km.cancelLearn();
    CHECK_FALSE (p.isLearnArmed());
    CHECK (p.keyMap().zone[2] == drums::defaultZones()[2]);
    km.startLearn();
    sendNotes (p, { { 120, false }, { 127, true } });
    sendNotes (p, { { 120, true } });   // highest first is fine: the range is sorted
    for (int n : p.takeLearnedNotes())
        km.learnedNote (n);
    CHECK (p.keyMap().zone[2] == drums::Zone { 120, 127, drums::noNote });   // the free keys above pad 9
    CHECK (p.isNoteHeld (127));
    sendNotes (p, { { 127, false }, { 120, false } });
    CHECK_FALSE (p.isNoteHeld (127));

    // A learnt range in the way of other pads waits for a choice (here: cancel).
    km.startLearn();
    sendNotes (p, { { 30, true }, { 50, true }, { 30, false }, { 50, false } });
    for (int n : p.takeLearnedNotes())
        km.learnedNote (n);
    REQUIRE (km.pendingResolution().has_value());
    CHECK (p.keyMap().zone[2] == drums::Zone { 120, 127, drums::noNote });
    km.resolveCancel();
}

TEST_CASE ("Auto Map previews first, changes only the active map, and undoes in one step", "[drums-plugin][ui][keymap]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    DrumsProcessor p;
    auto editor = openEditor (p);
    editor->content().setKeyMapOpen (true);
    auto& km = editor->content().keyMapEditor();
    p.setKeyboardMode (drums::KeyboardMode::expanded);
    km.refreshFromProcessor();
    const auto before = p.keyMap();

    REQUIRE (km.previewAutoMap (KeyMapEditor::AutoMap::twoOctaves));
    CHECK (km.hasPreview());
    CHECK (p.keyMap() == before);   // a preview writes nothing
    CHECK (km.statusText().contains ("16 keys each"));
    CHECK (km.shownMap().zone[0].width() == 16);
    km.cancelPreview();
    CHECK (p.keyMap() == before);

    REQUIRE (km.previewAutoMap (KeyMapEditor::AutoMap::oneOctave));
    km.applyPreview();
    CHECK (p.keyMap().zone[0].width() == 12);
    CHECK (p.keyMap().padNote == before.padNote);   // the PAD map is untouched
    for (int a = 0; a < 8; ++a)
        for (int b = a + 1; b < 8; ++b)
            CHECK_FALSE (p.keyMap().zone[static_cast<std::size_t> (a)].overlaps (p.keyMap().zone[static_cast<std::size_t> (b)]));
    p.undoManager().undo();
    CHECK (p.keyMap() == before);

    p.setKeyboardMode (drums::KeyboardMode::pad);
    km.refreshFromProcessor();
    REQUIRE (km.previewAutoMap (KeyMapEditor::AutoMap::chromatic));
    km.applyPreview();
    for (int s = 0; s < 8; ++s)
        CHECK (p.keyMap().padNote[static_cast<std::size_t> (p.padAtSlot (s))] == 36 + s);
    CHECK (p.keyMap().zone == before.zone);
}

TEST_CASE ("Mode switch with held notes and the pedal: note-offs end what they started", "[drums-plugin]")
{
    TempDir tmp;
    DrumsProcessor p;
    p.prepareToPlay (rate, 256);
    loadKit (p, tmp);
    p.setPadTrigger (0, drums::TriggerMode::gate);
    p.parameters().getParameter (DrumsProcessor::padParamId (0, "release"))->setValueNotifyingHost (0.0f);
    juce::AudioBuffer<float> buffer (2, 256);
    auto block = [&] (juce::MidiBuffer midi) { buffer.clear(); p.processBlock (buffer, midi); };
    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
    on.addEvent (juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (100)), 1);
    block (on);
    p.setKeyboardMode (drums::KeyboardMode::expanded);
    juce::MidiBuffer off;
    off.addEvent (juce::MidiMessage::noteOff (1, 36), 0);
    block (off);
    for (int i = 0; i < 20; ++i)
        block ({});
    CHECK (p.padVoices (0) == 1);   // the pedal holds it
    juce::MidiBuffer pedalUp;
    pedalUp.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);
    block (pedalUp);
    for (int i = 0; i < 40; ++i)
        block ({});
    CHECK (p.padVoices (0) == 0);
}

TEST_CASE ("The ceramic skin draws its materials in the style guide's colours", "[drums-plugin][ui]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    juce::Image img (juce::Image::ARGB, 40, 400, true);
    {
        juce::Graphics g (img);
        g.fillAll (osp::drumsplugin::ceramic::colour::backdrop);
        osp::drumsplugin::ceramic::teakCheek (g, { 9.0f, 10.0f, 22.0f, 380.0f });
    }
    // The middle of the cheek is teak (warm, saturated), not the backdrop or a shadow.
    const auto c = img.getPixelAt (20, 200);
    INFO ("teak pixel " << c.toDisplayString (true));
    {
        // Opaque wood, not a tinted shadow (images draw at the current colour's alpha).
        juce::Image clear (juce::Image::ARGB, 40, 400, true);
        juce::Graphics g2 (clear);
        osp::drumsplugin::ceramic::teakCheek (g2, { 9.0f, 10.0f, 22.0f, 380.0f });
        CHECK (clear.getPixelAt (20, 200).getAlpha() == 255);
    }
    CHECK (c.getRed() > c.getBlue() + 45);
    CHECK (c.getSaturation() > 0.3f);
}

TEST_CASE ("Layers: several files join a pad as round robins, in one undo step", "[drums-plugin][layers]")
{
    TempDir tmp;
    const ScopedStore store (tmp.dir.getChildFile ("store"));
    DrumsProcessor p;
    p.prepareToPlay (rate, 256);
    p.loadPad (0, writeDrum (tmp.dir, "Kick_A.wav", 110.0, 0.5));
    REQUIRE (p.waitForLoads());
    REQUIRE (p.padLayers (0).layers.size() == 1);
    p.addPadLayers (0, { writeDrum (tmp.dir, "Kick_B.wav", 220.0, 0.5), writeDrum (tmp.dir, "Kick_C.wav", 330.0, 0.5) },
                    drumsplugin::LayerDrop::roundRobin);
    REQUIRE (p.waitForLoads());
    const auto& layers = p.padLayers (0);
    REQUIRE (layers.layers.size() == 3);
    CHECK (layers.mode == drums::LayerMode::roundRobin);
    CHECK (layers.layers[1].displayName() == "Kick_B.wav");
    CHECK (p.padInfo (0).sound == layers.layers[0].sound);

    // Sequential order: every hit is the next layer, each at its own pitch.
    p.setLayerOrder (0, drums::RoundRobinOrder::sequential);
    hit (p, 36, 100, 0.02);   // the new bundle reaches the audio thread
    std::vector<int> order;
    std::vector<double> hz;
    for (int i = 0; i < 3; ++i)
    {
        p.prepareToPlay (rate, 256);   // silence between hits (and the picker starts again)
        for (int k = 0; k <= i; ++k)
        {
            const auto out = hit (p, 36, 100, 0.25);
            if (k == i)
                hz.push_back (frequencyOf (out, 2400, 11000));
        }
        order.push_back (p.lastLayerPlayed (0));
    }
    CHECK (order == std::vector<int> { 0, 1, 2 });
    CHECK (hz[0] == Approx (110.0).margin (2.0));

    // One undo removes both added layers; redo brings them back.
    p.undoManager().undo();   // the order change
    p.undoManager().undo();   // the added layers
    CHECK (p.padLayers (0).layers.size() == 1);
    p.undoManager().redo();
    CHECK (p.padLayers (0).layers.size() == 3);
}

TEST_CASE ("Layers: velocity layers sort softest first and keep their recorded levels", "[drums-plugin][layers]")
{
    TempDir tmp;
    const ScopedStore store (tmp.dir.getChildFile ("store"));
    DrumsProcessor p;
    p.prepareToPlay (rate, 256);
    // Dropped loud-first: the import sorts them.
    p.addPadLayers (0, { writeDrumAt (tmp.dir, "Loud.wav", 200.0, 0.5, 0.8), writeDrumAt (tmp.dir, "Soft.wav", 200.0, 0.5, 0.1),
                         writeDrumAt (tmp.dir, "Mid.wav", 200.0, 0.5, 0.3) },
                    drumsplugin::LayerDrop::velocity);
    REQUIRE (p.waitForLoads());
    const auto& l = p.padLayers (0);
    REQUIRE (l.layers.size() == 3);
    CHECK (l.mode == drums::LayerMode::velocity);
    CHECK (l.layers[0].displayName() == "Soft.wav");
    CHECK (l.layers[1].displayName() == "Mid.wav");
    CHECK (l.layers[2].displayName() == "Loud.wav");
    CHECK (l.layers[0].params.velocityLow == 1);
    CHECK (l.layers[2].params.velocityHigh == 127);
    CHECK (l.layers[1].params.velocityLow == l.layers[0].params.velocityHigh + 1);
    // Levels as recorded, against the loudest: about -18 dB and -8.5 dB.
    CHECK (l.layers[2].params.gainDb == Approx (0.0).margin (0.01));
    CHECK (l.layers[0].params.gainDb == Approx (20.0 * std::log10 (0.1 / 0.8)).margin (0.6));
    CHECK (l.layers[1].params.gainDb == Approx (20.0 * std::log10 (0.3 / 0.8)).margin (0.6));

    hit (p, 36, 20, 0.02);
    CHECK (p.lastLayerPlayed (0) == 0);
    hit (p, 36, 64, 0.02);
    CHECK (p.lastLayerPlayed (0) == 1);
    hit (p, 36, 127, 0.02);
    CHECK (p.lastLayerPlayed (0) == 2);
}

TEST_CASE ("Layers: edit, rename, reorder, duplicate, remove - each one undo step", "[drums-plugin][layers]")
{
    TempDir tmp;
    const ScopedStore store (tmp.dir.getChildFile ("store"));
    DrumsProcessor p;
    p.addPadLayers (2, { writeDrum (tmp.dir, "H1.wav", 3000.0, 0.2), writeDrum (tmp.dir, "H2.wav", 3200.0, 0.2) },
                    drumsplugin::LayerDrop::replace);
    REQUIRE (p.waitForLoads());
    REQUIRE (p.padLayers (2).layers.size() == 2);
    CHECK (p.padLayers (2).mode == drums::LayerMode::roundRobin);
    const auto before = p.padLayers (2);

    auto params = before.layers[0].params;
    params.gainDb = -6.0;
    params.start = 0.1;
    params.end = 0.05;   // clamped to START
    p.setLayerParams (2, 0, params);
    CHECK (p.padLayers (2).layers[0].params.end == Approx (0.1));
    p.renameLayer (2, 1, "Tight");
    CHECK (p.padLayers (2).layers[1].displayName() == "Tight");
    p.moveLayer (2, 1, 0);
    CHECK (p.padLayers (2).layers[0].displayName() == "Tight");
    p.duplicateLayer (2, 0);
    CHECK (p.padLayers (2).layers.size() == 3);
    CHECK (p.padLayers (2).layers[1].sound == p.padLayers (2).layers[0].sound);
    p.removeLayer (2, 2);
    CHECK (p.padLayers (2).layers.size() == 2);
    p.setLayerMode (2, drums::LayerMode::velocity);
    CHECK (p.padLayers (2).layers[0].params.velocityHigh < 127);   // velocity layers get ranges
    p.setLayerMode (2, drums::LayerMode::stack);
    CHECK (p.padLayers (2).layers[0].params.velocityHigh == 127);  // and give them back

    for (int i = 0; i < 7; ++i)
        p.undoManager().undo();
    CHECK (p.padLayers (2) == before);

    // Clearing a pad is undoable too.
    p.clearPad (2);
    CHECK (p.padInfo (2).sound == nullptr);
    p.undoManager().undo();
    CHECK (p.padLayers (2) == before);
}

TEST_CASE ("Layers: a session recalls every layer, its settings and the mode, bit-identically", "[drums-plugin][layers]")
{
    TempDir tmp;
    const ScopedStore store (tmp.dir.getChildFile ("store"));
    juce::MemoryBlock state;
    std::vector<float> reference;
    drumsplugin::PadLayers saved;
    {
        DrumsProcessor p;
        p.prepareToPlay (rate, 256);
        p.addPadLayers (0, { writeDrum (tmp.dir, "S1.wav", 400.0, 0.4), writeDrum (tmp.dir, "S2.wav", 500.0, 0.4) },
                        drumsplugin::LayerDrop::stack);
        REQUIRE (p.waitForLoads());
        auto params = p.padLayers (0).layers[1].params;
        params.gainDb = -3.5;
        params.end = 0.6;
        p.setLayerParams (0, 1, params);
        p.renameLayer (0, 0, "Body");
        saved = p.padLayers (0);
        p.prepareToPlay (rate, 256);
        reference = hit (p, 36, 100, 0.4);
        p.getStateInformation (state);
    }
    DrumsProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (q.waitForLoads());
    const auto& got = q.padLayers (0);
    REQUIRE (got.layers.size() == 2);
    CHECK (got.mode == drums::LayerMode::stack);
    CHECK (got.layers[0].displayName() == "Body");
    CHECK (got.layers[1].params == saved.layers[1].params);
    CHECK (q.undoManager().canUndo() == false);
    q.prepareToPlay (rate, 256);
    CHECK (hit (q, 36, 100, 0.4) == reference);

    // A state-2 session (one <Sound> per pad, no <Layers>) opens as one layer.
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (xml != nullptr);
    xml->setAttribute ("schemaVersion", 2);
    for (auto* pad : xml->getChildByName ("Pads")->getChildWithTagNameIterator ("Pad"))
        if (auto* layers = pad->getChildByName ("Layers"))
            pad->removeChildElement (layers, true);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    DrumsProcessor r;
    r.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    REQUIRE (r.waitForLoads());
    REQUIRE (r.padLayers (0).layers.size() == 1);
    CHECK (r.padLayers (0).layers[0].fileName() == "S1.wav");
}

TEST_CASE ("Layers: a layer auditions alone, without moving the round robin", "[drums-plugin][layers]")
{
    TempDir tmp;
    const ScopedStore store (tmp.dir.getChildFile ("store"));
    DrumsProcessor p;
    p.prepareToPlay (rate, 256);
    p.addPadLayers (0, { writeDrum (tmp.dir, "A.wav", 110.0, 0.5), writeDrum (tmp.dir, "B.wav", 330.0, 0.5) },
                    drumsplugin::LayerDrop::roundRobin);
    REQUIRE (p.waitForLoads());
    p.auditionLayer (0, 1, 100);
    const auto out = hit (p, 0, 0, 0.25);   // velocity 0: no note, only the audition
    CHECK (frequencyOf (out, 2400, 11000) == Approx (330.0).margin (3.0));
    CHECK (p.lastLayerPlayed (0) == -1);   // a hit has not happened yet
}

TEST_CASE ("Layer editor: drops on pads, the list selects, the velocity bar edits in one undo step", "[drums-plugin][ui][layers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    TempDir tmp;
    const ScopedStore store (tmp.dir.getChildFile ("store"));
    DrumsProcessor p;
    auto editor = openEditor (p);
    editor->setSize (1536, 1024);
    auto& content = editor->content();
    // Three files onto an empty pad (no window to ask in: the default, round robin).
    juce::StringArray files { writeDrum (tmp.dir, "Snare_1.wav", 300.0, 0.3).getFullPathName(), writeDrum (tmp.dir, "Snare_2.wav", 320.0, 0.3).getFullPathName(),
                              writeDrum (tmp.dir, "Snare_3.wav", 340.0, 0.3).getFullPathName() };
    auto& cell = content.padGrid().cell (p.slotOfPad (1));
    cell.filesDropped (files, 10, 10);
    REQUIRE (p.waitForLoads());
    CHECK (p.selectedPad == 1);
    REQUIRE (p.padLayers (1).layers.size() == 3);
    CHECK (p.padLayers (1).mode == drums::LayerMode::roundRobin);

    // Click the third row: it is selected and its controls follow.
    auto& panel = content.samplePanel();
    auto& list = panel.layerList();
    const auto row = list.rowRect (2).getCentre();
    juce::MouseEvent down (juce::Desktop::getInstance().getMainMouseSource(), row.translated (20.0f, 0.0f), juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                           &list, &list, juce::Time::getCurrentTime(), row.translated (20.0f, 0.0f), juce::Time::getCurrentTime(), 1, false);
    list.mouseDown (down);
    list.mouseUp (down);
    CHECK (p.selectedLayer[1] == 2);

    // Velocity bar: the drag is one undo step.
    auto& bar = panel.velocityRange();
    REQUIRE (bar.isVisible());
    REQUIRE (bar.onChange != nullptr);
    bar.onChange (40, 127, true);
    bar.onChange (60, 127, false);
    bar.onChange (80, 127, false);
    CHECK (p.padLayers (1).layers[2].params.velocityLow == 80);
    p.undoManager().undo();
    CHECK (p.padLayers (1).layers[2].params.velocityLow == 1);

    // One more file onto the populated pad (headless: replaces).
    cell.filesDropped (juce::StringArray { writeDrum (tmp.dir, "Snare_4.wav", 360.0, 0.3).getFullPathName() }, 10, 10);
    REQUIRE (p.waitForLoads());
    CHECK (p.padLayers (1).layers.size() == 1);
    p.undoManager().undo();
    CHECK (p.padLayers (1).layers.size() == 3);
}

TEST_CASE ("Duplicate pad: layers, settings and name, keeping each pad's note; one undo step", "[drums-plugin][kits]")
{
    TempDir tmp;
    const ScopedStore store (tmp.dir.getChildFile ("store"));
    DrumsProcessor p;
    p.prepareToPlay (rate, 256);
    p.addPadLayers (0, { writeDrum (tmp.dir, "K1.wav", 110.0, 0.4), writeDrum (tmp.dir, "K2.wav", 130.0, 0.4) }, drumsplugin::LayerDrop::roundRobin);
    REQUIRE (p.waitForLoads());
    p.setPadName (0, "Boom");
    p.parameters().getParameter (DrumsProcessor::padParamId (0, "gain"))->setValueNotifyingHost (0.4f);
    const int target = p.firstEmptyPad();
    CHECK (target == 1);
    const auto noteBefore = p.keyMap().padNote[1];
    REQUIRE (p.duplicatePad (0, target));
    CHECK (p.padLayers (1).layers.size() == 2);
    CHECK (p.padLayers (1).layers[0].sound == p.padLayers (0).layers[0].sound);   // shared, not reloaded
    CHECK (p.padDisplayName (1) == "Boom copy");
    CHECK (p.padInfo (1).category == p.padInfo (0).category);
    CHECK (p.parameters().getParameter (DrumsProcessor::padParamId (1, "gain"))->getValue() == Approx (0.4f));
    CHECK (p.keyMap().padNote[1] == noteBefore);
    CHECK (rmsOf (hit (p, noteBefore, 100, 0.2)) > 1.0e-5);   // the copy plays on its own note (at -31 dB)
    p.undoManager().undo();
    CHECK (p.padLayers (1).layers.empty());
    CHECK (p.padDisplayName (1) == "Snare");
    CHECK_FALSE (p.duplicatePad (2, 3));   // nothing to copy
    CHECK_FALSE (p.duplicatePad (0, 0));
}

TEST_CASE ("Kits: save, new and open (undoable); not an OSP file; not a newer schema", "[drums-plugin][kits]")
{
    TempDir tmp;
    const ScopedStore store (tmp.dir.getChildFile ("store"));
    DrumsProcessor p;
    p.prepareToPlay (rate, 256);
    p.addPadLayers (0, { writeDrum (tmp.dir, "A.wav", 110.0, 0.4), writeDrum (tmp.dir, "B.wav", 150.0, 0.4) }, drumsplugin::LayerDrop::stack);
    p.loadPad (1, writeDrum (tmp.dir, "S.wav", 400.0, 0.3));
    REQUIRE (p.waitForLoads());
    p.kitName = "Studio Kit";
    p.setKeyboardMode (drums::KeyboardMode::expanded);
    p.parameters().getParameter ("kit.space")->setValueNotifyingHost (0.3f);
    p.setPadChoke (1, 4);
    p.prepareToPlay (rate, 256);
    const auto reference = play (p, { p.keyMap().zone[0].rootNote(), p.keyMap().zone[1].rootNote() }, 0.4);

    const auto file = tmp.dir.getChildFile ("Studio Kit.andorkit");
    juce::String error;
    REQUIRE (p.saveKit (file, error));
    const auto xml = juce::XmlDocument::parse (file);
    REQUIRE (xml != nullptr);
    CHECK (xml->hasTagName ("AndorDrumsKit"));
    CHECK (xml->getIntAttribute ("kitSchemaVersion") == 1);

    p.newKit();
    CHECK (p.padLayers (0).layers.empty());
    CHECK (p.kitName == "New Kit");
    CHECK (p.keyboardMode() == drums::KeyboardMode::pad);
    p.undoManager().undo();   // the kit is back
    REQUIRE (p.waitForLoads());
    CHECK (p.padLayers (0).layers.size() == 2);
    CHECK (p.kitName == "Studio Kit");

    DrumsProcessor q;
    REQUIRE (q.loadKit (file, error));
    REQUIRE (q.waitForLoads());
    CHECK (q.kitName == "Studio Kit");
    CHECK (q.padLayers (0).mode == drums::LayerMode::stack);
    CHECK (q.padInfo (1).chokeGroup == 4);
    CHECK (q.keyboardMode() == drums::KeyboardMode::expanded);
    q.prepareToPlay (rate, 256);
    CHECK (play (q, { q.keyMap().zone[0].rootNote(), q.keyMap().zone[1].rootNote() }, 0.4) == reference);
    q.undoManager().undo();   // opening a kit is undoable
    CHECK (q.padLayers (0).layers.empty());

    // An OSP preset or session is never read as a kit.
    const auto ospFile = tmp.dir.getChildFile ("Preset.andorkit");
    juce::XmlElement osp ("OSP");
    osp.setAttribute ("stateVersion", 18);
    REQUIRE (osp.writeTo (ospFile));
    CHECK_FALSE (q.loadKit (ospFile, error));
    CHECK (error.contains ("not an ANDOR Drums kit"));
    // A kit from a newer version is refused, not misread.
    auto newer = juce::XmlElement (*xml);
    newer.setAttribute ("kitSchemaVersion", 99);
    const auto newerFile = tmp.dir.getChildFile ("Newer.andorkit");
    REQUIRE (newer.writeTo (newerFile));
    CHECK_FALSE (q.loadKit (newerFile, error));
}

TEST_CASE ("Portable kits: samples travel inside the kit, no absolute paths, duplicate names kept apart", "[drums-plugin][kits]")
{
    TempDir tmp;
    juce::File kit;
    std::vector<float> reference;
    {
        const ScopedStore store (tmp.dir.getChildFile ("store-a"));
        DrumsProcessor p;
        p.prepareToPlay (rate, 256);
        const auto a = tmp.dir.getChildFile ("one");
        const auto b = tmp.dir.getChildFile ("two");
        a.createDirectory();
        b.createDirectory();
        p.loadPad (0, writeDrum (a, "Kick.wav", 110.0, 0.4));
        p.loadPad (1, writeDrum (b, "Kick.wav", 170.0, 0.4));   // same name, other sound
        REQUIRE (p.waitForLoads());
        p.kitName = "Road Kit";
        p.prepareToPlay (rate, 256);
        reference = play (p, { 36, 38 }, 0.3);
        juce::String error;
        kit = p.exportPortableKit (tmp.dir.getChildFile ("export"), error);
        REQUIRE (kit.existsAsFile());
        CHECK (error.isEmpty());
        CHECK (kit.getParentDirectory().getChildFile ("Samples").getNumberOfChildFiles (juce::File::findFiles) == 2);
        const auto text = kit.loadFileAsString();
        CHECK_FALSE (text.contains (tmp.dir.getFullPathName()));   // nothing private
        a.deleteRecursively();
        b.deleteRecursively();
    }
    // Another computer: an empty sample store, the originals gone; only the kit folder.
    const ScopedStore store (tmp.dir.getChildFile ("store-b"));
    const auto moved = tmp.dir.getChildFile ("elsewhere");
    REQUIRE (kit.getParentDirectory().moveFileTo (moved));
    DrumsProcessor q;
    juce::String error;
    REQUIRE (q.loadKit (moved.getChildFile ("Road Kit.andorkit"), error));
    REQUIRE (q.waitForLoads());
    REQUIRE (q.padInfo (0).sound != nullptr);
    REQUIRE (q.padInfo (1).sound != nullptr);
    CHECK (q.padInfo (0).sound->contentHash != q.padInfo (1).sound->contentHash);
    q.prepareToPlay (rate, 256);
    CHECK (play (q, { 36, 38 }, 0.3) == reference);
}

TEST_CASE ("Missing samples: a folder search relinks by content, never by name alone; manual relink", "[drums-plugin][kits]")
{
    TempDir tmp;
    juce::MemoryBlock state;
    {
        const ScopedStore store (tmp.dir.getChildFile ("store-a"));
        DrumsProcessor p;
        const auto lib = tmp.dir.getChildFile ("library");
        lib.createDirectory();
        p.loadPad (0, writeDrum (lib, "Kick.wav", 110.0, 0.4));
        p.loadPad (1, writeDrum (lib, "Snare.wav", 600.0, 0.3));
        REQUIRE (p.waitForLoads());
        p.getStateInformation (state);
        // The library moves; the kick is renamed, and an unrelated file now has the snare's name.
        const auto moved = tmp.dir.getChildFile ("moved");
        moved.createDirectory();
        REQUIRE (lib.getChildFile ("Kick.wav").moveFileTo (moved.getChildFile ("Kick renamed.wav")));
        lib.getChildFile ("Snare.wav").deleteFile();
        writeDrum (moved, "Snare.wav", 900.0, 0.3);   // same name, other content
        lib.deleteRecursively();
    }
    const ScopedStore store (tmp.dir.getChildFile ("store-b"));
    DrumsProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (q.waitForLoads());
    CHECK (q.missingLayerCount() == 2);
    q.findMissingSamples (tmp.dir.getChildFile ("moved"));
    REQUIRE (q.waitForLoads());
    CHECK (q.padInfo (0).sound != nullptr);       // found by content under its new name
    CHECK (q.padInfo (1).sound == nullptr);       // the same-named impostor is not used
    CHECK (q.missingLayerCount() == 1);
    CHECK (q.relinkReport == "Found 1 of 2 missing samples.");
    // The user points the snare at a file explicitly: that is allowed (one undo step).
    q.relinkLayer (1, 0, tmp.dir.getChildFile ("moved").getChildFile ("Snare.wav"));
    REQUIRE (q.waitForLoads());
    CHECK (q.padInfo (1).sound != nullptr);
    CHECK (q.missingLayerCount() == 0);
    q.undoManager().undo();
    CHECK (q.missingLayerCount() == 1);
}

TEST_CASE ("Break: import slices it, the plan suggests pads, slice edits undo", "[drums-plugin][breaks]")
{
    TempDir tmp;
    const ScopedStore store (tmp.dir.getChildFile ("store"));
    DrumsProcessor p;
    p.prepareToPlay (rate, 256);
    p.importBreak (writeBreak (tmp.dir, "Amen-ish.wav"));
    CHECK (p.breakSession().loading);
    REQUIRE (p.waitForLoads());
    const auto& session = p.breakSession();
    REQUIRE (session.sound != nullptr);
    REQUIRE (session.slices.size() == 8);
    for (int h = 0; h < 8; ++h)
        CHECK (std::abs (static_cast<double> (session.slices[static_cast<std::size_t> (h)].onset) / rate - (0.1 + h * 0.25)) < 0.006);
    CHECK (session.bpm == Approx (120.0).margin (1.0));
    // Classified (kick, hat, snare, hat ...) and grouped: the kicks share the kick pad, the
    // snares the snare pad, the hats the closed-hat pad (M7's category plan).
    for (int h = 0; h < 8; ++h)
    {
        const auto& sl = session.slices[static_cast<std::size_t> (h)];
        const int kind = h % 4;
        INFO (h);
        CHECK (sl.category == (kind == 0 ? drums::DrumCategory::kick : kind == 2 ? drums::DrumCategory::snare : drums::DrumCategory::closedHat));
        CHECK (sl.pad == (kind == 0 ? 0 : kind == 2 ? 1 : 2));
    }

    // Delete a boundary, then undo it.
    auto edited = session.slices;
    REQUIRE (drums::removeSlice (edited, 3));
    p.editBreakSlices (edited, "Delete slice");
    CHECK (p.breakSession().slices.size() == 7);
    p.undoManager().undo();
    CHECK (p.breakSession().slices.size() == 8);

    // Audition a slice: the preview plays it (pads untouched).
    p.auditionSlice (0);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    double energy = 0.0;
    for (int b = 0; b < 20; ++b)
    {
        buffer.clear();
        p.processBlock (buffer, midi);
        energy += buffer.getRMSLevel (0, 0, 256);
    }
    CHECK (energy > 0.01);
    CHECK (p.padVoices (0) == 0);

    // A less sensitive setting finds no more slices than a sensitive one.
    auto settings = p.breakSession().settings;
    settings.sensitivity = 1.0;
    p.resliceBreak (settings);
    CHECK (p.breakSession().slices.size() >= 8);
    p.closeBreak();
    CHECK_FALSE (p.breakSession().open());
}

TEST_CASE ("Break: apply as a new kit, fill empty pads or merge; one undo step; the groove as MIDI", "[drums-plugin][breaks]")
{
    TempDir tmp;
    const ScopedStore store (tmp.dir.getChildFile ("store"));
    DrumsProcessor p;
    p.prepareToPlay (rate, 256);
    p.loadPad (1, writeDrum (tmp.dir, "MySnare.wav", 400.0, 0.3));
    REQUIRE (p.waitForLoads());
    const auto mySnare = p.padInfo (1).sound;

    // Fill empty pads: pad 2 is taken, so its slice stays in the tray.
    p.importBreak (writeBreak (tmp.dir, "Break.wav"));
    REQUIRE (p.waitForLoads());
    {
        auto plan = p.breakSession().slices;
        for (auto& s : plan)
            s.pad = -1;
        plan[0].pad = 0;
        plan[2].pad = 1;   // the occupied snare pad
        plan[4].pad = 0;   // a second kick: a round robin on pad 1
        p.editBreakSlices (plan, "Plan");
    }
    const auto report = p.applyBreak (DrumsProcessor::BreakApply::emptyPads);
    CHECK (report.contains ("2 slices placed"));
    CHECK (report.contains ("1 left in the tray"));
    REQUIRE (p.padLayers (0).layers.size() == 2);
    CHECK (p.padLayers (0).mode == drums::LayerMode::roundRobin);
    CHECK (p.padLayers (0).layers[0].sound == p.padLayers (0).layers[1].sound);   // one sound, two regions
    CHECK (p.padLayers (0).layers[0].params.end < p.padLayers (0).layers[1].params.start + 1.0e-9);
    CHECK (p.padInfo (1).sound == mySnare);
    CHECK_FALSE (p.breakSession().open());
    // The pad plays its slice: a low kick, not the whole break.
    p.prepareToPlay (rate, 256);
    const auto kick = hit (p, p.keyMap().padNote[0], 110, 0.2);
    CHECK (rmsOf (kick) > 0.001);
    CHECK (frequencyOf (kick, 480, 4800) == Approx (60.0).margin (6.0));
    // One undo step takes it all back.
    p.undoManager().undo();
    REQUIRE (p.waitForLoads());
    CHECK (p.padLayers (0).layers.empty());
    CHECK (p.padInfo (1).sound != nullptr);

    // Merge: the occupied pad gets the slice as a round robin next to its own sound.
    p.importBreak (writeBreak (tmp.dir, "Break.wav"));
    REQUIRE (p.waitForLoads());
    {
        auto plan = p.breakSession().slices;
        for (auto& s : plan)
            s.pad = -1;
        plan[2].pad = 1;
        plan[2].category = drums::DrumCategory::snare;
        p.editBreakSlices (plan, "Plan");
    }
    p.applyBreak (DrumsProcessor::BreakApply::merge);
    REQUIRE (p.padLayers (1).layers.size() == 2);
    CHECK (p.padLayers (1).layers[0].sound->filename == "MySnare.wav");

    // A new kit from the break: named after it, every planned slice on its pad.
    p.importBreak (writeBreak (tmp.dir, "Funky Drummer.wav"));
    REQUIRE (p.waitForLoads());
    p.suggestBreakPlan (true);
    p.applyBreak (DrumsProcessor::BreakApply::newKit);
    CHECK (p.kitName == "Funky Drummer");
    CHECK (p.padLayers (0).layers.size() == 2);   // two kicks: round robins
    CHECK (p.padLayers (1).layers.size() == 2);   // two snares
    CHECK (p.padLayers (2).layers.size() == 4);   // four hats
    CHECK (p.padInfo (2).category == drums::DrumCategory::closedHat);
    for (int pad = 3; pad < 9; ++pad)
        CHECK (p.padLayers (pad).layers.empty());
    REQUIRE (p.groove.hits.size() == 8);
    CHECK (p.groove.bpm == Approx (120.0).margin (1.0));
    CHECK (p.groove.hits[2].seconds == Approx (0.5).margin (0.006));

    // The groove as MIDI: 8 notes on the pads' notes, at 120 BPM.
    const auto mid = tmp.dir.getChildFile ("groove.mid");
    juce::String error;
    REQUIRE (p.writeGrooveMidi (mid, error));
    juce::MidiFile file;
    juce::FileInputStream in (mid);
    REQUIRE (file.readFrom (in));
    file.convertTimestampTicksToSeconds();
    std::vector<std::pair<double, int>> notes;
    for (const auto* e : *file.getTrack (0))
        if (e->message.isNoteOn())
            notes.push_back ({ e->message.getTimeStamp(), e->message.getNoteNumber() });
    REQUIRE (notes.size() == 8);
    CHECK (notes[2].first == Approx (0.5).margin (0.006));
    CHECK (notes[0].second == p.keyMap().padNote[static_cast<std::size_t> (p.padAtSlot (0))]);

    // The groove is saved with the kit.
    juce::MemoryBlock state;
    p.getStateInformation (state);
    DrumsProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (q.waitForLoads());
    CHECK (q.groove.hits.size() == 8);
    CHECK (q.padLayers (2).layers.size() == 4);
}

TEST_CASE ("Break view: opens with the session, chips drop onto pads, Delete joins slices", "[drums-plugin][ui][breaks]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    TempDir tmp;
    const ScopedStore store (tmp.dir.getChildFile ("store"));
    DrumsProcessor p;
    auto editor = openEditor (p);
    editor->setSize (1536, 1024);
    auto& content = editor->content();
    p.importBreak (writeBreak (tmp.dir, "Break.wav"));
    REQUIRE (p.waitForLoads());
    content.syncBreakView();   // the change message does this live
    auto& view = content.breakView();
    REQUIRE (view.isVisible());
    CHECK_FALSE (content.samplePanel().isVisible());
    // The markers sit where the slices start; the slice under a click is found.
    const auto& slices = p.breakSession().slices;
    REQUIRE (slices.size() == 8);
    CHECK (view.markerNear (view.xOfFrame (slices[3].start)) == 3);
    CHECK (view.sliceAtX (view.xOfFrame ((slices[5].start + slices[5].end) / 2)) == 5);
    CHECK (view.chipAt (view.chipRect (2).getCentre()) == 2);

    // Two chips dragged onto pad 5: both go there in the plan (one undo step).
    auto& cell = content.padGrid().cell (4);
    const juce::DragAndDropTarget::SourceDetails drop (juce::var (juce::String (drumsplugin::BreakView::dragPrefix) + "1,3"), &view, {});
    REQUIRE (cell.isInterestedInDragSource (drop));
    cell.itemDropped (drop);
    CHECK (p.breakSession().slices[1].pad == cell.pad());
    CHECK (p.breakSession().slices[3].pad == cell.pad());
    p.undoManager().undo();
    CHECK (p.breakSession().slices[1].pad != cell.pad());

    // Select two slices and press Delete: they join their previous slices.
    view.selection = { 2, 6 };
    view.removeSelectedBoundaries();
    CHECK (p.breakSession().slices.size() == 6);
    p.undoManager().undo();
    CHECK (p.breakSession().slices.size() == 8);

    // Category for the selection (the user's word: full confidence).
    view.selection = { 0 };
    view.setSelectionCategory (drums::DrumCategory::kick);
    CHECK (p.breakSession().slices[0].category == drums::DrumCategory::kick);
    CHECK (p.breakSession().slices[0].confidence == Approx (1.0));

    // Closing the session brings the pad editor back.
    p.closeBreak();
    content.syncBreakView();
    CHECK_FALSE (view.isVisible());
    CHECK (content.samplePanel().isVisible());
}

TEST_CASE ("Import a sample folder (scenario 6): classify, group, map, correct, import in one step", "[drums-plugin][import]")
{
    TempDir tmp;
    const ScopedStore store (tmp.dir.getChildFile ("store"));
    DrumsProcessor p;
    p.prepareToPlay (rate, 256);
    juce::Array<juce::File> files;
    // Three takes of one kick, a long 808, two snares, three hats, open hat, clap, tom, crash,
    // rim, shaker: 15 drum samples; plus a broken file and a duplicate.
    for (int i = 1; i <= 3; ++i)
        files.add (writeOneShot (tmp.dir, "Kick_0" + juce::String (i) + ".wav", "kick", static_cast<std::uint64_t> (i)));
    files.add (writeOneShot (tmp.dir, "808 Long.wav", "808", 5));
    files.add (writeOneShot (tmp.dir, "Snare_01.wav", "snare", 3));
    files.add (writeOneShot (tmp.dir, "Snare_02.wav", "snare", 4));
    for (int i = 1; i <= 3; ++i)
        files.add (writeOneShot (tmp.dir, "HH_0" + juce::String (i) + ".wav", "hat", static_cast<std::uint64_t> (10 + i)));
    files.add (writeOneShot (tmp.dir, "Open Hat.wav", "open", 20));
    files.add (writeOneShot (tmp.dir, "Clap.wav", "clap", 21));
    files.add (writeOneShot (tmp.dir, "Tom Low.wav", "tom", 22));
    files.add (writeOneShot (tmp.dir, "Crash.wav", "crash", 23));
    files.add (writeOneShot (tmp.dir, "Rim.wav", "rim", 24));
    files.add (writeOneShot (tmp.dir, "Shaker.wav", "shaker", 25));
    const auto broken = tmp.dir.getChildFile ("Broken.wav");
    REQUIRE (broken.replaceWithText ("this is not audio"));
    files.add (broken);
    const auto copy = tmp.dir.getChildFile ("Snare_01 copy.wav");
    REQUIRE (files[4].copyFileTo (copy));
    files.add (copy);

    p.importFiles (files);
    CHECK (p.importSession().loading());
    REQUIRE (p.waitForLoads());
    const auto& session = p.importSession();
    REQUIRE (session.items.size() == 17);
    CHECK_FALSE (session.items[15].error.empty());           // the broken file: listed, with why
    CHECK (session.items[16].duplicateOf == 4);               // the copy: listed, not placed twice
    CHECK (session.items[0].guess.category == drums::DrumCategory::kick);
    CHECK (session.items[7].guess.category == drums::DrumCategory::closedHat);

    // Groups: the three kick takes together, the 808 apart; snares together; hats together.
    auto groupOf = [&session] (int item) {
        for (std::size_t g = 0; g < session.groups.size(); ++g)
            for (int i : session.groups[g].items)
                if (i == item)
                    return static_cast<int> (g);
        return -1;
    };
    CHECK (groupOf (0) == groupOf (1));
    CHECK (groupOf (1) == groupOf (2));
    CHECK (groupOf (3) != groupOf (0));
    CHECK (groupOf (4) == groupOf (5));
    CHECK (groupOf (6) == groupOf (7));
    CHECK (groupOf (15) == -1);
    CHECK (groupOf (16) == -1);
    REQUIRE (session.groups.size() == 10);   // ten groups for nine pads: one waits in the tray
    int inTray = 0;
    for (const auto& g : session.groups)
        inTray += g.pad < 0 ? 1 : 0;
    CHECK (inTray == 1);
    CHECK (session.groups[static_cast<std::size_t> (groupOf (0))].pad == 0);   // the kicks on the kick pad
    CHECK (session.groups[static_cast<std::size_t> (groupOf (4))].pad == 1);   // the snares on the snare pad

    // Corrections: the 808 joins the kicks as a round robin; the clap is really a snare.
    {
        auto groups = session.groups;
        const auto kicks = static_cast<std::size_t> (groupOf (0)), eight = static_cast<std::size_t> (groupOf (3));
        groups[kicks].items.push_back (3);
        groups.erase (groups.begin() + static_cast<std::ptrdiff_t> (eight));
        p.editImportGroups (groups, "Merge");
    }
    CHECK (p.importSession().groups.size() == 9);
    {
        auto groups = p.importSession().groups;
        for (auto& g : groups)
            if (std::find (g.items.begin(), g.items.end(), 10) != g.items.end())
                g.category = drums::DrumCategory::snare;
        p.editImportGroups (groups, "Category");
    }
    // Now nine groups: every one gets a pad when suggestions are reset? No: corrections stay
    // until Reset. Give the tray group the last free pad by hand if there is one.
    {
        auto groups = p.importSession().groups;
        std::set<int> used;
        for (const auto& g : groups)
            if (g.pad >= 0)
                used.insert (g.pad);
        for (auto& g : groups)
            if (g.pad < 0)
                for (int pad = 0; pad < 9; ++pad)
                    if (used.count (pad) == 0)
                    {
                        g.pad = pad;
                        used.insert (pad);
                        break;
                    }
        p.editImportGroups (groups, "Assign");
    }

    const auto report = p.applyImport();
    CHECK (report.contains ("15 files on 9 pads"));
    CHECK_FALSE (p.importSession().open());
    CHECK (p.padLayers (0).layers.size() == 4);   // three kick takes and the 808
    CHECK (p.padInfo (0).category == drums::DrumCategory::kick);
    CHECK (p.padLayers (1).layers.size() == 2);
    // The kit plays: the kick pad's note sounds.
    p.prepareToPlay (rate, 256);
    CHECK (rmsOf (hit (p, p.keyMap().padNote[0], 100, 0.2)) > 0.001);
    // One undo step: the kit is empty again.
    p.undoManager().undo();
    REQUIRE (p.waitForLoads());
    for (int pad = 0; pad < 9; ++pad)
        CHECK (p.padLayers (pad).layers.empty());
}

TEST_CASE ("Import: a kit in use only gets its empty pads; a cancelled import changes nothing", "[drums-plugin][import]")
{
    TempDir tmp;
    const ScopedStore store (tmp.dir.getChildFile ("store"));
    DrumsProcessor p;
    p.loadPad (0, writeOneShot (tmp.dir, "MyKick.wav", "kick", 1));
    REQUIRE (p.waitForLoads());
    p.importFiles ({ writeOneShot (tmp.dir, "Kick B.wav", "808", 2), writeOneShot (tmp.dir, "Snare.wav", "snare", 3) });
    REQUIRE (p.waitForLoads());
    for (const auto& g : p.importSession().groups)
        CHECK (g.pad != 0);   // the used kick pad is never suggested
    p.closeImport();
    CHECK (p.padInfo (0).sound->filename == "MyKick.wav");
    CHECK (p.padLayers (1).layers.empty());
}

TEST_CASE ("Import view: opens with the session; split, move, group and destinations are undoable", "[drums-plugin][ui][import]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    TempDir tmp;
    const ScopedStore store (tmp.dir.getChildFile ("store"));
    DrumsProcessor p;
    auto editor = openEditor (p);
    editor->setSize (1536, 1024);
    auto& content = editor->content();
    p.importFiles ({ writeOneShot (tmp.dir, "Kick_01.wav", "kick", 1), writeOneShot (tmp.dir, "Kick_02.wav", "kick", 2),
                     writeOneShot (tmp.dir, "Snare.wav", "snare", 3), writeOneShot (tmp.dir, "HH.wav", "hat", 4) });
    REQUIRE (p.waitForLoads());
    content.syncBreakView();
    auto& view = content.importView();
    REQUIRE (view.isVisible());
    CHECK_FALSE (content.samplePanel().isVisible());
    REQUIRE (p.importSession().groups.size() == 3);
    REQUIRE (p.importSession().groups[0].items.size() == 2);   // the kick takes

    view.splitItem (1);   // the second kick gets its own (tray) group
    CHECK (p.importSession().groups.size() == 4);
    CHECK (p.importSession().groups.back().pad == -1);
    view.setGroupPad (3, p.importSession().groups[1].pad);   // onto the snare's pad: the snare goes to the tray
    CHECK (p.importSession().groups[1].pad == -1);
    p.undoManager().undo();
    p.undoManager().undo();
    CHECK (p.importSession().groups.size() == 3);
    CHECK (p.importSession().groups[1].pad >= 0);

    view.selection = { 2, 3 };   // snare and hat on one pad
    view.groupSelection();
    CHECK (p.importSession().groups.size() == 2);
    p.undoManager().undo();
    view.moveItem (3, 0);         // the hat into the kick group
    CHECK (p.importSession().groups[0].items.size() == 3);
    p.undoManager().undo();

    view.setGroupCategory (2, drums::DrumCategory::openHat);
    CHECK (p.importSession().groups[2].category == drums::DrumCategory::openHat);
    p.closeImport();
    content.syncBreakView();
    CHECK_FALSE (view.isVisible());
    CHECK (content.samplePanel().isVisible());
}

TEST_CASE ("Snapshots of every major state (written when ANDOR_DRUMS_SNAPSHOT_DIR is set)", "[drums-plugin][ui][snapshots]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    {
        DrumsProcessor empty;
        auto editor = openEditor (empty);
        snapshot (*editor, "01-empty-kit");
    }
    TempDir tmp;
    DrumsProcessor p;
    p.prepareToPlay (rate, 256);
    loadNine (p, tmp);
    p.setPadCategory (0, drums::DrumCategory::kick, false);
    p.parameters().getParameter (DrumsProcessor::padParamId (3, "mute"))->setValueNotifyingHost (1.0f);
    auto editor = openEditor (p);
    editor->setSize (1536, 1024);
    snapshot (*editor, "02-pads-1x8");
    sendNotes (p, { { 36, true }, { 38, true }, { 42, true } });
    editor->content().padGrid().refresh();
    snapshot (*editor, "03-playing");
    {
        // The mockup's state: EXPANDED, one octave per pad from C1 (MIDI 24 in OSP's names).
        const auto pads = p.keyMap();
        auto m = pads;
        REQUIRE (drums::applyZoneLayout (m, 12, { 0, 1, 2, 3, 4, 5, 6, 7 }, 24).applied);
        p.setKeyMap (m);
        p.setKeyboardMode (drums::KeyboardMode::expanded);
        editor->content().refreshModeBar();
        snapshot (*editor, "03b-keyboard-range-expanded");
        p.setKeyMap (pads);
        p.setKeyboardMode (drums::KeyboardMode::pad);
        editor->content().refreshModeBar();
    }
    editor->content().setLayout (DrumsProcessor::PadLayout::grid8);
    snapshot (*editor, "04a-pads-2x4");
    editor->content().setLayout (DrumsProcessor::PadLayout::grid9);
    editor->content().selectPad (8);
    snapshot (*editor, "04b-pads-3x3");
    editor->content().setLayout (DrumsProcessor::PadLayout::row8);
    editor->content().selectPad (0);
    editor->content().setKeyMapOpen (true);
    auto& km = editor->content().keyMapEditor();
    km.fitAssigned();
    snapshot (*editor, "05-keymap-pad");
    p.setKeyboardMode (drums::KeyboardMode::expanded);
    km.refreshFromProcessor();
    editor->content().refreshModeBar();   // a host or automation change; the editor's timer does this live
    km.showFullRange();
    snapshot (*editor, "06-keymap-expanded-full-range");
    REQUIRE (km.previewAutoMap (KeyMapEditor::AutoMap::oneOctave));
    km.applyPreview();
    {
        auto m = p.keyMap();
        REQUIRE (drums::setZoneRoot (m, 0, m.zone[0].low + 5).applied);
        p.setKeyMap (m);
    }
    km.fitAssigned();
    km.beginGesture (1, KeyMapEditor::Gesture::move, p.keyMap().zone[1].low);
    km.gestureTo (p.keyMap().zone[1].low - 7);
    snapshot (*editor, "07-keymap-dragging-collision");
    km.cancelGesture();
    km.setView (30.0, 24.0);
    snapshot (*editor, "08-keymap-zoomed");
    REQUIRE (km.previewAutoMap (KeyMapEditor::AutoMap::twoOctaves));
    snapshot (*editor, "09-keymap-automap-preview");
    km.cancelPreview();
    editor->setSize (juce::roundToInt (1536 * DrumsEditor::minScale), juce::roundToInt (1024 * DrumsEditor::minScale));
    snapshot (*editor, "10-keymap-minimum-size");
    editor->content().setKeyMapOpen (false);
    snapshot (*editor, "11-minimum-size");
    editor->setSize (1536, 1024);
    snapshot (*editor, "12-retina-2x", 2.0f);

    // Sample layers: a kick with three velocity layers, the middle one selected.
    p.addPadLayers (0, { writeDrumAt (tmp.dir, "Kick_soft.wav", 100.0, 0.5, 0.15), writeDrumAt (tmp.dir, "Kick_hard.wav", 100.0, 0.5, 0.9) },
                    drumsplugin::LayerDrop::velocity);
    REQUIRE (p.waitForLoads());
    editor->content().selectPad (0);
    p.selectedLayer[0] = 1;
    {
        auto params = p.padLayers (0).layers[1].params;
        params.end = 0.7;
        p.setLayerParams (0, 1, params);
    }
    p.setKeyboardMode (drums::KeyboardMode::pad);
    editor->content().refreshModeBar();
    sendNotes (p, { { p.keyMap().padNote[0], true } });
    CHECK (p.lastLayerPlayed (0) == 2);   // velocity 100: the hard layer
    editor->content().samplePanel().bind();
    snapshot (*editor, "13-layers-velocity");
    // Slicing a break: the import plan over the pad editor, some slices planned.
    p.importBreak (writeBreak (tmp.dir, "Break 120.wav", 16));
    REQUIRE (p.waitForLoads());
    {
        auto plan = p.breakSession().slices;
        const drums::DrumCategory kinds[] = { drums::DrumCategory::kick, drums::DrumCategory::closedHat, drums::DrumCategory::snare, drums::DrumCategory::closedHat };
        for (std::size_t i = 0; i < plan.size(); ++i)
            plan[i].category = kinds[i % 4];
        plan[9].pad = -1;
        plan[13].pad = -1;
        p.editBreakSlices (plan, "Plan");
    }
    editor->content().syncBreakView();
    editor->content().breakView().selection = { 2, 6 };
    editor->content().breakView().refresh();
    snapshot (*editor, "14-break-slicing");

    // Importing a folder: the plan before anything changes (scenario 6), on an empty kit.
    p.closeBreak();
    p.newKit();
    REQUIRE (p.waitForLoads());
    {
        const auto dir = tmp.dir.getChildFile ("folder");
        dir.createDirectory();
        juce::Array<juce::File> files;
        for (int i = 1; i <= 3; ++i)
            files.add (writeOneShot (dir, "Kick_0" + juce::String (i) + ".wav", "kick", static_cast<std::uint64_t> (i)));
        files.add (writeOneShot (dir, "808 Long.wav", "808", 5));
        files.add (writeOneShot (dir, "Snare_01.wav", "snare", 6));
        files.add (writeOneShot (dir, "Snare_02.wav", "snare", 7));
        for (int i = 1; i <= 3; ++i)
            files.add (writeOneShot (dir, "HH_0" + juce::String (i) + ".wav", "hat", static_cast<std::uint64_t> (10 + i)));
        files.add (writeOneShot (dir, "Open Hat.wav", "open", 20));
        files.add (writeOneShot (dir, "Clap.wav", "clap", 21));
        files.add (writeOneShot (dir, "Tom Low.wav", "tom", 22));
        files.add (writeOneShot (dir, "Crash.wav", "crash", 23));
        files.add (writeOneShot (dir, "Rim.wav", "rim", 24));
        files.add (writeOneShot (dir, "Shaker.wav", "shaker", 25));
        const auto broken = dir.getChildFile ("Broken.wav");
        REQUIRE (broken.replaceWithText ("not audio"));
        files.add (broken);
        p.importFiles (files);
        REQUIRE (p.waitForLoads());
    }
    editor->content().syncBreakView();
    editor->content().importView().selection = { 0 };
    editor->content().importView().refresh();
    snapshot (*editor, "15-import-preview");
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
