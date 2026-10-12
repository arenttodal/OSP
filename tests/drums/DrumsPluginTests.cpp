// Headless tests of the ANDOR Drums plugin processor: the object a DAW hosts. Identity
// (never OSP's), Milestone 1 through processBlock (two pads, independent notes, the shared
// engine), keyboard modes, session recall, and the functional editor.

#include "CeramicSkin.h"
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
