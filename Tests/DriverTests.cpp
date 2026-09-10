// ChipBoy -- driver tests (spec section 8, 9, 10).
#include "core/Bank/Bank.h"
#include "core/Driver/Clock.h"
#include "core/Driver/Driver.h"
#include "core/Apu/Apu.h"
#include "core/Render/Renderer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace chipboy;
using namespace chipboy::driver;
using namespace chipboy::bank;

namespace {

struct Rig {
    // A bank is 41 KB and a song 300 KB now that a phrase holds sixty-four
    // cells: both on the heap, so a Windows main thread's megabyte of stack
    // is never the limit (CLAUDE.md). `new T(prvalue)` builds in place.
    std::unique_ptr<Bank> bankOwned { new Bank(Bank::factory()) };
    std::unique_ptr<tracker::Song> songOwned { new tracker::Song() };
    Bank& bank = *bankOwned;
    tracker::Song& song = *songOwned;
    Driver drv;
    Clock clock;
    render::Renderer ren;
    std::vector<RegWrite> writes;
    std::vector<NoteEvent> events;   ///< the last block's events, as the driver stamped them
    uint64_t frame = 0;
    uint64_t tick = 0;
    double rate = 48000.0;
    double tickHz = 240.0;      ///< the free-running tick these tests count in
    Rig(Console model = Console::DMG)
    {
        ren.prepare(rate, AnalogModel::forConsole(model), 4096);
        drv.prepare(rate, &bank, &song, model);
        clock.prepare(rate);
        writes.reserve(4096);
    }
    /// Tick boundaries for a block, at `tickHz`, from the absolute frame.
    std::vector<TickPoint> ticksFor(uint32_t n)
    {
        std::vector<TickPoint> out;
        const double framesPerTick = rate / tickHz;
        double k = std::ceil(double(frame) / framesPerTick - 1e-9);
        for (;; k += 1.0) {
            const uint64_t f = uint64_t(std::llround(k * framesPerTick));
            if (f >= frame + n) break;
            if (f >= frame) out.push_back({ uint32_t(f - frame), int64_t(tick++) });
        }
        return out;
    }
    /// Run one block of `n` frames with these events; returns the writes.
    std::vector<RegWrite> block(std::vector<NoteEvent> ev, uint32_t n)
    {
        const auto ticks = ticksFor(n);
        return block(std::move(ev), n, ticks);
    }
    std::vector<RegWrite> block(std::vector<NoteEvent> ev, uint32_t n, const std::vector<TickPoint>& ticks)
    {
        writes.clear();
        events = std::move(ev);
        drv.process(events.data(), events.size(), n, frame, ticks.data(), ticks.size(), [this](uint64_t f) { return ren.cycleForFrame(f); }, writes);
        frame += n;
        return writes;
    }
    /// What the driver stamped on the last note-on of this block on a channel:
    /// whether it was plain and the instrument it loaded (section 9.4).
    const NoteEvent* report(int ch) const
    {
        const NoteEvent* r = nullptr;
        for (const auto& e : events)
            if ((e.channel & 3) == ch && e.kind == NoteEvent::NoteOn && e.b) r = &e;
        return r;
    }
    static NoteEvent on(int ch, uint8_t note, uint8_t vel = 100, uint32_t off = 0) { NoteEvent e; e.channel = uint8_t(ch); e.kind = NoteEvent::NoteOn; e.a = note; e.b = vel; e.offset = off; return e; }
    static NoteEvent off(int ch, uint8_t note, uint32_t o = 0) { NoteEvent e; e.channel = uint8_t(ch); e.kind = NoteEvent::NoteOff; e.a = note; e.offset = o; return e; }
};

/// A tracker cell as the Player sends it. `inst` 0 is a blank instrument
/// column, which is what makes a cell's note bare.
NoteEvent cellOn(int ch, uint8_t note, uint8_t inst, uint8_t vel = 100, uint32_t off = 0)
{
    NoteEvent e; e.channel = uint8_t(ch); e.kind = NoteEvent::NoteOn; e.source = NoteEvent::Tracker; e.velSet = true;
    e.a = note; e.b = vel; e.inst = inst; e.offset = off; return e;
}
/// A cell with no note: its columns are the slots from that step on.
NoteEvent cellCmd(int ch, const Command& c1, const Command& c2 = {})
{
    NoteEvent e; e.channel = uint8_t(ch); e.kind = NoteEvent::Command; e.source = NoteEvent::Tracker;
    e.cmd1 = c1; e.cmd2 = c2; return e;
}
/// A Hybrid channel's cell, as the Player sends it (section 20): never a
/// note, and marked so the driver holds its commands for the tick.
NoteEvent cellHybrid(int ch, uint8_t inst, const Command& c1 = {}, const Command& c2 = {}, uint8_t table = 0, uint32_t off = 0)
{
    NoteEvent e; e.channel = uint8_t(ch); e.kind = NoteEvent::Command; e.source = NoteEvent::Tracker;
    e.hybrid = true; e.inst = inst; e.table = table; e.cmd1 = c1; e.cmd2 = c2; e.offset = off; return e;
}
NoteEvent allOff(int ch)
{
    NoteEvent e; e.channel = uint8_t(ch); e.kind = NoteEvent::AllNotesOff; return e;
}

bool has(const std::vector<RegWrite>& w, uint16_t addr, int value = -1)
{
    for (const auto& x : w) if (x.addr == addr && (value < 0 || x.value == value)) return true;
    return false;
}
const RegWrite* last(const std::vector<RegWrite>& w, uint16_t addr)
{
    const RegWrite* r = nullptr;
    for (const auto& x : w) if (x.addr == addr) r = &x;
    return r;
}

/// The period the driver has arrived at, sampled once per block.
std::vector<int> periodSeries(Rig& r, int ch, int blocks, uint32_t n)
{
    std::vector<int> out;
    for (int i = 0; i < blocks; ++i) { r.block({}, n); out.push_back(int(r.drv.view(ch).period)); }
    return out;
}
/// How often the series falls through the middle of its range: one per cycle
/// of a vibrato.
int dips(const std::vector<int>& v)
{
    if (v.empty()) return 0;
    const int lo = *std::min_element(v.begin(), v.end()), hi = *std::max_element(v.begin(), v.end());
    const int mid = (lo + hi) / 2;
    int n = 0; bool below = false;
    for (int x : v) { if (!below && x < mid) { ++n; below = true; } else if (below && x > mid) below = false; }
    return n;
}
int minOf(const std::vector<int>& v) { return *std::min_element(v.begin(), v.end()); }
int maxOf(const std::vector<int>& v) { return *std::max_element(v.begin(), v.end()); }
bool anyTrigger(const std::vector<RegWrite>& w, uint16_t addr)
{
    for (const auto& x : w) if (x.addr == addr && (x.value & 0x80)) return true;
    return false;
}
int note(int n) { return Driver::periodForNote(n, false); }

/// The periods a block's writes carry, in order: each NRx3 with the NRx4 that
/// follows it. This is what the parity harness compares, so it is what these
/// tests read rather than sampling the view at block boundaries.
std::vector<int> periodWrites(const std::vector<RegWrite>& w, int ch = 0)
{
    const uint16_t lo3 = uint16_t(0xFF13 + ch * 5), hi4 = uint16_t(0xFF14 + ch * 5);
    std::vector<int> out; int lo = -1;
    for (const auto& x : w) {
        if (x.addr == lo3) lo = x.value;
        else if (x.addr == hi4 && lo >= 0) out.push_back(((x.value & 7) << 8) | lo);
    }
    return out;
}
/// Turning points in a series: two to a vibrato cycle.
int turns(const std::vector<int>& v)
{
    int n = 0, dir = 0;
    for (size_t i = 1; i < v.size(); ++i) {
        const int d = v[i] - v[i - 1];
        if (d == 0) continue;
        const int s = d > 0 ? 1 : -1;
        if (dir != 0 && s != dir) ++n;
        dir = s;
    }
    return n;
}

} // namespace

TEST_CASE("pitch: note to period and the chip's floor", "[driver]")
{
    CHECK(Driver::periodForNote(69, false) == 1750);      // A4 on pulse: 131072/(2048-1750) = 439.8 Hz
    CHECK(Driver::periodForNote(57, true) == 1750);       // A3 on wave sounds A3 with the same register
    CHECK(Driver::periodForNote(36, false) == 44);        // C2, the pulse floor
    CHECK(Driver::periodForNote(35, false) == -1);        // B1: unplayable
    CHECK(Driver::periodForNote(24, true) == 44);         // C1 on wave
    uint8_t s, d; Driver::noisePairForNote(69, s, d);
    const double clock = Driver::noiseClockHz(s, d);
    CHECK(std::fabs(std::log2(clock / (127.0 * 440.0))) < 0.35);   // within a third of an octave of in-tune
}

TEST_CASE("a note on writes the registers and a note off kills the DAC", "[driver]")
{
    Rig r;
    ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
    auto w = r.block({ Rig::on(0, 69, 127) }, 512);
    REQUIRE_FALSE(w.empty());
    CHECK(has(w, 0xFF10));                     // sweep register written on PU1
    CHECK(has(w, 0xFF11, 0x80));               // duty 50%, no length
    CHECK(has(w, 0xFF12, 0xF8));               // velocity 127 -> volume 15; the low nibble is always 8
    CHECK(has(w, 0xFF13, 1750 & 0xFF));
    CHECK(has(w, 0xFF14, uint8_t(0x80 | (1750 >> 8))));             // trigger
    CHECK(anyTrigger(w, 0xFF14));
    CHECK(has(w, 0xFF25));                     // NR51 pan
    CHECK(has(w, 0xFF24, 0x77));               // NR50 from the defaults
    // cycle-ordered, and spaced like a CPU writing them
    for (size_t i = 1; i < w.size(); ++i) CHECK(w[i].cycle >= w[i - 1].cycle);
    CHECK(r.drv.view(0).active);
    CHECK(r.drv.view(0).period == 1750);

    w = r.block({ Rig::off(0, 69) }, 512);
    CHECK(has(w, 0xFF12, 0x00));               // kill: DAC off (which holds on this hardware)
    CHECK_FALSE(r.drv.view(0).active);
}

TEST_CASE("velocity quantises to sixteen levels and the pulse floor is silent", "[driver]")
{
    Rig r;
    ChannelParams p; p.instrument = 1; r.drv.setParams(1, p);
    auto w = r.block({ Rig::on(1, 60, 64) }, 256);
    CHECK(last(w, 0xFF17)->value == 0x88);     // 64 -> level 8, hold nibble 8
    r.block({ Rig::off(1, 60) }, 256);
    w = r.block({ Rig::on(1, 30, 100) }, 256); // below C2
    CHECK(r.drv.view(1).outOfRange);
    CHECK_FALSE(has(w, 0xFF19, 0x80 | 0));     // no trigger at a fake period
}

TEST_CASE("a cell's VEL is a start volume in any instance and a blank VEL is the instrument's", "[driver]")
{
    // Section 9.1: a song file must sound the same whatever the channel's
    // Velocity mode -- the demo's drums are picked by velocity under the bank
    // mode and must not turn quiet in an instance set to start volume.
    Rig r;
    r.song.noteSource[1] = tracker::NoteSource::Tracker;
    ChannelParams p; p.instrument = 1; p.velocityMode = 1; r.drv.setParams(1, p);   // instrument bank
    auto w = r.block({ cellOn(1, 60, 1, 64) }, 256);
    CHECK(last(w, 0xFF17)->value == 0x88);     // VEL 64 -> level 8, the mode notwithstanding
    r.block({ Rig::off(1, 60) }, 256);
    p.velocityMode = 0; r.drv.setParams(1, p);  // start volume
    NoteEvent blank = cellOn(1, 60, 1, 100); blank.velSet = false;
    w = r.block({ blank }, 256);
    CHECK((last(w, 0xFF17)->value >> 4) == r.drv.view(1).envVol);   // blank VEL: the Square lead's own volume
    CHECK((last(w, 0xFF17)->value >> 4) != 12);                       // not what velocity 100 would give
}

TEST_CASE("tables step once per tick and stop at the end when told", "[driver]")
{
    Rig r;
    ChannelParams p; p.instrument = 5; r.drv.setParams(0, p);   // Chord arp: table 1 (0 3 7 12 ...)
    r.tickHz = 100.0;
    r.block({ Rig::on(0, 60, 100) }, 480);     // one tick at 100 Hz in 10 ms
    std::vector<int> periods;
    for (int i = 0; i < 6; ++i) {
        auto w = r.block({}, 480);
        if (const auto* lo = last(w, 0xFF13)) { const auto* hi = last(w, 0xFF14); periods.push_back(lo->value | ((hi ? hi->value & 7 : (r.drv.view(0).period >> 8)) << 8)); }
    }
    // The transpose column cycles 0, 3, 7, 12: three distinct periods above the root.
    REQUIRE(periods.size() >= 3);
    CHECK(periods[0] != periods[1]);
    CHECK(r.drv.view(0).tableSlot == 1);
}

TEST_CASE("MIDI notes overlap as the instrument's Overlap says", "[driver][notes]")
{
    // Section 8: a note over a held one is bare when it would load the
    // instrument already sounding and that instrument overlaps legato.
    SECTION("legato: the second note only moves the pitch") {
        Rig r;
        ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);   // a pulse instrument: legato
        r.block({ Rig::on(0, 60, 100) }, 256);
        auto w = r.block({ Rig::on(0, 64, 100) }, 256);
        const auto* hi = last(w, 0xFF14);
        REQUIRE(hi != nullptr);
        CHECK((hi->value & 0x80) == 0);            // no trigger bit
        CHECK_FALSE(has(w, 0xFF12));               // and no envelope rewrite
        CHECK_FALSE(r.report(0)->plain);
        CHECK(r.report(0)->loaded == 1);
    }
    SECTION("retrig: it starts the instrument again") {
        Rig r;
        r.bank.instruments[0].overlap = Overlap::Retrig;
        ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
        r.block({ Rig::on(0, 60, 100) }, 256);
        auto w = r.block({ Rig::on(0, 64, 100) }, 256);
        CHECK(anyTrigger(w, 0xFF14));
        CHECK(has(w, 0xFF12));
        CHECK(r.report(0)->plain);
    }
    SECTION("a different instrument is always plain") {
        Rig r;
        ChannelParams p; p.instrument = 1; p.velocityMode = 1; r.drv.setParams(0, p);   // velocity bank
        r.block({ Rig::on(0, 60, 8) }, 256);        // slot 1 + 1
        auto w = r.block({ Rig::on(0, 64, 16) }, 256);   // slot 1 + 2: another instrument
        CHECK(anyTrigger(w, 0xFF14));
        CHECK(r.report(0)->plain);
        CHECK(r.report(0)->loaded == 3);
    }
    SECTION("a keyswitch since the last note is always plain") {
        Rig r;
        ChannelParams p; p.instrument = 1; p.keyswitch = true; r.drv.setParams(0, p);
        r.block({ Rig::on(0, 60, 100) }, 256);
        r.block({ Rig::on(0, 26, 100) }, 256);      // D1 selects slot 3, and never sounds
        auto w = r.block({ Rig::on(0, 64, 100) }, 256);
        CHECK(anyTrigger(w, 0xFF14));
        CHECK(r.report(0)->plain);
        CHECK(r.report(0)->loaded == 3);
    }
}

TEST_CASE("releasing a note over a held one returns to it bare", "[driver][notes]")
{
    Rig r;
    ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
    r.block({ Rig::on(0, 60, 100) }, 256);
    r.block({ Rig::on(0, 64, 100) }, 256);
    auto w = r.block({ Rig::off(0, 64) }, 256);
    CHECK_FALSE(anyTrigger(w, 0xFF14));            // no attack on the way back
    CHECK_FALSE(has(w, 0xFF12));
    CHECK(r.drv.view(0).period == note(60));
}

TEST_CASE("wave instruments load wave RAM through the DMG dance", "[driver]")
{
    Rig r;
    ChannelParams p; p.instrument = 7; r.drv.setParams(2, p);
    auto w = r.block({ Rig::on(2, 48, 100) }, 512);
    CHECK(has(w, 0xFF1A, 0x00));               // DAC off first
    int ramWrites = 0; for (auto& x : w) if (x.addr >= 0xFF30 && x.addr <= 0xFF3F) ++ramWrites;
    CHECK(ramWrites == 16);
    CHECK(has(w, 0xFF1A, 0x80));               // DAC on
    CHECK(has(w, 0xFF1C, 0x20));               // 100%
    CHECK(anyTrigger(w, 0xFF1E));              // trigger
    // The RAM writes precede the DAC-on, and the trigger comes last.
    uint64_t lastRam = 0, dacOn = 0, trig = 0;
    for (auto& x : w) { if (x.addr >= 0xFF30 && x.addr <= 0xFF3F) lastRam = std::max(lastRam, x.cycle); if (x.addr == 0xFF1A && x.value == 0x80) dacOn = x.cycle; if (x.addr == 0xFF1E && (x.value & 0x80)) trig = x.cycle; }
    CHECK(lastRam < dacOn);
    CHECK(dacOn < trig);
}

