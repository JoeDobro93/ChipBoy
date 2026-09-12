// ChipBoy -- which mixing curve a kit instrument's byte 10 names
// (docs/COMMANDS_AND_TEMPO.md section 117).
// Core: <std> only.
//
// LSDj keeps its four mixing tables at `$D000`-`$D300` and the instrument
// stores the **page**, so byte 10 reads `D0`, `D1`, `D2` or `D3`. Which curve a
// page names moved at 9.2, so the model carries the list (LsdjModel::kitDist).
// The curves themselves are ChipBoy's own now: core/Bank/KitDist.h.
#pragma once

#include "core/Bank/KitDist.h"

namespace chipboy::lsdj {

using bank::KitDist;
using bank::kitDistEntry;
using bank::kitMix;

/// The page byte 10 holds for the first of the four tables, and how many.
inline constexpr uint8_t kKitDistFirstPage = 0xD0;
inline constexpr int     kKitDistPages = 4;

} // namespace chipboy::lsdj
