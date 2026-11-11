// The effects racks' approved ceramic controls (shared/andor-ui RackControls): bound to host
// parameters, two / three detents, labels, keyboard, undo, automation, accessibility.

#include "RackControls.h"

namespace
{
    juce::MouseEvent rackControlEvent (juce::Component& c, juce::Point<float> at)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (source, at, juce::ModifierKeys::leftButtonModifier, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c,
                                 now, at, now, 1, false);
    }
}

TEST_CASE ("rack controls: the two-position switch is the parameter - FREE above, SYNC below, keys, labels, undo", "[plugin][ui][rackcontrols]")
{
    using namespace osp::plugin::rackui;
    OspAudioProcessor p;
    auto& param = *p.parameters.getParameter ("echo.sync");
    param.setValueNotifyingHost (0.0f);   // FREE
    VerticalSwitch timing (param, { { "FREE", "Free", 0.0f }, { "SYNC", "Sync", 1.0f } }, &p.undo());
    const auto size = VerticalSwitch::preferredSize (2, VerticalSwitch::Style::slot);
    timing.setSize (size.x, size.y);
    CHECK (timing.positionIndex() == 0);
    // Approved geometry: a 20 x 61 slot, a 17 x 19 actuator at the top (FREE) or 39 px lower.
    CHECK (timing.trackBounds().getWidth() == 20.0f);
    CHECK (timing.trackBounds().getHeight() == 61.0f);
    CHECK (timing.actuatorBounds (1).getY() - timing.actuatorBounds (0).getY() == 39.0f);
    CHECK (timing.labelBounds (0).getBottom() <= timing.trackBounds().getY());   // FREE above
    CHECK (timing.labelBounds (1).getY() >= timing.trackBounds().getBottom());   // SYNC below

    // The host moves it: it follows (no gesture of its own).
    param.setValueNotifyingHost (1.0f);
    CHECK (timing.positionIndex() == 1);
    // A click on a label selects that position; the parameter follows at once.
    const auto free = timing.labelBounds (0).getCentre();
    timing.mouseDown (rackControlEvent (timing, free));
    CHECK (param.getValue() == Catch::Approx (0.0f));
    CHECK (timing.positionIndex() == 0);
    // Keys: down, up, End, Home; never past the ends.
    timing.keyPressed (juce::KeyPress (juce::KeyPress::downKey));
    CHECK (param.getValue() == Catch::Approx (1.0f));
    timing.keyPressed (juce::KeyPress (juce::KeyPress::downKey));
    CHECK (timing.positionIndex() == 1);
    timing.keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
    CHECK (timing.positionIndex() == 0);
    // Undo: each choice its own transaction, named after the parameter (the APVTS records
    // the change itself on its timer).
    timing.choose (1);
    CHECK (param.getValue() == Catch::Approx (1.0f));
    CHECK (p.undo().getCurrentTransactionName() == param.getName (64));
    // Accessibility: its value spoken by the position's name, and set by it.
    timing.choose (0);
    auto handler = timing.createAccessibilityHandler();
    REQUIRE (handler != nullptr);
    REQUIRE (handler->getValueInterface() != nullptr);
    CHECK (handler->getValueInterface()->getCurrentValueAsString() == "Free");
    handler->getValueInterface()->setValueAsString ("Sync");
    CHECK (timing.positionIndex() == 1);
}

