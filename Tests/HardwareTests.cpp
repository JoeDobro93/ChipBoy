// ChipBoy -- M8: the CGB chip variant, RAW output and the Hardware-panel
// options (spec sections 6.5, 12.3; UI_DESIGN section 5).
#include "core/Apu/Apu.h"
#include "core/Bank/Bank.h"
#include "core/Driver/Driver.h"
#include "core/Render/Renderer.h"
#include "core/Tracker/Song.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace chipboy;

namespace {

constexpr uint16_t NR30 = 0xFF1A, NR32 = 0xFF1C, NR33 = 0xFF1D, NR34 = 0xFF1E;
constexpr uint16_t NR11 = 0xFF11, NR12 = 0xFF12, NR13 = 0xFF13, NR14 = 0xFF14;
constexpr uint16_t NR50 = 0xFF24, NR51 = 0xFF25;

void loadWave(Apu& apu, uint8_t seed)
{
    for (int i = 0; i < 16; ++i) apu.write(uint16_t(0xFF30 + i), uint8_t(seed + i * 7));
}
std::vector<uint8_t> readWave(Apu& apu)
{
    std::vector<uint8_t> v;
    for (int i = 0; i < 16; ++i) v.push_back(apu.read(uint16_t(0xFF30 + i)));
    return v;
}
std::vector<uint8_t> expectedWave(uint8_t seed)
{
    std::vector<uint8_t> v;
    for (int i = 0; i < 16; ++i) v.push_back(uint8_t(seed + i * 7));
    return v;
}

/// Render `frames` of a fixed-rate script through a renderer set up by `setup`.
std::vector<float> renderWith(const std::function<void(render::Renderer&)>& setup,
                              const std::function<void(Apu&, double)>& script, double seconds)
{
    const double fs = 48000.0;
    render::Renderer r;
    r.prepare(fs, AnalogModel::dmg(), 4096);
    setup(r);
    Apu apu;
    const uint64_t frames = uint64_t(seconds * fs);
    std::vector<float> L(frames), R(frames);
    const int block = 480;
    for (uint64_t f = 0; f < frames; f += block) {
        const int n = int(std::min<uint64_t>(block, frames - f));
        // the script gets a chance at every block start, with the time in seconds
        apu.runTo(std::max(r.cycleForFrame(f), apu.cycle()));
        script(apu, double(f) / fs);
        apu.runTo(std::max(r.cycleForFrame(f + uint64_t(n)), apu.cycle()));
        r.render(apu, L.data() + f, R.data() + f, n);
    }
    return L;
}

} // namespace

TEST_CASE("a CGB reads and writes wave RAM while the channel plays; a DMG does not", "[hardware][apu]")
{
    for (Console model : { Console::DMG, Console::CGB }) {
        Apu apu;
        apu.setModel(model);
        loadWave(apu, 3);
        apu.write(NR30, 0x80); apu.write(NR32, 0x20); apu.write(NR33, 0x00); apu.write(NR34, 0x87);
        apu.runTo(apu.cycle() + 40001);   // well away from any fetch window
        INFO((model == Console::CGB ? "CGB" : "DMG"));
        const auto live = readWave(apu);
        const auto pattern = expectedWave(3);
        if (model == Console::CGB) {
            // The CGB serves the byte the channel is playing, whichever address is asked.
            CHECK(std::count(live.begin(), live.end(), live[0]) == 16);
            CHECK(std::find(pattern.begin(), pattern.end(), live[0]) != pattern.end());
        } else {
            CHECK(std::count(live.begin(), live.end(), 0xFF) == 16);
        }
        apu.write(NR30, 0x00);            // DAC off: the RAM itself was never touched
        CHECK(readWave(apu) == pattern);
    }
}

