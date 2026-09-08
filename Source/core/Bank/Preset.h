// ChipBoy -- instrument presets (docs/COMMANDS_AND_TEMPO.md section 15).
//
// One instrument with everything it references, transitively: its table, the
// tables that table's A commands start, the wave a wave instrument plays (and
// the wave slots its tables' W commands select) and the kit a kit instrument
// plays. Collecting and placing are core code, so a playback ROM's tooling can
// do both; the file format and the chooser are the plugin's.
//
// Plain C++20: no allocation on any audio path, nothing but the standard
// library and the bank.
#pragma once

#include "core/Bank/Bank.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace chipboy::bank {

/// An instrument and its dependencies, each with the slot it came from.
struct Preset {
    Instrument instrument;
    std::vector<std::pair<int, Table>> tables;
    std::vector<std::pair<int, Wave>>  waves;
    std::vector<std::pair<int, Kit>>   kits;
};

/// What placing a preset did, for the status line.
struct PlaceReport {
    enum class Kind : uint8_t { Instrument, Table, Wave, Kit };
    struct Move {
        Kind kind = Kind::Table;
        int  from = 0;               ///< the slot in the preset
        int  to = 0;                 ///< the slot in this bank
        bool reused = false;         ///< an identical one was already there
    };
    bool ok = false;
    int  instrumentSlot = 0;
    std::vector<Move> moves;
    const char* error = nullptr;     ///< why it failed: which kind ran out of slots
};

/// Content equality, names included: a renamed table is a different table, so
/// placing a preset never silently adopts something that looks alike.
bool sameTable(const Table& a, const Table& b);
bool sameWave(const Wave& a, const Wave& b);
bool sameKit(const Kit& a, const Kit& b);

/// Everything instrument `slot` references, walked transitively.
Preset collectPreset(const Bank& b, int instrumentSlot);

/// Place a preset into `targetSlot`: each dependency goes to the first free
/// slot of its kind unless an identical one is already in the bank, and every
/// reference -- the instrument's and the tables' -- is renumbered to match. It
/// fails cleanly, changing nothing, when a kind runs out of slots.
bool placePreset(Bank& b, const Preset& p, int targetSlot, PlaceReport& report);

} // namespace chipboy::bank
