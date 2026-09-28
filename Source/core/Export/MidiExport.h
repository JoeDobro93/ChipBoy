// ChipBoy -- MIDI export (docs/plan-midi-export.md, docs/COMMANDS_AND_TEMPO.md
// section 224): the song rendered through the engine, and the note each
// channel sounds after every tick written down as a standard MIDI file.
//
// Nothing about a command is re-implemented here. The Clock, the Player and
// the Driver play the song exactly as the plugin does -- the play order with
// its H hops, the grooves, the tables, the chords, the kills -- and the
// exporter asks the driver which note is sounding. A change is a note-off and
// a note-on; silence is a note-off. The time signatures are the song's own
// (section 222) and the tempo its master tempo with the T cells' map (4).
#pragma once
#include "core/Bank/Bank.h"
#include "core/Tracker/Song.h"

#include <cstdint>
#include <string>
#include <vector>

namespace chipboy::midi {

struct Options {
    bool    noise = true;        ///< write the noise channel's track
    uint8_t velocity = 100;      ///< every note-on's velocity: the file carries notes and nothing else
};

/// What the export did, for the status line.
struct Report {
    int notes[4] = { 0, 0, 0, 0 };   ///< note-ons written per channel
    int64_t ticks = 0;               ///< the song's length rendered
    int ppq = 24;                    ///< the file's ticks per quarter note
    std::vector<std::string> notes_; ///< what could not be carried exactly
};

/// The song rendered on the bank it plays through, as the bytes of a format-1
/// MIDI file: track 0 carries the name, the tempo and the signatures, then
/// one track per channel. Empty when the song has no rows.
std::vector<uint8_t> exportSong(const tracker::Song& song, const bank::Bank& bank, const Options& options, Report* report = nullptr);

} // namespace chipboy::midi
