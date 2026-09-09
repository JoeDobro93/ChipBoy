// ChipBoy -- the song's timeline (docs/COMMANDS_AND_TEMPO.md section 25).
//
// Every channel keeps its own time: a row lasts as long as its phrase's
// groove makes it, and the prefix table over a channel's row durations is
// what turns a tick into (row, step) and back. Built on the message thread
// when a song is published; the audio thread only reads it.
#include "core/Tracker/Song.h"

namespace chipboy::tracker {

Groove grooveFor(const Song& s, const Phrase* p, uint8_t slot)
{
    uint8_t g = slot;
    if (g == kGrooveNone) g = p ? p->groove : 0;
    if (g >= 1 && g <= 16) return s.grooves[size_t(g - 1)];
    return Groove{};                        // slot 0 is straight and not editable
}

void stepStartTicks(const Song& s, const Phrase* p, uint8_t groove, int* start)
{
    // A step is the groove's entry for it, in ticks: six at the straight
    // groove, always (section 25). The entries past the phrase's length hold
    // where the grid ends.
    const int steps = p ? p->length() : kEmptyRowTicks / kTicksPerStep;
    const Groove g = grooveFor(s, p, groove);
    int acc = 0;
    for (int i = 0; i < steps; ++i) { start[i] = acc; acc += g.at(i); }
    for (int i = steps; i <= kMaxSteps; ++i) start[i] = acc;
}

int phraseTicks(const Song& s, const Phrase* p)
{
    if (!p) return kEmptyRowTicks;
    // The phrase's own groove, not the one a G has put in force: the rows lie
    // end to end on a table built when the song was published, so a command
    // cannot move them. A G re-lays the steps inside the row instead, exactly
    // as a groove that does not fill a row leaves its last note sustaining
    // (section 9.2).
    const Groove g = grooveFor(s, p, kGrooveNone);
    return std::max(1, g.total(p->length()));
}

int rowTicks(const Song& s, int ch, int row)
{
    return phraseTicks(s, s.phrase(s.phraseAt(ch, row)));
}

void buildRowTables(Song& s)
{
    for (int ch = 0; ch < 4; ++ch) {
        const auto& chain = s.chain[size_t(ch)];
        auto& t = s.rowStartTicks[size_t(ch)];
        t.clear();
        t.reserve(chain.size() + 1);
        int64_t acc = 0;
        t.push_back(0);
        for (size_t r = 0; r < chain.size(); ++r) {
            acc = std::min<int64_t>(acc + phraseTicks(s, s.phrase(chain[r])), INT32_MAX / 2);
            t.push_back(int32_t(acc));
        }
        // The transposes are the chain's rows and no more (section 48).
        auto& tsp = s.chainTranspose[size_t(ch)];
        if (tsp.size() > chain.size()) tsp.resize(chain.size());
    }
}

int64_t rowStartTick(const Song& s, int ch, int row)
{
    if (row <= 0) return 0;
    const auto& t = s.rowStartTicks[size_t(ch & 3)];
    if (t.empty()) return int64_t(row) * kEmptyRowTicks;          // no table: empty rows, end to end
    if (size_t(row) < t.size()) return t[size_t(row)];
    return int64_t(t.back()) + int64_t(row - int(t.size()) + 1) * kEmptyRowTicks;
}

void rowAtTick(const Song& s, int ch, int64_t tick, int& row, int& inRow)
{
    const int64_t at = std::max<int64_t>(0, tick);
    const auto& t = s.rowStartTicks[size_t(ch & 3)];
    const int last = t.empty() ? 0 : int(t.size()) - 1;
    const int64_t end = rowStartTick(s, ch, last);
    if (at >= end) {
        // Past the chain: empty rows from here on, so the rest is arithmetic.
        row = last + int(std::min<int64_t>((at - end) / kEmptyRowTicks, 1 << 20));
    } else {
        // The last row that starts at or before this tick.
        int lo = 0, hi = last;
        while (lo < hi) {
            const int mid = lo + (hi - lo + 1) / 2;
            if (t[size_t(mid)] <= at) lo = mid; else hi = mid - 1;
        }
        row = lo;
    }
    inRow = int(at - rowStartTick(s, ch, row));
}

int64_t songTicks(const Song& s)
{
    int64_t n = 0;
    for (int ch = 0; ch < 4; ++ch) n = std::max(n, rowStartTick(s, ch, s.rows(ch)));
    return n;
}

int longestChain(const Song& s)
{
    int best = 0; int64_t at = -1;
    for (int ch = 0; ch < 4; ++ch) {
        const int64_t end = rowStartTick(s, ch, s.rows(ch));
        if (end > at) { at = end; best = ch; }
    }
    return best;
}

void buildTempoMap(Song& s, double baseBpm)
{
    // Every T cell, at the tick its step starts on -- its own channel's tick,
    // since the channels keep their own time (section 25). The chains are
    // short and this runs on the message thread when a song is published.
    //
    // The base is not in the map: it is the Song tempo parameter, which the
    // clock holds and a host can automate (section 4). Only T cells are here,
    // and a T reverting is the base again from its tick.
    buildRowTables(s);
    s.tempoMap.clear();
    const double base = std::clamp(baseBpm, 40.0, 255.0);
    std::vector<int> starts(size_t(kMaxSteps) + 1, 0);
    for (int ch = 0; ch < 4; ++ch) {
        const int rows = s.rows(ch);
        for (int row = 0; row < rows; ++row) {
            const Phrase* p = s.phrase(s.phraseAt(ch, row));
            if (!p) continue;
            const int length = phraseTicks(s, p);
            const int steps = p->length();
            stepStartTicks(s, p, kGrooveNone, starts.data());
            for (int step = 0; step < steps; ++step) {
                if (starts[size_t(step)] >= length) break;                     // that step never plays
                const Cell& cell = p->cells[size_t(step)];
                const bank::Command* t = cell.cmd1.cmd == bank::Cmd::T ? &cell.cmd1 : cell.cmd2.cmd == bank::Cmd::T ? &cell.cmd2 : nullptr;
                if (!t) continue;
                s.tempoMap.push_back({ rowStartTick(s, ch, row) + starts[size_t(step)],
                                       bank::isRevert(*t) ? base : double(bank::tempoBpmOfByte(t->a)) });
            }
        }
    }
    // The channels are scanned one after another, so the points arrive out of
    // order; the clock wants them by tick. Two T cells on one tick cannot both
    // be the timeline: the lower channel wins, which is the order they were
    // gathered in, so a stable sort keeps it.
    std::stable_sort(s.tempoMap.begin(), s.tempoMap.end(),
                     [](const driver::TempoPoint& a, const driver::TempoPoint& b) { return a.tick < b.tick; });
    s.tempoMap.erase(std::unique(s.tempoMap.begin(), s.tempoMap.end(),
                                 [](const driver::TempoPoint& a, const driver::TempoPoint& b) { return a.tick == b.tick; }),
                     s.tempoMap.end());
}

} // namespace chipboy::tracker
