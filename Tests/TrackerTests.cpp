// ChipBoy -- tracker playback and record tests (UI_DESIGN section 7,
// docs/COMMANDS_AND_TEMPO.md section 9).
#include "core/Driver/Clock.h"
#include "core/Link/Spsc.h"
#include "core/Tracker/Player.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace chipboy;
using namespace chipboy::tracker;
using driver::NoteEvent;

namespace {

/// A song is 300 KB now that a phrase holds sixty-four cells: every one of
/// them is built on the heap, as the plugin does, so a Windows main thread's
/// megabyte of stack is never the limit (CLAUDE.md).
std::unique_ptr<Song> blankSong() { return std::make_unique<Song>(); }

std::unique_ptr<Song> demoSong()
{
    auto owned = blankSong();
    Song& s = *owned;
    s.noteSource[0] = NoteSource::Tracker;
    auto& p = s.phrases[0]; p.used = true;
    p.steps[0].note = 60; p.steps[4].note = 64; p.steps[8].note = 67; p.steps[12].note = kNoteOff;
    p.steps[2].cmd1 = { bank::Cmd::V, 4, 6, 0 };
    s.chain[0] = { 1, 1 };
    buildBarTable(s);
    return owned;
}

/// The Host clock's ticks for a block, as the plugin feeds them in.
std::vector<driver::TickPoint> hostTicks(driver::Clock& c, uint64_t frame, uint32_t n, double bpm = 120.0, double rate = 48000.0)
{
    driver::Transport t; t.valid = true; t.playing = true; t.bpm = bpm; t.ppq = double(frame) / rate * (bpm / 60.0);
    t.seconds = double(frame) / rate; t.timeValid = true;
    c.process(t, n, frame);
    return std::vector<driver::TickPoint>(c.ticks(), c.ticks() + c.tickCount());
}

/// A run of consecutive ticks, straight into the Player.
std::vector<NoteEvent> ticks(Player& p, int64_t from, int count, bool playing = true)
{
    std::vector<driver::TickPoint> tp;
    for (int i = 0; i < count; ++i) tp.push_back({ uint32_t(i), from + i });
    std::vector<NoteEvent> out;
    p.process(tp.data(), tp.size(), playing, out);
    return out;
}

size_t countOf(const std::vector<NoteEvent>& v, NoteEvent::Kind k)
{
    size_t n = 0;
    for (const auto& e : v) if (e.kind == k) ++n;
    return n;
}

} // namespace

