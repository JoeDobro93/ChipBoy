// ChipBoy -- the clock: tick generation and the tracker's position
// (docs/COMMANDS_AND_TEMPO.md section 4).
#include "core/Driver/Clock.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace chipboy;
using namespace chipboy::driver;

namespace {

struct Tick { uint64_t frame; int64_t index; };

/// Section 160: where a tick nominally due at a frame lands -- the frame of
/// the first grid instant after it.
uint64_t onGrid(double nominalFrame) { return gridTickFrame(nominalFrame, 48000.0); }
/// The ROM's tick in frames at a whole-number tempo.
double framesPerTick(double bpm) { return 48000.0 * tickSeconds(bpm, false); }
/// The ROM's tick in frames at a whole-number tempo (`lsdjTempo`).
double romFramesPerTick(double bpm) { return 48000.0 * tickSeconds(bpm, true); }

/// Play `seconds` through the clock in blocks and collect every tick.
std::vector<Tick> run(Clock& c, double seconds, uint32_t block, double bpm, bool song, double rate = 48000.0, double startSeconds = 0.0)
{
    std::vector<Tick> out;
    const uint64_t total = uint64_t(seconds * rate);
    for (uint64_t f = 0; f < total; f += block) {
        const uint32_t n = uint32_t(std::min<uint64_t>(block, total - f));
        Transport t;
        t.valid = true; t.playing = true; t.bpm = bpm;
        t.ppq = double(f) / rate * (bpm / 60.0);
        t.seconds = startSeconds + double(f) / rate; t.timeValid = true;
        c.process(t, n, f);
        (void)song;
        for (size_t k = 0; k < c.tickCount(); ++k) out.push_back({ f + c.ticks()[k].offset, c.ticks()[k].tick });
    }
    return out;
}

} // namespace

TEST_CASE("host ticks are 24 to the beat, straight from the host's ppq", "[clock]")
{
    Clock c; c.prepare(48000.0);
    ClockConfig cfg; cfg.source = TempoSource::Host; c.setConfig(cfg);
    const auto a = run(c, 4.0, 512, 120.0, false);
    // 120 BPM: a beat is 24 000 frames, so a tick is due every 1 000 frames,
    // and lands on the grid instant after it (section 160): 0-2.8 ms later.
    REQUIRE(a.size() == 4 * 2 * 24);
    for (size_t i = 0; i < a.size(); ++i) {
        INFO("tick " << i);
        CHECK(a[i].index == int64_t(i));
        CHECK(a[i].frame == onGrid(double(i * 1000)));
        CHECK(a[i].frame >= i * 1000);
        CHECK(a[i].frame <= i * 1000 + 135);   // 11712 cycles is 134.03 frames
    }
    // The block size cannot move a tick.
    Clock d; d.prepare(48000.0); d.setConfig(cfg);
    const auto b = run(d, 4.0, 97, 120.0, false);
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) { CHECK(a[i].frame == b[i].frame); CHECK(a[i].index == b[i].index); }
    // And another tempo scales it exactly.
    Clock e; e.prepare(48000.0); e.setConfig(cfg);
    const auto slow = run(e, 4.0, 512, 60.0, false);
    REQUIRE(slow.size() == 4 * 24);
    CHECK(slow[1].frame == onGrid(2000.0));
}

