// ChipBoy -- driver tests (spec section 8, 9, 10).
#include "core/Bank/Bank.h"
#include "core/Driver/Clock.h"
#include "core/Driver/Driver.h"
#include "core/Apu/Apu.h"
#include "core/Render/Renderer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace chipboy;
using namespace chipboy::driver;
using namespace chipboy::bank;

namespace {

struct Rig {
    Bank bank = Bank::factory();
    tracker::Song song;
    Driver drv;
    Clock clock;
    render::Renderer ren;
    std::vector<RegWrite> writes;
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
        drv.process(ev.data(), ev.size(), n, frame, ticks.data(), ticks.size(), [this](uint64_t f) { return ren.cycleForFrame(f); }, writes);
        frame += n;
        return writes;
    }
    static NoteEvent on(int ch, uint8_t note, uint8_t vel = 100, uint32_t off = 0) { NoteEvent e; e.channel = uint8_t(ch); e.kind = NoteEvent::NoteOn; e.a = note; e.b = vel; e.offset = off; return e; }
    static NoteEvent off(int ch, uint8_t note, uint32_t o = 0) { NoteEvent e; e.channel = uint8_t(ch); e.kind = NoteEvent::NoteOff; e.a = note; e.offset = o; return e; }
};

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
    CHECK(has(w, 0xFF12, 0xF0));               // velocity 127 -> volume 15, hold
    CHECK(has(w, 0xFF13, 1750 & 0xFF));
    CHECK(last(w, 0xFF14)->value == uint8_t(0x80 | (1750 >> 8)));   // trigger
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
    CHECK(last(w, 0xFF17)->value == 0x80);     // 64 -> level 8
    r.block({ Rig::off(1, 60) }, 256);
    w = r.block({ Rig::on(1, 30, 100) }, 256); // below C2
    CHECK(r.drv.view(1).outOfRange);
    CHECK_FALSE(has(w, 0xFF19, 0x80 | 0));     // no trigger at a fake period
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

