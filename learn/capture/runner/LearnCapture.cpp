// ANDOR Learn Capture Studio: the scenario runner (development only, never shipped).
//
// Plays one scenario (learn/capture/scenarios/*.json) on the real ANDOR OSP processor and
// editor, on a desktop window of a virtual display (Xvfb with a window manager:
// learn/capture/tools/xvfb-wm.sh). Every instructional action is a window event sent through the
// editor's peer (the path a mouse takes); set-up actions may set state directly and are never
// filmed. Audio runs through the processor in step with the frames (1600 samples per frame at
// 48 kHz, 30 fps): MIDI, the on-screen keyboard and an input fixture on the Sidechain bus.
//
// Output (outside Git: learn/capture/out/<scene>/):
//   master.mkv              lossless RGB master: the editor at 2x, menu windows composited
//   frames.json             per frame: time, pointer, button, highlight bounds, badge, marks
//   run.json                result, expectations, timing, environment, failure reason
//
//   osp_learn_capture --scenario <file.json> --out <dir> [--fixtures <dir>]

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "TutorialSounds.h"
#include "Binding.h"
#include "CeramicSkin.h"
#include "EngineCard.h"
#include "EqEditor.h"
#include "ArpSection.h"
#include "LearnController.h"

#include "audio/utility/TestSignals.h"
#include "io/AudioFileIO.h"

#include "ProgressStore.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <cstdio>
#include <cstring>

using namespace osp::plugin;

// JUCE's own message dispatch on Linux (declared in juce_MessageManager.cpp; the plugin tests use it too).
namespace juce::detail
{
bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages);
}

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int fps = 30;
constexpr int samplesPerFrame = static_cast<int> (sampleRate) / fps;   // 1600
constexpr int blockSize = 400;
constexpr float captureScale = 2.0f;

struct Failure : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

juce::String str (const juce::var& v, const char* key, juce::String fallback = {})
{
    return v.hasProperty (key) ? v[key].toString() : fallback;
}

double num (const juce::var& v, const char* key, double fallback)
{
    return v.hasProperty (key) ? static_cast<double> (v[key]) : fallback;
}

void setEnv (const char* name, const juce::String& value) { setenv (name, value.toRawUTF8(), 1); }

void pumpMessages()
{
    for (int i = 0; i < 500 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++i)
    {
    }
}

//==============================================================================
/** The frames, as they are taken, into a lossless master (FFmpeg, RGB, x264 at CRF 0) on a
    thread of its own: the frame loop only copies pixels. Post-processing decodes the master. */
class FrameWriter
{
public:
    explicit FrameWriter (juce::File masterFile) : master (std::move (masterFile)) {}
    ~FrameWriter() { finish(); }

    void push (int, juce::Image image)
    {
        if (pipe == nullptr)
            open (image.getWidth(), image.getHeight());
        if (image.getWidth() != width || image.getHeight() != height)
            throw std::runtime_error ("the editor changed size during the scene");
        std::vector<std::uint8_t> bytes (static_cast<std::size_t> (width) * static_cast<std::size_t> (height) * 4);
        {
            const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < height; ++y)
                std::memcpy (bytes.data() + static_cast<std::size_t> (y) * static_cast<std::size_t> (width) * 4, data.getLinePointer (y), static_cast<std::size_t> (width) * 4);
        }
        std::unique_lock<std::mutex> lock (mutex);
        space.wait (lock, [this] { return queue.size() < 48; });
        queue.push_back (std::move (bytes));
        ready.notify_one();
    }

    void finish()
    {
        {
            std::lock_guard<std::mutex> lock (mutex);
            done = true;
        }
        ready.notify_all();
        if (worker.joinable())
            worker.join();
        if (pipe != nullptr)
        {
            ffmpegStatus = pclose (pipe);
            pipe = nullptr;
        }
    }

    int status() const noexcept { return ffmpegStatus; }

private:
    void open (int w, int h)
    {
        width = w;
        height = h;
        master.deleteFile();
        const auto cmd = "ffmpeg -loglevel error -y -f rawvideo -pix_fmt bgra -s " + juce::String (w) + "x" + juce::String (h)
                         + " -r " + juce::String (fps) + " -i - -c:v libx264rgb -preset ultrafast -crf 0 -threads 2 \"" + master.getFullPathName() + "\"";
        pipe = popen (cmd.toRawUTF8(), "w");
        if (pipe == nullptr)
            throw std::runtime_error ("cannot start ffmpeg");
        worker = std::thread ([this] { run(); });
    }

    void run()
    {
        for (;;)
        {
            std::vector<std::uint8_t> job;
            {
                std::unique_lock<std::mutex> lock (mutex);
                ready.wait (lock, [this] { return done || ! queue.empty(); });
                if (queue.empty())
                    return;
                job = std::move (queue.front());
                queue.pop_front();
                space.notify_one();
            }
            std::fwrite (job.data(), 1, job.size(), pipe);
        }
    }

    juce::File master;
    FILE* pipe = nullptr;
    int width = 0, height = 0, ffmpegStatus = 0;
    std::thread worker;
    std::deque<std::vector<std::uint8_t>> queue;
    std::mutex mutex;
    std::condition_variable ready, space;
    bool done = false;
};

