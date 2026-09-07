#include "tools/harness/Machine.h"

#include <fstream>
#include <iterator>

namespace chipboy::harness {

Machine::Machine(std::vector<uint8_t> rom)
{
    apu_.reset();
    bus_ = std::make_unique<Bus>(std::move(rom), apu_);
    cpu_ = std::make_unique<Sm83>(*bus_);
    // Post-boot divider on a DMG. The tests that care reset DIV themselves.
    for (int i = 0; i < 0xABCC / 4; ++i) apu_.runTo(apu_.cycle() + 4);
}

std::vector<uint8_t> Machine::loadRom(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

RunResult Machine::run(uint64_t maxCycles, bool stopOnBreakpoint)
{
    RunResult r;
    const uint64_t start = bus_->cycles();
    const uint8_t* ram = bus_->cartRam();
    cpu_->breakpoint = false;
    while (bus_->cycles() - start < maxCycles) {
        cpu_->step();
        if (stopOnBreakpoint && cpu_->breakpoint) {
            r.finished = r.breakpoint = true;
            break;
        }
        if ((cpu_->instructions & 0xFF) != 0) continue;
        // blargg's convention: $A001..$A003 = DE B0 61 marks a valid result
        // at $A000; $80 there means "still running".
        if (ram[1] == 0xDE && ram[2] == 0xB0 && ram[3] == 0x61 && ram[0] != 0x80) {
            r.finished = true;
            r.code = ram[0];
            for (int i = 4; i < 0x1000 && ram[i]; ++i) r.text.push_back(char(ram[i]));
            break;
        }
        // Fallback: the same verdict is printed to the serial port.
        const std::string& s = bus_->serial();
        if (s.size() >= 7 && s.compare(s.size() - 7, 7, "Passed\n") == 0) {
            r.finished = true;
            r.code = 0;
            break;
        }
        if (const auto pos = s.find("Failed"); pos != std::string::npos && s.find('\n', pos) != std::string::npos) {
            r.finished = true;
            r.code = ram[0] != 0x80 ? ram[0] : 1;
            break;
        }
    }
    r.serial = bus_->serial();
    r.cycles = bus_->cycles() - start;
    return r;
}

} // namespace chipboy::harness

namespace chipboy::harness {
void Machine::stepUntil(uint64_t cycle)
{
    while (bus_->cycles() < cycle) cpu_->step();
}
} // namespace chipboy::harness
