// lsdjref-compare -- the same test song through ChipBoy's driver, against the
// register stream the real LSDj produced.
//
// It reads tools/lsdjref/cases.spec (the file the save authoring tool reads),
// builds each case as a ChipBoy song and bank, plays it headless through the
// Clock, the Player and the Driver with the write log on, and diffs that
// stream against the trace tool's CSV.
//
// Only chipboy_core is linked: the Driver is what is under test and it knows
// nothing of JUCE, so no plugin, no host and no song file are needed. The
// song is built in memory rather than written as a .cbsong for the same
// reason -- the writers live in the plugin shell.
//
//   lsdjref-compare --spec cases.spec --traces DIR [--out REPORT.md]
//                   [--dump DIR] [--model dmg|cgb] [--tolerance CYCLES]
//
// Exit code 0 when every case had a trace to compare against; the report is
// an artefact, not a verdict, so a difference is not a failure.
#include "core/Console.h"
#include "core/Apu/Apu.h"
#include "core/Bank/Bank.h"
#include "core/Driver/Clock.h"
#include "core/Driver/Driver.h"
#include "core/Tracker/Player.h"
#include "core/Tracker/Song.h"

#include "Spec.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace chipboy;

namespace {

constexpr int      kSkipExit = 77;
constexpr double   kSampleRate = 48000.0;
constexpr uint32_t kBlock = 512;

/* ------------------------------------------------ the LSDj side, as read */

struct Write {
    uint64_t cycle = 0;
    uint16_t addr = 0;
    uint8_t  value = 0;
};

const char* regName(uint16_t a)
{
    switch (a) {
        case 0xFF10: return "NR10"; case 0xFF11: return "NR11"; case 0xFF12: return "NR12"; case 0xFF13: return "NR13"; case 0xFF14: return "NR14";
        case 0xFF16: return "NR21"; case 0xFF17: return "NR22"; case 0xFF18: return "NR23"; case 0xFF19: return "NR24";
        case 0xFF1A: return "NR30"; case 0xFF1B: return "NR31"; case 0xFF1C: return "NR32"; case 0xFF1D: return "NR33"; case 0xFF1E: return "NR34";
        case 0xFF20: return "NR41"; case 0xFF21: return "NR42"; case 0xFF22: return "NR43"; case 0xFF23: return "NR44";
        case 0xFF24: return "NR50"; case 0xFF25: return "NR51"; case 0xFF26: return "NR52";
        default: return (a >= 0xFF30 && a <= 0xFF3F) ? "WAVE" : "?";
    }
}

/// Which hardware channel a register belongs to; 4 for the global pair and
/// wave RAM, -1 for what is neither.
int channelOf(uint16_t a)
{
    if (a >= 0xFF10 && a <= 0xFF14) return 0;
    if (a >= 0xFF16 && a <= 0xFF19) return 1;
    if (a >= 0xFF1A && a <= 0xFF1E) return 2;
    if (a >= 0xFF20 && a <= 0xFF23) return 3;
    if (a == 0xFF24 || a == 0xFF25 || a == 0xFF26) return 4;
    if (a >= 0xFF30 && a <= 0xFF3F) return 4;
    return -1;
}

/// The register that carries the trigger bit on a channel.
bool isTrigger(uint16_t a, uint8_t v)
{
    return (a == 0xFF14 || a == 0xFF19 || a == 0xFF1E || a == 0xFF23) && (v & 0x80) != 0;
}

bool readTrace(const std::string& path, std::vector<Write>& out)
{
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line.compare(0, 5, "cycle") == 0) continue;
        // cycle,addr,name,value[,vol]
        Write w;
        char* end = nullptr;
        w.cycle = std::strtoull(line.c_str(), &end, 10);
        if (end == nullptr || *end != ',') continue;
        w.addr = uint16_t(std::strtol(end + 1, &end, 16));
        if (end == nullptr || *end != ',') continue;
        const char* name = end + 1;
        const char* comma = std::strchr(name, ',');
        if (comma == nullptr) continue;
        w.value = uint8_t(std::strtol(comma + 1, nullptr, 16));
        if (channelOf(w.addr) >= 0) out.push_back(w);
    }
    return true;
}

/* ---------------------------------------------- the spec, as ChipBoy sees */

