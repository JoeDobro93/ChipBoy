// ChipBoy -- the LSDj importer (docs/plan-lsdj-import.md section 6). Every
// save here is built in memory: no LSDj content enters the repository (L3).
#include "core/Import/LsdjModel.h"
#include "core/Import/LsdjSave.h"
#include "core/Import/LsdjSong.h"
#include "core/Driver/Driver.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

using namespace chipboy;
using namespace chipboy::lsdj;

namespace {

// The song layout, as LsdjSong.cpp reads it.
constexpr size_t kNotes = 0x0000, kGrooves = 0x1090, kRows = 0x1290, kTableEnv = 0x1690, kNames = 0x1E7A, kTableAlloc = 0x2020,
                 kInstAlloc = 0x2040, kChainPhrases = 0x2080, kChainTsp = 0x2880, kInst = 0x3080, kTableTsp = 0x3480,
                 kTableCmd1 = 0x3680, kTableCmd1V = 0x3880, kTableCmd2 = 0x3A80, kTableCmd2V = 0x3C80, kPhraseAlloc = 0x3E82, kTempo = 0x3FB4, kCmd = 0x4000, kCmdV = 0x4FF0,
                 kWaves = 0x6000, kPhraseInst = 0x7000;

/// A blank song image: the tables LSDj fills with FF are FF.
std::vector<uint8_t> blankSong(int format)
{
    std::vector<uint8_t> s(kSongSize, 0);
    std::memset(s.data() + kRows, 0xFF, 256 * 4);
    std::memset(s.data() + kChainPhrases, 0xFF, 128 * 16);
    std::memset(s.data() + kPhraseInst, 0xFF, 255 * 16);
    s[kTempo] = 165;
    s[kFormatVersionAt] = uint8_t(format);
    return s;
}

/// One song: a pulse lead with a three-stage envelope, a noise hat with a
/// transposing table, a wave bass; one phrase per channel; two song rows.
std::vector<uint8_t> testSong(int format)
{
    auto s = blankSong(format);
    // instrument 0: pulse, envelope A5 / 00 / D0 (A -> 0 at speed 5), duty 50 %, PU2 TSP +12
    s[kInstAlloc + 0] = 1;
    uint8_t* i0 = s.data() + kInst; i0[0] = 0; i0[1] = 0xA5; i0[2] = 12; i0[4] = 0xFF; i0[7] = 0x80 | 3; i0[9] = 0x00; i0[10] = 0xD0;
    std::memcpy(s.data() + kNames, "LEAD", 4);
    // instrument 1: noise, envelope 85, table 0 on
    s[kInstAlloc + 1] = 1;
    uint8_t* i1 = s.data() + kInst + 16; i1[0] = 3; i1[1] = 0x85; i1[6] = 0x20 | 0; i1[7] = 3; i1[10] = 0xD0;
    std::memcpy(s.data() + kNames + 5, "HAT", 3);
    // instrument 2: wave, level code 1 (100 %), synth 0
    s[kInstAlloc + 2] = 1;
    uint8_t* i2 = s.data() + kInst + 32; i2[0] = 1; i2[1] = 0x20; i2[3] = 0x00; i2[7] = 3;
    std::memcpy(s.data() + kNames + 10, "BASS", 4);
    // synth 0's first wave: a ramp
    for (int k = 0; k < 16; ++k) s[kWaves + size_t(k)] = uint8_t((k << 4) | k);
    // table 0: row 0 transposes -12, row 1 +7 with a K, row 2 volume 5
    s[kTableAlloc + 0] = 1;
    s[kTableTsp + 0] = 0xF4; s[kTableTsp + 1] = 7; s[kTableEnv + 2] = 0x54;          // amplitude 5, four ticks (section 64)
    // K is byte 9 in format 22 (B at 2), byte 8 before it
    s[kTableCmd1 + 1] = uint8_t(format >= 20 ? 9 : 8); s[kTableCmd1V + 1] = 2;
    // phrase 0 (PU1): C-4 with the lead and P -3 at step 4, A table 0 at step 8; phrase 1 (NOI): hats; phrase 2 (WAV): a note
    auto alloc = [&](int p) { s[kPhraseAlloc + size_t(p / 8)] |= uint8_t(1 << (p % 8)); };
    alloc(0); alloc(1); alloc(2);
    s[kNotes + 0] = 60 - 35; s[kPhraseInst + 0] = 0;
    s[kNotes + 4] = 62 - 35; s[kCmd + 4] = uint8_t(format >= 20 ? 13 : 12); s[kCmdV + 4] = 0xFD;   // P -3
    s[kNotes + 8] = 64 - 35; s[kCmd + 8] = 1; s[kCmdV + 8] = 0;                                      // A00: table 0
    s[kNotes + 16 + 0] = 93 - 35; s[kPhraseInst + 16 + 0] = 1;                                       // the hat on A-6 (MIDI 93 -> NR43 20)
    s[kNotes + 32 + 0] = 48 - 35; s[kPhraseInst + 32 + 0] = 2;                                       // the bass on C-3
    // chains: chain 0 = phrase 0 twice, the second row transposed +5; chain 1 = phrase 1; chain 2 = phrase 2
    s[kChainPhrases + 0] = 0; s[kChainPhrases + 1] = 0; s[kChainTsp + 1] = 5;
    s[kChainPhrases + 16] = 1;
    s[kChainPhrases + 32] = 2;
    // two song rows: PU1 chain 0, PU2 none, WAV chain 2, NOI chain 1; then chain 0 alone
    s[kRows + 0] = 0; s[kRows + 1] = 0xFF; s[kRows + 2] = 2; s[kRows + 3] = 1;
    s[kRows + 4] = 0; s[kRows + 5] = 0xFF; s[kRows + 6] = 0xFF; s[kRows + 7] = 0xFF;
    // groove 0: 7 5
    s[kGrooves + 0] = 7; s[kGrooves + 1] = 5;
    return s;
}

/// A small compressor: literals with the two escapes, runs of 4 or more,
/// the defaults where the bytes match them, a jump at each block's end.
struct SaveWriter {
    std::vector<uint8_t> save = std::vector<uint8_t>(kSaveSize, 0);
    int nextBlock = 1;
    SaveWriter() { std::memset(save.data() + 0x8141, 0xFF, 191); save[0x8140] = 0xFF; }
    void setWorking(const std::vector<uint8_t>& song) { std::memcpy(save.data(), song.data(), kSongSize); }
    void addFile(int file, const char* name, const std::vector<uint8_t>& song, bool active = false)
    {
        std::memcpy(save.data() + 0x8000 + size_t(file) * 8, name, std::min<size_t>(8, std::strlen(name)));
        if (active) save[0x8140] = uint8_t(file);
        std::vector<uint8_t> out;
        const uint8_t defWave[16] = { 0x8E, 0xCD, 0xCC, 0xBB, 0xAA, 0xA9, 0x99, 0x88, 0x87, 0x76, 0x66, 0x55, 0x54, 0x43, 0x32, 0x31 };
        const uint8_t defInst[16] = { 0xA8, 0, 0, 0xFF, 0, 0, 3, 0, 0, 0xD0, 0, 0, 0, 0xF3, 0, 0 };
        for (size_t i = 0; i < song.size();) {
            if (i + 16 <= song.size() && std::memcmp(song.data() + i, defWave, 16) == 0) { out.insert(out.end(), { 0xE0, 0xF0, 1 }); i += 16; continue; }
            if (i + 16 <= song.size() && std::memcmp(song.data() + i, defInst, 16) == 0) { out.insert(out.end(), { 0xE0, 0xF1, 1 }); i += 16; continue; }
            size_t run = 1;
            while (i + run < song.size() && song[i + run] == song[i] && run < 255) ++run;
            if (run >= 4) { out.insert(out.end(), { 0xC0, song[i], uint8_t(run) }); i += run; continue; }
            if (song[i] == 0xC0) out.insert(out.end(), { 0xC0, 0xC0 });
            else if (song[i] == 0xE0) out.insert(out.end(), { 0xE0, 0xE0 });
            else out.push_back(song[i]);
            ++i;
        }
        out.insert(out.end(), { 0xE0, 0xFF });
        // into blocks of 512, each ending with a jump to the next
        size_t pos = 0;
        while (pos < out.size()) {
            const int block = nextBlock++;
            REQUIRE(block <= kBlockCount);
            save[0x8141 + size_t(block - 1)] = uint8_t(file);
            uint8_t* dst = save.data() + 0x8000 + size_t(block) * 0x200;
            const size_t room = 0x200 - 2;                   // the jump, or the end marker inside
            size_t take = std::min(room, out.size() - pos);
            // never split a code: back up to a byte that starts one -- the
            // stream here is short enough that ending on a literal is safe
            // when we stop before an escape byte
            while (take > 1 && (out[pos + take - 1] == 0xC0 || out[pos + take - 1] == 0xE0)) --take;
            std::memcpy(dst, out.data() + pos, take);
            pos += take;
            if (pos < out.size()) { dst[take] = 0xE0; dst[take + 1] = uint8_t(nextBlock); }
        }
    }
};

/// Sections 81 and 83: under LSDj's own table the cell carries the table's
/// **entry number**, re-based onto ChipBoy's keyboard, and the driver wraps the
/// index. The test is the strong one -- the byte that will reach NR43 is the
/// byte the ROM writes -- and it reads the index exactly as the driver does.
bool noiseByteMatches(const bank::Bank& bank, const tracker::Phrase& p, int cell, uint8_t nr43)
{
    const auto& c = p.cells[size_t(cell)];
    if (c.inst < 1 || !bank.noiseMapSet || bank.noiseMapLen == 0) return false;
    if (!bank.instruments[size_t(c.inst - 1)].noiseLsdjMap) return false;
    const int len = int(bank.noiseMapLen);
    const int idx = ((int(c.note) - int(bank.noiseMapNote0)) % len + len) % len;
    return bank.noiseMap[size_t(idx)] == nr43;
}

} // namespace

