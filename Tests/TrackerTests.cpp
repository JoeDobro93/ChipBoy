// ChipBoy -- tracker playback and record tests (UI_DESIGN section 7,
// docs/COMMANDS_AND_TEMPO.md section 9).
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
        Song s = demoSong();
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
    Song s = demoSong();
    s.grooves[0].ticks = { 8, 4 };
    s.phrases[0].groove = 1;
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    int starts[17];
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
    const Song s;
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
    Song s = demoSong();
    s.grooves[0].ticks = { 8, 8 };          // 16 steps of 8 ticks is 128, a bar is 96
    s.phrases[0].groove = 1;
    for (int i = 0; i < kSteps; ++i) s.phrases[0].steps[size_t(i)].note = uint8_t(60 + i);
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    const auto out = ticks(p, 0, 96);
    CHECK(countOf(out, NoteEvent::NoteOn) == 12);      // steps 0-11 start inside the bar
    CHECK(out.back().a == 71);                         // step 11, and step 12 is the next bar
}

TEST_CASE("eight steps per bar doubles the groove's entries", "[tracker][groove]")
{
    Song s = demoSong();
    s.stepsPerBar = 8;
    s.grooves[0].ticks = { 7, 5 };
    s.phrases[0].groove = 1;
    CHECK(s.steps() == 8);
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    int starts[17];
    p.stepTicks(&s.phrases[0], starts);
    CHECK(starts[1] == 14);
    CHECK(starts[2] == 24);
    CHECK(starts[8] == 96);
    // 32 was offered and never worked: anything above 8 is 16 steps.
    s.stepsPerBar = 32;
    CHECK(s.steps() == 16);
}

TEST_CASE("the groove in force is the slot, then the last cell, then the phrase", "[tracker][groove]")
{
    Song s;
    s.noteSource[0] = NoteSource::Tracker;
    s.grooves[0].ticks = { 8, 4 };          // slot 1
    s.grooves[1].ticks = { 4, 4, 4 };       // slot 2
    auto& ph = s.phrases[0]; ph.used = true; ph.groove = 0;
    ph.steps[0].cmd1 = { bank::Cmd::G, 2, 0, 0 };
    s.chain[0] = { 1, 1 };
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());

    CHECK(p.groove(0) == kGrooveNone);                 // the phrase's own, straight
    int st[17];
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
    Song s;
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
    Song s = demoSong();
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
    Song s;
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
    Song s = demoSong();
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
    Song s = demoSong();
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
    Song s = demoSong();
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

TEST_CASE("a T cell fires on the tick its tempo map says, in the song's bars", "[tracker]")
{
    Song s = demoSong();
    s.beatsPerBar = 3.0;                      // a 72-tick bar
    s.phrases[0].steps[8].cmd2 = { bank::Cmd::T, 90, 0, 0 };
    buildTempoMap(s);
    REQUIRE(s.tempoMap.size() >= 2);
    CHECK(s.barTicks() == 72);
    CHECK(s.tempoMap[1].tick == 48);
    // The Player counts in the song's bar ticks, so the cell lands there too.
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    std::vector<int64_t> at;
    for (int64_t t = 0; t < 72; ++t)
        for (const auto& e : ticks(p, t, 1))
            if (e.cmd2.cmd == bank::Cmd::T) at.push_back(t);
    REQUIRE(at.size() == 1);
    CHECK(at[0] == s.tempoMap[1].tick);
    // The bar is twelve steps long: the last four never play.
    int starts[17];
    p.stepTicks(&s.phrases[0], starts);
    CHECK(starts[12] == 72);
}

/* --------------------------------------------------------------- quantise */

TEST_CASE("quantise picks the nearest step of the channel's own grid", "[tracker][record]")
{
    Song s = demoSong();
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
Song recordSong()
{
    Song s;
    s.noteSource[0] = NoteSource::Tracker;
    s.phrases[0].used = true;
    s.chain[0] = { 1, 1 };
    return s;
}

SlotRevert instrumentRevert()
{
    SlotRevert r;
    r.e[0] = 9; r.e[1] = 3;
    r.f = 1; r.o = 1;
    r.s[0] = 2; r.s[1] = 4;
    r.v[0] = 5; r.v[1] = 6;
    r.w = 3;
    r.m[0] = 6; r.m[1] = 5;
    r.t = 150;
    return r;
}

} // namespace

