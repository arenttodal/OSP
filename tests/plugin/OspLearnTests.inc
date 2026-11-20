// ANDOR Learn in ANDOR OSP's own plugin (apps/plugin/Source/OspLearn.cpp): the shared
// controller on OspAudioProcessorEditor, OSP's lessons, targets, readiness, examples and
// progress. Included at the end of PluginProcessorTests.cpp.

#include "LearnController.h"
#include "TutorialSounds.h"
#include <set>

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
        juce::String documentsBefore = juce::SystemStats::getEnvironmentVariable ("OSP_DOCUMENTS_DIR", {});
        ScopedLearnFolders()
        {
            dir.createDirectory();
            setEnv ("ANDOR_LEARN_DIR", dir.getChildFile ("learn").getFullPathName());
            setEnv ("OSP_TUTORIAL_DIR", dir.getChildFile ("sounds").getFullPathName());
            setEnv ("OSP_DOCUMENTS_DIR", dir.getChildFile ("documents").getFullPathName());   // (the CORE / USER preference)
        }
        ~ScopedLearnFolders()
        {
            setEnv ("ANDOR_LEARN_DIR", learnBefore);
            setEnv ("OSP_TUTORIAL_DIR", tutorialBefore);
            setEnv ("OSP_DOCUMENTS_DIR", documentsBefore);
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
    CHECK (rig.learn.catalog().lessons.size() == 21);
    CHECK (rig.learn.catalog().help.size() == 57);
    // Idle: no note, no browser, no ring (nothing of Learn's in the editor).
    CHECK_FALSE (rig.learn.isActive());
    CHECK (rig.learn.note() == nullptr);
    CHECK (rig.learn.highlight() == nullptr);
    // The menu: Learn, Tutorials..., Reset Tutorials... (no old labels; Info is the INFO key's).
    juce::PopupMenu menu;
    rig.learn.addMenuItems (menu);
    const auto dots = juce::String::fromUTF8 ("\xe2\x80\xa6");
    CHECK (menuHas (menu, "Tutorials" + dots));
    CHECK_FALSE (menuHas (menu, "Toggle Info"));
    CHECK (menuHas (menu, "Reset Tutorials" + dots));
    CHECK_FALSE (menuHas (menu, "Learn mode"));
    CHECK_FALSE (menuHas (menu, "Learn ANDOR" + dots));
    // Every lesson: a line for the browser, a completion text, and (all but Quick Start) a set-up
    // step first; none is offered a Skip (Learn 2.0 has none).
    for (const auto& l : rig.learn.catalog().lessons)
    {
        INFO (l.id);
        CHECK (l.description.isNotEmpty());
        CHECK (l.completionText.isNotEmpty());
        CHECK (l.exampleId.isNotEmpty());
        REQUIRE_FALSE (l.steps.empty());
        CHECK (l.steps.front().preparation);
    }
}

TEST_CASE ("learn: Quick Start, end to end, only the musician's own actions complete it", "[plugin][learn]")
{
    LearnRig rig;
    REQUIRE (rig.learn.startLesson ("osp.quickstart"));
    rig.tick();
    // An empty instrument: the set-up step, at the drop zone (no Card A pretended), its sound
    // not ready yet: Load Example Sound first.
    REQUIRE (rig.learn.noteKind() == LearnNote::lesson);
    CHECK (rig.stepId() == "setup");
    CHECK_FALSE (rig.learn.runner().preparationReady());
    CHECK (rig.learn.note()->actionIds().contains ("example"));
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
    rig.waitLoaded();
    rig.tick();
    CHECK (rig.learn.runner().preparationReady());   // (ready only once it has loaded and been analysed)
    rig.learn.noteAction ("current");
    rig.tick();
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
    rig.next();
    // The last step: where Learn is found again (the INFO key, and the menu's Learn). It points
    // at the key and turns nothing on.
    CHECK (rig.stepId() == "learn.again");
    REQUIRE (rig.learn.runner().isLastStep());
    CHECK (rig.learn.runner().step()->body.contains ("i in the corner"));
    CHECK (rig.learn.runner().step()->body.contains ("three-dot menu"));
    CHECK_FALSE (rig.learn.learnMode());
    {
        auto& content = *rig.learn.highlight()->getParentComponent();
        const auto key = rig.learn.targets().boundsIn ("keyboard.info", content);
        REQUIRE (key.has_value());
        auto* info = rig.ui.keyboardActions().button (osp::plugin::KeyboardUtilityActions::Action::info);
        REQUIRE (info != nullptr);
        CHECK (*key == content.getLocalArea (info, info->getLocalBounds()));
        REQUIRE (rig.learn.highlight()->target().has_value());
        CHECK (rig.learn.highlight()->target()->intersects (*key));
    }
    rig.learn.noteAction ("finish");
    rig.tick();
    // Nothing to choose (no example was loaded): the lesson ends in the tutorials, its tick shown.
    CHECK (rig.learn.noteKind() == LearnNote::none);
    REQUIRE (rig.learn.browser() != nullptr);
    CHECK (rig.learn.browser()->isVisible());
    // Progress: complete, under OSP's own name.
    const auto progress = rig.learn.store().read();
    CHECK (progress.product == "osp");
    REQUIRE (progress.lessons.count ("osp.quickstart") == 1);
    CHECK (progress.lessons.at ("osp.quickstart").status == "complete");
    // The example was never involved: nothing to restore.
    CHECK_FALSE (rig.p.canRestoreBeforeTutorial());
    rig.learn.closeBrowser();
    rig.tick();
    CHECK_FALSE (rig.learn.isActive());
}

TEST_CASE ("learn: a lesson played on its example asks Keep / Restore, then the tutorials open", "[plugin][learn]")
{
    LearnRig rig;
    REQUIRE (rig.learn.startLesson ("osp.quickstart"));
    rig.tick();
    rig.learn.noteAction ("example");
    rig.waitLoaded();
    rig.tick();
    rig.tick();
    REQUIRE (rig.p.canRestoreBeforeTutorial());
    // Straight to the last step (the lesson's own pages are tested above).
    REQUIRE (rig.learn.startLesson ("osp.quickstart", static_cast<int> (rig.learn.catalog().findLesson ("osp.quickstart")->steps.size()) - 1));
    rig.tick();
    REQUIRE (rig.stepId() == "learn.again");
    rig.learn.noteAction ("finish");
    rig.tick();
    REQUIRE (rig.learn.noteKind() == LearnNote::complete);
    CHECK (rig.learn.note()->actionIds().contains ("keep"));
    CHECK (rig.learn.note()->actionIds().contains ("restore"));
    CHECK (rig.learn.browser() == nullptr);
    rig.learn.noteAction ("keep");
    rig.tick();
    CHECK (rig.learn.noteKind() == LearnNote::none);
    REQUIRE (rig.learn.browser() != nullptr);
    rig.learn.closeBrowser();
}

