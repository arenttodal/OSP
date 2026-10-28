// ANDOR design-system capture (hidden): renders the current OSP interface in every documented
// state, crops each kind of component and writes every component's bounds, so the design
// bible (andor-design-system/) shows the real implementation and the visual regression has a
// baseline. Nothing here changes the plugin.
//   OSP_DESIGN_CAPTURE=<dir> xvfb-run -a build-plugin/apps/plugin/osp_plugin_tests "[design-capture]"
// Included at the end of PluginProcessorTests.cpp.

#include <cxxabi.h>

namespace designcapture
{
    juce::String className (const juce::Component& c)
    {
        int status = 0;
        const char* mangled = typeid (c).name();
        char* demangled = abi::__cxa_demangle (mangled, nullptr, nullptr, &status);
        juce::String name = status == 0 && demangled != nullptr ? juce::String (demangled) : juce::String (mangled);
        std::free (demangled);
        return name;
    }

    void savePng (const juce::Image& image, const juce::File& file)
    {
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
    }

    /** Every visible component's class, name and bounds in editor coordinates. */
    juce::var bounds (juce::Component& editor)
    {
        juce::Array<juce::var> list;
        std::function<void (juce::Component&, int)> walk = [&] (juce::Component& c, int depth) {
            for (auto* child : c.getChildren())
            {
                if (! child->isVisible())
                    continue;
                const auto r = editor.getLocalArea (child, child->getLocalBounds());
                auto* o = new juce::DynamicObject();
                o->setProperty ("class", className (*child));
                o->setProperty ("name", child->getName());
                o->setProperty ("depth", depth);
                o->setProperty ("x", r.getX());
                o->setProperty ("y", r.getY());
                o->setProperty ("w", r.getWidth());
                o->setProperty ("h", r.getHeight());
                list.add (juce::var (o));
                walk (*child, depth + 1);
            }
        };
        walk (editor, 0);
        return list;
    }

    void saveJson (const juce::var& value, const juce::File& file)
    {
        file.getParentDirectory().createDirectory();
        file.replaceWithText (juce::JSON::toString (value, false));
    }
}

