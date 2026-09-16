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
    if (g >= 1 && g <= kGrooveSlots) return s.grooves[size_t(g - 1)];
    return Groove{};                        // slot 0 is straight and not editable
}

namespace {
/// The cell's `H`, if it has one: `x` times, hop to step `y`. Either command
/// column carries it; column 1 first, as the ROM reads them.
const bank::Command* cellHop(const Cell& c)
{
    if (c.cmd1.cmd == bank::Cmd::H) return &c.cmd1;
    if (c.cmd2.cmd == bank::Cmd::H) return &c.cmd2;
    return nullptr;
}

/// The cell's `G`, if it has one: the groove it puts in force (section 135),
/// or kGrooveNone where it reverts. Either command column carries it.
const bank::Command* cellGroove(const Cell& c)
{
    if (c.cmd1.cmd == bank::Cmd::G) return &c.cmd1;
    if (c.cmd2.cmd == bank::Cmd::G) return &c.cmd2;
    return nullptr;
}
} // namespace

int phrasePlayOrder(const Phrase* p, uint8_t* order, int cap)
{
    const int steps = p ? p->length() : kEmptyRowTicks / kTicksPerStep;
    if (!p) { const int n = std::min(steps, cap); for (int i = 0; i < n; ++i) order[i] = uint8_t(i); return n; }
    // Section 102: the hop is taken before the step sounds, and its count is
    // per step and per phrase run -- so the order is fixed and can be laid out
    // once. `taken` is how many times each step's hop has fired.
    uint8_t taken[kMaxSteps] = {};
    int n = 0, at = 0;
    while (at >= 0 && at < steps && n < cap) {
        const bank::Command* h = cellHop(p->cells[size_t(at)]);
        // Section 214: `H F F` is the song's stop, a step of its own, not a hop.
        if (h != nullptr && !(h->a == 15 && h->b == 15)) {
            const int times = std::clamp<int>(h->a, 0, 255);
            const int to = std::clamp<int>(h->b, 0, kMaxSteps - 1);
            if (times == 0) break;                       // section 80: the chain hop ends the order
            if (taken[at] < times && taken[at] < 255) { ++taken[at]; at = to; continue; }
        }
        order[n++] = uint8_t(at);
        ++at;
    }
    if (n == 0) { order[0] = 0; return 1; }              // a phrase that plays nothing still has a row
    return n;
}

int stepStartTicks(const Song& s, const Phrase* p, uint8_t groove, int* start, uint8_t* step)
{
    // A slot named here is the one asked for and nothing else: the G slot
    // outranks the cells (section 9.2), and the editor previews one groove.
    GrooveWalk w{ groove, 0, groove != kGrooveNone };
    return stepStartTicks(s, p, w, start, step);
}

int stepStartTicks(const Song& s, const Phrase* p, GrooveWalk& w, int* start, uint8_t* step)
{
    // A position is the groove's entry for it, in ticks: six at the straight
    // groove, always (section 25), and the groove walks with the playing
    // rather than with the step number (section 102). The entry past the last
    // position holds where the grid ends.
    uint8_t order[kMaxPlaySteps];
    const int n = phrasePlayOrder(p, order, kMaxPlaySteps);
    // Section 135: a groove a `G` put in force walks on from where the last row
    // left it; the phrase's own starts at its first entry.
    if (w.slot == kGrooveNone) w.index = 0;
    Groove g = grooveFor(s, p, w.slot);
    int acc = 0;
    for (int i = 0; i < n; ++i) {
        start[i] = acc;
        if (step != nullptr) step[i] = order[i];
        // The `G` is read before the position it sits on is measured: that
        // position takes the new groove's first entry (section 135).
        if (p != nullptr && !w.locked) {
            if (const bank::Command* c = cellGroove(p->cells[size_t(order[i])])) {
                w.slot = bank::isRevert(*c) ? kGrooveNone : uint8_t(std::clamp<int>(c->a, 0, kGrooveSlots));
                w.index = 0;
                g = grooveFor(s, p, w.slot);
            }
        }
        acc += g.at(int(w.index));
        w.index = uint8_t((int(w.index) + 1) % g.length());
    }
    start[n] = acc;
    if (step != nullptr) step[n] = order[n - 1];
    return n;
}