TEST_CASE("kits stream wave RAM every 32 samples on DMG", "[driver]")
{
    Rig r;
    ChannelParams p; p.instrument = 16; r.drv.setParams(2, p);   // Kit 1
    auto w = r.block({ Rig::on(2, 36, 100) }, 4800);              // 100 ms
    int refills = 0;
    for (auto& x : w) if (x.addr == 0xFF1E && (x.value & 0x80)) ++refills;
    // 11 468 Hz / 32 = 358 loops per second: about 36 in 100 ms, plus the first trigger.
    CHECK(refills >= 30);
    CHECK(refills <= 40);
    // the kick is 0.3 s long: still playing
    CHECK(r.drv.view(2).active);
    std::vector<RegWrite> rest;
    for (int i = 0; i < 3; ++i) { w = r.block({}, 4800); rest.insert(rest.end(), w.begin(), w.end()); }
    CHECK_FALSE(r.drv.view(2).active);         // ended: one-shot
    CHECK(has(rest, 0xFF1A, 0x00));
}

TEST_CASE("kits stream without a re-trigger on CGB", "[driver]")
{
    Rig r(Console::CGB);
    ChannelParams p; p.instrument = 16; r.drv.setParams(2, p);
    auto w = r.block({ Rig::on(2, 36, 100) }, 4800);
    int triggers = 0, ram = 0;
    for (auto& x : w) { if (x.addr == 0xFF1E && (x.value & 0x80)) ++triggers; if (x.addr >= 0xFF30 && x.addr <= 0xFF3F) ++ram; }
    CHECK(triggers == 1);                      // only the note-on
    CHECK(ram > 16 * 30);                      // trailing writes, one loop ahead
}

TEST_CASE("the tick grid is independent of block size", "[driver]")
{
    auto run = [](uint32_t block) {
        Rig r;
        ChannelParams p; p.instrument = 5; r.drv.setParams(0, p);   // arpeggio table: many writes
        ChannelParams q; q.instrument = 11; r.drv.setParams(3, q);
        std::vector<RegWrite> all;
        const uint32_t total = 48000;
        for (uint32_t f = 0; f < total; f += block) {
            const uint32_t n = std::min(block, total - f);
            std::vector<NoteEvent> ev;
            if (f <= 1000 && f + n > 1000) ev.push_back(Rig::on(0, 62, 90, 1000 - f));
            if (f <= 1000 && f + n > 1000) ev.push_back(Rig::on(3, 36, 120, 1000 - f));
            if (f <= 30000 && f + n > 30000) ev.push_back(Rig::off(0, 62, 30000 - f));
            Transport t; t.valid = true; t.playing = true; t.bpm = 120.0; t.ppq = double(f) / 48000.0 * 2.0;
            r.clock.process(t, n, f);
            std::vector<TickPoint> ticks(r.clock.ticks(), r.clock.ticks() + r.clock.tickCount());
            auto w = r.block(ev, n, ticks);
            all.insert(all.end(), w.begin(), w.end());
        }
        return all;
    };
    const auto a = run(512), b = run(64), c = run(1000);
    REQUIRE(a.size() > 50);
    REQUIRE(a.size() == b.size());
    REQUIRE(a.size() == c.size());
    for (size_t i = 0; i < a.size(); ++i) {
        INFO("write " << i);
        CHECK(a[i].cycle == b[i].cycle); CHECK(a[i].addr == b[i].addr); CHECK(a[i].value == b[i].value);
        CHECK(a[i].cycle == c[i].cycle); CHECK(a[i].addr == c[i].addr); CHECK(a[i].value == c[i].value);
    }
}

TEST_CASE("E, W, P, S and A write what the letter says", "[driver][commands]")
{
    // The command table of docs/COMMANDS_AND_TEMPO.md section 2, as registers.
    SECTION("E is the envelope: volume in x, speed and direction in y") {
        Rig r;
        // The register always holds -- amplitude, direction up, period zero --
        // and the rate and direction the letter names are the *driver's*
        // envelope, which it steps itself (docs/LSDJ_PARITY.md sections 2, 7).
        ChannelParams p; p.instrument = 1; p.cmd[0] = { Cmd::E, 10, 3, 0 }; r.drv.setParams(0, p);
        auto w = r.block({ Rig::on(0, 69, 127) }, 512);
        CHECK(has(w, 0xFF12, 0xA8));                    // vol 10, hold
        CHECK(r.drv.view(0).envRate == 3);
        CHECK(r.drv.view(0).envDir == 0);               // down
        p.cmd[0] = { Cmd::E, 10, 11, 0 };               // y >= 8: rising
        r.drv.setParams(0, p);
        r.block({}, 512);
        CHECK(r.drv.view(0).envRate == 3);
        CHECK(r.drv.view(0).envDir == 1);
    }
    SECTION("E on the wave channel is its two-bit level") {
        Rig r;
        ChannelParams p; p.instrument = 7; p.cmd[0] = { Cmd::E, 1, 0, 0 }; r.drv.setParams(2, p);
        auto w = r.block({ Rig::on(2, 48, 100) }, 512);
        CHECK(last(w, 0xFF1C)->value == 0x60);          // NR32 code for 25 %
    }
    SECTION("W is duty on a pulse and a wave slot on WAV") {
        Rig r;
        ChannelParams p; p.instrument = 1; p.cmd[0] = { Cmd::W, 1, 0, 0 }; r.drv.setParams(0, p);
        auto w = r.block({ Rig::on(0, 69, 100) }, 512);
        CHECK(last(w, 0xFF11)->value == 0x40);          // duty 25 %
        Rig r2;
        ChannelParams q; q.instrument = 7; q.cmd[0] = { Cmd::W, 2, 0, 0 }; r2.drv.setParams(2, q);   // wave slot 2: saw
        w = r2.block({ Rig::on(2, 48, 100) }, 512);
        std::vector<uint8_t> ram;
        for (const auto& x : w) if (x.addr >= 0xFF30 && x.addr <= 0xFF3F) ram.push_back(x.value);
        REQUIRE(ram.size() == 16);
        CHECK(ram[0] == 0x00);                          // the saw starts at zero and climbs
        CHECK(ram[15] == 0xFF);
    }
    SECTION("P with the pitch speed at Step is an immediate offset") {
        Rig r;
        r.bank.instruments[0].pitchSpeed = PitchSpeed::Step;
        // The argument is two's complement (section 34) and the offset is
        // x/32 of a semitone, applied at the first pitch update after the
        // note-on, not in the note's own writes (docs/LSDJ_PARITY.md 5).
        r.bank.instruments[0].vib.depth = 0;
        ChannelParams p; p.instrument = 1; p.cmd[0] = { Cmd::P, 32, 0, 0 }; r.drv.setParams(0, p);
        r.block({ Rig::on(0, 69, 100) }, 2048);
        CHECK(r.drv.view(0).period == Driver::periodForNote(70, false));   // a whole semitone up
        p.cmd[0] = { Cmd::P, 256 - 32, 0, 0 };          // -32: back down again
        r.drv.setParams(0, p);
        r.block({}, 2048);
        CHECK(r.drv.view(0).period == Driver::periodForNote(69, false));
    }
    SECTION("S is PU1's sweep, down when x asks for it") {
        Rig r;
        // `y` is NR10's low nibble: 0-7 up at that shift, 8-15 down (34).
        ChannelParams p; p.instrument = 1; p.cmd[0] = { Cmd::S, 3, 2, 0 }; r.drv.setParams(0, p);
        auto w = r.block({ Rig::on(0, 69, 100) }, 512);
        CHECK(last(w, 0xFF10)->value == 0x32);          // rate 3, up, shift 2
        p.cmd[0] = { Cmd::S, 3, 10, 0 };
        r.drv.setParams(0, p);
        w = r.block({}, 512);
        CHECK(last(w, 0xFF10)->value == 0x3A);          // the same, downward
    }
    SECTION("A selects a table and 0 stops it") {
        Rig r;
        ChannelParams p; p.instrument = 1; p.cmd[0] = { Cmd::A, 6, 0, 0 }; r.drv.setParams(0, p);
        r.block({ Rig::on(0, 60, 100) }, 512);
        CHECK(r.drv.view(0).tableSlot == 6);
        p.cmd[0] = { Cmd::A, 0, 0, 0 };
        r.drv.setParams(0, p);
        r.block({}, 512);
        CHECK(r.drv.view(0).tableSlot == 0);
    }
}

TEST_CASE("a slot fires when it changes, again at each note-on, and reverts", "[driver][commands]")
{
    Rig r;
    ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);      // Square lead: vol 13, rate 0
    auto w = r.block({ Rig::on(0, 69, 127) }, 512);
    REQUIRE(last(w, 0xFF12) != nullptr);
    CHECK(last(w, 0xFF12)->value == 0xF8);                         // velocity 127, hold nibble 8

    // Changing a slot fires it at the next tick, with no note involved. A
    // level change is a zombie sequence, so what it says is where the chip's
    // volume ends up, not what the byte reads (docs/LSDJ_PARITY.md 6).
    p.cmd[1] = { Cmd::E, 4, 0, 0 };
    r.drv.setParams(0, p);
    w = r.block({}, 512);
    REQUIRE(last(w, 0xFF12) != nullptr);
    CHECK(r.drv.view(0).volume == 4);
    CHECK(r.drv.view(0).envVol == 4);
    // Unchanged, it does not fire again.
    w = r.block({}, 512);
    CHECK_FALSE(has(w, 0xFF12));

    // It applies to the next note too, after the instrument has loaded.
    r.block({ Rig::off(0, 69) }, 512);
    w = r.block({ Rig::on(0, 69, 127) }, 512);
    CHECK(last(w, 0xFF12)->value == 0x48);                         // not the velocity's 0xF8

    // None reverts to the instrument's envelope.
    p.cmd[1] = {};
    r.drv.setParams(0, p);
    w = r.block({}, 512);
    REQUIRE(last(w, 0xFF12) != nullptr);
    // The instrument's vol 13 again. It keeps the envelope's direction and
    // rate, so it is a level change: zombie-mode writes, no trigger, and the
    // direction bit of the last one is whichever way was shorter (26).
    CHECK_FALSE(anyTrigger(w, 0xFF14));
    CHECK(r.drv.view(0).volume == 13);                             // where the chip really is
    CHECK(r.drv.slot(0, 1).cmd == Cmd::None);
}

TEST_CASE("a cell's command is applied once and never occupies a slot", "[driver][commands]")
{
    // Section 12: a cell's two commands are applied at their step. A
    // persistent letter changes the running state, which holds until the next
    // plain note reloads the instrument -- LSDj's rule -- and nothing is left
    // in force to fire again at the notes after it.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[0] = tracker::NoteSource::Tracker;
    ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);      // Square lead: vol 13, rate 0
    NoteEvent e = cellOn(0, 69, 1, 127);
    e.cmd1 = { Cmd::E, 5, 0, 0 };
    auto w = r.block({ e }, 480);
    CHECK(last(w, 0xFF12)->value == 0x58);                         // E's volume at this note
    CHECK(r.drv.slot(0, 0).cmd == Cmd::None);                      // the slot is the lane's alone
    w = r.block({ cellOn(0, 67, 1, 127) }, 480);
    CHECK(last(w, 0xFF12)->value == 0xF8);                         // the next plain note is the instrument's
    w = r.block({ cellOn(0, 64, 1, 64) }, 480);
    CHECK(last(w, 0xFF12)->value == 0x88);                         // and velocity 64 gets through

    // The revert form still says "put the letter back", once.
    e = cellOn(0, 69, 1, 127); e.cmd1 = { Cmd::E, 5, 0, 0 };
    r.block({ e }, 480);
    w = r.block({ cellCmd(0, bank::revertOf(Cmd::E)) }, 480);
    REQUIRE(last(w, 0xFF12) != nullptr);
    CHECK(r.drv.view(0).volume == 13);                             // the instrument's own vol 13
    CHECK_FALSE(anyTrigger(w, 0xFF14));
}

TEST_CASE("a V on a bare note holds until the next plain note", "[driver][commands][pitch]")
{
    // Section 12's own example: a V written on a bare note stays through the
    // bare notes that follow and ends at the next note that carries an
    // instrument -- and looping back to that note plays it clean, because the
    // cell never occupied a slot.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[0] = tracker::NoteSource::Tracker;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);   // the lead: vib 10, depth 2
    r.block({ cellOn(0, 69, 1) }, 480);
    CHECK(r.drv.view(0).vibSpeed == 10);
    NoteEvent bare = cellOn(0, 71, 0);
    bare.cmd1 = { Cmd::V, 15, 12, 0 };
    r.block({ bare }, 480);
    CHECK(r.drv.view(0).vibSpeed == 15);
    CHECK(r.drv.view(0).vibDepth == 12);
    r.block({ cellOn(0, 72, 0) }, 480);                            // another bare note
    CHECK(r.drv.view(0).vibSpeed == 15);                           // it holds
    r.block({ cellOn(0, 69, 1) }, 480);                            // a plain note: the instrument again
    CHECK(r.drv.view(0).vibSpeed == 10);
    CHECK(r.drv.view(0).vibDepth == 2);

    // The loop wrap the Player sends, and then bar 1's plain note.
    r.block({ cellOn(0, 72, 0) }, 480);
    NoteEvent v2 = cellOn(0, 74, 0); v2.cmd1 = { Cmd::V, 15, 12, 0 };
    r.block({ v2 }, 480);
    CHECK(r.drv.view(0).vibSpeed == 15);
    r.block({ allOff(0) }, 480);
    r.block({ cellOn(0, 69, 1) }, 480);
    CHECK(r.drv.view(0).vibSpeed == 10);                           // clean, as if the V had never played
}

TEST_CASE("a K cell kills its own note and no other", "[driver][commands]")
{
    // K is a per-note letter (section 12): it shapes the cell it is on.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[0] = tracker::NoteSource::Tracker;
    ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
    NoteEvent e = cellOn(0, 69, 1);
    e.cmd1 = { Cmd::K, 2, 0, 0 };                                  // two ticks and it is gone
    r.block({ e }, 480);                                           // 480 frames is one tick at 100 Hz
    CHECK(r.drv.view(0).active);
    for (int i = 0; i < 3; ++i) r.block({}, 480);
    CHECK_FALSE(r.drv.view(0).active);
    r.block({ cellOn(0, 67, 1) }, 480);
    CHECK(r.drv.view(0).active);
    r.block({}, 2400);
    CHECK(r.drv.view(0).active);                                   // the K did not follow the note
}

TEST_CASE("the command octave fires the slots without sounding", "[driver][commands]")
{
    // Section 13: MIDI notes 0-11 never sound and never join the held stack.
    // They fire CMD1 then CMD2 on whatever the channel is playing, so a held
    // note can be shaped after its attack.
    Rig r;
    r.tickHz = 100.0;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2;
    p.cmd[0] = { Cmd::V, 15, 12, 0 };
    r.drv.setParams(0, p);
    r.block({}, 480);
    r.block({ Rig::on(0, 69, 100) }, 480);
    CHECK(r.drv.view(0).vibSpeed == 15);                           // the slot fires at the note-on
    // A cell's revert puts the instrument's vibrato back, once; the slot is
    // untouched, because a cell never writes one (section 12).
    r.block({ cellCmd(0, bank::revertOf(Cmd::V)) }, 480);
    CHECK(r.drv.view(0).vibSpeed == 10);
    CHECK(r.drv.slot(0, 0).cmd == Cmd::V);
    // Note 0: the slots fire again, on the note already sounding.
    auto w = r.block({ Rig::on(0, 0, 100) }, 480);
    CHECK(r.drv.view(0).vibSpeed == 15);
    CHECK(r.drv.view(0).vibDepth == 12);
    CHECK(r.drv.view(0).note == 69);                               // the same note, still held
    CHECK(r.drv.view(0).active);
    bool trigger = false;
    for (const auto& x : w) if (x.addr == 0xFF14 && (x.value & 0x80)) trigger = true;
    CHECK_FALSE(trigger);                                          // and it was not retriggered
    r.block({ Rig::off(0, 0) }, 480);
    CHECK(r.drv.view(0).active);                                   // its note-off means nothing
    // With nothing sounding the persistent letters still land in the state.
    r.block({ Rig::off(0, 69) }, 480);
    r.block({ cellCmd(0, bank::revertOf(Cmd::V)) }, 480);
    CHECK(r.drv.view(0).vibSpeed == 10);
    r.block({ Rig::on(0, 11, 100) }, 480);
    CHECK(r.drv.view(0).vibSpeed == 15);
    CHECK_FALSE(r.drv.view(0).active);
}

TEST_CASE("a V revert in a slot brings back the instrument's vibrato, delay and all", "[driver][commands][pitch]")
{
    // A V command has no delay argument, so the value the old recorder wrote
    // into a slot is lossy: in force, it starts the lead's vibrato at once at
    // every note-on. The revert form empties the slot instead, so the
    // instrument's own ten-tick delay is what the notes that follow get
    // (section 9.4). Slots are where this still matters: a cell's V is a
    // one-shot now (section 12).
    auto rig = [](bool useRevert) {
        auto r = std::make_unique<Rig>();
        r->tickHz = 100.0;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2;
        p.cmd[0] = { Cmd::V, 15, 12, 0 };                          // fast and deep, no delay
        r->drv.setParams(0, p);
        r->block({ Rig::on(0, 69, 100) }, 480);
        CHECK(r->drv.view(0).vibSpeed == 15);
        CHECK(r->drv.view(0).vibDepth == 12);
        // Back to the instrument's: the slot going to none, or the value the
        // old recorder wrote -- the speed and depth, its delay lost.
        if (useRevert) p.cmd[0] = {}; else p.cmd[0] = { Cmd::V, 10, 2, 0 };
        r->drv.setParams(0, p);
        r->block({ Rig::off(0, 69) }, 480);
        CHECK(r->drv.view(0).vibSpeed == 10);                      // the lead's speed
        CHECK(r->drv.view(0).vibDepth == 2);                       // and its depth, either way
        r->block({ Rig::on(0, 69, 100) }, 480);                    // a fresh note, five ticks in
        r->block({}, 2400);
        return int(r->drv.view(0).period);
    };
    CHECK(rig(true) == note(69));                                  // still waiting out the delay
    CHECK(rig(false) != note(69));                                 // a concrete V starts at once
}