TEST_CASE ("learn: a lesson that needs a sound asks first, loads the example only when chosen, and restores", "[plugin][learn]")
{
    LearnRig rig;
    // The musician's own state first: a recording and settings of theirs.
    juce::String error;
    const auto mine = osp::plugin::tutorial::files (osp::plugin::tutorial::Example::voice, error);
    REQUIRE (mine.size() == 1);
    rig.p.loadFile (mine[0], 0);
    rig.waitLoaded();
    rig.automate ("space", 77.0f);
    rig.automate ("layerA.sourceMode", 1.0f);
    // The multi-sample lesson: this sound is not a set; its set-up says so (nothing changed yet).
    const auto before = rig.parameterValues();
    REQUIRE (rig.learn.startLesson ("osp.multisample"));
    rig.tick();
    REQUIRE (rig.learn.noteKind() == LearnNote::lesson);
    CHECK (rig.stepId() == "setup");
    CHECK_FALSE (rig.learn.runner().preparationReady());
    CHECK (rig.learn.note()->actionIds().contains ("example"));
    CHECK (rig.parameterValues() == before);
    // X: nothing loaded, nothing restorable.
    rig.learn.noteAction ("close");
    rig.tick();
    CHECK_FALSE (rig.p.canRestoreBeforeTutorial());
    CHECK (rig.parameterValues() == before);
    // Use Current Sound: the next step waits, blocked, saying what is missing.
    REQUIRE (rig.learn.startLesson ("osp.multisample"));
    rig.tick();
    rig.learn.noteAction ("current");
    rig.tick();
    CHECK (rig.learn.runner().state() == andor::learn::LessonRunner::State::blocked);
    // Load Example Sound: the whole state is kept first; the lesson waits until it plays.
    rig.learn.noteAction ("example");
    CHECK (rig.p.canRestoreBeforeTutorial());
    rig.waitLoaded();
    rig.tick();
    rig.tick();
    CHECK (rig.learn.runner().state() != andor::learn::LessonRunner::State::blocked);
    const auto inst = rig.p.currentInstrument (0);
    REQUIRE (inst != nullptr);
    REQUIRE (inst->set != nullptr);
    CHECK (inst->set->members.size() == 8);
    CHECK (rig.p.parameterValue ("space") != 77.0f);   // a clean instrument for the lesson
    // X never restores; the menu's Restore Previous Sound does.
    rig.learn.noteAction ("close");
    rig.tick();
    CHECK (rig.p.currentInstrument (0)->set != nullptr);
    juce::PopupMenu menu;
    rig.learn.addMenuItems (menu);
    CHECK (menuHas (menu, "Restore Previous Sound"));
    REQUIRE (rig.learn.restorePreviousSound());
    rig.waitLoaded();
    CHECK (rig.p.parameterValue ("space") == Catch::Approx (77.0f).margin (0.01));
    CHECK (rig.p.parameterValue ("layerA.sourceMode") == Catch::Approx (1.0f));
    REQUIRE (rig.p.currentInstrument (0) != nullptr);
    CHECK (rig.p.currentInstrument (0)->set == nullptr);
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
        REQUIRE (rig.learn.noteKind() == LearnNote::lesson);
        if (rig.stepId() == "setup")
        {
            rig.learn.noteAction ("current");   // (never the example here: nothing may change)
            rig.tick();
        }
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
            REQUIRE (rig.learn.startLesson (c.lesson, index));
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
    rig.learn.confirmResetProgress();
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
    rig.learn.setLearnMode (false);
    rig.tick();
    CHECK_FALSE (rig.learn.isActive());
}

TEST_CASE ("learn: tutorial sounds are written once, deterministic and playable", "[plugin][learn]")
{
    ScopedLearnFolders folders;
    juce::String error;
    using osp::plugin::tutorial::Example;
    for (auto e : { Example::bowed, Example::pluck, Example::voice, Example::layered, Example::pluckSet, Example::phrase })
    {
        INFO (osp::plugin::tutorial::idOf (e));
        const auto files = osp::plugin::tutorial::files (e, error);
        REQUIRE (! files.isEmpty());
        for (const auto& f : files)
            CHECK (f.existsAsFile());
        // Written again from nothing: the same seed, the same files, byte for byte.
        std::vector<juce::MemoryBlock> first;
        for (const auto& f : files)
        {
            REQUIRE (f.loadFileAsData (first.emplace_back()));
            f.deleteFile();
        }
        const auto again = osp::plugin::tutorial::files (e, error);
        REQUIRE (again.size() == files.size());
        for (int i = 0; i < again.size(); ++i)
        {
            INFO (again[i].getFileName());
            juce::MemoryBlock second;
            REQUIRE (again[i].loadFileAsData (second));
            CHECK (first[static_cast<std::size_t> (i)] == second);
        }
    }
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
            rig.learn.confirmResetProgress();
            rig.tick();
            shoot (*rig.editor, "03-reset-confirm-" + w);
            rig.learn.noteAction ("cancel");
            REQUIRE (rig.learn.startLesson ("osp.multisample"));
            rig.tick();
            shoot (*rig.editor, "04-setup-" + w);
            rig.learn.noteAction ("current");
            rig.tick();
            shoot (*rig.editor, "05-blocked-" + w);
            rig.learn.noteAction ("example");
            rig.waitLoaded();
            rig.tick();
            rig.tick();
            shoot (*rig.editor, "06-example-loaded-" + w);
            rig.learn.noteAction ("close");
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
                    const auto example = lesson.exampleId.isNotEmpty() ? lesson.exampleId : juce::String ("bowed");
                    REQUIRE (rig.p.loadTutorialExample (example));
                    rig.waitLoaded();
                }
                // What the musician would have done by this step.
                if ((lesson.id == "osp.playback" && juce::StringArray { "pos", "size", "dens", "spread" }.contains (st.id))
                    || (lesson.id == "osp.keydna" && st.id != "setup" && st.id != "granular"))
                    rig.userSet ("layerA.sourceMode", 1.0f);
                if (lesson.id == "osp.modulation" && (st.id == "depth" || st.id == "listen") && ! rig.learn.targets().state ("osp.mod.lfo1.routed"))
                    rig.p.assignModulation (mod::Source::lfo1, osp::plugin::modui::destinationFor ("character", false), 50.0f);
                if (lesson.id == "osp.keydna" && juce::StringArray { "position", "other", "amount" }.contains (st.id) && ! rig.p.keyDnaSettings (0).enabled)
                {
                    auto k = rig.p.keyDnaSettings (0);
                    k.enabled = true;
                    rig.p.setKeyDnaSettings (0, k, "test", false);
                }
                REQUIRE (rig.learn.startLesson (lesson.id, s));
                rig.tick();
                if (rig.learn.note() != nullptr && rig.learn.note()->actionIds().contains ("showme"))
                    rig.learn.noteAction ("showme");
                rig.tick();
                rig.learn.layoutNow();
                shoot (*rig.editor, prefix + "-" + juce::String (s + 1).paddedLeft ('0', 2) + "-" + st.id + "-" + w);
                rig.learn.noteAction ("close");
                rig.tick();
                rig.ui.closePopup();
                rig.ui.closeKeyDna();
                rig.ui.closeLibrary();
                rig.tick();
            }
        }
    }
}

