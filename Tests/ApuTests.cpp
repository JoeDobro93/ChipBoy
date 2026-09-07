// ChipBoy -- APU core unit tests.
//
// These pin the behaviours the plugin depends on directly: register masks,
// pulse/wave/noise timing, envelope and length timing, the frame sequencer's
// relation to the divider, and that the event stream is independent of how
// runTo() is chunked (the renderer calls it once per audio block).
// blargg's ROMs (BlarggTests.cpp) are the acceptance gate for the quirks.
#include "core/Apu/Apu.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ios>
#include <vector>

using chipboy::Apu;
using chipboy::ApuEvent;
using chipboy::kCpuHz;

namespace {

constexpr uint16_t NR10 = 0xFF10, NR11 = 0xFF11, NR12 = 0xFF12, NR13 = 0xFF13, NR14 = 0xFF14;
constexpr uint16_t NR21 = 0xFF16, NR22 = 0xFF17, NR23 = 0xFF18, NR24 = 0xFF19;
constexpr uint16_t NR30 = 0xFF1A, NR31 = 0xFF1B, NR32 = 0xFF1C, NR33 = 0xFF1D, NR34 = 0xFF1E;
constexpr uint16_t NR41 = 0xFF20, NR42 = 0xFF21, NR43 = 0xFF22, NR44 = 0xFF23;
constexpr uint16_t NR50 = 0xFF24, NR51 = 0xFF25, NR52 = 0xFF26;

std::vector<ApuEvent> channelEvents(Apu& apu, int ch, uint64_t after = 0)
{
    std::vector<ApuEvent> out;
    for (const auto& e : apu.events())
        if (e.channel == ch && e.cycle > after) out.push_back(e);
    return out;
}

void setFreq(Apu& apu, uint16_t lo, uint16_t hi, uint16_t f, uint8_t hiBits)
{
    apu.write(lo, uint8_t(f & 0xFF));
    apu.write(hi, uint8_t(hiBits | (f >> 8)));
}

} // namespace

TEST_CASE("reset leaves the post-boot state", "[apu]")
{
    Apu apu;
    CHECK(apu.powered());
    CHECK(apu.read(NR50) == 0x77);
    CHECK(apu.read(NR51) == 0xF3);
    CHECK((apu.read(NR52) & 0x80) == 0x80);
    CHECK(apu.read(NR12) == 0xF3);
    CHECK(apu.dacOn(0));          // NR12 = $F3 leaves CH1's DAC on
    CHECK_FALSE(apu.dacOn(2));
    CHECK(apu.cycle() == 0);
    // One initial event per channel so consumers know the starting levels.
    REQUIRE(apu.events().size() == 4);
    for (int c = 0; c < 4; ++c) {
        CHECK(apu.events()[c].cycle == 0);
        CHECK(apu.events()[c].channel == c);
        CHECK(apu.events()[c].level == 0);
    }
}

TEST_CASE("register read masks", "[apu]")
{
    // Reference section 2: bits that always read as 1. Unused registers and
    // everything past NR52 read $FF.
    static constexpr uint8_t mask[0x16] = {
        0x80, 0x3F, 0x00, 0xFF, 0xBF,
        0xFF, 0x3F, 0x00, 0xFF, 0xBF,
        0x7F, 0xFF, 0x9F, 0xFF, 0xBF,
        0xFF, 0xFF, 0x00, 0x00, 0xBF,
        0x00, 0x00};
    Apu apu;
    for (int i = 0; i < 0x16; ++i) {
        const uint16_t addr = uint16_t(0xFF10 + i);
        apu.write(addr, 0x00);
        INFO("register FF" << std::hex << (0x10 + i) << " after writing 00");
        CHECK(apu.read(addr) == mask[i]);
    }
    for (int i = 0; i < 0x16; ++i) {
        const uint16_t addr = uint16_t(0xFF10 + i);
        apu.write(addr, 0xFF);
        INFO("register FF" << std::hex << (0x10 + i) << " after writing FF");
        CHECK(apu.read(addr) == 0xFF);
    }
    for (uint16_t addr = 0xFF27; addr <= 0xFF2F; ++addr) CHECK(apu.read(addr) == 0xFF);

    // NR52: bit 7 is power, bits 0-3 are the channel flags, 4-6 read as 1.
    CHECK((apu.read(NR52) & 0x70) == 0x70);
    apu.write(NR52, 0x00);
    CHECK(apu.read(NR52) == 0x70);
    CHECK_FALSE(apu.powered());
    // Powered off, every register reads as its mask (contents zeroed)...
    for (int i = 0; i < 0x16; ++i) CHECK(apu.read(uint16_t(0xFF10 + i)) == mask[i]);
    // ...and writes are ignored.
    apu.write(NR50, 0x77);
    CHECK(apu.read(NR50) == 0x00);
    apu.write(NR52, 0x80);
    CHECK(apu.read(NR52) == 0xF0);
    CHECK(apu.read(NR50) == 0x00);
}

