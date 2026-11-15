#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <optional>
#include <vector>

/**
    ANDOR Learn: guided lessons and a contextual Learn mode, as declarative data. A product
    describes its lessons and help regions with these types (immutable once handed over); the
    shared runner, overlay and store do the rest. Nothing here touches sound: a lesson only
    names parameters it reads to verify what the musician did.

    The core depends on juce_core only (no components), so it can be tested headless.
*/
namespace andor::learn
{

/** Who caused a change. Only the three user origins can complete a step. */
enum class Origin
{
    userPointer,
    userKeyboard,
    userAccessibility,
    hostAutomation,
    presetRecall,
    tutorial,
    unknown
};

constexpr bool isUserOrigin (Origin o) noexcept
{
    return o == Origin::userPointer || o == Origin::userKeyboard || o == Origin::userAccessibility;
}

/**
    A committed interaction, reported by the control that handled it (in its own message-thread
    handler, after its attachment wrote the parameter). Never derived from a parameter
    callback: those also fire for automation, presets and undo.
*/
struct LearnEvent
{
    juce::String actionId;      ///< "arp.pattern.changed"
    juce::String targetId;      ///< "arp.pattern"
    juce::String parameterId;   ///< the parameter it wrote, if any ("analog.arp.cardB.rate")
    Origin origin = Origin::unknown;
    std::uint64_t stepGeneration = 0;   ///< the step it was made in (stamped by the controller)
};

/**
    A postcondition, read on the message thread after a user event: parameter values in their
    real units (a choice's index, a switch's 0 / 1), or a named UI state the product's
    registry answers ("arp.panel.open"). Composable with all / any.
*/
struct Condition
{
    enum class Kind
    {
        always,
        parameterChanged,   ///< differs from its value when the step began
        parameterEquals,    ///< equals `value` (within half a step)
        state,              ///< a registered UI state is true
        all,
        any
    };
    Kind kind = Kind::always;
    juce::String id;
    float value = 0.0f;
    std::vector<Condition> terms;

    static Condition always() { return {}; }
    static Condition changed (juce::String parameterId) { return { Kind::parameterChanged, std::move (parameterId), 0.0f, {} }; }
    static Condition equals (juce::String parameterId, float v) { return { Kind::parameterEquals, std::move (parameterId), v, {} }; }
    static Condition state (juce::String stateId) { return { Kind::state, std::move (stateId), 0.0f, {} }; }
    static Condition all (std::vector<Condition> t) { return { Kind::all, {}, 0.0f, std::move (t) }; }
    static Condition any (std::vector<Condition> t) { return { Kind::any, {}, 0.0f, std::move (t) }; }

    /** The parameters it reads (snapshotted when a step begins). */
    void collectParameters (juce::StringArray& into) const
    {
        if (kind == Kind::parameterChanged || kind == Kind::parameterEquals)
            into.addIfNotAlreadyThere (id);
        for (const auto& t : terms)
            t.collectParameters (into);
    }
    void collectStates (juce::StringArray& into) const
    {
        if (kind == Kind::state)
            into.addIfNotAlreadyThere (id);
        for (const auto& t : terms)
            t.collectStates (into);
    }
};

/** How a step is completed. */
struct Completion
{
    enum class Kind
    {
        manual,       ///< an explanation: the musician reads it and presses Next (or Finish)
        userAction    ///< a user event among `actions`, then `postcondition` holds
    };
    Kind kind = Kind::manual;
    juce::StringArray actions;
    Condition postcondition;
    /** Already true when the step begins (the arpeggiator is on): the note offers
        "<alreadyLabel> · Continue" instead of making the task impossible. */
    std::optional<Condition> alreadyDone;
    juce::String alreadyLabel;

    static Completion manual() { return {}; }
    static Completion action (juce::StringArray accepted, Condition post)
    {
        Completion c;
        c.kind = Kind::userAction;
        c.actions = std::move (accepted);
        c.postcondition = std::move (post);
        return c;
    }
    Completion withAlreadyDone (Condition when, juce::String label) const
    {
        auto c = *this;
        c.alreadyDone = std::move (when);
        c.alreadyLabel = std::move (label);
        return c;
    }
};

struct Step
{
    juce::String id, title, body;
    juce::String targetId;         ///< what the note points at (empty: no target, centred)
    juce::String unavailableBody;  ///< said while the target cannot be shown (empty: a generic line)
    Completion completion;
    bool optional = false;
    /** The step opens what its target needs when it begins (the ARP panel for PATTERN). Off
        when opening it is the task itself: the note's "Show me" does it instead. */
    bool revealOnEnter = true;
    /** Said instead of `body` while this holds (MORPH with nothing playable in card C). */
    std::optional<Condition> variantWhen;
    juce::String variantBody;
};

struct Lesson
{
    juce::String id;          ///< "analog.arp.basics" (stable: progress is kept under it)
    int revision = 1;
    juce::String title;       ///< "Make a moving pattern"
    juce::String group;       ///< the browser's group ("Arpeggiator")
    juce::String capability;  ///< required product capability (empty: none)
    int minutes = 0;          ///< an estimate for the browser (0: none shown)
    std::vector<Step> steps;
};

/** Contextual help for a region in Learn mode. */
struct HelpTopic
{
    juce::String targetId;
    juce::String title, body;
    juce::String lessonId;    ///< a lesson it can start (empty: none, and no button pretends)
};

struct Catalog
{
    juce::String productId;            ///< "analog": the progress file's namespace
    std::vector<Lesson> lessons;       ///< in the browser's order
    std::vector<HelpTopic> help;
    juce::String quickStartLessonId;   ///< the invitation's first choice (empty: none)

    const Lesson* findLesson (const juce::String& id) const
    {
        for (const auto& l : lessons)
            if (l.id == id)
                return &l;
        return nullptr;
    }
    const HelpTopic* findHelp (const juce::String& targetId) const
    {
        for (const auto& h : help)
            if (h.targetId == targetId)
                return &h;
        return nullptr;
    }
};

} // namespace andor::learn