TEST_CASE ("learn: From a phrase to an instrument - the example phrase, MAP NOTES, its notes on the keys, NOTES; a done step moves on by itself", "[plugin][learn]")
{
    LearnRig rig;
    // Right after the Quick Start in the browser's order.
    REQUIRE (rig.learn.catalog().lessons.size() > 1);
    CHECK (rig.learn.catalog().lessons[1].id == "osp.phrase");
    rig.learn.setAutoAdvanceDelay (0);   // (each step by hand first; the pause is tested below)
    REQUIRE (rig.learn.startLesson ("osp.phrase"));
    rig.tick();
    REQUIRE (rig.stepId() == "setup");
    CHECK_FALSE (rig.learn.runner().preparationReady());
    REQUIRE (rig.learn.note()->actionIds().contains ("example"));
    // The example phrase loads like a dropped recording: OSP asks to map its notes.
    rig.learn.noteAction ("example");
    rig.waitLoaded();
    rig.tick();
    CHECK (rig.p.mapNotesPrompt (0) >= 5);
    if (rig.stepId() == "setup")
    {
        CHECK (rig.learn.runner().preparationReady());
        rig.learn.noteAction ("current");
        rig.tick();
    }
    REQUIRE (rig.stepId() == "map");
    auto& card = rig.ui.engineCard (0);
    REQUIRE (card.mapNotesPromptComponent() != nullptr);
    {
        auto& content = *rig.learn.highlight()->getParentComponent();
        const auto prompt = rig.learn.targets().boundsIn ("osp.source.mapNotes", content);
        REQUIRE (prompt.has_value());
        CHECK (*prompt == content.getLocalArea (card.mapNotesPromptComponent(), card.mapNotesPromptComponent()->getLocalBounds()));
    }
    CHECK_FALSE (rig.stepDone());
    card.answerMapNotesPrompt (true);   // MAP NOTES (the prompt's own handler)
    rig.tick();
    CHECK (rig.stepDone());
    // The analysis lands: the phrase's notes on their keys.
    for (int i = 0; i < 60 && ! (rig.p.currentInstrument (0) != nullptr && rig.p.currentInstrument (0)->atlas != nullptr
                                 && rig.p.atlasActivity (0) == osp::plugin::InstrumentHost::AtlasActivity::none);
         ++i)
    {
        REQUIRE (rig.p.waitForLoads (60000));
        rig.p.pollLoads();
    }
    REQUIRE (rig.p.currentInstrument (0) != nullptr);
    REQUIRE (rig.p.currentInstrument (0)->atlas != nullptr);
    std::set<int> keys;
    for (const auto& n : rig.p.currentInstrument (0)->atlas->notes)
        if (n.included)
            keys.insert (n.rootMidi);
    CHECK (keys == std::set<int> { 57, 60, 64, 69 });
    rig.next();
    REQUIRE (rig.stepId() == "play");
    CHECK_FALSE (rig.stepDone());
    // A done step moves on by itself after the pause (Next is not needed).
    rig.learn.setAutoAdvanceDelay (60);
    rig.playNote (60);
    REQUIRE (rig.stepDone());
    rig.tick();
    CHECK (rig.stepId() == "play");   // (not at once)
    juce::Thread::sleep (90);
    rig.tick();
    REQUIRE (rig.stepId() == "notes");
    rig.learn.setAutoAdvanceDelay (0);
    // NOTES: the card's own key.
    CHECK_FALSE (card.isNotesOpen());
    REQUIRE (card.onNotesToggle != nullptr);
    card.onNotesToggle (0, true);
    rig.tick();
    CHECK (card.isNotesOpen());
    REQUIRE (rig.stepDone());
    rig.next();
    REQUIRE (rig.stepId() == "any");
    rig.learn.noteAction ("finish");
    rig.tick();
    // The example was loaded: Keep or Restore, then the tutorials.
    REQUIRE (rig.learn.noteKind() == LearnNote::complete);
    rig.learn.noteAction ("keep");
    rig.tick();
    REQUIRE (rig.learn.browser() != nullptr);
    CHECK (rig.learn.store().read().lessons.at ("osp.phrase").status == "complete");
    rig.learn.closeBrowser();
}

TEST_CASE ("learn: Layering's last step says a sound or a phrase can simply be dropped", "[plugin][learn]")
{
    LearnRig rig;
    const auto* layers = rig.learn.catalog().findLesson ("osp.layers");
    REQUIRE (layers != nullptr);
    CHECK (layers->steps.back().id == "third");
    CHECK (layers->steps.back().body.contains ("drop a sound or a phrase onto the instrument"));
}

