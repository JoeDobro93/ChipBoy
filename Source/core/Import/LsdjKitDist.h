// ChipBoy -- how LSDj sums a kit note's two samples (docs/COMMANDS_AND_TEMPO.md
// section 117).
// Core: <std> only.
//
// A kit note plays one sample from each of the instrument's two kits, and the
// instrument's `DIST` says how the pair is added. LSDj does it with a 256-byte
// lookup table whose **page** the instrument stores in byte 10 -- `D0`, `D1`,
// `D2` or `D3`. Every one of those tables is a function of the sum of the two
// nibbles, so each is a curve, reproduced here in closed form and checked entry
// for entry against all 47 ROMs in the archive. Which curve a page names moved
// at 9.2, so the model carries the pair (LsdjModel::kitDist).
#pragma once

#include <cstdint>

namespace chipboy::lsdj {

/// The mixing curves, over the sum `s = a + b - 8` of two 0-15 nibbles.
enum class KitDist : uint8_t {
    Clip,   ///< `HARD` from 9.2, `CLIP` before: the sum, clamped to 0-15
    Soft,   ///< `SOFT` (9.2 and later): half slope outside a knee
    Fold,   ///< `FOLD` from 9.2, `SHAPE` before: the sum mirrored at 0 and 15
    Fold2,  ///< `SHAP2` (before 9.2): the mirror with twice the slope outside
    Wrap    ///< `WRAP`: `(s - 8) & 15`
};

/// The page byte 10 holds for each mode's table, in page order.
inline constexpr uint8_t kKitDistFirstPage = 0xD0;
inline constexpr int     kKitDistPages = 4;

/// One entry of LSDj's table: `row` indexes it by 16, `col` by 1. Only
/// `Fold2` tells the two apart.
inline int kitDistEntry(KitDist mode, int row, int col)
{
    auto clamp = [](int v) { return v < 0 ? 0 : (v > 15 ? 15 : v); };
    const int s = row + col - 8;
    switch (mode) {
        case KitDist::Clip: return clamp(s);
        case KitDist::Wrap: return s & 15;
        case KitDist::Fold: return s < 0 ? -s : (s > 15 ? 30 - s : s);
        case KitDist::Fold2:
            // The ROM's table has one entry the fold does not give (section 117).
            if (row == 12 && col == 15) return 5;
            return s < 0 ? clamp(-2 * s) : (s > 15 ? clamp(15 - 2 * (s - 15)) : s);
        case KitDist::Soft: {
            const int k = s >= 8 ? s - 8 : 8 - s;
            const int h = k <= 4 ? k : (k >= 13 ? 8 : (((k + 4) / 2) > 7 ? 7 : (k + 4) / 2));
            return clamp(s >= 8 ? 8 + h : 8 - h);
        }
    }
    return clamp(s);
}

/// One mixed nibble. `index` is the sample's position in the stream: LSDj packs
/// two samples to a byte and indexes the table the other way round for the low
/// one, which only `Fold2` notices.
inline uint8_t kitMix(KitDist mode, int index, int hiDigitSample, int loDigitSample)
{
    const int a = hiDigitSample & 15, b = loDigitSample & 15;
    return uint8_t((index & 1) == 0 ? kitDistEntry(mode, b, a) : kitDistEntry(mode, a, b));
}

} // namespace chipboy::lsdj
