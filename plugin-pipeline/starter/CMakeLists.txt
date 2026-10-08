cmake_minimum_required(VERSION 3.22)

# Before project(): the oldest macOS the plug-in runs on (11.0: the first with Apple Silicon).
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(@TARGET@ VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(MSVC)
    add_compile_options(/utf-8 /bigobj)
endif()

# JUCE is downloaded at a fixed version, so a fresh machine (or the CI) needs nothing
# installed. Offline: cmake -B build -DJUCE_DIR=/path/to/JUCE
set(JUCE_TAG "8.0.9" CACHE STRING "JUCE git tag")
set(JUCE_DIR "" CACHE PATH "Local JUCE checkout (skips the download)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${JUCE_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

# The plug-in's identity. Keep the codes once released: hosts recognise plug-ins (and
# their saved sessions) by them. PLUGIN_MANUFACTURER_CODE is yours, the same for all your
# plug-ins (4 characters, at least one capital); PLUGIN_CODE is unique per plug-in.
juce_add_plugin(@TARGET@
    PRODUCT_NAME "@PRODUCT@"
    COMPANY_NAME "@COMPANY@"
    BUNDLE_ID "@BUNDLE_ID@"
    PLUGIN_MANUFACTURER_CODE @MANUFACTURER_CODE@
    PLUGIN_CODE @PLUGIN_CODE@
    FORMATS AU VST3 Standalone
    IS_SYNTH @IS_SYNTH@
    NEEDS_MIDI_INPUT @IS_SYNTH@
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES @VST3_CATEGORY@
    AU_MAIN_TYPE @AU_MAIN_TYPE@)

target_sources(@TARGET@ PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_include_directories(@TARGET@ PRIVATE Source)

target_compile_definitions(@TARGET@ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(@TARGET@
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Headless tests of the processor (no DAW, no window): run by ctest locally and in CI.
option(BUILD_TESTS "Build the plug-in tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_executable(@TARGET@_Tests tests/PluginTests.cpp)
    target_include_directories(@TARGET@_Tests PRIVATE
        Source
        $<TARGET_PROPERTY:@TARGET@,INCLUDE_DIRECTORIES>)
    target_compile_definitions(@TARGET@_Tests PRIVATE
        $<TARGET_PROPERTY:@TARGET@,COMPILE_DEFINITIONS>)
    target_link_libraries(@TARGET@_Tests PRIVATE @TARGET@)
    add_test(NAME processor COMMAND @TARGET@_Tests)
endif()
