// ChipBoy -- instrument presets: the walk and the placement
// (docs/COMMANDS_AND_TEMPO.md section 15).
#include "core/Bank/Preset.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace chipboy;
using namespace chipboy::bank;

namespace {

/// A bank is 41 KB: on the heap, as everything that holds one is (CLAUDE.md).
std::unique_ptr<Bank> emptyBank() { return std::unique_ptr<Bank>(new Bank(Bank::empty())); }

Table namedTable(const char* name)
{
    Table t;
    t.used = true;
    t.name = name;
    t.steps[0].vol = 10;
    return t;
}

Wave namedWave(const char* name, uint8_t fill)
{
    Wave w;
    w.used = true;
    w.name = name;
    Frame f;
    f.s.fill(fill);
    w.frames.push_back(f);
    return w;
}

} // namespace

TEST_CASE("a preset collects what its instrument references, transitively", "[preset]")
{
    const auto owned = emptyBank();
    Bank& b = *owned;
    // A wave instrument on table 3, whose A starts table 7, whose W selects
    // wave 9; the instrument itself plays wave 4.
    b.instruments[0] = Instrument::defaults(InstrumentType::Wave, "Lead");
    b.instruments[0].used = true;
    b.instruments[0].table = 3;
    b.instruments[0].wave = 4;
    b.tables[2] = namedTable("first");
    b.tables[2].steps[1].cmd1 = { Cmd::A, 7, 0, 0 };
    b.tables[6] = namedTable("second");
    b.tables[6].steps[0].cmd2 = { Cmd::W, 9, 0, 0 };
    b.waves[3] = namedWave("four", 4);
    b.waves[8] = namedWave("nine", 9);

    const Preset p = collectPreset(b, 1);
    REQUIRE(p.instrument.used);
    CHECK(p.instrument.name == "Lead");
    REQUIRE(p.tables.size() == 2);
    CHECK(p.tables[0].first == 3);
    CHECK(p.tables[1].first == 7);            // the table the A command starts
    REQUIRE(p.waves.size() == 2);
    CHECK(p.waves[0].first == 9);             // the table's W
    CHECK(p.waves[1].first == 4);             // the instrument's own
    CHECK(p.kits.empty());

    // A table's W is a duty on a pulse and names no wave.
    b.instruments[0].type = InstrumentType::Pulse;
    const Preset pulse = collectPreset(b, 1);
    CHECK(pulse.tables.size() == 2);
    CHECK(pulse.waves.empty());

    // A cycle between tables is walked once, not for ever.
    b.tables[6].steps[1].cmd1 = { Cmd::A, 3, 0, 0 };
    CHECK(collectPreset(b, 1).tables.size() == 2);

    // A kit instrument brings its kit.
    b.instruments[1] = Instrument::defaults(InstrumentType::Kit, "Drums");
    b.instruments[1].used = true;
    b.instruments[1].table = 0;
    b.instruments[1].kit = 2;
    b.kits[1].used = true;
    b.kits[1].name = "kit two";
    const Preset kit = collectPreset(b, 2);
    CHECK(kit.tables.empty());
    REQUIRE(kit.kits.size() == 1);
    CHECK(kit.kits[0].first == 2);

    // An empty slot collects nothing.
    CHECK_FALSE(collectPreset(b, 100).instrument.used);
}

