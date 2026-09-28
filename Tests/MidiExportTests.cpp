// ChipBoy -- MIDI export tests (docs/plan-midi-export.md,
// docs/COMMANDS_AND_TEMPO.md section 224): a song built by hand, exported,
// and the file parsed back by a small reader here.
#include "core/Bank/Bank.h"
#include "core/Driver/Clock.h"
#include "core/Export/MidiExport.h"
#include "core/Tracker/Song.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

using namespace chipboy;

namespace {

struct Note { int note = 0; int64_t on = 0, off = -1; };
struct Track {
    std::string name;
    std::vector<Note> notes;                                   ///< in note-on order
    std::vector<std::pair<int64_t, uint32_t>> tempos;          ///< tick, microseconds per quarter
    std::vector<std::tuple<int64_t, int, int, int, int>> sigs; ///< tick, nn, dd, cc, bb
    int64_t end = -1;
};
struct File { int format = -1; int ppq = 0; std::vector<Track> tracks; };

uint32_t be16(const std::vector<uint8_t>& d, size_t at) { return uint32_t(d[at]) << 8 | d[at + 1]; }
uint32_t be32(const std::vector<uint8_t>& d, size_t at) { return be16(d, at) << 16 | be16(d, at + 2); }
uint32_t vlq(const std::vector<uint8_t>& d, size_t& at)
{
    uint32_t v = 0;
    for (int i = 0; i < 5; ++i) { const uint8_t b = d[at++]; v = (v << 7) | (b & 0x7f); if (!(b & 0x80)) break; }
    return v;
}

/// The whole file, each track's notes paired off by pitch.
File parse(const std::vector<uint8_t>& d)
{
    File f;
    REQUIRE(d.size() > 14);
    REQUIRE(std::string(d.begin(), d.begin() + 4) == "MThd");
    REQUIRE(be32(d, 4) == 6);
    f.format = int(be16(d, 8));
    const int nTracks = int(be16(d, 10));
    f.ppq = int(be16(d, 12));
    size_t at = 14;
    for (int t = 0; t < nTracks; ++t) {
        REQUIRE(at + 8 <= d.size());
        REQUIRE(std::string(d.begin() + long(at), d.begin() + long(at) + 4) == "MTrk");
        const size_t len = be32(d, at + 4);
        at += 8;
        const size_t stop = at + len;
        REQUIRE(stop <= d.size());
        Track tr;
        int64_t tick = 0;
        uint8_t running = 0;
        std::map<int, size_t> open;   // pitch -> index into notes
        while (at < stop) {
            tick += vlq(d, at);
            uint8_t st = d[at];
            if (st & 0x80) { ++at; running = st; } else st = running;
            if (st == 0xFF) {
                const uint8_t type = d[at++];
                const uint32_t n = vlq(d, at);
                const std::vector<uint8_t> data(d.begin() + long(at), d.begin() + long(at + n));
                at += n;
                if (type == 0x03) tr.name.assign(data.begin(), data.end());
                else if (type == 0x51) { REQUIRE(n == 3); tr.tempos.push_back({ tick, uint32_t(data[0]) << 16 | uint32_t(data[1]) << 8 | data[2] }); }
                else if (type == 0x58) { REQUIRE(n == 4); tr.sigs.push_back({ tick, data[0], data[1], data[2], data[3] }); }
                else if (type == 0x2F) { tr.end = tick; REQUIRE(at == stop); }
            } else if (st == 0xF0 || st == 0xF7) {
                at += vlq(d, at);
            } else {
                const uint8_t kind = st & 0xF0;
                const int a = d[at++];
                const int b = (kind == 0xC0 || kind == 0xD0) ? 0 : d[at++];
                if (kind == 0x90 && b > 0) {
                    REQUIRE(open.count(a) == 0);          // never two ons of one pitch
                    open[a] = tr.notes.size();
                    tr.notes.push_back({ a, tick, -1 });
                } else if (kind == 0x80 || (kind == 0x90 && b == 0)) {
                    REQUIRE(open.count(a) == 1);          // an off matches an on
                    tr.notes[open[a]].off = tick;
                    open.erase(a);
                }
            }
        }
        REQUIRE(open.empty());                            // every note ended
        REQUIRE(tr.end >= 0);
        f.tracks.push_back(std::move(tr));
    }
    REQUIRE(at == d.size());
    return f;
}

/// The song of the plan: PU1 a note ended by OFF and one killed by `K 02`
/// with a `T` on the way; PU2 a `C 47` chord; WAV a phrase replayed by
/// `H 1 0`; NOI one note. Two signatures, 120 BPM.
std::unique_ptr<tracker::Song> planSong()
{
    auto owned = std::make_unique<tracker::Song>();
    tracker::Song& s = *owned;
    s.tempoBpm = 120.0;
    s.signatures = { tracker::TimeSignature{ 0, 4, 4, 24 }, tracker::TimeSignature{ 48, 3, 4, 24 } };
    auto& pu1 = s.phrases[0]; pu1.used = true;
    pu1.cells[0].note = 60; pu1.cells[0].inst = 1;
    pu1.cells[4].note = tracker::kNoteOff;
    pu1.cells[8].note = 62; pu1.cells[8].inst = 1; pu1.cells[8].cmd1 = { bank::Cmd::K, 2, 0, 0 };
    pu1.cells[12].cmd2 = { bank::Cmd::T, 150, 0, 0 };
    s.chain[0] = { 1 };
    auto& pu2 = s.phrases[1]; pu2.used = true;
    pu2.cells[0].note = 60; pu2.cells[0].inst = 1; pu2.cells[0].cmd1 = { bank::Cmd::C, 4, 7, 0 };
    pu2.cells[3].note = tracker::kNoteOff;
    s.chain[1] = { 2 };
    auto& wav = s.phrases[2]; wav.used = true; wav.steps = 4;
    wav.cells[0].note = 48; wav.cells[0].inst = 7;
    wav.cells[1].note = 50;
    wav.cells[2].note = 52; wav.cells[2].cmd1 = { bank::Cmd::H, 1, 0, 0 };
    wav.cells[3].note = tracker::kNoteOff;
    s.chain[2] = { 3 };
    auto& noi = s.phrases[3]; noi.used = true;
    noi.cells[0].note = 60; noi.cells[0].inst = 12;
    noi.cells[2].note = tracker::kNoteOff;
    s.chain[3] = { 4 };
    return owned;
}

std::unique_ptr<bank::Bank> planBank()
{
    std::unique_ptr<bank::Bank> b(new bank::Bank(bank::Bank::factory()));
    b->instruments[11].envRate = 0;   // slot 12, the noise: an envelope that holds, so OFF ends the note
    return b;
}

} // namespace

