// ANDOR Learn in ANDOR OSP's own plugin (apps/plugin/Source/OspLearn.cpp): the shared
// controller on OspAudioProcessorEditor, OSP's lessons, targets, readiness, examples and
// progress. Included at the end of PluginProcessorTests.cpp.

#include "LearnController.h"
#include "TutorialSounds.h"

namespace
{
    void setEnv (const char* name, const juce::String& value)
    {
#if JUCE_WINDOWS
        _putenv_s (name, value.toRawUTF8());
#else
        setenv (name, value.toRawUTF8(), 1);
#endif
    }

    /** Learn's progress and the tutorial sounds in a folder of their own (never the user's). */
    struct ScopedLearnFolders
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("osp-learn-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        juce::String learnBefore = juce::SystemStats::getEnvironmentVariable ("ANDOR_LEARN_DIR", {});
        juce::String tutorialBefore = juce::SystemStats::getEnvironmentVariable ("OSP_TUTORIAL_DIR", {});
        ScopedLearnFolders()
        {
            dir.createDirectory();
            setEnv ("ANDOR_LEARN_DIR", dir.getChildFile ("learn").getFullPathName());
            setEnv ("OSP_TUTORIAL_DIR", dir.getChildFile ("sounds").getFullPathName());
        }
        ~ScopedLearnFolders()
        {
            setEnv ("ANDOR_LEARN_DIR", learnBefore);
            setEnv ("OSP_TUTORIAL_DIR", tutorialBefore);
            dir.deleteRecursively();
        }
    };

    using OspEditor = osp::plugin::OspAudioProcessorEditor;
    using LearnNote = andor::learn::LearnController::NoteKind;

    struct LearnRig
    {
        ScopedLearnFolders folders;
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor { p.createEditorIfNeeded() };
        OspEditor& ui = dynamic_cast<OspEditor&> (*editor);
        andor::learn::LearnController& learn = *ui.learnController();

        explicit LearnRig (int width = 1086)
        {
            editor->setSize (width, juce::roundToInt (ui.instrumentHeight() * static_cast<float> (width) / osp::plugin::design::width));
            ui.refreshNow();
        }
        void tick() { ui.refreshNow(); }
        /** A knob turned by the musician: the gesture the editor's attachment makes. */
        void userSet (const juce::String& id, float value)
        {
            auto* param = p.parameters.getParameter (id);
            REQUIRE (param != nullptr);
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (value));
            param->endChangeGesture();
            tick();
        }
        /** The host (or a preset) setting it: no gesture. */
        void automate (const juce::String& id, float value)
        {
            auto* param = p.parameters.getParameter (id);
            REQUIRE (param != nullptr);
            param->setValueNotifyingHost (param->convertTo0to1 (value));
            tick();
        }
        void waitLoaded()
        {
            REQUIRE (p.waitForLoads (30000));
            p.pollLoads();
            tick();
        }
        void playNote (int note = 60)
        {
            p.prepareToPlay (48000.0, 256);
            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer on;
            on.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)), 0);
            p.processBlock (buffer, on);
            juce::MidiBuffer off;
            off.addEvent (juce::MidiMessage::noteOff (1, note), 0);
            p.processBlock (buffer, off);
            tick();
        }
        juce::String stepId() const
        {
            const auto* s = learn.runner().step();
            return s != nullptr ? s->id : juce::String();
        }
        bool stepDone() const { return learn.runner().state() == andor::learn::LessonRunner::State::stepComplete; }
        void next()
        {
            REQUIRE (learn.note() != nullptr);
            learn.noteAction ("next");
            tick();
        }
        std::map<juce::String, float> parameterValues()
        {
            std::map<juce::String, float> out;
            for (auto* parameter : p.getParameters())
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
                    out[withId->paramID] = parameter->getValue();
            return out;
        }
    };

    bool menuHas (const juce::PopupMenu& menu, const juce::String& text)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            if (it.getItem().text == text)
                return true;
        return false;
    }
} // namespace

