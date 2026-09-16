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
    // Every note names its instrument, as LSDj's own editor writes them: from
    // 4.0.4 a cell whose instrument column is blank does not trigger, it bends
    // the channel to its note (section 101).
    s[kNotes + 0] = 60 - 35; s[kPhraseInst + 0] = 0;
    s[kNotes + 4] = 62 - 35; s[kPhraseInst + 4] = 0; s[kCmd + 4] = uint8_t(format >= 20 ? 13 : 12); s[kCmdV + 4] = 0xFD;   // P -3
    s[kNotes + 8] = 64 - 35; s[kPhraseInst + 8] = 0; s[kCmd + 8] = 1; s[kCmdV + 8] = 0;                                      // A00: table 0
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
        // The stream is built as whole codes, because a block boundary may
        // never fall inside one: the reader would take the tail of a code for
        // the head of another.
        std::vector<std::vector<uint8_t>> codes;
        const uint8_t defWave[16] = { 0x8E, 0xCD, 0xCC, 0xBB, 0xAA, 0xA9, 0x99, 0x88, 0x87, 0x76, 0x66, 0x55, 0x54, 0x43, 0x32, 0x31 };
        const uint8_t defInst[16] = { 0xA8, 0, 0, 0xFF, 0, 0, 3, 0, 0, 0xD0, 0, 0, 0, 0xF3, 0, 0 };
        for (size_t i = 0; i < song.size();) {
            if (i + 16 <= song.size() && std::memcmp(song.data() + i, defWave, 16) == 0) { codes.push_back({ 0xE0, 0xF0, 1 }); i += 16; continue; }
            if (i + 16 <= song.size() && std::memcmp(song.data() + i, defInst, 16) == 0) { codes.push_back({ 0xE0, 0xF1, 1 }); i += 16; continue; }
            size_t run = 1;
            while (i + run < song.size() && song[i + run] == song[i] && run < 255) ++run;
            if (run >= 4) { codes.push_back({ 0xC0, song[i], uint8_t(run) }); i += run; continue; }
            if (song[i] == 0xC0) codes.push_back({ 0xC0, 0xC0 });
            else if (song[i] == 0xE0) codes.push_back({ 0xE0, 0xE0 });
            else codes.push_back({ song[i] });
            ++i;
        }
        codes.push_back({ 0xE0, 0xFF });
        // into blocks of 512, each ending with a jump to the next
        size_t at = 0;
        while (at < codes.size()) {
            const int block = nextBlock++;
            REQUIRE(block <= kBlockCount);
            save[0x8141 + size_t(block - 1)] = uint8_t(file);
            uint8_t* dst = save.data() + 0x8000 + size_t(block) * 0x200;
            const size_t room = 0x200 - 2;                   // the jump, or the end marker inside
            size_t used = 0;
            while (at < codes.size() && used + codes[at].size() <= room) {
                std::memcpy(dst + used, codes[at].data(), codes[at].size());
                used += codes[at].size(); ++at;
            }
            if (at < codes.size()) { dst[used] = 0xE0; dst[used + 1] = uint8_t(nextBlock); }
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
    broken[0x8000 + 0x200 + 0x100] = 0xE0; broken[0x8000 + 0x200 + 0x101] = 0xF5;
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
    REQUIRE(n == 13);                                              // three inside format 22 (docs/LSDJ_VERSIONS.md section 11)
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
    CHECK(lsdjFormatForVersion("3.6.5") == 2);                   // the version the Computer Savvy songs were written in
    CHECK(lsdjFormatForVersion("3.5.1") == 0); CHECK(lsdjFormatForVersion("3.1.5") == 0); CHECK(lsdjFormatForVersion("2.0.0") == 0);
    CHECK(lsdjFormatForVersion("nonsense") == -1);
    CHECK(lsdjModelForRomVersion("9.4.2") == models[0]); CHECK(lsdjModelForRomVersion("9.3.9") == models[1]); CHECK(lsdjModelForRomVersion("9.2.L") == models[2]);
    CHECK(lsdjModelForRomVersion("8.8.6")->formatVersion == 15);
    CHECK(lsdjModelForRomVersion("8.4.0")->formatVersion == 11);
    // docs/LSDJ_VERSIONS.md: two releases can write the same format byte and
    // still read a song differently, and then only the ROM's version tells them
    // apart. Format 3 is the case: 4.8.0 changed `R x 0` from a single
    // retrigger to one every tick, and 8.8.1 changed it back.
    // 3.6.5 writes format 2 and reads it as 3.6.8 does, one release below the
    // model's old floor: it was taking the format-0 model and then the format's
    // default, which is 4.0.4's.
    CHECK(lsdjModelForRomVersion("3.6.5")->formatVersion == 2);
    CHECK(lsdjModelForRomVersion("3.6.5")->vibratoLaw == VibratoLaw::Semitone);
    CHECK(lsdjModelForRomVersion("3.6.5")->bareNoteSounds);
    CHECK(lsdjModelForRomVersion("3.5.1")->vibratoLaw == VibratoLaw::RegisterOneSided);
    CHECK(lsdjModelForRomVersion("4.7.3")->retrigZeroOnce);
    CHECK_FALSE(lsdjModelForRomVersion("5.0.3")->retrigZeroOnce);
    CHECK(lsdjModelForRomVersion("4.7.3") != lsdjModelForRomVersion("5.0.3"));
    CHECK(lsdjModelForRomVersion("4.7.3")->formatVersion == 3);
    CHECK(lsdjModelForRomVersion("5.0.3")->formatVersion == 3);
    CHECK(lsdjModelForFormat(3)->formatVersion == 3);
    CHECK(lsdjModelForRomVersion("4.3.0") == lsdjModelForFormat(2));
    // R's interval and the noise C and V, per version.
    CHECK(lsdjModelForFormat(22)->retrigPlus == 0); CHECK(lsdjModelForFormat(11)->retrigPlus == 1);
    CHECK(lsdjModelForFormat(22)->noiseVibrato); CHECK_FALSE(lsdjModelForFormat(11)->noiseVibrato);
    CHECK(lsdjModelForFormat(4)->noiseChord); CHECK_FALSE(lsdjModelForFormat(3)->noiseChord);
    CHECK(lsdjModelForFormat(11)->tempoLowIsHigh); CHECK_FALSE(lsdjModelForFormat(7)->tempoLowIsHigh);
    // docs/LSDJ_VERSIONS.md: a wave instrument walks a run of frames only from
    // format 7; before that it loads frame 0 and holds it.
    CHECK(lsdjModelForFormat(7)->waveFrameRun); CHECK_FALSE(lsdjModelForFormat(5)->waveFrameRun);
    CHECK_FALSE(lsdjModelForFormat(4)->waveFrameRun); CHECK(lsdjModelForFormat(11)->waveFrameRun);
    CHECK(lsdjModelForRomVersion("6.8.2")->waveFrameRun); CHECK_FALSE(lsdjModelForRomVersion("6.4.5")->waveFrameRun);
    // Section 91: the wave SPEED byte is signed, so FD is one tick a frame and
    // not 257. Every wave instrument in the user's SAMESONG stores a negative
    // speed, which read unsigned froze the run.
    {
        auto sp = testSong(22);
        uint8_t* w = sp.data() + kInst + 32;               // instrument 2 is the wave one
        w[9] = 1; w[10] = 0x0C; w[11] = 0xFD;
        auto bk = std::make_unique<bank::Bank>(); auto so = std::make_unique<tracker::Song>();
        ImportSummary s2; ImportNotes n2;
        REQUIRE(importSong(sp.data(), sp.size(), *lsdjModelForFormat(22), *bk, *so, s2, n2));
        CHECK(int(bk->instruments[2].frameAdvance) == 1);
        w[11] = 0x02;
        auto bk2 = std::make_unique<bank::Bank>(); auto so2 = std::make_unique<tracker::Song>();
        REQUIRE(importSong(sp.data(), sp.size(), *lsdjModelForFormat(22), *bk2, *so2, s2, n2));
        CHECK(int(bk2->instruments[2].frameAdvance) == 6);
    }
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

TEST_CASE("every synth is imported, in order, into its own wave slot", "[lsdj]")
{
    // Section 103: ChipBoy's bank is one flat 256-frame table and F walks
    // straight out of one slot into the next, so the slot next door has to hold
    // what the save's wave RAM held there -- whether or not an instrument names
    // it. LSDj synth k goes into wave slot k + 1, all sixteen, in order.
    auto song = testSong(22);
    // Tag every synth's every frame with its own number, in both nibbles.
    for (int synth = 0; synth < 16; ++synth)
        for (int f = 0; f < 16; ++f)
            for (int k = 0; k < 16; ++k)
                song[kWaves + size_t(synth * 16 + f) * 16 + size_t(k)] = uint8_t((synth << 4) | synth);
    auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;
    REQUIRE(importSong(song.data(), song.size(), lsdjLatestModel(), *bank, *out, sum, notes));
    CHECK(sum.waves == bank::kWaveSlots);
    for (int synth = 0; synth < bank::kWaveSlots; ++synth) {
        const auto& w = bank->waves[size_t(synth)];
        INFO("synth " << synth);
        REQUIRE(w.used);
        REQUIRE(w.frames.size() == size_t(bank::kMaxFrames));
        for (const auto& f : w.frames)
            for (auto v : f.s) CHECK(int(v) == synth);
    }
    // The song's only wave instrument names synth 0, and that is still slot 1:
    // the slots are the synths' own numbers, not the order they are referenced.
    const auto& bass = bank->instruments[2];
    REQUIRE(bass.type == bank::InstrumentType::Wave);
    CHECK(int(bass.wave) == 1);
}

TEST_CASE("a format-22 song imports its instruments, tables, phrases and chains", "[lsdj]")
{
    const auto song = testSong(22);
    auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;
    REQUIRE(importSong(song.data(), song.size(), lsdjLatestModel(), *bank, *out, sum, notes));
    CHECK(sum.instruments == 3); CHECK(sum.tables == 1); CHECK(sum.rows == 2);
    // Section 103: all sixteen synths come in, in order, whether or not an
    // instrument names them -- the bank is one flat table.
    CHECK(sum.waves == bank::kWaveSlots);
    for (int k = 0; k < bank::kWaveSlots; ++k) CHECK(bank->waves[size_t(k)].used);
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
    // Section 132: a stage is its tick count **plus** a fine 1/256, so the whole
    // part is the floor of what the rounded count used to be.
    CHECK(hat.env.mode == bank::EnvMode::Shaped); CHECK(hat.env.start == 8);
    CHECK(hat.env.attackTicks == 8); CHECK(hat.env.attackFine > 128);
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
    // Section 137: LSDj's groove 0 is the groove a phrase runs on until a `G`
    // says otherwise, and it lands in slot 1. This song's is 7 5, so every
    // imported phrase swings and its rows are 7 + 5 per pair, not six each.
    REQUIRE(out->chain[0][0] >= 1);
    CHECK(int(out->phrases[size_t(out->chain[0][0] - 1)].groove) == 1);
    CHECK(rowTicks(*out, 0, 0) == 8 * (7 + 5));
    for (auto src : out->noteSource) CHECK(src == tracker::NoteSource::Tracker);
    // Section 49: the PU2 transpose is a signed byte of semitones and ChipBoy's
    // is the same, so a transpose that keeps every note on the keyboard is
    // carried in silence -- +12 from MIDI 60 and 64 does.
    CHECK(int(bank->instruments[0].pu2Transpose) == 12);
    for (const auto& l : notes.lines) CHECK(l.find("PU2 TSP") == std::string::npos);
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
        // Section 189: byte 1 = A5 is NRx2 and the chip runs it; byte 9 = 00 is
        // no second stage, so the instrument is a plain Chip envelope.
        const auto& lead11 = bank->instruments[0];
        CHECK(lead11.env.mode == bank::EnvMode::Chip);
        CHECK(int(lead11.envVol) == 10); CHECK(int(lead11.envRate) == 5); CHECK(int(lead11.envStage2) == 0); CHECK(int(lead11.envStage3) == 0);
        CHECK(out->phrase(1)->cells[4].cmd1.cmd == bank::Cmd::P);
        // Section 188: the noise instrument takes the shape mode (SHAPE 00) and the
        // cell keeps LSDj's note -- the driver writes EF for A-6 as 8.4.0 does.
        bool found = false;
        for (uint8_t slot : out->chain[3]) if (const auto* p = out->phrase(slot)) {
            found = true;
            REQUIRE(p->cells[0].inst >= 1);
            CHECK(bank->instruments[size_t(p->cells[0].inst - 1)].noiseShapeMode);
            CHECK(int(bank->instruments[size_t(p->cells[0].inst - 1)].noiseShape) == 0);
            CHECK(int(p->cells[0].note) == int(song[kNotes + 16]) + 35);
        }
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
    s[kNotes + 16 + 2] = 64 - 35; s[kPhraseInst + 16 + 2] = 0; s[kCmd + 16 + 2] = letter('L'); s[kCmdV + 16 + 2] = 0x04;
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
    // Section 189: the three bytes stay the chip's -- byte 1 as the Chip
    // envelope, bytes 9 and 10 as the stages the driver writes with a retrigger.
    const auto& in11 = bank->instruments[0];
    REQUIRE(in11.env.mode == bank::EnvMode::Chip);
    CHECK(int(in11.envVol) == 1); CHECK(in11.envDir == bank::EnvDir::Up); CHECK(int(in11.envRate) == 7);
    CHECK(int(in11.envStage2) == 0x47); CHECK(int(in11.envStage3) == 0x20);
    // No byte 9, no stages at all -- byte 10 alone is ignored.
    i0[1] = 0x1F; i0[9] = 0x00;
    REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(11), *bank, *out, sum, notes));
    CHECK(bank->instruments[0].env.mode == bank::EnvMode::Chip);
    CHECK(bank->instruments[0].envRate == 7);
    CHECK(int(bank->instruments[0].envStage2) == 0); CHECK(int(bank->instruments[0].envStage3) == 0);
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
    // Section 188: the instrument takes the shape mode with SHAPE E3 and the cell
    // keeps LSDj's note (C-4 is MIDI 60): the driver makes ~E3 = 1C, octave 1: 2C.
    const int slot = noi->cells[0].inst;
    REQUIRE(slot >= 1);
    const auto& drum = bank->instruments[size_t(slot - 1)];
    CHECK(drum.noiseShapeMode); CHECK(int(drum.noiseShape) == 0xE3); CHECK_FALSE(drum.noiseStable);
    CHECK(int(noi->cells[0].note) == 60);
    // Section 66: the byte goes through as it stands and the instrument's Sweep
    // reads it -- before 9 that is the nibble subtraction on NR43.
    CHECK(drum.noiseDomain == bank::NoiseSweepDomain::Register);
    REQUIRE(noi->cells[0].cmd1.cmd == bank::Cmd::S);
    CHECK(int(noi->cells[0].cmd1.a) == 0xF); CHECK(int(noi->cells[0].cmd1.b) == 0x1);   // SF1, as written
    REQUIRE(noi->cells[1].cmd1.cmd == bank::Cmd::S);
    CHECK(int(noi->cells[1].cmd1.a) == 0xF); CHECK(int(noi->cells[1].cmd1.b) == 0x1);
    CHECK(int(noi->cells[8].note) == 72);                                    // C-5, the tabled drum
    const auto& t = bank->tables[0];
    REQUIRE(t.used);
    // The table's S rows keep their bytes too (section 66).
    CHECK_FALSE(t.steps[0].hasTranspose); CHECK_FALSE(t.steps[1].hasTranspose); CHECK_FALSE(t.steps[2].hasTranspose);
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

