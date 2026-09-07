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
    Source/core/Apu/Apu.cpp
    Source/core/Render/Kernel.cpp
    Source/core/Render/Renderer.cpp
    Source/core/Render/Reference.cpp
    Source/core/Bank/Bank.cpp
    Source/core/Driver/Driver.cpp
    Source/core/Tracker/Player.cpp)
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
        Source/tools/harness/Machine.cpp
        Source/tools/harness/Script.cpp
        Source/tools/harness/Wav.cpp)
    target_link_libraries(chipboy_harness PUBLIC chipboy_core)

    set(CHIPBOY_TEST_ROM_DIR "" CACHE PATH "Directory containing blargg's dmg_sound ROMs")
    set(CHIPBOY_SAMESUITE_ROM_DIR "" CACHE PATH "Directory containing assembled SameSuite ROMs")
    if(CHIPBOY_FETCH_TEST_ROMS)
        include(cmake/TestRoms.cmake)
    endif()

    add_executable(chipboy_tests
        Tests/ApuTests.cpp
        Tests/BlarggTests.cpp
        Tests/SameSuiteTests.cpp
        Tests/RenderTests.cpp
        Tests/DriverTests.cpp
        Tests/TrackerTests.cpp)
    target_link_libraries(chipboy_tests PRIVATE chipboy_core chipboy_harness Catch2::Catch2WithMain)
    target_compile_definitions(chipboy_tests PRIVATE
        CHIPBOY_TEST_ROM_DIR="${CHIPBOY_TEST_ROM_DIR}"
        CHIPBOY_SAMESUITE_ROM_DIR="${CHIPBOY_SAMESUITE_ROM_DIR}"
        CHIPBOY_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/Tests/golden")
    if(TARGET chipboy_samesuite_roms)
        add_dependencies(chipboy_tests chipboy_samesuite_roms)
    endif()

    # A tiny CLI that runs one ROM and prints its result and serial output --
    # what you reach for when a blargg test fails and you want to know which line.
    add_executable(chipboy_runrom Source/tools/harness/RunRomMain.cpp)
    target_link_libraries(chipboy_runrom PRIVATE chipboy_harness)

    # Renders a built-in tune through the chip and the analog model: the
    # first thing that can be heard, and the quickest check that it sounds
    # like a Game Boy.
    add_executable(chipboy_demo Source/tools/demo/DemoMain.cpp)
    target_link_libraries(chipboy_demo PRIVATE chipboy_harness)

    include(CTest)
    include(Catch)
    catch_discover_tests(chipboy_tests)
endif()

# ---------------------------------------------------------------------------
# plugins -- JUCE 8. Only the plugin shell and the interface live here; the
# chip, the analog stage, the driver, the bank and the tracker are the core
# above and know nothing of JUCE (spec rule L1).
# ---------------------------------------------------------------------------
if(CHIPBOY_BUILD_PLUGIN)
    option(CHIPBOY_COPY_PLUGINS "Copy built plugins into the user's plugin folders after each build" OFF)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)

    set(CHIPBOY_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND CHIPBOY_FORMATS AU)
    endif()

    juce_add_plugin(ChipBoy
        PRODUCT_NAME "ChipBoy"
        COMPANY_NAME "ChipBoy"
        BUNDLE_ID com.chipboy.chipboy
        PLUGIN_MANUFACTURER_CODE Chpb
        PLUGIN_CODE Chby
        FORMATS ${CHIPBOY_FORMATS}
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD ${CHIPBOY_COPY_PLUGINS}
        VST3_CATEGORIES Instrument Synth
        AU_MAIN_TYPE kAudioUnitType_MusicDevice)
    target_sources(ChipBoy PRIVATE
        Source/plugin/shared/Parameters.cpp
        Source/plugin/shared/BankJson.cpp
        Source/plugin/main/ChipBoyProcessor.cpp)
    target_include_directories(ChipBoy PRIVATE Source)
    target_compile_definitions(ChipBoy PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_MODAL_LOOPS_PERMITTED=0)
    target_link_libraries(ChipBoy
        PRIVATE chipboy_core juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
