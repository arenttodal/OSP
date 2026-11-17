// Library & Preset Browser polish (docs/research/library-polish.md): the preview's Space
// play / pause / resume, SOURCES A B C (a drop loads into that source), no LOAD TO keys and no
// "Keys play the instrument", STARTING STATES tiles above the lists. Included after
// UserMacroPluginTests.inc.

namespace
{
    /** A Library and Documents of its own, three sounds in it, the editor and the Library open
        on Sounds. */
    struct LibraryRig
    {
        TempDir tmp;
        ScopedEnvironment library { "OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName() };
        ScopedEnvironment documents { "OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName() };
        ScopedEnvironment store { "OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName() };
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        osp::plugin::OspAudioProcessorEditor* ui = nullptr;
        osp::plugin::LibraryPanel* panel = nullptr;
        std::string idDrone, idPluck, idChoir;

        LibraryRig()
        {
            const auto choir = writeSource (tmp.dir, "Choir Ah A3.wav", testsignals::vowel (midiToHz (57), 1.2, 48000.0, 3));
            const auto pluck = writeSource (tmp.dir, "Glass Pluck E4.wav", testsignals::pluck (midiToHz (64), 0.8, 48000.0, 4));
            const auto drone = writeSource (tmp.dir, "Low Drone A2.wav", testsignals::vowel (midiToHz (45), 3.0, 48000.0, 5));
            p.prepareToPlay (48000.0, 256);
            {
                std::string e;
                auto c = osp::library::Catalog::open (osp::library::Catalog::defaultFile(), e);
                REQUIRE (c != nullptr);
                const auto storeDir = std::filesystem::path (plugin::SampleStore::defaultDirectory().getFullPathName().toStdString());
                for (auto [file, id] : { std::pair<const juce::File*, std::string*> { &choir, &idChoir }, { &pluck, &idPluck }, { &drone, &idDrone } })
                {
                    osp::library::ImportRequest r;
                    r.file = file->getFullPathName().toStdString();
                    const auto result = osp::library::importSound (*c, storeDir, r);
                    REQUIRE (result.ok);
                    *id = result.assetId;
                }
            }
            editor.reset (p.createEditorIfNeeded());
            ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
            REQUIRE (ui != nullptr);
            editor->setSize (1448, 1086);
            ui->refreshNow();
            ui->openLibrary();
            panel = ui->libraryWindow();
            REQUIRE (panel != nullptr);
            REQUIRE (panel->settle());
        }
        void sounds()
        {
            panel->setView (osp::plugin::LibraryPanel::View::sounds);
            REQUIRE (panel->settle());
        }
        int rowOf (const std::string& id)
        {
            for (int i = 0; i < panel->rowCount(); ++i)
                if (panel->row (i).id == id)
                    return i;
            return -1;
        }
        /** Blocks of audio (the preview mixes into the processor's output). */
        double render (int blocks)
        {
            juce::AudioBuffer<float> buffer (2, 256);
            double peak = 0.0;
            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer midi;
                buffer.clear();
                p.processBlock (buffer, midi);
                peak = std::max (peak, static_cast<double> (buffer.getMagnitude (0, 256)));
            }
            p.audition().deliver();
            return peak;
        }
        bool space()
        {
            return panel->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
        }
        void spaceUp() { panel->keyStateChanged (false); }
        osp::plugin::Audition::Transport transport() { return p.audition().browserTransport(); }
        /** Visible, and every parent up to the Library visible (headless: nothing is "showing"). */
        bool shown (juce::Component& c)
        {
            for (auto* x = &c; x != nullptr && x != panel; x = x->getParentComponent())
                if (! x->isVisible())
                    return false;
            return true;
        }
        /** Every component under the Library with this button text or title. */
        int countText (const juce::String& text)
        {
            int n = 0;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
                for (auto* child : c.getChildren())
                {
                    if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && shown (*child) && b->getButtonText().containsIgnoreCase (text))
                    {
                        ++n;
                        UNSCOPED_INFO ("button: " << b->getButtonText() << " in " << child->getParentComponent()->getTitle());
                    }
                    walk (*child);
                }
            };
            walk (*panel);
            return n;
        }
        juce::Component* sources()
        {
            juce::Component* found = nullptr;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
                for (auto* child : c.getChildren())
                {
                    if (child->getTitle() == "Sources A, B and C")
                        found = child;
                    walk (*child);
                }
            };
            walk (*panel);
            return found;
        }
        void dropOnSource (int s, const std::string& id)
        {
            auto* tray = sources();
            REQUIRE (tray != nullptr);
            auto* target = dynamic_cast<juce::DragAndDropTarget*> (tray);
            REQUIRE (target != nullptr);
            const int w = (tray->getWidth() - 16 - 260 - 3 * 10) / 3;
            const juce::Point<int> at { 16 + s * (w + 10) + w / 2, 84 };
            const juce::DragAndDropTarget::SourceDetails details (osp::plugin::LibraryPanel::dragDescription (id), panel, at);
            REQUIRE (target->isInterestedInDragSource (details));
            target->itemDragMove (details);
            target->itemDropped (details);
            settle (p);
            REQUIRE (panel->settle());
        }
    };
} // namespace

