# Shared pieces of the ANDOR Lab plug-ins (docs/andor-lab/build-and-validation.md).
#
# Each plug-in is its own juce_add_plugin target with its own identity. They share:
#   - andor_dsp (pure C++: Note Atlas, Phrase analysis/engine, Grain engine)
#   - apps/andor-common (sample store, widgets)
#   - ANDOR/OSP's design system (Design, OspLookAndFeel, rendered knobs), compiled again with
#     the plug-in's own JUCE configuration and its own embedded fonts and knob frames. OSP's
#     12.5 MB ceramic skin pack is not embedded: the shared drawing falls back to its vector
#     ivory look, as ANDOR Drums does.
set(ANDOR_COMMON_DIR ${PROJECT_SOURCE_DIR}/apps/andor-common)
set(OSP_PLUGIN_SOURCE_DIR ${PROJECT_SOURCE_DIR}/apps/plugin/Source)
set(OSP_PLUGIN_RESOURCE_DIR ${PROJECT_SOURCE_DIR}/apps/plugin/Resources)

if(APPLE)
    set(ANDOR_LAB_FORMATS AU VST3 Standalone)
else()
    set(ANDOR_LAB_FORMATS VST3 Standalone)
endif()

set(ANDOR_LAB_RESOURCES
    ${OSP_PLUGIN_RESOURCE_DIR}/fonts/Inter-Light.ttf
    ${OSP_PLUGIN_RESOURCE_DIR}/fonts/Inter-Regular.ttf
    ${OSP_PLUGIN_RESOURCE_DIR}/fonts/Inter-Medium.ttf
    ${OSP_PLUGIN_RESOURCE_DIR}/fonts/Inter-SemiBold.ttf
    ${OSP_PLUGIN_RESOURCE_DIR}/fonts/Inter-Bold.ttf
    ${OSP_PLUGIN_RESOURCE_DIR}/fonts/Inter-ExtraBold.ttf
    ${OSP_PLUGIN_RESOURCE_DIR}/fonts/Outfit-ExtraBold.ttf
    ${OSP_PLUGIN_RESOURCE_DIR}/fonts/Outfit-ExtraLight.ttf
    ${OSP_PLUGIN_RESOURCE_DIR}/knobs/macro-ivory/macro-ivory-176.zip
    ${OSP_PLUGIN_RESOURCE_DIR}/knobs/macro-ivory/macro-ivory-352.zip)

set(ANDOR_LAB_SHARED_SOURCES
    ${ANDOR_COMMON_DIR}/AndorStore.cpp
    ${ANDOR_COMMON_DIR}/AndorUi.cpp
    ${OSP_PLUGIN_SOURCE_DIR}/Design.cpp
    ${OSP_PLUGIN_SOURCE_DIR}/OspLookAndFeel.cpp
    ${OSP_PLUGIN_SOURCE_DIR}/SpriteKnobRenderer.cpp
    ${OSP_PLUGIN_SOURCE_DIR}/CeramicSkin.cpp
    ${PROJECT_SOURCE_DIR}/src/io/AudioFileIO.cpp
    ${PROJECT_SOURCE_DIR}/src/io/ContentHash.cpp
    ${PROJECT_SOURCE_DIR}/src/io/JsonUtil.cpp
    ${ANDOR_IO_SOURCES})

# Settings every ANDOR Lab plug-in target gets.
function(andor_lab_configure target data)
    target_sources(${target} PRIVATE ${ANDOR_LAB_SHARED_SOURCES})
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src ${ANDOR_COMMON_DIR} ${OSP_PLUGIN_SOURCE_DIR}
                               ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_USE_FLAC=1
        JUCE_USE_OGGVORBIS=0
        JUCE_USE_MP3AUDIOFORMAT=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        DONT_SET_USING_JUCE_NAMESPACE=1)
    target_link_libraries(${target}
        PRIVATE
            andor_dsp
            ${data}
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_cryptography
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endfunction()

# A headless test executable for one plug-in's processor and editor.
function(andor_lab_plugin_tests name target source)
    if(NOT OSP_BUILD_TESTS)
        return()
    endif()
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests
        $<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>)
    target_compile_definitions(${name} PRIVATE $<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS> OSP_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(${name} PRIVATE ${target} andor_dsp Catch2::Catch2)
endfunction()
