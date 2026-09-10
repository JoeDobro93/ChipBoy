// ChipBoy -- what a version of LSDj means by a song's bytes
// (docs/plan-lsdj-import.md section 3, docs/COMMANDS_AND_TEMPO.md section 56).
// Core: <std> only.
//
// A song's format version (byte 0x7FFF) picks a model; the model holds every
// table and rule the interpreter switches on. The parser in LsdjSong.cpp is
// shared: only what a version does with a byte differs.
//
// MEASURED (every stable release in the LSDj archive, booted with
// `lsdjref_trace --init-sav` for the format it writes, then probed with the
// same saves patched to that format; section 56 has the table):
//   format 0   3.1.5 - 3.5.1   shape noise, S in nibbles, NRx2 envelope,
//                              P/L/V in period-register units a pitch clock
//   format 2-3 3.6.8 - 5.0.3   the same, V already 9.x's
//   format 4-7 5.7.8 - 7.0.2   the same, P/L/V 9.x's, pitch modes
//   format 11  8.4.0 - 8.5.1   the same, the letter table with B
//   format 15  8.8.6           raw noise (FF - n), three-stage envelope
//   format 22  9.2.J - 9.4.2   the musical noise map, S in semitones
// Formats 1, 6, 8-10, 12-14 and 16-21 were never written by a stable release
// and take the nearest model below.
//
// ADDING A VERSION (the user supplies the ROM; the harness under tools/lsdjref
// traces it):
//   1. Boot the ROM with `lsdjref_trace --init-sav` (3000 frames) and read
//      byte 0x7FFF of the save it wrote: that is the format it writes. If a
//      model's range holds it, add the version to that model's name and to
//      kVersionFormats; else copy the nearest entry in LsdjModel.cpp, name it,
//      and give it that format and the range it should read.
//   2. Re-measure what may differ, one harness case each, and point the
//      entry at the new tables or rules: the command byte table (a phrase with
//      every letter, read which register each byte moves -- the harness's own
//      table is the one without B, patch the bytes for a ROM with B); the noise
//      rule (every note on a noise instrument with a known SHAPE byte, read
//      NR43); S on noise (one note, S values on later rows); the envelope (one
//      note, the speed patched 1..F, section 51) and whether it is the NRx2
//      byte or the three stages; P, V and L (section 56's probe: P01/P08,
//      V24/V48/V83, two notes with L); the wave octave (section 45); PU2 TSP
//      (section 49). Probe saves are built by copying the probe's 32 KB over
//      the ROM's own --init-sav save and keeping that save's byte 0x7FFF.
//   3. Set `measured` once the ROM traced it; leave it false for an entry
//      assumed from another version, so the dialog can say so.
//   4. Add a case to Tests/LsdjImportTests.cpp that reads a synthetic song
//      under the new model. docs/HANDOFF.md repeats these steps.
#pragma once

#include <cstdint>

namespace chipboy::lsdj {

/// How a noise note becomes NR43 (section 56).
enum class NoiseRule : uint8_t {
    Shape,      ///< ~SHAPE (instrument byte 4) + 16 x (5 - octave), saturating; formats 0-11
    Raw,        ///< FF - note byte; format 15
    Map         ///< the measured musical map, `noiseMap`; format 22
};
/// What S does on the noise channel.
enum class NoiseS : uint8_t {
    Nibbles,    ///< each nibble of NR43 less the matching nibble of xy, mod 16, adding up; formats 0-15
    Semitones   ///< int8(xy) semitones through the map, adding up (section 55); format 22
};
/// P and L's domain.
enum class PitchLaw : uint8_t {
    Register,   ///< P adds xx period units a pitch clock, L slides at xx units a clock; formats 0-3
    Semitone    ///< the 9.x laws the driver implements (section 7)
};
/// V's law.
enum class VibratoLaw : uint8_t {
    RegisterOneSided,   ///< a triangle below the note, 8y units a clock for x + 1 clocks; format 0
    Semitone            ///< 9.x's
};

struct LsdjModel {
    const char*    name;             ///< "LSDj 9.2.J - 9.4.2 (format 22)"
    int            formatVersion;    ///< the song format this version writes
    int            formatMin, formatMax;   ///< the formats this model reads, inclusive
    const char*    commandLetters;   ///< command byte -> letter; index 0 is none; '\0' ends it
    bool           stagedEnvelope;   ///< the three-stage software envelope (section 51); else byte 1 is NRx2
    const uint8_t* envPeriods;       ///< [16] pitch-clock periods per level, speeds 0-F (staged only)
    NoiseRule      noiseRule;
    const uint8_t* noiseMap;         ///< [128] MIDI note -> NR43 byte, valid for noiseLo..noiseHi (Map only)
    int            noiseLo, noiseHi; ///< the MIDI notes the map was measured for; outside, the interpreter folds by octaves
    NoiseS         noiseS;
    PitchLaw       pitchLaw;
    VibratoLaw     vibratoLaw;
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
/// The format an LSDj version writes ("4.7.3" -> 3), from the measured table;
/// a version between two measured ones takes the nearer below. -1 when the
/// string is not a version.
int lsdjFormatForVersion(const char* version);
/// The model a ROM's version string names ("9.3.9"), or nullptr.
const LsdjModel* lsdjModelForRomVersion(const char* version);
const LsdjModel* lsdjModelNamed(const char* name);

} // namespace chipboy::lsdj
