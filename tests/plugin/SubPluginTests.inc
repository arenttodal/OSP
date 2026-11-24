// SUB in ANDOR OSP (ANDOR ANALOG's sub oscillator and its V7 popover; docs/sub/osp-sub.md): the
// ten `sub.*` parameters, the arpeggiator housing's second row, the shared panel above it, the
// musician's own keys (before the arpeggiator), recall, and sessions from before opening off.
// Included at the end of PluginProcessorTests.cpp.

#include "AndorSubPanel.h"

#include <catch2/catch_approx.hpp>

namespace
{
    float subValue (OspAudioProcessor& p, const char* field)
    {
        return p.parameterValue (OspAudioProcessor::subFieldId (field));
    }

    void setSub (OspAudioProcessor& p, const char* field, float v)
    {
        auto* param = p.parameters.getParameter (OspAudioProcessor::subFieldId (field));
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (v));
    }

    juce::MouseEvent subMouse (juce::Component& c, juce::Point<float> where, int clicks = 1)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (source, where, {}, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, where, now, clicks, false);
    }

    /** Renders `seconds` with `first` at the start (an empty instrument: only SUB can sound),
        returning the left channel. */
    std::vector<float> renderSub (OspAudioProcessor& p, double seconds, juce::MidiBuffer first)
    {
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buffer (2, 256);
        std::vector<float> out;
        const int blocks = static_cast<int> (seconds * 48000.0 / 256.0);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                midi = first;
            buffer.clear();
            p.processBlock (buffer, midi);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 256);
        }
        p.releaseResources();
        return out;
    }

    double subRms (const std::vector<float>& x, std::size_t from)
    {
        double s = 0.0;
        for (std::size_t i = from; i < x.size(); ++i)
            s += static_cast<double> (x[i]) * x[i];
        return x.size() > from ? std::sqrt (s / static_cast<double> (x.size() - from)) : 0.0;
    }
} // namespace

TEST_CASE ("sub: ten parameters with ANDOR ANALOG's ranges and defaults; off by default; ANALOG's names", "[plugin][sub]")
{
    OspAudioProcessor p;
    for (const auto* field : OspAudioProcessor::subFields())
        CHECK (p.parameters.getParameter (OspAudioProcessor::subFieldId (field)) != nullptr);
    CHECK (subValue (p, "enabled") == 0.0f);
    CHECK (subValue (p, "wave") == 0.0f);      // SINE
    CHECK (subValue (p, "octave") == 1.0f);    // -2
    CHECK (subValue (p, "grit") == 0.0f);
    CHECK (subValue (p, "level") == Catch::Approx (-18.0f));
    CHECK (subValue (p, "lowKey") == 48.0f);
    CHECK (subValue (p, "highKey") == 84.0f);
    CHECK (subValue (p, "response") == 1.0f);  // TAPER
    CHECK (subValue (p, "taperStart") == 60.0f);
    CHECK (subValue (p, "taperSlope") == Catch::Approx (6.0f));
    auto* low = p.parameters.getParameter (OspAudioProcessor::subFieldId ("lowKey"));
    CHECK (low->getName (64) == "Sub Low Key");
    CHECK (low->getText (low->convertTo0to1 (60.0f), 0) == "C4");
    // The settings the engine takes: ANDOR ANALOG's reader.
    std::array<float, 10> raw { 1.0f, 3.0f, 0.0f, 40.0f, -6.0f, 70.0f, 60.0f, 0.0f, 50.0f, 3.0f };
    const auto s = OspAudioProcessor::subSettingsFrom (raw);
    CHECK (s.enabled);
    CHECK (s.wave == osp::sub::SubSettings::Wave::saw);
    CHECK (s.octave == -1);
    CHECK (s.grit == Catch::Approx (0.4));
    CHECK (s.levelDb == Catch::Approx (-6.0));
    CHECK (s.lowKey == 70);
    CHECK (s.highKey == 70);   // (never below the low key)
    CHECK (! s.taper);
}

