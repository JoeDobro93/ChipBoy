// ChipBoy -- an LSDj song into a bank and a song (docs/plan-lsdj-import.md
// section 4; the rules are docs/COMMANDS_AND_TEMPO.md sections 45-52).
#include "core/Import/LsdjSong.h"

#include "core/Driver/Driver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <vector>

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
constexpr size_t kPhraseAlloc = 0x3E82, kTempo = 0x3FB4, kSongTranspose = 0x3FB5, kPhraseCmd = 0x4000, kPhraseCmdV = 0x4FF0, kWaves = 0x6000, kPhraseInst = 0x7000;
constexpr int kLsdjTables = 32, kLsdjInstruments = 64, kLsdjPhrases = 255, kLsdjChains = 128;
constexpr double kPitchClockMs = 11712.0 * 1000.0 / 4194304.0;   // 2.7924 ms

int signedByte(int b) { return b >= 128 ? b - 256 : b; }
std::string hex2(int v) { static const char* d = "0123456789ABCDEF"; std::string s; s += d[(v >> 4) & 15]; s += d[v & 15]; return s; }
double noiseClockHz(int shift, int divisor) { return 524288.0 / (divisor == 0 ? 0.5 : double(divisor)) / double(1u << (shift + 1)); }

/// S on noise before 9 (section 56): each nibble of NR43 less the matching
/// nibble of xy, modulo 16, no borrow between them.
uint8_t nibbleS(uint8_t nr43, int xy) { return uint8_t(((((nr43 >> 4) - (xy >> 4)) & 15) << 4) | (((nr43 & 15) - (xy & 15)) & 15)); }
/// The pulse period register for a MIDI note, the chip's own formula (the
/// LSDj table is within a unit of it): what a register-unit slide covers.
double gbPeriod(int midi) { return 2048.0 - 131072.0 / (440.0 * std::pow(2.0, (midi - 69) / 12.0)); }
/// Drum mode's units a pitch clock for a P speed (Driver.cpp: bendStep256 /
/// 256 of a semitone at kDrumUnitsPerSemitone).
double drumUnitsPerClock(int speed) { return double(driver::Driver::bendStepFor(speed)) / 256.0 * 19.11; }
/// The ChipBoy P speed whose Drum step is nearest `units` a pitch clock
/// (section 56: an old P adds its byte to the register every clock).
int drumSpeedFor(int units)
{
    int best = 1; double bestErr = 1e9;
    for (int m2 = 1; m2 <= 127; ++m2) { const double e = std::fabs(drumUnitsPerClock(m2) - double(units)); if (e < bestErr) { bestErr = e; best = m2; } }
    return best;
}

/// The interpreter's state for one song.
/// A kit instrument's two kits (plan section 4a, measured on 9.2.L): the
/// note's high digit picks a sample of the kit in byte 2, its low digit one
/// of the kit in byte 9; bytes 3 and 11 cut them to that many 32-sample
/// frames; byte 8 moves the period. One ChipBoy kit per instrument gathers the
/// samples its notes use, each on its own MIDI note from 36 up.
struct KitUse {
    int kitSlot = 0;                 ///< the ChipBoy kit slot, 1-32
    int kitA = -1, kitB = -1;        ///< LSDj kit numbers
    int lenA = 0, lenB = 0;          ///< frames of 32 samples, 0 whole
    std::map<int, uint8_t> noteOf;   ///< LSDj note byte -> the MIDI note its sample sits on
};

struct Reader {
    const uint8_t* s;
    const LsdjModel& m;
    bank::Bank& bank;
    tracker::Song& song;
    ImportNotes& notes;
    const std::vector<LsdjKit>* kits = nullptr;
    std::map<int, KitUse> kitUse;                        // LSDj instrument -> its kit
    int kitSlots = 0;
    double tickMs = 0.0;
    std::array<int, kLsdjInstruments> instType{};        // -1 none, 0 pulse, 1 wave, 2 kit, 3 noise
    std::array<bool, kLsdjInstruments> instTranspose{};
    std::map<int, int> waveSlotOfSynth;                  // LSDj synth -> ChipBoy wave slot
    std::map<int, std::set<bool>> noiseWidths;           // ChipBoy slot -> the LFSR widths its notes take
    // LSDj's noise clocks run from 16 Hz to 524 kHz; ChipBoy's notes 12-127
    // reach 2 kHz and up (section 9.4). The instrument's Shift parameter moves
    // the whole map by octaves, so each noise slot takes the offset that puts
    // the clocks its notes ask for onto the keyboard (section 56).
    std::map<int, std::set<uint8_t>> noiseClocks;         // ChipBoy slot -> the NR43 bytes its notes write
    std::map<int, int> noiseOffset;                       // ChipBoy slot -> shift offset (noiseShift - 5)
    /// What a channel carries from cell to cell in chain order (section 56):
    /// the noise channel's NR43 and the ChipBoy note it landed on, for S;
    /// the last note's LSDj MIDI on a pulse or wave channel, for L.
    /// `inst` is the instrument the channel carries (LSDj's is 00 at the
    /// start); `instSeen` whether a cell has named one yet.
    struct ChannelState { int nr43 = -1; int chipNote = 0; int lastMidi = -1; int inst = 0; bool instSeen = false; int noiseSlot = 0; };
    struct PhraseOut { int slot = 0; ChannelState end; };
    std::map<std::tuple<int, int, int>, PhraseOut> phraseSlot;   // (LSDj phrase, channel, folded noise transpose) -> ChipBoy phrase slot and the state it leaves
    // LSDj plays any instrument on any channel as the channel's kind -- a
    // pulse on NOI is a noise instrument with the same bytes (section 56).
    // ChipBoy's instruments have a type, so such a use gets a variant of the
    // channel's kind in the slots above LSDj's 64.
    std::map<int, std::set<int>> phraseChannels;         // LSDj phrase -> the channels it plays on
    std::map<std::pair<int, int>, int> variantSlot;      // (LSDj instrument, kind) -> ChipBoy slot, 1-based
    std::map<int, std::set<std::tuple<int, int, int>>> tableUse;   // table -> (LSDj instrument, channel, MIDI) of the notes that run it
    std::set<int> instUsed;                              // LSDj instruments a note plays, allocated or not
    int nextVariant = kLsdjInstruments;                  // the next free 0-based slot for a variant
    int phrasesOut = 0;
    /// ChipBoy's LFSR clock for a note, -72..127: the map continues below the
    /// keyboard for transposes (section 55).
    static double chipboyClockOf(int note) { uint8_t sh = 0, d = 0; driver::Driver::noisePairForNote(note, sh, d); return noiseClockHz(sh, d); }

