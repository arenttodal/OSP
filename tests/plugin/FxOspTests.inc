// ANDOR OSP with the shared FX library: the route is saved only once it is CUSTOM (every
// session and preset from before opens CLASSIC and sounds as it did), it survives a session,
// a CUSTOM route plays, and the FX rack opens below the sources and closes back to the very
// same window. Screenshots for the design review: OSP_FX_SHOTS=<folder> "[fx-shots]".
// Included at the end of PluginProcessorTests.cpp (after FxSharedTests.inc).

#include "FxRack.h"
#include "FxRouting.h"
#include "engine/fx/Effects.h"

namespace
{
    std::unique_ptr<juce::XmlElement> fxStateOf (OspAudioProcessor& p)
    {
        juce::MemoryBlock block;
        p.getStateInformation (block);
        return juce::AudioProcessor::getXmlFromBinary (block.getData(), static_cast<int> (block.getSize()));
    }

    void fxRestore (OspAudioProcessor& p, const juce::XmlElement& xml)
    {
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (xml, block);
        p.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
    }

    osp::fx::Graph fxCustom (std::vector<osp::fx::Node> chain)
    {
        osp::fx::Graph g;
        g.custom = true;
        g.chain = std::move (chain);
        return g;
    }
}

TEST_CASE ("fx osp: a CLASSIC session saves no route; the FX parameters exist and default", "[plugin][fx]")
{
    OspAudioProcessor p;
    CHECK (p.hasFxLibrary());
    CHECK_FALSE (p.fxRoute().custom);
    const auto xml = fxStateOf (p);
    REQUIRE (xml != nullptr);
    CHECK (xml->getChildByName ("FxRouting") == nullptr);
    CHECK (p.parameters.getParameter ("fx.eq.1.on") != nullptr);
    CHECK (p.parameters.getParameter ("fx.retro.2.mix") != nullptr);
    CHECK (p.parameters.getParameter ("analog.fx.eq.1.on") == nullptr);
}

TEST_CASE ("fx osp: a CUSTOM route survives the session; absent or unreadable is CLASSIC", "[plugin][fx]")
{
    using namespace osp::fx;
    OspAudioProcessor p;
    const auto route = fxCustom ({ Node::effect ({ Type::eq, 0 }), Node::effect ({ Type::drive, 0 }),
                                   Node::parallel ({ Node::effect ({ Type::echo, 0 }) }, { Node::effect ({ Type::space, 0 }) }, Law::send) });
    REQUIRE (p.setFxRoute (route));
    auto xml = fxStateOf (p);
    REQUIRE (xml != nullptr);
    const auto* saved = xml->getChildByName ("FxRouting");
    REQUIRE (saved != nullptr);
    CHECK (saved->getStringAttribute ("graph").toStdString() == toText (route));

    OspAudioProcessor q;
    fxRestore (q, *xml);
    CHECK (q.fxRoute() == route);

    // A session from before (no FxRouting) puts the route back to CLASSIC.
    xml->removeChildElement (xml->getChildByName ("FxRouting"), true);
    fxRestore (q, *xml);
    CHECK_FALSE (q.fxRoute().custom);

    // An unreadable route: CLASSIC, never a refusal to open.
    auto* bad = xml->createNewChildElement ("FxRouting");
    bad->setAttribute ("schema", 1);
    bad->setAttribute ("graph", "v1 custom fx nothing.1 on");
    REQUIRE (q.setFxRoute (route));
    fxRestore (q, *xml);
    CHECK_FALSE (q.fxRoute().custom);
}

TEST_CASE ("fx osp: a CUSTOM route plays the new effects; RETRO's latency reaches the host", "[plugin][fx]")
{
    using namespace osp::fx;
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    OspAudioProcessor p;
    const auto file = writeSource (tmp.dir, "pluck.wav", testsignals::pluck (220.0, 1.5, 48000.0, 4));
    juce::Array<juce::File> list;
    list.add (file);
    p.addLayers (list);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.setPlayConfigDetails (0, 2, 48000.0, 256);
    p.prepareToPlay (48000.0, 256);
    CHECK (p.getLatencySamples() == 0);
    REQUIRE (p.setFxRoute (fxCustom ({ Node::effect ({ Type::eq, 0 }), Node::effect ({ Type::retro, 0 }), Node::effect ({ Type::space, 0 }) })));
    juce::AudioBuffer<float> buffer (2, 256);
    double energy = 0.0;
    for (int b = 0; b < 200; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.8f), 0);
        buffer.clear();
        p.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
            {
                REQUIRE (std::isfinite (buffer.getSample (ch, i)));
                energy += static_cast<double> (buffer.getSample (ch, i)) * buffer.getSample (ch, i);
            }
    }
    CHECK (energy > 1.0e-3);
    CHECK (p.fxProcessor ({ Type::retro, 0 }) != nullptr);
    p.pollLoads();   // (the timer: the host hears of the route's latency)
    CHECK (p.getLatencySamples() == Retro::frameFor (48000.0));
    REQUIRE (p.setFxRoute (classicGraph()));
    juce::MidiBuffer none;
    p.processBlock (buffer, none);
    p.pollLoads();
    CHECK (p.getLatencySamples() == 0);
    p.releaseResources();
}

