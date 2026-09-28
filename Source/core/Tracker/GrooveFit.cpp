#include "core/Tracker/GrooveFit.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace chipboy::tracker {

namespace {

struct Candidate { std::vector<int> entries; int order; };

/// Simplest first (docs/plan-groove-inference.md): straight, the swing pairs
/// summing to 12 up to a 9:3 lilt, triplets, sixteenth triplets,
/// thirty-seconds, the swung thirty-seconds and the flam-like pairs, the
/// sparse grids, the finest.
const std::vector<Candidate>& candidates()
{
    static const std::vector<Candidate> c = {
        { { 6 }, 0 },
        { { 7, 5 }, 1 }, { { 8, 4 }, 1 }, { { 9, 3 }, 1 },
        { { 5, 7 }, 1 }, { { 4, 8 }, 1 }, { { 3, 9 }, 1 },
        { { 8 }, 2 },
        { { 4 }, 3 },
        { { 3 }, 4 },
        { { 4, 2 }, 5 }, { { 2, 4 }, 5 }, { { 5, 1 }, 5 }, { { 1, 5 }, 5 },
        { { 10, 2 }, 5 }, { { 11, 1 }, 5 }, { { 2, 10 }, 5 }, { { 1, 11 }, 5 },
        { { 12 }, 6 }, { { 24 }, 6 },
        { { 2 }, 7 }, { { 1 }, 7 },
    };
    return c;
}

bool inSlots(const FitSegment& s, const FitOptions& o)
{
    const Groove g = s.groove();
    if (o.slots == nullptr) return s.order == 0;
    for (const auto& slot : *o.slots) if (sameGroove(slot, g)) return true;
    return false;
}

/// The grid a candidate lays over `length` ticks: the step starts, and the
/// segment those steps make -- cyclic when the pattern divides the length,
/// else the step lengths written out with the last one taking the remainder.
bool layGrid(const std::vector<int>& entries, int length, int maxSteps, std::vector<int>& starts, FitSegment& seg)
{
    starts.clear();
    int at = 0; size_t i = 0; int cycle = 0;
    for (int e : entries) cycle += e;
    if (cycle <= 0 || length <= 0) return false;
    while (at < length) {
        starts.push_back(at);
        at += entries[i % entries.size()];
        ++i;
        if (int(starts.size()) > maxSteps) return false;
    }
    seg.steps = int(starts.size());
    seg.ticks.fill(0);
    if (length % cycle == 0 || at == length) {
        if (int(entries.size()) > kGrooveSteps) return false;
        seg.length = int(entries.size());
        for (size_t k = 0; k < entries.size(); ++k) seg.ticks[k] = uint8_t(entries[k]);
        return true;
    }
    // The last step takes the remainder: the entries are written out.
    if (seg.steps > kGrooveSteps) return false;
    seg.length = seg.steps;
    for (int k = 0; k < seg.steps; ++k) {
        const int next = k + 1 < seg.steps ? starts[size_t(k + 1)] : length;
        seg.ticks[size_t(k)] = uint8_t(std::clamp(next - starts[size_t(k)], 1, 48));
    }
    return true;
}

struct SegFit { bool ok = false; FitSegment seg; double dev = 0.0; int moved = 0; int maxMove = 0; double score = 0.0; };

/// One candidate over one segment's onsets (relative ticks): every onset on
/// a step within the tolerance, no two on one step. `snap` accepts any
/// distance and counts the moves instead, for the fallback.
SegFit fitSegment(const std::vector<int>& onsets, int length, const Candidate& c, const FitOptions& o, bool snap)
{
    SegFit f;
    std::vector<int> starts;
    if (!layGrid(c.entries, length, o.maxSteps, starts, f.seg)) return f;
    f.seg.order = c.order;
    // Notes take steps closest pair first, so a note a little off never
    // pushes an exact one off its own step: the pair with the smallest
    // distance is settled, then the next, until every note has a step.
    std::vector<char> taken(starts.size(), 0), placed(onsets.size(), 0);
    double dev = 0.0, moveCost = 0.0;
    int within = 0;
    for (size_t n = 0; n < onsets.size(); ++n) {
        int bestOn = -1, bestStep = -1, bestD = 1 << 30;
        for (size_t i = 0; i < onsets.size(); ++i) {
            if (placed[i]) continue;
            for (size_t k = 0; k < starts.size(); ++k) {
                if (taken[k]) continue;
                const int d = std::abs(starts[k] - onsets[i]);
                if (d < bestD) { bestD = d; bestOn = int(i); bestStep = int(k); }
            }
        }
        if (bestOn < 0) return f;
        if (bestD > o.tolerance) { if (!snap) return f; ++f.moved; f.maxMove = std::max(f.maxMove, bestD); moveCost += double(bestD) + 0.5; }
        else { dev += bestD; ++within; }
        taken[size_t(bestStep)] = 1; placed[size_t(bestOn)] = 1;
    }
    f.ok = true;
    f.dev = within == 0 ? 0.0 : dev / double(within);   // a moved note pays once, above
    const bool change = o.sticky != nullptr && !sameGroove(*o.sticky, f.seg.groove());
    f.seg.fresh = !inSlots(f.seg, o) && (o.sticky == nullptr || change);   // what is in force has a slot
    // Steps nothing lands on cost a little each: a finer grid that fits by
    // being fine is not simpler than a coarser one that fits by being right
    // (eight empty steps of a `3` outweigh a `G` and a change of groove). The
    // mean distance of the notes within tolerance from their steps is weighed
    // so that a grid every note sits on beats one they sit a tick off, one
    // rank up, when neither is in force. A note moved beyond the tolerance
    // costs its distance and a half.
    const double empty = 0.45 * double(std::max(0, f.seg.steps - int(onsets.size())));
    f.score = double(c.order) + 5.0 * f.dev + (f.seg.fresh ? 1.0 : 0.0) + (change ? 1.5 : 0.0) + empty + moveCost;
    return f;
}

/// The fallback: the onsets' own gaps as the entries, rests where a gap is
/// longer than a step may be.
SegFit fitCustom(const std::vector<int>& onsets, int length, const FitOptions& o)
{
    SegFit f;
    std::vector<int> points{ 0 };
    for (int t : onsets) if (t > 0 && t < length) points.push_back(t);
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    std::vector<int> entries;
    for (size_t k = 0; k < points.size(); ++k) {
        int gap = (k + 1 < points.size() ? points[k + 1] : length) - points[k];
        while (gap > 48) { entries.push_back(48); gap -= 48; }
        if (gap > 0) entries.push_back(gap);
    }
    if (entries.empty() || int(entries.size()) > kGrooveSteps || int(entries.size()) > o.maxSteps) return f;
    f.seg.steps = int(entries.size());
    f.seg.length = int(entries.size());
    f.seg.ticks.fill(0);
    for (size_t k = 0; k < entries.size(); ++k) f.seg.ticks[k] = uint8_t(entries[k]);
    f.seg.order = 8;
    f.seg.fresh = !inSlots(f.seg, o);
    f.ok = true;
    const bool change = o.sticky != nullptr && !sameGroove(*o.sticky, f.seg.groove());
    f.score = 8.0 + (f.seg.fresh ? 1.0 : 0.0) + (change ? 1.5 : 0.0);
    return f;
}

SegFit bestSimple(const std::vector<int>& onsets, int length, const FitOptions& o, bool snap)
{
    SegFit best;
    for (const auto& c : candidates()) {
        SegFit f = fitSegment(onsets, length, c, o, snap);
        if (!f.ok) continue;
        if (!best.ok || f.score < best.score) best = f;
    }
    return best;
}

} // namespace

