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

/// A tracker cell as the Player sends it. `inst` 0 is a blank instrument
/// column, which is what makes a cell's note bare.
NoteEvent cellOn(int ch, uint8_t note, uint8_t inst, uint8_t vel = 100, uint32_t off = 0)
{
    NoteEvent e; e.channel = uint8_t(ch); e.kind = NoteEvent::NoteOn; e.source = NoteEvent::Tracker;
    e.a = note; e.b = vel; e.inst = inst; e.offset = off; return e;
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
        CHECK_FALSE(r.drv.noteReport(0).plain);
        CHECK(r.drv.noteReport(0).instrument == 1);
    }
    SECTION("retrig: it starts the instrument again") {
        Rig r;
        r.bank.instruments[0].overlap = Overlap::Retrig;
        ChannelParams p; p.instrument = 1; r.drv.setParams(0, p);
        r.block({ Rig::on(0, 60, 100) }, 256);
        auto w = r.block({ Rig::on(0, 64, 100) }, 256);
        CHECK(anyTrigger(w, 0xFF14));
        CHECK(has(w, 0xFF12));
        CHECK(r.drv.noteReport(0).plain);
    }
    SECTION("a different instrument is always plain") {
        Rig r;
        ChannelParams p; p.instrument = 1; p.velocityMode = 1; r.drv.setParams(0, p);   // velocity bank
        r.block({ Rig::on(0, 60, 8) }, 256);        // slot 1 + 1
        auto w = r.block({ Rig::on(0, 64, 16) }, 256);   // slot 1 + 2: another instrument
        CHECK(anyTrigger(w, 0xFF14));
        CHECK(r.drv.noteReport(0).plain);
        CHECK(r.drv.noteReport(0).instrument == 3);
    }
    SECTION("a keyswitch since the last note is always plain") {
        Rig r;
        ChannelParams p; p.instrument = 1; p.keyswitch = true; r.drv.setParams(0, p);
        r.block({ Rig::on(0, 60, 100) }, 256);
        r.block({ Rig::on(0, 26, 100) }, 256);      // D1 selects slot 3, and never sounds
        auto w = r.block({ Rig::on(0, 64, 100) }, 256);
        CHECK(anyTrigger(w, 0xFF14));
        CHECK(r.drv.noteReport(0).plain);
        CHECK(r.drv.noteReport(0).instrument == 3);
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
    CHECK_FALSE(r.drv.noteReport(0).plain);
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
    SECTION("P with the pitch speed at Step is an immediate offset") {
        Rig r;
        r.bank.instruments[0].pitchSpeed = PitchSpeed::Step;
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
        if (const auto* nr2 = last(w, 0xFF12)) { CHECK((nr2->value >> 4) == 8); lows.push_back(nr2->value & 15); }
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

/* ------------------------------------------------------- pitch (section 7) */

TEST_CASE("V rides the pitch clock: 720/x updates a cycle, depth in semitones", "[driver][pitch]")
{
    Rig r;
    r.tickHz = 20.0;                                     // the pitch clock does this, not the tick
    r.bank.instruments[0].vib = { VibShape::Triangle, VibDir::Down, 8, 5, 0 };   // 4 Hz, one semitone
    ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
    r.block({ Rig::on(0, 69, 100) }, 480);
    const auto series = periodSeries(r, 0, 100, 480);     // a second, sampled every 10 ms
    CHECK(maxOf(series) == note(69));                    // the note itself, at the top of the swing
    CHECK(std::abs(minOf(series) - note(68)) <= 1);      // a semitone below it
    CHECK(dips(series) == 4);                            // speed 8 is four cycles a second

    // The depth is LSDj's table, not a period offset: index 11 is four semitones.
    Rig s;
    s.tickHz = 20.0;
    s.bank.instruments[0].vib = { VibShape::Triangle, VibDir::Down, 8, 11, 0 };
    s.drv.setParams(0, p);
    s.block({ Rig::on(0, 69, 100) }, 480);
    const auto deep = periodSeries(s, 0, 100, 480);
    CHECK(std::abs(minOf(deep) - note(65)) <= 3);
    CHECK(dips(deep) == 4);

    // Up swings the other way, and a square shape sits at one end or the other.
    Rig u;
    u.tickHz = 20.0;
    u.bank.instruments[0].vib = { VibShape::Square, VibDir::Up, 8, 5, 0 };
    u.drv.setParams(0, p);
    u.block({ Rig::on(0, 69, 100) }, 480);
    const auto up = periodSeries(u, 0, 100, 480);
    CHECK(minOf(up) == note(69));
    CHECK(std::abs(maxOf(up) - note(70)) <= 1);
    for (int x : up) CHECK((std::abs(x - note(69)) <= 1 || std::abs(x - note(70)) <= 1));   // no ramp between
}

TEST_CASE("V in Tick mode follows the tempo, and the command rate slows it", "[driver][pitch]")
{
    auto rig = [](uint8_t rate) {
        auto r = std::make_unique<Rig>();
        r->tickHz = 100.0;                               // one tick per 480-frame block
        auto& i = r->bank.instruments[0];
        i.pitchSpeed = PitchSpeed::Tick; i.cmdRate = rate;
        i.vib = { VibShape::Triangle, VibDir::Down, 8, 5, 0 };    // 96 / 8 = twelve ticks a cycle
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; r->drv.setParams(0, p);
        r->block({ Rig::on(0, 69, 100) }, 480);
        return r;
    };
    auto plain = rig(0);
    CHECK(dips(periodSeries(*plain, 0, 48, 480)) == 4);   // 48 ticks, twelve to a cycle
    auto slowed = rig(1);
    CHECK(dips(periodSeries(*slowed, 0, 48, 480)) == 2);  // one advance every two ticks
}

TEST_CASE("L slides for its duration, from wherever the channel is", "[driver][pitch]")
{
    const int a4 = note(69), c4 = note(60);
    // The lead's own vibrato would ride on top of every period below.
    SECTION("Fast: the duration is in 1/360 s") {
        Rig r;
        r.tickHz = 20.0;
        r.bank.instruments[0].vib.depth = 0;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; p.cmd[0] = { Cmd::L, 180, 0, 0 };
        r.drv.setParams(0, p);
        r.block({ Rig::on(0, 69, 100) }, 480);
        CHECK(r.drv.view(0).period == a4);               // the first note has nothing to slide from
        r.block({ Rig::off(0, 69) }, 480);
        r.block({ Rig::on(0, 60, 100) }, 240);
        CHECK(std::abs(int(r.drv.view(0).period) - a4) <= 4);    // it starts at the note before
        r.block({}, 11760);                              // half the half-second
        CHECK(std::abs(int(r.drv.view(0).period) - (c4 + (a4 - c4) / 2)) <= 4);
        r.block({}, 14000);                              // and it arrives
        CHECK(r.drv.view(0).period == c4);
    }
    SECTION("L 0 arrives at once") {
        Rig r;
        r.tickHz = 20.0;
        r.bank.instruments[0].vib.depth = 0;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; p.cmd[0] = { Cmd::L, 0, 0, 0 };
        r.drv.setParams(0, p);
        r.block({ Rig::on(0, 69, 100) }, 480);
        r.block({ Rig::off(0, 69) }, 480);
        r.block({ Rig::on(0, 60, 100) }, 240);
        CHECK(r.drv.view(0).period == c4);
    }
    SECTION("Tick: the duration is in ticks") {
        Rig r;
        r.tickHz = 100.0;                                // a tick per 480-frame block
        r.bank.instruments[0].pitchSpeed = PitchSpeed::Tick;
        r.bank.instruments[0].vib.depth = 0;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; p.cmd[0] = { Cmd::L, 20, 0, 0 };
        r.drv.setParams(0, p);
        r.block({ Rig::on(0, 69, 100) }, 480);
        r.block({ Rig::off(0, 69) }, 480);
        r.block({ Rig::on(0, 60, 100) }, 480);
        r.block({}, 480 * 9);                            // ten of the twenty ticks
        CHECK(std::abs(int(r.drv.view(0).period) - (c4 + (a4 - c4) / 2)) <= 12);
        r.block({}, 480 * 12);
        CHECK(r.drv.view(0).period == c4);
    }
    SECTION("Drum: linear in semitones, not in register units") {
        Rig r;
        r.tickHz = 20.0;
        r.bank.instruments[0].pitchSpeed = PitchSpeed::Drum;
        r.bank.instruments[0].vib.depth = 0;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; p.cmd[0] = { Cmd::L, 180, 0, 0 };
        r.drv.setParams(0, p);
        r.block({ Rig::on(0, 69, 100) }, 480);
        r.block({ Rig::off(0, 69) }, 480);
        r.block({ Rig::on(0, 45, 100) }, 240);           // two octaves down
        r.block({}, 11760);
        const int p2 = int(r.drv.view(0).period);
        const int halfSemis = note(57);                  // half the drop in semitones
        const int halfUnits = (a4 + note(45)) / 2;       // half of it in register units
        CHECK(std::abs(p2 - halfSemis) < std::abs(p2 - halfUnits));
        CHECK(std::abs(p2 - halfSemis) <= 12);
        r.block({}, 14000);
        CHECK(r.drv.view(0).period == note(45));
    }
}

TEST_CASE("P bends at the instrument's pitch speed", "[driver][pitch]")
{
    SECTION("Fast: x - 128 register units per update, and 128 stops it") {
        Rig r;
        r.tickHz = 20.0;
        r.bank.instruments[0].vib.depth = 0;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; p.cmd[0] = { Cmd::P, 129, 0, 0 };
        r.drv.setParams(0, p);
        r.block({ Rig::on(0, 69, 100) }, 240);
        r.block({}, 4800);                               // about 37 updates at 360 Hz
        const int off = r.drv.view(0).pitchOffset;
        CHECK(off >= 35); CHECK(off <= 39);
        CHECK(r.drv.view(0).period == note(69) + off);
        // P 128 stops the bend and keeps what it reached.
        p.cmd[0] = { Cmd::P, 128, 0, 0 }; r.drv.setParams(0, p);
        r.block({}, 4800);
        const int stopped = r.drv.view(0).pitchOffset;
        r.block({}, 4800);
        CHECK(r.drv.view(0).pitchOffset == stopped);
        // A bare note leaves it where it is (section 8).
        r.block({ Rig::on(0, 72, 100) }, 480);
        CHECK(r.drv.view(0).pitchOffset == stopped);
        CHECK_FALSE(r.drv.noteReport(0).plain);
        r.block({ Rig::off(0, 72) }, 480);
        // A plain note-on puts the offset back to zero.
        r.block({ Rig::off(0, 69) }, 480);
        r.block({ Rig::on(0, 69, 100) }, 480);
        CHECK(r.drv.view(0).pitchOffset == 0);
        CHECK(r.drv.view(0).period == note(69));
    }
    SECTION("Tick: a unit a tick, and the command rate halves it") {
        auto run = [](uint8_t rate) {
            auto r = std::make_unique<Rig>();
            r->tickHz = 100.0;
            auto& i = r->bank.instruments[0];
            i.pitchSpeed = PitchSpeed::Tick; i.cmdRate = rate; i.vib.depth = 0;
            ChannelParams p; p.instrument = 1; p.velocityMode = 2; p.cmd[0] = { Cmd::P, 129, 0, 0 };
            r->drv.setParams(0, p);
            r->block({ Rig::on(0, 69, 100) }, 480);      // the note's own tick counts
            r->block({}, 480 * 9);
            return int(r->drv.view(0).pitchOffset);
        };
        CHECK(run(0) == 10);
        CHECK(run(1) == 5);
    }
    SECTION("Drum: (x - 128) / 16 semitones an update") {
        Rig r;
        r.tickHz = 20.0;
        r.bank.instruments[0].pitchSpeed = PitchSpeed::Drum;
        r.bank.instruments[0].vib.depth = 0;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; p.cmd[0] = { Cmd::P, 126, 0, 0 };
        r.drv.setParams(0, p);
        r.block({ Rig::on(0, 69, 100) }, 240);
        r.block({}, 4060);                               // 32 updates: an eighth of a semitone each
        CHECK(std::abs(int(r.drv.view(0).period) - note(65)) <= 6);
        // The running state shows what a Drum bend is worth in register units.
        CHECK(std::abs(int(r.drv.view(0).pitchOffset) - (note(65) - note(69))) <= 6);
    }
}

TEST_CASE("the pitch clock restarts at the note, whatever sample it started on", "[driver][pitch]")
{
    auto run = [](uint32_t at) {
        Rig r;
        r.tickHz = 20.0;
        r.bank.instruments[0].vib = { VibShape::Triangle, VibDir::Down, 8, 5, 0 };
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
        r.block({}, 4800);                               // NR50 and the rest are out of the way
        std::vector<RegWrite> all;
        for (int i = 0; i < 5; ++i) {
            std::vector<NoteEvent> ev;
            if (i == 0) ev.push_back(Rig::on(0, 69, 100, at));
            auto w = r.block(ev, 4800);
            all.insert(all.end(), w.begin(), w.end());
        }
        return all;
    };
    const auto a = run(0), b = run(137);
    REQUIRE(a.size() > 20);
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        INFO("write " << i);
        CHECK(a[i].addr == b[i].addr);
        CHECK(a[i].value == b[i].value);
        const int64_t da = int64_t(a[i].cycle - a[0].cycle), db = int64_t(b[i].cycle - b[0].cycle);
        CHECK(std::abs(da - db) <= 2);                   // the same note, the same shape
    }
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
    CHECK(r.drv.noteReport(0).plain);
    CHECK(r.drv.noteReport(0).instrument == 1);
    r.block({}, 24000);                                  // half a second of vibrato
    const int bent = int(r.drv.view(0).period);
    CHECK(bent < note(69) - 20);                         // the vibrato is well away from the note
    w = r.block({ cellOn(0, 69, 0) }, 240);              // the same note, blank column
    CHECK_FALSE(anyTrigger(w, 0xFF14));                  // no trigger
    CHECK_FALSE(has(w, 0xFF12));                         // the envelope is not rewritten
    CHECK(std::abs(int(r.drv.view(0).period) - bent) <= 12);   // the vibrato kept its phase
    CHECK_FALSE(r.drv.noteReport(0).plain);
    CHECK(r.drv.noteReport(0).instrument == 1);
    // A plain cell starts the phase again, so the note is in tune at its start.
    w = r.block({ cellOn(0, 69, 1) }, 240);
    CHECK(anyTrigger(w, 0xFF14));
    CHECK(r.drv.view(0).period == note(69));
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
    CHECK(last(w, 0xFF12)->value == 0x80);               // 64 of 127 is level 8
    w = r.block({ cellOn(0, 71, 1, 127) }, 512);
    CHECK(last(w, 0xFF12)->value == 0xF0);
}

TEST_CASE("Note-off Release lets the sound finish", "[driver][notes]")
{
    SECTION("a held envelope gets a decrease written, without a trigger") {
        Rig r;
        auto& i = r.bank.instruments[0]; i.noteOff = NoteOff::Release; i.envRate = 0;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
        r.block({ Rig::on(0, 69, 100) }, 256);
        auto w = r.block({ Rig::off(0, 69) }, 256);
        REQUIRE(last(w, 0xFF12) != nullptr);
        CHECK((last(w, 0xFF12)->value & 0x07) == 1);     // rate 1
        CHECK((last(w, 0xFF12)->value & 0x08) == 0);     // downward
        CHECK((last(w, 0xFF12)->value >> 4) == 13);      // at the volume it had
        CHECK_FALSE(anyTrigger(w, 0xFF14));
        CHECK_FALSE(r.drv.view(0).active);
        CHECK(r.drv.view(0).dacOn);
    }
    SECTION("a rising envelope is turned round") {
        Rig r;
        auto& i = r.bank.instruments[0]; i.noteOff = NoteOff::Release; i.envDir = EnvDir::Up; i.envRate = 4;
        ChannelParams p; p.instrument = 1; p.velocityMode = 2; r.drv.setParams(0, p);
        r.block({ Rig::on(0, 69, 100) }, 256);
        auto w = r.block({ Rig::off(0, 69) }, 256);
        REQUIRE(last(w, 0xFF12) != nullptr);
        CHECK((last(w, 0xFF12)->value & 0x0F) == 1);     // down, rate 1
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

TEST_CASE("C arpeggiates 0, x, y and the command rate slows it", "[driver][commands]")
{
    auto series = [](uint8_t rate, int16_t x, int16_t y, int n) {
        Rig r;
        r.tickHz = 100.0;
        r.bank.instruments[0].cmdRate = rate; r.bank.instruments[0].vib.depth = 0;
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
    // One step every rate + 1 ticks.
    const auto slow = series(1, 3, 7, 12);
    for (size_t i = 0; i + 6 < slow.size(); ++i) CHECK(slow[i] == slow[i + 6]);
    CHECK(slow[0] == slow[1]);                           // two ticks to a step
    CHECK(slow[2] == slow[3]);
    CHECK(slow[1] != slow[2]);
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
        for (int i = 0; i < 8; ++i) { auto w = r.block({}, 480); if (const auto* n = last(w, 0xFF12)) out.push_back(n->value >> 4); }
        return out;
    };
    const auto up = volumes(2);                          // 1-7: up by that much
    REQUIRE(up.size() >= 3);
    CHECK(up[0] == 10); CHECK(up[1] == 12); CHECK(up[2] == 14);
    const auto down = volumes(10);                       // 9-15: down by x - 8
    REQUIRE(down.size() >= 3);
    CHECK(down[0] == 6); CHECK(down[1] == 4); CHECK(down[2] == 2);
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
    const uint8_t ticks[16] = { 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    r.drv.setTableGroove(0, ticks);                      // what the Player hands over
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