TEST_CASE ("learn: Create a macro - USER, a right-click on LEVEL, Assign to Macro 1, its range on the halo, Macro 1 turned", "[plugin][learn][usermacro]")
{
    LearnRig rig (1448);
    rig.learn.setAutoAdvanceDelay (0);
    REQUIRE (rig.learn.startLesson ("osp.usermacros"));
    rig.tick();
    REQUIRE (rig.stepId() == "setup");
    rig.learn.noteAction ("example");
    rig.waitLoaded();
    rig.tick();
    if (rig.stepId() == "setup")
    {
        rig.learn.noteAction ("current");
        rig.tick();
    }
    REQUIRE (rig.stepId() == "user");
    auto& um = rig.ui.userMacros();
    REQUIRE (um.bankShown() == 0);
    um.tab()->onChange (1);   // (a click on USER: the tab's own handler)
    rig.tick();
    CHECK (rig.stepDone());
    rig.learn.noteAction ("next");
    rig.tick();
    REQUIRE (rig.stepId() == "menu");
    const auto level = osp::plugin::InstrumentHost::layerParameterId (rig.p.editLayer(), "level");
    auto menu = rig.ui.modulationOverlay().contextMenu (level);   // (a right-click on LEVEL)
    rig.tick();
    REQUIRE (rig.stepId() == "assign");   // (its menu open: the step after speaks to it)
    const juce::PopupMenu::Item* macro1 = nullptr;
    for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
        if (it.getItem().text == "Macro 1")
            macro1 = &it.getItem();
    REQUIRE (macro1 != nullptr);
    macro1->action();
    rig.tick();
    CHECK (rig.stepDone());
    CHECK (um.bankShown() == 1);   // (assigning never switches the tab)
    rig.learn.noteAction ("next");
    rig.tick();
    REQUIRE (rig.stepId() == "range");
    // An end dragged on the halo (the overlay's own hook, as its mouse-up commits).
    auto& overlay = rig.ui.modulationOverlay();
    REQUIRE (overlay.editMacroRange != nullptr);
    using Edit = osp::plugin::ModulationOverlay::MacroEdit;
    overlay.editMacroRange (0, level, 1, 0.9f, Edit::begin);
    overlay.editMacroRange (0, level, 1, 0.9f, Edit::move);
    overlay.editMacroRange (0, level, 1, 0.9f, Edit::commit);
    rig.tick();
    CHECK (rig.stepDone());
    rig.learn.noteAction ("next");
    rig.tick();
    REQUIRE (rig.stepId() == "turn");
    // (the USER knob pressed, turned and released: Learn's own touch watcher on it)
    auto* watcher = rig.learn.touchWatcher ("usermacro.0");
    REQUIRE (watcher != nullptr);
    auto* knob = dynamic_cast<juce::Slider*> (watcher->watched());
    REQUIRE (knob == &um.row()[0].slider);
    const auto at = [knob] {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, knob, knob,
                                 juce::Time::getCurrentTime(), {}, juce::Time::getCurrentTime(), 1, false);
    };
    watcher->mouseDown (at());
    knob->setValue (60.0, juce::sendNotificationSync);
    rig.tick();
    watcher->mouseUp (at());
    rig.tick();
    CHECK (rig.stepDone());
    rig.learn.noteAction ("next");
    rig.tick();
    REQUIRE (rig.stepId() == "review");
    CHECK (rig.learn.targets().boundsIn ("usermacro.0.label", *rig.learn.highlight()->getParentComponent()).has_value());
    um.showBank (0);
}

//==============================================================================
// The beta features (docs/learn-studio/beta-readiness-in-plugin-learn.md): RECORD / GRAB LAST,
// the per-card arpeggiator, Custom Patterns, STRUM, SATELLITE, SYNTH / IMPRINT.

#include "CustomPatternBrowser.h"
#include "CustomPatternEditor.h"

#if ! (JUCE_MAC || JUCE_IOS || JUCE_ANDROID)
namespace juce::detail
{
bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages);   // (juce_events: Linux / Windows)
}
#endif

namespace
{
    /** Delivers the message loop's pending calls (the ARP panel opens its browser after the
        press). False where the test cannot pump it (macOS: the run loop is the app's). */
    bool learnPump()
    {
       #if ! (JUCE_MAC || JUCE_IOS || JUCE_ANDROID)
        for (int i = 0; i < 200 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++i)
        {
        }
        return true;
       #else
        return false;
       #endif
    }

    /** A press on `c` at `at` (its own coordinates), as the mouse delivers it. */
    juce::MouseEvent learnPress (juce::Component& c, juce::Point<float> at)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, {}, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, at,
                                 now, 1, false);
    }

    /** The style list scrolled `rows` rows (a wheel over it; negative: down). */
    void scrollArpList (osp::plugin::ArpInlinePanel& arp, int rows)
    {
        juce::MouseWheelDetails wheel {};
        wheel.deltaY = -static_cast<float> (rows) / 6.0f;
        arp.mouseWheelMove (learnPress (arp, arp.patternListArea().getCentre()), wheel);
        arp.refresh();
    }

    /** A style's row brought into view (the list scrolled from the top). */
    juce::Rectangle<float> arpRowInView (osp::plugin::ArpInlinePanel& arp, int pattern)
    {
        scrollArpList (arp, -40);
        for (int i = 0; i < 30 && arp.patternRowArea (pattern).isEmpty(); ++i)
            scrollArpList (arp, 1);
        return arp.patternRowArea (pattern);
    }

    void loadLearnExample (LearnRig& rig, const char* id)
    {
        REQUIRE (rig.p.loadTutorialExample (id));
        rig.waitLoaded();
    }

    /** The lesson started on the sound loaded now (its set-up passed with Use Current Sound). */
    void startOnCurrent (LearnRig& rig, const juce::String& lessonId)
    {
        rig.learn.setAutoAdvanceDelay (0);
        REQUIRE (rig.learn.startLesson (lessonId));
        rig.tick();
        REQUIRE (rig.stepId() == "setup");
        rig.learn.noteAction ("current");
        rig.tick();
    }

    /** The ARP lessons' first two tasks, done by the musician: ARP's key, then its light. */
    void openArpAndSwitchOn (LearnRig& rig, bool switchOn = true)
    {
        REQUIRE (rig.stepId() == "open");
        rig.ui.setArpExpanded (true);   // (a reveal shows it: not the musician's)
        rig.tick();
        CHECK_FALSE (rig.stepDone());
        rig.ui.setArpExpanded (false);
        rig.ui.arpControl().onToggleEditor();   // ARP's key (its own handler)
        rig.tick();
        REQUIRE (rig.stepDone());
        rig.next();
        if (! switchOn)
            return;
        REQUIRE (rig.stepId() == "on");
        rig.automate ("arp.enabled", 1.0f);   // (automation: not the musician's)
        CHECK_FALSE (rig.stepDone());
        rig.automate ("arp.enabled", 0.0f);
        rig.userSet ("arp.enabled", 1.0f);
        REQUIRE (rig.stepDone());
        rig.next();
    }
} // namespace

