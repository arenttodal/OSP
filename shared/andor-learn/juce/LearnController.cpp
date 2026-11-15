#include "LearnController.h"

#include <algorithm>
#include <cmath>

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
    checks.parameterKnown = [this] (const juce::String& id) { return hooks.parameterValue != nullptr && hooks.parameterValue (id).has_value(); };
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
    invitationPending = ! progress.firstInviteSeen;
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
    // The same section in every ANDOR product: LEARN, Tutorials..., Toggle Info, Reset Tutorials...
    juce::WeakReference<LearnController> weak (this);
    menu.addSectionHeader ("LEARN");
    menu.addItem ("Tutorials" + ellipsis(), [weak] {
        if (weak != nullptr)
            weak->openBrowser();
    });
    menu.addItem ("Toggle Info", true, inspecting, [weak] {
        if (weak != nullptr)
            weak->setLearnMode (! weak->inspecting);
    });
    if (hooks.canRestore != nullptr && hooks.canRestore())
        menu.addItem ("Restore Sound Before Tutorial", [weak] {
            if (weak != nullptr)
                weak->restorePreviousSound();
        });
    menu.addSeparator();
    menu.addItem ("Reset Tutorials" + ellipsis(), [weak] {
        if (weak != nullptr)
            weak->askToResetProgress();
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
    if (shownNote == NoteKind::preparing)
        updatePreparing();
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
bool LearnController::startLesson (const juce::String& lessonId)
{
    if (! usable || lessonRunner.catalog().findLesson (lessonId) == nullptr)
        return false;
    closeBrowser();
    if (inspecting)
        setLearnMode (false);
    if (hooks.closePopovers != nullptr)
        hooks.closePopovers();
    ensureLayers();
    return beginLesson (lessonId, 0);
}

bool LearnController::startLessonAt (const juce::String& lessonId, int stepIndex)
{
    if (! usable || lessonRunner.catalog().findLesson (lessonId) == nullptr)
        return false;
    closeBrowser();
    if (inspecting)
        setLearnMode (false);
    if (hooks.closePopovers != nullptr)
        hooks.closePopovers();
    ensureLayers();
    return beginLesson (lessonId, stepIndex);
}

bool LearnController::beginLesson (const juce::String& lessonId, int fromStep)
{
    const auto* lesson = lessonRunner.catalog().findLesson (lessonId);
    if (lesson == nullptr)
        return false;
    const auto& prep = lesson->preparation;
    const bool ready = ! prep.ready.has_value() || conditionHolds (*prep.ready);
    const bool canLoad = prep.exampleId.isNotEmpty() && hooks.loadExample != nullptr;
    if (! ready || (canLoad && prep.offerExampleWhen.has_value() && conditionHolds (*prep.offerExampleWhen)))
    {
        // Ask first: the musician's sound is never replaced without their choice.
        preparingLesson = lessonId;
        preparingStep = fromStep;
        prepareFailed = false;
        showNote (NoteKind::prepare);
        return true;
    }
    if (! lessonRunner.start (lessonId, fromStep))
        return false;
    showNote (NoteKind::lesson);
    return true;
}

bool LearnController::conditionHolds (const Condition& c)
{
    switch (c.kind)
    {
        case Condition::Kind::always: return true;
        case Condition::Kind::parameterChanged: return false;   // (no baseline before a lesson)
        case Condition::Kind::parameterEquals:
        {
            const auto v = parameterValue (c.id);
            return v.has_value() && std::abs (*v - c.value) < 0.5f;
        }
        case Condition::Kind::state: return state (c.id);
        case Condition::Kind::all:
            for (const auto& t : c.terms)
                if (! conditionHolds (t))
                    return false;
            return ! c.terms.empty();
        case Condition::Kind::any:
            for (const auto& t : c.terms)
                if (conditionHolds (t))
                    return true;
            return false;
    }
    return false;
}

void LearnController::loadExampleFor (const juce::String& lessonId)
{
    const auto* lesson = lessonRunner.catalog().findLesson (lessonId);
    if (lesson == nullptr || hooks.loadExample == nullptr)
        return;
    prepareFailed = ! hooks.loadExample (lesson->preparation.exampleId);
    exampleLoaded = exampleLoaded || ! prepareFailed;
    preparingTicks = 0;
    showNote (prepareFailed ? NoteKind::prepare : NoteKind::preparing);
}

void LearnController::updatePreparing()
{
    // The lesson begins once the example is really playable (loaded and analysed), never on
    // the request alone.
    const auto* lesson = lessonRunner.catalog().findLesson (preparingLesson);
    if (lesson == nullptr)
    {
        hideNote();
        return;
    }
    ++preparingTicks;
    if (hooks.exampleFailed != nullptr && hooks.exampleFailed())
    {
        prepareFailed = true;
        showNote (NoteKind::prepare);
        return;
    }
    const auto& prep = lesson->preparation;
    if ((! prep.ready.has_value() || conditionHolds (*prep.ready)) && (hooks.exampleReady == nullptr || hooks.exampleReady()))
    {
        const auto id = preparingLesson;
        preparingLesson = {};
        if (lessonRunner.start (id, preparingStep))
            showNote (NoteKind::lesson);
        else
            hideNote();
    }
}

bool LearnController::restorePreviousSound()
{
    exampleLoaded = false;
    if (hooks.canRestore == nullptr || ! hooks.canRestore() || hooks.restore == nullptr)
        return false;
    return hooks.restore();
}

void LearnController::offerRestoreIfExample()
{
    // Closing a tutorial never restores by itself (the musician may want what they made); it
    // asks, once, while the sound from before the example can really be restored.
    if (! exampleLoaded || hooks.canRestore == nullptr || ! hooks.canRestore())
        return;
    ensureLayers();
    showNote (NoteKind::restoreOffer);
}

juce::String LearnController::productName() const
{
    return hooks.productName.isNotEmpty() ? hooks.productName : juce::String ("this instrument");
}

void LearnController::askToResetProgress()
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
    browserPanel->setRows (browserRows());
    lessonRunner.setBrowsing (true);
    layoutNow();
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
    beginLesson (lessonId, from);
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
        bannerNote->setContent ({}, "Info", "Click a control to learn what it does" + dot() + "Esc to exit", {}, false);
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

void LearnController::inspect (const juce::String& targetId)
{
    if (! inspecting || lessonRunner.catalog().findHelp (targetId) == nullptr)
        return;
    helpTarget = targetId;
    showNote (NoteKind::help);
}

void LearnController::resetProgress()
{
    progressStore.reset();
    progress = progressStore.read();
    if (browserPanel != nullptr)
        browserPanel->setRows (browserRows());
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
            else if (id == "showme")
                lessonRunner.showMe();
            else if (id == "close")
            {
                lessonRunner.close();
                hideNote();
                offerRestoreIfExample();
            }
            break;
        case NoteKind::prepare:
            if (id == "loadexample")
                loadExampleFor (preparingLesson);
            else if (id == "usemine")
            {
                const auto lessonId = preparingLesson;
                preparingLesson = {};
                if (lessonRunner.start (lessonId, preparingStep))
                    showNote (NoteKind::lesson);
                else
                    hideNote();
            }
            else
            {
                preparingLesson = {};
                hideNote();
            }
            break;
        case NoteKind::preparing:
            preparingLesson = {};
            hideNote();   // (the example goes on loading; the previous sound stays restorable)
            offerRestoreIfExample();
            break;
        case NoteKind::restoreOffer:
            hideNote();
            if (id == "restore")
                restorePreviousSound();
            else
                exampleLoaded = false;   // (kept: not asked again; the menu still offers it)
            break;
        case NoteKind::confirmReset:
            hideNote();
            if (id == "reset")
                resetProgress();
            break;
        case NoteKind::help:
            if (id.startsWith ("start:"))
                startLesson (id.fromFirstOccurrenceOf ("start:", false, false));
            else if (id == "open")
            {
                // The controls are for using: Learn mode steps aside first.
                const auto target = helpTarget;
                setLearnMode (false);
                registry.reveal (target);
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
            hideNote();
            if (id == "browse")
                openBrowser();
            else if (id == "restore")
                restorePreviousSound();
            break;
        case NoteKind::none:
            break;
    }
    if (notePanel != nullptr)
        layoutNow();
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
    if (ring != nullptr)
        ring->setTarget (std::nullopt, false);
}

void LearnController::refreshNote()
{
    if (notePanel == nullptr)
        return;
    juce::String meta, title, body;
    std::vector<Note::Action> keys;
    bool done = false;
    if (shownNote == NoteKind::lesson)
    {
        const auto state = lessonRunner.state();
        if (state == LessonRunner::State::tourComplete)
            shownNote = NoteKind::complete;
        else if (! lessonRunner.inTour())
        {
            hideNote();
            return;
        }
    }
    switch (shownNote)
    {
        case NoteKind::lesson:
        {
            const auto* lesson = lessonRunner.lesson();
            const auto* step = lessonRunner.step();
            const int i = lessonRunner.stepIndex(), n = static_cast<int> (lesson->steps.size());
            meta = lesson->title.toUpperCase() + "   " + juce::String (i + 1) + " / " + juce::String (n);
            title = step->title;
            body = lessonRunner.currentBody();
            const auto state = lessonRunner.state();
            const auto* target = registry.find (step->targetId);
            const bool canShow = target != nullptr && target->reveal != nullptr;
            // No Skip: X closes, Back revisits, Continue follows a read explanation or a task
            // done (or already so). A task is never passed over.
            if (i > 0)
                keys.push_back ({ "back", "Back", false });
            if (state == LessonRunner::State::tourShowing)
                keys.push_back ({ lessonRunner.isLastStep() ? "finish" : "next", lessonRunner.isLastStep() ? "Finish" : "Continue", true });
            else if (state == LessonRunner::State::stepComplete)
            {
                done = true;
                keys.push_back ({ lessonRunner.isLastStep() ? "finish" : "next", lessonRunner.isLastStep() ? "Finish" : "Continue", true });
            }
            else
            {
                if (canShow && lessonRunner.lessonReady() && (! step->revealOnEnter || state == LessonRunner::State::paused))
                    keys.push_back ({ "showme", "Show me", false });
                if (lessonRunner.canContinue())
                    keys.push_back ({ "continue", (step->completion.alreadyLabel.isNotEmpty() ? step->completion.alreadyLabel : juce::String ("Already done")) + dot() + "Continue", true });
            }
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
            meta = "INFO";
            title = help->title;
            body = help->body;
            const auto* target = registry.find (helpTarget);
            if (target != nullptr && target->reveal != nullptr)
                keys.push_back ({ "open", "Open controls", false });
            keys.push_back ({ "close", "Close", help->lessonId.isEmpty() });
            if (help->lessonId.isNotEmpty())
                keys.push_back ({ "start:" + help->lessonId, "Start lesson", true });
            break;
        }
        case NoteKind::invitation:
            title = "Find your sound.";
            body = "Learn the instrument as you play.";
            if (! lessonRunner.catalog().lessons.empty())
            {
                const auto quick = lessonRunner.catalog().quickStartLessonId;
                const auto* first = lessonRunner.catalog().findLesson (quick.isNotEmpty() ? quick : lessonRunner.catalog().lessons.front().id);
                keys.push_back ({ "notnow", "Not now", false });
                keys.push_back ({ "browse", "Tutorials", false });
                keys.push_back ({ "quickstart", quick.isNotEmpty() ? juce::String ("Quick Start") : first->title, true });
            }
            else
                keys.push_back ({ "notnow", "Not now", true });
            break;
        case NoteKind::complete:
        {
            const auto* lesson = lessonRunner.lesson();
            meta = lesson != nullptr ? lesson->title.toUpperCase() : juce::String();
            title = "Lesson complete";
            body = lesson != nullptr && lesson->completion.isNotEmpty()
                       ? lesson->completion
                       : juce::String::fromUTF8 ("Your sound stays as you left it. More lessons are under Tutorials\xe2\x80\xa6 in the menu.");
            keys.push_back ({ "browse", "Tutorials", false });
            if (hooks.canRestore != nullptr && hooks.canRestore())
                keys.push_back ({ "restore", "Restore previous sound", false });
            keys.push_back ({ "close", "Close", true });
            break;
        }
        case NoteKind::prepare:
        {
            const auto* lesson = lessonRunner.catalog().findLesson (preparingLesson);
            if (lesson == nullptr)
            {
                hideNote();
                return;
            }
            const auto& prep = lesson->preparation;
            const bool ready = ! prep.ready.has_value() || conditionHolds (*prep.ready);
            const bool canLoad = prep.exampleId.isNotEmpty() && hooks.loadExample != nullptr;
            const auto example = prep.exampleName.isNotEmpty() ? prep.exampleName : juce::String ("the example");
            meta = lesson->title.toUpperCase();
            if (prepareFailed)
            {
                title = "The example could not be loaded";
                body = "Your sound is unchanged. You can load a recording of your own and start the lesson again.";
                keys.push_back ({ "close", "Close", true });
                break;
            }
            title = ready ? "Use your sound or an example?" : "Before this lesson";
            body = ready ? "The lesson works with the sound you have. " + example + " is prepared for it."
                         : (prep.needs.isNotEmpty() ? prep.needs : juce::String ("This lesson needs a different sound."));
            if (canLoad)
                body << " Loading " << example << " keeps your current sound so you can restore it afterwards.";
            if (ready)
            {
                keys.push_back ({ "loadexample", "Load " + example, false });
                keys.push_back ({ "usemine", "Use my sound", true });
            }
            else
            {
                keys.push_back ({ "close", "Not now", ! canLoad });
                if (canLoad)
                    keys.push_back ({ "loadexample", "Load " + example, true });
            }
            break;
        }
        case NoteKind::preparing:
        {
            const auto* lesson = lessonRunner.catalog().findLesson (preparingLesson);
            meta = lesson != nullptr ? lesson->title.toUpperCase() : juce::String();
            title = "Preparing the example";
            body = juce::String::fromUTF8 ("Loading and analysing the recording\xe2\x80\xa6 The lesson starts when it can be played.");
            keys.push_back ({ "close", "Cancel", false });
            break;
        }
        case NoteKind::restoreOffer:
            meta = "TUTORIAL";
            title = "Keep the example sound?";
            body = "The tutorial example replaced your sound. Your previous sound was kept and can be restored now, or later from the menu.";
            keys.push_back ({ "restore", "Restore previous sound", false });
            keys.push_back ({ "keep", "Keep this sound", true });
            break;
        case NoteKind::confirmReset:
            meta = "LEARN";
            title = "Reset tutorials?";
            body = "Every lesson of " + productName() + " starts again as not started. Your sound and settings are not changed.";
            keys.push_back ({ "cancel", "Cancel", false });
            keys.push_back ({ "reset", "Reset", true });
            break;
        case NoteKind::none:
            return;
    }
    juce::String signature = meta + "|" + title + "|" + body + "|" + (done ? "1" : "0");
    for (const auto& k : keys)
        signature << "|" << k.id << ":" << k.label;
    if (signature == noteSignature)
        return;
    noteSignature = signature;
    notePanel->setContent (meta, title, body, std::move (keys), done);
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
        // A quiet instruction at the foot of the instrument, out of the way of the header.
        const auto size = bannerNote->preferredSize();
        bannerNote->setBounds (area.getCentreX() - size.x / 2, area.getBottom() - size.y - 4, size.x, size.y);
    }
    std::optional<juce::Rectangle<int>> target;
    bool quiet = false;
    if (shownNote == NoteKind::lesson && lessonRunner.inTour() && lessonRunner.state() != LessonRunner::State::paused)
    {
        if (const auto* s = lessonRunner.step(); s != nullptr && s->targetId.isNotEmpty())
            target = registry.boundsIn (s->targetId, *hooks.content);
    }
    else if (shownNote == NoteKind::help)
        target = registry.boundsIn (helpTarget, *hooks.content);
    else if (inspecting && hoverTarget.isNotEmpty() && shownNote == NoteKind::none)
    {
        target = registry.boundsIn (hoverTarget, *hooks.content);
        quiet = true;
    }
    if (ring != nullptr)
        ring->setTarget (target, quiet);
    if (notePanel != nullptr)
    {
        if (shownNote != NoteKind::lesson && shownNote != NoteKind::help)
            placeBelowMenu (*notePanel);   // (the invitation, completion, preparation and confirmations)
        else
            placeNote (target);
    }
    if (browserPanel != nullptr)
        placeBelowMenu (*browserPanel);
}

void LearnController::placeBelowMenu (juce::Component& c)
{
    const auto area = contentBounds().reduced (4);
    const auto anchor = hooks.menuAnchor != nullptr ? hooks.menuAnchor() : juce::Rectangle<int> (area.getRight() - 40, area.getY() + 20, 30, 30);
    const int m = Note::margin;
    auto r = juce::Rectangle<int> (anchor.getRight() - c.getWidth() + m, anchor.getBottom() + 6 - m, c.getWidth(), c.getHeight());
    c.setBounds (r.constrainedWithin (area));
}

void LearnController::placeNote (std::optional<juce::Rectangle<int>> anchor)
{
    auto& note = *notePanel;
    const auto area = contentBounds().reduced (4);
    const int w = note.getWidth(), h = note.getHeight(), m = Note::margin;
    if (! anchor.has_value())
    {
        // No target (or it is not showing): under the header, centred.
        note.setBounds (juce::Rectangle<int> (area.getCentreX() - w / 2, area.getY() + 96 - m, w, h).constrainedWithin (area));
        return;
    }
    const auto t = *anchor;
    // The side with room: below, above, right, left; the card never covers its target.
    const std::array<juce::Rectangle<int>, 4> candidates { {
        { t.getCentreX() - w / 2, t.getBottom() + noteGap - m, w, h },
        { t.getCentreX() - w / 2, t.getY() - noteGap - h + m, w, h },
        { t.getRight() + noteGap - m, t.getCentreY() - h / 2, w, h },
        { t.getX() - noteGap - w + m, t.getCentreY() - h / 2, w, h },
    } };
    const auto avoid = t.expanded (4);
    for (const auto& c : candidates)
    {
        const auto inside = c.constrainedWithin (area);
        // Sliding along its side to stay inside is fine; crossing onto the target is not.
        if (! inside.reduced (m).intersects (avoid) && area.contains (inside))
        {
            note.setBounds (inside);
            return;
        }
    }
    // No side has room (a large target): the corner of the window farthest from its centre.
    const auto corner = juce::Rectangle<int> (t.getCentreX() < area.getCentreX() ? area.getRight() - w : area.getX(),
                                              t.getCentreY() < area.getCentreY() ? area.getBottom() - h : area.getY(), w, h);
    note.setBounds (corner);
}

//==============================================================================
void LearnController::persist (const Lesson& lesson, int stepIndex, bool finished, bool allStepsDone)
{
    juce::String resumeAt;
    if (finished && ! allStepsDone)
    {
        // Finished with skipped tasks: resume at the first one skipped.
        for (int s = 0; s < static_cast<int> (lesson.steps.size()); ++s)
            if (lessonRunner.lesson() == &lesson && lessonRunner.status (s) == LessonRunner::StepStatus::skipped)
            {
                resumeAt = lesson.steps[static_cast<std::size_t> (s)].id;
                break;
            }
    }
    else if (! finished && stepIndex >= 0 && stepIndex < static_cast<int> (lesson.steps.size()))
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
        if (p.status == "complete" && p.revision == revision)
            return;   // a lesson once completed stays completed
        p.revision = revision;
        p.status = "in-progress";
        p.completedAt = {};
        p.lastStep = resumeAt;
    });
    progress = progressStore.read();
}

int LearnController::resumeStep (const Lesson& lesson) const
{
    const auto it = progress.lessons.find (lesson.id);
    if (it == progress.lessons.end() || it->second.status != "in-progress" || it->second.revision != lesson.revision)
        return 0;
    for (int s = 0; s < static_cast<int> (lesson.steps.size()); ++s)
        if (lesson.steps[static_cast<std::size_t> (s)].id == it->second.lastStep)
            return s;
    return 0;
}

std::vector<Browser::Row> LearnController::browserRows() const
{
    std::vector<Browser::Row> rows;
    for (const auto& lesson : lessonRunner.catalog().lessons)
    {
        Browser::Row row;
        row.lessonId = lesson.id;
        row.title = lesson.title;
        row.group = lesson.group;
        row.minutes = lesson.minutes;
        if (const auto it = progress.lessons.find (lesson.id); it != progress.lessons.end() && it->second.revision == lesson.revision)
            row.status = it->second.status == "complete" ? Browser::Row::Status::complete : Browser::Row::Status::inProgress;
        rows.push_back (row);
    }
    return rows;
}

//==============================================================================
std::optional<float> LearnController::parameterValue (const juce::String& id)
{
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

} // namespace andor::learn
