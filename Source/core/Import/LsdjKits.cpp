// ChipBoy -- the kits an LSDj ROM carries (docs/plan-lsdj-import.md section 4a).
#include "core/Import/LsdjKits.h"

#include <algorithm>

namespace chipboy::lsdj {

namespace {
constexpr size_t kBankSize = 0x4000;
constexpr size_t kOffsetsAt = 0x00, kSampleNamesAt = 0x22, kKitNameAt = 0x52, kDataAt = 0x60;
std::string text(const uint8_t* p, size_t n)
{
    std::string s;
    for (size_t k = 0; k < n; ++k) { const uint8_t c = p[k]; if (c == 0) break; s += (c >= 32 && c < 127) ? char(c) : '?'; }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
} // namespace

std::vector<LsdjKit> readKits(const uint8_t* rom, size_t size)
{
    std::vector<LsdjKit> kits;
    if (rom == nullptr) return kits;
    for (size_t base = 0; base + kBankSize <= size; base += kBankSize) {
        if (rom[base] != 0x60 || rom[base + 1] != 0x40) continue;
        LsdjKit kit;
        kit.name = text(rom + base + kKitNameAt, 6);
        // Sixteen little-endian words: the address, in the 4000-7FFF window,
        // where each sample ends -- word 0 is the 60 40 magic read as 4060,
        // which is also where the first sample starts.
        uint16_t offs[16];
        for (int i = 0; i < 16; ++i) offs[i] = uint16_t(rom[base + kOffsetsAt + size_t(2 * i)] | (rom[base + kOffsetsAt + size_t(2 * i) + 1] << 8));
        for (int i = 0; i < 15; ++i) {
            const uint16_t a = offs[i], b = offs[i + 1];
            if (a < 0x4000 + kDataAt || b <= a || b > 0x8000) break;
            LsdjKitSample s;
            s.name = text(rom + base + kSampleNamesAt + size_t(3 * i), 3);
            s.nibbles.reserve(size_t(b - a) * 2);
            for (size_t k = size_t(a) - 0x4000; k < size_t(b) - 0x4000; ++k) { s.nibbles.push_back(uint8_t(rom[base + k] >> 4)); s.nibbles.push_back(uint8_t(rom[base + k] & 15)); }
            kit.samples.push_back(std::move(s));
        }
        kits.push_back(std::move(kit));
    }
    return kits;
}

uint16_t kitPeriodOfSpeed(uint8_t speedByte)
{
    const int signedSpeed = speedByte >= 128 ? int(speedByte) - 256 : int(speedByte);
    return uint16_t(std::clamp(1865 + signedSpeed, 0, 2047));
}

} // namespace chipboy::lsdj