TEST_CASE("MIDI export: the file's frame -- format 1, PPQ 24, named tracks, the song's length", "[midi]")
{
    auto song = planSong();
    auto bank = planBank();
    midi::Report rep;
    const auto bytes = midi::exportSong(*song, *bank, midi::Options{}, &rep);
    REQUIRE(!bytes.empty());
    const File f = parse(bytes);
    CHECK(f.format == 1);
    CHECK(f.ppq == 24);
    CHECK(rep.ppq == 24);
    CHECK(rep.ticks == 96);                       // the longest chain: sixteen steps of six ticks
    REQUIRE(f.tracks.size() == 5);
    CHECK(f.tracks[0].name == "ChipBoy");
    CHECK(f.tracks[1].name == "PU1");
    CHECK(f.tracks[2].name == "PU2");
    CHECK(f.tracks[3].name == "WAV");
    CHECK(f.tracks[4].name == "NOI");
    for (const auto& t : f.tracks) CHECK(t.end == 96);
    CHECK(rep.notes_.empty());
}

TEST_CASE("MIDI export: the tempo at 0 and a T cell's event, the two signatures", "[midi]")
{
    auto song = planSong();
    auto bank = planBank();
    const File f = parse(midi::exportSong(*song, *bank, midi::Options{}));
    const auto& zero = f.tracks[0];
    REQUIRE(zero.tempos.size() == 2);
    CHECK(zero.tempos[0].first == 0);
    CHECK(zero.tempos[0].second == 500000);       // 120 BPM: half a second a quarter
    CHECK(zero.tempos[1].first == 72);            // the T at step 12
    CHECK(zero.tempos[1].second == 400000);       // T 96 is 150 BPM
    REQUIRE(zero.sigs.size() == 2);
    CHECK(zero.sigs[0] == std::make_tuple(int64_t(0), 4, 2, 24, 8));
    CHECK(zero.sigs[1] == std::make_tuple(int64_t(48), 3, 2, 24, 8));
    for (size_t i = 1; i < f.tracks.size(); ++i) { CHECK(f.tracks[i].tempos.empty()); CHECK(f.tracks[i].sigs.empty()); }
}