/// LSDj's letter to ChipBoy's. The letters ChipBoy does not define on this
/// channel come back as None and the case says so in the report.
bank::Cmd cmdOf(char letter)
{
    switch (letter) {
        case 'A': return bank::Cmd::A; case 'C': return bank::Cmd::C; case 'D': return bank::Cmd::D;
        case 'E': return bank::Cmd::E; case 'F': return bank::Cmd::F; case 'G': return bank::Cmd::G;
        case 'H': return bank::Cmd::H; case 'K': return bank::Cmd::K; case 'L': return bank::Cmd::L;
        case 'M': return bank::Cmd::M; case 'O': return bank::Cmd::O; case 'P': return bank::Cmd::P;
        case 'R': return bank::Cmd::R; case 'S': return bank::Cmd::S; case 'T': return bank::Cmd::T;
        case 'V': return bank::Cmd::V; case 'W': return bank::Cmd::W; case 'Z': return bank::Cmd::Z;
        default:  return bank::Cmd::None;
    }
}

/// LSDj's one byte to ChipBoy's x and y (docs/LSDJ_PARITY.md, "the mapping").
/// Nibble letters split; the byte letters carry the whole byte, with A and G
/// moved up one because ChipBoy numbers slots from 1 and P re-centred because
/// ChipBoy stores the signed speed biased by 128.
bank::Command commandOf(const lsdjref::SpecCommand& c)
{
    bank::Command out;
    out.cmd = cmdOf(c.letter);
    const int hi = (c.value >> 4) & 0x0F, lo = c.value & 0x0F;
    switch (out.cmd) {
        case bank::Cmd::C: case bank::Cmd::E: case bank::Cmd::M:
        case bank::Cmd::R: case bank::Cmd::S: case bank::Cmd::V: case bank::Cmd::Z:
            out.a = int16_t(hi); out.b = int16_t(lo); break;
        case bank::Cmd::A: case bank::Cmd::G:
            out.a = int16_t(c.value + 1); break;      // ChipBoy's slots start at 1
        case bank::Cmd::P:
            out.a = int16_t(c.value); break;          // two's complement, as LSDj stores it (section 34)
        case bank::Cmd::H:
            out.a = int16_t(hi); out.b = int16_t(lo); break;   // times, row (section 34)
        default:
            out.a = int16_t(c.value); break;
    }
    return out;
}

/// LSDj's NRx2 byte to ChipBoy's three envelope fields.
void envelopeOf(uint8_t env, bank::InstrumentCore& i)
{
    const int vol = (env >> 4) & 0x0F, y = env & 0x0F;
    i.envVol = uint8_t(vol);
    if (y == 0 || y == 8) { i.envDir = bank::EnvDir::Down; i.envRate = 0; }
    else if (y < 8)       { i.envDir = bank::EnvDir::Down; i.envRate = uint8_t(y); }
    else                  { i.envDir = bank::EnvDir::Up;   i.envRate = uint8_t(y - 8); }
}

bank::Pan panOf(int out)
{
    switch (out & 3) {
        case 0: return bank::Pan::Off;
        case 1: return bank::Pan::Right;
        case 2: return bank::Pan::Left;
        default: return bank::Pan::Both;
    }
}

bank::PitchSpeed pitchOf(const std::string& s)
{
    if (s == "tick") return bank::PitchSpeed::Tick;
    if (s == "step") return bank::PitchSpeed::Step;
    if (s == "drum") return bank::PitchSpeed::Drum;
    return bank::PitchSpeed::Fast;
}