TEST_CASE("a W revert gives the instrument a keyswitch brought in its own wave", "[driver][commands]")
{
    // The demo's case: WAV's W is in force, a keyswitch hands the channel
    // another instrument, and the letter goes back to none. Nothing is left in
    // force, so the note that follows plays the new instrument's own wave; the
    // concrete W the old recorder wrote would override it for good (9.4).
    // The first sixteen wave-RAM bytes of a block: the frame the note loaded,
    // before the instrument's own frame advance writes any more.
    auto ram = [](const std::vector<RegWrite>& w) {
        std::vector<uint8_t> out;
        for (const auto& x : w) if (x.addr >= 0xFF30 && x.addr <= 0xFF3F && out.size() < 16) out.push_back(x.value);
        return out;
    };
    // A run with no W at all: what instrument 9 (Organ frames, wave 5) sounds like.
    auto plain = [&] {
        Rig r;
        r.song.noteSource[2] = tracker::NoteSource::Tracker;
        ChannelParams p; p.instrument = 9; r.drv.setParams(2, p);
        return ram(r.block({ cellOn(2, 48, 9) }, 512));
    };
    // The same through a slot, which is where a value does stay in force.
    auto afterW = [&](Command back, bool inSlot) {
        Rig r;
        r.song.noteSource[2] = tracker::NoteSource::Tracker;
        ChannelParams p; p.instrument = 7; p.keyswitch = true;
        if (inSlot) p.cmd[0] = { Cmd::W, 2, 0, 0 };                // wave 2, the saw, in force
        r.drv.setParams(2, p);                                     // Triangle bass
        NoteEvent e = cellOn(2, 48, 7);
        if (!inSlot) e.cmd1 = { Cmd::W, 2, 0, 0 };
        r.block({ e }, 512);
        r.block({ Rig::on(2, 20, 100) }, 512);                     // keyswitch: 12 + 9 - 1 selects slot 9
        if (inSlot) { p.cmd[0] = back.cmd == Cmd::None || bank::isRevert(back) ? Command{} : back; r.drv.setParams(2, p); r.block({}, 512); }
        else r.block({ cellCmd(2, back) }, 512);
        return ram(r.block({ cellOn(2, 48, 9) }, 512));
    };
    const auto organ = plain();
    REQUIRE(organ.size() == 16);
    CHECK(afterW(bank::revertOf(Cmd::W), true) == organ);          // the slot went to none: the Organ's own wave
    CHECK(afterW(Command{ Cmd::W, 1, 0, 0 }, true) != organ);      // a value stays in force: the triangle
    // A cell's W is a one-shot either way (section 12), so the plain note
    // that follows plays the instrument the keyswitch brought in.
    CHECK(afterW(bank::revertOf(Cmd::W), false) == organ);
    CHECK(afterW(Command{ Cmd::W, 1, 0, 0 }, false) == organ);
}

TEST_CASE("two note-ons in one block are each reported as what they were", "[driver][notes]")
{
    // The report used to be the block's last note-on per channel, so the
    // first note of a block took the second one's instrument. It is stamped on
    // each event now (section 9.4): a keyswitch between two notes of one block
    // gives the two notes two different instrument columns.
    Rig r;
    ChannelParams p; p.instrument = 1; p.keyswitch = true; r.drv.setParams(0, p);
    r.block({ Rig::on(0, 60, 100), Rig::off(0, 60, 100),
              Rig::on(0, 26, 100, 200),                            // D1: keyswitch to slot 3
              Rig::on(0, 64, 100, 300) }, 512);
    REQUIRE(r.events.size() == 4);
    CHECK(r.events[0].plain);
    CHECK(r.events[0].loaded == 1);                                // the Instrument parameter's
    CHECK(r.events[3].plain);
    CHECK(r.events[3].loaded == 3);                                // the keyswitch's, in the same block
}

TEST_CASE("Z re-runs the other slot with a random amount added", "[driver][commands]")
{
    // Section 7: Z adds a random 0..x to the command's x and 0..y to its y.
    Rig r;
    ChannelParams p; p.instrument = 1;
    p.cmd[0] = { Cmd::E, 0, 0, 0 };
    p.cmd[1] = { Cmd::Z, 15, 0, 0 };
    p.velocityMode = 2;                                            // velocity out of the way
    r.drv.setParams(0, p);
    std::vector<int> volumes;
    for (int i = 0; i < 24; ++i) {
        auto w = r.block({ Rig::on(0, 69, 100) }, 512);
        if (const auto* nr2 = last(w, 0xFF12)) volumes.push_back(nr2->value >> 4);
        r.block({ Rig::off(0, 69) }, 512);
    }
    REQUIRE(volumes.size() >= 8);
    CHECK(minOf(volumes) >= 0);                                    // E's own x, plus 0..15
    CHECK(maxOf(volumes) <= 15);
    CHECK(maxOf(volumes) > 0);                                     // it really is random
    CHECK(minOf(volumes) != maxOf(volumes));

    // The second argument randomises the second one: E's y is the envelope's
    // rate and direction.
    Rig s;
    ChannelParams q; q.instrument = 1; q.velocityMode = 2;
    q.cmd[0] = { Cmd::E, 8, 0, 0 }; q.cmd[1] = { Cmd::Z, 0, 15, 0 };
    s.drv.setParams(0, q);
    std::vector<int> lows;
    for (int i = 0; i < 24; ++i) {
        auto w = s.block({ Rig::on(0, 69, 100) }, 512);
        // The register always holds now (section 2), so the randomised `y`
        // shows in the envelope the driver runs, not in NRx2's low nibble.
        if (const auto* nr2 = last(w, 0xFF12)) CHECK((nr2->value & 15) == 8);
        lows.push_back(int(s.drv.view(0).envRate) | (s.drv.view(0).envDir ? 8 : 0));
        s.block({ Rig::off(0, 69) }, 512);
    }
    CHECK(minOf(lows) != maxOf(lows));

    // Without Z the same note gives the same volume every time.
    Rig t;
    ChannelParams u; u.instrument = 1; u.cmd[0] = { Cmd::E, 15, 0, 0 }; u.velocityMode = 2;
    t.drv.setParams(0, u);
    t.block({ Rig::on(0, 69, 100) }, 512);
    CHECK(t.drv.view(0).envVol == 15);
}

TEST_CASE("notes on ticks wait for the tick; off, they are sample accurate", "[driver][commands]")
{
    // One tick, at frame 300 of the block.
    const std::vector<TickPoint> ticks { { 300, 0 } };
    // The note's burst starts at the cycle the note was applied at; the
    // writes that follow it are spaced like a CPU writing them.
    // The mixer registers a driver writes once at its own initialisation come
    // first and are not the note's (docs/LSDJ_PARITY.md section 2).
    auto burstStart = [](const std::vector<RegWrite>& w) {
        for (const auto& x : w) if (x.addr != 0xFF24 && x.addr != 0xFF25) return x.cycle;
        return uint64_t(0);
    };
    Rig off;
    ChannelParams p; p.instrument = 1; off.drv.setParams(0, p);
    auto w = off.block({ Rig::on(0, 69, 100, 10) }, 512, ticks);
    CHECK(burstStart(w) == off.ren.cycleForFrame(10));             // where the host put it

    Rig on;
    on.drv.setParams(0, p);
    on.drv.setNotesOnTick(true);
    w = on.block({ Rig::on(0, 69, 100, 10) }, 512, ticks);
    CHECK(burstStart(w) == on.ren.cycleForFrame(300));             // held for the tick
    CHECK(on.drv.view(0).active);

    // A note that arrives after the block's last tick waits for the next block.
    Rig late;
    late.drv.setParams(0, p);
    late.drv.setNotesOnTick(true);
    w = late.block({ Rig::on(0, 69, 100, 400) }, 512, ticks);
    CHECK_FALSE(late.drv.view(0).active);
    w = late.block({}, 512, { { 100, 1 } });
    CHECK(late.drv.view(0).active);
    CHECK(burstStart(w) == late.ren.cycleForFrame(512 + 100));
}

TEST_CASE("keyswitches select instruments and never sound", "[driver]")
{
    Rig r;
    ChannelParams p; p.instrument = 1; p.keyswitch = true; r.drv.setParams(0, p);
    auto w = r.block({ Rig::on(0, 26, 100) }, 256);   // D1: keyswitch slot 3 (Bass 25)
    CHECK_FALSE(has(w, 0xFF14));
    CHECK_FALSE(r.drv.view(0).active);
    w = r.block({ Rig::on(0, 60, 100) }, 256);
    CHECK(last(w, 0xFF11)->value == 0x40);            // duty 25%: the bass instrument
    CHECK(r.drv.view(0).instrument == 3);
}

TEST_CASE("the driver drives the chip: rendered audio is not silent", "[driver]")
{
    Rig r;
    Apu apu;
    ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
    std::vector<float> L(512), R(512);
    double energy = 0.0;
    for (int blk = 0; blk < 20; ++blk) {
        std::vector<NoteEvent> ev; if (blk == 1) ev.push_back(Rig::on(0, 69, 120));
        auto w = r.block(ev, 512);
        for (const auto& x : w) { apu.runTo(std::max(x.cycle, apu.cycle())); apu.write(x.addr, x.value); }
        apu.runTo(std::max(r.ren.cycleForFrame(r.frame), apu.cycle()));
        r.ren.render(apu, L.data(), R.data(), 512);
        for (float s : L) energy += double(s) * double(s);
    }
    CHECK(energy > 1.0);
}

/* ------------------------------------------------------- pitch (section 7) */

TEST_CASE("the note table interpolates in period units", "[driver][pitch]")
{
    // Measured (docs/LSDJ_PARITY.md section 3): LSDj's table is one entry a
    // semitone and a fraction is interpolated between two of them in *period*
    // units. Half a semitone below C-5 lands on 1791, where the exponential
    // curve gives 1790.
    CHECK(Driver::periodForNote(72, false) == 1798);
    CHECK(Driver::periodForNote(71, false) == 1783);
    CHECK(Driver::periodForNote(71.5, false) == 1791);
    CHECK(Driver::periodForNote(72.5, false) == 1805);
    CHECK(Driver::periodForNote(69.5, false) == 1759);      // depth 8's trough, measured
    CHECK(Driver::periodForNote(64, false) == 1650);        // E-4, depth 15's trough
}

TEST_CASE("V is a triangle of 64/(x+1) updates, symmetric about the note", "[driver][pitch]")
{
    // Every number here was read off the ROM (docs/LSDJ_PARITY.md section 3):
    // the phase is a six-bit counter stepping by x + 1, the depth table is
    // LSDj's own, and the swing is either side of the note.
    auto series = [](uint8_t speed, uint8_t depth, VibDir dir) {
        Rig r;
        r.tickHz = 1.0;                                  // the pitch clock does this, not the tick
        r.bank.instruments[0].vib = { VibShape::Triangle, dir, speed, depth, 0 };
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
        r.block({ Rig::on(0, 72, 100) }, 64);
        return periodWrites(r.block({}, 48000));         // a second of updates
    };
    // Speed 1, depth 15: a semitone an update, eight of them to the trough.
    const auto deep = series(1, 15, VibDir::Down);
    REQUIRE(deep.size() >= 33);
    CHECK(deep[0] == 1798);                              // the first update is the note itself
    CHECK(deep[1] == 1783);
    CHECK(deep[8] == 1650);                              // eight semitones down
    CHECK(deep[16] == 1798);                             // and back through the note
    CHECK(deep[24] == 1890);                             // eight above it
    CHECK(deep[32] == 1798);                             // 64/(1+1) = 32 updates to the cycle
    // The direction only chooses which half comes first: both cover the swing.
    const auto up = series(1, 15, VibDir::Up);
    REQUIRE(up.size() >= 25);
    CHECK(up[8] == 1890);
    CHECK(up[24] == 1650);
    // A `V` command's depth 0 is an eighth of a semitone, not "off" -- while
    // the *instrument's* own vibrato at depth 0 is off (section 7).
    Rig sh;
    sh.tickHz = 1.0;
    sh.bank.instruments[0].vib.depth = 0;
    { ChannelParams q; q.instrument = 1; q.velocityMode = 2; q.cmd[0] = { Cmd::V, 1, 0, 0 }; sh.drv.setParams(0, q); }
    sh.block({ Rig::on(0, 72, 100) }, 64);
    const auto shallow = periodWrites(sh.block({}, 48000));
    REQUIRE(shallow.size() >= 9);
    CHECK(shallow[8] == 1796);
    CHECK(maxOf(shallow) == 1800);
    CHECK(minOf(shallow) == 1796);
    // Speed 15 is four updates to the cycle: note, trough, note, peak.
    const auto fast = series(15, 15, VibDir::Down);
    REQUIRE(fast.size() >= 4);
    CHECK(fast[0] == 1798); CHECK(fast[1] == 1650); CHECK(fast[2] == 1798); CHECK(fast[3] == 1890);
    // Speed 0 is the slowest, 64 updates to the cycle -- not "off".
    const auto slow = series(0, 15, VibDir::Down);
    REQUIRE(slow.size() >= 33);
    CHECK(slow[16] == 1650);
    CHECK(slow[32] == 1798);
}

TEST_CASE("V in Tick mode runs on the measured table of tick counts", "[driver][pitch]")
{
    // One cycle is 96, 72, 64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4.5, 4 or
    // 3 ticks -- the period halves every three speeds (LSDJ_PARITY 3).
    auto cyclesIn96 = [](uint8_t speed) {
        Rig r;
        r.tickHz = 100.0;                                // one tick a block
        auto& i = r.bank.instruments[0];
        i.pitchSpeed = PitchSpeed::Tick;
        i.vib = { VibShape::Triangle, VibDir::Down, speed, 15, 0 };
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
        r.block({ Rig::on(0, 72, 100) }, 480);
        std::vector<int> all;
        for (int k = 0; k < 96; ++k) { const auto v = periodWrites(r.block({}, 480)); all.insert(all.end(), v.begin(), v.end()); }
        return turns(all);                               // two turning points a cycle
    };
    CHECK(cyclesIn96(0) == 2);                           // 96 ticks a cycle
    CHECK(cyclesIn96(8) == 12);                          // 16
    CHECK(cyclesIn96(11) == 24);                         // 8
    CHECK(cyclesIn96(14) >= 47);                         // 4
}

TEST_CASE("L slides in x + 1 updates, linear in semitones", "[driver][pitch]")
{
    const int a4 = note(69), c5 = note(72);
    SECTION("Fast: x + 1 pitch-clock updates") {
        Rig r;
        r.tickHz = 1.0;
        r.bank.instruments[0].vib.depth = 0;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; p.cmd[0] = { Cmd::L, 4, 0, 0 };
        r.drv.setParams(0, p);
        r.block({ Rig::on(0, 60, 100) }, 4096);          // C-4 first, to slide from
        CHECK(r.drv.view(0).period == note(60));
        r.block({ Rig::off(0, 60) }, 4096);
        auto w = r.block({ Rig::on(0, 72, 100) }, 48000);
        const auto per = periodWrites(w);
        REQUIRE(per.size() >= 7);
        CHECK(per[0] == note(60));                       // the trigger is at the pitch it came from
        // Five equal steps in semitones: the periods LSDj wrote (LSDJ_PARITY 4).
        const int want[6] = { 1612, 1668, 1718, 1760, 1798, 1798 };
        for (int i = 0; i < 6; ++i) { INFO("step " << i); CHECK(per[size_t(i + 1)] == want[i]); }
        CHECK(per.size() == 7);                          // and then it stops writing
    }
    SECTION("L 0 is one update, and the note-on still starts where it was") {
        Rig r;
        r.tickHz = 1.0;
        r.bank.instruments[0].vib.depth = 0;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; p.cmd[0] = { Cmd::L, 0, 0, 0 };
        r.drv.setParams(0, p);
        r.block({ Rig::on(0, 69, 100) }, 4096);
        r.block({ Rig::off(0, 69) }, 4096);
        const auto per = periodWrites(r.block({ Rig::on(0, 72, 100) }, 48000));
        REQUIRE(per.size() == 2);
        CHECK(per[0] == a4);                             // the trigger, at the note before
        CHECK(per[1] == c5);                             // and one update to arrive
    }
    SECTION("Tick: the duration is in ticks") {
        Rig r;
        r.tickHz = 100.0;                                // a tick per 480-frame block
        r.bank.instruments[0].pitchSpeed = PitchSpeed::Tick;
        r.bank.instruments[0].vib.depth = 0;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; p.cmd[0] = { Cmd::L, 4, 0, 0 };
        r.drv.setParams(0, p);
        r.block({ Rig::on(0, 60, 100) }, 480);
        r.block({ Rig::off(0, 60) }, 480);
        std::vector<int> per = periodWrites(r.block({ Rig::on(0, 72, 100) }, 480));
        for (int i = 0; i < 6; ++i) { const auto v = periodWrites(r.block({}, 480)); per.insert(per.end(), v.begin(), v.end()); }
        REQUIRE(per.size() >= 6);
        const int want[6] = { note(60), 1612, 1668, 1718, 1760, 1798 };
        for (int i = 0; i < 6; ++i) { INFO("tick " << i); CHECK(per[size_t(i)] == want[i]); }
    }
}