TEST_CASE ("learn: the beta features have lessons, INFO topics, targets and states", "[plugin][learn]")
{
    LearnRig rig;
    const auto& catalog = rig.learn.catalog();
    const auto& targets = rig.learn.targets();
    for (const char* id : { "osp.capture", "osp.arp.layers", "osp.arp.custom", "osp.arp.strum", "osp.satellite", "osp.synth" })
    {
        INFO (id);
        const auto* lesson = catalog.findLesson (id);
        REQUIRE (lesson != nullptr);
        // Interface only: a set-up, then tasks the musician does (each a user action with a
        // verified result); every step points at a registered control.
        CHECK (lesson->steps.front().preparation);
        int tasks = 0;
        for (const auto& st : lesson->steps)
        {
            INFO (st.id);
            tasks += st.completion.kind == andor::learn::Completion::Kind::userAction ? 1 : 0;
            if (st.completion.kind == andor::learn::Completion::Kind::userAction)
                CHECK_FALSE (st.completion.actions.isEmpty());
            if (st.targetId.isNotEmpty())
                CHECK (targets.contains (st.targetId));
        }
        CHECK (tasks >= 3);
    }
    // The browser's order: CAPTURE after the phrase, SYNTH after REIMAGINED, the ARP lessons
    // after the Arpeggiator.
    juce::StringArray order;
    for (const auto& l : catalog.lessons)
        order.add (l.id);
    CHECK (order.indexOf ("osp.capture") == order.indexOf ("osp.phrase") + 1);
    CHECK (order.indexOf ("osp.synth") == order.indexOf ("osp.reimagined") + 1);
    CHECK (order.indexOf ("osp.arp.layers") == order.indexOf ("osp.arp") + 1);
    CHECK (order.indexOf ("osp.satellite") == order.indexOf ("osp.arp.strum") + 1);
    // INFO: a topic for each new control, on a registered target, each with its lesson.
    for (const char* topic : { "osp.arp.cards", "osp.arp.link", "osp.arp.edit", "osp.arp.custom.row", "osp.arp.custom.pencil", "osp.arp.custom.browser", "osp.arp.custom.editor",
                               "osp.arp.strum", "osp.arp.strum.panel", "osp.arp.satellite", "osp.arp.satellite.panel", "osp.source.rec", "osp.capture.page",
                               "osp.capture.record", "osp.capture.grab", "osp.source.synth", "osp.synth.page" })
    {
        INFO (topic);
        const andor::learn::HelpTopic* found = nullptr;
        for (const auto& h : catalog.help)
            if (h.targetId == topic)
                found = &h;
        REQUIRE (found != nullptr);
        CHECK (found->body.isNotEmpty());
        CHECK (catalog.findLesson (found->lessonId) != nullptr);
        CHECK (targets.contains (topic));
    }
    // The semantic targets and states the public guides name (shared IDs).
    for (const char* id : { "osp.source.rec", "osp.capture.page", "osp.capture.record", "osp.capture.grab", "osp.capture.meter", "osp.arp.cards", "osp.arp.link", "osp.arp.edit.A",
                            "osp.arp.edit.B", "osp.arp.edit.C", "osp.arp.custom.row", "osp.arp.custom.pencil", "osp.arp.custom.browser", "osp.arp.custom.editor", "osp.arp.strum",
                            "osp.arp.strum.light", "osp.arp.strum.panel", "osp.arp.satellite", "osp.arp.satellite.light", "osp.arp.satellite.panel", "osp.source.synth",
                            "osp.synth.page", "osp.synth.mode", "osp.synth.wave", "osp.synth.shape", "osp.synth.imprint" })
    {
        INFO (id);
        CHECK (targets.contains (id));
    }
    for (const char* id : { "osp.capture.open", "osp.capture.recording", "osp.capture.connected", "osp.synth.open" })
    {
        INFO (id);
        CHECK (targets.hasState (id));
    }
    // An empty instrument: no card, so no REC or SYNTH to point at.
    CHECK_FALSE (targets.boundsIn ("osp.source.rec", rig.ui).has_value());
    CHECK_FALSE (targets.boundsIn ("osp.source.synth", rig.ui).has_value());
}

TEST_CASE ("learn: Record or grab - REC, a take kept from the CAPTURE page, its notes played; only the musician's clicks complete it", "[plugin][learn][capture]")
{
    LearnRig rig (1448);
    loadLearnExample (rig, "bowed");
    startOnCurrent (rig, "osp.capture");
    REQUIRE (rig.stepId() == "open");
    auto& card = rig.ui.engineCard (0);
    REQUIRE (card.recToggle() != nullptr);
    card.setCaptureOpen (true);   // (as Show me opens it: not the musician's)
    rig.tick();
    CHECK_FALSE (rig.stepDone());
    card.setCaptureOpen (false);
    rig.tick();
    card.recToggle()->onClick();   // REC (the key's own handler)
    rig.tick();
    REQUIRE (card.isCaptureOpen());
    REQUIRE (rig.stepDone());
    rig.next();
    REQUIRE (rig.stepId() == "input");
    {
        auto& content = *rig.learn.highlight()->getParentComponent();
        const auto meter = rig.learn.targets().boundsIn ("osp.capture.meter", content);
        REQUIRE (meter.has_value());
        auto* page = card.captureEditorComponent();
        CHECK (*meter == content.getLocalArea (page, page->pictureArea().getSmallestIntegerContainer()));
    }
    rig.next();
    REQUIRE (rig.stepId() == "grab");
    rig.next();
    REQUIRE (rig.stepId() == "take");
    // No input: the step waits and says how to connect one (Options > Audio/MIDI Settings...).
    CHECK (rig.learn.runner().state() == andor::learn::LessonRunner::State::blocked);
    CHECK (rig.learn.runner().unmetRequirement() != nullptr);
    CHECK (rig.learn.runner().unmetRequirement()->body.contains ("Options > Audio/MIDI Settings"));
    connectInput (rig.p);
    rig.tick();
    CHECK (rig.learn.runner().state() != andor::learn::LessonRunner::State::blocked);
    auto* page = card.captureEditorComponent();
    REQUIRE (page != nullptr);
    page->refresh();
    REQUIRE (page->shownView().connected);
    // RECORD from the page (a take starts: not yet kept), then STOP keeps it.
    clickPart (*page, page->recordArea());
    CHECK (rig.p.recordingLayer() == 0);
    rig.tick();
    CHECK_FALSE (rig.stepDone());
    feedInput (rig.p, capturePhrase ({ 69, 74, 77 }, 0.2));
    page->refresh();
    CHECK (page->onUserAction != nullptr);
    CHECK (clickPart (*page, page->recordArea()));   // STOP: kept (the page asks to close)
    rig.tick();
    REQUIRE (rig.stepDone());
    card.setCaptureOpen (false);
    rig.next();
    REQUIRE (rig.stepId() == "play");
    settleAnalysis (rig.p);
    rig.tick();
    CHECK (includedKeys (rig.p, 0) == std::set<int> { 69, 74, 77 });
    CHECK (rig.learn.runner().state() != andor::learn::LessonRunner::State::blocked);
    rig.playNote (74);
    REQUIRE (rig.stepDone());
}

