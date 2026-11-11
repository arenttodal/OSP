// ANDOR Learn (shared/andor-learn), compiled into every shell product's test program: the
// lesson data checks, the runner's completion rules (only a user event of the current step,
// after which the postcondition holds), the per-user progress file, and the product opt-in
// (a product without a catalog has no controller, no menu items, no components).
#include "ShellEditor.h"
#include "ShellProcessor.h"
#include "LessonCatalog.h"
#include "LessonRunner.h"
#include "ProgressStore.h"

#include <catch2/catch_test_macros.hpp>

#include <map>

using namespace andor::learn;

namespace
{
    void setLearnDir (const juce::String& value)
    {
#if JUCE_WINDOWS
        _putenv_s ("ANDOR_LEARN_DIR", value.toRawUTF8());
#else
        setenv ("ANDOR_LEARN_DIR", value.toRawUTF8(), 1);
#endif
    }

    struct ScopedLearnDir
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("andor-learn-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        juce::String before = juce::SystemStats::getEnvironmentVariable ("ANDOR_LEARN_DIR", {});
        ScopedLearnDir()
        {
            dir.createDirectory();
            setLearnDir (dir.getFullPathName());
        }
        ~ScopedLearnDir()
        {
            setLearnDir (before);
            dir.deleteRecursively();
        }
    };

    Step step (const char* id, const char* title, const char* body, const char* target, Completion completion)
    {
        Step s;
        s.id = id;
        s.title = title;
        s.body = body;
        s.targetId = target;
        s.completion = std::move (completion);
        return s;
    }

    /** A test-only catalog (the sample definition a product would write): one explanation,
        three tasks, a summary. */
    Catalog testCatalog()
    {
        Catalog c;
        c.productId = "learntest";
        Lesson l;
        l.id = "test.basics";
        l.title = "Basics";
        l.group = "Tests";
        l.steps.push_back (step ("intro", "Intro", "Read this.", "", Completion::manual()));
        l.steps.push_back (step ("open", "Open", "Open the panel.", "panel", Completion::action ({ "panel.opened" }, Condition::state ("panel.open"))
                                                                                .withAlreadyDone (Condition::state ("panel.open"), "Already open")));
        auto& open = l.steps.back();
        open.revealOnEnter = false;
        l.steps.push_back (step ("change", "Change", "Change the rate.", "rate", Completion::action ({ "rate.changed" }, Condition::changed ("rate"))));
        l.steps.push_back (step ("cards", "Cards", "A off, B on.", "cards",
                                 Completion::action ({ "card.toggled" }, Condition::all ({ Condition::equals ("cardA", 0.0f), Condition::equals ("cardB", 1.0f) }))));
        l.steps.push_back (step ("done", "Done", "Finished.", "", Completion::manual()));
        c.lessons.push_back (l);
        Lesson other;
        other.id = "test.future";
        other.title = "Future feature";
        other.capability = "future";
        other.steps.push_back (step ("only", "Only", "x", "", Completion::manual()));
        c.lessons.push_back (other);
        c.help.push_back ({ "panel", "The panel", "It does things.", "test.basics" });
        c.help.push_back ({ "rate", "Rate", "Speed.", "test.future" });
        return c;
    }

    struct FakeHost final : RunnerHost
    {
        std::map<juce::String, float> values { { "rate", 3.0f }, { "cardA", 1.0f }, { "cardB", 1.0f } };
        std::map<juce::String, bool> states { { "panel.open", false } };
        std::map<juce::String, bool> visible { { "panel", true }, { "rate", true }, { "cards", true } };
        juce::StringArray revealed;
        std::optional<float> parameterValue (const juce::String& id) override
        {
            const auto it = values.find (id);
            return it == values.end() ? std::nullopt : std::optional<float> (it->second);
        }
        bool state (const juce::String& id) override { return states[id]; }
        bool targetReady (const juce::String& id) override { return visible[id]; }
        bool reveal (const juce::String& id) override
        {
            revealed.add (id);
            if (id == "panel")
                states["panel.open"] = true;
            visible[id] = true;
            return true;
        }
    };

    LearnEvent userEvent (LessonRunner& r, const juce::String& action, Origin origin = Origin::userPointer)
    {
        LearnEvent e;
        e.actionId = action;
        e.origin = origin;
        e.stepGeneration = r.generation();
        return e;
    }
} // namespace