TEST_CASE("the save's file table and its compressed files are read back", "[lsdj]")
{
    const auto song22 = testSong(22), song3 = testSong(3);
    auto w = std::make_unique<SaveWriter>();
    w->setWorking(song22);
    w->addFile(0, "NEWSONG", song22, true);
    w->addFile(5, "OLDSONG", song3);
    SaveIndex idx; std::string err;
    REQUIRE(indexSave(w->save.data(), w->save.size(), idx, err));
    REQUIRE(idx.files.size() == 2);
    CHECK(idx.files[0].name == "NEWSONG"); CHECK(idx.files[0].file == 0); CHECK(idx.files[0].formatVersion == 22); CHECK(idx.files[0].active);
    CHECK(idx.files[1].name == "OLDSONG"); CHECK(idx.files[1].file == 5); CHECK(idx.files[1].formatVersion == 3); CHECK_FALSE(idx.files[1].active);
    CHECK(idx.activeFile == 0); CHECK(idx.workingFormat == 22); CHECK(idx.workingUsed);
    std::vector<uint8_t> back;
    REQUIRE(decompressFile(w->save.data(), w->save.size(), 5, back, err));
    CHECK(back == song3);
    REQUIRE(decompressFile(w->save.data(), w->save.size(), 0, back, err));
    CHECK(back == song22);
    CHECK_FALSE(decompressFile(w->save.data(), w->save.size(), 7, back, err));   // no such file
    // A jump outside the save fails with a message rather than reading past it.
    auto broken = w->save;
    broken[0x8000 + 0x200 + 0x1FF] = 0xF5; broken[0x8000 + 0x200 + 0x1FE] = 0xE0;
    CHECK_FALSE(decompressFile(broken.data(), broken.size(), 0, back, err));
    CHECK_FALSE(err.empty());
    // Not a save at all.
    std::vector<uint8_t> tiny(100, 0);
    CHECK_FALSE(indexSave(tiny.data(), tiny.size(), idx, err));
}

TEST_CASE("the default codes expand to the default wave and instrument", "[lsdj]")
{
    auto song = blankSong(22);
    const uint8_t defWave[16] = { 0x8E, 0xCD, 0xCC, 0xBB, 0xAA, 0xA9, 0x99, 0x88, 0x87, 0x76, 0x66, 0x55, 0x54, 0x43, 0x32, 0x31 };
    const uint8_t defInst[16] = { 0xA8, 0, 0, 0xFF, 0, 0, 3, 0, 0, 0xD0, 0, 0, 0, 0xF3, 0, 0 };
    for (int k = 0; k < 4; ++k) std::memcpy(song.data() + kWaves + size_t(k) * 16, defWave, 16);
    std::memcpy(song.data() + kInst, defInst, 16);
    song[0x100] = 0xC0; song[0x101] = 0xE0;                        // the two escaped literals
    auto w = std::make_unique<SaveWriter>();
    w->addFile(1, "DEFAULTS", song);
    std::vector<uint8_t> back; std::string err;
    REQUIRE(decompressFile(w->save.data(), w->save.size(), 1, back, err));
    CHECK(back == song);
}

TEST_CASE("a model is chosen by format, by ROM title, by name, or the newest", "[lsdj]")
{
    int n = 0; const auto* const* models = lsdjModels(n);
    REQUIRE(n == 6);
    CHECK(std::string(lsdjLatestModel().name).find("9.4.2") != std::string::npos);
    CHECK(lsdjModelForFormat(22) == models[0]);
    CHECK(lsdjModelForFormat(15)->formatVersion == 15);          // 8.8.6, measured
    CHECK(lsdjModelForFormat(18)->formatVersion == 15);          // no release wrote it: the nearest below
    CHECK(lsdjModelForFormat(11)->formatVersion == 11);          // 8.4.x, 8.5.1
    CHECK(lsdjModelForFormat(7)->pitchLaw == PitchLaw::Semitone); CHECK(lsdjModelForFormat(7)->noiseRule == NoiseRule::Shape);
    CHECK(lsdjModelForFormat(3)->pitchLaw == PitchLaw::Register); CHECK(lsdjModelForFormat(3)->vibratoLaw == VibratoLaw::Semitone);
    CHECK(lsdjModelForFormat(0)->vibratoLaw == VibratoLaw::RegisterOneSided);
    CHECK(lsdjModelForFormat(1) == lsdjModelForFormat(0));
    for (int i = 0; i < n; ++i) CHECK(models[i]->measured);
    CHECK(lsdjModelForFormat(99) == nullptr);
    // A version string to the format it writes: the measured table, the nearer below between entries.
    CHECK(lsdjFormatForVersion("9.3.9") == 22); CHECK(lsdjFormatForVersion("9.2.J") == 22); CHECK(lsdjFormatForVersion("9.4.2") == 22);
    CHECK(lsdjFormatForVersion("8.8.6") == 15); CHECK(lsdjFormatForVersion("8.4.4") == 11); CHECK(lsdjFormatForVersion("8.6.0") == 11);
    CHECK(lsdjFormatForVersion("7.0.2") == 7); CHECK(lsdjFormatForVersion("6.4.5") == 5); CHECK(lsdjFormatForVersion("6.2.0") == 4);
    CHECK(lsdjFormatForVersion("5.0.3") == 3); CHECK(lsdjFormatForVersion("4.7.3") == 3); CHECK(lsdjFormatForVersion("4.3.0") == 2);
    CHECK(lsdjFormatForVersion("3.5.1") == 0); CHECK(lsdjFormatForVersion("3.1.5") == 0); CHECK(lsdjFormatForVersion("2.0.0") == 0);
    CHECK(lsdjFormatForVersion("nonsense") == -1);
    CHECK(lsdjModelForRomVersion("9.3.9") == models[0]);
    CHECK(lsdjModelForRomVersion("8.8.6")->formatVersion == 15);
    CHECK(lsdjModelForRomVersion("8.4.0")->formatVersion == 11);
    CHECK(lsdjModelForRomVersion("4.7.3") == lsdjModelForFormat(3));
    CHECK(lsdjModelForRomVersion("7.0.2") == lsdjModelForFormat(7));
    CHECK(lsdjModelForRomVersion("") == nullptr);
    CHECK(lsdjModelNamed(models[0]->name) == models[0]);
    CHECK(lsdjModelNamed("nothing") == nullptr);
    // The ROM's cartridge title names the version.
    std::vector<uint8_t> rom(0x150, 0);
    std::memcpy(rom.data() + 0x134, "LSDj-v9.3.9", 11);
    CHECK(romVersion(rom.data(), rom.size()) == "9.3.9");
    std::memcpy(rom.data() + 0x134, "TETRIS\0\0\0\0\0", 11);
    CHECK(romVersion(rom.data(), rom.size()).empty());
    // Before 4.3 the title is "LSDJ" and the welcome line carries the version.
    std::vector<uint8_t> old(0x2000, 0);
    std::memcpy(old.data() + 0x134, "LSDJ", 4);
    std::memcpy(old.data() + 0x9F2, "WELCOME TO LITTLE SOUND DJ V3.5.1!", 34);
    CHECK(romVersion(old.data(), old.size()) == "3.5.1");
}