//==============================================================================
/** Deterministic audio fixtures (fixed seeds; the same bytes on every run and machine). */
osp::AudioData capturePhraseFixture()
{
    // D4 F4 A4 C5 A4: five sung vowels, 0.8 s each with 0.2 s between (the CAPTURE page's input).
    const int keys[] = { 62, 65, 69, 72, 69 };
    const auto note = static_cast<std::int64_t> (0.8 * sampleRate), gap = static_cast<std::int64_t> (0.2 * sampleRate);
    auto audio = osp::AudioData::allocate (2, static_cast<std::int64_t> (std::size (keys)) * (note + gap) + gap, sampleRate);
    for (std::size_t k = 0; k < std::size (keys); ++k)
    {
        const auto hz = 440.0 * std::pow (2.0, (keys[k] - 69) / 12.0);
        const auto v = osp::testsignals::vowel (hz, 0.8, sampleRate, 0x1ea0 + k, 0.5, 1);
        const auto start = gap + static_cast<std::int64_t> (k) * (note + gap);
        for (int c = 0; c < 2; ++c)
            std::copy (v.channels[0].begin(), v.channels[0].begin() + note, audio.channels[static_cast<std::size_t> (c)].begin() + start);
    }
    return audio;
}

osp::AudioData fixtureById (const juce::String& id)
{
    if (id == "input:phrase-d4")
        return capturePhraseFixture();
    throw Failure ("unknown input fixture " + id.toStdString());
}

//==============================================================================
struct FrameRecord
{
    double t = 0;
    juce::Point<float> pointer;
    bool pointerShown = false, pressed = false;
    juce::Rectangle<int> highlight;
    juce::String badge;
    juce::Point<float> badgeAt;
    juce::StringArray marks;
    juce::Rectangle<int> editorBounds;
    juce::Rectangle<int> focus;   ///< the union of the scenario's focus targets shown in this frame
};

class Runner
{
public:
    Runner (juce::var scenarioIn, juce::File outIn, juce::File fixturesIn)
        : scenario (std::move (scenarioIn)), out (std::move (outIn)), fixtures (std::move (fixturesIn))
    {
    }

    int run()
    {
        auto result = new juce::DynamicObject();
        result->setProperty ("scenario", str (scenario, "id"));
        const auto started = juce::Time::getCurrentTime();
        int rc = 0;
        try
        {
            setUpEditor();
            for (const auto& a : *scenario["start"]["setup"].getArray())
                setupAction (a);
            settle (0.6);
            checkNoUnexpected();
            writer = std::make_unique<FrameWriter> (out.getChildFile ("master.mkv"));
            clockStart = std::chrono::steady_clock::now();
            for (const auto& step : *scenario["steps"].getArray())
                runStep (step);
            result->setProperty ("status", "passed");
        }
        catch (const std::exception& e)
        {
            rc = 1;
            result->setProperty ("status", "failed");
            result->setProperty ("failure", juce::String (e.what()));
            result->setProperty ("failedAtFrame", static_cast<int> (records.size()));
            std::cerr << "FAILED: " << e.what() << "\n";
            if (editor != nullptr)
                saveImage (snapshot(), out.getChildFile ("failure-last-frame.png"));
        }
        if (writer != nullptr)
            writer->finish();
        result->setProperty ("frames", static_cast<int> (records.size()));
        result->setProperty ("expectations", expectationLog);
        result->setProperty ("expectationsPassed", expectationsPassed);
        result->setProperty ("wallSeconds", (juce::Time::getCurrentTime() - started).inSeconds());
        result->setProperty ("lateFrames", lateFrames);
        result->setProperty ("meanFrameMs", records.empty() ? 0.0 : totalFrameMs / static_cast<double> (records.size()));
        result->setProperty ("maxFrameMs", maxFrameMs);
        result->setProperty ("meanSnapshotMs", records.empty() ? 0.0 : snapshotMs / static_cast<double> (records.size()));
        result->setProperty ("meanWriteWaitMs", records.empty() ? 0.0 : writeWaitMs / static_cast<double> (records.size()));
        result->setProperty ("fps", fps);
        result->setProperty ("scale", captureScale);
        result->setProperty ("fixtures", fixtureLog);
        result->setProperty ("notesPlayed", processor != nullptr ? processor->playedVelocityCount() : 0);
        writeFramesJson();
        out.getChildFile ("run.json").replaceWithText (juce::JSON::toString (juce::var (result), false));
        teardown();
        return rc;
    }

private:
    //==========================================================================
    // Set-up

    void setUpEditor()
    {
        out.createDirectory();
        const auto scratch = out.getChildFile ("scratch");
        scratch.deleteRecursively();
        scratch.createDirectory();
        // Everything the instrument writes goes to the scene's own scratch folder, never a user's.
        setEnv ("OSP_SAMPLE_STORE", scratch.getChildFile ("store").getFullPathName());
        setEnv ("OSP_LIBRARY_DIR", scratch.getChildFile ("library").getFullPathName());
        setEnv ("OSP_DOCUMENTS_DIR", scratch.getChildFile ("documents").getFullPathName());
        setEnv ("ANDOR_LEARN_DIR", scratch.getChildFile ("learn").getFullPathName());
        setEnv ("OSP_TUTORIAL_DIR", scratch.getChildFile ("tutorial-sounds").getFullPathName());

        skin::setLook (skin::Look::ceramic);
        SourceDisplay::softwareCaches = true;

        // A fresh profile would show Learn's one-time invitation after half a second on screen.
        andor::learn::ProgressStore ("osp").update ([] (andor::learn::ProgressData& d) { d.firstInviteSeen = true; });

        processor = std::make_unique<OspAudioProcessor>();
        if (scenario["start"].getProperty ("input", false))
        {
            // The host routing a track to OSP's Sidechain input (set-up; not filmed).
            auto layout = processor->getBusesLayout();
            if (layout.inputBuses.isEmpty())
                throw Failure ("the processor has no input bus");
            layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
            if (! processor->setBusesLayout (layout))
                throw Failure ("the stereo Sidechain layout was refused");
            inputConnected = true;
        }
        processor->prepareToPlay (sampleRate, blockSize);

        editor.reset (processor->createEditorIfNeeded());
        ui = dynamic_cast<OspAudioProcessorEditor*> (editor.get());
        if (ui == nullptr)
            throw Failure ("no OSP editor");
        resizeEditor();
        editor->setTopLeftPosition (0, 0);
        editor->addToDesktop (0);
        editor->setVisible (true);
        peer = editor->getPeer();
        if (peer == nullptr)
            throw Failure ("no desktop peer: run under learn/capture/tools/xvfb-wm.sh");
        editor->toFront (true);
        // Tooltips: the capture runs slower than real time (each frame takes longer than 1/30 s),
        // so a pointer that rests a fraction of a second in the video would have rested long
        // enough in wall-clock time to raise a tooltip a person moving at that speed never sees.
        for (auto* child : editor->getChildren())
            if (auto* tips = dynamic_cast<juce::TooltipWindow*> (child))
                tips->setMillisecondsBeforeTipAppears (1 << 30);
        ui->refreshNow();
        pointer = { static_cast<float> (editor->getWidth()) * 0.62f, static_cast<float> (editor->getHeight()) * 0.30f };
    }

