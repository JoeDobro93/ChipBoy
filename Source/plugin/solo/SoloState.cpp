#include "plugin/solo/SoloState.h"

#include "core/Driver/Driver.h"
#include "plugin/shared/BankJson.h"

#include <algorithm>

namespace chipboy::plugin {

using namespace juce;

void soloNoteRange(int channel, int& lo, int& hi)
{
    if (channel == 3) { lo = 12; hi = 127; return; }      // the noise map's keyboard (section 85)
    const bool wave = channel == 2;
    lo = driver::Driver::lowestNote(wave);
    hi = lo;
    for (int n = lo; n < 128; ++n) {
        const int p = driver::Driver::periodOfSemitone(n, wave);
        if (p >= 0 && p < 2048) hi = n;
    }
}

bool soloKeyMappable(int channel, int note)
{
    int lo = 0, hi = 127;
    soloNoteRange(channel, lo, hi);
    return note < lo || note > hi;
}

void soloDefaultKeyMap(int channel, std::array<uint8_t, 128>& map)
{
    map.fill(0);
    int lo = 0, hi = 127;
    soloNoteRange(channel, lo, hi);
    int sound = 1;
    for (int n = lo - 1; n >= 0 && sound <= kSoloSounds; --n) map[size_t(n)] = uint8_t(sound++);
    for (int n = hi + 1; n < 128 && sound <= kSoloSounds; ++n) map[size_t(n)] = uint8_t(sound++);
}

var soloStateToVar(const SoloState& s)
{
    auto* o = new DynamicObject();
    Array<var> sounds;
    for (int k = 0; k < kSoloSounds; ++k) {
        const auto& snd = s.sounds[size_t(k)];
        if (!snd.used) continue;
        auto* so = new DynamicObject();
        so->setProperty("slot", k + 1);
        so->setProperty("name", String(CharPointer_UTF8(snd.name.c_str())));
        so->setProperty("inst", int(snd.inst));
        so->setProperty("table", int(snd.table));
        if (snd.cmd1.cmd != bank::Cmd::None) so->setProperty("cmd1", commandToVar(snd.cmd1));
        if (snd.cmd2.cmd != bank::Cmd::None) so->setProperty("cmd2", commandToVar(snd.cmd2));
        sounds.add(var(so));
    }
    o->setProperty("sounds", sounds);
    Array<var> commands;
    for (int k = 0; k < kSoloCommands; ++k) {
        const auto& c = s.commands[size_t(k)];
        if (!c.used) continue;
        auto* co = new DynamicObject();
        co->setProperty("slot", k + 1);
        co->setProperty("name", String(CharPointer_UTF8(c.name.c_str())));
        co->setProperty("cmd", commandToVar(c.cmd));
        commands.add(var(co));
    }
    o->setProperty("commands", commands);
    Array<var> maps;
    bool anyKey = false;
    for (const auto& map : s.keyMaps) {
        Array<var> keys;
        for (uint8_t k : map) { keys.add(int(k)); anyKey = anyKey || k != 0; }
        maps.add(keys);
    }
    if (anyKey) o->setProperty("keyMaps", maps);
    return var(o);
}

bool soloStateFromVar(const var& v, SoloState& out)
{
    auto* o = v.getDynamicObject();
    if (o == nullptr) return false;
    out = SoloState{};
    if (auto* sounds = o->getProperty("sounds").getArray())
        for (const auto& sv : *sounds) {
            auto* so = sv.getDynamicObject();
            if (so == nullptr) continue;
            const int slot = int(so->getProperty("slot"));
            if (slot < 1 || slot > kSoloSounds) continue;
            auto& snd = out.sounds[size_t(slot - 1)];
            snd.used = true;
            snd.name = so->getProperty("name").toString().toStdString();
            snd.inst = uint8_t(std::clamp(int(so->getProperty("inst")), 0, bank::kInstrumentSlots));
            snd.table = uint8_t(std::clamp(int(so->getProperty("table")), 0, bank::kTableSlots));
            snd.cmd1 = commandFromVar(so->getProperty("cmd1"));
            snd.cmd2 = commandFromVar(so->getProperty("cmd2"));
        }
    if (auto* commands = o->getProperty("commands").getArray())
        for (const auto& cv : *commands) {
            auto* co = cv.getDynamicObject();
            if (co == nullptr) continue;
            const int slot = int(co->getProperty("slot"));
            if (slot < 1 || slot > kSoloCommands) continue;
            auto& c = out.commands[size_t(slot - 1)];
            c.used = true;
            c.name = co->getProperty("name").toString().toStdString();
            c.cmd = commandFromVar(co->getProperty("cmd"));
        }
    if (auto* maps = o->getProperty("keyMaps").getArray()) {
        for (int ch = 0; ch < 4 && ch < maps->size(); ++ch)
            if (auto* keys = (*maps)[ch].getArray())
                for (int n = 0; n < 128 && n < keys->size(); ++n)
                    out.keyMaps[size_t(ch)][size_t(n)] = uint8_t(std::clamp(int((*keys)[n]), 0, kSoloSounds));
    } else {
        // A file that says nothing about keys -- a generated one -- takes
        // the default layout, as a new instance does.
        for (int ch = 0; ch < 4; ++ch) soloDefaultKeyMap(ch, out.keyMaps[size_t(ch)]);
    }
    return true;
}

} // namespace chipboy::plugin