TEST_CASE ("learn: GRAB LAST from the CAPTURE page completes the take step; the processor's own grab does not", "[plugin][learn][capture]")
{
    LearnRig rig (1086);
    loadLearnExample (rig, "bowed");
    connectInput (rig.p);
    const auto& lesson = *rig.learn.catalog().findLesson ("osp.capture");
    int take = -1;
    for (int s = 0; s < static_cast<int> (lesson.steps.size()); ++s)
        if (lesson.steps[static_cast<std::size_t> (s)].id == "take")
            take = s;
    REQUIRE (take > 0);
    rig.learn.setAutoAdvanceDelay (0);
    REQUIRE (rig.learn.startLesson ("osp.capture", take));
    rig.tick();
    auto& card = rig.ui.engineCard (0);
    if (! card.isCaptureOpen())
        card.recToggle()->onClick();   // (the step's own reveal may have opened it)
    rig.tick();
    auto* page = card.captureEditorComponent();
    REQUIRE (page != nullptr);
    feedInput (rig.p, capturePhrase ({ 72, 79 }, 0.2));
    REQUIRE (rig.p.grabLast (10.0, 1));   // (the processor's own call, into B: no click)
    rig.tick();
    CHECK_FALSE (rig.stepDone());
    page->refresh();
    CHECK (clickPart (*page, page->grabArea (0)));   // GRAB LAST 10 s
    rig.tick();
    CHECK (rig.stepDone());
    card.setCaptureOpen (false);
    settleAnalysis (rig.p);
}

TEST_CASE ("learn: Each layer its own arpeggio - PLAYS, LINK, EDIT B and B's own pattern, from the panel's clicks", "[plugin][learn][arp]")
{
    LearnRig rig (1448);
    loadLearnExample (rig, "layered");
    startOnCurrent (rig, "osp.arp.layers");
    openArpAndSwitchOn (rig);
    auto& arp = rig.ui.arpInlinePanel();
    REQUIRE (arp.showsCards());
    // PLAYS: the programmatic toggle reports nothing; a click on B's chip does.
    REQUIRE (rig.stepId() == "plays");
    arp.toggleCard (1);
    arp.toggleCard (1);
    rig.tick();
    CHECK_FALSE (rig.stepDone());
    arp.mouseDown (learnPress (arp, arp.cardChipArea (1).getCentre()));
    rig.tick();
    REQUIRE (rig.stepDone());
    arp.mouseDown (learnPress (arp, arp.cardChipArea (1).getCentre()));   // (and back)
    rig.next();
    REQUIRE (rig.stepId() == "link");
    rig.automate ("arp.link", 0.0f);   // (automation: not the musician's)
    CHECK_FALSE (rig.stepDone());
    rig.automate ("arp.link", 1.0f);
    arp.refresh();
    REQUIRE (arp.isLinked());
    arp.mouseDown (learnPress (arp, arp.linkArea().getCentre()));
    rig.tick();
    REQUIRE (rig.stepDone());
    CHECK_FALSE (arp.isLinked());
    rig.next();
    REQUIRE (rig.stepId() == "edit");
    arp.mouseDown (learnPress (arp, arp.editTabArea (1).getCentre()));
    rig.tick();
    REQUIRE (rig.stepDone());
    CHECK (arp.editedCard() == 1);
    rig.next();
    REQUIRE (rig.stepId() == "pattern");
    {
        auto& content = *rig.learn.highlight()->getParentComponent();
        const auto list = rig.learn.targets().boundsIn ("osp.arp.lane.pattern", content);
        REQUIRE (list.has_value());
        CHECK (arp.editedCard() == 1);   // (pointing at it never switches the card)
    }
    const auto before = static_cast<int> (rig.p.parameterValue ("arp.cardB.pattern"));
    const int other = before == static_cast<int> (osp::ArpPattern::down) ? static_cast<int> (osp::ArpPattern::up) : static_cast<int> (osp::ArpPattern::down);
    const auto row = arpRowInView (arp, other);
    REQUIRE_FALSE (row.isEmpty());
    arp.mouseDown (learnPress (arp, row.getCentre()));
    rig.tick();
    REQUIRE (rig.stepDone());
    CHECK (static_cast<int> (rig.p.parameterValue ("arp.cardB.pattern")) == other);
    CHECK (static_cast<int> (rig.p.parameterValue ("arp.pattern")) != other);   // (A kept its own)
    rig.next();
    CHECK (rig.stepId() == "relink");
}

TEST_CASE ("learn: Use a custom pattern - the row opens the browser, a factory pattern is chosen, the pencil opens the editor", "[plugin][learn][arp]")
{
    LearnRig rig (1448);
    loadLearnExample (rig, "pluck");
    startOnCurrent (rig, "osp.arp.custom");
    openArpAndSwitchOn (rig);
    auto& arp = rig.ui.arpInlinePanel();
    REQUIRE (rig.stepId() == "row");
    // Opened by the program (as Show me does): not the musician's.
    arp.openCustomBrowser();
    rig.tick();
    CHECK (rig.learn.targets().state ("osp.arp.custom.browser.open"));
    CHECK_FALSE (rig.stepDone());
    rig.ui.closePopup();
    rig.tick();
    // The row, clicked (the list scrolled to its end).
    scrollArpList (arp, 40);
    const auto row = arp.customRowArea();
    REQUIRE_FALSE (row.isEmpty());
    {
        auto& content = *rig.learn.highlight()->getParentComponent();
        const auto ring = rig.learn.targets().boundsIn ("osp.arp.custom.row", content);
        REQUIRE (ring.has_value());
        CHECK (*ring == content.getLocalArea (&arp, row.getSmallestIntegerContainer()));
    }
    arp.mouseDown (learnPress (arp, row.getCentre()));
    rig.ui.mouseDownAnywhere (&arp);
    if (! learnPump())
        return;   // (macOS: the browser opens on the app's run loop)
    rig.tick();
    REQUIRE (rig.stepDone());
    rig.next();
    REQUIRE (rig.stepId() == "choose");
    auto* browser = dynamic_cast<osp::plugin::CustomPatternBrowser*> (rig.ui.openPopupPanel());
    REQUIRE (browser != nullptr);
    using Kind = osp::plugin::CustomPatternBrowser::Kind;
    const int heading = browser->find (Kind::factoryHeading);
    REQUIRE (heading >= 0);
    if (! browser->factoryExpanded())
        browser->activate (heading);   // ▸ FACTORY PATTERNS (opens the group: chooses nothing)
    rig.tick();
    CHECK_FALSE (rig.stepDone());
    const int factory = browser->find (Kind::factory);
    REQUIRE (factory >= 0);
    browser->activate (factory);   // a factory pattern, clicked
    rig.tick();
    REQUIRE (rig.stepDone());
    CHECK (rig.p.parameterValue ("arp.custom") >= 0.5f);
    learnPump();
    rig.next();
    REQUIRE (rig.stepId() == "pencil");
    arp.refresh();
    const auto pencil = arp.pencilArea();
    REQUIRE_FALSE (pencil.isEmpty());
    arp.mouseDown (learnPress (arp, pencil.getCentre()));
    rig.ui.mouseDownAnywhere (&arp);
    learnPump();
    rig.tick();
    REQUIRE (rig.stepDone());
    CHECK (dynamic_cast<osp::plugin::CustomPatternEditor*> (rig.ui.openPopupPanel()) != nullptr);
    rig.next();
    REQUIRE (rig.stepId() == "editor");
    CHECK (rig.learn.targets().boundsIn ("osp.arp.custom.editor", *rig.learn.highlight()->getParentComponent()).has_value());
    rig.learn.noteAction ("close");
    rig.ui.closePopup();
    learnPump();
}