TEST_CASE("a format-22 song imports its instruments, tables, phrases and chains", "[lsdj]")
{
    const auto song = testSong(22);
    auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;
    REQUIRE(importSong(song.data(), song.size(), lsdjLatestModel(), *bank, *out, sum, notes));
    CHECK(sum.instruments == 3); CHECK(sum.tables == 1); CHECK(sum.waves == 1); CHECK(sum.rows == 2);
    CHECK(out->tempoBpm == 165.0);
    // The lead: a staged envelope A -> 0 at speed 5 is 10 levels x 6 periods x 2.79 ms
    // = 167 ms = 11 ticks at 165 BPM (section 51); PU2 TSP is the instrument's.
    const auto& lead = bank->instruments[0];
    CHECK(lead.used); CHECK(lead.name == "LEAD"); CHECK(lead.type == bank::InstrumentType::Pulse);
    CHECK(lead.env.mode == bank::EnvMode::Shaped); CHECK(lead.env.start == 10); CHECK(lead.env.attackTicks == 11); CHECK(lead.env.peak == 0);
    CHECK(lead.env.decayTicks == 0); CHECK(lead.env.sustain == 0); CHECK(lead.env.fadeTicks == 0);
    CHECK(lead.duty == 2); CHECK(lead.pu2Transpose == 12); CHECK(lead.pan == bank::Pan::Both);
    // The hat: noise, table 1 (LSDj table 0), 15-bit; its note A-6 lands on the ChipBoy note with NR43 20's clock.
    const auto& hat = bank->instruments[1];
    CHECK(hat.type == bank::InstrumentType::Noise); CHECK(hat.table == 1); CHECK_FALSE(hat.lfsr7);
    CHECK(hat.env.mode == bank::EnvMode::Shaped); CHECK(hat.env.start == 8); CHECK(hat.env.attackTicks == 9);
    // The bass: wave level 100 %, wave slot 1 holding synth 0's ramp.
    const auto& bass = bank->instruments[2];
    CHECK(bass.type == bank::InstrumentType::Wave); CHECK(bass.waveLevel == 3); CHECK(bass.wave == 1);
    REQUIRE(bank->waves[0].frames.size() == 16);
    CHECK(bank->waves[0].frames[0].s[0] == 0); CHECK(bank->waves[0].frames[0].s[3] == 1); CHECK(bank->waves[0].frames[0].s[31] == 15);
    // The table: transposes for the hat, converted through the clocks, the K at row 1, the volume at row 2.
    const auto& t = bank->tables[0];
    REQUIRE(t.used);
    CHECK(t.steps[0].hasTranspose); CHECK(t.steps[1].hasTranspose); CHECK(t.steps[1].cmd1.cmd == bank::Cmd::K); CHECK(t.steps[1].cmd1.a == 2);
    CHECK(t.steps[2].vol == 5); CHECK(int(t.steps[2].volTicks) == 4); CHECK(t.steps[3].vol == -1);
    // Phrases: PU1's copy has C-4 with the lead, D-4 with P FD, E-4 with the table in its TBL column.
    const auto* p1 = out->phrase(1);
    REQUIRE(p1 != nullptr);
    CHECK(p1->cells[0].note == 60); CHECK(p1->cells[0].inst == 1);
    CHECK(p1->cells[4].note == 62); CHECK(p1->cells[4].cmd1.cmd == bank::Cmd::P); CHECK(p1->cells[4].cmd1.a == 0xFD);
    CHECK(p1->cells[8].note == 64); CHECK(p1->cells[8].table == 1); CHECK(p1->cells[8].cmd1.cmd == bank::Cmd::None);
    // The wave note sits an octave under its name (section 45); the noise note goes through the map.
    bool foundWave = false, foundNoise = false;
    for (int ch = 0; ch < 4; ++ch) for (uint8_t slot : out->chain[size_t(ch)]) {
        const auto* p = out->phrase(slot);
        if (ch == 2 && p) { foundWave = true; CHECK(p->cells[0].note == 48 - 12); }
        if (ch == 3 && p) { foundNoise = true; CHECK(noiseByteMatches(*bank, *p, 0, 0x20)); }   // section 81
    }
    CHECK(foundWave); CHECK(foundNoise);
    // Chains: chain 0 is the phrase twice, the second transposed +5, and PU1 plays it in both song rows; PU2 is empty (a note).
    REQUIRE(out->chain[0].size() == 4);
    CHECK(out->chain[0][0] == out->chain[0][1]); CHECK(out->chain[0][2] == out->chain[0][0]);
    CHECK(out->transposeAt(0, 0) == 0); CHECK(out->transposeAt(0, 1) == 5); CHECK(out->transposeAt(0, 3) == 5);
    CHECK(out->chain[1].empty());
    CHECK(out->grooves[0].ticks[0] == 7); CHECK(out->grooves[0].ticks[1] == 5); CHECK(out->grooves[0].ticks[2] == 0);
    CHECK(out->grooves[1].ticks[0] == 6);
    for (auto src : out->noteSource) CHECK(src == tracker::NoteSource::Tracker);
    bool noted = false;
    for (const auto& l : notes.lines) if (l.find("PU2 TSP") != std::string::npos) noted = true;
    CHECK(noted);
}

namespace {
double noiseClockOf(uint8_t nr43) { const int d = nr43 & 7; return 524288.0 / (d == 0 ? 0.5 : double(d)) / double(1u << ((nr43 >> 4) + 1)); }
/// Whether the cell's note, through its instrument's Shift, is the ChipBoy
/// note nearest the LSDj byte's clock and on the keyboard (12 or above).
bool noiseClockMatches(const bank::Bank& bank, const tracker::Phrase& p, int cell, uint8_t nr43)
{
    const auto& c = p.cells[size_t(cell)];
    if (c.inst < 1 || c.note < 12) return false;
    const int off = int(bank.instruments[size_t(c.inst - 1)].noiseShift) - 5;
    const int want = std::max(12, chipboyNoteForClock(noiseClockOf(nr43) * std::pow(2.0, off), 60));
    return c.note == want;
}
} // namespace

