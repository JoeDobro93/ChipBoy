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

TEST_CASE("a kit writes a frame every instant on both consoles, through the ROM's sequence", "[driver][kit]")
{
    // Section 172: the note-on writes NR32 and NR33; from the next instant on
    // the mixer writes sixteen bytes an instant -- pan bits cleared, DAC off,
    // the bytes, DAC on, the $7E0 pre-trigger, the pan, the period -- and the
    // DAC goes off at the instant after the sample's last frame.
    for (const auto model : { Console::DMG, Console::CGB }) {
        Rig r(model);
        ChannelParams p; p.instrument = 16; r.drv.setParams(2, p);   // Kit 1
        auto w = r.block({ Rig::on(2, 36, 100) }, 4800);              // 100 ms
        int frames = 0, triggers = 0, ram = 0;
        for (size_t k = 0; k < w.size(); ++k) {
            const auto& x = w[k];
            if (x.addr == 0xFF1A && x.value == 0x00) {
                ++frames;
                REQUIRE(k + 21 < w.size());
                CHECK(w[k - 1].addr == 0xFF25);                                   // the pan, wave bits cleared
                CHECK((w[k - 1].value & 0x44) == 0);
                for (int i = 0; i < 16; ++i) CHECK(w[k + 1 + size_t(i)].addr == 0xFF30 + i);
                CHECK(w[k + 17].addr == 0xFF1A); CHECK(w[k + 17].value == 0x80);
                CHECK(w[k + 18].addr == 0xFF1D); CHECK(w[k + 18].value == 0xE0);
                CHECK(w[k + 19].addr == 0xFF1E); CHECK(w[k + 19].value == 0x87);
                CHECK(w[k + 20].addr == 0xFF25);
                CHECK(w[k + 21].addr == 0xFF1D);
            }
            if (x.addr == 0xFF1E && (x.value & 0x80)) ++triggers;
            if (x.addr >= 0xFF30 && x.addr <= 0xFF3F) ++ram;
        }
        // 358 instants a second: about 36 frames in 100 ms, one trigger each.
        CHECK(frames >= 33); CHECK(frames <= 38);
        CHECK(triggers == frames);
        CHECK(ram == 16 * frames);
        CHECK(r.drv.view(2).active);                // the kick is 0.3 s long: still playing
        std::vector<RegWrite> rest;
        for (int i = 0; i < 3; ++i) { w = r.block({}, 4800); rest.insert(rest.end(), w.begin(), w.end()); }
        CHECK_FALSE(r.drv.view(2).dacOn);          // ended: one-shot, the DAC off; the note stays for a roll or a kit F (section 186)
        CHECK(has(rest, 0xFF1A, 0x00));
    }
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
    SECTION("E on the wave channel is its two-bit level, from y") {
        // Section 79: LSDj reads NR32's two bits from the command's **low**
        // nibble, and x does nothing. Measured on 9.3.9: E01 is 25 %, E02 50 %,
        // E03 100 %, and E10 / EF0 are all mute.
        Rig r;
        ChannelParams p; p.instrument = 7; p.cmd[0] = { Cmd::E, 0, 1, 0 }; r.drv.setParams(2, p);
        auto w = r.block({ Rig::on(2, 48, 100) }, 512);
        CHECK(last(w, 0xFF1C)->value == 0x60);          // NR32 code for 25 %
        Rig r2;
        ChannelParams q; q.instrument = 7; q.cmd[0] = { Cmd::E, 15, 0, 0 }; r2.drv.setParams(2, q);
        w = r2.block({ Rig::on(2, 48, 100) }, 512);
        CHECK(last(w, 0xFF1C)->value == 0x00);          // x alone says nothing: mute
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
    SECTION("S adds each nibble into PU1's running sweep byte") {
        // Section 72. The channel keeps the byte inverted, seeded from the
        // instrument; S adds x to its high nibble and y to its low, and NR10 is
        // the complement. From a sweep-00 instrument the first S is the
        // published formula, ((-x) & 15) << 4 | ((-y) & 15).
        Rig r;
        ChannelParams p; p.instrument = 1; p.cmd[0] = { Cmd::S, 2, 3, 0 }; r.drv.setParams(0, p);
        auto w = r.block({ Rig::on(0, 69, 100) }, 512);
        REQUIRE(last(w, 0xFF10) != nullptr);
        CHECK(last(w, 0xFF10)->value == 0xED);          // S23 -> ED, measured on 9.3.9
        // A table stepping S23 on four rows compounds: ED CA A7 84.
        Rig r2;
        auto& t = r2.bank.tables[0];
        t.used = true; t.end = TableEnd::Stop;
        for (int i = 0; i < 4; ++i) t.steps[i].cmd1 = { Cmd::S, 2, 3, 0 };
        ChannelParams q; q.instrument = 1; q.table = 1; r2.drv.setParams(0, q);
        w = r2.block({ Rig::on(0, 69, 100) }, 4096);
        std::vector<uint8_t> nr10;
        for (const auto& x : w) if (x.addr == 0xFF10) nr10.push_back(x.value);
        if (!nr10.empty() && nr10[0] == 0x00) nr10.erase(nr10.begin());   // section 180: the trigger's own NR10, the instrument's; row 0's S follows it
        INFO("NR10: " << [&]{ std::string o; for (auto x : nr10) { char b[8]; std::snprintf(b, sizeof b, "%02X ", x); o += b; } return o; }());
        REQUIRE(nr10.size() >= 4);
        CHECK(nr10[0] == 0xED); CHECK(nr10[1] == 0xCA);
        CHECK(nr10[2] == 0xA7); CHECK(nr10[3] == 0x84);
        // The y = 0 cases separate the per-nibble law from a whole-byte
        // negation: S20 is E0, not F0.
        Rig r3;
        ChannelParams u; u.instrument = 1; u.cmd[0] = { Cmd::S, 2, 0, 0 }; r3.drv.setParams(0, u);
        w = r3.block({ Rig::on(0, 69, 100) }, 512);
        REQUIRE(last(w, 0xFF10) != nullptr);
        CHECK(last(w, 0xFF10)->value == 0xE0);
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

TEST_CASE("a STEP table resumes at the row after its A on the next note, and the A's table is gone", "[driver][commands][table][rom942]")
{
    // Section 179 (STEP_A2): table 0 = A01 / W01 / W02, table 1 = W03 on
    // row 0 and W00 on row 2. Note 1 fires the A; table 1 runs on the ticks
    // after it. Note 2 plays row 1 of table 0 -- duty 1 -- and nothing of
    // table 1 follows it.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[0] = tracker::NoteSource::Tracker;
    Table t0; t0.used = true; t0.steps[0].cmd1 = { Cmd::A, 2, 0, 0 }; t0.steps[1].cmd1 = { Cmd::W, 1, 0, 0 }; t0.steps[2].cmd1 = { Cmd::W, 2, 0, 0 };
    Table t1; t1.used = true; t1.steps[0].cmd1 = { Cmd::W, 3, 0, 0 }; t1.steps[2].cmd1 = { Cmd::W, 0, 0, 0 };
    r.bank.tables[0] = t0; r.bank.tables[1] = t1;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Step");
    i.used = true; i.table = 1; i.tableMode = bank::TableMode::Step; i.duty = 2;
    ChannelParams p; p.instrument = 2; r.drv.setParams(0, p);
    const auto duties = [](const std::vector<RegWrite>& w) { std::vector<int> d; for (const auto& x : w) if (x.addr == 0xFF11) d.push_back(x.value >> 6); return d; };
    std::vector<RegWrite> w = r.block({ cellOn(0, 60, 2) }, 480);
    for (int k = 0; k < 5; ++k) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
    CHECK(duties(w) == std::vector<int>{ 2, 3, 0 });          // the note's own duty, then table 1's rows on the ticks
    w = r.block({ cellOn(0, 60, 2) }, 480);
    for (int k = 0; k < 5; ++k) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
    CHECK(duties(w) == std::vector<int>{ 2, 1 });             // the trigger, then row 1 of the instrument's table (section 180); table 1 no more
    w = r.block({ cellOn(0, 60, 2) }, 480);
    for (int k = 0; k < 5; ++k) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
    CHECK(duties(w).front() == 2);                            // the trigger; row 2's W02 is the duty it already has
}

TEST_CASE("a periodic R replays the instrument's table from row 0 and leaves the STEP position alone", "[driver][commands][table][rom942]")
{
    // Section 182 (Rtick_Astop, Rstep_Astop): table 0 = A02 on row 0 and W01 on
    // row 1, table 2 = W03 / W00 / A20. A STEP instrument with `R 03`: the
    // note plays row 0 (A02: duties 3, 0 on the ticks after), every retrigger
    // starts table 0 over (A02 again: 3, 0 again), and the next note plays
    // row 1 -- duty 1 -- as if no retrigger had run a row.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[0] = tracker::NoteSource::Tracker;
    Table t0; t0.used = true; t0.steps[0].cmd1 = { Cmd::A, 2, 0, 0 }; t0.steps[1].cmd1 = { Cmd::W, 1, 0, 0 };
    Table t1; t1.used = true; t1.steps[0].cmd1 = { Cmd::W, 3, 0, 0 }; t1.steps[1].cmd1 = { Cmd::W, 0, 0, 0 }; t1.steps[2].cmd1 = { Cmd::A, 0x20, 0, 0 };
    r.bank.tables[0] = t0; r.bank.tables[1] = t1;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Roll");
    i.used = true; i.table = 1; i.tableMode = bank::TableMode::Step; i.duty = 2;
    ChannelParams p; p.instrument = 2; r.drv.setParams(0, p);
    const auto duties = [](const std::vector<RegWrite>& w) { std::vector<int> d; int last = -1; for (const auto& x : w) if (x.addr == 0xFF11 && (x.value >> 6) != last) { last = x.value >> 6; d.push_back(last); } return d; };
    NoteEvent on = cellOn(0, 60, 2); on.cmd1 = { Cmd::R, 0, 3, 0 };
    std::vector<RegWrite> w = r.block({ on }, 480);
    for (int k = 0; k < 9; ++k) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
    // The note: duty 2 in the burst, table 2 on the ticks after: 3, 0. The R
    // at tick 3 and tick 6 keeps the duty the W left (the ROM's retrigger
    // writes the copy's duty), then 3, 0 again each time.
    CHECK(duties(w) == std::vector<int>{ 2, 3, 0, 3, 0, 3, 0 });
    w = r.block({ cellOn(0, 60, 2) }, 480);
    for (int k = 0; k < 3; ++k) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
    CHECK(duties(w) == std::vector<int>{ 2, 1 });     // row 1 of the STEP table: the retriggers moved nothing
}

TEST_CASE("a STEP table ending in H00 cycles its rows: the position is the A's own row plus one", "[driver][commands][table][rom942]")
{
    // Section 183 (UN_c05_full, UNMASKED's chain 05): table 0 = A02 / A03 /
    // H00, table 2 = W01, table 3 = W03. Notes 1, 2, 3, 4, 5 play A02, A03,
    // H00 -> row 0's A02, A03, A02 again: the hop moves the position first
    // and the A's row plus one is what the instrument keeps, so the third
    // note leaves it at 1, not at 3 (where twelve empty rows would follow).
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[0] = tracker::NoteSource::Tracker;
    Table t0; t0.used = true; t0.steps[0].cmd1 = { Cmd::A, 2, 0, 0 }; t0.steps[1].cmd1 = { Cmd::A, 3, 0, 0 }; t0.steps[2].cmd1 = { Cmd::H, 0, 0, 0 };
    Table t1; t1.used = true; t1.steps[0].cmd1 = { Cmd::W, 1, 0, 0 };
    Table t2; t2.used = true; t2.steps[0].cmd1 = { Cmd::W, 3, 0, 0 };
    r.bank.tables[0] = t0; r.bank.tables[1] = t1; r.bank.tables[2] = t2;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Cycle");
    i.used = true; i.table = 1; i.tableMode = bank::TableMode::Step; i.duty = 2;
    ChannelParams p; p.instrument = 2; r.drv.setParams(0, p);
    const auto duties = [](const std::vector<RegWrite>& w) { std::vector<int> d; int last = -1; for (const auto& x : w) if (x.addr == 0xFF11 && (x.value >> 6) != last) { last = x.value >> 6; d.push_back(last); } return d; };
    std::vector<int> seen;
    for (int n = 0; n < 6; ++n) {
        std::vector<RegWrite> w = r.block({ cellOn(0, 60, 2) }, 480);
        for (int k = 0; k < 3; ++k) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
        const auto d = duties(w);
        REQUIRE(d.size() == 2);                                   // the trigger's duty 2, then the A'd table's W on the tick after
        seen.push_back(d.back());
    }
    CHECK(seen == std::vector<int>{ 1, 3, 1, 3, 1, 3 });
}

TEST_CASE("a STEP table's lanes keep their own positions across a hop in one column", "[driver][commands][table][rom942]")
{
    // Section 183 (STEP_hopA): table 0 = W03 + A02 / W01 / (Z00, H00) / W02,
    // table 2 = W03 on row 0, A20 on row 1. CMD 2's H00 on the third note hops
    // its own lane to row 0, whose A02 replaces the table; CMD 1's position
    // walks on to row 3, so the fourth note plays W02 -- not row 1's W01 again.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[0] = tracker::NoteSource::Tracker;
    Table t0; t0.used = true;
    t0.steps[0].cmd1 = { Cmd::W, 3, 0, 0 }; t0.steps[0].cmd2 = { Cmd::A, 2, 0, 0 };
    t0.steps[1].cmd1 = { Cmd::W, 1, 0, 0 };
    t0.steps[2].cmd1 = { Cmd::Z, 0, 0, 0 }; t0.steps[2].cmd2 = { Cmd::H, 0, 0, 0 };
    t0.steps[3].cmd1 = { Cmd::W, 2, 0, 0 };
    Table t1; t1.used = true; t1.steps[0].cmd1 = { Cmd::W, 3, 0, 0 }; t1.steps[1].cmd1 = { Cmd::A, 0x20, 0, 0 };
    r.bank.tables[0] = t0; r.bank.tables[1] = t1;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Lanes");
    i.used = true; i.table = 1; i.tableMode = bank::TableMode::Step; i.duty = 0;
    ChannelParams p; p.instrument = 2; r.drv.setParams(0, p);
    const auto lastDuty = [](const std::vector<RegWrite>& w) { int d = -1; for (const auto& x : w) if (x.addr == 0xFF11) d = x.value >> 6; return d; };
    std::vector<int> seen;
    for (int n = 0; n < 5; ++n) {
        std::vector<RegWrite> w = r.block({ cellOn(0, 60, 2) }, 480);
        for (int k = 0; k < 3; ++k) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
        seen.push_back(lastDuty(w));
    }
    // Note 1: row 0's W03 then A02 (table 2's W03 on the tick after). Note 2:
    // W01. Note 3: Z00 re-runs W01, the hop's row 0 A02 -> W03. Note 4: CMD 1
    // at row 3, W02; CMD 2 at row 1, nothing. Note 5: CMD 1 past the rows,
    // CMD 2's H00 again -> W03.
    CHECK(seen == std::vector<int>{ 3, 1, 3, 2, 3 });
}

TEST_CASE("R's nibble walks a wave note's NR32 level a notch a retrigger and stays at mute", "[driver][commands][rom942]")
{
    // Section 182 (RF4_ph_ch2): `R F4` on a 100 % wave note: the immediate
    // retrigger writes 50 %, the next 25 %, the next mute, and the rolls after
    // that stay muted.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[2] = tracker::NoteSource::Tracker;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Wave, "Roll");
    i.used = true; i.waveLevel = 3;
    ChannelParams p; p.instrument = 2; r.drv.setParams(2, p);
    NoteEvent on = cellOn(2, 60, 2); on.cmd1 = { Cmd::R, 15, 4, 0 };
    std::vector<int> levels;
    auto w = r.block({ on }, 480);
    for (int k = 0; k < 14; ++k) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
    int last = -1;
    for (const auto& x : w) if (x.addr == 0xFF1C && (x.value >> 5) != last) { last = x.value >> 5; levels.push_back(last); }
    CHECK(levels == std::vector<int>{ 1, 2, 3, 0 });      // 100 %, 50 %, 25 %, mute -- and no wrap back
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

TEST_CASE("a table row's K counts from that row's own tick", "[driver][commands][table]")
{
    // Section 128: `K n` dies n ticks after the tick it was **read** on. A
    // table row is read inside the tick, so a row's `K 00` dies on that row's
    // own tick -- ChipBoy's countdown, stepped at the top of the tick, made it
    // the next one, which ran every note of `SAMESONG`'s EGUIT into the next.
    const auto diesAfter = [](int killValue, int row) {
        Rig r;
        r.tickHz = 100.0;
        r.song.noteSource[0] = tracker::NoteSource::Tracker;
        auto& t = r.bank.tables[0]; t.used = true; t.end = TableEnd::Stop;
        t.steps[size_t(row)].cmd1 = { Cmd::K, int16_t(killValue), 0, 0 };
        r.bank.instruments[0].table = 1;
        ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
        r.block({ cellOn(0, 69, 1) }, 480);            // tick 0: the note, and the table's row 0
        for (int tick = 1; tick < 40; ++tick) {
            if (!r.drv.view(0).active) return tick - 1;
            r.block({}, 480);
        }
        return r.drv.view(0).active ? -1 : 39;
    };
    CHECK(diesAfter(0, 3) == 3);                       // the row's own tick
    CHECK(diesAfter(1, 3) == 4);
    CHECK(diesAfter(2, 3) == 5);
    CHECK(diesAfter(0, 0) == 0);                       // row 0 fires with the note-on
    // A `K` whose count is the table's own length is due on the tick its row
    // comes round again: a countdown would be re-armed a tick before it fired
    // and the note would never die. Measured on the ROM at tick 19.
    CHECK(diesAfter(16, 3) == 19);
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
        // A slot change lands on the next tick, a second away at this rate;
        // until then the bend runs on, and past the table's bottom it comes
        // round nine octaves rather than stopping (section 153).
        r.block({}, 4194304 + 4096);
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

TEST_CASE("the pitch clock is the ROM's grid, 11704 cycles on average, free-running", "[driver][pitch]")
{
    // LSDj sets the Game Boy's timer once at boot and never moves it, so the
    // clock does not know that a note began: the phase of an update against a
    // note is where the player pressed play (docs/LSDJ_PARITY.md section 1).
    // Section 160: the instants are the grid's, six a video frame, run at the
    // first frame at or after each -- so 11712 apart to the sample, the sixth
    // to the next frame's first 11664, averaging 11704.
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
    for (size_t i = 1; i < at.size(); ++i) {
        const uint64_t gap = at[i] - at[i - 1];
        INFO("update " << i);
        CHECK((gap >= 11664 - 90 && gap <= 11712 + 90));   // a frame's rounding either way
        // The write sits a cycle or two into the instant's burst: one of the
        // two instants at or before it is its own, to the frame.
        bool onGrid = false;
        for (uint64_t n = driver::gridAfter(at[i]) - 2; n < driver::gridAfter(at[i]); ++n) {
            const uint64_t instant = 4194304 * driver::gridFrame(n, 48000.0) / 48000;
            if (at[i] >= instant && at[i] - instant <= 2) onGrid = true;
        }
        CHECK(onGrid);
    }
    const double mean = double(at.back() - at.front()) / double(at.size() - 1);
    CHECK(std::fabs(mean - 11704.0) < 2.0);
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
        // Section 134: the slot's `R` fires its first retrigger on the note's
        // own tick, so the note-on block already carries one.
        r.block({ Rig::on(0, 60, 100) }, 480);
        std::vector<int> out{ int(r.drv.view(0).envVol) };
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

TEST_CASE("R's fast roll starts on the pitch clock, not at the command", "[driver][commands]")
{
    // Section 143: `x = 8` is the roll on the pitch clock (section 90), every
    // `y + 1` clocks. Measured, the ROM fires nothing as the command is read --
    // where every other `x` fires one on its own tick (section 134) -- and
    // ChipBoy fired one there too, a doubled trigger at the note.
    auto triggers = [](int16_t x, int16_t y) {
        Rig r;
        r.tickHz = 65.2174;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2;
        p.cmd[0] = { Cmd::R, x, y, 0 };
        r.drv.setTickRate(r.tickHz);
        r.drv.setParams(0, p);
        const auto w = r.block({ Rig::on(0, 60, 100) }, 256);
        int n = 0;
        for (const auto& e : w) if (e.addr == 0xFF14 && (e.value & 0x80)) ++n;
        return n;
    };
    // The note's own burst and nothing else, in the block the note is in.
    CHECK(triggers(8, 8) == 1);
    CHECK(triggers(8, 4) == 1);
    // Every other x still owes one as section 134 measured.
    CHECK(triggers(0, 4) == 2);
    CHECK(triggers(15, 4) == 2);
}

TEST_CASE("a retrigger starts a fading envelope again", "[driver][commands]")
{
    // Section 136: ChipBoy added `R`'s step to the level the software envelope
    // had already faded to, so a fading instrument's roll was silent. A
    // retrigger is a note-on in everything but the note: the envelope starts
    // again, and the step counts from its start.
    auto volumes = [](int16_t x) {
        Rig r;
        r.tickHz = 100.0;
        auto& i = r.bank.instruments[0];
        i.env.mode = EnvMode::Shaped;
        i.env.start = 9; i.env.peak = 9; i.env.attackTicks = 0;
        i.env.decayTicks = 3; i.env.sustain = 0;          // nine to nothing in three ticks
        ChannelParams p; p.instrument = 1; p.velocityMode = 2;
        p.cmd[0] = { Cmd::R, x, 6, 0 };                   // every six ticks, well past the fade
        r.drv.setParams(0, p);
        // The level each trigger *sounds* at: the NR12 write the trigger in the
        // same block follows, not the envelope's level once the block is over --
        // it has faded by then, which is the whole point.
        auto volAtTrigger = [](const std::vector<RegWrite>& w) {
            int vol = -1, best = -1;
            for (const auto& x : w) {
                if (x.addr == 0xFF12) vol = (x.value >> 4) & 15;
                if (x.addr == 0xFF14 && (x.value & 0x80) && vol >= 0) best = vol;
            }
            return best;
        };
        std::vector<int> out;
        out.push_back(volAtTrigger(r.block({ Rig::on(0, 60, 100) }, 480)));
        for (int k = 0; k < 40; ++k) {
            const int at = volAtTrigger(r.block({}, 480));
            if (at >= 0) out.push_back(at);
        }
        return out;
    };
    // The slot's `R` fires its first retrigger on the note's own tick (section
    // 134), so the first entry is already a retrigger and not the note's nine.
    const auto flat = volumes(0);                         // x = 0: the start level every time
    REQUIRE(flat.size() >= 4);
    CHECK(flat[0] == 9); CHECK(flat[1] == 9); CHECK(flat[2] == 9); CHECK(flat[3] == 9);
    const auto down = volumes(15);                        // x = F: one step down each retrigger
    REQUIRE(down.size() >= 4);
    CHECK(down[0] == 8); CHECK(down[1] == 7); CHECK(down[2] == 6); CHECK(down[3] == 5);
}

TEST_CASE("M sets a side or moves it", "[driver][commands]")
{
    Rig r;
    GlobalParams g; g.masterL = 5; g.masterR = 5; r.drv.setGlobal(g);
    ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
    auto w = r.block({}, 512);
    REQUIRE(last(w, 0xFF24) != nullptr);
    CHECK(last(w, 0xFF24)->value == 0x55);
    // Section 75: 0-7 sets, 8-15 shifts by 0 +1 +2 +3 -4 -3 -2 -1, clamped.
    p.cmd[0] = { Cmd::M, 9, 13, 0 }; r.drv.setParams(0, p);   // left up 1, right down 3
    w = r.block({}, 512);
    CHECK(last(w, 0xFF24)->value == 0x62);
    p.cmd[0] = { Cmd::M, 3, 8, 0 }; r.drv.setParams(0, p);    // left to 3, right untouched
    w = r.block({}, 512);
    CHECK(last(w, 0xFF24)->value == 0x32);
    p.cmd[0] = { Cmd::M, 11, 15, 0 }; r.drv.setParams(0, p);  // up 3, down 1
    w = r.block({}, 512);
    CHECK(last(w, 0xFF24)->value == 0x61);
    p.cmd[0] = { Cmd::M, 12, 12, 0 }; r.drv.setParams(0, p);  // both down 4, clamping at 0
    w = r.block({}, 512);
    CHECK(last(w, 0xFF24)->value == 0x20);
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

TEST_CASE("an E does trigger when the LENGTH counter is on", "[driver][zombie]")
{
    // Section 138, measured by sweeping instrument byte 3 against the same `E`:
    // it is the counter **enable** bit that decides, not the length value and
    // not whether the counter has run out, and it holds on pulse and noise. A
    // length counter can have switched the channel off at any moment and the
    // driver cannot read back that it has, so a zombie write may land on a dead
    // channel; LSDj triggers instead. The trigger carries the level the `E`
    // just set, so the envelope is not restarted with it (section 136).
    auto triggers = [](uint16_t length, bool latent, int ch) {
        Rig r;
        auto& i = r.bank.instruments[size_t(ch == 3 ? 20 : 0)];
        if (ch == 3) i = Instrument::defaults(InstrumentType::Noise, "hat");
        i.length = length; i.lengthLatent = latent;
        ChannelParams p; p.instrument = uint8_t(ch == 3 ? 21 : 1); p.velocityMode = 2;
        r.drv.setParams(ch, p);
        r.block({ Rig::on(ch, 69, 100) }, 512);
        const auto w = r.block({ levelCell(ch, 9, 0) }, 512);
        return std::make_pair(anyTrigger(w, uint16_t(0xFF14 + 5 * ch)), int(r.drv.view(ch).envVol));
    };
    for (int ch : { 0, 3 }) {
        INFO("channel " << ch);
        CHECK_FALSE(triggers(0, false, ch).first);         // no length at all
        CHECK_FALSE(triggers(17, true, ch).first);          // a length, the counter latent
        const auto on = triggers(17, false, ch);
        CHECK(on.first);                                    // a length with the counter enabled
        CHECK(on.second == 9);                              // and it carries the E's own level
    }
}

TEST_CASE("a shaped envelope's first step lands on its own pitch clock", "[driver][shaped]")
{
    // Section 142: the level boundary of a six-level stage stored as 2 + 47/256
    // ticks sits at 93.17 of section 116's 1/256-tick units, and two pitch
    // clocks are worth 93.2 of them -- so truncating the position to that unit
    // put the first step a whole pitch clock (2.8 ms) late, which is what the
    // ROM's step times exposed. The position is 1/65536 of a tick now.
    Rig r;
    r.tickHz = 65.2174;                                   // tempo 163, as measured
    auto& i = r.bank.instruments[0];
    i.env.mode = EnvMode::Shaped;
    i.env.start = 6; i.env.peak = 6; i.env.attackTicks = 0; i.env.attackFine = 0;
    i.env.decayTicks = 2; i.env.decayFine = 47;           // what the importer makes of `env 62`
    i.env.sustain = 0;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2;
    r.drv.setTickRate(r.tickHz);                          // section 141
    r.drv.setParams(0, p);
    // The tick the note is on, then the pitch clocks inside it: the level must
    // leave 6 on the second clock, 5.58 ms in, not the third.
    std::vector<std::pair<double, int>> steps;
    int last = -1;
    for (int k = 0; k < 16; ++k) {
        const auto w = r.block(k == 0 ? std::vector<NoteEvent>{ Rig::on(0, 60, 100) } : std::vector<NoteEvent>{}, 128);
        const int lvl = int(r.drv.view(0).envVol);
        if (last >= 0 && lvl != last) steps.push_back({ double(k) * 128.0 / 48.0, lvl });
        last = lvl;
    }
    REQUIRE(steps.size() >= 2);
    INFO("first step at " << steps[0].first << " ms to level " << steps[0].second);
    CHECK(steps[0].second == 5);
    CHECK(steps[0].first < 7.0);                          // the second clock, not the third
    CHECK(steps[0].first > 3.0);
}

TEST_CASE("a STEP table's position is the instrument's own", "[driver][table]")
{
    // Section 140: measured on the ROM, two instruments keep their own position
    // in a STEP table and one playing in between does not move the other's --
    // ChipBoy kept one per channel and reset it whenever the table slot changed,
    // which put `READROOM`'s phrase `1A` back on the left after its `0D` note.
    Rig r;
    auto& a = r.bank.instruments[20];
    a = Instrument::defaults(InstrumentType::Noise, "A");
    a.pan = Pan::Both; a.table = 5; a.tableMode = TableMode::Step;
    auto& b = r.bank.instruments[21];
    b = Instrument::defaults(InstrumentType::Noise, "B");
    b.pan = Pan::Both; b.table = 6; b.tableMode = TableMode::Step;
    auto& ta = r.bank.tables[4];                          // slot 5: left, right, blank, hop
    ta = Table{};                                         // the factory slot holds a preset with rows of its own
    ta.used = true; ta.name = "pans";
    ta.steps[0].cmd1 = Command{ Cmd::O, 1, 0, 0 };
    ta.steps[1].cmd1 = Command{ Cmd::O, 2, 0, 0 };
    ta.steps[3].cmd1 = Command{ Cmd::H, 0, 0, 0 };
    auto& tb = r.bank.tables[5];                          // slot 6: B's own, a transpose
    tb = Table{};
    tb.used = true; tb.name = "tsp";
    tb.steps[0].hasTranspose = true; tb.steps[0].transpose = -6;
    tb.steps[3].cmd1 = Command{ Cmd::H, 0, 0, 0 };
    auto pan = [&](int slot) {
        ChannelParams p; p.instrument = uint8_t(slot); r.drv.setParams(3, p);
        const auto w = r.block({ Rig::on(3, 69, 100) }, 512);
        const RegWrite* l = last(w, 0xFF25);
        if (l == nullptr) return '?';
        const bool L = (l->value & 0x80) != 0, R = (l->value & 0x08) != 0;
        return L && R ? 'C' : L ? 'L' : R ? 'R' : '-';
    };
    // A's own walk is L R C, and B's notes in between leave it alone.
    std::string got;
    got += pan(21); got += pan(21); got += pan(22);        // A A B
    got += pan(21); got += pan(21); got += pan(22);        // A A B
    INFO("got " << got);
    CHECK(got[0] == 'L'); CHECK(got[1] == 'R');
    CHECK(got[3] == 'C');                                  // after B, A carries on
    CHECK(got[4] == 'L');                                  // and wraps
}

TEST_CASE("a table row's O lands after the note's own pan", "[driver][table]")
{
    // Section 139: LSDj's note pass writes the mixer and triggers, and its table
    // pass writes the row's `O` right after -- two NR51 writes in the one tick,
    // the note's first. ChipBoy folded the row's pan into the note's own write,
    // so a short hit's attack was already panned and `READROOM`'s phrase `1A`
    // put a centred retrigger on the left.
    Rig r;
    auto& i = r.bank.instruments[20];
    i = Instrument::defaults(InstrumentType::Noise, "hat");
    i.pan = Pan::Both; i.table = 5;                       // slot 5, below
    auto& tb = r.bank.tables[4];
    tb.used = true; tb.name = "pans";
    tb.steps[0].cmd1 = Command{ Cmd::O, 1, 0, 0 };        // left
    ChannelParams p; p.instrument = 21; r.drv.setParams(3, p);
    const auto w = r.block({ Rig::on(3, 69, 100) }, 512);
    // The trigger, then the note's own pan (both sides), then the row's.
    int trig = -1, after = -1, pans = 0;
    for (size_t k = 0; k < w.size(); ++k) {
        if (w[k].addr == 0xFF23 && (w[k].value & 0x80)) trig = int(k);
        if (w[k].addr == 0xFF25) { ++pans; if (trig >= 0 && after < 0) after = int(k); }
    }
    REQUIRE(trig >= 0);
    REQUIRE(after > trig);                                // a pan write follows the trigger
    CHECK(pans >= 2);
    // The last one is the row's: the noise channel on the left only. NR51 puts
    // the left gates in bits 4-7 and the right in 0-3, so noise is 0x80 and 0x08.
    const RegWrite* last51 = last(w, 0xFF25);
    REQUIRE(last51 != nullptr);
    CHECK((last51->value & 0x80) != 0);
    CHECK((last51->value & 0x08) == 0);
    // and the one the note itself wrote had both sides.
    int notePan = -1;
    for (size_t k = 0; k < w.size(); ++k)
        if (w[k].addr == 0xFF25 && int(k) > trig) { notePan = int(w[k].value); break; }
    REQUIRE(notePan >= 0);
    CHECK((notePan & 0x88) == 0x88);                      // both sides, the instrument's own
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
    tb = Table{};                                      // the factory slot holds a preset with rows of its own
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
            else if (x.addr == 0xFF1E && !(x.value & 0x80)) {      // section 171: the $7E0 pre-trigger is not a pitch
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
    // holds while the note is held (section 27). Section 116: the level moves
    // on the **pitch clock** now, not once a tick, so reading it at the end of
    // a block catches it most of the way to the next tick's value -- the shape
    // is the same, sampled a fraction of a tick later.
    // Section 121: and not on the tick the note started on -- that tick is the
    // envelope's first, so the list begins at the start level rather than a
    // tick into the attack. Measured against the ROM: a shaped stage's first
    // level change lands one envelope period after the note-on, not at it.
    // Section 132: the level is **truncated** along the ramp, not rounded, so
    // each level is held for its whole step as LSDj holds it -- every reading
    // sits at or one below the rounded list this used to carry.
    const std::vector<int> want { 1, 5, 8, 11, 11, 10, 8, 7, 6 };
    CHECK(levels == want);
    // What matters is that it climbs to the peak and settles on the sustain.
    CHECK(*std::max_element(levels.begin(), levels.end()) >= 11);
    CHECK(levels.back() == 6);
    // The release starts at the note-off and walks to silence over its ticks:
    // 6 -> 0 in four, linear. The tick of the note-off's own block is the
    // first of them.
    std::vector<int> rel;
    step({ Rig::off(0, 69) });
    rel.push_back(levels.back());
    for (int k = 0; k < 3; ++k) { step({}); rel.push_back(levels.back()); }
    // It falls, and it is over by the end of its four ticks.
    CHECK(rel[0] < 6);
    CHECK(rel[1] < rel[0]);
    CHECK(rel[2] < rel[1]);
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
            // Section 171: the wave's $7E0 pre-trigger pair is not a pitch write.
            for (size_t k = 0; k < w.size(); ++k)
                if (w[k].addr == 0xFF1D && !(k + 1 < w.size() && w[k + 1].addr == 0xFF1E && (w[k + 1].value & 0x80))) periods.push_back(w[k].value);
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
    // Section 84: the note-on triggers at the **plain** note; row 0's transpose
    // reaches the channel on the update after it, not in the note's own writes.
    auto w = r.block({ cellOn(3, 72, 21) }, 200);
    std::vector<uint8_t> nrs;
    for (const auto& x : w) if (x.addr == 0xFF22) nrs.push_back(x.value);
    REQUIRE(nrs.size() >= 2);
    CHECK(nrs[0] == nr43For(72));                             // plain, with the trigger
    CHECK(nrs[1] == nr43For(48));                             // row 0's -24, one update later
    CHECK(r.drv.view(3).tableSlot == 10);
    CHECK(std::count_if(w.begin(), w.end(), [](const RegWrite& x) { return x.addr == 0xFF23 && (x.value & 0x80); }) == 1);
    // Row 1 at the next tick: a new NR43, no trigger.
    w = r.block({}, 200);
    REQUIRE(last(w, 0xFF22) != nullptr);
    CHECK(last(w, 0xFF22)->value == nr43For(84));             // +12
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
    CHECK(last(w, 0xFF22)->value == nr43For(48));             // row 0 still moves it
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
    // Section 84: NR43 goes out when the byte **changes**, as the ROM writes it,
    // so a step between two notes the map gives the same pair writes nothing.
    // The running value is what the test follows.
    uint8_t nr = 0;
    const auto step = [&](const std::vector<RegWrite>& out) { if (const auto* x = last(out, 0xFF22)) nr = x->value; };
    auto w = r.block({ cellOn(3, 72, 21) }, 200);
    REQUIRE(last(w, 0xFF22) != nullptr);
    step(w);
    CHECK(nr == nr43For(72));
    w = r.block({ cellCmd(3, Command{ Cmd::S, 0, 1, 0 }) }, 200);        // S01: one up
    step(w);
    CHECK(nr == nr43For(73));
    CHECK_FALSE(has(w, 0xFF23));
    w = r.block({ cellCmd(3, Command{ Cmd::S, 0xF, 0xE, 0 }) }, 200);    // SFE: two down, on top of the one up
    step(w);
    CHECK(nr == nr43For(71));
    w = r.block({ cellCmd(3, Command{ Cmd::S, 1, 0, 0 }) }, 200);        // S10: sixteen up
    step(w);
    CHECK(nr == nr43For(87));
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
    // Section 78, measured on 9.3.9: on PU2 F is **x semitones up plus y/32**,
    // replacing the instrument's own transpose as LSDj's TSP does.
    auto e = cellOn(1, 60, 21); e.cmd1 = { Cmd::F, 1, 0, 0 };
    r.block({ e }, 200);
    CHECK(r.drv.view(1).period == Driver::periodForNote(61.0, false));
    e = cellOn(1, 60, 21); e.cmd1 = { Cmd::F, 15, 0, 0 };
    r.block({ e }, 200);
    CHECK(r.drv.view(1).period == Driver::periodForNote(75.0, false));
    e = cellOn(1, 60, 21); e.cmd1 = { Cmd::F, 0, 15, 0 };          // y/32 of a semitone up
    r.block({ e }, 200);
    CHECK(r.drv.view(1).period == Driver::periodForNote(60.0 + 15.0 / 32.0, false));
    // A plain note-on puts the instrument's own back.
    r.block({ cellOn(1, 60, 21) }, 200);
    CHECK(r.drv.view(1).period == plain72);
    // On PU1 it is a **downward** finetune of y/32, and x does nothing.
    e = cellOn(0, 60, 21); e.cmd1 = { Cmd::F, 0, 15, 0 };
    r.block({ e }, 200);
    CHECK(r.drv.view(0).period == Driver::periodForNote(60.0 - 15.0 / 32.0, false));
    e = cellOn(0, 60, 21); e.cmd1 = { Cmd::F, 15, 0, 0 };          // y = 0: nothing moves
    r.block({ e }, 200);
    CHECK(r.drv.view(0).period == plain60);
    // Absolute, not cumulative: three in a row leave the same offset as one.
    for (int k = 0; k < 3; ++k) { auto f = cellCmd(0, { Cmd::F, 0, 15, 0 }); r.block({ f }, 200); }
    CHECK(r.drv.view(0).period == Driver::periodForNote(60.0 - 15.0 / 32.0, false));
}

TEST_CASE("B gates a cell's note and hops a table's lane", "[driver][commands]")
{
    // Section 73, measured on 9.3.9. In a cell each nibble is an independent
    // roll that passes n times in **15** and the note sounds if either passes:
    // B00 never sounds, and any nibble of 15 always does. In a table it is a
    // hop to row y taken **x/16** of the time -- a different law, which is why
    // BF0 misses one hop in sixteen rather than always hopping.
    SECTION("B00 in a cell never sounds and B0F always does") {
        for (auto [val, want] : { std::pair<Command, bool>{ { Cmd::B, 0, 0, 0 }, false },
                                  { { Cmd::B, 0, 15, 0 }, true },
                                  { { Cmd::B, 15, 0, 0 }, true } }) {
            Rig r;
            for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
            ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
            int triggers = 0;
            for (int n = 0; n < 24; ++n) {
                auto e = cellOn(0, 69, 1); e.cmd1 = val;
                auto w = r.block({ e }, 512);
                for (const auto& x : w) if (x.addr == 0xFF14 && (x.value & 0x80)) ++triggers;
            }
            CHECK(bool(triggers > 0) == want);
            if (want) CHECK(triggers == 24);
        }
    }
    SECTION("a middling B sounds some of the time and not all of it") {
        Rig r;
        for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
        ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
        int triggers = 0;
        for (int n = 0; n < 200; ++n) {
            auto e = cellOn(0, 69, 1); e.cmd1 = { Cmd::B, 0, 8, 0 };
            auto w = r.block({ e }, 256);
            for (const auto& x : w) if (x.addr == 0xFF14 && (x.value & 0x80)) ++triggers;
        }
        CHECK(triggers > 60);                        // 8/15 is about 107 of 200
        CHECK(triggers < 150);
    }
    SECTION("a table B hops to row y") {
        // x = 15 is fifteen hops in sixteen, so over a long run the rows above
        // the hop are reached far more often than the ones below it.
        Rig r;
        auto& t = r.bank.tables[0];
        t.used = true; t.end = TableEnd::Loop;
        t.steps[0].hasTranspose = true; t.steps[0].transpose = 0;
        t.steps[1].hasTranspose = true; t.steps[1].transpose = 4;
        t.steps[2].hasTranspose = true; t.steps[2].transpose = 8;
        t.steps[2].cmd1 = { Cmd::B, 15, 0, 0 };
        for (int i = 3; i < 16; ++i) { t.steps[i].hasTranspose = true; t.steps[i].transpose = 20; }
        ChannelParams p; p.instrument = 1; p.table = 1; r.drv.setParams(0, p);
        auto w = r.block({ Rig::on(0, 60, 100) }, 32768);
        int low = 0, high = 0;
        for (const auto& x : w) {
            if (x.addr != 0xFF14) continue;
            (void) x;
        }
        // Count how often the lane is inside rows 0-2 against the +20 block,
        // by the period the transposes produce.
        const int p20 = Driver::periodForNote(80.0, false);
        int lastLo = 0;
        for (const auto& x : w) {
            if (x.addr == 0xFF13) lastLo = x.value;
            else if (x.addr == 0xFF14) { const int per = ((x.value & 7) << 8) | lastLo; if (per == p20) ++high; else ++low; }
        }
        CHECK(low > 0);
        CHECK(high > 0);                              // it does not hop *every* time
        CHECK(low > high * 3);                        // but it hops far more often than not
    }
}

TEST_CASE("the Z record outlives the note-on", "[driver][commands]")
{
    // Section 123, measured on 9.2.L: the cell lane's last command survives a
    // note-on -- and a different instrument -- so a `Z` on a later row re-runs
    // it. ChipBoy cleared the record at every note-on, which made every `Z`
    // after the first note inert; that is `SAMESONG`'s phrase 14.
    Rig r;
    r.tickHz = 100.0;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Wave, "Pad");
    i.used = true; i.waveLevel = 0;
    auto& j = r.bank.instruments[2];
    j = bank::Instrument::defaults(bank::InstrumentType::Wave, "Other");
    j.used = true; j.waveLevel = 0;
    ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(2, p);
    // NR32's volume code: 0 mute, 1 100%, 2 50%, 3 25%.
    const auto level = [&r](std::vector<NoteEvent> ev) {
        const auto w = r.block(std::move(ev), 480);
        const RegWrite* nr = last(w, 0xFF1C);
        return nr != nullptr ? int((nr->value >> 5) & 3) : -1;
    };
    NoteEvent e = cellOn(2, 60, 2);
    e.cmd1 = { Cmd::E, 0, 2, 0 };                        // 50% on the wave channel
    CHECK(level({ e }) == 2);
    CHECK(level({ cellOn(2, 60, 2) }) == 0);             // a plain note: the instrument's own
    NoteEvent z = cellOn(2, 60, 2);
    z.cmd1 = { Cmd::Z, 0, 0, 0 };                        // Z 00 re-runs it exactly
    CHECK(level({ z }) == 2);
    {   // and across a different instrument
        CHECK(level({ cellOn(2, 60, 3) }) == 0);
        NoteEvent z2 = cellOn(2, 60, 3);
        z2.cmd1 = { Cmd::Z, 0, 0, 0 };
        CHECK(level({ z2 }) == 2);
    }
}

TEST_CASE("Z re-runs its own lane, not the other column", "[driver][commands]")
{
    // Section 74, measured on 9.3.9: a command that ran a row earlier in the
    // *other* table column is not what a Z re-runs.
    Rig r;
    auto& t = r.bank.tables[0];
    t.used = true; t.end = TableEnd::Stop;
    // A Z that adds 0..15 to the target's low nibble, so a re-run shows up as
    // NR50 values other than the M's own.
    t.steps[0].cmd2 = { Cmd::M, 4, 0, 0 };            // column 2 sets NR50
    t.steps[1].cmd1 = { Cmd::Z, 0, 15, 0 };           // column 1's Z has nothing of its own
    ChannelParams p; p.instrument = 1; p.table = 1; r.drv.setParams(0, p);
    GlobalParams g; g.masterL = 7; g.masterR = 7; r.drv.setGlobal(g);
    auto w = r.block({ Rig::on(0, 60, 100) }, 4096);
    std::vector<uint8_t> nr50;
    for (const auto& x : w) if (x.addr == 0xFF24) nr50.push_back(x.value);
    REQUIRE(!nr50.empty());
    CHECK(nr50.back() == 0x40);                       // column 2's M ran; the Z re-ran nothing
    // The same M in column 1 *is* what column 1's Z re-runs.
    Rig r2;
    auto& t2 = r2.bank.tables[0];
    t2.used = true; t2.end = TableEnd::Loop;
    t2.steps[0].cmd1 = { Cmd::M, 4, 0, 0 };
    t2.steps[1].cmd1 = { Cmd::Z, 0, 15, 0 };
    ChannelParams q; q.instrument = 1; q.table = 1; r2.drv.setParams(0, q);
    r2.drv.setGlobal(g);
    w = r2.block({ Rig::on(0, 60, 100) }, 16384);
    std::vector<uint8_t> seen;
    for (const auto& x : w) if (x.addr == 0xFF24 && std::find(seen.begin(), seen.end(), x.value) == seen.end()) seen.push_back(x.value);
    CHECK(seen.size() > 2);                           // the M's own value and the Z's re-runs
}

TEST_CASE("R retriggers every y ticks, and y = 0 fires once", "[driver][commands]")
{
    // Section 76, measured on 9.3.9: R01 is one trigger a tick, R04 one every
    // four, and R00 retriggers once and stops rather than every tick.
    // The rig ticks at 240 Hz over 48 kHz, so 4000 frames is twenty ticks; the
    // count includes the note-on's own trigger.
    auto count = [](int y) {
        Rig r;
        ChannelParams p; p.instrument = 1; p.cmd[0] = { Cmd::R, 0, int16_t(y), 0 }; r.drv.setParams(0, p);
        const auto w = r.block({ Rig::on(0, 69, 100) }, 4000);
        int n = 0;
        for (const auto& x : w) if (x.addr == 0xFF14 && (x.value & 0x80)) ++n;
        return n;
    };
    const int one = count(1), two = count(2), four = count(4);
    CHECK(one >= 20);                                 // one a tick
    CHECK(std::abs(two * 2 - one) <= 3);
    CHECK(std::abs(four * 4 - one) <= 5);
    CHECK(count(0) == 2);                             // the note-on, one retrigger, and stop
}

TEST_CASE("C and V reach the noise channel", "[driver][commands][noise]")
{
    // Section 77: both work on the ROM and both were dropped. The chord's
    // semitones and the vibrato's swing walk the noise map exactly as a
    // table's transpose column does.
    SECTION("a chord walks NR43") {
        Rig r;
        ChannelParams p; p.instrument = 25; p.cmd[0] = { Cmd::C, 3, 7, 0 }; r.drv.setParams(3, p);
        auto w = r.block({ Rig::on(3, 60, 100) }, 8192);
        std::vector<uint8_t> seen;
        for (const auto& x : w) if (x.addr == 0xFF22 && std::find(seen.begin(), seen.end(), x.value) == seen.end()) seen.push_back(x.value);
        CHECK(seen.size() >= 3);                      // note, note + 3, note + 7
    }
    SECTION("a vibrato keeps NR43 moving") {
        Rig r;
        ChannelParams p; p.instrument = 25; r.drv.setParams(3, p);
        auto w = r.block({ Rig::on(3, 60, 100) }, 8192);
        const size_t still = std::count_if(w.begin(), w.end(), [](const RegWrite& x) { return x.addr == 0xFF22; });
        Rig r2;
        ChannelParams q; q.instrument = 25; q.cmd[0] = { Cmd::V, 4, 8, 0 }; r2.drv.setParams(3, q);
        w = r2.block({ Rig::on(3, 60, 100) }, 8192);
        const size_t moving = std::count_if(w.begin(), w.end(), [](const RegWrite& x) { return x.addr == 0xFF22; });
        CHECK(moving > still + 4);
    }
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
    // Section 116: the level moves on the pitch clock now, so a reading taken at
    // the end of a block sits a fraction of a tick ahead of the old per-tick
    // list. The stages still land where they did, to within that fraction.
    // Section 116: the level moves on the pitch clock, so where a reading lands
    // inside a tick is no longer exact -- the stages still take the ticks they
    // are given, to within one of them.
    CHECK(levels[3] >= 5); CHECK(levels[3] <= 7);                      // the attack has reached the peak of 5
    CHECK(levels[11] >= 11);                                           // the decay is at or near the sustain of 13
    CHECK(*std::max_element(levels.begin(), levels.begin() + 14) == 13);   // and does reach it
    CHECK(levels[24] <= 1);                                            // the fade is at or one off its level (section 132)
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
    // Section 132: the ladder is an 8.8 accumulator, `(frames * 256 - 1) / L`
    // truncated and the running total truncated too.
    CHECK(bank::waveRun(16, 8, run) == 8);
    CHECK(int(run[1]) == 2); CHECK(int(run[3]) == 6); CHECK(int(run[4]) == 9); CHECK(int(run[7]) == 15);
    // Every run length measured on the ROM, read off the wave-RAM loads.
    const auto ladder = [&](int len) { uint8_t r[16]; const int n = bank::waveRun(16, len, r); return std::vector<int>(r, r + n); };
    CHECK(ladder(3)  == std::vector<int>{ 0, 7, 15 });
    CHECK(ladder(4)  == std::vector<int>{ 0, 5, 10, 15 });
    CHECK(ladder(5)  == std::vector<int>{ 0, 3, 7, 11, 15 });
    CHECK(ladder(6)  == std::vector<int>{ 0, 3, 6, 9, 12, 15 });
    CHECK(ladder(9)  == std::vector<int>{ 0, 1, 3, 5, 7, 9, 11, 13, 15 });
    CHECK(ladder(11) == std::vector<int>{ 0, 1, 3, 4, 6, 7, 9, 11, 12, 14, 15 });
    CHECK(ladder(16) == std::vector<int>{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 });
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
    // view().frame is the frame plus one. The note's own tick belongs to frame 0
    // (section 94); the run then walks its four steps -- frames 0, 5, 10, 15 --
    // and repeats from step 2.
    CHECK(frames[0] == 0 + 1); CHECK(frames[1] == 5 + 1); CHECK(frames[2] == 10 + 1);
    CHECK(frames[3] == 15 + 1); CHECK(frames[4] == 10 + 1); CHECK(frames[5] == 15 + 1);
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

TEST_CASE("F advances a wave frame, past the ones the run skips", "[driver][wave]")
{
    // Section 92: measured on 8.4.4, 8.8.6, 9.2.L and 9.3.9 alike, F **advances**
    // the frame by its argument every time it runs, through the wave's frames
    // rather than through the run. Section 100: the argument is the whole byte,
    // `x * 16 + y`, so the step here is given as its two nibbles.
    const auto rig = [](int step) {
        auto r = std::make_unique<Rig>();
        r->tickHz = 100.0;
        auto& w = r->bank.waves[0];
        w.used = true; w.frames.clear();
        for (int f = 0; f < 16; ++f) { bank::Frame fr; fr.s.fill(uint8_t(f)); w.frames.push_back(fr); }
        Table t; t.used = true; t.name = "Frame";
        for (int k = 0; k < 16; ++k) t.steps[size_t(k)].cmd1 = { Cmd::F, int16_t((step >> 4) & 15), int16_t(step & 15), 0 };
        r->bank.tables[7] = t;
        auto& i = r->bank.instruments[1];
        i = bank::Instrument::defaults(bank::InstrumentType::Wave, "Manual");
        i.used = true; i.wave = 1; i.frameLength = 4; i.frameAdvance = 0; i.table = 8;   // the run is 0, 5, 10, 15
        ChannelParams p; p.instrument = 2; p.velocityMode = 2; r->drv.setParams(2, p);
        r->block({ Rig::on(2, 60, 100) }, 480);
        std::vector<int> seen{ int(r->drv.view(2).frame) - 1 };
        for (int k = 0; k < 5; ++k) { r->block({}, 480); seen.push_back(int(r->drv.view(2).frame) - 1); }
        return seen;
    };
    // The note-on fires row 0 in its own tick (section 84), so the first sample
    // already carries one step; what the rule says is the distance between them.
    const auto one = rig(1), two = rig(2), six = rig(6);
    const auto stepsOf = [](const std::vector<int>& v) {
        std::vector<int> d;
        for (size_t k = 1; k < v.size(); ++k) d.push_back(((v[k] - v[k - 1]) % 16 + 16) % 16);
        return d;
    };
    INFO("one " << one[0] << " " << one[1] << " " << one[2] << " " << one[3]);
    for (int d : stepsOf(one)) CHECK(d == 1);
    for (int d : stepsOf(two)) CHECK(d == 2);
    for (int d : stepsOf(six)) CHECK(d == 6);
    // Frames the run skips (the run is 0, 5, 10, 15) are reached all the same.
    CHECK(std::find(one.begin(), one.end(), 3) != one.end());
    CHECK(std::find(six.begin(), six.end(), 12) != six.end());
    // Section 100: the high nibble counts. `F 11` is seventeen frames on, which
    // over sixteen frames is one -- and section 103 puts that one in the next
    // slot, as it is on the ROM. The frame within the slot still steps by one.
    for (int d : stepsOf(rig(0x11))) CHECK(d == 1);
    for (int d : stepsOf(rig(0x12))) CHECK(d == 2);
}

TEST_CASE("F walks out of one wave slot into the next and wraps at the last", "[driver][wave]")
{
    // Section 103: the bank is one flat table of kWaveSlots slots of kMaxFrames
    // frames, and the jump moves the flat index. Each slot here is filled with
    // its own sample value -- slot s is all `s - 1` -- so the first wave-RAM byte
    // of a tick names the slot the voice is on.
    const auto run = [](int step, int ticks) {
        auto r = std::make_unique<Rig>();
        r->tickHz = 100.0;
        for (int sl = 0; sl < bank::kWaveSlots; ++sl) {
            auto& w = r->bank.waves[size_t(sl)];
            w.used = true; w.frames.assign(size_t(bank::kMaxFrames), bank::Frame{});
            for (auto& f : w.frames) f.s.fill(uint8_t(sl));
        }
        Table t; t.used = true; t.name = "Jump";
        for (int k = 0; k < 16; ++k) t.steps[size_t(k)].cmd1 = { Cmd::F, int16_t((step >> 4) & 15), int16_t(step & 15), 0 };
        r->bank.tables[7] = t;
        auto& i = r->bank.instruments[1];
        i = bank::Instrument::defaults(bank::InstrumentType::Wave, "Flat");
        i.used = true; i.wave = 1; i.frameAdvance = 0; i.table = 8;   // no run of its own; only F moves it
        ChannelParams p; p.instrument = 2; p.velocityMode = 2; r->drv.setParams(2, p);
        std::vector<std::pair<int, int>> seen;   // slot, frame
        // Section 180: a note-on writes the instrument's frame and then the
        // row's F frame, so the last wave RAM write of the block is the one.
        auto slotOf = [](const std::vector<RegWrite>& w) {
            int slot = 0;
            for (const auto& x : w) if (x.addr >= 0xFF30 && x.addr <= 0xFF3F) slot = int(x.value & 15) + 1;
            return slot;
        };
        auto writes = r->block({ Rig::on(2, 60, 100) }, 480);
        seen.push_back({ slotOf(writes), int(r->drv.view(2).frame) - 1 });
        for (int k = 0; k < ticks; ++k) {
            writes = r->block({}, 480);
            const int sl = slotOf(writes);
            seen.push_back({ sl ? sl : seen.back().first, int(r->drv.view(2).frame) - 1 });
        }
        return seen;
    };

    // `F 10` is a whole slot a tick. The note-on fires the table's row 0 in its
    // own tick (section 84), so the first sample is already on slot 2.
    const auto whole = run(0x10, 17);
    for (size_t k = 0; k < whole.size(); ++k) {
        INFO("tick " << k << " slot " << whole[k].first << " frame " << whole[k].second);
        CHECK(whole[k].first == int((k + 1) % size_t(bank::kWaveSlots)) + 1);
        CHECK(whole[k].second == 0);         // the frame within the slot never moves
    }
    // Sixteen jumps of a whole slot come back to where they started: the flat
    // index wraps at kWaveFrames, not at the slot.
    CHECK(whole.front() == whole[size_t(bank::kWaveSlots)]);

    // Half a slot a tick: the frame alternates 0 and 8 and the slot advances on
    // every second jump.
    const auto half = run(0x08, 5);
    CHECK(half[0] == std::make_pair(1, 8));
    CHECK(half[1] == std::make_pair(2, 0));
    CHECK(half[2] == std::make_pair(2, 8));
    CHECK(half[3] == std::make_pair(3, 0));
    CHECK(half[4] == std::make_pair(3, 8));
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

TEST_CASE("a noise vibrato moves once a tick, in map entries", "[driver][noise]")
{
    // Section 119, measured on 9.2.L: V on the noise channel steps once a
    // **tick** by the Tick table's phase, and its depth is in map entries --
    // eight per semitone of the depth table, rounded up.
    Rig r;
    r.tickHz = 100.0;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    for (int i = 0; i < 128; ++i) r.bank.noiseMap[size_t(i)] = uint8_t(i);
    r.bank.noiseMapLen = 128; r.bank.noiseMapNote0 = 1; r.bank.noiseMapSet = true;
    auto& i0 = r.bank.instruments[20];
    i0 = Instrument::defaults(InstrumentType::Noise, "drum");
    i0.used = true; i0.noiseLsdjMap = true; i0.noiseShift = 5;
    ChannelParams p; p.instrument = 21; r.drv.setParams(3, p);

    /// NR43 after each of `n` ticks, and how many times it was written inside one.
    auto walk = [&](uint8_t x, uint8_t y, int n) {
        NoteEvent e = cellOn(3, 64, 21);
        e.cmd1 = { Cmd::V, x, y, 0 };
        std::vector<int> out; int most = 0;
        auto w = r.block({ e }, 480);
        int cur = -1;
        for (int k = 0; k < n; ++k) {
            int writes = 0;
            for (const auto& q : w) if (q.addr == 0xFF22) { ++writes; cur = int(q.value); }
            most = std::max(most, writes);
            out.push_back(cur);
            w = r.block({}, 480);
        }
        r.block({ Rig::off(3, 64) }, 480);
        return std::pair<std::vector<int>, int>{ out, most };
    };
    {   // V 2 F: the deepest swing, four entries a tick, sixteen ticks to the peak.
        const auto [seen, most] = walk(2, 15, 8);
        CHECK(seen[0] == 63);                     // the note itself
        CHECK(seen[1] == 59);
        CHECK(seen[2] == 55);
        CHECK(seen[3] == 51);
        CHECK(seen[7] == 35);
        CHECK(most <= 1);                         // nothing moves between ticks
    }
    {   // V 2 0: the shallowest depth still moves the index by one, and holds
        // it while the triangle is inside one entry.
        const auto [seen, most] = walk(2, 0, 4);
        CHECK(seen[0] == 63);
        CHECK(seen[1] == 62);
        CHECK(seen[2] == 62);
        CHECK(most <= 1);
    }
    {   // The phase is the Tick table's whatever the instrument's PITCH: at
        // speed 8 the cycle is sixteen ticks, so the peak comes after four.
        i0.pitchSpeed = PitchSpeed::Fast;
        const auto [seen, most] = walk(8, 15, 9);
        CHECK(seen[0] == 63);
        CHECK(seen[4] == 63 - 64 + 128);          // the index wraps (section 83)
        CHECK(seen[8] == 63);
        CHECK(most <= 1);
    }
}

TEST_CASE("noise PITCH decides which pitch change restarts the channel", "[driver][noise]")
{
    // Sections 82, 86 and 87, measured on 9.3.9. A three-entry map: entry 0 is
    // 15-bit, entry 1 is 7-bit and entry 2 is 15-bit again, so a table walking
    // the transpose crosses the width both ways.
    const auto rig = [](bool safe, uint8_t length) {
        auto r = std::make_unique<Rig>();
        r->tickHz = 100.0;
        for (auto& src : r->song.noteSource) src = tracker::NoteSource::Tracker;
        r->bank.noiseMap[0] = 0x00; r->bank.noiseMap[1] = 0x08; r->bank.noiseMap[2] = 0x10;
        r->bank.noiseMapLen = 3; r->bank.noiseMapNote0 = 1; r->bank.noiseMapSet = true;
        auto& t = r->bank.tables[9]; t = Table{}; t.used = true; t.end = TableEnd::Stop;
        for (int k = 0; k < 4; ++k) { t.steps[size_t(k)].hasTranspose = true; t.steps[size_t(k)].transpose = int8_t(k); }
        auto& i = r->bank.instruments[20];
        i = Instrument::defaults(InstrumentType::Noise, "drum");
        i.used = true;
        i.noiseLsdjMap = true; i.noiseShift = 5; i.table = 10;
        i.noisePitch = safe ? NoisePitch::Safe : NoisePitch::Free;
        if (length) { i.length = length; i.lengthLatent = true; }
        ChannelParams p; p.instrument = 21; for (int ch = 0; ch < 4; ++ch) r->drv.setParams(ch, p);
        return r;
    };
    const auto walk = [](Rig& r) {
        // The note, then the table's rows one tick apart: (NR43, did it trigger).
        std::vector<std::pair<int, bool>> out;
        auto w = r.block({ cellOn(3, 1, 21) }, 480);
        for (int k = 0; k < 4; ++k) {
            const RegWrite* nr = last(w, 0xFF22);
            if (nr != nullptr) out.push_back({ int(nr->value), anyTrigger(w, 0xFF23) });
            w = r.block({}, 480);
        }
        return out;
    };
    {   // FREE: only the step that turns the 7-bit LFSR **on** restarts it.
        auto r = rig(false, 0);
        const auto seen = walk(*r);
        REQUIRE(seen.size() >= 3);
        CHECK(seen[0] == std::pair<int, bool>{ 0x00, true });    // the note-on
        CHECK(seen[1] == std::pair<int, bool>{ 0x08, true });    // 15 -> 7: a restart
        CHECK(seen[2] == std::pair<int, bool>{ 0x10, false });   // 7 -> 15: none
    }
    {   // SAFE: every pitch change restarts it.
        auto r = rig(true, 0);
        const auto seen = walk(*r);
        REQUIRE(seen.size() >= 3);
        CHECK(seen[0] == std::pair<int, bool>{ 0x00, true });
        CHECK(seen[1] == std::pair<int, bool>{ 0x08, true });
        CHECK(seen[2] == std::pair<int, bool>{ 0x10, true });
    }
    {   // The restart is not a note-on: NRx2 is re-armed at the level the note
        // has reached, so the envelope carries on, and NR44 = BF turns the
        // length counter on -- which is the only thing a latent LENGTH does.
        auto r = rig(true, 0);
        auto& i = r->bank.instruments[20];
        i.env.mode = EnvMode::Shaped; i.env.start = 15; i.env.peak = 0; i.env.attackTicks = 40;
        i.envVol = 15;
        r->block({ cellOn(3, 1, 21) }, 480);
        for (int k = 0; k < 6; ++k) r->block({}, 480);            // let the ramp fall a few levels
        const int level = int(r->drv.view(3).volume);
        auto w = r->block({}, 480);
        const RegWrite* nr4 = last(w, 0xFF23);
        if (nr4 != nullptr && (nr4->value & 0x80)) {
            CHECK(int(nr4->value) == 0xBF);                       // trigger + length enable
            const RegWrite* nr2 = last(w, 0xFF21);
            REQUIRE(nr2 != nullptr);
            CHECK(int(nr2->value & 0x0F) == 0x08);                // a hold, not the instrument's rate
            CHECK(int(nr2->value >> 4) == level);                 // at the level it had reached
        }
    }
    {   // A note-on never enables the counter, however long the LENGTH.
        auto r = rig(false, 1);
        auto w = r->block({ cellOn(3, 1, 21) }, 480);
        const RegWrite* nr4 = last(w, 0xFF23);
        REQUIRE(nr4 != nullptr);
        CHECK(int(nr4->value) == 0x80);
        CHECK(has(w, 0xFF20, 0x3F));                              // 64 - 1, the value LSDj writes
    }
}


TEST_CASE("R x = 8 is a fast retrigger every y + 1 pitch clocks, and R 8 F stops one", "[driver][commands]")
{
    // Section 90, measured on 9.3.9 and 9.2.L. The fast domain runs on the pitch
    // clock rather than the tick, and `y` is the interval there too -- which is
    // what section 76 missed, so a roll ran at every clock whatever y said.
    const auto rollIn = [](int val, uint32_t frames) {
        auto r = std::make_unique<Rig>();
        r->tickHz = 100.0;
        for (auto& src : r->song.noteSource) src = tracker::NoteSource::Tracker;
        auto& i = r->bank.instruments[20];
        i = Instrument::defaults(InstrumentType::Noise, "roll");
        i.used = true;
        ChannelParams p; p.instrument = 21; r->drv.setParams(3, p);
        auto w = r->block({ cellOn(3, 60, 21), cellCmd(3, Command{ Cmd::R, int16_t(val >> 4), int16_t(val & 15), 0 }) }, frames);
        int n = 0;
        for (const auto& x : w) if (x.addr == 0xFF23 && (x.value & 0x80)) ++n;
        return n;
    };
    // 48000 frames is one second; the pitch clock is 11712 cycles of 4194304,
    // so 358 of them a second. R80 fires on each, R81 on every second.
    const int n0 = rollIn(0x80, 24000), n1 = rollIn(0x81, 24000), n3 = rollIn(0x83, 24000);
    CHECK(n0 > n1); CHECK(n1 > n3);
    // Within a tenth: one clock, two clocks, four clocks.
    CHECK(std::abs(double(n1) * 2.0 / double(n0) - 1.0) < 0.1);
    CHECK(std::abs(double(n3) * 4.0 / double(n0) - 1.0) < 0.15);
    // R 8 F starts nothing, and stops a roll that is running.
    CHECK(rollIn(0x8F, 24000) == 1);
    {
        auto r = std::make_unique<Rig>();
        r->tickHz = 100.0;
        for (auto& src : r->song.noteSource) src = tracker::NoteSource::Tracker;
        auto& i = r->bank.instruments[20];
        i = Instrument::defaults(InstrumentType::Noise, "roll"); i.used = true;
        ChannelParams p; p.instrument = 21; r->drv.setParams(3, p);
        r->block({ cellOn(3, 60, 21), cellCmd(3, Command{ Cmd::R, 8, 0, 0 }) }, 4800);
        auto w = r->block({ cellCmd(3, Command{ Cmd::R, 8, 15, 0 }) }, 24000);
        int n = 0;
        for (const auto& x : w) if (x.addr == 0xFF23 && (x.value & 0x80)) ++n;
        CHECK(n <= 1);                                     // it stopped
    }
}


TEST_CASE("a wave note-on loads its frame once, not once per table lane", "[driver][wave]")
{
    // The user's SAMESONG drives every wave instrument from a table of F rows.
    // The ROM loads the frame once a tick; ChipBoy was loading it twice on the
    // tick a note starts, which is 268 extra triggers over that song.
    auto r = std::make_unique<Rig>();
    r->tickHz = 100.0;
    for (auto& src : r->song.noteSource) src = tracker::NoteSource::Tracker;
    auto& w = r->bank.waves[0];
    w.used = true; w.frames.clear();
    for (int f = 0; f < 16; ++f) { bank::Frame fr; fr.s.fill(uint8_t(f)); w.frames.push_back(fr); }
    Table t; t.used = true; t.name = "F rows";
    for (int k = 0; k < 16; ++k) t.steps[size_t(k)].cmd1 = { Cmd::F, 1, 0, 0 };   // including row 0
    r->bank.tables[7] = t;
    auto& i = r->bank.instruments[1];
    i = Instrument::defaults(InstrumentType::Wave, "GUITR");
    i.used = true; i.wave = 1; i.frameLength = 4; i.frameAdvance = 0; i.table = 8;
    ChannelParams p; p.instrument = 2; r->drv.setParams(2, p);
    // One tick a block, so a block is a tick: count the triggers in each.
    std::vector<int> perTick;
    auto w0 = r->block({ cellOn(2, 60, 2) }, 480);
    int n = 0; for (const auto& x : w0) if (x.addr == 0xFF1E && (x.value & 0x80)) ++n;
    perTick.push_back(n);
    for (int k = 0; k < 6; ++k) {
        auto wk = r->block({}, 480);
        n = 0; for (const auto& x : wk) if (x.addr == 0xFF1E && (x.value & 0x80)) ++n;
        perTick.push_back(n);
    }
    INFO("triggers per tick: " << perTick[0] << " " << perTick[1] << " " << perTick[2] << " "
         << perTick[3] << " " << perTick[4] << " " << perTick[5] << " " << perTick[6]);
    // Section 180: the note-on's tick writes the instrument's frame and then
    // row 0's F frame, as the ROM does (REPTCOMP's wave); a tick after it one.
    CHECK(perTick[0] <= 2);
    for (size_t k = 1; k < perTick.size(); ++k) CHECK(perTick[k] <= 1);
}


TEST_CASE("a table's H costs no tick", "[driver][commands]")
{
    // Section 95: four transposes with an H on row 4. The cycle is four ticks,
    // not five, because the row the hop lands on plays in that same tick --
    // which is what makes an LSDj arpeggio keep time.
    Rig r;
    r.tickHz = 100.0;
    Table t; t.used = true; t.name = "Arp";
    const int tsp[4] = { -3, 0, 5, 9 };
    for (int i = 0; i < 4; ++i) { t.steps[size_t(i)].hasTranspose = true; t.steps[size_t(i)].transpose = int8_t(tsp[i]); }
    t.steps[4].cmd1 = { Cmd::H, 0, 0, 0 };                 // hop back to row 0, for ever
    r.bank.tables[7] = t;
    r.bank.instruments[0].table = 8; r.bank.instruments[0].vib.depth = 0;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    r.block({ Rig::on(0, 60, 100) }, 480);
    std::vector<int> notes;
    for (int i = 0; i < 8; ++i) { r.block({}, 480); notes.push_back(int(r.drv.view(0).period)); }
    const int want[8] = { 60, 65, 69, 57, 60, 65, 69, 57 };
    for (int i = 0; i < 8; ++i) { INFO("tick " << i); CHECK(notes[size_t(i)] == note(want[i])); }
    r.bank.instruments[0].table = 0;
}

TEST_CASE("a kit note's VEL column names a second sample, summed through the kit's curve", "[driver][kit]")
{
    // docs/plan-kit-pairs.md: the note column picks one sample, VEL the other
    // by index + 1, and the driver sums them the way LSDj's mixer does (S117).
    Rig r;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    auto& k = r.bank.kits[0];
    k = bank::Kit{};
    k.used = true; k.name = "Pair"; k.period = 1865;
    std::vector<uint8_t> ramp(256), full(256, uint8_t(15));
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = uint8_t((i + 1) % 16);
    bank::KitSample a; a.note = 60; a.data = ramp;
    bank::KitSample b; b.note = 61; b.data = full;
    k.samples.push_back(a); k.samples.push_back(b);
    auto& i0 = r.bank.instruments[1];
    i0 = Instrument::defaults(InstrumentType::Kit, "Pair");
    i0.used = true; i0.kit = 1;
    ChannelParams p; p.instrument = 2; r.drv.setParams(2, p);

    /// The sixteen bytes of the first frame, as 32 nibbles. Section 172: the
    /// frame comes with the instant after the note-on, so the block spans one.
    auto firstChunk = [&](uint8_t vel) {
        auto w = r.block({ cellOn(2, 60, 2, vel) }, 480);
        std::array<int, 16> byteOf{}; byteOf.fill(-1);
        for (const auto& x : w) if (x.addr >= 0xFF30 && x.addr <= 0xFF3F && byteOf[size_t(x.addr - 0xFF30)] < 0) byteOf[size_t(x.addr - 0xFF30)] = x.value;
        std::vector<int> nib;
        for (int j = 0; j < 16; ++j) { nib.push_back(byteOf[size_t(j)] >> 4); nib.push_back(byteOf[size_t(j)] & 15); }
        r.block({ Rig::off(2, 60) }, 64);
        return nib;
    };
    for (auto mode : { bank::KitDist::Clip, bank::KitDist::Soft, bank::KitDist::Fold, bank::KitDist::Fold2, bank::KitDist::Wrap }) {
        k.dist = mode;
        const auto got = firstChunk(2);
        REQUIRE(got.size() == 32);
        for (int j = 0; j < 32; ++j) CHECK(got[size_t(j)] == int(bank::kitMix(mode, j, ramp[size_t(j)], 15)));
    }
    k.dist = bank::KitDist::Clip;
    // A VEL past the end of the kit -- every ordinary velocity -- plays one sample.
    const auto alone = firstChunk(100);
    for (int j = 0; j < 32; ++j) CHECK(alone[size_t(j)] == int(ramp[size_t(j)]));
    // And the note still ends with the **first** sample, however long the second is.
    k.samples[1].data.assign(4096, uint8_t(8));
    r.block({ cellOn(2, 60, 2, 2) }, 64);
    for (int j = 0; j < 12; ++j) r.block({}, 4800);
    CHECK_FALSE(r.drv.view(2).dacOn);                        // section 186: quiet, the note itself kept
}

TEST_CASE("a raw kit page in video RAM reads back FF in the LCD's mode 3: FE bytes, one every three or four, drifting", "[driver][kit][rom942]")
{
    // Section 184: the mixer reads a video RAM page twice a byte, 140 cycles a
    // byte, and a read in mode 3 (176 of every 456 cycles on 144 of 154
    // lines) is `$FF`, so a byte with both reads there is `FE`. One line is
    // 3.26 bytes, so a frame carries four or five `FE`s a line apart; a page
    // in work RAM (the flag off) carries none.
    Rig r;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    auto& k = r.bank.kits[0];
    k = bank::Kit{};
    k.used = true; k.name = "Vram"; k.period = 1865;
    std::vector<uint8_t> ramp(4096), full(4096, uint8_t(15));
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = uint8_t((i + 1) % 16);
    bank::KitSample a; a.note = 60; a.data = ramp;
    bank::KitSample b; b.note = 61; b.data = full;
    k.samples.push_back(a); k.samples.push_back(b);
    k.dist = bank::KitDist::Raw; k.distTable.assign(256, uint8_t(0x33)); k.distVram = true;
    auto& i0 = r.bank.instruments[1];
    i0 = Instrument::defaults(InstrumentType::Kit, "Vram");
    i0.used = true; i0.kit = 1;
    ChannelParams p; p.instrument = 2; r.drv.setParams(2, p);
    const auto frames = [&](int n) {
        std::vector<std::array<int, 16>> out;
        auto w = r.block({ cellOn(2, 60, 2, 2) }, 480);
        for (int i = 0; i < n; ++i) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
        std::array<int, 16> cur{}; int have = 0;
        for (const auto& x : w) {
            if (x.addr < 0xFF30 || x.addr > 0xFF3F) continue;
            if (x.addr == 0xFF30) { have = 0; cur.fill(-1); }
            cur[size_t(x.addr - 0xFF30)] = x.value;
            if (++have == 16) out.push_back(cur);
        }
        r.block({ Rig::off(2, 60) }, 64);
        return out;
    };
    const auto got = frames(20);
    REQUIRE(got.size() >= 20);
    int total = 0; std::vector<std::string> pattern;
    for (const auto& f : got) {
        int n = 0; std::string s;
        for (int j = 0; j < 16; ++j) { const bool fe = f[size_t(j)] == 0xFE; n += fe; s += fe ? '#' : '.'; }
        CHECK(n >= 3); CHECK(n <= 8);                  // four to seven a frame, a line apart, in runs of one or two
        CHECK(s.find("####") == std::string::npos);
        total += n; pattern.push_back(s);
    }
    CHECK(total >= 4 * int(got.size())); CHECK(total <= 6 * int(got.size()));
    // A byte that is not FE is the page's mix (33 swapped + 33 = 66), or a
    // half-hit (F3 + 33 = 26, or 33 + FF = 32 -- one read in mode 3).
    for (const auto& f : got) for (int j = 0; j < 16; ++j) CHECK((f[size_t(j)] == 0xFE || f[size_t(j)] == 0x66 || f[size_t(j)] == 0x26 || f[size_t(j)] == 0x32));
    // The same page in work RAM: no FE at all.
    k.distVram = false;
    for (const auto& f : frames(20)) for (int j = 0; j < 16; ++j) CHECK(f[size_t(j)] == 0x66);
}

TEST_CASE("a retrigger resets a DRUM wave's pitch to the entry, fraction and bend gone", "[driver][commands][rom942]")
{
    // Section 185 (X92_Wv_drumR): a DRUM note-on writes its entry's period and
    // the refresh writes the exact one, a few units on (section 169); under
    // `R 03` the R's own retrigger resets the offset word, so the exact period
    // never comes and every roll starts from the entry again.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[2] = tracker::NoteSource::Tracker;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Wave, "Kick");
    i.used = true; i.pitchSpeed = bank::PitchSpeed::Drum; i.waveLevel = 3;
    ChannelParams p; p.instrument = 2; r.drv.setParams(2, p);
    const auto lowsOf = [](const std::vector<RegWrite>& w) {
        std::vector<int> out; int lo = -1;
        for (const auto& x : w) { if (x.addr == 0xFF1D) lo = x.value; else if (x.addr == 0xFF1E && lo >= 0) { out.push_back(((x.value & 7) << 8) | lo); lo = -1; } }
        out.erase(std::remove(out.begin(), out.end(), 0x7E0), out.end());   // the frame writer's pre-trigger period (section 171), not the note's
        return out;
    };
    // The plain note: the entry (1700 for note 55, index 89), then the exact
    // period at the refresh (1714).
    auto w = r.block({ cellOn(2, 55, 2) }, 480);
    for (int k = 0; k < 3; ++k) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
    const auto plain = lowsOf(w);
    REQUIRE(plain.size() >= 2);
    const int entry = plain[0], exact = plain[1];
    REQUIRE(entry != exact);
    r.block({ Rig::off(2, 55) }, 64);
    // The same note under R 03: the entry, never the exact one, and the rolls.
    NoteEvent on = cellOn(2, 55, 2); on.cmd1 = { Cmd::R, 0, 3, 0 };
    w = r.block({ on }, 480);
    for (int k = 0; k < 9; ++k) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
    int trig = 0;
    for (const auto& x : w) if (x.addr == 0xFF1E && (x.value & 0x80)) ++trig;
    CHECK(trig >= 4);
    const auto rolled = lowsOf(w);
    REQUIRE_FALSE(rolled.empty());
    CHECK(rolled.front() == entry);
    CHECK(std::find(rolled.begin(), rolled.end(), exact) == rolled.end());
}

TEST_CASE("a roll leaves a DRUM wave's pitch running when the instrument keeps it", "[driver][commands][versions]")
{
    // Section 195: every LSDj before 9.4.0 -- the retrigger does not touch the
    // offset word, so the refresh's exact period still comes under `R 03`.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[2] = tracker::NoteSource::Tracker;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Wave, "Kick");
    i.used = true; i.pitchSpeed = bank::PitchSpeed::Drum; i.waveLevel = 3; i.retrigKeepsPitch = true;
    ChannelParams p; p.instrument = 2; r.drv.setParams(2, p);
    const auto lowsOf = [](const std::vector<RegWrite>& w) {
        std::vector<int> out; int lo = -1;
        for (const auto& x : w) { if (x.addr == 0xFF1D) lo = x.value; else if (x.addr == 0xFF1E && lo >= 0) { out.push_back(((x.value & 7) << 8) | lo); lo = -1; } }
        out.erase(std::remove(out.begin(), out.end(), 0x7E0), out.end());
        return out;
    };
    auto w = r.block({ cellOn(2, 55, 2) }, 480);
    for (int k = 0; k < 3; ++k) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
    const auto plain = lowsOf(w);
    REQUIRE(plain.size() >= 2);
    const int entry = plain[0], exact = plain[1];
    REQUIRE(entry != exact);
    r.block({ Rig::off(2, 55) }, 64);
    NoteEvent on = cellOn(2, 55, 2); on.cmd1 = { Cmd::R, 0, 3, 0 };
    w = r.block({ on }, 480);
    for (int k = 0; k < 9; ++k) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
    const auto rolled = lowsOf(w);
    REQUIRE_FALSE(rolled.empty());
    CHECK(rolled.front() == entry);
    CHECK(std::find(rolled.begin(), rolled.end(), exact) != rolled.end());   // the exact period arrives and the rolls keep it
}

TEST_CASE("a roll keeps rolling while the table it replays bends a DRUM pitch", "[driver][commands][rom942]")
{
    // X92_Wv_drumR: CASTSHDW's kick -- a DRUM wave with `P A9` on its table's
    // row 0 -- under `R 03` rolls every three ticks on the ROM for as long as
    // the note lasts, each roll replaying the P from the entry.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[2] = tracker::NoteSource::Tracker;
    // Section 185: and a ONCE run's end -- here a one-frame run three ticks
    // long, so it would end on every roll's tick -- neither stops the roll
    // nor writes its flat frame, the roll starting the run over first.
    Table t0; t0.used = true; t0.steps[0].cmd1 = { Cmd::P, 0xA9, 0, 0 };
    r.bank.tables[0] = t0;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Wave, "Kick");
    i.used = true; i.pitchSpeed = bank::PitchSpeed::Drum; i.waveLevel = 3; i.table = 1;
    i.frameLoop = bank::FrameLoop::Once; i.frameAdvance = 3; i.frameStart = 15;
    ChannelParams p; p.instrument = 2; r.drv.setParams(2, p);
    NoteEvent on = cellOn(2, 55, 2); on.cmd1 = { Cmd::R, 0, 3, 0 };
    auto w = r.block({ on }, 480);
    for (int k = 0; k < 12; ++k) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
    int trig = 0; bool flat = false;
    for (const auto& x : w) { if (x.addr == 0xFF1E && (x.value & 0x80)) ++trig; if (x.addr == 0xFF30 && x.value == 0x77) flat = true; }
    CHECK(trig >= 5);                                    // the note, its own R, and the rolls at ticks 3, 6, 9, 12
    CHECK_FALSE(flat);
    // With the roll every eight ticks the run ends at tick 3, and the roll
    // still comes at 8 and 16 and starts it again (X92_Wv_onceR8); the
    // ONCE end used to clear the roll with the fast retrigger.
    r.block({ Rig::off(2, 55) }, 64);
    on.cmd1 = { Cmd::R, 0, 8, 0 };
    w = r.block({ on }, 480);
    for (int k = 0; k < 18; ++k) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
    trig = 0;
    for (const auto& x : w) if (x.addr == 0xFF1E && (x.value & 0x80)) ++trig;
    CHECK(trig >= 4);
}

TEST_CASE("a kit's samples start over on a roll and on a kit F, even after they had ended", "[driver][kit][rom942]")
{
    // Section 186 (X92_KIT_R04, X92_KIT_F01): the ROM's `R 04` on a kit note
    // plays the kit's first frames again every four ticks; a bare `F 01` two
    // rows after the note plays the sample again from its second frame,
    // although it had ended.
    Rig r;
    r.tickHz = 100.0;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    auto& k = r.bank.kits[0];
    k = bank::Kit{};
    k.used = true; k.name = "Short"; k.period = 1865;
    std::vector<uint8_t> ramp(96);                       // three frames: 0..15, 0..15, 0..15 with the frame in the high nibble
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = uint8_t(i % 16);
    ramp[0] = 9; ramp[32] = 10; ramp[64] = 11;           // each frame's first nibble names it
    bank::KitSample a; a.note = 60; a.data = ramp;
    k.samples.push_back(a);
    auto& i0 = r.bank.instruments[1];
    i0 = Instrument::defaults(InstrumentType::Kit, "Short");
    i0.used = true; i0.kit = 1;
    ChannelParams p; p.instrument = 2; r.drv.setParams(2, p);
    const auto firstBytes = [](const std::vector<RegWrite>& w) { std::vector<int> out; for (const auto& x : w) if (x.addr == 0xFF30) out.push_back(x.value >> 4); return out; };
    NoteEvent on = cellOn(2, 60, 2); on.cmd1 = { Cmd::R, 0, 4, 0 };
    auto w = r.block({ on }, 480);
    for (int t = 0; t < 12; ++t) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
    auto fb = firstBytes(w);
    // The three frames, then -- the sample over -- the roll at tick 4 and 8 and 12 plays them again.
    CHECK(std::count(fb.begin(), fb.end(), 9) >= 3);
    r.block({ Rig::off(2, 60) }, 64);
    // A plain note, and a bare F 01 four ticks on, after the three frames have played.
    w = r.block({ cellOn(2, 60, 2) }, 480);
    for (int t = 0; t < 4; ++t) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
    fb = firstBytes(w);
    CHECK(std::count(fb.begin(), fb.end(), 9) == 1); CHECK(std::count(fb.begin(), fb.end(), 11) == 1);
    w = r.block({ cellCmd(2, { Cmd::F, 0, 1, 0 }) }, 480);
    for (int t = 0; t < 4; ++t) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
    fb = firstBytes(w);
    CHECK(std::count(fb.begin(), fb.end(), 9) == 0);     // from the second frame
    CHECK(std::count(fb.begin(), fb.end(), 10) == 1); CHECK(std::count(fb.begin(), fb.end(), 11) == 1);
}

TEST_CASE("P on a kit moves the period register, not the note", "[driver][kit]")
{
    // Section 97: the byte is period-register units -- three of them once under
    // STEP, one a pitch clock under FAST.
    Rig r;
    r.tickHz = 100.0;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    auto& k = r.bank.kits[0];
    k = bank::Kit{};
    k.used = true; k.name = "Drums"; k.period = 1865;
    bank::KitSample s; s.note = 60; s.data.assign(1024, uint8_t(8)); s.loopPoint = 0;
    k.samples.push_back(s);
    auto& i = r.bank.instruments[1];
    i = Instrument::defaults(InstrumentType::Kit, "Drums");
    i.used = true; i.kit = 1; i.pitchSpeed = PitchSpeed::Step;
    ChannelParams p; p.instrument = 2; r.drv.setParams(2, p);
    // The view has no period for a kit, so read the register the block wrote.
    int lo = 0, hi = 0;
    auto per = [&](const std::vector<RegWrite>& w) {
        for (const auto& x : w) { if (x.addr == 0xFF1D) lo = x.value; else if (x.addr == 0xFF1E) hi = x.value & 7; }
        return (hi << 8) | lo;
    };
    const int base = per(r.block({ cellOn(2, 60, 2) }, 480));
    CHECK(base > 0);
    NoteEvent e = cellOn(2, 60, 2); e.cmd1 = { Cmd::P, 2, 0, 0 };
    CHECK(per(r.block({ e }, 480)) == base + 6);            // three units a unit, once
    CHECK(per(r.block({}, 480)) == base + 6);               // and no bend after it
    i.pitchSpeed = PitchSpeed::Fast;
    NoteEvent e2 = cellOn(2, 60, 2); e2.cmd1 = { Cmd::P, 2, 0, 0 };
    const int after = per(r.block({ e2 }, 480));
    CHECK(after > base);                                    // a unit a pitch clock
    CHECK(per(r.block({}, 480)) > after);                   // and it keeps going
}


TEST_CASE("a slide replaces a running bend, and in Drum it walks the register", "[driver][commands]")
{
    // Section 99: `P A0` then `L 30` a tick later is SAMESONG's wave kick. The
    // bend must stop when the slide starts -- left running it takes the period
    // off the bottom of the register and wraps it round, which is heard as a
    // second kick -- and in Drum the slide moves a fixed number of period
    // **units** an update rather than a fixed number of semitones.
    Rig r;
    r.tickHz = 100.0;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    auto& w = r.bank.waves[0];
    w.used = true; w.frames.clear();
    for (int f = 0; f < 16; ++f) { bank::Frame fr; fr.s.fill(uint8_t(f)); w.frames.push_back(fr); }
    Table t; t.used = true; t.name = "Kick";
    t.steps[0].cmd1 = { Cmd::P, int16_t(0xA0), 0, 0 };                 // -96: a steep drop
    t.steps[1].hasTranspose = true; t.steps[1].transpose = int8_t(-128);
    t.steps[1].cmd1 = { Cmd::L, 0x30, 0, 0 };
    r.bank.tables[7] = t;
    auto& i = r.bank.instruments[1];
    i = Instrument::defaults(InstrumentType::Wave, "Kick");
    i.used = true; i.wave = 1; i.table = 8; i.pitchSpeed = PitchSpeed::Drum;
    i.frameLength = 1; i.frameAdvance = 0; i.vib.depth = 0;
    ChannelParams p; p.instrument = 2; r.drv.setParams(2, p);
    std::vector<int> per; int lo = 0;
    auto collect = [&](const std::vector<RegWrite>& ws) {
        for (const auto& x : ws) {
            if (x.addr == 0xFF1D) lo = x.value;
            else if (x.addr == 0xFF1E && !(x.value & 0x80)) {
                const int q = ((x.value & 7) << 8) | lo;
                if (per.empty() || per.back() != q) per.push_back(q);
            }
        }
    };
    collect(r.block({ cellOn(2, 96, 2) }, 480));
    for (int k = 0; k < 12; ++k) collect(r.block({}, 480));
    REQUIRE(per.size() > 10);
    // Section 180: the trigger is the DRUM table's entry below the note and the
    // refresh the note itself (section 169), so the walk down starts there.
    per.erase(per.begin());
    // Every step down, never back up: a wrap would show as a jump to the top.
    for (size_t k = 1; k < per.size(); ++k) { INFO("step " << k << " of " << per.size()); CHECK(per[k] <= per[k - 1]); }
    // The bend's own steps are the steep ones; after the slide takes over the
    // steps are smaller and all the same size, which is what "in the register"
    // means -- a semitone slide's steps grow as the period falls.
    std::vector<int> d;
    for (size_t k = 1; k < per.size(); ++k) d.push_back(per[k - 1] - per[k]);
    const int late = int(d.size()) - 4;
    REQUIRE(late > 2);
    for (int k = late; k < int(d.size()); ++k) { INFO("late step " << k); CHECK(std::abs(d[size_t(k)] - d[size_t(late)]) <= 1); }
    CHECK(d[size_t(late)] < d[0]);                       // and slower than the bend was
}

TEST_CASE("a wave frame is written at the wave's sync boundary, through the ROM's writer", "[driver][wave]")
{
    // Section 171. A run stepping every tick at 163 BPM on a note whose period
    // is $797: the sync period is four wave cycles, 26880 cycles, and each
    // frame lands within a DIV tick and a write's worth of a boundary counted
    // from the trigger before it -- never on the tick itself.
    Rig r;
    r.tickHz = 163.0 * 24.0 / 60.0;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    auto& w = r.bank.waves[0];
    w.used = true; w.frames.clear();
    for (int f = 0; f < 16; ++f) { bank::Frame fr; fr.s.fill(uint8_t(f)); w.frames.push_back(fr); }
    auto& i = r.bank.instruments[1];
    i = Instrument::defaults(InstrumentType::Wave, "Run");
    i.used = true; i.wave = 1; i.frameLength = 4; i.frameAdvance = 1; i.frameLoop = FrameLoop::Loop; i.vib.depth = 0;
    ChannelParams p; p.instrument = 2; r.drv.setParams(2, p);
    std::vector<RegWrite> all;
    auto collect = [&](const std::vector<RegWrite>& ws) { all.insert(all.end(), ws.begin(), ws.end()); };
    collect(r.block({ cellOn(2, 75, 2) }, 480));                      // D#5: wave period $797
    for (int k = 0; k < 40; ++k) collect(r.block({}, 480));
    // The writer's order after the bytes: on, the $7E0 pre-trigger, the pan, the period.
    std::vector<uint64_t> offs;
    for (size_t k = 0; k + 6 < all.size(); ++k) {
        if (all[k].addr != 0xFF1A || all[k].value != 0x00) continue;
        offs.push_back(all[k].cycle);
        size_t j = k + 1;
        while (j < all.size() && all[j].addr >= 0xFF30 && all[j].addr <= 0xFF3F) ++j;
        REQUIRE(j + 5 < all.size());
        CHECK(all[j].addr == 0xFF1A); CHECK(all[j].value == 0x80);
        CHECK(all[j + 1].addr == 0xFF1D); CHECK(all[j + 1].value == 0xE0);
        CHECK(all[j + 2].addr == 0xFF1E); CHECK(all[j + 2].value == 0x87);
        CHECK(all[j + 3].addr == 0xFF25);
        CHECK(all[j + 4].addr == 0xFF1D); CHECK(all[j + 4].value == 0x97);
        CHECK(all[j + 5].addr == 0xFF1E); CHECK(all[j + 5].value == 0x07);
    }
    REQUIRE(offs.size() >= 8);
    const uint64_t tick = uint64_t(4194304.0 * 60.0 / (24.0 * 163.0));
    for (size_t k = 1; k < offs.size(); ++k) {
        const uint64_t gap = offs[k] - offs[k - 1];
        INFO("frame " << k << " gap " << gap);
        const uint64_t m = gap % 26880;                    // on the grid, within DIV's 256-cycle tick and the writes
        CHECK((m < 400 || m > 26880 - 300));
        CHECK(gap >= 26880); CHECK(gap <= tick + 26880);   // never earlier than its tick, never a period later
    }
}

TEST_CASE("the sync phase carries what an earlier period left in it across a pitch jump", "[driver][wave][rom942]")
{
    // Section 178. A low note (sync period 843 units, unshifted) for three
    // ticks, a table transpose to a high one (298 units) on tick 3, and a
    // frame queued on tick 4: its boundary counts from the last 843-unit
    // boundary before the jump, not from the trigger.
    Rig r;
    r.tickHz = 50.0;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    auto& w = r.bank.waves[0];
    w.used = true; w.frames.clear();
    for (int f = 0; f < 16; ++f) { bank::Frame fr; fr.s.fill(uint8_t(f)); w.frames.push_back(fr); }
    auto& t = r.bank.tables[0]; t = bank::Table{}; t.used = true; t.end = TableEnd::Stop;
    for (int row = 3; row < 16; ++row) { t.steps[size_t(row)].hasTranspose = true; t.steps[size_t(row)].transpose = 30; }
    auto& i = r.bank.instruments[1];
    i = Instrument::defaults(InstrumentType::Wave, "Jump");
    i.used = true; i.wave = 1; i.table = 1; i.frameLength = 4; i.frameAdvance = 4; i.frameLoop = FrameLoop::Loop; i.vib.depth = 0;
    ChannelParams p; p.instrument = 2; r.drv.setParams(2, p);
    std::vector<RegWrite> all;
    auto collect = [&](const std::vector<RegWrite>& ws) { all.insert(all.end(), ws.begin(), ws.end()); };
    collect(r.block({ cellOn(2, 39, 2) }, 960));
    for (int k = 0; k < 12; ++k) collect(r.block({}, 960));
    const auto units = [](int per) { int s = 2048 - per; for (int k = 0; s < 256 && k < 6; ++k) s <<= 1; return uint64_t(s); };
    const uint64_t s1 = units(Driver::periodOfSemitone(39, true)), s2 = units(Driver::periodOfSemitone(69, true));
    REQUIRE(s1 == 843); REQUIRE(s2 == 298);
    const uint64_t tick = uint64_t(4194304.0 / 50.0);
    std::vector<uint64_t> offs;
    for (const auto& x : all) if (x.addr == 0xFF1A && x.value == 0x00) offs.push_back(x.cycle);
    REQUIRE(offs.size() >= 2);
    const uint64_t t0 = offs[0];                                   // the note's own writer zeroes the word
    REQUIRE(offs[1] > 4 * tick);                                   // the run's first step, on tick 4
    const uint64_t q = (3 * tick - t0) / (64 * s1);                // whole old-period grids before the jump
    REQUIRE(q == 4);
    const uint64_t w1 = offs[1] - t0 - q * s1 * 64;                // from the last old boundary, in the new period
    const uint64_t m = w1 % (s2 * 64);
    INFO("write " << offs[1] << " t0 " << t0 << " m " << m);
    CHECK((m < 400 || m > s2 * 64 - 400));
    CHECK(((offs[1] - t0) % (s2 * 64)) > 4000);                    // section 171's from-scratch grid would not put it here
}

TEST_CASE("a wave R's own fire is NR30, NR32 and the trigger; a tick's retrigger writes the frame again", "[driver][wave][rom942]")
{
    // Section 180 (R01_ph_ch2): the note-on's writer clears the ROM's frame
    // flag, so the R's fire 1 ms in is the short form; the tick's retrigger
    // reloads the instrument and runs the writer.
    Rig r;
    r.tickHz = 100.0;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    auto& w = r.bank.waves[0];
    w.used = true; w.frames.clear();
    for (int f = 0; f < 16; ++f) { bank::Frame fr; fr.s.fill(uint8_t(f)); w.frames.push_back(fr); }
    auto& i = r.bank.instruments[1];
    i = Instrument::defaults(InstrumentType::Wave, "Roll");
    i.used = true; i.wave = 1; i.frameAdvance = 0; i.vib.depth = 0;
    ChannelParams p; p.instrument = 2; r.drv.setParams(2, p);
    NoteEvent on = cellOn(2, 60, 2); on.cmd1 = { Cmd::R, 0, 1, 0 };
    std::vector<RegWrite> all = r.block({ on }, 480);
    { auto more = r.block({}, 480); all.insert(all.end(), more.begin(), more.end()); }
    std::vector<size_t> trig;
    for (size_t k = 0; k < all.size(); ++k) if (all[k].addr == 0xFF1E && (all[k].value & 0x80)) trig.push_back(k);
    REQUIRE(trig.size() >= 3);                        // the note's own $7E0 pre-trigger, the R's fire, the tick's writer's
    // The R's fire: NR30 = 80, NR32, NR34 | 80 -- no NR33, no wave RAM.
    const size_t k1 = trig[1];
    REQUIRE(k1 >= 2);
    CHECK(all[k1 - 2].addr == 0xFF1A); CHECK(all[k1 - 2].value == 0x80);
    CHECK(all[k1 - 1].addr == 0xFF1C);
    bool ramBetween = false;
    for (size_t k = trig[0] + 1; k < k1; ++k) if (all[k].addr >= 0xFF30 && all[k].addr <= 0xFF3F) ramBetween = true;
    CHECK_FALSE(ramBetween);
    CHECK(all[k1].cycle - all[trig[0]].cycle > 3000);   // a millisecond in, not in the burst
    // The tick's: the writer, NR30 = 00 and sixteen bytes before its pre-trigger.
    const size_t k2 = trig[2];
    bool ramBefore = false;
    for (size_t k = k1 + 1; k < k2; ++k) if (all[k].addr >= 0xFF30 && all[k].addr <= 0xFF3F) ramBefore = true;
    CHECK(ramBefore);
}

TEST_CASE("a ONCE run's end halts a slide where it stands", "[driver][wave][rom942]")
{
    // Section 181 (CASTSHDW's kick): a two-frame ONCE run a tick a frame, a
    // cell P bend on the note. The run ends after its second frame; the ROM's
    // wave stop zeroes the step there, so the period holds from then on.
    Rig r;
    r.tickHz = 100.0;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    auto& w = r.bank.waves[0];
    w.used = true; w.frames.clear();
    for (int f = 0; f < 16; ++f) { bank::Frame fr; fr.s.fill(uint8_t(f)); w.frames.push_back(fr); }
    auto& i = r.bank.instruments[1];
    i = Instrument::defaults(InstrumentType::Wave, "Kick");
    i.used = true; i.wave = 1; i.frameLength = 2; i.frameAdvance = 1; i.frameLoop = FrameLoop::Once; i.vib.depth = 0;
    ChannelParams p; p.instrument = 2; r.drv.setParams(2, p);
    NoteEvent on = cellOn(2, 60, 2); on.cmd1 = { Cmd::P, int16_t(0xF0), 0, 0 };   // -16: a steady fall
    std::vector<RegWrite> all = r.block({ on }, 480);
    for (int k = 0; k < 8; ++k) { auto more = r.block({}, 480); all.insert(all.end(), more.begin(), more.end()); }
    // The flat frame marks the end; every period write after it repeats the last.
    size_t flatAt = 0;
    for (size_t k = 0; k + 15 < all.size(); ++k)
        if (all[k].addr == 0xFF30 && all[k].value == 0x77 && all[k + 15].addr == 0xFF3F && all[k + 15].value == 0x77) { flatAt = k; break; }
    REQUIRE(flatAt > 0);
    int lo = -1, per = -1, moved = 0, after = 0, strayed = 0;
    for (size_t k = 0; k < all.size(); ++k) {
        const auto& x = all[k];
        if (x.addr == 0xFF1D) lo = x.value;
        else if (x.addr == 0xFF1E && !(x.value & 0x80) && lo >= 0) {
            const int q = ((x.value & 7) << 8) | lo;
            if (k < flatAt) { if (per >= 0 && q != per) ++moved; per = q; }
            else { ++after; if (q != per) ++strayed; }
        }
    }
    CHECK(moved >= 3);                      // the bend ran while the run played
    CHECK(after >= 1);                      // the flat frame carries the period it stopped at
    CHECK(strayed == 0);                    // and nothing moves it afterwards
}

TEST_CASE("RESYNC writes each frame at its tick, and a run starts at the instrument's start frame", "[driver][wave]")
{
    // Section 171: PLAY = 4 retriggers at the tick, no boundary; and a run of
    // eight from start frame 4 plays 4 6 8 A D F, then the next slot's 1 and 3.
    Rig r;
    r.tickHz = 100.0;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    for (int slot = 0; slot < 2; ++slot) {
        auto& w = r.bank.waves[size_t(slot)];
        w.used = true; w.frames.clear();
        for (int f = 0; f < 16; ++f) { bank::Frame fr; fr.s.fill(uint8_t(f)); fr.s[0] = uint8_t(slot); w.frames.push_back(fr); }
    }
    auto& i = r.bank.instruments[1];
    i = Instrument::defaults(InstrumentType::Wave, "Resync");
    i.used = true; i.wave = 1; i.frameLength = 8; i.frameAdvance = 2; i.frameLoop = FrameLoop::Resync; i.frameStart = 4; i.vib.depth = 0;
    ChannelParams p; p.instrument = 2; r.drv.setParams(2, p);
    std::vector<RegWrite> all;
    auto collect = [&](const std::vector<RegWrite>& ws) { all.insert(all.end(), ws.begin(), ws.end()); };
    collect(r.block({ cellOn(2, 60, 2) }, 480));
    for (int k = 0; k < 40; ++k) collect(r.block({}, 480));
    std::vector<int> w0; std::vector<uint64_t> at;
    for (size_t k = 0; k + 1 < all.size(); ++k)
        if (all[k].addr == 0xFF1A && all[k].value == 0x00 && all[k + 1].addr == 0xFF30) { w0.push_back(all[k + 1].value); at.push_back(all[k].cycle); }
    REQUIRE(w0.size() >= 12);
    // W0 = slot << 4 | frame: slot 1's frames 4 6 8 A D F, slot 2's 1 3, then back down.
    const int want[12] = { 0x04, 0x06, 0x08, 0x0A, 0x0D, 0x0F, 0x11, 0x13, 0x11, 0x0F, 0x0D, 0x0A };
    for (int k = 0; k < 12; ++k) { INFO("frame " << k); CHECK(w0[size_t(k)] == want[k]); }
    const uint64_t two = uint64_t(2.0 * 4194304.0 / 100.0);
    for (size_t k = 2; k < at.size(); ++k) { INFO("step " << k); CHECK(at[k] - at[k - 1] <= two + 400); CHECK(at[k] - at[k - 1] + 400 >= two); }
}

TEST_CASE("a table's volume column on the wave channel is the NR32 level", "[driver][wave]")
{
    // Section 108, measured on 9.2.L across all sixteen amplitudes: the column's
    // amplitude selects the level by `amplitude & 3` -- 0 mute, 1 25 %, 2 50 %,
    // 3 100 % -- and wraps every four. `SAMESONG`'s table 0F is 1, 2, 3, a
    // swell, and `vol / 4` had made every one of them mute.
    const auto levels = [](int a, int b, int c) {
        auto r = std::make_unique<Rig>();
        r->tickHz = 100.0;
        auto& w = r->bank.waves[0];
        w.used = true;
        for (auto& f : w.frames) f.s.fill(8);
        Table t; t.used = true; t.name = "Swell";
        t.steps[0].vol = int8_t(a); t.steps[0].volTicks = 1;
        t.steps[1].vol = int8_t(b); t.steps[1].volTicks = 1;
        t.steps[2].vol = int8_t(c); t.steps[2].volTicks = 1;
        r->bank.tables[5] = t;
        auto& i = r->bank.instruments[1];
        i = bank::Instrument::defaults(bank::InstrumentType::Wave, "Bass");
        i.used = true; i.wave = 1; i.waveLevel = 3; i.table = 6; i.frameAdvance = 0;
        ChannelParams p; p.instrument = 2; p.velocityMode = 2; r->drv.setParams(2, p);
        std::vector<int> out;
        auto take = [&out](const std::vector<RegWrite>& ws) {
            for (const auto& x : ws) if (x.addr == 0xFF1C) out.push_back((x.value >> 5) & 3);
        };
        take(r->block({ Rig::on(2, 60, 100) }, 480));
        for (int k = 0; k < 6; ++k) take(r->block({}, 480));
        return out;
    };
    // NR32's own bits run the other way round from LSDj's numbering: level 1 is
    // 25 %, which is the bit pattern 3. The note's own level comes first.
    const auto swell = levels(1, 2, 3);
    INFO("swell " << swell.size() << " writes");
    REQUIRE(swell.size() >= 3);
    const std::vector<int> wantSwell{ 3, 2, 1 };
    CHECK(std::vector<int>(swell.end() - 3, swell.end()) == wantSwell);
    // And it wraps every four rather than clamping.
    const auto wrapped = levels(4, 5, 7);
    REQUIRE(wrapped.size() >= 3);
    const std::vector<int> wantWrap{ 0, 3, 1 };
    CHECK(std::vector<int>(wrapped.end() - 3, wrapped.end()) == wantWrap);
}

TEST_CASE("E on a channel a K has killed runs a whole envelope", "[driver][noise]")
{
    // Section 109, measured on 9.2.L: `E x y` is the plain NRx2 byte and a whole
    // envelope -- the level goes to x and then to **zero** at rate y -- and a
    // channel a K has killed answers it just the same. That is how `SAMESONG`'s
    // hats get their ghost notes; ChipBoy walked the level to x and stopped,
    // because the kill had torn the voice down and the envelope stopped running.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[3] = tracker::NoteSource::Tracker;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Noise, "Hat");
    i.used = true; i.envVol = 15; i.envRate = 0; i.envDir = bank::EnvDir::Up;
    ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(3, p);
    NoteEvent hit = cellOn(3, 60, 2);
    hit.cmd1 = { Cmd::K, 1, 0, 0 };
    r.block({ hit }, 480);
    r.block({}, 480);                                   // the kill falls due
    r.block({ cellCmd(3, { Cmd::E, 4, 1, 0 }) }, 480);  // the ghost hit
    CHECK(int(r.drv.view(3).envVol) == 4);
    // It does not sit there: at rate 1 the envelope takes it to zero inside a
    // hundred milliseconds.
    for (int k = 0; k < 12; ++k) r.block({}, 480);
    CHECK(int(r.drv.view(3).envVol) == 0);
}

TEST_CASE("a cell's L slides the bare note under a table transpose", "[driver][commands]")
{
    // Section 110, measured on 9.2.L. `SAMESONG`'s phrase 21: the instrument's
    // table blips an octave up every third tick, and a bare note with `L 10` on
    // the row after. The ROM slides the note's own pitch and suppresses the
    // column for the run; ChipBoy slid from the blip, an octave up.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[0] = tracker::NoteSource::Tracker;
    Table t; t.used = true; t.name = "Octave";
    t.steps[1].hasTranspose = true; t.steps[1].transpose = 12;
    r.bank.tables[6] = t;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Lead");
    i.used = true; i.table = 7;
    ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(0, p);
    r.block({ cellOn(0, 60, 2) }, 480);
    const int plain = int(r.drv.view(0).period);
    r.block({}, 480);
    const int blipped = int(r.drv.view(0).period);
    INFO("plain " << plain << " blipped " << blipped);
    REQUIRE(plain > 0);
    REQUIRE(blipped > plain);                            // the table's octave is in force
    // A bare note two semitones up with an L, while the column is in force.
    NoteEvent bare = cellOn(0, 62, 0);
    bare.cmd1 = { Cmd::L, 16, 0, 0 };
    const auto atL = r.block({ bare }, 480);
    // Section 111: the first period written on that update keeps the pitch the
    // channel was already on, column and all -- it does not drop to the bare
    // note a pitch update early. A whole block is too coarse for `view()` to
    // show it, so read the writes.
    int firstPer = -1;
    {
        int lo = -1;
        for (const auto& x : atL) {
            if (x.addr == 0xFF13) lo = x.value;
            else if (x.addr == 0xFF14 && lo >= 0) { firstPer = ((x.value & 7) << 8) | lo; break; }
        }
    }
    INFO("first period on the L's update " << firstPer);
    CHECK(firstPer == blipped);
    // The slide runs on the note, not on the blip: every period it walks
    // through stays below the octave the table was holding.
    int highest = 0;
    for (int k = 0; k < 8; ++k) { r.block({}, 480); highest = std::max(highest, int(r.drv.view(0).period)); }
    INFO("highest while sliding " << highest);
    CHECK(highest < blipped);
}

TEST_CASE("a pulse instrument's finetune detunes PU1 down and PU2 up", "[driver][commands]")
{
    // Section 112, measured on 9.2.L: LSDj's instrument byte 11 is a detune of
    // `fineTune / 256` of a semitone, **down** on PU1 and **up** on PU2, so a
    // pair of pulses beat against each other. It lands on the first pitch
    // update, not on the trigger, and a cell's F replaces it.
    const auto run = [](int ch, int ft, bool withF) {
        auto r = std::make_unique<Rig>();
        r->tickHz = 100.0;
        r->song.noteSource[size_t(ch)] = tracker::NoteSource::Tracker;
        auto& i = r->bank.instruments[1];
        i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Lead");
        i.used = true; i.fineTune = uint8_t(ft);
        ChannelParams p; p.instrument = 2; p.velocityMode = 2; r->drv.setParams(ch, p);
        NoteEvent on = cellOn(ch, 60, 2);
        if (withF) on.cmd1 = { Cmd::F, 0, 8, 0 };
        r->block({ on }, 480);
        r->block({}, 480);
        return int(r->drv.view(ch).period);
    };
    const int pu1Plain = run(0, 0, false), pu2Plain = run(1, 0, false);
    CHECK(pu1Plain == pu2Plain);
    // Half a semitone either way.
    const int pu1Fine = run(0, 0x80, false), pu2Fine = run(1, 0x80, false);
    INFO("PU1 " << pu1Plain << " -> " << pu1Fine << ", PU2 " << pu2Plain << " -> " << pu2Fine);
    CHECK(pu1Fine < pu1Plain);
    CHECK(pu2Fine > pu2Plain);
    // The same distance either way, to within the register's own rounding --
    // a semitone is not a whole number of units and the curve is not symmetric.
    CHECK(std::abs((pu1Plain - pu1Fine) - (pu2Fine - pu2Plain)) <= 1);
    // An F on the cell replaces it rather than adding to it: F 0 8 is y/32 of a
    // semitone down, a quarter of what 0x80 asks for, and that is what is left.
    const int withF = run(0, 0x80, true), fOnly = run(0, 0, true);
    CHECK(withF == fOnly);
    CHECK(withF > pu1Fine);
}

TEST_CASE("a table an A starts runs one row a tick, whatever the instrument says", "[driver][table]")
{
    // Section 122, measured on 9.2.L with a table whose rows each carry an `E`:
    // STEP governs only the table the instrument names. A table an `A` starts --
    // from a cell or from inside another table -- runs one row a **tick**, and
    // its row 0 is the next tick's, one behind the row that started it.
    const auto levels = [](bank::TableMode mode) {
        auto r = std::make_unique<Rig>();
        r->tickHz = 100.0;
        Table first; first.used = true; first.name = "Chain";
        first.steps[0].cmd1 = { Cmd::A, 7, 0, 0 };           // start table slot 7
        r->bank.tables[4] = first;                            // slot 5
        Table second; second.used = true; second.name = "Env";
        for (int k = 0; k < 4; ++k) second.steps[size_t(k)].cmd1 = { Cmd::E, 0, int16_t(3 - k), 0 };
        r->bank.tables[6] = second;                           // slot 7
        auto& i = r->bank.instruments[1];
        i = bank::Instrument::defaults(bank::InstrumentType::Wave, "Pad");
        i.used = true; i.table = 5; i.tableMode = mode;
        i.waveLevel = 0;                                      // so row 0's E 03 is a change
        ChannelParams p; p.instrument = 2; p.velocityMode = 2; r->drv.setParams(2, p);
        std::vector<int> out;
        auto w = r->block({ Rig::on(2, 60, 100) }, 480);
        for (int k = 0; k < 6; ++k) {
            const RegWrite* nr = last(w, 0xFF1C);
            out.push_back(nr != nullptr ? int((nr->value >> 5) & 3) : -1);
            w = r->block({}, 480);
        }
        return out;
    };
    // Row 0's E 03 is 100% (NR32 code 1), then 50%, 25% and mute (2, 3, 0). The
    // note's own tick has the instrument's level; the table's row 0 is the next.
    for (auto mode : { bank::TableMode::Tick, bank::TableMode::Step }) {
        const auto seen = levels(mode);
        INFO("mode " << int(mode) << " levels " << seen[0] << " " << seen[1] << " " << seen[2] << " " << seen[3] << " " << seen[4]);
        CHECK(seen[0] == 0);                                  // the instrument's own level
        CHECK(seen[1] == 1);                                  // row 0, a tick later
        CHECK(seen[2] == 2);
        CHECK(seen[3] == 3);
        CHECK(seen[4] == 0);
    }
}

TEST_CASE("a table a table starts reaches its vibrato in Step mode too", "[driver][table]")
{
    // Section 113, as section 122 leaves it: `SAMESONG`'s instrument 02 runs a
    // STEP-mode table whose row 0 starts another table holding the vibrato. The
    // second table has to run for the note to have any shape at all.
    const auto span = [](bank::TableMode mode) {
        auto r = std::make_unique<Rig>();
        r->tickHz = 100.0;
        Table first; first.used = true; first.name = "Chain";
        first.steps[0].cmd2 = { Cmd::A, 7, 0, 0 };          // start table slot 7
        r->bank.tables[4] = first;                           // slot 5
        Table second; second.used = true; second.name = "Vib";
        second.steps[0].cmd2 = { Cmd::V, 15, 3, 0 };
        r->bank.tables[6] = second;                          // slot 7
        auto& i = r->bank.instruments[1];
        i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Lead");
        i.used = true; i.table = 5; i.tableMode = mode;
        ChannelParams p; p.instrument = 2; p.velocityMode = 2; r->drv.setParams(0, p);
        r->block({ Rig::on(0, 60, 100) }, 480);
        int lo = 9999, hi = 0;
        for (int k = 0; k < 24; ++k) {
            r->block({}, 480);
            const int per = int(r->drv.view(0).period);
            if (per > 0) { lo = std::min(lo, per); hi = std::max(hi, per); }
        }
        return hi - lo;
    };
    CHECK(span(bank::TableMode::Tick) > 0);
    CHECK(span(bank::TableMode::Step) > 0);   // this is the one that was flat
}

TEST_CASE("an S on a note's own row retriggers after the note", "[driver][commands]")
{
    // Section 127: the sweep unit reloads on a trigger, so the ROM writes NR10
    // after the note's burst and triggers again. ChipBoy folded the write into
    // the burst and never emitted that second trigger.
    Rig r;
    r.tickHz = 100.0;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Sweep");
    i.used = true;
    ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(0, p);
    const auto triggers = [](const std::vector<RegWrite>& w) {
        int n = 0;
        for (const auto& q : w) if (q.addr == 0xFF14 && (q.value & 0x80)) ++n;
        return n;
    };
    NoteEvent plain = cellOn(0, 60, 2);
    CHECK(triggers(r.block({ plain }, 480)) == 1);
    r.block({ Rig::off(0, 60) }, 480);
    NoteEvent withS = cellOn(0, 60, 2);
    withS.cmd1 = { Cmd::S, 7, 1, 0 };
    const auto w = r.block({ withS }, 480);
    CHECK(triggers(w) == 2);                       // the note's, then the sweep's
    int sweeps = 0;
    for (const auto& q : w) if (q.addr == 0xFF10) ++sweeps;
    CHECK(sweeps == 2);                            // NR10 written in the burst and again after
    {   // PU2 has no sweep unit, so nothing extra there
        ChannelParams p2; p2.instrument = 2; p2.velocityMode = 2; r.drv.setParams(1, p2);
        NoteEvent s2 = cellOn(1, 60, 2);
        s2.cmd1 = { Cmd::S, 7, 1, 0 };
        const auto w2 = r.block({ s2 }, 480);
        int n = 0;
        for (const auto& q : w2) if (q.addr == 0xFF19 && (q.value & 0x80)) ++n;
        CHECK(n == 1);
    }
}

TEST_CASE("a pitch effect off the bottom of the table clamps, and V 00 stops", "[driver][commands]")
{
    // Sections 125 and 126, measured on 9.2.L and confirmed on 9.3.9 and 9.4.2
    // with `SAMESONG`'s EGUIT: a low note with a deep **square** vibrato
    // triggers at the plain note, swings down to a period of 0 rather than
    // going silent, and `V 00` puts it back on the note.
    Rig r;
    r.tickHz = 100.0;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "EGuit");
    i.used = true; i.vib.shape = bank::VibShape::Square; i.vib.dir = bank::VibDir::Down;
    ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(0, p);
    const int lowest = 37;                       // near the bottom of the pulse table
    NoteEvent e = cellOn(0, uint8_t(lowest), 2);
    e.cmd1 = { Cmd::V, 15, 9, 0 };               // three semitones, square: full swing at once
    auto w = r.block({ e }, 480);
    // The trigger carries the plain note, not the vibrato's trough.
    const RegWrite* trig = nullptr;
    for (const auto& q : w) if (q.addr == 0xFF14 && (q.value & 0x80)) { trig = &q; break; }
    REQUIRE(trig != nullptr);
    int lo = 0;
    for (const auto& q : w) { if (q.addr == 0xFF13) lo = int(q.value); if (&q == trig) break; }
    const int plain = ((int(trig->value) & 7) << 8) | lo;
    CHECK(plain > 0);                            // the note sounds at its own pitch
    CHECK(r.drv.view(0).active);
    CHECK_FALSE(r.drv.view(0).outOfRange);
    {   // and the swing reaches 0 rather than silencing the voice
        int least = 9999, most = 0;
        for (int k = 0; k < 12; ++k) {
            const auto more = r.block({}, 480);
            for (const auto& q : more) if (q.addr == 0xFF13) lo = int(q.value);
            for (const auto& q : more) if (q.addr == 0xFF14) { const int per = ((int(q.value) & 7) << 8) | lo; least = std::min(least, per); most = std::max(most, per); }
        }
        INFO("period range " << least << " to " << most);
        CHECK(least == 0);
        CHECK(most > plain);
        CHECK(r.drv.view(0).active);
    }
    {   // Section 126: V 00 turns it off; V 20 is a vibrato of depth zero.
        NoteEvent off = cellCmd(0, { Cmd::V, 0, 0, 0 });
        r.block({ off }, 480);
        const auto after = r.block({}, 480);
        int seen = -1;
        for (const auto& q : after) if (q.addr == 0xFF13) lo = int(q.value);
        for (const auto& q : after) if (q.addr == 0xFF14) seen = ((int(q.value) & 7) << 8) | lo;
        CHECK(int(r.drv.view(0).vibDepth) >= 0);
        CHECK((seen == -1 || seen == plain));    // back on the note, or not moving at all
    }
}

TEST_CASE("every vibrato shape is centred on the note", "[driver][commands]")
{
    // Section 114, measured on 9.2.L: triangle, saw and square all swing the
    // full depth either side of the note -- `V F3` is a span of 12 register
    // units whichever shape it is -- and the instrument's direction bit only
    // picks which half comes first. Saw and Square used to run 0..+1.
    const auto swing = [](bank::VibShape shape, bank::VibDir dir) {
        auto r = std::make_unique<Rig>();
        r->tickHz = 100.0;
        auto& i = r->bank.instruments[1];
        i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Lead");
        i.used = true; i.vib.shape = shape; i.vib.dir = dir;
        ChannelParams p; p.instrument = 2; p.velocityMode = 2; r->drv.setParams(0, p);
        r->song.noteSource[0] = tracker::NoteSource::Tracker;
        NoteEvent on = cellOn(0, 60, 2);
        on.cmd1 = { Cmd::V, 15, 3, 0 };
        r->block({ on }, 480);
        int lo = 9999, hi = 0;
        const int plain = int(r->drv.view(0).period);
        for (int k = 0; k < 40; ++k) {
            r->block({}, 480);
            const int per = int(r->drv.view(0).period);
            if (per > 0) { lo = std::min(lo, per); hi = std::max(hi, per); }
        }
        (void) plain;
        return std::make_pair(lo, hi);
    };
    // Shape 3 is no vibrato at all, whatever V asks for -- so it also gives the
    // note's own period to measure the others against.
    const auto [offLo, offHi] = swing(bank::VibShape::Off, bank::VibDir::Down);
    CHECK(offLo == offHi);
    const int note = offLo;
    for (auto shape : { bank::VibShape::Triangle, bank::VibShape::Saw, bank::VibShape::Square }) {
        const auto [lo, hi] = swing(shape, bank::VibDir::Down);
        INFO("shape " << int(shape) << ": note " << note << ", " << lo << " .. " << hi);
        CHECK(lo < note);      // it goes below the note
        CHECK(hi > note);      // and above it
    }
}

TEST_CASE("a loop counted from the run's end stays at the end when U lengthens the run", "[driver][wave]")
{
    // Section 201 (W6_p1_r0_W12, W6_p1_r1_W12, W6_p2_rF_W12 on 6.0.1): before
    // 7.7.6 the loop is the REPEAT nibble plus one steps from the run's end. A
    // one-frame run under `W12` walks frames 0, 7 and F; a tail of one then
    // holds F, a tail of two keeps 7 and F, and a ping-pong over the whole run
    // turns at both ends.
    const auto runs = [](bank::FrameLoop loop, int tail, int blocks) {
        auto r = std::make_unique<Rig>();
        r->tickHz = 100.0;
        r->song.noteSource[2] = tracker::NoteSource::Tracker;
        auto& w = r->bank.waves[0];
        w.used = true;
        for (int f = 0; f < bank::kMaxFrames; ++f) w.frames[size_t(f)].s.fill(uint8_t(f));
        auto& i = r->bank.instruments[1];
        i = bank::Instrument::defaults(bank::InstrumentType::Wave, "Tail");
        i.used = true; i.wave = 1; i.frameAdvance = 1; i.frameLength = 1; i.frameLoopStep = 0;
        i.frameLoop = loop; i.frameLoopTail = uint8_t(tail);
        ChannelParams p; p.instrument = 2; p.velocityMode = 2; r->drv.setParams(2, p);
        NoteEvent on = cellOn(2, 60, 2);
        on.cmd1 = { Cmd::U, 1, 2, 0 };
        r->block({ on }, 480);
        std::vector<int> seen{ int(r->drv.view(2).frame) - 1 };
        for (int k = 0; k < blocks; ++k) { r->block({}, 480); seen.push_back(int(r->drv.view(2).frame) - 1); }
        return seen;
    };
    CHECK(runs(bank::FrameLoop::Loop, 1, 5) == std::vector<int>{ 0, 7, 15, 15, 15, 15 });
    CHECK(runs(bank::FrameLoop::Loop, 2, 5) == std::vector<int>{ 0, 7, 15, 7, 15, 7 });
    CHECK(runs(bank::FrameLoop::PingPong, 16, 6) == std::vector<int>{ 0, 7, 15, 7, 0, 7, 15 });
    // Without a tail the 9.x rule holds: the loop keeps its frame, here frame 0.
    CHECK(runs(bank::FrameLoop::Loop, 0, 4) == std::vector<int>{ 0, 7, 15, 0, 7 });
}

TEST_CASE("with retrigTableLate a retrigger starts the instrument's table over on the next tick", "[driver][commands][table]")
{
    // Section 202 (Rtbl6_R03 on 7.0.2 and older): the retrigger's tick runs the
    // row the table was on, row 0 comes the tick after -- the immediate fire at
    // the note-on included, so row 1 is two ticks after row 0 there. Table 0 =
    // W03, W01, W02, W00 under `R 03` on a duty-2 instrument; the duty after
    // each tick tells the row.
    const auto duties = [](bool late) {
        auto r = std::make_unique<Rig>();
        r->tickHz = 100.0;
        r->song.noteSource[0] = tracker::NoteSource::Tracker;
        Table t0; t0.used = true;
        t0.steps[0].cmd1 = { Cmd::W, 3, 0, 0 }; t0.steps[1].cmd1 = { Cmd::W, 1, 0, 0 }; t0.steps[2].cmd1 = { Cmd::W, 2, 0, 0 }; t0.steps[3].cmd1 = { Cmd::W, 0, 0, 0 };
        r->bank.tables[0] = t0;
        auto& i = r->bank.instruments[1];
        i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Late");
        i.used = true; i.table = 1; i.tableMode = bank::TableMode::Tick; i.duty = 2; i.retrigTableLate = late;
        ChannelParams p; p.instrument = 2; r->drv.setParams(0, p);
        NoteEvent on = cellOn(0, 60, 2); on.cmd1 = { Cmd::R, 0, 3, 0 };
        r->block({ on }, 480);
        std::vector<int> seen{ int(r->drv.view(0).duty) };
        for (int k = 0; k < 7; ++k) { r->block({}, 480); seen.push_back(int(r->drv.view(0).duty)); }
        return seen;
    };
    // The 9.x rule (section 182): row 0 on the note-on, rows 1 and 2 on the
    // ticks after, and the roll at tick 3 plays row 0 on its own tick.
    CHECK(duties(false) == std::vector<int>{ 3, 1, 2, 3, 1, 2, 3, 1 });
    // Late: the note-on's immediate fire replays row 0 on tick 1, and the roll
    // at tick 3 lets row 2 run and starts over on tick 4.
    CHECK(duties(true) == std::vector<int>{ 3, 3, 1, 2, 3, 1, 2, 3 });
}

namespace {
/// The period register's swing about the note over the blocks: (lowest, highest) minus the note-on's period.
std::pair<int, int> periodSwing(const std::vector<RegWrite>& w, int lo13, int lo14)
{
    int nr13 = 0, nr14 = 0, base = -1, lo = 0, hi = 0; bool seen14 = false;
    for (size_t k = 0; k < w.size(); ++k) {
        const auto& x = w[k];
        if (x.addr == lo13) nr13 = x.value;
        else if (x.addr == lo14) { nr14 = x.value & 7; seen14 = true; }
        else continue;
        // The pair is one write: read the period once the burst is over.
        size_t j = k + 1;
        while (j < w.size() && w[j].addr != lo13 && w[j].addr != lo14) ++j;
        if (j < w.size() && w[j].cycle - x.cycle < 64) continue;
        if (!seen14) continue;
        const int per = (nr14 << 8) | nr13;
        if (base < 0) { base = per; continue; }
        lo = std::min(lo, per - base); hi = std::max(hi, per - base);
    }
    return { lo, hi };
}
}

TEST_CASE("the vibrato's depth follows the instrument's ladder", "[driver][vibrato]")
{
    // Section 203, at C3 (56 units a semitone): the 9.x ladder's depth 2 is a
    // multiplier of 3 and its depth 6 of 12; 5.7.8's are 2 and 7; the unit laws
    // swing the register by the ladder times the divider in sixty-fourths (15
    // at C3), the downward half a thirty-second short from 3.7.5; and depth 0
    // is off on 6.8.2 - 8.8.6, an eighth of a semitone before and after.
    const auto swing = [](bank::VibLadder ladder, int depth, bool registerLaw) {
        auto r = std::make_unique<Rig>();
        r->tickHz = 100.0;
        r->song.noteSource[0] = tracker::NoteSource::Tracker;
        auto& i = r->bank.instruments[1];
        i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Vib");
        i.used = true; i.vibLadder = ladder;
        if (registerLaw) { i.pitchSpeed = bank::PitchSpeed::Drum; i.pitchRegisterUnits = true; }
        ChannelParams p; p.instrument = 2; r->drv.setParams(0, p);
        NoteEvent on = cellOn(0, 48, 2); on.cmd1 = { Cmd::V, 0, int16_t(depth), 0 };
        std::vector<RegWrite> w = r->block({ on }, 480);
        for (int k = 0; k < 80; ++k) { auto more = r->block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
        return periodSwing(w, 0xFF13, 0xFF14);
    };
    const auto near = [](std::pair<int, int> got, int lo, int hi) { return std::abs(got.first - lo) <= 1 && std::abs(got.second - hi) <= 1; };
    CHECK(near(swing(bank::VibLadder::Lsdj9, 2, false), -22, 21));
    CHECK(near(swing(bank::VibLadder::Lsdj9, 6, false), -91, 83));
    CHECK(near(swing(bank::VibLadder::Lsdj57, 2, false), -15, 14));
    CHECK(near(swing(bank::VibLadder::Lsdj57, 6, false), -51, 49));
    CHECK(near(swing(bank::VibLadder::Lsdj68, 6, false), -81, 76));            // the downward half peaks at 31/32
    CHECK(swing(bank::VibLadder::Lsdj68, 0, false) == std::pair<int, int>{ 0, 0 });
    CHECK(near(swing(bank::VibLadder::Lsdj58, 0, false), -7, 7));
    CHECK(swing(bank::VibLadder::Lsdj78, 0, false) == std::pair<int, int>{ 0, 0 });
    CHECK(near(swing(bank::VibLadder::Lsdj78, 6, false), -91, 83));
    CHECK(swing(bank::VibLadder::Units39, 6, true) == std::pair<int, int>{ -102, 105 });
    CHECK(swing(bank::VibLadder::Units36, 6, true) == std::pair<int, int>{ -105, 105 });
    CHECK(swing(bank::VibLadder::Units39, 15, true) == std::pair<int, int>{ -451, 465 });
}

TEST_CASE("under the register law F on a pulse is period units down and a note's L slides from the old period", "[driver][commands]")
{
    // Section 204 (F03 on 5.0.3: 97 -> 94) and section 206 (L03_second_ch0 on
    // 5.0.3: the trigger keeps the old period and walks three units an instant).
    const auto run = [](bool registerLaw, std::vector<NoteEvent> first, std::vector<NoteEvent> second, int blocksBetween) {
        auto r = std::make_unique<Rig>();
        r->tickHz = 100.0;
        r->song.noteSource[0] = tracker::NoteSource::Tracker;
        auto& i = r->bank.instruments[1];
        i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Reg");
        i.used = true;
        if (registerLaw) { i.pitchSpeed = bank::PitchSpeed::Drum; i.pitchRegisterUnits = true; }
        ChannelParams p; p.instrument = 2; r->drv.setParams(0, p);
        std::vector<RegWrite> w = r->block(first, 480);
        for (int k = 0; k < blocksBetween; ++k) r->block({}, 480);
        if (!second.empty()) { w = r->block(second, 480); auto more = r->block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
        return w;
    };
    // Every period the block wrote, in order, from the NR13/NR14 pairs.
    const auto periods = [](const std::vector<RegWrite>& w) {
        std::vector<int> out; int nr13 = 0, nr14 = 0; bool seen14 = false;
        for (const auto& x : w) {
            if (x.addr == 0xFF13) nr13 = x.value; else if (x.addr == 0xFF14) { nr14 = x.value & 7; seen14 = true; } else continue;
            if (seen14) out.push_back((nr14 << 8) | nr13);
        }
        return out;
    };
    NoteEvent plain = cellOn(0, 48, 2);
    NoteEvent withF = cellOn(0, 48, 2); withF.cmd1 = { Cmd::F, 0, 3, 0 };
    // The finetune lands on the update after the trigger (section 163), so the
    // block's last period is the one to read.
    const int base = periods(run(true, { plain }, {}, 0)).back();
    CHECK(periods(run(true, { withF }, {}, 0)).back() == base - 3);             // section 204: three units, x ignored
    CHECK(periods(run(false, { withF }, {}, 0)).back() < base - 3);             // 9.x: 3/32 of a semitone, five units here
    // Section 206: note 60, then note 65 with L02 (three updates): the second
    // trigger carries note 60's period and the walk ends on note 65's.
    NoteEvent hi = cellOn(0, 65, 2); hi.cmd1 = { Cmd::L, 2, 0, 0 };
    const int per60 = periods(run(true, { cellOn(0, 60, 2) }, {}, 0)).front();
    const int per65 = periods(run(true, { cellOn(0, 65, 2) }, {}, 0)).front();
    const auto slid = periods(run(true, { cellOn(0, 60, 2) }, { hi }, 4));
    REQUIRE(slid.size() >= 2);
    CHECK(slid.front() == per60);
    CHECK(slid.back() == per65);
    CHECK(slid[1] > per60); CHECK(slid[1] < per65);
}

TEST_CASE("a 3.x noise table's transposes add up nibble by nibble in the running byte", "[driver][noise]")
{
    // Section 207 (tsp_tbl_noi on 3.9.2): note $34 gives NR43 $00 under shape FF;
    // rows of 3, 7 and C subtract nibble by nibble as they play -- 0D, 06, 0A --
    // and the note-on's trigger carries the plain byte. Under the 4.x rule the
    // same rows are a byte off the note's: FD, F9, F4.
    const auto nr43s = [](bool nibbles) {
        auto r = std::make_unique<Rig>();
        r->tickHz = 100.0;
        r->song.noteSource[3] = tracker::NoteSource::Tracker;
        Table t0; t0.used = true;
        t0.steps[0].hasTranspose = true; t0.steps[0].transpose = 3;
        t0.steps[1].hasTranspose = true; t0.steps[1].transpose = 7;
        t0.steps[2].hasTranspose = true; t0.steps[2].transpose = 12;
        r->bank.tables[0] = t0;
        auto& i = r->bank.instruments[1];
        i = bank::Instrument::defaults(bank::InstrumentType::Noise, "Old");
        i.used = true; i.table = 1; i.tableMode = bank::TableMode::Tick;
        i.noiseShapeMode = true; i.noiseShape = 0xFF; i.noiseStable = false; i.noiseTspNibbles = nibbles;
        i.noiseDomain = bank::NoiseSweepDomain::Register;
        ChannelParams p; p.instrument = 2; r->drv.setParams(3, p);
        std::vector<RegWrite> w = r->block({ cellOn(3, 0x34 + 35, 2) }, 480);
        for (int k = 0; k < 3; ++k) { auto more = r->block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
        std::vector<int> seen; int last = -1;
        for (const auto& x : w) if (x.addr == 0xFF22 && int(x.value) != last) { last = int(x.value); seen.push_back(last); }
        if (seen.size() > 4) seen.resize(4);                                  // the rows after the third are empty
        return seen;
    };
    CHECK(nr43s(true) == std::vector<int>{ 0x00, 0x0D, 0x06, 0x0A });
    CHECK(nr43s(false) == std::vector<int>{ 0x00, 0xFD, 0xF9, 0xF4 });
}

TEST_CASE("U sets the wave run's speed and length", "[driver][wave]")
{
    // Sections 115 and 129, measured on 9.2.L: LSDj's `W` on a wave instrument
    // is the run -- x ticks a frame, y the run's length -- and x = 0 leaves the
    // speed alone. ChipBoy's `W` is the wave slot, so this is `U`.
    const auto runs = [](int x, int y, int blocks, int ownLength, int ownLoopStep) {
        auto r = std::make_unique<Rig>();
        r->tickHz = 100.0;
        r->song.noteSource[2] = tracker::NoteSource::Tracker;
        auto& w = r->bank.waves[0];
        w.used = true;
        for (int f = 0; f < bank::kMaxFrames; ++f) w.frames[size_t(f)].s.fill(uint8_t(f));
        auto& i = r->bank.instruments[1];
        i = bank::Instrument::defaults(bank::InstrumentType::Wave, "Run");
        i.used = true; i.wave = 1; i.frameAdvance = 8;
        i.frameLength = uint8_t(ownLength); i.frameLoopStep = uint8_t(ownLoopStep);
        ChannelParams p; p.instrument = 2; p.velocityMode = 2; r->drv.setParams(2, p);
        NoteEvent on = cellOn(2, 60, 2);
        on.cmd1 = { Cmd::U, int16_t(x), int16_t(y), 0 };
        r->block({ on }, 480);
        std::vector<int> seen{ int(r->drv.view(2).frame) - 1 };
        for (int k = 0; k < blocks; ++k) { r->block({}, 480); seen.push_back(int(r->drv.view(2).frame) - 1); }
        return seen;
    };
    const auto frames = [&](int x, int y, int blocks) { return runs(x, y, blocks, 0, 0); };
    // x = 1 is a frame a tick, so every block moves one on. y = F is the run
    // of all sixteen; section 129 -- y = 0 is not.
    const auto fast = frames(1, 15, 6);
    CHECK(fast[1] == 1); CHECK(fast[2] == 2); CHECK(fast[3] == 3);
    // x = 2 is a frame every two.
    const auto half = frames(2, 15, 6);
    CHECK(half[1] == 0); CHECK(half[2] == 1); CHECK(half[4] == 2);
    // Section 131: `y = 0` leaves the run's **length** as it stands, as x = 0
    // leaves its speed. With the instrument's own four-step run that is still
    // 0, 5, 10, 15 -- not the sixteen §115 read it as, and not the frozen run
    // §129 read it as.
    const auto own = runs(1, 0, 6, 4, 3);
    CHECK(own[1] == 5); CHECK(own[2] == 10); CHECK(own[3] == 15);
    // The loop keeps the **frame** it returned to, not its step number: a run
    // holding its last frame goes on holding it when a `U` lengthens it, where
    // a stale step index left it oscillating between the last two (section 131).
    const auto held = runs(1, 4, 8, 4, 3);
    CHECK(held[1] == 3); CHECK(held[2] == 7); CHECK(held[3] == 11); CHECK(held[4] == 15);
    for (int k = 5; k < int(held.size()); ++k) { INFO("step " << k); CHECK(held[size_t(k)] == 15); }
    // y = 3 is a run of four spread across the sixteen: 0, 5, 10, 15.
    const auto four = frames(1, 3, 6);
    CHECK(four[1] == 5); CHECK(four[2] == 10); CHECK(four[3] == 15);
    // y = 1 is a run of two: the ends.
    const auto two = frames(1, 1, 4);
    CHECK(two[1] == 15); CHECK(two[2] == 0);
    // y = 4 is a run of five: 0, 3, 7, 11, 15 -- the ROM's ladder, where the
    // old spacing gave 0, 4, 8, 12, 15 (section 129).
    const auto five = frames(1, 4, 6);
    CHECK(five[1] == 3); CHECK(five[2] == 7); CHECK(five[3] == 11); CHECK(five[4] == 15);
}

TEST_CASE("R fires a retrigger on the command's own tick", "[driver][commands]")
{
    // Section 134: the ROM retriggers on the tick the `R` is read and then every
    // y ticks. ChipBoy counted y from the command, so it missed the first and
    // ran a tick early after it; `y = 0` is that first one with nothing to
    // repeat, not a special case.
    const auto ticksOf = [](int y, int rows) {
        Rig r;
        r.tickHz = 100.0;
        r.song.noteSource[0] = tracker::NoteSource::Tracker;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
        NoteEvent e = cellOn(0, 60, 1);
        e.cmd1 = { Cmd::R, 0, int16_t(y), 0 };
        std::vector<int> at;
        if (anyTrigger(r.block({ e }, 480), 0xFF14)) at.push_back(0);
        for (int k = 1; k <= rows; ++k)
            if (anyTrigger(r.block({}, 480), 0xFF14)) at.push_back(k);
        return at;
    };
    // Tick 0 carries the note and its retrigger together; then every y.
    CHECK(ticksOf(3, 12) == std::vector<int>{ 0, 3, 6, 9, 12 });
    CHECK(ticksOf(2, 8) == std::vector<int>{ 0, 2, 4, 6, 8 });
    CHECK(ticksOf(1, 4) == std::vector<int>{ 0, 1, 2, 3, 4 });
    CHECK(ticksOf(0, 6) == std::vector<int>{ 0 });        // the one, and no more
}

TEST_CASE("R steps the level on the noise channel too", "[driver][commands][noise]")
{
    // Section 133: `x` is a signed nibble of volume change -- 1-7 up, 9-15 down
    // by sixteen minus it -- and it applies on noise, which the driver used to
    // refuse. `READROOM`'s noise rolls retriggered at a flat level without it.
    // `x = 0` gives the level the same note reaches with no step, so the checks
    // read as the step itself whatever the instrument's own level is.
    const auto level = [](int x) {
        Rig r;
        r.tickHz = 100.0;
        r.song.noteSource[3] = tracker::NoteSource::Tracker;
        auto& i = r.bank.instruments[1];
        i = bank::Instrument::defaults(bank::InstrumentType::Noise, "Roll");
        i.used = true; i.env.mode = bank::EnvMode::Chip; i.envRate = 0;
        ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(3, p);
        NoteEvent e = cellOn(3, 60, 2);
        e.cmd1 = { Cmd::R, int16_t(x), 0, 0 };          // one retrigger
        r.block({ e }, 480);
        r.block({}, 480);
        return int(r.drv.view(3).volume);
    };
    const int base = level(0x0);
    REQUIRE(base >= 8);                                 // room to step either way
    CHECK(level(0xF) == base - 1);
    CHECK(level(0xE) == base - 2);
    CHECK(level(0x9) == base - 7);
    CHECK(level(0x1) == base + 1);
    CHECK(level(0x3) == base + 3);
    CHECK(level(0x8) == base);                          // 8 is the resync, no step
}

TEST_CASE("a wave run that plays once goes quiet at its end", "[driver][wave]")
{
    // Section 132: one step past the end of a `PLAY = ONCE` run the ROM writes a
    // wave of sixteen 0x77 bytes -- a flat line at mid-scale, silent -- where
    // ChipBoy held the run's last frame and the note would not stop.
    Rig r(Console::DMG);
    Chip chip(Console::DMG);
    r.tickHz = 100.0;
    r.song.noteSource[2] = tracker::NoteSource::Tracker;
    auto& w = r.bank.waves[0];
    w.used = true;
    for (int f = 0; f < bank::kMaxFrames; ++f) w.frames[size_t(f)].s.fill(uint8_t(f == 0 ? 0 : 15));
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Wave, "Once");
    i.used = true; i.wave = 1; i.frameAdvance = 1; i.frameLength = 4;
    i.frameLoop = bank::FrameLoop::Once; i.frameLoopStep = 3;
    ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(2, p);
    chip.feed(r.block({ cellOn(2, 60, 2) }, 480));
    bool flat = false;
    for (int k = 0; k < 10 && !flat; ++k) {
        for (const auto& x : r.block({}, 480))
            if (x.addr >= 0xFF30 && x.addr <= 0xFF3F && x.value == 0x77) flat = true;
    }
    CHECK(flat);                                       // the run ended and wrote its flat wave
    // A run that loops never writes one -- it goes back to its loop step.
    Rig r2(Console::DMG);
    r2.tickHz = 100.0;
    r2.song.noteSource[2] = tracker::NoteSource::Tracker;
    r2.bank.waves[0] = w;
    r2.bank.instruments[1] = i; r2.bank.instruments[1].frameLoop = bank::FrameLoop::Loop;
    r2.bank.instruments[1].frameLoopStep = 0;
    r2.drv.setParams(2, p);
    r2.block({ cellOn(2, 60, 2) }, 480);
    bool looped = false;
    for (int k = 0; k < 10; ++k)
        for (const auto& x : r2.block({}, 480))
            if (x.addr >= 0xFF30 && x.addr <= 0xFF3F && x.value == 0x77) looped = true;
    CHECK_FALSE(looped);
}

TEST_CASE("a bare note does not step a STEP table", "[driver][table]")
{
    // Section 131: a bare note writes only the period -- no trigger, no reload,
    // no table restart (section 8) -- and it does not step the table either.
    // Stepping it fired the row's `F` again from wherever the wave had got to,
    // with no instrument reload to count from, so the frame walked away.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[2] = tracker::NoteSource::Tracker;
    auto& w = r.bank.waves[0];
    w.used = true;
    for (int f = 0; f < bank::kMaxFrames; ++f) w.frames[size_t(f)].s.fill(uint8_t(f));
    auto& t = r.bank.tables[0]; t.used = true; t.end = TableEnd::Stop;
    t.steps[0].cmd1 = { Cmd::F, 0, 1, 0 };                  // every row: one frame on
    t.steps[1].cmd1 = { Cmd::F, 0, 1, 0 };
    t.steps[2].cmd1 = { Cmd::H, 0, 1, 0 };
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Wave, "Step");
    i.used = true; i.wave = 1; i.frameAdvance = 0; i.table = 1; i.tableMode = bank::TableMode::Step;
    ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(2, p);
    r.block({ cellOn(2, 60, 2) }, 480);
    CHECK(int(r.drv.view(2).frame) - 1 == 1);               // the plain note stepped row 0
    NoteEvent bare = cellOn(2, 62, 2); bare.inst = 0;       // a note with no instrument column
    r.block({ bare }, 480);
    CHECK(int(r.drv.view(2).frame) - 1 == 1);               // and the bare note stepped nothing
}

TEST_CASE("a bare note pays what its commands owe the burst", "[driver][commands]")
{
    // Section 146, from `SAMESONG`'s phrase 2F: `setLevel()` leaves a level to the
    // note's own burst while a note-on is in progress (section 3), and a bare
    // note's writes are the period and the pan -- so an `E` beside one reached no
    // register at all. Measured on the ROM: `NR32` at 100 % where ChipBoy wrote no
    // `NR32`, eight zombie steps on a pulse where it wrote none, and section 138's
    // trigger lost with them.
    auto bareWith = [](int ch, const Command& c) {
        NoteEvent e = cellOn(ch, 62, 2); e.inst = 0; e.cmd1 = c; return e;
    };
    SECTION("the wave channel's NR32")
    {
        Rig r;
        r.tickHz = 100.0;
        r.song.noteSource[2] = tracker::NoteSource::Tracker;
        r.bank.waves[0].used = true;
        auto& i = r.bank.instruments[1];
        i = Instrument::defaults(InstrumentType::Wave, "wav");
        i.wave = 1; i.waveLevel = 2;                        // 50 % to begin with
        ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(2, p);
        r.block({ cellOn(2, 60, 2) }, 480);
        // Section 108: on the wave channel the `E`'s low nibble **is** the level.
        const auto w = r.block({ bareWith(2, Command{ Cmd::E, 0, 3, 0 }) }, 480);
        const RegWrite* x = last(w, 0xFF1C);
        REQUIRE(x != nullptr);                              // it wrote NR32 at all
        CHECK(int((x->value >> 5) & 3) == 1);               // NR32's code for 100 %
    }
    SECTION("a pulse's level")
    {
        Rig r;
        r.tickHz = 100.0;
        r.song.noteSource[0] = tracker::NoteSource::Tracker;
        auto& i = r.bank.instruments[1];
        i = Instrument::defaults(InstrumentType::Pulse, "pu");
        i.envRate = 0;
        ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(0, p);
        r.block({ cellOn(0, 60, 2) }, 480);
        const int before = int(r.drv.view(0).volume);        // the velocity's level
        const auto w = r.block({ bareWith(0, Command{ Cmd::E, 4, 0, 0 }) }, 480);
        int downs = 0;
        for (const auto& x : w) if (x.addr == 0xFF12 && x.value == 0x09) ++downs;
        INFO("from " << before << " to " << int(r.drv.view(0).volume) << ", " << downs << " steps");
        CHECK(downs == before - 4);                          // one zombie triple a level down
        CHECK(int(r.drv.view(0).volume) == 4);
    }
    SECTION("and section 138's trigger with it")
    {
        Rig r;
        r.tickHz = 100.0;
        r.song.noteSource[0] = tracker::NoteSource::Tracker;
        auto& i = r.bank.instruments[1];
        i = Instrument::defaults(InstrumentType::Pulse, "pu");
        i.envVol = 4; i.envRate = 0;
        i.length = 59; i.lengthLatent = false;              // the counter is enabled
        ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(0, p);
        r.block({ cellOn(0, 60, 2) }, 480);
        const auto w = r.block({ bareWith(0, Command{ Cmd::E, 12, 0, 0 }) }, 480);
        int trigs = 0;
        for (const auto& x : w) if (x.addr == 0xFF14 && (x.value & 0x80)) ++trigs;
        CHECK(trigs == 1);                                  // the bare note itself triggers none
    }
}

TEST_CASE("a Z plays what it rolled without remembering it", "[driver][commands][table]")
{
    // Section 130: the lane's record is the last command actually **written**,
    // so a `Z` re-rolls from the same place every pass. ChipBoy wrote the rolled
    // value back, so `Z 10` on an `E 00` recorded `E 10`, then rolled from that
    // to `E 20` -- the level climbing a step a pass however the dice fell. The
    // check does not depend on the random: after the fix the level can only ever
    // be the record's 0 plus the one nibble `Z 1 0` rolls.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[0] = tracker::NoteSource::Tracker;
    auto& t = r.bank.tables[0]; t.used = true; t.end = TableEnd::Stop;
    t.steps[0].cmd1 = { Cmd::E, 0, 0, 0 };                  // the record: level 0
    t.steps[1].cmd1 = { Cmd::Z, 1, 0, 0 };                  // re-run it, 0..1 on the high nibble
    t.steps[2].cmd1 = { Cmd::H, 0, 1, 0 };                  // back to the Z for ever
    r.bank.instruments[0].table = 1;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    r.block({ cellOn(0, 69, 1) }, 480);
    for (int k = 0; k < 12; ++k) {
        r.block({}, 480);
        INFO("pass " << k);
        CHECK(r.drv.view(0).volume <= 1);
    }
}

TEST_CASE("a shaped envelope stage can be shorter than a tick", "[driver][shaped]")
{
    // Section 121: a stage's length is its tick count **and** a fraction of a
    // tick, so a decay of half a tick reaches the sustain halfway through the
    // first one and holds -- where a whole-tick count had to round it up to one.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[0] = tracker::NoteSource::Tracker;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Clap");
    i.used = true;
    i.env.mode = bank::EnvMode::Shaped;
    i.env.start = 12; i.env.peak = 12; i.env.attackTicks = 0; i.env.attackFine = 0;
    i.env.decayTicks = 0; i.env.decayFine = 128;     // half a tick
    i.env.sustain = 4; i.env.fadeTicks = 0;
    ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(0, p);
    // How many milliseconds until the level lands on the sustain.
    const auto landsAt = [&p](uint8_t ticks, uint8_t fine) {
        auto rig = std::make_unique<Rig>();
        rig->tickHz = 51.6;                           // a tick is 19.4 ms: seven pitch clocks
        rig->song.noteSource[0] = tracker::NoteSource::Tracker;
        auto& in = rig->bank.instruments[1];
        in = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Clap");
        in.used = true;
        in.env.mode = bank::EnvMode::Shaped;
        in.env.start = 12; in.env.peak = 12; in.env.attackTicks = 0;
        in.env.decayTicks = ticks; in.env.decayFine = fine; in.env.sustain = 4; in.env.fadeTicks = 0;
        rig->drv.setParams(0, p);
        rig->block({ cellOn(0, 60, 2) }, 48);
        for (int ms = 1; ms < 120; ++ms) {
            if (int(rig->drv.view(0).envVol) == 4) return ms;
            rig->block({}, 48);
        }
        return 999;
    };
    const int half = landsAt(0, 128);                 // half a tick: about 10 ms
    const int two = landsAt(2, 0);                    // two ticks: about 39 ms
    INFO("half a tick landed at " << half << " ms, two ticks at " << two);
    CHECK(half < two);
    CHECK(half <= 16);                                // inside the first tick, not rounded up to one
    CHECK(two >= 30);
}

TEST_CASE("a shaped envelope stage shorter than its levels walks through them", "[driver][shaped]")
{
    // Section 116, measured against the ROM on `SAMESONG`'s CLAP: a stage of one
    // tick that crosses four levels steps through every one of them on the pitch
    // clock, where ChipBoy used to emit a single jump once a tick.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[0] = tracker::NoteSource::Tracker;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Clap");
    i.used = true;
    i.env.mode = bank::EnvMode::Shaped;
    i.env.start = 12; i.env.peak = 12; i.env.attackTicks = 0;
    i.env.decayTicks = 1; i.env.sustain = 4;        // four levels in one tick
    i.env.fadeTicks = 0;
    ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(0, p);
    auto w = r.block({ cellOn(0, 60, 2) }, 480);
    for (int k = 0; k < 2; ++k) { const auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
    // Count the zombie steps down: 12 to 4 is eight of them, and they may not
    // all fall in one burst -- what matters is that the level visits each one.
    int steps = 0;
    for (size_t k = 0; k + 2 < w.size(); ++k)
        if (w[k].addr == 0xFF12 && w[k].value == 0x09 && w[k + 1].value == 0x11 && w[k + 2].value == 0x18) ++steps;
    INFO("zombie steps down: " << steps);
    CHECK(steps >= 6);                               // it walks the levels, not one jump
    CHECK(int(r.drv.view(0).envVol) == 4);           // and lands on the sustain
}

/* --------------------------------------- the third campaign, on 9.4.2's code */

TEST_CASE("a bare note's S retriggers and its W writes the duty", "[driver][commands][rom942]")
{
    // Section 148: the ROM's S refresh ($6058) always triggers, and W writes NR11.
    Rig r; r.tickHz = 100.0; r.song.noteSource[0] = tracker::NoteSource::Tracker;
    r.bank.instruments[0].vib.depth = 0;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    r.block({ cellOn(0, 69, 1) }, 480);
    NoteEvent bare = cellOn(0, 71, 0); bare.cmd1 = { Cmd::S, 2, 1, 0 };
    auto w = r.block({ bare }, 480);
    CHECK(has(w, 0xFF10));                                    // the sweep byte goes out
    CHECK(anyTrigger(w, 0xFF14));                             // and the refresh triggers
    NoteEvent bare2 = cellOn(0, 72, 0); bare2.cmd1 = { Cmd::W, 1, 0, 0 };
    w = r.block({ bare2 }, 480);
    const RegWrite* d = last(w, 0xFF11);
    REQUIRE(d != nullptr);
    CHECK((d->value >> 6) == 1);
    CHECK_FALSE(anyTrigger(w, 0xFF14));                       // W alone does not retrigger
}

TEST_CASE("a C on a running voice plays the root on its own tick, and a second C keeps the phase", "[driver][commands][rom942]")
{
    // Section 149: $4F3A reads the phase, adds, then advances; $476C stores the value only.
    Rig r; r.tickHz = 100.0; r.song.noteSource[0] = tracker::NoteSource::Tracker;
    r.bank.instruments[0].vib.depth = 0;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    r.block({ cellOn(0, 69, 1) }, 480); r.block({}, 480);
    NoteEvent bare = cellOn(0, 71, 0); bare.cmd1 = { Cmd::C, 3, 7, 0 };
    r.block({ bare }, 480);
    CHECK(int(r.drv.view(0).period) == note(71));             // the root on the C's tick
    r.block({}, 480);
    CHECK(int(r.drv.view(0).period) == note(74));             // +3 on the next
    NoteEvent again = cellOn(0, 71, 0); again.cmd1 = { Cmd::C, 3, 7, 0 };
    r.block({ again }, 480);
    CHECK(int(r.drv.view(0).period) == note(78));             // the phase carries on: +7, not the root
    r.block({}, 480);
    CHECK(int(r.drv.view(0).period) == note(71));
}

TEST_CASE("a noise P's first step lands on the tick after its row", "[driver][commands][rom942]")
{
    // Section 150.
    Rig r; r.tickHz = 100.0; r.song.noteSource[3] = tracker::NoteSource::Tracker;
    auto& n = r.bank.instruments[20]; n = Instrument::defaults(InstrumentType::Noise, "N"); n.used = true;
    ChannelParams p; p.instrument = 21; p.velocityMode = 2; r.drv.setParams(3, p);
    NoteEvent e = cellOn(3, 60, 21); e.cmd1 = { Cmd::P, 0x20, 0, 0 };
    auto w = r.block({ e }, 480);
    std::vector<int> nr43;
    for (const auto& x : w) if (x.addr == 0xFF22 && (nr43.empty() || nr43.back() != x.value)) nr43.push_back(x.value);
    CHECK(nr43.size() == 1);                                  // the note's own byte, nothing moved yet
    w = r.block({}, 480);
    CHECK(has(w, 0xFF22));                                    // the first step, a tick later
}

TEST_CASE("V 00 starts the slowest vibrato when none runs, and stops one that does", "[driver][commands][pitch][rom942]")
{
    // Section 151 ($7DEB).
    Rig r; r.tickHz = 100.0; r.song.noteSource[0] = tracker::NoteSource::Tracker;
    r.bank.instruments[0].vib.depth = 0;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    NoteEvent e = cellOn(0, 69, 1); e.cmd1 = { Cmd::V, 0, 0, 0 };
    r.block({ e }, 480);
    auto w = r.block({}, 48000);
    CHECK(periodWrites(w).size() > 4);                        // the pitch moves: a vibrato runs
    NoteEvent off = cellOn(0, 69, 0); off.cmd1 = { Cmd::V, 0, 0, 0 };
    r.block({ off }, 480);
    w = r.block({}, 48000);
    CHECK(periodWrites(w).size() <= 2);                       // and V 00 again stops it
}

TEST_CASE("a table's L aims at its own row's transpose as an offset while the column in force stays", "[driver][table][commands][rom942]")
{
    // Section 152: the manual's own example, then an L row whose transpose is 0.
    Rig r; r.tickHz = 100.0; r.song.noteSource[0] = tracker::NoteSource::Tracker;
    Table t; t.used = true; t.name = "Manual";
    t.steps[0].hasTranspose = true; t.steps[0].transpose = 12;
    t.steps[1].hasTranspose = true; t.steps[1].transpose = -12; t.steps[1].cmd1 = { Cmd::L, 8, 0, 0 };
    for (int i = 2; i < 16; ++i) { t.steps[size_t(i)].hasTranspose = true; t.steps[size_t(i)].transpose = 12; }
    t.end = TableEnd::Loop;
    r.bank.tables[0] = t;
    auto& in = r.bank.instruments[0]; in.vib.depth = 0; in.table = 1; in.pitchSpeed = PitchSpeed::Fast;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    auto w = r.block({ cellOn(0, 69, 1) }, 480);
    CHECK(has(w, 0xFF13, note(81) & 0xFF));                  // +12 on the note
    r.block({}, 480);                                         // row 1: the L
    r.block({}, 4800);                                        // the nine-update slide is long done
    CHECK(int(r.drv.view(0).period) == note(69));             // +12 in force, offset -12: the plain note
    Table u; u.used = true; u.name = "Hold";
    u.steps[0].hasTranspose = true; u.steps[0].transpose = 4;
    u.steps[1].cmd1 = { Cmd::L, 3, 0, 0 };
    u.end = TableEnd::Loop;
    r.bank.tables[0] = u;
    r.block({ cellOn(0, 69, 1) }, 480);
    CHECK(int(r.drv.view(0).period) == note(73));
    r.block({}, 480);                                         // row 1: L 03 with no transpose -- nothing moves
    CHECK(int(r.drv.view(0).period) == note(73));
    r.block({}, 480);                                         // row 2, no transpose: plain
    CHECK(int(r.drv.view(0).period) == note(69));
}

TEST_CASE("a bend past the top of the note table comes round nine octaves", "[driver][pitch][rom942]")
{
    // Section 153 (0:$1B28).
    Rig r; r.tickHz = 100.0; r.song.noteSource[0] = tracker::NoteSource::Tracker;
    auto& in = r.bank.instruments[0]; in.vib.depth = 0; in.pitchSpeed = PitchSpeed::Fast;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    NoteEvent e = cellOn(0, 100, 1); e.cmd1 = { Cmd::P, 0x20, 0, 0 };
    const auto per = periodWrites(r.block({ e }, 48000));
    REQUIRE(per.size() > 40);
    bool wrapped = false; size_t at = 0;
    for (size_t i = 1; i < per.size(); ++i) if (per[i] < per[i - 1] - 500) { wrapped = true; at = i; break; }
    CHECK(wrapped);                                           // it came round
    CHECK(per.back() != 2047);                                // and did not stick at the top
    if (wrapped && at + 3 < per.size()) CHECK(per[at + 3] > per[at]);   // climbing again
}

TEST_CASE("an A to an empty table runs it", "[driver][table][rom942]")
{
    // Section 154.
    Rig r; r.tickHz = 100.0; r.song.noteSource[0] = tracker::NoteSource::Tracker;
    Table t; t.used = true; t.name = "Parent";
    t.steps[0].hasTranspose = true; t.steps[0].transpose = 4;
    t.steps[1].cmd1 = { Cmd::A, 2, 0, 0 };
    t.end = TableEnd::Loop; r.bank.tables[0] = t;
    r.bank.tables[1] = Table{};
    auto& in = r.bank.instruments[0]; in.vib.depth = 0; in.table = 1;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    r.block({ cellOn(0, 69, 1) }, 480);
    CHECK(int(r.drv.view(0).period) == note(73));
    r.block({}, 480);                                         // row 1: the A
    r.block({}, 480 * 20);
    CHECK(int(r.drv.view(0).period) == note(69));             // sixteen empty rows, and no row 0 coming round
}

TEST_CASE("a table H to its own row holds the row", "[driver][table][rom942]")
{
    // Section 155.
    Rig r; r.tickHz = 100.0; r.song.noteSource[0] = tracker::NoteSource::Tracker;
    Table t; t.used = true; t.name = "Hold";
    t.steps[0].hasTranspose = true; t.steps[0].transpose = -12; t.steps[0].cmd1 = { Cmd::H, 0, 0, 0 };
    t.end = TableEnd::Loop; r.bank.tables[0] = t;
    auto& in = r.bank.instruments[0]; in.vib.depth = 0; in.table = 1;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    r.block({ cellOn(0, 69, 1) }, 480);
    CHECK(int(r.drv.view(0).period) == note(57));
    r.block({}, 480 * 30);
    CHECK(int(r.drv.view(0).period) == note(57));             // still on row 0, thirty ticks on
}

TEST_CASE("an A inside a table replaces the table", "[driver][table][rom942]")
{
    // Section 157 (correcting 131): the ROM keeps one table number per channel.
    // Measured: a parent whose row 1 carries `A` in CMD 2 never reaches its own
    // row 2 (`W 03`) again, and the called table's row 3 (`+4`) lands on tick 5
    // and every sixteen ticks after.
    Rig r; r.tickHz = 100.0; r.song.noteSource[0] = tracker::NoteSource::Tracker;
    Table a; a.used = true; a.end = TableEnd::Loop;
    a.steps[0].cmd1 = { Cmd::W, 1, 0, 0 };
    a.steps[1].cmd2 = { Cmd::A, 2, 0, 0 };
    a.steps[2].cmd1 = { Cmd::W, 3, 0, 0 };
    r.bank.tables[0] = a;
    Table b; b.used = true; b.end = TableEnd::Loop;
    b.steps[3].hasTranspose = true; b.steps[3].transpose = 4;
    r.bank.tables[1] = b;
    auto& in = r.bank.instruments[0]; in.vib.depth = 0; in.table = 1;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    auto w = r.block({ cellOn(0, 69, 1) }, 480);                    // tick 0: row 0, W 01
    const RegWrite* d = last(w, 0xFF11); REQUIRE(d != nullptr); CHECK((d->value >> 6) == 1);
    r.block({}, 480);                                               // tick 1: the A
    for (int k = 2; k <= 4; ++k) {                                  // ticks 2-4: the called table's rows 0-2
        w = r.block({}, 480);
        CHECK_FALSE(has(w, 0xFF11));                                // the parent's W 03 never comes
        CHECK(int(r.drv.view(0).period) == note(69));
    }
    r.block({}, 480);                                               // tick 5: row 3, +4
    CHECK(int(r.drv.view(0).period) == note(73));
    r.block({}, 480);
    CHECK(int(r.drv.view(0).period) == note(69));
    for (int k = 7; k <= 20; ++k) r.block({}, 480);
    r.block({}, 480);                                               // tick 21: row 3 again
    CHECK(int(r.drv.view(0).period) == note(73));
}

TEST_CASE("the transpose column after an A is the new table's, and on noise the note keeps the old one", "[driver][table][noise][rom942]")
{
    // Sections 145 and 157: `parent 0 A+4 / called 0 +12` gives +4, +12, plain on
    // a pulse (1964 1995 1943 in the ROM's periods) and +4, +16, +4 on noise.
    Rig r; r.tickHz = 100.0; r.song.noteSource[0] = tracker::NoteSource::Tracker;
    Table a; a.used = true; a.end = TableEnd::Loop;
    a.steps[0].hasTranspose = true; a.steps[0].transpose = 4; a.steps[0].cmd1 = { Cmd::A, 2, 0, 0 };
    Table b; b.used = true; b.end = TableEnd::Loop;
    b.steps[0].hasTranspose = true; b.steps[0].transpose = 12;
    r.bank.tables[0] = a; r.bank.tables[1] = b;
    auto& in = r.bank.instruments[0]; in.vib.depth = 0; in.table = 1;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    r.block({ cellOn(0, 69, 1) }, 480);
    CHECK(int(r.drv.view(0).period) == note(73));
    r.block({}, 480);
    CHECK(int(r.drv.view(0).period) == note(81));
    r.block({}, 480);
    CHECK(int(r.drv.view(0).period) == note(69));
    // Noise: the same two tables under a noise instrument. The column's deltas
    // are on the note, so the +4 stays under the called table.
    r.song.noteSource[3] = tracker::NoteSource::Tracker;
    auto& n = r.bank.instruments[20]; n = Instrument::defaults(InstrumentType::Noise, "N"); n.used = true; n.table = 1;
    ChannelParams q; q.instrument = 21; q.velocityMode = 2; r.drv.setParams(3, q);
    auto nr43 = [&](const std::vector<RegWrite>& w) { const RegWrite* l = last(w, 0xFF22); return l ? int(l->value) : -1; };
    const int t0 = nr43(r.block({ cellOn(3, 60, 21) }, 480));
    const int t1 = nr43(r.block({}, 480));
    const int t2 = nr43(r.block({}, 480));
    CHECK(t0 >= 0); CHECK(t1 >= 0);
    CHECK(t1 != t0);                                                // +16 on tick 1
    CHECK((t2 == t0 || t2 < 0));                                    // +4 again on tick 2 (or no write: the same byte)
}

TEST_CASE("a table Z on a W re-rolls the duty", "[driver][table][rom942]")
{
    // Section 74's law on a table's CMD 2: `W 00` on row 0, `Z 02` on row 1 --
    // the ROM's duty comes out 0, 1 or 2 on the Z's tick (CASTSHDW's PU1).
    Rig r; r.tickHz = 100.0; r.song.noteSource[0] = tracker::NoteSource::Tracker;
    Table t; t.used = true; t.end = TableEnd::Loop;
    t.steps[0].cmd2 = { Cmd::W, 0, 0, 0 };
    t.steps[1].cmd2 = { Cmd::Z, 0, 2, 0 };
    r.bank.tables[0] = t;
    auto& in = r.bank.instruments[0]; in.vib.depth = 0; in.table = 1; in.duty = 2;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    int changed = 0;
    for (int n = 0; n < 6; ++n) {
        auto w = r.block({ cellOn(0, 69, 1) }, 480);
        const RegWrite* d0 = last(w, 0xFF11); REQUIRE(d0 != nullptr); CHECK((d0->value >> 6) == 0);   // row 0: W 00
        w = r.block({}, 480);                                                                            // row 1: the Z
        if (const RegWrite* d1 = last(w, 0xFF11); d1 != nullptr && (d1->value >> 6) != 0) ++changed;
        r.block({}, 480 * 2);
    }
    CHECK(changed > 0);                                   // random, but not never
}

TEST_CASE("a pulse note's trigger is the plain period; the finetune rides the next pitch clock", "[driver][commands][rom942]")
{
    // Section 163: the ROM triggers from the plain note's period and the
    // refresh at the next 358 Hz instant brings the finetune; a bare note
    // starts plain again. On PU1 `40` is a quarter semitone down.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[0] = tracker::NoteSource::Tracker;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Lead");
    i.used = true; i.fineTune = 0x40;
    ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(0, p);
    auto w = r.block({ cellOn(0, 60, 2) }, 480);
    const int plain = Driver::periodForNote(60, false), fine = Driver::periodForNote(60.0 - 0x40 / 256.0, false);
    REQUIRE(fine < plain);
    std::vector<std::pair<uint64_t, int>> lo;     // (cycle, NR13) in order
    for (const auto& x : w) if (x.addr == 0xFF13) lo.push_back({ x.cycle, int(x.value) });
    REQUIRE(lo.size() >= 2);
    CHECK(lo[0].second == (plain & 0xFF));         // the trigger: plain
    CHECK(lo[1].second == (fine & 0xFF));          // the epilogue: finetuned, in the tick's own burst
    CHECK(lo[1].first - lo[0].first < 2000);
    // With a FAST P on the cell the refresh is the 358 Hz handler's, at the next instant.
    {
        Rig q;
        q.tickHz = 100.0;
        q.song.noteSource[0] = tracker::NoteSource::Tracker;
        q.bank.instruments[1] = i;
        q.drv.setParams(0, p);
        NoteEvent on = cellOn(0, 60, 2); on.cmd1 = { Cmd::P, 3, 0, 0 };
        auto wq = q.block({ on }, 480);
        std::vector<std::pair<uint64_t, int>> lq;
        for (const auto& x : wq) if (x.addr == 0xFF13) lq.push_back({ x.cycle, int(x.value) });
        REQUIRE(lq.size() >= 2);
        CHECK(lq[0].second == (plain & 0xFF));
        CHECK(lq[1].second == (fine & 0xFF));
        CHECK(lq[1].first - lq[0].first > 2000);
        CHECK(lq[1].first - lq[0].first <= 11712);
    }
    // A bare note two ticks on: plain again at its own write, finetuned at the next instant.
    r.block({}, 480);
    NoteEvent bare = cellOn(0, 63, 0);
    auto w2 = r.block({ bare }, 480);
    const int plain2 = Driver::periodForNote(63, false), fine2 = Driver::periodForNote(63.0 - 0x40 / 256.0, false);
    std::vector<int> lo2;
    for (const auto& x : w2) if (x.addr == 0xFF13) lo2.push_back(int(x.value));
    REQUIRE(lo2.size() >= 2);
    CHECK(lo2[0] == (plain2 & 0xFF));
    CHECK(lo2[1] == (fine2 & 0xFF));
}

TEST_CASE("an imported instrument's envelope is the ROM's three-stage countdown machine", "[driver][shaped][rom942]")
{
    // Section 164: a level step every table[rate] pitch-clock instants toward
    // each stage's target; a stage whose target is the current level holds
    // one countdown; a rate of zero stops the machine.
    auto stepsOf = [](uint8_t b1, uint8_t b9, uint8_t b10) {
        auto r = std::make_unique<Rig>();
        r->tickHz = 100.0;
        r->song.noteSource[0] = tracker::NoteSource::Tracker;
        auto& i = r->bank.instruments[1];
        i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Lead");
        i.used = true; i.env.mode = bank::EnvMode::Shaped; i.env.start = uint8_t(b1 >> 4);
        i.env.lsdj = true; i.env.lsdjByte1 = b1; i.env.lsdjByte9 = b9; i.env.lsdjByte10 = b10;
        i.envVol = uint8_t(b1 >> 4);
        ChannelParams p; p.instrument = 2; p.velocityMode = 2; r->drv.setParams(0, p);
        std::vector<RegWrite> w = r->block({ cellOn(0, 60, 2) }, 480);
        for (int k = 0; k < 60; ++k) { auto more = r->block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
        uint64_t note = 0; bool haveNote = false;
        std::vector<std::pair<int, int>> steps;   // (instant index, 8 up / 9 down)
        for (const auto& x : w) {
            if (x.addr == 0xFF14 && (x.value & 0x80) && !haveNote) { haveNote = true; note = x.cycle; continue; }
            if (!haveNote || x.addr != 0xFF12) continue;
            if (x.value == 0x08 || x.value == 0x09) steps.push_back({ int((x.cycle - note + 5852) / 11704), int(x.value) });
        }
        return steps;
    };
    {   // F3 85 46: F -> 8 every 3, 8 -> 4 every 6, 4 -> 0 every 8
        const auto s = stepsOf(0xF3, 0x85, 0x46);
        const std::vector<int> want = { 3, 6, 9, 12, 15, 18, 21, 27, 33, 39, 45, 53, 61, 69, 77 };
        REQUIRE(s.size() == want.size());
        for (size_t k = 0; k < want.size(); ++k) { INFO("step " << k); CHECK(s[k].first == want[k]); CHECK(s[k].second == 9); }
    }
    {   // 39 36 08: 3 -> 3 holds 20, then 3 -> 0 every 8, then a silent stage of 15 and off
        const auto s = stepsOf(0x39, 0x36, 0x08);
        const std::vector<int> want = { 28, 36, 44 };
        REQUIRE(s.size() == want.size());
        for (size_t k = 0; k < want.size(); ++k) { INFO("step " << k); CHECK(s[k].first == want[k]); }
    }
    {   // 42 C3 07: 4 -> C up every 2, C -> 0 down every 3, then off
        const auto s = stepsOf(0x42, 0xC3, 0x07);
        REQUIRE(s.size() == 8 + 12);
        for (int k = 0; k < 8; ++k) { INFO("up " << k); CHECK(s[size_t(k)].first == 2 * (k + 1)); CHECK(s[size_t(k)].second == 8); }
        for (int k = 0; k < 12; ++k) { INFO("down " << k); CHECK(s[size_t(8 + k)].first == 16 + 3 * (k + 1)); CHECK(s[size_t(8 + k)].second == 9); }
    }
    {   // F0 85 46: a first rate of zero never starts the machine
        CHECK(stepsOf(0xF0, 0x85, 0x46).empty());
    }
}

TEST_CASE("a STEP table's position is shared by the channels playing the instrument", "[driver][table][rom942]")
{
    // Section 166: one walk per instrument, in note order across the channels.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[0] = tracker::NoteSource::Tracker; r.song.noteSource[1] = tracker::NoteSource::Tracker;
    auto& t = r.bank.tables[4]; t = Table{}; t.used = true;
    t.steps[0].hasTranspose = true; t.steps[0].transpose = 12;
    t.steps[1].hasTranspose = true; t.steps[1].transpose = 5;
    t.steps[2].hasTranspose = true; t.steps[2].transpose = 8;
    t.steps[3].hasTranspose = true; t.steps[3].transpose = 1;
    auto& i = r.bank.instruments[20];
    i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Step");
    i.used = true; i.table = 5; i.tableMode = bank::TableMode::Step;
    for (int ch = 0; ch < 2; ++ch) { ChannelParams p; p.instrument = 21; r.drv.setParams(ch, p); }
    std::vector<int> got;
    auto period = [&](int ch, const std::vector<RegWrite>& w) {
        int lo = -1, hi = -1;
        for (const auto& x : w) { if (x.addr == uint16_t(0xFF13 + ch * 5)) lo = x.value; if (x.addr == uint16_t(0xFF14 + ch * 5)) hi = x.value & 7; }
        return lo < 0 ? -1 : (hi << 8) | lo;
    };
    // Both channels on one tick: PU1 first, then PU2.
    auto w = r.block({ cellOn(0, 60, 21), cellOn(1, 60, 21) }, 480);
    got.push_back(period(0, w)); got.push_back(period(1, w));
    r.block({}, 480);
    w = r.block({ cellOn(0, 60, 21), cellOn(1, 60, 21) }, 480);
    got.push_back(period(0, w)); got.push_back(period(1, w));
    REQUIRE(got.size() == 4);
    CHECK(got[0] == Driver::periodForNote(72, false));   // row 0: +12
    CHECK(got[1] == Driver::periodForNote(65, false));   // row 1: +5
    CHECK(got[2] == Driver::periodForNote(68, false));   // row 2: +8
    CHECK(got[3] == Driver::periodForNote(61, false));   // row 3: +1
}

TEST_CASE("a STEP row's A does not move the instrument's position past its own row", "[driver][table][rom942]")
{
    // Section 166, EGOFLEX's instrument 1B: row 0 is an `A` to another table
    // beside a `W`; the second channel's note, the same tick, takes row 1.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[0] = tracker::NoteSource::Tracker; r.song.noteSource[1] = tracker::NoteSource::Tracker;
    auto& t = r.bank.tables[4]; t = Table{}; t.used = true;              // slot 5: the instrument's STEP table
    t.steps[0].cmd1 = { Cmd::A, 6, 0, 0 }; t.steps[0].cmd2 = { Cmd::W, 0, 0, 0 };
    t.steps[1].hasTranspose = true; t.steps[1].transpose = 5;
    auto& u = r.bank.tables[5]; u = Table{}; u.used = true;              // slot 6: the A's target
    u.steps[0].hasTranspose = true; u.steps[0].transpose = 12;
    auto& i = r.bank.instruments[20];
    i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Step");
    i.used = true; i.table = 5; i.tableMode = bank::TableMode::Step;
    for (int ch = 0; ch < 2; ++ch) { ChannelParams p; p.instrument = 21; r.drv.setParams(ch, p); }
    auto period = [&](int ch, const std::vector<RegWrite>& w) {
        int lo = -1, hi = -1;
        for (const auto& x : w) { if (x.addr == uint16_t(0xFF13 + ch * 5)) { lo = x.value; } if (x.addr == uint16_t(0xFF14 + ch * 5)) hi = x.value & 7; }
        return lo < 0 ? -1 : (hi << 8) | lo;
    };
    auto w = r.block({ cellOn(0, 60, 21), cellOn(1, 60, 21) }, 480);
    CHECK(period(1, w) == Driver::periodForNote(65, false));   // PU2: row 1, +5
    // PU1 took row 0: the A moved it to the other table, whose row 0 (+12) is the next tick's.
    auto w2 = r.block({}, 480);
    int lo = -1; for (const auto& x : w2) if (x.addr == 0xFF13) lo = x.value;
    CHECK(lo == (Driver::periodForNote(72, false) & 0xFF));
}

TEST_CASE("R's level nibble rewrites the envelope's levels and the machine restarts from there", "[driver][shaped][rom942]")
{
    // Section 175, REPTCOMP's instrument 16: 74 71 48 with R B0 on the note
    // becomes 24 21 08 -- level 2, a hold of four, two steps to 0, and the
    // third stage finds its target reached: two steps and no more.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[3] = tracker::NoteSource::Tracker;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Noise, "Hat");
    i.used = true; i.env.mode = bank::EnvMode::Shaped; i.env.start = 7; i.envVol = 7;
    i.env.lsdj = true; i.env.lsdjByte1 = 0x74; i.env.lsdjByte9 = 0x71; i.env.lsdjByte10 = 0x48;
    ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(3, p);
    NoteEvent on = cellOn(3, 60, 2); on.cmd1 = { Cmd::R, 11, 0, 0 };
    std::vector<RegWrite> w = r.block({ on }, 480);
    for (int k = 0; k < 12; ++k) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
    uint64_t note = 0; bool haveNote = false; int trigLevel = -1;
    std::vector<std::pair<int, int>> steps;
    for (const auto& x : w) {
        if (x.addr == 0xFF23 && (x.value & 0x80)) { if (!haveNote) { haveNote = true; note = x.cycle; } continue; }
        if (!haveNote || x.addr != 0xFF21) continue;
        if (x.value == 0x08 || x.value == 0x09) steps.push_back({ int((x.cycle - note + 5852) / 11704), int(x.value) });
        else if (trigLevel < 0 || (x.cycle - note) < 2000) trigLevel = x.value >> 4;
    }
    CHECK(trigLevel == 2);                                    // 7 - 5
    REQUIRE(steps.size() == 2);
    // A hold of 4 (the level is its own first target), then down a step an instant to 0, then nothing.
    CHECK(steps[0].first == 5); CHECK(steps[0].second == 9);
    CHECK(steps[1].first == 6); CHECK(steps[1].second == 9);
}

TEST_CASE("a roll's trigger carries the machine's level, comes before the step, and has no length bit", "[driver][commands][rom942]")
{
    // Section 168: R 82 on a noise note with a LENGTH and a stepping machine.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[3] = tracker::NoteSource::Tracker;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Noise, "Hat");
    i.used = true; i.env.mode = bank::EnvMode::Shaped; i.env.start = 12; i.envVol = 12; i.length = 20;
    i.env.lsdj = true; i.env.lsdjByte1 = 0xC3; i.env.lsdjByte9 = 0x84; i.env.lsdjByte10 = 0x41;
    ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(3, p);
    NoteEvent on = cellOn(3, 60, 2); on.cmd1 = { Cmd::R, 8, 2, 0 };
    std::vector<RegWrite> w = r.block({ on }, 480);
    for (int k = 0; k < 4; ++k) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
    // The note's trigger keeps the length bit; the roll's does not, and its NR42 is the level before the instant's step.
    int trig = 0; bool sawStepAfterRoll = false; int rollLevel = -1; uint8_t rollNr44 = 0xFF; uint64_t rollAt = 0;
    for (size_t k = 0; k < w.size(); ++k) {
        const auto& x = w[k];
        if (x.addr == 0xFF23 && (x.value & 0x80)) { ++trig; if (trig == 2) { rollNr44 = x.value; rollAt = x.cycle; for (size_t j = k; j > 0; --j) if (w[j - 1].addr == 0xFF21) { rollLevel = w[j - 1].value >> 4; break; } } }
        if (trig == 2 && x.addr == 0xFF21 && x.value == 0x09 && x.cycle >= rollAt && x.cycle < rollAt + 2000) sawStepAfterRoll = true;
    }
    REQUIRE(trig >= 2);
    CHECK((rollNr44 & 0x40) == 0);
    CHECK(rollLevel == 12);                                   // C, then the step to B follows
    CHECK(sawStepAfterRoll);
}

TEST_CASE("K walks the machine's level to 0, one triplet a level, after the steps the machine made", "[driver][commands][rom942]")
{
    // Section 176, REPTCOMP's PU2: `E67` on the note (level 6, rate 7), the
    // machine steps 6 -> 3 in three instants' worth of rate, and `K` writes
    // three down-triplets -- the ROM's walk from its own copy of the level.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[1] = tracker::NoteSource::Tracker;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Pulse, "P");
    i.used = true; i.env.mode = bank::EnvMode::Shaped; i.env.start = 0; i.envVol = 0;
    i.env.lsdj = true; i.env.lsdjByte1 = 0x07; i.env.lsdjByte9 = 0xC0; i.env.lsdjByte10 = 0x00;
    ChannelParams p; p.instrument = 2; p.velocityMode = 2; r.drv.setParams(1, p);
    NoteEvent on = cellOn(1, 60, 2); on.cmd1 = { Cmd::E, 6, 7, 0 };
    std::vector<RegWrite> w = r.block({ on }, 480);
    for (int k = 0; k < 36; ++k) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }   // 0.37 s: the machine has taken some of its rate-7 steps
    const size_t before = w.size();
    { auto more = r.block({ cellCmd(1, { Cmd::K, 0, 0, 0 }) }, 480); w.insert(w.end(), more.begin(), more.end()); }
    int stepsBefore = 0, stepsAtKill = 0;
    for (size_t k = 0; k < w.size(); ++k) if (w[k].addr == 0xFF17 && w[k].value == 0x09) (k < before ? stepsBefore : stepsAtKill)++;
    CHECK(has(w, 0xFF17, 0x68));                      // the note's level is E's 6
    CHECK(stepsBefore + stepsAtKill == 6);            // 6 -> 0 in all: the machine's steps and the kill's walk
    CHECK(stepsAtKill == 6 - stepsBefore);
    CHECK(stepsAtKill >= 3);                           // the machine had not walked past 3 by then
    CHECK_FALSE(r.drv.view(1).active);
    for (int k = 0; k < 4; ++k) { auto more = r.block({}, 480); for (const auto& x : more) CHECK(x.addr != 0xFF17); }   // dead: nothing more
}

TEST_CASE("a bare cell takes its own chain row's transpose, and its L slides there", "[driver][commands][rom942]")
{
    // Section 177, EGOFLEX's pad: phrase 2B (transpose 12) plays note 24 with
    // the instrument, phrase 4D (transpose 20) the same note bare with `L 60`.
    // The ROM slides 36 -> 44 over 97 instants; ChipBoy kept the old
    // transpose and had nothing to slide.
    Rig r;
    r.tickHz = 100.0;
    r.song.noteSource[2] = tracker::NoteSource::Tracker;
    auto& i = r.bank.instruments[1];
    i = bank::Instrument::defaults(bank::InstrumentType::Wave, "Pad");
    i.used = true; i.transpose = true;
    ChannelParams p; p.instrument = 2; r.drv.setParams(2, p);
    NoteEvent first = cellOn(2, 24, 2); first.transpose = 12;
    r.block({ first }, 480);
    for (int k = 0; k < 5; ++k) r.block({}, 480);
    NoteEvent bare = cellOn(2, 24, 0); bare.transpose = 20; bare.cmd1 = { Cmd::L, 0x60, 0, 0 };
    std::vector<RegWrite> w = r.block({ bare }, 480);
    for (int k = 0; k < 40; ++k) { auto more = r.block({}, 480); w.insert(w.end(), more.begin(), more.end()); }
    int lo = -1, per = -1, writes = 0, firstPer = -1;
    for (const auto& x : w) {
        if (x.addr == 0xFF1D) lo = x.value;
        else if (x.addr == 0xFF1E && lo >= 0) { per = ((x.value & 7) << 8) | lo; if (firstPer < 0) firstPer = per; ++writes; }
    }
    const int from = Driver::periodOfSemitone(36, true), to = Driver::periodOfSemitone(44, true);
    CHECK(firstPer == from);                                   // the trigger carries the old period (section 173)
    CHECK(writes > 60);                                        // a step an instant, 97 of them
    CHECK(per == to);                                          // and it lands on the transposed note
}


TEST_CASE("Z on a wave F adds to the frame, and F 00 writes no frame", "[driver][wave][rom942]")
{
    // Section 182 (UNMASKED's phrase 10, instrument 18's table: F 00 on row 0,
    // Z 0F on row 1): the Z's byte is x * 16 + y, so `Z 0F` moves the frame by
    // 0-15 within the slot, and `F 00` rewrites nothing.
    Rig r;
    r.tickHz = 100.0;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    auto& w = r.bank.waves[0];
    w.used = true; w.frames.clear();
    for (int f = 0; f < 16; ++f) { bank::Frame fr; fr.s.fill(uint8_t(f)); w.frames.push_back(fr); }
    Table t; t.used = true; t.steps[0].cmd1 = { Cmd::F, 0, 0, 0 }; t.steps[1].cmd1 = { Cmd::Z, 0, 15, 0 };
    r.bank.tables[7] = t;
    auto& i = r.bank.instruments[1];
    i = Instrument::defaults(InstrumentType::Wave, "Z"); i.used = true; i.wave = 1; i.frameAdvance = 0; i.table = 8;
    ChannelParams p; p.instrument = 2; r.drv.setParams(2, p);
    int moved = 0;
    for (int n = 0; n < 12; ++n) {
        std::vector<RegWrite> all = r.block({ cellOn(2, 60, 2) }, 480);
        for (int k = 0; k < 2; ++k) { auto more = r.block({}, 480); all.insert(all.end(), more.begin(), more.end()); }
        std::vector<int> w0;
        for (const auto& x : all) if (x.addr == 0xFF30) w0.push_back(x.value);
        REQUIRE(w0.size() >= 1);
        CHECK(w0.size() <= 2);                          // the note's frame, then the Z's: no rewrite for F 00
        CHECK(w0[0] == 0);
        if (w0.size() == 2 && w0[1] != 0) ++moved;
    }
    CHECK(moved >= 6);                                   // 0-15 at random: most notes move
}

// --- section 188: the pre-9.1 noise channel as an instrument mode -----------

TEST_CASE("the shape mode makes NR43 from SHAPE and the octave, and S, P, C and the column work on the byte", "[driver][noise][versions]")
{
    // Measured on the 8.5.1 ROM (section 188): SHAPE FF at G-5 (LSDj note 20,
    // MIDI 67) is 10; S 03 takes the nibbles to 1D; P 02 walks 1E 1C 1A; C 37
    // alternates the note and the byte's complement E9; a table's transpose
    // column comes off as a byte and drops the S delta.
    Rig r;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    auto& i = r.bank.instruments[20]; i = Instrument::defaults(InstrumentType::Noise, "old"); i.used = true;
    i.noiseShapeMode = true; i.noiseShape = 0xFF; i.noiseDomain = bank::NoiseSweepDomain::Register; i.noisePitch = bank::NoisePitch::Never;
    ChannelParams p; p.instrument = 21; r.drv.setParams(3, p);
    auto nr43s = [](const std::vector<RegWrite>& w) { std::vector<int> v; for (const auto& x : w) if (x.addr == 0xFF22) v.push_back(x.value); return v; };
    SECTION("the note and the octave") {
        auto w = r.block({ cellOn(3, 67, 21) }, 200);
        REQUIRE_FALSE(nr43s(w).empty()); CHECK(nr43s(w).front() == 0x10);
        w = r.block({ cellOn(3, 67 - 12, 21) }, 200);                 // G-4: an octave down raises the shift
        CHECK(nr43s(w).front() == 0x20);
        w = r.block({ cellOn(3, 67 + 24, 21) }, 200);                 // G-7: saturates at 0
        CHECK(nr43s(w).front() == 0x00);
    }
    SECTION("SHAPE's nibbles saturate on their own") {
        r.bank.instruments[20].noiseShape = 0x0F;
        auto w = r.block({ cellOn(3, 55, 21) }, 200);                 // G-4 with SHAPE 0F: F0, not FF
        CHECK(nr43s(w).front() == 0xF0);
    }
    SECTION("S subtracts nibble by nibble, P every tick, C alternates the byte") {
        auto e = cellOn(3, 67, 21); e.cmd1 = { Cmd::S, 0, 3, 0 };
        auto w = r.block({ e }, 200);
        REQUIRE(last(w, 0xFF22) != nullptr); CHECK(last(w, 0xFF22)->value == 0x1D);
        e = cellOn(3, 67, 21); e.cmd1 = { Cmd::P, 2, 0, 0 };
        w = r.block({ e }, 200);
        w = r.block({}, 200); REQUIRE(last(w, 0xFF22) != nullptr); CHECK(last(w, 0xFF22)->value == 0x1E);
        w = r.block({}, 200); REQUIRE(last(w, 0xFF22) != nullptr); CHECK(last(w, 0xFF22)->value == 0x1C);
        e = cellOn(3, 67, 21); e.cmd1 = { Cmd::C, 3, 7, 0 };
        w = r.block({ e }, 200);
        w = r.block({}, 200); REQUIRE(last(w, 0xFF22) != nullptr); CHECK(last(w, 0xFF22)->value == 0xE9);
        w = r.block({}, 200); REQUIRE(last(w, 0xFF22) != nullptr); CHECK(last(w, 0xFF22)->value == 0x10);
    }
    SECTION("STABLE keeps the note's width bit") {
        r.bank.instruments[20].noiseShape = 0xF7; r.bank.instruments[20].noiseStable = true;   // the note is 18: bit 3 set
        auto e = cellOn(3, 67, 21); e.cmd1 = { Cmd::S, 0, 3, 0 };
        auto w = r.block({ e }, 200);
        REQUIRE(last(w, 0xFF22) != nullptr); CHECK(last(w, 0xFF22)->value == 0x1D);           // 15 with the width bit kept
    }
    SECTION("the table's transpose column is a byte off NR43 and drops the S delta") {
        auto& t = r.bank.tables[9]; t = Table{}; t.used = true;
        t.steps[0].hasTranspose = true; t.steps[0].transpose = 3;
        r.bank.instruments[20].table = 10;
        auto e = cellOn(3, 67, 21); e.cmd1 = { Cmd::S, 0, 3, 0 };
        auto w = r.block({ e }, 200);                                                          // the S (1D), then row 0's column on the update after
        REQUIRE(last(w, 0xFF22) != nullptr); CHECK(last(w, 0xFF22)->value == 0x0D);           // 10 - 03, the S gone
        w = r.block({}, 200);                                                                   // row 1, no column: the note again
        REQUIRE(last(w, 0xFF22) != nullptr); CHECK(last(w, 0xFF22)->value == 0x10);
        w = r.block({}, 200);                                                                   // row 2: nothing changes, nothing written
        CHECK_FALSE(has(w, 0xFF22));
    }
}

// --- section 189: the hardware envelope stages ----------------------------

TEST_CASE("a Chip envelope's stages write the next byte with a retrigger after the levels and a half", "[driver][envelope][versions]")
{
    // 8.5.1's A3 / 54 / 20: stage 2 at (2 x 5 + 1) x 3 / 128 s = 0.258 s, stage 3
    // (2 x 3 + 1) x 4 / 128 s = 0.219 s later, each a retrigger (section 189).
    Rig r;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    auto& i = r.bank.instruments[0]; i = Instrument::defaults(InstrumentType::Pulse, "adsr"); i.used = true;
    i.env.mode = bank::EnvMode::Chip; i.envVol = 0xA; i.envDir = bank::EnvDir::Down; i.envRate = 3; i.envStage2 = 0x54; i.envStage3 = 0x20;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    std::vector<std::pair<uint64_t, int>> trig;     // (cycle, NR12 just before)
    int nr12 = -1;
    auto scan = [&](const std::vector<RegWrite>& w) {
        for (const auto& x : w) {
            if (x.addr == 0xFF12) nr12 = x.value;
            if (x.addr == 0xFF14 && (x.value & 0x80)) trig.push_back({ x.cycle, nr12 });
        }
    };
    scan(r.block({ cellOn(0, 60, 1, 80) }, 200));            // velocity 80 is level A
    for (int k = 0; k < 140; ++k) scan(r.block({}, 200));
    // Three triggers: the note's, stage 2's and stage 3's, with the bytes A8, 58, 28 (ChipBoy holds the nibble and steps the level itself).
    REQUIRE(trig.size() == 3);
    CHECK(trig[0].second == 0xA8); CHECK(trig[1].second == 0x58); CHECK(trig[2].second == 0x28);
    CHECK(r.drv.view(0).envVol == 2);
}

TEST_CASE("the stages keep the ROM's timing", "[driver][envelope][versions]")
{
    Rig r;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    auto& i = r.bank.instruments[0]; i = Instrument::defaults(InstrumentType::Pulse, "adsr"); i.used = true;
    i.env.mode = bank::EnvMode::Chip; i.envVol = 0xA; i.envDir = bank::EnvDir::Down; i.envRate = 3; i.envStage2 = 0x54; i.envStage3 = 0x20;
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    std::vector<uint64_t> trigCycles;
    auto scan = [&](const std::vector<RegWrite>& w) { for (const auto& x : w) if (x.addr == 0xFF14 && (x.value & 0x80)) trigCycles.push_back(x.cycle); };
    scan(r.block({ cellOn(0, 60, 1, 80) }, 200));
    for (int k = 0; k < 140; ++k) scan(r.block({}, 200));
    REQUIRE(trigCycles.size() == 3);
    const double s2 = double(trigCycles[1] - trigCycles[0]) / 4194304.0, s3 = double(trigCycles[2] - trigCycles[1]) / 4194304.0;
    CHECK(s2 > 0.250); CHECK(s2 < 0.266);            // 0.258 s, within a pitch clock
    CHECK(s3 > 0.211); CHECK(s3 < 0.227);            // 0.219 s
}

// --- section 190: a kit's bend steps on the tick too ----------------------

TEST_CASE("a kit's P moves the period once more on every tick", "[driver][kit][versions]")
{
    Rig r;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    auto& k = r.bank.kits[0]; k = bank::Kit{}; k.used = true; k.name = "K"; k.period = 1000;   // low enough not to wrap past 2047
    bank::KitSample a; a.note = 60; a.data.assign(8192, uint8_t(8));
    k.samples.push_back(a);
    auto& i0 = r.bank.instruments[1]; i0 = Instrument::defaults(InstrumentType::Kit, "K"); i0.used = true; i0.kit = 1;
    ChannelParams p; p.instrument = 2; r.drv.setParams(2, p);
    auto e = cellOn(2, 60, 2); e.cmd1 = { Cmd::P, 4, 0, 0 };
    int lo = -1, hi = -1;
    auto scan = [&](const std::vector<RegWrite>& w) { for (const auto& x : w) { if (x.addr == 0xFF1D) lo = x.value; if (x.addr == 0xFF1E) hi = x.value & 7; } };   // NR33, NR34
    scan(r.block({ e }, 200));
    for (int t = 0; t < 47; ++t) scan(r.block({}, 200));            // 48 ticks, 0.2 s: 71-72 instants
    REQUIRE(lo >= 0); REQUIRE(hi >= 0);
    const int period = (hi << 8) | lo;
    // 4 an instant (287) plus 4 a tick (192): about 479, where the instant alone gave 287.
    CHECK(period - 1000 > 440); CHECK(period - 1000 < 500);
}

// --- section 197: any instrument on any channel ---------------------------

TEST_CASE("a channel reads an instrument of another kind as LSDj would, from its bytes", "[driver][versions]")
{
    Rig r;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    SECTION("a native pulse on the wave channel is a wave: its envelope byte's bits 5-6 are the level") {
        auto& i = r.bank.instruments[0]; i = Instrument::defaults(InstrumentType::Pulse, "lead"); i.used = true;
        i.envVol = 0xC; i.envRate = 3;                                   // byte 1 = C3: code 2, level 2 (50 %)
        ChannelParams p; p.instrument = 1; r.drv.setParams(2, p);
        auto w = r.block({ cellOn(2, 60, 1) }, 480);
        CHECK(has(w, 0xFF1A, 0x80));                                     // NR30: the DAC on
        REQUIRE(last(w, 0xFF1C) != nullptr); CHECK(last(w, 0xFF1C)->value == 0x40);   // NR32 50 %
        CHECK_FALSE(has(w, 0xFF12));
    }
    SECTION("an imported wave on PU1 is a pulse read from the save's bytes") {
        auto& i = r.bank.instruments[0]; i = Instrument::defaults(InstrumentType::Wave, "bass"); i.used = true;
        i.lsdjFormat = 22;
        i.lsdjBytes = { 1, 0x20, 0x0F, 0x00, 0xFF, 0, 0, 3, 0, 0x01, 0x0F, 0xFF, 0, 0, 0, 0 };   // X942_wavOnPu's instrument
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
        auto w = r.block({ cellOn(0, 60, 1, 16) }, 480);                              // velocity 16 is level 2, the byte's own
        REQUIRE(last(w, 0xFF12) != nullptr); CHECK(last(w, 0xFF12)->value == 0x28);   // byte 1 = 20: volume 2, held
        REQUIRE(last(w, 0xFF11) != nullptr); CHECK((last(w, 0xFF11)->value & 0xC0) == 0);   // byte 7 = 03: duty 0
        CHECK_FALSE(has(w, 0xFF1A));
    }
}

// --- section 199: a ping-pong run's first pass ----------------------------

TEST_CASE("a ping-pong run walks its first pass through and bounces inside the loop after", "[driver][versions]")
{
    // Probed on 9.4.2 and 8.5.1 (WvP3_b2_0E) and on 7.0.2 (WvP2_b2_01): four
    // steps, the loop the last two -- 0 1 2 3 2 3 2 3. ChipBoy turned at the loop
    // step on the way up too and played 0 3 2 3.
    Rig r;
    for (auto& src : r.song.noteSource) src = tracker::NoteSource::Tracker;
    auto& wv = r.bank.waves[0];
    REQUIRE(wv.frames.size() >= 16);
    for (int f = 0; f < 16; ++f) wv.frames[size_t(f)].s.fill(uint8_t(f));
    auto& i = r.bank.instruments[0]; i = Instrument::defaults(InstrumentType::Wave, "pp"); i.used = true;
    i.wave = 1; i.frameLength = 4; i.frameLoopStep = 2; i.frameLoop = bank::FrameLoop::PingPong; i.frameAdvance = 4; i.frameStart = 0;
    ChannelParams p; p.instrument = 1; r.drv.setParams(2, p);
    std::vector<int> w0;
    auto scan = [&](const std::vector<RegWrite>& w) { for (const auto& x : w) if (x.addr == 0xFF30) w0.push_back(x.value); };
    scan(r.block({ cellOn(2, 60, 1) }, 200));
    for (int k = 0; k < 40; ++k) scan(r.block({}, 200));
    REQUIRE(w0.size() >= 8);
    const std::vector<int> want = { 0x00, 0x55, 0xAA, 0xFF, 0xAA, 0xFF, 0xAA, 0xFF };
    CHECK(std::vector<int>(w0.begin(), w0.begin() + 8) == want);
}
