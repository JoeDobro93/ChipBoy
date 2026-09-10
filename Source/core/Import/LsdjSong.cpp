// ChipBoy -- an LSDj song into a bank and a song (docs/plan-lsdj-import.md
// section 4; the rules are docs/COMMANDS_AND_TEMPO.md sections 45-52).
#include "core/Import/LsdjSong.h"

#include "core/Driver/Driver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <set>

namespace chipboy::lsdj {

using bank::Cmd;
using bank::Command;

void ImportNotes::add(const std::string& line)
{
    for (const auto& l : lines) if (l == line) return;
    lines.push_back(line);
}

namespace {

// The song's layout (plan section 2).
constexpr size_t kPhraseNotes = 0x0000, kGrooves = 0x1090, kSongRows = 0x1290, kTableEnv = 0x1690, kInstNames = 0x1E7A;
constexpr size_t kTableAlloc = 0x2020, kInstAlloc = 0x2040, kChainPhrases = 0x2080, kChainTsp = 0x2880, kInstParams = 0x3080;
constexpr size_t kTableTsp = 0x3480, kTableCmd1 = 0x3680, kTableCmd1V = 0x3880, kTableCmd2 = 0x3A80, kTableCmd2V = 0x3C80;
constexpr size_t kPhraseAlloc = 0x3E82, kTempo = 0x3FB4, kPhraseCmd = 0x4000, kPhraseCmdV = 0x4FF0, kWaves = 0x6000, kPhraseInst = 0x7000;
constexpr int kLsdjTables = 32, kLsdjInstruments = 64, kLsdjPhrases = 255, kLsdjChains = 128;
constexpr double kPitchClockMs = 11712.0 * 1000.0 / 4194304.0;   // 2.7924 ms

int signedByte(int b) { return b >= 128 ? b - 256 : b; }
std::string hex2(int v) { static const char* d = "0123456789ABCDEF"; std::string s; s += d[(v >> 4) & 15]; s += d[v & 15]; return s; }
double noiseClockHz(int shift, int divisor) { return 524288.0 / (divisor == 0 ? 0.5 : double(divisor)) / double(1u << (shift + 1)); }

/// The interpreter's state for one song.
struct Reader {
    const uint8_t* s;
    const LsdjModel& m;
    bank::Bank& bank;
    tracker::Song& song;
    ImportNotes& notes;
    double tickMs = 0.0;
    std::array<int, kLsdjInstruments> instType{};        // -1 none, 0 pulse, 1 wave, 2 kit, 3 noise
    std::array<bool, kLsdjInstruments> instTranspose{};
    std::map<int, int> waveSlotOfSynth;                  // LSDj synth -> ChipBoy wave slot
    std::map<int, std::set<bool>> noiseWidths;           // instrument -> the LFSR widths its notes take
    std::map<std::pair<int, int>, int> phraseSlot;       // (LSDj phrase, channel) -> ChipBoy phrase slot
    int phrasesOut = 0;
    std::array<double, 128> chipboyClock{};

    Reader(const uint8_t* bytes, const LsdjModel& model, bank::Bank& b, tracker::Song& so, ImportNotes& n)
        : s(bytes), m(model), bank(b), song(so), notes(n)
    {
        instType.fill(-1);
        for (int k = 0; k < 128; ++k) { uint8_t sh = 0, d = 0; driver::Driver::noisePairForNote(k, sh, d); chipboyClock[size_t(k)] = noiseClockHz(sh, d); }
    }

    // --- bytes ----------------------------------------------------------
    uint8_t at(size_t i) const { return s[i]; }
    const uint8_t* inst(int i) const { return s + kInstParams + size_t(i) * 16; }
    std::string instName(int i) const
    {
        std::string n;
        for (int k = 0; k < 5; ++k) { const uint8_t c = at(kInstNames + size_t(i) * 5 + size_t(k)); if (c == 0) break; n += (c >= 32 && c < 127) ? char(c) : '?'; }
        while (!n.empty() && n.back() == ' ') n.pop_back();
        return n;
    }
    char letterOf(int code) const
    {
        const char* t = m.commandLetters;
        for (int k = 0; t[k] != 0; ++k) if (k == code) return k == 0 ? 0 : t[k];
        return 0;
    }
    bool phraseAllocated(int p) const { return (at(kPhraseAlloc + size_t(p / 8)) >> (p % 8)) & 1; }