TEST_CASE("P bends by the measured table, and Drum wraps", "[driver][pitch]")
{
    SECTION("the step table") {
        // The step per update in 1/256 of a semitone (LSDJ_PARITY 5).
        CHECK(Driver::bendStepFor(1) == 1);
        CHECK(Driver::bendStepFor(4) == 4);
        CHECK(Driver::bendStepFor(8) == 12);
        CHECK(Driver::bendStepFor(16) == 40);
        CHECK(Driver::bendStepFor(32) == 144);
        CHECK(Driver::bendStepFor(64) == 544);
        CHECK(Driver::bendStepFor(127) == 2080);
    }
    SECTION("Fast: the note moves, and P 0 stops it where it is") {
        Rig r;
        r.tickHz = 1.0;
        r.bank.instruments[0].vib.depth = 0;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2;
        p.cmd[0] = { Cmd::P, 256 - 64, 0, 0 };           // -64: 544/256 semitones an update
        r.drv.setParams(0, p);
        r.block({ Rig::on(0, 72, 100) }, 64);
        const auto per = periodWrites(r.block({}, 4 * 160));
        REQUIRE(per.size() >= 4);
        // Each update is 544/256 of a semitone below the one before.
        for (int i = 0; i < 4; ++i) {
            INFO("update " << i);
            CHECK(per[size_t(i)] == Driver::periodForNote(72.0 - double(i + 1) * 544.0 / 256.0, false));
        }
        p.cmd[0] = { Cmd::P, 0, 0, 0 }; r.drv.setParams(0, p);
        r.block({}, 4096);
        const int stopped = int(r.drv.view(0).period);
        r.block({}, 8192);
        CHECK(int(r.drv.view(0).period) == stopped);
    }
    SECTION("Drum: the period register moves and wraps at 2048") {
        Rig r;
        r.tickHz = 1.0;
        r.bank.instruments[0].pitchSpeed = PitchSpeed::Drum;
        r.bank.instruments[0].vib.depth = 0;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; p.cmd[0] = { Cmd::P, 16, 0, 0 };
        r.drv.setParams(0, p);
        r.block({ Rig::on(0, 84, 100) }, 64);            // C-6 = 1923
        const auto per = periodWrites(r.block({}, 160 * 3));
        REQUIRE(per.size() >= 2);
        CHECK(per[0] == 1926);                           // 40/256 semitones is about three units
        CHECK(per[1] == 1929);
        r.block({}, 160 * 80);                           // straight through the top of the register
        CHECK(int(r.drv.view(0).period) < 1900);         // it wrapped rather than sticking at 2047
    }
    SECTION("Tick: four of the pitch clock's steps a tick") {
        Rig r;
        r.tickHz = 100.0;
        auto& i = r.bank.instruments[0];
        i.pitchSpeed = PitchSpeed::Tick; i.vib.depth = 0;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; p.cmd[0] = { Cmd::P, 256 - 16, 0, 0 };
        r.drv.setParams(0, p);
        // The note-on's own tick writes the note; the ticks after it bend.
        std::vector<int> per = periodWrites(r.block({ Rig::on(0, 72, 100) }, 480));
        for (int i = 0; i < 5; ++i) { const auto v = periodWrites(r.block({}, 480)); per.insert(per.end(), v.begin(), v.end()); }
        REQUIRE(per.size() >= 5);
        CHECK(per[0] == 1798);
        for (int i = 1; i < 5; ++i) {
            INFO("tick " << i);
            CHECK(per[size_t(i)] == Driver::periodForNote(72.0 - double(i) * 4.0 * 40.0 / 256.0, false));
        }
    }
}

TEST_CASE("the pitch clock is 11712 cycles and free-running", "[driver][pitch]")
{
    // LSDj sets the Game Boy's timer once at boot and never moves it, so the
    // clock does not know that a note began: the phase of an update against a
    // note is where the player pressed play (docs/LSDJ_PARITY.md section 1).
    Rig r;
    r.tickHz = 1.0;
    r.bank.instruments[0].vib = { VibShape::Triangle, VibDir::Down, 1, 15, 0 };
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    r.block({ Rig::on(0, 72, 100) }, 256);
    std::vector<uint64_t> at;
    for (int i = 0; i < 40; ++i) {
        auto w = r.block({}, 512);
        for (const auto& x : w) if (x.addr == 0xFF13) at.push_back(x.cycle);
    }
    REQUIRE(at.size() > 8);
    for (size_t i = 1; i < at.size(); ++i) CHECK(at[i] - at[i - 1] == 11712);
}

/* ------------------------------------------------ notes, plain and bare */

TEST_CASE("a tracker cell with a blank instrument column is bare", "[driver][notes]")
{
    Rig r;
    r.song.noteSource[0] = tracker::NoteSource::Tracker;
    r.tickHz = 20.0;
    r.bank.instruments[0].vib = { VibShape::Triangle, VibDir::Down, 1, 11, 0 };   // slow and deep
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    auto w = r.block({ cellOn(0, 69, 1) }, 480);
    CHECK(anyTrigger(w, 0xFF14));                        // an instrument column: a plain note
    CHECK(r.report(0)->plain);
    CHECK(r.report(0)->loaded == 1);
    r.block({}, 24000);                                  // half a second of vibrato
    const int bent = int(r.drv.view(0).period);
    CHECK(bent != note(69));                             // the vibrato has moved the pitch
    w = r.block({ cellOn(0, 69, 0) }, 240);              // the same note, blank column
    CHECK_FALSE(anyTrigger(w, 0xFF14));                  // no trigger
    CHECK_FALSE(has(w, 0xFF12));                         // the envelope is not rewritten
    CHECK(std::abs(int(r.drv.view(0).period) - bent) <= 40);   // the vibrato kept its phase
    CHECK_FALSE(r.report(0)->plain);
    CHECK(r.report(0)->loaded == 1);
    // A plain cell starts the phase again, so the note is in tune at its start.
    w = r.block({ cellOn(0, 69, 1) }, 240);
    CHECK(anyTrigger(w, 0xFF14));
    CHECK(has(w, 0xFF13, note(69) & 0xFF));              // the trigger is on the note itself
}

TEST_CASE("a bare cell leaves the table where it is", "[driver][notes]")
{
    Rig r;
    r.song.noteSource[0] = tracker::NoteSource::Tracker;
    r.tickHz = 100.0;
    ChannelParams p; p.instrument = 5; p.velocityMode = 2; r.drv.setParams(0, p);   // Chord arp: table 1
    r.block({ cellOn(0, 60, 5) }, 480);
    r.block({}, 480 * 7);
    const int step = r.drv.view(0).tableStep;
    CHECK(step >= 6);
    r.block({ cellOn(0, 62, 0) }, 480);                  // bare: the table runs on
    CHECK(r.drv.view(0).tableStep >= step);
    r.block({ cellOn(0, 64, 5) }, 480);                  // plain: it starts again
    CHECK(r.drv.view(0).tableStep <= 2);
}

TEST_CASE("a tracker cell's velocity is honoured like MIDI's", "[driver][notes]")
{
    Rig r;
    r.song.noteSource[0] = tracker::NoteSource::Tracker;
    auto w = r.block({ cellOn(0, 69, 1, 64) }, 512);
    ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
    w = r.block({ cellOn(0, 69, 1, 64) }, 512);
    CHECK(last(w, 0xFF12)->value == 0x88);               // 64 of 127 is level 8
    w = r.block({ cellOn(0, 71, 1, 127) }, 512);
    CHECK(last(w, 0xFF12)->value == 0xF8);
}

TEST_CASE("Note-off Release lets the sound finish", "[driver][notes]")
{
    SECTION("a held envelope gets a decrease written, without a trigger") {
        Rig r;
        auto& i = r.bank.instruments[0]; i.noteOff = NoteOff::Release; i.envRate = 0;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
        r.block({ Rig::on(0, 69, 100) }, 256);
        auto w = r.block({ Rig::off(0, 69) }, 256);
        // The register holds and the driver runs the fade itself (section 26),
        // so what the release does is visible in the running state and in the
        // volume the chip is really at, not in NRx2's low nibble.
        CHECK(r.drv.view(0).envRate == 1);
        CHECK(r.drv.view(0).envDir == 0);                // downward
        CHECK_FALSE(anyTrigger(w, 0xFF14));
        CHECK_FALSE(r.drv.view(0).active);
        CHECK(r.drv.view(0).dacOn);
        r.block({}, 48000 * 2);
        CHECK(r.drv.view(0).volume < 13);                // and it really does fall
    }
    SECTION("a rising envelope is turned round") {
        Rig r;
        auto& i = r.bank.instruments[0]; i.noteOff = NoteOff::Release; i.envDir = EnvDir::Up; i.envRate = 4;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
        r.block({ Rig::on(0, 69, 100) }, 256);
        r.block({ Rig::off(0, 69) }, 256);
        CHECK(r.drv.view(0).envRate == 1);               // down, rate 1
        CHECK(r.drv.view(0).envDir == 0);
    }
    SECTION("a decaying envelope is left alone") {
        Rig r;
        auto& i = r.bank.instruments[0]; i.noteOff = NoteOff::Release; i.envRate = 3;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
        r.block({ Rig::on(0, 69, 100) }, 256);
        auto w = r.block({ Rig::off(0, 69) }, 256);
        CHECK_FALSE(has(w, 0xFF12));
    }
    SECTION("WAV steps its level down, a tick apart") {
        Rig r;
        r.tickHz = 100.0;
        r.bank.instruments[6].noteOff = NoteOff::Release;      // Triangle bass
        ChannelParams p; p.instrument = 7; r.drv.setParams(2, p);
        r.block({ Rig::on(2, 48, 100) }, 480);
        auto w = r.block({ Rig::off(2, 48) }, 480);
        CHECK(has(w, 0xFF1C, 0x40));                     // 50 %
        w = r.block({}, 480);
        CHECK(has(w, 0xFF1C, 0x60));                     // 25 %
        w = r.block({}, 480);
        CHECK(has(w, 0xFF1C, 0x00));                     // mute
        CHECK_FALSE(r.drv.view(2).dacOn);
    }
}

TEST_CASE("All notes off silences a channel whatever its state says", "[driver][notes]")
{
    SECTION("a released voice is not active and is still killed") {
        Rig r;
        auto& i = r.bank.instruments[0]; i.noteOff = NoteOff::Release; i.envRate = 3;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
        r.block({ Rig::on(0, 69, 100) }, 256);
        r.block({ Rig::off(0, 69) }, 256);
        REQUIRE_FALSE(r.drv.view(0).active);
        REQUIRE(r.drv.view(0).dacOn);
        auto w = r.block({ allOff(0) }, 256);
        CHECK(has(w, 0xFF12, 0x00));
        CHECK_FALSE(r.drv.view(0).dacOn);
    }
    SECTION("CC 120 does the same, and clears what was queued") {
        Rig r;
        r.tickHz = 100.0;
        auto& i = r.bank.instruments[0]; i.noteOff = NoteOff::Release; i.envRate = 3;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; p.cmd[0] = { Cmd::D, 8, 0, 0 };
        r.drv.setParams(0, p);
        r.block({ Rig::on(0, 69, 100) }, 480);           // D 8: waiting to start
        NoteEvent cc; cc.channel = 0; cc.kind = NoteEvent::Control; cc.a = 120; cc.b = 0;
        r.block({ cc }, 480);
        auto w = r.block({}, 480 * 12);
        CHECK_FALSE(r.drv.view(0).active);
        CHECK_FALSE(anyTrigger(w, 0xFF14));              // the delayed note never arrives
    }
    SECTION("a tracker channel's flush is not filtered by the source gate") {
        Rig r;
        r.song.noteSource[0] = tracker::NoteSource::Tracker;
        ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
        r.block({ cellOn(0, 69, 1) }, 256);
        REQUIRE(r.drv.view(0).dacOn);
        NoteEvent e = allOff(0); e.source = NoteEvent::Midi;
        auto w = r.block({ e }, 256);
        CHECK(has(w, 0xFF12, 0x00));
    }
}

TEST_CASE("a kill clears the held notes, so a later release starts nothing", "[driver][notes]")
{
    Rig r;
    r.tickHz = 100.0;
    ChannelParams p; p.instrument = 1; p.cmd[0] = { Cmd::K, 1, 0, 0 }; r.drv.setParams(0, p);
    r.block({ Rig::on(0, 60, 100) }, 480);
    r.block({ Rig::on(0, 64, 100) }, 480);
    r.block({}, 480 * 3);
    REQUIRE_FALSE(r.drv.view(0).active);
    auto w = r.block({ Rig::off(0, 64) }, 480);
    CHECK_FALSE(r.drv.view(0).active);
    CHECK_FALSE(has(w, 0xFF14));
    CHECK_FALSE(has(w, 0xFF13));
}

TEST_CASE("the notes-on-tick queue never runs an event out of order", "[driver][notes]")
{
    // The queue holds 256 events; a fuller block runs the oldest first, so a
    // note-off can never overtake its own note-on (section 8).
    Rig r;
    r.drv.setNotesOnTick(true);
    ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
    std::vector<NoteEvent> ev;
    ev.push_back(Rig::on(0, 60, 100));                   // the note-on is the oldest
    for (int i = 0; i < 255; ++i) ev.push_back(Rig::off(0, 61));   // fills the queue
    ev.push_back(Rig::off(0, 60));                       // one too many: the oldest runs now
    auto w = r.block(ev, 512, { { 300, 0 } });
    CHECK(anyTrigger(w, 0xFF14));                        // the note did sound
    CHECK_FALSE(r.drv.view(0).active);                   // and its note-off came after it
}

TEST_CASE("the Instrument parameter moving clears a keyswitch", "[driver]")
{
    Rig r;
    r.tickHz = 100.0;
    ChannelParams p; p.instrument = 1; p.keyswitch = true; r.drv.setParams(0, p);
    r.block({ Rig::on(0, 26, 100) }, 480);               // D1: keyswitch slot 3 (Bass 25)
    auto w = r.block({ Rig::on(0, 60, 100) }, 480);
    CHECK(last(w, 0xFF11)->value == 0x40);               // duty 25 %
    CHECK(r.drv.view(0).instrument == 3);
    r.block({ Rig::off(0, 60) }, 480);
    p.instrument = 6; r.drv.setParams(0, p);             // the lane is not dead
    r.block({}, 480);
    w = r.block({ Rig::on(0, 60, 100) }, 480);
    CHECK(r.drv.view(0).instrument == 6);
    CHECK(last(w, 0xFF11)->value == 0x80);               // Duty cycler: 50 %
}

/* ---------------------------------------------------- the other letters */

TEST_CASE("C arpeggiates 0, x, y and the chord rate slows it", "[driver][commands]")
{
    auto series = [](uint8_t rate, int16_t x, int16_t y, int n, uint8_t cmdRate = 0) {
        Rig r;
        r.tickHz = 100.0;
        r.bank.instruments[0].chordRate = rate; r.bank.instruments[0].cmdRate = cmdRate; r.bank.instruments[0].vib.depth = 0;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; p.cmd[0] = { Cmd::C, x, y, 0 };
        r.drv.setParams(0, p);
        r.block({ Rig::on(0, 60, 100) }, 480);
        return periodSeries(r, 0, n, 480);
    };
    const auto three = series(0, 3, 7, 9);
    for (size_t i = 0; i + 3 < three.size(); ++i) CHECK(three[i] == three[i + 3]);
    std::vector<int> sorted = three; std::sort(sorted.begin(), sorted.end()); sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    REQUIRE(sorted.size() == 3);
    CHECK(sorted[0] == note(60)); CHECK(sorted[1] == note(63)); CHECK(sorted[2] == note(67));
    // y = 0 is a two-note cycle, and C 0 0 stops.
    const auto two = series(0, 3, 0, 8);
    for (size_t i = 0; i + 2 < two.size(); ++i) CHECK(two[i] == two[i + 2]);
    const auto none = series(0, 0, 0, 4);
    for (int x : none) CHECK(x == note(60));
    // One step every rate + 1 ticks, and the note's own tick is the root
    // (measured): the pairs start one sample in.
    const auto slow = series(1, 3, 7, 13);
    REQUIRE(slow.size() >= 7);
    CHECK(slow[1] == slow[2]);
    CHECK(slow[3] == slow[4]);
    CHECK(slow[1] != slow[3]);
    // The chord's rate is its own (section 37): the command rate does not
    // touch it, and it does not touch the command rate.
    const auto cmdOnly = series(0, 3, 7, 9, 3);
    for (size_t i = 0; i + 3 < cmdOnly.size(); ++i) CHECK(cmdOnly[i] == cmdOnly[i + 3]);
    CHECK(cmdOnly[1] != cmdOnly[2]);
    // At chord rate 2 the steps come three ticks apart: from the first change
    // on, runs of three.
    const auto both = series(2, 3, 7, 20, 0);
    size_t f = 1;
    while (f < both.size() && both[f] == both[0]) ++f;
    REQUIRE(f + 6 < both.size());
    CHECK(both[f] == both[f + 1]);
    CHECK(both[f + 1] == both[f + 2]);
    CHECK(both[f + 2] != both[f + 3]);
    CHECK(both[f + 3] == both[f + 4]);
    CHECK(both[f + 4] == both[f + 5]);
    CHECK(both[f + 5] != both[f + 6]);
}