TEST_CASE ("library polish: the preview engine pauses where it is and resumes there, over a short ramp; the tray and the keys untouched", "[plugin][library][library-polish][preview]")
{
    using Engine = osp::library::PreviewEngine;
    Engine engine;
    engine.prepare (48000.0);
    // A steady full-scale sine (a click would show as a jump between samples).
    AudioData audio;
    audio.sampleRate = 48000.0;
    audio.channels.assign (1, std::vector<float> (48000 * 2));
    for (std::size_t i = 0; i < audio.channels[0].size(); ++i)
        audio.channels[0][i] = 0.5f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * static_cast<double> (i) / 48000.0));
    engine.setSound (Engine::browserSlot, osp::library::makePreviewSound (audio, 57.0));
    engine.setGainDb (0.0f);
    std::vector<float> l (256), r (256), out;
    auto run = [&] (int blocks) {
        for (int b = 0; b < blocks; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            float* ch[2] = { l.data(), r.data() };
            engine.render (ch, 2, 256);
            out.insert (out.end(), l.begin(), l.end());
        }
    };
    engine.play (1u << Engine::browserSlot);
    run (40);   // ~0.21 s
    const double at = engine.playheadSeconds (Engine::browserSlot);
    CHECK (at == Approx (40.0 * 256.0 / 48000.0).margin (0.01));
    engine.pause (1u << Engine::browserSlot);
    run (4);
    CHECK (engine.playheadSeconds (Engine::browserSlot) < 0.0);   // (paused: nothing playing)
    CHECK_FALSE (engine.isSounding());
    engine.playFrom (1u << Engine::browserSlot, at);
    run (1);
    CHECK (engine.playheadSeconds (Engine::browserSlot) == Approx (at + 256.0 / 48000.0).margin (0.005));   // (from where it paused)
    run (10);
    // No click: the largest step between samples stays near the sine's own (0.5 * 2 pi * 220 /
    // 48000 = 0.0144 per sample), across the pause and the resume (a cut would jump up to 0.5).
    float largest = 0.0f;
    for (std::size_t i = 64; i < out.size(); ++i)   // (after the first start's own 0.5 ms attack)
        largest = std::max (largest, std::abs (out[i] - out[i - 1]));
    CHECK (largest < 0.02f);
}