TEST_CASE("song position is the integral of the tempo map", "[clock]")
{
    Clock c; c.prepare(48000.0);
    ClockConfig cfg; cfg.source = TempoSource::Song; cfg.songTempo = 120.0; cfg.lsdjTempo = true;
    c.setConfig(cfg);
    // 120 BPM to tick 96 (two seconds, one bar), then 60 BPM from a T cell.
    // An imported song: the tick is the ROM's word (section 160).
    const TempoPoint map[] = { { 0, 120.0 }, { 96, 60.0 } };
    c.setTempoMap(map, 2);

    // The rate is the ROM's word (section 160): 15290/2048 grid steps at 120,
    // 30580/2048 at 60 -- within 0.01 % of 48 and 24 ticks a second, and the
    // integral is exact in those units.
    const double t120 = tickSeconds(120.0, true), t60 = tickSeconds(60.0, true);
    CHECK(std::fabs(t120 * 48.0 - 1.0) < 2e-4);
    CHECK(std::fabs(t60 * 24.0 - 1.0) < 2e-4);
    CHECK(c.ticksAtSeconds(0.0) == 0.0);
    CHECK(std::fabs(c.ticksAtSeconds(1.0) - 1.0 / t120) < 1e-9);       // 120 BPM: 48 ticks a second
    CHECK(std::fabs(c.ticksAtSeconds(96.0 * t120) - 96.0) < 1e-9);
    CHECK(std::fabs(c.ticksAtSeconds(96.0 * t120 + 1.0) - (96.0 + 1.0 / t60)) < 1e-9);   // 60 BPM: 24 a second
    CHECK(std::fabs(c.secondsAtTicks(120.0) - (96.0 * t120 + 24.0 * t60)) < 1e-9);
    CHECK(c.bpmAtTick(95) == 120.0);
    CHECK(c.bpmAtTick(96) == 60.0);

    // Playing through: the ticks are the map's, and the tempo change lands.
    const auto a = run(c, 4.0, 512, 120.0, true);
    REQUIRE(a.size() == 96 + 48);
    CHECK(a[0].index == 0);
    CHECK(a[47].frame == onGrid(47.0 * romFramesPerTick(120.0)));      // still 120 BPM
    CHECK(a[96].index == 96);
    CHECK(a[97].frame == onGrid(96.0 * romFramesPerTick(120.0) + romFramesPerTick(60.0)));   // 60 BPM: a tick every 2 000 frames
    CHECK(a.back().index == int64_t(a.size() - 1));
}

TEST_CASE("a jump lands on the tick playing through would have reached", "[clock]")
{
    ClockConfig cfg; cfg.source = TempoSource::Song; cfg.songTempo = 150.0; cfg.songStartSeconds = 0.5;
    const TempoPoint map[] = { { 0, 150.0 }, { 60, 100.0 } };

    Clock through; through.prepare(48000.0); through.setConfig(cfg); through.setTempoMap(map, 2);
    const auto played = run(through, 6.0, 512, 120.0, true);
    REQUIRE_FALSE(played.empty());

    // The same clock, located straight to 5 s: the position must match the one
    // the play-through was at, tick for tick.
    Clock jumped; jumped.prepare(48000.0); jumped.setConfig(cfg); jumped.setTempoMap(map, 2);
    Transport t; t.valid = true; t.playing = true; t.bpm = 120.0; t.seconds = 5.0; t.timeValid = true; t.ppq = 10.0;
    jumped.process(t, 512, 240000);
    const int64_t at5 = jumped.tickAtBlockStart();
    int64_t playedAt5 = -1;
    // The nominal position: a tick fires up to 2.8 ms after it is due
    // (section 160), so the last one played by 5 s is the one due by then or
    // the one before it.
    for (const auto& k : played) if (k.frame <= 5 * 48000) playedAt5 = k.index;
    CHECK((at5 == playedAt5 || at5 == playedAt5 + 1));
    CHECK(at5 == int64_t(std::floor(jumped.ticksAtSeconds(5.0))));

    // Before the song start there are no song ticks yet.
    Transport early = t; early.seconds = 0.25;
    jumped.process(early, 512, 0);
    CHECK(jumped.tickAtBlockStart() < 0);
}