TEST_CASE ("fx osp: the FX rack opens below the sources and closes back to the same window", "[plugin][fx]")
{
    using Editor = osp::plugin::OspAudioProcessorEditor;
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<Editor*> (e.get());
    REQUIRE (ui != nullptr);
    const auto closedHeight = ui->instrumentHeight();
    const auto closedSize = e->getBounds();
    CHECK (ui->hasRack (osp::plugin::rack::Kind::fx));
    CHECK (ui->hasRack (osp::plugin::rack::Kind::space));
    CHECK_FALSE (ui->hasRack (osp::plugin::rack::Kind::filter));
    // SPACE, ECHO and MOVEMENT open their own racks (as in ANDOR ANALOG), in their enamel.
    for (const auto type : { osp::fx::Type::space, osp::fx::Type::echo, osp::fx::Type::movement })
    {
        osp::plugin::openFxRack (*ui, { type, 0 });
        CHECK (ui->instrumentHeight() > closedHeight);
    }
    CHECK (p.rackFaceplate (static_cast<int> (osp::plugin::rack::Kind::space)) != nullptr);
    for (const auto kind : { osp::plugin::rack::Kind::space, osp::plugin::rack::Kind::echo, osp::plugin::rack::Kind::movement })
        ui->setRackExpanded (kind, false);
    CHECK (ui->instrumentHeight() == closedHeight);
    osp::plugin::openFxRack (*ui, { osp::fx::Type::eq, 0 });
    CHECK (ui->effectsRack().isShown (osp::plugin::rack::Kind::fx));
    CHECK (ui->instrumentHeight() > closedHeight);
    CHECK (osp::plugin::fxRackShown() == osp::fx::InstanceId { osp::fx::Type::eq, 0 });
    // Another effect swaps the module; the rack keeps its height.
    const auto openHeight = ui->instrumentHeight();
    osp::plugin::openFxRack (*ui, { osp::fx::Type::retro, 1 });
    CHECK (ui->instrumentHeight() == openHeight);
    ui->setRackExpanded (osp::plugin::rack::Kind::fx, false);
    CHECK (ui->instrumentHeight() == closedHeight);
    CHECK (e->getBounds() == closedSize);
}

TEST_CASE ("fx osp: the routing key shows the routing view in the cards' place, and back", "[plugin][fx][routing]")
{
    using Editor = osp::plugin::OspAudioProcessorEditor;
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<Editor*> (e.get());
    REQUIRE (ui != nullptr);
    juce::Array<juce::File> list;
    list.add (writeSource (tmp.dir, "pad.wav", testsignals::vowel (midiToHz (60), 1.5, 48000.0, 3)));
    list.add (writeSource (tmp.dir, "pluck.wav", testsignals::pluck (220.0, 1.5, 48000.0, 4)));
    p.addLayers (list);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    ui->refreshNow();
    const auto height = ui->instrumentHeight();
    // The key sits beside SAVE, on its centre line.
    auto& key = ui->sourceViewToggle();
    CHECK (key.isVisible());
    CHECK_FALSE (ui->sourceViewShown());
    key.onClick();   // (a click: triggerClick would post it)
    REQUIRE (ui->sourceViewShown());
    auto* view = dynamic_cast<osp::plugin::FxRoutingView*> (ui->sourceViewComponent());
    REQUIRE (view != nullptr);
    CHECK (view->isVisible());
    CHECK_FALSE (ui->engineCard (0).isVisible());
    CHECK (ui->instrumentHeight() == height);   // (a view of the same area: the window keeps its size)
    // The ENGINE MIXER: one row per loaded layer, each knob the layer's own LEVEL.
    REQUIRE (view->mixerRowCount() == 2);
    auto* levelB = view->levelKnob (1);
    REQUIRE (levelB != nullptr);
    levelB->setValue (-6.0, juce::sendNotificationSync);
    CHECK (p.parameterValue ("layerB.level") == Catch::Approx (-6.0f).margin (0.05f));
    // CLASSIC to start; an edit makes it CUSTOM, as one undoable step.
    CHECK_FALSE (p.fxRoute().custom);
    REQUIRE (view->insertNew (osp::fx::Type::eq));
    CHECK (p.fxRoute().custom);
    CHECK (osp::fx::contains (p.fxRoute(), { osp::fx::Type::eq, 0 }));
    p.undo().undo();
    CHECK_FALSE (osp::fx::contains (p.fxRoute(), { osp::fx::Type::eq, 0 }));
    view->restoreOriginal();
    CHECK_FALSE (p.fxRoute().custom);
    // A double-click's editor: SPACE opens its own rack below the sources.
    view->openEditor ({ osp::fx::Type::space, 0 });
    CHECK (ui->effectsRack().isShown (osp::plugin::rack::Kind::space));
    ui->setRackExpanded (osp::plugin::rack::Kind::space, false);
    // Back to the cards (the key again, or Esc).
    key.onClick();   // (a click: triggerClick would post it)
    CHECK_FALSE (ui->sourceViewShown());
    CHECK (ui->engineCard (0).isVisible());
    CHECK (ui->instrumentHeight() == height);
}

