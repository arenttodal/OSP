# ANDOR Memo Lab (docs/research/memo-lab): a standalone development app that turns a
# recording (iCloud Voice Memos' local copies, or any approved folder) into a playable ANDOR
# instrument. It hosts ANDOR OSP's own processor (the plugin's shared code, as the plugin's
# tests do) and adds no DSP: ONE SHOT, MAP NOTES, playback and the saved .ospinstrument are
# OSP's own.

set(MEMO_LAB_CORE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/MemoDecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/MemoSources.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/MemoSession.cpp)
if(APPLE)
    list(APPEND MEMO_LAB_CORE ${CMAKE_CURRENT_SOURCE_DIR}/Source/MemoDecoderMac.mm)
endif()

# The plugin's shared code brings JUCE and OSP; its definitions and includes come with it.
function(memo_lab_use_osp target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/apps/plugin/Source
        $<TARGET_PROPERTY:OSP_Plugin,INCLUDE_DIRECTORIES>)
    target_compile_definitions(${target} PRIVATE $<TARGET_PROPERTY:OSP_Plugin,COMPILE_DEFINITIONS>)
    target_link_libraries(${target} PRIVATE OSP_Plugin osp_dsp osp_library)
    if(APPLE)
        target_link_libraries(${target} PRIVATE "-framework AVFoundation" "-framework CoreMedia")
    endif()
endfunction()

juce_add_gui_app(ANDOR_Memo_Lab
    PRODUCT_NAME "ANDOR Memo Lab"
    COMPANY_NAME "ANDOR"
    BUNDLE_ID "com.andor.memolab"
    VERSION "0.1.0"
    MICROPHONE_PERMISSION_ENABLED FALSE)
target_sources(ANDOR_Memo_Lab PRIVATE
    Source/Main.cpp
    Source/MemoLabWindow.cpp
    ${MEMO_LAB_CORE})
memo_lab_use_osp(ANDOR_Memo_Lab)
target_link_libraries(ANDOR_Memo_Lab PRIVATE juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

if(OSP_BUILD_TESTS)
    add_executable(memo_lab_tests
        ${PROJECT_SOURCE_DIR}/tests/memo-lab/MemoLabTests.cpp
        ${PROJECT_SOURCE_DIR}/tests/support/AllocationCounter.cpp
        Source/MemoLabWindow.cpp
        ${MEMO_LAB_CORE})
    memo_lab_use_osp(memo_lab_tests)
    target_include_directories(memo_lab_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
    target_link_libraries(memo_lab_tests PRIVATE Catch2::Catch2)
    add_test(NAME memo_lab COMMAND memo_lab_tests)
endif()
