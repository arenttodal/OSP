// The final precision UI refinement (docs/research/ui-refinement/README.md): every state the
// review compares, rendered by the real editor at the canonical 1448 x 1086 in 1x and 2x, in one,
// two and three layers. Run on demand: OSP_REFINE_DIR=<folder> osp_plugin_tests "[refine-shots]".
// Included after UnifiedPhase5Tests.inc (its helpers: phraseRecording, settleEdits, writeSource).

TEST_CASE ("plugin: UI refinement screenshots (OSP_REFINE_DIR)", "[.][refine-shots]")
{
    const char* dir = std::getenv ("OSP_REFINE_DIR");
    if (dir == nullptr)
        return;
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedLook ceramic (skin::Look::ceramic);
    auto save = [dir] (const juce::Image& image, const juce::String& name) {
        juce::FileOutputStream out (juce::File (dir).getChildFile (name));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
    };
    const auto rec = phraseRecording (tmp.dir, "phrase.wav", { 69, 74, 77, 81, 74, 79 });
    const auto pad = writeSource (tmp.dir, "pad.wav", testsignals::vowel (midiToHz (60), 2.4, 48000.0, 3));
    const auto pluck = writeSource (tmp.dir, "pluck.wav", testsignals::pluck (220.0, 2.4, 48000.0, 4));

    using Editor = osp::plugin::OspAudioProcessorEditor;
    auto shoot = [&] (std::vector<juce::File> files, const std::function<void (OspAudioProcessor&, Editor&)>& setUp, const juce::String& name) {
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto* ui = dynamic_cast<Editor*> (e.get());
        REQUIRE (ui != nullptr);
        juce::Array<juce::File> list;
        for (const auto& f : files)
            list.add (f);
        p.addLayers (list);
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        e->setSize (1448, 1086);
        setUp (p, *ui);
        ui->refreshNow();
        ui->refreshNow();
        for (const float scale : { 1.0f, 2.0f })
            save (e->createComponentSnapshot (e->getLocalBounds(), true, scale), name + (scale > 1.5f ? "@2x.png" : "@1x.png"));
    };
    // The NOTE MAP of layer A, mapped; `select` picks the notes by index (none: nothing selected).
    auto noteMap = [] (OspAudioProcessor& p, Editor& ui, std::vector<int> select) {
        p.mapNotes (0);
        settleEdits (p);
        p.dismissMapNotesPrompt (1);
        p.dismissMapNotesPrompt (2);
        ui.refreshNow();
        ui.engineCard (0).onNotesToggle (0, true);
        auto* map = ui.engineCard (0).noteMapComponent();
        REQUIRE (map != nullptr);
        map->refresh();
        std::vector<std::string> ids;
        for (const int i : select)
            ids.push_back (p.currentInstrument (0)->atlas->notes[static_cast<std::size_t> (i)].id);
        map->select (ids);
        return map;
    };
    auto hover = [] (juce::Component& c, juce::Point<float> at) { c.mouseMove (mouseAt (c, at, at, false)); };

    // 01 NOTE MAP: nothing selected, one note selected, hover on another, two selected; 2 and 3 layers.
    shoot ({ rec.file }, [&] (OspAudioProcessor& p, Editor& ui) { noteMap (p, ui, {}); }, "01-notemap-1L-none");
    shoot ({ rec.file }, [&] (OspAudioProcessor& p, Editor& ui) { noteMap (p, ui, { 2 }); }, "01-notemap-1L-selected");
    shoot ({ rec.file }, [&] (OspAudioProcessor& p, Editor& ui) {
        auto* map = noteMap (p, ui, { 2 });
        hover (*map, map->regionBounds (p.currentInstrument (0)->atlas->notes[4].id).getCentre());
    }, "01-notemap-1L-hover");
    shoot ({ rec.file }, [&] (OspAudioProcessor& p, Editor& ui) { noteMap (p, ui, { 1, 2 }); }, "01-notemap-1L-multi");
    shoot ({ rec.file, pluck }, [&] (OspAudioProcessor& p, Editor& ui) { noteMap (p, ui, { 1 }); }, "01-notemap-2L-selected");
    shoot ({ rec.file, pluck, pad }, [&] (OspAudioProcessor& p, Editor& ui) { noteMap (p, ui, { 1 }); }, "01-notemap-3L-selected");
    shoot ({ rec.file, pluck, pad }, [&] (OspAudioProcessor& p, Editor& ui) { noteMap (p, ui, {}); }, "01-notemap-3L-none");
    // ... a boundary being dragged (the selected note's end, held mid-drag), and a hover with two layers.
    shoot ({ rec.file }, [&] (OspAudioProcessor& p, Editor& ui) {
        auto* map = noteMap (p, ui, { 2 });
        const auto r = map->regionBounds (p.currentInstrument (0)->atlas->notes[2].id);
        const juce::Point<float> edge { r.getRight(), r.getCentreY() };
        map->mouseMove (mouseAt (*map, edge, edge, false));
        map->mouseDown (mouseAt (*map, edge, edge, false));
        map->mouseDrag (mouseAt (*map, edge.translated (-18.0f, 0.0f), edge, true));
    }, "01-notemap-1L-dragging");
    shoot ({ rec.file, pluck }, [&] (OspAudioProcessor& p, Editor& ui) {
        auto* map = noteMap (p, ui, { 1 });
        hover (*map, map->regionBounds (p.currentInstrument (0)->atlas->notes[3].id).getCentre());
    }, "01-notemap-2L-hover");

    // 02 Source header: an ordinary layer and a mapped one (NOTES), EQ open; 1, 2, 3 layers.
    shoot ({ rec.file }, [&] (OspAudioProcessor& p, Editor& ui) {
        p.mapNotes (0);
        settleEdits (p);
        ui.refreshNow();
    }, "02-header-1L-mapped");
    shoot ({ rec.file, pluck }, [&] (OspAudioProcessor& p, Editor& ui) {
        p.mapNotes (0);
        settleEdits (p);
        p.dismissMapNotesPrompt (1);
        ui.refreshNow();
        ui.engineCard (1).onEqToggle (1, true);
    }, "02-header-2L");
    shoot ({ rec.file, pluck, pad }, [&] (OspAudioProcessor& p, Editor& ui) {
        p.mapNotes (0);
        settleEdits (p);
        p.dismissMapNotesPrompt (1);
        p.dismissMapNotesPrompt (2);
        ui.refreshNow();
    }, "02-header-3L");

    // 03 Preset bar, volume, menu: the plain instrument and a favourite preset.
    shoot ({ pad }, [] (OspAudioProcessor&, Editor&) {}, "03-topbar-1L");
    {
        // The header's icon family at its displayed size, 1x and 2x: heart, filled heart,
        // cassette, chevrons, on the card colour.
        for (const float scale : { 1.0f, 2.0f })
        {
            juce::Image sheet (juce::Image::RGB, juce::roundToInt (260.0f * scale), juce::roundToInt (40.0f * scale), true, juce::SoftwareImageType());
            juce::Graphics g (sheet);
            g.addTransform (juce::AffineTransform::scale (scale));
            g.fillAll (osp::plugin::design::colour::cardTop);
            using osp::plugin::icons::Kind;
            const auto ink = osp::plugin::design::colour::text.withAlpha (0.6f);
            osp::plugin::icons::draw (g, Kind::chevronLeft, { 8.0f, 8.0f, 24.0f, 24.0f }, ink, 1.5f);
            osp::plugin::icons::draw (g, Kind::heart, { 48.0f, 8.0f, 24.0f, 24.0f }, ink, 1.5f);
            osp::plugin::icons::draw (g, Kind::heartFilled, { 88.0f, 8.0f, 24.0f, 24.0f }, osp::plugin::design::colour::accent, 1.5f);
            osp::plugin::icons::draw (g, Kind::chevronRight, { 128.0f, 8.0f, 24.0f, 24.0f }, ink, 1.5f);
            osp::plugin::icons::draw (g, Kind::cassette, { 166.0f, 6.5f, 27.0f, 27.0f }, ink, 1.5f);
            g.setColour (osp::plugin::design::colour::text.withAlpha (0.72f));
            for (int i = -1; i <= 1; ++i)
                g.fillEllipse (juce::Rectangle<float> (4.6f, 4.6f).withCentre ({ 230.0f, 20.0f + 9.5f * static_cast<float> (i) }));
            save (sheet, juce::String ("03-icons") + (scale > 1.5f ? "@2x.png" : "@1x.png"));
        }
    }

    // 04 EQ (bands in use) and DAMPEN (on, a shelf).
    auto eqBands = [] (OspAudioProcessor& p) {
        auto id = [] (const char* n) { return osp::plugin::InstrumentHost::eqParameterId (0, n); };
        p.setParameterValue (id ("enabled"), 1.0f);
        p.setParameterValue (id ("lowShelf.enabled"), 1.0f);
        p.setParameterValue (id ("lowShelf.gain"), 4.0f);
        p.setParameterValue (id ("bell.enabled"), 1.0f);
        p.setParameterValue (id ("bell.frequency"), 1800.0f);
        p.setParameterValue (id ("bell.gain"), -5.0f);
        p.setParameterValue (id ("highShelf.enabled"), 1.0f);
        p.setParameterValue (id ("highShelf.gain"), 2.5f);
    };
    shoot ({ pad }, [&] (OspAudioProcessor& p, Editor& ui) {
        eqBands (p);
        ui.refreshNow();
        ui.engineCard (0).onEqToggle (0, true);
        if (auto* eq = ui.engineCard (0).eqEditorComponent())
            eq->selectBand (2);
    }, "04-eq-1L");
    shoot ({ pad }, [&] (OspAudioProcessor& p, Editor& ui) {
        p.setParameterValue (osp::plugin::InstrumentHost::dampenParameterId (0, "enabled"), 1.0f);
        ui.refreshNow();
        ui.engineCard (0).onEqToggle (0, true);
        if (auto* eq = ui.engineCard (0).eqEditorComponent())
            eq->showDampen (true);
    }, "04-dampen-1L");
    shoot ({ pad, pluck }, [&] (OspAudioProcessor& p, Editor& ui) {
        eqBands (p);
        ui.refreshNow();
        ui.engineCard (0).onEqToggle (0, true);
    }, "04-eq-2L");
    // Three engines: the narrower cards stack the EQ's band values, and DAMPEN's mode, AMOUNT
    // and value, under the tabs instead of over them.
    shoot ({ pad, pluck, rec.file }, [&] (OspAudioProcessor& p, Editor& ui) {
        ui.setSize (1086, 814);   // the default window: the narrowest cards
        eqBands (p);
        ui.refreshNow();
        ui.engineCard (0).onEqToggle (0, true);
        if (auto* eq = ui.engineCard (0).eqEditorComponent())
            eq->selectBand (2);
    }, "04-eq-3L");
    shoot ({ pad, pluck, rec.file }, [&] (OspAudioProcessor& p, Editor& ui) {
        ui.setSize (1086, 814);
        p.setParameterValue (osp::plugin::InstrumentHost::dampenParameterId (0, "enabled"), 1.0f);
        ui.refreshNow();
        ui.engineCard (0).onEqToggle (0, true);
        if (auto* eq = ui.engineCard (0).eqEditorComponent())
            eq->showDampen (true);
    }, "04-dampen-3L");
    shoot ({ pad, pluck, rec.file }, [&] (OspAudioProcessor&, Editor& ui) {
        ui.setSize (1086, 814);
        ui.refreshNow();
        ui.engineCard (0).onEqToggle (0, true);
        if (auto* eq = ui.engineCard (0).eqEditorComponent())
            eq->showDampen (true);
    }, "04-dampen-off-3L");
    // The EQ off (its hint) and the pointer over a band (its readout).
    shoot ({ pad }, [&] (OspAudioProcessor&, Editor& ui) {
        ui.engineCard (0).onEqToggle (0, true);
    }, "04-eq-off-1L");
    shoot ({ pad }, [&] (OspAudioProcessor& p, Editor& ui) {
        eqBands (p);
        ui.refreshNow();
        ui.engineCard (0).onEqToggle (0, true);
        if (auto* eq = ui.engineCard (0).eqEditorComponent())
        {
            eq->refresh();
            hover (*eq, eq->nodePosition (2));
        }
    }, "04-eq-hover-1L");

    // 05 KEY DNA (the additional view): SIZE with a falling curve and variation, keys held.
    shoot ({ pad }, [] (OspAudioProcessor& p, Editor& ui) {
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
    }, "05-keydna-1L");
    // ... and keys drawn by hand: a stroke from C4 up to G4 above the curve, a low B3 (each
    // drawn key with its ring).
    shoot ({ pad }, [] (OspAudioProcessor& p, Editor& ui) {
        p.setParameterValue ("layerA.sourceMode", 1.0f);
        auto dna = p.keyDnaSettings (0);
        dna.enabled = true;
        dna[keydna::Param::size].variation = 0.45;
        dna[keydna::Param::size].curveDepth = 0.7;
        for (int k = 60; k <= 67; ++k)
            dna[keydna::Param::size].drawn[static_cast<std::size_t> (k)] = 0.3f + 0.08f * static_cast<float> (k - 60);
        dna[keydna::Param::size].drawn[59] = -0.8f;
        p.setKeyDnaSettings (0, dna, "KEY DNA", true);
        ui.refreshNow();
        ui.openKeyDna (0);
        if (auto* d = ui.keyDnaWindow())
            d->showParameter (keydna::Param::size);
    }, "05-keydna-drawn-1L");

    // 06 SYNTHESIZE (Phase S1): each card's sixth knob, in one, two and three layers.
    auto synth = [] (OspAudioProcessor& p, std::initializer_list<float> amounts) {
        int l = 0;
        for (const float a : amounts)
            p.setParameterValue (osp::plugin::InstrumentHost::synthesizeId (l++), a);
    };
    shoot ({ pad }, [&] (OspAudioProcessor& p, Editor&) { synth (p, { 35.0f }); }, "06-synth-1L");
    shoot ({ pad, pluck }, [&] (OspAudioProcessor& p, Editor&) { synth (p, { 35.0f, 0.0f }); }, "06-synth-2L");
    shoot ({ pad, pluck, rec.file }, [&] (OspAudioProcessor& p, Editor&) { synth (p, { 35.0f, 0.0f, 80.0f }); }, "06-synth-3L");
    // ... the same in Granular (each source's own, beside its granular controls).
    shoot ({ pad, pluck, rec.file }, [&] (OspAudioProcessor& p, Editor&) {
        for (int l = 0; l < 3; ++l)
            p.setParameterValue (osp::plugin::InstrumentHost::layerParameterId (l, "sourceMode"), 1.0f);
        synth (p, { 20.0f, 55.0f, 100.0f });
    }, "06-synth-granular-3L");

    // 07 The SYNTH page (Phase S2), opened from the chip: the default saw, then a detuned
    // square an octave down, dark; three cards (two rows of controls); off (0 %).
    auto page = [] (OspAudioProcessor& p, Editor& ui, std::initializer_list<std::pair<const char*, float>> values) {
        for (const auto& [name, v] : values)
            p.setParameterValue (osp::plugin::InstrumentHost::synthParameterId (0, name), v);
        ui.refreshNow();
        ui.engineCard (0).setSynthOpen (true);
        if (auto* s = ui.engineCard (0).synthEditorComponent())
            s->refresh();
    };
    shoot ({ pad }, [&] (OspAudioProcessor& p, Editor& ui) { synth (p, { 45.0f }); page (p, ui, {}); }, "07-synth-page-1L");
    shoot ({ pad, pluck }, [&] (OspAudioProcessor& p, Editor& ui) {
        synth (p, { 60.0f, 0.0f });
        page (p, ui, { { "wave", 1.0f }, { "octave", -1.0f }, { "detune", 14.0f }, { "tone", 30.0f }, { "shape", 1.0f }, { "pitch", 1.0f } });
    }, "07-synth-page-2L");
    shoot ({ pad, pluck, rec.file }, [&] (OspAudioProcessor& p, Editor& ui) {
        ui.setSize (1086, 814);
        synth (p, { 35.0f, 0.0f, 0.0f });
        page (p, ui, { { "wave", 3.0f } });
    }, "07-synth-page-3L");
    shoot ({ pad }, [&] (OspAudioProcessor& p, Editor& ui) { page (p, ui, { { "wave", 2.0f } }); }, "07-synth-page-off-1L");

    // 08 SYNTHESIS S2, the spectral imprint: IMPRINT and MOTION beside the controls (one card),
    // on the first row (two), on a row of their own (three; the small window gives them the
    // picture's room); the chip with the imprint's curve.
    shoot ({ pad }, [&] (OspAudioProcessor& p, Editor& ui) {
        synth (p, { 60.0f });
        page (p, ui, { { "mode", 1.0f }, { "imprint", 80.0f }, { "motion", 40.0f } });
    }, "08-imprint-page-1L");
    shoot ({ pad, pluck }, [&] (OspAudioProcessor& p, Editor& ui) {
        synth (p, { 60.0f, 0.0f });
        page (p, ui, { { "mode", 1.0f }, { "imprint", 100.0f }, { "motion", 0.0f } });
    }, "08-imprint-page-2L");
    shoot ({ pad, pluck, rec.file }, [&] (OspAudioProcessor& p, Editor& ui) {
        synth (p, { 60.0f, 0.0f, 0.0f });
        page (p, ui, { { "mode", 1.0f }, { "imprint", 70.0f }, { "motion", 50.0f } });
    }, "08-imprint-page-3L");
    shoot ({ pad, pluck, rec.file }, [&] (OspAudioProcessor& p, Editor& ui) {
        ui.setSize (1086, 814);
        synth (p, { 60.0f, 0.0f, 0.0f });
        page (p, ui, { { "mode", 1.0f }, { "imprint", 70.0f }, { "motion", 50.0f } });
    }, "08-imprint-page-3L-small");
    shoot ({ pad, pluck, rec.file }, [&] (OspAudioProcessor& p, Editor& ui) {
        synth (p, { 45.0f, 70.0f, 0.0f });
        for (int l = 0; l < 2; ++l)
        {
            p.setParameterValue (osp::plugin::InstrumentHost::synthParameterId (l, "mode"), 1.0f);
            p.setParameterValue (osp::plugin::InstrumentHost::synthParameterId (l, "imprint"), l == 0 ? 80.0f : 0.0f);
        }
        ui.refreshNow();
    }, "08-imprint-chips-3L");

    // 09 The SYNTH chip at 0 %, 42 % and 100 % (Imprint on at 100 %), in one, two and three cards.
    auto chips = [] (OspAudioProcessor& p, Editor& ui, std::initializer_list<float> amounts, int imprintLayer) {
        int l = 0;
        for (const float a : amounts)
            p.setParameterValue (osp::plugin::InstrumentHost::synthesizeId (l++), a);
        if (imprintLayer >= 0)
        {
            p.setParameterValue (osp::plugin::InstrumentHost::synthParameterId (imprintLayer, "mode"), 1.0f);
            p.setParameterValue (osp::plugin::InstrumentHost::synthParameterId (imprintLayer, "imprint"), 80.0f);
        }
        ui.refreshNow();
    };
    shoot ({ pad }, [&] (OspAudioProcessor& p, Editor& ui) { chips (p, ui, { 42.0f }, -1); }, "09-synth-chip-1L-42");
    shoot ({ pad, pluck }, [&] (OspAudioProcessor& p, Editor& ui) { chips (p, ui, { 0.0f, 100.0f }, 1); }, "09-synth-chip-2L-0-100imprint");
    shoot ({ pad, pluck, rec.file }, [&] (OspAudioProcessor& p, Editor& ui) { chips (p, ui, { 0.0f, 42.0f, 100.0f }, 2); }, "09-synth-chip-3L");
    shoot ({ pad, pluck, rec.file }, [&] (OspAudioProcessor& p, Editor& ui) {
        ui.setSize (1086, 814);
        chips (p, ui, { 0.0f, 42.0f, 100.0f }, 2);
    }, "09-synth-chip-3L-small");
}

