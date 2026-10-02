add_executable(osp_tests
    support/TestHelpers.cpp
    unit/PitchMathTests.cpp
    unit/PrngTests.cpp
    unit/InterpolationTests.cpp
    unit/AdsrTests.cpp
    unit/SamplerTests.cpp
    unit/AudioFileTests.cpp
    unit/JsonTests.cpp
    unit/HashTests.cpp
    unit/MidiTests.cpp
    unit/AnalysisTests.cpp
    unit/ModelExchangeTests.cpp
    unit/PlaybackPreparationTests.cpp
    unit/PitchEngineTests.cpp
    unit/ContinuationTests.cpp
    unit/PerformanceTests.cpp
    integration/RenderIntegrationTests.cpp
    integration/CorpusIntegrationTests.cpp
    integration/BakeoffIntegrationTests.cpp
    integration/EngineIntegrationTests.cpp
    regression/GoldenTests.cpp
)
target_include_directories(osp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(osp_tests PRIVATE osp_research Catch2::Catch2WithMain)
target_compile_definitions(osp_tests PRIVATE OSP_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
osp_set_warnings(osp_tests)

# One CTest entry per suite (Catch2 tags) keeps output readable.
add_test(NAME unit COMMAND osp_tests "[unit]")
add_test(NAME integration COMMAND osp_tests "[integration]")
add_test(NAME regression COMMAND osp_tests "[regression]")

# Performance smoke test: must run and stay comfortably real-time even on CI machines.
# The full benchmark is `research-renderer --benchmark` (see docs/testing.md).
add_test(NAME performance-smoke
    COMMAND research-renderer --benchmark --seconds 3 --voices 24 --output-json ${CMAKE_BINARY_DIR}/benchmark-smoke.json)
set_tests_properties(performance-smoke PROPERTIES LABELS performance)