TEST_CASE ("learn: catalog validation finds every authoring mistake", "[learn][unit]")
{
    auto c = testCatalog();
    CatalogChecks checks;
    checks.targetKnown = [] (const juce::String& id) { return id == "panel" || id == "rate" || id == "cards"; };
    checks.stateKnown = [] (const juce::String& id) { return id == "panel.open"; };
    checks.parameterKnown = [] (const juce::String& id) { return id == "rate" || id == "cardA" || id == "cardB"; };
    CHECK (validate (c, checks).isEmpty());

    auto broken = c;
    broken.lessons.push_back (broken.lessons.front());                                   // duplicate lesson
    broken.lessons[0].steps.push_back (broken.lessons[0].steps.front());                  // duplicate step
    broken.lessons[0].steps[2].targetId = "nowhere";                                      // unknown target
    broken.lessons[0].steps[3].completion.actions.clear();                                // accepts nothing
    broken.lessons[0].steps[2].completion.postcondition = Condition::changed ("missing"); // unknown parameter
    broken.help.push_back ({ "panel", "Again", "Twice.", "" });                           // duplicate help
    broken.help.push_back ({ "cards", "Cards", "x", "no.such.lesson" });                  // unknown lesson
    broken.quickStartLessonId = "absent";
    const auto problems = validate (broken, checks).joinIntoString ("\n");
    INFO (problems);
    CHECK (problems.contains ("duplicate lesson test.basics"));
    CHECK (problems.contains ("duplicate step test.basics/intro"));
    CHECK (problems.contains ("unknown target nowhere"));
    CHECK (problems.contains ("accepts no action"));
    CHECK (problems.contains ("unknown parameter missing"));
    CHECK (problems.contains ("duplicate help for panel"));
    CHECK (problems.contains ("unknown lesson no.such.lesson"));
    CHECK (problems.contains ("unknown quick start lesson absent"));
}

TEST_CASE ("learn: lessons for capabilities the product lacks are never offered", "[learn][unit]")
{
    const auto c = testCatalog();
    const auto offered = available (c, [] (const juce::String&) { return false; });
    REQUIRE (offered.lessons.size() == 1);
    CHECK (offered.lessons[0].id == "test.basics");
    CHECK (offered.findHelp ("rate")->lessonId.isEmpty());   // no Start lesson for a hidden lesson
    CHECK (offered.findHelp ("panel")->lessonId == "test.basics");
    CHECK (available (c, [] (const juce::String& cap) { return cap == "future"; }).lessons.size() == 2);
}

TEST_CASE ("learn: only a user event of the current step, with its postcondition, completes it", "[learn][unit]")
{
    FakeHost host;
    LessonRunner r (host);
    r.setCatalog (testCatalog());
    REQUIRE (r.start ("test.basics"));
    CHECK (r.state() == LessonRunner::State::tourShowing);   // an explanation: Next only
    CHECK_FALSE (r.handle (userEvent (r, "panel.opened")));
    r.next();

    // Step 2: open the panel. revealOnEnter is off: the step does not do the task itself.
    CHECK (r.stepIndex() == 1);
    CHECK (r.state() == LessonRunner::State::waitingForAction);
    CHECK_FALSE (host.revealed.contains ("panel"));
    CHECK_FALSE (r.canContinue());
    // An event without the postcondition (the panel is still closed) completes nothing.
    CHECK_FALSE (r.handle (userEvent (r, "panel.opened")));
    host.states["panel.open"] = true;
    // Host automation, a preset, the tutorial itself, an unknown source: never.
    for (auto origin : { Origin::hostAutomation, Origin::presetRecall, Origin::tutorial, Origin::unknown })
        CHECK_FALSE (r.handle (userEvent (r, "panel.opened", origin)));
    // An action the step does not accept: no.
    CHECK_FALSE (r.handle (userEvent (r, "rate.changed")));
    // An event stamped for another step: no.
    auto stale = userEvent (r, "panel.opened");
    stale.stepGeneration -= 1;
    CHECK_FALSE (r.handle (stale));
    // The musician's own: yes.
    CHECK (r.handle (userEvent (r, "panel.opened", Origin::userKeyboard)));
    CHECK (r.state() == LessonRunner::State::stepComplete);
    CHECK (r.status (1) == LessonRunner::StepStatus::done);
    // Only the musician moves on.
    CHECK (r.stepIndex() == 1);
    r.next();

    // Step 3: change the rate (compared with its value when the step began).
    REQUIRE (r.stepIndex() == 2);
    const auto event = userEvent (r, "rate.changed");
    CHECK_FALSE (r.handle (event));   // nothing changed yet
    host.values["rate"] = 5.0f;
    CHECK (r.handle (event));
    // The same event can never complete the next step (its generation is spent).
    r.next();
    REQUIRE (r.stepIndex() == 3);
    host.values["cardA"] = 0.0f;
    CHECK_FALSE (r.handle (event));
    // Step 4: A off and B on, verified as a whole.
    host.values["cardB"] = 0.0f;
    CHECK_FALSE (r.handle (userEvent (r, "card.toggled")));
    host.values["cardB"] = 1.0f;
    CHECK (r.handle (userEvent (r, "card.toggled")));
}