TEST_CASE ("rack controls: the three-position switch - MONO / PING / WIDE on the right, exactly three detents", "[plugin][ui][rackcontrols]")
{
    using namespace osp::plugin::rackui;
    OspAudioProcessor p;
    auto& param = *p.parameters.getParameter ("echo.stereo");
    VerticalSwitch stereo (param, { { "MONO", "Mono", 0.0f }, { "PING", "Ping-Pong", 1.0f }, { "WIDE", "Wide", 2.0f } }, &p.undo());
    const auto size = VerticalSwitch::preferredSize (3, VerticalSwitch::Style::slot);
    stereo.setSize (size.x, size.y);
    for (int i = 0; i < 3; ++i)
    {
        INFO (i);
        CHECK (stereo.labelBounds (i).getX() >= stereo.trackBounds().getRight() + 8.0f);   // labels to the right
        if (i > 0)
            CHECK (stereo.labelBounds (i).getY() > stereo.labelBounds (i - 1).getY());       // top to bottom
    }
    CHECK (stereo.actuatorBounds (1).getY() - stereo.actuatorBounds (0).getY() == Catch::Approx (19.5f));
    // A drag anywhere snaps to a detent: never between.
    const auto t = stereo.trackBounds();
    for (const float y : { 0.1f, 0.3f, 0.45f, 0.62f, 0.9f })
    {
        stereo.mouseDrag (rackControlEvent (stereo, { t.getCentreX(), t.getY() + y * t.getHeight() }));
        const float plain = param.convertFrom0to1 (param.getValue());
        CHECK (plain == Catch::Approx (std::round (plain)));
        CHECK (stereo.positionIndex() == juce::roundToInt (plain));
    }
    // Every label selects its own position.
    for (int i = 0; i < 3; ++i)
    {
        stereo.mouseDown (rackControlEvent (stereo, stereo.labelBounds (i).getCentre()));
        CHECK (juce::roundToInt (param.convertFrom0to1 (param.getValue())) == i);
    }
    stereo.choose (1);
    auto handler = stereo.createAccessibilityHandler();
    REQUIRE (handler != nullptr);
    REQUIRE (handler->getValueInterface() != nullptr);
    CHECK (handler->getValueInterface()->getCurrentValueAsString() == "Ping-Pong");
}

TEST_CASE ("rack controls: the segmented switch and the header's mini switch follow and set their parameters", "[plugin][ui][rackcontrols]")
{
    using namespace osp::plugin::rackui;
    OspAudioProcessor p;
    auto& target = *p.parameters.getParameter ("movement.shaper.target");
    SegmentedSwitch seg (target, { { "VOL", "Volume", 0.0f }, { "FILTER", "Filter", 1.0f }, { "BOTH", "Both", 2.0f } }, &p.undo(), juce::Colour (0xff7b9db6));
    seg.setSize (119, 27);
    for (int i = 0; i < 3; ++i)
    {
        seg.mouseDown (rackControlEvent (seg, seg.segmentBounds (i).getCentre()));
        CHECK (juce::roundToInt (target.convertFrom0to1 (target.getValue())) == i);
        CHECK (seg.positionIndex() == i);
    }
    seg.keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
    CHECK (seg.positionIndex() == 1);
    target.setValueNotifyingHost (target.convertTo0to1 (0.0f));   // automation
    CHECK (seg.positionIndex() == 0);
    // MOVEMENT's approved active tint, the same quietness in another hue.
    CHECK (palette::activeFill (juce::Colour (0xff7b9db6)) == palette::segmentActive);
    const auto echo = palette::activeFill (juce::Colour (0xff7caaa8));
    CHECK (echo.getSaturation() == Catch::Approx (palette::segmentActive.getSaturation()).margin (0.01));

    // The mini switch (the header's lane power): ON up, OFF down, a click throws it.
    auto& freeze = *p.parameters.getParameter ("space.abyss.freeze");
    freeze.setValueNotifyingHost (1.0f);
    VerticalSwitch mini (freeze, { { "ON", "On", 1.0f }, { "OFF", "Off", 0.0f } }, &p.undo(), VerticalSwitch::Style::mini);
    mini.setSize (16, 26);
    CHECK (mini.positionIndex() == 0);
    CHECK (mini.actuatorBounds (1).getY() - mini.actuatorBounds (0).getY() == 9.0f);
    mini.mouseDown (rackControlEvent (mini, { 8.0f, 13.0f }));
    CHECK (freeze.getValue() == Catch::Approx (0.0f));
    mini.mouseDown (rackControlEvent (mini, { 8.0f, 13.0f }));
    CHECK (freeze.getValue() == Catch::Approx (1.0f));
}

