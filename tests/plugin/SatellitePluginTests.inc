// SATELLITE in ANDOR OSP (ANDOR ANALOG's, docs/STATUS.md): the key on the per-card
// arpeggiator's title row, the shared panel anchored under it, each layer's ten parameters,
// the audio moving, recall, and sessions from before opening with every layer still.
// Included at the end of PluginProcessorTests.cpp (after CharacterGraphTests.inc: setPlain).

#include "Binding.h"
#include "SatellitePanel.h"

namespace
{
    float satValue (OspAudioProcessor& p, int layer, const char* field)
    {
        return p.parameterValue (OspAudioProcessor::satelliteFieldId (layer, field));
    }

    /** Plays a held note and returns the left / right levels over the last half of `seconds`. */
    std::pair<double, double> playLevels (OspAudioProcessor& p, double seconds)
    {
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buffer (2, 256);
        double l = 0.0, r = 0.0;
        const int blocks = static_cast<int> (seconds * 48000.0 / 256.0);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, static_cast<juce::uint8> (100)), 0);
            buffer.clear();
            p.processBlock (buffer, midi);
            if (b >= blocks / 2)
                for (int i = 0; i < 256; ++i)
                {
                    l += static_cast<double> (buffer.getSample (0, i)) * buffer.getSample (0, i);
                    r += static_cast<double> (buffer.getSample (1, i)) * buffer.getSample (1, i);
                }
        }
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        p.processBlock (buffer, off);
        p.releaseResources();
        return { std::sqrt (l), std::sqrt (r) };
    }
}

TEST_CASE ("satellite: the key on the arpeggiator's title row switches and opens; the panel shows a card and never switches it", "[plugin][satellite]")
{
    using Editor = osp::plugin::OspAudioProcessorEditor;
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<Editor*> (e.get());
    REQUIRE (ui != nullptr);
    ui->arpControl().onToggleEditor();
    REQUIRE (ui->isArpExpanded());
    auto& arp = ui->arpInlinePanel();
    auto* key = arp.satelliteKey();
    REQUIRE (key != nullptr);
    CHECK (key->isVisible());
    // On the title row, after EDIT A | B | C, inside the panel.
    CHECK (static_cast<float> (key->getX()) >= arp.editTabArea (2).getRight());
    CHECK (arp.getLocalBounds().contains (key->getBounds()));
    for (int layer = 0; layer < 3; ++layer)
        CHECK (satValue (p, layer, "enabled") < 0.5f);
    CHECK_FALSE (key->anyOn());

    // The light: the edited card on; again: all off (remembered); again: those back. One undo step.
    key->toggle();
    CHECK (satValue (p, 0, "enabled") > 0.5f);
    CHECK (key->anyOn());
    key->toggle();
    CHECK (satValue (p, 0, "enabled") < 0.5f);
    key->toggle();
    CHECK (satValue (p, 0, "enabled") > 0.5f);
    key->toggle();
    CHECK (satValue (p, 0, "enabled") < 0.5f);
    CHECK (ui->openPopupIndex() < 0);   // switching never opens
    // (One undo step: the key opens one transaction; the parameter tree records it on its flush.)

    // The name: the panel, anchored under the key; opening never switches.
    key->openPanel();
    CHECK (ui->openPopupIndex() == Editor::anchoredPopup);
    CHECK (key->isOpen());
    auto* panel = dynamic_cast<osp::plugin::SatellitePanel*> (ui->openPopupPanel());
    REQUIRE (panel != nullptr);
    CHECK (panel->shownCard() == 0);
    CHECK (satValue (p, 0, "enabled") < 0.5f);
    // Under the key (above it only where there is no room), never over it, inside the window.
    const int shadow = juce::roundToInt (osp::plugin::MiniPanel::shadowMargin * panel->getTransform().getScaleFactor());
    const auto panelBounds = ui->getLocalArea (panel->getParentComponent(), panel->getBoundsInParent()).reduced (shadow);
    const auto keyBounds = ui->getLocalArea (&arp, key->getBounds());
    INFO ("panel " << panelBounds.toString() << " key " << keyBounds.toString() << " editor " << ui->getLocalBounds().toString());
    CHECK_FALSE (panelBounds.intersects (keyBounds));
    CHECK ((panelBounds.getY() >= keyBounds.getBottom() || panelBounds.getBottom() <= keyBounds.getY()));
    CHECK (ui->getLocalBounds().contains (panelBounds));
    // A / B / C show a card's settings; only its light switches it.
    panel->showCard (2);
    CHECK (panel->shownCard() == 2);
    CHECK (satValue (p, 2, "enabled") < 0.5f);
    panel->lamp().toggle();
    CHECK (satValue (p, 2, "enabled") > 0.5f);
    CHECK (satValue (p, 0, "enabled") < 0.5f);
    // Its controls are the shown card's parameters.
    CHECK (osp::plugin::binding::tagsOf (panel->modeControl()).contains (OspAudioProcessor::satelliteFieldId (2, "mode")));
    // Contextual: RATE as a division in SYNC, in Hz in FREE; CYCLE / NOTE only in BOUNCE.
    panel->refreshContent();
    CHECK (panel->divisionKnob().isVisible());
    CHECK_FALSE (panel->rateKnob().isVisible());
    CHECK_FALSE (panel->bounceControl().isVisible());
    setPlain (p, OspAudioProcessor::satelliteFieldId (2, "sync").toRawUTF8(), 1.0f);
    setPlain (p, OspAudioProcessor::satelliteFieldId (2, "mode").toRawUTF8(), 2.0f);
    panel->refreshContent();
    CHECK (panel->rateKnob().isVisible());
    CHECK (panel->bounceControl().isVisible());
    // The key again closes it.
    key->openPanel();
    CHECK (ui->openPopupIndex() < 0);
}