TEST_CASE ("learn: a parameter changed by automation before the gesture cannot pass for the musician's", "[learn][unit]")
{
    FakeHost host;
    LessonRunner r (host);
    r.setCatalog (testCatalog());
    REQUIRE (r.start ("test.basics", 2));
    // Automation moves the rate: no user event, nothing completes, however long it plays.
    host.values["rate"] = 9.0f;
    r.update();
    CHECK (r.state() == LessonRunner::State::waitingForAction);
    CHECK_FALSE (r.handle (userEvent (r, "rate.changed", Origin::hostAutomation)));
    // Nor does a later user event that changed nothing itself (the baseline followed it).
    CHECK_FALSE (r.handle (userEvent (r, "rate.changed")));
    CHECK (r.status (2) == LessonRunner::StepStatus::pending);
    // While the musician holds a knob, the baseline holds still: their drag counts.
    r.setGestureActive (true);
    host.values["rate"] = 10.0f;
    r.update();
    r.setGestureActive (false);
    CHECK (r.handle (userEvent (r, "rate.changed")));
}

TEST_CASE ("learn: skip, back, continue, pause and close", "[learn][unit]")
{
    FakeHost host;
    LessonRunner r (host);
    r.setCatalog (testCatalog());
    std::vector<std::tuple<juce::String, int, bool, bool>> reports;
    r.onProgress = [&] (const Lesson& l, int s, bool finished, bool all) { reports.emplace_back (l.id, s, finished, all); };
    REQUIRE (r.start ("test.basics", 1));
    // Already done when it began: Continue is offered (and marks it continued, not done).
    host.states["panel.open"] = true;
    CHECK (r.canContinue());
    CHECK (r.continueAlreadyDone());
    CHECK (r.status (1) == LessonRunner::StepStatus::continued);
    // Skip marks skipped and moves on.
    REQUIRE (r.stepIndex() == 2);
    r.skip();
    CHECK (r.status (2) == LessonRunner::StepStatus::skipped);
    CHECK (r.stepIndex() == 3);
    // Back returns without restoring anything; the step's task is asked again.
    host.values["rate"] = 7.0f;
    r.back();
    CHECK (r.stepIndex() == 2);
    CHECK (juce::exactlyEqual (host.values["rate"], 7.0f));
    CHECK (r.status (2) == LessonRunner::StepStatus::pending);
    // A target that disappears pauses the step; Show me brings it back.
    host.visible["rate"] = false;
    r.update();
    CHECK (r.state() == LessonRunner::State::paused);
    CHECK (r.currentBody().contains ("Show me"));
    CHECK_FALSE (r.handle (userEvent (r, "rate.changed")));   // nothing to click while paused
    CHECK (r.showMe());
    CHECK (r.state() == LessonRunner::State::waitingForAction);
    // Close reports the step to resume from; nothing else happens to the values.
    r.close();
    CHECK (r.state() == LessonRunner::State::stopped);
    REQUIRE (reports.size() == 1);
    CHECK (std::get<0> (reports[0]) == "test.basics");
    CHECK (std::get<1> (reports[0]) == 2);
    CHECK_FALSE (std::get<2> (reports[0]));
    // Finishing with a skipped task is not "all done".
    REQUIRE (r.start ("test.basics", 3));
    r.skip();
    r.next();
    REQUIRE (r.state() == LessonRunner::State::tourComplete);
    CHECK (std::get<2> (reports.back()));
    CHECK_FALSE (std::get<3> (reports.back()));
    CHECK_FALSE (r.start ("no.such.lesson"));
    CHECK_FALSE (r.start ("test.basics", 99));
}