TEST_CASE ("learn: Strum a chord - Chord from the list, STRUM's light and name, DIRECTION and SPREAD", "[plugin][learn][arp]")
{
    LearnRig rig (1448);
    loadLearnExample (rig, "pluck");
    startOnCurrent (rig, "osp.arp.strum");
    openArpAndSwitchOn (rig);
    auto& arp = rig.ui.arpInlinePanel();
    auto* key = arp.strumKey();
    REQUIRE (key != nullptr);
    REQUIRE (rig.stepId() == "chord");
    const auto chord = static_cast<int> (osp::ArpPattern::chord);
    rig.automate ("arp.pattern", static_cast<float> (chord));   // (automation)
    CHECK_FALSE (rig.stepDone());
    rig.automate ("arp.pattern", 0.0f);
    const auto row = arpRowInView (arp, chord);
    REQUIRE_FALSE (row.isEmpty());
    arp.mouseDown (learnPress (arp, row.getCentre()));
    rig.tick();
    REQUIRE (rig.stepDone());
    CHECK (key->isAvailable());
    rig.next();
    REQUIRE (rig.stepId() == "strum");
    rig.automate ("arp.strum.on", 1.0f);
    CHECK_FALSE (rig.stepDone());
    rig.automate ("arp.strum.on", 0.0f);
    key->mouseDown (learnPress (*key, key->lightArea().getCentre().toFloat()));   // the light
    rig.tick();
    REQUIRE (rig.stepDone());
    CHECK (rig.p.parameterValue ("arp.strum.on") >= 0.5f);
    rig.next();
    REQUIRE (rig.stepId() == "panel");
    // The name: the key opens its panel, and Learn's own watcher hears the touch.
    const juce::Point<float> name (static_cast<float> (key->lightArea().getRight() + 12), static_cast<float> (key->getHeight()) * 0.5f);
    key->mouseDown (learnPress (*key, name));
    auto* watcher = rig.learn.touchWatcher ("osp.arp.strum");
    REQUIRE (watcher != nullptr);
    CHECK (watcher->watched() == key);
    watcher->mouseUp (learnPress (*key, name));
    rig.tick();
    REQUIRE (key->isOpen());
    REQUIRE (rig.stepDone());
    rig.next();
    REQUIRE (rig.stepId() == "direction");
    rig.automate ("arp.strum.direction", 2.0f);
    CHECK_FALSE (rig.stepDone());
    rig.userSet ("arp.strum.direction", 1.0f);   // (the segments' attachment: a gesture)
    REQUIRE (rig.stepDone());
    rig.next();
    REQUIRE (rig.stepId() == "spread");
    rig.userSet ("arp.strum.ms", 120.0f);
    REQUIRE (rig.stepDone());
    rig.ui.closePopup();
}

TEST_CASE ("learn: Move each layer - SATELLITE's name and light, PAN / ORBIT / BOUNCE and RATE", "[plugin][learn][arp]")
{
    LearnRig rig (1448);
    loadLearnExample (rig, "layered");
    startOnCurrent (rig, "osp.satellite");
    openArpAndSwitchOn (rig, false);
    auto& arp = rig.ui.arpInlinePanel();
    auto* key = arp.satelliteKey();
    REQUIRE (key != nullptr);
    REQUIRE (rig.stepId() == "panel");
    key->openPanel();   // (as Show me opens it: no touch)
    rig.tick();
    CHECK_FALSE (rig.stepDone());
    rig.ui.closePopup();
    rig.tick();
    const juce::Point<float> name (static_cast<float> (key->lightArea().getRight() + 12), static_cast<float> (key->getHeight()) * 0.5f);
    key->mouseDown (learnPress (*key, name));
    rig.learn.touchWatcher ("osp.arp.satellite")->mouseUp (learnPress (*key, name));
    rig.tick();
    REQUIRE (rig.stepDone());
    rig.next();
    REQUIRE (rig.stepId() == "on");
    const auto enabled = osp::plugin::InstrumentHost::satelliteFieldId (0, "enabled");
    rig.automate (enabled, 1.0f);
    CHECK_FALSE (rig.stepDone());
    rig.automate (enabled, 0.0f);
    key->mouseDown (learnPress (*key, key->lightArea().getCentre().toFloat()));
    rig.tick();
    REQUIRE (rig.stepDone());
    CHECK (key->anyOn());
    rig.next();
    REQUIRE (rig.stepId() == "mode");
    rig.userSet (osp::plugin::InstrumentHost::satelliteFieldId (0, "mode"), 1.0f);   // ORBIT
    REQUIRE (rig.stepDone());
    rig.next();
    REQUIRE (rig.stepId() == "rate");
    {
        auto* rate = rig.p.parameters.getParameter (osp::plugin::InstrumentHost::satelliteFieldId (0, "rate"));
        REQUIRE (rate != nullptr);
        rig.userSet (osp::plugin::InstrumentHost::satelliteFieldId (0, "rate"), rate->convertFrom0to1 (0.7f));
    }
    REQUIRE (rig.stepDone());
    rig.next();
    REQUIRE (rig.stepId() == "layers");
    rig.ui.closePopup();
}

