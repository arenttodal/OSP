# The ANDOR Instrument Shell: one call makes a complete ANDOR plugin (AU/VST3/Standalone)
# with ANDOR OSP's interface, its own identity and its own engine.
#
#   include(${PROJECT_SOURCE_DIR}/shared/andor-shell/AndorInstrument.cmake)
#   andor_add_instrument(ANDOR_Example
#       PRODUCT_NAME "ANDOR Example"   # host menus, window title
#       SLUG example                   # [a-z][a-z0-9]*: file and state namespaces
#       PLUGIN_CODE Aexm               # 4 characters, at least one capital; unique per plugin
#       BUNDLE_ID com.osp.andorexample # unique per plugin
#       VERSION 0.1.0
#       SOURCES Source/Product.cpp)    # productInfo(), createEngine() and the engine
#
# Every product shares ANDOR OSP's manufacturer code (Ospx) and company, so hosts list them
# together. A product's identifiers must be its own entry in andor-design-system/
# product-registry.json (ANDOR OSP, Drums, Grain and Phrase are there): anything else fails
# at configure time. tools/create_andor_instrument.py writes the entry and the product.

include(${CMAKE_CURRENT_LIST_DIR}/../andor-ui/AndorUi.cmake)
set(ANDOR_SHELL_DIR ${CMAKE_CURRENT_LIST_DIR})

# Every ANDOR plugin's identifiers (andor-design-system/product-registry.json).
set(ANDOR_PRODUCT_REGISTRY ${CMAKE_CURRENT_LIST_DIR}/../../andor-design-system/product-registry.json)

# Fails unless the product's identifiers are its own: its registry entry (by slug) must say
# the same, and no other ANDOR plugin may use them.
function(andor_check_registry target slug name code bundle)
    file(READ ${ANDOR_PRODUCT_REGISTRY} registry)
    string(JSON count LENGTH "${registry}" products)
    set(found FALSE)
    math(EXPR last "${count} - 1")
    foreach(i RANGE ${last})
        string(JSON entrySlug GET "${registry}" products ${i} slug)
        string(JSON entryCode GET "${registry}" products ${i} pluginCode)
        string(JSON entryBundle GET "${registry}" products ${i} bundleId)
        string(JSON entryName GET "${registry}" products ${i} name)
        if(entrySlug STREQUAL slug)
            set(found TRUE)
            if(NOT entryCode STREQUAL code OR NOT entryBundle STREQUAL bundle OR NOT entryName STREQUAL name)
                message(FATAL_ERROR "andor_add_instrument(${target}): its identifiers differ from its entry in ${ANDOR_PRODUCT_REGISTRY}")
            endif()
        elseif(entryCode STREQUAL code OR entryBundle STREQUAL bundle OR entryName STREQUAL name)
            message(FATAL_ERROR "andor_add_instrument(${target}): its name, PLUGIN_CODE or BUNDLE_ID is ${entryName}'s (${ANDOR_PRODUCT_REGISTRY})")
        endif()
    endforeach()
    if(NOT found)
        message(FATAL_ERROR "andor_add_instrument(${target}): '${slug}' is not in ${ANDOR_PRODUCT_REGISTRY} (tools/create_andor_instrument.py adds it)")
    endif()
endfunction()

