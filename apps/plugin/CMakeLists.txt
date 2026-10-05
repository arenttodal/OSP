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
    MICROPHONE_PERMISSION_ENABLED FALSE)

# Embedded typeface (Inter, SIL Open Font License: Resources/fonts/Inter-OFL.txt), with
# tabular figures as its default digits (values line up as they change).
juce_add_binary_data(OSP_PluginData
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        Resources/fonts/Inter-Regular.ttf
        Resources/fonts/Inter-Medium.ttf
        Resources/fonts/Inter-SemiBold.ttf)

target_sources(OSP_Plugin PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/OspLookAndFeel.cpp
    Source/ShapingPopups.cpp
    Source/MacroPopups.cpp
    Source/EngineCard.cpp
    Source/MainSections.cpp
    Source/InstrumentLoader.cpp
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
        OSP_PluginData
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Headless tests of the plugin's processor (load, play, state recall) without a DAW.
if(OSP_BUILD_TESTS)
    add_executable(osp_plugin_tests ${PROJECT_SOURCE_DIR}/tests/plugin/PluginProcessorTests.cpp)
    target_include_directories(osp_plugin_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        Source
        $<TARGET_PROPERTY:OSP_Plugin,INCLUDE_DIRECTORIES>)
    target_compile_definitions(osp_plugin_tests PRIVATE
        $<TARGET_PROPERTY:OSP_Plugin,COMPILE_DEFINITIONS>)
    target_link_libraries(osp_plugin_tests PRIVATE OSP_Plugin osp_dsp Catch2::Catch2)
    add_test(NAME plugin COMMAND osp_plugin_tests)
endif()