TEST_CASE ("fx osp: routing view and rack screenshots (OSP_FX_SHOTS)", "[.][fx-routing-shots]")
{
    // On demand: OSP_FX_SHOTS=<folder> osp_plugin_tests "[fx-routing-shots]". Two layers; the
    // routing view CLASSIC, then a CUSTOM chain; SPACE, ECHO and MOVEMENT racks.
    const char* dir = std::getenv ("OSP_FX_SHOTS");
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
    using Editor = osp::plugin::OspAudioProcessorEditor;
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<Editor*> (e.get());
    REQUIRE (ui != nullptr);
    juce::Array<juce::File> list;
    list.add (writeSource (tmp.dir, "Warm Pad.wav", testsignals::vowel (midiToHz (60), 2.4, 48000.0, 3)));
    list.add (writeSource (tmp.dir, "Glass Pluck.wav", testsignals::pluck (220.0, 2.4, 48000.0, 4)));
    p.addLayers (list);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.setPlayConfigDetails (0, 2, 48000.0, 256);
    p.prepareToPlay (48000.0, 256);
    auto shoot = [&] (const juce::String& name) {
        ui->refreshNow();
        save (e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f), name + ".png");
    };
    shoot ("routing-00-cards");
    ui->setSourceViewShown (true);
    shoot ("routing-01-classic");
    auto* view = dynamic_cast<osp::plugin::FxRoutingView*> (ui->sourceViewComponent());
    REQUIRE (view != nullptr);
    view->insertNew (osp::fx::Type::eq);
    view->insertNew (osp::fx::Type::compressor);
    view->insertNew (osp::fx::Type::retro);
    shoot ("routing-02-custom");
    view->showLibrary (true);
    shoot ("routing-03-library");
    view->showLibrary (false);
    view->openEditor ({ osp::fx::Type::space, 0 });
    shoot ("routing-04-space-rack");
    ui->setRackExpanded (osp::plugin::rack::Kind::space, false);
    view->openEditor ({ osp::fx::Type::echo, 0 });
    shoot ("routing-05-echo-rack");
    ui->setRackExpanded (osp::plugin::rack::Kind::echo, false);
    view->openEditor ({ osp::fx::Type::movement, 0 });
    shoot ("routing-06-movement-rack");
    ui->setRackExpanded (osp::plugin::rack::Kind::movement, false);
    view->openEditor ({ osp::fx::Type::compressor, 0 });
    shoot ("routing-07-compressor-rack");
    p.releaseResources();
}

TEST_CASE ("fx osp: FX rack screenshots for the design review (OSP_FX_SHOTS)", "[.][fx-shots]")
{
    // On demand: OSP_FX_SHOTS=<folder> osp_plugin_tests "[fx-shots]". OSP with one sound, the
    // rack closed (as today) and each new effect's rack open below the sources, 1x.
    const char* dir = std::getenv ("OSP_FX_SHOTS");
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
    using Editor = osp::plugin::OspAudioProcessorEditor;
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<Editor*> (e.get());
    REQUIRE (ui != nullptr);
    juce::Array<juce::File> list;
    list.add (pad);
    p.addLayers (list);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.setPlayConfigDetails (0, 2, 48000.0, 256);
    p.prepareToPlay (48000.0, 256);
    ui->refreshNow();
    const float scale = 1086.0f / e->getHeight();
    auto shoot = [&] (const juce::String& name) {
        ui->refreshNow();
        save (e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f), name + ".png");
    };
    juce::ignoreUnused (scale);
    shoot ("fx-00-closed");
    using osp::fx::Type;
    const std::vector<std::pair<Type, const char*>> effects { { Type::eq, "eq" }, { Type::compressor, "compressor" }, { Type::filter, "filter" },
                                                               { Type::spread, "spread" }, { Type::phaser, "phaser" }, { Type::flanger, "flanger" },
                                                               { Type::shifter, "shifter" }, { Type::convolution, "convolution" },
                                                               { Type::utility, "utility" }, { Type::retro, "retro" } };
    int n = 1;
    for (const auto& [type, key] : effects)
    {
        osp::plugin::openFxRack (*ui, { type, 0 });
        shoot (juce::String ("fx-") + juce::String (n++).paddedLeft ('0', 2) + "-" + key);
    }
    ui->setRackExpanded (osp::plugin::rack::Kind::fx, false);
    shoot ("fx-99-closed-again");
    p.releaseResources();
}
