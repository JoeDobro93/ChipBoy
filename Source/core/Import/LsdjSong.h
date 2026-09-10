// ChipBoy -- an LSDj song's 32 KB into a bank and a song
// (docs/plan-lsdj-import.md sections 2 and 4). Core: <std> and "core/...".
#pragma once

#include "core/Bank/Bank.h"
#include "core/Import/LsdjKits.h"
#include "core/Import/LsdjModel.h"
#include "core/Tracker/Song.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chipboy::lsdj {

/// What could not be carried over exactly, one line each, no repeats: the
/// converter's FINDINGS.
struct ImportNotes {
    std::vector<std::string> lines;
    void add(const std::string& line);
};

struct ImportSummary {
    int instruments = 0, tables = 0, waves = 0, kits = 0, phrases = 0, rows = 0;
    double tempoBpm = 0.0;
};

/// Reads a 32 KB song under a model into a blank bank and a blank song.
/// False when the buffer is short. The bank and the song are overwritten.
/// `kits` are the ROM's kit banks (plan section 4a); without them a kit
/// instrument is noted and skipped.
bool importSong(const uint8_t* song, size_t size, const LsdjModel& model,
                bank::Bank& bank, tracker::Song& out, ImportSummary& summary, ImportNotes& notes,
                const std::vector<LsdjKit>* kits = nullptr);

/// The ChipBoy note whose noise pair has the same LFSR clock as an NR43
/// byte, the one nearest `prefer` on a tie (section 45). Exposed for tests.
int chipboyNoteForNr43(uint8_t nr43, int prefer);

} // namespace chipboy::lsdj
