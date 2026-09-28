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

struct SegFit { bool ok = false; FitSegment seg; int moved = 0; int maxMove = 0; double score = 0.0; };

double segScore(const FitSegment& seg, const FitOptions& o, int notes, bool& fresh)
{
    const bool change = o.sticky != nullptr && !sameGroove(*o.sticky, seg.groove());
    fresh = !inSlots(seg, o) && (o.sticky == nullptr || change);   // what is in force has a slot
    // The rank, a new slot, a change from the groove in force, and steps
    // nothing lands on: a finer grid that fits by being fine is not simpler
    // than a coarser one that fits by being right (eight empty steps of a `3`
    // outweigh a `G` and a change of groove).
    const double empty = 0.45 * double(std::max(0, seg.steps - notes));
    return double(seg.order) + (fresh ? 1.0 : 0.0) + (change ? 1.5 : 0.0) + empty;
}

/// One candidate over one segment's onsets (relative ticks): every onset on
/// a step of its own, exactly. `snap` accepts any distance and counts the
/// moves instead, for a row no groove can say.
SegFit fitSegment(const std::vector<int>& onsets, int length, const Candidate& c, const FitOptions& o, bool snap)
{
    SegFit f;
    std::vector<int> starts;
    if (!layGrid(c.entries, length, o.maxSteps, starts, f.seg)) return f;
    f.seg.order = c.order;
    // Notes take steps closest pair first, so when snapping a note a little
    // off never pushes an exact one off its own step.
    std::vector<char> taken(starts.size(), 0), placed(onsets.size(), 0);
    double moveCost = 0.0;
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
        if (bestD > 0) { if (!snap) return f; ++f.moved; f.maxMove = std::max(f.maxMove, bestD); moveCost += double(bestD) + 0.5; }
        taken[size_t(bestStep)] = 1; placed[size_t(bestOn)] = 1;
    }
    f.ok = true;
    f.score = segScore(f.seg, o, int(onsets.size()), f.seg.fresh) + moveCost;
    return f;
}

/// The row's own gaps as the entries, rests where a gap is longer than a
/// step may be: exact by construction, ranked last.
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
    f.ok = true;
    f.score = segScore(f.seg, o, int(onsets.size()), f.seg.fresh);
    return f;
}

/// The best exact layout of one segment: a candidate, or the gaps.
SegFit bestExact(const std::vector<int>& onsets, int length, const FitOptions& o)
{
    SegFit best;
    for (const auto& c : candidates()) {
        SegFit f = fitSegment(onsets, length, c, o, false);
        if (f.ok && (!best.ok || f.score < best.score)) best = f;
    }
    if (SegFit f = fitCustom(onsets, length, o); f.ok && (!best.ok || f.score < best.score)) best = f;
    return best;
}

SegFit bestSnap(const std::vector<int>& onsets, int length, const FitOptions& o)
{
    SegFit best;
    for (const auto& c : candidates()) {
        SegFit f = fitSegment(onsets, length, c, o, true);
        if (f.ok && (!best.ok || f.score < best.score)) best = f;
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

    // Every exact way of laying the row competes on one score: one grid, or
    // two joined by a G, each a candidate or the segment's own gaps.
    struct Choice { bool ok = false; double score = 1e9; std::vector<FitSegment> segs; };
    Choice best;
    auto offer = [&](const Choice& c) { if (c.ok && c.score < best.score) best = c; };

    if (SegFit single = bestExact(onsets, length, o); single.ok) {
        Choice c; c.ok = true; c.score = single.score; single.seg.start = 0; c.segs = { single.seg };
        offer(c);
    }
    for (size_t k = 1; k < onsets.size(); ++k) {
        const std::vector<int> a(onsets.begin(), onsets.begin() + long(k));
        // The first part under a candidate, split at the last step of its grid
        // before the second part's first note; or under its own gaps, split at
        // that note.
        std::vector<std::pair<int, SegFit>> firsts;
        for (const auto& cand : candidates()) {
            std::vector<int> starts; FitSegment probe;
            if (!layGrid(cand.entries, length, o.maxSteps, starts, probe)) continue;
            int boundary = -1;
            for (int st : starts) if (st <= onsets[k]) boundary = st;
            if (boundary <= a.back()) continue;                 // the first part's last note needs its step
            SegFit fa = fitSegment(a, boundary, cand, o, false);
            if (fa.ok) firsts.emplace_back(boundary, fa);
        }
        if (SegFit fa = fitCustom(a, onsets[k], o); fa.ok) firsts.emplace_back(onsets[k], fa);
        for (auto& [boundary, fa] : firsts) {
            std::vector<int> b;
            for (size_t i = k; i < onsets.size(); ++i) b.push_back(onsets[i] - boundary);
            FitOptions ob = o; ob.sticky = nullptr; ob.maxSteps = o.maxSteps - fa.seg.steps;
            SegFit fb = bestExact(b, length - boundary, ob);
            if (!fb.ok) continue;
            Choice c; c.ok = true; c.score = fa.score + fb.score + 2.0;   // the G, and the row that reads in two grids
            fa.seg.start = 0; fb.seg.start = boundary; c.segs = { fa.seg, fb.seg };
            offer(c);
        }
    }
    if (best.ok) { r.segments = best.segs; r.score = best.score; r.fits = true; return r; }
    // No groove says this row: the nearest grid, its moves counted.
    if (SegFit snap = bestSnap(onsets, length, o); snap.ok) {
        snap.seg.start = 0;
        r.segments = { snap.seg }; r.score = snap.score; r.fits = false; r.moved = snap.moved; r.maxMove = snap.maxMove;
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
