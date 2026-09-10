// ChipBoy -- what a version of LSDj means by a song's bytes
// (docs/plan-lsdj-import.md section 3). Core: <std> only.
//
// A song's format version (byte 0x7FFF) picks a model; the model holds every
// table the interpreter switches on. The parser in LsdjSong.cpp is shared:
// only what a version does with a byte differs.
//
// ADDING A VERSION (the user supplies the ROM; the harness under tools/lsdjref
// traces it):
//   1. Copy the 9.3.9 entry in LsdjModel.cpp, name it, and give it the format
//      version that ROM writes (load any song on it and read byte 0x7FFF of
//      the working song) and the range of formats it should read.
//   2. Re-measure what may differ, one harness case each, and point the
//      entry at the new tables: the command byte table (a phrase with every
//      letter, read which register each byte moves -- 9.x inserted B at 2);
//      the noise map (every note on a noise instrument, read NR43); the
//      envelope (one note, the speed patched 1..F, read the NR42 step
//      periods, docs/COMMANDS_AND_TEMPO.md section 51) and whether the
//      envelope is the NRx2 byte or the three stages; the wave octave (a wave
//      note against the pulse period table, section 45); PU2 TSP (section
//      49). The tick-based letters P and V (section 7) belong here too when a
//      version's tables differ; today the driver's are used for every model.
//   3. Set `measured` once the ROM traced it; leave it false for an entry
//      assumed from another version, so the dialog can say so.
//   4. Add a case to Tests/LsdjImportTests.cpp that reads a synthetic song
//      under the new model. docs/HANDOFF.md repeats these steps.
#pragma once

#include <cstdint>

namespace chipboy::lsdj {

struct LsdjModel {
    const char*    name;             ///< "LSDj 9.3.9"
    int            formatVersion;    ///< the song format this version writes
    int            formatMin, formatMax;   ///< the formats this model reads, inclusive
    const char*    commandLetters;   ///< command byte -> letter; index 0 is none; '\0' ends it
    bool           stagedEnvelope;   ///< the three-stage software envelope (section 51); else byte 1 is NRx2
    const uint8_t* envPeriods;       ///< [16] pitch-clock periods per level, speeds 0-F (staged only)
    const uint8_t* noiseMap;         ///< [128] MIDI note -> NR43 byte; 0xFF where unmeasured
    int            waveOctave;       ///< semitones added to a wave note (section 45)
    bool           pu2Transpose;     ///< instrument byte 2 applies on PU2 (section 49)
    bool           measured;         ///< traced on that ROM, or assumed from another model
};

/// Every model, newest first. `count` receives how many.
const LsdjModel* const* lsdjModels(int& count);
/// The newest model: what a song of unknown format takes when no ROM is found.
const LsdjModel& lsdjLatestModel();
/// The model whose range holds the format, or nullptr when none does.
const LsdjModel* lsdjModelForFormat(int formatVersion);
/// The model a ROM's version string names ("9.3.9"), or nullptr.
const LsdjModel* lsdjModelForRomVersion(const char* version);
const LsdjModel* lsdjModelNamed(const char* name);

} // namespace chipboy::lsdj
