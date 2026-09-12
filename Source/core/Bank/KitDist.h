// ChipBoy -- how a kit note's two samples are summed (docs/COMMANDS_AND_TEMPO.md
// section 117, docs/plan-kit-pairs.md).
// Core: <std> only.
//
// LSDj mixes a kit note's two samples through a 256-byte lookup table, one per
// `DIST` setting. Every one of those tables turned out to be a function of the
// sum of the two nibbles, so each is a curve; the five here are those curves in
// closed form, checked entry for entry against all 47 ROMs in the archive and
// end to end against the ROM's own streamed wave RAM. They are ChipBoy's own
// kit mixing modes now, not only an import detail: the driver sums a pair live.
#pragma once

#include <cstdint>

namespace chipboy::bank {

/// The mixing curves, over the sum `s = a + b - 8` of two 0-15 nibbles.
/// `Fold2` is LSDj's pre-9.2 `SHAP2`; the rest are 9.2's `HARD`, `SOFT`,
/// `FOLD` and `WRAP`.
enum class KitDist : uint8_t {
    Clip,   ///< the sum, clamped to 0-15
    Soft,   ///< half slope outside a knee four either side of the middle
    Fold,   ///< the sum mirrored at 0 and 15
    Fold2,  ///< the mirror with twice the slope outside
    Wrap    ///< `(s - 8) & 15` -- the sum wraps round
};
constexpr int kKitDistCount = 5;

/// One entry of LSDj's table: `row` indexes it by 16, `col` by 1. Only `Fold2`
/// tells the two apart.
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
            const int half = (k + 4) / 2;
            const int h = k <= 4 ? k : (k >= 13 ? 8 : (half > 7 ? 7 : half));
            return clamp(s >= 8 ? 8 + h : 8 - h);
        }
    }
    return clamp(s);
}

/// One mixed nibble. `index` is the sample's position in the stream: LSDj packs
/// two samples to a byte and indexes the table the other way round for the low
/// one, which only `Fold2` notices.
inline uint8_t kitMix(KitDist mode, int index, int first, int second)
{
    const int a = first & 15, b = second & 15;
    return uint8_t((index & 1) == 0 ? kitDistEntry(mode, b, a) : kitDistEntry(mode, a, b));
}

/// The name shown in the editor and written to a bank.
inline const char* kitDistName(KitDist m)
{
    switch (m) {
        case KitDist::Clip:  return "clip";
        case KitDist::Soft:  return "soft";
        case KitDist::Fold:  return "fold";
        case KitDist::Fold2: return "fold2";
        case KitDist::Wrap:  return "wrap";
    }
    return "clip";
}

} // namespace chipboy::bank