TEST_CASE("a plain note records its instrument, a bare note leaves the column blank", "[tracker][record]")
{
    Song s = recordSong();
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    p.resetRecord();
    const SlotRevert rev = instrumentRevert();
    const bank::Command e { bank::Cmd::E, 12, 3, 0 };
    const bank::Command w { bank::Cmd::W, 1, 0, 0 };
    RecordMessage m;
    REQUIRE(p.recordNote(0, 0.0, 60, 100, false, true, 5, 2, e, w, rev, m));
    CHECK(m.bar == 0); CHECK(m.step == 0);
    CHECK(m.cell.note == 60);
    CHECK(m.cell.vel == 100);
    CHECK(m.cell.inst == 5);
    CHECK(m.cell.table == 2);
    CHECK(m.cell.cmd1.cmd == bank::Cmd::E); CHECK(m.cell.cmd1.a == 12); CHECK(m.cell.cmd1.b == 3);
    CHECK(m.cell.cmd2.cmd == bank::Cmd::W);
    // The same instrument again is written again: a blank column plays bare.
    REQUIRE(p.recordNote(0, 6.0, 62, 80, false, true, 5, 2, e, w, rev, m));
    CHECK(m.step == 1);
    CHECK(m.cell.inst == 5);
    CHECK(m.cell.vel == 80);
    CHECK(m.cell.cmd1.cmd == bank::Cmd::E);      // a plain note carries the slots in force
    // A bare note (an overlap) records with the column blank and no slots.
    REQUIRE(p.recordNote(0, 12.0, 64, 70, false, false, 5, 0, e, w, rev, m));
    CHECK(m.step == 2);
    CHECK(m.cell.note == 64);
    CHECK(m.cell.inst == 0);
    CHECK(m.cell.cmd1.cmd == bank::Cmd::None);
    CHECK(m.cell.cmd2.cmd == bank::Cmd::None);
}

TEST_CASE("a note-off goes to its step, or the one after its note's", "[tracker][record]")
{
    Song s = recordSong();
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    p.resetRecord();
    const SlotRevert rev;
    const bank::Command none;
    RecordMessage m;
    SECTION("a later step takes the OFF") {
        REQUIRE(p.recordNote(0, 0.0, 60, 100, false, true, 1, 0, none, none, rev, m));
        REQUIRE(p.recordNote(0, 12.0, 60, 0, true, false, 1, 0, none, none, rev, m));
        CHECK(m.step == 2);
        CHECK(m.cell.note == kNoteOff);
    }
    SECTION("the note's own step pushes it to the next") {
        REQUIRE(p.recordNote(0, 0.0, 60, 100, false, true, 1, 0, none, none, rev, m));
        REQUIRE(p.recordNote(0, 1.0, 60, 0, true, false, 1, 0, none, none, rev, m));
        CHECK(m.step == 1);                       // a note shorter than a step lasts one step
        CHECK(m.cell.note == kNoteOff);
    }
    SECTION("the last step of a bar pushes it into the next bar") {
        REQUIRE(p.recordNote(0, 90.0, 60, 100, false, true, 1, 0, none, none, rev, m));
        REQUIRE(p.recordNote(0, 90.0, 60, 0, true, false, 1, 0, none, none, rev, m));
        CHECK(m.bar == 1);
        CHECK(m.step == 0);
    }
    SECTION("a step that already holds a note keeps it") {
        s.phrases[0].steps[2].note = 64;          // from an earlier take
        REQUIRE(p.recordNote(0, 0.0, 60, 100, false, true, 1, 0, none, none, rev, m));
        CHECK_FALSE(p.recordNote(0, 12.0, 60, 0, true, false, 1, 0, none, none, rev, m));
    }
    SECTION("a newer note on the same step ends the older one") {
        REQUIRE(p.recordNote(0, 12.0, 60, 100, false, true, 1, 0, none, none, rev, m));
        CHECK_FALSE(p.recordNote(0, 12.0, 55, 0, true, false, 1, 0, none, none, rev, m));
    }
}