TEST_CASE("wave RAM is readable and writable while the channel is off", "[apu]")
{
    Apu apu;
    for (int i = 0; i < 16; ++i) apu.write(uint16_t(0xFF30 + i), uint8_t(i * 0x11));
    for (int i = 0; i < 16; ++i) CHECK(apu.read(uint16_t(0xFF30 + i)) == uint8_t(i * 0x11));
    // Power off: wave RAM survives on a DMG (reference section 10.1).
    apu.write(NR52, 0x00);
    apu.write(NR52, 0x80);
    for (int i = 0; i < 16; ++i) CHECK(apu.read(uint16_t(0xFF30 + i)) == uint8_t(i * 0x11));
}

TEST_CASE("pulse period and duty", "[apu]")
{
    // f = 1750: 131072 / (2048 - 1750) = 439.8 Hz; one duty step is
    // (2048 - 1750) * 4 = 1192 cycles, one waveform 9536.
    Apu apu;
    apu.runTo(1000);
    apu.write(NR12, 0xF0);                       // volume 15, no envelope
    apu.write(NR11, 0x80);                       // duty 2 (50%)
    setFreq(apu, NR13, NR14, 1750, 0x80);        // trigger
    apu.runTo(1000 + 9536 * 4);

    auto ev = channelEvents(apu, 0, 1000);
    REQUIRE(ev.size() >= 8);
    // Duty 2 is 10000111: high for one step, low for four, high for three.
    CHECK(apu.events().back().dacOn);
    CHECK(ev[0].cycle == 1000 + 1192);           // falls after step 0
    CHECK(ev[0].level == 0);
    CHECK(ev[1].cycle == 1000 + 5 * 1192);       // rises at step 5
    CHECK(ev[1].level == 15);
    CHECK(ev[2].cycle == ev[0].cycle + 9536);
    CHECK(ev[3].cycle == ev[1].cycle + 9536);

    // Duty 0 (00000001): high for one step in eight.
    apu.write(NR21, 0x00);
    apu.write(NR22, 0xA0);                       // volume 10
    setFreq(apu, NR23, NR24, 2017, 0x80);        // step = 124 cycles
    const uint64_t t0 = apu.cycle();
    apu.runTo(t0 + 992 * 3);
    ev = channelEvents(apu, 1, t0);
    REQUIRE(ev.size() >= 4);
    CHECK(ev[0].level == 10);
    CHECK(ev[1].level == 0);
    CHECK(ev[1].cycle - ev[0].cycle == 124);     // high for exactly one step
    CHECK(ev[2].cycle - ev[0].cycle == 992);     // one waveform later
}