bool sameGroove(const Groove& a, const Groove& b)
{
    // As the steps see them: `6` and `6 6` are one groove.
    for (int i = 0; i < kGrooveSteps; ++i) if (a.at(i) != b.at(i)) return false;
    return true;
}

FitResult fitRow(std::vector<int> onsets, const FitOptions& o)
{
    FitResult r;
    const int length = std::max(1, o.rowTicks);
    std::sort(onsets.begin(), onsets.end());
    onsets.erase(std::unique(onsets.begin(), onsets.end()), onsets.end());
    onsets.erase(std::remove_if(onsets.begin(), onsets.end(), [length](int t) { return t < 0 || t >= length; }), onsets.end());

    // Every way of laying the row competes on one score: one exact grid, two
    // grids joined by a G, the nearest grid with its moves counted, and the
    // onsets' own gaps when three or more notes would otherwise move.
    struct Choice { bool ok = false; double score = 1e9; std::vector<FitSegment> segs; bool exact = true; int moved = 0; int maxMove = 0; };
    Choice best;
    auto offer = [&](const Choice& c) { if (c.ok && c.score < best.score) best = c; };

    if (SegFit single = bestSimple(onsets, length, o, false); single.ok) {
        Choice c; c.ok = true; c.score = single.score; single.seg.start = 0; c.segs = { single.seg };
        offer(c);
    }
    for (size_t k = 1; k < onsets.size(); ++k) {
        const std::vector<int> a(onsets.begin(), onsets.begin() + long(k));
        for (const auto& cand : candidates()) {
            std::vector<int> starts; FitSegment probe;
            if (!layGrid(cand.entries, length, o.maxSteps, starts, probe)) continue;
            int boundary = -1;
            for (int st : starts) if (st <= onsets[k]) boundary = st;
            if (boundary <= a.back()) continue;                 // the first part's last note needs its step
            SegFit fa = fitSegment(a, boundary, cand, o, false);
            if (!fa.ok) continue;
            std::vector<int> b;
            for (size_t i = k; i < onsets.size(); ++i) b.push_back(onsets[i] - boundary);
            FitOptions ob = o; ob.sticky = nullptr; ob.maxSteps = o.maxSteps - fa.seg.steps;
            SegFit fb = bestSimple(b, length - boundary, ob, false);
            if (!fb.ok) continue;
            Choice c; c.ok = true; c.score = fa.score + fb.score + 2.0;   // the G, and the row that reads in two grids
            fa.seg.start = 0; fb.seg.start = boundary; c.segs = { fa.seg, fb.seg };
            offer(c);
        }
    }
    if (SegFit snap = bestSimple(onsets, length, o, true); snap.ok) {
        Choice c; c.ok = true; c.score = snap.score; snap.seg.start = 0; c.segs = { snap.seg };
        c.exact = snap.moved == 0; c.moved = snap.moved; c.maxMove = snap.maxMove;
        offer(c);
        // The onsets' own gaps: for three notes that would move, or two when
        // they are a quarter of the row -- never for one clumsy note.
        if (snap.moved >= 3 || (snap.moved >= 2 && snap.moved * 4 >= int(onsets.size())))
            if (SegFit custom = fitCustom(onsets, length, o); custom.ok) { Choice cc; cc.ok = true; cc.score = custom.score; custom.seg.start = 0; cc.segs = { custom.seg }; offer(cc); }
    }
    if (best.ok) {
        r.segments = best.segs; r.score = best.score; r.fits = best.exact; r.moved = best.moved; r.maxMove = best.maxMove;
        return r;
    }
    // Too many notes for any grid: straight, the extras counted as moved.
    FitSegment s; std::vector<int> starts;
    layGrid({ 6 }, length, o.maxSteps, starts, s);
    s.start = 0;
    r.segments = { s }; r.fits = false; r.moved = int(onsets.size()); r.score = 100.0;
    return r;
}

std::vector<int> layoutSteps(const FitResult& r)
{
    std::vector<int> out;
    for (const auto& s : r.segments) {
        int at = s.start;
        for (int k = 0; k < s.steps; ++k) { out.push_back(at); at += s.ticks[size_t(k % std::max(1, s.length))]; }
    }
    return out;
}

int stepNearTick(const FitResult& r, int tick, int* stepTick)
{
    const auto steps = layoutSteps(r);
    int best = -1, bestD = 1 << 30;
    for (size_t k = 0; k < steps.size(); ++k) { const int d = std::abs(steps[k] - tick); if (d < bestD) { bestD = d; best = int(k); } }
    if (stepTick != nullptr && best >= 0) *stepTick = steps[size_t(best)];
    return best;
}

} // namespace chipboy::tracker
