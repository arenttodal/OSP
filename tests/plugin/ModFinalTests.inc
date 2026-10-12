// The final modulation system in the plugin: the CC 1-4 sources (controller numbers, Learn,
// saved with the session), PRESSURE, VEL and NOTE (their curves saved as c2, older sessions'
// DYNAMICS curve applied to AMP), the six tabs and the migration of the panel's tab.
// Included at the end of PluginProcessorTests.cpp.

#include "ModulationPanel.h"

namespace
{
    void mfRun (OspAudioProcessor& p, const juce::MidiBuffer& midi, int samples = 256)
    {
        juce::AudioBuffer<float> buffer (2, samples);
        buffer.clear();
        auto copy = midi;
        p.processBlock (buffer, copy);
    }

    juce::MidiBuffer mfOne (const juce::MidiMessage& m)
    {
        juce::MidiBuffer b;
        b.addEvent (m, 0);
        return b;
    }

    juce::MemoryBlock mfState (OspAudioProcessor& p)
    {
        juce::MemoryBlock data;
        p.getStateInformation (data);
        return data;
    }

    /** The session as XML (to edit it as an older one would be), and back. */
    std::unique_ptr<juce::XmlElement> mfXml (OspAudioProcessor& p)
    {
        const auto data = mfState (p);
        return juce::AudioProcessor::getXmlFromBinary (data.getData(), static_cast<int> (data.getSize()));
    }

    void mfApply (OspAudioProcessor& p, const juce::XmlElement& xml)
    {
        juce::MemoryBlock data;
        juce::AudioProcessor::copyXmlToBinary (xml, data);
        p.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    }
}

TEST_CASE ("plugin: CC 1-4 - controller numbers, Learn, heard as sources, saved with the session", "[plugin][mod][mod-final]")
{
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    OspAudioProcessor p;
    p.prepareToPlay (48000.0, 256);
    CHECK (p.modControllerNumber (0) == 2);
    CHECK (p.modControllerNumber (1) == 11);
    CHECK (p.modControllerNumber (2) == 16);
    CHECK (p.modControllerNumber (3) == 17);
    CHECK (p.learningModController() == -1);

    // CC 11 moves CC 2's source (expression); after the glide (a 10 ms time constant) it is there.
    mfRun (p, mfOne (juce::MidiMessage::controllerEvent (1, 11, 127)));
    for (int i = 0; i < 40; ++i)
        mfRun (p, {});
    CHECK (p.modulationView().value[mod::slot::cc1 + 1] == Approx (1.0f).margin (1.0e-3));
    CHECK (p.modulationView().value[mod::slot::cc1] == 0.0f);

    // Learn: the next controller that moves is CC 3's; the slot follows it from then on.
    p.learnModController (2);
    CHECK (p.learningModController() == 2);
    mfRun (p, mfOne (juce::MidiMessage::controllerEvent (1, 21, 64)));
    CHECK (p.learningModController() == -1);
    CHECK (p.modControllerNumber (2) == 21);
    for (int i = 0; i < 40; ++i)
        mfRun (p, {});
    CHECK (p.modulationView().value[mod::slot::cc1 + 2] == Approx (64.0f / 127.0f).margin (1.0e-3));

    // PRESSURE: channel pressure (and polyphonic aftertouch, collapsed).
    mfRun (p, mfOne (juce::MidiMessage::channelPressureChange (1, 100)));
    for (int i = 0; i < 40; ++i)
        mfRun (p, {});
    CHECK (p.modulationView().value[mod::slot::pressure] == Approx (100.0f / 127.0f).margin (1.0e-3));

    // Saved with the session, restored; an older session gets the defaults.
    p.setModControllerNumber (0, 7);
    const auto saved = mfState (p);
    OspAudioProcessor q;
    q.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK (q.modControllerNumber (0) == 7);
    CHECK (q.modControllerNumber (2) == 21);
    auto xml = mfXml (p);
    xml->removeAttribute ("modControllers");
    OspAudioProcessor old;
    mfApply (old, *xml);
    CHECK (old.modControllerNumber (0) == 2);
    CHECK (old.modControllerNumber (2) == 16);
    // A route from the new sources is a plain route (saved by its source's place).
    CHECK (p.addModulationRoute (mod::Source::cc2, mod::Dest::space, 50.0f) >= 0);
    CHECK (p.addModulationRoute (mod::Source::pressure, mod::Dest::levelA, -30.0f) >= 0);
    CHECK (p.addModulationRoute (mod::Source::vel, mod::Dest::cutoff, 40.0f) >= 0);
    const auto withRoutes = mfState (p);
    OspAudioProcessor r;
    r.setStateInformation (withRoutes.getData(), static_cast<int> (withRoutes.getSize()));
    int found = 0;
    for (const auto& route : r.modulationRoutes())
        found += route.route.source == mod::Source::cc2 || route.route.source == mod::Source::pressure || route.route.source == mod::Source::vel ? 1 : 0;
    CHECK (found == 3);
}

