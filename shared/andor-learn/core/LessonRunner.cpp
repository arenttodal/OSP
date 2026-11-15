#include "LessonRunner.h"

#include <cmath>

namespace andor::learn
{

LessonRunner::LessonRunner (RunnerHost& h) : host (h) {}

void LessonRunner::setCatalog (Catalog c)
{
    if (inTour())
        close();
    active = nullptr;
    index = -1;
    lessons = std::move (c);
}

bool LessonRunner::inTour() const noexcept
{
    return current == State::tourPreparing || current == State::tourShowing || current == State::waitingForAction || current == State::stepComplete
        || current == State::paused || current == State::blocked;
}

const Step* LessonRunner::step() const noexcept
{
    if (active == nullptr || index < 0 || index >= static_cast<int> (active->steps.size()))
        return nullptr;
    return &active->steps[static_cast<std::size_t> (index)];
}

bool LessonRunner::isLastStep() const noexcept
{
    return active != nullptr && index == static_cast<int> (active->steps.size()) - 1;
}

LessonRunner::StepStatus LessonRunner::status (int s) const noexcept
{
    if (s < 0 || s >= static_cast<int> (statuses.size()))
        return StepStatus::pending;
    return statuses[static_cast<std::size_t> (s)];
}

bool LessonRunner::allDone() const noexcept
{
    if (active == nullptr)
        return false;
    // From where this tour began (a resumed lesson's earlier steps were done before), every
    // task was done or already so - there is no way past one otherwise.
    for (std::size_t s = static_cast<std::size_t> (std::max (0, startedAt)); s < statuses.size(); ++s)
    {
        // The summary (a final explanation) counts as done once Finish is pressed.
        if (statuses[s] == StepStatus::pending && active->steps[s].completion.kind == Completion::Kind::userAction && ! active->steps[s].optional)
            return false;
    }
    return true;
}

bool LessonRunner::start (const juce::String& lessonId, int fromStep)
{
    const auto* lesson = lessons.findLesson (lessonId);
    if (lesson == nullptr || lesson->steps.empty() || fromStep < 0 || fromStep >= static_cast<int> (lesson->steps.size()))
        return false;
    if (inTour())
        close();
    active = lesson;
    loadedExample = false;
    startedAt = fromStep;
    statuses.assign (lesson->steps.size(), StepStatus::pending);
    enterStep (fromStep);
    return true;
}

LessonRunner::State LessonRunner::settledState() const
{
    const auto* s = step();
    if (s == nullptr)
        return State::idle;
    if (statuses[static_cast<std::size_t> (index)] == StepStatus::done && s->completion.kind == Completion::Kind::userAction)
        return State::stepComplete;
    return s->completion.kind == Completion::Kind::manual ? State::tourShowing : State::waitingForAction;
}

void LessonRunner::enterStep (int s)
{
    index = s;
    ++gen;   // events of the previous step are stale from here
    gesture = false;
    pendingTicks = 0;
    current = State::tourPreparing;
    const auto* st = step();
    snapshot.clear();
    juce::StringArray parameters;
    st->completion.postcondition.collectParameters (parameters);
    if (st->completion.alreadyDone.has_value())
        st->completion.alreadyDone->collectParameters (parameters);
    for (const auto& r : st->requirements)
        r.holds.collectParameters (parameters);
    for (const auto& id : parameters)
        if (const auto v = host.parameterValue (id))
            snapshot[id] = *v;
    // Re-entering a step (Back) starts it over: its task is asked again.
    statuses[static_cast<std::size_t> (s)] = StepStatus::pending;
    if (st->revealOnEnter && st->targetId.isNotEmpty())
        host.reveal (st->targetId);
    current = settledState();
    beforePause = current;
    if (! st->preparation && unmetRequirement() != nullptr)
        current = State::blocked;   // (what it needs first: the note says what, and where)
    else if (st->targetId.isNotEmpty() && ! host.targetReady (st->targetId))
        current = State::paused;
    changed();
}

const Requirement* LessonRunner::unmetRequirement() const
{
    const auto* st = step();
    if (st == nullptr)
        return nullptr;
    for (const auto& r : st->requirements)
        if (! holds (r.holds))
            return &r;
    return nullptr;
}

bool LessonRunner::preparationReady() const
{
    const auto* st = step();
    return st != nullptr && st->preparation && unmetRequirement() == nullptr;
}

bool LessonRunner::holds (const Condition& c) const
{
    switch (c.kind)
    {
        case Condition::Kind::always:
            return true;
        case Condition::Kind::parameterChanged:
        {
            const auto now = host.parameterValue (c.id);
            const auto then = snapshot.find (c.id);
            return now.has_value() && then != snapshot.end() && std::abs (*now - then->second) > 1.0e-4f * std::max (1.0f, std::abs (then->second));
        }
        case Condition::Kind::parameterEquals:
        {
            // Within half a step: the switches and choices lessons check are whole numbers.
            const auto now = host.parameterValue (c.id);
            return now.has_value() && std::abs (*now - c.value) < 0.5f;
        }
        case Condition::Kind::state:
            return host.state (c.id);
        case Condition::Kind::all:
            for (const auto& t : c.terms)
                if (! holds (t))
                    return false;
            return ! c.terms.empty();
        case Condition::Kind::any:
            for (const auto& t : c.terms)
                if (holds (t))
                    return true;
            return false;
        case Condition::Kind::negation:
            return c.terms.size() == 1 && ! holds (c.terms.front());
    }
    return false;
}

bool LessonRunner::handle (const LearnEvent& e)
{
    if (current != State::waitingForAction || ! isUserOrigin (e.origin) || e.stepGeneration != gen)
        return false;
    const auto* st = step();
    if (st == nullptr || st->completion.kind != Completion::Kind::userAction || ! st->completion.actions.contains (e.actionId))
        return false;
    if (st->completion.parameterId.isNotEmpty() && e.parameterId != st->completion.parameterId)
        return false;   // (another control's)
    if (unmetRequirement() != nullptr)
        return false;
    if (! holds (st->completion.postcondition))
    {
        // The touch opened a menu whose choice comes later (a dropdown): the step waits a
        // little for it, its baseline held still meanwhile.
        if (st->completion.choiceFollows)
            pendingTicks = pendingTouchTicks;
        return false;
    }
    pendingTicks = 0;
    statuses[static_cast<std::size_t> (index)] = StepStatus::done;
    current = State::stepComplete;
    beforePause = current;
    ++gen;   // the same event can never complete another step
    if (st->advanceWhenDone && ! isLastStep())
    {
        enterStep (index + 1);
        return true;
    }
    changed();
    return true;
}

bool LessonRunner::canContinue() const
{
    const auto* st = step();
    if (st == nullptr || (current != State::waitingForAction && current != State::paused) || ! st->completion.alreadyDone.has_value())
        return false;
    return unmetRequirement() == nullptr && holds (*st->completion.alreadyDone);
}

juce::String LessonRunner::currentBody() const
{
    const auto* st = step();
    if (st == nullptr)
        return {};
    if (current == State::paused)
        return st->unavailableBody.isNotEmpty() ? st->unavailableBody
                                                : juce::String::fromUTF8 ("This control isn\xe2\x80\x99t showing right now. Show me opens it.");
    if (st->variantWhen.has_value() && holds (*st->variantWhen))
        return st->variantBody;
    return st->body;
}

void LessonRunner::finishTour()
{
    current = State::tourComplete;
    ++gen;
    if (onProgress != nullptr)
        onProgress (*active, index, true, allDone());
    changed();
}

void LessonRunner::next()
{
    if (current != State::tourShowing && current != State::stepComplete)
        return;
    if (current == State::tourShowing)
        statuses[static_cast<std::size_t> (index)] = StepStatus::done;
    if (isLastStep())
    {
        finishTour();
        return;
    }
    enterStep (index + 1);
}

void LessonRunner::back()
{
    if (! inTour() || index <= 0)
        return;
    enterStep (index - 1);
}

bool LessonRunner::continueAlreadyDone()
{
    if (! canContinue())
        return false;
    statuses[static_cast<std::size_t> (index)] = StepStatus::continued;
    if (isLastStep())
    {
        finishTour();
        return true;
    }
    enterStep (index + 1);
    return true;
}

bool LessonRunner::useCurrentSound()
{
    const auto* st = step();
    if (st == nullptr || ! st->preparation || ! inTour())
        return false;
    statuses[static_cast<std::size_t> (index)] = StepStatus::done;
    if (isLastStep())
        finishTour();
    else
        enterStep (index + 1);
    return true;
}

bool LessonRunner::loadExample()
{
    const auto* st = step();
    if (st == nullptr || active == nullptr || active->exampleId.isEmpty() || ! inTour())
        return false;
    if (! host.loadExample (active->exampleId))
        return false;   // (nothing was changed: the note says it stays as it is)
    loadedExample = true;
    // The example replaced every parameter: the step's baseline follows it (a load is never the
    // musician's change of a control).
    for (auto& [id, value] : snapshot)
        if (const auto now = host.parameterValue (id))
            value = *now;
    if (st->preparation)
        return useCurrentSound();
    update();
    changed();
    return true;
}

bool LessonRunner::showMe()
{
    const auto* st = step();
    if (! inTour() || st == nullptr || st->targetId.isEmpty())
        return false;
    const bool shown = host.reveal (st->targetId);
    update();
    changed();
    return shown;
}

bool LessonRunner::revealRequirement()
{
    const auto* r = unmetRequirement();
    if (! inTour() || r == nullptr || r->targetId.isEmpty())
        return false;
    const bool shown = host.reveal (r->targetId);
    update();
    changed();
    return shown;
}

void LessonRunner::close()
{
    if (! inTour() && current != State::tourComplete)
        return;
    const bool wasTour = inTour();
    current = State::stopped;
    ++gen;
    if (wasTour && active != nullptr && onProgress != nullptr)
        onProgress (*active, index, false, false);
    changed();
}

void LessonRunner::update()
{
    const auto* st = step();
    if (st == nullptr || ! inTour())
        return;
    if (pendingTicks > 0 && current == State::waitingForAction)
    {
        // The musician's touch is still being answered (a menu item chosen after the click).
        --pendingTicks;
        if (unmetRequirement() == nullptr && holds (st->completion.postcondition))
        {
            pendingTicks = 0;
            statuses[static_cast<std::size_t> (index)] = StepStatus::done;
            current = State::stepComplete;
            beforePause = current;
            ++gen;
            changed();
            return;
        }
    }
    if (! gesture && pendingTicks == 0 && (current == State::waitingForAction || current == State::paused || current == State::blocked))
        for (auto& [id, value] : snapshot)
            if (const auto now = host.parameterValue (id))
                value = *now;
    // What the step needs, checked on the real state each tick: missing, it waits for the fix
    // (blocked); fixed, it carries on where it was.
    if (! st->preparation && current != State::stepComplete)
    {
        const bool missing = unmetRequirement() != nullptr;
        if (missing && current != State::blocked)
        {
            beforePause = current;
            current = State::blocked;
            changed();
            return;
        }
        if (! missing && current == State::blocked)
        {
            current = settledState();
            if (st->targetId.isNotEmpty() && ! host.targetReady (st->targetId))
                current = State::paused;
            changed();
            return;
        }
        if (missing)
            return;
    }
    if (st->targetId.isEmpty())
        return;
    if (current == State::tourShowing || current == State::waitingForAction)
    {
        if (! host.targetReady (st->targetId))
        {
            beforePause = current;
            current = State::paused;
            changed();
        }
    }
    else if (current == State::paused && host.targetReady (st->targetId))
    {
        current = beforePause == State::paused || beforePause == State::blocked ? settledState() : beforePause;
        changed();
    }
}

void LessonRunner::setBrowsing (bool open)
{
    if (inTour())
        return;
    current = open ? State::topicBrowser : State::idle;
    changed();
}

void LessonRunner::setInspecting (bool on)
{
    if (inTour())
        return;
    current = on ? State::inspection : State::idle;
    changed();
}

void LessonRunner::changed()
{
    if (onChanged != nullptr)
        onChanged();
}

} // namespace andor::learn