    Reader(const uint8_t* bytes, const LsdjModel& model, bank::Bank& b, tracker::Song& so, ImportNotes& n)
        : s(bytes), m(model), bank(b), song(so), notes(n)
    {
        instType.fill(-1);
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
    /// The kind a channel plays: pulse on PU1 and PU2, a kit or a wave on WAV, noise on NOI.
    int kindFor(int ins, int ch) const { if (ch == 3) return 3; if (ch == 2) return (ins >= 0 && ins < kLsdjInstruments && inst(ins)[0] == 2) ? 2 : 1; return 0; }
    /// The ChipBoy slot (1-based) a cell on `ch` reaches for LSDj instrument `ins`.
    int slotFor(int ins, int ch) const { const auto it = variantSlot.find({ ins, kindFor(ins, ch) }); return it != variantSlot.end() ? it->second : ins + 1; }
    bool transposeOn(int ins) const { return ins >= 0 && ins < kLsdjInstruments && !(inst(ins)[5] & 0x20); }
    static const char* channelName(int ch) { static const char* k[4] = { "PU1", "PU2", "WAV", "NOI" }; return k[ch & 3]; }

    /// Which phrases play on which channels and which instruments notes reach
    /// there, in chain order, before anything is built: the variants come from
    /// this, and so do the instruments a note plays without a column (LSDj's
    /// instrument 00 until a cell names one).
    void usage()
    {
        std::array<int, 4> cur{ 0, 0, 0, 0 };
        int runNr = -1;                                       // the noise channel's NR43, for the S rows' clocks
        const bool fold = m.noiseRule != NoiseRule::Map;
        const auto use = [this](int ins, int ch) {
            if (ins < 0 || ins >= kLsdjInstruments) return;
            instUsed.insert(ins);
            const int kind = kindFor(ins, ch), own = inst(ins)[0];
            if (kind == own || kind == 2) return;
            const auto key = std::make_pair(ins, kind);
            if (variantSlot.count(key)) return;
            if (nextVariant >= bank::kInstrumentSlots) { notes.add("no slot left for a " + std::string(channelName(ch)) + " variant of instrument " + hex2(ins) + "; it plays as itself"); return; }
            variantSlot[key] = ++nextVariant;                                  // 1-based: 0-based slot nextVariant - 1
        };
        for (int r = 0; r < 256; ++r) {
            const uint8_t* row = s + kSongRows + size_t(r) * 4;
            if (row[0] == 0xFF && row[1] == 0xFF && row[2] == 0xFF && row[3] == 0xFF) break;
            for (int ch = 0; ch < 4; ++ch) {
                const uint8_t c = row[ch];
                if (c == 0xFF || c >= kLsdjChains) continue;
                for (int st = 0; st < 16; ++st) {
                    const uint8_t p = at(kChainPhrases + size_t(c) * 16 + size_t(st));
                    if (p == 0xFF || p >= kLsdjPhrases) break;
                    phraseChannels[p].insert(ch);
                    const int tsp = signedByte(at(kChainTsp + size_t(c) * 16 + size_t(st)));
                    for (int k = 0; k < 16; ++k) {
                        const size_t i = size_t(p) * 16 + size_t(k);
                        const uint8_t ins = at(kPhraseInst + i);
                        if (ins != 0xFF && ins < kLsdjInstruments) cur[size_t(ch)] = ins;
                        const uint8_t n = at(kPhraseNotes + i);
                        if (!n) continue;
                        const int ci = cur[size_t(ch)];
                        const char letter = letterOf(at(kPhraseCmd + i));
                        const uint8_t v = at(kPhraseCmdV + i);
                        if (ch == 3 && !n && letter == 'S' && runNr >= 0 && m.noiseS == NoiseS::Nibbles) { runNr = nibbleS(uint8_t(runNr), v); noiseClocks[slotFor(ci, 3)].insert(uint8_t(runNr)); }
                        if (!n) continue;
                        use(ci, ch);
                        // The table this note runs: the cell's A, else the instrument's.
                        int tbl = -1;
                        if (letter == 'A' && v != 0x20) tbl = v;
                        else if (ci >= 0 && ci < kLsdjInstruments && (inst(ci)[6] & 0x20)) tbl = inst(ci)[6] & 0x1F;
                        if (tbl >= 0 && tbl < kLsdjTables) tableUse[tbl].insert({ ci, ch, int(n) + 35 });
                        if (ch == 3) {
                            // The clocks this slot asks for: the note, the S rows after it, the table's rows.
                            const int slot = slotFor(ci, 3);
                            runNr = lsdjNr43(int(n) + 35 + (fold && transposeOn(ci) ? tsp : 0), ci);
                            noiseClocks[slot].insert(uint8_t(runNr));
                            if (letter == 'S' && m.noiseS == NoiseS::Nibbles) { runNr = nibbleS(uint8_t(runNr), v); noiseClocks[slot].insert(uint8_t(runNr)); }
                            if (tbl >= 0 && tbl < kLsdjTables) {
                                uint8_t tr = uint8_t(runNr);
                                for (int r = 0; r < 16; ++r) {
                                    const size_t ti = size_t(tbl) * 16 + size_t(r);
                                    const uint8_t tt = at(kTableTsp + ti);
                                    if (tt && fold) noiseClocks[slot].insert(uint8_t((int(runNr) - signedByte(tt)) & 0xFF));
                                    for (const auto& [code, val] : { std::pair<uint8_t, uint8_t>{ at(kTableCmd1 + ti), at(kTableCmd1V + ti) }, std::pair<uint8_t, uint8_t>{ at(kTableCmd2 + ti), at(kTableCmd2V + ti) } })
                                        if (letterOf(code) == 'S' && m.noiseS == NoiseS::Nibbles) { tr = nibbleS(tr, val); noiseClocks[slot].insert(tr); }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // --- noise (section 45) ---------------------------------------------
    /// NR43 for a noise note under the model's rule (section 56). `instSlot`
    /// is the note's instrument, whose byte 4 is the SHAPE before 8.8.
    uint8_t lsdjNr43(int midi, int instSlot)
    {
        if (m.noiseRule == NoiseRule::Shape) {
            const int shape = instSlot >= 0 && instSlot < kLsdjInstruments ? int(inst(instSlot)[4]) : 0xFF;
            const int octave = (std::clamp(midi, 36, 36 + 12 * 12) - 36) / 12 + 2;   // C-2 to B-2 is octave 2
            return uint8_t(std::clamp(((~shape) & 0xFF) + 16 * (5 - octave), 0, 255));
        }
        if (m.noiseRule == NoiseRule::Raw) return uint8_t(std::clamp(0xFF - (midi - 35), 0, 255));
        const int lo = std::clamp(m.noiseLo, 0, 127), hi = std::clamp(m.noiseHi, lo, 127);
        int k = std::clamp(midi, 0, 127);
        if (k < lo || k > hi) {
            // Outside the measured range: the octave neighbour inside it.
            notes.add("noise note " + std::to_string(midi) + " is outside the measured map; mapped as its octave's neighbour");
            while (k < lo) k += 12;
            while (k > hi) k -= 12;
            k = std::clamp(k, lo, hi);
        }
        return m.noiseMap[size_t(k)];
    }
    int offsetOf(int slot) const { const auto it = noiseOffset.find(slot); return it == noiseOffset.end() ? 0 : it->second; }
    /// The ChipBoy note that plays NR43 `v` on `slot`, its Shift offset taken
    /// into account: the map's clock is 2^offset times the byte's.
    int noteForNr43(uint8_t v, int prefer, int slot)
    {
        const double c = noiseClockHz(v >> 4, v & 7) * std::pow(2.0, offsetOf(slot));
        const int n = chipboyNoteForClock(c, prefer);
        if (std::fabs(std::log(chipboyClockOf(n)) - std::log(c)) > 1e-6)
            notes.add("noise NR43 " + hex2(v) + ": no ChipBoy note has exactly its clock; the nearest note is used");
        return n;
    }
    /// Each noise slot's Shift offset: the first of 0, +1 .. +8, -1 .. -5
    /// that puts every note it plays on the keyboard (12-127), else the one
    /// that puts most there.
    void chooseNoiseOffsets()
    {
        static const int kTry[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, -1, -2, -3, -4, -5 };
        for (const auto& [slot, bytes] : noiseClocks) {
            int best = 0; double bestErr = 1e9;
            for (int o : kTry) {
                double err = 0.0;
                for (uint8_t v : bytes) {
                    const double c = noiseClockHz(v >> 4, v & 7);
                    const int n = std::clamp(chipboyNoteForClock(c * std::pow(2.0, o), 60), 12, 127);
                    uint8_t sh = 0, d = 0; driver::Driver::noisePairForNote(n, sh, d);
                    const double got = noiseClockHz(std::clamp(int(sh) + o, 0, 13), d);   // the driver's shift clamp
                    err += std::min(3.0, std::fabs(std::log2(got / c)));                    // octaves off, a click is a click past three
                }
                if (err < bestErr - 1e-9) { bestErr = err; best = o; }
            }
            noiseOffset[slot] = best;
            if (best != 0) notes.add("noise slot " + hex2(slot - 1) + " plays clocks off ChipBoy's keyboard: its Shift is set to " + std::to_string(5 + best) + " so its notes land on it");
        }
    }

    // --- envelope (sections 51 and 58) ------------------------------------
    /// The ticks a stage costs to walk `delta` levels at `speed`: the measured
    /// period table on the software stages, the chip's own (period / 64) of a
    /// second a level on the hardware ones (section 58).
    int envTicks(int delta, int speed) const
    {
        if (speed == 0 || delta == 0) return 0;
        const double perLevelMs = m.envPeriods != nullptr ? double(m.envPeriods[size_t(speed & 15)]) * kPitchClockMs
                                                          : double(speed & 7) * 1000.0 / 64.0;
        const double ms = std::abs(delta) * perLevelMs;
        return std::clamp(int(std::lround(ms / tickMs)), 1, 255);
    }
    void envelope(const uint8_t* b, bank::Instrument& o, const std::string& name)
    {
        if (m.envelopeLaw == EnvelopeLaw::Chip) {
            // The byte is NRx2: the chip's own envelope, exactly.
            o.env.mode = bank::EnvMode::Chip;
            o.envVol = uint8_t(b[1] >> 4); o.envDir = (b[1] & 8) ? bank::EnvDir::Up : bank::EnvDir::Down; o.envRate = uint8_t(b[1] & 7);
            return;
        }
        const bool hw = m.envelopeLaw == EnvelopeLaw::HardwareStages;
        const int mask = hw ? 7 : 15;                                     // NRx2 keeps the period in three bits
        const int a1 = b[1] >> 4, s1 = b[1] & mask, a2 = b[9] >> 4, s2 = b[9] & mask, a3 = b[10] >> 4, s3 = b[10] & mask;
        // On the chip a stage hands over only when its ramp can reach the next
        // amplitude; a direction that points away from it never arrives, and
        // the note holds where it is (section 58).
        const auto reaches = [hw](uint8_t from, int to, int speed) {
            if (speed == 0) return false;
            if (!hw) return true;
            return (from & 8) ? to > (from >> 4) : to < (from >> 4);
        };
        o.envVol = uint8_t(a1);
        if (!reaches(b[1], a2, s1)) {   // a held level: the chip's own envelope says it best
            o.env.mode = bank::EnvMode::Chip;
            o.envDir = (b[1] & 8) ? bank::EnvDir::Up : bank::EnvDir::Down;
            o.envRate = uint8_t(hw ? s1 : 0);
            return;
        }
        o.env.mode = bank::EnvMode::Shaped;
        o.env.start = uint8_t(a1); o.env.attackTicks = uint8_t(envTicks(a1 - a2, s1)); o.env.peak = uint8_t(a2); o.env.releaseTicks = 0;
        if (!reaches(b[9], a3, s2)) { o.env.decayTicks = 0; o.env.sustain = uint8_t(a2); o.env.fadeTicks = 0; }
        else {
            o.env.decayTicks = uint8_t(envTicks(a2 - a3, s2)); o.env.sustain = uint8_t(a3);
            if (s3 && !hw) { o.env.fadeTicks = uint8_t(envTicks(a3, s3)); o.env.fadeTo = 0; }
        }
        for (int sp : { s1, s2, s3 }) {
            const double perLevelMs = m.envPeriods != nullptr ? double(m.envPeriods[size_t(sp & 15)]) * kPitchClockMs : double(sp & 7) * 1000.0 / 64.0;
            if (sp && perLevelMs < tickMs) { notes.add("instrument " + name + ": an envelope stage faster than a tick a level is quantised to the tick"); break; }
        }
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
            if (!at(kInstAlloc + size_t(i)) && !instUsed.count(i)) continue;
            bank::Instrument o;
            if (!buildInstrument(i, inst(i)[0], o)) continue;
            instType[size_t(i)] = inst(i)[0];
            bank.instruments[size_t(i)] = o;
            ++sum.instruments;
        }
        for (const auto& [key, slot] : variantSlot) {
            bank::Instrument o;
            if (!buildInstrument(key.first, key.second, o)) continue;
            static const char* kKind[4] = { " (PU)", " (WAV)", " (kit)", " (NOI)" };
            o.name += kKind[key.second & 3];
            bank.instruments[size_t(slot - 1)] = o;
            notes.add("instrument " + hex2(key.first) + " " + bank.instruments[size_t(key.first)].name + " also plays on " + (key.second == 3 ? "NOI" : key.second == 0 ? "PU1/PU2" : "WAV") + ": a " + std::string(kKind[key.second & 3] + 2, std::strlen(kKind[key.second & 3]) - 3) + " variant sits in slot " + hex2(slot - 1));
            ++sum.instruments;
        }
        for (const auto& [slot, o] : noiseOffset) if (slot >= 1 && slot <= bank::kInstrumentSlots) bank.instruments[size_t(slot - 1)].noiseShift = uint8_t(std::clamp(5 + o, 0, 13));
        if (m.pitchLaw == PitchLaw::Register) notes.add("this format's P and L work in the period register (section 56): pulse and wave instruments are set to the Drum pitch mode and P, L and V are converted");
    }
    /// LSDj instrument `i` read as kind `t` (its own, or the channel's for a variant).
    bool buildInstrument(int i, int t, bank::Instrument& o)
    {
        {
            const uint8_t* b = inst(i);
            std::string name = instName(i);
            if (name.empty()) name = "Inst " + hex2(i);
            if (t == 2 && (kits == nullptr || kits->empty())) { notes.add("instrument " + hex2(i) + " " + name + " is a kit: its samples live in the ROM, and none was found beside the save; skipped"); return false; }
            if (t > 3) { notes.add("instrument " + hex2(i) + " has an unknown type " + std::to_string(t) + "; skipped"); return false; }
            const auto type = t == 0 ? bank::InstrumentType::Pulse : t == 1 ? bank::InstrumentType::Wave : t == 2 ? bank::InstrumentType::Kit : bank::InstrumentType::Noise;
            o = bank::Instrument::defaults(type, name.c_str());
            o.name = name;
            o.pan = bank::Pan(b[7] & 3); o.length = 0; o.noteOff = bank::NoteOff::Kill;
            o.cmdRate = uint8_t(b[8] & 15); o.chordRate = o.cmdRate;
            o.tableMode = (b[5] & 0x08) ? bank::TableMode::Step : bank::TableMode::Tick;
            o.vib.shape = bank::VibShape(std::min(2, (b[5] >> 1) & 3)); o.vib.dir = (b[5] & 1) ? bank::VibDir::Up : bank::VibDir::Down;
            o.vib.speed = 8; o.vib.depth = 0; o.vib.delay = 0;
            o.table = (b[6] & 0x20) ? uint8_t((b[6] & 0x1F) + 1) : uint8_t(0);
            o.transpose = !(b[5] & 0x20);
            // Before 8.8 an E writes NRx2 and triggers (section 59).
            o.envRetrig = m.envelopeLaw != EnvelopeLaw::SoftwareStages;
            instTranspose[size_t(i)] = o.transpose;
            if (t == 0 || t == 3) envelope(b, o, name);
            if (t == 0) {
                o.duty = uint8_t(b[7] >> 6); o.dutySeqLen = 0; o.pitchSpeed = m.pitchLaw == PitchLaw::Register ? bank::PitchSpeed::Drum : pitchSpeedOf(b[5]);
                const int nr10 = (~b[4]) & 0xFF;
                o.sweepRate = uint8_t((nr10 >> 4) & 7); o.sweepDown = (nr10 & 8) != 0; o.sweepShift = uint8_t(nr10 & 7);
                if (m.pu2Transpose && b[2]) { o.pu2Transpose = int8_t(signedByte(b[2])); notes.add("pulse instrument " + name + " carries PU2 TSP " + hex2(b[2]) + " (" + std::to_string(signedByte(b[2])) + " semitones), kept as its PU2 transpose"); }
                if (b[11]) notes.add("pulse instrument " + name + " has finetune " + hex2(b[11]) + ": ChipBoy has no finetune");
            } else if (t == 1) {
                static const uint8_t kLevel[4] = { 0, 3, 2, 1 };       // the stored bits are the NR32 code, 1 = 100 %
                o.waveLevel = kLevel[(b[1] >> 5) & 3];
                // The synth byte is 2 before 9.x and 3 after (section 60); its low
                // nibble is LSDj's LOOP POS, not a start frame (section 65).
                const uint8_t wb = b[size_t(m.waveByte == 3 ? 3 : 2)];
                const int synth = wb >> 4, loopPos = wb & 15;
                o.wave = uint8_t(waveSlotFor(synth));
                o.pitchSpeed = m.pitchLaw == PitchLaw::Register ? bank::PitchSpeed::Drum : pitchSpeedOf(b[5]);
                // The run: LENGTH is 16 - the low nibble of byte 10, SPEED is byte
                // 11 and costs four ticks on top, PLAY is byte 9's low two bits,
                // and the loop covers the last 16 - LOOP POS steps of the run.
                const int len = 16 - int(b[10] & 15);
                o.frameLength = uint8_t(len);
                o.frameLoopStep = uint8_t(std::max(0, len - (16 - loopPos)));
                switch (b[9] & 3) {
                    case 0: o.frameAdvance = 0; o.frameLoop = bank::FrameLoop::Loop; break;      // MANUAL: only an F moves it
                    case 1: o.frameLoop = bank::FrameLoop::Once; break;
                    case 3: o.frameLoop = bank::FrameLoop::PingPong; break;
                    default: o.frameLoop = bank::FrameLoop::Loop; break;
                }
                if (b[9] & 3) o.frameAdvance = uint8_t(std::min(255, int(b[11]) + 4));
            } else if (t == 2) {
                if (!kitInstrument(i, b, o, name)) return false;
            } else {
                o.lfsr7 = false; o.noiseManual = false; o.noiseShift = 5; o.noiseDivisor = 1; o.noiseSweep = 0;
                // Section 66: before 9 the noise commands work on the NR43 byte.
                o.noiseDomain = m.noiseS == NoiseS::Semitones ? bank::NoiseSweepDomain::Notes : bank::NoiseSweepDomain::Register;
            }
            o.used = true;
            return true;
        }
    }

    // --- kits (plan section 4a) -------------------------------------------
    bool kitInstrument(int i, const uint8_t* b, bank::Instrument& o, const std::string& name)
    {
        if (kitSlots >= bank::kKitSlots) { notes.add("kit instrument " + name + ": ChipBoy's 32 kit slots are full; skipped"); return false; }
        KitUse use;
        use.kitA = b[2] & 0x3F; use.kitB = b[9] & 0x3F; use.lenA = b[3]; use.lenB = b[11];
        const int count = int(kits->size());
        if (use.kitA >= count && use.kitB >= count) { notes.add("kit instrument " + name + " names kits " + hex2(use.kitA) + " and " + hex2(use.kitB) + ", which this ROM does not have; skipped"); return false; }
        use.kitSlot = ++kitSlots;
        auto& k = bank.kits[size_t(use.kitSlot - 1)];
        k = bank::Kit{};
        k.used = true;
        k.name = (use.kitA < count ? (*kits)[size_t(use.kitA)].name : std::string("?")) + (use.kitB < count && use.kitB != use.kitA ? "+" + (*kits)[size_t(use.kitB)].name : std::string());
        k.period = kitPeriodOfSpeed(b[8]);
        k.loop = bank::KitLoop::Once;
        o.kit = uint8_t(use.kitSlot); o.kitLoop = bank::KitLoop::Once; o.waveLevel = 3;
        o.env.mode = bank::EnvMode::Chip;
        kitUse[i] = use;
        if (b[12] || b[13]) notes.add("kit instrument " + name + ": the sample offsets (" + hex2(b[12]) + ", " + hex2(b[13]) + ") are not mapped; samples play from their start");
        if (b[5] & 0x40) notes.add("kit instrument " + name + ": a loop or half-speed flag in byte 5 is not mapped");
        return true;
    }
    /// The MIDI note a kit note byte plays on: the sample (or the two,
    /// summed and clipped) is added to the instrument's ChipBoy kit the first
    /// time the byte is seen.
    uint8_t kitNote(int inst, int noteByte, const std::string& where)
    {
        auto it = kitUse.find(inst);
        if (it == kitUse.end()) return 0;
        KitUse& use = it->second;
        if (auto found = use.noteOf.find(noteByte); found != use.noteOf.end()) return found->second;
        const int hi = noteByte >> 4, lo = noteByte & 15;
        auto sampleOf = [&](int kit, int digit, int len) -> const LsdjKitSample* {
            if (digit == 0 || kit < 0 || kit >= int(kits->size())) return nullptr;
            const auto& ks = (*kits)[size_t(kit)].samples;
            if (digit - 1 >= int(ks.size())) { notes.add("kit note " + hex2(noteByte) + " at " + where + " names sample " + std::to_string(digit) + " of kit " + hex2(kit) + ", which has " + std::to_string(ks.size()) + "; silent"); return nullptr; }
            (void)len;
            return &ks[size_t(digit - 1)];
        };
        const LsdjKitSample* a = sampleOf(use.kitA, hi, use.lenA);
        const LsdjKitSample* b = sampleOf(use.kitB, lo, use.lenB);
        bank::KitSample out;
        auto cut = [](std::vector<uint8_t> v, int frames) { if (frames > 0 && size_t(frames) * 32 < v.size()) v.resize(size_t(frames) * 32); return v; };
        if (a && b) {
            // Both kits at once: LSDj sums them under its DIST setting; the
            // sum clipped to 15 stands in for every mode (plan section 4a).
            const auto da = cut(a->nibbles, use.lenA), db = cut(b->nibbles, use.lenB);
            const size_t n = std::max(da.size(), db.size());
            out.data.resize(n);
            for (size_t k = 0; k < n; ++k) out.data[k] = uint8_t(std::min(15, (k < da.size() ? int(da[k]) : 8) + (k < db.size() ? int(db[k]) : 8) - 8));
            out.name = a->name + "+" + b->name;
            notes.add("kit note " + hex2(noteByte) + " plays two samples at once: they are summed and clipped, LSDj's DIST modes are not modelled");
        } else if (a || b) {
            const auto* one = a ? a : b;
            out.data = cut(one->nibbles, a ? use.lenA : use.lenB);
            out.name = one->name;
        } else return 0;
        auto& kit = bank.kits[size_t(use.kitSlot - 1)];
        if (kit.samples.size() >= 32) { notes.add("kit instrument at " + where + " uses more than 32 different sounds; the rest are silent"); return 0; }
        out.note = uint8_t(36 + int(kit.samples.size()));
        out.loopPoint = 0;
        kit.samples.push_back(std::move(out));
        use.noteOf[noteByte] = kit.samples.back().note;
        return kit.samples.back().note;
    }

    // --- commands (sections 34, 46, 49) -----------------------------------
    /// A phrase or table command into a ChipBoy one. `cell` takes an A as its
    /// TBL column; `instKind` and `channel` decide E, F and W. `st` is the
    /// channel's running state for S on noise and L (nullptr in a table, where
    /// S on noise is folded into the transpose column instead); `targetMidi`
    /// the cell's own LSDj note, -1 without one, for L.
    bool command(char letter, int v, const std::string& where, int instKind, int channel, tracker::Cell* cell, Command& out,
                 ChannelState* st = nullptr, int targetMidi = -1)
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
            case 'V':
                if (m.vibratoLaw == VibratoLaw::RegisterOneSided && channel != 3) {
                    // Section 56: 8y units a clock for x + 1 clocks below the note and back.
                    const int speed = std::clamp(int(std::lround(32.0 / double(x + 1))) - 1, 0, 15);
                    const int depth = y == 0 ? 0 : std::clamp(int(std::lround(8.0 * y * (x + 1) / 2.0 / 19.11)), 1, 15);
                    notes.add("V" + hex2(v) + " at " + where + ": this format's vibrato swings below the note in register units; ChipBoy's is centred (speed " + std::to_string(speed) + ", depth " + std::to_string(depth) + ")");
                    out = { Cmd::V, int16_t(speed), int16_t(depth), 0 }; return true;
                }
                out = { Cmd::V, int16_t(x), int16_t(y), 0 }; return true;
            case 'Z': out = { Cmd::Z, int16_t(x), int16_t(y), 0 }; return true;
            case 'M': out = { Cmd::M, int16_t(x), int16_t(y), 0 }; return true;
            case 'R': out = { Cmd::R, int16_t(x), int16_t(y), 0 }; return true;
            case 'H': out = { Cmd::H, int16_t(x), int16_t(y), 0 }; return true;            // times, row: 0-based in both
            case 'E':
                if (instKind == 1) out = { Cmd::E, int16_t(std::min(3, y)), 0, 0 };
                else out = { Cmd::E, int16_t(x), int16_t(y), 0 };
                return true;
            case 'S':
                if (channel == 3) {
                    // The byte goes through as it stands (section 66): on 9.x the
                    // instrument reads it as semitones, before that as the nibble
                    // subtraction on NR43. Either way it is the same two digits.
                    if (m.noiseS == NoiseS::Semitones && st) st->chipNote += signedByte(v);
                    else if (st && st->nr43 >= 0) { const uint8_t nr = nibbleS(uint8_t(st->nr43), v); st->chipNote = noteForNr43(nr, st->chipNote, st->noiseSlot); st->nr43 = nr; }
                    out = { Cmd::S, int16_t(x), int16_t(y), 0 }; return true;
                }
                out = { Cmd::S, int16_t(x & 7), int16_t(y), 0 }; return true;
            case 'D': out = { Cmd::D, int16_t(v), 0, 0 }; return true;
            case 'K': out = { Cmd::K, int16_t(v), 0, 0 }; return true;
            case 'L':
                if (m.pitchLaw == PitchLaw::Register && channel != 3) {
                    // Section 56: a speed in register units a clock, into ChipBoy's duration.
                    if (v == 0) { out = { Cmd::L, 0, 0, 0 }; return true; }
                    if (st == nullptr) { notes.add("L" + hex2(v) + " at " + where + ": a slide speed inside a table has no start note to measure from; dropped"); return false; }
                    if (targetMidi < 0) { notes.add("L" + hex2(v) + " at " + where + " has no note to slide to; dropped"); return false; }
                    if (st->lastMidi < 0) { notes.add("L" + hex2(v) + " at " + where + " has no note before it in the chain to slide from; dropped"); return false; }
                    const double dist = std::fabs(gbPeriod(targetMidi) - gbPeriod(st->lastMidi));
                    const int updates = std::max(1, int(std::ceil(dist / double(v))));
                    out = { Cmd::L, int16_t(std::clamp(updates - 1, 0, 255)), 0, 0 }; return true;
                }
                out = { Cmd::L, int16_t(v), 0, 0 }; return true;
            case 'P':
                // Section 66: P on noise goes through too -- the instrument's
                // Sweep says whether it walks the map or the NR43 nibbles.
                if (channel == 3) { out = { Cmd::P, int16_t(v), 0, 0 }; return true; }
                if (m.pitchLaw == PitchLaw::Register && v != 0) {
                    // Section 56: xx register units a clock, into the Drum speed with the nearest step.
                    const int units = signedByte(v);
                    const int speed = drumSpeedFor(std::abs(units));
                    out = { Cmd::P, int16_t(units < 0 ? 256 - speed : speed), 0, 0 }; return true;
                }
                out = { Cmd::P, int16_t(v), 0, 0 }; return true;                     // the two's-complement byte
            case 'G': out = { Cmd::G, int16_t(std::min(v + 1, 16)), 0, 0 }; return true;           // LSDj's groove 00 is ChipBoy's slot 1
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
    void tables(ImportSummary& sum)
    {
        const auto& users = tableUse;
        for (int t = 0; t < kLsdjTables; ++t) {
            // Allocated, or holding anything: LSDj 9's allocation bytes miss
            // tables its instruments name (measured on the user's saves).
            bool content = at(kTableAlloc + size_t(t)) != 0;
            for (int r = 0; r < 16 && !content; ++r) { const size_t i = size_t(t) * 16 + size_t(r); content = at(kTableEnv + i) || at(kTableTsp + i) || at(kTableCmd1 + i) || at(kTableCmd2 + i); }
            if (!content) continue;
            int noiseBase = -1, noiseInst = -1; bool others = false; std::set<int> bases;
            if (auto it = users.find(t); it != users.end())
                for (const auto& [ins, ch, midi] : it->second) { if (kindFor(ins, ch) == 3) { if (bases.empty() || midi < *bases.begin()) noiseInst = ins; bases.insert(midi); } else others = true; }
            if (!bases.empty()) {
                noiseBase = *bases.begin();
                if (bases.size() > 1) notes.add("table " + hex2(t) + " is used by several noise notes: its transposes are mapped for the lowest; the others land a little off");
                if (others) notes.add("table " + hex2(t) + " is used by noise and non-noise instruments: its transposes are mapped for the noise");
            }
            auto& tb = bank.tables[size_t(t)];
            tb = bank::Table{};
            tb.used = true; tb.name = "Table " + hex2(t); tb.end = bank::TableEnd::Loop; tb.hopStep = 1;
            // Section 56: the transpose column is resolved for the lowest note
            // the table runs with, through the shape rule. An S row goes through
            // as the byte it is: the instrument's Sweep reads it (section 66).
            const int noiseSlot = noiseInst >= 0 ? slotFor(noiseInst, 3) : 0;
            const uint8_t baseNr = noiseBase >= 0 ? lsdjNr43(noiseBase, noiseInst) : uint8_t(0);
            const int baseNote = noiseBase >= 0 ? std::max(12, noteForNr43(baseNr, noiseBase, noiseSlot)) : 0;   // what the cell plays: 12 at least
            for (int r = 0; r < 16; ++r) {
                const size_t i = size_t(t) * 16 + size_t(r);
                const uint8_t env = at(kTableEnv + i), tsp = at(kTableTsp + i);
                auto& st = tb.steps[size_t(r)];
                // Section 64: the ENV byte is an amplitude and a duration in ticks;
                // a low digit of 0 is a blank row and F hops the lane to the row the
                // high digit names.
                const int amp = env >> 4, dur = env & 15;
                if (dur == 0) st.vol = -1;
                // A hop row costs a tick before 8.9.3 and nothing after; ChipBoy's
                // is free, so the older ones ask for the tick with a LEN of 1.
                else if (dur == 15) { st.vol = -1; st.volHop = int8_t(amp); st.volTicks = m.envHopCostsTick ? uint8_t(1) : uint8_t(0); }
                else { st.vol = int8_t(amp); st.volTicks = uint8_t(dur); }
                const std::pair<uint8_t, uint8_t> cmds[2] = { { at(kTableCmd1 + i), at(kTableCmd1V + i) }, { at(kTableCmd2 + i), at(kTableCmd2V + i) } };
                if (tsp) {
                    st.hasTranspose = true;
                    if (noiseBase >= 0) {
                        // Before 9 the column is subtracted from NR43 itself, byte-wise
                        // (a -2 is +2 on the register; measured on the format-3
                        // songs); on 9.x it moves the note through the map.
                        const int midi = noiseBase + signedByte(tsp);
                        const uint8_t nr = m.noiseRule == NoiseRule::Map ? lsdjNr43(midi, noiseInst) : uint8_t((int(baseNr) - signedByte(tsp)) & 0xFF);
                        st.transpose = int8_t(std::clamp(noteForNr43(nr, baseNote, noiseSlot) - baseNote, -128, 127));
                    } else st.transpose = int8_t(signedByte(tsp));
                }
                for (int k = 0; k < 2; ++k) {
                    const char letter = letterOf(cmds[k].first);
                    if (!letter) continue;
                    Command c;
                    if (command(letter, cmds[k].second, "table " + hex2(t) + " row " + std::to_string(r), -1, noiseBase >= 0 ? 3 : -1, nullptr, c)) (k == 0 ? st.cmd1 : st.cmd2) = c;
                }
            }
            ++sum.tables;
        }
    }

    // --- phrases and chains (section 48) -----------------------------------
    /// `state` is the channel's running state (section 56), read at the top
    /// and left as the phrase ends; a phrase converted once keeps its first
    /// reading and hands back the state it left then.
    /// `noiseTsp` is the chain row's transpose when the noise channel folds
    /// it into the note (section 56: before 9 the octave is all that counts,
    /// so the phrase is converted per transpose), else 0.
    int phraseFor(int p, int channel, ChannelState& state, int noiseTsp)
    {
        const auto key = std::make_tuple(p, channel, noiseTsp);
        if (auto it = phraseSlot.find(key); it != phraseSlot.end()) { state = it->second.end; return it->second.slot; }
        if (phrasesOut >= tracker::kPhraseSlots) { notes.add("more phrase copies than ChipBoy's 255 slots; the rest are left empty"); return 0; }
        const int slot = ++phrasesOut;
        auto& ph = song.phrases[size_t(slot - 1)];
        ph = tracker::Phrase{};
        ph.used = true; ph.steps = 16; ph.groove = 0;
        int hopStep = -1;                    // an H that ends the phrase (section 56)
        for (int st = 0; st < 16; ++st) {
            const size_t i = size_t(p) * 16 + size_t(st);
            const uint8_t n = at(kPhraseNotes + i), ins = at(kPhraseInst + i);
            auto& c = ph.cells[size_t(st)];
            if (ins != 0xFF && ins < kLsdjInstruments) { state.inst = ins; state.instSeen = true; c.inst = uint8_t(slotFor(ins, channel)); }
            const int cur = state.inst;
            const int kind = kindFor(cur, channel);
            const int lsdjMidi = n ? int(n) + 35 : -1;
            if (n && !state.instSeen) {
                // A note before any instrument column: LSDj plays the channel's instrument, 00 at the start.
                c.inst = uint8_t(slotFor(cur, channel)); state.instSeen = true;
                notes.add(std::string(channelName(channel)) + " plays notes before any cell names an instrument: LSDj's instrument 00 is used for them");
            }
            if (n && kind == 2) {
                const uint8_t kn = kitNote(cur, int(n), "phrase " + hex2(p) + " step " + std::to_string(st));
                if (kn) c.note = kn;
            } else if (n) {
                int midi = lsdjMidi;
                if (kind == 1) midi += m.waveOctave;                       // the wave channel's period table (section 45)
                else if (kind == 3) {
                    const int tspMidi = midi + (transposeOn(cur) ? noiseTsp : 0);
                    const uint8_t nr43 = lsdjNr43(tspMidi, cur);
                    state.noiseSlot = slotFor(cur, channel);
                    noiseWidths[state.noiseSlot].insert((nr43 & 8) != 0);
                    midi = noteForNr43(nr43, tspMidi, state.noiseSlot);
                    if (midi < 12) { notes.add("noise NR43 " + hex2(nr43) + " clocks below ChipBoy's lowest noise note (a click rather than a pitch); the lowest, note 12, is used"); midi = 12; }
                    state.nr43 = nr43; state.chipNote = midi;             // an S on this row moves on from here
                }
                c.note = uint8_t(std::clamp(midi, kind == 3 ? 12 : 1, 127));
            }
            const char letter = letterOf(at(kPhraseCmd + i));
            if (letter == 'H' && hopStep < 0) {
                // H in a phrase is a chain hop, not a table hop (section 56):
                // `H 0 y` ends the phrase here and starts the next at its row y,
                // which is ChipBoy's phrase length when y is 0.
                const uint8_t v = at(kPhraseCmdV + i);
                hopStep = st;
                const std::string where = " at phrase " + hex2(p) + " step " + std::to_string(st);
                // The high digit is how many times, 0 meaning every time: `H 4 0`
                // ends the phrase four times and then lets it play in full, which
                // measures as four short passes and one long one on 8.4.4. ChipBoy
                // has no count on a phrase, so it ends every time -- right four
                // passes in five rather than wrong in all of them.
                if (v >> 4) notes.add("H" + hex2(v) + where + " ends the phrase " + std::to_string(v >> 4) + " times and then lets it play in full; ChipBoy ends it every time (section 56)");
                if (v & 15) notes.add("H" + hex2(v) + where + " starts the next phrase at row " + std::to_string(v & 15) + "; ChipBoy's phrases always start at row 0, so it starts there");
                continue;
            }
            if (letter) {
                Command cmd;
                if (command(letter, at(kPhraseCmdV + i), "phrase " + hex2(p) + " step " + std::to_string(st), kind, channel, &c, cmd, &state, lsdjMidi)) c.cmd1 = cmd;
            }
            if (lsdjMidi > 0 && kind != 3) state.lastMidi = lsdjMidi;      // an L on a later row slides from here
        }
        if (hopStep >= 0) { ph.steps = uint8_t(std::max(1, hopStep)); if (hopStep == 0) ph.cells[0] = tracker::Cell{}; }
        phraseSlot[key] = PhraseOut{ slot, state };
        return slot;
    }
    void chains(ImportSummary& sum)
    {
        static const char* kNames[4] = { "PU1", "PU2", "WAV", "NOI" };
        bool noiseTsp = false;
        std::array<ChannelState, 4> state{};
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
                    const bool fold = ch == 3 && m.noiseRule != NoiseRule::Map;   // section 56: the octave is all that counts, resolved here
                    const int slot = phraseFor(p, ch, state[size_t(ch)], fold ? tsp : 0);
                    auto& chain = song.chain[size_t(ch)];
                    chain.push_back(uint8_t(slot));
                    if (tsp && !fold) { song.setTranspose(ch, int(chain.size()) - 1, int8_t(tsp)); if (ch == 3) noiseTsp = true; }
                }
            }
        }
        if (noiseTsp) notes.add("chain transposes on the noise channel go through ChipBoy's map: near LSDj's pitch, not on it");
        sum.phrases = phrasesOut;
    }
    void noiseWidthsToInstruments()
    {
        for (const auto& [slot, widths] : noiseWidths) {                    // keyed by the ChipBoy slot, 1-based
            auto& o = bank.instruments[size_t(slot - 1)];
            if (widths.size() > 1) notes.add("noise instrument " + o.name + " plays both 15-bit and 7-bit notes in LSDj; ChipBoy's width is per instrument (15-bit chosen)");
            o.lfsr7 = widths.size() == 1 && *widths.begin();
        }
    }
    /// Section 63: before LSDj 9 a table's `G` gives every row of the run the
    /// groove's *first* step, where ChipBoy (and LSDj 9) walk the groove. The
    /// two agree when the groove has one step, so each `G` in a table is
    /// pointed at a one step groove holding that count, taken from a slot the
    /// song never names. Runs after grooves(), which fills the sixteen slots.
    void flattenTableGrooves()
    {
        if (m.tableGrooveWalks) return;
        std::set<int> named;                       // groove slots the song asks for, 1-16
        auto noteG = [&named](const bank::Command& c) { if (c.cmd == bank::Cmd::G && c.a >= 1 && c.a <= 16) named.insert(int(c.a)); };
        for (const auto& ph : song.phrases) {
            if (!ph.used) continue;
            if (ph.groove >= 1 && ph.groove <= 16) named.insert(int(ph.groove));
            for (const auto& c : ph.cells) { noteG(c.cmd1); noteG(c.cmd2); }
        }
        for (const auto& tb : bank.tables) { if (!tb.used) continue; for (const auto& st : tb.steps) { noteG(st.cmd1); noteG(st.cmd2); } }
        // Free slots, least precious first: a slot LSDj left empty, then one
        // holding its 6 6 default, and only then a groove the user wrote but
        // never names -- and the high slots before the low ones, since LSDj
        // fills its grooves from the bottom. Taking a groove the song can
        // still see in the Grooves tab is the last resort, not the first.
        std::vector<std::pair<int, int>> ranked;      // (how precious, slot)
        for (int g = 15; g >= 0; --g) {
            if (named.count(g + 1)) continue;
            bool empty = true, dflt = true;
            for (int k = 0; k < 16 && (empty || dflt); ++k) {
                const uint8_t b = at(kGrooves + size_t(g) * 16 + size_t(k));
                if (b != 0) empty = false;
                if (b != (k < 2 ? 6 : 0)) dflt = false;
            }
            ranked.push_back({ empty ? 0 : dflt ? 1 : 2, g + 1 });
        }
        std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<int> free;
        for (const auto& [rank, slot] : ranked) free.push_back(slot);
        bool tookAGroove = false;
        std::map<int, int> flatOf;                 // ticks -> the slot holding a one step groove of them
        size_t spare = 0; bool ranOut = false;
        auto slotFor = [&](int ticks) -> int {
            if (auto f = flatOf.find(ticks); f != flatOf.end()) return f->second;
            if (spare >= free.size()) { ranOut = true; return 0; }
            const int slot = free[spare++];
            if (ranked[spare - 1].first == 2) tookAGroove = true;
            auto& g = song.grooves[size_t(slot - 1)];
            g = tracker::Groove{};
            g.ticks.fill(0);
            g.ticks[0] = uint8_t(ticks);
            // Name it for the G it stands in for, so a song read beside LSDj
            // says where the number went (section 63).
            const std::string label = "held " + std::to_string(ticks);
            for (size_t k = 0; k < g.name.size() - 1 && k < label.size(); ++k) g.name[k] = label[k];
            flatOf.emplace(ticks, slot);
            return slot;
        };
        bool moved = false;
        for (auto& tb : bank.tables) {
            if (!tb.used) continue;
            for (auto& st : tb.steps)
                for (auto* c : { &st.cmd1, &st.cmd2 }) {
                    if (c->cmd != bank::Cmd::G || c->a < 1 || c->a > 16) continue;
                    const int ticks = int(song.grooves[size_t(c->a - 1)].ticks[0]);
                    if (ticks == 0) continue;
                    const int slot = slotFor(ticks);
                    if (slot == 0) continue;
                    if (slot != int(c->a)) { c->a = int16_t(slot); moved = true; }
                }
        }
        if (moved) notes.add("a G in a table holds the groove's first step before LSDj 9 (section 63); those rows point at a one step groove of that length instead");
        if (tookAGroove) notes.add("the song leaves no spare groove slot, so a groove it never names was overwritten to make room for one of those (section 63)");
        if (ranOut) notes.add("the song leaves no spare groove, so a G in a table keeps its own; its rows will swing where LSDj held one length (section 63)");
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

int chipboyNoteForClock(double hz, int prefer)
{
    const double c = std::log(hz);
    int best = 1; double bestErr = 1e9; int bestDist = 1000;
    for (int k = -driver::Driver::kNoiseMapBelow; k < 128; ++k) {      // the map continues below the keyboard (section 55)
        uint8_t sh = 0, d = 0;
        driver::Driver::noisePairForNote(k, sh, d);
        const double err = std::round(std::fabs(std::log(noiseClockHz(sh, d)) - c) * 1e6) / 1e6;
        const int dist = std::abs(k - prefer);
        if (err < bestErr || (err == bestErr && dist < bestDist)) { best = k; bestErr = err; bestDist = dist; }
    }
    return best;
}

int chipboyNoteForNr43(uint8_t v, int prefer) { return chipboyNoteForClock(noiseClockHz(v >> 4, v & 7), prefer); }

bool importSong(const uint8_t* bytes, size_t size, const LsdjModel& model,
                bank::Bank& bank, tracker::Song& out, ImportSummary& summary, ImportNotes& notes,
                const std::vector<LsdjKit>* kits)
{
    if (bytes == nullptr || size < 0x8000) return false;
    summary = ImportSummary{};
    { auto blank = std::make_unique<bank::Bank>(); bank = std::move(*blank); }
    { auto blank = std::make_unique<tracker::Song>(); out = std::move(*blank); }
    Reader r(bytes, model, bank, out, notes);
    r.kits = kits;
    const int tempo = std::clamp<int>(bytes[kTempo], 40, 255);
    summary.tempoBpm = tempo;
    out.tempoBpm = tempo;
    out.transpose = int8_t(signedByte(bytes[kSongTranspose]));      // the PROJECT screen's TRANSPOSE (section 61)
    if (out.transpose) notes.add("the song's own transpose is " + std::to_string(int(out.transpose)) + " semitones; it moves every note whose instrument admits a transpose");
    r.tickMs = 60000.0 / (double(tempo) * 24.0);
    r.usage();
    r.chooseNoiseOffsets();
    r.instruments(summary);
    r.tables(summary);
    r.chains(summary);
    r.noiseWidthsToInstruments();
    r.grooves();
    r.flattenTableGrooves();
    summary.waves = int(r.waveSlotOfSynth.size());
    summary.kits = r.kitSlots;
    for (auto& src : out.noteSource) src = tracker::NoteSource::Tracker;
    for (auto& arm : out.recordArm) arm = false;
    tracker::buildRowTables(out);
    return true;
}

} // namespace chipboy::lsdj