TEST_CASE("a 9.x noise instrument brings its PITCH, its LENGTH and LSDj's own note numbers", "[lsdj]")
{
    // Sections 85, 86 and 87, measured on 9.3.9: instrument byte 2 is PITCH
    // (0 FREE, anything else SAFE), byte 3 goes straight into NR41 without the
    // note-on ever enabling the counter, and a phrase's noise note is LSDj's
    // own note byte -- the table's entry number plus one.
    auto song = testSong(22);
    uint8_t* i1 = song.data() + kInst + 16;
    i1[2] = 0x04;                                     // PITCH = SAFE, as SUNRISE's own kick has it
    i1[3] = 0x3F;                                     // LENGTH: NR41 = 3F
    auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;
    REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(22), *bank, *out, sum, notes));
    const auto& noi = bank->instruments[1];
    CHECK(noi.noiseLsdjMap);
    CHECK(noi.noisePitch == bank::NoisePitch::Safe);
    CHECK(noi.lengthLatent);
    CHECK(int(noi.length) == 64 - 0x3F);
    // The cell carries LSDj's note byte, so the grid prints what LSDj prints.
    const auto* p = out->phrase(out->chain[3].at(0));
    REQUIRE(p != nullptr);
    CHECK(int(p->cells[0].note) == 93 - 35);
    CHECK(int(bank->noiseMapNote0) == 1);
    CHECK(noiseByteMatches(*bank, *p, 0, 0x20));
    // PITCH = 0 is FREE.
    i1[2] = 0x00;
    auto bank2 = std::make_unique<bank::Bank>(); auto out2 = std::make_unique<tracker::Song>();
    REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(22), *bank2, *out2, sum, notes));
    CHECK(bank2->instruments[1].noisePitch == bank::NoisePitch::Free);
    // Before 9.2.J the byte is not PITCH, so nothing reads it.
    auto old = testSong(11);
    old[kInst + 16 + 2] = 0x04;
    auto bank3 = std::make_unique<bank::Bank>(); auto out3 = std::make_unique<tracker::Song>();
    REQUIRE(importSong(old.data(), old.size(), *lsdjModelForFormat(11), *bank3, *out3, sum, notes));
    // Before 9.2 no pitch change restarts the channel at all, and byte 2 is
    // the S MODE setting instead (docs/LSDJ_VERSIONS.md).
    CHECK(bank3->instruments[1].noisePitch == bank::NoisePitch::Never);
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
    // Section 88: P08 is eight period-register units a clock, and the byte goes
    // through as it stands -- the instrument's pitchRegisterUnits is what makes
    // the driver move exactly eight rather than the nearest Drum step.
    REQUIRE(pu->cells[4].cmd1.cmd == bank::Cmd::P);
    CHECK(int(pu->cells[4].cmd1.a) == 8);
    CHECK(bank->instruments[0].pitchRegisterUnits);
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