TEST_CASE("the Song tempo parameter is the base a T cell modifies", "[clock]")
{
    // Section 4: the parameter is the song's base tempo, and T cells move it
    // from their tick. A published song with no T cells therefore runs at the
    // parameter's tempo -- the song does not carry a base of its own into the
    // map.
    Clock c; c.prepare(48000.0);
    ClockConfig cfg; cfg.source = TempoSource::Song; cfg.songTempo = 150.0; cfg.lsdjTempo = true; c.setConfig(cfg);
    c.setTempoMap(nullptr, 0);
    const double t150 = tickSeconds(150.0, true), t100 = tickSeconds(100.0, true);
    CHECK(std::fabs(c.ticksAtSeconds(1.0) - 1.0 / t150) < 1e-9);  // 150 BPM: 60 ticks a second
    CHECK(std::fabs(t150 * 60.0 - 1.0) < 2e-4);
    const auto none = run(c, 2.0, 512, 120.0, true);
    REQUIRE(none.size() == 120);
    CHECK(none[60].frame == onGrid(60.0 * romFramesPerTick(150.0)));

    // A T at bar 9 (tick 768 at four beats a bar) changes it from there.
    Clock d; d.prepare(48000.0); d.setConfig(cfg);
    const TempoPoint bar9[] = { { 768, 100.0 } };
    d.setTempoMap(bar9, 1);
    CHECK(d.bpmAtTick(767) == 150.0);                             // the parameter's, until the cell
    CHECK(d.bpmAtTick(768) == 100.0);
    CHECK(std::fabs(d.secondsAtTicks(768.0) - 768.0 * t150) < 1e-9);
    CHECK(std::fabs(d.ticksAtSeconds(768.0 * t150 + 1.0) - (768.0 + 1.0 / t100)) < 1e-9);   // 100 BPM: 40 a second

    // Moving the parameter moves the base and everything after it, and a T
    // cell at tick 0 is a cell, so it keeps the base it names.
    cfg.songTempo = 75.0;
    d.setConfig(cfg);
    d.setTempoMap(bar9, 1);
    CHECK(d.bpmAtTick(0) == 75.0);
    CHECK(d.bpmAtTick(768) == 100.0);
    const TempoPoint atZero[] = { { 0, 200.0 }, { 768, 100.0 } };
    d.setTempoMap(atZero, 2);
    CHECK(d.bpmAtTick(0) == 200.0);
}

TEST_CASE("the Song tempo parameter moving re-integrates from where it is", "[clock]")
{
    // Section 4: automate the parameter and the plugin integrates on from the
    // position it is at, rather than jumping to what the new tempo says the
    // whole song so far would have taken.
    Clock c; c.prepare(48000.0);
    ClockConfig cfg; cfg.source = TempoSource::Song; cfg.songTempo = 120.0; c.setConfig(cfg);
    c.setTempoMap(nullptr, 0);
    Transport t; t.valid = true; t.playing = true; t.bpm = 120.0; t.timeValid = true;
    uint64_t f = 0;
    auto block = [&] {
        t.seconds = double(f) / 48000.0; t.ppq = t.seconds * 2.0;
        c.process(t, 512, f);
        f += 512;
    };
    for (int i = 0; i < 94; ++i) block();                          // just short of a second
    const int64_t before = c.tickAtBlockStart();
    CHECK(before > 40);                                            // 48 ticks a second at 120
    cfg.songTempo = 240.0;
    c.setConfig(cfg);
    c.setTempoMap(nullptr, 0);
    block();
    // Continuous: the position carries on from where it was, it does not jump
    // to twice the elapsed time.
    CHECK(c.tickAtBlockStart() >= before);
    CHECK(c.tickAtBlockStart() < before + 4);
    const int64_t at = c.tickAtBlockStart();
    for (int i = 0; i < 94; ++i) block();                          // another second, at 240 BPM
    CHECK(c.tickAtBlockStart() - at > 90);                         // 96 ticks a second now, not 48
}

TEST_CASE("a stopped transport still ticks, so live playing has tables", "[clock]")
{
    Clock c; c.prepare(48000.0);
    ClockConfig cfg; cfg.source = TempoSource::Host; c.setConfig(cfg);
    Transport t; t.valid = true; t.playing = false; t.bpm = 120.0;
    size_t n = 0;
    for (uint64_t f = 0; f < 48000; f += 512) { c.process(t, uint32_t(std::min<uint64_t>(512, 48000 - f)), f); n += c.tickCount(); }
    CHECK(n == 48);                       // one second at 120 BPM: 48 free-running ticks
    // In Song mode it free-runs at the song's tempo instead.
    Clock d; d.prepare(48000.0);
    ClockConfig s; s.source = TempoSource::Song; s.songTempo = 60.0; d.setConfig(s);
    size_t m = 0;
    for (uint64_t f = 0; f < 48000; f += 512) { d.process(t, uint32_t(std::min<uint64_t>(512, 48000 - f)), f); m += d.tickCount(); }
    CHECK(m == 24);
}

/* --------------------------------------- the plugin's own transport (16) */

namespace {

/// The clock running itself: no host transport at all, as the Standalone has.
std::vector<Tick> runOwn(Clock& c, double seconds, uint32_t block, double rate = 48000.0)
{
    std::vector<Tick> out;
    const uint64_t total = uint64_t(seconds * rate);
    const Transport none;                    // not valid: there is no host
    for (uint64_t f = 0; f < total; f += block) {
        const uint32_t n = uint32_t(std::min<uint64_t>(block, total - f));
        c.process(none, n, f);
        for (size_t k = 0; k < c.tickCount(); ++k) out.push_back({ f + c.ticks()[k].offset, c.ticks()[k].tick });
    }
    return out;
}

} // namespace