TEST_CASE("R retriggers and steps the volume", "[driver][commands]")
{
    auto volumes = [](int16_t x) {
        Rig r;
        r.tickHz = 100.0;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2;
        p.cmd[0] = { Cmd::E, 8, 0, 0 }; p.cmd[1] = { Cmd::R, x, 2, 0 };
        r.drv.setParams(0, p);
        r.block({ Rig::on(0, 60, 100) }, 480);
        std::vector<int> out;
        for (int i = 0; i < 24; ++i) {
            auto w = r.block({}, 480);
            if (anyTrigger(w, 0xFF14)) out.push_back(int(r.drv.view(0).envVol));
        }
        return out;
    };
    // `x` is a signed nibble: 1-7 up by that much, 9-15 down by sixteen minus
    // it -- measured, `R A` steps down by six (docs/LSDJ_PARITY.md section 8).
    const auto up = volumes(2);
    REQUIRE(up.size() >= 3);
    CHECK(up[0] == 10); CHECK(up[1] == 12); CHECK(up[2] == 14);
    const auto down = volumes(10);
    REQUIRE(down.size() >= 2);
    CHECK(down[0] == 2); CHECK(down[1] == 0);
    const auto flat = volumes(0);                        // 0: the level does not move
    for (int v : flat) CHECK(v == 8);
}

TEST_CASE("M sets a side or moves it", "[driver][commands]")
{
    Rig r;
    GlobalParams g; g.masterL = 5; g.masterR = 5; r.drv.setGlobal(g);
    ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
    auto w = r.block({}, 512);
    REQUIRE(last(w, 0xFF24) != nullptr);
    CHECK(last(w, 0xFF24)->value == 0x55);
    p.cmd[0] = { Cmd::M, 9, 13, 0 }; r.drv.setParams(0, p);   // left up 1, right down 1
    w = r.block({}, 512);
    CHECK(last(w, 0xFF24)->value == 0x64);
    p.cmd[0] = { Cmd::M, 3, 8, 0 }; r.drv.setParams(0, p);    // left to 3, right untouched
    w = r.block({}, 512);
    CHECK(last(w, 0xFF24)->value == 0x34);
    p.cmd[0] = { Cmd::M, 11, 15, 0 }; r.drv.setParams(0, p);  // up 3, down 3
    w = r.block({}, 512);
    CHECK(last(w, 0xFF24)->value == 0x61);
}

TEST_CASE("a Step-mode table advances a row per trigger", "[driver][commands]")
{
    Rig r;
    r.tickHz = 100.0;
    r.bank.instruments[4].tableMode = TableMode::Step;    // Chord arp: table 1, transposes 0 3 7 12
    ChannelParams p; p.instrument = 5; p.velocityMode = 2; r.drv.setParams(0, p);
    r.block({ Rig::on(0, 60, 100) }, 480);
    CHECK(r.drv.view(0).period == note(60));             // row 0
    r.block({}, 480 * 6);
    CHECK(r.drv.view(0).period == note(60));             // ticks do not move it
    r.block({ Rig::off(0, 60) }, 480);
    r.block({ Rig::on(0, 60, 100) }, 480);
    CHECK(r.drv.view(0).period == note(63));             // row 1
    r.block({ Rig::off(0, 60) }, 480);
    r.block({ Rig::on(0, 60, 100) }, 480);
    CHECK(r.drv.view(0).period == note(67));             // row 2
}

TEST_CASE("a table's G asks the Player for its row lengths", "[driver][commands]")
{
    Rig r;
    r.tickHz = 100.0;
    // A table that asks for groove 2 and then transposes a row at a time.
    Table t; t.used = true; t.name = "Grooved";
    t.steps[0].cmd1 = { Cmd::G, 2, 0, 0 };
    for (int i = 0; i < 4; ++i) { t.steps[size_t(i)].hasTranspose = true; t.steps[size_t(i)].transpose = int8_t(i * 5); }
    t.end = TableEnd::Stop;
    r.bank.tables[7] = t;
    r.bank.instruments[0].table = 8; r.bank.instruments[0].vib.depth = 0;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    r.song.grooves[1].ticks = { 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };   // slot 2, which the G names
    const uint8_t ticks[16] = { 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    r.drv.setTableGroove(0, ticks);                      // the Player hands the same ticks back (section 57)
    r.block({ Rig::on(0, 60, 100) }, 480);               // the first tick runs row 0
    CHECK(r.drv.tableGrooveSlot(0) == 2);                // the slot the table asked for
    const auto series = periodSeries(r, 0, 8, 480);
    // Three ticks a row instead of one.
    CHECK(series[0] == note(60)); CHECK(series[1] == note(60));
    CHECK(series[2] == note(65)); CHECK(series[3] == note(65)); CHECK(series[4] == note(65));
    CHECK(series[5] == note(70));
    // Without a groove the table is back to a row a tick.
    r.drv.setTableGroove(0, nullptr);
    r.bank.instruments[0].table = 0;
}

TEST_CASE("the row that carries a table's G takes the groove's first step", "[driver][commands]")
{
    // Section 57: the G lands at its own row, so the groove's step 0 is that
    // row's length -- the Driver reads the song's groove where it used to wait
    // for the Player's next hand-off and give the row one tick.
    Rig r;
    r.tickHz = 100.0;
    r.song.grooves[1].ticks = { 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };   // slot 2: three ticks, then one
    Table t; t.used = true; t.name = "Grooved";
    t.steps[0].cmd1 = { Cmd::G, 2, 0, 0 };
    for (int i = 0; i < 4; ++i) { t.steps[size_t(i)].hasTranspose = true; t.steps[size_t(i)].transpose = int8_t(i * 5); }
    t.end = TableEnd::Stop;
    r.bank.tables[7] = t;
    r.bank.instruments[0].table = 8; r.bank.instruments[0].vib.depth = 0;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    r.block({ Rig::on(0, 60, 100) }, 480);               // the first tick runs row 0, with no groove handed over
    const auto series = periodSeries(r, 0, 6, 480);
    CHECK(series[0] == note(60)); CHECK(series[1] == note(60));   // row 0 holds for its three ticks
    CHECK(series[2] == note(65));                                  // row 1 takes step 1: one tick
    CHECK(series[3] == note(70)); CHECK(series[4] == note(70));
    r.bank.instruments[0].table = 0;
}

TEST_CASE("L in a table slides to the row's own transpose", "[driver][commands]")
{
    Rig r;
    r.tickHz = 100.0;
    r.bank.instruments[0].vib.depth = 0;
    r.bank.instruments[0].table = 5;                     // Slide up: an octave below, then L 60
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    r.block({ Rig::on(0, 69, 100) }, 480);
    CHECK(r.drv.view(0).period == note(57));             // the first row's transpose
    r.block({}, 480);                                    // the second row: L from there to the note
    r.block({}, 4000);
    const int mid = int(r.drv.view(0).period);
    CHECK(mid > note(57));
    CHECK(mid < note(69));
    r.block({}, 12000);
    CHECK(r.drv.view(0).period == note(69));             // and it arrives at the note itself
}

/* ---------------------------------------------------- cells and the clock */

TEST_CASE("an OFF cell's command columns still apply", "[driver][notes]")
{
    // A cell that ends a note carries its columns like any other cell
    // (docs/COMMANDS_AND_TEMPO.md section 3): the note stops, and the two
    // commands are the slots in force from that step on.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[0] = tracker::NoteSource::Tracker;
    ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);      // Square lead, duty 2
    auto w = r.block({ cellOn(0, 69, 1) }, 480);
    CHECK(last(w, 0xFF11)->value == 0x80);                         // 50 %
    NoteEvent e; e.channel = 0; e.kind = NoteEvent::NoteOff; e.source = NoteEvent::Tracker; e.a = 69;
    e.cmd1 = { Cmd::W, 0, 0, 0 };                                  // 12.5 % from here
    r.block({ e }, 480);
    CHECK(r.drv.view(0).duty == 0);                                // applied at the OFF's own step
    w = r.block({ cellOn(0, 69, 0) }, 480);                        // a bare note keeps it
    CHECK(r.drv.view(0).duty == 0);
    w = r.block({ cellOn(0, 69, 1) }, 480);                        // a plain one reloads the instrument
    CHECK(last(w, 0xFF11)->value == 0x80);
}

TEST_CASE("a cell's instrument column is exact under the velocity bank", "[driver][notes]")
{
    // Velocity picks an instrument around the channel's own choice; a cell has
    // already named the one the recorder saw load (section 9.4), so playing it
    // back must not move it again.
    Rig r;
    r.song.noteSource[3] = tracker::NoteSource::Tracker;
    r.drv.setRecordMask(0xF);                                      // MIDI plays through onto a Trk lane
    ChannelParams p; p.instrument = 11; p.velocityMode = 1; r.drv.setParams(3, p);
    auto w = r.block({ Rig::on(3, 60, 20) }, 512);                 // 11 + 20/8 = 13, Hat closed
    CHECK(last(w, 0xFF21)->value == 0x98);                         // vol 9, the low nibble always 8
    r.block({ Rig::off(3, 60) }, 512);
    NoteEvent c = cellOn(3, 60, 13, 100); c.velSet = false;        // as recorded under the bank: the slot, no VEL
    w = r.block({ c }, 512);                                       // the cell names 13, and stays there
    CHECK(last(w, 0xFF21)->value == 0x98);
    CHECK(r.drv.view(3).envRate == 1);                              // Hat closed's own rate
}

TEST_CASE("a pitch update inside a tick's burst follows it", "[driver][commands]")
{
    // A tick's register writes go out as one burst an instruction pair apart.
    // A 360 Hz pitch update landing inside that burst must follow it, or a
    // period computed at the tick would be written over the fresher one the
    // update produced -- the last period write must always be the period the
    // driver has arrived at.
    Rig r;
    r.tickHz = 375.0;
    r.song.noteSource[0] = tracker::NoteSource::Tracker;
    ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
    r.block({ cellOn(0, 69, 1) }, 128);
    int checked = 0;
    for (int i = 0; i < 120; ++i) {
        NoteEvent e; e.channel = 0; e.kind = NoteEvent::Command; e.source = NoteEvent::Tracker;
        e.cmd1 = { Cmd::E, int16_t(10 + (i & 3)), 0, 0 };          // a write before the period one
        e.cmd2 = { Cmd::V, 15, int16_t(8 + (i & 1)), 0 };          // and one that moves the period
        const auto w = r.block({ e }, 128);
        if (const auto* lo = last(w, 0xFF13)) {
            CHECK(lo->value == (r.drv.view(0).period & 0xFF));
            ++checked;
        }
    }
    CHECK(checked > 50);
}


/* ---------------------------------------------------------------- Hybrid */
// docs/COMMANDS_AND_TEMPO.md section 20: the notes come from MIDI and
// everything else from the song's cells.

TEST_CASE("a MIDI note under Hybrid takes the cell's instrument and commands", "[driver][hybrid]")
{
    Rig r;
    r.song.noteSource[0] = tracker::NoteSource::Hybrid;
    ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
    // The cell's columns apply first; the note-on in the same tick loads what
    // the cell chose and then takes its commands.
    auto w = r.block({ cellHybrid(0, 3, { Cmd::E, 5, 2, 0 }), Rig::on(0, 69, 100) }, 512);
    CHECK(r.drv.view(0).instrument == 3);            // not the Instrument parameter's 1
    CHECK(r.drv.view(0).active);
    CHECK(anyTrigger(w, 0xFF14));                    // the MIDI note sounded
    REQUIRE(last(w, 0xFF12) != nullptr);
    CHECK(r.drv.view(0).envVol == 5);                // E 5 2, after the note's own envelope
    CHECK(r.drv.view(0).envRate == 2);
}

TEST_CASE("a Hybrid cell with no note lands on the sounding one at the tick", "[driver][hybrid]")
{
    Rig r;
    r.song.noteSource[0] = tracker::NoteSource::Hybrid;
    ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
    r.block({ cellHybrid(0, 3), Rig::on(0, 69, 100) }, 512);
    // No note this block: the commands wait for the tick and then shape what
    // is sounding.
    auto w = r.block({ cellHybrid(0, 0, { Cmd::E, 9, 1, 0 }) }, 512);
    REQUIRE(last(w, 0xFF12) != nullptr);
    CHECK(r.drv.view(0).envVol == 9);
    CHECK(r.drv.view(0).envRate == 1);
    CHECK(r.drv.view(0).active);                     // and it is still the same note
    CHECK(r.drv.view(0).note == 69);
}

TEST_CASE("a Hybrid cell's note and OFF are ignored", "[driver][hybrid]")
{
    Rig r;
    r.song.noteSource[0] = tracker::NoteSource::Hybrid;
    ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
    // The Player never sends one, and the source gate drops it if it does.
    auto w = r.block({ cellOn(0, 60, 1) }, 512);
    CHECK(!r.drv.view(0).active);
    CHECK(!anyTrigger(w, 0xFF14));
    r.block({ Rig::on(0, 69, 100) }, 512);
    CHECK(r.drv.view(0).active);
    NoteEvent cellOff;
    cellOff.channel = 0; cellOff.kind = NoteEvent::NoteOff; cellOff.source = NoteEvent::Tracker; cellOff.a = 69;
    r.block({ cellOff }, 512);
    CHECK(r.drv.view(0).active);                     // the MIDI note owns the channel
}

TEST_CASE("a Hybrid channel's slots and keyswitches are inert", "[driver][hybrid]")
{
    Rig r;
    r.song.noteSource[0] = tracker::NoteSource::Hybrid;
    ChannelParams p;
    p.instrument = 3; p.table = 2; p.keyswitch = true;
    p.cmd[0] = { Cmd::E, 3, 2, 0 };
    r.drv.setParams(0, p);
    // A note in the keyswitch octave does nothing at all: it neither selects
    // nor sounds.
    auto w = r.block({ Rig::on(0, 27, 100) }, 512);
    CHECK(!r.drv.view(0).active);
    CHECK(!has(w, 0xFF12));                          // the channel's registers are untouched
    CHECK(!has(w, 0xFF14));
    // A playable note takes neither the Instrument parameter nor the slot.
    w = r.block({ Rig::on(0, 69, 100) }, 512);
    CHECK(r.drv.view(0).active);
    CHECK(r.drv.view(0).instrument == 0);            // the parameter's 3 is inert
    REQUIRE(last(w, 0xFF12) != nullptr);
    CHECK(last(w, 0xFF12)->value != 0x32);           // and so is the E slot
    CHECK(r.drv.view(0).tableSlot == 0);             // and the Table parameter
    // The command octave is inert too: it fires nothing.
    const auto before = r.drv.view(0).envVol;
    w = r.block({ Rig::on(0, 5, 100) }, 512);
    CHECK(r.drv.view(0).envVol == before);
}

TEST_CASE("L in a Hybrid cell is the next note's portamento", "[driver][hybrid]")
{
    Rig r;
    r.tickHz = 20.0;
    r.bank.instruments[0].vib.depth = 0;             // the lead's own vibrato would ride on top
    r.song.noteSource[0] = tracker::NoteSource::Hybrid;
    ChannelParams p; p.velocityMode = 2; r.drv.setParams(0, p);
    r.block({ cellHybrid(0, 1), Rig::on(0, 60, 100) }, 480);
    const int from = note(60), target = note(72);
    CHECK(int(r.drv.view(0).period) == from);
    // The cell asks for a slide; no note arrives in its tick, so it waits for
    // one instead of sliding what is sounding (section 20).
    r.block({ cellHybrid(0, 0, { Cmd::L, 60, 0, 0 }) }, 480);
    CHECK(int(r.drv.view(0).period) == from);        // nothing has moved yet
    r.block({ Rig::on(0, 72, 100) }, 120);
    const int started = int(r.drv.view(0).period);
    INFO("from " << from << " started " << started << " target " << target);
    CHECK(std::abs(started - from) <= 8);            // it left from the note before
    r.block({}, 4200);                               // about thirty of the sixty-one updates
    const int half = int(r.drv.view(0).period);
    CHECK(std::abs(half - (target + (from - target) / 2)) <= 30);
    r.block({}, 8000);
    CHECK(int(r.drv.view(0).period) == target);      // and it arrives
}

TEST_CASE("a Hybrid cell's D holds its commands back", "[driver][hybrid]")
{
    Rig r;
    r.tickHz = 240.0;
    r.song.noteSource[0] = tracker::NoteSource::Hybrid;
    ChannelParams p; r.drv.setParams(0, p);
    r.block({ cellHybrid(0, 1), Rig::on(0, 69, 100) }, 512);
    // D 3 with no note: the E waits three ticks and then lands.
    auto w = r.block({ cellHybrid(0, 0, { Cmd::D, 3, 0, 0 }, { Cmd::E, 7, 1, 0 }) }, 400);
    CHECK_FALSE(has(w, 0xFF12));                     // the E has not fired yet
    CHECK(r.drv.view(0).envRate == 0);
    r.block({}, 400);
    CHECK(r.drv.view(0).envVol == 7);                // and now it has
    CHECK(r.drv.view(0).envRate == 1);
}

