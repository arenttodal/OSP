// The popovers' common scale (PITCH, MOD, ADVANCED against the macro popovers): measured in
// reference px (design units) as the editor shows them. Hidden measurement and screenshots:
// [.][popover-scale] (OSP_POPOVER_SHOTS=<dir>). Included at the end of PluginProcessorTests.cpp.

#include <set>

namespace
{
    struct PopoverMeasure
    {
        juce::String name;
        juce::Rectangle<float> card;          ///< reference px
        float header = 0.0f;                  ///< title block height, reference px
        std::vector<juce::Rectangle<float>> knobs;   ///< every MiniKnob, reference px
    };

    /** Opens popup `which` in `ui` at the reference width and measures it. */
    PopoverMeasure measurePopover (osp::plugin::OspAudioProcessorEditor& ui, int which, const juce::String& name)
    {
        ui.openPopup (which);
        ui.refreshNow();
        PopoverMeasure m;
        m.name = name;
        auto* panel = ui.openPopupPanel();
        if (panel == nullptr)
            return m;
        const float scale = static_cast<float> (osp::plugin::design::width) / static_cast<float> (ui.getWidth());
        auto toRef = [&] (juce::Component& c, juce::Rectangle<int> r) { return ui.getLocalArea (&c, r).toFloat() * scale; };
        m.card = toRef (*panel, panel->card());
        m.header = static_cast<float> (panel->headerHeight()) / panel->unit();
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
            if (auto* k = dynamic_cast<osp::plugin::MiniKnob*> (&c))
                m.knobs.push_back (toRef (*k, k->getLocalBounds()));
            for (auto* child : c.getChildren())
                if (child->isVisible())
                    walk (*child);
        };
        walk (*panel);
        return m;
    }
} // namespace

TEST_CASE ("popovers: measured against the macro popovers (OSP_POPOVER_SHOTS)", "[.][popover-scale]")
{
    using Editor = osp::plugin::OspAudioProcessorEditor;
    const auto dirName = juce::SystemStats::getEnvironmentVariable ("OSP_POPOVER_SHOTS", {});
    const juce::File dir (dirName);
    if (dirName.isNotEmpty())
        dir.createDirectory();
    for (const int width : { 1448, 1086, 869 })
    {
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto& ui = dynamic_cast<Editor&> (*e);
        e->setSize (width, juce::roundToInt (ui.instrumentHeight() * static_cast<float> (width) / osp::plugin::design::width));
        ui.refreshNow();
        const std::vector<std::pair<int, juce::String>> popups {
            { 0, "LIFE" }, { 1, "DRIVE" }, { 2, "FILTER" }, { 3, "MOVEMENT" }, { Editor::echoPopup, "ECHO" }, { 4, "SPACE" },
            { Editor::reimaginedPopup, "REIMAGINED" }, { Editor::advancedPopup, "ADVANCED" },
            { Editor::pitchWheelPopup, "PITCH" }, { Editor::modWheelPopup, "MOD" } };
        for (const auto& [which, name] : popups)
        {
            const auto m = measurePopover (ui, which, name);
            float knobW = 0.0f, knobH = 0.0f;
            for (const auto& k : m.knobs)
            {
                knobW = std::max (knobW, k.getWidth());
                knobH = std::max (knobH, k.getHeight());
            }
            if (width == 1448)
                std::cout << name << ": card " << juce::roundToInt (m.card.getWidth()) << " x " << juce::roundToInt (m.card.getHeight())
                          << ", header " << juce::roundToInt (m.header) << ", knobs " << m.knobs.size() << " (largest " << juce::roundToInt (knobW) << " x "
                          << juce::roundToInt (knobH) << ")" << std::endl;
            if (dirName.isNotEmpty())
            {
                const auto image = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream out (dir.getChildFile (juce::String (width) + "-" + name + ".png"));
                out.setPosition (0);
                out.truncate();
                juce::PNGImageFormat().writeImageToStream (image, out);
            }
            ui.closePopup();
        }
    }
}