TEST_CASE("a 9.x wave instrument brings its start frame, its FINETUNE and the RESYNC play mode", "[lsdj]")
{
    // Sections 170 and 171: byte 3 is the frame index whole, byte 12 the
    // FINETUNE (a signed byte of 1/256 semitones), byte 9 = 4 is RESYNC.
    auto song = blankSong(22);
    song[kInstAlloc + 0] = 1;
    uint8_t* i0 = song.data() + kInst;
    i0[0] = 1; i0[1] = 0x20; i0[2] = 0x0F; i0[3] = 0x23; i0[7] = 3; i0[9] = 4; i0[10] = 0x08; i0[11] = 0xFE; i0[12] = 0xF0;
    song[kPhraseAlloc] |= 1; song[kNotes] = 60 - 35; song[kPhraseInst] = 0;
    song[kChainPhrases] = 0; song[kRows + 0] = 0xFF; song[kRows + 1] = 0xFF; song[kRows + 2] = 0; song[kRows + 3] = 0xFF;
    auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;
    REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(22), *bank, *out, sum, notes));
    const auto& w = bank->instruments[0];
    CHECK(int(w.frameStart) == 3);
    CHECK(int(w.fineTune) == 0xF0);                                   // -16/256 of a semitone, read signed by the driver
    CHECK(w.frameLoop == bank::FrameLoop::Resync);
    CHECK(int(w.frameLength) == 8);
    CHECK(int(w.frameAdvance) == 2);                                   // FE + 4
    CHECK(int(bank->waves[size_t(w.wave - 1)].frames[0].s[0]) == 0);  // synth 2 is blank in this song, the slot exists
}

TEST_CASE("the wave instrument's synth comes from byte 2 before 9 and REPEAT from its own", "[lsdj]")
{
    // Section 60: the synth is byte 2 up to format 15 and byte 3 from 17.
    // Section 93: REPEAT is byte 2's low nibble on both, and byte 3's on
    // formats 7 and 8 -- it is not the synth byte's.
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
    CHECK(int(w22.frameLoopStep) == 5);                                    // REPEAT still byte 2 (section 93)
    CHECK(int(bank->waves[size_t(w22.wave - 1)].frames[0].s[0]) == 4);      // synth 4, from byte 3
    // Formats 7 and 8 take the synth from byte 2 and REPEAT from byte 3.
    song[kFormatVersionAt] = 7;
    REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(7), *bank, *out, sum, notes));
    const auto& w7 = bank->instruments[0];
    CHECK(int(w7.frameLoopStep) == 0);                                     // byte 3's low nibble is 0
    CHECK(int(bank->waves[size_t(w7.wave - 1)].frames[0].s[0]) == 2);      // synth 2, from byte 2
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
    // Section 102: a **counted** H is a different command -- it hops back inside
    // the phrase rather than ending it -- and the engine plays it now, so it
    // goes through as the cell's own command and the phrase keeps its length.
    song[kCmdV + 3] = 0x21;
    ImportNotes counted;
    REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(22), *bank, *out, sum, counted));
    const auto* p1 = out->phrase(out->chain[0].at(0));
    REQUIRE(p1 != nullptr);
    CHECK(int(p1->steps) == 16);                                    // not cut short
    CHECK(p1->cells[3].cmd1.cmd == bank::Cmd::H);
    CHECK(int(p1->cells[3].cmd1.a) == 2);                           // twice
    CHECK(int(p1->cells[3].cmd1.b) == 1);                           // back to step 1
    {   // and the order it makes: 0 1 2 | 1 2 | 1 2 | 3 4 ... 15
        uint8_t order[tracker::kMaxPlaySteps];
        const int n = tracker::phrasePlayOrder(p1, order, tracker::kMaxPlaySteps);
        REQUIRE(n == 20);
        const uint8_t want[8] = { 0, 1, 2, 1, 2, 1, 2, 3 };
        for (int i = 0; i < 8; ++i) { INFO("position " << i); CHECK(order[i] == want[i]); }
        CHECK(order[19] == 15);
    }
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

