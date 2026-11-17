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
    /** Loads the lesson's example sound, keeping the sound before it for Restore (only ever
        called on the musician's explicit choice). False: not loaded (nothing was changed). */
    virtual bool loadExample (const juce::String& exampleId) { juce::ignoreUnused (exampleId); return false; }
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
        blocked,           ///< something the step needs is missing (a Requirement): fix it first
        stopped
    };
    /** There is no Skip: a step is done by the musician, or continued because it already was. */
    enum class StepStatus
    {
        pending,
        done,        ///< the musician did it
        continued    ///< it was already so; they chose Continue
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
    /** The step was already done when it began: mark it so and move on. False if not. */
    bool continueAlreadyDone();
    /** Opens what the target needs (the note's reveal key). Navigation: completes nothing. */
    bool showMe();
    /** Opens what fixes the current unmet requirement (its target). Completes nothing. */
    bool revealRequirement();
    /** The musician chose Load Example Sound: loads it (the sound before it is kept). A
        preparation step then moves on; a blocked step re-checks what it needs. */
    bool loadExample();
    /** The preparation step: Use Current Sound (moves on, whatever is missing). */
    bool useCurrentSound();
    /** Stops the tour (Close). Progress is reported, the sound is left as it is. */
    void close();
    /** The editor's tick: pauses while the target cannot be shown, resumes after; and, while no
        gesture is under way, moves the step's baseline to the parameters' present values, so a
        change made by automation, a preset or undo between gestures never counts as the
        musician's (only what changes during their gesture does). */
    void update();
    /** How long an accepted touch waits for its result (a dropdown's menu): about 10 s of ticks. */
    static constexpr int pendingTouchTicks = 300;
    /** A musician's press is under way (a knob held): the baseline stays where it was. */
    void setGestureActive (bool held) noexcept { gesture = held; }
    bool gestureActive() const noexcept { return gesture; }
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
    /** The current step's task is already done (live) and nothing it needs is missing. */
    bool canContinue() const;
    /** The first thing the current step needs that is missing now (null: none). */
    const Requirement* unmetRequirement() const;
    /** The preparation step's sound is ready (every requirement holds). */
    bool preparationReady() const;
    /** An example was loaded during this tour. */
    bool exampleLoaded() const noexcept { return loadedExample; }
    /** What the note says for the current step now (its variant, or why it is paused). */
    juce::String currentBody() const;
    /** True if every task step was done or continued. */
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
    void finishTour();

    RunnerHost& host;
    Catalog lessons;
    const Lesson* active = nullptr;
    int index = -1;
    State current = State::idle;
    State beforePause = State::idle;
    std::uint64_t gen = 0;
    bool gesture = false;
    bool loadedExample = false;
    /** An accepted touch whose result is still coming (a dropdown's menu is open): ticks left
        in which the postcondition completes the step once it holds. */
    int pendingTicks = 0;
    int startedAt = 0;   ///< the step this tour began at (a resumed lesson's)
    std::vector<StepStatus> statuses;
    std::map<juce::String, float> snapshot;
};

} // namespace andor::learn