    // --- noise (section 45) ---------------------------------------------
    uint8_t lsdjNr43(int midi)
    {
        const int n = std::clamp(midi, 0, 127);
        if (m.noiseMap[size_t(n)] != 0xFF) return m.noiseMap[size_t(n)];
        // Outside the measured range: the octave neighbour inside it.
        int lo = 0, hi = 127;
        while (lo < 128 && m.noiseMap[size_t(lo)] == 0xFF) ++lo;
        while (hi > lo && m.noiseMap[size_t(hi)] == 0xFF) --hi;
        notes.add("noise note " + std::to_string(midi) + " is outside the measured map; mapped as its octave's neighbour");
        int k = n;
        while (k < lo) k += 12;
        while (k > hi) k -= 12;
        return m.noiseMap[size_t(std::clamp(k, lo, hi))];
    }
    int noteForNr43(uint8_t v, int prefer)
    {
        const int n = chipboyNoteForNr43(v, prefer);
        const double c = noiseClockHz(v >> 4, v & 7);
        if (std::fabs(std::log(chipboyClock[size_t(n)]) - std::log(c)) > 1e-6)
            notes.add("noise NR43 " + hex2(v) + ": no ChipBoy note has exactly its clock; the nearest note is used");
        return n;
    }

    // --- envelope (section 51) ------------------------------------------
    int envTicks(int delta, int speed) const
    {
        if (speed == 0 || delta == 0) return 0;
        const double ms = std::abs(delta) * double(m.envPeriods[size_t(speed & 15)]) * kPitchClockMs;
        return std::clamp(int(std::lround(ms / tickMs)), 1, 255);
    }
    void envelope(const uint8_t* b, bank::Instrument& o, const std::string& name)
    {
        if (!m.stagedEnvelope) {
            // The byte is NRx2: the chip's own envelope, exactly.
            o.env.mode = bank::EnvMode::Chip;
            o.envVol = uint8_t(b[1] >> 4); o.envDir = (b[1] & 8) ? bank::EnvDir::Up : bank::EnvDir::Down; o.envRate = uint8_t(b[1] & 7);
            return;
        }
        const int a1 = b[1] >> 4, s1 = b[1] & 15, a2 = b[9] >> 4, s2 = b[9] & 15, a3 = b[10] >> 4, s3 = b[10] & 15;
        o.envVol = uint8_t(a1);
        if (s1 == 0) { o.env.mode = bank::EnvMode::Chip; o.envDir = bank::EnvDir::Down; o.envRate = 0; return; }   // a held level
        o.env.mode = bank::EnvMode::Shaped;
        o.env.start = uint8_t(a1); o.env.attackTicks = uint8_t(envTicks(a1 - a2, s1)); o.env.peak = uint8_t(a2); o.env.releaseTicks = 0;
        if (s2 == 0) { o.env.decayTicks = 0; o.env.sustain = uint8_t(a2); o.env.fadeTicks = 0; }
        else {
            o.env.decayTicks = uint8_t(envTicks(a2 - a3, s2)); o.env.sustain = uint8_t(a3);
            if (s3) { o.env.fadeTicks = uint8_t(envTicks(a3, s3)); o.env.fadeTo = 0; }
        }
        for (int sp : { s1, s2, s3 })
            if (sp && double(m.envPeriods[size_t(sp)]) * kPitchClockMs < tickMs) { notes.add("instrument " + name + ": an envelope stage faster than a tick a level is quantised to the tick"); break; }
    }