TEST_CASE("the same bytes under the older models take their own envelope, letters and noise", "[lsdj]")
{
    SECTION("before 8.4: the letters without B and the hardware envelope") {
        const auto song = testSong(3);
        auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
        ImportSummary sum; ImportNotes notes;
        REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(3), *bank, *out, sum, notes));
        const auto& lead = bank->instruments[0];
        CHECK(lead.env.mode == bank::EnvMode::Chip); CHECK(lead.envVol == 10); CHECK(lead.envRate == 5); CHECK(lead.envDir == bank::EnvDir::Down);
        const auto* p1 = out->phrase(1);
        REQUIRE(p1 != nullptr);
        CHECK(p1->cells[4].cmd1.cmd == bank::Cmd::P);           // byte 12 is P without B in the table
        CHECK(bank->tables[0].steps[1].cmd1.cmd == bank::Cmd::K);   // byte 8 is K
    }
    SECTION("8.4.0, format 11: three stages the chip ramps between, B in the table, the SHAPE noise rule") {
        const auto song = testSong(22);                              // the same bytes: byte 9 kills, byte 13 bends
        auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
        ImportSummary sum; ImportNotes notes;
        REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(11), *bank, *out, sum, notes));
        // Section 58: byte 1 = A5 is amplitude 10 falling at period 5 and byte 9 = 00
        // is amplitude 0, so the chip's ramp decays it to silence over ten levels.
        const auto& env11 = bank->instruments[0].env;
        CHECK(env11.mode == bank::EnvMode::Shaped);
        CHECK(int(env11.start) == 10); CHECK(int(env11.peak) == 0); CHECK(int(env11.sustain) == 0);
        CHECK(int(env11.attackTicks) == int(std::lround(10 * 5 * 1000.0 / 64.0 / (60000.0 / (165.0 * 24.0)))));
        CHECK(out->phrase(1)->cells[4].cmd1.cmd == bank::Cmd::P);
        bool found = false;
        for (uint8_t slot : out->chain[3]) if (const auto* p = out->phrase(slot)) { found = true; CHECK(noiseClockMatches(*bank, *p, 0, 0xEF)); }   // A-6 writes EF on 8.4.0: a 2.3 Hz click, the deepest the Shift reaches
        CHECK(found);
    }
    SECTION("8.8.6, format 15: the three stages and the raw noise column") {
        const auto song = testSong(22);
        auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
        ImportSummary sum; ImportNotes notes;
        REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(15), *bank, *out, sum, notes));
        CHECK(bank->instruments[0].env.mode == bank::EnvMode::Shaped); CHECK(bank->instruments[0].env.attackTicks == 11);
        bool found = false;
        for (uint8_t slot : out->chain[3]) if (const auto* p = out->phrase(slot)) { found = true; CHECK(noiseClockMatches(*bank, *p, 0, uint8_t(0xFF - (93 - 35)))); }   // A-6 is note byte 58: FF - 58
        CHECK(found);
    }
}

namespace {
/// A song for the older formats (section 56): a pulse lead, a noise drum with
/// SHAPE E3 and a table of S rows, two phrases -- the noise one with S on the
/// note's row and the next, the pulse one with L and P and V.
std::vector<uint8_t> oldSong(int format)
{
    auto s = blankSong(format);
    const bool withB = format >= 11;
    const auto letter = [withB](char c) { const char* t = withB ? "-ABCDEFGHKLMOPRSTVWZ" : "-ACDEFGHKLMOPRSTVWZ"; return uint8_t(std::strchr(t, c) - t); };
    s[kInstAlloc + 0] = 1;
    uint8_t* i0 = s.data() + kInst; i0[0] = 0; i0[1] = 0xA5; i0[4] = 0xFF; i0[7] = 0x80 | 3;
    s[kInstAlloc + 1] = 1;
    uint8_t* i1 = s.data() + kInst + 16; i1[0] = 3; i1[1] = 0x51; i1[4] = 0xE3; i1[7] = 3;          // SHAPE E3
    s[kInstAlloc + 2] = 1;
    uint8_t* i2 = s.data() + kInst + 32; i2[0] = 3; i2[1] = 0x51; i2[4] = 0xE3; i2[6] = 0x20 | 0; i2[7] = 3;   // the same, table 0 on
    // table 0: S07 at row 1, S10 at row 2
    s[kTableAlloc + 0] = 1;
    s[kTableCmd1 + 1] = letter('S'); s[kTableCmd1V + 1] = 0x07;
    s[kTableCmd1 + 2] = letter('S'); s[kTableCmd1V + 2] = 0x10;
    auto alloc = [&](int p) { s[kPhraseAlloc + size_t(p / 8)] |= uint8_t(1 << (p % 8)); };
    alloc(0); alloc(1);
    // phrase 0 (NOI): C-4 with the drum and SF1, SF1 on the next row; C-5 with the tabled drum at step 8
    s[kNotes + 0] = 60 - 35; s[kPhraseInst + 0] = 1; s[kCmd + 0] = letter('S'); s[kCmdV + 0] = 0xF1;
    s[kCmd + 1] = letter('S'); s[kCmdV + 1] = 0xF1;
    s[kNotes + 8] = 72 - 35; s[kPhraseInst + 8] = 2;
    // phrase 1 (PU1): C-4 with the lead; E-4 with L04 at step 2; P08 at step 4; V24 at step 6
    s[kNotes + 16 + 0] = 60 - 35; s[kPhraseInst + 16 + 0] = 0;
    s[kNotes + 16 + 2] = 64 - 35; s[kCmd + 16 + 2] = letter('L'); s[kCmdV + 16 + 2] = 0x04;
    s[kCmd + 16 + 4] = letter('P'); s[kCmdV + 16 + 4] = 0x08;
    s[kCmd + 16 + 6] = letter('V'); s[kCmdV + 16 + 6] = 0x24;
    s[kChainPhrases + 0] = 1;                       // chain 0 = phrase 1 (PU1)
    s[kChainPhrases + 16] = 0;                      // chain 1 = phrase 0 (NOI)
    s[kRows + 0] = 0; s[kRows + 1] = 0xFF; s[kRows + 2] = 0xFF; s[kRows + 3] = 1;
    return s;
}
int semitoneOf(const bank::Command& c) { return int(int8_t(uint8_t((c.a << 4) | c.b))); }
} // namespace

TEST_CASE("format 11's envelope is three stages the chip ramps between", "[lsdj]")
{
    // Section 58: byte 1 goes to NRx2, and each stage hands over when the chip's
    // ramp reaches the next amplitude, one level every (period / 64) of a second.
    auto song = blankSong(11);
    song[kInstAlloc + 0] = 1;
    uint8_t* i0 = song.data() + kInst;
    i0[0] = 0; i0[1] = 0x1F; i0[9] = 0x47; i0[10] = 0x20; i0[4] = 0xFF; i0[7] = 0x80 | 3;   // 1 up at 7, 4 down at 7, hold at 2
    song[kPhraseAlloc] |= 1; song[kNotes] = 60 - 35; song[kPhraseInst] = 0;
    song[kChainPhrases] = 0; song[kRows + 0] = 0; song[kRows + 1] = 0xFF; song[kRows + 2] = 0xFF; song[kRows + 3] = 0xFF;
    song[kTempo] = 120;
    auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;
    REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(11), *bank, *out, sum, notes));
    const auto& e = bank->instruments[0].env;
    REQUIRE(e.mode == bank::EnvMode::Shaped);
    const double tickMs = 60000.0 / (120.0 * 24.0);
    CHECK(int(e.start) == 1); CHECK(int(e.peak) == 4); CHECK(int(e.sustain) == 2);
    CHECK(int(e.attackTicks) == int(std::lround(3 * 7 * 1000.0 / 64.0 / tickMs)));   // 1 -> 4 at period 7
    CHECK(int(e.decayTicks) == int(std::lround(2 * 7 * 1000.0 / 64.0 / tickMs)));    // 4 -> 2 at period 7
    CHECK(int(e.fadeTicks) == 0);
    // A direction that cannot reach the next amplitude never hands over.
    i0[1] = 0x1F; i0[9] = 0x00;                                                       // rising from 1, target 0
    REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(11), *bank, *out, sum, notes));
    CHECK(bank->instruments[0].env.mode == bank::EnvMode::Chip);
    CHECK(bank->instruments[0].envRate == 7);
    // Section 59: E re-attacks on every format through 11, and not from 15 up.
    CHECK(bank->instruments[0].envRetrig);
    // Formats 0 to 7 ignore bytes 9 and 10 altogether.
    auto old7 = blankSong(7); std::memcpy(old7.data(), song.data(), song.size());
    old7[kFormatVersionAt] = 7;
    old7[kInst + 1] = 0x1F; old7[kInst + 9] = 0x47; old7[kInst + 10] = 0x20;
    REQUIRE(importSong(old7.data(), old7.size(), *lsdjModelForFormat(7), *bank, *out, sum, notes));
    CHECK(bank->instruments[0].env.mode == bank::EnvMode::Chip);
    CHECK(bank->instruments[0].envVol == 1); CHECK(bank->instruments[0].envRate == 7);
    CHECK(bank->instruments[0].envRetrig);
    // From 8.8.6 on, E is zombie mode and never triggers.
    auto new15 = blankSong(15); std::memcpy(new15.data(), song.data(), song.size());
    new15[kFormatVersionAt] = 15;
    REQUIRE(importSong(new15.data(), new15.size(), *lsdjModelForFormat(15), *bank, *out, sum, notes));
    CHECK_FALSE(bank->instruments[0].envRetrig);
}

