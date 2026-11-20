# andor_arp_core: the arpeggiator shared by ANDOR OSP, ANDOR ANALOG (through osp_dsp) and ANDOR ARP
# (its Max for Live external and its plugin). Pure C++20: no JUCE, no Max SDK, no Live API, none of
# OSP's sampler, voices, FX or interface. The sources stay where they are (src/engine, src/model):
# this target only owns them, and osp_dsp links it, so nothing is compiled twice into one binary.
#
# Standalone (no JUCE is fetched; Catch2 only for the tests):
#   cmake -S arp-core -B build-arp-core -G Ninja && cmake --build build-arp-core
#   ctest --test-dir build-arp-core --output-on-failure
#
# From another project (ANDOR ARP pins OSP as a submodule):
#   add_subdirectory(<osp>/arp-core andor-arp-core)
#   target_link_libraries(my_target PRIVATE andor::arp_core)
cmake_minimum_required(VERSION 3.25)

get_filename_component(ANDOR_ARP_CORE_OSP_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(ANDOR_ARP_CORE_STANDALONE OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(ANDOR_ARP_CORE_STANDALONE ON)
    project(AndorArpCore VERSION 1.0.0 DESCRIPTION "ANDOR arpeggiator core (pure C++20)" LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
    if(MSVC)
        add_compile_options(/utf-8)
    endif()
endif()

set(_root "${ANDOR_ARP_CORE_OSP_ROOT}/src")
add_library(andor_arp_core STATIC
    ${_root}/engine/Arpeggiator.cpp
    ${_root}/engine/ArpCustomPattern.cpp
    ${_root}/engine/PatternCapture.cpp
    ${_root}/model/ArpPatternModel.cpp
    ${_root}/model/ArpFactoryPatterns.cpp
    ${_root}/model/ArpPatternCodec.cpp
)
add_library(andor::arp_core ALIAS andor_arp_core)
# The headers include each other as "engine/...", "model/...", "core/Prng.h".
target_include_directories(andor_arp_core PUBLIC ${_root})
target_compile_features(andor_arp_core PUBLIC cxx_std_20)
# Linked into plugin bundles and Max externals (shared objects).
set_target_properties(andor_arp_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(COMMAND osp_set_warnings)
    osp_set_warnings(andor_arp_core)
endif()

# What the core may include: itself, core/Prng.h and the standard library. Checked as a test, so
# a dependency on JUCE or on the rest of OSP cannot creep in unnoticed.
set(ANDOR_ARP_CORE_FILES
    engine/Arpeggiator.h engine/Arpeggiator.cpp
    engine/ArpCustomPattern.h engine/ArpCustomPattern.cpp
    engine/HostTiming.h
    engine/PatternCapture.h engine/PatternCapture.cpp
    model/ArpPatternModel.h model/ArpPatternModel.cpp
    model/ArpFactoryPatterns.h model/ArpFactoryPatterns.cpp
    model/ArpPatternCodec.h model/ArpPatternCodec.cpp
    model/ArpCoreVersion.h
    core/Prng.h
    CACHE INTERNAL "andor_arp_core's files (relative to OSP's src)")

option(ANDOR_ARP_CORE_BUILD_TESTS "Build andor_arp_core's standalone tests" ${ANDOR_ARP_CORE_STANDALONE})
if(ANDOR_ARP_CORE_BUILD_TESTS)
    enable_testing()
    if(NOT TARGET Catch2::Catch2WithMain)
        set(ANDOR_ARP_CORE_CATCH2_DIR "" CACHE PATH "Local Catch2 checkout (skips download)")
        if(ANDOR_ARP_CORE_CATCH2_DIR)
            add_subdirectory(${ANDOR_ARP_CORE_CATCH2_DIR} ${CMAKE_BINARY_DIR}/_deps/catch2-build EXCLUDE_FROM_ALL)
        else()
            include(FetchContent)
            FetchContent_Declare(Catch2
                GIT_REPOSITORY https://github.com/catchorg/Catch2.git
                GIT_TAG v3.9.1
                GIT_SHALLOW TRUE)
            FetchContent_MakeAvailable(Catch2)
        endif()
    endif()

    # The JUCE-free subset of OSP's ARP tests (the same sources, in their own binary: never linked
    # beside osp_tests), the 18-style golden among them, plus the core's own tests.
    add_executable(andor_arp_core_tests
        ${ANDOR_ARP_CORE_OSP_ROOT}/tests/unit/ArpStrumTests.cpp
        ${ANDOR_ARP_CORE_OSP_ROOT}/tests/regression/ArpStyleBaselineTests.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/ArpCoreTests.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/ArpPatternCodecTests.cpp
    )
    target_link_libraries(andor_arp_core_tests PRIVATE andor_arp_core Catch2::Catch2WithMain)
    target_compile_definitions(andor_arp_core_tests PRIVATE OSP_SOURCE_DIR="${ANDOR_ARP_CORE_OSP_ROOT}")
    add_test(NAME arp-core-unit COMMAND andor_arp_core_tests "[unit]")
    add_test(NAME arp-core-regression COMMAND andor_arp_core_tests "[regression]")
    add_test(NAME arp-core-includes
        COMMAND ${CMAKE_COMMAND} -DROOT=${_root} "-DFILES=${ANDOR_ARP_CORE_FILES}"
                -P ${CMAKE_CURRENT_LIST_DIR}/CheckIncludes.cmake)
endif()
