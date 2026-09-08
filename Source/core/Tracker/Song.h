// ChipBoy -- the tracker's song (spec section 13.1, UI_DESIGN section 7,
// docs/COMMANDS_AND_TEMPO.md sections 9 and 11).
//
// Phrases of up to sixty-four cells, chained by bar per channel. A bar is
// steps x step ticks long, and a bar may override the song's step count, so
// the bars are laid end to end from the song start through a prefix table --
// the tracker owns its ruler. Self-contained with the bank on purpose: a song
// must be able to leave for a playback ROM one day (spec section 15.3).
#pragma once

#include "core/Bank/Bank.h"
#include "core/Driver/Clock.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace chipboy::tracker {

constexpr int kPhraseSlots = 255;
/// The cells a phrase holds; the bar's step count says how many of them play
/// (section 11). Steps per bar is a number from 1 to this.
constexpr int kMaxSteps = 64;
/// A groove is sixteen tick counts, whatever the step count (section 9.2).
constexpr int kGrooveSteps = 16;
constexpr uint8_t kNoteOff = 255;
constexpr uint8_t kDefaultVelocity = 100;   ///< what an empty VEL column means

struct Cell {
    uint8_t note = 0;            ///< 0 empty, 1-127 MIDI note, 255 note off
    uint8_t vel = 0;             ///< 1-127, 0 = the default velocity
    uint8_t inst = 0;            ///< 0 blank (a bare note), else instrument slot
    uint8_t table = 0;           ///< 0 keep, else table slot
    bank::Command cmd1, cmd2;
};
/// What a cell's note sounds at (section 9.1): its VEL column, or the default.
inline uint8_t velocityOf(const Cell& c) { return c.vel ? c.vel : kDefaultVelocity; }
/// Nothing in the cell at all: the Player skips it and the writers leave it out.
inline bool emptyCell(const Cell& c)
{
    return c.note == 0 && c.vel == 0 && c.inst == 0 && c.table == 0
           && c.cmd1.cmd == bank::Cmd::None && c.cmd2.cmd == bank::Cmd::None;
}

struct Phrase {
    bool used = false;
    std::array<Cell, kMaxSteps> steps{};
    uint8_t groove = 0;          ///< groove slot, 0 = straight, 1-16 the song's
};

/// Sixteen tick counts, LSDj's groove screen (section 9.2): each 1-48, 0 =
/// unused. The length is the leading non-zero entries, and step i lasts
/// ticks[i mod length] ticks -- at sixteen steps to a 4/4 bar; another step
/// count scales them (section 11). A groove that does not fill the bar leaves
/// its last note sustaining; steps that would start at or past the bar's end
/// do not fire at all.
struct Groove {
    std::array<uint8_t, kGrooveSteps> ticks{ 6, 6 };

    int length() const { int n = 0; while (n < kGrooveSteps && ticks[size_t(n)] != 0) ++n; return n ? n : 1; }
    /// The ticks step i lasts, repeating; a groove with no entries is straight.
    int at(int i) const { const int n = length(); const int t = ticks[size_t(((i % n) + n) % n)]; return t ? t : 6; }
    /// What the first `steps` steps add up to, against the bar's ticks.
    int total(int steps) const { int t = 0; for (int i = 0; i < steps; ++i) t += at(i); return t; }
};

/// The sixteen editable grooves a new song holds: swing pairs, one triplet,
/// the rest straight (section 9.2).
constexpr std::array<Groove, 16> factoryGrooves()
{
    std::array<Groove, 16> g{};
    g[1].ticks = { 7, 5 };
    g[2].ticks = { 8, 4 };
    g[3].ticks = { 5, 7 };
    g[4].ticks = { 9, 3 };
    g[5].ticks = { 4, 4, 4 };
    return g;
}

/// What a channel plays (section 14): the incoming MIDI, or its own cells.
/// These are the data model's names; the window calls them MIDI and Trkr.
enum class NoteSource : uint8_t { PianoRoll = 0, Tracker = 1 };