    void resizeEditor()
    {
        const int width = static_cast<int> (num (scenario["editor"], "width", 1448));
        editor->setSize (width, juce::roundToInt (ui->instrumentHeight() * static_cast<float> (width) / 1448.0f));
    }

    void teardown()
    {
        if (editor != nullptr)
        {
            juce::PopupMenu::dismissAllActiveMenus();
            pumpMessages();
            editor->removeFromDesktop();
            processor->editorBeingDeleted (editor.get());
            editor.reset();
        }
        processor.reset();
    }

    juce::File tutorialFile (const juce::String& id, int index = 0)
    {
        const auto example = tutorial::fromId (id);
        if (! example)
            throw Failure ("unknown tutorial sound " + id.toStdString());
        juce::String error;
        auto files = tutorial::files (*example, error);
        if (files.isEmpty())
            throw Failure ("tutorial sound not written: " + error.toStdString());
        const auto f = files[juce::jlimit (0, files.size() - 1, index)];
        noteFixture ("tutorial:" + id + (files.size() > 1 ? ":" + juce::String (index) : juce::String()), f);
        return f;
    }

    void noteFixture (const juce::String& id, const juce::File& f)
    {
        for (const auto& v : fixtureLog)
            if (v["id"].toString() == id)
                return;
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", id);
        o->setProperty ("file", f.getFileName());
        juce::MemoryBlock data;
        f.loadFileAsData (data);
        o->setProperty ("sha256", juce::SHA256 (data).toHexString());
        o->setProperty ("bytes", static_cast<juce::int64> (data.getSize()));
        fixtureLog.add (juce::var (o));
        fixtures.createDirectory();
        f.copyFileTo (fixtures.getChildFile (f.getFileName()));
    }

    void setupAction (const juce::var& a)
    {
        const auto kind = str (a, "do");
        if (kind == "addLayers")
        {
            juce::Array<juce::File> files;
            for (const auto& id : *a["sounds"].getArray())
                files.add (tutorialFile (id.toString()));
            if (processor->addLayers (files) <= 0)
                throw Failure ("addLayers failed");
            waitLoads();
        }
        else if (kind == "setParam")
            setParameter (str (a, "id"), static_cast<float> (num (a, "value", 0)));
        else if (kind == "expandArp")
        {
            ui->setArpExpanded (true);
            resizeEditor();
        }
        else if (kind == "editLayer")
            processor->setEditLayer (static_cast<int> (num (a, "layer", 0)));
        else if (kind == "arpLink")
        {
            auto& panel = ui->arpInlinePanel();
            if (panel.isLinked() != static_cast<bool> (a["value"]))
                panel.toggleLink();
        }
        else if (kind == "arpEditCard")
            ui->arpInlinePanel().setEditCard (static_cast<int> (num (a, "layer", 0)));
        else if (kind == "inputHistory")
        {
            // A connected input that has been playing for a while before the scene starts.
            startInput (str (a, "fixture"), static_cast<bool> (a.getProperty ("loop", false)));
            settle (num (a, "seconds", 6.0));
        }
        else if (kind == "input")
            startInput (str (a, "fixture"), static_cast<bool> (a.getProperty ("loop", false)));
        else if (kind == "holdNotes")
        {
            for (const auto& n : *a["keys"].getArray())
                pendingMidi.push_back (juce::MidiMessage::noteOn (1, static_cast<int> (n), static_cast<juce::uint8> (num (a, "velocity", 90))));
        }
        else if (kind == "settle")
            settle (num (a, "seconds", 0.5));
        else
            throw Failure ("unknown set-up action " + kind.toStdString());
        ui->refreshNow();
    }