TEST_CASE("H FF ends the channel's chain", "[lsdj]")
{
    // Section 120: the ROM stops the channel outright, so ChipBoy's channel
    // has no timeline past it -- a playhead dropped beyond finds silence.
    auto song = blankSong(22);
    song[kInstAlloc + 0] = 1;
    song[kPhraseAlloc] |= 0x07;                     // phrases 0, 1 and 2
    for (int p = 0; p < 3; ++p)
        for (int st = 0; st < 4; ++st) { song[kNotes + size_t(p) * 16 + size_t(st)] = uint8_t(60 + st); song[kPhraseInst + size_t(p) * 16 + size_t(st)] = 0; }
    song[kCmd + 2] = 8; song[kCmdV + 2] = 0xFF;     // phrase 0 step 2: H FF
    song[kChainPhrases + 0] = 0; song[kChainPhrases + 1] = 1; song[kChainPhrases + 2] = 2;
    song[kRows + 0] = 0; song[kRows + 1] = 0xFF; song[kRows + 2] = 0xFF; song[kRows + 3] = 0xFF;
    song[kRows + 4] = 0xFF; song[kRows + 5] = 0xFF; song[kRows + 6] = 0xFF; song[kRows + 7] = 0xFF;
    auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;
    REQUIRE(importSong(song.data(), song.size(), lsdjLatestModel(), *bank, *out, sum, notes, nullptr));
    REQUIRE(out->chain[0].size() == 1);             // the two phrases after it are not laid out
    const auto* p0 = out->phrase(out->chain[0][0]);
    REQUIRE(p0 != nullptr);
    CHECK(int(p0->steps) == 2);                     // and the phrase ends at the H
    CHECK(p0->cells[0].note != 0);
    for (int ch = 1; ch < 4; ++ch) CHECK(out->chain[size_t(ch)].empty());   // the others are untouched
    bool said = false;
    for (const auto& l : notes.lines) if (l.find("HFF") != std::string::npos) said = true;
    CHECK(said);
    {   // Without the H FF the same song lays all three phrases out.
        auto s2 = song;
        s2[kCmd + 2] = 0; s2[kCmdV + 2] = 0;
        auto b2 = std::make_unique<bank::Bank>(); auto o2 = std::make_unique<tracker::Song>();
        ImportSummary m2; ImportNotes n2;
        REQUIRE(importSong(s2.data(), s2.size(), lsdjLatestModel(), *b2, *o2, m2, n2, nullptr));
        CHECK(o2->chain[0].size() == 3);
    }
}

TEST_CASE("a kit instrument's DIST curve sums its two samples", "[lsdj]")
{
    using namespace chipboy::lsdj;
    // Section 117: the four curves, at the points that tell them apart. `s` is
    // the sum of the two nibbles less 8; both operands are 0-15.
    SECTION("the curves") {
        for (int a = 0; a <= 15; ++a) for (int b = 0; b <= 15; ++b) {
            const int s = a + b - 8;
            CHECK(kitDistEntry(KitDist::Clip, a, b) == std::clamp(s, 0, 15));
            CHECK(kitDistEntry(KitDist::Wrap, a, b) == (s & 15));
            CHECK(kitDistEntry(KitDist::Fold, a, b) == (s < 0 ? -s : (s > 15 ? 30 - s : s)));
        }
        // SOFT: a knee at four either side of 8, then half the slope.
        CHECK(kitDistEntry(KitDist::Soft, 8, 8) == 8);
        CHECK(kitDistEntry(KitDist::Soft, 8, 12) == 12);        // s = 12, still 1:1
        CHECK(kitDistEntry(KitDist::Soft, 8, 13) == 12);        // s = 13, the slope halves
        CHECK(kitDistEntry(KitDist::Soft, 8, 14) == 13);
        CHECK(kitDistEntry(KitDist::Soft, 15, 15) == 15);       // s = 22, clamped
        CHECK(kitDistEntry(KitDist::Soft, 8, 4) == 4);          // s = 4
        CHECK(kitDistEntry(KitDist::Soft, 8, 3) == 4);          // s = 3
        CHECK(kitDistEntry(KitDist::Soft, 8, 0) == 2);          // s = 0
        CHECK(kitDistEntry(KitDist::Soft, 0, 0) == 0);          // s = -8, clamped
        // SHAP2 before 9.2: the mirror at twice the slope, and the ROM's one
        // entry that the mirror does not give.
        CHECK(kitDistEntry(KitDist::Fold2, 8, 8) == 8);
        CHECK(kitDistEntry(KitDist::Fold2, 0, 7) == 2);         // s = -1 -> 2
        CHECK(kitDistEntry(KitDist::Fold2, 0, 0) == 15);        // s = -8 -> 16, clamped
        CHECK(kitDistEntry(KitDist::Fold2, 15, 10) == 11);      // s = 17 -> 15 - 4
        CHECK(kitDistEntry(KitDist::Fold2, 12, 15) == 5);       // the odd one
        CHECK(kitDistEntry(KitDist::Fold2, 15, 12) == 7);       // its transpose, as the mirror gives
        // The low nibble of a byte indexes the table the other way round, which
        // is why the two above differ.
        CHECK(kitMix(KitDist::Fold2, 0, 15, 12) == 5);          // even: row is the low digit's sample
        CHECK(kitMix(KitDist::Fold2, 1, 15, 12) == 7);
    }
    SECTION("a mixed kit note") {
        // One ROM bank per kit: kit 00 a ramp, kit 01 a flat 15.
        std::vector<uint8_t> rom(10 * 0x4000, 0);
        auto bankAt = [&](int b, const char* name, const std::vector<uint8_t>& sample) {
            uint8_t* p = rom.data() + size_t(b) * 0x4000;
            p[0] = 0x60; p[1] = 0x40;
            std::memcpy(p + 0x52, name, 6);
            std::memcpy(p + 0x22, "S01", 3);
            for (size_t k = 0; k < sample.size(); k += 2) p[0x60 + k / 2] = uint8_t((sample[k] << 4) | sample[k + 1]);
            const uint16_t end = uint16_t(0x4060 + sample.size() / 2);
            p[2] = uint8_t(end & 0xFF); p[3] = uint8_t(end >> 8);
        };
        std::vector<uint8_t> ramp(64), full(64, 15);
        for (size_t k = 0; k < ramp.size(); ++k) ramp[k] = uint8_t((k + 1) % 16);
        bankAt(8, "RAMPKT", ramp);                          // section 172: kit 00 is bank 8
        bankAt(9, "FULLKT", full);
        const auto kits = readKits(rom.data(), rom.size());
        REQUIRE(kits.size() == 2);

        // The import keeps the pair rather than baking it (plan-kit-pairs): the
        // kit holds both source samples, the note column names the first and
        // the VEL column the second, and the kit carries the curve.
        struct Imported { std::unique_ptr<bank::Bank> bank; std::unique_ptr<tracker::Song> song; };
        auto importPair = [&](uint8_t distByte, int format, const LsdjModel& model, uint8_t noteByte) {
            auto s2 = blankSong(format);
            s2[kInstAlloc + 0] = 1;
            uint8_t* i0 = s2.data() + kInst;
            i0[0] = 2; i0[1] = 0xF0; i0[2] = 0x00; i0[7] = 3; i0[9] = 0x01; i0[10] = distByte;
            s2[kPhraseAlloc] |= 1;
            s2[kNotes + 0] = noteByte; s2[kPhraseInst + 0] = 0;
            s2[kChainPhrases + 0] = 0;
            s2[kRows + 0] = 0xFF; s2[kRows + 1] = 0xFF; s2[kRows + 2] = 0; s2[kRows + 3] = 0xFF;
            Imported out{ std::make_unique<bank::Bank>(), std::make_unique<tracker::Song>() };
            ImportSummary sum; ImportNotes notes;
            REQUIRE(importSong(s2.data(), s2.size(), model, *out.bank, *out.song, sum, notes, &kits));
            return out;
        };
        {   // note 11: sample 1 of each kit
            auto in = importPair(0xD0, 22, lsdjLatestModel(), 0x11);
            const auto& kit = in.bank->kits[0];
            REQUIRE(kit.samples.size() == 2);
            CHECK(kit.samples[0].data == ramp);
            CHECK(kit.samples[1].data == full);
            CHECK(kit.dist == bank::KitDist::Clip);
            const auto* ph = in.song->phrase(1);
            REQUIRE(ph != nullptr);
            CHECK(ph->cells[0].note == kit.samples[0].note);
            CHECK(ph->cells[0].vel == 2);                  // the second sample, index + 1
        }
        {   // one digit only: no second sample
            auto in = importPair(0xD0, 22, lsdjLatestModel(), 0x10);
            REQUIRE(in.bank->kits[0].samples.size() == 1);
            CHECK(in.song->phrase(1)->cells[0].vel == 0);
            auto lo = importPair(0xD0, 22, lsdjLatestModel(), 0x01);
            REQUIRE(lo.bank->kits[0].samples.size() == 1);
            CHECK(lo.bank->kits[0].samples[0].data == full);
            CHECK(lo.song->phrase(1)->cells[0].vel == 0);
        }
        // Each page names its curve, and the list moved at 9.2: before it, D1
        // is the mirror and D2 the steep one.
        CHECK(importPair(0xD1, 22, lsdjLatestModel(), 0x11).bank->kits[0].dist == bank::KitDist::Soft);
        CHECK(importPair(0xD2, 22, lsdjLatestModel(), 0x11).bank->kits[0].dist == bank::KitDist::Fold);
        CHECK(importPair(0xD3, 22, lsdjLatestModel(), 0x11).bank->kits[0].dist == bank::KitDist::Wrap);
        const LsdjModel* old = lsdjModelNamed("LSDj 8.4.0 - 8.5.1 (format 11)");
        REQUIRE(old != nullptr);
        CHECK(importPair(0xD1, 11, *old, 0x11).bank->kits[0].dist == bank::KitDist::Fold);
        CHECK(importPair(0xD2, 11, *old, 0x11).bank->kits[0].dist == bank::KitDist::Fold2);
    }
}

