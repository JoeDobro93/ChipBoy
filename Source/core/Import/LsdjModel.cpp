// ChipBoy -- the LSDj models (docs/plan-lsdj-import.md section 3).
#include "core/Import/LsdjModel.h"

#include <cstring>
#include <string>

namespace chipboy::lsdj {

namespace {

// --- LSDj 9.3.9, song format 22: every table traced on the user's ROM ------

// Command bytes (docs/COMMANDS_AND_TEMPO.md section 44's finding): B sits at
// 2 and shifts every letter after it.
constexpr const char* kLetters9 = "-ABCDEFGHKLMOPRSTVWZ";
// The letters before B existed: the version-0 saves the harness writes are
// read with this table (measured on the same ROM).
constexpr const char* kLettersLegacy = "-ACDEFGHKLMOPRSTVWZ";

// Pitch-clock periods (11712 cycles, 2.79 ms) per level for envelope speeds
// 0-F (section 51). 0 holds.
constexpr uint8_t kEnvPeriods9[16] = { 0, 1, 2, 3, 4, 6, 8, 11, 15, 20, 27, 36, 48, 64, 86, 115 };

// The noise map: MIDI note -> NR43, measured note by note on a format-22 song
// for C-2 (36) to G-8 (115); above A-6 the values are 7-bit (bit 3). Outside
// that range the interpreter folds the note into it by octaves.
// Section 83: the table swept whole -- note byte 1 to 120, so **index 0 is note
// byte 1**, which is MIDI 36. Bytes 1-60 are the 15-bit half and 61-120 the
// 7-bit half; byte 121 and up read past the end on the ROM. The index wraps
// modulo 120, measured in both directions.
// Section 156: the ROM's own map, generated rather than measured. 9.4.2 keeps
// 120 bytes at bank 02:$5EE4: every (shift, divisor) pair the chip has with
// shift 0-13 and divisor 0-7 (0 counting as a half), sorted by the clock they
// give from slowest to fastest, two pairs with the same clock kept once as the
// one with the larger shift -- sixty entries, the 15-bit half -- and the same
// sixty with the width bit set as the 7-bit half. Checked byte for byte
// against the ROM. Index 0 is note byte 1 (MIDI 36) and the index wraps
// modulo 120 (section 83).
struct NoiseMap9 {
    uint8_t v[120] = {};
    constexpr NoiseMap9()
    {
        // The clock is 524288 / divisor / 2^(shift + 1): comparing
        // divisor * 2^(shift + 1), with divisor 0 as a half (so everything
        // doubled), orders the pairs the same way with no floating point.
        long long key[112] = {}; uint8_t byte[112] = {};
        int n = 0;
        for (int sh = 0; sh <= 13; ++sh)
            for (int dv = 0; dv <= 7; ++dv) {
                const long long k = (dv == 0 ? 1LL : 2LL * dv) << (sh + 1);   // larger = slower
                const uint8_t b = uint8_t((sh << 4) | dv);
                // Insertion sort: slowest first, and among equal clocks the larger shift first.
                int i = n;
                while (i > 0 && (key[i - 1] < k || (key[i - 1] == k && (byte[i - 1] >> 4) < sh))) { key[i] = key[i - 1]; byte[i] = byte[i - 1]; --i; }
                key[i] = k; byte[i] = b; ++n;
            }
        int out = 0;
        for (int i = 0; i < n && out < 60; ++i) {
            if (i > 0 && key[i] == key[i - 1]) continue;              // the same clock, kept once
            v[out] = byte[i]; v[60 + out] = uint8_t(byte[i] | 8); ++out;
        }
    }
};
constexpr NoiseMap9 kNoiseMap9{};
constexpr const uint8_t* kNoise9 = kNoiseMap9.v;
// The whole table swept on 9.3.9, note byte 1 to 120 (docs/LSDJ_COMMAND_MATRIX
// section 6.15): the 15-bit half is bytes 1-60 and the 7-bit half bytes 61-120,
// and byte 121 upward is off the end and reads as junk. Byte n is MIDI n + 35,
// so the table runs to **MIDI 155** and this array, indexed by MIDI note,
// cannot hold the last twenty-eight entries:
//
//   MIDI 128-155  4F 5B 4D 78 3F 4B 3D 68 2F 3B 2D 58
//                 1F 2B 1D 48 0F 1B 0D 38 0B 28 18 08
//
// They are why a save can ask for an NR43 the importer has no note for; the
// note would have to be carried as LSDj's own byte rather than as a MIDI note
// to reach them.

// --- LSDj 8.8.6, song format 15: traced on the user's ROM --------------------
// The envelope is already the three stages of 9.x (A -> 5 at speed 5, up to D
// at 7, down at 3, the same periods). The noise column is a raw register: note
// byte n writes NR43 = FF - n, so C-2 (36) is FE and G-8 (115) is AF. The
// letter table with B is in the ROM; its behaviour and the wave octave are
// taken from 9.x until traced.
constexpr uint8_t kNoise886[128] = {
    0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,               // 0-35 unmeasured
    0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8, 0xF7, 0xF6, 0xF5, 0xF4, 0xF3,                     // 36-47
    0xF2, 0xF1, 0xF0, 0xEF, 0xEE, 0xED, 0xEC, 0xEB, 0xEA, 0xE9, 0xE8, 0xE7,                     // 48-59
    0xE6, 0xE5, 0xE4, 0xE3, 0xE2, 0xE1, 0xE0, 0xDF, 0xDE, 0xDD, 0xDC, 0xDB,                     // 60-71
    0xDA, 0xD9, 0xD8, 0xD7, 0xD6, 0xD5, 0xD4, 0xD3, 0xD2, 0xD1, 0xD0, 0xCF,                     // 72-83
    0xCE, 0xCD, 0xCC, 0xCB, 0xCA, 0xC9, 0xC8, 0xC7, 0xC6, 0xC5, 0xC4, 0xC3,                     // 84-95
    0xC2, 0xC1, 0xC0, 0xBF, 0xBE, 0xBD, 0xBC, 0xBB, 0xBA, 0xB9, 0xB8, 0xB7,                     // 96-107
    0xB6, 0xB5, 0xB4, 0xB3, 0xB2, 0xB1, 0xB0, 0xAF, 0,0,0,0,                                    // 108-115, then unmeasured
    0,0,0,0,0,0,0,0
};

// --- 8.4.0 - 8.5.1 (format 11) and every release before: the SHAPE rule ----
// Traced on 3.1.5, 3.1.9, 3.4.4, 3.5.1, 3.6.8, 3.7.5, 3.8.7, 3.8.9, 3.9.2,
// 4.0.4, 4.1.0, 4.3.0, 4.4.0, 4.5.4, 4.6.0, 4.6.2, 4.6.9, 4.7.3, 4.8.0,
// 4.9.4, 5.0.3, 5.7.8, 5.8.8, 5.9.9, 6.0.1, 6.4.5, 6.8.2, 6.9.0, 7.0.2, 8.4.0,
// 8.4.4 and 8.5.1 (section 56): a noise note writes NR43 = ~SHAPE + 16 x
// (5 - octave), saturating, SHAPE being the instrument's byte 4; the note
// inside the octave is ignored. S on noise subtracts its nibbles from NR43's,
// each modulo 16, and adds up until the next note-on. The envelope byte is
// NRx2 itself. From 8.4.0 the letter table has B; before, it has not (3.1 has
// no Z either). Before 5.7.8 P and L work in period-register units a pitch
// clock and there are no pitch modes; before 3.6.8 V does too.

// The `DIST` lists (section 117): which curve each of byte 10's four table
// pages names. 9.2 pushed a new soft clip in at `D1` and moved the old ones
// down; `D0` and `D3` never changed.
constexpr KitDist kKitDist9[4]      = { KitDist::Clip, KitDist::Soft,  KitDist::Fold, KitDist::Wrap };
constexpr KitDist kKitDistLegacy[4] = { KitDist::Clip, KitDist::Fold,  KitDist::Fold2, KitDist::Wrap };

constexpr LsdjModel kLsdj94  { "LSDj 9.4.0 - 9.4.2 (format 22)",   22, 22, 31, kLetters9,      EnvelopeLaw::SoftwareStages, kEnvPeriods9, NoiseRule::Map,   kNoise9,   36, 155, NoiseS::Semitones, PitchLaw::Semitone, VibratoLaw::Semitone,         3, 2, 12, -12, true, 2, 0, true,  true,  true, true, false, true, true, false, true, kKitDist9, true, true, true, false, false, false, false, true, bank::VibScale::One, FineCmdLaw::Semitone32, true, NoiseStable::Byte2, false, true, bank::WaveWrite::PreTrigger };
constexpr LsdjModel kLsdj934 { "LSDj 9.3.4 - 9.3.9 (format 22)",   22, 22, 31, kLetters9,      EnvelopeLaw::SoftwareStages, kEnvPeriods9, NoiseRule::Map,   kNoise9,   36, 155, NoiseS::Semitones, PitchLaw::Semitone, VibratoLaw::Semitone,         3, 2, 12, -12, true, 2, 0, true,  true,  true, true, false, true, true, false, true, kKitDist9, true, false, false, false, false, false, false, true, bank::VibScale::One, FineCmdLaw::Semitone32, true, NoiseStable::Byte2, false, true, bank::WaveWrite::PreTrigger };
constexpr LsdjModel kLsdj92  { "LSDj 9.2.J - 9.3.3 (format 22)",   22, 22, 31, kLetters9,      EnvelopeLaw::SoftwareStages, kEnvPeriods9, NoiseRule::Map,   kNoise9,   36, 155, NoiseS::Semitones, PitchLaw::Semitone, VibratoLaw::Semitone,         3, 2, 12, -12, true, 2, 0, true,  true,  true, true, false, true, true, false, true, kKitDist9, false, false, false, false, false, false, false, true, bank::VibScale::One, FineCmdLaw::Semitone32, true, NoiseStable::Byte2, false, true, bank::WaveWrite::PreTrigger };
constexpr LsdjModel kLsdj886 { "LSDj 8.8.6 (format 15)",           15, 15, 21, kLetters9,      EnvelopeLaw::SoftwareStages, kEnvPeriods9, NoiseRule::Raw,   kNoise886, 36, 115, NoiseS::Nibbles,   PitchLaw::Semitone, VibratoLaw::Semitone,         2, 2, -1, -12, true, -1, 1, true,  true,  false, true, false, true, false, true, true, kKitDistLegacy, false, false, false, false, false, false, false, true, bank::VibScale::One, FineCmdLaw::Semitone32, true, NoiseStable::Byte2, false, true, bank::WaveWrite::Muted };
constexpr LsdjModel kLsdj84  { "LSDj 8.4.0 - 8.5.1 (format 11)",   11, 11, 14, kLetters9,      EnvelopeLaw::HardwareStages, nullptr, NoiseRule::Shape, nullptr,   36, 115, NoiseS::Nibbles,   PitchLaw::Semitone, VibratoLaw::Semitone,         2, 2, -1, -12, true, -1, 1, false, true,  false, true, false, true, false, true, true, kKitDistLegacy, false, false, false, true, false, false, false, true, bank::VibScale::One, FineCmdLaw::Semitone32, true, NoiseStable::Byte2, false, true, bank::WaveWrite::Muted };
constexpr LsdjModel kLsdj68  { "LSDj 6.8.2 - 7.2.3 (formats 7-8)",  7,  7,  8, kLettersLegacy, EnvelopeLaw::Chip,           nullptr, NoiseRule::Shape, nullptr,   36, 115, NoiseS::Nibbles,   PitchLaw::Semitone, VibratoLaw::Semitone,         2, 2, -1, -12, true, -1, 1, false, true,  false, false, false, true, false, true, true, kKitDistLegacy, false, false, false, true, false, true, true, true, bank::VibScale::One, FineCmdLaw::Semitone32, true, NoiseStable::Byte2, false, true, bank::WaveWrite::Muted };
constexpr LsdjModel kLsdj75  { "LSDj 7.5.4 - 7.7.5 (format 9)",    9,  9,  9, kLettersLegacy, EnvelopeLaw::Chip,           nullptr, NoiseRule::Shape, nullptr,   36, 115, NoiseS::Nibbles,   PitchLaw::Semitone, VibratoLaw::Semitone,         2, 2, -1, -12, true, -1, 1, false, true,  false, false, false, true, false, true, true, kKitDistLegacy, false, false, false, true, false, true, true, true, bank::VibScale::One, FineCmdLaw::Semitone32, true, NoiseStable::Byte2, false, true, bank::WaveWrite::Muted };
constexpr LsdjModel kLsdj79  { "LSDj 7.7.6 - 8.0.0 (format 10)",  10, 10, 10, kLettersLegacy, EnvelopeLaw::Chip,           nullptr, NoiseRule::Shape, nullptr,   36, 115, NoiseS::Nibbles,   PitchLaw::Semitone, VibratoLaw::Semitone,         2, 2, -1, -12, true, -1, 1, false, true,  false, false, false, true, false, true, true, kKitDistLegacy, false, false, false, true, false, false, false, true, bank::VibScale::One, FineCmdLaw::Semitone32, true, NoiseStable::Byte2, false, true, bank::WaveWrite::Muted };
constexpr LsdjModel kLsdj60  { "LSDj 6.0.1 - 6.4.5 (formats 4-6)", 4, 4, 6, kLettersLegacy, EnvelopeLaw::Chip,           nullptr, NoiseRule::Shape, nullptr,   36, 115, NoiseS::Nibbles,   PitchLaw::Semitone, VibratoLaw::Semitone,         2, 2, -1, -12, true, -1, 1, false, true,  false, false, false, false, false, true, true, kKitDistLegacy, false, false, false, true, false, false, false, true, bank::VibScale::One, FineCmdLaw::Semitone32, true, NoiseStable::Byte2, false, true, bank::WaveWrite::Muted };
constexpr LsdjModel kLsdj58  { "LSDj 5.8.8 - 5.9.9 (format 4)", 4, 4, 4, kLettersLegacy, EnvelopeLaw::Chip,           nullptr, NoiseRule::Shape, nullptr,   36, 115, NoiseS::Nibbles,   PitchLaw::Semitone, VibratoLaw::Semitone,         2, 2, -1, -12, true, -1, 1, false, true,  false, false, false, false, false, true, true, kKitDistLegacy, false, false, false, true, false, false, false, false, bank::VibScale::One, FineCmdLaw::Semitone32, true, NoiseStable::Byte2, false, true, bank::WaveWrite::Muted };
constexpr LsdjModel kLsdj57  { "LSDj 5.7.8 (format 4)", 4, 4, 4, kLettersLegacy, EnvelopeLaw::Chip,           nullptr, NoiseRule::Shape, nullptr,   36, 115, NoiseS::Nibbles,   PitchLaw::Semitone, VibratoLaw::Semitone,         2, 2, -1, -12, true, -1, 1, false, true,  false, false, false, false, false, true, true, kKitDistLegacy, false, false, false, true, false, false, false, false, bank::VibScale::Half, FineCmdLaw::Semitone32, true, NoiseStable::Byte2, false, true, bank::WaveWrite::Muted };
constexpr LsdjModel kLsdj503  { "LSDj 5.0.3 - 5.7.7 (format 3)", 3, 3, 3, kLettersLegacy, EnvelopeLaw::Chip,           nullptr, NoiseRule::Shape, nullptr,   36, 115, NoiseS::Nibbles,   PitchLaw::Register, VibratoLaw::Semitone,         2, 2, -1, -12, true, -1, 1, false, false, false, false, false, false, false, true, true, kKitDistLegacy, false, false, false, true, true, false, false, false, bank::VibScale::One, FineCmdLaw::Units, false, NoiseStable::Byte2, false, true, bank::WaveWrite::Muted };
constexpr LsdjModel kLsdj48  { "LSDj 4.8.0 - 4.9.4 (format 3)", 3, 3, 3, kLettersLegacy, EnvelopeLaw::Chip,           nullptr, NoiseRule::Shape, nullptr,   36, 115, NoiseS::Nibbles,   PitchLaw::Register, VibratoLaw::Semitone,         2, 2, -1, -12, true, -1, 1, false, false, false, false, false, false, false, true, true, kKitDistLegacy, false, false, false, true, true, false, false, false, bank::VibScale::One, FineCmdLaw::None, false, NoiseStable::Byte2, false, true, bank::WaveWrite::Muted };
constexpr LsdjModel kLsdj44  { "LSDj 4.4.0 - 4.6.9 (format 3)",     3,  3,  3, kLettersLegacy, EnvelopeLaw::Chip,           nullptr, NoiseRule::Shape, nullptr,   36, 115, NoiseS::Nibbles,   PitchLaw::Register, VibratoLaw::Semitone,         2, 2, -1, -12, true, -1, 1, true,  false, false, false, false, false, false, true, true, kKitDistLegacy, false, false, false, true, true, false, false, false, bank::VibScale::One, FineCmdLaw::None, false, NoiseStable::Byte2, false, true, bank::WaveWrite::Plain };
constexpr LsdjModel kLsdj473 { "LSDj 4.7.3 - 4.7.9 (format 3)",     3,  3,  3, kLettersLegacy, EnvelopeLaw::Chip,           nullptr, NoiseRule::Shape, nullptr,   36, 115, NoiseS::Nibbles,   PitchLaw::Register, VibratoLaw::Semitone,         2, 2, -1, -12, true, -1, 1, true,  false, false, false, false, false, false, true, true, kKitDistLegacy, false, false, false, true, true, false, false, false, bank::VibScale::One, FineCmdLaw::None, false, NoiseStable::Byte2, false, true, bank::WaveWrite::Muted };
constexpr LsdjModel kLsdj41  { "LSDj 4.1.0 - 4.3.0 (format 2)", 2, 2, 2, kLettersLegacy, EnvelopeLaw::Chip,           nullptr, NoiseRule::Shape, nullptr,   36, 115, NoiseS::Nibbles,   PitchLaw::Register, VibratoLaw::Semitone,         2, 2, -1, -12, true, -1, 1, true,  false, false, false, false, false, false, true, true, kKitDistLegacy, false, false, false, true, true, false, false, false, bank::VibScale::One, FineCmdLaw::None, false, NoiseStable::Byte2, false, true, bank::WaveWrite::Plain };
constexpr LsdjModel kLsdj404  { "LSDj 4.0.4 (format 2)", 2, 2, 2, kLettersLegacy, EnvelopeLaw::Chip,           nullptr, NoiseRule::Shape, nullptr,   36, 115, NoiseS::Nibbles,   PitchLaw::Register, VibratoLaw::Semitone,         2, 2, -1, -12, true, -1, 1, true,  false, false, false, false, false, false, true, true, kKitDistLegacy, false, false, false, true, true, false, false, false, bank::VibScale::One, FineCmdLaw::None, false, NoiseStable::Free, false, true, bank::WaveWrite::Plain };
constexpr LsdjModel kLsdj37  { "LSDj 3.7.5 - 3.9.2 (format 2)", 2, 2, 2, kLettersLegacy, EnvelopeLaw::Chip,           nullptr, NoiseRule::Shape, nullptr,   36, 115, NoiseS::Nibbles,   PitchLaw::Register, VibratoLaw::Semitone,         2, 2, -1, -12, true, -1, 1, true,  false, false, false, true, false, false, true, true, kKitDistLegacy, false, false, false, true, true, false, false, false, bank::VibScale::One, FineCmdLaw::None, false, NoiseStable::Free, true, true, bank::WaveWrite::Plain };
constexpr LsdjModel kLsdj36  { "LSDj 3.6.5 - 3.7.4 (format 2)", 2, 2, 2, kLettersLegacy, EnvelopeLaw::Chip,           nullptr, NoiseRule::Shape, nullptr,   36, 115, NoiseS::Nibbles,   PitchLaw::Register, VibratoLaw::Semitone,         2, 2, -1, -12, true, -1, 1, true,  false, false, false, true, false, false, true, true, kKitDistLegacy, false, false, false, true, true, false, false, false, bank::VibScale::One, FineCmdLaw::None, false, NoiseStable::Free, true, true, bank::WaveWrite::Plain };
constexpr LsdjModel kLsdj31  { "LSDj 3.1.5 - 3.5.1 (format 0)",     0,  0,  1, kLettersLegacy, EnvelopeLaw::Chip,           nullptr, NoiseRule::Shape, nullptr,   36, 115, NoiseS::Nibbles,   PitchLaw::Register, VibratoLaw::RegisterOneSided, 2, 2, -1, -12, true, -1, 1, true,  false, false, false, true, false, false, true, true, kKitDistLegacy, false, false, false, false, false, false, false, false, bank::VibScale::One, FineCmdLaw::None, false, NoiseStable::Free, true, false, bank::WaveWrite::Plain };

constexpr const LsdjModel* kModels[] = { &kLsdj94, &kLsdj934, &kLsdj92, &kLsdj886, &kLsdj84, &kLsdj79, &kLsdj75, &kLsdj68, &kLsdj60, &kLsdj58, &kLsdj57, &kLsdj503, &kLsdj48, &kLsdj473, &kLsdj44, &kLsdj41, &kLsdj404, &kLsdj37, &kLsdj36, &kLsdj31 };   // newest first

// The model each measured release follows, newest first. Finer than the format
// alone: two releases can write the same format byte and still read a song
// differently (docs/LSDJ_VERSIONS.md), and then only the ROM's own version
// string tells them apart. `lsdjModelForFormat` gives the default for a format
// -- the first entry in `kModels` whose range holds it.
struct VersionModel { int major, minor, patch; const LsdjModel* model; };
constexpr VersionModel kVersionModels[] = {
    { 9, 4, 0, &kLsdj94 }, { 9, 3, 4, &kLsdj934 }, { 9, 2, 0, &kLsdj92 }, { 8, 8, 6, &kLsdj886 }, { 8, 4, 0, &kLsdj84 }, { 7, 7, 6, &kLsdj79 }, { 7, 5, 4, &kLsdj75 }, { 6, 8, 2, &kLsdj68 }, { 6, 0, 1, &kLsdj60 }, { 5, 8, 8, &kLsdj58 }, { 5, 7, 8, &kLsdj57 },
    { 5, 0, 3, &kLsdj503 }, { 4, 8, 0, &kLsdj48 }, { 4, 7, 3, &kLsdj473 }, { 4, 4, 0, &kLsdj44 }, { 4, 1, 0, &kLsdj41 }, { 4, 0, 4, &kLsdj404 },
    { 3, 7, 5, &kLsdj37 }, { 3, 6, 5, &kLsdj36 }, { 3, 1, 5, &kLsdj31 },
};

// The format each measured release writes, newest first (section 56). A
// version between two entries takes the entry below it.
struct VersionFormat { int major, minor, patch; int format; };
constexpr VersionFormat kVersionFormats[] = {
    { 9, 4, 2, 22 }, { 9, 3, 9, 22 }, { 9, 2, 0, 22 },
    { 8, 8, 6, 15 },
    { 8, 5, 1, 11 }, { 8, 4, 4, 11 }, { 8, 4, 0, 11 },
    { 7, 0, 2, 7 }, { 6, 9, 0, 7 }, { 6, 8, 2, 7 },
    { 6, 4, 5, 5 },
    { 6, 0, 1, 4 }, { 5, 9, 9, 4 }, { 5, 8, 8, 4 }, { 5, 7, 8, 4 },
    { 5, 0, 3, 3 }, { 4, 9, 4, 3 }, { 4, 8, 0, 3 }, { 4, 7, 3, 3 }, { 4, 6, 9, 3 }, { 4, 6, 2, 3 }, { 4, 6, 0, 3 }, { 4, 5, 4, 3 }, { 4, 4, 0, 3 },
    { 4, 3, 0, 2 }, { 4, 1, 0, 2 }, { 4, 0, 4, 2 }, { 3, 9, 2, 2 }, { 3, 8, 9, 2 }, { 3, 8, 7, 2 }, { 3, 7, 5, 2 }, { 3, 6, 8, 2 }, { 3, 6, 5, 2 },
    { 3, 5, 1, 0 }, { 3, 4, 4, 0 }, { 3, 1, 9, 0 }, { 3, 1, 5, 0 },
};

/// "9.2.J" -> 9, 2, 19 (a letter patch counts from A = 10 so it sorts after
/// the digits); false when the string does not start with a version.
bool parseVersion(const char* v, int& major, int& minor, int& patch)
{
    if (v == nullptr) return false;
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (!digit(v[0]) || v[1] != '.' || !digit(v[2])) return false;
    major = v[0] - '0'; minor = v[2] - '0'; patch = 0;
    if (v[3] == '.' && v[4] != 0) {
        const char c = v[4];
        if (digit(c)) { patch = c - '0'; if (digit(v[5])) patch = patch * 10 + (v[5] - '0'); }
        else if (c >= 'A' && c <= 'Z') patch = 10 + (c - 'A');
        else if (c >= 'a' && c <= 'z') patch = 10 + (c - 'a');
    }
    return true;
}

} // namespace

const LsdjModel* const* lsdjModels(int& count) { count = int(sizeof(kModels) / sizeof(kModels[0])); return kModels; }
const LsdjModel& lsdjLatestModel() { return *kModels[0]; }

const LsdjModel* lsdjModelForFormat(int formatVersion)
{
    for (const LsdjModel* m : kModels) if (formatVersion >= m->formatMin && formatVersion <= m->formatMax) return m;
    return nullptr;
}

int lsdjFormatForVersion(const char* version)
{
    int major = 0, minor = 0, patch = 0;
    if (!parseVersion(version, major, minor, patch)) return -1;
    const long key = long(major) * 10000 + long(minor) * 100 + long(patch);
    for (const auto& e : kVersionFormats) {
        const long k = long(e.major) * 10000 + long(e.minor) * 100 + long(e.patch);
        if (key >= k) return e.format;
    }
    return kVersionFormats[sizeof(kVersionFormats) / sizeof(kVersionFormats[0]) - 1].format;   // older than the oldest measured
}

const LsdjModel* lsdjModelForRomVersion(const char* version)
{
    int major = 0, minor = 0, patch = 0;
    if (!parseVersion(version, major, minor, patch)) return nullptr;
    const long key = long(major) * 10000 + long(minor) * 100 + long(patch);
    for (const auto& e : kVersionModels)
        if (key >= long(e.major) * 10000 + long(e.minor) * 100 + long(e.patch)) return e.model;
    return kVersionModels[sizeof(kVersionModels) / sizeof(kVersionModels[0]) - 1].model;
}

const LsdjModel* lsdjModelNamed(const char* name)
{
    if (name == nullptr) return nullptr;
    for (const LsdjModel* m : kModels) if (std::strcmp(m->name, name) == 0) return m;
    return nullptr;
}

} // namespace chipboy::lsdj