TEST_CASE ("sub: off, the output is unchanged; on, it plays the held key two octaves down, and recalls", "[plugin][sub]")
{
    juce::MidiBuffer chord;
    chord.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 0);
    {
        OspAudioProcessor p;
        const auto out = renderSub (p, 0.5, chord);
        CHECK (subRms (out, 0) == 0.0);   // (an empty instrument, SUB off: silence)
    }
    OspAudioProcessor p;
    setSub (p, "enabled", 1.0f);
    setSub (p, "level", -12.0f);
    const auto out = renderSub (p, 0.8, chord);
    CHECK (subRms (out, 9600) > 0.05);
    // C2 (65.4 Hz): positive-going zero crossings over the last half second.
    int crossings = 0;
    for (std::size_t i = 14400; i + 1 < out.size(); ++i)
        crossings += out[i] <= 0.0f && out[i + 1] > 0.0f ? 1 : 0;
    const double seconds = static_cast<double> (out.size() - 14400) / 48000.0;
    CHECK (std::abs (crossings / seconds - 65.4) < 3.0);

    // Recall: the state keeps every SUB setting.
    setSub (p, "wave", 2.0f);
    setSub (p, "lowKey", 36.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    OspAudioProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (subValue (q, "enabled") == 1.0f);
    CHECK (subValue (q, "wave") == 2.0f);
    CHECK (subValue (q, "lowKey") == 36.0f);
    CHECK (subValue (q, "level") == Catch::Approx (-12.0f));
}