TEST_CASE ("plugin: VEL and NOTE curves - saved with the curves; older sessions keep their DYNAMICS response through APPLY TO AMP", "[plugin][mod][mod-final][dynamics]")
{
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    // c1 (four curves) still reads: VEL and NOTE straight.
    const auto c1 = OspAudioProcessor::decodeModulationCurves ("c1:0,0,0;1,1,0|0,0,0;1,1,0|0,1,0;1,0,0|0,1,0;1,0,0");
    REQUIRE (c1.has_value());
    CHECK ((*c1)[4] == mod::straightCurve());
    CHECK ((*c1)[5] == mod::straightCurve());
    const auto encoded = OspAudioProcessor::encodeModulationCurves (*c1);
    CHECK (encoded.startsWith ("c3:"));
    const auto again = OspAudioProcessor::decodeModulationCurves (encoded);
    REQUIRE (again.has_value());
    CHECK (OspAudioProcessor::encodeModulationCurves (*again) == encoded);   // stable (4 decimals)
    for (int c = 0; c < 6; ++c)
        CHECK ((*again)[static_cast<std::size_t> (c)] == (*c1)[static_cast<std::size_t> (c)]);
    CHECK_FALSE (OspAudioProcessor::decodeModulationCurves ("c2:0,0,0;1,1,0").has_value());   // not six

    OspAudioProcessor p;
    p.prepareToPlay (48000.0, 256);
    CHECK (p.velocityAppliesToAmp());
    CHECK (mod::matchingPreset (p.modulationCurve (4)) == static_cast<int> (mod::CurvePreset::linear));
    // DYNAMICS' curve (kept for compatibility) sets the VEL curve when it moves.
    p.setParameterValue ("dynamics.curve", 2.0f);   // HARD
    p.pollLoads();
    CHECK (mod::matchingPreset (p.modulationCurve (4)) == static_cast<int> (mod::CurvePreset::hard));

    // A session of its own: the edited VEL curve, APPLY TO AMP off, the NOTE curve.
    auto custom = mod::presetCurve (mod::CurvePreset::exponential);
    p.setModulationCurve (4, custom, false);
    p.setModulationCurve (5, mod::presetCurve (mod::CurvePreset::midEmphasis), false);
    p.setVelocityAppliesToAmp (false);
    const auto saved = mfState (p);
    OspAudioProcessor q;
    q.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    q.pollLoads();   // the timer must not overwrite the restored curve with DYNAMICS'
    CHECK (mod::matchingPreset (q.modulationCurve (4)) == static_cast<int> (mod::CurvePreset::exponential));   // saved to 4 decimals
    CHECK (mod::matchingPreset (q.modulationCurve (5)) == static_cast<int> (mod::CurvePreset::midEmphasis));
    CHECK_FALSE (q.velocityAppliesToAmp());

    // An older session (no c2, no velToAmp) with DYNAMICS SOFT: VEL is SOFT, applied to AMP.
    auto xml = mfXml (p);
    xml->setAttribute ("modCurves", "c1:0,0,0;1,1,0|0,0,0;1,1,0|0,1,0;1,0,0|0,1,0;1,0,0");
    xml->removeAttribute ("velToAmp");
    for (auto* param : xml->getChildIterator())
        if (param->getStringAttribute ("id") == "dynamics.curve")
            param->setAttribute ("value", 0.0);
    OspAudioProcessor old;
    mfApply (old, *xml);
    old.pollLoads();
    CHECK (mod::matchingPreset (old.modulationCurve (4)) == static_cast<int> (mod::CurvePreset::soft));
    CHECK (old.velocityAppliesToAmp());
}