/* ============================ zombie-mode levels (section 26) ============ */

namespace {

/// A chip fed exactly what the driver wrote, so a test can ask the APU what
/// the volume really is after a sequence (section 26).
struct Chip {
    Apu apu;
    explicit Chip(Console model = Console::DMG) { apu.setModel(model); apu.reset(); }
    void feed(const std::vector<RegWrite>& w)
    {
        for (const auto& x : w) { apu.runTo(std::max(x.cycle, apu.cycle())); apu.write(x.addr, x.value); }
    }
};

/// The E command as a cell, which is how a level change arrives from a table
/// or a phrase.
NoteEvent levelCell(int ch, int level, int env = 0)
{
    return cellCmd(ch, Command{ Cmd::E, int16_t(level), int16_t(env), 0 });
}

} // namespace

TEST_CASE("a level change lands on the chip's volume without a trigger", "[driver][zombie]")
{
    // Section 26: the driver issues the shortest zombie-mode NRx2 sequence to
    // the level it wants. The check is against the APU itself -- the sequence
    // is only right if the chip ends up there.
    for (auto model : { Console::DMG, Console::CGB })
        for (int target = 0; target <= 15; ++target) {
            Rig r(model);
            Chip chip(model);
            ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);   // vol 13, rate 0
            chip.feed(r.block({ Rig::on(0, 69, 100) }, 512));
            REQUIRE(chip.apu.channelVolume(0) == 13);
            const auto w = r.block({ levelCell(0, target) }, 512);
            chip.feed(w);
            CHECK(chip.apu.channelVolume(0) == target);
            CHECK_FALSE(anyTrigger(w, 0xFF14));
            CHECK(chip.apu.channelActive(0));               // the DAC never goes off on the way
            CHECK(r.drv.view(0).volume == target);          // and the driver's model agrees
        }
}

TEST_CASE("a level change on the noise channel is zombie mode too", "[driver][zombie]")
{
    Rig r;
    Chip chip;
    ChannelParams p; p.instrument = 5; p.velocityMode = 2; r.drv.setParams(3, p);   // a noise instrument
    chip.feed(r.block({ Rig::on(3, 60, 100) }, 512));
    const uint8_t was = chip.apu.channelVolume(3);
    CHECK(was > 0);
    const auto w = r.block({ levelCell(3, 4) }, 512);
    chip.feed(w);
    CHECK(chip.apu.channelVolume(3) == 4);
    CHECK_FALSE(anyTrigger(w, 0xFF23));
    CHECK_FALSE(anyTrigger(w, 0xFF13));
}

TEST_CASE("a table's volume column is a level change, not a retrigger", "[driver][zombie][table]")
{
    Rig r;
    Chip chip;
    r.tickHz = 100.0;
    auto& t = r.bank.tables[0]; t.used = true; t.end = TableEnd::Stop;
    t.steps[0].vol = 15; t.steps[1].vol = 11; t.steps[2].vol = 7; t.steps[3].vol = 3;
    r.bank.instruments[0].table = 1;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    chip.feed(r.block({ Rig::on(0, 69, 100) }, 480));
    // Row 0 goes out with the note itself now (section 31).
    CHECK(chip.apu.channelVolume(0) == 15);
    for (const int want : { 11, 7, 3 }) {
        const auto w = r.block({}, 480);
        chip.feed(w);
        CHECK(chip.apu.channelVolume(0) == want);
        CHECK_FALSE(anyTrigger(w, 0xFF14));
    }
}

TEST_CASE("E never triggers, whatever it does to the envelope", "[driver][zombie]")
{
    // Measured (docs/LSDJ_PARITY.md section 6): `E x y` walks the level to x
    // by zombie steps at its own tick and sets the direction and rate of what
    // follows. There is no trigger and no phase reset, whether or not the
    // envelope moved -- `E 8 0` on a channel at 15 is seven down-triples and
    // nothing else.
    Rig r;
    Chip chip;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);   // rate 0, down
    chip.feed(r.block({ Rig::on(0, 69, 100) }, 512));
    SECTION("the same direction and rate") {
        const auto w = r.block({ levelCell(0, 9, 0) }, 512);
        chip.feed(w);
        CHECK_FALSE(anyTrigger(w, 0xFF14));
        CHECK(chip.apu.channelVolume(0) == 9);
    }
    SECTION("a rate of its own") {
        const auto w = r.block({ levelCell(0, 9, 3) }, 512);
        chip.feed(w);
        CHECK_FALSE(anyTrigger(w, 0xFF14));
        CHECK(chip.apu.channelVolume(0) == 9);
        CHECK(r.drv.view(0).envRate == 3);
    }
    SECTION("a direction of its own") {
        const auto w = r.block({ levelCell(0, 9, 8 + 3) }, 512);
        chip.feed(w);
        CHECK_FALSE(anyTrigger(w, 0xFF14));
        CHECK(chip.apu.channelVolume(0) == 9);
        CHECK(r.drv.view(0).envDir == 1);
    }
}

TEST_CASE("a slide holds its aim through the table and stops at the bottom of the range", "[driver][table]")
{
    // Section 71: LSDj's wave kick is a table row carrying TSP -60 beside L20,
    // and the rows after it are empty. The sweep has to survive the table
    // stepping off that row, and it has to aim at a note the channel can
    // sound -- note 12 is below the wave channel's bottom, so LSDj slides to
    // note 24 in x + 1 updates instead of tearing past it.
    Rig r;
    auto& tb = r.bank.tables[0];
    tb.used = true; tb.name = "Kick";
    tb.steps[0].hasTranspose = true; tb.steps[0].transpose = -60;
    tb.steps[0].cmd1 = Command{ Cmd::L, 0x20, 0, 0 };
    auto& in = r.bank.instruments[0];
    in = Instrument::defaults(InstrumentType::Wave, "Kick");
    in.used = true; in.table = 1; in.transpose = true;
    r.song.noteSource[2] = tracker::NoteSource::Tracker;
    ChannelParams p; p.instrument = 1; r.drv.setParams(2, p);
    NoteEvent e = cellOn(2, 72, 1); e.velSet = false;
    std::vector<int> periods;
    int lo = 0;
    for (int b = 0; b < 900 && periods.size() < 400; ++b)
        for (const auto& x : r.block(b == 0 ? std::vector<NoteEvent>{ e } : std::vector<NoteEvent>{}, 128)) {
            if (x.addr == 0xFF1D) lo = x.value;
            else if (x.addr == 0xFF1E) {
                const int per = ((x.value & 7) << 8) | lo;
                if (periods.empty() || per != periods.back()) periods.push_back(per);
            }
        }
    REQUIRE(periods.size() > 20);
    CHECK(periods[0] == 1923);                       // the plain note, C-5 (section 68)
    // It falls, every update, and never turns back up: the whine was the base
    // leaping by the transpose when the table stepped to its empty second row.
    for (size_t i = 1; i < 20; ++i) CHECK(periods[i] < periods[i - 1]);
    // LSDj's own periods for the first updates of this sweep, off by at most
    // one where the fixed-point step rounds the other way.
    const int lsdj[10] = { 1923, 1911, 1900, 1887, 1873, 1857, 1840, 1823, 1803, 1782 };
    for (size_t i = 0; i < 10; ++i) CHECK(std::abs(periods[i] - lsdj[i]) <= 1);
    // It reaches the bottom of the range rather than wrapping past it: the
    // whine was the period leaping to 2040, which is -8 in eleven bits.
    CHECK(*std::min_element(periods.begin(), periods.end()) <= 60);
    CHECK(*std::max_element(periods.begin(), periods.end()) == 1923);  // nothing ever rises above the note
    CHECK(Driver::lowestNote(true) == 24);           // wave: note 24 is period 44, LSDj's own resting place
    CHECK(Driver::lowestNote(false) == 36);          // pulse counts twice as fast, so an octave higher
}

TEST_CASE("an envelope's levels come at the rate LSDj gives them", "[driver][zombie]")
{
    // Measured on 9.3.9 (docs/LSDJ_COMMAND_MATRIX.md 6.5) by holding a note and
    // reading the interval between the zombie steps: 6, 11, 17, 22, 28, 34 and
    // 39 pitch clocks for rates 1-7. That is the chip's own rate, one level
    // every `rate / 64` s, and rates 6 and 7 are a sixth apart -- not the equal
    // 36 that LSDJ_PARITY section 7 read off a generated probe save.
    constexpr uint64_t kPitchClock = 11712;
    auto meanStep = [](int rate) {
        Rig r;
        auto& in = r.bank.instruments[0];
        in = Instrument::defaults(InstrumentType::Pulse, "Env");
        in.used = true;
        in.env.mode = EnvMode::Chip;
        r.song.noteSource[0] = tracker::NoteSource::Tracker;
        ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
        NoteEvent e = cellOn(0, 60, 1); e.velSet = false;
        r.block({ e, levelCell(0, 15, rate) }, 512);
        std::vector<uint64_t> at;
        for (int b = 0; b < 400 && at.size() < 12; ++b)
            for (const auto& x : r.block({}, 512))
                if (x.addr == 0xFF12 && x.value == 0x09) at.push_back(x.cycle);
        REQUIRE(at.size() >= 12);
        return double(at[11] - at[1]) / 10.0;
    };
    // One level every rate * 65536 cycles, to within the rounding to a clock.
    for (int rate = 1; rate <= 7; ++rate)
        CHECK(std::abs(meanStep(rate) - double(rate) * 65536.0) < double(kPitchClock));
    // And the two fastest rates are distinct, which is the thing section 7 had
    // wrong and the thing a song using both can hear.
    CHECK(meanStep(7) > meanStep(6) * 1.1);
}

TEST_CASE("the zombie sequence is LSDj's own bytes", "[driver][zombie]")
{
    // One step down is `09 11 18` and one step up is `08`, byte for byte as
    // the ROM writes them, repeated to the target (LSDJ_PARITY 6). Under the
    // APU's own rule the triple lands on v - 1 and the single on v + 1, so the
    // driver's model and the chip agree.
    Rig r;
    Chip chip;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);   // vol 13
    chip.feed(r.block({ Rig::on(0, 69, 100) }, 512));
    auto bytes = [&](int target) {
        const auto w = r.block({ levelCell(0, target) }, 512);
        chip.feed(w);
        REQUIRE(chip.apu.channelVolume(0) == target);
        std::vector<int> out; for (const auto& x : w) if (x.addr == 0xFF12) out.push_back(x.value);
        return out;
    };
    CHECK(bytes(14) == std::vector<int>{ 0x08 });                          // one step up
    CHECK(bytes(13) == std::vector<int>{ 0x09, 0x11, 0x18 });              // one step down
    CHECK(bytes(10) == std::vector<int>{ 0x09, 0x11, 0x18, 0x09, 0x11, 0x18, 0x09, 0x11, 0x18 });
    CHECK(bytes(13) == std::vector<int>{ 0x08, 0x08, 0x08 });              // three up
    // The steps are spaced as the ROM spaces them: sixteen cycles inside a
    // down-triple, a hundred and twelve between them, sixty-eight between ups.
    const auto w = r.block({ levelCell(0, 11) }, 512);
    chip.feed(w);
    std::vector<uint64_t> at; for (const auto& x : w) if (x.addr == 0xFF12) at.push_back(x.cycle);
    REQUIRE(at.size() == 6);
    CHECK(at[1] - at[0] == 16);
    CHECK(at[2] - at[1] == 16);
    CHECK(at[3] - at[0] == 112);
}

/* ============================= shaped envelopes (section 27) ============= */

TEST_CASE("a shaped envelope is one level per tick and never triggers", "[driver][shaped]")
{
    Rig r;
    Chip chip;
    r.tickHz = 100.0;
    auto& i = r.bank.instruments[0];
    i.env.mode = EnvMode::Shaped;
    i.env.attackTicks = 4; i.env.peak = 12; i.env.decayTicks = 4; i.env.sustain = 6; i.env.releaseTicks = 4;
    i.noteOff = NoteOff::Release;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    std::vector<int> levels;
    auto step = [&](std::vector<NoteEvent> ev) {
        const auto w = r.block(std::move(ev), 480);
        chip.feed(w);
        levels.push_back(int(chip.apu.channelVolume(0)));
        return w;
    };
    const auto first = step({ Rig::on(0, 69, 100) });
    CHECK(anyTrigger(first, 0xFF14));                  // the note itself triggers, once
    // Its NRx2 is the envelope's first level with the direction bit up and no
    // rate: a level of zero that still leaves the DAC on, so the writes that
    // follow are zombie writes (sections 26, 27).
    REQUIRE(first.size() > 0);
    bool sawStart = false;
    for (const auto& x : first) if (x.addr == 0xFF12 && x.value == 0x08) sawStart = true;
    CHECK(sawStart);
    for (int k = 0; k < 8; ++k) {
        const auto w = step({});
        CHECK_FALSE(anyTrigger(w, 0xFF14));            // and nothing after it does
    }
    // Attack 0 -> 12 over four ticks, then decay to the sustain of 6, which
    // holds while the note is held (section 27). The level is read at the end
    // of each block, one tick after the note started at silence.
    const std::vector<int> want { 3, 6, 9, 12, 10, 9, 7, 6, 6 };
    CHECK(levels == want);
    // The release starts at the note-off and walks to silence over its ticks:
    // 6 -> 0 in four, linear. The tick of the note-off's own block is the
    // first of them.
    std::vector<int> rel;
    step({ Rig::off(0, 69) });
    rel.push_back(levels.back());
    for (int k = 0; k < 3; ++k) { step({}); rel.push_back(levels.back()); }
    CHECK(rel[0] == 4);
    CHECK(rel[1] == 3);
    CHECK(rel[2] == 1);
    CHECK_FALSE(r.drv.view(0).dacOn);                  // and the note is over
}

TEST_CASE("a shaped envelope's curves are the segment shapes", "[driver][shaped]")
{
    // Section 27: exponential is fast at the start, logarithmic slow at it.
    CHECK(envSegmentLevel(0, 12, 4, 2, EnvCurve::Linear) == 6);
    CHECK(envSegmentLevel(0, 12, 4, 2, EnvCurve::Exponential) == 9);
    CHECK(envSegmentLevel(0, 12, 4, 2, EnvCurve::Logarithmic) == 3);
    CHECK(envSegmentLevel(0, 12, 4, 0, EnvCurve::Exponential) == 0);
    CHECK(envSegmentLevel(0, 12, 4, 4, EnvCurve::Logarithmic) == 12);
    CHECK(envSegmentLevel(12, 0, 4, 4, EnvCurve::Exponential) == 0);
    CHECK(envSegmentLevel(7, 7, 0, 0, EnvCurve::Linear) == 7);       // no ticks: straight there
}

TEST_CASE("a shaped wave instrument uses the four NR32 levels", "[driver][shaped]")
{
    Rig r;
    r.tickHz = 100.0;
    auto& i = r.bank.instruments[6];                   // Triangle bass, a wave instrument
    i.env.mode = EnvMode::Shaped;
    i.env.attackTicks = 0; i.env.peak = 15; i.env.decayTicks = 8; i.env.sustain = 0;
    ChannelParams p; p.instrument = 7; r.drv.setParams(2, p);
    r.block({ Rig::on(2, 48, 100) }, 480);
    std::vector<int> codes;
    for (int k = 0; k < 8; ++k) {
        const auto w = r.block({}, 480);
        CHECK_FALSE(has(w, 0xFF12));                   // the wave channel has no NRx2 at all
        if (const auto* nr32 = last(w, 0xFF1C)) codes.push_back((nr32->value >> 5) & 3);
    }
    REQUIRE(!codes.empty());
    CHECK(codes.front() <= 3);
    CHECK(codes.back() == 0);                          // 100 % down to mute, in four steps
}

TEST_CASE("a table's volume column takes a shaped envelope over", "[driver][shaped]")
{
    // Section 27: the segments left stop until the next plain note-on.
    Rig r;
    Chip chip;
    r.tickHz = 100.0;
    auto& t = r.bank.tables[0]; t.used = true; t.end = TableEnd::Stop;
    t.steps[0].vol = 9;
    auto& i = r.bank.instruments[0];
    i.env.mode = EnvMode::Shaped;
    i.env.attackTicks = 0; i.env.peak = 15; i.env.decayTicks = 8; i.env.sustain = 0;
    i.table = 1;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    chip.feed(r.block({ Rig::on(0, 69, 100) }, 480));
    CHECK(chip.apu.channelVolume(0) == 9);             // the table's row 0, with the note
    for (int k = 0; k < 4; ++k) chip.feed(r.block({}, 480));
    CHECK(chip.apu.channelVolume(0) == 9);             // and the decay never resumes
}

/* ====================== the table's first row (section 31) ============== */

