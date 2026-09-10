// ChipBoy -- the LSDj importer (docs/plan-lsdj-import.md section 6). Every
// save here is built in memory: no LSDj content enters the repository (L3).
#include "core/Import/LsdjModel.h"
#include "core/Import/LsdjSave.h"
#include "core/Import/LsdjSong.h"
#include "core/Driver/Driver.h"

#include <catch2/catch_test_macros.hpp>

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
                 kTableCmd1 = 0x3680, kTableCmd1V = 0x3880, kPhraseAlloc = 0x3E82, kTempo = 0x3FB4, kCmd = 0x4000, kCmdV = 0x4FF0,
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
    s[kTableTsp + 0] = 0xF4; s[kTableTsp + 1] = 7; s[kTableEnv + 2] = 0x50;
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
    REQUIRE(n >= 4);
    CHECK(std::string(lsdjLatestModel().name).find("9.3.9") != std::string::npos);
    CHECK(lsdjModelForFormat(22) == models[0]);
    CHECK(lsdjModelForFormat(15)->formatVersion == 15);          // 8.8.6, measured
    CHECK(lsdjModelForFormat(18)->formatVersion == 15);          // no ROM yet: the nearest below
    CHECK(lsdjModelForFormat(11)->formatVersion == 11);          // 8.4.0, measured
    CHECK(lsdjModelForFormat(3) != nullptr); CHECK(std::string(lsdjModelForFormat(3)->name).find("assumed") != std::string::npos);
    CHECK(lsdjModelForFormat(99) == nullptr);
    CHECK(lsdjModelForRomVersion("9.3.9") == models[0]);
    CHECK(lsdjModelForRomVersion("9.2.J") == models[0]);
    CHECK(lsdjModelForRomVersion("8.8.6")->formatVersion == 15);
    CHECK(lsdjModelForRomVersion("8.4.0")->formatVersion == 11);
    CHECK(lsdjModelForRomVersion("4.7.3")->formatVersion == 3);
    CHECK(lsdjModelForRomVersion("") == nullptr);
    CHECK(lsdjModelNamed(models[0]->name) == models[0]);
    CHECK(lsdjModelNamed("nothing") == nullptr);
    // The ROM's cartridge title names the version.
    std::vector<uint8_t> rom(0x150, 0);
    std::memcpy(rom.data() + 0x134, "LSDj-v9.3.9", 11);
    CHECK(romVersion(rom.data(), rom.size()) == "9.3.9");
    std::memcpy(rom.data() + 0x134, "TETRIS\0\0\0\0\0", 11);
    CHECK(romVersion(rom.data(), rom.size()).empty());
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
    CHECK(t.steps[2].vol == 5); CHECK(t.steps[3].vol == -1);
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
        if (ch == 3 && p) { foundNoise = true; CHECK(p->cells[0].note == chipboyNoteForNr43(0x20, 93)); }
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
    SECTION("8.4.0, format 11: the hardware envelope, B in the table, an octave-only noise map") {
        const auto song = testSong(22);                              // the same bytes: byte 9 kills, byte 13 bends
        auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
        ImportSummary sum; ImportNotes notes;
        REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(11), *bank, *out, sum, notes));
        CHECK(bank->instruments[0].env.mode == bank::EnvMode::Chip); CHECK(bank->instruments[0].envRate == 5);
        CHECK(out->phrase(1)->cells[4].cmd1.cmd == bank::Cmd::P);
        bool found = false;
        for (uint8_t slot : out->chain[3]) if (const auto* p = out->phrase(slot)) { found = true; CHECK(p->cells[0].note == chipboyNoteForNr43(0xEF, 93)); }   // A-6 writes EF on 8.4.0
        CHECK(found);
    }
    SECTION("8.8.6, format 15: the three stages and the raw noise column") {
        const auto song = testSong(22);
        auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
        ImportSummary sum; ImportNotes notes;
        REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(15), *bank, *out, sum, notes));
        CHECK(bank->instruments[0].env.mode == bank::EnvMode::Shaped); CHECK(bank->instruments[0].env.attackTicks == 11);
        bool found = false;
        for (uint8_t slot : out->chain[3]) if (const auto* p = out->phrase(slot)) { found = true; CHECK(p->cells[0].note == chipboyNoteForNr43(uint8_t(0xFF - (93 - 35)), 93)); }   // A-6 is note byte 58: FF - 58
        CHECK(found);
    }
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
