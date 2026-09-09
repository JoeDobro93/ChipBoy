# The LSDj parity harness (docs/COMMANDS_AND_TEMPO.md 31, docs/LSDJ_PARITY.md).
#
# Off by default and reached only through -DCHIPBOY_LSDJREF=ON: it fetches
# SameBoy, needs RGBDS for the boot ROMs, and its tests need an LSDj ROM the
# user owns and the repository never sees.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/SameBoy.cmake")

set(LSDJREF_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(LSDJREF_WORK "${CMAKE_BINARY_DIR}/lsdjref")
file(MAKE_DIRECTORY "${LSDJREF_WORK}")

# --- lsdjref-trace: the ROM, in SameBoy, with a log on the APU registers ---
add_executable(lsdjref_trace
    "${LSDJREF_DIR}/trace/main.cpp"
    "${LSDJREF_DIR}/trace/shim.c")
target_link_libraries(lsdjref_trace PRIVATE lsdjref_sameboy)
target_include_directories(lsdjref_trace SYSTEM PRIVATE "${sameboy_SOURCE_DIR}/Core")
target_compile_features(lsdjref_trace PRIVATE cxx_std_20)
set_source_files_properties("${LSDJREF_DIR}/trace/shim.c" PROPERTIES
    COMPILE_DEFINITIONS "GB_INTERNAL;_GNU_SOURCE"
    COMPILE_OPTIONS "-w")                      # SameBoy's own header, on its own terms
target_compile_options(lsdjref_trace PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>)
set_target_properties(lsdjref_trace PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${LSDJREF_WORK}")
if(LSDJREF_BOOTROM_DIR)
    target_compile_definitions(lsdjref_trace PRIVATE CHIPBOY_LSDJREF_BOOTROM_DIR="${LSDJREF_BOOTROM_DIR}")
    if(TARGET lsdjref_bootroms)
        add_dependencies(lsdjref_trace lsdjref_bootroms)
    endif()
endif()

# --- lsdjref-compare: the same songs through ChipBoy's driver -------------
# chipboy_core only: the Driver is what is being compared and it links nothing.
add_executable(lsdjref_compare
    "${LSDJREF_DIR}/compare/main.cpp"
    "${LSDJREF_DIR}/compare/Spec.cpp")
target_link_libraries(lsdjref_compare PRIVATE chipboy_core)
target_compile_features(lsdjref_compare PRIVATE cxx_std_20)
target_compile_definitions(lsdjref_compare PRIVATE CHIPBOY_LSDJREF_SPEC="${LSDJREF_DIR}/cases.spec")
target_compile_options(lsdjref_compare PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(lsdjref_compare PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${LSDJREF_WORK}")

# --- the run script: base save, saves, traces, report ---------------------
find_package(Python3 COMPONENTS Interpreter QUIET)
configure_file("${LSDJREF_DIR}/run.sh.in" "${LSDJREF_WORK}/lsdjref-run" @ONLY
               FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# --- tests ---------------------------------------------------------------
# Registered whether or not the ROM is there: every step exits 77 when it has
# nothing to work with, and CTest reads that as a skip. So a checkout with no
# ROM still runs `ctest` clean, and one with a ROM measures.
enable_testing()

# The baseline: author its save, play it on the ROM, and check the log is not
# empty and opens with the five registers a plain LSDj note-on writes.
add_test(NAME lsdjref_baseline COMMAND "${LSDJREF_WORK}/lsdjref-run" --check)
set_tests_properties(lsdjref_baseline PROPERTIES
    SKIP_RETURN_CODE 77 DEPENDS "" TIMEOUT 600)

# The comparison. Its report is an artefact, not a verdict: it exits 77 when
# no traces have been captured yet.
add_test(NAME lsdjref_compare
         COMMAND lsdjref_compare --traces "${LSDJREF_WORK}/trace"
                 --out "${LSDJREF_WORK}/LSDJ_PARITY_REPORT.dmg.md"
                 --dump "${LSDJREF_WORK}/trace")
set_tests_properties(lsdjref_compare PROPERTIES
    SKIP_RETURN_CODE 77 DEPENDS lsdjref_baseline TIMEOUT 600)