TEST_CASE("a table's first row fires with the note-on", "[driver][table]")
{
    // Section 31: a wave kick whose table drops the pitch must start dropping
    // in the note's own event -- a note that falls just after a tick used to
    // play its raw pitch for up to a tick, which is the beep this fixes.
    Rig r;
    r.tickHz = 140.0 * 24.0 / 60.0;                    // 140 BPM quarters, 24 ticks a beat
    auto& t = r.bank.tables[0]; t.used = true; t.end = TableEnd::Stop;
    t.steps[0].cmd1 = { Cmd::P, 256 - 73, 0, 0 };      // -73, two's complement (section 34)
    t.steps[0].vol = 15;
    auto& i = r.bank.instruments[6];                   // a wave instrument
    i.table = 1;
    ChannelParams p; p.instrument = 7; r.drv.setParams(2, p);
    // The note lands a third of the way into a tick, where the old code left
    // the raw pitch sounding until the next one.
    const uint32_t frames = uint32_t(48000.0 / r.tickHz);
    r.block({}, frames / 3);
    const auto w = r.block({ Rig::on(2, 108, 100) }, frames);
    // Every period write of this tick, in order: the note's own, then the
    // 360 Hz updates the P bend makes.
    std::vector<int> periods;
    for (const auto& x : w) if (x.addr == 0xFF1D) periods.push_back(x.value);
    REQUIRE(periods.size() >= 2);
    CHECK(periods.back() < periods.front());           // the drop starts in the same tick
    CHECK(r.drv.view(2).tableRow == 0);
    CHECK(r.drv.view(2).tableSlot == 1);
}

TEST_CASE("a kick played twice never plays its raw pitch on its own", "[driver][table]")
{
    // The same instrument twice, 429 ms apart (140 BPM), plain and then as an
    // overlapping MIDI note: the drop starts in the note's own tick both
    // times (sections 31 and 8).
    for (bool overlap : { false, true }) {
        Rig r;
        r.tickHz = 140.0 * 24.0 / 60.0;
        auto& t = r.bank.tables[0]; t.used = true; t.end = TableEnd::Stop;
        t.steps[0].cmd1 = { Cmd::P, 256 - 73, 0, 0 };
        auto& i = r.bank.instruments[6];
        i.table = 1;
        ChannelParams p; p.instrument = 7; r.drv.setParams(2, p);
        const uint32_t quarter = uint32_t(48000.0 * 60.0 / 140.0);
        auto hit = [&](std::vector<NoteEvent> ev) {
            const auto w = r.block(std::move(ev), 512);
            std::vector<int> periods;
            for (const auto& x : w) if (x.addr == 0xFF1D) periods.push_back(x.value);
            return periods;
        };
        const auto first = hit({ Rig::on(2, 108, 100) });
        REQUIRE(first.size() >= 2);
        CHECK(first.back() < first.front());
        if (!overlap) r.block({ Rig::off(2, 108) }, quarter - 512);
        else r.block({}, quarter - 512);
        const auto second = hit({ Rig::on(2, 108, 100) });
        REQUIRE(second.size() >= 2);
        CHECK(second.back() < second.front());
        CHECK(r.drv.view(2).tableRow == 0);
    }
}

TEST_CASE("a note-on at the pitch already sounding is never legato", "[driver][notes]")
{
    // Section 31: legato is for moving between pitches; a repeated pitch is a
    // drum hit in succession, and a drum hit is a retrigger.
    Rig r;
    ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);       // a pulse instrument: legato
    r.block({ Rig::on(0, 60, 100) }, 256);
    SECTION("the same pitch retriggers") {
        const auto w = r.block({ Rig::on(0, 60, 100) }, 256);
        CHECK(anyTrigger(w, 0xFF14));
        CHECK(r.report(0)->plain);
    }
    SECTION("a different pitch is still bare") {
        const auto w = r.block({ Rig::on(0, 64, 100) }, 256);
        CHECK_FALSE(anyTrigger(w, 0xFF14));
        CHECK_FALSE(r.report(0)->plain);
    }
}

/* ==================== the table run the view publishes (section 32) ===== */

TEST_CASE("the view publishes the table's row and a run serial", "[driver][table]")
{
    Rig r;
    r.tickHz = 100.0;
    auto& t = r.bank.tables[0]; t.used = true; t.end = TableEnd::Stop;
    for (int k = 0; k < 4; ++k) t.steps[size_t(k)].vol = int8_t(15 - k * 4);
    r.bank.instruments[0].table = 1;
    ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
    CHECK(r.drv.view(0).tableRow == -1);                           // nothing running yet
    CHECK(r.drv.view(0).tableRun == 0);
    r.block({ Rig::on(0, 69, 100) }, 480);
    CHECK(r.drv.view(0).tableSlot == 1);
    CHECK(r.drv.view(0).tableRow == 0);                            // row 0 fired with the note
    const uint16_t run = r.drv.view(0).tableRun;
    CHECK(run == 1);
    r.block({}, 480);
    CHECK(r.drv.view(0).tableRow == 1);
    CHECK(r.drv.view(0).tableRun == run);                          // the same run, one row on
    // A table that ends stops, and the row goes with it: sixteen rows, then
    // TableEnd::Stop.
    for (int k = 0; k < 16; ++k) r.block({}, 480);
    CHECK(r.drv.view(0).tableRow == -1);
    CHECK(r.drv.view(0).tableSlot == 0);
    // An A command starts a run of its own.
    r.block({ cellCmd(0, Command{ Cmd::A, 1, 0, 0 }) }, 480);
    CHECK(r.drv.view(0).tableRun == run + 1);
    CHECK(r.drv.view(0).tableRow == 0);
    // And so does the next plain note.
    r.block({ Rig::off(0, 69) }, 480);
    r.block({ Rig::on(0, 67, 100) }, 480);
    CHECK(r.drv.view(0).tableRun == run + 2);
}

TEST_CASE("all notes off drops a note still waiting for its tick", "[driver][notes]")
{
    // Section 8: a note waiting for the tick, under notes-on-tick, is a
    // delayed start, and an all-notes-off clears those too -- the fuzz found
    // this as a channel left sounding after a panic (section 28).
    Rig r;
    r.tickHz = 20.0;                                   // a tick every 2400 frames
    r.drv.setNotesOnTick(true);
    ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
    // The note-on lands in a block with no tick in it, so it waits.
    r.block({}, 512);                                  // the tick at frame 0 goes by first
    r.block({ Rig::on(0, 69, 100) }, 512);
    CHECK_FALSE(r.drv.view(0).active);
    auto w = r.block({ allOff(0) }, 512);
    // ... and the ticks that follow must not start it after all.
    for (int k = 0; k < 8; ++k) {
        w = r.block({}, 512);
        CHECK_FALSE(anyTrigger(w, 0xFF14));
    }
    CHECK_FALSE(r.drv.view(0).active);
    CHECK_FALSE(r.drv.view(0).dacOn);
}

// --- sections 45 to 49: noise tables, the TBL span, the chain's and the instrument's transposes

namespace {
uint8_t nr43For(int note) { uint8_t s = 0, d = 0; Driver::noisePairForNote(note, s, d); return uint8_t((s << 4) | d); }
}

TEST_CASE("a noise instrument takes its table's transpose column through the map", "[driver][noise]")
{
    Rig r;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    { ChannelParams p; p.instrument = 21; for (int ch = 0; ch < 4; ++ch) r.drv.setParams(ch, p); }
    auto& t = r.bank.tables[9]; t = Table{}; t.used = true;            // slot 10: -24, then +12, then the note
    t.steps[0].hasTranspose = true; t.steps[0].transpose = -24;
    t.steps[1].hasTranspose = true; t.steps[1].transpose = 12;
    auto& i = r.bank.instruments[20]; i = Instrument::defaults(InstrumentType::Noise, "hat"); i.table = 10;   // slot 21
    // The note-on carries row 0's transpose in its own NR43 (section 31).
    auto w = r.block({ cellOn(3, 72, 21) }, 200);
    REQUIRE(last(w, 0xFF22) != nullptr);
    CHECK(last(w, 0xFF22)->value == nr43For(48));
    CHECK(r.drv.view(3).tableSlot == 10);
    // Row 1 at the next tick: a new NR43, no trigger.
    w = r.block({}, 200);
    REQUIRE(last(w, 0xFF22) != nullptr);
    CHECK(last(w, 0xFF22)->value == nr43For(84));
    CHECK_FALSE(has(w, 0xFF23));
    // Row 2 is blank: the note itself.
    w = r.block({}, 200);
    REQUIRE(last(w, 0xFF22) != nullptr);
    CHECK(last(w, 0xFF22)->value == nr43For(72));
    CHECK(r.drv.view(3).tableRow == 2);
    // The instrument's Transpose flag gates the song's and the chain's offsets,
    // never the table's own column (section 61), so this still moves.
    i.transpose = false;
    w = r.block({ cellOn(3, 72, 21) }, 200);
    CHECK(last(w, 0xFF22)->value == nr43For(48));
}

TEST_CASE("E re-attacks the note when the instrument asks for it", "[driver][commands]")
{
    // Section 59: LSDj's rule before 8.8. Off by default, so nothing that
    // exists changes; on, an E sets the level and hits the note again.
    Rig r;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    { ChannelParams p; p.instrument = 21; for (int ch = 0; ch < 4; ++ch) r.drv.setParams(ch, p); }
    auto& i = r.bank.instruments[20]; i = Instrument::defaults(InstrumentType::Pulse, "lead");
    r.block({ cellOn(0, 60, 21) }, 200);
    auto w = r.block({ cellCmd(0, Command{ Cmd::E, 8, 0, 0 }) }, 200);
    CHECK(has(w, 0xFF12));                                  // the level moved
    CHECK_FALSE(anyTrigger(w, 0xFF14));                     // and never triggered (section 26)
    i.envRetrig = true;
    r.block({ cellOn(0, 60, 21) }, 200);
    w = r.block({ cellCmd(0, Command{ Cmd::E, 8, 0, 0 }) }, 200);
    CHECK(has(w, 0xFF12));
    CHECK(anyTrigger(w, 0xFF14));                           // now it hits the note again
    // A table's E row does the same, and it is the noise channel's drum stutter.
    auto& t = r.bank.tables[9]; t = Table{}; t.used = true; t.end = TableEnd::Stop;
    t.steps[0].cmd1 = Command{ Cmd::E, 5, 0, 0 };
    t.steps[1].cmd1 = Command{ Cmd::E, 3, 0, 0 };
    auto& n = r.bank.instruments[21]; n = Instrument::defaults(InstrumentType::Noise, "drum");
    n.table = 10; n.envRetrig = true;
    { ChannelParams p; p.instrument = 22; r.drv.setParams(3, p); }
    r.block({ cellOn(3, 60, 22) }, 200);
    w = r.block({}, 200);                                   // row 1's E, a tick later
    CHECK(anyTrigger(w, 0xFF23));
    // A table's volume column does it too, which is the old drum stutter.
    t.steps[0].cmd1 = {}; t.steps[1].cmd1 = {};
    t.steps[0].vol = 10; t.steps[1].vol = 6;
    r.block({ cellOn(3, 60, 22) }, 200);
    w = r.block({}, 200);                                   // row 1's volume, a tick later
    CHECK(has(w, 0xFF21));
    CHECK(anyTrigger(w, 0xFF23));
    // Never on the wave channel: its level is NR32 and needs no trigger.
    auto& wv = r.bank.instruments[22]; wv = Instrument::defaults(InstrumentType::Wave, "wave");
    wv.envRetrig = true;
    { ChannelParams p; p.instrument = 23; r.drv.setParams(2, p); }
    r.block({ cellOn(2, 60, 23) }, 200);
    w = r.block({ cellCmd(2, Command{ Cmd::E, 2, 0, 0 }) }, 200);
    CHECK(has(w, 0xFF1C));                                  // NR32 moved
    CHECK_FALSE(anyTrigger(w, 0xFF1E));
}

TEST_CASE("S on the noise channel transposes through the map and adds up", "[driver][noise]")
{
    // Section 55: S xy adds int8(xy) semitones to the noise note, each S on
    // top of the last, NR43 rewritten without a trigger; a note-on clears it.
    Rig r;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    { ChannelParams p; p.instrument = 21; for (int ch = 0; ch < 4; ++ch) r.drv.setParams(ch, p); }
    auto& i = r.bank.instruments[20]; i = Instrument::defaults(InstrumentType::Noise, "snare");   // slot 21
    auto w = r.block({ cellOn(3, 72, 21) }, 200);
    REQUIRE(last(w, 0xFF22) != nullptr);
    CHECK(last(w, 0xFF22)->value == nr43For(72));
    w = r.block({ cellCmd(3, Command{ Cmd::S, 0, 1, 0 }) }, 200);        // S01: one up
    REQUIRE(last(w, 0xFF22) != nullptr);
    CHECK(last(w, 0xFF22)->value == nr43For(73));
    CHECK_FALSE(has(w, 0xFF23));
    w = r.block({ cellCmd(3, Command{ Cmd::S, 0xF, 0xE, 0 }) }, 200);    // SFE: two down, on top of the one up
    REQUIRE(last(w, 0xFF22) != nullptr);
    CHECK(last(w, 0xFF22)->value == nr43For(71));
    w = r.block({ cellCmd(3, Command{ Cmd::S, 1, 0, 0 }) }, 200);        // S10: sixteen up
    CHECK(last(w, 0xFF22)->value == nr43For(87));
    w = r.block({ cellOn(3, 72, 21) }, 200);                             // the note-on starts over
    CHECK(last(w, 0xFF22)->value == nr43For(72));
    // An S in the note's own cell transposes the note-on itself (the write is
    // the tick's); the next cell's S adds to it.
    auto e = cellOn(3, 48, 21); e.cmd1 = Command{ Cmd::S, 0xF, 0xA, 0 };  // SFA: six down
    w = r.block({ e }, 200);
    REQUIRE(last(w, 0xFF22) != nullptr);
    CHECK(last(w, 0xFF22)->value == nr43For(42));
    w = r.block({ cellCmd(3, Command{ Cmd::S, 0xF, 0xA, 0 }) }, 200);
    REQUIRE(last(w, 0xFF22) != nullptr);
    CHECK(last(w, 0xFF22)->value == nr43For(36));
    // A table's S rows do the same: row 0 in the note-on itself, row 1 a tick later, adding up.
    auto& t = r.bank.tables[9]; t = Table{}; t.used = true; t.end = TableEnd::Stop;   // slot 10
    t.steps[0].cmd1 = Command{ Cmd::S, 0xF, 0xE, 0 };                                   // two down
    t.steps[1].cmd1 = Command{ Cmd::S, 0, 5, 0 };                                       // five up, on top
    i.table = 10;
    w = r.block({ cellOn(3, 72, 21) }, 200);
    REQUIRE(last(w, 0xFF22) != nullptr);
    CHECK(last(w, 0xFF22)->value == nr43For(70));
    w = r.block({}, 200);
    REQUIRE(last(w, 0xFF22) != nullptr);
    CHECK(last(w, 0xFF22)->value == nr43For(75));
}

TEST_CASE("a TBL column lasts until a cell names an instrument", "[driver][commands]")
{
    Rig r;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    { ChannelParams p; p.instrument = 21; for (int ch = 0; ch < 4; ++ch) r.drv.setParams(ch, p); }
    for (int slot : { 10, 11 }) { auto& t = r.bank.tables[size_t(slot - 1)]; t = Table{}; t.used = true; t.steps[0].vol = 12; }
    auto& a = r.bank.instruments[20]; a = Instrument::defaults(InstrumentType::Pulse, "with"); a.table = 10;   // slot 21
    auto& b = r.bank.instruments[21]; b = Instrument::defaults(InstrumentType::Pulse, "without");             // slot 22, no table
    auto e = cellOn(0, 60, 22); e.table = 11;
    r.block({ e }, 200);
    CHECK(r.drv.view(0).tableSlot == 11);                 // the cell's TBL
    r.block({ cellOn(0, 62, 0) }, 200);
    CHECK(r.drv.view(0).tableSlot == 11);                 // a bare cell leaves it (section 46)
    r.block({ cellOn(0, 64, 22) }, 200);
    CHECK(r.drv.view(0).tableSlot == 0);                  // an instrument column with a blank TBL: the instrument's own, none
    r.block({ cellOn(0, 65, 21) }, 200);
    CHECK(r.drv.view(0).tableSlot == 10);                 // ... or its own table
}

TEST_CASE("the chain row's transpose moves the note when the instrument's Transpose is on", "[driver][notes]")
{
    Rig r;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    { ChannelParams p; p.instrument = 21; for (int ch = 0; ch < 4; ++ch) r.drv.setParams(ch, p); }
    auto& on = r.bank.instruments[20]; on = Instrument::defaults(InstrumentType::Pulse, "lead");                        // slot 21
    auto& off = r.bank.instruments[21]; off = Instrument::defaults(InstrumentType::Pulse, "drum"); off.transpose = false;  // slot 22
    const int plain72 = Driver::periodForNote(72.0, false), plain60 = Driver::periodForNote(60.0, false);
    auto e = cellOn(0, 60, 21); e.transpose = 12;
    r.block({ e }, 200);
    CHECK(r.drv.view(0).period == plain72);
    CHECK(r.drv.view(0).note == 60);                      // the note stays what the cell said
    e = cellOn(0, 60, 22); e.transpose = 12;
    r.block({ e }, 200);
    CHECK(r.drv.view(0).period == plain60);               // Transpose off: a drum keeps its pitch
    // Noise goes through the map transposed.
    auto& n = r.bank.instruments[22]; n = Instrument::defaults(InstrumentType::Noise, "hat");    // slot 23
    e = cellOn(3, 60, 23); e.transpose = -12;
    auto w = r.block({ e }, 200);
    REQUIRE(last(w, 0xFF22) != nullptr);
    CHECK(last(w, 0xFF22)->value == nr43For(48));
}