void buildBank(const lsdjref::SpecCase& c, bank::Bank& b)
{
    for (const auto& [slot, spec] : c.instruments) {
        if (slot < 0 || slot >= bank::kInstrumentSlots - 1) continue;
        auto& inst = b.instruments[size_t(slot)];       // LSDj slot n is ChipBoy slot n + 1
        if (spec.kind == "wave") {
            inst = bank::Instrument::defaults(bank::InstrumentType::Wave, "lsdjref");
            inst.waveLevel = uint8_t(spec.get("volume", 3));
            inst.wave = uint8_t(spec.get("wave", 0) + 1);
            inst.frameAdvance = uint8_t(spec.get("speed", 0));
            switch (spec.get("playtype", 2)) {
                case 1:  inst.frameLoop = bank::FrameLoop::Once; break;
                case 3:  inst.frameLoop = bank::FrameLoop::PingPong; break;
                default: inst.frameLoop = bank::FrameLoop::Loop; break;
            }
        }
        else if (spec.kind == "noise") {
            inst = bank::Instrument::defaults(bank::InstrumentType::Noise, "lsdjref");
            envelopeOf(uint8_t(spec.get("env", 0xF0)), inst);
        }
        else {
            inst = bank::Instrument::defaults(bank::InstrumentType::Pulse, "lsdjref");
            envelopeOf(uint8_t(spec.get("env", 0xF0)), inst);
            inst.duty = uint8_t(spec.get("duty", 2));
            // LSDj stores the sweep as NR10's complement (measured); ChipBoy
            // keeps NR10's own three fields.
            const uint8_t nr10 = uint8_t(~uint8_t(spec.get("sweep", 0xFF)));
            inst.sweepRate = uint8_t((nr10 >> 4) & 7);
            inst.sweepDown = (nr10 & 0x08) != 0;
            inst.sweepShift = uint8_t(nr10 & 7);
        }
        inst.used = true;
        inst.pitchSpeed = pitchOf(spec.text("pitch", "fast"));
        inst.cmdRate = uint8_t(spec.get("cmdrate", 0));
        inst.pan = panOf(spec.get("out", 3));
        inst.vib.shape = bank::VibShape(std::min(2, spec.get("vib_shape", 0)));
        inst.vib.dir = spec.get("vib_dir", 0) ? bank::VibDir::Up : bank::VibDir::Down;
        inst.tableMode = spec.get("table_step", 0) ? bank::TableMode::Step : bank::TableMode::Tick;
        const auto t = spec.fields.find("table");
        inst.table = (t == spec.fields.end() || t->second == "-") ? 0 : uint8_t(spec.get("table", 0) + 1);
        // Nothing in a test case wants a note cut short by the driver, so the
        // instrument holds until the next note, as LSDj's does.
        inst.noteOff = bank::NoteOff::Kill;
        inst.length = 0;
    }

    for (const auto& [slot, rows] : c.tables) {
        if (slot < 0 || slot >= bank::kTableSlots - 1) continue;
        auto& tab = b.tables[size_t(slot)];
        tab.used = true;
        tab.name = "lsdjref";
        for (const auto& r : rows) {
            if (r.step < 0 || r.step >= bank::kTableSteps) continue;
            auto& s = tab.steps[size_t(r.step)];
            // A table's volume column is LSDj's NRx2 byte; ChipBoy's is a
            // level 0-15 with -1 for blank. Measured: a row **writes nothing
            // at all** unless both nibbles are non-zero -- `F0`, `80`, `01` and
            // `00` are all blank rows -- so that is how the byte translates.
            s.vol = ((r.env >> 4) & 0x0F) != 0 && (r.env & 0x0F) != 0
                    ? int8_t((r.env >> 4) & 0x0F) : int8_t(-1);
            if (r.transpose != 0) { s.hasTranspose = true; s.transpose = int8_t(r.transpose); }
            s.cmd1 = commandOf(r.cmd1);
            s.cmd2 = commandOf(r.cmd2);
        }
    }
}

void buildSong(const lsdjref::SpecCase& c, tracker::Song& s, double seconds)
{
    s.tempoBpm = double(c.tempo);
    for (const auto& [slot, ticks] : c.grooves) {
        if (slot < 0 || slot >= 16) continue;
        auto& g = s.grooves[size_t(slot)];
        g.ticks.fill(0);
        for (size_t i = 0; i < ticks.size() && i < g.ticks.size(); ++i) g.ticks[i] = uint8_t(ticks[i]);
    }
    for (const auto& [slot, rows] : c.phrases) {
        if (slot < 0 || slot >= tracker::kPhraseSlots - 1) continue;
        auto& p = s.phrases[size_t(slot)];           // LSDj phrase n is ChipBoy slot n + 1
        p.used = true;
        for (const auto& r : rows) {
            if (r.step < 0 || r.step >= tracker::kMaxSteps) continue;
            auto& cell = p.cells[size_t(r.step)];
            if (!r.note.empty()) {
                const int midi = lsdjref::midiOf(r.note);
                if (midi > 0 && midi < 128) cell.note = uint8_t(midi);
            }
            if (r.instrument >= 0) cell.inst = uint8_t(r.instrument + 1);
            cell.cmd1 = commandOf(r.cmd);
        }
    }
    for (const auto& [ch, phrases] : c.chains) {
        if (ch < 0 || ch > 3) continue;
        s.noteSource[size_t(ch)] = tracker::NoteSource::Tracker;
        auto& chain = s.chain[size_t(ch)];
        // LSDj plays the song round and round for as long as the capture runs,
        // and nothing is flushed at the seam. Repeating the chain here is that,
        // and it avoids the plugin transport's loop, which *does* flush.
        const size_t need = size_t(seconds * 48.0 / 96.0) + 2;
        while (chain.size() < need && !phrases.empty())
            for (int p : phrases) chain.push_back(uint8_t(p + 1));
        if (phrases.empty()) continue;
    }
    tracker::buildRowTables(s);
    tracker::buildTempoMap(s, s.tempoBpm);
}