TEST_CASE ("plugin: the modulation panel's six tabs - AMP ENV LFO 1 LFO 2 VEL NOTE; an older tab migrates", "[plugin][mod][mod-final]")
{
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    using namespace osp::plugin;
    CHECK (modui::tabCount == 6);
    CHECK (modui::sourceOfTab (0) == -1);
    CHECK (modui::sourceOfTab (1) == mod::slot::env1);
    CHECK (modui::sourceOfTab (2) == mod::slot::lfo1);
    CHECK (modui::sourceOfTab (3) == mod::slot::lfo2);
    CHECK (modui::sourceOfTab (4) == mod::slot::vel);
    CHECK (modui::sourceOfTab (5) == mod::slot::note);
    CHECK (modui::tabOfSource (mod::slot::env2) == 0);   // legacy: no tab
    CHECK (modui::tabOfSource (mod::slot::cc1) == 0);
    CHECK (modui::tabOfSource (mod::slot::note) == 5);
    CHECK (modui::migratedTab (2) == 1);   // ENV 2 -> ENV
    CHECK (modui::migratedTab (4) == 3);   // LFO 2

    OspAudioProcessor p;
    // A session saved before the six tabs, on LFO 2: it opens LFO 2.
    auto xml = mfXml (p);
    xml->setAttribute ("modTab", 4);
    xml->removeAttribute ("modTabs");
    mfApply (p, *xml);
    CHECK (p.modPanelTab() == 3);
    p.setModPanelTab (5);
    const auto saved = mfState (p);
    OspAudioProcessor q;
    q.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK (q.modPanelTab() == 5);

    // ENV 2 is offered in menus only while a route uses it.
    auto has = [] (const std::vector<int>& v, int s) { return std::find (v.begin(), v.end(), s) != v.end(); };
    CHECK_FALSE (has (modui::menuSources (p, false), mod::slot::env2));
    CHECK (has (modui::menuSources (p, false), mod::slot::vel));
    CHECK (has (modui::menuSources (p, true), mod::slot::pressure));
    REQUIRE (p.addModulationRoute (mod::Source::env2, mod::Dest::drive, 30.0f) >= 0);
    CHECK (has (modui::menuSources (p, false), mod::slot::env2));
    CHECK (juce::String (modui::sourceLabel (mod::slot::env2)).contains ("legacy"));

    // The panel shows VEL's curve and its row; NOTE's.
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    editor->setSize (1448, 1086);
    ModulationPanel* panel = nullptr;
    std::function<void (juce::Component&)> find = [&] (juce::Component& c) {
        if (auto* m = dynamic_cast<ModulationPanel*> (&c))
            panel = m;
        for (auto* child : c.getChildren())
            find (*child);
    };
    find (*editor);
    REQUIRE (panel != nullptr);
    panel->showTab (4);
    CHECK (panel->selectedSource() == mod::slot::vel);
    CHECK (panel->curveView().isVisible());
    CHECK (panel->curveView().isEditable());
    CHECK (panel->keyPage (mod::slot::vel).isVisible());
    CHECK (panel->keyPage (mod::slot::vel).presetMenu().getNumItems() >= 6);
    panel->showTab (5);
    CHECK (panel->keyPage (mod::slot::note).isVisible());
    CHECK_FALSE (panel->keyPage (mod::slot::vel).isVisible());
    CHECK (panel->sourceMenu (mod::slot::note).getNumItems() >= 3);
    panel->showTab (2);
    CHECK (panel->selectedSource() == mod::slot::lfo1);
}

TEST_CASE ("plugin: CC 1-4 in the menus - the controller each follows, Learn, any controller by number", "[plugin][mod][mod-final]")
{
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    using namespace osp::plugin;
    OspAudioProcessor p;
    CHECK (modui::controllerName (11) == "CC 11 Expression");
    CHECK (modui::controllerName (21) == "CC 21");
    CHECK (modui::menuLabel (p, mod::slot::cc1) == "CC 1 (CC 2 Breath)");
    CHECK (modui::menuLabel (p, mod::slot::lfo1) == "LFO 1");
    p.learnModController (1);
    CHECK (modui::menuLabel (p, mod::slot::cc1 + 1) == juce::String::fromUTF8 ("CC 2 (learning\xe2\x80\xa6)"));
    p.learnModController (-1);
    const auto menu = modui::controllerMenu (p);
    int slots = 0;
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        slots += it.getItem().subMenu != nullptr ? 1 : 0;
    CHECK (slots == mod::ccSlots);
}

