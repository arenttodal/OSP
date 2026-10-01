# Third-party dependencies.
#
# By default JUCE and Catch2 are fetched (pinned tags) with FetchContent.
# For offline builds point at local checkouts instead:
#   cmake -B build -DOSP_JUCE_DIR=/path/to/JUCE -DOSP_CATCH2_DIR=/path/to/Catch2

include(FetchContent)

set(OSP_JUCE_TAG "8.0.9" CACHE STRING "JUCE git tag")
set(OSP_CATCH2_TAG "v3.9.1" CACHE STRING "Catch2 git tag")
set(OSP_JUCE_DIR "" CACHE PATH "Local JUCE checkout (skips download)")
set(OSP_CATCH2_DIR "" CACHE PATH "Local Catch2 checkout (skips download)")

# Headless builds only need the JUCE modules; skipping juceaide avoids the
# GUI toolchain dependencies (X11/freetype on Linux) and speeds up configure.
if(NOT OSP_BUILD_PLUGIN)
    set(JUCE_MODULES_ONLY ON CACHE BOOL "" FORCE)
else()
    set(JUCE_MODULES_ONLY OFF CACHE BOOL "" FORCE)
endif()

if(OSP_JUCE_DIR)
    add_subdirectory(${OSP_JUCE_DIR} ${CMAKE_BINARY_DIR}/_deps/juce-build EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${OSP_JUCE_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

if(OSP_BUILD_TESTS)
    if(OSP_CATCH2_DIR)
        add_subdirectory(${OSP_CATCH2_DIR} ${CMAKE_BINARY_DIR}/_deps/catch2-build EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG ${OSP_CATCH2_TAG}
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(Catch2)
    endif()
endif()