TEST_CASE ("rack controls: drawing them never changes a parameter", "[plugin][ui][rackcontrols]")
{
    using namespace osp::plugin::rackui;
    OspAudioProcessor p;
    auto& param = *p.parameters.getParameter ("echo.stereo");
    const float before = param.getValue();
    VerticalSwitch stereo (param, { { "MONO", "Mono", 0.0f }, { "PING", "Ping-Pong", 1.0f }, { "WIDE", "Wide", 2.0f } }, &p.undo());
    stereo.setSize (66, 79);
    SegmentedSwitch seg (*p.parameters.getParameter ("movement.shaper.target"), { { "VOL", {}, 0.0f }, { "FILTER", {}, 1.0f }, { "BOTH", {}, 2.0f } }, &p.undo(),
                         juce::Colour (0xffa995b1));
    seg.setSize (119, 27);
    LaneKey key ("A");
    key.setSize (LaneKey::width, LaneKey::height);
    const auto undoSteps = p.undo().getNumActionsInCurrentTransaction();
    for (int i = 0; i < 3; ++i)
    {
        juce::ignoreUnused (stereo.createComponentSnapshot (stereo.getLocalBounds()));
        juce::ignoreUnused (seg.createComponentSnapshot (seg.getLocalBounds()));
        juce::ignoreUnused (key.createComponentSnapshot (key.getLocalBounds()));
    }
    CHECK (param.getValue() == before);
    CHECK (p.undo().getNumActionsInCurrentTransaction() == undoSteps);
}

TEST_CASE ("rack controls: a sheet of the approved controls, to compare with the approved images (OSP_SCREENSHOT_DIR)", "[.][screenshots][rackcontrols-shots]")
{
    using namespace osp::plugin::rackui;
    const auto* dir = std::getenv ("OSP_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    OspAudioProcessor p;
    p.parameters.getParameter ("echo.sync")->setValueNotifyingHost (1.0f);
    struct Sheet final : juce::Component
    {
        void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xfff2ede4)); }
    } sheet;
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    osp::plugin::OspLookAndFeel look;
    sheet.setLookAndFeel (&look);
    sheet.setSize (720, 150);
    VerticalSwitch timing (*p.parameters.getParameter ("echo.sync"), { { "FREE", {}, 0.0f }, { "SYNC", {}, 1.0f } }, &p.undo());
    VerticalSwitch stereo (*p.parameters.getParameter ("echo.stereo"), { { "MONO", {}, 0.0f }, { "PING", {}, 1.0f }, { "WIDE", {}, 2.0f } }, &p.undo());
    SegmentedSwitch target (*p.parameters.getParameter ("movement.shaper.target"), { { "VOL", {}, 0.0f }, { "FILTER", {}, 1.0f }, { "BOTH", {}, 2.0f } }, &p.undo(),
                            juce::Colour (0xff7b9db6));
    VerticalSwitch mini (*p.parameters.getParameter ("space.abyss.freeze"), { { "ON", {}, 1.0f }, { "OFF", {}, 0.0f } }, &p.undo(), VerticalSwitch::Style::mini);
    LaneKey a ("A"), b ("B");
    a.setToggleState (true, juce::dontSendNotification);
    b.lit = [] { return false; };
    osp::plugin::MiniKnob rate (p.parameters, "movement.shaper.rate", "RATE", [&p] (double v) { return p.parameters.getParameter ("movement.shaper.rate")->getText (static_cast<float> (v) / 5.0f, 32); });
    rate.setRackMetrics (true);
    rate.setDetents (true, juce::Colour (0xff7b9db6));
    rate.setArcColour (juce::Colour (0xff7b9db6));
    osp::plugin::MiniKnob depth (p.parameters, "space.decay", "DECAY", [] (double v) { return juce::String (v, 1) + " s"; });
    depth.setRackMetrics (true);
    depth.setArcColour (juce::Colour (0xffa995b1));
    for (juce::Component* c : std::initializer_list<juce::Component*> { &timing, &stereo, &target, &mini, &a, &b, &rate, &depth })
        sheet.addAndMakeVisible (c);
    timing.setBounds (20, 30, 95, 83);
    stereo.setBounds (122, 32, 119, 79);
    target.setBounds (250, 58, 119, 27);
    a.setBounds (390, 20, LaneKey::width, LaneKey::height);
    b.setBounds (432, 20, LaneKey::width, LaneKey::height);
    mini.setBounds (490, 18, 16, 26);
    rate.setBounds (520, 20, 104, 104);
    depth.setBounds (612, 20, 104, 104);
    const auto image = sheet.createComponentSnapshot (sheet.getLocalBounds(), true, 2.0f);
    const auto file = juce::File (dir).getChildFile ("rack-controls-sheet.png");
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat().writeImageToStream (image, out);
    sheet.setLookAndFeel (nullptr);
    osp::plugin::skin::setLook (osp::plugin::skin::Look::classic);
}
