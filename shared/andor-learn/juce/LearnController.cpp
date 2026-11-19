#include "LearnController.h"

#include <limits>

#include <algorithm>

namespace andor::learn
{

namespace
{
    constexpr int noteGap = 10;   ///< reference px between a target and its note
    juce::String ellipsis() { return juce::String::fromUTF8 ("\xe2\x80\xa6"); }
    juce::String dot() { return juce::String::fromUTF8 (" \xc2\xb7 "); }
} // namespace

LearnController::LearnController (Catalog c, EditorHooks h)
    : fullCatalog (std::move (c)), hooks (std::move (h)), progressStore (fullCatalog.productId)
{
    jassert (hooks.content != nullptr);
}

LearnController::~LearnController()
{
    lessonRunner.onChanged = nullptr;
    lessonRunner.onProgress = nullptr;
    // An editor closed mid-tour keeps the lesson's progress (as Close does), not its place on
    // screen: a new editor never resurrects a tour.
    if (lessonRunner.inTour())
        persist (*lessonRunner.lesson(), lessonRunner.stepIndex(), false, false);
    if (invitationClaimed)
        ProgressStore::releaseInvitation (fullCatalog.productId);
    watchers.clear();
    hitRegions.clear();
    notePanel.reset();
    browserPanel.reset();
    bannerNote.reset();
    ring.reset();
}

void LearnController::finishSetup()
{
    const auto offered = available (fullCatalog, hooks.hasCapability);
    CatalogChecks checks;
    checks.targetKnown = [this] (const juce::String& id) { return registry.contains (id); };
    checks.stateKnown = [this] (const juce::String& id) { return registry.hasState (id); };
    checks.parameterKnown = [this] (const juce::String& id) { return registry.hasValue (id) || (hooks.parameterValue != nullptr && hooks.parameterValue (id).has_value()); };
    setupProblems = validate (offered, checks);
    usable = setupProblems.isEmpty();
    if (! usable)
    {
        for (const auto& problem : setupProblems)
        {
            DBG ("ANDOR Learn: " << problem);
            juce::ignoreUnused (problem);
        }
        return;
    }
    lessonRunner.setCatalog (offered);
    lessonRunner.onChanged = [this] { refreshNote(); };
    lessonRunner.onProgress = [this] (const Lesson& l, int s, bool finished, bool all) { persist (l, s, finished, all); };
    progress = progressStore.read();
    invitationPending = hooks.offerInvitation && ! progress.firstInviteSeen;
    for (const auto& t : registry.all())
        if (t.watchTouches)
        {
            auto w = std::make_unique<TouchWatcher> (t.id, [this] (const LearnEvent& e) { userAction (e); },
                                                     [this] (bool active) { lessonRunner.setGestureActive (active); });
            w->watch (t.component());
            watchers.push_back (std::move (w));
        }
}

//==============================================================================
void LearnController::addMenuItems (juce::PopupMenu& menu)
{
    menu.addSeparator();
    if (! usable)
    {
        menu.addItem ("Learn (unavailable)", false, false, [] {});
        return;
    }
    // One entry: Learn, with Tutorials... and Reset Tutorials... inside. (Info has its own key
    // beneath ARP: KeyboardUtilityActions' INFO.)
    juce::WeakReference<LearnController> weak (this);
    juce::PopupMenu sub;
    sub.addSectionHeader ("LEARN");
    sub.addItem ("Tutorials" + ellipsis(), [weak] {
        if (weak != nullptr)
            weak->openBrowser();
    });
    if (hooks.canRestoreSound != nullptr && hooks.canRestoreSound())
    {
        // (while the sound kept before an example can still be brought back)
        sub.addSeparator();
        sub.addItem ("Restore Previous Sound", [weak] {
            if (weak != nullptr)
                weak->restorePreviousSound();
        });
    }
    sub.addSeparator();
    sub.addItem ("Reset Tutorials" + ellipsis(), [weak] {
        if (weak != nullptr)
            weak->confirmResetProgress();
    });
    menu.addSubMenu ("Learn", sub);
}

void LearnController::addSupportItems (juce::PopupMenu& menu)
{
    if (! usable)
        return;
    juce::WeakReference<LearnController> weak (this);
    menu.addItem ("Tutorials" + ellipsis(), [weak] {
        if (weak != nullptr)
            weak->openBrowser();
    });
    if (hooks.canRestoreSound != nullptr && hooks.canRestoreSound())
        menu.addItem ("Restore Previous Sound", [weak] {
            if (weak != nullptr)
                weak->restorePreviousSound();
        });
    menu.addItem ("Reset Tutorials" + ellipsis(), [weak] {
        if (weak != nullptr)
            weak->confirmResetProgress();
    });
}

void LearnController::userAction (const juce::String& actionId, const juce::String& targetId, const juce::String& parameterId, Origin origin)
{
    LearnEvent e;
    e.actionId = actionId;
    e.targetId = targetId;
    e.parameterId = parameterId;
    e.origin = origin;
    userAction (e);
}

void LearnController::userAction (const LearnEvent& event)
{
    if (! usable || ! lessonRunner.inTour())
        return;
    auto e = event;
    // Stamped now, in the control's own handler: a step entered later can never take it.
    e.stepGeneration = lessonRunner.generation();
    lessonRunner.handle (e);
    openBrowserAfterLesson();
}

void LearnController::autoAdvance()
{
    // Done by the musician (never "already so": that waits for Continue), the step moves on by
    // itself after a short pause, so the lesson flows; Next still moves at once.
    using State = LessonRunner::State;
    if (autoAdvanceMs <= 0 || shownNote != NoteKind::lesson || lessonRunner.state() != State::stepComplete
        || lessonRunner.status (lessonRunner.stepIndex()) != LessonRunner::StepStatus::done)
    {
        doneSince = 0.0;
        return;
    }
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (doneSince <= 0.0 || doneGeneration != lessonRunner.generation() || lessonRunner.gestureActive())
    {
        // (a knob still held: the pause starts when it is let go)
        doneGeneration = lessonRunner.generation();
        doneSince = now;
        return;
    }
    if (now - doneSince >= static_cast<double> (autoAdvanceMs))
    {
        doneSince = 0.0;
        noteAction ("next");
    }
}

bool LearnController::offersRestoreChoice() const
{
    const auto* lesson = lessonRunner.lesson();
    return lesson != nullptr && lesson->exampleId.isNotEmpty() && hooks.canRestoreSound != nullptr && hooks.canRestoreSound();
}

void LearnController::openBrowserAfterLesson()
{
    // Every lesson ends in the tutorials (its tick shows it done): the next one is one click
    // away. Only a lesson that played on its example asks first (keep it, or the sound before).
    if (! browserAfterLesson)
        return;
    browserAfterLesson = false;
    openBrowser();
}

bool LearnController::keyPressed (const juce::KeyPress& key)
{
    if (! usable || key != juce::KeyPress::escapeKey)
        return false;
    if (notePanel != nullptr)
    {
        noteAction ("close");
        return true;
    }
    if (browserPanel != nullptr)
    {
        closeBrowser();
        return true;
    }
    if (inspecting)
    {
        setLearnMode (false);
        return true;
    }
    return false;
}

void LearnController::tick()
{
    if (! usable)
        return;
    openBrowserAfterLesson();
    if (invitationPending)
    {
        // Once, after the editor is on screen and its layout settled (about half a second).
        if (hooks.showing != nullptr && hooks.showing())
        {
            if (++showingTicks > 15)
            {
                invitationPending = false;
                showInvitation();
            }
        }
        else
            showingTicks = 0;
    }
    if (! isActive())
        return;
    autoAdvance();
    if (inspecting)
        followPointer();
    // Controls rebuilt since (a routing view created on demand) are watched again.
    for (auto& w : watchers)
        if (const auto* t = registry.find (w->targetId()))
            w->watch (t->component());
    lessonRunner.update();
    refreshNote();
    layoutNow();
}

bool LearnController::isActive() const noexcept
{
    return notePanel != nullptr || browserPanel != nullptr || inspecting;
}

//==============================================================================
bool LearnController::startLesson (const juce::String& lessonId, int fromStep)
{
    if (! usable || lessonRunner.catalog().findLesson (lessonId) == nullptr)
        return false;
    closeBrowser();
    if (inspecting)
        setLearnMode (false);
    if (hooks.closePopovers != nullptr)
        hooks.closePopovers();
    ensureLayers();
    exampleFailed = false;
    if (! lessonRunner.start (lessonId, fromStep))
        return false;
    showNote (NoteKind::lesson);
    return true;
}

void LearnController::openBrowser()
{
    if (! usable)
        return;
    if (lessonRunner.inTour())
        lessonRunner.close();
    if (shownNote == NoteKind::invitation)
        markInvitationSeen();
    hideNote();
    if (inspecting)
        setLearnMode (false);
    if (hooks.closePopovers != nullptr)
        hooks.closePopovers();
    ensureLayers();
    if (browserPanel == nullptr)
    {
        browserPanel = std::make_unique<Browser>();
        browserPanel->onClose = [this] { closeBrowser(); };
        browserPanel->onChoose = [this] (const juce::String& chosen) { chooseLesson (chosen); };
        hooks.content->addAndMakeVisible (*browserPanel);
    }
    progress = progressStore.read();   // (another instance of the plugin may have moved on)
    browserPanel->setRows (browserRows());
    lessonRunner.setBrowsing (true);
    layoutNow();
    // The arrow keys and Return go to the list while it is open (Escape still closes it).
    if (browserPanel->isShowing())
        browserPanel->grabKeyboardFocus();
}

void LearnController::chooseLesson (juce::String lessonId)
{
    // (By value: closing the browser destroys the row and the callback that named it.)
    const auto* lesson = lessonRunner.catalog().findLesson (lessonId);
    if (lesson == nullptr)
        return;
    const int from = resumeStep (*lesson);
    closeBrowser();
    ensureLayers();
    if (hooks.closePopovers != nullptr)
        hooks.closePopovers();
    exampleFailed = false;
    if (lessonRunner.start (lessonId, from))
        showNote (NoteKind::lesson);
}

void LearnController::closeBrowser()
{
    if (browserPanel == nullptr)
        return;
    browserPanel.reset();
    lessonRunner.setBrowsing (false);
    releaseLayersIfIdle();
}

void LearnController::setLearnMode (bool on)
{
    if (! usable || on == inspecting)
        return;
    if (on)
    {
        if (lessonRunner.inTour())
            lessonRunner.close();
        closeBrowser();
        hideNote();
        if (hooks.closePopovers != nullptr)
            hooks.closePopovers();   // (no region ever sits over a popover)
        ensureLayers();
        inspecting = true;
        bannerNote = std::make_unique<Note>();
        Note::Content banner;
        banner.title = "Info";
        banner.body = "Point to a control to read about it. The controls keep working." + dot() + "Esc to exit";
        bannerNote->setContent (std::move (banner));
        bannerNote->onAction = [this] (const juce::String&) { setLearnMode (false); };
        hooks.content->addAndMakeVisible (*bannerNote);
        lessonRunner.setInspecting (true);
    }
    else
    {
        inspecting = false;
        if (shownNote == NoteKind::help)
            hideNote();
        hitRegions.clear();
        bannerNote.reset();
        hoverTarget = {};
        lessonRunner.setInspecting (false);
    }
    progressStore.update ([on] (ProgressData& d) { d.learnModeEnabled = on; });
    progress.learnModeEnabled = on;
    if (on)
        layoutNow();
    else
        releaseLayersIfIdle();
}

void LearnController::followPointer()
{
    // Info: what is under the pointer (the smallest area there) is outlined, and after a short
    // rest its explanation shows - the controls themselves keep every click. Over the note or
    // the banner nothing changes (their keys stay reachable).
    if (hooks.content == nullptr || ! hooks.content->isShowing())
        return;
    const auto at = hooks.content->getLocalPoint (nullptr, juce::Desktop::getInstance().getMainMouseSource().getScreenPosition());
    for (const juce::Component* over : { static_cast<const juce::Component*> (notePanel.get()), static_cast<const juce::Component*> (bannerNote.get()) })
        if (over != nullptr && over->isVisible() && over->getBounds().toFloat().contains (at))
            return;
    juce::String under;
    int smallest = std::numeric_limits<int>::max();
    for (const auto& r : hitRegions)
        if (r->isVisible() && r->getBounds().toFloat().contains (at) && r->getWidth() * r->getHeight() < smallest)
        {
            smallest = r->getWidth() * r->getHeight();
            under = r->targetId();
        }
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (under != hoverTarget)
    {
        hoverTarget = under;
        hoverSince = now;
        layoutNow();
    }
    if (under.isNotEmpty() && (shownNote != NoteKind::help || helpTarget != under) && now - hoverSince >= hoverDelayMs)
        inspect (under);
}

void LearnController::inspect (const juce::String& targetId)
{
    if (! inspecting || lessonRunner.catalog().findHelp (targetId) == nullptr)
        return;
    helpTarget = targetId;
    showNote (NoteKind::help);
}

void LearnController::confirmResetProgress()
{
    if (! usable)
        return;
    if (lessonRunner.inTour())
        lessonRunner.close();
    closeBrowser();
    if (inspecting)
        setLearnMode (false);
    if (hooks.closePopovers != nullptr)
        hooks.closePopovers();
    ensureLayers();
    showNote (NoteKind::confirmReset);
}

void LearnController::resetProgress()
{
    // The lessons' records only: no tutorial starts, no sound or preset changes.
    progressStore.reset();
    progress = progressStore.read();
    if (browserPanel != nullptr)
        browserPanel->setRows (browserRows());
}

bool LearnController::restorePreviousSound()
{
    if (hooks.canRestoreSound == nullptr || ! hooks.canRestoreSound() || hooks.restoreSound == nullptr)
        return false;
    return hooks.restoreSound();
}

void LearnController::showInvitation()
{
    if (! usable || isActive() || ! ProgressStore::claimInvitation (fullCatalog.productId))
        return;   // another editor of this product shows it already
    invitationClaimed = true;
    invitationPending = false;
    ensureLayers();
    showNote (NoteKind::invitation);
}

void LearnController::markInvitationSeen()
{
    invitationPending = false;
    progress.firstInviteSeen = true;
    progressStore.update ([] (ProgressData& d) { d.firstInviteSeen = true; });
    if (invitationClaimed)
    {
        ProgressStore::releaseInvitation (fullCatalog.productId);
        invitationClaimed = false;
    }
}

//==============================================================================
void LearnController::noteAction (const juce::String& id)
{
    switch (shownNote)
    {
        case NoteKind::lesson:
            if (id == "next" || id == "finish")
                lessonRunner.next();
            else if (id == "back")
                lessonRunner.back();
            else if (id == "continue")
                lessonRunner.continueAlreadyDone();
            else if (id == "current")
                lessonRunner.useCurrentSound();
            else if (id == "example")
            {
                // The musician chose it: the sound before it is kept (Restore Previous Sound).
                exampleFailed = ! lessonRunner.loadExample();
                noteSignature = {};
                refreshNote();
            }
            else if (id == "showme")
                lessonRunner.showMe();
            else if (id == "fix")
                lessonRunner.revealRequirement();
            else if (id == "close")
            {
                // X: out at once. Nothing is restored or replaced (Restore stays in the menu).
                lessonRunner.close();
                hideNote();
            }
            break;
        case NoteKind::help:
            if (id.startsWith ("start:"))
                startLesson (id.fromFirstOccurrenceOf ("start:", false, false));
            else if (id == "open")
            {
                // The controls are for using: Info steps aside first.
                const auto target = helpTarget;
                setLearnMode (false);
                registry.reveal (target);
            }
            else if (id == "tutorials")
            {
                // The tutorial list, nothing started: Info closes first (its note and its mode).
                setLearnMode (false);
                hideNote();
                helpTarget = {};
                openBrowser();
            }
            else if (id == "close")
            {
                hideNote();
                helpTarget = {};
            }
            break;
        case NoteKind::invitation:
            markInvitationSeen();
            hideNote();
            if (id == "quickstart")
            {
                const auto quick = lessonRunner.catalog().quickStartLessonId;
                startLesson (quick.isNotEmpty() ? quick : lessonRunner.catalog().lessons.front().id);
            }
            else if (id == "browse")
                openBrowser();
            break;
        case NoteKind::complete:
            if (id == "restore")
                restorePreviousSound();
            hideNote();
            // (the sound chosen, the tutorials follow; X alone leaves them closed)
            if (id == "browse" || id == "restore" || id == "keep")
                browserAfterLesson = true;
            break;
        case NoteKind::confirmReset:
            hideNote();
            if (id == "reset")
                resetProgress();
            break;
        case NoteKind::none:
            break;
    }
    if (notePanel != nullptr)
        layoutNow();
    openBrowserAfterLesson();
    releaseLayersIfIdle();
}

void LearnController::showNote (NoteKind kind)
{
    if (notePanel == nullptr)
    {
        notePanel = std::make_unique<Note>();
        notePanel->onAction = [this] (const juce::String& id) { noteAction (id); };
        hooks.content->addAndMakeVisible (*notePanel);
    }
    shownNote = kind;
    noteSignature = {};
    refreshNote();
    layoutNow();
}

void LearnController::hideNote()
{
    notePanel.reset();
    shownNote = NoteKind::none;
    noteSignature = {};
    lastScrolledTarget = {};
    if (ring != nullptr)
    {
        ring->setTarget (std::nullopt, false);
        ring->setConnector (std::nullopt);
    }
}

void LearnController::refreshNote()
{
    if (notePanel == nullptr)
        return;
    Note::Content c;
    auto& keys = c.actions;
    if (shownNote == NoteKind::lesson)
    {
        const auto state = lessonRunner.state();
        if (state == LessonRunner::State::tourComplete)
        {
            if (! offersRestoreChoice())
            {
                // Nothing to choose: straight back to the tutorials.
                browserAfterLesson = true;
                hideNote();
                return;
            }
            shownNote = NoteKind::complete;
        }
        else if (! lessonRunner.inTour())
        {
            hideNote();
            return;
        }
    }
    auto nextKey = [this] {
        return lessonRunner.isLastStep() ? Note::Action { "finish", "Finish", true } : Note::Action { "next", "Next", true };
    };
    switch (shownNote)
    {
        case NoteKind::lesson:
        {
            const auto* lesson = lessonRunner.lesson();
            const auto* step = lessonRunner.step();
            const int i = lessonRunner.stepIndex(), n = static_cast<int> (lesson->steps.size());
            c.meta = (lesson->group.isNotEmpty() ? lesson->group : lesson->title).toUpperCase();
            c.step = "STEP " + juce::String (i + 1) + " OF " + juce::String (n);
            const auto state = lessonRunner.state();
            const bool hasExample = lesson->exampleId.isNotEmpty();
            if (i > 0)
                keys.push_back ({ "back", "Back", false });
            if (step->preparation)
            {
                // Set up the lesson: the current sound, or the lesson's example.
                c.title = step->title;
                c.body = step->body;
                if (lessonRunner.preparationReady())
                {
                    c.status = "Your current sound is ready for this lesson.";
                    keys.push_back ({ "example", "Load Example Sound", false });
                    keys.push_back ({ "current", "Use Current Sound", true });
                }
                else
                {
                    if (const auto* missing = lessonRunner.unmetRequirement())
                        c.status = missing->body;
                    keys.push_back ({ "current", "Use Current Sound", false });
                    keys.push_back ({ "example", "Load Example Sound", true });
                }
            }
            else if (state == LessonRunner::State::blocked)
            {
                // Something the step needs is missing: say what, point at the fix, offer it.
                const auto* missing = lessonRunner.unmetRequirement();
                c.title = missing != nullptr ? missing->title : step->title;
                c.body = missing != nullptr ? missing->body : step->body;
                const bool example = missing != nullptr && missing->offerExample && hasExample;
                if (missing != nullptr && missing->fixLabel.isNotEmpty())
                    keys.push_back ({ "fix", missing->fixLabel, ! example });
                if (example)
                    keys.push_back ({ "example", "Load Example Sound", true });
            }
            else
            {
                c.title = step->title;
                c.body = lessonRunner.currentBody();
                const auto* target = registry.find (step->targetId);
                const bool canShow = target != nullptr && target->reveal != nullptr;
                const auto revealLabel = step->revealLabel.isNotEmpty() ? step->revealLabel : juce::String ("Open");
                if (state == LessonRunner::State::tourShowing)
                    keys.push_back (nextKey());
                else if (state == LessonRunner::State::stepComplete)
                {
                    c.done = true;
                    c.status = step->doneText.isNotEmpty() ? step->doneText : juce::String ("Done.");
                    keys.push_back (nextKey());
                }
                else if (lessonRunner.canContinue())
                {
                    // Already so: a status line, and Continue (never a sentence in a key).
                    c.status = step->completion.alreadyLabel.isNotEmpty() ? step->completion.alreadyLabel : juce::String ("This is already set.");
                    keys.push_back ({ "continue", "Continue", true });
                }
                else if (state == LessonRunner::State::paused)
                {
                    c.title = "Open the controls";
                    c.body = step->unavailableBody.isNotEmpty() ? step->unavailableBody
                                                                : juce::String ("The control for this step is inside the closed rack. Open the rack to continue.");
                    if (canShow)
                        keys.push_back ({ "showme", revealLabel, true });
                }
                else if (canShow && ! step->revealOnEnter && ! registry.ready (step->targetId, *hooks.content))
                    keys.push_back ({ "showme", revealLabel, false });
            }
            if (exampleFailed)
                c.status = juce::String::fromUTF8 ("The example sound couldn\xe2\x80\x99t be loaded. Your sound hasn\xe2\x80\x99t changed.");
            break;
        }
        case NoteKind::help:
        {
            const auto* help = lessonRunner.catalog().findHelp (helpTarget);
            if (help == nullptr)
            {
                hideNote();
                return;
            }
            c.meta = "INFO";
            c.title = help->title;
            c.body = help->body;
            const auto* target = registry.find (helpTarget);
            if (target != nullptr && target->reveal != nullptr)
                keys.push_back ({ "open", "Open Controls", false });
            // The tutorials from any topic: a neutral key beside Close, which keeps its styling.
            if (! lessonRunner.catalog().lessons.empty())
                keys.push_back ({ "tutorials", "See tutorials", false });
            keys.push_back ({ "close", "Close", help->lessonId.isEmpty() });
            if (help->lessonId.isNotEmpty())
                keys.push_back ({ "start:" + help->lessonId, "Start Lesson", true });
            break;
        }
        case NoteKind::invitation:
            c.title = "Learn ANDOR";
            c.body = "Explore the main controls or follow a short lesson using the instrument itself.";
            if (! lessonRunner.catalog().lessons.empty())
            {
                keys.push_back ({ "notnow", "Not Now", false });
                keys.push_back ({ "browse", "Tutorials", false });
                const auto quick = lessonRunner.catalog().quickStartLessonId;
                const auto* first = lessonRunner.catalog().findLesson (quick.isNotEmpty() ? quick : lessonRunner.catalog().lessons.front().id);
                keys.push_back ({ "quickstart", quick.isNotEmpty() ? juce::String ("Quick Start") : first->title, true });
            }
            else
                keys.push_back ({ "notnow", "Not Now", true });
            break;
        case NoteKind::complete:
        {
            const auto* lesson = lessonRunner.lesson();
            c.meta = lesson != nullptr ? (lesson->group.isNotEmpty() ? lesson->group : lesson->title).toUpperCase() : juce::String();
            c.title = "Lesson complete";
            c.body = lesson != nullptr && lesson->completionText.isNotEmpty()
                         ? lesson->completionText
                         : juce::String::fromUTF8 ("You\xe2\x80\x99ve finished this lesson. Choose another tutorial or return to the instrument.");
            if (offersRestoreChoice())
            {
                // The lesson played on its example: keep it, or bring back the sound from before.
                keys.push_back ({ "restore", "Restore Previous Sound", false });
                keys.push_back ({ "keep", "Keep Example Sound", true });
            }
            else
            {
                keys.push_back ({ "browse", "Tutorials", false });
                keys.push_back ({ "close", "Close", true });
            }
            break;
        }
        case NoteKind::confirmReset:
            c.title = "Reset tutorial progress?";
            c.body = juce::String::fromUTF8 ("This clears your completed lessons and lets you start them again. Your sounds and presets won\xe2\x80\x99t be changed.");
            keys.push_back ({ "cancel", "Cancel", false });
            keys.push_back ({ "reset", "Reset Tutorials", true });
            break;
        case NoteKind::none:
            return;
    }
    juce::String signature = c.meta + "|" + c.step + "|" + c.title + "|" + c.body + "|" + c.status + "|" + (c.done ? "1" : "0");
    for (const auto& k : keys)
        signature << "|" << k.id << ":" << k.label << (k.primary ? "*" : "");
    if (signature == noteSignature)
        return;
    noteSignature = signature;
    notePanel->setContent (std::move (c));
}

//==============================================================================
void LearnController::ensureLayers()
{
    if (ring == nullptr)
    {
        ring = std::make_unique<HighlightLayer>();
        hooks.content->addAndMakeVisible (*ring);
        ring->setBounds (contentBounds());
    }
}

void LearnController::releaseLayersIfIdle()
{
    // Closed means gone: no component of Learn stays in the editor.
    if (isActive())
        return;
    hitRegions.clear();
    bannerNote.reset();
    ring.reset();
}

std::vector<HitRegion*> LearnController::regions() const
{
    std::vector<HitRegion*> out;
    for (const auto& r : hitRegions)
        out.push_back (r.get());
    return out;
}

TouchWatcher* LearnController::touchWatcher (const juce::String& targetId) const
{
    for (const auto& w : watchers)
        if (w->targetId() == targetId)
            return w.get();
    return nullptr;
}

juce::Rectangle<int> LearnController::contentBounds() const
{
    return hooks.content->getLocalBounds();
}

void LearnController::updateRegions()
{
    bool orderChanged = false;
    for (const auto& t : registry.all())
    {
        if (! t.inspectable || lessonRunner.catalog().findHelp (t.id) == nullptr)
            continue;
        auto it = std::find_if (hitRegions.begin(), hitRegions.end(), [&t] (const auto& r) { return r->targetId() == t.id; });
        if (it == hitRegions.end())
        {
            auto region = std::make_unique<HitRegion> (t.id);
            region->setInterceptsMouseClicks (false, false);   // (the control under it keeps its clicks)
            region->onHover = [this] (const juce::String& id) {
                hoverTarget = id;
                layoutNow();
            };
            region->onChoose = [this] (const juce::String& id) { inspect (id); };
            hooks.content->addChildComponent (*region);
            hitRegions.push_back (std::move (region));
            it = std::prev (hitRegions.end());
            orderChanged = true;
        }
        const auto bounds = registry.boundsIn (t.id, *hooks.content);
        if ((*it)->isVisible() != bounds.has_value())
            orderChanged = true;
        (*it)->setVisible (bounds.has_value());
        if (bounds.has_value())
            (*it)->setBounds (*bounds);
    }
    if (orderChanged)
    {
        // Larger areas below smaller ones, so a small region inside a large one stays reachable.
        std::vector<HitRegion*> order;
        for (auto& r : hitRegions)
            order.push_back (r.get());
        std::stable_sort (order.begin(), order.end(), [] (const HitRegion* a, const HitRegion* b) {
            return a->getWidth() * a->getHeight() > b->getWidth() * b->getHeight();
        });
        for (auto* r : order)
            r->toFront (false);
    }
}

void LearnController::raise()
{
    // Learn's layers above everything the editor shows (its popovers included), in this order;
    // reordered only when something came in front of them.
    std::vector<juce::Component*> wanted;
    for (auto& r : hitRegions)
        if (r->isVisible())
            wanted.push_back (r.get());
    if (ring != nullptr)
        wanted.push_back (ring.get());
    if (bannerNote != nullptr)
        wanted.push_back (bannerNote.get());
    if (browserPanel != nullptr)
        wanted.push_back (browserPanel.get());
    if (notePanel != nullptr)
        wanted.push_back (notePanel.get());
    auto& c = *hooks.content;
    const int n = c.getNumChildComponents(), m = static_cast<int> (wanted.size());
    bool inOrder = n >= m;
    if (inOrder && ring != nullptr)
    {
        // Only the non-region layers must be on top; regions keep their size order below them.
        int k = n - 1;
        for (int i = m - 1; i >= 0 && inOrder; --i)
        {
            if (dynamic_cast<HitRegion*> (wanted[static_cast<std::size_t> (i)]) != nullptr)
                break;
            inOrder = c.getChildComponent (k--) == wanted[static_cast<std::size_t> (i)];
        }
    }
    if (inOrder)
        return;
    for (auto* w : wanted)
        w->toFront (false);
}

juce::Rectangle<int> LearnController::visibleBounds() const
{
    auto area = contentBounds();
    if (hooks.visibleArea != nullptr)
    {
        const auto shown = hooks.visibleArea().getIntersection (area);
        if (! shown.isEmpty())
            area = shown;
    }
    return area;
}

juce::String LearnController::currentTargetId() const
{
    // The exact control the musician needs now: what fixes a missing requirement, else the step's.
    if (shownNote != NoteKind::lesson || ! lessonRunner.inTour())
        return {};
    if (lessonRunner.state() == LessonRunner::State::blocked)
        if (const auto* missing = lessonRunner.unmetRequirement())
            return missing->targetId;
    if (lessonRunner.state() == LessonRunner::State::paused)
        return {};
    if (const auto* s = lessonRunner.step())
        return s->targetId;
    return {};
}

std::vector<juce::Rectangle<int>> LearnController::keepClearAreas() const
{
    std::vector<juce::Rectangle<int>> out;
    if (shownNote != NoteKind::lesson || ! lessonRunner.inTour())
        return out;
    const auto* s = lessonRunner.step();
    if (s == nullptr)
        return out;
    for (const auto& id : s->keepClear)
        if (const auto r = registry.boundsIn (id, *hooks.content))
            out.push_back (*r);
    for (const auto& r : s->requirements)
        if (r.targetId.isNotEmpty())
            if (const auto b = registry.boundsIn (r.targetId, *hooks.content))
                out.push_back (*b);
    return out;
}

void LearnController::layoutNow()
{
    if (hooks.content == nullptr || ! isActive())
        return;
    if (inspecting)
        updateRegions();
    raise();
    const auto area = contentBounds();
    if (ring != nullptr && ring->getBounds() != area)
        ring->setBounds (area);
    if (bannerNote != nullptr)
    {
        // A quiet line at the foot of what is in view, out of the way of the header.
        const auto shown = visibleBounds();
        const auto size = bannerNote->preferredSize();
        bannerNote->setBounds (shown.getCentreX() - size.x / 2, shown.getBottom() - size.y - 4, size.x, size.y);
    }
    std::optional<juce::Rectangle<int>> target;
    bool quiet = false;
    if (const auto id = currentTargetId(); id.isNotEmpty())
    {
        target = registry.boundsIn (id, *hooks.content);
        // A control below the visible part of a scrolled editor is brought into view once.
        if (target.has_value() && id != lastScrolledTarget && hooks.scrollIntoView != nullptr && ! visibleBounds().contains (*target))
        {
            lastScrolledTarget = id;
            hooks.scrollIntoView (target->expanded (40));
            target = registry.boundsIn (id, *hooks.content);
        }
    }
    else if (shownNote == NoteKind::help)
        target = registry.boundsIn (helpTarget, *hooks.content);
    else if (inspecting && hoverTarget.isNotEmpty() && shownNote == NoteKind::none)
    {
        target = registry.boundsIn (hoverTarget, *hooks.content);
        quiet = true;
    }
    // (A step about a menu item places its note by the control without outlining it.)
    const bool outline = ! (shownNote == NoteKind::lesson && lessonRunner.inTour() && lessonRunner.state() != LessonRunner::State::blocked
                            && lessonRunner.step() != nullptr && ! lessonRunner.step()->outlineTarget);
    if (ring != nullptr)
        ring->setTarget (outline ? target : std::nullopt, quiet);
    if (notePanel != nullptr)
    {
        if (shownNote == NoteKind::invitation || shownNote == NoteKind::complete || shownNote == NoteKind::confirmReset)
            placeBelowMenu (*notePanel);
        else
            placeNote (target);
    }
    if (ring != nullptr && (notePanel == nullptr || shownNote != NoteKind::lesson))
        ring->setConnector (std::nullopt);
    if (browserPanel != nullptr)
    {
        // As tall as the list, no taller than the room under the menu (it scrolls inside).
        const auto shown = visibleBounds();
        const auto anchor = hooks.menuAnchor != nullptr ? hooks.menuAnchor() : juce::Rectangle<int> (shown.getRight() - 40, shown.getY() + 20, 30, 30);
        browserPanel->setMaximumHeight (std::max (200, shown.getBottom() - anchor.getBottom() - 4));
        placeBelowMenu (*browserPanel);
    }
}

void LearnController::placeBelowMenu (juce::Component& c)
{
    const auto area = visibleBounds().reduced (4);
    const auto anchor = hooks.menuAnchor != nullptr ? hooks.menuAnchor() : juce::Rectangle<int> (area.getRight() - 40, area.getY() + 20, 30, 30);
    const int m = Note::margin;
    auto r = juce::Rectangle<int> (anchor.getRight() - c.getWidth() + m, anchor.getBottom() + 6 - m, c.getWidth(), c.getHeight());
    c.setBounds (r.constrainedWithin (area));
}

void LearnController::placeNote (std::optional<juce::Rectangle<int>> anchor)
{
    auto& note = *notePanel;
    const auto area = visibleBounds().reduced (4);
    const int w = note.getWidth(), h = note.getHeight(), m = Note::margin;
    if (! anchor.has_value())
    {
        // No target (or it is not showing): under the header, centred.
        note.setBounds (juce::Rectangle<int> (area.getCentreX() - w / 2, area.getY() + 96 - m, w, h).constrainedWithin (area));
        if (ring != nullptr)
            ring->setConnector (std::nullopt);
        return;
    }
    // Scored placement: beside the target on each side (right, left, above, below - in that
    // order of preference), each slid along its side to stay inside the editor. A card that
    // would cover the target (with its outline) is out; covering the other controls the step
    // uses costs a lot; the distance to the target costs a little; then the preference order.
    const auto t = *anchor;
    const int pad = juce::roundToInt (HighlightLayer::padding) + 4;
    const auto forbidden = t.expanded (pad);
    const auto keep = keepClearAreas();
    // A small control inside a larger panel (a key of the ARP panel, a segment of a switch): the
    // rest of that panel holds the controls the next steps need - the note prefers to sit
    // outside it. (A full-component target has no container: nothing extra to avoid.)
    std::optional<juce::Rectangle<int>> container;
    if (const auto id = currentTargetId(); id.isNotEmpty())
        container = registry.containerIn (id, *hooks.content);

    struct Candidate
    {
        juce::Rectangle<int> bounds;
        float bias;
    };
    std::vector<Candidate> candidates;
    const int gap = noteGap + pad;
    auto along = [&] (juce::Rectangle<int> r, bool horizontal) {
        // Centred on the target, and aligned with either of its edges.
        if (horizontal)
            return std::array<juce::Rectangle<int>, 3> { r, r.withY (t.getY() - m), r.withY (t.getBottom() - h + m) };
        return std::array<juce::Rectangle<int>, 3> { r, r.withX (t.getX() - m), r.withX (t.getRight() - w + m) };
    };
    for (const auto& r : along ({ t.getRight() + gap - m, t.getCentreY() - h / 2, w, h }, true))
        candidates.push_back ({ r, 0.0f });
    for (const auto& r : along ({ t.getX() - gap - w + m, t.getCentreY() - h / 2, w, h }, true))
        candidates.push_back ({ r, 8.0f });
    for (const auto& r : along ({ t.getCentreX() - w / 2, t.getY() - gap - h + m, w, h }, false))
        candidates.push_back ({ r, 16.0f });
    for (const auto& r : along ({ t.getCentreX() - w / 2, t.getBottom() + gap - m, w, h }, false))
        candidates.push_back ({ r, 24.0f });
    auto cardOf = [m] (juce::Rectangle<int> r) { return r.reduced (m); };
    auto distance = [] (juce::Rectangle<int> a, juce::Rectangle<int> b) {
        const int dx = std::max ({ 0, b.getX() - a.getRight(), a.getX() - b.getRight() });
        const int dy = std::max ({ 0, b.getY() - a.getBottom(), a.getY() - b.getBottom() });
        return std::sqrt (static_cast<float> (dx * dx + dy * dy));
    };
    std::optional<juce::Rectangle<int>> best;
    float bestScore = 1.0e9f;
    for (const auto& cand : candidates)
    {
        const auto inside = cand.bounds.constrainedWithin (area);
        if (! area.contains (inside))
            continue;   // (larger than the editor's room)
        const auto card = cardOf (inside);
        if (card.intersects (forbidden))
            continue;   // never over the control the musician needs
        float score = cand.bias + distance (card, t) * 0.6f;
        for (const auto& k : keep)
            if (const auto overlap = card.getIntersection (k); ! overlap.isEmpty())
                score += 400.0f + 0.05f * static_cast<float> (overlap.getWidth() * overlap.getHeight());
        if (container.has_value())
            if (const auto overlap = card.getIntersection (*container); ! overlap.isEmpty())
                score += 0.004f * static_cast<float> (overlap.getWidth() * overlap.getHeight());
        if (score < bestScore)
        {
            bestScore = score;
            best = inside;
        }
    }
    if (! best.has_value())
    {
        // Nowhere beside it (a large target): where it covers least of it, joined by a connector.
        float leastOverlap = 1.0e12f;
        for (const auto& cand : candidates)
        {
            const auto inside = cand.bounds.constrainedWithin (area);
            const auto o = cardOf (inside).getIntersection (forbidden);
            const float covered = static_cast<float> (o.getWidth()) * static_cast<float> (o.getHeight());
            if (covered < leastOverlap)
            {
                leastOverlap = covered;
                best = inside;
            }
        }
    }
    note.setBounds (*best);
    // A connector when the card sits apart from its target (more than a short gap away).
    if (ring != nullptr)
    {
        const auto card = cardOf (*best).toFloat();
        const auto outline = t.toFloat().expanded (HighlightLayer::padding);
        if (distance (card.toNearestInt(), outline.toNearestInt()) > static_cast<float> (gap + 14))
        {
            auto nearestOn = [] (juce::Rectangle<float> r, juce::Point<float> p) {
                return juce::Point<float> (juce::jlimit (r.getX(), r.getRight(), p.x), juce::jlimit (r.getY(), r.getBottom(), p.y));
            };
            const auto from = nearestOn (card, outline.getCentre());
            const auto to = nearestOn (outline, from);
            ring->setConnector (juce::Line<float> (from, to));
        }
        else
            ring->setConnector (std::nullopt);
    }
}

//==============================================================================
void LearnController::persist (const Lesson& lesson, int stepIndex, bool finished, bool allStepsDone)
{
    // There is no Skip: a finished lesson had every task done or already so (Continue).
    juce::String resumeAt;
    if (! finished && stepIndex >= 0 && stepIndex < static_cast<int> (lesson.steps.size()))
        resumeAt = lesson.steps[static_cast<std::size_t> (stepIndex)].id;
    const auto id = lesson.id;
    const int revision = lesson.revision;
    progressStore.update ([&] (ProgressData& d) {
        auto& p = d.lessons[id];
        if (finished && allStepsDone)
        {
            p.revision = revision;
            p.status = "complete";
            p.completedAt = juce::Time::getCurrentTime().toISO8601 (true);
            p.lastStep = {};
            return;
        }
        if (p.status == "complete")
            return;   // a lesson once completed stays completed (replaying it never undoes that)
        p.revision = revision;
        p.status = "in-progress";
        p.completedAt = {};
        p.lastStep = resumeAt;
    });
    progress = progressStore.read();
}

int LearnController::resumeStep (const Lesson& lesson) const
{
    // Resumed at the same step (by its stable ID, whichever revision left it there); a lesson
    // revised since, without that step, starts from the beginning.
    const auto it = progress.lessons.find (lesson.id);
    if (it == progress.lessons.end() || it->second.status != "in-progress")
        return 0;
    for (int s = 0; s < static_cast<int> (lesson.steps.size()); ++s)
        if (lesson.steps[static_cast<std::size_t> (s)].id == it->second.lastStep)
            return s;
    return 0;
}

std::vector<Browser::Row> LearnController::browserRows() const
{
    std::vector<Browser::Row> rows;
    const auto quick = lessonRunner.catalog().quickStartLessonId;
    for (const auto& lesson : lessonRunner.catalog().lessons)
    {
        Browser::Row row;
        row.lessonId = lesson.id;
        row.title = lesson.title;
        row.description = lesson.description;
        row.minutes = lesson.minutes;
        row.stepCount = static_cast<int> (lesson.steps.size());
        row.featured = lesson.id == quick;
        if (const auto it = progress.lessons.find (lesson.id); it != progress.lessons.end())
        {
            if (it->second.status == "complete")
                row.status = Browser::Row::Status::complete;
            else if (const int at = resumeStep (lesson); at > 0)
            {
                row.status = Browser::Row::Status::inProgress;
                row.resumeStep = at;
            }
        }
        rows.push_back (row);
    }
    return rows;
}

//==============================================================================
std::optional<float> LearnController::parameterValue (const juce::String& id)
{
    if (const auto v = registry.value (id))
        return v;   // (a product's own value, not a parameter)
    return hooks.parameterValue != nullptr ? hooks.parameterValue (id) : std::nullopt;
}

bool LearnController::state (const juce::String& id)
{
    return registry.state (id);
}

bool LearnController::targetReady (const juce::String& id)
{
    return registry.ready (id, *hooks.content);
}

bool LearnController::reveal (const juce::String& id)
{
    return registry.reveal (id);
}

bool LearnController::loadExample (const juce::String& exampleId)
{
    return hooks.loadExample != nullptr && hooks.loadExample (exampleId);
}

} // namespace andor::learn