TEST_CASE ("plugin: playback-mode selector screenshots (OSP_MODE_SHOTS)", "[.][mode-shots]")
{
    // On demand: OSP_MODE_SHOTS=<folder> osp_plugin_tests "[mode-shots]". The source headers in
    // one, two and three layers (A One Shot, B Granular, C One Shot), 1x and 2x, both window
    // sizes for three; and, where the selector offers it, its menu open.
    const char* dir = std::getenv ("OSP_MODE_SHOTS");
    if (dir == nullptr)
        return;
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedLook ceramic (skin::Look::ceramic);
    auto save = [dir] (const juce::Image& image, const juce::String& name) {
        juce::FileOutputStream out (juce::File (dir).getChildFile (name));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
    };
    const auto pad = writeSource (tmp.dir, "pad.wav", testsignals::vowel (midiToHz (60), 2.4, 48000.0, 3));
    const auto pluck = writeSource (tmp.dir, "pluck.wav", testsignals::pluck (220.0, 2.4, 48000.0, 4));
    const auto low = writeSource (tmp.dir, "low.wav", testsignals::vowel (midiToHz (48), 2.4, 48000.0, 5));
    using Editor = osp::plugin::OspAudioProcessorEditor;
    auto shoot = [&] (std::vector<juce::File> files, juce::Point<int> size, bool openMenu, const juce::String& name) {
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto* ui = dynamic_cast<Editor*> (e.get());
        REQUIRE (ui != nullptr);
        juce::Array<juce::File> list;
        for (const auto& f : files)
            list.add (f);
        p.addLayers (list);
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        if (files.size() > 1)
            p.setParameterValue (osp::plugin::InstrumentHost::layerParameterId (1, "sourceMode"), 1.0f);
        e->setSize (size.x, size.y);
        ui->refreshNow();
        ui->refreshNow();
        // Open: the menu below B's selector, as its window places it (drawn into the editor:
        // no menu window can open headless).
        std::unique_ptr<juce::Component> menu;
        if (auto* selector = ui->engineCard (1).modeSelector(); openMenu && selector != nullptr)
        {
            menu = selector->createMenuPicture();
            const auto below = e->getLocalArea (selector, selector->getLocalBounds()).getBottomLeft();
            menu->setTopLeftPosition (std::min (below.x, e->getWidth() - menu->getWidth() - 8), below.y - 3);
            e->addAndMakeVisible (*menu);
        }
        for (const float scale : { 1.0f, 2.0f })
            save (e->createComponentSnapshot (e->getLocalBounds(), true, scale), name + (scale > 1.5f ? "@2x.png" : "@1x.png"));
        if (menu != nullptr)
            e->removeChildComponent (menu.get());
    };
    shoot ({ pad }, { 1448, 1086 }, false, "mode-1L");
    shoot ({ pad, pluck }, { 1448, 1086 }, false, "mode-2L");
    shoot ({ pad, pluck, low }, { 1448, 1086 }, false, "mode-3L");
    shoot ({ pad, pluck, low }, { 1086, 814 }, false, "mode-3L-small");
    shoot ({ pad, pluck }, { 1448, 1086 }, true, "mode-2L-menu");
    shoot ({ pad, pluck, low }, { 1086, 814 }, true, "mode-3L-small-menu");
}