TEST_CASE ("learn: OSP's own plugin has the shared Learn, idle until asked; its catalog is valid", "[plugin][learn]")
{
    LearnRig rig;
    REQUIRE (rig.ui.learnController() != nullptr);
    INFO (rig.learn.problems().joinIntoString ("\n"));
    CHECK (rig.learn.isUsable());
    CHECK (rig.learn.catalog().productId == "osp");
    CHECK (rig.learn.catalog().lessons.size() == 11);
    CHECK (rig.learn.catalog().help.size() >= 30);
    // Idle: no note, no browser, no ring (nothing of Learn's in the editor).
    CHECK_FALSE (rig.learn.isActive());
    CHECK (rig.learn.note() == nullptr);
    CHECK (rig.learn.highlight() == nullptr);
    // The menu: LEARN, Tutorials..., Toggle Info, Reset Tutorials... (no old labels).
    juce::PopupMenu menu;
    rig.learn.addMenuItems (menu);
    const auto dots = juce::String::fromUTF8 ("\xe2\x80\xa6");
    CHECK (menuHas (menu, "Tutorials" + dots));
    CHECK (menuHas (menu, "Toggle Info"));
    CHECK (menuHas (menu, "Reset Tutorials" + dots));
    CHECK_FALSE (menuHas (menu, "Learn mode"));
    CHECK_FALSE (menuHas (menu, "Learn ANDOR" + dots));
    // The groups, in the browser's order.
    juce::StringArray groups;
    for (const auto& l : rig.learn.catalog().lessons)
        groups.addIfNotAlreadyThere (l.group);
    CHECK (groups == juce::StringArray ({ "Start here", "Working with recordings", "Playing and shaping", "Managing sounds" }));
}

TEST_CASE ("learn: Quick Start, end to end, only the musician's own actions complete it", "[plugin][learn]")
{
    LearnRig rig;
    REQUIRE (rig.learn.startLesson ("osp.quickstart"));
    rig.tick();
    // An empty instrument: the lesson starts at once, at the drop zone (no Card A pretended).
    REQUIRE (rig.learn.noteKind() == LearnNote::lesson);
    CHECK (rig.stepId() == "load");
    REQUIRE (rig.learn.highlight() != nullptr);
    REQUIRE (rig.learn.highlight()->target().has_value());
    // No Skip, anywhere.
    CHECK_FALSE (rig.learn.note()->actionIds().contains ("skip"));
    // A file arrives (the drop's own handler reports it), then loads: the step completes once
    // the recording is playable, not on the request.
    juce::String error;
    const auto files = osp::plugin::tutorial::files (osp::plugin::tutorial::Example::pluck, error);
    REQUIRE (files.size() == 1);
    auto& content = *rig.learn.highlight()->getParentComponent();
    const auto zone = rig.learn.targets().boundsIn ("osp.source.dropZone", content);
    REQUIRE (zone.has_value());
    const auto at = rig.ui.getLocalPoint (&content, zone->getCentre());
    rig.ui.filesDropped ({ files[0].getFullPathName() }, at.x, at.y);
    CHECK_FALSE (rig.stepDone());
    rig.waitLoaded();
    for (int i = 0; i < 3 && ! rig.stepDone(); ++i)
        rig.tick();
    REQUIRE (rig.stepDone());
    rig.next();
    CHECK (rig.stepId() == "play");
    rig.playNote();
    REQUIRE (rig.stepDone());
    rig.next();
    // START: automation and a preset recall are not the musician's change.
    CHECK (rig.stepId() == "start");
    rig.automate ("layerA.start", 30.0f);
    CHECK_FALSE (rig.stepDone());
    rig.userSet ("layerA.start", 10.0f);
    REQUIRE (rig.stepDone());
    rig.next();
    CHECK (rig.stepId() == "level");
    rig.userSet ("layerA.level", -12.0f);
    REQUIRE (rig.stepDone());
    rig.next();
    CHECK (rig.stepId() == "reimagined");
    rig.userSet ("reimagined", 40.0f);   // (layer A's REIMAGINED keeps its original ID)
    REQUIRE (rig.stepDone());
    rig.next();
    CHECK (rig.stepId() == "original");
    rig.userSet ("reimagined", 20.0f);
    CHECK_FALSE (rig.stepDone());
    rig.userSet ("reimagined", 0.0f);
    REQUIRE (rig.stepDone());
    rig.learn.noteAction ("finish");
    rig.tick();
    CHECK (rig.learn.noteKind() == LearnNote::complete);
    // Progress: complete, under OSP's own name.
    const auto progress = rig.learn.store().read();
    CHECK (progress.product == "osp");
    REQUIRE (progress.lessons.count ("osp.quickstart") == 1);
    CHECK (progress.lessons.at ("osp.quickstart").status == "complete");
    // The example was never involved: nothing to restore.
    CHECK_FALSE (rig.p.canRestoreBeforeTutorial());
    rig.learn.noteAction ("close");
    rig.tick();
    CHECK_FALSE (rig.learn.isActive());
}