TEST_CASE("steps fire on their ticks, block size notwithstanding", "[tracker]")
{
    auto run = [](uint32_t block) {
        const auto owned = demoSong(); Song& s = *owned;
        driver::Clock clock; clock.prepare(48000.0);
        Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
        std::vector<std::pair<uint64_t, NoteEvent>> all;
        const uint64_t total = 192000;  // two bars at 120 BPM (4 s)
        for (uint64_t f = 0; f < total; f += block) {
            const uint32_t n = uint32_t(std::min<uint64_t>(block, total - f));
            const auto tk = hostTicks(clock, f, n);
            std::vector<NoteEvent> out;
            p.process(tk.data(), tk.size(), true, out);
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

/* ---------------------------------------------------------------- grooves */

TEST_CASE("a groove is a list of tick counts and repeats to fill the bar", "[tracker][groove]")
{
    const auto owned = demoSong(); Song& s = *owned;
    s.grooves[0].ticks = { 8, 4 };
    s.phrases[0].groove = 1;
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    int starts[kMaxSteps + 1];
    p.stepTicks(&s.phrases[0], starts);
    CHECK(starts[0] == 0);
    CHECK(starts[1] == 8);     // straight would be 6
    CHECK(starts[2] == 12);    // 8 + 4: the pair still spans two straight steps
    CHECK(starts[16] == 96);

    // Three entries: the groove is a triplet feel and repeats every three.
    s.grooves[0].ticks = { 4, 4, 4 };
    CHECK(s.grooves[0].length() == 3);
    CHECK(s.grooves[0].at(3) == 4);
    CHECK(s.grooves[0].total(16) == 64);
    p.stepTicks(&s.phrases[0], starts);
    CHECK(starts[5] == 20);
    CHECK(starts[15] == 60);
    CHECK(starts[16] == 64);   // short of the bar: the last note sustains
}

TEST_CASE("the factory grooves are the swing pairs", "[tracker][groove]")
{
    const auto owned = std::make_unique<Song>(); const Song& s = *owned;
    CHECK(s.grooves[0].at(0) == 6); CHECK(s.grooves[0].at(1) == 6);
    CHECK(s.grooves[1].at(0) == 7); CHECK(s.grooves[1].at(1) == 5);
    CHECK(s.grooves[2].at(0) == 8); CHECK(s.grooves[2].at(1) == 4);
    CHECK(s.grooves[3].at(0) == 5); CHECK(s.grooves[3].at(1) == 7);
    CHECK(s.grooves[4].at(0) == 9); CHECK(s.grooves[4].at(1) == 3);
    CHECK(s.grooves[5].length() == 3);
    CHECK(s.grooves[15].at(0) == 6);
}

TEST_CASE("steps that start past the bar do not fire", "[tracker][groove]")
{
    const auto owned = demoSong(); Song& s = *owned;
    s.grooves[0].ticks = { 8, 8 };          // 16 steps of 8 ticks is 128, a bar is 96
    s.phrases[0].groove = 1;
    for (int i = 0; i < 16; ++i) s.phrases[0].steps[size_t(i)].note = uint8_t(60 + i);
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    const auto out = ticks(p, 0, 96);
    CHECK(countOf(out, NoteEvent::NoteOn) == 12);      // steps 0-11 start inside the bar
    CHECK(out.back().a == 71);                         // step 11, and step 12 is the next bar
}

TEST_CASE("eight steps per bar doubles the groove's entries", "[tracker][groove]")
{
    const auto owned = demoSong(); Song& s = *owned;
    s.stepsPerBar = 8;
    s.grooves[0].ticks = { 7, 5 };
    s.phrases[0].groove = 1;
    CHECK(s.steps() == 8);
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    int starts[kMaxSteps + 1];
    p.stepTicks(&s.phrases[0], starts);
    CHECK(starts[1] == 14);
    CHECK(starts[2] == 24);
    CHECK(starts[8] == 96);
}

/* --------------------------------------------------- bars and step counts */

TEST_CASE("steps per bar is a number from 1 to 64", "[tracker][bars]")
{
    // Section 11: a step starts at floor(i x bar ticks / steps), so a count
    // that does not divide the bar jitters by less than a tick and the bar
    // still ends where it should.
    const auto owned = demoSong(); Song& s = *owned;
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    int starts[kMaxSteps + 1];
    s.stepsPerBar = 64;
    CHECK(s.steps() == 64);
    p.stepTicks(&s.phrases[0], starts);
    CHECK(starts[1] == 1);                        // 96 ticks over 64 steps: one and a half
    CHECK(starts[2] == 3);
    CHECK(starts[64] == 96);
    s.stepsPerBar = 5;                            // five steps in a 96-tick bar
    CHECK(s.steps() == 5);
    p.stepTicks(&s.phrases[0], starts);
    CHECK(starts[0] == 0);
    CHECK(starts[1] == 19);                       // floor(96 / 5) = 19
    CHECK(starts[3] == 57);
    CHECK(starts[5] == 96);
    s.stepsPerBar = 200;                          // clamped to the cells a phrase holds
    CHECK(s.steps() == 64);
}

TEST_CASE("a bar override moves the bars after it", "[tracker][bars]")
{
    // Section 11: a bar of eight steps in a sixteen-step song is half a bar
    // long, so everything after it comes half a bar early. The prefix table
    // is what a locate lands through.
    const auto owned = demoSong(); Song& s = *owned;
    s.chain[0] = { 1, 1, 1, 1 };
    s.barSteps = { 0, 8, 0, 0 };
    buildBarTable(s);
    CHECK(s.stepsOfBar(0) == 16);
    CHECK(s.stepsOfBar(1) == 8);
    CHECK(barStartTick(s, 0, 96) == 0);
    CHECK(barStartTick(s, 1, 96) == 96);
    CHECK(barLengthTicks(s, 1, 96) == 48);        // eight steps of six ticks
    CHECK(barStartTick(s, 2, 96) == 144);
    CHECK(barStartTick(s, 3, 96) == 240);
    CHECK(barStartTick(s, 4, 96) == 336);         // past the table: default bars, end to end
    int bar = 0, inBar = 0;
    barAtTick(s, 100, 96, bar, inBar);
    CHECK(bar == 1); CHECK(inBar == 4);
    barAtTick(s, 144, 96, bar, inBar);
    CHECK(bar == 2); CHECK(inBar == 0);
    barAtTick(s, 400, 96, bar, inBar);
    CHECK(bar == 4); CHECK(inBar == 64);

    // And the Player plays them there: bar 2's step 0 fires at tick 96, bar
    // 3's at 144 rather than 192.
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    std::vector<int64_t> at;
    for (int64_t t = 0; t < 250; ++t)
        for (const auto& e : ticks(p, t, 1))
            if (e.kind == NoteEvent::NoteOn && e.a == 60) at.push_back(t);
    REQUIRE(at.size() == 4);
    CHECK(at[0] == 0); CHECK(at[1] == 96); CHECK(at[2] == 144); CHECK(at[3] == 240);
}

TEST_CASE("a short bar plays only its own steps", "[tracker][bars]")
{
    const auto owned = std::make_unique<Song>(); Song& s = *owned;
    s.noteSource[0] = NoteSource::Tracker;
    auto& ph = s.phrases[0]; ph.used = true;
    for (int i = 0; i < 16; ++i) ph.steps[size_t(i)].note = uint8_t(60 + i);
    s.chain[0] = { 1, 1 };
    s.barSteps = { 4 };                            // a quarter-bar of four steps
    buildBarTable(s);
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    std::vector<uint8_t> notes;
    for (int64_t t = 0; t < 24; ++t)
        for (const auto& e : ticks(p, t, 1))
            if (e.kind == NoteEvent::NoteOn) notes.push_back(e.a);
    REQUIRE(notes.size() == 4);
    CHECK(notes[3] == 63);                         // steps 4-15 belong to no bar
    CHECK(barLengthTicks(s, 0, 96) == 24);
}

TEST_CASE("the groove in force is the slot, then the last cell, then the phrase", "[tracker][groove]")
{
    const auto owned = std::make_unique<Song>(); Song& s = *owned;
    s.noteSource[0] = NoteSource::Tracker;
    s.grooves[0].ticks = { 8, 4 };          // slot 1
    s.grooves[1].ticks = { 4, 4, 4 };       // slot 2
    auto& ph = s.phrases[0]; ph.used = true; ph.groove = 0;
    ph.steps[0].cmd1 = { bank::Cmd::G, 2, 0, 0 };
    s.chain[0] = { 1, 1 };
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());

    CHECK(p.groove(0) == kGrooveNone);                 // the phrase's own, straight
    int st[kMaxSteps + 1];
    p.stepTicks(&ph, st, p.groove(0));
    CHECK(st[1] == 6);

    ticks(p, 0, 1);                                     // the G cell at step 0 plays
    CHECK(p.groove(0) == 2);
    p.stepTicks(&ph, st, p.groove(0));
    CHECK(st[1] == 4);

    p.setGrooveSlot(0, 1);                              // a G slot wins over the cell
    CHECK(p.groove(0) == 1);
    p.stepTicks(&ph, st, p.groove(0));
    CHECK(st[1] == 8);

    p.setGrooveSlot(0, kGrooveNone);                    // the slot goes to none
    CHECK(p.groove(0) == 2);                            // the cell's groove is back
    ticks(p, 0, 0, false);                              // stopping starts the song again
    CHECK(p.groove(0) == kGrooveNone);
}

TEST_CASE("a mid-bar G moves the steps that follow it", "[tracker][groove]")
{
    const auto owned = std::make_unique<Song>(); Song& s = *owned;
    s.noteSource[0] = NoteSource::Tracker;
    s.grooves[0].ticks = { 12, 12 };
    auto& ph = s.phrases[0]; ph.used = true;
    ph.steps[0].note = 60;
    ph.steps[1].note = 62;
    ph.steps[2].note = 64;
    ph.steps[2].cmd1 = { bank::Cmd::G, 1, 0, 0 };
    ph.steps[3].note = 65;
    s.chain[0] = { 1 };
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    std::vector<std::pair<int64_t, uint8_t>> notes;
    for (int64_t t = 0; t < 96; ++t) {
        const auto out = ticks(p, t, 1);
        for (const auto& e : out) if (e.kind == NoteEvent::NoteOn) notes.push_back({ t, e.a });
    }
    REQUIRE(notes.size() >= 4);
    CHECK(notes[0] == std::make_pair<int64_t, uint8_t>(0, 60));
    CHECK(notes[1] == std::make_pair<int64_t, uint8_t>(6, 62));
    CHECK(notes[2] == std::make_pair<int64_t, uint8_t>(12, 64));   // still on the straight grid
    CHECK(notes[3].first == 36);                                   // 12 + 12 ticks from the G
}

/* ------------------------------------------------------------------ notes */

TEST_CASE("cells fire with their velocity", "[tracker]")
{
    const auto owned = demoSong(); Song& s = *owned;
    s.phrases[0].steps[0].vel = 42;
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    const auto out = ticks(p, 0, 30);
    REQUIRE(out.size() >= 2);
    CHECK(out[0].kind == NoteEvent::NoteOn);
    CHECK(out[0].b == 42);
    CHECK(out.back().b == kDefaultVelocity);      // step 4: an empty VEL column
}

TEST_CASE("a blank phrase sustains, a bar with no phrase ends the note", "[tracker]")
{
    const auto owned = std::make_unique<Song>(); Song& s = *owned;
    s.noteSource[0] = NoteSource::Tracker;
    s.phrases[0].used = true; s.phrases[0].steps[0].note = 67;
    s.phrases[1].used = true;                     // sixteen empty cells
    s.chain[0] = { 1, 2, 0 };
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    ticks(p, 0, 96);                              // bar 1 plays
    const auto blank = ticks(p, 96, 96);
    CHECK(blank.empty());                         // bar 2 is blank: the note holds
    const auto gone = ticks(p, 192, 8);
    REQUIRE(gone.size() == 1);
    CHECK(gone[0].kind == NoteEvent::NoteOff);
    CHECK(gone[0].a == 67);                       // the note still sounding from bar 1
}

TEST_CASE("all notes off when the transport stops", "[tracker]")
{
    const auto owned = demoSong(); Song& s = *owned;
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    ticks(p, 0, 8);
    const auto out = ticks(p, 8, 0, false);
    REQUIRE(out.size() == 1);
    CHECK(out[0].kind == NoteEvent::AllNotesOff);
    CHECK(out[0].channel == 0);
    CHECK(p.position(0).bar == -1);
}

TEST_CASE("all notes off when the tick stream jumps", "[tracker]")
{
    const auto owned = demoSong(); Song& s = *owned;
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    ticks(p, 0, 8);
    // Forwards: a locate to the middle of the bar.
    const auto fwd = ticks(p, 48, 1);
    REQUIRE(fwd.size() >= 1);
    CHECK(fwd[0].kind == NoteEvent::AllNotesOff);
    // Backwards: a loop wrapping to the top.
    const auto back = ticks(p, 0, 1);
    REQUIRE(back.size() == 2);
    CHECK(back[0].kind == NoteEvent::AllNotesOff);
    CHECK(back[1].kind == NoteEvent::NoteOn);      // step 0 fires after the flush
    // A tick that follows the last one is not a jump.
    const auto on = ticks(p, 1, 5);
    CHECK(countOf(on, NoteEvent::AllNotesOff) == 0);
}

TEST_CASE("all notes off when the lane goes", "[tracker]")
{
    const auto owned = demoSong(); Song& s = *owned;
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    ticks(p, 0, 8);
    SECTION("the source changes to the piano roll") {
        s.noteSource[0] = NoteSource::PianoRoll;
        const auto out = ticks(p, 8, 4);
        REQUIRE(out.size() == 1);
        CHECK(out[0].kind == NoteEvent::AllNotesOff);
        CHECK(out[0].channel == 0);
    }
    SECTION("the channel is muted for recording") {
        p.setMuteMask(1u);
        const auto out = ticks(p, 8, 4);
        REQUIRE(out.size() == 1);
        CHECK(out[0].kind == NoteEvent::AllNotesOff);
    }
    SECTION("and only once") {
        s.noteSource[0] = NoteSource::PianoRoll;
        ticks(p, 8, 4);
        CHECK(ticks(p, 12, 4).empty());
    }
}

/* ------------------------------------------------------------------ tempo */

TEST_CASE("T cells become the song's tempo map", "[tracker]")
{
    const auto owned = demoSong(); Song& s = *owned;
    s.phrases[0].steps[8].cmd2 = { bank::Cmd::T, 90, 0, 0 };
    buildTempoMap(s, 140.0);
    // Only the T cells are in the map: the base is the Song tempo parameter,
    // which the clock holds live (section 4).
    REQUIRE(s.tempoMap.size() == 2);          // the T in each of the two bars
    CHECK(s.tempoMap[0].tick == 48);          // step 8 of bar 1: half a bar of 96 ticks
    CHECK(s.tempoMap[0].bpm == 90.0);
    CHECK(s.tempoMap[1].tick == 144);
    // A T reverting is the base again from its tick.
    s.phrases[0].steps[12].cmd2 = bank::revertOf(bank::Cmd::T);
    buildTempoMap(s, 140.0);
    REQUIRE(s.tempoMap.size() == 4);
    CHECK(s.tempoMap[1].tick == 72);
    CHECK(s.tempoMap[1].bpm == 140.0);
    buildTempoMap(s, 96.0);                   // the parameter moved: so does the revert
    CHECK(s.tempoMap[1].bpm == 96.0);
}

TEST_CASE("a T cell fires on the tick its tempo map says, in the song's bars", "[tracker]")
{
    const auto owned = demoSong(); Song& s = *owned;
    s.beatsPerBar = 3.0;                      // a 72-tick bar
    s.phrases[0].steps[8].cmd2 = { bank::Cmd::T, 90, 0, 0 };
    buildTempoMap(s, 120.0);
    REQUIRE(!s.tempoMap.empty());
    CHECK(s.barTicks() == 72);
    // Sixteen steps divide the bar, whatever its length (section 11): a step
    // is four and a half ticks here, and step 8 is half a bar in.
    CHECK(s.tempoMap[0].tick == 36);
    // The Player counts in the song's bar ticks, so the cell lands there too.
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    std::vector<int64_t> at;
    for (int64_t t = 0; t < 72; ++t)
        for (const auto& e : ticks(p, t, 1))
            if (e.cmd2.cmd == bank::Cmd::T) at.push_back(t);
    REQUIRE(at.size() == 1);
    CHECK(at[0] == s.tempoMap[0].tick);
    // Every step of the bar plays, and the sixteenth ends it.
    int starts[kMaxSteps + 1];
    p.stepTicks(&s.phrases[0], starts);
    CHECK(starts[12] == 54);
    CHECK(starts[16] == 72);
}

/* --------------------------------------------------------------- quantise */

TEST_CASE("quantise picks the nearest step of the channel's own grid", "[tracker][record]")
{
    const auto owned = demoSong(); Song& s = *owned;
    s.noteSource[1] = NoteSource::Tracker;
    s.grooves[0].ticks = { 8, 4 };
    auto& swung = s.phrases[1]; swung.used = true; swung.groove = 1;
    s.chain[1] = { 1, 2 };
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    int bar = 0, step = 0; int64_t at = 0;
    // Bar 2 (ticks 96-191), just before step 1 at tick 102.
    REQUIRE(p.quantise(0, 96.0 + 5.8, bar, step, at));
    CHECK(bar == 1);
    CHECK(step == 1);
    CHECK(at == 102);
    // The same bar on channel 2, whose phrase swings 8/4: its step 1 is at 104.
    REQUIRE(p.quantise(1, 96.0 + 7.0, bar, step, at));
    CHECK(step == 1);
    CHECK(at == 104);
    REQUIRE(p.quantise(1, 96.0 + 3.0, bar, step, at));
    CHECK(step == 0);
    CHECK(at == 96);
    CHECK(p.quantise(0, 96.0 + 7.0, bar, step, at));
    CHECK(at == 102);                                   // channel 1 is still straight
    // A G slot in force moves that channel's grid too.
    p.setGrooveSlot(0, 1);
    REQUIRE(p.quantise(0, 96.0 + 7.0, bar, step, at));
    CHECK(at == 104);
    // Past the last step: the next bar's first.
    REQUIRE(p.quantise(0, 95.5, bar, step, at));
    CHECK(bar == 1);
    CHECK(step == 0);
    CHECK(at == 96);
}

/* ----------------------------------------------------------------- record */

namespace {

/// A song with two Trk channels and a straight grid, for the recorder.
std::unique_ptr<Song> recordSong()
{
    auto owned = std::make_unique<Song>();
    Song& s = *owned;
    s.noteSource[0] = NoteSource::Tracker;
    s.phrases[0].used = true;
    s.chain[0] = { 1, 1 };
    buildBarTable(s);
    return owned;
}

} // namespace

TEST_CASE("a plain note records its instrument, a bare note leaves the column blank", "[tracker][record]")
{
    const auto owned = recordSong(); Song& s = *owned;
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    p.resetRecord();
    const bank::Command e { bank::Cmd::E, 12, 3, 0 };
    const bank::Command w { bank::Cmd::W, 1, 0, 0 };
    RecordMessage m;
    REQUIRE(p.recordNote(0, 0.0, 60, 100, false, true, 5, 2, e, w, m));
    CHECK(m.bar == 0); CHECK(m.step == 0);
    CHECK(m.cell.note == 60);
    CHECK(m.cell.vel == 100);
    CHECK(m.cell.inst == 5);
    CHECK(m.cell.table == 2);
    CHECK(m.cell.cmd1.cmd == bank::Cmd::E); CHECK(m.cell.cmd1.a == 12); CHECK(m.cell.cmd1.b == 3);
    CHECK(m.cell.cmd2.cmd == bank::Cmd::W);
    // The same instrument again is written again: a blank column plays bare.
    REQUIRE(p.recordNote(0, 6.0, 62, 80, false, true, 5, 2, e, w, m));
    CHECK(m.step == 1);
    CHECK(m.cell.inst == 5);
    CHECK(m.cell.vel == 80);
    CHECK(m.cell.cmd1.cmd == bank::Cmd::E);      // a plain note carries the slots in force
    // A bare note (an overlap) records with the column blank and no slots.
    REQUIRE(p.recordNote(0, 12.0, 64, 70, false, false, 5, 0, e, w, m));
    CHECK(m.step == 2);
    CHECK(m.cell.note == 64);
    CHECK(m.cell.inst == 0);
    CHECK(m.cell.cmd1.cmd == bank::Cmd::None);
    CHECK(m.cell.cmd2.cmd == bank::Cmd::None);
}

TEST_CASE("a bare note's cell carries the per-note letters only", "[tracker][record]")
{
    // Section 12: the persistent letters are already in the running state, and
    // re-writing E on a sounding pulse would restart its envelope. K, which
    // shapes the note it is on, is written.
    const auto owned = recordSong(); Song& s = *owned;
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    p.resetRecord();
    const bank::Command v { bank::Cmd::V, 4, 6, 0 };
    const bank::Command k { bank::Cmd::K, 3, 0, 0 };
    RecordMessage m;
    REQUIRE(p.recordNote(0, 0.0, 60, 100, false, true, 5, 0, v, k, m));
    CHECK(m.cell.cmd1.cmd == bank::Cmd::V);          // a plain note carries both
    CHECK(m.cell.cmd2.cmd == bank::Cmd::K);
    REQUIRE(p.recordNote(0, 6.0, 62, 100, false, false, 5, 0, v, k, m));
    CHECK(m.cell.inst == 0);
    CHECK(m.cell.cmd1.cmd == bank::Cmd::None);       // V is in force already
    CHECK(m.cell.cmd2.cmd == bank::Cmd::K);          // K shapes this note
    // A slot going to none is a change, so its revert reaches a bare note too.
    const bank::Command none;
    REQUIRE(p.recordNote(0, 12.0, 64, 100, false, false, 5, 0, none, k, m));
    CHECK(m.cell.cmd1.cmd == bank::Cmd::V);
    CHECK(bank::isRevert(m.cell.cmd1));
}

TEST_CASE("the command octave writes the slots in force, changed or not", "[tracker][record]")
{
    // Section 13: a note in the command octave fires the slots on whatever is
    // sounding, so its cell carries them whether they moved or not.
    const auto owned = recordSong(); Song& s = *owned;
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    p.resetRecord();
    const bank::Command none;
    const bank::Command v { bank::Cmd::V, 4, 6, 0 };
    RecordMessage m;
    REQUIRE(p.recordSlots(0, 0.0, v, none, m));                  // the change
    CHECK_FALSE(p.recordSlots(0, 6.0, v, none, m));              // unchanged: nothing
    REQUIRE(p.recordSlots(0, 12.0, v, none, m, /*force*/ true)); // the command octave fired it
    CHECK(m.slotsOnly);
    CHECK(m.step == 2);
    CHECK(m.cell.note == 0);
    CHECK(m.cell.cmd1.cmd == bank::Cmd::V);
}

TEST_CASE("a note-off goes to its step, or the one after its note's", "[tracker][record]")
{
    const auto owned = recordSong(); Song& s = *owned;
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    p.resetRecord();
    const bank::Command none;
    RecordMessage m;
    SECTION("a later step takes the OFF") {
        REQUIRE(p.recordNote(0, 0.0, 60, 100, false, true, 1, 0, none, none, m));
        REQUIRE(p.recordNote(0, 12.0, 60, 0, true, false, 1, 0, none, none, m));
        CHECK(m.step == 2);
        CHECK(m.cell.note == kNoteOff);
    }
    SECTION("the note's own step pushes it to the next") {
        REQUIRE(p.recordNote(0, 0.0, 60, 100, false, true, 1, 0, none, none, m));
        REQUIRE(p.recordNote(0, 1.0, 60, 0, true, false, 1, 0, none, none, m));
        CHECK(m.step == 1);                       // a note shorter than a step lasts one step
        CHECK(m.cell.note == kNoteOff);
    }
    SECTION("the last step of a bar pushes it into the next bar") {
        REQUIRE(p.recordNote(0, 90.0, 60, 100, false, true, 1, 0, none, none, m));
        REQUIRE(p.recordNote(0, 90.0, 60, 0, true, false, 1, 0, none, none, m));
        CHECK(m.bar == 1);
        CHECK(m.step == 0);
    }
    SECTION("a step that already holds a note keeps it") {
        s.phrases[0].steps[2].note = 64;          // from an earlier take
        REQUIRE(p.recordNote(0, 0.0, 60, 100, false, true, 1, 0, none, none, m));
        CHECK_FALSE(p.recordNote(0, 12.0, 60, 0, true, false, 1, 0, none, none, m));
    }
    SECTION("a newer note on the same step ends the older one") {
        REQUIRE(p.recordNote(0, 12.0, 60, 100, false, true, 1, 0, none, none, m));
        CHECK_FALSE(p.recordNote(0, 12.0, 55, 0, true, false, 1, 0, none, none, m));
    }
}

TEST_CASE("a slot is written at the step whose tick it changed on", "[tracker][record]")
{
    const auto owned = recordSong(); Song& s = *owned;
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    p.resetRecord();
    const bank::Command none;
    const bank::Command e { bank::Cmd::E, 12, 3, 0 };
    const bank::Command v { bank::Cmd::V, 4, 6, 0 };
    RecordMessage m;
    CHECK_FALSE(p.recordSlots(0, 0.0, none, none, m));       // nothing in force, nothing written
    REQUIRE(p.recordSlots(0, 6.0, e, none, m));
    CHECK(m.slotsOnly);
    CHECK(m.step == 1);
    CHECK(m.cell.note == 0);
    CHECK(m.cell.cmd1.cmd == bank::Cmd::E);
    CHECK(m.cell.cmd1.a == 12);
    CHECK(m.cell.cmd2.cmd == bank::Cmd::None);
    CHECK_FALSE(p.recordSlots(0, 12.0, e, none, m));         // unchanged: written once
    REQUIRE(p.recordSlots(0, 18.0, e, v, m));                // the other slot moves
    CHECK(m.step == 3);
    CHECK(m.cell.cmd1.cmd == bank::Cmd::None);
    CHECK(m.cell.cmd2.cmd == bank::Cmd::V);
    // A note at a step carries the slots in force even when they have not moved.
    REQUIRE(p.recordNote(0, 24.0, 60, 100, false, true, 1, 0, e, v, m));
    CHECK(m.cell.cmd1.cmd == bank::Cmd::E);
    CHECK(m.cell.cmd2.cmd == bank::Cmd::V);
}

TEST_CASE("a slot going to none records the letter's revert form", "[tracker][record]")
{
    // The letters that leave something behind revert; the per-note ones have
    // nothing to put back (docs/COMMANDS_AND_TEMPO.md section 3).
    for (auto letter : { bank::Cmd::A, bank::Cmd::E, bank::Cmd::F, bank::Cmd::G, bank::Cmd::M,
                         bank::Cmd::O, bank::Cmd::P, bank::Cmd::S, bank::Cmd::T, bank::Cmd::V, bank::Cmd::W }) {
        const bank::Command r = bank::revertOf(letter);
        CHECK(r.cmd == letter);
        CHECK(bank::isRevert(r));
        CHECK(r.a == 0); CHECK(r.b == 0);       // the revert form carries no value
    }
    for (auto letter : { bank::Cmd::C, bank::Cmd::D, bank::Cmd::H, bank::Cmd::K,
                         bank::Cmd::L, bank::Cmd::R, bank::Cmd::Z })
        CHECK(bank::revertOf(letter).cmd == bank::Cmd::None);

    const auto owned = recordSong(); Song& s = *owned;
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    p.resetRecord();
    const bank::Command none;
    const bank::Command e { bank::Cmd::E, 12, 3, 0 };
    const bank::Command pitch { bank::Cmd::P, 200, 0, 0 };
    RecordMessage m;
    REQUIRE(p.recordSlots(0, 0.0, e, pitch, m));
    REQUIRE(p.recordSlots(0, 6.0, none, none, m));
    CHECK(m.cell.cmd1.cmd == bank::Cmd::E);       // "put the envelope back", not a value
    CHECK(bank::isRevert(m.cell.cmd1));
    CHECK(m.cell.cmd2.cmd == bank::Cmd::P);       // the offset and the bend go
    CHECK(bank::isRevert(m.cell.cmd2));
    // The same letter coming back is a change again.
    REQUIRE(p.recordSlots(0, 12.0, e, none, m));
    CHECK(m.cell.cmd1.a == 12);
    // A per-note letter going to none writes nothing.
    const bank::Command k { bank::Cmd::K, 4, 0, 0 };
    REQUIRE(p.recordSlots(0, 18.0, k, none, m));
    CHECK_FALSE(p.recordSlots(0, 24.0, none, none, m));
}

TEST_CASE("a cell's revert form reaches the driver as it was written", "[tracker][record]")
{
    // The Player passes a cell's commands through untouched, so what the
    // recorder wrote as a revert arrives at the driver as a revert
    // (docs/COMMANDS_AND_TEMPO.md section 3).
    const auto owned = std::make_unique<Song>(); Song& s = *owned;
    s.noteSource[0] = NoteSource::Tracker;
    auto& ph = s.phrases[0]; ph.used = true;
    ph.steps[0].note = 60; ph.steps[0].inst = 1;
    ph.steps[4].cmd1 = bank::revertOf(bank::Cmd::E);
    ph.steps[4].cmd2 = bank::revertOf(bank::Cmd::V);
    s.chain[0] = { 1 };
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    const auto out = ticks(p, 0, 96);
    const NoteEvent* cmd = nullptr;
    for (const auto& e : out) if (e.kind == NoteEvent::Command) cmd = &e;
    REQUIRE(cmd != nullptr);
    CHECK(cmd->cmd1.cmd == bank::Cmd::E);
    CHECK(bank::isRevert(cmd->cmd1));
    CHECK(cmd->cmd2.cmd == bank::Cmd::V);
    CHECK(bank::isRevert(cmd->cmd2));
}

TEST_CASE("a G cell reverting puts the phrase's own groove back", "[tracker][groove]")
{
    const auto owned = std::make_unique<Song>(); Song& s = *owned;
    s.noteSource[0] = NoteSource::Tracker;
    s.grooves[1].ticks = { 12, 12 };            // slot 2: twelve ticks a step
    auto& ph = s.phrases[0]; ph.used = true;
    ph.groove = 0;                              // the phrase's own is straight
    ph.steps[0].cmd1 = { bank::Cmd::G, 2, 0, 0 };
    s.chain[0] = { 1, 1 };
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    ticks(p, 0, 1);
    CHECK(p.groove(0) == 2);
    auto& ph2 = s.phrases[0];
    ph2.steps[1].cmd1 = bank::revertOf(bank::Cmd::G);
    ticks(p, 1, 20);
    CHECK(p.groove(0) == kGrooveNone);          // the phrase's own again
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
