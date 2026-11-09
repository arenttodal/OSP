# OSP instrument plugin (Phase 1: baseline sampler with a minimal debug UI).
#
# Formats: AU + VST3 + Standalone on macOS, VST3 + Standalone elsewhere.
# The DSP comes from osp_dsp; the JUCE-backed I/O sources are compiled into the
# plugin's shared code with the plugin's own JUCE configuration.

if(APPLE)
    set(OSP_PLUGIN_FORMATS AU VST3 Standalone)
else()
    set(OSP_PLUGIN_FORMATS VST3 Standalone)
endif()

juce_add_plugin(OSP_Plugin
    PRODUCT_NAME "OSP"
    COMPANY_NAME "OSP"
    BUNDLE_ID "com.osp.instrument"
    PLUGIN_MANUFACTURER_CODE Ospx
    PLUGIN_CODE Osp1
    FORMATS ${OSP_PLUGIN_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    # Live capture: the standalone app listens to the chosen audio input (kept in memory for
    # RECORD and GRAB; nothing is written until a take is kept).
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "OSP listens to your audio input so RECORD and GRAB can turn what you play into an instrument. Nothing is saved until you keep a take, and nothing leaves your computer.")

# The interface (components, look, fonts, knob sprites, ceramic skin) is the shared ANDOR
# interface in shared/andor-ui; OSP's own windows (editor, popovers, Library) stay here.
include(${PROJECT_SOURCE_DIR}/shared/andor-ui/AndorUi.cmake)
andor_ui_add_to(OSP_Plugin)

target_sources(OSP_Plugin PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/LibraryService.cpp
    Source/Audition.cpp
    Source/LibraryCapture.cpp
    Source/LibraryPacks.cpp
    Source/LibraryPlaces.cpp
    Source/LibraryPanel.cpp
    ${OSP_IO_SOURCES})

target_include_directories(OSP_Plugin PRIVATE ${PROJECT_SOURCE_DIR}/src Source)

target_compile_definitions(OSP_Plugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=0
    JUCE_USE_MP3AUDIOFORMAT=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    DONT_SET_USING_JUCE_NAMESPACE=1)

target_link_libraries(OSP_Plugin
    PRIVATE
        osp_dsp
        osp_library
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Headless tests of the plugin's processor (load, play, state recall) without a DAW.
if(OSP_BUILD_TESTS)
    add_executable(osp_plugin_tests ${PROJECT_SOURCE_DIR}/tests/plugin/PluginProcessorTests.cpp
                                    ${PROJECT_SOURCE_DIR}/tests/support/AllocationCounter.cpp)
    target_include_directories(osp_plugin_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/tests
        Source
        $<TARGET_PROPERTY:OSP_Plugin,INCLUDE_DIRECTORIES>)
    target_compile_definitions(osp_plugin_tests PRIVATE
        $<TARGET_PROPERTY:OSP_Plugin,COMPILE_DEFINITIONS>
        OSP_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(osp_plugin_tests PRIVATE OSP_Plugin osp_dsp osp_library Catch2::Catch2)
    add_test(NAME plugin COMMAND osp_plugin_tests)
    # The ANDOR visual regression's reference: OSP renders the shared states that every
    # product built from the ANDOR Instrument Shell is compared with (andor_*_tests_visual).
    add_test(NAME osp_visual_reference COMMAND osp_plugin_tests "[visual-reference]")
    set_tests_properties(osp_visual_reference PROPERTIES
        ENVIRONMENT "ANDOR_VISUAL_REFERENCE_DIR=${CMAKE_BINARY_DIR}/andor-visual-regression/osp"
        FIXTURES_SETUP andor_visual_reference)
endif()

# Factory packs: the core sounds and presets built into the installer (docs/library/packs.md,
# "Publishing"). Every *.osppack in OSP_FACTORY_PACKS_DIR is copied into each plugin bundle's
# Contents/Resources/Factory, and the plugin installs it into <Library>/Factory at startup.
# With no pack in the folder (the default, and every build before this step) nothing is
# added and the bundles are exactly as before.
set(OSP_FACTORY_PACKS_DIR "${PROJECT_SOURCE_DIR}/packaging/factory-packs" CACHE PATH "Folder of factory packs (*.osppack) built into the plugin")
file(GLOB OSP_FACTORY_PACKS CONFIGURE_DEPENDS "${OSP_FACTORY_PACKS_DIR}/*.osppack")
if(OSP_FACTORY_PACKS)
    foreach(format IN LISTS OSP_PLUGIN_FORMATS)
        # Bundles only: macOS (all three) and the VST3 elsewhere. A Windows or Linux standalone
        # is a bare executable with no Contents folder to carry them.
        if(NOT APPLE AND NOT format STREQUAL "VST3")
            continue()
        endif()
        set(target OSP_Plugin_${format})
        set(bundle_arg "")
        if(APPLE)
            set(bundle_arg "-DBUNDLE=$<TARGET_BUNDLE_DIR:${target}>")
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} "-DSOURCE=${OSP_FACTORY_PACKS_DIR}" "-DDEST=$<TARGET_FILE_DIR:${target}>/../Resources/Factory" ${bundle_arg}
                    -P "${PROJECT_SOURCE_DIR}/cmake/CopyFactoryPacks.cmake"
            VERBATIM)
    endforeach()
    list(LENGTH OSP_FACTORY_PACKS osp_factory_pack_count)
    message(STATUS "OSP: ${osp_factory_pack_count} factory pack(s) from ${OSP_FACTORY_PACKS_DIR}")
endif()
