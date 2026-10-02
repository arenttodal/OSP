cmake_minimum_required(VERSION 3.25)

project(OSP
    VERSION 0.1.0
    DESCRIPTION "One-Sample Performance instrument - research harness and engine"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    # Sources are UTF-8 (musical symbols in strings); large translation units (JUCE).
    add_compile_options(/utf-8 /bigobj)
endif()

if(APPLE)
    # Universal binaries can be enabled later with -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64".
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

option(OSP_BUILD_TESTS "Build unit/integration/regression tests" ON)
option(OSP_BUILD_PLUGIN "Build the JUCE plugin (AU/VST3/Standalone). Requires JUCE GUI dependencies." OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(OspDependencies)
include(OspWarnings)

# ---------------------------------------------------------------------------
# osp_dsp: pure C++20 DSP + analysis. No JUCE, no I/O, no global state.
# Everything here must be usable from the audio thread *or* from offline tools.
# ---------------------------------------------------------------------------
add_library(osp_dsp STATIC
    src/core/Fft.cpp
    src/core/PitchMath.cpp
    src/core/Statistics.cpp
    src/audio/envelopes/Adsr.cpp
    src/audio/pitch/SincInterpolator.cpp
    src/audio/pitch/OfflinePitchShifter.cpp
    src/audio/voices/SamplerVoice.cpp
    src/audio/sampler/BaselineSampler.cpp
    src/audio/utility/TestSignals.cpp
    src/analysis/Analyzer.cpp
    src/analysis/AnalysisFrames.cpp
    src/analysis/pitch/YinPitchTracker.cpp
    src/analysis/pitch/PitchAnalyzer.cpp
    src/analysis/loudness/EnvelopeAnalyzer.cpp
    src/analysis/onset/OnsetDetector.cpp
    src/analysis/spectrum/SpectralAnalyzer.cpp
    src/analysis/spectrum/SpectralEnvelope.cpp
    src/analysis/stereo/StereoAnalyzer.cpp
    src/analysis/continuation/ContinuationAnalyzer.cpp
    src/analysis/transient/TransientSeparation.cpp
    src/engine/InstrumentVoice.cpp
    src/engine/InstrumentEngine.cpp
    src/engine/InstrumentBuilder.cpp
    src/engine/PerformanceEngine.cpp
    src/engine/PostProcessor.cpp
    src/engine/CharacterFilter.cpp
    src/engine/MovementBus.cpp
    src/engine/SpaceReverb.cpp
    src/engine/GranularSource.cpp
    src/engine/SampleSetInference.cpp
    src/midi/MidiFixtures.cpp
    src/model/RootChoice.cpp
    src/model/PlaybackPreparation.cpp
)
target_include_directories(osp_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Linked into plugin shared objects (VST3/AU bundles).
set_target_properties(osp_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(osp_dsp PRIVATE signalsmith-stretch)
osp_set_warnings(osp_dsp)

# ---------------------------------------------------------------------------
# JUCE-backed I/O (audio decoding, MIDI files, JSON, hashing) and the research
# library (render sessions, metrics, corpus runner, benchmark).
#
# JUCE modules are header+unity-source "interface" libraries. For headless
# tools we compile them exactly once into osp_juce_headless; plugin targets
# compile their own JUCE with plugin-specific settings and pull in the I/O
# sources through osp_io_sources.
# ---------------------------------------------------------------------------
set(OSP_IO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/AudioFileIO.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/ContentHash.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/AnalysisJson.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/JsonUtil.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/midi/MidiFileIO.cpp
)

add_library(osp_juce_headless STATIC)
target_link_libraries(osp_juce_headless
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags)
target_compile_definitions(osp_juce_headless PUBLIC
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=0
    JUCE_USE_MP3AUDIOFORMAT=0
    JUCE_USE_LAME_AUDIO_FORMAT=0
    JUCE_USE_WINDOWS_MEDIA_FORMAT=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    DONT_SET_USING_JUCE_NAMESPACE=1)
# Re-export the include paths / definitions the JUCE interface targets gave us.
target_include_directories(osp_juce_headless INTERFACE
    $<TARGET_PROPERTY:osp_juce_headless,INCLUDE_DIRECTORIES>)
target_compile_definitions(osp_juce_headless INTERFACE
    $<TARGET_PROPERTY:osp_juce_headless,COMPILE_DEFINITIONS>)

add_library(osp_research STATIC
    ${OSP_IO_SOURCES}
    src/research/RenderConfig.cpp
    src/research/RenderSession.cpp
    src/research/RenderMetrics.cpp
    src/research/Fixtures.cpp
    src/research/CorpusIndex.cpp
    src/research/CorpusRunner.cpp
    src/research/Benchmark.cpp
    src/research/SourceAnalysis.cpp
    src/research/TestSignalSet.cpp
    src/research/Bakeoff.cpp
    src/research/Experiment.cpp
)
target_include_directories(osp_research PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(osp_research PUBLIC osp_dsp osp_juce_headless)
target_compile_definitions(osp_research PUBLIC OSP_VERSION_STRING="${PROJECT_VERSION}")
osp_set_warnings(osp_research)

if(OSP_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(apps/research-renderer)

if(OSP_BUILD_PLUGIN)
    add_subdirectory(apps/plugin)
endif()

if(OSP_BUILD_TESTS)
    add_subdirectory(tests)
endif()
