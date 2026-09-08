// ChipBoy -- the tracker's song (spec section 13.1, UI_DESIGN section 7,
// docs/COMMANDS_AND_TEMPO.md section 9).
//
// Phrases of sixteen steps, chained by bar per channel, following the host
// transport. Self-contained with the bank on purpose: a song must be able to
// leave for a playback ROM one day (spec section 15.3).
#pragma once

#include "core/Bank/Bank.h"
#include "core/Driver/Clock.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace chipboy::tracker {

constexpr int kPhraseSlots = 255;
constexpr int kSteps = 16;
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

struct Phrase {
    bool used = false;
    std::array<Cell, kSteps> steps{};
    uint8_t groove = 0;          ///< groove slot, 0 = straight, 1-16 the song's
};

/// Sixteen tick counts, LSDj's groove screen (section 9.2): each 1-48, 0 =
/// unused. The length is the leading non-zero entries, and step i lasts
/// ticks[i mod length] -- so two entries swing and three make triplets. A
/// groove that does not fill the bar leaves its last note sustaining; steps
/// that would start at or past the bar's end do not fire at all.
struct Groove {
    std::array<uint8_t, 16> ticks{ 6, 6 };

    int length() const { int n = 0; while (n < kSteps && ticks[size_t(n)] != 0) ++n; return n ? n : 1; }
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

enum class NoteSource : uint8_t { PianoRoll = 0, Tracker = 1 };

struct Song {
    std::array<Phrase, kPhraseSlots> phrases;         ///< slot n is phrases[n-1]
    std::array<std::vector<uint8_t>, 4> chain;        ///< per channel: bar -> phrase slot (0 none)
    std::array<NoteSource, 4> noteSource{ NoteSource::PianoRoll, NoteSource::PianoRoll, NoteSource::PianoRoll, NoteSource::PianoRoll };
    std::array<Groove, 16> grooves = factoryGrooves();
    uint8_t stepsPerBar = 16;          ///< 8 or 16 (section 9.1)
    // The song's own timeline (docs/COMMANDS_AND_TEMPO.md section 4), used
    // when the tempo source is Song.
    double  tempoBpm = 120.0;          ///< the base tempo, before any T
    double  songStartSeconds = 0.0;    ///< host time where tick 0 sits
    double  beatsPerBar = 4.0;
    /// Built from the T cells by buildTempoMap() when the song is published;
    /// the clock integrates it. Not part of the file.
    std::vector<driver::TempoPoint> tempoMap;

    const Phrase* phrase(int slot) const { return slot >= 1 && slot <= kPhraseSlots && phrases[size_t(slot - 1)].used ? &phrases[size_t(slot - 1)] : nullptr; }
    uint8_t phraseAt(int ch, int bar) const { const auto& c = chain[size_t(ch & 3)]; return bar >= 0 && size_t(bar) < c.size() ? c[size_t(bar)] : 0; }
    int barTicks() const { return std::max(1, int(beatsPerBar * driver::kTicksPerBeat + 0.5)); }
    int steps() const { return stepsPerBar <= 8 ? 8 : kSteps; }
};

/// The slot in force: 0 straight, 1-16 the song's grooves, kGrooveNone the
/// phrase's own.
constexpr uint8_t kGrooveNone = 255;
/// The groove that slot resolves to on a phrase.
Groove grooveFor(const Song& s, const Phrase* p, uint8_t slot);

/// Step start ticks inside a bar, per the groove: kSteps + 1 entries, the
/// running sum of the groove's entries. The entries from the last step on hold
/// where the grid ends, which is short of the bar when the groove does not
/// fill it; a step at or past the bar's ticks does not fire. At eight steps per
/// bar every groove entry is doubled, so the same groove swings the same way.
void stepStartTicks(const Song& s, const Phrase* p, uint8_t groove, int* start);

/// Scan the chains for T cells: the tempo map the clock integrates. Message
/// thread, when a song is published.
void buildTempoMap(Song& s);

} // namespace chipboy::tracker