TEST_CASE ("library polish: Space plays, pauses (keeping the position), resumes; the end restarts; one press per press; another sound starts fresh", "[plugin][library][library-polish][preview]")
{
    LibraryRig rig;
    rig.sounds();
    using T = osp::plugin::Audition::Transport;
    // No sound selected: nothing.
    rig.panel->selectRow (-1);
    rig.space();
    rig.spaceUp();
    CHECK (rig.transport() == T::stopped);
    // Select the drone, prepare it.
    rig.panel->selectRow (rig.rowOf (rig.idDrone));
    settle (rig.p);
    REQUIRE (rig.p.audition().slot (osp::library::PreviewEngine::browserSlot).sound != nullptr);
    // Space: plays from the start.
    CHECK (rig.space());
    rig.spaceUp();
    CHECK (rig.transport() == T::playing);
    CHECK (rig.render (60) > 0.01);
    const double playing = rig.p.audition().browserPosition();
    CHECK (playing > 0.2);
    // Space again: pauses, the position kept; silence after the ramp.
    CHECK (rig.space());
    rig.spaceUp();
    CHECK (rig.transport() == T::paused);
    rig.render (4);
    const double kept = rig.p.audition().browserPosition();
    CHECK (kept == Approx (playing).margin (0.02));
    CHECK (rig.render (20) < 1.0e-4);
    CHECK (rig.p.audition().browserPosition() == Approx (kept));   // (still there)
    // Held: auto-repeat never toggles again (one physical press, one transition).
    CHECK (rig.space());
    CHECK (rig.transport() == T::playing);
    for (int repeat = 0; repeat < 10; ++repeat)
        CHECK (rig.space());   // (consumed, no transition)
    CHECK (rig.transport() == T::playing);
    rig.spaceUp();
    rig.render (2);
    CHECK (rig.p.audition().browserPosition() >= kept);   // (resumed there, not from 0)
    CHECK (rig.p.audition().browserPosition() < kept + 0.1);
    // The inspector's key is the same transport.
    CHECK (rig.countText (juce::String::fromUTF8 ("\xe2\x9d\x9a\xe2\x9d\x9a")) == 1);   // (pause shown while playing)
    // Runs to its end: stopped; the next press starts from the beginning.
    rig.render (static_cast<int> (3.2 * 48000.0 / 256.0));
    CHECK (rig.transport() == T::stopped);
    rig.panel->timerCallbackForTest();
    CHECK (rig.countText (juce::String::fromUTF8 ("\xe2\x96\xb6")) >= 1);
    rig.space();
    rig.spaceUp();
    rig.render (2);
    CHECK (rig.transport() == T::playing);
    CHECK (rig.p.audition().browserPosition() < 0.1);
    // Paused, then another sound selected: the old one is forgotten, Space starts the new one.
    rig.space();
    rig.spaceUp();
    REQUIRE (rig.transport() == T::paused);
    rig.panel->selectRow (rig.rowOf (rig.idChoir));
    CHECK (rig.transport() == T::stopped);
    settle (rig.p);
    rig.space();
    rig.spaceUp();
    rig.render (2);
    CHECK (rig.transport() == T::playing);
    CHECK (rig.p.audition().browserKey() == rig.idChoir);
    CHECK (rig.p.audition().browserPosition() < 0.1);
    // Typing in the search field: Space is a space there.
    rig.panel->setSearchText ("glass");
    REQUIRE (rig.panel->settle());
    // (the panel only takes Space when the search field has no focus: covered by its own guard)
    // Another tab: the preview stops; closing the Library: nothing keeps sounding.
    rig.panel->setView (osp::plugin::LibraryPanel::View::presets);
    CHECK (rig.transport() == T::stopped);
    rig.ui->closeLibrary();
    rig.render (8);
    CHECK_FALSE (rig.p.audition().isSounding());
}