TEST_CASE("a kit instrument takes its samples from the ROM beside the save", "[lsdj]")
{
    // A ROM of two kit banks, built here: bank 8 (kit 00, section 172: a kit
    // is its number plus eight) "TESTKT" with two samples, bank 9 (kit 01)
    // "SECOND" with one.
    std::vector<uint8_t> rom(10 * 0x4000, 0);
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
    bankAt(8, "TESTKT", { ramp, flat });
    bankAt(9, "SECOND", { tri });
    rom[8 * 0x4000 + 0x5C] = 0x02;                          // the header's loop bits: sample 2 of kit 00 loops on its own
    const auto kits = readKits(rom.data(), rom.size());
    REQUIRE(kits.size() == 2);
    CHECK(kits[0].name == "TESTKT"); CHECK(kits[0].bank == 8); CHECK(kits[0].loopBits == 2);
    CHECK(kits[0].samples.size() == 2); CHECK(kits[0].samples[0].nibbles == ramp); CHECK(kits[0].samples[1].nibbles == flat);
    CHECK(kits[1].samples.size() == 1); CHECK(kits[1].samples[0].nibbles == tri);
    CHECK(lsdjKitByNumber(kits, 1) == &kits[1]); CHECK(lsdjKitByNumber(kits, 2) == nullptr);
    // Section 193: a kit number counts kit banks, not banks -- a gap bank is skipped.
    std::vector<uint8_t> gapped(12 * 0x4000, 0);
    std::memcpy(gapped.data() + 8 * 0x4000, rom.data() + 8 * 0x4000, 0x4000);      // bank 8: kit 00
    std::memcpy(gapped.data() + 10 * 0x4000, rom.data() + 9 * 0x4000, 0x4000);     // bank 10: kit 01, bank 9 empty
    const auto kits2 = readKits(gapped.data(), gapped.size());
    REQUIRE(kits2.size() == 2);
    CHECK(kits2[1].bank == 10);
    CHECK(lsdjKitByNumber(kits2, 1) == &kits2[1]); CHECK(lsdjKitByNumber(kits2, 1)->name == "SECOND"); CHECK(lsdjKitByNumber(kits2, 2) == nullptr);
    CHECK(kitPeriodOfSpeed(0x00) == 1865); CHECK(kitPeriodOfSpeed(0xD0) == 1817); CHECK(kitPeriodOfSpeed(0x40) == 1929);
    CHECK(kitPeriodOfSpeed(0x00, true) == 1682);

    // A song with one kit instrument: kit A = 00, kit B = 01, each side cut to
    // two frames by its own LEN (bytes 3 and 11, section 172), speed D0.
    auto song = blankSong(22);
    song[kInstAlloc + 0] = 1;
    uint8_t* i0 = song.data() + kInst; i0[0] = 2; i0[1] = 0xA8; i0[2] = 0x00; i0[3] = 2; i0[7] = 3; i0[8] = 0xD0; i0[9] = 0x01; i0[11] = 2;
    std::memcpy(song.data() + kNames, "DRUMS", 5);
    song[kPhraseAlloc] |= 1;
    song[kNotes + 0] = 0x10; song[kPhraseInst + 0] = 0;     // kit A sample 1: the ramp, two frames of it
    song[kNotes + 4] = 0x20; song[kPhraseInst + 4] = 0;     // kit A sample 2: the flat one
    song[kNotes + 8] = 0x01; song[kPhraseInst + 8] = 0;     // kit B sample 1: the triangle
    song[kNotes + 12] = 0x10; song[kPhraseInst + 12] = 0;   // the ramp again: the same note
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
    CHECK(kit.samples[1].data == flat);                     // already two frames
    CHECK(kit.samples[2].data.size() == 64);                // and the 96-nibble triangle is cut too
    CHECK(inst.kitLoop == bank::KitLoop::Once);             // byte 5 has no LOOP bit
    CHECK(kit.perSampleLoop);
    CHECK(kit.samples[0].loop == bank::KitLoop::Once);
    CHECK(kit.samples[1].loop == bank::KitLoop::Loop);       // the header's own bit, LOOP off on the instrument
    CHECK(int(inst.waveLevel) == 3);                         // byte 1's NR32 code: A8 is 100 %
    {   // Section 172: bit 6 of byte 5 loops side A, bit 5 side B; OFFSET (byte
        // 12) starts A a frame in; ATK (bit 7 of byte 2) starts at the sample's
        // beginning and loops from the offset; bit 6 of byte 2 is half speed.
        i0[5] = 0x60; i0[12] = 1; i0[2] = 0x80 | 0x40;
        auto b2 = std::make_unique<bank::Bank>(); auto o2 = std::make_unique<tracker::Song>();
        ImportSummary s2; ImportNotes n2;
        REQUIRE(importSong(song.data(), song.size(), lsdjLatestModel(), *b2, *o2, s2, n2, &kits));
        CHECK(b2->instruments[0].kitLoop == bank::KitLoop::Loop);
        const auto& k2 = b2->kits[0];
        CHECK(k2.loop == bank::KitLoop::Loop);
        CHECK(k2.halfSpeed); CHECK(k2.period == 1682 - 48);
        REQUIRE(k2.samples.size() == 3);
        CHECK(k2.samples[0].loop == bank::KitLoop::FromPoint);   // ATK
        CHECK(k2.samples[0].loopPoint == 32);                    // from the offset
        CHECK(k2.samples[0].data.size() == 96);                  // the start, then two frames past the offset
        CHECK(k2.samples[2].loop == bank::KitLoop::Loop);        // side B, bit 5
        i0[5] = 0; i0[12] = 0; i0[2] = 0;
    }
    {   // Section 172: a raw DIST page comes from the ROM with the kits.
        i0[10] = 0x8E;
        LsdjRawPages pages; pages[0x8E] = std::vector<uint8_t>(256, 0x0F);
        auto b3 = std::make_unique<bank::Bank>(); auto o3 = std::make_unique<tracker::Song>();
        ImportSummary s3; ImportNotes n3;
        REQUIRE(importSong(song.data(), song.size(), lsdjLatestModel(), *b3, *o3, s3, n3, &kits, &pages));
        CHECK(b3->kits[0].dist == bank::KitDist::Raw);
        REQUIRE(b3->kits[0].distTable.size() == 256);
        CHECK(bank::kitMixByte(b3->kits[0], 0x12, 0x34) == uint8_t(0xF0 + 0x0F));   // swap(T) + T, eight bits
        i0[10] = 0xD0;
    }
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

TEST_CASE("inside format 22 a 9.2 song's wave R nibble and kit vibrato depth are translated to 9.4.2's", "[lsdj][versions]")
{
    // docs/LSDJ_VERSIONS.md section 11, probed 9.2.J against 9.4.2: before
    // 9.3.4 R's volume nibble does nothing on the wave channel (kits too), and
    // before 9.4.0 a kit's V is twice as deep. A wave R F4 becomes R 04; the
    // kit's V bytes stay and the kit instrument takes `vibDouble` (section
    // 187); the 9.4.x model keeps every byte.
    auto song = blankSong(22);
    song[kInstAlloc + 0] = 1; song[kInstAlloc + 1] = 1;
    uint8_t* i0 = song.data() + kInst; i0[0] = 1; i0[1] = 0xA8; i0[3] = 0x00; i0[7] = 3; i0[10] = 0xD0;           // a wave
    uint8_t* i1 = song.data() + kInst + 16; i1[0] = 2; i1[1] = 0xA8; i1[2] = 0x00; i1[7] = 3; i1[8] = 0xD0; i1[9] = 0x00; i1[10] = 0xD0;   // a kit
    std::memcpy(song.data() + kNames, "WAVE", 4); std::memcpy(song.data() + kNames + 5, "DRUMS", 5);
    song[kPhraseAlloc] |= 1;
    const uint8_t R = 14, V = 17;                            // "-ABCDEFGHKLMOPRSTVWZ"
    song[kNotes + 0] = 0x34; song[kPhraseInst + 0] = 0; song[kCmd + 0] = R; song[kCmdV + 0] = 0xF4;
    song[kNotes + 4] = 0x10; song[kPhraseInst + 4] = 1; song[kCmd + 4] = V; song[kCmdV + 4] = 0x42;
    song[kNotes + 8] = 0x10; song[kPhraseInst + 8] = 1; song[kCmd + 8] = V; song[kCmdV + 8] = 0x4A;
    song[kNotes + 12] = 0x34; song[kPhraseInst + 12] = 0; song[kCmd + 12] = R; song[kCmdV + 12] = 0x84;
    song[kCmd + 6] = 6; song[kCmdV + 6] = 0x01;              // a bare F 01 on the kit: the frame to start over from (section 186)
    song[kChainPhrases + 0] = 0;
    song[kRows + 0] = 0xFF; song[kRows + 1] = 0xFF; song[kRows + 2] = 0; song[kRows + 3] = 0xFF;
    // A kit for the kit instrument to take its samples from (as the kit test builds one).
    std::vector<uint8_t> rom(9 * 0x4000, 0);
    {
        uint8_t* b = rom.data() + 8 * 0x4000;
        b[0] = 0x60; b[1] = 0x40; std::memcpy(b + 0x52, "TESTKT", 6); std::memcpy(b + 0x22, "S01", 3);
        std::vector<uint8_t> ramp(64); for (size_t k = 0; k < ramp.size(); ++k) ramp[k] = uint8_t(k % 16);
        for (size_t k = 0; k < ramp.size(); k += 2) b[0x60 + k / 2] = uint8_t((ramp[k] << 4) | ramp[k + 1]);
        b[2] = uint8_t((0x4060 + 32) & 0xFF); b[3] = uint8_t((0x4060 + 32) >> 8);
    }
    const auto kits = readKits(rom.data(), rom.size());
    REQUIRE(kits.size() == 1);
    const LsdjModel* old = lsdjModelNamed("LSDj 9.2.J - 9.3.3 (format 22)");
    REQUIRE(old != nullptr);
    CHECK_FALSE(old->waveRetrigNibble); CHECK_FALSE(old->kitVibratoHalved); CHECK_FALSE(old->retrigResetsDrumPitch);
    CHECK(lsdjModelForRomVersion("9.2.J") == old); CHECK(lsdjModelForRomVersion("9.2.L") == old); CHECK(lsdjModelForRomVersion("9.3.3") == old);
    CHECK(lsdjModelForRomVersion("9.3.4")->waveRetrigNibble); CHECK_FALSE(lsdjModelForRomVersion("9.3.9")->kitVibratoHalved);
    CHECK(lsdjModelForRomVersion("9.4.0")->kitVibratoHalved); CHECK(&lsdjLatestModel() == lsdjModelForRomVersion("9.4.2"));
    CHECK(lsdjModelForFormat(22) == &lsdjLatestModel());
    for (const LsdjModel* m : { old, &lsdjLatestModel() }) {
        auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
        ImportSummary sum; ImportNotes notes;
        REQUIRE(importSong(song.data(), song.size(), *m, *bank, *out, sum, notes, &kits));
        const auto* ph = out->phrase(1);
        REQUIRE(ph != nullptr);
        const bool o = m == old;
        CHECK(ph->cells[0].cmd1.cmd == bank::Cmd::R); CHECK(ph->cells[0].cmd1.a == (o ? 0 : 15)); CHECK(ph->cells[0].cmd1.b == 4);
        CHECK(ph->cells[4].cmd1.cmd == bank::Cmd::V); CHECK(ph->cells[4].cmd1.a == 4); CHECK(ph->cells[4].cmd1.b == 2);
        CHECK(ph->cells[8].cmd1.cmd == bank::Cmd::V); CHECK(ph->cells[8].cmd1.b == 10);
        CHECK(bank->instruments[1].type == bank::InstrumentType::Kit); CHECK(bank->instruments[1].vibDouble == o);   // section 187: the depth lives on the kit
        CHECK(ph->cells[12].cmd1.cmd == bank::Cmd::R); CHECK(ph->cells[12].cmd1.a == 8);            // the resync is not a volume
        CHECK(ph->cells[6].cmd1.cmd == bank::Cmd::F); CHECK(ph->cells[6].cmd1.a == 0); CHECK(ph->cells[6].cmd1.b == 1);
        for (const auto& n : notes.lines) CHECK(n.find("V4A") == std::string::npos);
    }
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
    REQUIRE(slot >= 1); REQUIRE(slot <= tracker::kGrooveSlots);
    CHECK(int(out->grooves[size_t(slot - 1)].ticks[0]) == 7);    // one step of seven: every row the same
    CHECK(int(out->grooves[size_t(slot - 1)].ticks[1]) == 0);
    CHECK(int(out->grooves[1].ticks[1]) == 4);                   // the song's groove 1 is left alone
    bool told = false;
    for (const auto& l : oldNotes.lines) if (l.find("one step groove") != std::string::npos) told = true;
    CHECK(told);
}

TEST_CASE("a pulse instrument's LENGTH comes off byte 3, enable bit and all", "[import][instrument]")
{
    // Section 134: byte 3's low six bits are the length code and bit 6 enables
    // the counter. The importer read the byte only for noise, and there always
    // marked it latent; both types follow the same rule.
    auto song = testSong(22);
    auto bank = std::make_unique<bank::Bank>();
    auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;
    uint8_t* i0 = song.data() + kInst;
    const auto readBack = [&](uint8_t b3, bool noise) {
        std::fill(i0, i0 + 16, uint8_t(0));
        i0[0] = noise ? 3 : 0; i0[1] = 0xF0; i0[3] = b3; i0[7] = 3;
        REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(22), *bank, *out, sum, notes));
        const auto& in = bank->instruments[0];
        return std::pair<int, bool>{ int(in.length), in.lengthLatent };
    };
    for (int which = 0; which < 2; ++which) {
        const bool noise = which == 1;
        INFO(std::string(noise ? "noise" : "pulse") + " instrument");
        CHECK(readBack(0x00, noise) == std::pair<int, bool>{ 0, false });       // no length at all
        CHECK(readBack(0x3B, noise) == std::pair<int, bool>{ 5, true });        // the code, not armed
        CHECK(readBack(0x7B, noise) == std::pair<int, bool>{ 5, false });       // and armed
        CHECK(readBack(0x40, noise) == std::pair<int, bool>{ 64, false });
        CHECK(readBack(0xBB, noise) == std::pair<int, bool>{ 5, true });        // bit 7 is ignored
    }
}