TEST_CASE("MIDI export: PU1's note ends at its OFF, and a K 02 ends one two ticks on", "[midi]")
{
    auto song = planSong();
    auto bank = planBank();
    const File f = parse(midi::exportSong(*song, *bank, midi::Options{}));
    const auto& pu1 = f.tracks[1];
    REQUIRE(pu1.notes.size() == 2);
    CHECK(pu1.notes[0].note == 60);
    CHECK(pu1.notes[0].on == 0);
    CHECK(pu1.notes[0].off == 24);                // the OFF at step 4
    CHECK(pu1.notes[1].note == 62);
    CHECK(pu1.notes[1].on == 48);                 // step 8
    CHECK(pu1.notes[1].off == 50);                // K 02: killed two ticks after its note
}

TEST_CASE("MIDI export: a C 47 cell writes the chord's notes, one tick each", "[midi]")
{
    auto song = planSong();
    auto bank = planBank();
    const File f = parse(midi::exportSong(*song, *bank, midi::Options{}));
    const auto& pu2 = f.tracks[2];
    REQUIRE(pu2.notes.size() == 18);              // eighteen ticks to the OFF at step 3
    static const int chord[3] = { 60, 64, 67 };
    for (size_t i = 0; i < pu2.notes.size(); ++i) {
        INFO("note " << i);
        CHECK(pu2.notes[i].on == int64_t(i));
        CHECK(pu2.notes[i].off == int64_t(i) + 1);
        CHECK(pu2.notes[i].note == chord[i % 3]);
    }
}

TEST_CASE("MIDI export: an H 1 0 row plays the phrase's first steps twice", "[midi]")
{
    auto song = planSong();
    auto bank = planBank();
    const File f = parse(midi::exportSong(*song, *bank, midi::Options{}));
    const auto& wav = f.tracks[3];
    // The four-step phrase plays 0 1 0 1 2 3 (thirty-six ticks), then comes
    // round again while the longer chains play on (section 212).
    REQUIRE(wav.notes.size() >= 5);
    const int expect[5] = { 48, 50, 48, 50, 52 };
    for (size_t i = 0; i < 5; ++i) {
        INFO("note " << i);
        CHECK(wav.notes[i].note == expect[i]);
        CHECK(wav.notes[i].on == int64_t(i) * 6);
        CHECK(wav.notes[i].off == int64_t(i) * 6 + 6);
    }
    // The OFF at step 3 ends the row at tick 30; the row's second pass
    // starts at 36 (section 212), so no note sounds between.
    CHECK(wav.notes[5].on == 36);
    CHECK(wav.notes[5].note == 48);
    CHECK(wav.notes.size() == 14);                // passes at 0 and 36 whole, the third cut at 96
}

TEST_CASE("MIDI export: the noise track is there, and left out by the option", "[midi]")
{
    auto song = planSong();
    auto bank = planBank();
    midi::Report rep;
    const File with = parse(midi::exportSong(*song, *bank, midi::Options{}, &rep));
    REQUIRE(with.tracks.size() == 5);
    REQUIRE(with.tracks[4].notes.size() == 1);
    CHECK(with.tracks[4].notes[0].note == 60);
    CHECK(with.tracks[4].notes[0].on == 0);
    CHECK(with.tracks[4].notes[0].off == 12);     // the OFF at step 2
    CHECK(rep.notes[3] == 1);
    midi::Options o; o.noise = false;
    const File without = parse(midi::exportSong(*song, *bank, o, &rep));
    REQUIRE(without.tracks.size() == 4);
    CHECK(without.tracks[3].name == "WAV");
    CHECK(rep.notes[3] == 1);                     // counted even when not written
}

TEST_CASE("MIDI export: a song with no rows writes nothing", "[midi]")
{
    auto song = std::make_unique<tracker::Song>();
    auto bank = planBank();
    midi::Report rep;
    CHECK(midi::exportSong(*song, *bank, midi::Options{}, &rep).empty());
    REQUIRE(rep.notes_.size() == 1);
}
