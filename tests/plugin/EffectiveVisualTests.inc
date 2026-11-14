// Universal modulation-aware visual feedback (ANDOR OSP): a picture draws what the DSP plays.
// A control moved by modulation draws as the same control set by hand to that value; the knob
// keeps its stored value; nothing is written back. Included at the end of PluginProcessorTests.cpp.

#include "MacroPopups.h"
#include "engine/EffectiveValues.h"

namespace
{
    /** The pictures in `root` (the shared popovers' and racks' wells: component ID "picture"),
        drawn side by side - the knobs, which keep their stored values, left out. */
    juce::Image picturesOf (juce::Component& root)
    {
        std::vector<juce::Component*> found;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
            for (auto* child : c.getChildren())
            {
                if (child->getComponentID() == "picture" && child->isVisible())
                    found.push_back (child);
                walk (*child);
            }
        };
        walk (root);
        int width = 0, height = 0;
        for (auto* c : found)
        {
            width += c->getWidth();
            height = std::max (height, c->getHeight());
        }
        juce::Image out (juce::Image::ARGB, std::max (1, width), std::max (1, height), true);
        juce::Graphics g (out);
        int x = 0;
        for (auto* c : found)
        {
            g.drawImageAt (c->createComponentSnapshot (c->getLocalBounds(), true, 1.0f), x, 0);
            x += c->getWidth();
        }
        return out;
    }

    /** Pixels differing by more than `tolerance` in any channel. */
    int pixelsDiffering (const juce::Image& a, const juce::Image& b, int tolerance = 2)
    {
        if (a.getBounds() != b.getBounds())
            return a.getWidth() * a.getHeight() + 1;
        int n = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
            {
                const auto p = a.getPixelAt (x, y), q = b.getPixelAt (x, y);
                if (std::abs (p.getRed() - q.getRed()) > tolerance || std::abs (p.getGreen() - q.getGreen()) > tolerance
                    || std::abs (p.getBlue() - q.getBlue()) > tolerance || std::abs (p.getAlpha() - q.getAlpha()) > tolerance)
                    ++n;
            }
        return n;
    }

    /** The MOVEMENT popover's pictures as they stand now. */
    juce::Image movementPicture (OspAudioProcessor& p)
    {
        auto popup = osp::plugin::createMacroPopup (osp::plugin::MacroPopup::movement, p);
        const auto card = popup->cardSize();
        popup->setSize (card.x + 2 * osp::plugin::MiniPanel::shadowMargin, card.y + 2 * osp::plugin::MiniPanel::shadowMargin);
        return picturesOf (*popup);
    }

    void effectiveRun (OspAudioProcessor& p, int blocks)
    {
        juce::AudioBuffer<float> buffer (2, 256);
        for (int i = 0; i < blocks; ++i)
        {
            juce::MidiBuffer midi;
            if (i == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, static_cast<juce::uint8> (100)), 0);
            buffer.clear();
            p.processBlock (buffer, midi);
        }
    }
}

TEST_CASE ("effective values: the telemetry table - one block's value, its range over the frame, inactive when cleared", "[unit][effective]")
{
    osp::EffectiveValues t;
    osp::EffectiveValues::Reading r;
    CHECK_FALSE (t.read (3, r));   // (never published)
    t.beginBlock (256, 48000.0);
    t.publish (3, 20.0f);
    t.publish (3, 80.0f);
    REQUIRE (t.read (3, r));
    CHECK (r.value == 80.0f);
    CHECK (r.low == 20.0f);
    CHECK (r.high == 80.0f);
    // Two windows on (~33 ms each): the old span is forgotten.
    for (int i = 0; i < 20; ++i)
    {
        t.beginBlock (256, 48000.0);
        t.publish (3, 50.0f);
    }
    REQUIRE (t.read (3, r));
    CHECK (r.low == 50.0f);
    CHECK (r.high == 50.0f);
    t.clear (3);
    CHECK_FALSE (t.read (3, r));
    CHECK_FALSE (t.read (-1, r));
    CHECK_FALSE (t.read (osp::EffectiveValues::capacity, r));
}

TEST_CASE ("effective values: MOVEMENT SHAPER draws the depth that plays - modulated to 80 % it is the hand-set 80 %", "[plugin][effective][shaper]")
{
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const auto a = writeSource (tmp.dir, "first.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 3));
    // `motion` stored; `wheelDepth`: the mod wheel (held at full) on MOVEMENT, percent.
    auto rig = [&] (float motion, float wheelDepth) {
        auto p = std::make_unique<OspAudioProcessor>();
        p->loadFile (a, 0);
        REQUIRE (p->waitForLoads (30000));
        p->pollLoads();
        p->prepareToPlay (48000.0, 256);
        p->setParameterValue ("movement.mode", static_cast<float> (MovementMode::shaper));
        p->setParameterValue ("motion", motion);
        if (wheelDepth != 0.0f)
        {
            p->assignModulation (mod::Source::modWheel, mod::Dest::movement, wheelDepth);
            p->setScreenModWheel (1.0f);
        }
        effectiveRun (*p, 40);
        return p;
    };
    const auto byHand80 = rig (80.0f, 0.0f);
    const auto byHand20 = rig (20.0f, 0.0f);
    const auto modulated = rig (20.0f, 60.0f);

    // The engine plays 80 % (the stored 20 % plus the wheel's 60 %); nothing hand-set reports one.
    osp::plugin::InstrumentHost::EffectiveValue v;
    REQUIRE (modulated->effectiveValue ("motion", v));
    CHECK (v.value == Approx (80.0f).margin (0.01));
    CHECK_FALSE (byHand80->effectiveValue ("motion", v));
    // The knob keeps its stored value: no write-back, no automation.
    CHECK (modulated->parameters.getParameter ("motion")->convertFrom0to1 (modulated->parameters.getParameter ("motion")->getValue()) == Approx (20.0f));

    const auto hand80 = movementPicture (*byHand80), hand20 = movementPicture (*byHand20), mod80 = movementPicture (*modulated);
    REQUIRE (hand80.getWidth() > 10);
    INFO ("pixels differing: hand 80 vs hand 20 " << pixelsDiffering (hand80, hand20) << ", hand 80 vs modulated 80 " << pixelsDiffering (hand80, mod80));
    CHECK (pixelsDiffering (hand80, hand20) > 200);   // (depth changes the curve)
    CHECK (pixelsDiffering (hand80, mod80) == 0);     // the same geometry

    // An LFO sweeping it: the picture moves with it; the span it covers is reported.
    auto swept = rig (50.0f, 0.0f);
    swept->assignModulation (mod::Source::lfo1, mod::Dest::movement, 30.0f);
    float low = 1000.0f, high = -1000.0f;
    int pictureChanges = 0;
    auto last = movementPicture (*swept);
    for (int i = 0; i < 12; ++i)
    {
        effectiveRun (*swept, 13);
        REQUIRE (swept->effectiveValue ("motion", v));
        low = std::min (low, v.value);
        high = std::max (high, v.value);
        CHECK (v.low <= v.value);
        CHECK (v.high >= v.value);
        const auto now = movementPicture (*swept);
        pictureChanges += pixelsDiffering (now, last) > 0 ? 1 : 0;
        last = now;
    }
    INFO ("swept between " << low << " and " << high << ", picture changed " << pictureChanges << " times");
    CHECK (high - low > 20.0f);
    CHECK (low >= 0.0f);
    CHECK (high <= 100.0f);
    CHECK (pictureChanges >= 6);
}