/* --------------------------------------------------------- the playback */

/// Plays the case through the driver and returns the writes it made.
std::vector<Write> playChipBoy(const lsdjref::SpecCase& c, Console console, double seconds)
{
    auto bank = std::make_unique<bank::Bank>();
    auto song = std::make_unique<tracker::Song>();
    buildBank(c, *bank);
    buildSong(c, *song, seconds);

    driver::Clock clock;
    tracker::Player player;
    driver::Driver driver;
    clock.prepare(kSampleRate);
    player.prepare(kSampleRate);
    driver.prepare(kSampleRate, bank.get(), song.get(), console);
    player.setSong(song.get());

    driver::ClockConfig cc;
    cc.source = driver::TempoSource::Song;
    cc.songTempo = double(c.tempo);
    clock.setConfig(cc);
    if (!song->tempoMap.empty()) clock.setTempoMap(song->tempoMap.data(), song->tempoMap.size());
    clock.setOwnsTransport(true);
    clock.ownPlay();

    std::vector<driver::RegWrite> log;
    log.reserve(1u << 16);
    driver.setWriteLog(&log);
    for (int ch = 0; ch < 4; ++ch) driver.setParams(ch, driver::ChannelParams{});

    std::vector<driver::NoteEvent> events;
    events.reserve(256);
    std::vector<driver::RegWrite> blockWrites;
    const auto cycleAt = [](uint64_t f) { return uint64_t(double(f) * double(chipboy::kCpuHz) / kSampleRate); };

    const int64_t blocks = int64_t(seconds * kSampleRate / kBlock) + 1;
    uint64_t frame = 0;
    for (int64_t b = 0; b < blocks; ++b) {
        driver::Transport t;                 // the plugin's own transport owns the clock
        clock.process(t, kBlock, frame);
        for (int ch = 0; ch < 4; ++ch) {
            const int slot = driver.tableGrooveSlot(ch);
            driver.setTableGroove(ch, slot >= 1 && slot <= 16 ? song->grooves[size_t(slot - 1)].ticks.data() : nullptr);
            driver.setViewGroove(ch, player.groove(ch));
        }
        events.clear();
        player.process(clock.ticks(), clock.tickCount(), clock.playing(), events);
        blockWrites.clear();
        driver.process(events.data(), events.size(), kBlock, frame, clock.ticks(), clock.tickCount(), cycleAt, blockWrites);
        frame += kBlock;
    }
    driver.setWriteLog(nullptr);

    std::vector<Write> out;
    out.reserve(log.size());
    for (const auto& w : log)
        if (channelOf(w.addr) >= 0) out.push_back({ w.cycle, w.addr, w.value });
    std::stable_sort(out.begin(), out.end(), [](const Write& a, const Write& b) { return a.cycle < b.cycle; });
    return out;
}

/* ------------------------------------------------------------ the diff */

struct Verdict {
    std::string text;                 ///< identical | timing | values | no trace
    size_t      lsdjWrites = 0, chipboyWrites = 0;
    size_t      firstDivergence = 0;
    size_t      phaseWrites = 0;      ///< trailing pitch updates put down to the clock's phase
    size_t      tailWrites = 0;       ///< writes past where the shorter capture stopped
    uint64_t    worstSkew = 0;        ///< the widest gap between a write and its opposite number
    std::string detail;
};

/// The register that carries a channel's level: NR12, NR22, NR32, NR42.
uint16_t levelRegister(int channel)
{
    static const uint16_t kRegs[4] = { 0xFF12, 0xFF17, 0xFF1C, 0xFF21 };
    return kRegs[channel & 3];
}

