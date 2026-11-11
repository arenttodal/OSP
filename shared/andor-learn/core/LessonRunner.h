#pragma once

#include "LessonTypes.h"

#include <functional>
#include <map>

namespace andor::learn
{

/** What the runner asks of the editor (message thread only). Reading values and showing
    panels; never writing a parameter. */
struct RunnerHost
{
    virtual ~RunnerHost() = default;
    /** A parameter's value in its real units (a choice's index), or nothing if unknown. */
    virtual std::optional<float> parameterValue (const juce::String& parameterId) = 0;
    /** A registered UI state ("arp.panel.open"). */
    virtual bool state (const juce::String& stateId) = 0;
    /** The target can be pointed at now (shown, with a size). */
    virtual bool targetReady (const juce::String& targetId) = 0;
    /** Shows what the target needs (a panel, a tab): navigation only, never the sound. */
    virtual bool reveal (const juce::String& targetId) = 0;
};

/**
    One editor's lesson state machine. It has no components: the overlay draws what it says.
    A step completes only through handle(): a user-origin event of the step's generation,
    among the actions the step accepts, after which its postcondition holds (parameter values
    compared with their values when the step began). Host automation, presets, undo and the
    tutorial's own navigation cannot complete anything because they never produce such an
    event. Only the musician moves on (Next), except when they choose Continue on a step that
    was already done.
*/
class LessonRunner
{
public:
    enum class State
    {
        idle,
        topicBrowser,
        inspection,
        tourPreparing,
        tourShowing,       ///< an explanation step (Next)
        waitingForAction,  ///< a task step, not yet done
        stepComplete,      ///< the task is done; Next moves on
        tourComplete,
        paused,            ///< the step's target cannot be shown (a panel was closed)
        stopped
    };
    enum class StepStatus
    {
        pending,
        done,        ///< the musician did it
        continued,   ///< it was already so; they chose Continue
        skipped
    };

    explicit LessonRunner (RunnerHost&);

    void setCatalog (Catalog);
    const Catalog& catalog() const noexcept { return lessons; }

    /** Starts a lesson at a step (a resumed one). False: unknown lesson or step. */
    bool start (const juce::String& lessonId, int fromStep = 0);
    /** A user event: true when it completed the current step. */
    bool handle (const LearnEvent&);
    /** Next (an explanation, or a completed task); on the last step: Finish. */
    void next();
    void back();
    /** Marks the step skipped (not done) and moves on. */
    void skip();
    /** The step was already done when it began: mark it so and move on. False if not. */
    bool continueAlreadyDone();
    /** "Show me": opens what the target needs. A demonstration: completes nothing. */
    bool showMe();
    /** Stops the tour (Close). Progress is reported, the sound is left as it is. */
    void close();
    /** The editor's tick: pauses while the target cannot be shown, resumes after; and, while no
        gesture is under way, moves the step's baseline to the parameters' present values, so a
        change made by automation, a preset or undo between gestures never counts as the
        musician's (only what changes during their gesture does). */
    void update();
    /** A musician's press is under way (a knob held): the baseline stays where it was. */
    void setGestureActive (bool held) noexcept { gesture = held; }
    /** The browser / Learn mode, while no tour runs (the states they are in). */
    void setBrowsing (bool open);
    void setInspecting (bool on);

    State state() const noexcept { return current; }
    bool inTour() const noexcept;
    const Lesson* lesson() const noexcept { return active; }
    int stepIndex() const noexcept { return index; }
    const Step* step() const noexcept;
    bool isLastStep() const noexcept;
    StepStatus status (int step) const noexcept;
    /** Bumped whenever a step begins or the tour ends: an event from another step is stale. */
    std::uint64_t generation() const noexcept { return gen; }
    /** The current step's task is already done (live): offer "<label> · Continue". */
    bool canContinue() const;
    /** What the note says for the current step now (its variant, or why it is paused). */
    juce::String currentBody() const;
    /** True if every step was done or continued (none skipped). */
    bool allDone() const noexcept;

    /** Something to redraw. */
    std::function<void()> onChanged;
    /** A tour ended or left a step: persist (finished: reached Finish). */
    std::function<void (const Lesson&, int stepIndex, bool finished, bool allStepsDone)> onProgress;

private:
    void enterStep (int);
    void changed();
    bool holds (const Condition&) const;
    State settledState() const;

    RunnerHost& host;
    Catalog lessons;
    const Lesson* active = nullptr;
    int index = -1;
    State current = State::idle;
    State beforePause = State::idle;
    std::uint64_t gen = 0;
    bool gesture = false;
    std::vector<StepStatus> statuses;
    std::map<juce::String, float> snapshot;
};

} // namespace andor::learn
