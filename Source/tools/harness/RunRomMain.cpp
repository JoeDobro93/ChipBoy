// chipboy_runrom -- run one ROM and print what it reports.
#include "tools/harness/Machine.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: chipboy_runrom rom.gb [seconds]\n"); return 2; }
    auto rom = chipboy::harness::Machine::loadRom(argv[1]);
    if (rom.empty()) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    const double secs = argc > 2 ? std::stod(argv[2]) : 60.0;
    chipboy::harness::Machine m(std::move(rom));
    auto r = m.run(uint64_t(secs * chipboy::kCpuHz));
    std::printf("%s\n", argv[1]);
    std::printf("  finished: %s   code: %u   cycles: %llu (%.2f s)\n",
                r.finished ? "yes" : "NO (timed out)", unsigned(r.code),
                (unsigned long long)r.cycles, double(r.cycles) / chipboy::kCpuHz);
    if (!r.text.empty())   std::printf("  text:\n%s\n", r.text.c_str());
    if (!r.serial.empty()) std::printf("  serial:\n%s\n", r.serial.c_str());
    return (r.finished && r.code == 0) ? 0 : 1;
}