TEST_CASE("the clock counts ticks and nothing else", "[clock]")
{
    // Sections 19 and 25: the host contributes the tempo, and bars have left
    // the model -- the song's time is its phrases' lengths and grooves, per
    // channel, so the clock has no signature of any kind in it.
    for (auto source : { TempoSource::Host, TempoSource::Song }) {
        Clock c; c.prepare(48000.0);
        ClockConfig cfg; cfg.source = source; cfg.songTempo = 120.0;
        c.setConfig(cfg);
        c.setTempoMap(nullptr, 0);
        Transport t; t.valid = true; t.playing = true; t.bpm = 120.0; t.ppq = 0.0; t.seconds = 0.0; t.timeValid = true;
        c.process(t, 512, 0);
        // 512 samples at 48 kHz is 10.7 ms; a tick at 120 BPM is 20.8 ms.
        CHECK(c.tickCount() == 1);
        CHECK(c.ticks()[0].tick == 0);
        CHECK(c.tickAtBlockStart() == 0);
    }
}

TEST_CASE("the plugin's own transport runs the song at the Song tempo", "[clock][transport]")
{
    Clock c; c.prepare(48000.0);
    ClockConfig cfg; cfg.source = TempoSource::Host; cfg.songTempo = 120.0;
    c.setConfig(cfg);
    c.setTempoMap(nullptr, 0);
    c.setOwnsTransport(true);
    CHECK(c.ownsTransport());
    CHECK_FALSE(c.ownPlaying());

    // Stopped, it free-runs so a live note still has tables and vibrato, and
    // the transport reads stopped.
    const auto idle = runOwn(c, 1.0, 512);
    CHECK(idle.size() == 48);
    CHECK_FALSE(c.playing());

    // Playing: from the song's start, at the Song tempo whatever the source
    // parameter says -- there is no host beat to follow.
    c.ownPlay();
    const auto a = runOwn(c, 1.0, 512);
    REQUIRE(a.size() == 48);              // 120 BPM: 48 ticks a second
    CHECK(c.playing());
    CHECK(a[0].index == 0);
    CHECK(a[0].frame == onGrid(0.0));
    CHECK(a[24].index == 24);
    CHECK(a[24].frame == onGrid(24.0 * framesPerTick(120.0)));
    CHECK(c.tickAtBlockStart() > 40);

    // The block size cannot move a tick here either.
    Clock d; d.prepare(48000.0); d.setConfig(cfg); d.setTempoMap(nullptr, 0);
    d.setOwnsTransport(true); d.ownPlay();
    const auto b = runOwn(d, 1.0, 97);
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) { CHECK(a[i].frame == b[i].frame); CHECK(a[i].index == b[i].index); }

    // Stopping leaves the position where it was; playing again starts over.
    c.ownStop();
    const auto stopped = runOwn(c, 0.5, 512);
    CHECK_FALSE(c.playing());
    CHECK(stopped.size() == 24);          // free-running, not the song
    c.ownPlay();
    const auto again = runOwn(c, 0.25, 512);
    REQUIRE_FALSE(again.empty());
    CHECK(again[0].index == 0);

    // A slower Song tempo is a slower transport.
    cfg.songTempo = 60.0;
    c.setConfig(cfg); c.setTempoMap(nullptr, 0);
    c.ownPlay();
    CHECK(runOwn(c, 1.0, 512).size() == 24);
}