TEST_CASE ("sub: a session from before SUB opens with it off", "[plugin][sub]")
{
    OspAudioProcessor p;
    setSub (p, "enabled", 1.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    // The same state without any `sub.*` parameter (as every session saved before them).
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (xml != nullptr);
    std::vector<juce::XmlElement*> drop;
    for (auto* e : xml->getChildIterator())
        if (e->getStringAttribute ("id").startsWith ("sub."))
            drop.push_back (e);
    for (auto* e : xml->getChildIterator())
        for (auto* c : e->getChildIterator())
            if (c->getStringAttribute ("id").startsWith ("sub."))
                drop.push_back (c);
    REQUIRE (! drop.empty());
    for (auto* e : drop)
        if (auto* parent = xml->findParentElementOf (e))
            parent->removeChildElement (e, true);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    OspAudioProcessor q;
    setSub (q, "enabled", 1.0f);   // (a state load sets what it does not mention to the default)
    q.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    CHECK (subValue (q, "enabled") == 0.0f);
}

TEST_CASE ("sub: it plays the keys held, not the arpeggiator's notes", "[plugin][sub]")
{
    // The arpeggiator on, a C3 held: the cards would play the arpeggio, SUB holds C1.
    OspAudioProcessor p;
    setSub (p, "enabled", 1.0f);
    setSub (p, "level", -12.0f);
    p.setParameterValue ("arp.enabled", 1.0f);
    juce::MidiBuffer held;
    held.addEvent (juce::MidiMessage::noteOn (1, 48, static_cast<juce::uint8> (100)), 0);
    const auto out = renderSub (p, 1.0, held);
    // Steady: SUB is not retriggered by the arpeggio's steps.
    double lo = 1.0e9, hi = 0.0;
    for (std::size_t at = 24000; at + 2400 <= out.size(); at += 2400)
    {
        std::vector<float> w (out.begin() + static_cast<std::ptrdiff_t> (at), out.begin() + static_cast<std::ptrdiff_t> (at + 2400));
        const double v = subRms (w, 0);
        lo = std::min (lo, v);
        hi = std::max (hi, v);
    }
    INFO ("lo " << lo << " hi " << hi);
    CHECK (hi > 0.02);   // (C1, 32.7 Hz, under the output gain)
    CHECK (lo > 0.9 * hi);
}

TEST_CASE ("sub: the arpeggiator housing's second row switches SUB and opens its panel above; the panel never switches it", "[plugin][sub]")
{
    using Editor = osp::plugin::OspAudioProcessorEditor;
    for (const int width : { 869, 1086, 1448, 1810 })
    {
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto* ui = dynamic_cast<Editor*> (e.get());
        REQUIRE (ui != nullptr);
        e->setSize (width, juce::roundToInt (static_cast<float> (width) * static_cast<float> (e->getHeight()) / static_cast<float> (e->getWidth())));
        ui->refreshNow();
        auto& arp = ui->arpControl();
        REQUIRE (arp.hasSecondRow());
        // The lower light switches SUB (one host gesture, undoable).
        arp.mouseDown (subMouse (arp, arp.secondLightArea().getCentre()));
        CHECK (subValue (p, "enabled") == 1.0f);
        arp.mouseDown (subMouse (arp, arp.secondLightArea().getCentre()));
        CHECK (subValue (p, "enabled") == 0.0f);
        // The row opens the panel; opening it switches nothing.
        REQUIRE (arp.onSecondRow != nullptr);
        arp.onSecondRow();
        ui->refreshNow();
        REQUIRE (ui->openPopupIndex() == Editor::subPopup);
        auto* panel = dynamic_cast<osp::plugin::SubPanel*> (ui->openPopupPanel());
        REQUIRE (panel != nullptr);
        CHECK (subValue (p, "enabled") == 0.0f);
        // Above the housing, inside the window.
        const auto card = ui->getLocalArea (panel, panel->card());
        const auto housing = ui->getLocalArea (&arp, arp.getLocalBounds());
        INFO (width << ": card " << card.toString() << " housing " << housing.toString());
        CHECK (card.getBottom() <= housing.getY());
        CHECK (ui->getLocalBounds().contains (card));
        // Its controls are SUB's parameters.
        CHECK (panel->gritKnob().slider.getProperties()["paramId"].toString() == OspAudioProcessor::subFieldId ("grit"));
        CHECK (panel->levelKnob().slider.getProperties()["paramId"].toString() == OspAudioProcessor::subFieldId ("level"));
        // The row again closes it.
        arp.onSecondRow();
        CHECK (ui->openPopupIndex() != Editor::subPopup);
    }
}

TEST_CASE ("sub: the panel's keyboard lights the keys held (before the arpeggiator)", "[plugin][sub]")
{
    OspAudioProcessor p;
    auto panel = p.createSubPanel();
    auto* sub = dynamic_cast<osp::plugin::SubPanel*> (panel.get());
    REQUIRE (sub != nullptr);
    p.keyboardState.noteOn (1, 52, 0.8f);
    sub->display().pollHeldKeys();
    CHECK (sub->display().heldKeys()[52]);
    CHECK (! sub->display().heldKeys()[53]);
    p.keyboardState.noteOff (1, 52, 0.0f);
    sub->display().pollHeldKeys();
    CHECK (! sub->display().heldKeys()[52]);
}

TEST_CASE ("sub: the drawn response is SubOsc::keyGain's, and the range drag is one gesture, never crossing", "[plugin][sub]")
{
    using osp::plugin::SubResponseDisplay;
    namespace subview = osp::plugin::subview;
    for (const bool taper : { false, true })
        for (const int start : { 40, 60, 90 })
            for (const double slope : { 0.0, 5.5, 12.0 })
            {
                osp::sub::SubSettings s;
                s.enabled = true;
                s.lowKey = 0;
                s.highKey = 127;
                s.taper = taper;
                s.taperStart = start;
                s.taperDbPerOctave = slope;
                osp::sub::SubOsc osc;
                osc.prepare (48000.0);
                osc.setSettings (s);
                const double ref = 20.0 * std::log10 (osc.keyGain (0));
                for (int key = 0; key <= 127; key += 7)
                    CHECK (subview::relativeLossDb (key, taper, start, slope) == Catch::Approx (ref - 20.0 * std::log10 (osc.keyGain (key))).margin (1.0e-3));
            }

    OspAudioProcessor p;
    auto panel = p.createSubPanel();
    const auto size = panel->cardSize();
    panel->setSize (size.x + 2 * osp::plugin::MiniPanel::shadowMargin, size.y + 2 * osp::plugin::MiniPanel::shadowMargin);
    auto& d = dynamic_cast<osp::plugin::SubPanel&> (*panel).display();
    auto drag = [&d] (juce::Point<float> from, juce::Point<float> to) {
        d.mouseDown (subMouse (d, from));
        d.mouseDrag (subMouse (d, to));
        d.mouseUp (subMouse (d, to));
    };
    // The low end dragged past the high end stops at it.
    drag (d.handlePosition (SubResponseDisplay::Handle::low).translated (0.0f, 4.0f), { d.keyArea (96).getCentreX(), d.keybedArea().getCentreY() });
    CHECK (juce::roundToInt (subValue (p, "lowKey")) == 84);
    CHECK (juce::roundToInt (subValue (p, "highKey")) == 84);
    // FROM on the curve, whole semitones.
    setSub (p, "lowKey", 48.0f);
    setSub (p, "highKey", 84.0f);
    drag (d.handlePosition (SubResponseDisplay::Handle::from), { d.keyArea (67).getCentreX(), d.handlePosition (SubResponseDisplay::Handle::from).y });
    CHECK (juce::roundToInt (subValue (p, "taperStart")) == 67);
}

TEST_CASE ("sub: the SUB lesson, end to end, from the musician's own actions", "[plugin][sub][learn]")
{
    LearnRig rig;
    REQUIRE (rig.learn.startLesson ("osp.sub"));
    rig.tick();
    CHECK (rig.stepId() == "setup");
    rig.learn.noteAction ("current");   // (SUB needs no recording)
    rig.tick();
    CHECK (rig.stepId() == "open");
    auto& arp = rig.ui.arpControl();
    arp.onSecondRow();   // (the row's own click handler)
    rig.tick();
    REQUIRE (rig.stepDone());
    rig.next();
    CHECK (rig.stepId() == "on");
    arp.mouseDown (subMouse (arp, arp.secondLightArea().getCentre()));   // the light: one gesture
    rig.tick();
    REQUIRE (rig.stepDone());
    CHECK (subValue (rig.p, "enabled") == 1.0f);
    rig.next();
    CHECK (rig.stepId() == "play");
    rig.playNote (48);
    REQUIRE (rig.stepDone());
    rig.next();
    CHECK (rig.stepId() == "level");
    rig.automate (OspAudioProcessor::subFieldId ("level"), -30.0f);   // (automation is not the musician)
    CHECK_FALSE (rig.stepDone());
    rig.userSet (OspAudioProcessor::subFieldId ("level"), -10.0f);
    REQUIRE (rig.stepDone());
    rig.next();
    CHECK (rig.stepId() == "range");
    rig.userSet (OspAudioProcessor::subFieldId ("lowKey"), 36.0f);
    REQUIRE (rig.stepDone());
}

TEST_CASE ("sub: screenshots and demo renders (OSP_SUB_SHOTS)", "[.][sub-shots]")
{
    const auto dirName = juce::SystemStats::getEnvironmentVariable ("OSP_SUB_SHOTS", {});
    REQUIRE (dirName.isNotEmpty());
    const juce::File dir (dirName);
    dir.createDirectory();
    auto shoot = [&dir] (juce::Component& c, const juce::String& name) {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        juce::FileOutputStream out (dir.getChildFile (name + ".png"));
        REQUIRE (out.openedOk());
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
    };
    using Editor = osp::plugin::OspAudioProcessorEditor;
    for (const int width : { 1448, 1086, 869 })
    {
        const auto w = juce::String (width);
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto& ui = dynamic_cast<Editor&> (*e);
        e->setSize (width, juce::roundToInt (ui.instrumentHeight() * static_cast<float> (width) / osp::plugin::design::width));
        ui.refreshNow();
        shoot (*e, "sub-00-housing-off-" + w);
        setSub (p, "enabled", 1.0f);
        ui.refreshNow();
        shoot (*e, "sub-01-housing-on-" + w);
        ui.openPopup (Editor::subPopup);
        ui.refreshNow();
        shoot (*e, "sub-02-panel-defaults-" + w);
        setSub (p, "wave", 3.0f);
        setSub (p, "grit", 60.0f);
        setSub (p, "lowKey", 36.0f);
        setSub (p, "highKey", 72.0f);
        setSub (p, "taperStart", 48.0f);
        setSub (p, "taperSlope", 9.0f);
        ui.refreshNow();
        shoot (*e, "sub-03-panel-values-" + w);
        setSub (p, "response", 0.0f);
        ui.refreshNow();
        shoot (*e, "sub-04-panel-flat-" + w);
        setSub (p, "response", 1.0f);
        for (const int k : { 43, 48, 52, 55, 79 })
            p.keyboardState.noteOn (1, k, 0.8f);
        if (auto* panel = dynamic_cast<osp::plugin::SubPanel*> (ui.openPopupPanel()))
            panel->display().pollHeldKeys();
        ui.refreshNow();
        shoot (*e, "sub-05-panel-held-" + w);
        setSub (p, "voices", 1.0f);
        ui.refreshNow();
        shoot (*e, "sub-06-panel-mono-" + w);
        setSub (p, "priority", 1.0f);
        ui.refreshNow();
        shoot (*e, "sub-07-panel-mono-latest-" + w);
    }

    // Demo renders: the plucked tutorial note, C3 E3 G3 held under the arpeggiator (1/16, UP),
    // SUB off and on, then with FILTER closed and with MOVEMENT's PULSE. 24-bit, 48 kHz.
    auto render = [&dir] (const juce::String& name, std::function<void (OspAudioProcessor&)> setUp) {
        OspAudioProcessor p;
        REQUIRE (p.loadTutorialExample ("pluck"));
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        p.setParameterValue ("arp.enabled", 1.0f);
        setUp (p);
        p.prepareToPlay (48000.0, 256);
        const int blocks = static_cast<int> (6.0 * 48000.0 / 256.0);
        juce::AudioBuffer<float> all (2, blocks * 256), buffer (2, 256);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                for (const int k : { 48, 52, 55 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, k, static_cast<juce::uint8> (100)), 0);
            if (b == static_cast<int> (4.5 * 48000.0 / 256.0))
                for (const int k : { 48, 52, 55 })
                    midi.addEvent (juce::MidiMessage::noteOff (1, k), 0);
            buffer.clear();
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                all.copyFrom (ch, b * 256, buffer, ch, 0, 256);
        }
        p.releaseResources();
        const auto file = dir.getChildFile (name + ".wav");
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> out (file.createOutputStream());
        REQUIRE (out != nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer (juce::WavAudioFormat().createWriterFor (out.get(), 48000.0, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        out.release();
        writer->writeFromAudioSampleBuffer (all, 0, all.getNumSamples());
    };
    render ("sub-render-a-off", [] (OspAudioProcessor&) {});
    render ("sub-render-b-on", [] (OspAudioProcessor& p) { setSub (p, "enabled", 1.0f); setSub (p, "level", -9.0f); });
    render ("sub-render-c-on-filter-closed", [] (OspAudioProcessor& p) {
        setSub (p, "enabled", 1.0f);
        setSub (p, "level", -9.0f);
        p.setParameterValue ("character", 0.0f);
    });
    render ("sub-render-d-on-pulse", [] (OspAudioProcessor& p) {
        setSub (p, "enabled", 1.0f);
        setSub (p, "level", -9.0f);
        p.setParameterValue ("movement.mode", 3.0f);
        p.setParameterValue ("motion", 100.0f);
    });
    render ("sub-render-e-on-saw-grit", [] (OspAudioProcessor& p) {
        setSub (p, "enabled", 1.0f);
        setSub (p, "level", -12.0f);
        setSub (p, "wave", 3.0f);
        setSub (p, "grit", 70.0f);
        setSub (p, "octave", 0.0f);
    });
}

TEST_CASE ("sub: VOICES (POLY / MONO) and MONO NOTE (LOWEST / LATEST) - POLY and LOWEST by default, in the panel where RESPONSE was, and the engine takes them", "[plugin][sub]")
{
    OspAudioProcessor p;
    CHECK (subValue (p, "voices") == 0.0f);     // POLY: every session from before as it was
    CHECK (subValue (p, "priority") == 0.0f);   // LOWEST
    auto panel = p.createSubPanel();
    auto* sub = dynamic_cast<osp::plugin::SubPanel*> (panel.get());
    REQUIRE (sub != nullptr);
    panel->setSize (panel->cardSize().x, panel->cardSize().y);
    CHECK (sub->voicesControl().isVisible());
    CHECK (sub->priorityControl().isVisible());
    CHECK (sub->priorityControl().getAlpha() < 1.0f);   // (POLY: MONO NOTE dimmed, still there)
    CHECK (sub->priorityControl().getY() > sub->voicesControl().getBottom());
    CHECK (sub->voicesControl().getX() > sub->levelKnob().getRight());
    CHECK (panel->getLocalBounds().contains (sub->priorityControl().getBounds()));
    setSub (p, "voices", 1.0f);
    sub->refreshContent();
    CHECK (sub->priorityControl().getAlpha() == 1.0f);
    // What it plays: three keys held (below the taper, so the level is the same for each), MONO
    // sounds exactly as the lowest alone; POLY sounds all three.
    auto render = [] (bool mono, std::initializer_list<int> keys) {
        OspAudioProcessor q;
        setSub (q, "enabled", 1.0f);
        setSub (q, "voices", mono ? 1.0f : 0.0f);
        juce::MidiBuffer chord;
        for (const int k : keys)
            chord.addEvent (juce::MidiMessage::noteOn (1, k, static_cast<juce::uint8> (100)), 0);
        return renderSub (q, 0.5, chord);
    };
    const auto mono = render (true, { 58, 50, 55 });
    const auto lowest = render (true, { 50 });
    const auto poly = render (false, { 58, 50, 55 });
    REQUIRE (mono.size() == lowest.size());
    double monoDiff = 0.0, polyDiff = 0.0;
    for (std::size_t i = 0; i < mono.size(); ++i)
    {
        monoDiff = std::max (monoDiff, static_cast<double> (std::abs (mono[i] - lowest[i])));
        polyDiff = std::max (polyDiff, static_cast<double> (std::abs (poly[i] - lowest[i])));
    }
    CHECK (subRms (lowest, 4800) > 0.0);
    CHECK (monoDiff < 1.0e-6);
    CHECK (polyDiff > 1.0e-3);
}
