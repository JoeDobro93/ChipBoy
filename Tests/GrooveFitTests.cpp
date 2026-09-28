// ChipBoy -- grooves inferred from what is played (docs/plan-groove-inference.md,
// docs/COMMANDS_AND_TEMPO.md section 226): the cases, as onset ticks.
#include "core/Tracker/GrooveFit.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

using namespace chipboy;
using namespace chipboy::tracker;

namespace {
std::vector<int> every(int step, int count, int from = 0) { std::vector<int> v; for (int i = 0; i < count; ++i) v.push_back(from + i * step); return v; }
Groove groove(std::vector<int> t) { Groove g; g.ticks.fill(0); for (size_t i = 0; i < t.size(); ++i) g.ticks[i] = uint8_t(t[i]); return g; }
std::array<Groove, kGrooveSlots> slots() { return factoryGrooves(); }   // 7 5, 8 4, 5 7, 9 3, 4 4 4 and straight
}

TEST_CASE("groove fit: straight playing is straight, no slot, one segment", "[groovefit]")
{
    const auto s = slots();
    FitOptions o; o.slots = &s;
    const auto r = fitRow(every(6, 16), o);
    REQUIRE(r.segments.size() == 1);
    CHECK(r.fits);
    CHECK(r.segments[0].order == 0);
    CHECK(r.segments[0].steps == 16);
    CHECK(sameGroove(r.segments[0].groove(), groove({ 6 })));
    CHECK_FALSE(r.segments[0].fresh);
}

TEST_CASE("groove fit: a shuffled bar with rests is 8 4, and stays 8 4 across a bar of downbeats", "[groovefit]")
{
    const auto s = slots();
    FitOptions o; o.slots = &s;
    const auto r = fitRow({ 0, 8, 12, 24, 32, 36, 48, 56, 60, 72, 80, 84 }, o);
    REQUIRE(r.segments.size() == 1);
    CHECK(r.fits);
    CHECK(sameGroove(r.segments[0].groove(), groove({ 8, 4 })));
    CHECK(r.segments[0].steps == 16);
    // The next bar, downbeats alone: straight fits too, but the shuffle sticks.
    const Groove sticky = groove({ 8, 4 });
    o.sticky = &sticky;
    const auto r2 = fitRow({ 0, 12, 24, 36, 48, 60, 72, 84 }, o);
    REQUIRE(r2.segments.size() == 1);
    CHECK(sameGroove(r2.segments[0].groove(), groove({ 8, 4 })));
    // Offbeats back on 6: the shuffle breaks.
    const auto r3 = fitRow(every(6, 16), o);
    CHECK(sameGroove(r3.segments[0].groove(), groove({ 6 })));
}

TEST_CASE("groove fit: drifting swing is written as it was played", "[groovefit]")
{
    const auto s = slots();
    FitOptions o; o.slots = &s;
    const std::vector<int> on = { 0, 7, 12, 20, 24, 32, 36, 45, 48, 56, 60, 68, 72, 80, 84, 93 };   // offbeats at 7, 8, 8, 9
    const auto r = fitRow(on, o);
    CHECK(r.fits);
    CHECK(r.moved == 0);
    // Every note is on a step of its own, whatever grooves say it.
    const auto steps = layoutSteps(r);
    for (int t : on) CHECK(std::find(steps.begin(), steps.end(), t) != steps.end());
}

TEST_CASE("groove fit: triplets are 8, sixteenth triplets 4, thirty-seconds 3", "[groovefit]")
{
    const auto s = slots();
    FitOptions o; o.slots = &s;
    auto r = fitRow(every(8, 12), o);
    CHECK(sameGroove(r.segments[0].groove(), groove({ 8 })));
    CHECK(r.segments[0].steps == 12);
    CHECK(r.segments[0].fresh);                       // not a factory slot
    r = fitRow(every(4, 24), o);
    CHECK(sameGroove(r.segments[0].groove(), groove({ 4 })));
    CHECK(r.segments[0].steps == 24);
    r = fitRow(every(3, 32), o);
    CHECK(sameGroove(r.segments[0].groove(), groove({ 3 })));
    CHECK(r.segments[0].steps == 32);
}

TEST_CASE("groove fit: a fast partial bar splits at a G, and the row keeps its length", "[groovefit]")
{
    const auto s = slots();
    FitOptions o; o.slots = &s;
    std::vector<int> on = every(6, 12);
    for (int t : every(3, 8, 72)) on.push_back(t);
    const auto r = fitRow(on, o);
    REQUIRE(r.segments.size() == 2);
    CHECK(r.fits);
    CHECK(sameGroove(r.segments[0].groove(), groove({ 6 })));
    CHECK(r.segments[0].steps == 12);
    CHECK(r.segments[1].start == 72);
    CHECK(sameGroove(r.segments[1].groove(), groove({ 3 })));
    CHECK(r.segments[1].steps == 8);
    CHECK(r.totalSteps() == 20);
    const auto steps = layoutSteps(r);
    REQUIRE(steps.size() == 20);
    CHECK(steps[12] == 72);
    CHECK(steps[19] == 93);
}