TEST_CASE ("satellite: a layer's place is heard; saved and recalled; a session from before opens with every layer still", "[plugin][satellite]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    OspAudioProcessor p;
    loadAndWait (p, writeSource (tmp.dir, "vowel.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 4)));
    const auto centre = playLevels (p, 0.6);
    setPlain (p, "layerA.satellite.enabled", 1.0f);
    setPlain (p, "layerA.satellite.depth", 0.0f);
    setPlain (p, "layerA.satellite.offset", 100.0f);
    const auto right = playLevels (p, 0.6);
    // Hard right: the left keeps only what the effects after the voices spread (SPACE, REIMAGINED).
    CHECK (right.first < 0.35 * right.second);
    CHECK (centre.first > 0.6 * centre.second);
    // The engine reports it (the panel's marker).
    float x = 0.0f, rear = 0.0f;
    CHECK (p.satelliteView (0, x, rear));
    CHECK (x == Approx (1.0f).margin (0.01));

    setPlain (p, "layerA.satellite.mode", 1.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    {
        OspAudioProcessor restored;
        restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        CHECK (satValue (restored, 0, "enabled") > 0.5f);
        CHECK (satValue (restored, 0, "mode") == Approx (1.0f));
        CHECK (satValue (restored, 0, "offset") == Approx (100.0f));
    }
    // The same session without the SATELLITE parameters (saved before they existed): still.
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (xml != nullptr);
    auto tree = juce::ValueTree::fromXml (*xml);
    std::function<void (juce::ValueTree)> strip = [&strip] (juce::ValueTree t) {
        for (int i = t.getNumChildren(); --i >= 0;)
        {
            auto child = t.getChild (i);
            if (child["id"].toString().contains (".satellite."))
                t.removeChild (i, nullptr);
            else
                strip (child);
        }
    };
    strip (tree);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);
    // (into an instance where A was moving: the old session must stop it)
    p.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    for (int layer = 0; layer < 3; ++layer)
        CHECK (satValue (p, layer, "enabled") < 0.5f);
    CHECK (satValue (p, 0, "offset") == Approx (0.0f));
}

TEST_CASE ("satellite: screenshots (OSP_SATELLITE_SHOTS)", "[.][satellite-shots]")
{
    const char* dir = std::getenv ("OSP_SATELLITE_SHOTS");
    if (dir == nullptr)
        return;
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedLook ceramic (skin::Look::ceramic);
    OspAudioProcessor p;
    loadAndWait (p, writeSource (tmp.dir, "vowel.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 4)));
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
    REQUIRE (ui != nullptr);
    auto save = [dir] (const juce::Image& image, const juce::String& name) {
        juce::FileOutputStream out (juce::File (dir).getChildFile (name));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
    };
    for (const int width : { 1448, 869 })
    {
        ui->arpControl().onToggleEditor();
        if (! ui->isArpExpanded())
            ui->arpControl().onToggleEditor();
        e->setSize (width, juce::roundToInt (static_cast<float> (width) * ui->instrumentHeight() / 1448.0f));
        setPlain (p, "layerA.satellite.enabled", 1.0f);
        setPlain (p, "layerA.satellite.mode", 1.0f);
        setPlain (p, "layerA.satellite.sync", 1.0f);
        setPlain (p, "layerA.satellite.rate", 0.5f);
        setPlain (p, "layerA.satellite.depth", 80.0f);
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buffer (2, 256);
        for (int b = 0; b < 60; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, static_cast<juce::uint8> (100)), 0);
            buffer.clear();
            p.processBlock (buffer, midi);
        }
        ui->refreshNow();
        save (e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f), "satellite-1-key-" + juce::String (width) + ".png");
        auto* key = ui->arpInlinePanel().satelliteKey();
        REQUIRE (key != nullptr);
        key->openPanel();
        if (auto* panel = ui->openPopupPanel())
        {
            panel->refreshContent();
            panel->repaint();
        }
        ui->refreshNow();
        save (e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f), "satellite-2-panel-" + juce::String (width) + ".png");
        key->openPanel();   // (closes)
        p.releaseResources();
    }
}