TEST_CASE("runTo chunking does not change the event stream", "[apu]")
{
    // The plugin advances the APU once per audio block; a 1-cycle step and a
    // 1-block step must produce identical events (spec section 5.4).
    auto scenario = [](Apu& apu, uint32_t chunk) {
        apu.write(NR10, 0x2A);                   // sweep: period 2, negate, shift 2
        apu.write(NR11, 0x7C);                   // duty 1, length 60 -> 4 left
        apu.write(NR12, 0xF2);
        setFreq(apu, NR13, NR14, 1000, 0xC0);    // trigger, length on
        apu.write(NR21, 0xC1);
        apu.write(NR22, 0x1B);                   // up envelope, period 3
        setFreq(apu, NR23, NR24, 1900, 0x80);
        for (int i = 0; i < 16; ++i) apu.write(uint16_t(0xFF30 + i), uint8_t(0x0F + i * 0x10));
        apu.write(NR30, 0x80);
        apu.write(NR31, 0xF0);
        apu.write(NR32, 0x40);
        setFreq(apu, NR33, NR34, 1500, 0xC0);
        apu.write(NR42, 0xF3);
        apu.write(NR43, 0x41);                   // shift 4, 15-bit, divisor 1
        apu.write(NR44, 0x80);
        const uint64_t end = kCpuHz / 2;
        while (apu.cycle() < end) apu.runTo(std::min<uint64_t>(end, apu.cycle() + chunk));
        apu.write(NR52, 0x00);                   // power cycle on the way out
        apu.write(NR52, 0x80);
        apu.write(NR12, 0xF0);
        setFreq(apu, NR13, NR14, 1800, 0x80);
        apu.runTo(end + 20000);
    };
    Apu a, b, c;
    scenario(a, 1);
    scenario(b, 7);
    scenario(c, 1u << 20);
    REQUIRE(a.events().size() > 100);
    // Golden: a 64-bit FNV-1a of the event stream, integer data, so it is
    // identical on every platform. A change here is a timing change in the
    // core; if deliberate, update the constant and say so in CHANGES.md.
    {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](uint64_t v) { for (int i = 0; i < 8; ++i) { h ^= (v >> (8 * i)) & 0xFF; h *= 1099511628211ull; } };
        for (const auto& e : a.events()) { mix(e.cycle); mix(uint64_t(e.channel) | (uint64_t(e.level) << 8) | (uint64_t(e.dacOn) << 16)); }
        for (const auto& e : a.mixEvents()) { mix(e.cycle); mix(uint64_t(e.nr50) | (uint64_t(e.nr51) << 8) | (uint64_t(e.powered) << 16)); }
        INFO("event stream hash 0x" << std::hex << h << " over " << std::dec << a.events().size() << " + " << a.mixEvents().size() << " events");
        CHECK(h == 0xd25bba1dc809b549ull);
    }
    REQUIRE(a.events().size() == b.events().size());
    REQUIRE(a.events().size() == c.events().size());
    for (size_t i = 0; i < a.events().size(); ++i) {
        INFO("event " << i);
        CHECK(a.events()[i].cycle == b.events()[i].cycle);
        CHECK(a.events()[i].cycle == c.events()[i].cycle);
        CHECK(a.events()[i].channel == b.events()[i].channel);
        CHECK(a.events()[i].level == b.events()[i].level);
        CHECK(a.events()[i].level == c.events()[i].level);
        CHECK(a.events()[i].dacOn == c.events()[i].dacOn);
    }
}

TEST_CASE("envelope steps every period/64 s", "[apu]")
{
    Apu apu;
    apu.write(NR12, 0xF1);                       // 15, decreasing, period 1
    apu.write(NR11, 0x80);
    setFreq(apu, NR13, NR14, 2017, 0x80);        // 992-cycle waveform
    apu.runTo(kCpuHz);

    // The high level of the pulse tracks the volume: 15, 14, ..., 1, then 0.
    std::vector<uint64_t> firstSeen(16, 0);
    std::vector<bool> seen(16, false);
    for (const auto& e : channelEvents(apu, 0))
        if (e.level > 0 && !seen[e.level]) { seen[e.level] = true; firstSeen[e.level] = e.cycle; }
    for (int v = 1; v <= 15; ++v) { INFO("level " << v); CHECK(seen[v]); }
    for (int v = 14; v >= 2; --v) {
        const int64_t d = int64_t(firstSeen[v - 1]) - int64_t(firstSeen[v]);
        INFO("level " << v << " -> " << v - 1);
        CHECK(d >= 65536 - 992);
        CHECK(d <= 65536 + 992);
    }
    // At volume 0 the channel is still enabled (NR52 bit 0) but silent.
    CHECK((apu.read(NR52) & 1) == 1);
    CHECK(apu.level(0) == 0);
}