TEST_CASE ("learn: Blend in SYNTH and IMPRINT - the chip opens the page; AMOUNT, WAVE, SHAPE, IMPRINT and MOTION", "[plugin][learn][synth]")
{
    LearnRig rig (1448);
    loadLearnExample (rig, "bowed");
    startOnCurrent (rig, "osp.synth");
    REQUIRE (rig.stepId() == "open");
    auto& card = rig.ui.engineCard (0);
    auto* chip = card.synthesizeControl();
    REQUIRE (chip != nullptr);
    card.setSynthOpen (true);   // (as Show me opens it)
    rig.tick();
    CHECK_FALSE (rig.stepDone());
    card.setSynthOpen (false);   // (the chip shows again: the page covered it)
    rig.tick();
    REQUIRE (chip->onOpenPage != nullptr);
    chip->onOpenPage();   // a click on SYNTH (the chip's own handler)
    rig.tick();
    REQUIRE (rig.stepDone());
    auto* page = card.synthEditorComponent();
    REQUIRE (page != nullptr);
    rig.next();
    REQUIRE (rig.stepId() == "amount");
    {
        // The ring: the page's own AMOUNT field (not the page, which carries its controls' tags).
        auto& content = *rig.learn.highlight()->getParentComponent();
        const auto amount = rig.learn.targets().boundsIn ("osp.synth.amount", content);
        REQUIRE (amount.has_value());
        CHECK (amount->getWidth() < page->getWidth() / 2);
    }
    const auto amountId = osp::plugin::InstrumentHost::synthesizeId (0);
    rig.automate (amountId, 30.0f);
    CHECK_FALSE (rig.stepDone());
    rig.userSet (amountId, 40.0f);
    REQUIRE (rig.stepDone());
    rig.next();
    REQUIRE (rig.stepId() == "wave");
    page->choose ("wave", static_cast<int> (rig.p.parameterValue (osp::plugin::InstrumentHost::synthParameterId (0, "wave"))) == 2 ? 1 : 2);
    rig.tick();
    REQUIRE (rig.stepDone());
    rig.next();
    REQUIRE (rig.stepId() == "shape");
    page->choose ("shape", rig.p.parameterValue (osp::plugin::InstrumentHost::synthParameterId (0, "shape")) >= 0.5f ? 0 : 1);
    rig.tick();
    REQUIRE (rig.stepDone());
    rig.next();
    REQUIRE (rig.stepId() == "imprint");
    page->choose ("mode", 1);   // IMPRINT
    rig.tick();
    REQUIRE (rig.stepDone());
    rig.next();
    REQUIRE (rig.stepId() == "much");
    page->refresh();
    rig.userSet (osp::plugin::InstrumentHost::synthParameterId (0, "imprint"), 60.0f);
    REQUIRE (rig.stepDone());
    rig.next();
    REQUIRE (rig.stepId() == "motion");
    rig.userSet (osp::plugin::InstrumentHost::synthParameterId (0, "motion"), 40.0f);
    REQUIRE (rig.stepDone());
}

TEST_CASE ("learn: the beta lessons' rings are the real controls and the note stays clear, at 869, 1086 and 1448 px", "[plugin][learn]")
{
    for (const int width : { 869, 1086, 1448 })
    {
        INFO ("width " << width);
        LearnRig rig (width);
        loadLearnExample (rig, "layered");
        rig.automate ("arp.pattern", static_cast<float> (osp::ArpPattern::chord));   // STRUM's steps need Chord
        struct Case
        {
            const char* lesson;
            const char* step;
            const char* target;
        };
        for (const auto& c : { Case { "osp.capture", "open", "osp.source.rec" }, Case { "osp.capture", "grab", "osp.capture.grab" },
                               Case { "osp.capture", "take", "osp.capture.record" }, Case { "osp.synth", "open", "osp.source.synth" },
                               Case { "osp.synth", "wave", "osp.synth.wave" }, Case { "osp.synth", "shape", "osp.synth.shape" },
                               Case { "osp.arp.layers", "plays", "osp.arp.cards" }, Case { "osp.arp.layers", "link", "osp.arp.link" },
                               Case { "osp.arp.layers", "edit", "osp.arp.edit.B" }, Case { "osp.arp.custom", "row", "osp.arp.custom.row" },
                               Case { "osp.arp.strum", "strum", "osp.arp.strum.light" }, Case { "osp.arp.strum", "direction", "osp.strum.direction" },
                               Case { "osp.arp.strum", "spread", "osp.strum.spread" }, Case { "osp.satellite", "panel", "osp.arp.satellite" },
                               Case { "osp.satellite", "mode", "osp.satellite.mode" }, Case { "osp.satellite", "rate", "osp.satellite.rate" } })
        {
            INFO (c.lesson << " / " << c.step);
            if (juce::String (c.step) == "edit")
                rig.automate ("arp.link", 0.0f);   // (EDIT shows with LINK off)
            if (juce::String (c.step) == "take")
                connectInput (rig.p);   // (the take needs an input)
            const auto& lesson = *rig.learn.catalog().findLesson (c.lesson);
            int index = -1;
            for (int s = 0; s < static_cast<int> (lesson.steps.size()); ++s)
                if (lesson.steps[static_cast<std::size_t> (s)].id == c.step)
                    index = s;
            REQUIRE (index >= 0);
            REQUIRE (rig.learn.startLesson (c.lesson, index));
            rig.tick();
            REQUIRE (rig.learn.noteKind() == LearnNote::lesson);
            REQUIRE (rig.learn.runner().state() != andor::learn::LessonRunner::State::blocked);
            rig.learn.noteAction ("showme");
            rig.tick();
            rig.learn.layoutNow();
            REQUIRE (rig.learn.highlight() != nullptr);
            auto& content = *rig.learn.highlight()->getParentComponent();
            const auto bounds = rig.learn.targets().boundsIn (c.target, content);
            REQUIRE (bounds.has_value());
            CHECK_FALSE (bounds->isEmpty());
            CHECK (content.getLocalBounds().contains (*bounds));
            REQUIRE (rig.learn.highlight()->target().has_value());
            CHECK (*rig.learn.highlight()->target() == *bounds);
            REQUIRE (rig.learn.note() != nullptr);
            const auto card = rig.learn.note()->getBounds().reduced (andor::learn::Note::margin);
            CHECK_FALSE (card.intersects (*bounds));
            CHECK (content.getLocalBounds().contains (card));
            rig.learn.noteAction ("close");
            rig.tick();
            rig.ui.closePopup();
            for (int layer = 0; layer < 2; ++layer)
            {
                rig.ui.engineCard (layer).setCaptureOpen (false);
                rig.ui.engineCard (layer).setSynthOpen (false);
            }
            rig.tick();
        }
    }
}
