// ChipBoy -- tracker playback tests (UI_DESIGN section 7).
#include "core/Driver/Clock.h"
#include "core/Link/Spsc.h"
#include "core/Tracker/Player.h"

#include <catch2/catch_test_macros.hpp>

using namespace chipboy;
using namespace chipboy::tracker;
using driver::NoteEvent;

namespace {

Song demoSong()
{
    Song s;
    s.noteSource[0] = NoteSource::Tracker;
    auto& p = s.phrases[0]; p.used = true;
    p.steps[0].note = 60; p.steps[4].note = 64; p.steps[8].note = 67; p.steps[12].note = kNoteOff;
    p.steps[2].cmd1 = { bank::Cmd::V, 4, 6, 0 };
    s.chain[0] = { 1, 1 };
    return s;
}

/// The Host clock's ticks for a block, as the plugin feeds them in.
std::vector<driver::TickPoint> hostTicks(driver::Clock& c, uint64_t frame, uint32_t n, double bpm = 120.0, double rate = 48000.0)
{
    driver::Transport t; t.valid = true; t.playing = true; t.bpm = bpm; t.ppq = double(frame) / rate * (bpm / 60.0);
    t.seconds = double(frame) / rate; t.timeValid = true;
    c.process(t, n, frame);
    return std::vector<driver::TickPoint>(c.ticks(), c.ticks() + c.tickCount());
}

} // namespace

TEST_CASE("steps fire on their ticks, block size notwithstanding", "[tracker]")
{
    auto run = [](uint32_t block) {
        Song s = demoSong();
        driver::Clock clock; clock.prepare(48000.0);
        Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
        std::vector<std::pair<uint64_t, NoteEvent>> all;
        const uint64_t total = 192000;  // two bars at 120 BPM (4 s)
        for (uint64_t f = 0; f < total; f += block) {
            const uint32_t n = uint32_t(std::min<uint64_t>(block, total - f));
            const auto ticks = hostTicks(clock, f, n);
            std::vector<NoteEvent> out;
            p.process(ticks.data(), ticks.size(), true, out);
            for (auto& e : out) all.push_back({ f + e.offset, e });
        }
        return all;
    };
    const auto a = run(512), b = run(97);
    REQUIRE(a.size() == 10);   // 4 notes + 1 command per bar, two bars
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) { CHECK(a[i].first == b[i].first); CHECK(a[i].second.kind == b[i].second.kind); CHECK(a[i].second.a == b[i].second.a); }
    // Step 4 of bar 1 is beat 2: frame 24000 at 120 BPM.
    CHECK(a[2].first == 24000);
    CHECK(a[2].second.a == 64);
    CHECK(a[1].second.kind == NoteEvent::Command);
    CHECK(a[1].second.cmd1.cmd == bank::Cmd::V);
    // bar 2 repeats the phrase
    CHECK(a[5].first == 96000);
}

TEST_CASE("grooves stretch alternate steps", "[tracker]")
{
    Song s = demoSong();
    s.grooves[0] = { 8, 4 };
    s.phrases[0].groove = 1;
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    int starts[17];
    p.stepTicks(&s.phrases[0], starts);
    CHECK(starts[0] == 0);
    CHECK(starts[1] == 8);     // straight would be 6
    CHECK(starts[2] == 12);    // 8 + 4: the pair still spans two straight steps
    CHECK(starts[16] == 96);
}

TEST_CASE("a G slot overrides the phrase's groove until it changes back", "[tracker]")
{
    Song s = demoSong();
    s.grooves[0] = { 8, 4 };
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    int straight[17], swung[17];
    p.stepTicks(&s.phrases[0], straight);
    CHECK(straight[1] == 6);
    p.setGrooveOverride(0, 1);
    CHECK(p.groove(0) == 1);
    p.stepTicks(&s.phrases[0], swung, p.groove(0));
    CHECK(swung[1] == 8);
    p.setGrooveOverride(0, kGrooveNone);
    CHECK(p.groove(0) == kGrooveNone);
}

TEST_CASE("quantise picks the nearest step, counted in ticks", "[tracker]")
{
    Song s = demoSong();
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    int bar = 0, step = 0; int64_t at = 0;
    // Bar 2 (ticks 96-191), just before step 1 at tick 102.
    REQUIRE(p.quantise(96.0 + 5.8, bar, step, at));
    CHECK(bar == 1);
    CHECK(step == 1);
    CHECK(at == 102);
}

TEST_CASE("T cells become the song's tempo map", "[tracker]")
{
    Song s = demoSong();
    s.tempoBpm = 140.0;
    s.phrases[0].steps[8].cmd2 = { bank::Cmd::T, 90, 0, 0 };
    buildTempoMap(s);
    REQUIRE(s.tempoMap.size() == 3);          // the base, then the T in each of the two bars
    CHECK(s.tempoMap[0].tick == 0);
    CHECK(s.tempoMap[0].bpm == 140.0);
    CHECK(s.tempoMap[1].tick == 48);          // step 8 of bar 1: half a bar of 96 ticks
    CHECK(s.tempoMap[1].bpm == 90.0);
    CHECK(s.tempoMap[2].tick == 144);
}

TEST_CASE("recording writes the slots in force", "[tracker]")
{
    Song s = demoSong();
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    p.resetRecord();
    const bank::Command e { bank::Cmd::E, 12, 3, 0 };
    const bank::Command w { bank::Cmd::W, 1, 0, 0 };
    RecordMessage m;
    REQUIRE(p.recordNote(0, 0.0, 60, false, 5, 2, e, w, m));
    CHECK(m.bar == 0); CHECK(m.step == 0);
    CHECK(m.cell.note == 60);
    CHECK(m.cell.inst == 5);                  // first cell on this channel: the instrument is written
    CHECK(m.cell.table == 2);
    CHECK(m.cell.cmd1.cmd == bank::Cmd::E); CHECK(m.cell.cmd1.a == 12); CHECK(m.cell.cmd1.b == 3);
    CHECK(m.cell.cmd2.cmd == bank::Cmd::W);
    // The same instrument again is not repeated.
    REQUIRE(p.recordNote(0, 6.0, 62, false, 5, 2, e, w, m));
    CHECK(m.step == 1);
    CHECK(m.cell.inst == 0);
    // A slot that changed since the last step is written with no note...
    const bank::Command v { bank::Cmd::V, 4, 6, 0 };
    REQUIRE(p.recordSlots(0, 12.0, e, v, m));
    CHECK(m.slotsOnly);
    CHECK(m.step == 2);
    CHECK(m.cell.note == 0);
    CHECK(m.cell.cmd2.cmd == bank::Cmd::V);
    // ... and only once.
    CHECK_FALSE(p.recordSlots(0, 18.0, e, v, m));
}

TEST_CASE("the SPSC ring survives a corrupted head", "[link]")
{
    link::Spsc<int, 8> q;
    for (int i = 0; i < 8; ++i) CHECK(q.push(i));
    CHECK_FALSE(q.push(99));
    int v = 0; CHECK(q.pop(v)); CHECK(v == 0);
    q.head.store(1000000);          // garbage from another process
    CHECK_FALSE(q.pop(v));          // resynced rather than reading out of range
    CHECK(q.size() == 0);
}