TEST_CASE("the formats before 9 read the noise SHAPE and resolve S to the note it lands on", "[lsdj]")
{
    // Section 56: NR43 = ~SHAPE + 16 x (5 - octave); S subtracts its nibbles from NR43's.
    const auto song = oldSong(11);
    auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;
    REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(11), *bank, *out, sum, notes));
    const auto* noi = out->phrase(out->chain[3].at(0));
    REQUIRE(noi != nullptr);
    // ~E3 = 1C, octave 4: 1C + 16 = 2C (16 kHz, 7-bit); the drum's Shift puts it on the keyboard.
    const int slot = noi->cells[0].inst;
    REQUIRE(slot >= 1);
    const int off = int(bank->instruments[size_t(slot - 1)].noiseShift) - 5;
    const auto noteOf = [off](uint8_t nr43, int prefer) { return chipboyNoteForClock(noiseClockOf(nr43) * std::pow(2.0, off), prefer); };
    const int n2C = noteOf(0x2C, 60), n3B = noteOf(0x3B, n2C), n4A = noteOf(0x4A, n3B);
    CHECK(noi->cells[0].note == n2C);
    CHECK(bank->instruments[size_t(slot - 1)].lfsr7);
    // Section 66: the byte goes through as it stands and the instrument's Sweep
    // reads it -- before 9 that is the nibble subtraction on NR43.
    CHECK(bank->instruments[size_t(slot - 1)].noiseDomain == bank::NoiseSweepDomain::Register);
    REQUIRE(noi->cells[0].cmd1.cmd == bank::Cmd::S);
    CHECK(int(noi->cells[0].cmd1.a) == 0xF); CHECK(int(noi->cells[0].cmd1.b) == 0x1);   // SF1, as written
    REQUIRE(noi->cells[1].cmd1.cmd == bank::Cmd::S);
    CHECK(int(noi->cells[1].cmd1.a) == 0xF); CHECK(int(noi->cells[1].cmd1.b) == 0x1);
    CHECK(noiseClockMatches(*bank, *noi, 8, 0x1C));                          // C-5: 1C + 0
    // The table's S rows are folded into its transpose column for the note it is used with (C-5, 1C).
    const auto& t = bank->tables[0];
    REQUIRE(t.used);
    const int slot2 = noi->cells[8].inst; REQUIRE(slot2 >= 1);
    const int off2 = int(bank->instruments[size_t(slot2 - 1)].noiseShift) - 5;
    const auto noteOf2 = [off2](uint8_t nr43, int prefer) { return chipboyNoteForClock(noiseClockOf(nr43) * std::pow(2.0, off2), prefer); };
    // The table's S rows keep their bytes too (section 66).
    CHECK_FALSE(t.steps[0].hasTranspose); CHECK_FALSE(t.steps[1].hasTranspose); CHECK_FALSE(t.steps[2].hasTranspose);
    (void) noteOf2; (void) off2; (void) slot2;
    REQUIRE(t.steps[1].cmd1.cmd == bank::Cmd::S); CHECK(int(t.steps[1].cmd1.a) == 0); CHECK(int(t.steps[1].cmd1.b) == 7);
    REQUIRE(t.steps[2].cmd1.cmd == bank::Cmd::S); CHECK(int(t.steps[2].cmd1.a) == 1); CHECK(int(t.steps[2].cmd1.b) == 0);
    // On 9.x the same byte is the transpose itself.
    const auto song9 = oldSong(22);
    REQUIRE(importSong(song9.data(), song9.size(), *lsdjModelForFormat(22), *bank, *out, sum, notes));
    const auto* noi9 = out->phrase(out->chain[3].at(0));
    REQUIRE(noi9 != nullptr);
    REQUIRE(noi9->cells[0].cmd1.cmd == bank::Cmd::S);
    CHECK(semitoneOf(noi9->cells[0].cmd1) == -15);                             // F1 two's complement
    CHECK(noiseByteMatches(*bank, *noi9, 0, 0x77));                          // C-4 on the 9.x map, byte for byte (section 81)
}

TEST_CASE("the formats before 5.7 convert P, L and V from the period register", "[lsdj]")
{
    const auto song = oldSong(3);
    auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;
    REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(3), *bank, *out, sum, notes));
    CHECK(bank->instruments[0].pitchSpeed == bank::PitchSpeed::Drum);         // P and L work in the register: Drum
    const auto* pu = out->phrase(out->chain[0].at(0));
    REQUIRE(pu != nullptr);
    // L04 from C-4 to E-4: the register distance over 4 units a clock, less one, is the duration.
    REQUIRE(pu->cells[2].cmd1.cmd == bank::Cmd::L);
    const double dist = (2048.0 - 131072.0 / 329.6276) - (2048.0 - 131072.0 / 261.6256);
    CHECK(int(pu->cells[2].cmd1.a) == int(std::ceil(dist / 4.0)) - 1);
    // P08: 8 units a clock, the Drum speed whose measured step is nearest (27: 105/256 x 19.11 = 7.84).
    REQUIRE(pu->cells[4].cmd1.cmd == bank::Cmd::P);
    CHECK(int(pu->cells[4].cmd1.a) == 27);
    // V under format 3 is already the 9.x law: untouched.
    REQUIRE(pu->cells[6].cmd1.cmd == bank::Cmd::V);
    CHECK(int(pu->cells[6].cmd1.a) == 2); CHECK(int(pu->cells[6].cmd1.b) == 4);
    // Format 0's one-sided vibrato: 8y units a clock for x + 1 clocks, centred by ChipBoy.
    const auto song0 = oldSong(0);
    REQUIRE(importSong(song0.data(), song0.size(), *lsdjModelForFormat(0), *bank, *out, sum, notes));
    const auto* pu0 = out->phrase(out->chain[0].at(0));
    REQUIRE(pu0 != nullptr);
    REQUIRE(pu0->cells[6].cmd1.cmd == bank::Cmd::V);
    CHECK(int(pu0->cells[6].cmd1.a) == 10); CHECK(int(pu0->cells[6].cmd1.b) == 3);
    // Under format 7 the same bytes keep the 9.x laws and the instrument's own pitch mode.
    const auto song7 = oldSong(7);
    REQUIRE(importSong(song7.data(), song7.size(), *lsdjModelForFormat(7), *bank, *out, sum, notes));
    CHECK(bank->instruments[0].pitchSpeed == bank::PitchSpeed::Fast);
    CHECK(int(out->phrase(out->chain[0].at(0))->cells[4].cmd1.a) == 8);
}

TEST_CASE("the song's own transpose is read and added to every chain row", "[lsdj]")
{
    // Section 61: byte 0x3FB5, two's complement, on top of the chain's column;
    // the instrument's Transpose flag gates the sum, which the Driver does.
    auto song = blankSong(22);
    song[0x3FB5] = 0xFB;                                  // -5
    song[kInstAlloc + 0] = 1;
    uint8_t* i0 = song.data() + kInst; i0[0] = 0; i0[1] = 0xA5; i0[4] = 0xFF; i0[7] = 0x80 | 3;
    song[kPhraseAlloc] |= 1; song[kNotes] = 60 - 35; song[kPhraseInst] = 0;
    song[kChainPhrases] = 0; song[kChainPhrases + 1] = 0; song[kChainTsp + 1] = 7;
    song[kRows + 0] = 0; song[kRows + 1] = 0xFF; song[kRows + 2] = 0xFF; song[kRows + 3] = 0xFF;
    auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;
    REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(22), *bank, *out, sum, notes));
    CHECK(int(out->transpose) == -5);
    CHECK(int(out->transposeAt(0, 0)) == -5);             // the song's alone
    CHECK(int(out->transposeAt(0, 1)) == 2);              // and the chain's +7 on top
    bool told = false;
    for (const auto& l : notes.lines) if (l.find("song's own transpose") != std::string::npos) told = true;
    CHECK(told);
}