/// The first note-on in a stream: where the two runs are lined up.
///
/// Not simply the first trigger. Before the song starts, LSDj beeps its
/// interface by writing a period and a trigger and nothing else, so a note-on
/// is a trigger whose channel had its level register written just before it --
/// which is what both a real driver and ChipBoy's do, and what an interface
/// click does not.
size_t firstNoteOn(const std::vector<Write>& w)
{
    constexpr uint64_t kSetupWindow = 4096;    // about a millisecond
    for (size_t i = 0; i < w.size(); ++i) {
        if (!isTrigger(w[i].addr, w[i].value)) continue;
        const uint16_t level = levelRegister(channelOf(w[i].addr));
        for (size_t j = i; j-- > 0;) {
            if (w[i].cycle - w[j].cycle > kSetupWindow) break;
            if (w[j].addr == level) return i;
        }
    }
    return w.size();
}

/// The writes of one channel from an alignment point on, as (offset, addr, value).
std::vector<Write> channelStream(const std::vector<Write>& w, size_t from, int channel, uint64_t origin)
{
    std::vector<Write> out;
    for (size_t i = from; i < w.size(); ++i) {
        if (channelOf(w[i].addr) != channel) continue;
        out.push_back({ w[i].cycle - origin, w[i].addr, w[i].value });
    }
    return out;
}

std::string hex2(uint8_t v)
{
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02X", v);
    return buf;
}

/// The note-ons in a channel's stream, as the index of the **first write of
/// each one**. A note-on is a burst of five writes ending in the trigger, and
/// the four before it belong to the note that is starting, not to the one that
/// is ending: splitting at the trigger would put them in the wrong note and
/// make every note boundary look like a difference.
std::vector<size_t> noteOnsOf(const std::vector<Write>& w, int channel)
{
    constexpr uint64_t kSetupWindow = 4096;   // a level write this far back makes it a note
    constexpr uint64_t kBurst = 1024;         // ... and the burst itself is this tight
    std::vector<size_t> out;
    for (size_t i = 0; i < w.size(); ++i) {
        if (channelOf(w[i].addr) != channel || !isTrigger(w[i].addr, w[i].value)) continue;
        const uint16_t level = levelRegister(channel);
        bool isNote = false;
        for (size_t j = i; j-- > 0;) {
            if (w[i].cycle - w[j].cycle > kSetupWindow) break;
            if (w[j].addr == level) { isNote = true; break; }
        }
        if (!isNote) continue;
        size_t start = i;
        while (start > 0 && w[i].cycle - w[start - 1].cycle <= kBurst
               && !isTrigger(w[start - 1].addr, w[start - 1].value)) --start;
        out.push_back(start);
    }
    return out;
}