TEST_CASE ("library polish: Sounds - no LOAD TO keys, no Keys play the instrument; a drop on A, B or C loads into that source, recoverably", "[plugin][library][library-polish][sources]")
{
    LibraryRig rig;
    rig.sounds();
    rig.panel->selectRow (rig.rowOf (rig.idDrone));
    settle (rig.p);
    CHECK (rig.countText ("Load to") == 0);
    CHECK (rig.countText ("Keys play") == 0);
    CHECK (rig.countText ("Load to Instrument") == 0);
    // A drop on B: the sound is in source B at once (no other key).
    rig.dropOnSource (1, rig.idPluck);
    REQUIRE (rig.p.currentInstrument (1) != nullptr);
    CHECK (juce::String (rig.p.currentInstrument (1)->filename).startsWith ("Glass Pluck"));
    CHECK (rig.p.currentInstrument (0) == nullptr);
    CHECK (rig.p.audition().slot (1).assetId == rig.idPluck);   // (its well shows it)
    // On A and C.
    rig.dropOnSource (0, rig.idDrone);
    rig.dropOnSource (2, rig.idChoir);
    REQUIRE (rig.p.currentInstrument (0) != nullptr);
    REQUIRE (rig.p.currentInstrument (2) != nullptr);
    CHECK (juce::String (rig.p.currentInstrument (0)->filename).startsWith ("Low Drone"));
    CHECK (juce::String (rig.p.currentInstrument (2)->filename).startsWith ("Choir"));
    // An occupied source replaced; Back to previous patch undoes it.
    rig.dropOnSource (0, rig.idPluck);
    CHECK (juce::String (rig.p.currentInstrument (0)->filename).startsWith ("Glass Pluck"));
    REQUIRE (rig.p.canRestorePreviousState());
    rig.p.restorePreviousState();
    settle (rig.p);
    REQUIRE (rig.p.currentInstrument (0) != nullptr);
    CHECK (juce::String (rig.p.currentInstrument (0)->filename).startsWith ("Low Drone"));
    // The keys still play the instrument (no preview keys).
    CHECK_FALSE (rig.p.previewKeys());
}

TEST_CASE ("library polish: STARTING STATES - tiles above PRESETS and TEMPLATES, by identity (a user preset called Natural stays a preset); a tile loads its state", "[plugin][library][library-polish][states]")
{
    LibraryRig rig;
    // A user preset called Natural, and a user starting state.
    OspAudioProcessor::presetFolder().createDirectory();
    OspAudioProcessor::startingStateFolder().createDirectory();
    REQUIRE (rig.p.savePreset (OspAudioProcessor::presetFolder().getChildFile ("Natural.osppreset")));
    REQUIRE (rig.p.saveStartingState (OspAudioProcessor::startingStateFolder().getChildFile ("My State.ospstate")));
    REQUIRE (rig.p.libraryService().waitUntilIdle (10000));
    const auto states = OspAudioProcessor::factoryStartingStates();
    for (const auto v : { osp::plugin::LibraryPanel::View::presets, osp::plugin::LibraryPanel::View::templates })
    {
        rig.panel->setView (v);
        rig.panel->refresh();
        REQUIRE (rig.panel->settle());
        REQUIRE (rig.panel->stateCount() == states.size());
        for (int i = 0; i < rig.panel->stateCount(); ++i)
        {
            CHECK (rig.panel->state (i).name == states[i]);
            CHECK (rig.panel->state (i).program == i);
            REQUIRE (rig.panel->stateTile (i) != nullptr);
            CHECK (rig.panel->stateTile (i)->isShowing() == rig.panel->isShowing());
        }
        // The list holds no built-in state.
        for (int i = 0; i < rig.panel->rowCount(); ++i)
            CHECK_FALSE (rig.panel->row (i).isBuiltIn());
    }
    // Presets: the user's Natural is a list row, a preset, never a tile.
    rig.panel->setView (osp::plugin::LibraryPanel::View::presets);
    REQUIRE (rig.panel->settle());
    const int userNatural = rowNamed (*rig.panel, "Natural");
    REQUIRE (userNatural >= 0);
    CHECK_FALSE (rig.panel->row (userNatural).isBuiltIn());
    // Search narrows both sections, each in its own place.
    rig.panel->setSearchText ("flo");
    REQUIRE (rig.panel->settle());
    REQUIRE (rig.panel->stateCount() == 1);
    CHECK (rig.panel->state (0).name == "Floating");
    rig.panel->setSearchText ({});
    REQUIRE (rig.panel->settle());
    // A scope that is the user's own: no built-in tiles.
    rig.panel->selectScope ("user");
    REQUIRE (rig.panel->settle());
    CHECK (rig.panel->stateCount() == 0);
    rig.panel->selectScope ("all");
    REQUIRE (rig.panel->settle());
    // A tile loads its state (settings; the sounds stay), marked as the one in use; recoverable.
    rig.p.setParameterValue ("character", 20.0f);
    const int dream = states.indexOf ("Dream");
    REQUIRE (dream >= 0);
    rig.panel->chooseState (dream);
    CHECK (rig.p.activeBuiltInState() == dream);
    CHECK (rig.p.presetDisplayName() == "Dream");
    CHECK (rig.p.canRestorePreviousState());
    // Loading the user's Natural (a preset) marks no tile.
    rig.panel->selectRow (userNatural);
    rig.panel->loadSelected();
    REQUIRE (rig.p.waitForLoads (30000));
    rig.p.pollLoads();
    CHECK (rig.p.activeBuiltInState() == -1);
}