    // --- instruments ------------------------------------------------------
    static bank::PitchSpeed pitchSpeedOf(uint8_t b5)
    {
        return (b5 & 0x80) ? bank::PitchSpeed::Step : (b5 & 0x40) ? bank::PitchSpeed::Drum : (b5 & 0x10) ? bank::PitchSpeed::Tick : bank::PitchSpeed::Fast;
    }
    int waveSlotFor(int synth)
    {
        auto it = waveSlotOfSynth.find(synth);
        if (it != waveSlotOfSynth.end()) return it->second;
        const int slot = int(waveSlotOfSynth.size()) + 1;
        waveSlotOfSynth[synth] = slot;
        auto& w = bank.waves[size_t(slot - 1)];
        w = bank::Wave{};
        w.used = true; w.name = "LSDj synth " + hex2(synth).substr(1);
        w.frames.resize(16);
        for (int f = 0; f < 16; ++f)
            for (int k = 0; k < 16; ++k) {
                const uint8_t x = at(kWaves + size_t(synth * 16 + f) * 16 + size_t(k));
                w.frames[size_t(f)].s[size_t(2 * k)] = uint8_t(x >> 4);
                w.frames[size_t(f)].s[size_t(2 * k + 1)] = uint8_t(x & 15);
            }
        return slot;
    }
    void instruments(ImportSummary& sum)
    {
        for (int i = 0; i < kLsdjInstruments; ++i) {
            if (!at(kInstAlloc + size_t(i))) continue;
            const uint8_t* b = inst(i);
            const int t = b[0];
            std::string name = instName(i);
            if (name.empty()) name = "Inst " + hex2(i);
            if (t == 2) { notes.add("instrument " + hex2(i) + " " + name + " is a kit: its samples live in the ROM, not the save; skipped"); continue; }
            if (t > 3) { notes.add("instrument " + hex2(i) + " has an unknown type " + std::to_string(t) + "; skipped"); continue; }
            const auto type = t == 0 ? bank::InstrumentType::Pulse : t == 1 ? bank::InstrumentType::Wave : bank::InstrumentType::Noise;
            bank::Instrument o = bank::Instrument::defaults(type, name.c_str());
            o.name = name;
            o.pan = bank::Pan(b[7] & 3); o.length = 0; o.noteOff = bank::NoteOff::Kill;
            o.cmdRate = uint8_t(b[8] & 15); o.chordRate = o.cmdRate;
            o.tableMode = (b[5] & 0x08) ? bank::TableMode::Step : bank::TableMode::Tick;
            o.vib.shape = bank::VibShape(std::min(2, (b[5] >> 1) & 3)); o.vib.dir = (b[5] & 1) ? bank::VibDir::Up : bank::VibDir::Down;
            o.vib.speed = 8; o.vib.depth = 0; o.vib.delay = 0;
            o.table = (b[6] & 0x20) ? uint8_t((b[6] & 0x1F) + 1) : uint8_t(0);
            o.transpose = !(b[5] & 0x20);
            instTranspose[size_t(i)] = o.transpose;
            if (t == 0 || t == 3) envelope(b, o, name);
            if (t == 0) {
                o.duty = uint8_t(b[7] >> 6); o.dutySeqLen = 0; o.pitchSpeed = pitchSpeedOf(b[5]);
                const int nr10 = (~b[4]) & 0xFF;
                o.sweepRate = uint8_t((nr10 >> 4) & 7); o.sweepDown = (nr10 & 8) != 0; o.sweepShift = uint8_t(nr10 & 7);
                if (m.pu2Transpose && b[2]) { o.pu2Transpose = int8_t(signedByte(b[2])); notes.add("pulse instrument " + name + " carries PU2 TSP " + hex2(b[2]) + " (" + std::to_string(signedByte(b[2])) + " semitones), kept as its PU2 transpose"); }
                if (b[11]) notes.add("pulse instrument " + name + " has finetune " + hex2(b[11]) + ": ChipBoy has no finetune");
            } else if (t == 1) {
                static const uint8_t kLevel[4] = { 0, 3, 2, 1 };       // the stored bits are the NR32 code, 1 = 100 %
                o.waveLevel = kLevel[(b[1] >> 5) & 3];
                const int synth = b[3] >> 4, w = b[3] & 15;
                o.wave = uint8_t(waveSlotFor(synth)); o.frameAdvance = 0; o.frameLoop = bank::FrameLoop::Loop;
                o.pitchSpeed = pitchSpeedOf(b[5]);
                if (w) notes.add("wave instrument " + name + " starts at wave " + hex2(w).substr(1) + " of synth " + hex2(synth).substr(1) + ", not at the first frame");
                if (b[9] & 3) notes.add("wave instrument " + name + ": PLAY / SPEED / LENGTH frame animation is not mapped");
            } else {
                o.lfsr7 = false; o.noiseManual = false; o.noiseShift = 5; o.noiseDivisor = 1; o.noiseSweep = 0;
            }
            o.used = true;
            instType[size_t(i)] = t;
            bank.instruments[size_t(i)] = o;
            ++sum.instruments;
        }
    }

