// ChipBoy -- tracker playback tests (UI_DESIGN section 7).
#include "core/Tracker/Player.h"
#include "core/Link/Spsc.h"

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
}

TEST_CASE("steps fire at their ppq positions, block size notwithstanding", "[tracker]")
{
    auto run = [](uint32_t block) {
        Song s = demoSong();
        Player p; p.prepare(48000.0); p.setSong(&s); p.setTicksPerBeat(24); p.setBeatsPerBar(4.0);
        std::vector<std::pair<uint64_t, NoteEvent>> all;
        const uint64_t total = 192000;  // two bars at 120 BPM (4 s)
        for (uint64_t f = 0; f < total; f += block) {
            const uint32_t n = uint32_t(std::min<uint64_t>(block, total - f));
            driver::Transport t; t.valid = true; t.playing = true; t.bpm = 120.0; t.ppq = double(f) / 48000.0 * 2.0;
            std::vector<NoteEvent> out;
            p.process(t, n, out);
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
    Player p; p.prepare(48000.0); p.setSong(&s);
    int starts[17];
    p.stepTicks(&s.phrases[0], 16, starts);
    CHECK(starts[0] == 0);
    CHECK(starts[1] == 8);     // straight would be 6
    CHECK(starts[2] == 12);    // 8 + 4: the pair still spans two straight steps
    CHECK(starts[16] == 96);
}

TEST_CASE("quantise picks the nearest step", "[tracker]")
{
    Song s = demoSong();
    Player p; p.prepare(48000.0); p.setSong(&s);
    driver::Transport t; t.valid = true; t.playing = true; t.bpm = 120.0; t.ppq = 4.0 + 0.24;   // bar 2, just before step 1 (0.25 ppq)
    int bar = 0, step = 0; double at = 0;
    REQUIRE(p.quantise(t, 0, 64, bar, step, at));
    CHECK(bar == 1);
    CHECK(step == 1);
    CHECK(at == 4.25);
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