TEST_CASE ("learn: a lesson that needs a sound asks first, loads the example only when chosen, and restores", "[plugin][learn]")
{
    LearnRig rig;
    // The musician's own state first: a recording and a setting of theirs.
    juce::String error;
    const auto mine = osp::plugin::tutorial::files (osp::plugin::tutorial::Example::voice, error);
    REQUIRE (mine.size() == 1);
    rig.p.loadFile (mine[0], 0);
    rig.waitLoaded();
    rig.automate ("space", 77.0f);
    rig.automate ("layerA.sourceMode", 1.0f);   // Granular: the multi-sample lesson still needs a set
    // The multi-sample lesson: this sound is not a set, so it asks (nothing changed yet).
    const auto before = rig.parameterValues();
    REQUIRE (rig.learn.startLesson ("osp.multisample"));
    rig.tick();
    REQUIRE (rig.learn.noteKind() == LearnNote::prepare);
    CHECK (rig.learn.note()->actionIds().contains ("loadexample"));
    CHECK (rig.parameterValues() == before);
    CHECK (rig.p.currentInstrument (0) != nullptr);
    // Not now: nothing loaded, nothing restorable.
    rig.learn.noteAction ("close");
    rig.tick();
    CHECK_FALSE (rig.p.canRestoreBeforeTutorial());
    CHECK (rig.parameterValues() == before);
    // Again, and Load: the whole state is kept first, the lesson waits until it can be played.
    REQUIRE (rig.learn.startLesson ("osp.multisample"));
    rig.tick();
    rig.learn.noteAction ("loadexample");
    CHECK (rig.p.canRestoreBeforeTutorial());   // (kept before anything was replaced)
    CHECK (rig.learn.noteKind() == LearnNote::preparing);
    rig.waitLoaded();
    for (int i = 0; i < 4 && rig.learn.noteKind() != LearnNote::lesson; ++i)
        rig.tick();
    REQUIRE (rig.learn.noteKind() == LearnNote::lesson);
    CHECK (rig.stepId() == "set");
    const auto inst = rig.p.currentInstrument (0);
    REQUIRE (inst != nullptr);
    REQUIRE (inst->set != nullptr);
    CHECK (inst->set->members.size() == 8);
    CHECK (rig.p.parameterValue ("space") != 77.0f);   // a clean instrument for the lesson
    // Closing offers the restore (never silently): restoring brings the musician's sound back.
    rig.learn.noteAction ("close");
    rig.tick();
    REQUIRE (rig.learn.noteKind() == LearnNote::restoreOffer);
    rig.learn.noteAction ("restore");
    rig.waitLoaded();
    CHECK (rig.p.parameterValue ("space") == Catch::Approx (77.0f).margin (0.01));
    CHECK (rig.p.parameterValue ("layerA.sourceMode") == Catch::Approx (1.0f));
    REQUIRE (rig.p.currentInstrument (0) != nullptr);
    CHECK (rig.p.currentInstrument (0)->set == nullptr);
    CHECK_FALSE (rig.learn.isActive());
}