    // --- commands (sections 34, 46, 49) -----------------------------------
    /// A phrase or table command into a ChipBoy one. `cell` takes an A as its
    /// TBL column; `instKind` and `channel` decide E, F and W.
    bool command(char letter, int v, const std::string& where, int instKind, int channel, tracker::Cell* cell, Command& out)
    {
        const int x = v >> 4, y = v & 15;
        out = Command{};
        switch (letter) {
            case 'A':
                if (cell) {
                    if (v == 0x20) { notes.add("A20 (table stop) at " + where + ": the table column cannot say 'stop'; dropped"); return false; }
                    cell->table = uint8_t(std::min(v + 1, int(bank::kTableSlots)));
                    return false;
                }
                out = { Cmd::A, int16_t(v == 0x20 ? 0 : std::min(v + 1, int(bank::kTableSlots))), 0, 0 }; return true;
            case 'C': out = { Cmd::C, int16_t(x), int16_t(y), 0 }; return true;
            case 'V': out = { Cmd::V, int16_t(x), int16_t(y), 0 }; return true;
            case 'Z': out = { Cmd::Z, int16_t(x), int16_t(y), 0 }; return true;
            case 'M': out = { Cmd::M, int16_t(x), int16_t(y), 0 }; return true;
            case 'R': out = { Cmd::R, int16_t(x), int16_t(y), 0 }; return true;
            case 'H': out = { Cmd::H, int16_t(x), int16_t(y), 0 }; return true;            // times, row: 0-based in both
            case 'E':
                if (instKind == 1) out = { Cmd::E, int16_t(std::min(3, y)), 0, 0 };
                else out = { Cmd::E, int16_t(x), int16_t(y), 0 };
                return true;
            case 'S': out = { Cmd::S, int16_t(x & 7), int16_t(y), 0 }; return true;
            case 'D': out = { Cmd::D, int16_t(v), 0, 0 }; return true;
            case 'K': out = { Cmd::K, int16_t(v), 0, 0 }; return true;
            case 'L': out = { Cmd::L, int16_t(v), 0, 0 }; return true;
            case 'P': out = { Cmd::P, int16_t(v), 0, 0 }; return true;                     // the two's-complement byte
            case 'G': out = { Cmd::G, int16_t(std::min(v, 16)), 0, 0 }; return true;
            case 'O': out = { Cmd::O, int16_t(v & 3), 0, 0 }; return true;
            case 'T': out = { Cmd::T, int16_t(v), 0, 0 }; return true;                     // the byte, as the driver reads it
            case 'W':
                if (instKind == 1) { notes.add("W" + hex2(v) + " at " + where + " on a wave instrument (synth speed / length): not mapped"); return false; }
                if (x) notes.add("W" + hex2(v) + " at " + where + ": the high digit has no register effect on the ROM; only the duty (low digit) is kept");
                out = { Cmd::W, int16_t(y & 3), 0, 0 }; return true;
            case 'F':
                if (instKind == 1) { out = { Cmd::F, int16_t(std::min(16, y + 1)), 0, 0 }; return true; }
                if (channel == 1) { out = { Cmd::F, int16_t(v), 0, 0 }; return true; }    // PU2: the transpose for the note (section 49)
                notes.add("F" + hex2(v) + " at " + where + ": finetune has no ChipBoy equivalent on this channel; dropped"); return false;
            case 'B': notes.add("B" + hex2(v) + " (MayBe) at " + where + ": no ChipBoy equivalent; dropped"); return false;
            default: notes.add(std::string(1, letter ? letter : '?') + hex2(v) + " at " + where + ": not mapped"); return false;
        }
    }