TEST_CASE("the wave instrument's synth and frame come from byte 2 before 9", "[lsdj]")
{
    // Section 60: byte 2 up to format 15, byte 3 from 22.
    auto song = blankSong(11);
    song[kInstAlloc + 0] = 1;
    uint8_t* i0 = song.data() + kInst;
    i0[0] = 1; i0[1] = 0x20; i0[2] = 0x25; i0[3] = 0x40; i0[7] = 3;      // byte 2 says synth 2 frame 5, byte 3 says synth 4
    for (int f = 0; f < 16; ++f)
        for (int k = 0; k < 16; ++k) {
            song[kWaves + size_t((2 * 16 + f) * 16 + k)] = uint8_t(0x20 | f);   // synth 2
            song[kWaves + size_t((4 * 16 + f) * 16 + k)] = uint8_t(0x40 | f);   // synth 4
        }
    song[kPhraseAlloc] |= 1; song[kNotes] = 60 - 35; song[kPhraseInst] = 0;
    song[kChainPhrases] = 0; song[kRows + 0] = 0xFF; song[kRows + 1] = 0xFF; song[kRows + 2] = 0; song[kRows + 3] = 0xFF;
    auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;
    REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(11), *bank, *out, sum, notes));
    const auto& w11 = bank->instruments[0];
    REQUIRE(w11.wave >= 1);
    CHECK(int(w11.frameLoopStep) == 5);          // the nibble is LOOP POS, not a start frame (section 65)
    const auto& wave11 = bank->waves[size_t(w11.wave - 1)];
    REQUIRE(!wave11.frames.empty());
    CHECK(int(wave11.frames[0].s[0]) == 2);                               // synth 2, from byte 2
    // The same bytes under format 22 take byte 3.
    song[kFormatVersionAt] = 22;
    REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(22), *bank, *out, sum, notes));
    const auto& w22 = bank->instruments[0];
    CHECK(int(w22.frameLoopStep) == 0);
    CHECK(int(bank->waves[size_t(w22.wave - 1)].frames[0].s[0]) == 4);      // synth 4, from byte 3
}

TEST_CASE("an H that ends a phrase becomes the phrase's length", "[lsdj]")
{
    // Section 56: H 0 y ends the phrase there and starts the next at row y.
    // The row the H sits on does not play.
    auto song = blankSong(22);
    song[kInstAlloc + 0] = 1;
    uint8_t* i0 = song.data() + kInst; i0[0] = 0; i0[1] = 0xA5; i0[4] = 0xFF; i0[7] = 0x80 | 3;
    song[kPhraseAlloc] |= 1;
    for (int st = 0; st < 6; ++st) { song[kNotes + size_t(st)] = uint8_t(60 - 35 + st); song[kPhraseInst + size_t(st)] = 0; }
    song[kCmd + 3] = 8; song[kCmdV + 3] = 0x00;                    // H00 at step 3, byte 8 is H in the table with B
    song[kChainPhrases] = 0; song[kRows + 0] = 0; song[kRows + 1] = 0xFF; song[kRows + 2] = 0xFF; song[kRows + 3] = 0xFF;
    auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;
    REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(22), *bank, *out, sum, notes));
    const auto* p0 = out->phrase(out->chain[0].at(0));
    REQUIRE(p0 != nullptr);
    CHECK(int(p0->steps) == 3);                                     // rows 0, 1, 2 play; row 3 does not
    CHECK(p0->cells[0].note == 60); CHECK(p0->cells[2].note == 62);
    CHECK(p0->cells[3].cmd1.cmd == bank::Cmd::None);
    // Section 80, measured on 9.3.9 with two phrases in the chain: a **counted**
    // H is a different command -- it hops back inside the phrase rather than
    // ending it -- so the importer still ends the phrase there, and says so.
    song[kCmdV + 3] = 0x21;
    ImportNotes counted;
    REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(22), *bank, *out, sum, counted));
    const auto* p1 = out->phrase(out->chain[0].at(0));
    REQUIRE(p1 != nullptr);
    CHECK(int(p1->steps) == 3);
    CHECK(p1->cells[3].cmd1.cmd == bank::Cmd::None);
    bool told = false;
    for (const auto& l : counted.lines)
        if (l.find("hops back to step 1 inside the phrase") != std::string::npos) told = true;
    CHECK(told);
    // And `H 0 y`, which really does end the phrase, says where the next one
    // would have started.
    song[kCmdV + 3] = 0x01;
    ImportNotes chained;
    REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(22), *bank, *out, sum, chained));
    bool toldRow = false;
    for (const auto& l : chained.lines)
        if (l.find("starts the next one at step 1") != std::string::npos) toldRow = true;
    CHECK(toldRow);
}

TEST_CASE("a project file decompresses to the song the save's file holds", "[lsdj]")
{
    // Plan section 1a: name, version, the blocks in order; the jump codes
    // inside still name the save's block numbers and are read as "next".
    SaveWriter w;
    const auto song = testSong(22);
    w.addFile(3, "PROJECT", song, true);
    const auto& save = w.save;
    int first = -1, count = 0;
    for (int b = 0; b < kBlockCount; ++b) if (save[0x8141 + size_t(b)] == 3) { if (first < 0) first = b + 1; ++count; }
    REQUIRE(first > 0);
    std::vector<uint8_t> proj(9, 0);
    std::memcpy(proj.data(), "PROJECT", 7); proj[8] = 5;
    proj.insert(proj.end(), save.begin() + 0x8000 + first * 0x200, save.begin() + 0x8000 + (first + count) * 0x200);
    CHECK(looksLikeProject(proj.data(), proj.size()));
    CHECK_FALSE(looksLikeProject(save.data(), save.size()));
    std::string name, err; int version = -1; std::vector<uint8_t> out;
    REQUIRE(decompressProject(proj.data(), proj.size(), name, version, out, err));
    CHECK(name == "PROJECT"); CHECK(version == 5);
    CHECK(out == song);
    proj.resize(proj.size() - 7);                    // a torn file is refused
    CHECK_FALSE(looksLikeProject(proj.data(), proj.size()));
}

TEST_CASE("a noise byte maps to the ChipBoy note with the same LFSR clock", "[lsdj]")
{
    // NR43 20: shift 2, divisor 0 -> 131072 Hz; ChipBoy's pair (0, 2) has the same clock.
    const int n = chipboyNoteForNr43(0x20, 89);
    uint8_t sh = 0, d = 0; driver::Driver::noisePairForNote(n, sh, d);
    const double clockOf = [](int s, int dv) { return 524288.0 / (dv == 0 ? 0.5 : double(dv)) / double(1u << (s + 1)); }(sh, d);
    CHECK(clockOf == 131072.0);
}

TEST_CASE("a real save, when one is given, imports every song without a fault", "[lsdj]")
{
    // CHIPBOY_LSDJ_SAV names a save outside the tree (rule L3); without it
    // the case passes trivially, as the parity tests do without a ROM.
    const char* path = std::getenv("CHIPBOY_LSDJ_SAV");
    if (path == nullptr || *path == 0) { SUCCEED("no save given"); return; }
    FILE* f = std::fopen(path, "rb");
    REQUIRE(f != nullptr);
    std::vector<uint8_t> save(kSaveSize + 1);
    const size_t n = std::fread(save.data(), 1, save.size(), f);
    std::fclose(f);
    save.resize(n);
    SaveIndex idx; std::string err;
    REQUIRE(indexSave(save.data(), save.size(), idx, err));
    CHECK_FALSE(idx.files.empty());
    int imported = 0;
    for (const auto& e : idx.files) {
        std::vector<uint8_t> song;
        REQUIRE(decompressFile(save.data(), save.size(), e.file, song, err));
        const auto* model = lsdjModelForFormat(e.formatVersion);
        if (model == nullptr) model = &lsdjLatestModel();
        auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
        ImportSummary sum; ImportNotes notes;
        REQUIRE(importSong(song.data(), song.size(), *model, *bank, *out, sum, notes));
        CHECK(sum.rows > 0);
        ++imported;
        INFO(e.name << " format " << e.formatVersion << ": " << sum.instruments << " instruments, " << sum.phrases << " phrases, " << sum.rows << " rows, " << notes.lines.size() << " notes");
        CHECK(sum.instruments > 0);
    }
    CHECK(imported == int(idx.files.size()));
}

