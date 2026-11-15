#pragma once

#include "LearnActionBridge.h"
#include "LearnBrowser.h"
#include "LearnOverlay.h"
#include "LearnTargetRegistry.h"
#include "LessonCatalog.h"
#include "LessonRunner.h"
#include "ProgressStore.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace andor::learn
{

/** What the controller needs from its editor (all message thread). */
struct EditorHooks
{
    /** The overlay's parent and coordinate space (the instrument's scaled content). */
    juce::Component* content = nullptr;
    /** The header menu key's bounds in `content` (the browser and invitation sit below it). */
    std::function<juce::Rectangle<int>()> menuAnchor;
    /** Closes the editor's open popover (before the browser or a note takes the stage). */
    std::function<void()> closePopovers;
    /** The editor is on screen (the invitation waits for it). */
    std::function<bool()> showing;
    /** A parameter's value in its real units, or nothing. */
    std::function<std::optional<float> (const juce::String&)> parameterValue;
    /** The product has a capability (lessons for absent features are not offered). */
    std::function<bool (const juce::String&)> hasCapability;
    /** The product's name in notes ("ANDOR OSP"). */
    juce::String productName;

    // Lesson examples (Preparation::exampleId), all optional: without them no example is offered.
    /** Keeps the current sound (so it can be restored), then starts loading the example. False:
        nothing was changed (unknown example, or the sound could not be kept). */
    std::function<bool (const juce::String& exampleId)> loadExample;
    /** The example finished loading and is playable (with Preparation::ready, the lesson's own). */
    std::function<bool()> exampleReady;
    /** The example failed to load. */
    std::function<bool()> exampleFailed;
    /** The sound from before a tutorial example can be restored now. */
    std::function<bool()> canRestore;
    /** Restores it. */
    std::function<bool()> restore;
};

/**
    ANDOR Learn for one editor: the lesson runner, the targets, the per-user progress, and the
    temporary teaching layer (focus ring, note, Learn mode regions, browser, invitation).

    Nothing is added to the editor until the musician asks for it (or the one-time invitation
    appears): while idle there is no child component, no timer of its own (the editor's tick
    returns at once) and nothing painted. All of it lives on the message thread; nothing here
    writes a parameter or touches the audio thread. Closing anything leaves the sound as the
    musician last set it.
*/
class LearnController final : private RunnerHost
{
public:
    LearnController (Catalog, EditorHooks);
    ~LearnController() override;

    TargetRegistry& targets() noexcept { return registry; }
    /** After every target is registered: validates the catalog against them, keeps the lessons
        the product can run, reads the musician's progress. A catalog with problems disables
        Learn (problems() says why; the menu says it is unavailable). */
    void finishSetup();
    bool isUsable() const noexcept { return usable; }
    const juce::StringArray& problems() const noexcept { return setupProblems; }

    // The editor's hooks.
    /** The two items (and the Learn submenu) for the header menu. */
    void addMenuItems (juce::PopupMenu&);
    /** A control's committed gesture (see actions::). Only user origins can complete steps. */
    void userAction (const juce::String& actionId, const juce::String& targetId, const juce::String& parameterId, Origin);
    void userAction (const LearnEvent&);
    /** Escape: the note first, then the browser, then Learn mode. False: not Learn's key. */
    bool keyPressed (const juce::KeyPress&);
    /** The editor's timer: the invitation's schedule, the overlay following its targets. */
    void tick();

    // What the musician can do (also the tests' entry points).
    bool startLesson (const juce::String& lessonId);
    /** Starts a lesson at one of its steps (tests, snapshots), through its preparation as Start does. */
    bool startLessonAt (const juce::String& lessonId, int step);
    void openBrowser();
    void closeBrowser();
    /** A lesson chosen in the browser: resumed where it was left (in progress), else from the start. */
    void chooseLesson (juce::String lessonId);
    void setLearnMode (bool on);
    bool learnMode() const noexcept { return inspecting; }
    void inspect (const juce::String& targetId);
    void resetProgress();
    /** Reset Tutorials...: asks first (a ceramic confirmation), then resets this product's lessons only. */
    void askToResetProgress();
    /** The sound from before a tutorial example, back (when the product can). */
    bool restorePreviousSound();
    /** The lesson waiting for its preparation (the example loading), or empty. */
    const juce::String& preparingLessonId() const noexcept { return preparingLesson; }
    /** Shows the invitation now (tests; the editor's schedule calls it once). */
    void showInvitation();
    /** A note's key, by its action ID (as its click does). */
    void noteAction (const juce::String& actionId);

    // State (tests, snapshots).
    LessonRunner& runner() noexcept { return lessonRunner; }
    const Catalog& catalog() const noexcept { return lessonRunner.catalog(); }
    ProgressStore& store() noexcept { return progressStore; }
    Note* note() const noexcept { return notePanel.get(); }
    Browser* browser() const noexcept { return browserPanel.get(); }
    HighlightLayer* highlight() const noexcept { return ring.get(); }
    juce::Component* banner() const noexcept { return bannerNote.get(); }
    std::vector<HitRegion*> regions() const;
    /** The watcher of a target's touches (Target::watchTouches), or nullptr. Tests deliver the
        mouse events the operating system would (headless editors have no window). */
    TouchWatcher* touchWatcher (const juce::String& targetId) const;
    enum class NoteKind { none, lesson, help, invitation, complete, prepare, preparing, restoreOffer, confirmReset };
    NoteKind noteKind() const noexcept { return shownNote; }
    const juce::String& inspectedTarget() const noexcept { return helpTarget; }
    bool isActive() const noexcept;
    /** Lays the overlay out now (tests and snapshots; the tick does it while active). */
    void layoutNow();

private:
    // RunnerHost
    std::optional<float> parameterValue (const juce::String&) override;
    bool state (const juce::String&) override;
    bool targetReady (const juce::String&) override;
    bool reveal (const juce::String&) override;

    bool beginLesson (const juce::String& lessonId, int fromStep);
    bool conditionHolds (const Condition&);
    void loadExampleFor (const juce::String& lessonId);
    void updatePreparing();
    void offerRestoreIfExample();
    juce::String productName() const;
    void ensureLayers();
    void releaseLayersIfIdle();
    void refreshNote();
    void showNote (NoteKind);
    void hideNote();
    void placeNote (std::optional<juce::Rectangle<int>> anchor);
    void placeBelowMenu (juce::Component&);
    void updateRegions();
    void raise();
    void persist (const Lesson&, int stepIndex, bool finished, bool allStepsDone);
    void markInvitationSeen();
    std::vector<Browser::Row> browserRows() const;
    int resumeStep (const Lesson&) const;
    juce::Rectangle<int> contentBounds() const;

    Catalog fullCatalog;
    EditorHooks hooks;
    TargetRegistry registry;
    LessonRunner lessonRunner { *this };
    ProgressStore progressStore;
    ProgressData progress;
    bool usable = false;
    juce::StringArray setupProblems;
    JUCE_DECLARE_WEAK_REFERENCEABLE (LearnController)

    std::unique_ptr<HighlightLayer> ring;
    std::unique_ptr<Note> notePanel;
    std::unique_ptr<Browser> browserPanel;
    std::unique_ptr<Note> bannerNote;
    std::vector<std::unique_ptr<HitRegion>> hitRegions;
    std::vector<std::unique_ptr<TouchWatcher>> watchers;
    NoteKind shownNote = NoteKind::none;
    juce::String noteSignature, helpTarget, hoverTarget;
    bool inspecting = false;
    bool invitationPending = false, invitationClaimed = false;
    int showingTicks = 0;
    juce::String preparingLesson;
    int preparingStep = 0, preparingTicks = 0;
    bool prepareFailed = false, exampleLoaded = false;
};

} // namespace andor::learn