    void setParameter (const juce::String& id, float value)
    {
        auto* param = processor->parameters.getParameter (id);
        if (param == nullptr)
            throw Failure ("no parameter " + id.toStdString());
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    void waitLoads()
    {
        if (! processor->waitForLoads (60000))
            throw Failure ("loading timed out");
        processor->pollLoads();
        pumpMessages();
    }

    /** Runs time forward without filming (set-up). */
    void settle (double seconds)
    {
        const int frames = static_cast<int> (seconds * fps);
        for (int i = 0; i < frames; ++i)
        {
            pumpAudio();
            pumpMessages();
            juce::Thread::sleep (2);
        }
        ui->refreshNow();
    }

    void checkNoUnexpected()
    {
        if (auto* learn = ui->learnController(); learn != nullptr && learn->isActive())
            throw Failure ("a Learn note is showing at the start");
    }

    //==========================================================================
    // Audio

    void startInput (const juce::String& id, bool loop)
    {
        if (! inputConnected)
            throw Failure ("input fixture without a connected input (start.input)");
        input = fixtureById (id);
        inputPos = 0;
        inputLoop = loop;
        // The fixture as a file beside the outputs (its checksum is the fixture's identity).
        const auto f = fixtures.getChildFile (id.replaceCharacter (':', '-') + ".wav");
        fixtures.createDirectory();
        std::string error;
        if (! f.existsAsFile() && ! osp::io::writeAudioFile (f.getFullPathName().toStdString(), input, osp::io::SampleFormat::pcm24, error))
            throw Failure ("cannot write fixture: " + error);
        noteFixture (id, f);
    }

    void pumpAudio()
    {
        for (int done = 0; done < samplesPerFrame; done += blockSize)
        {
            const int channels = std::max (processor->getTotalNumInputChannels(), processor->getTotalNumOutputChannels());
            juce::AudioBuffer<float> buffer (channels, blockSize);
            buffer.clear();
            if (inputConnected && ! input.channels.empty())
            {
                auto in = processor->getBusBuffer (buffer, true, 0);
                for (int s = 0; s < blockSize; ++s)
                {
                    if (inputPos >= input.numFrames())
                    {
                        if (! inputLoop)
                            break;
                        inputPos = 0;
                    }
                    for (int c = 0; c < in.getNumChannels(); ++c)
                        in.setSample (c, s, input.channels[static_cast<std::size_t> (std::min (c, input.numChannels() - 1))][static_cast<std::size_t> (inputPos)]);
                    ++inputPos;
                }
            }
            juce::MidiBuffer midi;
            for (const auto& m : pendingMidi)
                midi.addEvent (m, 0);
            pendingMidi.clear();
            processor->processBlock (buffer, midi);
        }
    }

    //==========================================================================
    // Targets (editor coordinates)

    juce::Rectangle<float> inEditor (juce::Component& c, juce::Rectangle<float> local) const
    {
        return editor->getLocalArea (&c, local);
    }

    juce::Component* findButton (juce::Component& root, const juce::String& text)
    {
        for (auto* child : root.getChildren())
        {
            if (! child->isVisible())
                continue;
            if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && b->getButtonText().startsWith (text))
                return b;
            if (auto* found = findButton (*child, text))
                return found;
        }
        return nullptr;
    }

    CaptureEditor* capturePage()
    {
        if (auto* page = ui->newLayerCaptureComponent(); page != nullptr && page->isShowing())
            return page;
        for (int l = 0; l < 3; ++l)
            if (auto* page = ui->engineCard (l).captureEditorComponent(); page != nullptr && page->isShowing())
                return page;
        return nullptr;
    }

    static int cardIndex (const juce::String& letter) { return juce::jlimit (0, 2, letter.toUpperCase()[0] - 'A'); }

    /** A target's bounds now, or empty if it is not shown. Grammar: "<kind>:<argument>". */
    juce::Rectangle<float> locate (const juce::String& spec)
    {
        const auto kind = spec.upToFirstOccurrenceOf (":", false, false);
        const auto arg = spec.fromFirstOccurrenceOf (":", false, false);
        auto& arp = ui->arpInlinePanel();
        if (kind == "learn")
        {
            if (auto* learn = ui->learnController())
                if (auto b = learn->targets().boundsIn (arg, *editor))
                    return b->toFloat();
            return {};
        }
        if (kind == "key")
        {
            auto& kb = ui->keyboardComponent();
            return inEditor (kb, kb.getRectangleForKey (arg.getIntValue()));
        }
        if (kind == "button")
            if (auto* b = findButton (*editor, arg))
                return inEditor (*b, b->getLocalBounds().toFloat());
        if (kind == "param")
        {
            // "<parameter>" or "<parameter>#<segment>/<count>" for a segmented control.
            const auto id = arg.upToFirstOccurrenceOf ("#", false, false);
            if (auto* c = binding::findControl (*editor, id); c != nullptr && c->isShowing())
            {
                auto r = c->getLocalBounds().toFloat();
                if (arg.contains ("#"))
                {
                    const auto seg = arg.fromFirstOccurrenceOf ("#", false, false);
                    const int i = seg.upToFirstOccurrenceOf ("/", false, false).getIntValue();
                    const int n = std::max (1, seg.fromFirstOccurrenceOf ("/", false, false).getIntValue());
                    const float w = r.getWidth() / static_cast<float> (n);
                    r = { r.getX() + w * static_cast<float> (i), r.getY(), w, r.getHeight() };
                }
                return inEditor (*c, r);
            }
            return {};
        }
        if (kind == "card")
        {
            // "A.rec", "A.notes", "A.mapNotes", "A.card"
            const int l = cardIndex (arg);
            auto& card = ui->engineCard (l);
            const auto part = arg.fromFirstOccurrenceOf (".", false, false);
            if (part == "rec")
                if (auto* r = card.recToggle(); r != nullptr && r->isShowing())
                    return inEditor (*r, r->getLocalBounds().toFloat());
            if (part == "notes" && card.notesToggle().isShowing())
                return inEditor (card.notesToggle(), card.notesToggle().getLocalBounds().toFloat());
            if (part == "mapNotes")
                if (auto* m = card.mapNotesPromptComponent(); m != nullptr && m->isShowing())
                {
                    // MapPrompt::resized: reduced 5, NO 44 at the right, 5 apart, MAP NOTES 98.
                    auto r = m->getLocalBounds().toFloat().reduced (5.0f);
                    r.removeFromRight (44.0f);
                    r.removeFromRight (5.0f);
                    return inEditor (*m, r.removeFromRight (98.0f));
                }
            if (part == "card" && card.isShowing())
                return inEditor (card, card.getLocalBounds().toFloat());
            return {};
        }
        if (kind == "capture")
        {
            auto* page = capturePage();
            if (page == nullptr)
                return {};
            if (arg == "record")
                return inEditor (*page, page->recordArea());
            if (arg == "discard")
                return inEditor (*page, page->discardArea());
            if (arg.startsWith ("grab"))
                return inEditor (*page, page->grabArea (arg.fromFirstOccurrenceOf ("grab", false, false).getIntValue()));
            if (arg == "page")
                return inEditor (*page, page->getLocalBounds().toFloat());
            return {};
        }
        if (kind == "arpctl")
        {
            auto& c = ui->arpControl();
            if (arg == "light")
                return inEditor (c, c.lightArea());
            if (arg == "name")
            {
                const auto l = c.lightArea(), ch = c.chevronArea();
                return inEditor (c, { l.getRight() + 4.0f, l.getY(), std::max (8.0f, ch.getX() - l.getRight() - 8.0f), l.getHeight() });
            }
            return {};
        }
        if (kind == "arp")
        {
            if (! arp.isShowing())
                return {};
            if (arg.startsWith ("chip."))
                return inEditor (arp, arp.cardChipArea (cardIndex (arg.fromFirstOccurrenceOf (".", false, false))));
            if (arg == "link")
                return inEditor (arp, arp.linkArea());
            if (arg.startsWith ("edit."))
                return inEditor (arp, arp.editTabArea (cardIndex (arg.fromFirstOccurrenceOf (".", false, false))));
            if (arg.startsWith ("pattern."))
                return inEditor (arp, arp.patternRowArea (arg.fromFirstOccurrenceOf (".", false, false).getIntValue()));
            if (arg == "list")
                return inEditor (arp, arp.patternListArea());
            if (arg == "field")
                return inEditor (arp, arp.patternFieldArea());
            if (arg == "display")
                return inEditor (arp, arp.displayArea());
            if (arg == "panel")
                return inEditor (arp, arp.getLocalBounds().toFloat());
            auto key = [&] (SatelliteKey* k, bool light) -> juce::Rectangle<float> {
                if (k == nullptr || ! k->isShowing())
                    return {};
                auto r = k->getLocalBounds().toFloat();
                return inEditor (*k, light ? k->lightArea().toFloat() : r.withTrimmedLeft (static_cast<float> (k->lightArea().getWidth())));
            };
            if (arg == "strum.light")
                return key (arp.strumKey(), true);
            if (arg == "strum.name")
                return key (arp.strumKey(), false);
            if (arg == "satellite.light")
                return key (arp.satelliteKey(), true);
            if (arg == "satellite.name")
                return key (arp.satelliteKey(), false);
            return {};
        }
        if (kind == "popover")
            if (auto* panel = ui->openPopupPanel(); panel != nullptr && panel->isShowing())
                return inEditor (*panel, panel->getLocalBounds().toFloat());
        if (kind == "editor")
            return editor->getLocalBounds().toFloat();
        return {};
    }

