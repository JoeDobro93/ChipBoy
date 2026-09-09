# SameBoy's emulator core (LIJI32, MIT), fetched at configure time and built
# as a static library for the parity harness only. It is never linked into
# chipboy_core or either plugin -- spec rule L1 stands -- and it is fetched
# only when CHIPBOY_LSDJREF is ON, so the default build never sees it.
#
# SameBoy's core is GNU C: statement expressions and __typeof__. GCC and Clang
# take it; MSVC does not, which is why the harness is a Linux/macOS tool.
include(FetchContent)

FetchContent_Declare(sameboy
    GIT_REPOSITORY https://github.com/LIJI32/SameBoy.git
    GIT_TAG        213a12ce93d66b105a113debd9396306066a7cfc
    GIT_SHALLOW    FALSE
    SOURCE_DIR     "${CMAKE_SOURCE_DIR}/TestRoms/SameBoy")
FetchContent_GetProperties(sameboy)
if(NOT sameboy_POPULATED)
    message(STATUS "ChipBoy: fetching SameBoy (LIJI32/SameBoy) for the LSDj parity harness")
    FetchContent_MakeAvailable(sameboy)
endif()

file(GLOB LSDJREF_SAMEBOY_SOURCES CONFIGURE_DEPENDS "${sameboy_SOURCE_DIR}/Core/*.c")
if(NOT LSDJREF_SAMEBOY_SOURCES)
    message(FATAL_ERROR "ChipBoy: SameBoy sources not found under ${sameboy_SOURCE_DIR}/Core")
endif()

add_library(lsdjref_sameboy STATIC ${LSDJREF_SAMEBOY_SOURCES})
# SYSTEM: SameBoy's headers are GNU C and warn under -Wpedantic from C++,
# which is the third party's business and not this build's.
target_include_directories(lsdjref_sameboy SYSTEM PUBLIC "${sameboy_SOURCE_DIR}")
# GB_INTERNAL is how SameBoy builds its own core -- its sources use keywords
# the header only defines then. The whole core is built: its DISABLE_* options
# each want their own file left out, and dropping any of them buys nothing
# here. The debugger stays in because its cycle counter is the trace's clock.
target_compile_definitions(lsdjref_sameboy PRIVATE
    _GNU_SOURCE
    GB_INTERNAL
    GB_VERSION="chipboy-lsdjref"
    GB_COPYRIGHT_YEAR="2026")
set_target_properties(lsdjref_sameboy PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
# Third-party code: warnings are its author's business, not this build's.
target_compile_options(lsdjref_sameboy PRIVATE -w)

# ---------------------------------------------------------------------------
# Boot ROMs. SameBoy ships its own, under the MIT licence with the rest of the
# project, as RGBDS assembly; they are assembled here when RGBDS is on the
# PATH. Without them the trace tool has nothing to boot, and its tests skip.
# CHIPBOY_LSDJREF_BOOTROM_DIR names a directory of prebuilt dmg_boot.bin and
# cgb_boot.bin instead.
# ---------------------------------------------------------------------------
set(CHIPBOY_LSDJREF_BOOTROM_DIR "" CACHE PATH "Directory holding prebuilt dmg_boot.bin / cgb_boot.bin")

if(CHIPBOY_LSDJREF_BOOTROM_DIR AND EXISTS "${CHIPBOY_LSDJREF_BOOTROM_DIR}/dmg_boot.bin")
    set(LSDJREF_BOOTROM_DIR "${CHIPBOY_LSDJREF_BOOTROM_DIR}")
    message(STATUS "ChipBoy: LSDj parity harness uses prebuilt boot ROMs at ${LSDJREF_BOOTROM_DIR}")
else()
    find_program(LSDJREF_RGBASM  rgbasm)
    find_program(LSDJREF_RGBLINK rgblink)
    find_program(LSDJREF_RGBGFX  rgbgfx)
    if(LSDJREF_RGBASM AND LSDJREF_RGBLINK AND LSDJREF_RGBGFX)
        set(_br "${CMAKE_BINARY_DIR}/lsdjref/BootROMs")
        file(MAKE_DIRECTORY "${_br}")

        # SameBoy's logo is compressed by a tiny C program that ships with it.
        add_executable(lsdjref_pb12 "${sameboy_SOURCE_DIR}/BootROMs/pb12.c")
        target_compile_options(lsdjref_pb12 PRIVATE -w)
        set_target_properties(lsdjref_pb12 PROPERTIES
            C_STANDARD 99 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lsdjref")

        add_custom_command(OUTPUT "${_br}/SameBoyLogo.2bpp"
            COMMAND "${LSDJREF_RGBGFX}" -Z -u -c embedded -o "${_br}/SameBoyLogo.2bpp"
                    "${sameboy_SOURCE_DIR}/BootROMs/SameBoyLogo.png"
            DEPENDS "${sameboy_SOURCE_DIR}/BootROMs/SameBoyLogo.png"
            COMMENT "lsdjref: SameBoy logo tiles" VERBATIM)
        add_custom_command(OUTPUT "${_br}/SameBoyLogo.pb12"
            COMMAND lsdjref_pb12 < "${_br}/SameBoyLogo.2bpp" > "${_br}/SameBoyLogo.pb12"
            DEPENDS "${_br}/SameBoyLogo.2bpp" lsdjref_pb12
            COMMENT "lsdjref: SameBoy logo pb12" VERBATIM)

        set(_boot_roms "")
        foreach(_b dmg_boot cgb_boot)
            add_custom_command(OUTPUT "${_br}/${_b}.bin"
                COMMAND "${LSDJREF_RGBASM}" --include "${_br}/" --include "${sameboy_SOURCE_DIR}/BootROMs/"
                        -o "${_br}/${_b}.o" "${sameboy_SOURCE_DIR}/BootROMs/${_b}.asm"
                COMMAND "${LSDJREF_RGBLINK}" -x -o "${_br}/${_b}.bin" "${_br}/${_b}.o"
                DEPENDS "${sameboy_SOURCE_DIR}/BootROMs/${_b}.asm" "${_br}/SameBoyLogo.pb12"
                COMMENT "lsdjref: assembling ${_b}.bin" VERBATIM)
            list(APPEND _boot_roms "${_br}/${_b}.bin")
        endforeach()
        add_custom_target(lsdjref_bootroms ALL DEPENDS ${_boot_roms})
        set(LSDJREF_BOOTROM_DIR "${_br}")
        message(STATUS "ChipBoy: LSDj parity harness assembles SameBoy's boot ROMs into ${_br}")
    else()
        set(LSDJREF_BOOTROM_DIR "")
        message(STATUS "ChipBoy: RGBDS not found; the LSDj parity harness has no boot ROMs and its tests will skip")
    endif()
endif()
