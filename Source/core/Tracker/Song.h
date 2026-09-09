// ChipBoy -- the tracker's song (spec section 13.1, UI_DESIGN section 7,
// docs/COMMANDS_AND_TEMPO.md sections 9 and 25).
//
// A phrase has its own length (1-64 cells) and its own groove, and a step is
// six ticks at the straight groove, always -- so a phrase lasts as long as its
// groove makes it. The chain is per channel, one row after another, and a
// channel moves to its next row when its own row ends: two channels whose
// phrases differ in length drift apart by design (section 25). Bars are not
// part of the model at all; the host's are a ruler.
//
// Self-contained with the bank on purpose: a song must be able to leave for a
// playback ROM one day (spec section 15.3, docs/HARDWARE_DRIVER_AUDIT.md).
#pragma once

#include "core/Bank/Bank.h"
#include "core/Driver/Clock.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace chipboy::tracker {

constexpr int kPhraseSlots = 255;
/// The cells a phrase holds; its own `steps` says how many of them play.
constexpr int kMaxSteps = 64;
/// A groove is sixteen tick counts, whatever the phrase's length (section 9.2).
constexpr int kGrooveSteps = 16;
/// A step at the straight groove, always (section 25). Sixteen of them are
/// 96 ticks -- four beats -- which is where the numbers below come from.
constexpr int kTicksPerStep = 6;
/// A row with no phrase lasts this long, with a note-off at its start
/// (section 25): sixteen straight steps, the length of a 4/4 bar.
constexpr int kEmptyRowTicks = kTicksPerStep * 16;
constexpr uint8_t kNoteOff = 255;
constexpr uint8_t kDefaultVelocity = 100;   ///< the velocity a blank VEL carries; the driver keeps the instrument's volume for it

struct Cell {
    uint8_t note = 0;            ///< 0 empty, 1-127 MIDI note, 255 note off
    uint8_t vel = 0;             ///< 1-127 a start volume whatever the Velocity mode; 0 = the instrument's own volume
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

/// A phrase: its cells, how many of them play, and its groove (section 25).
/// The cells beyond `steps` are kept -- shortening a phrase in the editor and
/// lengthening it again brings them back -- but never play.
struct Phrase {
    bool used = false;
    std::array<Cell, kMaxSteps> cells{};
    uint8_t steps = 16;          ///< the phrase's length, 1-64
    uint8_t groove = 0;          ///< groove slot, 0 = straight, 1-16 the song's

    /// The length that plays, clamped into range.
    int length() const { return std::clamp<int>(steps, 1, kMaxSteps); }
};

/// Sixteen tick counts, LSDj's groove screen (section 9.2): each 1-48, 0 =
/// unused. The length is the leading non-zero entries, and step i lasts
/// ticks[i mod length] ticks -- ticks, not a fraction of a bar: a phrase is
/// as long as its groove makes it (section 25).
struct Groove {
    std::array<uint8_t, kGrooveSteps> ticks{ 6, 6 };
    /// A name, up to fifteen characters (section 39); a char array so the
    /// factory grooves stay constexpr. Empty means unnamed.
    std::array<char, 16> name{};

    const char* nameOf() const { return name.data(); }
    bool named() const { return name[0] != 0; }
    void setName(const char* s)
    {
        name.fill(0);
        for (size_t i = 0; s != nullptr && s[i] != 0 && i + 1 < name.size(); ++i) name[i] = s[i];
    }

    int length() const { int n = 0; while (n < kGrooveSteps && ticks[size_t(n)] != 0) ++n; return n ? n : 1; }
    /// The ticks step i lasts, repeating; a groove with no entries is straight.
    int at(int i) const { const int n = length(); const int t = ticks[size_t(((i % n) + n) % n)]; return t ? t : 6; }
    /// What the first `steps` steps add up to: the ticks a phrase of that
    /// length lasts under this groove.
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

/// What a channel plays (sections 14 and 20): the incoming MIDI, its own
/// cells, or both -- Hybrid takes the notes from MIDI and everything else
/// from the cells. These are the data model's names; the window calls them
/// MIDI, Trkr and Hybrid.
enum class NoteSource : uint8_t { PianoRoll = 0, Tracker = 1, Hybrid = 2 };

/// The channel's cells play at their steps: its notes, or only its columns.
inline bool cellsPlay(NoteSource s) { return s == NoteSource::Tracker || s == NoteSource::Hybrid; }
/// The channel's notes come from its cells; under Hybrid they come from MIDI.
inline bool cellNotes(NoteSource s) { return s == NoteSource::Tracker; }

struct Song {
    std::array<Phrase, kPhraseSlots> phrases;         ///< slot n is phrases[n-1]
    std::array<std::vector<uint8_t>, 4> chain;        ///< per channel: row -> phrase slot (0 none)
    std::array<NoteSource, 4> noteSource{ NoteSource::PianoRoll, NoteSource::PianoRoll, NoteSource::PianoRoll, NoteSource::PianoRoll };
    /// The record arm per channel (section 14). On for a new song, so a song
    /// written before the arms existed records exactly as it used to.
    std::array<bool, 4> recordArm{ true, true, true, true };
    std::array<Groove, 16> grooves = factoryGrooves();
    // The song's own timeline (docs/COMMANDS_AND_TEMPO.md section 4).
    /// The base tempo the file carries, written from the Song tempo
    /// parameter when the song is saved and read back into it when one is
    /// loaded, so a saved song opens at its own tempo. It never overrides the
    /// live parameter: the clock's base is always the parameter (section 4).
    double  tempoBpm = 120.0;
    double  songStartSeconds = 0.0;    ///< host time where tick 0 sits
    /// Built from the T cells by buildTempoMap() when the song is published;
    /// the clock integrates it. Not part of the file.
    std::vector<driver::TempoPoint> tempoMap;
    /// Per channel, the tick each of its rows starts on: the prefix sum over
    /// that channel's row durations, built by buildRowTables() when the song
    /// is published so the audio thread never walks the phrases (section 25).
    /// One entry per row plus the end. Not part of the file.
    std::array<std::vector<int32_t>, 4> rowStartTicks;