TEST_CASE ("plugin: ARP destinations - RATE (smooth and stepped), VELOCITY; saved with the session", "[plugin][mod][mod-final][arp]")
{
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    using namespace osp::plugin;
    CHECK (OspAudioProcessor::steppedArpRatio (1.0) == 1.0);
    CHECK (OspAudioProcessor::steppedArpRatio (1.3) == Approx (4.0 / 3.0));
    CHECK (OspAudioProcessor::steppedArpRatio (0.6) == Approx (2.0 / 3.0));
    CHECK (OspAudioProcessor::steppedArpRatio (9.0) == 4.0);
    CHECK (modui::destinationFor ("arp.rate", false) == mod::Dest::arpRate);
    CHECK (modui::destinationFor ("arp.octaves", false) == mod::Dest::arpOctaves);

    const auto file = writeSource (tmp.dir, "pluck.wav", testsignals::pluck (midiToHz (60), 2.0, 48000.0, 5));
    auto steps = [&file] (bool wheelToRate, bool stepped, float velocity) {
        OspAudioProcessor p;
        loadAndWait (p, file);
        arpOn (p);   // UP, 1/8, no host: 120 BPM, a step every 12000 samples
        p.setParameterValue ("arp.velocity", velocity);
        p.setArpRateStepped (stepped);
        if (wheelToRate)
            CHECK (p.addModulationRoute (mod::Source::modWheel, mod::Dest::arpRate, 100.0f) >= 0);
        const auto run = renderArp (p, 48000.0, 256, 1.0, [] (int pos, int n, juce::MidiBuffer& m) {
            at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::controllerEvent (1, 1, 127));
            at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)));
        });
        const int count = p.velocityCount.load();
        return std::pair<int, int> { run.noteOns, p.recentVelocity[static_cast<std::size_t> ((count - 1) % OspAudioProcessor::velocityHistory)].load() };
    };
    // Plain: steps at 0, 0.25, 0.5, 0.75 s. The wheel at the top through 100 %: two octaves
    // faster (4x, a step every 3000 samples once the wheel's glide is there).
    const auto plain = steps (false, false, 100.0f);
    CHECK (plain.first == 4);
    CHECK (plain.second == 100);
    const auto fast = steps (true, false, 100.0f);
    CHECK (fast.first >= 14);
    CHECK (fast.first <= 17);
    const auto stepped = steps (true, true, 50.0f);
    CHECK (stepped.first >= 12);   // the first ratio waits for the second step
    CHECK (stepped.second == 50);   // VELOCITY 50 %

    // Stepped and VELOCITY are part of the session; older sessions: smooth, 100 %.
    OspAudioProcessor p;
    p.setArpRateStepped (true);
    p.setParameterValue ("arp.velocity", 150.0f);
    const auto saved = mfState (p);
    OspAudioProcessor q;
    q.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK (q.arpRateIsStepped());
    CHECK (q.parameterValue ("arp.velocity") == Approx (150.0f));
    auto xml = mfXml (p);
    xml->removeAttribute ("arpRateStepped");
    juce::XmlElement* velocityParam = nullptr;
    for (auto* param : xml->getChildIterator())
        if (param->getStringAttribute ("id") == "arp.velocity")
            velocityParam = param;
    REQUIRE (velocityParam != nullptr);
    xml->removeChildElement (velocityParam, true);
    OspAudioProcessor old;
    mfApply (old, *xml);
    CHECK_FALSE (old.arpRateIsStepped());
    CHECK (old.parameterValue ("arp.velocity") == Approx (100.0f));
}