TEST_CASE ("learn: every lesson's navigation (Show me, Back, Continue, X) never changes the sound", "[plugin][learn]")
{
    LearnRig rig;
    juce::String error;
    const auto files = osp::plugin::tutorial::files (osp::plugin::tutorial::Example::bowed, error);
    REQUIRE (files.size() == 1);
    rig.p.loadFile (files[0], 0);
    rig.waitLoaded();
    const auto before = rig.parameterValues();
    for (const auto& lesson : rig.learn.catalog().lessons)
    {
        INFO (lesson.id);
        REQUIRE (rig.learn.startLesson (lesson.id));
        rig.tick();
        if (rig.learn.noteKind() == LearnNote::prepare)
        {
            if (rig.learn.note()->actionIds().contains ("usemine"))
                rig.learn.noteAction ("usemine");
            else
            {
                rig.learn.noteAction ("close");   // (needs another sound: not loaded here)
                continue;
            }
            rig.tick();
        }
        REQUIRE (rig.learn.noteKind() == LearnNote::lesson);
        for (int s = 0; s < static_cast<int> (lesson.steps.size()); ++s)
        {
            auto ids = rig.learn.note() != nullptr ? rig.learn.note()->actionIds() : juce::StringArray();
            CHECK_FALSE (ids.contains ("skip"));
            if (ids.contains ("showme"))
                rig.learn.noteAction ("showme");
            rig.tick();
            ids = rig.learn.note() != nullptr ? rig.learn.note()->actionIds() : juce::StringArray();
            if (ids.contains ("next"))
                rig.learn.noteAction ("next");
            else if (ids.contains ("back") && s > 0)
                rig.learn.noteAction ("back");
            rig.tick();
        }
        rig.learn.noteAction ("close");
        rig.tick();
        if (rig.learn.noteKind() == LearnNote::restoreOffer)
            rig.learn.noteAction ("keep");
        rig.ui.closePopup();
        rig.tick();
    }
    CHECK (rig.parameterValues() == before);
    CHECK_FALSE (rig.p.canRestoreBeforeTutorial());
}

TEST_CASE ("learn: the ring is the real control and the note stays clear of it, at 869, 1086 and 1448 px", "[plugin][learn]")
{
    for (const int width : { 869, 1086, 1448 })
    {
        INFO ("width " << width);
        LearnRig rig (width);
        juce::String error;
        const auto files = osp::plugin::tutorial::files (osp::plugin::tutorial::Example::bowed, error);
        rig.p.loadFile (files[0], 0);
        rig.waitLoaded();
        rig.automate ("layerA.sourceMode", 1.0f);   // Granular: POS is a target
        struct Case
        {
            const char* lesson;
            const char* step;
            const char* target;
        };
        for (const auto& c : { Case { "osp.quickstart", "start", "osp.source.start" }, Case { "osp.playback", "pos", "osp.granular.position" },
                               Case { "osp.arp", "rate", "osp.arp.rate" }, Case { "osp.sound", "filter", "osp.macro.filter" },
                               Case { "osp.life", "zero", "osp.macro.life" } })
        {
            INFO (c.lesson << " / " << c.step);
            const auto& lesson = *rig.learn.catalog().findLesson (c.lesson);
            int index = -1;
            for (int s = 0; s < static_cast<int> (lesson.steps.size()); ++s)
                if (lesson.steps[static_cast<std::size_t> (s)].id == c.step)
                    index = s;
            REQUIRE (index >= 0);
            REQUIRE (rig.learn.startLessonAt (c.lesson, index));
            rig.tick();
            if (rig.learn.noteKind() == LearnNote::prepare)
                rig.learn.noteAction ("usemine");
            rig.tick();
            REQUIRE (rig.learn.noteKind() == LearnNote::lesson);
            rig.learn.noteAction ("showme");
            rig.tick();
            rig.learn.layoutNow();
            REQUIRE (rig.learn.highlight() != nullptr);
            const auto bounds = rig.learn.targets().boundsIn (c.target, *rig.learn.highlight()->getParentComponent());
            REQUIRE (bounds.has_value());
            REQUIRE (rig.learn.highlight()->target().has_value());
            CHECK (*rig.learn.highlight()->target() == *bounds);
            REQUIRE (rig.learn.note() != nullptr);
            const auto card = rig.learn.note()->getBounds().reduced (andor::learn::Note::margin);
            CHECK_FALSE (card.intersects (*bounds));
            CHECK (rig.learn.highlight()->getParentComponent()->getLocalBounds().contains (card));
            rig.learn.noteAction ("close");
            rig.tick();
            rig.ui.closePopup();
        }
    }
}

