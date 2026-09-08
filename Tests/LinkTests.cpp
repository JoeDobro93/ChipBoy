// ChipBoy -- the link region's plain-data parts (spec sections 11.3, 11.8).
#include "core/Link/LinkLayout.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

using namespace chipboy;
using namespace chipboy::link;

TEST_CASE("the region is plain data of a fixed size", "[link]")
{
    // Anything that changes the layout must bump kVersion; this pins the
    // pieces the two plugins agree on.
    STATIC_REQUIRE(std::is_trivially_copyable_v<LinkEvent>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<ScopeSample>);
    STATIC_REQUIRE(sizeof(ScopeSample) == 16);
    STATIC_REQUIRE(std::is_standard_layout_v<InstanceRegion>);
    CHECK(sizeof(InstanceRegion) > sizeof(ChannelSlot) * 4);
}

TEST_CASE("a corrupted or truncated region is refused", "[link]")
{
    std::vector<uint8_t> bytes(sizeof(InstanceRegion), 0);
    auto* r = reinterpret_cast<InstanceRegion*>(bytes.data());
    CHECK_FALSE(regionLooksValid(r, bytes.size()));          // zeroed: no magic
    r->magic = kMagic; r->version = kVersion; r->size = uint32_t(sizeof(InstanceRegion));
    CHECK(regionLooksValid(r, bytes.size()));
    CHECK_FALSE(regionLooksValid(r, bytes.size() - 1));      // truncated file
    r->version = kVersion + 1;
    CHECK_FALSE(regionLooksValid(r, bytes.size()));          // another build's layout
    r->version = kVersion; r->size = 12;
    CHECK_FALSE(regionLooksValid(r, bytes.size()));
}

TEST_CASE("strings from a region are never trusted to terminate", "[link]")
{
    char field[8];
    std::memset(field, 'x', sizeof(field));                  // no terminator at all
    char out[8];
    safeString(field, 8, out, 8);
    CHECK(std::strlen(out) == 7);
    setString(field, 8, "a much longer name than fits");
    CHECK(std::strlen(field) == 7);
    CHECK(field[7] == 0);
}

TEST_CASE("the scope ring hands back the newest samples in order", "[link]")
{
    ScopeRing ring;
    for (uint32_t i = 0; i < kScopeRing + 100; ++i) ring.push(i * 10, int(i & 15));
    ScopeSample out[64];
    const uint32_t n = ring.snapshot(out, 64);
    REQUIRE(n == 64);
    CHECK(out[63].cycle == (kScopeRing + 99) * 10);
    CHECK(out[0].cycle == (kScopeRing + 99 - 63) * 10);
    for (uint32_t i = 1; i < n; ++i) CHECK(out[i].cycle > out[i - 1].cycle);
}

TEST_CASE("snapshots round-trip and report an interrupted write", "[link]")
{
    Snapshot<driver::ChannelParams> snap;
    driver::ChannelParams p; p.instrument = 42; p.transpose = -12; p.liveFollow = true;
    snap.write(p);
    driver::ChannelParams q;
    REQUIRE(snap.read(q));
    CHECK(q.instrument == 42); CHECK(q.transpose == -12); CHECK(q.liveFollow);
    snap.seq.store(snap.seq.load() + 1);                      // a writer died mid-write
    CHECK_FALSE(snap.read(q));
}

TEST_CASE("packed channel state survives the trip", "[link]")
{
    driver::VoiceView v;
    v.active = true; v.dacOn = true; v.outOfRange = false; v.volume = 9; v.note = 67; v.instrument = 12;
    v.regs[0] = 0x80; v.regs[1] = 0x3F; v.regs[2] = 0xF3; v.regs[3] = 0xC1; v.regs[4] = 0x87;
    driver::VoiceView back;
    unpackState(packState(v), back);
    CHECK(back.active); CHECK(back.dacOn); CHECK_FALSE(back.outOfRange);
    CHECK(back.volume == 9); CHECK(back.note == 67); CHECK(back.instrument == 12);
    for (int i = 0; i < 5; ++i) CHECK(back.regs[i] == v.regs[i]);
    CHECK(back.period == 0x7C1);
}

TEST_CASE("the running state survives the second word", "[link]")
{
    // docs/COMMANDS_AND_TEMPO.md section 3: what the slots did, next to the
    // registers. A layout change here is what the version bump is for.
    CHECK(kVersion == 3);
    driver::VoiceView v;
    v.envVol = 11; v.envRate = 5; v.envDir = 1;
    v.vibSpeed = 3; v.vibDepth = 7;
    v.pitchOffset = -37;
    v.pan = 2; v.duty = 3; v.frame = 9;
    v.tableSlot = 64; v.tableStep = 12;
    v.groove = 4;
    driver::VoiceView back;
    unpackState2(packState2(v), back);
    CHECK(back.envVol == 11); CHECK(back.envRate == 5); CHECK(back.envDir == 1);
    CHECK(back.vibSpeed == 3); CHECK(back.vibDepth == 7);
    CHECK(back.pitchOffset == -37);
    CHECK(back.pan == 2); CHECK(back.duty == 3); CHECK(back.frame == 9);
    CHECK(back.tableSlot == 64); CHECK(back.tableStep == 12);
    CHECK(back.groove == 4);
    driver::VoiceView none;
    none.groove = driver::VoiceView::kNoGroove;
    unpackState2(packState2(none), back);
    CHECK(back.groove == driver::VoiceView::kNoGroove);
}

TEST_CASE("the channel a Voice sends is the new one", "[link]")
{
    // A Voice sends its whole channel through the region: two command slots
    // and no override lanes (section 3).
    Snapshot<driver::ChannelParams> snap;
    driver::ChannelParams p;
    p.instrument = 9; p.table = 3; p.level = 12; p.pan = 1; p.transpose = -5;
    p.cmd[0] = { bank::Cmd::E, 12, 11, 0 };
    p.cmd[1] = { bank::Cmd::W, 2, 0, 0 };
    snap.write(p);
    driver::ChannelParams q;
    REQUIRE(snap.read(q));
    CHECK(q.instrument == 9); CHECK(q.table == 3); CHECK(q.transpose == -5);
    CHECK(q.cmd[0].cmd == bank::Cmd::E); CHECK(q.cmd[0].a == 12); CHECK(q.cmd[0].b == 11);
    CHECK(q.cmd[1].cmd == bank::Cmd::W);
}

TEST_CASE("heartbeats age out", "[link]")
{
    CHECK(fresh(1000, 1000 + kHeartbeatStaleMs - 1));
    CHECK_FALSE(fresh(1000, 1000 + kHeartbeatStaleMs));
    CHECK_FALSE(fresh(0, 5000));
    CHECK_FALSE(fresh(6000, 5000));                           // from the future: a clock jump, treat as dead
}