    // --- tables (section 45's noise rows) -----------------------------------
    /// The noise notes each table is used with, for its transposes.
    std::map<int, std::set<std::pair<int, int>>> tableUsers()   // table -> (inst, midi)
    {
        std::map<int, std::set<std::pair<int, int>>> users;
        for (int p = 0; p < kLsdjPhrases; ++p) {
            if (!phraseAllocated(p)) continue;
            int last = -1;
            for (int st = 0; st < 16; ++st) {
                const size_t i = size_t(p) * 16 + size_t(st);
                const uint8_t ins = at(kPhraseInst + i);
                if (ins != 0xFF && ins < kLsdjInstruments) last = ins;
                const uint8_t n = at(kPhraseNotes + i);
                if (!n || last < 0) continue;
                int tbl = -1;
                const char letter = letterOf(at(kPhraseCmd + i));
                const uint8_t v = at(kPhraseCmdV + i);
                if (letter == 'A' && v != 0x20) tbl = v;
                else if (inst(last)[6] & 0x20) tbl = inst(last)[6] & 0x1F;
                if (tbl >= 0 && tbl < kLsdjTables) users[tbl].insert({ last, int(n) + 35 });
            }
        }
        return users;
    }
    void tables(ImportSummary& sum)
    {
        const auto users = tableUsers();
        for (int t = 0; t < kLsdjTables; ++t) {
            if (!at(kTableAlloc + size_t(t))) continue;
            int noiseBase = -1; bool others = false; std::set<int> bases;
            if (auto it = users.find(t); it != users.end())
                for (const auto& [ins, midi] : it->second) { if (instType[size_t(ins)] == 3) bases.insert(midi); else others = true; }
            if (!bases.empty()) {
                noiseBase = *bases.begin();
                if (bases.size() > 1) notes.add("table " + hex2(t) + " is used by several noise notes: its transposes are mapped for the lowest; the others land a little off");
                if (others) notes.add("table " + hex2(t) + " is used by noise and non-noise instruments: its transposes are mapped for the noise");
            }
            auto& tb = bank.tables[size_t(t)];
            tb = bank::Table{};
            tb.used = true; tb.name = "Table " + hex2(t); tb.end = bank::TableEnd::Loop; tb.hopStep = 1;
            bool fade = false;
            for (int r = 0; r < 16; ++r) {
                const size_t i = size_t(t) * 16 + size_t(r);
                const uint8_t env = at(kTableEnv + i), tsp = at(kTableTsp + i);
                auto& st = tb.steps[size_t(r)];
                st.vol = env ? int8_t(env >> 4) : int8_t(-1);
                if (env & 15) fade = true;
                if (tsp) {
                    st.hasTranspose = true;
                    if (noiseBase >= 0) {
                        const int base = noteForNr43(lsdjNr43(noiseBase), noiseBase);
                        const int moved = noteForNr43(lsdjNr43(noiseBase + signedByte(tsp)), noiseBase + signedByte(tsp));
                        st.transpose = int8_t(std::clamp(moved - base, -128, 127));
                    } else st.transpose = int8_t(signedByte(tsp));
                }
                const std::pair<uint8_t, uint8_t> cmds[2] = { { at(kTableCmd1 + i), at(kTableCmd1V + i) }, { at(kTableCmd2 + i), at(kTableCmd2V + i) } };
                for (int k = 0; k < 2; ++k) {
                    const char letter = letterOf(cmds[k].first);
                    if (!letter) continue;
                    Command c;
                    if (command(letter, cmds[k].second, "table " + hex2(t) + " row " + std::to_string(r), -1, -1, nullptr, c)) (k == 0 ? st.cmd1 : st.cmd2) = c;
                }
            }
            if (fade) notes.add("table " + hex2(t) + ": the ENV column's low digit (fade speed per row) is not mapped; the amplitude is written at the row");
            ++sum.tables;
        }
    }

