#include "core/Tracker/MidiMap.h"

#include <algorithm>

namespace chipboy::tracker {

void normalizeMidiMap(MidiMap& m)
{
    for (auto& c : m.channels) {
        c.target = int8_t(c.target < 0 || c.target > 3 ? -1 : c.target);
        auto& r = c.regions;
        if (r.empty()) r.push_back(MidiRegion{});
        std::stable_sort(r.begin(), r.end(), [](const MidiRegion& a, const MidiRegion& b) { return a.from < b.from; });
        // No two regions start together: the later one, in the order they
        // were kept, wins the velocity and the earlier goes.
        for (size_t i = 1; i < r.size();) {
            if (r[i].from == r[i - 1].from) r.erase(r.begin() + long(i - 1));
            else ++i;
        }
        if (r.size() > size_t(kMaxRegions)) r.resize(size_t(kMaxRegions));
        r.front().from = 1;
        for (auto& g : r) {
            g.from = uint8_t(std::clamp<int>(g.from, 1, 127));
            g.inst = uint8_t(std::clamp<int>(g.inst, 0, bank::kInstrumentSlots));
            g.table = uint8_t(std::clamp<int>(g.table, 0, bank::kTableSlots));
            g.cmd1 = midiRegionCommand(g.cmd1);
            g.cmd2 = midiRegionCommand(g.cmd2);
        }
    }
}

const MidiRegion& regionFor(const MidiChannelMap& c, uint8_t velocity)
{
    const MidiRegion* best = &c.regions.front();
    for (const auto& r : c.regions) if (r.from <= velocity) best = &r;
    return *best;
}

bool midiMapIsDefault(const MidiMap& m)
{
    if (m.on) return false;
    for (const auto& c : m.channels) {
        if (c.target >= 0 || c.regions.size() != 1) return false;
        const auto& r = c.regions.front();
        if (r.from != 1 || r.inst || r.table || r.transpose || r.sample || r.cmd1.cmd != bank::Cmd::None || r.cmd2.cmd != bank::Cmd::None) return false;
    }
    return true;
}

bool midiCommandAllowed(bank::Cmd c) { return c != bank::Cmd::H && c != bank::Cmd::G && c != bank::Cmd::T; }

bank::Command midiRegionCommand(const bank::Command& c) { return midiCommandAllowed(c.cmd) ? c : bank::Command{}; }

} // namespace chipboy::tracker
