// Website captures (hidden): the real editor in the states the ANDOR website shows, rendered at
// 1x, 2x and 3x so every product image on the site is a genuine capture of the current build.
// Nothing here changes the plugin.
//   OSP_WEB_CAPTURE=<dir> xvfb-run -a build-plugin/apps/plugin/osp_plugin_tests "[web-capture]"
// Optional OSP_WEB_SOURCES="<a.wav>;<b.wav>;<c.wav>" replaces the deterministic stand-in sources
// with real recordings (the approved demo sources), so the same shots can be re-taken for launch.
// Included after RefinementShotsTests.inc (its helpers: writeSource, TempDir, ScopedLook).

// Test-only switch defined in shared/andor-ui/Source/Design.cpp (shot 10): knobs without caps.
namespace osp::plugin::design::draw { extern bool captureWithoutCaps; }

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
        juce::Point<int> size { 1448, 0 };
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
    // Optional: what to measure after the final refresh (editor-space bounds), saved as <name>.json
    // so the website can crop a popup or panel exactly.
    std::function<void (Editor&, juce::DynamicObject&)> mark;
    auto rectVar = [] (juce::Rectangle<int> r) {
        auto* o = new juce::DynamicObject();
        o->setProperty ("x", r.getX()); o->setProperty ("y", r.getY());
        o->setProperty ("w", r.getWidth()); o->setProperty ("h", r.getHeight());
        return juce::var (o);
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
        // Height 0: the instrument's own height (OSP's trimmed footer: 1017, not 1086).
        e->setSize (shot.size.x, shot.size.y > 0 ? shot.size.y : juce::roundToInt (ui->instrumentHeight()));
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
        if (mark)
        {
            juce::DynamicObject::Ptr marks (new juce::DynamicObject());
            marks->setProperty ("editor", rectVar (e->getLocalBounds()));
            mark (*ui, *marks);
            out.getChildFile (name + ".json").replaceWithText (juce::JSON::toString (juce::var (marks.get()), false));
            mark = nullptr;
        }
        for (const float scale : shot.scales)
            save (e->createComponentSnapshot (e->getLocalBounds(), true, scale),
                  name + "@" + juce::String (juce::roundToInt (scale)) + "x.png");
        // An open popover on its own (transparent around it), at the shot's largest scale: the site
        // shows the panel isolated, with no editor around it cut off at the crop.
        if (auto* panel = ui->openPopupPanel())
        {
            const float s = shot.scales.back();
            save (panel->createComponentSnapshot (panel->getLocalBounds(), true, s),
                  name + "-panel@" + juce::String (juce::roundToInt (s)) + "x.png");
        }
        p.releaseResources();
    };
    const std::vector<int> chord { 48, 55, 60, 64, 71 };   // C3 G3 C4 E4 B4: an open Cmaj7

    // 01 The hero: the instrument as a player meets it, at 1, 2 and 3 layers (3x for print-sharp crops).
    shoot ({ 1, { 1448, 0 }, { 1.0f, 2.0f, 3.0f }, chord }, [&] (OspAudioProcessor& p, Editor&) {
        set (p, "reimagined", 24.0f);
    }, "web-01-hero-1L");
    shoot ({ 2, { 1448, 0 }, { 1.0f, 2.0f, 3.0f }, chord }, [&] (OspAudioProcessor& p, Editor&) {
        set (p, "reimagined", 24.0f);
        set (p, Host::layerParameterId (1, "sourceMode"), 1.0f);
    }, "web-01-hero-2L");
    shoot ({ 3, { 1448, 0 }, { 1.0f, 2.0f, 3.0f }, chord }, [&] (OspAudioProcessor& p, Editor&) {
        set (p, "reimagined", 24.0f);
        set (p, Host::layerParameterId (1, "sourceMode"), 1.0f);
        set (p, Host::synthesizeId (2), 45.0f);
        set (p, Host::synthParameterId (2, "mode"), 1.0f);
        set (p, Host::synthParameterId (2, "imprint"), 80.0f);
    }, "web-01-hero-3L");
    // ... and the empty instrument (the first moment: drop a sound).
    shoot ({ 0, { 1448, 0 }, { 1.0f, 2.0f }, {} }, [] (OspAudioProcessor&, Editor&) {}, "web-00-empty");

    // 02 REIMAGINED: each of the five modes with its popover open, one layer, 60 %.
    const char* modes[] = { "kaleidoscope", "tape-frame", "toybox", "mosaic", "mirage" };
    for (int m = 0; m < 5; ++m)
        shoot ({ 1, { 1448, 0 }, { 1.0f, 2.0f }, chord }, [&, m] (OspAudioProcessor& p, Editor& ui) {
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
    shoot ({ 1, { 1448, 0 }, { 1.0f, 2.0f }, chord }, [] (OspAudioProcessor&, Editor&) {}, "web-03-synth-1-sample");
    shoot ({ 1, { 1448, 0 }, { 1.0f, 2.0f }, chord }, [&] (OspAudioProcessor& p, Editor& ui) { page (p, ui, 60.0f, 0.0f, 0.0f, 0.0f); }, "web-03-synth-2-analog");
    shoot ({ 1, { 1448, 0 }, { 1.0f, 2.0f }, chord }, [&] (OspAudioProcessor& p, Editor& ui) { page (p, ui, 60.0f, 1.0f, 85.0f, 0.0f); }, "web-03-synth-3-imprint");
    shoot ({ 1, { 1448, 0 }, { 1.0f, 2.0f }, chord }, [&] (OspAudioProcessor& p, Editor& ui) { page (p, ui, 60.0f, 1.0f, 85.0f, 60.0f); }, "web-03-synth-4-imprint-motion");

    // 04 Granular: one source as a cloud of grains, its controls shown.
    shoot ({ 1, { 1448, 0 }, { 1.0f, 2.0f }, chord }, [&] (OspAudioProcessor& p, Editor& ui) {
        set (p, "layerA.sourceMode", 1.0f);
        set (p, "layerA.granular.size", 180.0f);
        set (p, "layerA.granular.density", 22.0f);
        set (p, "layerA.granular.spread", 45.0f);
        ui.refreshNow();
        ui.engineCard (0).showGranularControls (true, false);
    }, "web-04-granular-1L");

    // 05 The macro popovers that carry the SPACE and ECHO story.
    shoot ({ 1, { 1448, 0 }, { 1.0f, 2.0f }, chord }, [&] (OspAudioProcessor&, Editor& ui) { ui.openPopup (4); }, "web-05-space");
    shoot ({ 1, { 1448, 0 }, { 1.0f, 2.0f }, chord }, [&] (OspAudioProcessor& p, Editor& ui) {
        set (p, "echo", 40.0f);
        set (p, "echo.type", 2.0f);   // ASCEND
        ui.openPopup (Editor::echoPopup);
    }, "web-05-echo-ascend");

    // 06 Deep-dive popovers for the website's feature modals, each with its exact bounds.
    auto popupBounds = [&] (Editor& ui, juce::DynamicObject& m) {
        if (auto* panel = ui.openPopupPanel())
            m.setProperty ("popup", rectVar (ui.getLocalArea (panel, panel->getLocalBounds())));
    };
    mark = popupBounds;
    shoot ({ 1, { 1448, 0 }, { 3.0f }, chord }, [&] (OspAudioProcessor&, Editor& ui) { ui.openPopup (0); }, "web-06-life");
    mark = popupBounds;
    shoot ({ 1, { 1448, 0 }, { 3.0f }, chord }, [&] (OspAudioProcessor&, Editor& ui) { ui.openPopup (4); }, "web-06-space");
    // Every macro's popover (DRIVE, FILTER, MOVEMENT, ECHO too), so the site can pair each macro with its panel.
    for (int which : { 1, 2, 3, 5 })
    {
        mark = popupBounds;
        shoot ({ 1, { 1448, 0 }, { 3.0f }, chord }, [&, which] (OspAudioProcessor&, Editor& ui) { ui.openPopup (which); },
               "web-06-macro-" + juce::String (which));
    }
    for (int m = 0; m < 5; ++m)
    {
        mark = popupBounds;
        shoot ({ 1, { 1448, 0 }, { 3.0f }, chord }, [&, m] (OspAudioProcessor& p, Editor& ui) {
            set (p, "layerA.reimagined.mode", static_cast<float> (m));
            set (p, "reimagined", 60.0f);
            ui.refreshNow();
            ui.openPopup (Editor::reimaginedPopup);
        }, "web-06-reimagined-" + juce::String (m + 1) + "-" + modes[m]);
    }

    // 07 The arpeggiator: three layers, the arp on and its panel open (the window grows).
    mark = [&] (Editor& ui, juce::DynamicObject& m) {
        auto& arp = ui.arpInlinePanel();
        m.setProperty ("arp", rectVar (ui.getLocalArea (&arp, arp.getLocalBounds())));
    };
    shoot ({ 3, { 1448, 0 }, { 2.0f }, chord }, [&] (OspAudioProcessor& p, Editor& ui) {
        set (p, Host::layerParameterId (1, "sourceMode"), 1.0f);
        set (p, "arp.enabled", 1.0f);
        set (p, "arp.pattern", 1.0f);
        set (p, "arp.rate", 2.0f);
        set (p, "arp.octaves", 2.0f);
        set (p, "arp.swing", 18.0f);
        ui.setArpExpanded (true);
        ui.setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    }, "web-07-arp-3L");

    // 08 The three-layer hero again, with every component's bounds (the site pins notes to them).
    mark = [&] (Editor& ui, juce::DynamicObject& m) { m.setProperty ("components", designcapture::bounds (ui)); };
    shoot ({ 3, { 1448, 0 }, { 1.0f }, chord }, [&] (OspAudioProcessor& p, Editor&) {
        set (p, "reimagined", 24.0f);
        set (p, Host::layerParameterId (1, "sourceMode"), 1.0f);
        set (p, Host::synthesizeId (2), 45.0f);
        set (p, Host::synthParameterId (2, "mode"), 1.0f);
        set (p, Host::synthParameterId (2, "imprint"), 80.0f);
    }, "web-08-hero-3L-bounds");

    // 09 The remaining sections' states, each with the bounds of the part the site crops.
    auto boundsOf = [&] (Editor& ui, juce::Component* c) { return rectVar (ui.getLocalArea (c, c->getLocalBounds())); };
    // Granular: one source as a cloud of grains with its controls shown.
    mark = [&] (Editor& ui, juce::DynamicObject& m) { m.setProperty ("card", boundsOf (ui, &ui.engineCard (0))); };
    shoot ({ 1, { 1448, 0 }, { 2.0f }, chord }, [&] (OspAudioProcessor& p, Editor& ui) {
        set (p, "layerA.sourceMode", 1.0f);
        set (p, "layerA.granular.size", 180.0f);
        set (p, "layerA.granular.density", 22.0f);
        set (p, "layerA.granular.spread", 45.0f);
        set (p, "layerA.granular.scatter", 3.0f);
        ui.refreshNow();
        ui.engineCard (0).showGranularControls (true, true);
    }, "web-09-granular-1L");
    // ECHO with ASCEND (the popover, the way the design capture opens it).
    mark = popupBounds;
    shoot ({ 1, { 1448, 0 }, { 3.0f }, chord }, [&] (OspAudioProcessor& p, Editor& ui) {
        set (p, "echo", 40.0f);
        set (p, "echo.type", 2.0f);
        ui.refreshNow();
        ui.openPopup (Editor::echoPopup);
    }, "web-09-echo");
    // The modulation panel on LFO 1.
    mark = [&] (Editor& ui, juce::DynamicObject& m) { m.setProperty ("mod", boundsOf (ui, &ui.modulationPanel())); };
    shoot ({ 1, { 1448, 0 }, { 3.0f }, chord }, [&] (OspAudioProcessor&, Editor& ui) { ui.modulationPanel().showTab (2); }, "web-09-modulation");
    // KEY DNA: SIZE with a falling curve and variation, keys held.
    mark = [&] (Editor& ui, juce::DynamicObject& m) { if (auto* d = ui.keyDnaWindow()) m.setProperty ("keydna", boundsOf (ui, d)); };
    shoot ({ 1, { 1448, 0 }, { 2.0f }, {} }, [&] (OspAudioProcessor& p, Editor& ui) {
        p.setParameterValue ("layerA.sourceMode", 1.0f);
        auto dna = p.keyDnaSettings (0);
        dna.enabled = true;
        dna[keydna::Param::size].variation = 0.45;
        dna[keydna::Param::size].curveDepth = 0.7;
        dna[keydna::Param::position].variation = 0.3;
        p.setKeyDnaSettings (0, dna, "KEY DNA", true);
        mod::Curve c;
        c.count = 3;
        c.points[0] = { 0.0f, 0.85f, 0.0f };
        c.points[1] = { 0.55f, 0.45f, 0.3f };
        c.points[2] = { 1.0f, 0.15f, 0.0f };
        p.setKeyDnaCurve (0, keydna::Param::size, c, true);
        ui.refreshNow();
        ui.openKeyDna (0);
        if (auto* d = ui.keyDnaWindow())
            d->showParameter (keydna::Param::size);
        p.keyboardState.noteOn (1, 60, 0.8f);
        p.keyboardState.noteOn (1, 64, 0.8f);
    }, "web-09-keydna");
    // The Library with the three sounds.
    shoot ({ 3, { 1448, 0 }, { 2.0f }, {} }, [&] (OspAudioProcessor&, Editor& ui) { ui.openLibrary(); }, "web-09-library");

    // NOTE MAP (Note Atlas): a recorded phrase mapped into its notes, one selected.
    {
        const auto rec = phraseRecording (tmp.dir, "web-phrase.wav", { 69, 74, 77, 81, 74, 79 });
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto* ui = dynamic_cast<Editor*> (e.get());
        REQUIRE (ui != nullptr);
        juce::Array<juce::File> list;
        list.add (rec.file);
        p.addLayers (list);
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        e->setSize (1448, juce::roundToInt (dynamic_cast<Editor&> (*e).instrumentHeight()));
        voiced (p);
        p.mapNotes (0);
        settleEdits (p);
        p.dismissMapNotesPrompt (1);
        p.dismissMapNotesPrompt (2);
        ui->refreshNow();
        ui->engineCard (0).onNotesToggle (0, true);
        if (auto* map = ui->engineCard (0).noteMapComponent())
        {
            map->refresh();
            map->select ({ p.currentInstrument (0)->atlas->notes[2].id });
        }
        ui->refreshNow();
        juce::DynamicObject::Ptr marks (new juce::DynamicObject());
        marks->setProperty ("card", rectVar (ui->getLocalArea (&ui->engineCard (0), ui->engineCard (0).getLocalBounds())));
        out.getChildFile ("web-09-notemap.json").replaceWithText (juce::JSON::toString (juce::var (marks.get()), false));
        save (e->createComponentSnapshot (e->getLocalBounds(), true, 2.0f), "web-09-notemap@2x.png");
    }

    // Live Capture: the card's REC page with an input connected, a phrase played in, recording.
    {
        OspAudioProcessor p;
        juce::Array<juce::File> list;
        list.add (src[0]);
        p.addLayers (list);
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto* ui = dynamic_cast<Editor*> (e.get());
        REQUIRE (ui != nullptr);
        e->setSize (1448, juce::roundToInt (ui->instrumentHeight()));
        voiced (p);
        ui->refreshNow();
        auto& card = ui->engineCard (0);
        card.setCaptureOpen (true);
        auto* page = card.captureEditorComponent();
        REQUIRE (page != nullptr);
        page->refresh();
        connectInput (p);
        const auto phrase = capturePhrase ({ 69, 74, 77, 81 }, 0.3);
        const int perTick = static_cast<int> (captureRate / 30.0);
        auto feed = [&] (std::int64_t from, std::int64_t to) {
            for (std::int64_t at = from; at + perTick <= to; at += perTick)
            {
                AudioData slice = AudioData::allocate (2, perTick, captureRate);
                for (int ch = 0; ch < 2; ++ch)
                    std::copy_n (phrase.channels[static_cast<std::size_t> (ch)].begin() + at, perTick, slice.channels[static_cast<std::size_t> (ch)].begin());
                feedInput (p, slice, perTick);
                page->refresh();
            }
        };
        feed (0, phrase.numFrames());
        p.startTake (0);
        feed (0, phrase.numFrames() / 2);
        ui->refreshNow();
        juce::DynamicObject::Ptr marks (new juce::DynamicObject());
        marks->setProperty ("card", rectVar (ui->getLocalArea (&card, card.getLocalBounds())));
        out.getChildFile ("web-09-capture.json").replaceWithText (juce::JSON::toString (juce::var (marks.get()), false));
        save (e->createComponentSnapshot (e->getLocalBounds(), true, 2.0f), "web-09-capture@2x.png");
        p.cancelTake();
    }

    // 10 The three-layer hero again for the product renders' physical knobs: every knob's ticks
    // and arcs but no cap (a test-only switch in design::draw::knob), plus each knob's centre,
    // cap radius and pointer angle in editor coordinates, computed as drawRotarySlider does, so a
    // 3D cap can stand exactly where and as the editor draws it, on a clean panel.
    {
        struct NoCaps
        {
            NoCaps() { osp::plugin::design::draw::captureWithoutCaps = true; }
            ~NoCaps() { osp::plugin::design::draw::captureWithoutCaps = false; }
        };
        mark = [&] (Editor& ui, juce::DynamicObject& m) {
            juce::Array<juce::var> knobs;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& parent) {
                for (auto* child : parent.getChildren())
                {
                    // (isShowing() is false off the desktop: visible here means visible up to the editor.)
                    auto visibleInEditor = [&ui] (juce::Component* c) {
                        for (; c != nullptr && c != &ui; c = c->getParentComponent())
                            if (! c->isVisible())
                                return false;
                        return c == &ui;
                    };
                    if (auto* s = dynamic_cast<juce::Slider*> (child); s != nullptr && visibleInEditor (s) && s->isRotary())
                    {
                        const auto& props = s->getProperties();
                        const bool mini = static_cast<bool> (props["mini"]);
                        const bool ticks = ! mini && ! static_cast<bool> (props["noTicks"]);
                        const bool popup = static_cast<bool> (props["popup"]);
                        const auto b = ui.getLocalArea (s, s->getLocalBounds()).toFloat();
                        const float side = std::min (b.getWidth(), b.getHeight());
                        const float r = popup ? 0.5f * side / 1.36f : 0.5f * side / (ticks ? 1.42f : 1.2f);
                        const auto rp = s->getRotaryParameters();
                        const auto pos = static_cast<float> (s->valueToProportionOfLength (s->getValue()));
                        auto* k = new juce::DynamicObject();
                        k->setProperty ("x", b.getCentreX());
                        k->setProperty ("y", b.getCentreY());
                        k->setProperty ("r", r);
                        k->setProperty ("angle", rp.startAngleRadians + pos * (rp.endAngleRadians - rp.startAngleRadians));
                        k->setProperty ("ticks", ticks);
                        k->setProperty ("mini", mini);
                        k->setProperty ("w", b.getWidth());
                        knobs.add (juce::var (k));
                    }
                    walk (*child);
                }
            };
            walk (ui);
            m.setProperty ("knobs", knobs);
        };
        const NoCaps noCaps;
        shoot ({ 3, { 1448, 0 }, { 1.0f, 3.0f, 4.0f }, chord }, [&] (OspAudioProcessor& p, Editor&) {
            set (p, "reimagined", 24.0f);
            set (p, Host::layerParameterId (1, "sourceMode"), 1.0f);
            set (p, Host::synthesizeId (2), 45.0f);
            set (p, Host::synthParameterId (2, "mode"), 1.0f);
            set (p, Host::synthParameterId (2, "imprint"), 80.0f);
        }, "web-10-hero-3L-nocaps");
    }
}