TEST_CASE("retriggering never corrupts wave RAM on a CGB", "[hardware][apu]")
{
    int corruptedOnDmg = 0;
    for (Console model : { Console::DMG, Console::CGB }) {
        for (uint32_t offset = 0; offset < 600; ++offset) {     // more than one 512-cycle sample period
            Apu apu;
            apu.setModel(model);
            loadWave(apu, 9);
            apu.write(NR30, 0x80); apu.write(NR32, 0x20); apu.write(NR33, 0x00); apu.write(NR34, 0x87);
            apu.runTo(apu.cycle() + 3000 + offset);
            apu.write(NR34, 0x87);              // retrigger at an arbitrary phase
            apu.write(NR30, 0x00);              // DAC off: RAM readable on both models
            const bool same = readWave(apu) == expectedWave(9);
            if (model == Console::CGB) { INFO("offset " << offset); CHECK(same); }
            else if (!same) ++corruptedOnDmg;
        }
    }
    CHECK(corruptedOnDmg > 0);                  // the DMG quirk is still there
}

TEST_CASE("the pulse quiet-edge query lands on the low half", "[hardware][apu]")
{
    Apu apu;
    apu.write(NR11, 0x80);                      // duty 2 (50 %)
    apu.write(NR12, 0xF0);
    apu.write(NR13, 0x00); apu.write(NR14, 0x87);   // frequency 0x700
    apu.write(NR51, 0x11); apu.write(NR50, 0x77);
    int checkedHigh = 0, checkedLow = 0;
    for (uint32_t step = 0; step < 200; ++step) {
        apu.runTo(apu.cycle() + 37);
        const uint64_t d = apu.cyclesUntilPulseLow(0);
        if (apu.level(0) == 0) { CHECK(d == 0); ++checkedLow; continue; }
        REQUIRE(d > 0);
        Apu probe = apu;
        probe.runTo(probe.cycle() + d - 1);
        CHECK(probe.level(0) != 0);             // still high one cycle before
        probe.runTo(probe.cycle() + 1);
        CHECK(probe.level(0) == 0);             // low exactly there
        ++checkedHigh;
    }
    CHECK(checkedHigh > 10);
    CHECK(checkedLow > 10);
}

TEST_CASE("RAW output is silence with the DACs off and carries no noise floor", "[hardware][render]")
{
    const auto out = renderWith(
        [](render::Renderer& r) { r.setModel(AnalogModel::dmg(), true); render::Renderer::Options o; o.noise = true; o.lcd = true; r.setOptions(o); },
        [](Apu&, double) {}, 0.25);
    float peak = 0.0f;
    for (float x : out) peak = std::max(peak, std::fabs(x));
    CHECK(peak == 0.0f);
}

TEST_CASE("RAW keeps a square's flat tops; the DMG stage sags them", "[hardware][render]")
{
    auto script = [](Apu& apu, double t) {
        if (t == 0.0) {
            apu.write(NR51, 0x11); apu.write(NR50, 0x77);
            apu.write(NR11, 0x80); apu.write(NR12, 0xF0);
            apu.write(NR13, 0x00); apu.write(NR14, 0x80);   // 64 Hz square
        }
    };
    auto tilt = [](const std::vector<float>& out) {
        // Within the last few high half-cycles (7.8 ms each), how much does the
        // top decay between 1 ms after the edge and 1 ms before the next?
        const size_t half = size_t(48000.0 / 64.0 / 2.0);
        double worst = 0.0;
        for (size_t start = out.size() - 10 * half; start + half < out.size(); ++start) {
            if (out[start] > 0.5f && out[start - 1] <= 0.5f) {           // a rising edge
                const double a = out[start + 48], b = out[start + half - 48];
                worst = std::max(worst, (a - b) / a);
                start += half;
            }
        }
        return worst;
    };
    const auto raw = renderWith([](render::Renderer& r) { r.setModel(AnalogModel::dmg(), true); }, script, 0.5);
    const auto dmg = renderWith([](render::Renderer& r) { r.setModel(AnalogModel::dmg(), false); }, script, 0.5);
    INFO("tilt raw " << tilt(raw) << " dmg " << tilt(dmg));
    CHECK(tilt(raw) < 0.06);
    CHECK(tilt(dmg) > 0.3);                     // 25 Hz coupling on a 64 Hz square
}