TEST_CASE ("library polish: screenshots - Presets with STARTING STATES, Templates, Sounds selected / playing / paused, the shortcut keys (OSP_LIBRARY_SHOTS)", "[.][library-polish-shots]")
{
    const char* dir = std::getenv ("OSP_LIBRARY_SHOTS");
    REQUIRE (dir != nullptr);
    const juce::File out (juce::String::fromUTF8 (dir));
    out.createDirectory();
    LibraryRig rig;
    OspAudioProcessor::presetFolder().createDirectory();
    OspAudioProcessor::startingStateFolder().createDirectory();
    REQUIRE (rig.p.savePreset (OspAudioProcessor::presetFolder().getChildFile ("Warm Hymn.osppreset")));
    REQUIRE (rig.p.saveStartingState (OspAudioProcessor::startingStateFolder().getChildFile ("Floating Settings.ospstate")));
    REQUIRE (rig.p.libraryService().waitUntilIdle (10000));
    auto write = [&] (const juce::String& name) {
        rig.ui->refreshNow();
        const auto image = rig.editor->createComponentSnapshot (rig.editor->getLocalBounds(), true, 1.0f);
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    rig.panel->setView (osp::plugin::LibraryPanel::View::presets);
    rig.panel->refresh();
    REQUIRE (rig.panel->settle());
    rig.panel->chooseState (0);
    write ("01-presets-starting-states");
    rig.panel->setView (osp::plugin::LibraryPanel::View::templates);
    REQUIRE (rig.panel->settle());
    write ("02-templates");
    rig.sounds();
    rig.panel->selectRow (rig.rowOf (rig.idDrone));
    settle (rig.p);
    REQUIRE (rig.panel->settle());
    write ("03-sounds-selected");
    rig.space();
    rig.spaceUp();
    rig.render (90);
    rig.panel->timerCallbackForTest();
    write ("04-sounds-playing");
    rig.space();
    rig.spaceUp();
    rig.render (4);
    rig.panel->timerCallbackForTest();
    write ("05-sounds-paused");
    rig.dropOnSource (0, rig.idPluck);
    rig.panel->selectRow (rig.rowOf (rig.idDrone));
    write ("06-sources-after-drop");
    auto& keys = rig.ui->keyboardActions();
    const auto area = rig.editor->getLocalArea (&keys, keys.getLocalBounds()).expanded (20, 16).getIntersection (rig.editor->getLocalBounds());
    rig.ui->closeLibrary();
    rig.ui->refreshNow();
    const auto image = rig.editor->createComponentSnapshot (area, true, 4.0f);
    const auto file = out.getChildFile ("07-shortcut-keys.png");
    file.deleteFile();
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat().writeImageToStream (image, stream);
}