/// Compares one channel of the two streams.
///
/// The two are lined up at the **first note-on**, and then again at **every
/// note-on**, because two things that are not the driver's behaviour separate
/// them otherwise and would swamp everything that is:
///
///  - LSDj counts its tempo in timer interrupts, so a tick is a whole number
///    of them and jitters by one either way -- 11712 cycles, three times this
///    tolerance -- while ChipBoy's clock is exact. Over a twenty-second case
///    the two tick rates also drift apart by 88 parts per million.
///  - The pitch clock free-runs on both sides, so where a note falls inside
///    its period is where the player pressed play. That is why the tolerance
///    is one whole period: a write may be up to a period from its opposite
///    number without either engine being wrong.
///
/// Inside a note the comparison is strict: register for register, value for
/// value, in order. The one allowance is the **last pitch update** of a note,
/// which belongs to whichever side's clock ticked once more before the next
/// note-on; a difference of one trailing NRx3/NRx4 pair is reported as a phase
/// artefact and not as a difference.
Verdict compareStreams(const std::vector<Write>& lsdj, const std::vector<Write>& chip,
                       int channel, uint64_t tolerance)
{
    Verdict v;
    const size_t la = firstNoteOn(lsdj), ca = firstNoteOn(chip);
    if (la == lsdj.size() || ca == chip.size()) { v.text = "no note-on"; return v; }
    auto ls = channelStream(lsdj, la, channel, lsdj[la].cycle);
    auto cs = channelStream(chip, ca, channel, chip[ca].cycle);
    v.lsdjWrites = ls.size();
    v.chipboyWrites = cs.size();
    if (ls.empty() && cs.empty()) { v.text = "silent on both"; return v; }

    // Compare only as far as both captures go. The two runs are the same
    // length in *seconds*, but LSDj's tempo counter and ChipBoy's clock do not
    // agree to the tick, so one of them fits a note more in at the end; what
    // that note wrote is not a difference between the drivers.
    auto ln = noteOnsOf(ls, channel), cn = noteOnsOf(cs, channel);
    if (!ln.empty() && !cn.empty()) {
        const size_t notes = std::min(ln.size(), cn.size());
        if (ln.size() > notes) { ls.resize(ln[notes]); ln.resize(notes); }
        if (cn.size() > notes) { cs.resize(cn[notes]); cn.resize(notes); }
    } else {
        const size_t n = std::min(ls.size(), cs.size());
        ls.resize(n); cs.resize(n);
    }
    // Walk the two streams note by note. `li`/`ci` are the read positions and
    // `lo`/`co` the origins the times are measured from.
    size_t li = 0, ci = 0, ni = 0;
    uint64_t lo = 0, co = 0;
    bool values = true, timing = true;
    size_t phase = 0;                       // trailing updates written off as clock phase
    uint64_t worst = 0;
    size_t index = 0;
    auto isPeriodWrite = [&](const Write& w) {
        const uint16_t lo3[4] = { 0xFF13, 0xFF18, 0xFF1D, 0xFF22 };
        const uint16_t hi4[4] = { 0xFF14, 0xFF19, 0xFF1E, 0xFF23 };
        return (w.addr == lo3[channel & 3] || w.addr == hi4[channel & 3]) && !isTrigger(w.addr, w.value);
    };
    while (li < ls.size() || ci < cs.size()) {
        const size_t lEnd = ni < ln.size() ? ln[ni] : ls.size();
        const size_t cEnd = ni < cn.size() ? cn[ni] : cs.size();
        // The stretch up to the next note-on on each side.
        while (li < lEnd || ci < cEnd) {
            if (li >= lEnd || ci >= cEnd) {
                // One side has more writes in this note than the other. A
                // trailing pitch update is the free-running clock; anything
                // else is a real difference.
                const bool trailing = (li < lEnd ? isPeriodWrite(ls[li]) : isPeriodWrite(cs[ci]));
                // At the very end of the comparison the two captures simply
                // stop at different places inside the last note -- the runs are
                // the same length in seconds and the two tempo counters do not
                // agree to the tick -- so whatever is left over there is
                // counted and reported, not called a difference. Inside a note
                // the allowance is one NRx3/NRx4 pair, and a pair only.
                const bool last = lEnd == ls.size() && cEnd == cs.size();
                if (last) { ++v.tailWrites; if (li < lEnd) ++li; else ++ci; continue; }
                if (trailing && (li >= lEnd ? cEnd - ci : lEnd - li) <= 2) {
                    ++phase;
                    if (li < lEnd) ++li; else ++ci;
                    continue;
                }
                if (v.detail.empty()) {
                    char buf[256];
                    std::snprintf(buf, sizeof buf, "#%zu one stream has %s and the other has nothing",
                                  index, li < lEnd ? regName(ls[li].addr) : regName(cs[ci].addr));
                    v.detail = buf;
                }
                values = false;
                if (li < lEnd) ++li; else ++ci;
                ++index;
                continue;
            }
            bool same = ls[li].addr == cs[ci].addr && ls[li].value == cs[ci].value;
            // A local resynchronisation, and the only one: the free-running
            // pitch clock gives one side an update the other does not have at
            // the end of a note, so one NRx3/NRx4 pair can be a write ahead.
            // Skipping it is allowed where the writes after it line up again.
            if (!same) {
                auto agrees = [&](size_t i, size_t j) {
                    for (size_t k = 0; k < 4; ++k) {
                        if (i + k >= lEnd || j + k >= cEnd) return false;
                        if (ls[i + k].addr != cs[j + k].addr || ls[i + k].value != cs[j + k].value) return false;
                    }
                    return true;
                };
                for (size_t skip = 1; skip <= 2 && !same; ++skip) {
                    if (isPeriodWrite(ls[li]) && agrees(li + skip, ci)) { li += skip; phase += skip; same = ls[li].addr == cs[ci].addr && ls[li].value == cs[ci].value; }
                    else if (isPeriodWrite(cs[ci]) && agrees(li, ci + skip)) { ci += skip; phase += skip; same = ls[li].addr == cs[ci].addr && ls[li].value == cs[ci].value; }
                }
            }
            const uint64_t lt = ls[li].cycle - lo, ct = cs[ci].cycle - co;
            const uint64_t skew = lt > ct ? lt - ct : ct - lt;
            if (skew > worst) worst = skew;
            if (!same) values = false;
            if (skew > tolerance) timing = false;
            if ((!same || skew > tolerance) && v.detail.empty()) {
                char buf[256];
                std::snprintf(buf, sizeof buf, "#%zu LSDj %s=%s at %llu, ChipBoy %s=%s at %llu",
                              index, regName(ls[li].addr), hex2(ls[li].value).c_str(), (unsigned long long) lt,
                              regName(cs[ci].addr), hex2(cs[ci].value).c_str(), (unsigned long long) ct);
                v.detail = buf;
                v.firstDivergence = index;
            }
            ++li; ++ci; ++index;
        }
        if (ni < ln.size() && ni < cn.size()) { lo = ls[ln[ni]].cycle; co = cs[cn[ni]].cycle; }
        else if (li >= ls.size() && ci >= cs.size()) break;
        else if (ni >= ln.size() || ni >= cn.size()) {
            // One side has more note-ons than the other: that *is* a
            // difference, and the rest of the stream is reported as one.
            if (v.detail.empty()) v.detail = "one stream has more note-ons than the other";
            values = false;
            break;
        }
        ++ni;
    }
    v.phaseWrites = phase;
    v.worstSkew = worst;
    if (values && timing) v.text = phase || v.tailWrites ? "same values, timing within tolerance" : "identical";
    else if (values)      v.text = "same values, timing outside tolerance";
    else                  v.text = "different values";
    return v;
}