TEST_CASE("a kit instrument takes its samples from the ROM beside the save", "[lsdj]")
{
    // A ROM of two kit banks, built here: bank 0 (kit 00) "TESTKT" with two
    // samples, bank 1 (kit 01) "SECOND" with one.
    std::vector<uint8_t> rom(2 * 0x4000, 0);
    auto bankAt = [&](int bank, const char* name, const std::vector<std::vector<uint8_t>>& samples) {
        uint8_t* b = rom.data() + size_t(bank) * 0x4000;
        b[0] = 0x60; b[1] = 0x40;
        std::memcpy(b + 0x52, name, 6);
        uint16_t at = 0x4060;
        for (size_t i = 0; i < samples.size(); ++i) {
            std::memcpy(b + 0x22 + 3 * i, "S01", 3);
            for (size_t k = 0; k < samples[i].size(); k += 2) b[size_t(at) - 0x4000 + k / 2] = uint8_t((samples[i][k] << 4) | samples[i][k + 1]);
            at = uint16_t(at + samples[i].size() / 2);
            b[2 * (i + 1)] = uint8_t(at & 0xFF); b[2 * (i + 1) + 1] = uint8_t(at >> 8);
        }
    };
    std::vector<uint8_t> ramp(128), flat(64, 8), tri(96);
    for (size_t k = 0; k < ramp.size(); ++k) ramp[k] = uint8_t(k % 16);
    for (size_t k = 0; k < tri.size(); ++k) tri[k] = uint8_t(k % 32 < 16 ? k % 16 : 15 - k % 16);
    bankAt(0, "TESTKT", { ramp, flat });
    bankAt(1, "SECOND", { tri });
    const auto kits = readKits(rom.data(), rom.size());
    REQUIRE(kits.size() == 2);
    CHECK(kits[0].name == "TESTKT"); CHECK(kits[0].samples.size() == 2); CHECK(kits[0].samples[0].nibbles == ramp); CHECK(kits[0].samples[1].nibbles == flat);
    CHECK(kits[1].samples.size() == 1); CHECK(kits[1].samples[0].nibbles == tri);
    CHECK(kitPeriodOfSpeed(0x00) == 1865); CHECK(kitPeriodOfSpeed(0xD0) == 1817); CHECK(kitPeriodOfSpeed(0x40) == 1929);

    // A song with one kit instrument: kit A = 00 cut to 2 frames, kit B = 01 whole, speed D0.
    auto song = blankSong(22);
    song[kInstAlloc + 0] = 1;
    uint8_t* i0 = song.data() + kInst; i0[0] = 2; i0[1] = 0xA8; i0[2] = 0x00; i0[3] = 2; i0[7] = 3; i0[8] = 0xD0; i0[9] = 0x01; i0[11] = 0;
    std::memcpy(song.data() + kNames, "DRUMS", 5);
    song[kPhraseAlloc] |= 1;
    song[kNotes + 0] = 0x10; song[kPhraseInst + 0] = 0;     // kit A sample 1: the ramp, two frames of it
    song[kNotes + 4] = 0x20;                                // kit A sample 2: the flat one
    song[kNotes + 8] = 0x01;                                // kit B sample 1: the triangle
    song[kNotes + 12] = 0x10;                               // the ramp again: the same note
    song[kChainPhrases + 0] = 0;
    song[kRows + 0] = 0xFF; song[kRows + 1] = 0xFF; song[kRows + 2] = 0; song[kRows + 3] = 0xFF;
    auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;
    REQUIRE(importSong(song.data(), song.size(), lsdjLatestModel(), *bank, *out, sum, notes, &kits));
    CHECK(sum.kits == 1); CHECK(sum.instruments == 1);
    const auto& inst = bank->instruments[0];
    CHECK(inst.type == bank::InstrumentType::Kit); CHECK(inst.kit == 1);
    const auto& kit = bank->kits[0];
    REQUIRE(kit.used);
    CHECK(kit.name == "TESTKT+SECOND"); CHECK(kit.period == 1817);
    REQUIRE(kit.samples.size() == 3);
    CHECK(kit.samples[0].data.size() == 64);                // two frames of the 128-nibble ramp
    CHECK(kit.samples[0].data[17] == 1);
    CHECK(kit.samples[1].data == flat);
    CHECK(kit.samples[2].data == tri);
    const auto* p = out->phrase(1);
    REQUIRE(p != nullptr);
    CHECK(p->cells[0].note == 36); CHECK(p->cells[4].note == 37); CHECK(p->cells[8].note == 38); CHECK(p->cells[12].note == 36);
    // Without a ROM the instrument is a note and nothing else.
    auto bank2 = std::make_unique<bank::Bank>(); auto out2 = std::make_unique<tracker::Song>();
    ImportSummary sum2; ImportNotes notes2;
    REQUIRE(importSong(song.data(), song.size(), lsdjLatestModel(), *bank2, *out2, sum2, notes2, nullptr));
    CHECK(sum2.instruments == 0);
    bool noted = false;
    for (const auto& l : notes2.lines) if (l.find("kit") != std::string::npos) noted = true;
    CHECK(noted);
}

TEST_CASE("a table's second command column keeps its own hop", "[lsdj]")
{
    // Section 64: LSDj's two table command columns loop independently and
    // ChipBoy's do too, so the H in the second column is carried, not dropped.
    for (const int format : { 11, 22 }) {
        const bool withB = format >= 11;
        const auto letter = [withB](char c) { const char* t = withB ? "-ABCDEFGHKLMOPRSTVWZ" : "-ACDEFGHKLMOPRSTVWZ"; return uint8_t(std::strchr(t, c) - t); };
        auto song = blankSong(format);
        song[kInstAlloc + 0] = 1;
        uint8_t* i0 = song.data() + kInst; i0[0] = 0; i0[1] = 0xA5; i0[4] = 0xFF; i0[6] = 0x20 | 0; i0[7] = 0x80 | 3;
        song[kTableAlloc + 0] = 1;
        song[kTableTsp + 1] = 3; song[kTableTsp + 2] = 7;
        song[kTableCmd1 + 3] = letter('H'); song[kTableCmd1V + 3] = 0;      // the hop the transpose column follows
        song[kTableCmd2 + 1] = letter('H'); song[kTableCmd2V + 1] = 0;      // and the second column's own
        song[kPhraseAlloc] |= 1; song[kNotes] = uint8_t(60 - 35); song[kPhraseInst] = 0;
        song[kChainPhrases] = 0;
        song[kRows + 0] = 0; song[kRows + 1] = 0xFF; song[kRows + 2] = 0xFF; song[kRows + 3] = 0xFF;
        auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
        ImportSummary sum; ImportNotes notes;
        REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(format), *bank, *out, sum, notes));
        const auto& tb = bank->tables[0];
        CHECK(tb.steps[3].cmd1.cmd == bank::Cmd::H);
        CHECK(tb.steps[1].cmd2.cmd == bank::Cmd::H);
        for (const auto& l : notes.lines) CHECK(l.find("second command column") == std::string::npos);
    }
}