    // --- phrases and chains (section 48) -----------------------------------
    int phraseFor(int p, int channel)
    {
        const auto key = std::make_pair(p, channel);
        if (auto it = phraseSlot.find(key); it != phraseSlot.end()) return it->second;
        if (phrasesOut >= tracker::kPhraseSlots) { notes.add("more phrase copies than ChipBoy's 255 slots; the rest are left empty"); return 0; }
        const int slot = ++phrasesOut;
        phraseSlot[key] = slot;
        auto& ph = song.phrases[size_t(slot - 1)];
        ph = tracker::Phrase{};
        ph.used = true; ph.steps = 16; ph.groove = 0;
        int last = -1;
        for (int st = 0; st < 16; ++st) {
            const size_t i = size_t(p) * 16 + size_t(st);
            const uint8_t n = at(kPhraseNotes + i), ins = at(kPhraseInst + i);
            auto& c = ph.cells[size_t(st)];
            if (ins != 0xFF && ins < kLsdjInstruments) { last = ins; c.inst = uint8_t(ins + 1); }
            const int cur = last;
            const int kind = cur >= 0 ? instType[size_t(cur)] : -1;
            if (n) {
                int midi = int(n) + 35;
                if (kind == 1) midi += m.waveOctave;                       // the wave channel's period table (section 45)
                else if (kind == 3) {
                    const uint8_t nr43 = lsdjNr43(midi);
                    noiseWidths[cur].insert((nr43 & 8) != 0);
                    midi = noteForNr43(nr43, midi);
                }
                c.note = uint8_t(std::clamp(midi, 1, 127));
            }
            const char letter = letterOf(at(kPhraseCmd + i));
            if (letter) {
                Command cmd;
                if (command(letter, at(kPhraseCmdV + i), "phrase " + hex2(p) + " step " + std::to_string(st), kind, channel, &c, cmd)) c.cmd1 = cmd;
            }
        }
        return slot;
    }
    void chains(ImportSummary& sum)
    {
        static const char* kNames[4] = { "PU1", "PU2", "WAV", "NOI" };
        bool noiseTsp = false;
        for (int r = 0; r < 256; ++r) {
            const uint8_t* row = s + kSongRows + size_t(r) * 4;
            if (row[0] == 0xFF && row[1] == 0xFF && row[2] == 0xFF && row[3] == 0xFF) break;
            ++sum.rows;
            for (int ch = 0; ch < 4; ++ch) {
                const uint8_t c = row[ch];
                if (c == 0xFF || c >= kLsdjChains) { notes.add(std::string("song row ") + hex2(r) + " has an empty " + kNames[ch] + " step: LSDj stops that channel there"); continue; }
                for (int st = 0; st < 16; ++st) {
                    const uint8_t p = at(kChainPhrases + size_t(c) * 16 + size_t(st));
                    if (p == 0xFF || p >= kLsdjPhrases) break;
                    const int tsp = signedByte(at(kChainTsp + size_t(c) * 16 + size_t(st)));
                    const int slot = phraseFor(p, ch);
                    auto& chain = song.chain[size_t(ch)];
                    chain.push_back(uint8_t(slot));
                    if (tsp) { song.setTranspose(ch, int(chain.size()) - 1, int8_t(tsp)); if (ch == 3) noiseTsp = true; }
                }
            }
        }
        if (noiseTsp) notes.add("chain transposes on the noise channel go through ChipBoy's map: near LSDj's pitch, not on it");
        sum.phrases = phrasesOut;
    }
    void noiseWidthsToInstruments()
    {
        for (const auto& [i, widths] : noiseWidths) {
            if (widths.size() > 1) notes.add("noise instrument " + bank.instruments[size_t(i)].name + " plays both 15-bit and 7-bit notes in LSDj; ChipBoy's width is per instrument (15-bit chosen)");
            bank.instruments[size_t(i)].lfsr7 = widths.size() == 1 && *widths.begin();
        }
    }
    void grooves()
    {
        for (int g = 0; g < 16; ++g) {
            auto& gr = song.grooves[size_t(g)];
            gr = tracker::Groove{};
            int n = 16;
            while (n > 0 && at(kGrooves + size_t(g) * 16 + size_t(n - 1)) == 0) --n;
            if (n == 0) { gr.ticks[0] = 6; gr.ticks[1] = 6; continue; }
            for (int k = 0; k < 16; ++k) gr.ticks[size_t(k)] = k < n ? uint8_t(std::min<int>(48, at(kGrooves + size_t(g) * 16 + size_t(k)))) : uint8_t(0);
        }
    }
};

} // namespace

int chipboyNoteForNr43(uint8_t v, int prefer)
{
    const double c = std::log(noiseClockHz(v >> 4, v & 7));
    int best = 1; double bestErr = 1e9; int bestDist = 1000;
    for (int k = 1; k < 128; ++k) {
        uint8_t sh = 0, d = 0;
        driver::Driver::noisePairForNote(k, sh, d);
        const double err = std::round(std::fabs(std::log(noiseClockHz(sh, d)) - c) * 1e6) / 1e6;
        const int dist = std::abs(k - prefer);
        if (err < bestErr || (err == bestErr && dist < bestDist)) { best = k; bestErr = err; bestDist = dist; }
    }
    return best;
}

bool importSong(const uint8_t* bytes, size_t size, const LsdjModel& model,
                bank::Bank& bank, tracker::Song& out, ImportSummary& summary, ImportNotes& notes)
{
    if (bytes == nullptr || size < 0x8000) return false;
    summary = ImportSummary{};
    { auto blank = std::make_unique<bank::Bank>(); bank = std::move(*blank); }
    { auto blank = std::make_unique<tracker::Song>(); out = std::move(*blank); }
    Reader r(bytes, model, bank, out, notes);
    const int tempo = std::clamp<int>(bytes[kTempo], 40, 255);
    summary.tempoBpm = tempo;
    out.tempoBpm = tempo;
    r.tickMs = 60000.0 / (double(tempo) * 24.0);
    r.instruments(summary);
    r.tables(summary);
    r.chains(summary);
    r.noiseWidthsToInstruments();
    r.grooves();
    summary.waves = int(r.waveSlotOfSynth.size());
    for (auto& src : out.noteSource) src = tracker::NoteSource::Tracker;
    for (auto& arm : out.recordArm) arm = false;
    tracker::buildRowTables(out);
    return true;
}

} // namespace chipboy::lsdj