    juce::Rectangle<float> require (const juce::String& spec)
    {
        auto r = locate (spec);
        if (r.isEmpty())
            throw Failure ("target not shown: " + spec.toStdString());
        if (! editor->getLocalBounds().toFloat().contains (r.getCentre()))
            throw Failure ("target outside the editor: " + spec.toStdString());
        return r;
    }

    //==========================================================================
    // Conditions

    float parameterValue (const juce::String& id)
    {
        auto* param = processor->parameters.getParameter (id);
        if (param == nullptr)
            throw Failure ("no parameter " + id.toStdString());
        return param->convertFrom0to1 (param->getValue());
    }

    /** {"state": id} | {"param": id, "equals"|"min"|"max": v} | {"layers": n} | {"mapped": "A"}
        | {"activity": "A", "is": "none"} | {"notesPlayed": n} | {"recording": "A"|"none"}
        | {"capturePage": true|false} | {"target": spec} | {"linked": bool} | {"notesOpen": "A"} */
    bool holds (const juce::var& c, juce::String& describe)
    {
        if (c.hasProperty ("state"))
        {
            describe = "state " + str (c, "state");
            auto* learn = ui->learnController();
            if (learn == nullptr || ! learn->targets().hasState (str (c, "state")))
                throw Failure ("unknown state " + str (c, "state").toStdString());
            return learn->targets().state (str (c, "state")) == static_cast<bool> (c.getProperty ("is", true));
        }
        if (c.hasProperty ("param"))
        {
            const auto v = parameterValue (str (c, "param"));
            describe = str (c, "param") + " = " + juce::String (v, 3);
            if (c.hasProperty ("equals"))
                return std::abs (v - static_cast<float> (static_cast<double> (c["equals"]))) < 1.0e-3f;
            bool ok = true;
            if (c.hasProperty ("min"))
                ok = ok && v >= static_cast<float> (static_cast<double> (c["min"]));
            if (c.hasProperty ("max"))
                ok = ok && v <= static_cast<float> (static_cast<double> (c["max"]));
            return ok;
        }
        if (c.hasProperty ("paramText"))
        {
            // The parameter's own displayed value (e.g. "CHORD"): checks what a person would read,
            // not a stored index that may differ from the order on screen.
            auto* param = processor->parameters.getParameter (str (c, "paramText"));
            if (param == nullptr)
                throw Failure ("no parameter " + str (c, "paramText").toStdString());
            const auto text = param->getCurrentValueAsText();
            describe = str (c, "paramText") + " reads \"" + text + "\"";
            return text.equalsIgnoreCase (str (c, "equals"));
        }
        if (c.hasProperty ("layers"))
        {
            describe = "layers = " + juce::String (processor->occupiedLayerCount());
            return processor->occupiedLayerCount() == static_cast<int> (c["layers"]);
        }
        if (c.hasProperty ("mapped"))
        {
            const int l = cardIndex (str (c, "mapped"));
            describe = "layer " + str (c, "mapped") + " mapped (atlas, analysis finished)";
            return processor->atlasActivity (l) == InstrumentHost::AtlasActivity::none && processor->mapNotesPrompt (l) == 0 && hasAtlas (l);
        }
        if (c.hasProperty ("notesPlayed"))
        {
            describe = "notes played = " + juce::String (processor->playedVelocityCount());
            return processor->playedVelocityCount() >= static_cast<int> (c["notesPlayed"]);
        }
        if (c.hasProperty ("recording"))
        {
            const auto want = str (c, "recording");
            describe = "recording layer = " + juce::String (processor->recordingLayer());
            return want == "none" ? processor->recordingLayer() < 0 : processor->recordingLayer() == cardIndex (want);
        }
        if (c.hasProperty ("capturePage"))
        {
            describe = "capture page shown";
            return (capturePage() != nullptr) == static_cast<bool> (c["capturePage"]);
        }
        if (c.hasProperty ("target"))
        {
            describe = "target shown " + str (c, "target");
            return ! locate (str (c, "target")).isEmpty();
        }
        if (c.hasProperty ("linked"))
        {
            describe = "ARP linked";
            return ui->arpInlinePanel().isLinked() == static_cast<bool> (c["linked"]);
        }
        if (c.hasProperty ("notesOpen"))
        {
            describe = "NOTES open on " + str (c, "notesOpen");
            return ui->engineCard (cardIndex (str (c, "notesOpen"))).isNotesOpen();
        }
        if (c.hasProperty ("popover"))
        {
            describe = "a popover is open";
            return (ui->openPopupPanel() != nullptr) == static_cast<bool> (c["popover"]);
        }
        throw Failure ("unknown condition " + juce::JSON::toString (c, true).toStdString());
    }