void writeCsv(const std::string& path, const std::vector<Write>& w, const char* what)
{
    std::ofstream o(path);
    if (!o) return;
    o << "# lsdjref-compare 1\n# " << what << "\ncycle,addr,name,value\n";
    for (const auto& x : w) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%llu,%04X,%s,%02X\n",
                      (unsigned long long) x.cycle, x.addr, regName(x.addr), x.value);
        o << buf;
    }
}

void usage()
{
    std::fprintf(stderr,
        "lsdjref-compare --spec cases.spec --traces DIR [--out REPORT.md]\n"
        "                [--dump DIR] [--model dmg|cgb] [--tolerance CYCLES]\n");
}

} // namespace

int main(int argc, char** argv)
{
    std::string spec, traces, out, dump, model = "dmg";
    int startFrame = -1;                 // when the harness pressed START
    // Two pitch-clock periods. One is where a note falls inside the
    // free-running timer -- which is where the player pressed play, not a
    // property of either driver -- and the second covers an update one side
    // fitted in and the other did not, which shifts everything after it by a
    // period (see compareStreams).
    uint64_t tolerance = 2 * 11712;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "lsdjref-compare: %s needs a value\n", what); std::exit(2); }
            return argv[++i];
        };
        if      (a == "--spec")      spec = next("--spec");
        else if (a == "--traces")    traces = next("--traces");
        else if (a == "--out")       out = next("--out");
        else if (a == "--dump")      dump = next("--dump");
        else if (a == "--model")     model = next("--model");
        else if (a == "--tolerance") tolerance = std::strtoull(next("--tolerance").c_str(), nullptr, 10);
        else if (a == "--start-frame") startFrame = std::atoi(next("--start-frame").c_str());
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { std::fprintf(stderr, "lsdjref-compare: unknown option %s\n", a.c_str()); usage(); return 2; }
    }
#ifdef CHIPBOY_LSDJREF_SPEC
    if (spec.empty()) spec = CHIPBOY_LSDJREF_SPEC;
