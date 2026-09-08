// ChipBoy -- song files (docs/COMMANDS_AND_TEMPO.md sections 15 and 18).
//
// A `.cbsong` holds the song -- chains, phrases, grooves, the steps per bar
// and the bar overrides, tempo, song start, beats per bar, playback sources
// and arms -- and, since format 5, **the bank it plays through**: instruments,
// tables, waves and kits with their samples, so a song file is complete and
// opening one opens a tab with its own sounds (section 18). It still records
// the bank's name and the name of every instrument slot it uses, so a
// format-4 file meeting another bank can say where it differs: "slot 7 was
// Triangle bass; this bank has Organ".
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
    bool hasBank = false;                  ///< format 5: the file brought its own bank
};

/// The song, the bank it plays through, the names of the instrument slots it
/// uses and the bank's own name, as one JSON document (format 5).
juce::String songFileText(const tracker::Song& song, const bank::Bank& bank, const juce::String& bankName);
bool saveSong(const tracker::Song& song, const bank::Bank& bank, const juce::File& file, const juce::String& bankName = {});

/// Read one back. `current` is the bank the song is about to play through, if
/// there is one: for a format-4 file the report then names the slots whose
/// names differ. `bankOut` receives the file's own bank when it carries one
/// (format 5), and `report.hasBank` says whether it did; the report is then
/// against that bank, so it has nothing to complain about. A Bank is 41 KB:
/// pass one that lives on the heap.
bool loadSongText(const juce::String& text, tracker::Song& out, SongReport& report,
                  const bank::Bank* current = nullptr, bank::Bank* bankOut = nullptr);
bool loadSong(const juce::File& file, tracker::Song& out, SongReport& report,
              const bank::Bank* current = nullptr, bank::Bank* bankOut = nullptr);

/// Every instrument slot a song's cells name.
std::vector<int> instrumentsUsedBy(const tracker::Song& song);

/// Documents/ChipBoy/Songs, beside the banks folder; where the dialogs start.
juce::File songsFolder();
constexpr const char* kSongExtension = ".cbsong";

} // namespace chipboy::plugin
