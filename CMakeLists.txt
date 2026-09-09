cmake_minimum_required(VERSION 3.24)

project(ChipBoy VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CHIPBOY_BUILD_TESTS     "Build the unit tests and the test-ROM harness" ON)
option(CHIPBOY_BUILD_PLUGIN    "Build the JUCE plugins (JUCE is fetched; off until M3)" OFF)
option(CHIPBOY_FETCH_TEST_ROMS "Fetch blargg's test ROMs at configure time" ON)
# The LSDj parity harness (docs/COMMANDS_AND_TEMPO.md 31, docs/LSDJ_PARITY.md):
# off by default, so the ordinary build fetches nothing new and builds nothing
# new. With it on, SameBoy is fetched and two console tools are built; their
# tests skip unless an LSDj ROM the user owns is named by CHIPBOY_LSDJ_ROM.
option(CHIPBOY_LSDJREF "Build the LSDj parity harness (fetches SameBoy; tools only)" OFF)

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
    Source/core/Bank/Preset.cpp
    Source/core/Driver/Clock.cpp
    Source/core/Driver/Driver.cpp
    Source/core/Tracker/Player.cpp
    Source/core/Tracker/Song.cpp)
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
        Tests/ClockTests.cpp
        Tests/TrackerTests.cpp
        Tests/PresetTests.cpp
        Tests/LinkTests.cpp
        Tests/HardwareTests.cpp)
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

    # Sources both plugins compile. JUCE modules are compiled into each plugin
    # target, so the shared code is simply compiled twice rather than linked
    # from a library.
    file(GLOB CHIPBOY_PLUGIN_SHARED_SOURCES CONFIGURE_DEPENDS
        Source/plugin/shared/*.cpp
        Source/plugin/ui/*.cpp)
    file(GLOB CHIPBOY_MAIN_SOURCES CONFIGURE_DEPENDS
        Source/plugin/main/*.cpp
        Source/plugin/main/panels/*.cpp)
    file(GLOB CHIPBOY_VOICE_SOURCES CONFIGURE_DEPENDS
        Source/plugin/voice/*.cpp)

    # Fonts and other assets: every file in Source/plugin/ui/assets becomes
    # BinaryData (namespace chipboy::assets), e.g. IBMPlexSansRegular_ttf.
    file(GLOB CHIPBOY_ASSET_FILES CONFIGURE_DEPENDS Source/plugin/ui/assets/*)
    if(CHIPBOY_ASSET_FILES)
        juce_add_binary_data(ChipBoyAssets NAMESPACE chipboy::assets SOURCES ${CHIPBOY_ASSET_FILES})
        set_target_properties(ChipBoyAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
        set(CHIPBOY_ASSET_TARGET ChipBoyAssets)
        set(CHIPBOY_HAVE_ASSETS 1)
    else()
        set(CHIPBOY_ASSET_TARGET "")
        set(CHIPBOY_HAVE_ASSETS 0)
    endif()

    set(CHIPBOY_MODAL_LOOPS 0)   # console test tools set this to 1 to pump the message loop
    function(chipboy_configure_plugin target)
        target_include_directories(${target} PRIVATE Source)
        target_compile_definitions(${target} PUBLIC
            CHIPBOY_HAVE_ASSETS=${CHIPBOY_HAVE_ASSETS}
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_REPORT_APP_USAGE=0
            JUCE_DISPLAY_SPLASH_SCREEN=0
            JUCE_MODAL_LOOPS_PERMITTED=${CHIPBOY_MODAL_LOOPS})
        target_link_libraries(${target}
            PRIVATE chipboy_core ${CHIPBOY_ASSET_TARGET} juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_extra
            PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
        # The console tools (juce_add_console_app makes an executable; a plugin's
        # shared code is a library) hold ChipBoyProcessors, and one is about
        # 450 KB. Windows reserves a megabyte of stack for an executable, which
        # a couple of them overrun, so ask for the eight the other two platforms
        # give a main thread anyway.
        get_target_property(chipboy_target_type ${target} TYPE)
        if(chipboy_target_type STREQUAL "EXECUTABLE")
            target_link_options(${target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/STACK:8388608>)
        endif()
    endfunction()

    # --- ChipBoy: the machine -------------------------------------------
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
    target_sources(ChipBoy PRIVATE ${CHIPBOY_PLUGIN_SHARED_SOURCES} ${CHIPBOY_MAIN_SOURCES})
    chipboy_configure_plugin(ChipBoy)

    # --- ChipBoy Voice: one channel of one machine, on its own track ------
    set(CHIPBOY_VOICE_FORMATS VST3)
    if(APPLE)
        list(APPEND CHIPBOY_VOICE_FORMATS AU)
    endif()
    juce_add_plugin(ChipBoyVoice
        PRODUCT_NAME "ChipBoy Voice"
        COMPANY_NAME "ChipBoy"
        BUNDLE_ID com.chipboy.chipboyvoice
        PLUGIN_MANUFACTURER_CODE Chpb
        PLUGIN_CODE Chbv
        FORMATS ${CHIPBOY_VOICE_FORMATS}
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD ${CHIPBOY_COPY_PLUGINS}
        VST3_CATEGORIES Instrument Synth
        AU_MAIN_TYPE kAudioUnitType_MusicDevice)
    target_sources(ChipBoyVoice PRIVATE ${CHIPBOY_PLUGIN_SHARED_SOURCES} ${CHIPBOY_VOICE_SOURCES})
    chipboy_configure_plugin(ChipBoyVoice)

    # --- chipboy_paramdump: prints the host-visible parameter list in index
    # order, for the demo project generator and the documentation ------------
    juce_add_console_app(chipboy_paramdump PRODUCT_NAME "chipboy_paramdump")
    target_sources(chipboy_paramdump PRIVATE
        ${CHIPBOY_PLUGIN_SHARED_SOURCES} ${CHIPBOY_MAIN_SOURCES}
        tools/paramdump/main.cpp)
    target_compile_definitions(chipboy_paramdump PRIVATE
        CHIPBOY_NO_PLUGIN_FILTER=1
        JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Name="ChipBoy" JucePlugin_Manufacturer="ChipBoy" JucePlugin_ManufacturerCode=0x43687062 JucePlugin_PluginCode=0x43686279
        JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0
        JucePlugin_EditorRequiresKeyboardFocus=1 JucePlugin_Version=0.1.0 JucePlugin_VersionCode=0x100 JucePlugin_VersionString="0.1.0"
        JucePlugin_Build_VST3=0 JucePlugin_Build_AU=0 JucePlugin_Build_Standalone=0
        JucePlugin_MaxNumInputChannels=0 JucePlugin_MaxNumOutputChannels=2 JucePlugin_PreferredChannelConfigurations= )
    chipboy_configure_plugin(chipboy_paramdump)

    # --- chipboy_linktest: both plugins in one process, talking through a
    # region file (spec 16.5) -------------------------------------------------
    juce_add_console_app(chipboy_linktest PRODUCT_NAME "chipboy_linktest")
    target_sources(chipboy_linktest PRIVATE
        ${CHIPBOY_PLUGIN_SHARED_SOURCES} ${CHIPBOY_MAIN_SOURCES} ${CHIPBOY_VOICE_SOURCES}
        tools/linktest/main.cpp)
    target_compile_definitions(chipboy_linktest PRIVATE
        CHIPBOY_NO_PLUGIN_FILTER=1
        JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Name="ChipBoy" JucePlugin_Manufacturer="ChipBoy" JucePlugin_ManufacturerCode=0x43687062 JucePlugin_PluginCode=0x43686279
        JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0
        JucePlugin_EditorRequiresKeyboardFocus=1 JucePlugin_Version=0.1.0 JucePlugin_VersionCode=0x100 JucePlugin_VersionString="0.1.0"
        JucePlugin_Build_VST3=0 JucePlugin_Build_AU=0 JucePlugin_Build_Standalone=0
        JucePlugin_MaxNumInputChannels=0 JucePlugin_MaxNumOutputChannels=2 JucePlugin_PreferredChannelConfigurations= )
    set(CHIPBOY_MODAL_LOOPS 1)
    chipboy_configure_plugin(chipboy_linktest)
    set(CHIPBOY_MODAL_LOOPS 0)
    enable_testing()
    add_test(NAME chipboy_linktest COMMAND chipboy_linktest)

    # --- chipboy_recordtest: the demo recorded and played back, register for
    # register (docs/COMMANDS_AND_TEMPO.md section 9.5) ----------------------
    juce_add_console_app(chipboy_recordtest PRODUCT_NAME "chipboy_recordtest")
    target_sources(chipboy_recordtest PRIVATE
        ${CHIPBOY_PLUGIN_SHARED_SOURCES} ${CHIPBOY_MAIN_SOURCES}
        tools/recordtest/main.cpp)
    target_compile_definitions(chipboy_recordtest PRIVATE
        CHIPBOY_NO_PLUGIN_FILTER=1
        JUCE_STANDALONE_APPLICATION=1
        CHIPBOY_DEMO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Demo"
        JucePlugin_Name="ChipBoy" JucePlugin_Manufacturer="ChipBoy" JucePlugin_ManufacturerCode=0x43687062 JucePlugin_PluginCode=0x43686279
        JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0
        JucePlugin_EditorRequiresKeyboardFocus=1 JucePlugin_Version=0.1.0 JucePlugin_VersionCode=0x100 JucePlugin_VersionString="0.1.0"
        JucePlugin_Build_VST3=0 JucePlugin_Build_AU=0 JucePlugin_Build_Standalone=0
        JucePlugin_MaxNumInputChannels=0 JucePlugin_MaxNumOutputChannels=2 JucePlugin_PreferredChannelConfigurations= )
    set(CHIPBOY_MODAL_LOOPS 1)
    chipboy_configure_plugin(chipboy_recordtest)
    set(CHIPBOY_MODAL_LOOPS 0)
    add_test(NAME chipboy_recordtest COMMAND chipboy_recordtest --out "${CMAKE_CURRENT_BINARY_DIR}/recordtest")

    # The demo song file is the recorded pass, written through the song writer
    # (docs/COMMANDS_AND_TEMPO.md section 16): recording the demo again must
    # produce it byte for byte. It carries no timestamp, so it can.
    add_test(NAME demo_song_matches COMMAND chipboy_recordtest
             --out "${CMAKE_CURRENT_BINARY_DIR}/recordtest"
             --check-song "${CMAKE_CURRENT_SOURCE_DIR}/Demo/ChipBoy Demo.cbsong")

    # The hybrid demo project's plugin state (docs/COMMANDS_AND_TEMPO.md
    # section 20): the demo song in a tab with its own bank, four Hybrid
    # channels and the two static parameters it needs. It carries no
    # timestamp and no path, so it is the same bytes on any machine.
    add_test(NAME demo_state_matches COMMAND chipboy_recordtest
             --out "${CMAKE_CURRENT_BINARY_DIR}/recordtest"
             --check-state "${CMAKE_CURRENT_SOURCE_DIR}/Demo/chipboy_demo_hybrid.state")

    # The demo songs (docs/COMMANDS_AND_TEMPO.md section 24): each one opens in
    # a tab and plays eight bars on the plugin's own transport, and what comes
    # out is measured -- no NaN, not silent, and no channel that never moves.
    # One test per file, so a failure names the song.
    file(GLOB CHIPBOY_DEMO_SONGS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Demo/songs/*.cbsong")
    foreach(song ${CHIPBOY_DEMO_SONGS})
        get_filename_component(song_name "${song}" NAME_WE)
        add_test(NAME demo_songs_load.${song_name} COMMAND chipboy_recordtest
                 --out "${CMAKE_CURRENT_BINARY_DIR}/recordtest" --play-song "${song}" 8)
    endforeach()

    # --- chipboy_fuzz: random songs through the whole plugin, to find the
    # crash nobody would have written by hand (section 28) -------------------
    juce_add_console_app(chipboy_fuzz PRODUCT_NAME "chipboy_fuzz")
    target_sources(chipboy_fuzz PRIVATE
        ${CHIPBOY_PLUGIN_SHARED_SOURCES} ${CHIPBOY_MAIN_SOURCES}
        tools/fuzz/main.cpp)
    target_compile_definitions(chipboy_fuzz PRIVATE
        CHIPBOY_NO_PLUGIN_FILTER=1
        JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Name="ChipBoy" JucePlugin_Manufacturer="ChipBoy" JucePlugin_ManufacturerCode=0x43687062 JucePlugin_PluginCode=0x43686279
        JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0
        JucePlugin_EditorRequiresKeyboardFocus=1 JucePlugin_Version=0.1.0 JucePlugin_VersionCode=0x100 JucePlugin_VersionString="0.1.0"
        JucePlugin_Build_VST3=0 JucePlugin_Build_AU=0 JucePlugin_Build_Standalone=0
        JucePlugin_MaxNumInputChannels=0 JucePlugin_MaxNumOutputChannels=2 JucePlugin_PreferredChannelConfigurations= )
    set(CHIPBOY_MODAL_LOOPS 1)
    chipboy_configure_plugin(chipboy_fuzz)
    set(CHIPBOY_MODAL_LOOPS 0)
    # A fixed seed set, so the test is the same run everywhere; a failure
    # prints the seed to replay.
    add_test(NAME chipboy_fuzz COMMAND chipboy_fuzz --seeds 8 --rows 64
             --out "${CMAKE_CURRENT_BINARY_DIR}/fuzz")

    # --- chipboy_uishot: PNG snapshots of the windows (needs a display) ----
    juce_add_console_app(chipboy_uishot PRODUCT_NAME "chipboy_uishot")
    target_sources(chipboy_uishot PRIVATE
        ${CHIPBOY_PLUGIN_SHARED_SOURCES} ${CHIPBOY_MAIN_SOURCES} ${CHIPBOY_VOICE_SOURCES}
        tools/uishot/main.cpp)
    target_compile_definitions(chipboy_uishot PRIVATE
        CHIPBOY_NO_PLUGIN_FILTER=1
        JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Name="ChipBoy" JucePlugin_Manufacturer="ChipBoy" JucePlugin_ManufacturerCode=0x43687062 JucePlugin_PluginCode=0x43686279
        JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0
        JucePlugin_EditorRequiresKeyboardFocus=1 JucePlugin_Version=0.1.0 JucePlugin_VersionCode=0x100 JucePlugin_VersionString="0.1.0"
        JucePlugin_Build_VST3=0 JucePlugin_Build_AU=0 JucePlugin_Build_Standalone=0
        JucePlugin_MaxNumInputChannels=0 JucePlugin_MaxNumOutputChannels=2 JucePlugin_PreferredChannelConfigurations= )
    set(CHIPBOY_MODAL_LOOPS 1)
    chipboy_configure_plugin(chipboy_uishot)
    set(CHIPBOY_MODAL_LOOPS 0)
endif()

# ---------------------------------------------------------------------------
# the LSDj parity harness -- tools only, opt in, and nothing of it links into
# chipboy_core or a plugin (docs/COMMANDS_AND_TEMPO.md 31)
# ---------------------------------------------------------------------------
if(CHIPBOY_LSDJREF)
    if(MSVC)
        message(WARNING "ChipBoy: the LSDj parity harness needs GCC or Clang (SameBoy's core is GNU C); not built")
    else()
        # CTest has to be enabled from the top of the tree for a subdirectory's
        # tests to be listed, whether or not the unit tests are being built.
        enable_testing()
        add_subdirectory(tools/lsdjref)
    endif()
endif()
