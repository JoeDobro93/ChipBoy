// ChipBoy -- grooves inferred from what is played (docs/plan-groove-inference.md,
// docs/COMMANDS_AND_TEMPO.md section 226). A recorded row's note-on ticks are
// fitted, at the row's end, against a simplest-first list of grooves within a
// tolerance; a row may be two segments joined by a `G`; a row's length never
// changes. The fitter is pure: ticks in, a layout out.
#pragma once
#include "core/Tracker/Song.h"

#include <array>
#include <cstdint>
#include <vector>

namespace chipboy::tracker {

/// One run of steps under one groove. `ticks` holds the groove's entries
/// (`length` of them, used cyclically, as a Groove does); `steps` is how
/// many steps the segment has; `start` its first tick in the row.
struct FitSegment {
    int start = 0;
    int steps = 0;
    std::array<uint8_t, kGrooveSteps> ticks{};
    int length = 1;
    int order = 0;            ///< the candidate's rank: 0 straight, 1 swing ... 8 custom
    bool fresh = false;       ///< not among the song's slots: a new one would be needed
    Groove groove() const { Groove g; g.ticks.fill(0); for (int i = 0; i < length; ++i) g.ticks[size_t(i)] = ticks[size_t(i)]; return g; }
};

struct FitResult {
    std::vector<FitSegment> segments;   ///< one or two
    bool fits = true;         ///< every onset within the tolerance of its step
    int moved = 0;            ///< onsets quantised beyond the tolerance (only when !fits)
    int maxMove = 0;          ///< the largest such move, in ticks
    double score = 0.0;
    int totalSteps() const { int n = 0; for (const auto& s : segments) n += s.steps; return n; }
};

struct FitOptions {
    int rowTicks = 96;        ///< the row's length, which the layout must keep
    int tolerance = 1;        ///< ticks an onset may sit from its step
    const Groove* sticky = nullptr;                      ///< the groove in force before this row: a change costs
    const std::array<Groove, kGrooveSlots>* slots = nullptr;   ///< the song's grooves: a new one costs
    int maxSteps = kMaxSteps;
};

/// The row's layout for these note-on ticks (relative to the row's start,
/// any order, duplicates allowed).
FitResult fitRow(std::vector<int> onsets, const FitOptions& o);

/// Every step's start tick under a layout, in order across the segments.
std::vector<int> layoutSteps(const FitResult& r);
/// The step nearest a tick, and its start; -1 with no steps.
int stepNearTick(const FitResult& r, int tick, int* stepTick = nullptr);
/// Two grooves the same, entries compared to their length.
bool sameGroove(const Groove& a, const Groove& b);

} // namespace chipboy::tracker
