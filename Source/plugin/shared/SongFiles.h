// ChipBoy -- song files (docs/COMMANDS_AND_TEMPO.md section 15).
//
// A `.cbsong` holds the song alone -- chains, phrases, grooves, the steps per
// bar and the bar overrides, tempo, song start, beats per bar, playback
// sources and arms -- in the song JSON of format 4. It also records the name
// of the bank it was written with and the name of every instrument slot it
// uses, so a load can say where this bank differs: "slot 7 was Triangle bass;
// this bank has Organ". Loading replaces the song; with the bank it was
// written with it sounds the same.
#pragma once

#include "core/Bank/Bank.h"
#include "core/Tracker/Song.h"

#include <juce_core/juce_core.h>

namespace chipboy::plugin {

/// What a load found: the bank the song was written with, and every slot
/// whose name in this bank differs from the one the file remembers.
struct SongReport {
    juce::String bankName;                 ///< the bank the file was written with
    juce::StringArray differences;         ///< one line per slot that does not match
    juce::StringArray missing;             ///< slots the song uses that this bank has not filled
    int instrumentsUsed = 0;
};

/// The song, the names of the instrument slots it uses, and the bank's own
/// name, as one JSON document.
juce::String songFileText(const tracker::Song& song, const bank::Bank& bank, const juce::String& bankName);
bool saveSong(const tracker::Song& song, const bank::Bank& bank, const juce::File& file, const juce::String& bankName = {});

/// Read one back. `current` is the bank the song is about to play through, if
/// there is one: the report then names the slots whose names differ.
bool loadSongText(const juce::String& text, tracker::Song& out, SongReport& report, const bank::Bank* current = nullptr);
bool loadSong(const juce::File& file, tracker::Song& out, SongReport& report, const bank::Bank* current = nullptr);

/// Every instrument slot a song's cells name.
std::vector<int> instrumentsUsedBy(const tracker::Song& song);

/// Documents/ChipBoy/Songs, beside the banks folder; where the dialogs start.
juce::File songsFolder();
constexpr const char* kSongExtension = ".cbsong";

} // namespace chipboy::plugin
