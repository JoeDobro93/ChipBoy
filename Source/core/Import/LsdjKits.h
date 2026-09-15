// ChipBoy -- the kits an LSDj ROM carries (docs/plan-lsdj-import.md section
// 4a): 16 KB banks of 4-bit samples at 11468 Hz, read from the user's own ROM
// at import time so a kit instrument can be carried into a song. Core: <std>.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace chipboy::lsdj {

struct LsdjKitSample {
    std::string name;                ///< three characters
    std::vector<uint8_t> nibbles;    ///< 4-bit samples, one per byte, 0-15
};
struct LsdjKit {
    std::string name;                ///< six characters
    std::vector<LsdjKitSample> samples;   ///< up to 15
    int      bank = 0;               ///< the ROM bank it sits in: kit number + 8 (section 172)
    uint16_t loopBits = 0;           ///< one loop bit a sample, from the bank header at $405C/$405D
};

/// Every kit bank in a ROM, in ROM order -- which is how LSDj numbers them,
/// kit 00 first. A bank is a kit when it opens with the bytes 60 40.
std::vector<LsdjKit> readKits(const uint8_t* rom, size_t size);

/// Kit number `k` of the ROM: the bank `k + 8`, gaps or not (section 172).
const LsdjKit* lsdjKitByNumber(const std::vector<LsdjKit>& kits, int k);

/// The wave period a kit instrument plays at: `$749` (1865, 11468 Hz) plus the
/// instrument's speed byte read two's complement, `$692` plus it at half speed
/// (section 172).
uint16_t kitPeriodOfSpeed(uint8_t speedByte, bool halfSpeed = false);

/// Section 172: the pages of memory a kit's DIST byte can name outside LSDj's
/// four tables, as the ROM fills them -- page `$8E` is the font tiles at VRAM
/// `$8E00`, loaded from the ROM's own data. Only what has been read on the
/// ROM named by `version` is returned.
using LsdjRawPages = std::map<int, std::vector<uint8_t>>;
LsdjRawPages lsdjRawPages(const uint8_t* rom, size_t size, const std::string& version);

} // namespace chipboy::lsdj