#endif
    if (spec.empty()) { usage(); return 2; }

    std::vector<lsdjref::SpecCase> cases;
    std::string error;
    if (!lsdjref::readSpec(spec, cases, error)) { std::fprintf(stderr, "lsdjref-compare: %s\n", error.c_str()); return 2; }

    const Console console = model == "cgb" ? Console::CGB : Console::DMG;
    if (startFrame < 0) startFrame = model == "cgb" ? 300 : 180;
    std::ostringstream md;
    md << "# LSDj vs ChipBoy: the register streams\n\n"
       << "Written by `lsdjref-compare` from `" << spec << "`, model **" << model
       << "**, cycle tolerance " << tolerance << ".\n\n"
       << "Each case is aligned at the first note-on in each stream, then the two are\n"
       << "compared write for write on each channel. The verdict is what the comparison\n"
       << "found, not a pass or a failure: the differences are the findings.\n\n";

    int compared = 0, missing = 0;
    std::ostringstream table, detail;
    table << "| case | channel | LSDj writes | ChipBoy writes | verdict | worst skew |\n|---|---|---:|---:|---|---:|\n";

    for (const auto& c : cases) {
        const std::string tracePath = traces + "/" + c.name + "." + model + ".csv";
        std::vector<Write> lsdj;
        if (traces.empty() || !readTrace(tracePath, lsdj)) {
            ++missing;
            table << "| `" << c.name << "` | - | - | - | no trace (" << tracePath << ") | - |\n";
            continue;
        }
        ++compared;
        // Play as far as the trace does *after* the song starts: the capture's
        // frames include LSDj's boot and the wait for START.
        const double seconds = std::max(1.0, double(c.frames - startFrame) / 60.0);
        const auto chip = playChipBoy(c, console, seconds);
        if (!dump.empty()) {
            writeCsv(dump + "/" + c.name + ".chipboy.csv", chip, "ChipBoy's driver");
            writeCsv(dump + "/" + c.name + ".lsdj.csv", lsdj, "LSDj, as traced");
        }

        detail << "\n## " << c.name << "\n\n" << c.desc << "\n\n";
        for (int ch = 0; ch < 5; ++ch) {
            static const char* kNames[5] = { "PU1", "PU2", "WAV", "NOI", "global" };
            const Verdict v = compareStreams(lsdj, chip, ch, tolerance);
            if (v.lsdjWrites == 0 && v.chipboyWrites == 0) continue;
            table << "| `" << c.name << "` | " << kNames[ch] << " | " << v.lsdjWrites
                  << " | " << v.chipboyWrites << " | " << v.text
                  << " | " << v.worstSkew << " |\n";
            detail << "- **" << kNames[ch] << "**: " << v.text
                   << " (" << v.lsdjWrites << " vs " << v.chipboyWrites << " writes";
            if (v.phaseWrites) detail << ", " << v.phaseWrites << " put down to the clock's phase";
            if (v.tailWrites) detail << ", " << v.tailWrites << " past where the shorter capture stopped";
            detail << ")";
            if (!v.detail.empty()) detail << " -- first divergence: " << v.detail;
            detail << "\n";
        }

        // The first two dozen writes side by side: what a reader needs to see
        // the shape of the difference without opening the CSVs.
        const size_t la = firstNoteOn(lsdj), ca = firstNoteOn(chip);
        if (la < lsdj.size() && ca < chip.size()) {
            detail << "\n| # | LSDj | at | ChipBoy | at |\n|---:|---|---:|---|---:|\n";
            const uint64_t lo = lsdj[la].cycle, co = chip[ca].cycle;
            for (size_t i = 0; i < 24; ++i) {
                const bool hasL = la + i < lsdj.size(), hasC = ca + i < chip.size();
                if (!hasL && !hasC) break;
                detail << "| " << i << " | ";
                if (hasL) detail << regName(lsdj[la + i].addr) << "=" << hex2(lsdj[la + i].value)
                                 << " | " << (lsdj[la + i].cycle - lo) << " | ";
                else detail << "- | - | ";
                if (hasC) detail << regName(chip[ca + i].addr) << "=" << hex2(chip[ca + i].value)
                                 << " | " << (chip[ca + i].cycle - co) << " |\n";
                else detail << "- | - |\n";
            }
        }
    }

    md << table.str() << detail.str() << "\n";
    if (out.empty()) std::fputs(md.str().c_str(), stdout);
    else {
        std::ofstream o(out);
        if (!o) { std::fprintf(stderr, "lsdjref-compare: cannot write %s\n", out.c_str()); return 2; }
        o << md.str();
        std::fprintf(stderr, "lsdjref-compare: %d cases compared, %d without a trace -> %s\n",
                     compared, missing, out.c_str());
    }
    if (compared == 0) {
        std::fprintf(stderr, "lsdjref-compare: no traces found under '%s'; skipping\n", traces.c_str());
        return kSkipExit;
    }
    return 0;
}