    const Phrase* phrase(int slot) const { return slot >= 1 && slot <= kPhraseSlots && phrases[size_t(slot - 1)].used ? &phrases[size_t(slot - 1)] : nullptr; }
    uint8_t phraseAt(int ch, int row) const { const auto& c = chain[size_t(ch & 3)]; return row >= 0 && size_t(row) < c.size() ? c[size_t(row)] : 0; }
    /// How many rows a channel's chain describes.
    int rows(int ch) const { return int(chain[size_t(ch & 3)].size()); }
    /// The longest chain: how many rows the song describes at all.
    int rows() const { int n = 0; for (const auto& c : chain) n = std::max(n, int(c.size())); return n; }
    /// The steps a channel's row plays: its phrase's length, or the sixteen an
    /// empty row is counted as.
    int stepsOfRow(int ch, int row) const { const Phrase* p = phrase(phraseAt(ch, row)); return p ? p->length() : kEmptyRowTicks / kTicksPerStep; }
};

/// The slot in force: 0 straight, 1-16 the song's grooves, kGrooveNone the
/// phrase's own.
constexpr uint8_t kGrooveNone = 255;
/// The groove that slot resolves to on a phrase.
Groove grooveFor(const Song& s, const Phrase* p, uint8_t slot);

/// Step start ticks inside a row, per the groove: kMaxSteps + 1 entries, in
/// ticks from the row's own start. Step i starts at the groove's ticks for the
/// steps before it -- six each at the straight groove (section 25). The
/// entries from the phrase's length on hold where the grid ends; a step at or
/// past the row's own ticks does not fire, which is how a groove that does not
/// fill the row leaves its last note sustaining (section 9.2).
void stepStartTicks(const Song& s, const Phrase* p, uint8_t groove, int* start);

/// How long a row of this phrase lasts: its groove's ticks over its length,
/// or kEmptyRowTicks for a row with no phrase (section 25). This is the
/// phrase's own groove -- a G cell or a G slot re-lays the steps inside the
/// row, and never moves the rows (docs/HARDWARE_DRIVER_AUDIT.md).
int phraseTicks(const Song& s, const Phrase* p);
/// How long a channel's row lasts.
int rowTicks(const Song& s, int ch, int row);

/// The prefix tables of row starts, per channel, in ticks from the song
/// start. Message thread, when a song is published; the Player then maps a
/// tick to (row, step) through them without walking the phrases (section 25).
void buildRowTables(Song& s);

/// The tick a channel's row starts on. Rows past the chain's end are empty
/// rows, end to end.
int64_t rowStartTick(const Song& s, int ch, int row);
/// Which row of a channel an absolute tick falls in, and how far into it.
void rowAtTick(const Song& s, int ch, int64_t tick, int& row, int& inRow);
/// The whole song's length: the longest channel's chain (section 25). This is
/// what the plugin's own transport loops.
int64_t songTicks(const Song& s);
/// The channel whose chain lasts longest, the lowest of them when two do: the
/// one the own transport's loop counts its rows in (section 25).
int longestChain(const Song& s);

/// Scan the chains for T cells: the tempo map the clock integrates. Message
/// thread, when a song is published; it builds the row tables first, since a
/// T cell's tick is counted through them -- its channel's, since each channel
/// keeps its own time (section 25). The base tempo is the caller's -- the Song
/// tempo parameter -- and is not itself a point in the map; a T cell modifies
/// it from its tick, and a T reverting is the base again from there
/// (docs/COMMANDS_AND_TEMPO.md section 4).
void buildTempoMap(Song& s, double baseBpm);

} // namespace chipboy::tracker
