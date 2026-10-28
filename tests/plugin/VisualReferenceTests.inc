// ANDOR visual regression, ANDOR OSP's side (hidden): renders the shared states
// (tests/support/VisualStates.h) into ANDOR_VISUAL_REFERENCE_DIR for the shell products'
// comparison (ctest runs it as osp_visual_reference before andor_*_tests_visual).
// Included at the end of PluginProcessorTests.cpp.

#include "../support/VisualStates.h"

TEST_CASE ("plugin: ANDOR visual regression reference (OSP renders the shared states)", "[.][visual-reference]")
{
    const char* dirEnv = std::getenv ("ANDOR_VISUAL_REFERENCE_DIR");
    REQUIRE (dirEnv != nullptr);
    const juce::File out (dirEnv);
    out.deleteRecursively();
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedLook ceramic (skin::Look::ceramic);
    visualstates::render<OspAudioProcessor, osp::plugin::OspAudioProcessorEditor> (out, tmp.dir, [] { return std::make_unique<OspAudioProcessor>(); });
    for (const auto& name : visualstates::names())
        CHECK (out.getChildFile (name + ".png").existsAsFile());
}