TEST_CASE ("learn: Reset Tutorials asks, then resets OSP's lessons only; Escape closes the note first", "[plugin][learn]")
{
    LearnRig rig;
    // Another product's progress beside OSP's (Analog's file): untouched by OSP's reset.
    andor::learn::ProgressStore analog ("analog");
    REQUIRE (analog.update ([] (andor::learn::ProgressData& d) { d.lessons["analog.arp.basics"].status = "complete"; }));
    REQUIRE (rig.learn.store().update ([] (andor::learn::ProgressData& d) { d.lessons["osp.arp"].status = "complete"; }));
    rig.learn.askToResetProgress();
    rig.tick();
    REQUIRE (rig.learn.noteKind() == LearnNote::confirmReset);
    CHECK (rig.learn.store().read().lessons.count ("osp.arp") == 1);   // (asked, not done)
    rig.learn.noteAction ("reset");
    CHECK (rig.learn.store().read().lessons.count ("osp.arp") == 0);
    CHECK (analog.read().lessons.count ("analog.arp.basics") == 1);
    // Escape: the note, then the editor's popover.
    REQUIRE (rig.learn.startLesson ("osp.quickstart"));
    rig.ui.openPopup (OspEditor::advancedPopup);
    rig.tick();
    REQUIRE (rig.learn.note() != nullptr);
    CHECK (rig.ui.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK (rig.learn.note() == nullptr);
}

TEST_CASE ("learn: Toggle Info explains the control clicked, only where it is shown", "[plugin][learn]")
{
    LearnRig rig;
    rig.learn.setLearnMode (true);
    rig.tick();
    // An empty instrument: no card regions (there is no card to explain).
    bool cardRegion = false;
    for (auto* r : rig.learn.regions())
        cardRegion = cardRegion || (r->isVisible() && r->targetId().startsWith ("osp.source."));
    CHECK_FALSE (cardRegion);
    rig.learn.inspect ("osp.macro.filter");
    rig.tick();
    REQUIRE (rig.learn.noteKind() == LearnNote::help);
    CHECK (rig.learn.note()->titleText() == "FILTER");
    CHECK (rig.learn.note()->metaText() == "INFO");
    rig.learn.setLearnMode (false);
    rig.tick();
    CHECK_FALSE (rig.learn.isActive());
}

TEST_CASE ("learn: tutorial sounds are written once, deterministic and playable", "[plugin][learn]")
{
    ScopedLearnFolders folders;
    juce::String error;
    for (auto e : { osp::plugin::tutorial::Example::bowed, osp::plugin::tutorial::Example::pluck, osp::plugin::tutorial::Example::voice,
                    osp::plugin::tutorial::Example::layered, osp::plugin::tutorial::Example::pluckSet })
    {
        INFO (osp::plugin::tutorial::idOf (e));
        const auto files = osp::plugin::tutorial::files (e, error);
        REQUIRE (! files.isEmpty());
        for (const auto& f : files)
            CHECK (f.existsAsFile());
    }
    const auto bowed = osp::plugin::tutorial::files (osp::plugin::tutorial::Example::bowed, error)[0];
    juce::MemoryBlock first;
    REQUIRE (bowed.loadFileAsData (first));
    bowed.deleteFile();
    const auto again = osp::plugin::tutorial::files (osp::plugin::tutorial::Example::bowed, error)[0];
    juce::MemoryBlock second;
    REQUIRE (again.loadFileAsData (second));
    CHECK (first == second);   // (the same seed: the same file, byte for byte)
}

TEST_CASE ("learn: screenshots of every lesson step (OSP_LEARN_SHOTS)", "[.][learn-shots]")
{
    const auto dirName = juce::SystemStats::getEnvironmentVariable ("OSP_LEARN_SHOTS", {});
    REQUIRE (dirName.isNotEmpty());
    const juce::File dir (dirName);
    dir.createDirectory();
    auto shoot = [&dir] (juce::AudioProcessorEditor& editor, const juce::String& name) {
        const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        juce::FileOutputStream out (dir.getChildFile (name + ".png"));
        REQUIRE (out.openedOk());
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
    };
    for (const int width : { 1086, 869 })
    {
        const auto w = juce::String (width);
        {
            LearnRig rig (width);
            shoot (*rig.editor, "00-off-" + w);
            rig.learn.openBrowser();
            rig.tick();
            shoot (*rig.editor, "01-tutorials-" + w);
            rig.learn.closeBrowser();
            rig.learn.setLearnMode (true);
            rig.learn.inspect ("osp.macro.filter");
            rig.tick();
            shoot (*rig.editor, "02-info-filter-" + w);
            rig.learn.setLearnMode (false);
            rig.learn.askToResetProgress();
            rig.tick();
            shoot (*rig.editor, "03-reset-confirm-" + w);
            rig.learn.noteAction ("cancel");
            REQUIRE (rig.learn.startLesson ("osp.multisample"));
            rig.tick();
            shoot (*rig.editor, "04-prepare-" + w);
            rig.learn.noteAction ("loadexample");
            rig.tick();
            shoot (*rig.editor, "05-preparing-" + w);
            rig.waitLoaded();
            rig.tick();
            rig.learn.noteAction ("close");
            rig.tick();
            shoot (*rig.editor, "06-restore-offer-" + w);
            rig.learn.noteAction ("keep");
            rig.tick();
        }
        int lessonNumber = 10;
        std::vector<andor::learn::Lesson> lessons;
        {
            LearnRig probe;
            lessons = probe.learn.catalog().lessons;
        }
        for (const auto& lesson : lessons)
        {
            LearnRig rig (width);
            ++lessonNumber;
            const auto prefix = juce::String (lessonNumber) + "-" + lesson.id.fromFirstOccurrenceOf ("osp.", false, false);
            for (int s = 0; s < static_cast<int> (lesson.steps.size()); ++s)
            {
                const auto& st = lesson.steps[static_cast<std::size_t> (s)];
                // The sound the lesson works on (Quick Start's first step: the empty instrument).
                if (! (lesson.id == "osp.quickstart" && s == 0) && rig.p.occupiedLayerCount() == 0)
                {
                    const auto example = lesson.preparation.exampleId.isNotEmpty() ? lesson.preparation.exampleId : juce::String ("bowed");
                    REQUIRE (rig.p.loadTutorialExample (example));
                    rig.waitLoaded();
                }
                // What the musician would have done by this step.
                if ((lesson.id == "osp.playback" && s >= 3 && s < 7) || (lesson.id == "osp.keydna" && s >= 1))
                    rig.userSet ("layerA.sourceMode", 1.0f);
                if (lesson.id == "osp.modulation" && s >= 4 && ! rig.learn.targets().state ("osp.mod.lfo1.routed"))
                    rig.p.assignModulation (mod::Source::lfo1, osp::plugin::modui::destinationFor ("character", false), 50.0f);
                if (lesson.id == "osp.keydna" && s >= 4 && ! rig.p.keyDnaSettings (0).enabled)
                {
                    auto k = rig.p.keyDnaSettings (0);
                    k.enabled = true;
                    rig.p.setKeyDnaSettings (0, k, "test", false);
                }
                REQUIRE (rig.learn.startLessonAt (lesson.id, s));
                rig.tick();
                if (rig.learn.noteKind() == LearnNote::prepare)
                {
                    rig.learn.noteAction (rig.learn.note()->actionIds().contains ("usemine") ? "usemine" : "loadexample");
                    rig.waitLoaded();
                    rig.tick();
                    rig.tick();
                }
                if (rig.learn.note() != nullptr && rig.learn.note()->actionIds().contains ("showme"))
                    rig.learn.noteAction ("showme");
                rig.tick();
                rig.learn.layoutNow();
                shoot (*rig.editor, prefix + "-" + juce::String (s + 1).paddedLeft ('0', 2) + "-" + st.id + "-" + w);
                rig.learn.noteAction ("close");
                rig.tick();
                if (rig.learn.noteKind() == LearnNote::restoreOffer)
                    rig.learn.noteAction ("keep");
                rig.ui.closePopup();
                rig.ui.closeKeyDna();
                rig.ui.closeLibrary();
                rig.tick();
            }
        }
    }
}