TEST_CASE("a cell with a blank instrument column keeps its note", "[lsdj]")
{
    // Section 101, measured on 3.6.5, 4.0.4 and 9.2.L. From 4.0.4 such a cell
    // does not trigger -- it moves the channel to that note, which is ChipBoy's
    // own bare note -- and an `L` on the same row slides to it. Before 4.0.4 it
    // triggers with the channel's last instrument. It used to be dropped, which
    // lost the note and the row's command with it.
    const auto build = [](int format) {
        auto song = blankSong(format);
        song[kInstAlloc + 0] = 1;
        uint8_t* i0 = song.data() + kInst; i0[0] = 0; i0[1] = 0xF0; i0[7] = 0xC3;
        song[kPhraseAlloc] |= 1;
        song[kNotes + 0] = 60 - 35; song[kPhraseInst + 0] = 0;                  // a note with its instrument
        song[kNotes + 4] = 67 - 35; song[kPhraseInst + 4] = 0xFF;               // and one without
        song[kChainPhrases + 0] = 0;
        song[kRows + 0] = 0; song[kRows + 1] = 0xFF; song[kRows + 2] = 0xFF; song[kRows + 3] = 0xFF;
        return song;
    };
    {   // format 22: the note stands, the instrument column stays blank
        auto song = build(22);
        auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
        ImportSummary sum; ImportNotes notes;
        REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(22), *bank, *out, sum, notes));
        const auto* p = out->phrase(1);
        REQUIRE(p != nullptr);
        CHECK(int(p->cells[0].note) == 60); CHECK(int(p->cells[0].inst) != 0);
        CHECK(int(p->cells[4].note) == 67); CHECK(int(p->cells[4].inst) == 0);   // bare: no trigger
    }
    {   // format 0: the same cell sounds, so the column is filled in
        auto song = build(0);
        auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
        ImportSummary sum; ImportNotes notes;
        REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(0), *bank, *out, sum, notes));
        const auto* p = out->phrase(1);
        REQUIRE(p != nullptr);
        CHECK(int(p->cells[4].note) == 67); CHECK(int(p->cells[4].inst) != 0);
    }
}

