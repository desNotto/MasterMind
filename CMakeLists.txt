cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
# Product identity. The product name lives in exactly one variable so it can
# be changed later (spec section 12).
# ---------------------------------------------------------------------------
set(MM_PRODUCT_NAME "MasterMind" CACHE STRING "Product name used for the plugin, files and folders")
set(MM_COMPANY_NAME "desNotto")
set(MM_MILESTONE 0)
set(MM_BUILD_NUMBER 0 CACHE STRING "Build number (GitHub Actions run number)")
set(MM_GIT_HASH "" CACHE STRING "Short git commit hash (detected automatically when empty)")
set(MM_VERSION "0.${MM_MILESTONE}.${MM_BUILD_NUMBER}")

project(${MM_PRODUCT_NAME} VERSION ${MM_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static C++ runtime: no Visual C++ Redistributable needed (spec section 3).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(MM_BUILD_TESTS "Build the unit tests" ON)

if(MM_GIT_HASH STREQUAL "")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD
                        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                        OUTPUT_VARIABLE MM_GIT_HASH
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)
    endif()
    if(MM_GIT_HASH STREQUAL "")
        set(MM_GIT_HASH "unknown")
    endif()
endif()

message(STATUS "${MM_PRODUCT_NAME} version ${MM_VERSION} (${MM_GIT_HASH})")

set(MM_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
configure_file(${CMAKE_SOURCE_DIR}/cmake/mm_build_info.h.in ${MM_GENERATED_DIR}/mm_build_info.h @ONLY)
file(WRITE ${CMAKE_BINARY_DIR}/version.txt "${MM_VERSION}")

# ---------------------------------------------------------------------------
# Dependencies, pinned to release tags.
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.17-security.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(JUCE)

if(MM_BUILD_TESTS)
    FetchContent_Declare(Catch2
        URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.1.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(Catch2)
endif()

# ---------------------------------------------------------------------------
# Warnings: /W4 /WX for our own sources only. JUCE and Catch2 headers are
# included with angle brackets and treated as external (no warnings).
# ---------------------------------------------------------------------------
if(MSVC)
    set(MM_STRICT_OPTIONS /W4 /WX /permissive- /external:anglebrackets /external:W0 /utf-8)
else()
    set(MM_STRICT_OPTIONS -Wall -Wextra -Werror)
endif()

function(mm_strict_sources)
    set_source_files_properties(${ARGN} DIRECTORY ${CMAKE_SOURCE_DIR}
        PROPERTIES COMPILE_OPTIONS "${MM_STRICT_OPTIONS}")
endfunction()

set(MM_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# ---------------------------------------------------------------------------
# mastermind_core: GUI-free DSP, analysis and state code. It is a CMake
# INTERFACE (source) library compiled into each consumer, because JUCE
# modules are source modules (see docs/DECISIONS.md, D-004).
# ---------------------------------------------------------------------------
file(GLOB MM_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/Source/dsp/*.cpp
    ${CMAKE_SOURCE_DIR}/Source/analysis/*.cpp
    ${CMAKE_SOURCE_DIR}/Source/state/*.cpp
    ${CMAKE_SOURCE_DIR}/Source/monitoring/*.cpp
    ${CMAKE_SOURCE_DIR}/Source/reference/*.cpp
    ${CMAKE_SOURCE_DIR}/Source/presets/*.cpp
    ${CMAKE_SOURCE_DIR}/Source/album/*.cpp)
mm_strict_sources(${MM_CORE_SOURCES})

add_library(mastermind_core INTERFACE)
target_sources(mastermind_core INTERFACE ${MM_CORE_SOURCES})
target_include_directories(mastermind_core INTERFACE ${CMAKE_SOURCE_DIR}/Source ${MM_GENERATED_DIR})
target_link_libraries(mastermind_core INTERFACE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_dsp)

# ---------------------------------------------------------------------------
# The plugin.
# ---------------------------------------------------------------------------
juce_add_plugin(${MM_PRODUCT_NAME}
    PRODUCT_NAME "${MM_PRODUCT_NAME}"
    COMPANY_NAME "${MM_COMPANY_NAME}"
    BUNDLE_ID com.desnotto.mastermind
    DESCRIPTION "Master-bus suite for modern metal"
    PLUGIN_MANUFACTURER_CODE Dnto
    PLUGIN_CODE Mmnd
    FORMATS VST3
    VST3_CATEGORIES Fx Mastering
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

file(GLOB MM_UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Source/ui/*.cpp)
set(MM_PLUGIN_SOURCES
    ${CMAKE_SOURCE_DIR}/Source/PluginProcessor.cpp
    ${MM_UI_SOURCES})
mm_strict_sources(${MM_PLUGIN_SOURCES})

target_sources(${MM_PRODUCT_NAME} PRIVATE ${MM_PLUGIN_SOURCES})
target_compile_definitions(${MM_PRODUCT_NAME} PUBLIC
    ${MM_COMMON_DEFINITIONS}
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(${MM_PRODUCT_NAME}
    PRIVATE
        mastermind_core
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Flat single-file VST3 (spec section 3): copy the inner binary of the bundle.
set(MM_DIST_DIR ${CMAKE_BINARY_DIR}/dist)
add_custom_command(TARGET ${MM_PRODUCT_NAME}_VST3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MM_DIST_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${MM_PRODUCT_NAME}_VST3> ${MM_DIST_DIR}/${MM_PRODUCT_NAME}.vst3
    COMMENT "Creating flat single-file ${MM_PRODUCT_NAME}.vst3")

# ---------------------------------------------------------------------------
# Tests.
# ---------------------------------------------------------------------------
if(MM_BUILD_TESTS)
    enable_testing()

    # Unit tests of the GUI-free core (Catch2).
    juce_add_console_app(MasterMindTests PRODUCT_NAME "MasterMindTests")
    file(GLOB MM_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*Tests.cpp)
    mm_strict_sources(${MM_TEST_SOURCES})
    target_sources(MasterMindTests PRIVATE ${MM_TEST_SOURCES})
    target_compile_definitions(MasterMindTests PRIVATE ${MM_COMMON_DEFINITIONS})
    target_link_libraries(MasterMindTests PRIVATE
        mastermind_core
        Catch2::Catch2WithMain
        juce::juce_recommended_config_flags)
    add_test(NAME core_unit_tests COMMAND MasterMindTests)

    # Loads the shipped flat .vst3 file like a host and checks that it is a
    # bit-exact stereo passthrough at several sample rates and block sizes.
    juce_add_console_app(MasterMindBinaryTest PRODUCT_NAME "MasterMindBinaryTest")
    set(MM_BINARY_TEST_SOURCES ${CMAKE_SOURCE_DIR}/tests/BinaryNullTest.cpp)
    mm_strict_sources(${MM_BINARY_TEST_SOURCES})
    target_sources(MasterMindBinaryTest PRIVATE ${MM_BINARY_TEST_SOURCES})
    target_compile_definitions(MasterMindBinaryTest PRIVATE
        ${MM_COMMON_DEFINITIONS}
        JUCE_PLUGINHOST_VST3=1)
    target_link_libraries(MasterMindBinaryTest PRIVATE
        juce::juce_audio_processors
        juce::juce_recommended_config_flags)
    add_dependencies(MasterMindBinaryTest ${MM_PRODUCT_NAME}_VST3)
    add_test(NAME binary_null_test
        COMMAND MasterMindBinaryTest ${MM_DIST_DIR}/${MM_PRODUCT_NAME}.vst3)
endif()