TEST_CASE("legato and retrigger", "[driver]")
{
    Rig r;
    Bank& b = r.bank;
    b.instruments[0].legato = true;
    ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
    r.block({ Rig::on(0, 60, 100) }, 256);
    auto w = r.block({ Rig::on(0, 64, 100) }, 256);   // second note while the first is held
    const auto* hi = last(w, 0xFF14);
    REQUIRE(hi != nullptr);
    CHECK((hi->value & 0x80) == 0);            // legato: no trigger bit
    CHECK_FALSE(has(w, 0xFF12));               // and no envelope rewrite
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
    CHECK(last(w, 0xFF1E)->value & 0x80);      // trigger
    // The RAM writes precede the DAC-on, and the trigger comes last.
    uint64_t lastRam = 0, dacOn = 0, trig = 0;
    for (auto& x : w) { if (x.addr >= 0xFF30 && x.addr <= 0xFF3F) lastRam = std::max(lastRam, x.cycle); if (x.addr == 0xFF1A && x.value == 0x80) dacOn = x.cycle; if (x.addr == 0xFF1E) trig = x.cycle; }
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
        ChannelParams p; p.instrument = 1; p.cmd[0] = { Cmd::E, 10, 3, 0 }; r.drv.setParams(0, p);
        auto w = r.block({ Rig::on(0, 69, 127) }, 512);
        CHECK(last(w, 0xFF12)->value == 0xA3);          // vol 10, down, rate 3
        p.cmd[0] = { Cmd::E, 10, 11, 0 };               // y >= 8: rising
        r.drv.setParams(0, p);
        w = r.block({}, 512);
        CHECK(last(w, 0xFF12)->value == 0xAB);
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
    SECTION("P is a signed period offset around 128") {
        Rig r;
        ChannelParams p; p.instrument = 1; p.cmd[0] = { Cmd::P, 128 + 10, 0, 0 }; r.drv.setParams(0, p);
        r.block({ Rig::on(0, 69, 100) }, 512);
        CHECK(r.drv.view(0).period == 1760);            // A4 is 1750
        CHECK(r.drv.view(0).pitchOffset == 10);
        p.cmd[0] = { Cmd::P, 128 - 10, 0, 0 };
        r.drv.setParams(0, p);
        r.block({}, 512);
        CHECK(r.drv.view(0).period == 1740);
    }
    SECTION("S is PU1's sweep, down when x asks for it") {
        Rig r;
        ChannelParams p; p.instrument = 1; p.cmd[0] = { Cmd::S, 3, 2, 0 }; r.drv.setParams(0, p);
        auto w = r.block({ Rig::on(0, 69, 100) }, 512);
        CHECK(last(w, 0xFF10)->value == 0x32);          // rate 3, up, shift 2
        p.cmd[0] = { Cmd::S, 128 + 3, 2, 0 };
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
    CHECK(last(w, 0xFF12)->value == 0xF0);                         // velocity 127

    // Changing a slot fires it at the next tick, with no note involved.
    p.cmd[1] = { Cmd::E, 4, 0, 0 };
    r.drv.setParams(0, p);
    w = r.block({}, 512);
    REQUIRE(last(w, 0xFF12) != nullptr);
    CHECK(last(w, 0xFF12)->value == 0x40);
    CHECK(r.drv.view(0).envVol == 4);
    // Unchanged, it does not fire again.
    w = r.block({}, 512);
    CHECK_FALSE(has(w, 0xFF12));

    // It applies to the next note too, after the instrument has loaded.
    r.block({ Rig::off(0, 69) }, 512);
    w = r.block({ Rig::on(0, 69, 127) }, 512);
    CHECK(last(w, 0xFF12)->value == 0x40);                         // not the velocity's 0xF0

    // None reverts to the instrument's envelope.
    p.cmd[1] = {};
    r.drv.setParams(0, p);
    w = r.block({}, 512);
    REQUIRE(last(w, 0xFF12) != nullptr);
    CHECK(last(w, 0xFF12)->value == 0xD0);                         // the instrument's vol 13
    CHECK(r.drv.slot(0, 1).cmd == Cmd::None);
}

TEST_CASE("Z randomises the other slot at every note-on", "[driver][commands]")
{
    Rig r;
    ChannelParams p; p.instrument = 1;
    p.cmd[0] = { Cmd::E, 15, 0, 0 };
    p.cmd[1] = { Cmd::Z, 15, 0, 0 };
    p.velocityMode = 2;                                            // velocity out of the way
    r.drv.setParams(0, p);
    std::vector<int> volumes;
    for (int i = 0; i < 16; ++i) {
        auto w = r.block({ Rig::on(0, 69, 100) }, 512);
        if (const auto* nr2 = last(w, 0xFF12)) volumes.push_back(nr2->value >> 4);
        r.block({ Rig::off(0, 69) }, 512);
    }
    REQUIRE(volumes.size() >= 8);
    std::sort(volumes.begin(), volumes.end());
    CHECK(volumes.front() >= 0);
    CHECK(volumes.back() <= 15);
    CHECK(volumes.front() != volumes.back());                      // it really is random
    // Without Z the same note gives the same volume every time.
    Rig s;
    ChannelParams q; q.instrument = 1; q.cmd[0] = { Cmd::E, 15, 0, 0 }; q.velocityMode = 2;
    s.drv.setParams(0, q);
    s.block({ Rig::on(0, 69, 100) }, 512);
    CHECK(s.drv.view(0).envVol == 15);
}

TEST_CASE("notes on ticks wait for the tick; off, they are sample accurate", "[driver][commands]")
{
    // One tick, at frame 300 of the block.
    const std::vector<TickPoint> ticks { { 300, 0 } };
    // The note's burst starts at the cycle the note was applied at; the
    // writes that follow it are spaced like a CPU writing them.
    auto burstStart = [](const std::vector<RegWrite>& w) { return w.empty() ? uint64_t(0) : w.front().cycle; };
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