    bool hasAtlas (int layer)
    {
        auto* learn = ui->learnController();
        if (learn == nullptr)
            return false;
        const auto before = processor->editLayer();
        processor->setEditLayer (layer);
        const bool mapped = learn->targets().state ("osp.source.mapped");
        processor->setEditLayer (before);
        return mapped;
    }

    void expect (const juce::var& c, const juce::String& context)
    {
        juce::String what;
        const bool ok = holds (c, what);
        auto* o = new juce::DynamicObject();
        o->setProperty ("step", context);
        o->setProperty ("frame", static_cast<int> (records.size()));
        o->setProperty ("check", what);
        o->setProperty ("passed", ok);
        expectationLog.add (juce::var (o));
        if (! ok)
            throw Failure ("expectation failed in " + context.toStdString() + ": " + what.toStdString());
        ++expectationsPassed;
    }

    //==========================================================================
    // Frames

    juce::Image snapshot()
    {
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, captureScale);
        // Menus open as their own desktop windows (as in a host): drawn where they stand.
        const auto origin = editor->getScreenPosition();
        auto& desktop = juce::Desktop::getInstance();
        juce::Graphics g (image);
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* c = desktop.getComponent (i); c != nullptr && c != editor.get() && c->isVisible()
                                                    && juce::String (typeid (*c).name()).contains ("MenuWindow"))
            {
                const auto b = c->getScreenBounds() - origin;
                g.drawImageAt (c->createComponentSnapshot (c->getLocalBounds(), true, captureScale),
                               juce::roundToInt (static_cast<float> (b.getX()) * captureScale),
                               juce::roundToInt (static_cast<float> (b.getY()) * captureScale));
            }
        return image;
    }

    static void saveImage (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream os (file);
        juce::PNGImageFormat().writeImageToStream (image, os);
    }

    /** One video frame: audio for 1/30 s, the message loop (timers, posted closes), paced to
        the wall clock (the interface's own timings are in real time), then the picture. */
    void frame()
    {
        const auto t0 = std::chrono::steady_clock::now();
        pumpAudio();
        pumpMessages();
        const auto deadline = clockStart + std::chrono::microseconds (static_cast<long long> ((records.size() + 1) * 1.0e6 / fps));
        while (std::chrono::steady_clock::now() < deadline)
        {
            pumpMessages();
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
        if (std::chrono::steady_clock::now() > deadline + std::chrono::milliseconds (17))
            ++lateFrames;
        if (editor->getHeight() != lastHeight)
            lastHeight = editor->getHeight();
        const auto s0 = std::chrono::steady_clock::now();
        auto image = snapshot();
        snapshotMs += std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - s0).count();
        FrameRecord r;
        r.t = static_cast<double> (records.size()) / fps;
        r.pointer = pointer;
        r.pointerShown = pointerShown;
        r.pressed = buttonDown;
        if (highlightSpec.isNotEmpty())
            r.highlight = locate (highlightSpec).getSmallestIntegerContainer();
        r.badge = badge;
        r.badgeAt = badgeAt;
        r.marks = pendingMarks;
        pendingMarks.clear();
        r.editorBounds = editor->getLocalBounds();
        if (auto* focusList = scenario["frame"]["focus"].getArray())
            for (const auto& f : *focusList)
                if (const auto b = locate (f.toString()).getSmallestIntegerContainer(); ! b.isEmpty())
                    r.focus = r.focus.isEmpty() ? b : r.focus.getUnion (b);
        records.push_back (r);
        const auto w0 = std::chrono::steady_clock::now();
        writer->push (static_cast<int> (records.size()) - 1, image);
        writeWaitMs += std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - w0).count();
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        totalFrameMs += ms;
        maxFrameMs = std::max (maxFrameMs, ms);
    }

    void frames (double seconds)
    {
        const int n = std::max (1, juce::roundToInt (seconds * fps));
        for (int i = 0; i < n; ++i)
            frame();
    }

    //==========================================================================
    // Pointer: real window events through the editor's peer

    void sendMouse (juce::Point<float> at, bool down, juce::ModifierKeys extra = {})
    {
        const auto buttons = down ? juce::ModifierKeys::leftButtonModifier : 0;
        const juce::ModifierKeys mods (buttons | extra.getRawFlags());
        juce::ModifierKeys::currentModifiers = mods;
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, at, mods, juce::MouseInputSource::defaultPressure,
                                juce::MouseInputSource::defaultOrientation, juce::Time::currentTimeMillis());
        pointer = at;
        buttonDown = down;
    }

    static float ease (float x) { return x < 0.5f ? 4.0f * x * x * x : 1.0f - std::pow (-2.0f * x + 2.0f, 3.0f) / 2.0f; }

    /** Glides the pointer to `to` (mouse moves, or drags while pressed) over `seconds`. */
    void glide (juce::Point<float> to, double seconds)
    {
        pointerShown = true;
        const auto from = pointer;
        const int n = std::max (1, juce::roundToInt (seconds * fps));
        for (int i = 1; i <= n; ++i)
        {
            const float k = ease (static_cast<float> (i) / static_cast<float> (n));
            sendMouse (from + (to - from) * k, buttonDown);
            frame();
        }
    }

    void moveTo (const juce::String& spec, double seconds)
    {
        const auto target = require (spec).getCentre();
        const double distance = pointer.getDistanceFrom (target);
        glide (target, seconds > 0 ? seconds : juce::jlimit (0.35, 0.9, distance / 900.0));
    }

    void click (const juce::String& spec, double moveSeconds)
    {
        moveTo (spec, moveSeconds);
        frames (0.12);
        const auto at = require (spec).getCentre();   // (found again: a layout may have moved)
        sendMouse (at, true);
        frames (0.12);
        sendMouse (at, false);
        frame();
    }

    //==========================================================================
    // Steps

    void runStep (const juce::var& step)
    {
        const auto mark = str (step, "mark");
        pendingMarks.add (mark);
        for (const auto& a : *step["do"].getArray())
            action (a, mark);
        if (auto* list = step["expect"].getArray())
            for (const auto& c : *list)
                expect (c, mark);
    }

    void action (const juce::var& a, const juce::String& mark)
    {
        const auto kind = str (a, "do");
        if (kind == "hold")
            frames (num (a, "seconds", 1.0));
        else if (kind == "highlight")
            highlightSpec = str (a, "target");
        else if (kind == "pointer")
        {
            pointerShown = static_cast<bool> (a.getProperty ("shown", true));
            if (a.hasProperty ("at"))
                sendMouse (require (str (a, "at")).getCentre(), false);
        }
        else if (kind == "move")
            moveTo (str (a, "target"), num (a, "seconds", 0));
        else if (kind == "click")
        {
            if (a.hasProperty ("highlight") && static_cast<bool> (a["highlight"]))
            {
                highlightSpec = str (a, "target");
                frames (0.3);
            }
            click (str (a, "target"), num (a, "move", 0));
        }
        else if (kind == "key")
        {
            // A key of the on-screen keyboard, pressed with the mouse for `seconds`.
            const auto spec = "key:" + juce::String (static_cast<int> (a["note"]));
            moveTo (spec, num (a, "move", 0));
            frames (0.08);
            const auto r = require (spec);
            const juce::Point<float> at { r.getCentreX(), r.getY() + r.getHeight() * 0.78f };
            sendMouse (at, true);
            frames (num (a, "seconds", 0.6));
            sendMouse (at, false);
            frames (num (a, "after", 0.15));
        }
        else if (kind == "drag")
        {
            const auto spec = str (a, "target");
            moveTo (spec, num (a, "move", 0));
            frames (0.12);
            const auto start = require (spec).getCentre();
            sendMouse (start, true);
            frames (0.1);
            glide (start + juce::Point<float> (static_cast<float> (num (a, "dx", 0)), static_cast<float> (num (a, "dy", 0))), num (a, "seconds", 0.8));
            frames (0.1);
            sendMouse (pointer, false);
            frame();
        }
        else if (kind == "wheel")
        {
            const auto spec = str (a, "target");
            moveTo (spec, num (a, "move", 0));
            const int steps = static_cast<int> (num (a, "steps", 1));
            for (int i = 0; i < std::abs (steps); ++i)
            {
                juce::MouseWheelDetails w;
                w.deltaX = 0.0f;
                w.deltaY = steps > 0 ? -0.25f : 0.25f;   // positive steps scroll down the list
                w.isReversed = false;
                w.isSmooth = false;
                w.isInertial = false;
                peer->handleMouseWheel (juce::MouseInputSource::InputSourceType::mouse, pointer, juce::Time::currentTimeMillis(), w);
                frames (0.1);
            }
        }
        else if (kind == "scrollUntil")
        {
            // Mouse-wheel steps over `target` until `until` is shown (a list scrolled by the user).
            const auto spec = str (a, "target"), until = str (a, "until");
            moveTo (spec, num (a, "move", 0));
            for (int i = 0; locate (until).isEmpty(); ++i)
            {
                if (i >= 30)
                    throw Failure ("scrolled 30 steps and " + until.toStdString() + " is still not shown");
                juce::MouseWheelDetails w;
                w.deltaX = 0.0f;
                w.deltaY = -0.25f;
                w.isReversed = false;
                w.isSmooth = false;
                w.isInertial = false;
                peer->handleMouseWheel (juce::MouseInputSource::InputSourceType::mouse, pointer, juce::Time::currentTimeMillis(), w);
                frames (0.08);
            }
            frames (0.15);
        }
        else if (kind == "drop")
        {
            // The editor's own drop handler (filesDropped) at the target, after a file badge
            // travels there. Not an OS drag-and-drop: those cannot be produced on a virtual
            // display. The import and analysis that follow are the instrument's own.
            const auto file = tutorialFile (str (a, "sound"));
            const auto target = require (str (a, "target")).getCentre();
            badge = file.getFileName();
            pointerShown = true;
            const juce::Point<float> from { std::min (static_cast<float> (editor->getWidth()) - 260.0f, target.x + 300.0f), target.y - 90.0f };
            pointer = from;
            badgeAt = from;
            frames (0.25);
            const int n = juce::roundToInt (num (a, "seconds", 1.0) * fps);
            for (int i = 1; i <= n; ++i)
            {
                const float k = ease (static_cast<float> (i) / static_cast<float> (n));
                pointer = from + (target - from) * k;
                badgeAt = pointer;
                frame();
            }
            ui->fileDragEnter ({ file.getFullPathName() }, juce::roundToInt (target.x), juce::roundToInt (target.y));
            frames (0.2);
            ui->filesDropped ({ file.getFullPathName() }, juce::roundToInt (target.x), juce::roundToInt (target.y));
            badge.clear();
            frame();
        }
        else if (kind == "midi")
        {
            const bool on = str (a, "state", "on") == "on";
            for (const auto& n : *a["keys"].getArray())
                pendingMidi.push_back (on ? juce::MidiMessage::noteOn (1, static_cast<int> (n), static_cast<juce::uint8> (num (a, "velocity", 90)))
                                          : juce::MidiMessage::noteOff (1, static_cast<int> (n)));
            frame();
        }
        else if (kind == "input")
            startInput (str (a, "fixture"), static_cast<bool> (a.getProperty ("loop", false)));
        else if (kind == "waitFor")
        {
            const double timeout = num (a, "timeout", 20.0);
            const int maxFrames = juce::roundToInt (timeout * fps);
            juce::String what;
            int i = 0;
            for (; i < maxFrames; ++i)
            {
                if (holds (a["until"], what))
                    break;
                frame();
            }
            if (i == maxFrames)
                throw Failure ("timed out after " + std::to_string (timeout) + " s waiting for " + what.toStdString() + " in " + mark.toStdString());
            auto* o = new juce::DynamicObject();
            o->setProperty ("step", mark);
            o->setProperty ("frame", static_cast<int> (records.size()));
            o->setProperty ("check", "waited " + juce::String (static_cast<double> (i) / fps, 2) + " s for " + what);
            o->setProperty ("passed", true);
            expectationLog.add (juce::var (o));
            ++expectationsPassed;
        }
        else if (kind == "expect")
            expect (a["that"], mark);
        else
            throw Failure ("unknown action " + kind.toStdString() + " (set-up actions belong in start.setup)");
    }

    //==========================================================================

    void writeFramesJson()
    {
        juce::Array<juce::var> list;
        for (const auto& r : records)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("t", r.t);
            o->setProperty ("x", r.pointer.x);
            o->setProperty ("y", r.pointer.y);
            o->setProperty ("shown", r.pointerShown);
            o->setProperty ("down", r.pressed);
            if (! r.highlight.isEmpty())
                o->setProperty ("hl", juce::Array<juce::var> { r.highlight.getX(), r.highlight.getY(), r.highlight.getWidth(), r.highlight.getHeight() });
            if (r.badge.isNotEmpty())
            {
                o->setProperty ("badge", r.badge);
                o->setProperty ("bx", r.badgeAt.x);
                o->setProperty ("by", r.badgeAt.y);
            }
            if (! r.marks.isEmpty())
                o->setProperty ("marks", r.marks.joinIntoString (","));
            if (! r.focus.isEmpty())
                o->setProperty ("focus", juce::Array<juce::var> { r.focus.getX(), r.focus.getY(), r.focus.getWidth(), r.focus.getHeight() });
            o->setProperty ("w", r.editorBounds.getWidth());
            o->setProperty ("h", r.editorBounds.getHeight());
            list.add (juce::var (o));
        }
        auto* root = new juce::DynamicObject();
        root->setProperty ("fps", fps);
        root->setProperty ("scale", captureScale);
        root->setProperty ("frames", list);
        out.getChildFile ("frames.json").replaceWithText (juce::JSON::toString (juce::var (root), true));
    }

    juce::var scenario;
    juce::File out, fixtures;
    std::unique_ptr<OspAudioProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    OspAudioProcessorEditor* ui = nullptr;
    juce::ComponentPeer* peer = nullptr;
    std::unique_ptr<FrameWriter> writer;
    std::vector<FrameRecord> records;
    std::chrono::steady_clock::time_point clockStart;
    juce::Point<float> pointer, badgeAt;
    bool pointerShown = false, buttonDown = false;
    juce::String highlightSpec, badge;
    juce::StringArray pendingMarks;
    std::vector<juce::MidiMessage> pendingMidi;
    bool inputConnected = false, inputLoop = false;
    osp::AudioData input;
    std::int64_t inputPos = 0;
    juce::Array<juce::var> expectationLog, fixtureLog;
    int expectationsPassed = 0, lateFrames = 0, lastHeight = 0;
    double totalFrameMs = 0, maxFrameMs = 0, snapshotMs = 0, writeWaitMs = 0;
};
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::String scenarioPath, outPath, fixturesPath;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const juce::String key (argv[i]), value (argv[i + 1]);
        if (key == "--scenario")
            scenarioPath = value;
        else if (key == "--out")
            outPath = value;
        else if (key == "--fixtures")
            fixturesPath = value;
    }
    if (scenarioPath.isEmpty() || outPath.isEmpty())
    {
        std::cerr << "usage: osp_learn_capture --scenario <file.json> --out <dir> [--fixtures <dir>]\n";
        return 2;
    }
    const juce::File scenarioFile = juce::File::getCurrentWorkingDirectory().getChildFile (scenarioPath);
    const auto scenario = juce::JSON::parse (scenarioFile);
    if (! scenario.isObject() || static_cast<int> (scenario["schemaVersion"]) != 1 || scenario["kind"].toString() != "andor.learn.scenario")
    {
        std::cerr << "not an andor.learn.scenario of schemaVersion 1: " << scenarioPath << "\n";
        return 2;
    }
    const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (outPath);
    const auto fixtures = fixturesPath.isNotEmpty() ? juce::File::getCurrentWorkingDirectory().getChildFile (fixturesPath) : outDir.getChildFile ("fixtures");
    Runner runner (scenario, outDir, fixtures);
    return runner.run();
}