TEST_CASE("the 9.x noise map is the ROM's, generated from the clock order", "[import][noise][rom942]")
{
    // Section 156: bank 02:$5EE4 on 9.4.2, checked byte for byte there; these
    // are the entries the measured table had shifted or missing.
    const auto* m = lsdjModelForFormat(22);
    REQUIRE(m != nullptr); REQUIRE(m->noiseMap != nullptr);
    const uint8_t* map = m->noiseMap;
    CHECK(map[0] == 0xD7); CHECK(map[3] == 0xD4); CHECK(map[4] == 0xC7);
    CHECK(map[12] == 0xA7); CHECK(map[13] == 0xB3); CHECK(map[15] == 0xD0);
    CHECK(map[31] == 0x90); CHECK(map[55] == 0x30); CHECK(map[59] == 0x00);
    CHECK(map[60] == 0xDF); CHECK(map[63] == 0xDC); CHECK(map[71] == 0xD9); CHECK(map[119] == 0x08);
    for (int i = 0; i < 60; ++i) { CHECK((map[i] & 8) == 0); CHECK(map[60 + i] == (map[i] | 8)); }
}

TEST_CASE("a G past 0F names one of LSDj's thirty-two grooves", "[lsdj][groove][rom942]")
{
    // Section 162: `REACTION`'s noise phrase opens with `G 12`; LSDj's slot
    // $12 is ChipBoy's 19, and its ticks come with it.
    const auto letter = [](char c) { const char* t = "-ABCDEFGHKLMOPRSTVWZ"; return uint8_t(std::strchr(t, c) - t); };
    auto song = blankSong(22);
    song[kGrooves + 0x12 * 16 + 0] = 9; song[kGrooves + 0x12 * 16 + 1] = 3; song[kGrooves + 0x12 * 16 + 2] = 6;
    song[kGrooves + 0x1F * 16 + 0] = 2;                                     // the last slot too
    song[kInstAlloc + 0] = 1;
    uint8_t* i0 = song.data() + kInst; i0[0] = 0; i0[1] = 0xA5; i0[4] = 0xFF; i0[7] = 0x80 | 3;
    song[kPhraseAlloc] |= 1; song[kNotes] = uint8_t(60 - 35); song[kPhraseInst] = 0;
    song[kCmd + 0] = letter('G'); song[kCmdV + 0] = 0x12;
    song[kCmd + 1] = letter('G'); song[kCmdV + 1] = 0x1F;
    song[kChainPhrases] = 0;
    song[kRows + 0] = 0; song[kRows + 1] = 0xFF; song[kRows + 2] = 0xFF; song[kRows + 3] = 0xFF;
    auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;
    REQUIRE(importSong(song.data(), song.size(), *lsdjModelForFormat(22), *bank, *out, sum, notes));
    REQUIRE(out->phrases[0].cells[0].cmd1.cmd == bank::Cmd::G);
    CHECK(int(out->phrases[0].cells[0].cmd1.a) == 0x12 + 1);
    CHECK(int(out->phrases[0].cells[1].cmd1.a) == 0x1F + 1);
    CHECK(int(out->grooves[0x12].ticks[0]) == 9); CHECK(int(out->grooves[0x12].ticks[1]) == 3); CHECK(int(out->grooves[0x12].ticks[2]) == 6);
    CHECK(int(out->grooves[0x1F].ticks[0]) == 2); CHECK(int(out->grooves[0x1F].ticks[1]) == 0);
    CHECK(int(out->grooves[0x10].ticks[0]) == 6);                             // an empty slot is LSDj's 6 6
}

