// ChipBoy -- blargg dmg_sound acceptance tests (spec section 16.1).
//
// Each ROM writes its result to cartridge RAM: $A000 = code (0 = pass),
// $A001..$A003 = DE B0 61 as a signature, $A004.. = its message. The harness
// runs the ROM until that appears. The ROMs are fetched at configure time
// (cmake/TestRoms.cmake) and never committed; without them these tests are
// skipped, not failed.
#include "tools/harness/Machine.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

namespace {

void runBlargg(const char* romName)
{
    const std::string dir = CHIPBOY_TEST_ROM_DIR;
    if (dir.empty()) SKIP("CHIPBOY_TEST_ROM_DIR is not set");
    const auto path = std::filesystem::path(dir) / romName;
    if (!std::filesystem::exists(path)) SKIP("missing test ROM: " << path.string());

    auto rom = chipboy::harness::Machine::loadRom(path.string());
    REQUIRE_FALSE(rom.empty());
    chipboy::harness::Machine m(std::move(rom));
    const auto r = m.run(uint64_t(chipboy::kCpuHz) * 60);

    INFO("text:   " << r.text);
    INFO("serial: " << r.serial);
    INFO("cycles: " << r.cycles);
    REQUIRE(r.finished);
    CHECK(r.code == 0);
}

} // namespace

#define CHIPBOY_BLARGG(name, file) \
    TEST_CASE("blargg dmg_sound " name, "[blargg]") { runBlargg(file); }

CHIPBOY_BLARGG("01 registers",              "01-registers.gb")
CHIPBOY_BLARGG("02 len ctr",                "02-len ctr.gb")
CHIPBOY_BLARGG("03 trigger",                "03-trigger.gb")
CHIPBOY_BLARGG("04 sweep",                  "04-sweep.gb")
CHIPBOY_BLARGG("05 sweep details",          "05-sweep details.gb")
CHIPBOY_BLARGG("06 overflow on trigger",    "06-overflow on trigger.gb")
CHIPBOY_BLARGG("07 len sweep period sync",  "07-len sweep period sync.gb")
CHIPBOY_BLARGG("08 len ctr during power",   "08-len ctr during power.gb")
CHIPBOY_BLARGG("09 wave read while on",     "09-wave read while on.gb")
CHIPBOY_BLARGG("10 wave trigger while on",  "10-wave trigger while on.gb")
CHIPBOY_BLARGG("11 regs after power",       "11-regs after power.gb")
CHIPBOY_BLARGG("12 wave write while on",    "12-wave write while on.gb")
