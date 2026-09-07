// ChipBoy -- SameSuite APU acceptance tests (spec section 16.1).
//
// SameSuite (LIJI32, MIT) tests are assembled from source at build time when
// RGBDS is available (cmake/TestRoms.cmake). Each ROM compares its results
// against a table baked into the ROM, sends six bytes over the serial port --
// the Fibonacci numbers 3 5 8 13 21 34 on a pass, six $42 on a failure -- and
// executes `ld b,b`, the software breakpoint the harness stops on.
//
// Only the tests observable on a DMG are run here. The rest read the CGB-only
// PCM12/PCM34 registers; they gate the CGB model when that is built.
#include "tools/harness/Machine.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

/// Address of a label in the rgblink .sym file next to the ROM, or -1.
long symbolAddress(const std::filesystem::path& sym, const std::string& label)
{
    std::ifstream in(sym);
    std::string line;
    while (std::getline(in, line)) {
        // "00:c000 RESULTS_START"
        std::istringstream ss(line);
        std::string addr, name;
        if (!(ss >> addr >> name) || name != label) continue;
        const auto colon = addr.find(':');
        if (colon == std::string::npos) continue;
        return std::stol(addr.substr(colon + 1), nullptr, 16);
    }
    return -1;
}

std::string hexRows(const uint8_t* p, size_t n)
{
    std::string out;
    char buf[8];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(buf, sizeof buf, "%02X%s", p[i], (i % 16 == 15) ? "\n" : " ");
        out += buf;
    }
    return out;
}

void runSameSuite(const char* name)
{
    const std::string dir = CHIPBOY_SAMESUITE_ROM_DIR;
    if (dir.empty()) SKIP("SameSuite ROMs not assembled (RGBDS not found at configure time)");
    const auto rom = std::filesystem::path(dir) / (std::string(name) + ".gb");
    if (!std::filesystem::exists(rom)) SKIP("missing test ROM: " << rom.string());

    auto bytes = chipboy::harness::Machine::loadRom(rom.string());
    REQUIRE_FALSE(bytes.empty());
    chipboy::harness::Machine m(std::move(bytes));
    const auto r = m.run(uint64_t(chipboy::kCpuHz) * 30, /*stopOnBreakpoint*/ true);

    INFO("cycles: " << r.cycles);
    REQUIRE(r.breakpoint);
    const std::string pass("\x03\x05\x08\x0d\x15\x22", 6);
    if (r.serial != pass) {
        // Show what the ROM saw against what it expected. Results start at
        // RESULTS_START in WRAM; the expected table is CorrectResults in ROM.
        const auto sym = std::filesystem::path(dir) / (std::string(name) + ".sym");
        long results = symbolAddress(sym, "RESULTS_START");
        const long correct = symbolAddress(sym, "CorrectResults");
        if (results < 0) results = 0xC000;
        std::string diag;
        if (correct >= 0 && results >= 0xC000 && results < 0xE000) {
            const size_t n = 128;
            diag += "results ($" + std::to_string(results) + ")\n"
                  + hexRows(m.bus().wram() + (results - 0xC000), n)
                  + "expected (CorrectResults)\n"
                  + hexRows(m.bus().rom().data() + correct, n);
        }
        INFO("serial: " << hexRows(reinterpret_cast<const uint8_t*>(r.serial.data()), r.serial.size()));
        INFO(diag);
        FAIL("SameSuite " << name << " reported a failure");
    }
    SUCCEED();
}

} // namespace

#define CHIPBOY_SAMESUITE(name) \
    TEST_CASE("SameSuite apu " name, "[samesuite]") { runSameSuite(name); }

CHIPBOY_SAMESUITE("div_write_trigger")
CHIPBOY_SAMESUITE("div_write_trigger_10")
CHIPBOY_SAMESUITE("channel_3_wave_ram_dac_on_rw")
CHIPBOY_SAMESUITE("channel_3_wave_ram_locked_write")