int phraseTicks(const Song& s, const Phrase* p, GrooveWalk& w)
{
    // A row with no phrase is sixteen straight steps and leaves the walk where
    // it was (sections 25 and 135).
    if (!p) return kEmptyRowTicks;
    // The ticks the groove in force makes of the phrase's play order, so a
    // cell's `H` makes the row longer (section 102) and a `G` before or inside
    // it makes the row shorter or longer (section 135).
    int start[kMaxPlaySteps + 1];
    const int n = stepStartTicks(s, p, w, start);
    return std::max(1, start[n]);
}

int phraseTicks(const Song& s, const Phrase* p)
{
    // The phrase's own groove and no cell read: what the groove editor measures
    // a phrase against, where no `G` has spoken yet (section 135).
    GrooveWalk w{ kGrooveNone, 0, true };
    return phraseTicks(s, p, w);
}

int rowTicks(const Song& s, int ch, int row)
{
    GrooveWalk w = s.walkAt(ch, row);
    return phraseTicks(s, s.phrase(s.phraseAt(ch, row)), w);
}

void buildRowTables(Song& s)
{
    int64_t stop = -1;
    for (int ch = 0; ch < 4; ++ch) {
        const auto& chain = s.chain[size_t(ch)];
        auto& t = s.rowStartTicks[size_t(ch)];
        auto& walk = s.rowWalk[size_t(ch)];
        t.clear();
        walk.clear();
        t.reserve(chain.size() + 1);
        walk.reserve(chain.size() + 1);
        int64_t acc = 0;
        t.push_back(0);
        // One walk down the channel's chain: a `G` is in force until the next
        // one, so a row's length depends on the rows before it (section 135).
        GrooveWalk w{};
        for (size_t r = 0; r < chain.size(); ++r) {
            walk.push_back(w);
            const Phrase* p = s.phrase(chain[r]);
            // Section 214: the earliest `H F F` over the channels is the song's
            // stop; the row is measured on its own grid as the tempo map does.
            if (p != nullptr && (stop < 0 || acc < stop)) {
                int start[kMaxPlaySteps + 1]; uint8_t step[kMaxPlaySteps + 1];
                GrooveWalk probe = w;
                const int n = stepStartTicks(s, p, probe, start, step);
                for (int pos = 0; pos < n; ++pos) {
                    if (start[pos] >= start[n]) break;
                    const Cell& c = p->cells[size_t(step[pos])];
                    const bool ff = (c.cmd1.cmd == bank::Cmd::H && c.cmd1.a == 15 && c.cmd1.b == 15) || (c.cmd2.cmd == bank::Cmd::H && c.cmd2.a == 15 && c.cmd2.b == 15);
                    if (ff) { const int64_t at = acc + start[pos]; if (stop < 0 || at < stop) stop = at; break; }
                }
            }
            acc = std::min<int64_t>(acc + phraseTicks(s, p, w), INT32_MAX / 2);
            t.push_back(int32_t(acc));
        }
        walk.push_back(w);
        // The transposes are the chain's rows and no more (section 48).
        auto& tsp = s.chainTranspose[size_t(ch)];
        if (tsp.size() > chain.size()) tsp.resize(chain.size());
    }
    s.stopTick = stop;
}

int64_t rowStartTick(const Song& s, int ch, int row)
{
    if (row <= 0) return 0;
    const auto& t = s.rowStartTicks[size_t(ch & 3)];
    if (t.empty()) return int64_t(row) * kEmptyRowTicks;          // no table: empty rows, end to end
    if (size_t(row) < t.size()) return t[size_t(row)];
    return int64_t(t.back()) + int64_t(row - int(t.size()) + 1) * kEmptyRowTicks;
}

