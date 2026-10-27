# ANDOR Grain: per-key polyphonic granular hybrid (docs/andor-lab/grain.md). Its own identity
# (name, codes, bundle id, state tag, preset folder); no copy into the system plug-in folders.
include(${PROJECT_SOURCE_DIR}/apps/andor-common/AndorPlugin.cmake)

juce_add_plugin(ANDOR_Grain
    PRODUCT_NAME "ANDOR Grain"
    VERSION 0.1.0
    COMPANY_NAME "OSP"
    BUNDLE_ID "com.osp.andorgrain"
    PLUGIN_MANUFACTURER_CODE Ospx
    PLUGIN_CODE Agrn
    FORMATS ${ANDOR_LAB_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED FALSE)

juce_add_binary_data(ANDOR_GrainData HEADER_NAME BinaryData.h NAMESPACE BinaryData SOURCES ${ANDOR_LAB_RESOURCES})

target_sources(ANDOR_Grain PRIVATE
    Source/GrainProcessor.cpp
    Source/GrainEditor.cpp)
andor_lab_configure(ANDOR_Grain ANDOR_GrainData)

andor_lab_plugin_tests(andor_grain_plugin_tests ANDOR_Grain ${PROJECT_SOURCE_DIR}/tests/andor/plugin/GrainPluginTests.cpp)
if(OSP_BUILD_TESTS)
    add_test(NAME andor-grain-plugin COMMAND andor_grain_plugin_tests)
endif()
