#include "core/Bank/Preset.h"

#include <algorithm>
#include <array>

namespace chipboy::bank {

namespace {

bool sameStep(const TableStep& a, const TableStep& b)
{
    return a.vol == b.vol && a.hasTranspose == b.hasTranspose && a.transpose == b.transpose
           && sameCmd(a.cmd1, b.cmd1) && sameCmd(a.cmd2, b.cmd2);
}

/// The slot a command names, or 0: A selects a table, W a wave on the wave
/// channel (on a pulse it is the duty and names nothing). A revert form
/// carries no slot.
int tableRefOf(const Command& c) { return c.cmd == Cmd::A && !isRevert(c) && c.a >= 1 && c.a <= kTableSlots ? int(c.a) : 0; }
int waveRefOf(const Command& c) { return c.cmd == Cmd::W && !isRevert(c) && c.a >= 1 && c.a <= kWaveSlots ? int(c.a) : 0; }

/// The first slot nothing uses and nothing else in this preset has taken.
template <typename Array>
int freeSlot(const Array& slots, const std::vector<int>& taken)
{
    for (int i = 0; i < int(slots.size()); ++i) {
        if (slots[size_t(i)].used) continue;
        if (std::find(taken.begin(), taken.end(), i + 1) != taken.end()) continue;
        return i + 1;
    }
    return 0;
}

/// Where one dependency goes: an identical slot already in the bank, else the
/// first free one. Returns 0 when the kind is full.
template <typename Array, typename T, typename Same>
int placeOne(const Array& slots, const T& value, std::vector<int>& taken, bool& reused, Same same)
{
    for (int i = 0; i < int(slots.size()); ++i)
        if (slots[size_t(i)].used && same(slots[size_t(i)], value)) { reused = true; return i + 1; }
    reused = false;
    const int slot = freeSlot(slots, taken);
    if (slot) taken.push_back(slot);
    return slot;
}

int mapped(const std::vector<std::pair<int, int>>& m, int from)
{
    for (const auto& e : m) if (e.first == from) return e.second;
    return from;
}

} // namespace

bool sameTable(const Table& a, const Table& b)
{
    if (a.name != b.name || a.end != b.end || a.hopStep != b.hopStep) return false;
    for (size_t i = 0; i < a.steps.size(); ++i) if (!sameStep(a.steps[i], b.steps[i])) return false;
    return true;
}

bool sameWave(const Wave& a, const Wave& b)
{
    if (a.name != b.name || a.frames.size() != b.frames.size()) return false;
    for (size_t i = 0; i < a.frames.size(); ++i) if (a.frames[i].s != b.frames[i].s) return false;
    return true;
}

bool sameKit(const Kit& a, const Kit& b)
{
    if (a.name != b.name || a.period != b.period || a.loop != b.loop || a.samples.size() != b.samples.size()) return false;
    for (size_t i = 0; i < a.samples.size(); ++i) {
        const auto& x = a.samples[i];
        const auto& y = b.samples[i];
        if (x.name != y.name || x.note != y.note || x.loopPoint != y.loopPoint || x.data != y.data) return false;
    }
    return true;
}

Preset collectPreset(const Bank& b, int instrumentSlot)
{
    Preset p;
    const Instrument* inst = b.instrument(instrumentSlot);
    if (!inst) return p;
    p.instrument = *inst;

    // W means a wave slot on the wave channel and a duty on a pulse, so a
    // table's W is only a reference when the instrument plays waves.
    const bool wavesFromTables = inst->type == InstrumentType::Wave;

    auto haveTable = [&p](int slot) { for (const auto& t : p.tables) if (t.first == slot) return true; return false; };
    auto haveWave = [&p](int slot) { for (const auto& w : p.waves) if (w.first == slot) return true; return false; };
    auto addWave = [&](int slot) {
        if (slot < 1 || haveWave(slot)) return;
        if (const Wave* w = b.wave(slot)) p.waves.emplace_back(slot, *w);
    };

    // The tables, breadth first: a table an A command starts brings its own.
    std::vector<int> queue;
    if (inst->table) queue.push_back(inst->table);
    for (size_t at = 0; at < queue.size(); ++at) {
        const int slot = queue[at];
        if (haveTable(slot)) continue;
        const Table* t = b.table(slot);
        if (!t) continue;
        p.tables.emplace_back(slot, *t);
        for (const auto& step : t->steps)
            for (const Command* c : { &step.cmd1, &step.cmd2 }) {
                if (const int next = tableRefOf(*c); next && !haveTable(next)) queue.push_back(next);
                if (wavesFromTables) addWave(waveRefOf(*c));
            }
    }

    if (inst->type == InstrumentType::Wave) addWave(inst->wave);
    if (inst->type == InstrumentType::Kit)
        if (const Kit* k = b.kit(inst->kit)) p.kits.emplace_back(inst->kit, *k);
    return p;
}

bool placePreset(Bank& b, const Preset& p, int targetSlot, PlaceReport& report)
{
    report = PlaceReport{};
    if (targetSlot < 1 || targetSlot > kInstrumentSlots) { report.error = "the instrument slot is out of range"; return false; }
    if (!p.instrument.used) { report.error = "the preset holds no instrument"; return false; }

    // Work the placement out in full before touching the bank, so a preset
    // that does not fit leaves it exactly as it was.
    std::vector<std::pair<int, int>> tableMap, waveMap, kitMap;
    std::vector<int> tableTaken, waveTaken, kitTaken;
    std::vector<PlaceReport::Move> moves;
    bool reused = false;

    for (const auto& t : p.tables) {
        const int to = placeOne(b.tables, t.second, tableTaken, reused, sameTable);
        if (!to) { report.error = "the bank has no free table slot"; return false; }
        tableMap.emplace_back(t.first, to);
        moves.push_back({ PlaceReport::Kind::Table, t.first, to, reused });
    }
    for (const auto& w : p.waves) {
        const int to = placeOne(b.waves, w.second, waveTaken, reused, sameWave);
        if (!to) { report.error = "the bank has no free wave slot"; return false; }
        waveMap.emplace_back(w.first, to);
        moves.push_back({ PlaceReport::Kind::Wave, w.first, to, reused });
    }
    for (const auto& k : p.kits) {
        const int to = placeOne(b.kits, k.second, kitTaken, reused, sameKit);
        if (!to) { report.error = "the bank has no free kit slot"; return false; }
        kitMap.emplace_back(k.first, to);
        moves.push_back({ PlaceReport::Kind::Kit, k.first, to, reused });
    }

    // Commit: the copies go in renumbered, so every reference points at where
    // its dependency actually landed.
    const bool wavesFromTables = p.instrument.type == InstrumentType::Wave;
    for (const auto& t : p.tables) {
        const int to = mapped(tableMap, t.first);
        Table copy = t.second;
        for (auto& step : copy.steps)
            for (Command* c : { &step.cmd1, &step.cmd2 }) {
                if (const int ref = tableRefOf(*c)) c->a = int16_t(mapped(tableMap, ref));
                if (wavesFromTables) if (const int ref = waveRefOf(*c)) c->a = int16_t(mapped(waveMap, ref));
            }
        copy.used = true;
        b.tables[size_t(to - 1)] = std::move(copy);
    }
    for (const auto& w : p.waves) { Wave copy = w.second; copy.used = true; b.waves[size_t(mapped(waveMap, w.first) - 1)] = std::move(copy); }
    for (const auto& k : p.kits) { Kit copy = k.second; copy.used = true; b.kits[size_t(mapped(kitMap, k.first) - 1)] = std::move(copy); }

    Instrument inst = p.instrument;
    inst.used = true;
    if (inst.table) inst.table = uint8_t(mapped(tableMap, inst.table));
    if (inst.type == InstrumentType::Wave) inst.wave = uint8_t(mapped(waveMap, inst.wave));
    if (inst.type == InstrumentType::Kit) inst.kit = uint8_t(mapped(kitMap, inst.kit));
    b.instruments[size_t(targetSlot - 1)] = std::move(inst);

    report.ok = true;
    report.instrumentSlot = targetSlot;
    report.moves = std::move(moves);
    return true;
}

} // namespace chipboy::bank