TEST_CASE ("plugin: DAMPEN - per layer, off by default, heard by key, saved with the session; the EQ | DAMPEN page", "[plugin][mod-final][dampen]")
{
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    using namespace osp::plugin;
    {
        OspAudioProcessor fresh;
        for (int l = 0; l < OspAudioProcessor::numLayers; ++l)
        {
            CHECK (fresh.parameterValue (OspAudioProcessor::dampenParameterId (l, "enabled")) == 0.0f);
            CHECK (fresh.parameterValue (OspAudioProcessor::dampenParameterId (l, "amount")) == Approx (100.0f));
            CHECK (fresh.parameterValue (OspAudioProcessor::dampenParameterId (l, "frequency")) == Approx (3000.0f));
            CHECK (fresh.modulationCurve (OspAudioProcessor::dampenCurveIndex (l)) == [] { auto c = dampen::defaultCurve(); c.normalise(); return c; }());
        }
        CHECK (modui::destinationFor (OspAudioProcessor::dampenParameterId (1, "amount"), true) == mod::Dest::dampenAmountB);
        CHECK (modui::destinationFor (OspAudioProcessor::dampenParameterId (2, "cutoff"), true) == mod::Dest::dampenCutoffC);
    }

    // Heard: a high note loses its highs; switched off (whatever its curve) it is exactly as before.
    const auto file = writeSource (tmp.dir, "pluck.wav", testsignals::pluck (midiToHz (60), 2.0, 48000.0, 5));
    auto render = [&file] (bool on, bool editedCurve) {
        OspAudioProcessor p;
        loadAndWait (p, file);
        p.setParameterValue (OspAudioProcessor::dampenParameterId (0, "enabled"), on ? 1.0f : 0.0f);
        if (editedCurve)
            p.setModulationCurve (OspAudioProcessor::dampenCurveIndex (0), mod::presetCurve (mod::CurvePreset::flat), false);
        return renderArp (p, 48000.0, 256, 0.6, [] (int pos, int n, juce::MidiBuffer& m) {
            at (m, pos, n, 48000.0, 0.0, juce::MidiMessage::noteOn (1, 96, static_cast<juce::uint8> (100)));
        });
    };
    const auto plain = render (false, false);
    CHECK (render (false, true).audio == plain.audio);
    const auto damped = render (true, false);
    CHECK (damped.finite);
    CHECK (damped.audio != plain.audio);

    // Saved: its parameters and its curve; an older session: off, the default curve.
    OspAudioProcessor p;
    p.setParameterValue (OspAudioProcessor::dampenParameterId (1, "enabled"), 1.0f);
    p.setParameterValue (OspAudioProcessor::dampenParameterId (1, "mode"), 1.0f);
    const auto lowOnly = [] { auto c = mod::presetCurve (mod::CurvePreset::lowOnly); c.normalise(); return c; }();
    p.setModulationCurve (OspAudioProcessor::dampenCurveIndex (1), lowOnly, false);
    const auto saved = mfState (p);
    OspAudioProcessor q;
    q.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK (q.parameterValue (OspAudioProcessor::dampenParameterId (1, "enabled")) == 1.0f);
    CHECK (q.parameterValue (OspAudioProcessor::dampenParameterId (1, "mode")) == 1.0f);
    CHECK (mod::matchingPreset (q.modulationCurve (OspAudioProcessor::dampenCurveIndex (1))) == static_cast<int> (mod::CurvePreset::lowOnly));
    auto xml = mfXml (p);
    xml->setAttribute ("modCurves", OspAudioProcessor::encodeModulationCurves (OspAudioProcessor::defaultModulationCurves()).replace ("c3:", "c2:").upToLastOccurrenceOf ("|", false, false)
                                                                                  .upToLastOccurrenceOf ("|", false, false).upToLastOccurrenceOf ("|", false, false));
    std::vector<juce::XmlElement*> dampenParams;
    for (auto* param : xml->getChildIterator())
        if (param->getStringAttribute ("id").contains (".dampen."))
            dampenParams.push_back (param);
    CHECK (dampenParams.size() == 15);
    for (auto* param : dampenParams)
        xml->removeChildElement (param, true);
    OspAudioProcessor old;
    mfApply (old, *xml);
    CHECK (old.parameterValue (OspAudioProcessor::dampenParameterId (1, "enabled")) == 0.0f);
    CHECK (mod::matchingPreset (old.modulationCurve (OspAudioProcessor::dampenCurveIndex (1))) == -1);   // the default (no preset)
    CHECK (old.modulationCurve (OspAudioProcessor::dampenCurveIndex (1)).count == 3);

    // The page: EQ | DAMPEN; the mode, the switch, the presets and a point added by double-click.
    OspAudioProcessor e;
    EqEditor eqEditor (e, 0);
    eqEditor.setSize (700, 240);
    CHECK_FALSE (eqEditor.dampenShown());
    eqEditor.showDampen (true);
    REQUIRE (eqEditor.dampenShown());
    auto& page = *eqEditor.dampenEditor();
    CHECK (page.getBounds() == eqEditor.getLocalBounds());
    auto click = [] (juce::Component& c, juce::Point<float> at, int clicks = 1) {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (source, at, {}, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, at, now, clicks, false);
    };
    page.mouseDown (click (page, page.powerArea().getCentre()));
    CHECK (e.parameterValue (OspAudioProcessor::dampenParameterId (0, "enabled")) == 1.0f);
    page.mouseDown (click (page, { page.modeArea().getRight() - 10.0f, page.modeArea().getCentreY() }));
    CHECK (e.parameterValue (OspAudioProcessor::dampenParameterId (0, "mode")) == 1.0f);
    page.applyPreset (static_cast<int> (mod::CurvePreset::highOnly) + 1);
    CHECK (mod::matchingPreset (e.modulationCurve (OspAudioProcessor::dampenCurveIndex (0))) == static_cast<int> (mod::CurvePreset::highOnly));
    CHECK (page.dampingAt (127) == Approx (1.0).margin (1.0e-3));
    const int before = e.modulationCurve (OspAudioProcessor::dampenCurveIndex (0)).count;
    const auto g = page.graphArea();
    page.mouseDoubleClick (click (page, { g.getX() + 0.3f * g.getWidth(), g.getY() + 0.1f * g.getHeight() }, 2));
    CHECK (e.modulationCurve (OspAudioProcessor::dampenCurveIndex (0)).count == before + 1);
    e.undoManager.undo();
    CHECK (e.modulationCurve (OspAudioProcessor::dampenCurveIndex (0)).count == before);
    // Painting both pages is safe.
    juce::Image image (juce::Image::ARGB, 700, 240, true);
    juce::Graphics graphics (image);
    page.paint (graphics);
    eqEditor.showDampen (false);
    CHECK_FALSE (eqEditor.dampenShown());
    eqEditor.paint (graphics);
}