TEST_CASE("the plugin's own transport loops, keeping its place", "[clock][transport]")
{
    Clock c; c.prepare(48000.0);
    ClockConfig cfg; cfg.source = TempoSource::Song; cfg.songTempo = 120.0;
    c.setConfig(cfg);
    c.setTempoMap(nullptr, 0);
    c.setOwnsTransport(true);
    c.setLoop(true, 0, 96);               // one 4/4 bar: 96 ticks, two seconds
    CHECK(c.loopOn());
    c.ownPlay();
    const auto a = runOwn(c, 3.0, 512);
    REQUIRE(a.size() == 144);             // three seconds at 48 ticks a second
    for (int i = 0; i < 96; ++i) { INFO("tick " << i); CHECK(a[size_t(i)].index == int64_t(i)); }
    // The wrap is a jump in the tick stream -- what tells the Player to flush
    // -- and it lands on the sample the next tick was due on.
    CHECK(a[96].index == 0);
    CHECK(a[96].frame == onGrid(96.0 * framesPerTick(120.0)));
    CHECK(a[97].index == 1);
    CHECK(a[97].frame == onGrid(97.0 * framesPerTick(120.0)));

    // A loop that starts later starts there.
    Clock d; d.prepare(48000.0); d.setConfig(cfg); d.setTempoMap(nullptr, 0);
    d.setOwnsTransport(true);
    d.setLoop(true, 96, 192);
    d.ownPlay();
    const auto b = runOwn(d, 2.5, 512);
    REQUIRE(b.size() >= 120);
    CHECK(b[0].index == 96);
    CHECK(b[96].index == 96);             // round again
    // Loop off: it plays straight on past the end.
    d.setLoop(false, 96, 192);
    d.ownPlay();
    const auto e = runOwn(d, 3.0, 512);
    REQUIRE(e.size() == 144);
    CHECK(e.back().index == 143);
}

/* ------------------------------------------------------- the grid (160) */

TEST_CASE("a tick lands on the ROM's 358 Hz grid, whatever the block size", "[clock][grid]")
{
    // Section 160: six instants a video frame, 11712 cycles apart and the
    // sixth to the next frame's first 11664; a tick fires on the first instant
    // strictly after its nominal frame.
    CHECK(gridCycle(0) == 0);
    CHECK(gridCycle(5) == 5 * 11712);
    CHECK(gridCycle(6) == 70224);
    CHECK(gridCycle(6) - gridCycle(5) == 11664);
    CHECK(gridAfter(0) == 1);
    CHECK(gridAfter(11711) == 1);
    CHECK(gridAfter(11712) == 2);
    CHECK(gridAfter(58560) == 6);
    CHECK(gridAfter(70223) == 6);
    // The tempo word: round(1834828.8 / BPM) 2048ths of the mean step.
    CHECK(std::fabs(tickSeconds(120.0, true) - 15290.0 / 2048.0 * 11704.0 / 4194304.0) < 1e-12);
    CHECK(std::fabs(tickSeconds(163.0, true) - 11257.0 / 2048.0 * 11704.0 / 4194304.0) < 1e-12);
    CHECK(std::fabs(tickSeconds(280.0, true) - 6553.0 / 2048.0 * 11704.0 / 4194304.0) < 1e-12);
    CHECK(std::fabs(tickSeconds(120.5, true) - 60.0 / (120.5 * 24.0)) < 1e-12);   // a fraction: the plain period
    CHECK(std::fabs(tickSeconds(120.0, false) - 1.0 / 48.0) < 1e-12);            // a song written here: exact

    // At 280 BPM the ticks are three or four instants apart and never
    // between two: every tick's frame is an instant's frame, and the block
    // size -- 512, 97 or 1 -- cannot move one, a tick pushed past a block's
    // end being carried into the next.
    for (uint32_t block : { 512u, 97u, 1u }) {
        INFO("block " << block);
        Clock c; c.prepare(48000.0);
        ClockConfig cfg; cfg.source = TempoSource::Song; cfg.songTempo = 280.0; cfg.lsdjTempo = true; c.setConfig(cfg);
        c.setTempoMap(nullptr, 0);
        const auto a = run(c, 1.0, block, 120.0, true);
        REQUIRE(a.size() > 100);
        for (size_t i = 0; i < a.size(); ++i) {
            INFO("tick " << i);
            CHECK(a[i].index == int64_t(i));
            CHECK(a[i].frame == onGrid(double(i) * romFramesPerTick(280.0)));
            const uint64_t cyc = a[i].frame * 4194304 / 48000;
            const uint64_t n = gridAfter(cyc) - 1;
            CHECK(gridFrame(n, 48000.0) == a[i].frame);
            if (i) {
                const uint64_t gap = a[i].frame - a[i - 1].frame;
                CHECK((gap >= 3 * 11664 / 87.4 - 2 && gap <= 4 * 11712 / 87.4 + 2));
            }
        }
    }
}