TEST_CASE("placing a preset renumbers every reference", "[preset]")
{
    const auto srcOwned = emptyBank();
    Bank& src = *srcOwned;
    src.instruments[0] = Instrument::defaults(InstrumentType::Wave, "Lead");
    src.instruments[0].used = true;
    src.instruments[0].table = 3;
    src.instruments[0].wave = 4;
    src.tables[2] = namedTable("first");
    src.tables[2].steps[1].cmd1 = { Cmd::A, 7, 0, 0 };
    src.tables[6] = namedTable("second");
    src.waves[3] = namedWave("four", 4);
    const Preset p = collectPreset(src, 1);

    // Into a bank whose first slots are taken: everything moves down, and the
    // A command follows its table.
    const auto dstOwned = emptyBank();
    Bank& dst = *dstOwned;
    dst.tables[0] = namedTable("in the way");
    dst.waves[0] = namedWave("in the way", 1);
    PlaceReport report;
    REQUIRE(placePreset(dst, p, 20, report));
    CHECK(report.ok);
    CHECK(report.instrumentSlot == 20);
    REQUIRE(report.moves.size() == 3);
    CHECK(dst.instruments[19].used);
    CHECK(dst.instruments[19].name == "Lead");
    CHECK(dst.instruments[19].table == 2);            // table 3 landed in slot 2
    CHECK(dst.instruments[19].wave == 2);
    REQUIRE(dst.tables[1].used);
    CHECK(dst.tables[1].name == "first");
    CHECK(dst.tables[2].name == "second");
    CHECK(dst.tables[1].steps[1].cmd1.a == 3);        // the A points at slot 3 now
    CHECK(dst.waves[1].name == "four");
}

TEST_CASE("placing a preset reuses an identical table, wave or kit", "[preset]")
{
    const auto srcOwned = emptyBank();
    Bank& src = *srcOwned;
    src.instruments[0] = Instrument::defaults(InstrumentType::Wave, "Lead");
    src.instruments[0].used = true;
    src.instruments[0].table = 1;
    src.instruments[0].wave = 1;
    src.tables[0] = namedTable("shared");
    src.waves[0] = namedWave("shared", 7);
    const Preset p = collectPreset(src, 1);

    const auto dstOwned = emptyBank();
    Bank& dst = *dstOwned;
    dst.tables[4] = namedTable("shared");             // the same content, slot 5
    dst.waves[5] = namedWave("shared", 7);            // slot 6
    PlaceReport report;
    REQUIRE(placePreset(dst, p, 1, report));
    CHECK(dst.instruments[0].table == 5);
    CHECK(dst.instruments[0].wave == 6);
    CHECK_FALSE(dst.tables[0].used);                  // nothing new was written
    REQUIRE(report.moves.size() == 2);
    CHECK(report.moves[0].reused);
    CHECK(report.moves[0].to == 5);
    CHECK(report.moves[1].reused);

    // A table renamed is a different table: it takes a slot of its own.
    const auto otherOwned = emptyBank();
    Bank& other = *otherOwned;
    other.tables[4] = namedTable("not the same name");
    other.waves[5] = namedWave("shared", 7);
    REQUIRE(placePreset(other, p, 1, report));
    CHECK(other.instruments[0].table == 1);
    CHECK(other.tables[0].used);
    CHECK_FALSE(report.moves[0].reused);
}

TEST_CASE("a preset that does not fit changes nothing", "[preset]")
{
    const auto srcOwned = emptyBank();
    Bank& src = *srcOwned;
    src.instruments[0] = Instrument::defaults(InstrumentType::Pulse, "Lead");
    src.instruments[0].used = true;
    src.instruments[0].table = 1;
    src.tables[0] = namedTable("wanted");
    const Preset p = collectPreset(src, 1);

    const auto dstOwned = emptyBank();
    Bank& dst = *dstOwned;
    for (int i = 0; i < kTableSlots; ++i) dst.tables[size_t(i)] = namedTable("full");
    PlaceReport report;
    CHECK_FALSE(placePreset(dst, p, 5, report));
    CHECK_FALSE(report.ok);
    REQUIRE(report.error != nullptr);
    CHECK_FALSE(dst.instruments[4].used);             // the bank is exactly as it was

    // And an instrument slot out of range is refused too.
    CHECK_FALSE(placePreset(dst, p, 0, report));
    CHECK_FALSE(placePreset(dst, p, kInstrumentSlots + 1, report));
}
