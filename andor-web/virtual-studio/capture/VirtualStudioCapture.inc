// ANDOR Virtual Studio: the plugin capture of a performance project (internal marketing tool).
//
// Include from the plugin's test translation unit (tests/plugin/PluginProcessorTests.cpp, after the
// other .inc files; it uses their helpers: TempDir, ScopedEnvironment, ScopedLook, writeSource,
// testsignals) and run:
//
//   VS_PROJECT=<project dir> VS_FPS=24 VS_BUILD=$(git describe --always --dirty) \
//     osp_plugin_tests "[vs-capture]"
//
// It reads <project>/performance/performance.json (schema 2, written by vs.py ingest) and writes
// <project>/captures/<fps>fps/: face/NNNN.png (the editor without knob caps, 2x: the Blender
// panel texture), editor/NNNN.png (as seen, 1x), knobs/NNNN.json (every rotary: centre, pointer
// angle, parameter, value), capture_meta.json; and <project>/audio/headless.wav (the plugin's
// own render of the same performance, for sync checks against the DAW bounce).
//
// Timing: one sample clock. Frame n (0-based, file n+1) shows the state at t = n / fps, i.e. after
// samples [0, round(n * sr / fps)) have been processed. MIDI events go in at their exact sample
// offsets; automation is applied at 64-sample steps (as a host delivers it per block). The
// on-screen keyboard is the plugin's own (it follows the incoming MIDI): nothing is drawn for it
// here, so notes the instrument generates itself (the arpeggiator) show only where the plugin
// shows them.
//
// State: <project>/preset/*.state (a plugin state blob, getStateInformation) if present, else the
// project's sources/*.wav loaded as layers, else the proof of concept's hero state on test
// signals (labelled "test-signal" in capture_meta.json: never flagship material).

namespace osp::plugin::design::draw { extern bool captureWithoutCaps; }   // the capture hook (Design.cpp)

namespace vscapture
{
    struct Ev { std::int64_t sample; juce::MidiMessage msg; };

    inline double valueAt (const juce::Array<juce::var>& rows, double t)
    {
        if (rows.isEmpty()) return 0.0;
        auto tv = [] (const juce::var& r, int i) { return static_cast<double> (r[i]); };
        if (t <= tv (rows[0], 0)) return tv (rows[0], 1);
        for (int i = 0; i + 1 < rows.size(); ++i)
        {
            const double t0 = tv (rows[i], 0), t1 = tv (rows[i + 1], 0);
            if (t >= t0 && t < t1)
                return t1 > t0 ? tv (rows[i], 1) + (tv (rows[i + 1], 1) - tv (rows[i], 1)) * (t - t0) / (t1 - t0) : tv (rows[i], 1);
        }
        return tv (rows.getLast(), 1);
    }

    inline void savePng (const juce::Image& image, const juce::File& f)
    {
        // Written to a temporary name and moved: a frame on disk is always complete.
        const auto tmp = f.getSiblingFile (f.getFileName() + ".tmp");
        {
            juce::FileOutputStream s (tmp);
            REQUIRE (s.openedOk());
            s.setPosition (0);
            s.truncate();
            REQUIRE (juce::PNGImageFormat().writeImageToStream (image, s));
            s.flush();
            REQUIRE (! s.getStatus().failed());
        }
        REQUIRE (tmp.moveFileTo (f));
    }
}