TEST_CASE("an instrument's PU2 transpose applies on the second pulse only, and F sets it", "[driver][commands][pitch]")
{
    Rig r;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    { ChannelParams p; p.instrument = 21; for (int ch = 0; ch < 4; ++ch) r.drv.setParams(ch, p); }
    auto& i = r.bank.instruments[20]; i = Instrument::defaults(InstrumentType::Pulse, "phase"); i.pu2Transpose = 12;   // slot 21
    const int plain72 = Driver::periodForNote(72.0, false), plain60 = Driver::periodForNote(60.0, false);
    r.block({ cellOn(1, 60, 21) }, 200);
    CHECK(r.drv.view(1).period == plain72);
    r.block({ cellOn(0, 60, 21) }, 200);
    CHECK(r.drv.view(0).period == plain60);               // PU1: the instrument's own pitch
    // F on PU2 *is* the offset for the note in progress, two's complement: it
    // replaces the instrument's, as LSDj's TSP does.
    auto e = cellOn(1, 60, 21); e.cmd1 = { Cmd::F, 0xF4, 0, 0 };   // -12
    r.block({ e }, 200);
    CHECK(r.drv.view(1).period == Driver::periodForNote(48.0, false));
    // A plain note-on puts the instrument's own back.
    r.block({ cellOn(1, 60, 21) }, 200);
    CHECK(r.drv.view(1).period == plain72);
    // F on PU1 stays inert.
    e = cellOn(0, 60, 21); e.cmd1 = { Cmd::F, 12, 0, 0 };
    r.block({ e }, 200);
    CHECK(r.drv.view(0).period == plain60);
}

TEST_CASE("a shaped envelope can start above silence and fade past its sustain", "[driver][shaped]")
{
    // Section 51: LSDj's three stages -- a1 to a2, a2 to a3, a3 to silence --
    // are Start, Attack to Peak, Decay to Sustain and the Fade to a level.
    Rig r;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    { ChannelParams p; p.instrument = 21; for (int ch = 0; ch < 4; ++ch) r.drv.setParams(ch, p); }
    auto& i = r.bank.instruments[20]; i = Instrument::defaults(InstrumentType::Pulse, "stages");   // slot 21
    i.env.mode = EnvMode::Shaped;
    i.env.start = 10; i.env.attackTicks = 5; i.env.peak = 5;          // A to 5, one level a tick
    i.env.decayTicks = 8; i.env.sustain = 13;                          // 5 up to D
    i.env.fadeTicks = 13; i.env.fadeTo = 0;                            // D down to silence, then held
    auto w = r.block({ cellOn(0, 60, 21) }, 200);
    bool first = true;
    for (const auto& x : w) if (x.addr == 0xFF12 && first) { CHECK((x.value >> 4) == 10); first = false; }   // the start level, not the peak
    CHECK_FALSE(first);
    // The block's own tick took the first step; each block after it is one more.
    std::vector<int> levels;
    for (int t = 0; t < 30; ++t) { r.block({}, 200); levels.push_back(int(r.drv.view(0).envVol)); }
    CHECK(levels[3] == 5);                                             // the attack reached the peak at tick 5
    CHECK(levels[11] == 13);                                           // the decay reached the sustain at tick 13
    CHECK(levels[24] == 0);                                            // the fade reached its level at tick 26
    CHECK(levels[29] == 0);                                            // ... and holds there
    // Without a fade the sustain holds, as it always did.
    i.env.fadeTicks = 0;
    r.block({ cellOn(0, 60, 21) }, 200);
    for (int t = 0; t < 30; ++t) r.block({}, 200);
    CHECK(r.drv.view(0).envVol == 13);
}

TEST_CASE("a table's volume lane keeps its own time", "[driver][commands]")
{
    // Section 64: the transpose column steps a row a tick while the volume
    // column holds each of its rows for its own LEN.
    Rig r;
    r.tickHz = 100.0;
    Table t; t.used = true; t.name = "Lanes";
    for (int i = 0; i < 4; ++i) { t.steps[size_t(i)].hasTranspose = true; t.steps[size_t(i)].transpose = int8_t(i * 5); }
    t.steps[0].vol = 12; t.steps[0].volTicks = 4;
    t.steps[1].vol = 4;  t.steps[1].volTicks = 4;
    t.end = TableEnd::Stop;
    r.bank.tables[7] = t;
    r.bank.instruments[0].table = 8; r.bank.instruments[0].vib.depth = 0;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    r.block({ Rig::on(0, 60, 100) }, 480);
    std::vector<int> notes, vols;
    for (int i = 0; i < 7; ++i) { r.block({}, 480); notes.push_back(int(r.drv.view(0).period)); vols.push_back(int(r.drv.view(0).envVol)); }
    // The transpose walks every tick: 60, 65, 70, 75 and then the table stops.
    CHECK(notes[0] == note(65)); CHECK(notes[1] == note(70)); CHECK(notes[2] == note(75));
    // The volume lane is still on its first row through all of that.
    CHECK(vols[0] == 12); CHECK(vols[1] == 12); CHECK(vols[2] == 12);
    CHECK(vols[3] == 4);                                  // its second row, four ticks in
    r.bank.instruments[0].table = 0;
}

TEST_CASE("an H in the second command column loops that column alone", "[driver][commands]")
{
    // Section 64: the transpose column and CMD 1 run to the end of the table
    // while CMD 2 replays its own first two rows.
    Rig r;
    r.tickHz = 100.0;
    Table t; t.used = true; t.name = "Two lanes";
    for (int i = 0; i < 6; ++i) { t.steps[size_t(i)].hasTranspose = true; t.steps[size_t(i)].transpose = int8_t(i); }
    t.steps[0].cmd2 = { Cmd::O, int16_t(bank::Pan::Left), 0, 0 };
    t.steps[1].cmd2 = { Cmd::H, 0, 0, 0 };                // hop this column back to its row 0, for ever
    t.end = TableEnd::Stop;
    r.bank.tables[7] = t;
    r.bank.instruments[0].table = 8; r.bank.instruments[0].vib.depth = 0;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    r.block({ Rig::on(0, 60, 100) }, 480);
    std::vector<int> notes;
    for (int i = 0; i < 5; ++i) { r.block({}, 480); notes.push_back(int(r.drv.view(0).period)); }
    CHECK(notes[0] == note(61)); CHECK(notes[1] == note(62));     // the transpose column ran past the hop
    CHECK(notes[2] == note(63)); CHECK(notes[3] == note(64));
    CHECK(r.drv.view(0).pan == uint8_t(bank::Pan::Left));         // and CMD 2 kept setting its own row 0
    r.bank.instruments[0].table = 0;
}

TEST_CASE("the volume lane hops on its own", "[driver][commands]")
{
    // Section 64: volHop moves the volume lane and leaves the transpose alone.
    Rig r;
    r.tickHz = 100.0;
    Table t; t.used = true; t.name = "Vol hop";
    for (int i = 0; i < 6; ++i) { t.steps[size_t(i)].hasTranspose = true; t.steps[size_t(i)].transpose = int8_t(i); }
    t.steps[0].vol = 15; t.steps[0].volTicks = 1;
    t.steps[1].vol = 7;  t.steps[1].volTicks = 1;
    t.steps[2].volHop = 1;                                 // back to row 1, for ever
    t.end = TableEnd::Stop;
    r.bank.tables[7] = t;
    r.bank.instruments[0].table = 8; r.bank.instruments[0].vib.depth = 0;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    r.block({ Rig::on(0, 60, 100) }, 480);
    std::vector<int> notes, vols;
    for (int i = 0; i < 6; ++i) { r.block({}, 480); notes.push_back(int(r.drv.view(0).period)); vols.push_back(int(r.drv.view(0).envVol)); }
    CHECK(notes[0] == note(61)); CHECK(notes[3] == note(64));     // the transpose column runs on
    CHECK(vols[0] == 7);                                          // row 1
    CHECK(vols[2] == 7);                                          // and row 1 again after the hop
    r.bank.instruments[0].table = 0;
}

TEST_CASE("a wave instrument's frame run takes its length and loops from its own step", "[driver][wave]")
{
    // Section 65: four frames of a sixteen frame wave are 0, 5, 10, 15, and the
    // loop returns to the run's own step.
    uint8_t run[16];
    CHECK(bank::waveRun(16, 4, run) == 4);
    CHECK(int(run[0]) == 0); CHECK(int(run[1]) == 5); CHECK(int(run[2]) == 10); CHECK(int(run[3]) == 15);
    CHECK(bank::waveRun(16, 8, run) == 8);
    CHECK(int(run[1]) == 2); CHECK(int(run[3]) == 6); CHECK(int(run[4]) == 9); CHECK(int(run[7]) == 15);
    CHECK(bank::waveRun(16, 1, run) == 1); CHECK(int(run[0]) == 0);
    CHECK(bank::waveRun(16, 0, run) == 16); CHECK(int(run[15]) == 15);

    Rig r;
    r.tickHz = 100.0;
    auto& w = r.bank.waves[0];
    w.used = true; w.frames.clear();
    for (int f = 0; f < 16; ++f) { bank::Frame fr; fr.s.fill(uint8_t(f)); w.frames.push_back(fr); }
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Wave, "Run");
    i.used = true; i.wave = 1; i.frameLength = 4; i.frameAdvance = 1; i.frameLoop = bank::FrameLoop::Loop; i.frameLoopStep = 2;
    ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(2, p);
    r.block({ Rig::on(2, 60, 100) }, 480);
    std::vector<int> frames{ int(r.drv.view(2).frame) };
    for (int k = 0; k < 6; ++k) { r.block({}, 480); frames.push_back(int(r.drv.view(2).frame)); }
    // view().frame is the frame plus one. The run walks its four steps once --
    // frames 0, 5, 10, 15 -- and then repeats from step 2.
    CHECK(frames[0] == 5 + 1); CHECK(frames[1] == 10 + 1); CHECK(frames[2] == 15 + 1);
    CHECK(frames[3] == 10 + 1); CHECK(frames[4] == 15 + 1); CHECK(frames[5] == 10 + 1);
}

TEST_CASE("PingPong turns at the run's loop step, not at its first", "[driver][wave]")
{
    // Section 65: LSDj bounces between LOOP POS and the run's end.
    Rig r;
    r.tickHz = 100.0;
    auto& w = r.bank.waves[0];
    w.used = true; w.frames.clear();
    for (int f = 0; f < 16; ++f) { bank::Frame fr; fr.s.fill(uint8_t(f)); w.frames.push_back(fr); }
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Wave, "Bounce");
    i.used = true; i.wave = 1; i.frameLength = 4; i.frameAdvance = 1;   // the run is 0, 5, 10, 15
    i.frameLoop = bank::FrameLoop::PingPong; i.frameLoopStep = 1;       // bounce over 5, 10, 15
    ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(2, p);
    r.block({ Rig::on(2, 60, 100) }, 480);
    std::vector<int> f;
    for (int k = 0; k < 24; ++k) { r.block({}, 480); f.push_back(int(r.drv.view(2).frame) - 1); }
    // It bounces over the run's last three steps and never returns to frame 0,
    // which is the run's first: the turn is at the loop step.
    for (int v : f) { CHECK(v != 0); CHECK((v == 5 || v == 10 || v == 15)); }
    CHECK(std::find(f.begin(), f.end(), 15) != f.end());   // it does reach the run's end
    CHECK(std::find(f.begin(), f.end(), 5) != f.end());    // and comes back to the loop step
}

TEST_CASE("F names a wave frame the run skips", "[driver][wave]")
{
    // Section 65: measured on 8.4.4, F loads the frame it names whether or not
    // the instrument's LENGTH leaves that frame in the run.
    Rig r;
    r.tickHz = 100.0;
    auto& w = r.bank.waves[0];
    w.used = true; w.frames.clear();
    for (int f = 0; f < 16; ++f) { bank::Frame fr; fr.s.fill(uint8_t(f)); w.frames.push_back(fr); }
    Table t; t.used = true; t.name = "Frame";
    t.steps[1].cmd1 = { Cmd::F, 7, 0, 0 };
    t.end = TableEnd::Stop;
    r.bank.tables[7] = t;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Wave, "Manual");
    i.used = true; i.wave = 1; i.frameLength = 4; i.frameAdvance = 0; i.table = 8;   // the run is 0, 5, 10, 15
    ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(2, p);
    r.block({ Rig::on(2, 60, 100) }, 480);
    CHECK(int(r.drv.view(2).frame) == 0 + 1);             // the run's first step
    std::vector<int> seen;
    for (int k = 0; k < 4; ++k) { r.block({}, 480); seen.push_back(int(r.drv.view(2).frame)); }
    CHECK(std::find(seen.begin(), seen.end(), 6 + 1) != seen.end());   // frame 6, which that run skips
}

TEST_CASE("S and P on noise work on NR43 in the Register domain", "[driver][noise]")
{
    // Section 66, measured on 8.4.4: each nibble less the matching nibble of the
    // value, modulo sixteen, no borrow. S does it once, P every tick.
    CHECK(int(bank::noiseNibbleSub(0x10, 0x11)) == 0x0F);
    CHECK(int(bank::noiseNibbleSub(0x0F, 0x11)) == 0xFE);
    CHECK(int(bank::noiseNibbleSub(0x10, 0x0F)) == 0x11);
    CHECK(int(bank::noiseNibbleSub(0x10, 0xFF)) == 0x21);

    const auto nr43 = [](Rig& r) { return int(r.drv.view(3).regs[3]); };
    {   // S: three rows of a table, each taking 0x11 off
        Rig r;
        r.tickHz = 100.0;
        Table t; t.used = true; t.name = "S";
        for (int k = 1; k <= 3; ++k) t.steps[size_t(k)].cmd1 = { Cmd::S, 1, 1, 0 };
        t.end = TableEnd::Stop;
        r.bank.tables[7] = t;
        auto& i = r.bank.instruments[0];
        i = bank::Instrument::defaults(bank::InstrumentType::Noise, "Reg");
        i.used = true; i.noiseDomain = bank::NoiseSweepDomain::Register; i.table = 8;
        i.noiseManual = true; i.noiseShift = 1; i.noiseDivisor = 0; i.lfsr7 = false;   // NR43 = 0x10
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(3, p);
        r.block({ Rig::on(3, 60, 100) }, 480);
        std::vector<int> seen{ nr43(r) };
        for (int k = 0; k < 6; ++k) { r.block({}, 480); seen.push_back(nr43(r)); }
        INFO("NR43: " << std::hex << seen[0] << " " << seen[1] << " " << seen[2] << " " << seen[3] << " " << seen[4] << " " << seen[5] << " " << seen[6]);
        // The row 0 tick fires with the note-on, so the S rows land next.
        CHECK(std::find(seen.begin(), seen.end(), 0x0F) != seen.end());
        CHECK(std::find(seen.begin(), seen.end(), 0xFE) != seen.end());
        CHECK(std::find(seen.begin(), seen.end(), 0xED) != seen.end());
    }
    {   // P: the same subtraction every tick, and it keeps going
        Rig r;
        r.tickHz = 100.0;
        Table t; t.used = true; t.name = "P";
        t.steps[1].cmd1 = { Cmd::P, 0x01, 0, 0 };
        t.end = TableEnd::Stop;
        r.bank.tables[7] = t;
        auto& i = r.bank.instruments[0];
        i = bank::Instrument::defaults(bank::InstrumentType::Noise, "Reg");
        i.used = true; i.noiseDomain = bank::NoiseSweepDomain::Register; i.table = 8;
        i.noiseManual = true; i.noiseShift = 1; i.noiseDivisor = 0; i.lfsr7 = false;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(3, p);
        r.block({ Rig::on(3, 60, 100) }, 480);
        std::vector<int> seen{ nr43(r) };
        for (int k = 0; k < 6; ++k) { r.block({}, 480); seen.push_back(nr43(r)); }
        INFO("NR43: " << std::hex << seen[0] << " " << seen[1] << " " << seen[2] << " " << seen[3] << " " << seen[4] << " " << seen[5] << " " << seen[6]);
        CHECK(std::find(seen.begin(), seen.end(), 0x1F) != seen.end());
        CHECK(std::find(seen.begin(), seen.end(), 0x1E) != seen.end());
        CHECK(std::find(seen.begin(), seen.end(), 0x1D) != seen.end());
    }
    {   // Notes is what it always was: S adds semitones, and P bends the note
        Rig r;
        r.tickHz = 100.0;
        auto& i = r.bank.instruments[0];
        i = bank::Instrument::defaults(bank::InstrumentType::Noise, "Notes");
        i.used = true;                                     // Notes by default
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(3, p);
        r.block({ Rig::on(3, 60, 100) }, 480);
        const int base = nr43(r);
        std::vector<int> seen;
        for (int k = 0; k < 4; ++k) { r.block({}, 480); seen.push_back(nr43(r)); }
        for (int v : seen) CHECK(v == base);               // nothing moves it without a command
    }
}
