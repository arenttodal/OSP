# ANDOR Phrase: performance-to-instrument sampler (docs/andor-lab/phrase.md). Its own
# identity (name, codes, bundle id, state tag, preset folder) so it installs beside OSP and
# ANDOR Drums and never opens another product's sessions. No copy into the system plug-in
# folders after building (development builds never overwrite an installed product).
include(${PROJECT_SOURCE_DIR}/apps/andor-common/AndorPlugin.cmake)

juce_add_plugin(ANDOR_Phrase
    PRODUCT_NAME "ANDOR Phrase"
    VERSION 0.1.0
    COMPANY_NAME "OSP"
    BUNDLE_ID "com.osp.andorphrase"
    PLUGIN_MANUFACTURER_CODE Ospx
    PLUGIN_CODE Aphr
    FORMATS ${ANDOR_LAB_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED FALSE)

juce_add_binary_data(ANDOR_PhraseData HEADER_NAME BinaryData.h NAMESPACE BinaryData SOURCES ${ANDOR_LAB_RESOURCES})

target_sources(ANDOR_Phrase PRIVATE
    Source/PhraseProcessor.cpp
    Source/PhraseEditor.cpp)
andor_lab_configure(ANDOR_Phrase ANDOR_PhraseData)

andor_lab_plugin_tests(andor_phrase_plugin_tests ANDOR_Phrase ${PROJECT_SOURCE_DIR}/tests/andor/plugin/PhrasePluginTests.cpp)
if(OSP_BUILD_TESTS)
    add_test(NAME andor-phrase-plugin COMMAND andor_phrase_plugin_tests)
endif()