TEST_CASE("a table's ENV column carries its duration and its hop", "[lsdj]")
{
    // Section 64: the low digit is a duration in ticks, 0 blanks the row and F
    // hops the volume lane to the row the high digit names.
    auto song = blankSong(11);
    song[kInstAlloc + 0] = 1;
    uint8_t* i0 = song.data() + kInst; i0[0] = 0; i0[1] = 0xA5; i0[4] = 0xFF; i0[6] = 0x20 | 0; i0[7] = 0x80 | 3;
    song[kTableAlloc + 0] = 1;
    song[kTableEnv + 0] = 0xA4;      // amplitude 10, four ticks
    song[kTableEnv + 1] = 0x01;      // amplitude 0 is a level, not a blank
    song[kTableEnv + 2] = 0xB0;      // duration 0: the row is blank
    song[kTableEnv + 3] = 0x1F;      // hop the lane to row 1
    song[kPhraseAlloc] |= 1; song[kNotes] = uint8_t(60 - 35); song[kPhraseInst] = 0;
    song[kChainPhrases] = 0;
    song[kRows + 0] = 0; song[kRows + 1] = 0xFF; song[kRows + 2] = 0xFF; song[kRows + 3] = 0xFF;
    auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;
    REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(11), *bank, *out, sum, notes));
    const auto& tb = bank->tables[0];
    CHECK(int(tb.steps[0].vol) == 10); CHECK(int(tb.steps[0].volTicks) == 4); CHECK(int(tb.steps[0].volHop) == -1);
    CHECK(int(tb.steps[1].vol) == 0);  CHECK(int(tb.steps[1].volTicks) == 1);
    CHECK(int(tb.steps[2].vol) == -1); CHECK(int(tb.steps[2].volTicks) == 0);
    CHECK(int(tb.steps[3].vol) == -1); CHECK(int(tb.steps[3].volHop) == 1);
    CHECK(int(tb.steps[3].volTicks) == 1);                // format 11 spends a tick on the hop

    // Format 22 hops for free (measured on 9.2.L: the cycle keeps its length).
    auto song22 = song; song22[kFormatVersionAt] = 22;
    ImportNotes notes22;
    REQUIRE(importSong(song22.data(), song22.size(), *lsdjModelForFormat(22), *bank, *out, sum, notes22));
    CHECK(int(bank->tables[0].steps[3].volHop) == 1);
    CHECK(int(bank->tables[0].steps[3].volTicks) == 0);
}

TEST_CASE("a wave instrument's PLAY, LENGTH, LOOP POS and SPEED are read", "[lsdj]")
{
    // Section 65: byte 9's low two bits, byte 10's low nibble, byte 11, and the
    // low nibble of the synth byte.
    const auto build = [](int play, int lengthNibble, int speed, int loopPos) {
        auto song = blankSong(11);
        song[kInstAlloc + 0] = 1;
        uint8_t* i0 = song.data() + kInst;
        i0[0] = 1; i0[1] = 0x20; i0[2] = uint8_t((2 << 4) | loopPos); i0[7] = 3;
        i0[9] = uint8_t(play); i0[10] = uint8_t(lengthNibble); i0[11] = uint8_t(speed);
        for (int f = 0; f < 16; ++f)
            for (int k = 0; k < 16; ++k) song[kWaves + size_t((2 * 16 + f) * 16 + k)] = uint8_t(0x20 | f);
        song[kPhraseAlloc] |= 1; song[kNotes] = uint8_t(60 - 35); song[kPhraseInst] = 0;
        song[kChainPhrases] = 0; song[kRows + 0] = 0xFF; song[kRows + 1] = 0xFF; song[kRows + 2] = 0; song[kRows + 3] = 0xFF;
        return song;
    };
    auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;

    const auto loop = build(2, 8, 3, 9);                  // LOOP, LENGTH 8, SPEED 3, LOOP POS 9
    REQUIRE(importSong(loop.data(), loop.size(), *lsdjModelForFormat(11), *bank, *out, sum, notes));
    const auto& a = bank->instruments[0];
    CHECK(a.frameLoop == bank::FrameLoop::Loop);
    CHECK(int(a.frameLength) == 8);
    CHECK(int(a.frameAdvance) == 7);                      // SPEED + 4 ticks a frame
    CHECK(int(a.frameLoopStep) == 1);                     // 8 - (16 - 9)

    const auto manual = build(0, 0, 0, 0);
    REQUIRE(importSong(manual.data(), manual.size(), *lsdjModelForFormat(11), *bank, *out, sum, notes));
    CHECK(int(bank->instruments[0].frameAdvance) == 0);   // MANUAL never advances
    CHECK(int(bank->instruments[0].frameLength) == 16);

    const auto once = build(1, 12, 0, 5);
    REQUIRE(importSong(once.data(), once.size(), *lsdjModelForFormat(11), *bank, *out, sum, notes));
    CHECK(bank->instruments[0].frameLoop == bank::FrameLoop::Once);
    CHECK(int(bank->instruments[0].frameLength) == 4);
    CHECK(int(bank->instruments[0].frameLoopStep) == 0);  // 4 - (16 - 5) is negative: the whole run

    const auto ping = build(3, 0, 15, 11);
    REQUIRE(importSong(ping.data(), ping.size(), *lsdjModelForFormat(11), *bank, *out, sum, notes));
    CHECK(bank->instruments[0].frameLoop == bank::FrameLoop::PingPong);
    CHECK(int(bank->instruments[0].frameAdvance) == 19);
    CHECK(int(bank->instruments[0].frameLoopStep) == 11);
}

TEST_CASE("before LSDj 9 a table's G holds the groove's first step", "[lsdj]")
{
    // Section 63: the same run under a format-11 and a format-22 model.
    const auto build = [](int format) {
        const bool withB = format >= 11;
        const auto letter = [withB](char c) { const char* t = withB ? "-ABCDEFGHKLMOPRSTVWZ" : "-ACDEFGHKLMOPRSTVWZ"; return uint8_t(std::strchr(t, c) - t); };
        auto song = blankSong(format);
        song[kGrooves + 0] = 6; song[kGrooves + 1] = 6;                     // groove 0, named by nothing here
        song[kGrooves + 16 + 0] = 7; song[kGrooves + 16 + 1] = 4;           // groove 1: the table's
        song[kInstAlloc + 0] = 1;
        uint8_t* i0 = song.data() + kInst; i0[0] = 0; i0[1] = 0xA5; i0[4] = 0xFF; i0[6] = 0x20 | 0; i0[7] = 0x80 | 3;
        song[kTableAlloc + 0] = 1;
        song[kTableCmd1 + 0] = letter('G'); song[kTableCmd1V + 0] = 1;      // G01
        song[kTableTsp + 1] = 3;
        song[kPhraseAlloc] |= 1; song[kNotes] = uint8_t(60 - 35); song[kPhraseInst] = 0;
        song[kChainPhrases] = 0;
        song[kRows + 0] = 0; song[kRows + 1] = 0xFF; song[kRows + 2] = 0xFF; song[kRows + 3] = 0xFF;
        return song;
    };
    auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;

    const auto nine = build(22);
    REQUIRE(importSong(nine.data(), nine.size(), *lsdjModelForFormat(22), *bank, *out, sum, notes));
    REQUIRE(bank->tables[0].steps[0].cmd1.cmd == bank::Cmd::G);
    CHECK(int(bank->tables[0].steps[0].cmd1.a) == 2);            // LSDj's groove 1 is ChipBoy's slot 2
    CHECK(int(out->grooves[1].ticks[0]) == 7); CHECK(int(out->grooves[1].ticks[1]) == 4);

    const auto old = build(11);
    ImportNotes oldNotes;
    REQUIRE(importSong(old.data(), old.size(), *lsdjModelForFormat(11), *bank, *out, sum, oldNotes));
    REQUIRE(bank->tables[0].steps[0].cmd1.cmd == bank::Cmd::G);
    const int slot = int(bank->tables[0].steps[0].cmd1.a);
    CHECK(slot != 2);                                            // moved off the song's own groove
    REQUIRE(slot >= 1); REQUIRE(slot <= 16);
    CHECK(int(out->grooves[size_t(slot - 1)].ticks[0]) == 7);    // one step of seven: every row the same
    CHECK(int(out->grooves[size_t(slot - 1)].ticks[1]) == 0);
    CHECK(int(out->grooves[1].ticks[1]) == 4);                   // the song's groove 1 is left alone
    bool told = false;
    for (const auto& l : oldNotes.lines) if (l.find("one step groove") != std::string::npos) told = true;
    CHECK(told);
}