int64_t chainLoopTicks(const Song& s, int ch)
{
    if (s.chainEnd[size_t(ch & 3)] != ChainEnd::Loop) return 0;
    const int rows = s.loopRows(ch);
    return rows > 0 ? rowStartTick(s, ch, rows) : 0;
}

void rowAtTick(const Song& s, int ch, int64_t tick, int& row, int& inRow, int* pass)
{
    int64_t at = std::max<int64_t>(0, tick);
    // Section 212: a looping channel plays its chain round again from row 0.
    const int64_t loop = chainLoopTicks(s, ch);
    if (pass != nullptr) *pass = loop > 0 ? int(std::min<int64_t>(at / loop, 1 << 30)) : 0;
    if (loop > 0) at %= loop;
    rowAtTickLaid(s, ch, at, row, inRow);
}

void rowAtTickLaid(const Song& s, int ch, int64_t tick, int& row, int& inRow)
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
    // Section 214: an `H F F` ends the song where it stands. The stop's own
    // tick is the song's length, so a transport loop comes round there.
    if (s.stopTick >= 0 && s.stopTick < n) n = std::max<int64_t>(1, s.stopTick);
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
    const double base = std::clamp(baseBpm, 40.0, 295.0);   // section 161
    std::vector<int> starts(size_t(kMaxPlaySteps) + 1, 0);
    std::vector<uint8_t> stepOf(size_t(kMaxPlaySteps) + 1, 0);
    for (int ch = 0; ch < 4; ++ch) {
        const int rows = s.rows(ch);
        const size_t first = s.tempoMap.size();       // this channel's points start here
        for (int row = 0; row < rows; ++row) {
            const Phrase* p = s.phrase(s.phraseAt(ch, row));
            if (!p) continue;
            // The row's own grid: the walk the table was built from
            // (section 135), so a `T` sits where its step really sounds.
            GrooveWalk w = s.walkAt(ch, row);
            // Positions, not steps (section 102): a `T` on a step an `H` plays
            // twice is two points on the timeline, and both belong to it.
            const int n = stepStartTicks(s, p, w, starts.data(), stepOf.data());
            const int length = starts[size_t(n)];
            for (int pos = 0; pos < n; ++pos) {
                if (starts[size_t(pos)] >= length) break;                      // that position never plays
                const Cell& cell = p->cells[size_t(stepOf[size_t(pos)])];
                const bank::Command* t = cell.cmd1.cmd == bank::Cmd::T ? &cell.cmd1 : cell.cmd2.cmd == bank::Cmd::T ? &cell.cmd2 : nullptr;
                if (!t) continue;
                // Section 165: the ROM adds the tick's word before the tick's
                // own T runs, so a T takes effect from the tick after its own.
                s.tempoMap.push_back({ rowStartTick(s, ch, row) + starts[size_t(pos)] + (s.lsdjTempo ? 1 : 0),
                                       bank::isRevert(*t) ? base : double(bank::tempoBpmOfByte(t->a)) });
            }
        }
        // Section 212: a looping channel's T cells come round on every pass
        // for as long as the longest chain lasts (a bound on the points keeps
        // a one-step chain against a long song from filling the map).
        const int64_t loop = chainLoopTicks(s, ch);
        if (loop > 0 && s.tempoMap.size() > first) {
            const int64_t total = songTicks(s);
            const size_t last = s.tempoMap.size();
            for (int64_t off = loop; off < total && s.tempoMap.size() < first + 4096; off += loop)
                for (size_t k = first; k < last; ++k)
                    if (s.tempoMap[k].tick < loop) s.tempoMap.push_back({ s.tempoMap[k].tick + off, s.tempoMap[k].bpm });
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
