# Fetch blargg's Game Boy test ROMs (mirror: retrio/gb-test-roms) at configure
# time into the git-ignored TestRoms/ directory (spec section 16.1), where they
# survive a deleted build tree. Skipped with a message if the network is
# unavailable, so an offline build still configures.
include(FetchContent)
FetchContent_Declare(gb_test_roms
    GIT_REPOSITORY https://github.com/retrio/gb-test-roms.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    SOURCE_DIR     "${CMAKE_SOURCE_DIR}/TestRoms/gb-test-roms")
FetchContent_GetProperties(gb_test_roms)
if(NOT gb_test_roms_POPULATED)
    message(STATUS "ChipBoy: fetching test ROMs (retrio/gb-test-roms)")
    FetchContent_MakeAvailable(gb_test_roms)
endif()
if(EXISTS "${gb_test_roms_SOURCE_DIR}/dmg_sound/rom_singles")
    set(CHIPBOY_TEST_ROM_DIR "${gb_test_roms_SOURCE_DIR}/dmg_sound/rom_singles" CACHE PATH "" FORCE)
    message(STATUS "ChipBoy: test ROMs at ${CHIPBOY_TEST_ROM_DIR}")
else()
    message(WARNING "ChipBoy: test ROMs not found; blargg tests will be skipped")
endif()

# ---------------------------------------------------------------------------
# SameSuite (MIT, LIJI32). Ships as source; the ROMs are assembled here with
# RGBDS when it is on the PATH, otherwise those tests are skipped. Only the
# tests observable on a DMG are built: the rest read the CGB-only PCM12/PCM34
# registers and become the acceptance gate for the CGB model (spec section 6.5).
# ---------------------------------------------------------------------------
FetchContent_Declare(samesuite
    GIT_REPOSITORY https://github.com/LIJI32/SameSuite.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    SOURCE_DIR     "${CMAKE_SOURCE_DIR}/TestRoms/SameSuite")
FetchContent_GetProperties(samesuite)
if(NOT samesuite_POPULATED)
    message(STATUS "ChipBoy: fetching SameSuite (LIJI32/SameSuite)")
    FetchContent_MakeAvailable(samesuite)
endif()

find_program(RGBASM  rgbasm)
find_program(RGBLINK rgblink)
find_program(RGBFIX  rgbfix)

set(CHIPBOY_SAMESUITE_ROM_DIR "" CACHE PATH "Directory containing the assembled SameSuite ROMs" FORCE)
if(RGBASM AND RGBLINK AND RGBFIX AND EXISTS "${samesuite_SOURCE_DIR}/apu")
    set(_ss_dir "${CMAKE_BINARY_DIR}/samesuite")
    file(MAKE_DIRECTORY "${_ss_dir}")
    set(_ss_tests
        apu/div_write_trigger
        apu/div_write_trigger_10
        apu/channel_3/channel_3_wave_ram_dac_on_rw
        apu/channel_3/channel_3_wave_ram_locked_write)
    file(GLOB _ss_includes "${samesuite_SOURCE_DIR}/include/*")
    set(_ss_roms "")
    foreach(_t IN LISTS _ss_tests)
        get_filename_component(_name "${_t}" NAME)
        add_custom_command(
            OUTPUT  "${_ss_dir}/${_name}.gb" "${_ss_dir}/${_name}.sym"
            COMMAND "${RGBASM}"  -I "${samesuite_SOURCE_DIR}/include/" -o "${_ss_dir}/${_name}.o" "${samesuite_SOURCE_DIR}/${_t}.asm"
            COMMAND "${RGBLINK}" -o "${_ss_dir}/${_name}.gb" -n "${_ss_dir}/${_name}.sym" "${_ss_dir}/${_name}.o"
            COMMAND "${RGBFIX}"  -jv "${_ss_dir}/${_name}.gb"
            DEPENDS "${samesuite_SOURCE_DIR}/${_t}.asm" ${_ss_includes}
            COMMENT "Assembling SameSuite ${_name}"
            VERBATIM)
        list(APPEND _ss_roms "${_ss_dir}/${_name}.gb")
    endforeach()
    add_custom_target(chipboy_samesuite_roms ALL DEPENDS ${_ss_roms})
    set(CHIPBOY_SAMESUITE_ROM_DIR "${_ss_dir}" CACHE PATH "" FORCE)
    message(STATUS "ChipBoy: SameSuite ROMs will be assembled into ${_ss_dir}")
else()
    message(STATUS "ChipBoy: RGBDS (rgbasm/rgblink/rgbfix) not found; SameSuite tests will be skipped")
endif()