TEST_CASE("de-click spreads a DAC-on step", "[hardware][render]")
{
    auto script = [](Apu& apu, double t) {
        if (t == 0.0) { apu.write(NR51, 0x44); apu.write(NR50, 0x77); loadWave(apu, 0); for (int i = 0; i < 16; ++i) apu.write(uint16_t(0xFF30 + i), 0xFF); }
        if (std::fabs(t - 0.1) < 1e-9) { apu.write(NR30, 0x80); apu.write(NR32, 0x20); apu.write(NR33, 0xFF); apu.write(NR34, 0x87); }   // DAC on at 15
    };
    auto maxStep = [](const std::vector<float>& out) {
        float m = 0.0f;
        for (size_t i = size_t(0.1 * 48000) - 100; i < size_t(0.1 * 48000) + 500; ++i) m = std::max(m, std::fabs(out[i] - out[i - 1]));
        return m;
    };
    const auto stock = renderWith([](render::Renderer& r) { render::Renderer::Options o; o.declickMs = 0.0f; r.setOptions(o); }, script, 0.2);
    const auto soft  = renderWith([](render::Renderer& r) { render::Renderer::Options o; o.declickMs = 3.0f; r.setOptions(o); }, script, 0.2);
    INFO("max step stock " << maxStep(stock) << " de-clicked " << maxStep(soft));
    CHECK(maxStep(stock) > 0.2f);
    CHECK(maxStep(soft) < maxStep(stock) * 0.25f);
}

TEST_CASE("the driver asks for the quiet edge, and mute is an NR51 gate", "[hardware][driver]")
{
    using namespace chipboy::driver;
    const auto bank = bank::Bank::factory();
    tracker::Song song;
    Driver d;
    d.prepare(48000.0, &bank, &song, Console::DMG);
    GlobalParams g; g.tick = TickSource::Custom; g.customHz = 240.0; g.volumeAtEdges = true;
    d.setGlobal(g);
    ChannelParams p; p.instrument = 1; d.setParams(0, p);
    auto cycleAt = [](uint64_t f) { return f * uint64_t(kCpuHz) / 48000; };
    std::vector<RegWrite> out;
    Transport t;

    NoteEvent on; on.kind = NoteEvent::NoteOn; on.channel = 0; on.a = 60; on.b = 100;
    d.process(&on, 1, 4800, 0, t, cycleAt, out);
    const bool markedAtStart = std::any_of(out.begin(), out.end(), [](const RegWrite& w) { return w.addr == Driver::kAlignToQuietEdge; });
    CHECK_FALSE(markedAtStart);                 // a fresh note has nothing to pop

    out.clear();
    NoteEvent cc; cc.kind = NoteEvent::Control; cc.channel = 0; cc.a = 7; cc.b = 64;   // level change on a playing note
    d.process(&cc, 1, 4800, 4800, t, cycleAt, out);
    auto marker = std::find_if(out.begin(), out.end(), [](const RegWrite& w) { return w.addr == Driver::kAlignToQuietEdge; });
    REQUIRE(marker != out.end());
    CHECK(marker->value == 0);
    auto nr12 = std::find_if(marker, out.end(), [](const RegWrite& w) { return w.addr == NR12; });
    REQUIRE(nr12 != out.end());
    CHECK(nr12->cycle >= marker->cycle);

    // Mute: the next tick rewrites NR51 without channel 0's bits.
    out.clear();
    d.setGateMask(0b1110);
    d.process(nullptr, 0, 4800, 9600, t, cycleAt, out);
    auto nr51 = std::find_if(out.rbegin(), out.rend(), [](const RegWrite& w) { return w.addr == NR51; });
    REQUIRE(nr51 != out.rend());
    CHECK((nr51->value & 0x11) == 0);
    out.clear();
    d.setGateMask(15);
    d.process(nullptr, 0, 4800, 14400, t, cycleAt, out);
    nr51 = std::find_if(out.rbegin(), out.rend(), [](const RegWrite& w) { return w.addr == NR51; });
    REQUIRE(nr51 != out.rend());
    CHECK((nr51->value & 0x11) == 0x11);
}
