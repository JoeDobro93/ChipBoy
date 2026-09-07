// ChipBoy -- driver tests (spec section 8, 9, 10).
#include "core/Bank/Bank.h"
#include "core/Driver/Driver.h"
#include "core/Apu/Apu.h"
#include "core/Render/Renderer.h"

#include <catch2/catch_test_macros.hpp>

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
    render::Renderer ren;
    std::vector<RegWrite> writes;
    uint64_t frame = 0;
    double rate = 48000.0;
    Rig(Console model = Console::DMG)
    {
        ren.prepare(rate, AnalogModel::forConsole(model), 4096);
        drv.prepare(rate, &bank, &song, model);
        writes.reserve(4096);
        // A fast free-running tick so every small block contains one.
        GlobalParams g; g.tick = TickSource::Custom; g.customHz = 240.0; drv.setGlobal(g);
    }
    /// Run one block of `n` frames with these events; returns the writes.
    std::vector<RegWrite> block(std::vector<NoteEvent> ev, uint32_t n, Transport t = {})
    {
        writes.clear();
        drv.process(ev.data(), ev.size(), n, frame, t, [this](uint64_t f) { return ren.cycleForFrame(f); }, writes);
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
    GlobalParams g; g.tick = TickSource::Custom; g.customHz = 100.0; r.drv.setGlobal(g);
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
        GlobalParams g; g.tick = TickSource::Host; g.ticksPerBeat = 24; r.drv.setGlobal(g);
        std::vector<RegWrite> all;
        const uint32_t total = 48000;
        for (uint32_t f = 0; f < total; f += block) {
            const uint32_t n = std::min(block, total - f);
            std::vector<NoteEvent> ev;
            if (f <= 1000 && f + n > 1000) ev.push_back(Rig::on(0, 62, 90, 1000 - f));
            if (f <= 1000 && f + n > 1000) ev.push_back(Rig::on(3, 36, 120, 1000 - f));
            if (f <= 30000 && f + n > 30000) ev.push_back(Rig::off(0, 62, 30000 - f));
            Transport t; t.valid = true; t.playing = true; t.bpm = 120.0; t.ppq = double(f) / 48000.0 * 2.0;
            auto w = r.block(ev, n, t);
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