function(andor_add_instrument target)
    cmake_parse_arguments(A "" "PRODUCT_NAME;SLUG;PLUGIN_CODE;BUNDLE_ID;VERSION" "SOURCES" ${ARGN})
    foreach(required PRODUCT_NAME SLUG PLUGIN_CODE BUNDLE_ID VERSION SOURCES)
        if(NOT A_${required})
            message(FATAL_ERROR "andor_add_instrument(${target}): ${required} is required")
        endif()
    endforeach()
    if(NOT A_SLUG MATCHES "^[a-z][a-z0-9]*$")
        message(FATAL_ERROR "andor_add_instrument(${target}): SLUG '${A_SLUG}' must be lower-case letters and digits")
    endif()
    string(LENGTH "${A_PLUGIN_CODE}" codeLength)
    if(NOT codeLength EQUAL 4 OR NOT A_PLUGIN_CODE MATCHES "^[A-Za-z0-9]+$" OR NOT A_PLUGIN_CODE MATCHES "[A-Z]")
        message(FATAL_ERROR "andor_add_instrument(${target}): PLUGIN_CODE '${A_PLUGIN_CODE}' must be 4 letters or digits with at least one capital")
    endif()
    andor_check_registry(${target} "${A_SLUG}" "${A_PRODUCT_NAME}" "${A_PLUGIN_CODE}" "${A_BUNDLE_ID}")
    # Two products in one build may not share a slug either (a copied folder).
    get_property(usedSlugs GLOBAL PROPERTY ANDOR_USED_SLUGS)
    if(A_SLUG IN_LIST usedSlugs)
        message(FATAL_ERROR "andor_add_instrument(${target}): SLUG ${A_SLUG} is added twice in this build")
    endif()
    set_property(GLOBAL APPEND PROPERTY ANDOR_USED_SLUGS ${A_SLUG})

    if(APPLE)
        set(formats AU VST3 Standalone)
    else()
        set(formats VST3 Standalone)
    endif()

    juce_add_plugin(${target}
        PRODUCT_NAME "${A_PRODUCT_NAME}"
        VERSION ${A_VERSION}
        COMPANY_NAME "OSP"
        BUNDLE_ID "${A_BUNDLE_ID}"
        PLUGIN_MANUFACTURER_CODE Ospx
        PLUGIN_CODE ${A_PLUGIN_CODE}
        FORMATS ${formats}
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Instrument Sampler
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        MICROPHONE_PERMISSION_ENABLED FALSE)

    andor_ui_add_to(${target})
    target_sources(${target} PRIVATE
        ${ANDOR_SHELL_DIR}/Source/ShellProcessor.cpp
        ${ANDOR_SHELL_DIR}/Source/ShellEditor.cpp
        ${ANDOR_SHELL_DIR}/Source/DiagnosticEngine.cpp
        ${A_SOURCES}
        ${OSP_IO_SOURCES})
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src ${ANDOR_SHELL_DIR}/Source ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    # The same JUCE configuration as ANDOR OSP's.
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
            osp_dsp
            osp_library
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_cryptography
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endfunction()

# Headless tests of a product built from the shell (tests/shell): it builds, loads sounds,
# plays MIDI, saves and reloads presets, keeps its identity, and its interface matches ANDOR
# OSP's (the visual regression). Registered as ctest test `test_name`.
function(andor_add_instrument_tests target test_name)
    if(NOT OSP_BUILD_TESTS)
        return()
    endif()
    add_executable(${test_name} ${PROJECT_SOURCE_DIR}/tests/shell/ShellTests.cpp)
    target_include_directories(${test_name} PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        $<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>)
    target_compile_definitions(${test_name} PRIVATE
        $<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>
        OSP_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(${test_name} PRIVATE ${target} osp_dsp osp_library Catch2::Catch2)
    add_test(NAME ${test_name} COMMAND ${test_name})
    # The visual regression: this product's interface against ANDOR OSP's renders.
    if(TARGET osp_plugin_tests)
        add_test(NAME ${test_name}_visual COMMAND ${test_name} "[visual-regression]")
        set_tests_properties(${test_name}_visual PROPERTIES
            ENVIRONMENT "ANDOR_VISUAL_REFERENCE_DIR=${CMAKE_BINARY_DIR}/andor-visual-regression/osp;ANDOR_VISUAL_OUTPUT_DIR=${CMAKE_BINARY_DIR}/andor-visual-regression/${target}"
            FIXTURES_REQUIRED andor_visual_reference)
    endif()
endfunction()
