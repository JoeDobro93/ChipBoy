cmake_minimum_required(VERSION 3.24)

project(ChipBoy VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CHIPBOY_BUILD_TESTS     "Build the unit tests and the test-ROM harness" ON)
option(CHIPBOY_BUILD_PLUGIN    "Build the JUCE plugins (JUCE is fetched; off until M3)" OFF)
option(CHIPBOY_FETCH_TEST_ROMS "Fetch blargg's test ROMs at configure time" ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# core -- the chip. Plain C++20 and the standard library. Links NOTHING.
#
# This is spec rule L1 and it is structural, not a convention: the plugin
# shell depends on this target, never the other way round, and this target
# has no dependencies to leak. It can be licensed on its own (LICENSING.md,
# path C) precisely because it is built this way.
# ---------------------------------------------------------------------------
add_library(chipboy_core STATIC
    Source/core/Apu/Apu.cpp)
target_include_directories(chipboy_core PUBLIC Source)
target_compile_features(chipboy_core PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(chipboy_core PRIVATE /W4)
else()
    target_compile_options(chipboy_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# tests and harness
# ---------------------------------------------------------------------------
if(CHIPBOY_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)

    # The test-ROM harness: a minimal SM83 CPU, memory map and timer, existing
    # only so real Game Boy test ROMs can be run against the APU. Ships in nothing.
    add_library(chipboy_harness STATIC
        Source/tools/harness/Sm83.cpp
        Source/tools/harness/Bus.cpp
        Source/tools/harness/Machine.cpp)
    target_link_libraries(chipboy_harness PUBLIC chipboy_core)

    set(CHIPBOY_TEST_ROM_DIR "" CACHE PATH "Directory containing blargg's dmg_sound ROMs")
    set(CHIPBOY_SAMESUITE_ROM_DIR "" CACHE PATH "Directory containing assembled SameSuite ROMs")
    if(CHIPBOY_FETCH_TEST_ROMS)
        include(cmake/TestRoms.cmake)
    endif()

    add_executable(chipboy_tests
        Tests/ApuTests.cpp
        Tests/BlarggTests.cpp
        Tests/SameSuiteTests.cpp)
    target_link_libraries(chipboy_tests PRIVATE chipboy_core chipboy_harness Catch2::Catch2WithMain)
    target_compile_definitions(chipboy_tests PRIVATE
        CHIPBOY_TEST_ROM_DIR="${CHIPBOY_TEST_ROM_DIR}"
        CHIPBOY_SAMESUITE_ROM_DIR="${CHIPBOY_SAMESUITE_ROM_DIR}")
    if(TARGET chipboy_samesuite_roms)
        add_dependencies(chipboy_tests chipboy_samesuite_roms)
    endif()

    # A tiny CLI that runs one ROM and prints its result and serial output --
    # what you reach for when a blargg test fails and you want to know which line.
    add_executable(chipboy_runrom Source/tools/harness/RunRomMain.cpp)
    target_link_libraries(chipboy_runrom PRIVATE chipboy_harness)

    include(CTest)
    include(Catch)
    catch_discover_tests(chipboy_tests)
endif()

if(CHIPBOY_BUILD_PLUGIN)
    message(FATAL_ERROR "The plugin targets arrive in M3; the core and harness are M1.")
endif()