TEST_CASE ("plugin: ANDOR Virtual Studio capture of a performance project (VS_PROJECT)", "[.][vs-capture]")
{
    using namespace vscapture;
    const juce::File project (juce::SystemStats::getEnvironmentVariable ("VS_PROJECT", {}));
    REQUIRE (project.isDirectory());
    const double fps = juce::SystemStats::getEnvironmentVariable ("VS_FPS", "24").getDoubleValue();
    REQUIRE (fps > 0.0);
    const auto perf = juce::JSON::parse (project.getChildFile ("performance/performance.json"));
    REQUIRE (static_cast<int> (perf["schemaVersion"]) == 2);
    const double sampleRate = perf["timebase"]["sampleRate"], duration = perf["timebase"]["duration"];
    const int frames = static_cast<int> (std::ceil (duration * fps - 1e-9));
    auto frameSample = [&] (int n) { return static_cast<std::int64_t> (std::llround (n * sampleRate / fps)); };
    const auto outDir = project.getChildFile ("captures/" + juce::String (juce::roundToInt (fps)) + "fps");
    for (const char* sub : { "face", "editor", "knobs" })
        outDir.getChildFile (sub).createDirectory();

    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedLook ceramic (osp::plugin::skin::Look::ceramic);
    using Editor = osp::plugin::OspAudioProcessorEditor;

    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<Editor*> (e.get());
    REQUIRE (ui != nullptr);
    juce::String stateKind;
    const auto states = project.getChildFile ("preset").findChildFiles (juce::File::findFiles, false, "*.state");
    const auto sources = project.getChildFile ("sources").findChildFiles (juce::File::findFiles, false, "*.wav");
    if (! states.isEmpty())
    {
        juce::MemoryBlock blob;
        REQUIRE (states[0].loadFileAsData (blob));
        p.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
        stateKind = "preset:" + states[0].getFileName();
    }
    else if (! sources.isEmpty())
    {
        juce::Array<juce::File> src;
        for (const auto& f : sources) src.add (f);
        src.sort();
        p.addLayers (src);
        stateKind = "sources";
    }
    else
    {
        juce::Array<juce::File> src;   // the proof of concept's hero state (test signals)
        src.add (writeSource (tmp.dir, "A.wav", testsignals::vowel (midiToHz (60), 2.4, 48000.0, 3)));
        src.add (writeSource (tmp.dir, "B.wav", testsignals::pluck (220.0, 2.6, 48000.0, 4)));
        src.add (writeSource (tmp.dir, "C.wav", testsignals::vowel (midiToHz (48), 2.4, 48000.0, 5)));
        p.addLayers (src);
        stateKind = "test-signal";
    }
    REQUIRE (p.waitForLoads (60000));
    p.pollLoads();
    e->setSize (1448, juce::roundToInt (ui->instrumentHeight()));

    // The performance as sample-stamped MIDI (notes, pitch bend, CC) and automation lanes.
    std::vector<Ev> evs;
    auto at = [&] (double t) { return static_cast<std::int64_t> (std::llround (t * sampleRate)); };
    for (const auto& n : *perf["notes"].getArray())
    {
        const int note = n["note"], vel = n["velocity"], ch = static_cast<int> (n["channel"]) + 1;
        evs.push_back ({ at (n["on"]), juce::MidiMessage::noteOn (ch, note, static_cast<juce::uint8> (vel)) });
        evs.push_back ({ at (n["off"]), juce::MidiMessage::noteOff (ch, note) });
    }
    for (const auto& r : *perf["pitchBend"].getArray())
        evs.push_back ({ at (r[0]), juce::MidiMessage::pitchWheel (1, juce::jlimit (0, 16383, 8192 + static_cast<int> (std::lround (static_cast<double> (r[1]) * 8191.0)))) });
    if (auto* cc = perf["cc"].getDynamicObject())
        for (const auto& lane : cc->getProperties())
            for (const auto& r : *lane.value.getArray())
                evs.push_back ({ at (r[0]), juce::MidiMessage::controllerEvent (1, lane.name.toString().getIntValue(), static_cast<int> (r[1])) });
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) {
        return a.sample != b.sample ? a.sample < b.sample : (a.msg.isNoteOff() && ! b.msg.isNoteOff());   // off before on at one instant
    });
    std::vector<std::pair<juce::String, juce::Array<juce::var>>> lanes;
    if (auto* au = perf["automation"].getDynamicObject())
        for (const auto& lane : au->getProperties())
            lanes.push_back ({ lane.name.toString(), *lane.value.getArray() });

    constexpr int step = 64, maxBlock = 2048;
    p.prepareToPlay (sampleRate, maxBlock);
    const auto total = frameSample (frames);
    juce::AudioBuffer<float> audio (2, static_cast<int> (total)), buffer (2, maxBlock);
    std::size_t next = 0;
    std::int64_t pos = 0;
    auto processTo = [&] (std::int64_t end) {
        while (pos < end)
        {
            const int n = static_cast<int> (std::min<std::int64_t> ({ step, end - pos }));
            const double t = static_cast<double> (pos) / sampleRate;
            for (const auto& [id, rows] : lanes)
                p.setParameterValue (id, static_cast<float> (valueAt (rows, t)));
            juce::MidiBuffer midi;
            while (next < evs.size() && evs[next].sample < pos + n)
            {
                midi.addEvent (evs[next].msg, static_cast<int> (std::max<std::int64_t> (0, evs[next].sample - pos)));
                ++next;
            }
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
            view.clear();
            p.processBlock (view, midi);
            for (int ch = 0; ch < 2; ++ch)
                audio.copyFrom (ch, static_cast<int> (pos), view, ch, 0, n);
            pos += n;
        }
    };
    juce::Array<juce::var> boundaries;
    for (int f = 0; f < frames; ++f)
    {
        processTo (frameSample (f));   // the state at t = f / fps
        boundaries.add (static_cast<juce::int64> (frameSample (f)));
        ui->refreshNow();
        ui->refreshNow();
        const auto num = juce::String (f + 1).paddedLeft ('0', 4);
        if (! outDir.getChildFile ("face/" + num + ".png").existsAsFile())   // resumable runs only redo missing frames
        {
            savePng (e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f), outDir.getChildFile ("editor/" + num + ".png"));
            juce::Array<juce::var> knobs;
            // Headless, nothing is "showing" (no window): a rotary counts when it and every
            // ancestor up to the editor are visible.
            auto visibleInEditor = [&] (juce::Component& c) {
                for (auto* p = &c; p != nullptr && p != ui; p = p->getParentComponent())
                    if (! p->isVisible())
                        return false;
                return true;
            };
            std::function<void (juce::Component&)> walk = [&] (juce::Component& parent) {
                for (auto* child : parent.getChildren())
                {
                    if (auto* s = dynamic_cast<juce::Slider*> (child); s != nullptr && s->isRotary() && visibleInEditor (*s))
                    {
                        const auto b = ui->getLocalArea (s, s->getLocalBounds()).toFloat();
                        const auto rp = s->getRotaryParameters();
                        const auto prop = static_cast<float> (s->valueToProportionOfLength (s->getValue()));
                        auto* k = new juce::DynamicObject();
                        k->setProperty ("x", b.getCentreX());
                        k->setProperty ("y", b.getCentreY());
                        k->setProperty ("angle", rp.startAngleRadians + prop * (rp.endAngleRadians - rp.startAngleRadians));
                        k->setProperty ("proportion", prop);
                        k->setProperty ("value", s->getValue());
                        k->setProperty ("text", s->getTextFromValue (s->getValue()));
                        k->setProperty ("param", s->getProperties()["paramId"].toString());
                        knobs.add (juce::var (k));
                    }
                    walk (*child);
                }
            };
            walk (*ui);
            outDir.getChildFile ("knobs/" + num + ".json").replaceWithText (juce::JSON::toString (juce::var (knobs), true));
            osp::plugin::design::draw::captureWithoutCaps = true;
            savePng (e->createComponentSnapshot (e->getLocalBounds(), true, 2.0f), outDir.getChildFile ("face/" + num + ".png"));
            osp::plugin::design::draw::captureWithoutCaps = false;
        }
    }
    processTo (total);
    {
        const auto wavFile = project.getChildFile ("audio/headless.wav");
        project.getChildFile ("audio").createDirectory();
        wavFile.deleteFile();
        std::unique_ptr<juce::OutputStream> os (wavFile.createOutputStream());
        REQUIRE (os != nullptr);
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sampleRate, 2, 24, {}, 0));
        REQUIRE (w != nullptr);
        os.release();
        REQUIRE (w->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()));
    }
    auto* meta = new juce::DynamicObject();
    meta->setProperty ("schemaVersion", 1);
    meta->setProperty ("build", juce::SystemStats::getEnvironmentVariable ("VS_BUILD", "unknown"));
    meta->setProperty ("state", stateKind);
    meta->setProperty ("fps", fps);
    meta->setProperty ("frames", frames);
    meta->setProperty ("sampleRate", sampleRate);
    meta->setProperty ("automationStepSamples", step);
    meta->setProperty ("frameStartSamples", boundaries);
    meta->setProperty ("editor", juce::var (juce::Array<juce::var> { e->getWidth(), e->getHeight() }));
    meta->setProperty ("faceScale", 2);
    outDir.getChildFile ("capture_meta.json").replaceWithText (juce::JSON::toString (juce::var (meta), false));
    p.releaseResources();
}
