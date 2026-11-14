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
        || current == State::paused;
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
    for (std::size_t s = 0; s < statuses.size(); ++s)
    {
        // The summary (a final explanation) counts as done once Finish is pressed.
        if (statuses[s] == StepStatus::skipped)
            return false;
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
    current = State::tourPreparing;
    const auto* st = step();
    snapshot.clear();
    juce::StringArray parameters;
    st->completion.postcondition.collectParameters (parameters);
    if (st->completion.alreadyDone.has_value())
        st->completion.alreadyDone->collectParameters (parameters);
    for (const auto& id : parameters)
        if (const auto v = host.parameterValue (id))
            snapshot[id] = *v;
    // Re-entering a step (Back) starts it over: its task is asked again.
    statuses[static_cast<std::size_t> (s)] = StepStatus::pending;
    if (st->revealOnEnter && st->targetId.isNotEmpty())
        host.reveal (st->targetId);
    current = settledState();
    beforePause = current;
    if (st->targetId.isNotEmpty() && ! host.targetReady (st->targetId))
        current = State::paused;
    changed();
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
    if (! holds (st->completion.postcondition))
        return false;
    statuses[static_cast<std::size_t> (index)] = StepStatus::done;
    current = State::stepComplete;
    beforePause = current;
    ++gen;   // the same event can never complete another step
    changed();
    return true;
}

bool LessonRunner::canContinue() const
{
    const auto* st = step();
    if (st == nullptr || (current != State::waitingForAction && current != State::paused) || ! st->completion.alreadyDone.has_value())
        return false;
    return holds (*st->completion.alreadyDone);
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

void LessonRunner::next()
{
    if (current != State::tourShowing && current != State::stepComplete)
        return;
    if (current == State::tourShowing)
        statuses[static_cast<std::size_t> (index)] = StepStatus::done;
    if (isLastStep())
    {
        current = State::tourComplete;
        ++gen;
        if (onProgress != nullptr)
            onProgress (*active, index, true, allDone());
        changed();
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

void LessonRunner::skip()
{
    if (! inTour())
        return;
    if (statuses[static_cast<std::size_t> (index)] == StepStatus::pending)
        statuses[static_cast<std::size_t> (index)] = StepStatus::skipped;
    if (isLastStep())
    {
        current = State::tourComplete;
        ++gen;
        if (onProgress != nullptr)
            onProgress (*active, index, true, allDone());
        changed();
        return;
    }
    enterStep (index + 1);
}

bool LessonRunner::continueAlreadyDone()
{
    if (! canContinue())
        return false;
    statuses[static_cast<std::size_t> (index)] = StepStatus::continued;
    if (isLastStep())
    {
        current = State::tourComplete;
        ++gen;
        if (onProgress != nullptr)
            onProgress (*active, index, true, allDone());
        changed();
        return true;
    }
    enterStep (index + 1);
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
    if (st == nullptr)
        return;
    if (! gesture && (current == State::waitingForAction || current == State::paused))
        for (auto& [id, value] : snapshot)
            if (const auto now = host.parameterValue (id))
                value = *now;
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
        current = beforePause == State::paused ? settledState() : beforePause;
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
