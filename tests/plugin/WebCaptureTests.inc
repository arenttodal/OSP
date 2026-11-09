// Website captures (hidden): the real editor in the states the ANDOR website shows, rendered at
// 1x, 2x and 3x so every product image on the site is a genuine capture of the current build.
// Nothing here changes the plugin.
//   OSP_WEB_CAPTURE=<dir> xvfb-run -a build-plugin/apps/plugin/osp_plugin_tests "[web-capture]"
// Optional OSP_WEB_SOURCES="<a.wav>;<b.wav>;<c.wav>" replaces the deterministic stand-in sources
// with real recordings (the approved demo sources), so the same shots can be re-taken for launch.
// Included after RefinementShotsTests.inc (its helpers: writeSource, TempDir, ScopedLook).

TEST_CASE ("plugin: website captures (OSP_WEB_CAPTURE)", "[.][web-capture]")
{
    const char* dir = std::getenv ("OSP_WEB_CAPTURE");
    if (dir == nullptr)
        return;
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedLook ceramic (skin::Look::ceramic);
    const juce::File out (dir);
    out.createDirectory();
    auto save = [&out] (const juce::Image& image, const juce::String& name) {
        juce::FileOutputStream stream (out.getChildFile (name));
        stream.setPosition (0);
        stream.truncate();
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };

    // Sources: real recordings when given, else the deterministic stand-ins the design captures use.
    std::vector<juce::File> src;
    if (const char* given = std::getenv ("OSP_WEB_SOURCES"))
        for (const auto& path : juce::StringArray::fromTokens (juce::String (given), ";", ""))
            if (juce::File (path).existsAsFile())
                src.push_back (juce::File (path));
    if (src.size() < 3)
    {
        src.clear();
        src.push_back (writeSource (tmp.dir, "A.wav", testsignals::vowel (midiToHz (60), 2.4, 48000.0, 3)));
        src.push_back (writeSource (tmp.dir, "B.wav", testsignals::pluck (220.0, 2.6, 48000.0, 4)));
        src.push_back (writeSource (tmp.dir, "C.wav", testsignals::vowel (midiToHz (48), 2.4, 48000.0, 5)));
    }

    using Editor = osp::plugin::OspAudioProcessorEditor;
    using Host = osp::plugin::InstrumentHost;
    struct Shot
    {
        int layers = 1;
        juce::Point<int> size { 1448, 1086 };
        std::vector<float> scales { 1.0f, 2.0f };
        std::vector<int> held;   // keys held while the shot is taken (played for a moment first)
    };
    auto set = [] (OspAudioProcessor& p, const juce::String& id, float value) { p.setParameterValue (id, value); };
    // The musical starting point every shot shares: the macros where a player might leave them.
    auto voiced = [&] (OspAudioProcessor& p) {
        set (p, "life", 62.0f);
        set (p, "character", 74.0f);
        set (p, "motion", 28.0f);
        set (p, "echo", 18.0f);
        set (p, "space", 46.0f);
    };
    auto shoot = [&] (const Shot& shot, const std::function<void (OspAudioProcessor&, Editor&)>& setUp, const juce::String& name) {
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto* ui = dynamic_cast<Editor*> (e.get());
        REQUIRE (ui != nullptr);
        juce::Array<juce::File> list;
        for (int i = 0; i < shot.layers; ++i)
            list.add (src[static_cast<std::size_t> (i)]);
        p.addLayers (list);
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        e->setSize (shot.size.x, shot.size.y);
        voiced (p);
        setUp (p, *ui);
        if (! shot.held.empty())
        {
            // Play the keys for ~0.6 s so voices, playheads and grains are really running.
            p.prepareToPlay (48000.0, 480);
            juce::AudioBuffer<float> buffer (2, 480);
            for (int block = 0; block < 60; ++block)
            {
                juce::MidiBuffer midi;
                if (block == 0)
                    for (const int n : shot.held)
                        midi.addEvent (juce::MidiMessage::noteOn (1, n, static_cast<juce::uint8> (96)), 0);
                buffer.clear();
                p.processBlock (buffer, midi);
            }
            for (const int n : shot.held)
                p.keyboardState.noteOn (1, n, 0.75f);
        }
        ui->refreshNow();
        ui->refreshNow();
        for (const float scale : shot.scales)
            save (e->createComponentSnapshot (e->getLocalBounds(), true, scale),
                  name + "@" + juce::String (juce::roundToInt (scale)) + "x.png");
        p.releaseResources();
    };
    const std::vector<int> chord { 48, 55, 60, 64, 71 };   // C3 G3 C4 E4 B4: an open Cmaj7

    // 01 The hero: the instrument as a player meets it, at 1, 2 and 3 layers (3x for print-sharp crops).
    shoot ({ 1, { 1448, 1086 }, { 1.0f, 2.0f, 3.0f }, chord }, [&] (OspAudioProcessor& p, Editor&) {
        set (p, "reimagined", 24.0f);
    }, "web-01-hero-1L");
    shoot ({ 2, { 1448, 1086 }, { 1.0f, 2.0f, 3.0f }, chord }, [&] (OspAudioProcessor& p, Editor&) {
        set (p, "reimagined", 24.0f);
        set (p, Host::layerParameterId (1, "sourceMode"), 1.0f);
    }, "web-01-hero-2L");
    shoot ({ 3, { 1448, 1086 }, { 1.0f, 2.0f, 3.0f }, chord }, [&] (OspAudioProcessor& p, Editor&) {
        set (p, "reimagined", 24.0f);
        set (p, Host::layerParameterId (1, "sourceMode"), 1.0f);
        set (p, Host::synthesizeId (2), 45.0f);
        set (p, Host::synthParameterId (2, "mode"), 1.0f);
        set (p, Host::synthParameterId (2, "imprint"), 80.0f);
    }, "web-01-hero-3L");
    // ... and the empty instrument (the first moment: drop a sound).
    shoot ({ 0, { 1448, 1086 }, { 1.0f, 2.0f }, {} }, [] (OspAudioProcessor&, Editor&) {}, "web-00-empty");

    // 02 REIMAGINED: each of the five modes with its popover open, one layer, 60 %.
    const char* modes[] = { "kaleidoscope", "tape-frame", "toybox", "mosaic", "mirage" };
    for (int m = 0; m < 5; ++m)
        shoot ({ 1, { 1448, 1086 }, { 1.0f, 2.0f }, chord }, [&, m] (OspAudioProcessor& p, Editor& ui) {
            set (p, "layerA.reimagined.mode", static_cast<float> (m));
            set (p, "reimagined", 60.0f);
            ui.refreshNow();
            ui.openPopup (Editor::reimaginedPopup);
        }, "web-02-reimagined-" + juce::String (m + 1) + "-" + modes[m]);

    // 03 SYNTH + IMPRINT: sample only, analog, imprint, imprint with motion; the SYNTH page open.
    auto page = [&] (OspAudioProcessor& p, Editor& ui, float amount, float mode, float imprint, float motion) {
        set (p, Host::synthesizeId (0), amount);
        set (p, Host::synthParameterId (0, "mode"), mode);
        set (p, Host::synthParameterId (0, "imprint"), imprint);
        set (p, Host::synthParameterId (0, "motion"), motion);
        ui.refreshNow();
        ui.engineCard (0).setSynthOpen (true);
        if (auto* s = ui.engineCard (0).synthEditorComponent())
            s->refresh();
    };
    shoot ({ 1, { 1448, 1086 }, { 1.0f, 2.0f }, chord }, [] (OspAudioProcessor&, Editor&) {}, "web-03-synth-1-sample");
    shoot ({ 1, { 1448, 1086 }, { 1.0f, 2.0f }, chord }, [&] (OspAudioProcessor& p, Editor& ui) { page (p, ui, 60.0f, 0.0f, 0.0f, 0.0f); }, "web-03-synth-2-analog");
    shoot ({ 1, { 1448, 1086 }, { 1.0f, 2.0f }, chord }, [&] (OspAudioProcessor& p, Editor& ui) { page (p, ui, 60.0f, 1.0f, 85.0f, 0.0f); }, "web-03-synth-3-imprint");
    shoot ({ 1, { 1448, 1086 }, { 1.0f, 2.0f }, chord }, [&] (OspAudioProcessor& p, Editor& ui) { page (p, ui, 60.0f, 1.0f, 85.0f, 60.0f); }, "web-03-synth-4-imprint-motion");

    // 04 Granular: one source as a cloud of grains, its controls shown.
    shoot ({ 1, { 1448, 1086 }, { 1.0f, 2.0f }, chord }, [&] (OspAudioProcessor& p, Editor& ui) {
        set (p, "layerA.sourceMode", 1.0f);
        set (p, "layerA.granular.size", 180.0f);
        set (p, "layerA.granular.density", 22.0f);
        set (p, "layerA.granular.spread", 45.0f);
        ui.refreshNow();
        ui.engineCard (0).showGranularControls (true, false);
    }, "web-04-granular-1L");

    // 05 The macro popovers that carry the SPACE and ECHO story.
    shoot ({ 1, { 1448, 1086 }, { 1.0f, 2.0f }, chord }, [&] (OspAudioProcessor&, Editor& ui) { ui.openPopup (4); }, "web-05-space");
    shoot ({ 1, { 1448, 1086 }, { 1.0f, 2.0f }, chord }, [&] (OspAudioProcessor& p, Editor& ui) {
        set (p, "echo", 40.0f);
        set (p, "echo.type", 2.0f);   // ASCEND
        ui.openPopup (Editor::echoPopup);
    }, "web-05-echo-ascend");
}