TEST_CASE("a 9.x instrument's envelope bytes come through for the ROM's machine", "[lsdj][rom942]")
{
    // Section 164: bytes 1, 9 and 10 verbatim beside the shaped picture; a
    // first rate of zero is a held level and brings none.
    auto build = [](uint8_t b1, uint8_t b9, uint8_t b10) {
        auto song = blankSong(22);
        song[kInstAlloc + 0] = 1;
        uint8_t* i0 = song.data() + kInst; i0[0] = 0; i0[1] = b1; i0[4] = 0xFF; i0[7] = 0x80 | 3; i0[9] = b9; i0[10] = b10;
        song[kPhraseAlloc] |= 1; song[kNotes] = uint8_t(60 - 35); song[kPhraseInst] = 0;
        song[kChainPhrases] = 0;
        song[kRows + 0] = 0; song[kRows + 1] = 0xFF; song[kRows + 2] = 0xFF; song[kRows + 3] = 0xFF;
        return song;
    };
    auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;
    const auto a = build(0xF3, 0x85, 0x46);
    REQUIRE(importSong(a.data(), a.size(), *lsdjModelForFormat(22), *bank, *out, sum, notes));
    CHECK(bank->instruments[0].env.mode == bank::EnvMode::Shaped);
    CHECK(bank->instruments[0].env.lsdj);
    CHECK(int(bank->instruments[0].env.lsdjByte1) == 0xF3); CHECK(int(bank->instruments[0].env.lsdjByte9) == 0x85); CHECK(int(bank->instruments[0].env.lsdjByte10) == 0x46);
    CHECK(int(bank->instruments[0].env.start) == 15);
    const auto h = build(0xF0, 0x85, 0x46);
    REQUIRE(importSong(h.data(), h.size(), *lsdjModelForFormat(22), *bank, *out, sum, notes));
    CHECK_FALSE(bank->instruments[0].env.lsdj);
    CHECK(bank->instruments[0].env.mode == bank::EnvMode::Chip);
}

TEST_CASE("a format-11 song's noise, finetune and envelope stages come in as the 8.5.1 ROM plays them", "[lsdj][versions]")
{
    // Sections 188, 189 and 191: the noise instrument takes the shape mode
    // (SHAPE from byte 4, S MODE from byte 2), the pulse its FINETUNE nibble
    // from byte 7 bits 2-5 as eight times the 9.x unit, and the three envelope
    // bytes stay Chip-mode bytes with two stages instead of a shaped walk.
    auto song = blankSong(11);
    song[kInstAlloc + 0] = 1;
    uint8_t* i0 = song.data() + kInst; i0[0] = 0; i0[1] = 0xA3; i0[4] = 0xFF; i0[7] = uint8_t(0x80 | (0x0F << 2) | 3); i0[9] = 0x54; i0[10] = 0x20;
    song[kInstAlloc + 1] = 1;
    uint8_t* i1 = song.data() + kInst + 16; i1[0] = 3; i1[1] = 0xF0; i1[2] = 0x01; i1[4] = 0x0F; i1[7] = 3;
    song[kPhraseAlloc] |= 3;
    song[kNotes] = uint8_t(60 - 35); song[kPhraseInst] = 0;                                   // phrase 0: the pulse
    song[kNotes + 16] = 0x20; song[kPhraseInst + 16] = 1; song[kCmd + 16] = 0x0F; song[kCmdV + 16] = 0x03;   // phrase 1: the noise note G-5 with S 03 (S is 0F in the 8.4 letter table)
    song[kChainPhrases] = 0; song[kChainPhrases + 16] = 1;
    song[kRows + 0] = 0; song[kRows + 1] = 0xFF; song[kRows + 2] = 0xFF; song[kRows + 3] = 1;
    song[kRows + 4] = 0xFF; song[kRows + 5] = 0xFF; song[kRows + 6] = 0xFF; song[kRows + 7] = 0xFF;
    auto bank = std::make_unique<bank::Bank>(); auto out = std::make_unique<tracker::Song>();
    ImportSummary sum; ImportNotes notes;
    const auto* m = lsdjModelForFormat(11);
    REQUIRE(m != nullptr); CHECK(m->fineTuneNibble);
    REQUIRE(importSong(song.data(), song.size(), *m, *bank, *out, sum, notes));
    const auto& pu = bank->instruments[0];
    CHECK(pu.env.mode == bank::EnvMode::Chip);
    CHECK(int(pu.envVol) == 0xA); CHECK(int(pu.envRate) == 3); CHECK(pu.envDir == bank::EnvDir::Down);
    CHECK(int(pu.envStage2) == 0x54); CHECK(int(pu.envStage3) == 0x20);
    CHECK(int(pu.fineTune) == 0x0F * 8);
    CHECK(pu.retrigKeepsPitch);                                            // section 195: before 9.4.0
    // Section 196: 3.6.8 - 5.0.3 read the same nibble as period units, capped at a semitone.
    const auto* m3 = lsdjModelForRomVersion("4.7.3");
    REQUIRE(m3 != nullptr); CHECK(m3->fineTuneNibble); CHECK(m3->fineTuneUnits);
    {
        auto s3 = song; s3[kFormatVersionAt] = 3;
        auto bank3 = std::make_unique<bank::Bank>(); auto out3 = std::make_unique<tracker::Song>();
        ImportSummary sum3; ImportNotes notes3;
        REQUIRE(importSong(s3.data(), s3.size(), *m3, *bank3, *out3, sum3, notes3));
        CHECK(int(bank3->instruments[0].fineTune) == 255);                   // F units is past a semitone: the cap
        s3[kInst + 7] = uint8_t(0x80 | (2 << 2) | 3);
        REQUIRE(importSong(s3.data(), s3.size(), *m3, *bank3, *out3, sum3, notes3));
        CHECK(int(bank3->instruments[0].fineTune) == 84);                    // two units, about 42/256 each
    }
    CHECK_FALSE(lsdjModelForFormat(22)->fineTuneUnits);
    const auto& noi = bank->instruments[1];
    CHECK(noi.noiseShapeMode); CHECK(int(noi.noiseShape) == 0x0F); CHECK(noi.noiseStable);
    CHECK_FALSE(noi.noiseLsdjMap);
    CHECK(noi.noiseDomain == bank::NoiseSweepDomain::Register);
    // The cell keeps the LSDj note (0x20 is MIDI 67) and the S its byte.
    const auto& c = out->phrases[1].cells[0];
    CHECK(int(c.note) == 0x20 + 35);
    CHECK(c.cmd1.cmd == bank::Cmd::S); CHECK(int(c.cmd1.a) == 0); CHECK(int(c.cmd1.b) == 3);
    // 9.x keeps byte 11: the same bytes under the 9.4.2 model read 0 there.
    CHECK_FALSE(lsdjModelForFormat(22)->fineTuneNibble);
}
