#pragma once

#include "LessonTypes.h"

#include <functional>

namespace andor::learn
{

/**
    Validation and capability filtering of a product's catalog, run when an editor opens Learn.
    A catalog that fails validation is never shown (the menu says Learn is unavailable), so a
    broken lesson cannot strand a musician mid-tour.
*/
struct CatalogChecks
{
    /** A target ID the product registered (null: targets are not checked). */
    std::function<bool (const juce::String&)> targetKnown;
    /** A UI state ID the product registered (null: not checked). */
    std::function<bool (const juce::String&)> stateKnown;
    /** A parameter ID the processor has (null: not checked). */
    std::function<bool (const juce::String&)> parameterKnown;
};

/** Every problem found (empty: valid). Duplicate lesson, step or help IDs, lessons without
    steps, user-action steps without accepted actions, unknown targets / states / parameters,
    help that starts an unknown lesson, an unknown quick start. Lessons are linear (Next,
    Back, Skip), so there are no loops to find. */
juce::StringArray validate (const Catalog&, const CatalogChecks& = {});

/** The catalog with only the lessons whose capability the product has (a lesson for a
    feature the build lacks is never advertised); help and the quick start lose references
    to removed lessons. */
Catalog available (const Catalog&, const std::function<bool (const juce::String& capability)>& hasCapability);

} // namespace andor::learn