TEST_CASE("a slot is written at the step whose tick it changed on", "[tracker][record]")
{
    Song s = recordSong();
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    p.resetRecord();
    const SlotRevert rev = instrumentRevert();
    const bank::Command none;
    const bank::Command e { bank::Cmd::E, 12, 3, 0 };
    const bank::Command v { bank::Cmd::V, 4, 6, 0 };
    RecordMessage m;
    CHECK_FALSE(p.recordSlots(0, 0.0, none, none, rev, m));       // nothing in force, nothing written
    REQUIRE(p.recordSlots(0, 6.0, e, none, rev, m));
    CHECK(m.slotsOnly);
    CHECK(m.step == 1);
    CHECK(m.cell.note == 0);
    CHECK(m.cell.cmd1.cmd == bank::Cmd::E);
    CHECK(m.cell.cmd1.a == 12);
    CHECK(m.cell.cmd2.cmd == bank::Cmd::None);
    CHECK_FALSE(p.recordSlots(0, 12.0, e, none, rev, m));         // unchanged: written once
    REQUIRE(p.recordSlots(0, 18.0, e, v, rev, m));                // the other slot moves
    CHECK(m.step == 3);
    CHECK(m.cell.cmd1.cmd == bank::Cmd::None);
    CHECK(m.cell.cmd2.cmd == bank::Cmd::V);
    // A note at a step carries the slots in force even when they have not moved.
    REQUIRE(p.recordNote(0, 24.0, 60, 100, false, true, 1, 0, e, v, rev, m));
    CHECK(m.cell.cmd1.cmd == bank::Cmd::E);
    CHECK(m.cell.cmd2.cmd == bank::Cmd::V);
}

TEST_CASE("a slot going to none records what it reverts to", "[tracker][record]")
{
    const SlotRevert rev = instrumentRevert();
    CHECK(revertCommand(bank::Cmd::E, rev).a == 9);
    CHECK(revertCommand(bank::Cmd::E, rev).b == 3);
    CHECK(revertCommand(bank::Cmd::F, rev).a == 1);
    CHECK(revertCommand(bank::Cmd::O, rev).a == 1);
    CHECK(revertCommand(bank::Cmd::S, rev).a == 2);
    CHECK(revertCommand(bank::Cmd::V, rev).b == 6);
    CHECK(revertCommand(bank::Cmd::W, rev).a == 3);
    CHECK(revertCommand(bank::Cmd::M, rev).a == 6);
    CHECK(revertCommand(bank::Cmd::M, rev).b == 5);
    CHECK(revertCommand(bank::Cmd::T, rev).a == 150);
    CHECK(revertCommand(bank::Cmd::P, rev).a == 128);
    CHECK(revertCommand(bank::Cmd::A, rev).a == 0);
    CHECK(revertCommand(bank::Cmd::G, rev).a == 0);
    CHECK(revertCommand(bank::Cmd::C, rev).cmd == bank::Cmd::None);   // per-note letters leave nothing
    CHECK(revertCommand(bank::Cmd::L, rev).cmd == bank::Cmd::None);
    CHECK(revertCommand(bank::Cmd::R, rev).cmd == bank::Cmd::None);

    Song s = recordSong();
    Player p; p.prepare(48000.0); p.setSong(&s); p.setBarTicks(s.barTicks());
    p.resetRecord();
    const bank::Command none;
    const bank::Command e { bank::Cmd::E, 12, 3, 0 };
    const bank::Command pitch { bank::Cmd::P, 200, 0, 0 };
    RecordMessage m;
    REQUIRE(p.recordSlots(0, 0.0, e, pitch, rev, m));
    REQUIRE(p.recordSlots(0, 6.0, none, none, rev, m));
    CHECK(m.cell.cmd1.cmd == bank::Cmd::E);       // the instrument's own envelope
    CHECK(m.cell.cmd1.a == 9);
    CHECK(m.cell.cmd1.b == 3);
    CHECK(m.cell.cmd2.cmd == bank::Cmd::P);       // P 128 keeps the offset and stops the bend
    CHECK(m.cell.cmd2.a == 128);
    // The same letter coming back is a change again.
    REQUIRE(p.recordSlots(0, 12.0, e, none, rev, m));
    CHECK(m.cell.cmd1.a == 12);
    // A per-note letter going to none writes nothing.
    const bank::Command k { bank::Cmd::K, 4, 0, 0 };
    REQUIRE(p.recordSlots(0, 18.0, k, none, rev, m));
    CHECK_FALSE(p.recordSlots(0, 24.0, none, none, rev, m));
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
