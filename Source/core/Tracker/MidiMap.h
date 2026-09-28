// ChipBoy -- the MIDI map (docs/plan-midi-map.md, docs/COMMANDS_AND_TEMPO.md
// section 225): each of a port's sixteen MIDI channels assigned to one ChipBoy
// channel, and within each, velocity regions that are tracker rows without
// the note -- an instrument, a table, two commands. A mapped note-on is the
// cell the region describes, played by the driver's cell path.
#pragma once
#include "core/Bank/Bank.h"

#include <array>
#include <cstdint>
#include <vector>

namespace chipboy::tracker {

constexpr int kMidiChannels = 16, kMaxRegions = 8;

/// A row without a note: the lowest velocity it takes, and a cell's other
/// columns. The next region's `from` ends it; the last runs to 127.
struct MidiRegion {
    uint8_t from = 1;
    uint8_t inst = 0, table = 0;          ///< 0 keep, as a cell's columns
    bank::Command cmd1, cmd2;
};

struct MidiChannelMap {
    int8_t target = -1;                   ///< the ChipBoy channel 0-3, -1 off
    std::vector<MidiRegion> regions{ MidiRegion{} };   ///< one blank region: pitch and gate alone
};

struct MidiMap {
    bool on = false;                      ///< off: the channels' Source parameters route, as before
    std::array<MidiChannelMap, kMidiChannels> channels;
};

/// Regions sorted by `from`, the first at 1, no two alike, one to kMaxRegions.
void normalizeMidiMap(MidiMap& m);
/// The region a velocity lands in: the last whose `from` is at or below it.
const MidiRegion& regionFor(const MidiChannelMap& c, uint8_t velocity);
/// Off with nothing assigned and every region blank: the file leaves it out.
bool midiMapIsDefault(const MidiMap& m);
/// The letters a region may carry: not H, G or T -- a hop, a groove and a
/// tempo belong to the timeline.
bool midiCommandAllowed(bank::Cmd c);
/// The region's columns as a cell's, the disallowed letters dropped.
bank::Command midiRegionCommand(const bank::Command& c);

} // namespace chipboy::tracker
