#include "LessonCatalog.h"

namespace andor::learn
{

namespace
{
    void checkCondition (const Condition& c, const CatalogChecks& checks, const juce::String& where, juce::StringArray& problems)
    {
        switch (c.kind)
        {
            case Condition::Kind::always:
                break;
            case Condition::Kind::parameterChanged:
            case Condition::Kind::parameterEquals:
                if (c.id.isEmpty())
                    problems.add (where + ": a parameter condition without a parameter");
                else if (checks.parameterKnown != nullptr && ! checks.parameterKnown (c.id))
                    problems.add (where + ": unknown parameter " + c.id);
                break;
            case Condition::Kind::state:
                if (c.id.isEmpty())
                    problems.add (where + ": a state condition without a state");
                else if (checks.stateKnown != nullptr && ! checks.stateKnown (c.id))
                    problems.add (where + ": unknown state " + c.id);
                break;
            case Condition::Kind::all:
            case Condition::Kind::any:
                if (c.terms.empty())
                    problems.add (where + ": an empty all / any");
                for (const auto& t : c.terms)
                    checkCondition (t, checks, where, problems);
                break;
        }
    }
} // namespace

juce::StringArray validate (const Catalog& catalog, const CatalogChecks& checks)
{
    juce::StringArray problems, lessonIds, helpIds;
    if (catalog.productId.isEmpty())
        problems.add ("the catalog has no product ID");
    for (const auto& lesson : catalog.lessons)
    {
        if (lesson.id.isEmpty())
            problems.add ("a lesson without an ID");
        else if (lessonIds.contains (lesson.id))
            problems.add ("duplicate lesson " + lesson.id);
        lessonIds.add (lesson.id);
        if (lesson.revision < 1)
            problems.add (lesson.id + ": revision must be 1 or more");
        if (lesson.title.isEmpty())
            problems.add (lesson.id + ": no title");
        if (lesson.steps.empty())
            problems.add (lesson.id + ": no steps");
        juce::StringArray stepIds;
        for (const auto& step : lesson.steps)
        {
            const auto where = lesson.id + "/" + step.id;
            if (step.id.isEmpty())
                problems.add (lesson.id + ": a step without an ID");
            else if (stepIds.contains (step.id))
                problems.add ("duplicate step " + where);
            stepIds.add (step.id);
            if (step.title.isEmpty())
                problems.add (where + ": no title");
            if (step.targetId.isNotEmpty() && checks.targetKnown != nullptr && ! checks.targetKnown (step.targetId))
                problems.add (where + ": unknown target " + step.targetId);
            if (step.completion.kind == Completion::Kind::userAction && step.completion.actions.isEmpty())
                problems.add (where + ": a user-action step that accepts no action");
            checkCondition (step.completion.postcondition, checks, where, problems);
            if (step.completion.alreadyDone.has_value())
                checkCondition (*step.completion.alreadyDone, checks, where, problems);
            if (step.variantWhen.has_value())
            {
                checkCondition (*step.variantWhen, checks, where, problems);
                if (step.variantBody.isEmpty())
                    problems.add (where + ": a variant without its text");
            }
        }
    }
    for (const auto& help : catalog.help)
    {
        if (help.targetId.isEmpty())
            problems.add ("help without a target");
        else if (helpIds.contains (help.targetId))
            problems.add ("duplicate help for " + help.targetId);
        helpIds.add (help.targetId);
        if (help.title.isEmpty() || help.body.isEmpty())
            problems.add ("help for " + help.targetId + " needs a title and a description");
        if (help.targetId.isNotEmpty() && checks.targetKnown != nullptr && ! checks.targetKnown (help.targetId))
            problems.add ("help for unknown target " + help.targetId);
        if (help.lessonId.isNotEmpty() && ! lessonIds.contains (help.lessonId))
            problems.add ("help for " + help.targetId + " starts unknown lesson " + help.lessonId);
    }
    if (catalog.quickStartLessonId.isNotEmpty() && ! lessonIds.contains (catalog.quickStartLessonId))
        problems.add ("unknown quick start lesson " + catalog.quickStartLessonId);
    return problems;
}

Catalog available (const Catalog& catalog, const std::function<bool (const juce::String&)>& hasCapability)
{
    Catalog out;
    out.productId = catalog.productId;
    for (const auto& lesson : catalog.lessons)
        if (lesson.capability.isEmpty() || (hasCapability != nullptr && hasCapability (lesson.capability)))
            out.lessons.push_back (lesson);
    for (auto help : catalog.help)
    {
        if (help.lessonId.isNotEmpty() && out.findLesson (help.lessonId) == nullptr)
            help.lessonId = {};
        out.help.push_back (std::move (help));
    }
    if (out.findLesson (catalog.quickStartLessonId) != nullptr)
        out.quickStartLessonId = catalog.quickStartLessonId;
    return out;
}

} // namespace andor::learn