TEST_CASE ("plugin: design-system capture (states, components, bounds)", "[.][design-capture]")
{
    const char* dirEnv = std::getenv ("OSP_DESIGN_CAPTURE");
    if (dirEnv == nullptr)
        return;
    const juce::File out (dirEnv);
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedLook restore (skin::Look::ceramic);
    // The same stand-in sounds as the ceramic screenshots (deterministic, no licensed audio).
    const auto a = writeSource (tmp.dir, "A A4.wav", testsignals::vowel (midiToHz (69), 2.1, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "B A4.wav", testsignals::pluck (440.0, 3.2, 48000.0, 4));
    const auto c = writeSource (tmp.dir, "C E4.wav", testsignals::pluck (329.63, 2.4, 48000.0, 5));

    using designcapture::bounds;
    using designcapture::saveJson;
    using designcapture::savePng;

    auto shoot = [&] (juce::AudioProcessorEditor& e, osp::plugin::OspAudioProcessorEditor& u, const juce::String& name, float scale = 1.0f) {
        u.refreshNow();
        savePng (e.createComponentSnapshot (e.getLocalBounds(), true, scale), out.getChildFile ("states/" + name + ".png"));
        if (scale < 1.01f)   // bounds once, at the reference size
            saveJson (bounds (e), out.getChildFile ("bounds/" + name + ".json"));
    };

    // Empty instrument (the drop zone).
    {
        OspAudioProcessor q;
        std::unique_ptr<juce::AudioProcessorEditor> e (q.createEditorIfNeeded());
        auto* u = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
        REQUIRE (u != nullptr);
        e->setSize (1448, 1086);
        shoot (*e, *u, "00-empty");
    }
    // One and two layers.
    for (const int count : { 1, 2 })
    {
        OspAudioProcessor q;
        std::unique_ptr<juce::AudioProcessorEditor> e (q.createEditorIfNeeded());
        auto* u = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
        REQUIRE (u != nullptr);
        q.addLayers (count == 1 ? juce::Array<juce::File> { a } : juce::Array<juce::File> { a, b });
        REQUIRE (q.waitForLoads (30000));
        q.pollLoads();
        e->setSize (1448, 1086);
        shoot (*e, *u, count == 1 ? "01-one-layer" : "02-two-layers");
    }

    // Three layers: the reference instrument for everything else.
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    p.addLayers ({ a, b, c });
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    editor->setSize (1448, 1086);
    shoot (*editor, *ui, "03-three-layers");
    for (const auto scale : { 1.5f, 2.0f })
        shoot (*editor, *ui, "03-three-layers@" + juce::String (scale, 1) + "x", scale);

    // Window sizes (the instrument scales as one piece): minimum, default, maximum.
    for (const int width : { 869, 1086, 1810 })
    {
        editor->setSize (width, juce::roundToInt (width * 1086.0f / 1448.0f));
        ui->refreshNow();
        savePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), out.getChildFile ("states/04-window-" + juce::String (width) + ".png"));
    }
    editor->setSize (1448, 1086);

    // Modulation tabs: AMP, ENV, LFO 1, LFO 2, VEL, NOTE.
    const char* tabs[] = { "amp", "env", "lfo1", "lfo2", "vel", "note" };
    for (int t = 0; t < 6; ++t)
    {
        ui->modulationPanel().showTab (t);
        shoot (*editor, *ui, "05-mod-tab-" + juce::String (tabs[t]));
    }
    ui->modulationPanel().showTab (0);

    // Macro popovers (LIFE, DRIVE, CHARACTER, MOVEMENT, SPACE, ECHO), Advanced, REIMAGINED A.
    const std::pair<int, const char*> popups[] = { { 0, "life" }, { 1, "drive" }, { 2, "character" }, { 3, "movement" }, { 4, "space" },
                                                   { osp::plugin::OspAudioProcessorEditor::echoPopup, "echo" },
                                                   { osp::plugin::OspAudioProcessorEditor::advancedPopup, "advanced" },
                                                   { osp::plugin::OspAudioProcessorEditor::reimaginedPopup, "reimagined-a" } };
    for (const auto& [index, name] : popups)
    {
        ui->openPopup (index);
        shoot (*editor, *ui, "06-popup-" + juce::String (name));
        if (auto* panel = ui->openPopupPanel())
        {
            const auto r = editor->getLocalArea (panel, panel->getLocalBounds());
            savePng (editor->createComponentSnapshot (r, true, 2.0f), out.getChildFile ("components/popover-" + juce::String (name) + "@2x.png"));
        }
        ui->closePopup();
    }
    // A modulation source's routes popover (LFO 1).
    ui->openModulationPopup (osp::plugin::OspAudioProcessorEditor::routesPopup, 0);
    shoot (*editor, *ui, "06-popup-routes-lfo1");
    ui->closePopup();

    // Card states: Granular with its controls shown, EQ overlay, DAMPEN page.
    p.setParameterValue ("layerA.sourceMode", 1.0f);
    ui->refreshNow();
    ui->engineCard (0).showGranularControls (true, true);
    shoot (*editor, *ui, "07-card-granular");
    ui->engineCard (0).showGranularControls (false, true);
    p.setParameterValue ("layerA.sourceMode", 0.0f);
    ui->refreshNow();
    ui->engineCard (0).setEqOpen (true);
    shoot (*editor, *ui, "07-card-eq");
    if (auto* eq = ui->engineCard (0).eqEditorComponent())
    {
        eq->showDampen (true);
        shoot (*editor, *ui, "07-card-dampen");
    }
    ui->engineCard (0).setEqOpen (false);

    // Keyboard activity (keys held: the accent tint).
    for (int n : { 48, 52, 55, 60, 64 })
        p.keyboardState.noteOn (1, n, 0.8f);
    shoot (*editor, *ui, "08-keys-held");
    p.keyboardState.allNotesOff (1);

    // The arpeggiator's inline editor (the window grows).
    ui->setArpExpanded (true);
    editor->setSize (1448, juce::roundToInt (ui->instrumentHeight()));
    shoot (*editor, *ui, "09-arp-open");
    ui->setArpExpanded (false);
    editor->setSize (1448, 1086);

    // The Library overlay.
    ui->openLibrary();
    shoot (*editor, *ui, "10-library");
    ui->closeLibrary();
    ui->refreshNow();

    // One crop of every kind of component (two instances at most), at 2x, from the reference state.
    std::map<juce::String, int> seen;
    std::function<void (juce::Component&)> crop = [&] (juce::Component& comp) {
        for (auto* child : comp.getChildren())
        {
            if (! child->isVisible() || child->getWidth() < 4 || child->getHeight() < 4)
                continue;
            auto name = designcapture::className (*child).fromLastOccurrenceOf ("::", false, false);
            if (++seen[name] <= 2)
            {
                const auto r = editor->getLocalArea (child, child->getLocalBounds()).expanded (6).getIntersection (editor->getLocalBounds());
                savePng (editor->createComponentSnapshot (r, true, 2.0f), out.getChildFile ("components/" + name + "-" + juce::String (seen[name]) + "@2x.png"));
            }
            crop (*child);
        }
    };
    crop (*editor);
    SUCCEED();
}