TEST_CASE("length counter disables the channel", "[apu]")
{
    Apu apu;
    apu.write(NR22, 0xF0);
    apu.write(NR21, 0x80 | (64 - 2));            // 2 length clocks
    setFreq(apu, NR23, NR24, 1750, 0xC0);        // trigger with length enabled
    CHECK((apu.read(NR52) & 2) == 2);
    apu.runTo(3 * 16384);                        // >= 2 clocks at 256 Hz, any phase
    CHECK((apu.read(NR52) & 2) == 0);
    CHECK(apu.level(1) == 0);
    CHECK(apu.dacOn(1));                         // the DAC stays on; only the channel stopped

    // Without the enable bit the counter never stops the channel.
    apu.write(NR21, 0x80 | (64 - 2));
    setFreq(apu, NR23, NR24, 1750, 0x80);
    apu.runTo(apu.cycle() + kCpuHz);
    CHECK((apu.read(NR52) & 2) == 2);
}

TEST_CASE("frame sequencer follows divider bit 12", "[apu]")
{
    Apu apu;
    CHECK(apu.divider() == 0);
    CHECK(apu.frameStep() == 0);
    apu.runTo(8191);
    CHECK(apu.frameStep() == 0);
    apu.runTo(8192);
    CHECK(apu.frameStep() == 1);
    apu.runTo(8192 * 3);
    CHECK(apu.frameStep() == 3);
    // A DIV write with bit 12 set is a falling edge: the sequencer clocks.
    apu.runTo(8192 * 3 + 4096);
    CHECK((apu.divider() & 0x1000) != 0);
    apu.divReset();
    CHECK(apu.divider() == 0);
    CHECK(apu.frameStep() == 4);
    // With bit 12 clear it is not.
    apu.runTo(apu.cycle() + 100);
    apu.divReset();
    CHECK(apu.frameStep() == 4);
    // Power-off/on resets the sequencer to step 0; the divider keeps counting.
    apu.runTo(apu.cycle() + 8192 * 2 + 50);
    apu.write(NR52, 0x00);
    apu.write(NR52, 0x80);
    CHECK(apu.frameStep() == 0);
    CHECK((apu.divider() & 0x1000) == 0);
    apu.runTo(apu.cycle() + 8192);           // one falling edge: step 0 ran
    CHECK(apu.frameStep() == 1);

    // Powering on while bit 12 is set skips the first tick outright
    // (SameSuite div_write_trigger_10), and until then the sequencer counts
    // as being about to run a step that does not clock length.
    apu.write(NR52, 0x00);
    apu.runTo(apu.cycle() + 4096);           // bit 12 now set
    CHECK((apu.divider() & 0x1000) != 0);
    apu.write(NR52, 0x80);
    apu.write(NR22, 0xF0);
    apu.write(NR21, 0x80 | (64 - 1));        // length 1
    apu.write(NR24, 0xC7);                   // trigger + length: extra clock -> 0 -> reload 64 -> 63
    CHECK((apu.read(NR52) & 2) == 2);
    apu.divReset();                          // the skipped tick
    CHECK(apu.frameStep() == 0);
    CHECK((apu.read(NR52) & 2) == 2);
    apu.runTo(apu.cycle() + 8192);           // step 0: length 63 -> 62, still on
    CHECK(apu.frameStep() == 1);
    CHECK((apu.read(NR52) & 2) == 2);
    // And with length 2 the channel dies on the second real length step.
    apu.write(NR52, 0x00);
    apu.runTo(apu.cycle() + ((4096 - (apu.divider() & 0x1FFF)) & 0x1FFF));
    CHECK((apu.divider() & 0x1000) != 0);
    apu.write(NR52, 0x80);
    apu.write(NR22, 0xF0);
    apu.write(NR21, 0x80 | (64 - 2));
    apu.write(NR24, 0xC7);                   // extra clock: 2 -> 1
    apu.divReset();                          // skipped
    CHECK((apu.read(NR52) & 2) == 2);
    apu.runTo(apu.cycle() + 8192);           // step 0: 1 -> 0, off
    CHECK((apu.read(NR52) & 2) == 0);
}