struct Song {
    std::array<Phrase, kPhraseSlots> phrases;         ///< slot n is phrases[n-1]
    std::array<std::vector<uint8_t>, 4> chain;        ///< per channel: bar -> phrase slot (0 none)
    std::array<NoteSource, 4> noteSource{ NoteSource::PianoRoll, NoteSource::PianoRoll, NoteSource::PianoRoll, NoteSource::PianoRoll };
    /// The record arm per channel (section 14). On for a new song, so a song
    /// written before the arms existed records exactly as it used to.
    std::array<bool, 4> recordArm{ true, true, true, true };
    std::array<Groove, 16> grooves = factoryGrooves();
    uint8_t stepsPerBar = 16;          ///< 1-64 (section 11)
    /// One bar's own step count, 0 = the song's (section 11). One entry per
    /// bar; bars past the end take the default.
    std::vector<uint8_t> barSteps;
    // The song's own timeline (docs/COMMANDS_AND_TEMPO.md section 4), used
    // when the tempo source is Song.
    /// The base tempo the file carries, written from the Song tempo
    /// parameter when the song is saved and read back into it when one is
    /// loaded, so a saved song opens at its own tempo. It never overrides the
    /// live parameter: the clock's base is always the parameter (section 4).
    double  tempoBpm = 120.0;
    double  songStartSeconds = 0.0;    ///< host time where tick 0 sits
    double  beatsPerBar = 4.0;
    /// Built from the T cells by buildTempoMap() when the song is published;
    /// the clock integrates it. Not part of the file.
    std::vector<driver::TempoPoint> tempoMap;
    /// Bar b starts this many steps into the song: the prefix sum over the
    /// bars' step counts, built by buildBarTable() when the song is published
    /// so the audio thread never walks the overrides (section 11). It counts
    /// steps rather than ticks, so one table serves whatever bar ticks the
    /// host reports. Not part of the file.
    std::vector<int32_t> barStartSteps;

    const Phrase* phrase(int slot) const { return slot >= 1 && slot <= kPhraseSlots && phrases[size_t(slot - 1)].used ? &phrases[size_t(slot - 1)] : nullptr; }
    uint8_t phraseAt(int ch, int bar) const { const auto& c = chain[size_t(ch & 3)]; return bar >= 0 && size_t(bar) < c.size() ? c[size_t(bar)] : 0; }
    int barTicks() const { return std::max(1, int(beatsPerBar * driver::kTicksPerBeat + 0.5)); }
    /// The song's default steps per bar, 1-64.
    int steps() const { return std::clamp<int>(stepsPerBar, 1, kMaxSteps); }
    /// A bar's own step count: its override, or the default (section 11).
    int stepsOfBar(int bar) const
    {
        if (bar >= 0 && size_t(bar) < barSteps.size() && barSteps[size_t(bar)] != 0)
            return std::clamp<int>(barSteps[size_t(bar)], 1, kMaxSteps);
        return steps();
    }
    /// How many bars the song describes: the longest chain, and any bar an
    /// override names.
    int bars() const
    {
        size_t n = barSteps.size();
        for (const auto& c : chain) n = std::max(n, c.size());
        return int(n);
    }
};

/// The slot in force: 0 straight, 1-16 the song's grooves, kGrooveNone the
/// phrase's own.
constexpr uint8_t kGrooveNone = 255;
/// The groove that slot resolves to on a phrase.
Groove grooveFor(const Song& s, const Phrase* p, uint8_t slot);

/// Step start ticks inside a bar, per the groove: kMaxSteps + 1 entries, in
/// ticks from the bar's own start. Step i starts at
/// `groove ticks so far x bar ticks / (6 x steps per bar)`, which is the
/// addendum's floor(i x bar ticks / steps) at the straight groove and the
/// running sum of the groove's entries at sixteen steps in 4/4 (section 11).
/// `barTicks` is the bar length in force (0 for the song's own) and
/// `barSteps` the bar's own step count (0 for the song's default). The
/// entries from the last step on hold where the grid ends, which is short of
/// the bar when the groove does not fill it; a step at or past the bar's
/// ticks does not fire.
void stepStartTicks(const Song& s, const Phrase* p, uint8_t groove, int* start, int barTicks = 0, int barSteps = 0);

/// The prefix table of bar starts, in steps from the song start. Message
/// thread, when a song is published; the Player then maps a tick to (bar,
/// step) through it without walking the overrides (section 11).
void buildBarTable(Song& s);

/// The step index bar b starts at, from the table; bars past it are the
/// song's default length.
int64_t barStartStep(const Song& s, int bar);
/// The tick bar b starts on, for a bar of `barTicks` ticks.
int64_t barStartTick(const Song& s, int bar, int barTicks);
/// How many ticks bar b lasts: its steps x the song's step ticks.
int barLengthTicks(const Song& s, int bar, int barTicks);
/// Which bar an absolute tick falls in, and how far into it. Bars lie end to
/// end from tick 0 (section 11).
void barAtTick(const Song& s, int64_t tick, int barTicks, int& bar, int& inBar);

/// Scan the chains for T cells: the tempo map the clock integrates. Message
/// thread, when a song is published; it builds the bar table first, since a
/// T cell's tick is counted through it. The base tempo is the caller's -- the
/// Song tempo parameter -- and is not itself a point in the map; a T cell
/// modifies it from its tick, and a T reverting is the base again from there
/// (docs/COMMANDS_AND_TEMPO.md section 4).
void buildTempoMap(Song& s, double baseBpm);

} // namespace chipboy::tracker