TEST_CASE ("plugin: a popover over the instrument hides the halos under it (and they cannot be grabbed through it)", "[plugin][mod][mod-final][halo]")
{
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    using namespace osp::plugin;
    const auto file = writeSource (tmp.dir, "pluck.wav", testsignals::pluck (midiToHz (60), 2.0, 48000.0, 5));
    OspAudioProcessor p;
    loadAndWait (p, file);
    const juce::StringArray ids { "layerA.level", "layerA.pan", "layerA.tune", "layerA.start", OspAudioProcessor::reimaginedParameterId (0) };
    for (const auto dest : { mod::Dest::levelA, mod::Dest::panA, mod::Dest::fineTuneA, mod::Dest::startA, mod::Dest::reimaginedA })
        REQUIRE (p.addModulationRoute (mod::Source::lfo1, dest, 40.0f) >= 0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditorIfNeeded());
    auto* editor = dynamic_cast<OspAudioProcessorEditor*> (base.get());
    REQUIRE (editor != nullptr);
    editor->refreshNow();
    auto& overlay = editor->modulationOverlay();
    overlay.refresh();
    for (const auto& id : ids)
        CHECK (overlay.haloCovers (id).empty());   // nothing open: nothing covered

    int covered = 0;
    for (const int popup : { 0, 1, 2, 3, 4, OspAudioProcessorEditor::echoPopup })
    {
        editor->openPopup (popup);
        editor->refreshNow();
        overlay.refresh();
        for (const auto& id : ids)
        {
            const auto covers = overlay.haloCovers (id);
            if (covers.empty())
                continue;
            ++covered;
            // Every point of the ring inside the popover's card belongs to the popover.
            const auto centre = overlay.haloCentre (id);
            const float radius = overlay.haloRadius (id);
            for (int a = 0; a < 72; ++a)
            {
                const auto at = centre.getPointOnCircumference (radius, juce::MathConstants<float>::twoPi * static_cast<float> (a) / 72.0f);
                if (! covers.front().reduced (14.0f).contains (at))
                    continue;
                const auto hit = overlay.hitAt (at);
                CHECK (hit.parameterId != id);
            }
        }
        if (const char* dir = std::getenv ("OSP_SNAPSHOT_DIR"); dir != nullptr && popup == OspAudioProcessorEditor::echoPopup)
        {
            // For review: ECHO's popover over modulated knobs (their halos stay under it).
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream out (juce::File (dir).getChildFile ("halo-under-popover.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
        editor->closePopup();
    }
    CHECK (covered > 0);   // the macros' popovers unfold over the cards' knobs
    editor->refreshNow();
    overlay.refresh();
    for (const auto& id : ids)
        CHECK (overlay.haloCovers (id).empty());   // closed: every halo again
}