TEST_CASE("wave channel plays the stale buffer first, then the ramp", "[apu]")
{
    Apu apu;
    // Ramp 0..15 twice, one nibble per sample.
    for (int i = 0; i < 16; ++i)
        apu.write(uint16_t(0xFF30 + i), uint8_t((((2 * i) & 15) << 4) | ((2 * i + 1) & 15)));
    apu.write(NR30, 0x80);
    apu.write(NR32, 0x20);                       // 100%
    apu.runTo(5000);
    setFreq(apu, NR33, NR34, 2017, 0x80);        // 62 cycles per sample
    apu.runTo(5000 + 62 * 40 + 36);             // mid-way through sample 40

    const auto ev = channelEvents(apu, 2, 4999);
    // Sample buffer is not refilled on trigger: the first sample is the stale
    // buffer (0 after reset), which is also the level before the trigger, so
    // the first event is the ramp's second entry (1) one period later.
    REQUIRE(ev.size() >= 32);
    CHECK(ev[0].level == 1);
    CHECK(ev[0].cycle >= 5000 + 62);
    CHECK(ev[0].cycle <= 5000 + 62 + 16);        // trigger delay is small
    for (size_t i = 1; i < 31; ++i) {
        INFO("sample " << i);
        CHECK(ev[i].level == (i + 1) % 16);
        CHECK(ev[i].cycle - ev[i - 1].cycle == 62);
    }
    // NR32 volume shifts: 50% halves, 25% quarters, 0 mutes.
    CHECK(apu.level(2) == 8);                    // sample 40 -> nibble 8
    apu.write(NR32, 0x40);
    CHECK(apu.level(2) == 4);
    apu.write(NR32, 0x60);
    CHECK(apu.level(2) == 2);
    apu.write(NR32, 0x00);
    CHECK(apu.level(2) == 0);
    CHECK(apu.channelActive(2));                 // muted, not stopped
}

TEST_CASE("noise LFSR starts all ones and first outputs high on the 15th clock", "[apu]")
{
    Apu apu;
    apu.write(NR42, 0xF0);
    apu.write(NR43, 0x00);                       // shift 0, divisor code 0: 8-cycle period
    apu.runTo(2000);
    apu.write(NR44, 0x80);
    apu.runTo(2000 + 8 * 40);
    const auto ev = channelEvents(apu, 3, 1999);
    REQUIRE_FALSE(ev.empty());
    CHECK(ev[0].level == 15);
    CHECK(ev[0].cycle == 2000 + 15 * 8);
    // 7-bit mode repeats every 127 clocks.
    Apu b;
    b.write(NR42, 0xF0);
    b.write(NR43, 0x08);
    b.write(NR44, 0x80);
    b.runTo(8 * 127 * 3 + 8);
    std::vector<uint8_t> lv(8 * 127 * 3 + 8, 0);
    {
        uint8_t cur = 0;
        size_t k = 0;
        for (const auto& e : channelEvents(b, 3)) {
            for (; k < e.cycle && k < lv.size(); ++k) lv[k] = cur;
            cur = e.level;
        }
        for (; k < lv.size(); ++k) lv[k] = cur;
    }
    for (size_t k = 8 * 127; k < lv.size(); ++k) {
        INFO("cycle " << k);
        REQUIRE(lv[k] == lv[k - 8 * 127]);
    }
}

TEST_CASE("DAC off is reported and the channel is stopped", "[apu]")
{
    Apu apu;
    apu.write(NR22, 0xF0);
    setFreq(apu, NR23, NR24, 1750, 0x80);
    CHECK(apu.channelActive(1));
    apu.write(NR22, 0x07);                       // top 5 bits clear: DAC off
    CHECK_FALSE(apu.dacOn(1));
    CHECK_FALSE(apu.channelActive(1));
    CHECK_FALSE(apu.events().back().dacOn);
    // A trigger with the DAC off does not enable the channel.
    apu.write(NR24, 0x80);
    CHECK_FALSE(apu.channelActive(1));
    apu.write(NR22, 0x10);                       // DAC on again, volume 1
    CHECK(apu.dacOn(1));
    CHECK_FALSE(apu.channelActive(1));           // ...but not triggered
}