TEST_CASE ("learn: progress is per user and per product, written safely, corrupt files tolerated", "[learn][unit]")
{
    ScopedLearnDir scoped;
    ProgressStore a ("analog"), o ("osp");
    CHECK (a.file().getParentDirectory() == scoped.dir);
    CHECK (a.read().lessons.empty());   // missing file: empty
    REQUIRE (a.update ([] (ProgressData& d) {
        d.firstInviteSeen = true;
        d.lessons["analog.arp.basics"] = { 1, "complete", "2026-11-10T00:00:00Z", {} };
    }));
    REQUIRE (o.update ([] (ProgressData& d) { d.lessons["osp.first-sound"] = { 2, "in-progress", {}, "load" }; }));
    const auto read = a.read();
    CHECK (read.firstInviteSeen);
    CHECK (read.lessons.at ("analog.arp.basics").status == "complete");
    CHECK (read.lessons.count ("osp.first-sound") == 0);   // another product's namespace
    CHECK (o.read().lessons.at ("osp.first-sound").lastStep == "load");
    const auto json = juce::JSON::parse (a.file().loadFileAsString());
    CHECK (static_cast<int> (json.getProperty ("schema", 0)) == 1);
    CHECK (json.getProperty ("product", {}).toString() == "analog");
    // Fields from a later version are kept.
    a.file().replaceWithText (a.file().loadFileAsString().replace ("\"schema\"", "\"futureField\": 42, \"schema\""));
    REQUIRE (a.update ([] (ProgressData& d) { d.learnModeEnabled = true; }));
    CHECK (static_cast<int> (juce::JSON::parse (a.file().loadFileAsString()).getProperty ("futureField", 0)) == 42);
    // A corrupt file reads as empty, is kept beside it, and the next write replaces it.
    a.file().replaceWithText ("{ not json");
    CHECK (a.read().lessons.empty());
    CHECK_FALSE (a.read().firstInviteSeen);
    REQUIRE (a.update ([] (ProgressData& d) { d.firstInviteSeen = true; }));
    CHECK (a.file().withFileExtension ("corrupt").existsAsFile());
    CHECK (a.read().firstInviteSeen);
    // Reset forgets lessons and the invitation, nothing else of the product's.
    REQUIRE (a.reset());
    CHECK (a.read().lessons.empty());
    CHECK_FALSE (a.read().firstInviteSeen);
    // One invitation per product in a process.
    CHECK (ProgressStore::claimInvitation ("x"));
    CHECK_FALSE (ProgressStore::claimInvitation ("x"));
    ProgressStore::releaseInvitation ("x");
    CHECK (ProgressStore::claimInvitation ("x"));
    ProgressStore::releaseInvitation ("x");
}

TEST_CASE ("learn: a product without a catalog has no Learn at all", "[learn][integration]")
{
    ScopedLearnDir scoped;
    andor::shell::ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& shellEditor = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    if (! andor::shell::productInfo().learn.has_value())
    {
        CHECK_FALSE (p.learnEnabled());
        CHECK (shellEditor.learnController() == nullptr);
        CHECK_FALSE (scoped.dir.getChildFile ("example.json").existsAsFile());
        return;
    }
    // A product with a catalog: valid against the editor's targets, and idle until asked.
    auto* learn = shellEditor.learnController();
    REQUIRE (learn != nullptr);
    INFO (learn->problems().joinIntoString ("\n"));
    CHECK (learn->isUsable());
    CHECK_FALSE (learn->isActive());
    CHECK (learn->note() == nullptr);
    CHECK (learn->highlight() == nullptr);
    CHECK (learn->regions().empty());
    // Headless (never on screen): no invitation, and nothing was written.
    for (int i = 0; i < 40; ++i)
        learn->tick();
    CHECK (learn->note() == nullptr);
    CHECK (scoped.dir.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0);
}
