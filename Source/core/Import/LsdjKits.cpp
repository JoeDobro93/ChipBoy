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
        kit.bank = int(base / kBankSize);
        kit.loopBits = uint16_t(rom[base + 0x5C] | (rom[base + 0x5D] << 8));   // section 172
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

const LsdjKit* lsdjKitByNumber(const std::vector<LsdjKit>& kits, int k)
{
    // Section 193: the k-th kit bank in ROM order, gaps skipped -- probed on
    // 9.2.L and 9.4.2 (kit byte 13 plays the twentieth kit, bank 20, over five
    // empty banks; a number past the count plays nothing).
    return k >= 0 && size_t(k) < kits.size() ? &kits[size_t(k)] : nullptr;
}

uint16_t kitPeriodOfSpeed(uint8_t speedByte, bool halfSpeed)
{
    const int signedSpeed = speedByte >= 128 ? int(speedByte) - 256 : int(speedByte);
    return uint16_t(std::clamp((halfSpeed ? 1682 : 1865) + signedSpeed, 0, 2047));
}

namespace {
/// A version string as a triple: "9.2.L" -> (9, 2, 21), the patch's letters
/// after its digits as LSDj counts them.
bool versionTriple(const std::string& v, int out[3])
{
    int n = 0; size_t i = 0;
    for (int k = 0; k < 3; ++k) {
        if (i >= v.size()) return false;
        const char c = v[i];
        if (c >= '0' && c <= '9') { int x = 0; while (i < v.size() && v[i] >= '0' && v[i] <= '9') x = x * 10 + (v[i++] - '0'); out[k] = x; }
        else if (c >= 'A' && c <= 'Z') { out[k] = 10 + (c - 'A'); ++i; }
        else return false;
        ++n;
        if (k < 2) { if (i >= v.size() || v[i] != '.') return false; ++i; }
    }
    return n == 3;
}
} // namespace

LsdjRawPages lsdjRawPages(const uint8_t* rom, size_t size, const std::string& version)
{
    LsdjRawPages pages;
    // Section 192: video RAM during playback, dumped on every ROM of the
    // archive (tools/lsdjref's `--dump`, /root/lsdj/probe/vramdump.py). Pages
    // $82-$87 and $9B-$9F are zero on every one of them; pages $89-$8C, $8E-$8F,
    // $91-$93 and $95-$97 are tile blocks copied from the ROM, all at fixed
    // distances from the font block at $8E00, whose ROM offset moves with the
    // version. The rest ($80, $81, $88, $8D, $90, $94, $98-$9A) are drawn at
    // run time and are not reproduced.
    for (int pg = 0x82; pg <= 0x87; ++pg) pages[pg] = std::vector<uint8_t>(256, 0);
    for (int pg = 0x9B; pg <= 0x9F; ++pg) pages[pg] = std::vector<uint8_t>(256, 0);
    if (rom == nullptr) return pages;
    // The font block's offset by version: the newest entry at or below the ROM's.
    struct Base { int v[3]; size_t at; };
    static const Base kBases[] = {
        { { 3, 1, 5 }, 0x784A9 }, { { 3, 4, 4 }, 0x7845C }, { { 3, 6, 8 }, 0x7845B }, { { 3, 8, 7 }, 0x78446 },
        { { 4, 5, 4 }, 0x7843C }, { { 5, 0, 3 }, 0x78457 }, { { 8, 4, 0 }, 0x7843C }, { { 9, 2, 19 }, 0x7842A },
    };
    int v[3] = { 0, 0, 0 };
    if (!versionTriple(version, v)) return pages;
    const Base* best = nullptr;
    for (const Base& b : kBases) {
        const bool below = b.v[0] < v[0] || (b.v[0] == v[0] && (b.v[1] < v[1] || (b.v[1] == v[1] && b.v[2] <= v[2])));
        if (below) best = &b;
    }
    if (best == nullptr) return pages;
    struct Block { int page; size_t off; };
    static const Block kBlocks[] = {
        { 0x89, 0x163A }, { 0x8A, 0x173A }, { 0x8B, 0x183A }, { 0x8C, 0x193A }, { 0x8E, 0x0000 }, { 0x8F, 0x0100 },
        { 0x91, 0x1208 }, { 0x92, 0x1308 }, { 0x93, 0x1408 }, { 0x95, 0x1B5C }, { 0x96, 0x1C5C }, { 0x97, 0x1D5C },
    };
    for (const Block& b : kBlocks) {
        const size_t at = best->at + b.off;
        if (at + 256 <= size) pages[b.page] = std::vector<uint8_t>(rom + at, rom + at + 256);
    }
    return pages;
}

} // namespace chipboy::lsdj
