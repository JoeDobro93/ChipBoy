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
constexpr uint8_t kNoise9[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                       // 0-11 unmeasured
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                       // 12-23
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                       // 24-35
    0xD7, 0xD6, 0xD5, 0xD4, 0xC7, 0xD3, 0xC5, 0xD2, 0xB7, 0xC3, 0xB5, 0xD1,   // 36-47
    0xA7, 0xB3, 0xA5, 0xD0, 0x97, 0xA3, 0x95, 0xC0, 0x87, 0x93, 0x85, 0xB0,   // 48-59
    0x77, 0x83, 0x75, 0xA0, 0x67, 0x73, 0x65, 0x90, 0x57, 0x63, 0x55, 0x80,   // 60-71
    0x47, 0x53, 0x45, 0x70, 0x37, 0x43, 0x35, 0x60, 0x27, 0x33, 0x25, 0x50,   // 72-83
    0x17, 0x23, 0x15, 0x40, 0x07, 0x13, 0x05, 0x30, 0x03, 0x20, 0x10, 0x00,   // 84-95
    0xDF, 0xDE, 0xDD, 0xDC, 0xCF, 0xDB, 0xCD, 0xDA, 0xBF, 0xCB, 0xBD, 0xD9,   // 96-107 (7-bit)
    0xAF, 0xBB, 0xAD, 0xD8, 0x9F, 0xAB, 0x9D, 0xC8, 0, 0, 0, 0,               // 108-115, then unmeasured
    0, 0, 0, 0, 0, 0, 0, 0
};

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

// --- LSDj 8.4.0, song format 11: traced on the user's ROM --------------------
// The envelope byte is NRx2 itself (the chip's envelope). The noise map moves
// by octaves only: C-2 to B-5 write FF, C-6 to B-6 EF, C-7 to B-7 DF, C-8 up
// CF. The letter table with B is in the ROM.
constexpr uint8_t kNoise840[128] = {
    0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,   // 36-47
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,   // 48-59
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,   // 60-71
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,   // 72-83
    0xEF, 0xEF, 0xEF, 0xEF, 0xEF, 0xEF, 0xEF, 0xEF, 0xEF, 0xEF, 0xEF, 0xEF,   // 84-95
    0xDF, 0xDF, 0xDF, 0xDF, 0xDF, 0xDF, 0xDF, 0xDF, 0xDF, 0xDF, 0xDF, 0xDF,   // 96-107
    0xCF, 0xCF, 0xCF, 0xCF, 0xCF, 0xCF, 0xCF, 0xCF, 0,0,0,0,                  // 108-115
    0,0,0,0,0,0,0,0
};

// --- before 8.4: assumed -------------------------------------------------------
// Traced on the 9.3.9 ROM playing the harness's version-0 saves, which it
// reads with older rules: the command table without B and this noise map;
// the envelope byte is NRx2. Assumed for formats 0-10 until a ROM of each is
// measured (LsdjModel.h explains how).
constexpr uint8_t kNoiseLegacy[128] = {
    0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,
    0xDF, 0xDE, 0xDD, 0xDC, 0xDB, 0xDA, 0xD9, 0xD8, 0xD7, 0xD6, 0xD5, 0xD4,   // 36-47
    0xD3, 0xD2, 0xD1, 0xD0, 0xCF, 0xDB, 0xCD, 0xDA, 0xCB, 0xD9, 0xD8, 0xC8,   // 48-59
    0xC7, 0xD3, 0xC5, 0xD2, 0xC3, 0xD1, 0xD0, 0xC0, 0xBF, 0xCB, 0xBD, 0xD9,   // 60-71
    0xBB, 0xD8, 0xC8, 0xB8, 0xB7, 0xC3, 0xB5, 0xD1, 0xB3, 0xD0, 0xC0, 0xB0,   // 72-83
    0xAF, 0xDF, 0xDE, 0xDD, 0xDC, 0xDB, 0xDA, 0xD9, 0xD8, 0xD7, 0xD6, 0xD5,   // 84-95
    0xD4, 0xD3, 0xD2, 0xD1, 0xD0, 0xCF, 0xDB, 0xCD, 0xDA, 0xCB, 0xD9, 0xD8,   // 96-107
    0xC8, 0xC7, 0xD3, 0xC5, 0xD2, 0xC3, 0xD1, 0xD0, 0,0,0,0,
    0,0,0,0,0,0,0,0
};

constexpr LsdjModel kLsdj939 { "LSDj 9.2.J - 9.3.9 (format 22)", 22, 22, 31, kLetters9, true,  kEnvPeriods9, kNoise9,      36, 115, -12, true, true };
constexpr LsdjModel kLsdj886 { "LSDj 8.8.6 (format 15)",         15, 15, 21, kLetters9, true,  kEnvPeriods9, kNoise886,    36, 115, -12, true, false };
constexpr LsdjModel kLsdj840 { "LSDj 8.4.0 (format 11)",         11, 11, 14, kLetters9, false, nullptr,      kNoise840,    36, 115, -12, true, false };
constexpr LsdjModel kLegacy  { "LSDj before 8.4 (assumed)",       3,  0,  10, kLettersLegacy, false, nullptr, kNoiseLegacy, 36, 115, -12, true, false };

constexpr const LsdjModel* kModels[] = { &kLsdj939, &kLsdj886, &kLsdj840, &kLegacy };   // newest first

} // namespace

const LsdjModel* const* lsdjModels(int& count) { count = int(sizeof(kModels) / sizeof(kModels[0])); return kModels; }
const LsdjModel& lsdjLatestModel() { return *kModels[0]; }

const LsdjModel* lsdjModelForFormat(int formatVersion)
{
    for (const LsdjModel* m : kModels) if (formatVersion >= m->formatMin && formatVersion <= m->formatMax) return m;
    return nullptr;
}

const LsdjModel* lsdjModelForRomVersion(const char* version)
{
    if (version == nullptr || *version == 0) return nullptr;
    const std::string v = version;
    for (const LsdjModel* m : kModels) {
        const std::string name = m->name;
        if (name.find(v) != std::string::npos) return m;
    }
    // A version no model names: the nearest measured one below it.
    const int major = v[0] >= '0' && v[0] <= '9' ? v[0] - '0' : -1;
    const int minor = v.size() > 2 && v[2] >= '0' && v[2] <= '9' ? v[2] - '0' : 0;
    if (major >= 9) return &kLsdj939;
    if (major == 8) return minor >= 8 ? &kLsdj886 : minor >= 4 ? &kLsdj840 : &kLegacy;
    if (major >= 0) return &kLegacy;
    return nullptr;
}

const LsdjModel* lsdjModelNamed(const char* name)
{
    if (name == nullptr) return nullptr;
    for (const LsdjModel* m : kModels) if (std::strcmp(m->name, name) == 0) return m;
    return nullptr;
}

} // namespace chipboy::lsdj