TEST_CASE("groove fit: a run that starts mid-bar then straight again is two segments the other way", "[groovefit]")
{
    const auto s = slots();
    FitOptions o; o.slots = &s;
    std::vector<int> on = every(3, 8);                     // 0..21 fast
    for (int t : every(6, 12, 24)) on.push_back(t);        // 24..90 straight
    const auto r = fitRow(on, o);
    REQUIRE(r.segments.size() == 2);
    CHECK(sameGroove(r.segments[0].groove(), groove({ 3 })));
    CHECK(r.segments[0].steps == 8);
    CHECK(r.segments[1].start == 24);
    CHECK(sameGroove(r.segments[1].groove(), groove({ 6 })));
    CHECK(r.segments[1].steps == 12);
}

TEST_CASE("groove fit: a remainder goes to the last step, written out", "[groovefit]")
{
    const auto s = slots();
    FitOptions o; o.slots = &s;
    // Straight for 66 ticks, then triplet eighths over the last 30: 8 8 8 6.
    std::vector<int> on = every(6, 11);
    for (int t : { 66, 74, 82, 90 }) on.push_back(t);
    const auto r = fitRow(on, o);
    REQUIRE(r.segments.size() == 2);
    CHECK(r.segments[1].start == 66);
    CHECK(r.segments[1].length == 4);
    CHECK(r.segments[1].ticks[0] == 8); CHECK(r.segments[1].ticks[3] == 6);
    CHECK(r.totalSteps() == 15);
    const auto steps = layoutSteps(r);
    CHECK(steps.back() == 90);
}

TEST_CASE("groove fit: one note four ticks late is literal: the bar's own gaps", "[groovefit]")
{
    const auto s = slots();
    FitOptions o; o.slots = &s;
    std::vector<int> on = every(6, 16);
    on[6] = 40;                                            // four ticks late
    const auto r = fitRow(on, o);
    REQUIRE(r.segments.size() == 1);
    CHECK(r.fits);
    CHECK(r.segments[0].order == 8);
    CHECK(layoutSteps(r) == on);
    CHECK(sameGroove(r.segments[0].groove(), groove({ 6, 6, 6, 6, 6, 10, 2, 6, 6, 6, 6, 6, 6, 6, 6, 6 })));
}

TEST_CASE("groove fit: irregular notes are exact, the onsets' own gaps as the groove", "[groovefit]")
{
    const auto s = slots();
    FitOptions o; o.slots = &s;
    const std::vector<int> on = { 0, 5, 17, 40, 61, 70, 92 };
    const auto r = fitRow(on, o);
    REQUIRE(r.segments.size() == 1);
    CHECK(r.fits);
    CHECK(r.segments[0].order == 8);
    CHECK(layoutSteps(r) == on);
    CHECK(r.segments[0].steps == 7);
}

TEST_CASE("groove fit: a row no groove can say moves the fewest notes and says so", "[groovefit]")
{
    const auto s = slots();
    FitOptions o; o.slots = &s;
    // Forty onsets with no pattern: more gaps than two grooves hold.
    std::vector<int> on;
    for (int t = 0, i = 0; t < 96 && int(on.size()) < 40; ++i) { on.push_back(t); t += 1 + (i * 7) % 3; }
    const auto r = fitRow(on, o);
    CHECK_FALSE(r.fits);
    CHECK(r.moved > 0);
    CHECK(r.moved < int(on.size()));
    CHECK(r.segments.size() == 1);
}

TEST_CASE("groove fit: an empty row and a single note are straight, or the groove in force", "[groovefit]")
{
    const auto s = slots();
    FitOptions o; o.slots = &s;
    auto r = fitRow({}, o);
    CHECK(sameGroove(r.segments[0].groove(), groove({ 6 })));
    CHECK(r.segments[0].steps == 16);
    const Groove sticky = groove({ 7, 5 });
    o.sticky = &sticky;
    r = fitRow({ 24 }, o);
    CHECK(sameGroove(r.segments[0].groove(), groove({ 7, 5 })));
}

TEST_CASE("groove fit: a late sixteenth is literal, not nudged", "[groovefit]")
{
    const auto s = slots();
    FitOptions o; o.slots = &s;
    std::vector<int> on = every(6, 16);
    on[3] = 19;
    const auto r = fitRow(on, o);
    CHECK(r.fits);
    CHECK(layoutSteps(r) == on);
    int st = 0;
    CHECK(stepNearTick(r, 19, &st) == 3);
    CHECK(st == 19);
}

TEST_CASE("groove fit: swung thirty-seconds and a sparse row", "[groovefit]")
{
    const auto s = slots();
    FitOptions o; o.slots = &s;
    std::vector<int> on;
    for (int i = 0; i < 16; ++i) { on.push_back(i * 6); on.push_back(i * 6 + 4); }
    auto r = fitRow(on, o);
    CHECK(sameGroove(r.segments[0].groove(), groove({ 4, 2 })));
    CHECK(r.segments[0].steps == 32);
    r = fitRow({ 0, 24, 48, 72 }, o);                     // quarters: straight fits, and is the simplest
    CHECK(sameGroove(r.segments[0].groove(), groove({ 6 })));
}
