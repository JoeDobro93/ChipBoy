// ChipBoy test-ROM harness -- loads a ROM and runs it to a blargg result.
#pragma once

#include "core/Apu/Apu.h"
#include "tools/harness/Bus.h"
#include "tools/harness/Sm83.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chipboy::harness {

struct RunResult {
    bool        finished = false;   ///< a result signature or breakpoint appeared
    bool        breakpoint = false; ///< stopped on `ld b,b`
    uint8_t     code = 0xFF;        ///< blargg: 0 = pass
    std::string text;               ///< the test's own message, if any
    std::string serial;             ///< everything written to the serial port
    uint64_t    cycles = 0;
};

class Machine {
public:
    explicit Machine(std::vector<uint8_t> rom);
    static std::vector<uint8_t> loadRom(const std::string& path);

    /// Run until the ROM reports a blargg result, `maxCycles` elapse, or --
    /// with `stopOnBreakpoint` -- the CPU executes `ld b,b`, the software
    /// breakpoint SameSuite ends every test with.
    RunResult run(uint64_t maxCycles = 4194304ull * 60, bool stopOnBreakpoint = false);
    /// Step the CPU until the APU has reached `cycle` (it overshoots by at
    /// most one instruction). For rendering a ROM's audio block by block.
    void stepUntil(uint64_t cycle);

    Apu&  apu()  { return apu_; }
    Bus&  bus()  { return *bus_; }
    Sm83& cpu()  { return *cpu_; }

private:
    Apu apu_;
    std::unique_ptr<Bus>  bus_;
    std::unique_ptr<Sm83> cpu_;
};

} // namespace chipboy::harness