TEST_CASE ("popovers: PITCH BEND, MOD WHEEL and ADVANCED at the macro popovers' scale, every control kept, at every size", "[plugin][ui][popover-scale]")
{
    using Editor = osp::plugin::OspAudioProcessorEditor;
    namespace pop = osp::plugin::popover;
    for (const int width : { 869, 1086, 1448, 1810 })
    {
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto& ui = dynamic_cast<Editor&> (*e);
        e->setSize (width, juce::roundToInt (ui.instrumentHeight() * static_cast<float> (width) / osp::plugin::design::width));
        ui.refreshNow();
        const auto life = measurePopover (ui, 0, "LIFE");
        ui.closePopup();
        for (const auto& [which, name] : { std::pair<int, const char*> { Editor::pitchWheelPopup, "PITCH" }, { Editor::modWheelPopup, "MOD" },
                                           { Editor::advancedPopup, "ADVANCED" } })
        {
            INFO (width << " px, " << name);
            const auto m = measurePopover (ui, which, name);
            auto* panel = ui.openPopupPanel();
            REQUIRE (panel != nullptr);
            CHECK (panel->compact());   // (the macro popovers' card: small title, no window chrome)
            // The macro family's scale: as wide as SPACE plus a little, never the old 436 .. 610.
            CHECK (m.card.getWidth() <= 345.0f);
            CHECK (m.card.getHeight() <= 300.0f);
            CHECK (m.card.getWidth() >= life.card.getWidth() - 1.0f);
            // Knobs: the macro popovers' cells.
            REQUIRE (! m.knobs.empty());
            for (const auto& k : m.knobs)
            {
                CHECK (std::abs (k.getHeight() - static_cast<float> (pop::cellHeight)) < 2.5f);   // (rounding at fractional scales)
                CHECK (k.getWidth() <= static_cast<float> (pop::maxCellWidth) + 2.5f);
            }
            // Inside the window and the card; no two controls overlap.
            const auto card = ui.getLocalArea (panel, panel->card());
            CHECK (ui.getLocalBounds().contains (card));
            std::vector<juce::Rectangle<int>> boxes;
            for (auto* child : panel->getChildren())
                if (child->isVisible())
                {
                    CHECK (panel->card().contains (child->getBounds()));
                    boxes.push_back (child->getBounds());
                }
            for (std::size_t a = 0; a < boxes.size(); ++a)
                for (std::size_t b = a + 1; b < boxes.size(); ++b)
                    CHECK_FALSE (boxes[a].intersects (boxes[b]));
            // Every control.
            std::set<juce::String> captions, tags;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
                if (auto* k = dynamic_cast<osp::plugin::MiniKnob*> (&c))
                    captions.insert (k->captionText());
                for (const auto& t : osp::plugin::binding::tagsOf (c))
                    tags.insert (t);
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (*panel);
            if (which == Editor::pitchWheelPopup)
                CHECK (captions == std::set<juce::String> { "RANGE" });
            else if (which == Editor::modWheelPopup)
            {
                CHECK (captions == std::set<juce::String> { "DEPTH", "RATE", "DELAY", "RISE", "AMOUNT" });
                for (const char* id : { "vibrato.enabled", "vibrato.control", "vibrato.sourceA", "vibrato.sourceB", "vibrato.sourceC" })
                    CHECK (tags.count (id) == 1);
            }
            else
            {
                CHECK (captions == std::set<juce::String> { "FINE", "GLIDE", "AMOUNT", "RANGE", "TONE" });
                CHECK (captions.count ("BEND") == 0);   // (the bend's RANGE lives under PITCH)
                for (const char* id : { "voiceMode", "pitchCharacter", "dynamics.curve", "mpe", "seed" })
                    CHECK (tags.count (id) == 1);
            }
            // Clear of what opens it: the wheels stay in view, ADVANCED sits above ARP / SUB.
            if (which == Editor::advancedPopup)
                CHECK (card.getBottom() <= ui.getLocalArea (&ui.arpControl(), ui.arpControl().getLocalBounds()).getY());
            ui.closePopup();
        }
    }
}

TEST_CASE ("learn: the vibrato lesson, end to end, from the musician's own actions", "[plugin][learn][wheels]")
{
    LearnRig rig;
    REQUIRE (rig.learn.startLesson ("osp.vibrato"));
    rig.tick();
    CHECK (rig.stepId() == "setup");
    // The example (a held, bowed note): loaded only because it is chosen here.
    rig.learn.noteAction ("example");
    rig.waitLoaded();
    rig.tick();
    rig.learn.noteAction ("current");
    rig.tick();
    REQUIRE (rig.stepId() == "open");
    // The highlight is the MOD caption itself, not the keyboard's whole section.
    auto& content = *rig.learn.highlight()->getParentComponent();
    const auto ring = rig.learn.targets().boundsIn ("osp.wheel.mod", content);
    REQUIRE (ring.has_value());
    CHECK (ring->getHeight() < 40);   // (the caption and the outline's padding)
    rig.ui.modWheelControl().onCaptionClick();
    rig.tick();
    REQUIRE (rig.stepDone());
    rig.next();
    CHECK (rig.stepId() == "on");
    rig.userSet ("vibrato.enabled", 1.0f);
    REQUIRE (rig.stepDone());
    rig.next();
    CHECK (rig.stepId() == "control");
    // MOD WHEEL is the default: the step is already so, and offers Continue (no Skip).
    CHECK (rig.learn.note()->actionIds().contains ("continue"));
    rig.learn.noteAction ("continue");
    rig.tick();
    CHECK (rig.stepId() == "depth");
    rig.automate ("vibrato.depth", 40.0f);   // (automation is not the musician)
    CHECK_FALSE (rig.stepDone());
    rig.userSet ("vibrato.depth", 20.0f);
    REQUIRE (rig.stepDone());
    rig.next();
    CHECK (rig.stepId() == "rate");
    rig.userSet ("vibrato.rate", 6.0f);
    REQUIRE (rig.stepDone());
    rig.next();
    CHECK (rig.stepId() == "play");
    rig.playNote();
    CHECK_FALSE (rig.stepDone());   // (the wheel still down)
    {
        rig.p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer up;
        up.addEvent (juce::MidiMessage::controllerEvent (1, 1, 100), 0);
        rig.p.processBlock (buffer, up);
    }
    rig.playNote();
    REQUIRE (rig.stepDone());
    rig.next();
    CHECK (rig.stepId() == "shape");
    rig.userSet ("vibrato.rise", 600.0f);
    REQUIRE (rig.stepDone());
    rig.next();
    CHECK (rig.stepId() == "sources");
    // The sources' keys are found in the popover.
    CHECK (rig.learn.targets().boundsIn ("osp.vibrato.sources", content).has_value());
}
