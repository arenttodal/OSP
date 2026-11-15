# ANDOR Learn (shared/andor-learn): guided lessons and a contextual Learn mode for products
# built from the ANDOR Instrument Shell. Compiled into every shell product (a few small
# sources); it does nothing unless the product opts in (ProductInfo::learn).
#   core/  lesson data, catalog checks, the runner, the per-user progress (juce_core only)
#   juce/  targets, user-action bridge, overlay, browser, controller (juce_gui_basics)

set(ANDOR_LEARN_DIR ${CMAKE_CURRENT_LIST_DIR})

set(ANDOR_LEARN_SOURCES
    ${ANDOR_LEARN_DIR}/core/LessonCatalog.cpp
    ${ANDOR_LEARN_DIR}/core/LessonRunner.cpp
    ${ANDOR_LEARN_DIR}/core/ProgressStore.cpp
    ${ANDOR_LEARN_DIR}/juce/LearnTargetRegistry.cpp
    ${ANDOR_LEARN_DIR}/juce/LearnActionBridge.cpp
    ${ANDOR_LEARN_DIR}/juce/LearnOverlay.cpp
    ${ANDOR_LEARN_DIR}/juce/LearnBrowser.cpp
    ${ANDOR_LEARN_DIR}/juce/LearnController.cpp)

set(ANDOR_LEARN_INCLUDE_DIRS ${ANDOR_LEARN_DIR}/core ${ANDOR_LEARN_DIR}/juce)
