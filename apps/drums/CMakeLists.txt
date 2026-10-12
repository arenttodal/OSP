# ANDOR Drums: a second, separately installable plugin built from the same repository
# (docs/drums/architecture.md). Its identity (name, codes, bundle id, state tag) is its own,
# so it installs beside OSP and an OSP project always reopens OSP.
#
# Shared with OSP without moving OSP's files: the sample loader and store, the palette,
# fonts and knobs are compiled again here with this plugin's JUCE configuration (JUCE
# modules are per-target), and OSP's embedded fonts are linked as data.

if(APPLE)
    set(ANDOR_DRUMS_FORMATS AU VST3 Standalone)
else()
    set(ANDOR_DRUMS_FORMATS VST3 Standalone)
endif()

juce_add_plugin(ANDOR_Drums
    PRODUCT_NAME "ANDOR Drums"
    VERSION 0.1.0
    COMPANY_NAME "OSP"
    BUNDLE_ID "com.osp.andordrums"
    PLUGIN_MANUFACTURER_CODE Ospx
    PLUGIN_CODE Adrm
    FORMATS ${ANDOR_DRUMS_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Drum Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED FALSE)

set(OSP_PLUGIN_SOURCE_DIR ${PROJECT_SOURCE_DIR}/apps/plugin/Source)

target_sources(ANDOR_Drums PRIVATE
    Source/DrumsProcessor.cpp
    Source/DrumsEditor.cpp
    Source/PadGrid.cpp
    Source/KeyMapEditor.cpp
    Source/CeramicSkin.cpp
    ${OSP_PLUGIN_SOURCE_DIR}/InstrumentLoader.cpp
    ${OSP_PLUGIN_SOURCE_DIR}/OspLookAndFeel.cpp
    ${OSP_PLUGIN_SOURCE_DIR}/Design.cpp
    ${OSP_PLUGIN_SOURCE_DIR}/SpriteKnobRenderer.cpp
    ${OSP_IO_SOURCES})

target_include_directories(ANDOR_Drums PRIVATE ${PROJECT_SOURCE_DIR}/src Source ${OSP_PLUGIN_SOURCE_DIR})

target_compile_definitions(ANDOR_Drums PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=0
    JUCE_USE_MP3AUDIOFORMAT=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    DONT_SET_USING_JUCE_NAMESPACE=1)

target_link_libraries(ANDOR_Drums
    PRIVATE
        osp_drums
        osp_dsp
        osp_library
        OSP_PluginData
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Headless tests of the Drums processor (load, play, recall, identity) without a DAW.
if(OSP_BUILD_TESTS)
    add_executable(andor_drums_plugin_tests ${PROJECT_SOURCE_DIR}/tests/drums/DrumsPluginTests.cpp)
    target_include_directories(andor_drums_plugin_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        Source
        ${OSP_PLUGIN_SOURCE_DIR}
        $<TARGET_PROPERTY:ANDOR_Drums,INCLUDE_DIRECTORIES>)
    target_compile_definitions(andor_drums_plugin_tests PRIVATE
        $<TARGET_PROPERTY:ANDOR_Drums,COMPILE_DEFINITIONS>
        OSP_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(andor_drums_plugin_tests PRIVATE ANDOR_Drums osp_drums osp_dsp osp_library Catch2::Catch2)
    add_test(NAME drums-plugin COMMAND andor_drums_plugin_tests)
endif()
