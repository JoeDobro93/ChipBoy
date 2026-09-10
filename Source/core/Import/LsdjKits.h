// ChipBoy -- the kits an LSDj ROM carries (docs/plan-lsdj-import.md section
// 4a): 16 KB banks of 4-bit samples at 11468 Hz, read from the user's own ROM
// at import time so a kit instrument can be carried into a song. Core: <std>.
#pragma once

#include <cstddef>
#include <cstdint>
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
};

/// Every kit bank in a ROM, in ROM order -- which is how LSDj numbers them,
/// kit 00 first. A bank is a kit when it opens with the bytes 60 40.
std::vector<LsdjKit> readKits(const uint8_t* rom, size_t size);

/// The wave period a kit instrument plays at: 1865 (11468 Hz) plus the
/// instrument's speed byte read two's complement (measured on 9.2.L).
uint16_t kitPeriodOfSpeed(uint8_t speedByte);

} // namespace chipboy::lsdj
