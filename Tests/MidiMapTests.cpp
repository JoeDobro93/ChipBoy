// ChipBoy -- the MIDI map's data laws (docs/plan-midi-map.md,
// docs/COMMANDS_AND_TEMPO.md section 225).
#include "core/Tracker/MidiMap.h"

#include <catch2/catch_test_macros.hpp>

using namespace chipboy;
using namespace chipboy::tracker;

TEST_CASE("MIDI map: normalising orders the regions, pins the first at 1 and caps them", "[midimap]")
{
    MidiMap m;
    auto& c = m.channels[1];
    c.target = 2;
    c.regions = { MidiRegion{ 65, 2, 0, 0, 0, {}, {} }, MidiRegion{ 30, 0, 0, 0, 0, { bank::Cmd::V, 4, 6, 0 }, {} } };
    normalizeMidiMap(m);
    REQUIRE(c.regions.size() == 2);
    CHECK(c.regions[0].from == 1);                 // the lowest region starts at 1 whatever it said
    CHECK(c.regions[0].cmd1.cmd == bank::Cmd::V);
    CHECK(c.regions[1].from == 65);
    CHECK(c.regions[1].inst == 2);

    c.regions.clear();
    normalizeMidiMap(m);
    REQUIRE(c.regions.size() == 1);                // never none
    CHECK(c.regions[0].from == 1);

    c.regions = { MidiRegion{ 1, 1 }, MidiRegion{ 40, 2 }, MidiRegion{ 40, 3 } };
    normalizeMidiMap(m);
    REQUIRE(c.regions.size() == 2);                // two at one velocity: the later kept
    CHECK(c.regions[1].inst == 3);

    c.regions.clear();
    for (int i = 0; i < 130; ++i) c.regions.push_back(MidiRegion{ uint8_t(1 + i % 127), uint8_t(1 + i % 100) });
    normalizeMidiMap(m);
    CHECK(c.regions.size() == size_t(kMaxRegions));          // 127: a region a velocity, no more

    m.channels[5].target = 9;
    normalizeMidiMap(m);
    CHECK(m.channels[5].target == -1);
}

TEST_CASE("MIDI map: a velocity lands in the last region at or below it", "[midimap]")
{
    MidiChannelMap c;
    c.regions = { MidiRegion{ 1, 1 }, MidiRegion{ 65, 2 }, MidiRegion{ 100, 3 } };
    CHECK(regionFor(c, 1).inst == 1);
    CHECK(regionFor(c, 64).inst == 1);
    CHECK(regionFor(c, 65).inst == 2);
    CHECK(regionFor(c, 99).inst == 2);
    CHECK(regionFor(c, 100).inst == 3);
    CHECK(regionFor(c, 127).inst == 3);
}

TEST_CASE("MIDI map: H, G and T are not a region's to carry", "[midimap]")
{
    CHECK_FALSE(midiCommandAllowed(bank::Cmd::H));
    CHECK_FALSE(midiCommandAllowed(bank::Cmd::G));
    CHECK_FALSE(midiCommandAllowed(bank::Cmd::T));
    CHECK(midiCommandAllowed(bank::Cmd::E));
    CHECK(midiCommandAllowed(bank::Cmd::K));
    CHECK(midiCommandAllowed(bank::Cmd::B));
    CHECK(midiRegionCommand({ bank::Cmd::H, 1, 0, 0 }).cmd == bank::Cmd::None);
    CHECK(midiRegionCommand({ bank::Cmd::V, 4, 6, 0 }).cmd == bank::Cmd::V);
    MidiMap m;
    m.channels[0].regions.front().cmd2 = { bank::Cmd::T, 120, 0, 0 };
    normalizeMidiMap(m);
    CHECK(m.channels[0].regions.front().cmd2.cmd == bank::Cmd::None);
}

TEST_CASE("MIDI map: a fresh map is the default the file leaves out", "[midimap]")
{
    MidiMap m;
    CHECK(midiMapIsDefault(m));
    m.on = true;
    CHECK_FALSE(midiMapIsDefault(m));
    m.on = false; m.channels[3].target = 0;
    CHECK_FALSE(midiMapIsDefault(m));
    m.channels[3].target = -1; m.channels[3].regions.front().inst = 4;
    CHECK_FALSE(midiMapIsDefault(m));
    m.channels[3].regions.front().inst = 0; m.channels[3].regions.front().transpose = -12;
    CHECK_FALSE(midiMapIsDefault(m));
}
