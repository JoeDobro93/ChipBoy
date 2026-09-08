#include "plugin/shared/BankJson.h"

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::bank;

namespace {

var cmdToVar(const Command& c)
{
    if (c.cmd == Cmd::None) return var();
    auto* o = new DynamicObject();
    o->setProperty("c", String(cmdLetter(c.cmd)));
    o->setProperty("a", c.a); o->setProperty("b", c.b); o->setProperty("x", c.c);
    return var(o);
}
Command cmdFromVar(const var& v)
{
    Command c;
    if (auto* o = v.getDynamicObject()) {
        const String l = o->getProperty("c").toString();
        c.cmd = l.isNotEmpty() ? cmdFromLetter(char(l[0])) : Cmd::None;
        c.a = int16_t(int(o->getProperty("a"))); c.b = int16_t(int(o->getProperty("b"))); c.c = int16_t(int(o->getProperty("x")));
    }
    return c;
}
template <typename T> T getOr(const DynamicObject* o, const char* key, T def)
{
    if (!o || !o->hasProperty(key)) return def;
    return T(int(o->getProperty(key)));
}

var instrumentToVarSlot(const Instrument& i, int slot)
{
    auto* o = new DynamicObject();
    o->setProperty("slot", slot);
    o->setProperty("name", String(i.name));
    o->setProperty("type", int(i.type));
    o->setProperty("pan", int(i.pan)); o->setProperty("length", int(i.length)); o->setProperty("table", int(i.table));
    o->setProperty("transpose", i.transpose); o->setProperty("noteOff", int(i.noteOff)); o->setProperty("legato", i.legato);
    o->setProperty("vibShape", int(i.vib.shape)); o->setProperty("vibSpeed", int(i.vib.speed)); o->setProperty("vibDepth", int(i.vib.depth)); o->setProperty("vibDelay", int(i.vib.delay));
    o->setProperty("duty", int(i.duty));
    { Array<var> seq; for (int k = 0; k < i.dutySeqLen; ++k) seq.add(int(i.dutySeq[size_t(k)])); o->setProperty("dutySeq", seq); }
    o->setProperty("envVol", int(i.envVol)); o->setProperty("envDir", int(i.envDir)); o->setProperty("envRate", int(i.envRate));
    o->setProperty("sweepRate", int(i.sweepRate)); o->setProperty("sweepDown", i.sweepDown); o->setProperty("sweepShift", int(i.sweepShift));
    o->setProperty("wave", int(i.wave)); o->setProperty("frameAdvance", int(i.frameAdvance)); o->setProperty("frameLoop", int(i.frameLoop)); o->setProperty("waveLevel", int(i.waveLevel));
    o->setProperty("kit", int(i.kit)); o->setProperty("kitLoop", int(i.kitLoop));
    o->setProperty("lfsr7", i.lfsr7); o->setProperty("noiseManual", i.noiseManual); o->setProperty("noiseShift", int(i.noiseShift)); o->setProperty("noiseDivisor", int(i.noiseDivisor)); o->setProperty("noiseSweep", int(i.noiseSweep));
    return var(o);
}
void instrumentFromVarImpl(const var& v, Instrument& i)
{
    auto* o = v.getDynamicObject(); if (!o) return;
    i.used = true;
    i.name = o->getProperty("name").toString().toStdString();
    i.type = InstrumentType(std::clamp(getOr(o, "type", 0), 0, 3));
    i.pan = Pan(std::clamp(getOr(o, "pan", 3), 0, 3)); i.length = uint16_t(std::clamp(getOr(o, "length", 0), 0, 256)); i.table = uint8_t(std::clamp(getOr(o, "table", 0), 0, 64));
    i.transpose = bool(o->getProperty("transpose")); i.noteOff = NoteOff(std::clamp(getOr(o, "noteOff", 0), 0, 2)); i.legato = bool(o->getProperty("legato"));
    i.vib.shape = VibShape(std::clamp(getOr(o, "vibShape", 0), 0, 3)); i.vib.speed = uint8_t(std::clamp(getOr(o, "vibSpeed", 4), 1, 15)); i.vib.depth = uint8_t(std::clamp(getOr(o, "vibDepth", 0), 0, 15)); i.vib.delay = uint8_t(std::clamp(getOr(o, "vibDelay", 0), 0, 255));
    i.duty = uint8_t(std::clamp(getOr(o, "duty", 2), 0, 3));
    i.dutySeqLen = 0;
    if (auto* seq = o->getProperty("dutySeq").getArray()) for (const auto& s : *seq) if (i.dutySeqLen < 16) i.dutySeq[i.dutySeqLen++] = uint8_t(std::clamp(int(s), 0, 3));
    i.envVol = uint8_t(std::clamp(getOr(o, "envVol", 15), 0, 15)); i.envDir = EnvDir(std::clamp(getOr(o, "envDir", 0), 0, 1)); i.envRate = uint8_t(std::clamp(getOr(o, "envRate", 0), 0, 7));
    i.sweepRate = uint8_t(std::clamp(getOr(o, "sweepRate", 0), 0, 7)); i.sweepDown = bool(o->getProperty("sweepDown")); i.sweepShift = uint8_t(std::clamp(getOr(o, "sweepShift", 0), 0, 7));
    i.wave = uint8_t(std::clamp(getOr(o, "wave", 1), 1, 64)); i.frameAdvance = uint8_t(std::clamp(getOr(o, "frameAdvance", 0), 0, 15)); i.frameLoop = FrameLoop(std::clamp(getOr(o, "frameLoop", 0), 0, 2)); i.waveLevel = uint8_t(std::clamp(getOr(o, "waveLevel", 3), 0, 3));
    i.kit = uint8_t(std::clamp(getOr(o, "kit", 1), 1, 32)); i.kitLoop = KitLoop(std::clamp(getOr(o, "kitLoop", 0), 0, 2));
    i.lfsr7 = bool(o->getProperty("lfsr7")); i.noiseManual = bool(o->getProperty("noiseManual")); i.noiseShift = uint8_t(std::clamp(getOr(o, "noiseShift", 5), 0, 13)); i.noiseDivisor = uint8_t(std::clamp(getOr(o, "noiseDivisor", 1), 0, 7)); i.noiseSweep = int8_t(std::clamp(getOr(o, "noiseSweep", 0), -7, 7));
}

var tableToVar(const Table& t, int slot)
{
    auto* o = new DynamicObject();
    o->setProperty("slot", slot); o->setProperty("name", String(t.name)); o->setProperty("end", int(t.end)); o->setProperty("hop", int(t.hopStep));
    Array<var> steps;
    for (const auto& s : t.steps) {
        auto* so = new DynamicObject();
        so->setProperty("vol", int(s.vol));
        if (s.hasTranspose) so->setProperty("trn", int(s.transpose));
        if (s.cmd1.cmd != Cmd::None) so->setProperty("c1", cmdToVar(s.cmd1));
        if (s.cmd2.cmd != Cmd::None) so->setProperty("c2", cmdToVar(s.cmd2));
        steps.add(var(so));
    }
    o->setProperty("steps", steps);
    return var(o);
}
void tableFromVar(const var& v, Table& t)
{
    auto* o = v.getDynamicObject(); if (!o) return;
    t.used = true; t.name = o->getProperty("name").toString().toStdString(); t.end = TableEnd(std::clamp(getOr(o, "end", 0), 0, 2)); t.hopStep = uint8_t(std::clamp(getOr(o, "hop", 1), 1, 16));
    if (auto* steps = o->getProperty("steps").getArray())
        for (int k = 0; k < std::min(16, steps->size()); ++k) {
            auto* so = (*steps)[k].getDynamicObject(); if (!so) continue;
            auto& s = t.steps[size_t(k)];
            s.vol = int8_t(std::clamp(getOr(so, "vol", -1), -1, 15));
            s.hasTranspose = so->hasProperty("trn"); s.transpose = int8_t(std::clamp(getOr(so, "trn", 0), -60, 60));
            s.cmd1 = cmdFromVar(so->getProperty("c1")); s.cmd2 = cmdFromVar(so->getProperty("c2"));
        }
}

var waveToVar(const Wave& w, int slot)
{
    auto* o = new DynamicObject();
    o->setProperty("slot", slot); o->setProperty("name", String(w.name));
    Array<var> frames;
    for (const auto& f : w.frames) { Array<var> s; for (auto v : f.s) s.add(int(v)); frames.add(s); }
    o->setProperty("frames", frames);
    return var(o);
}
void waveFromVar(const var& v, Wave& w)
{
    auto* o = v.getDynamicObject(); if (!o) return;
    w.used = true; w.name = o->getProperty("name").toString().toStdString(); w.frames.clear();
    if (auto* frames = o->getProperty("frames").getArray())
        for (const auto& fv : *frames) {
            if (w.frames.size() >= size_t(kMaxFrames)) break;
            Frame f; if (auto* s = fv.getArray()) for (int k = 0; k < std::min(32, s->size()); ++k) f.s[size_t(k)] = uint8_t(std::clamp(int((*s)[k]), 0, 15));
            w.frames.push_back(f);
        }
    if (w.frames.empty()) w.frames.push_back(Frame{});
}

var kitToVar(const Kit& k, int slot)
{
    auto* o = new DynamicObject();
    o->setProperty("slot", slot); o->setProperty("name", String(k.name)); o->setProperty("period", int(k.period)); o->setProperty("loop", int(k.loop));
    Array<var> samples;
    for (const auto& s : k.samples) {
        auto* so = new DynamicObject();
        so->setProperty("name", String(s.name)); so->setProperty("note", int(s.note)); so->setProperty("loopPoint", int(s.loopPoint)); so->setProperty("length", int(s.data.size()));
        MemoryBlock packed((s.data.size() + 1) / 2, true);
        for (size_t i = 0; i < s.data.size(); ++i) { auto* b = static_cast<uint8_t*>(packed.getData()); if (i & 1) b[i / 2] |= uint8_t(s.data[i] & 15); else b[i / 2] = uint8_t(s.data[i] << 4); }
        so->setProperty("data", Base64::toBase64(packed.getData(), packed.getSize()));
        samples.add(var(so));
    }
    o->setProperty("samples", samples);
    return var(o);
}
void kitFromVar(const var& v, Kit& k)
{
    auto* o = v.getDynamicObject(); if (!o) return;
    k.used = true; k.name = o->getProperty("name").toString().toStdString(); k.period = uint16_t(std::clamp(getOr(o, "period", 1865), 0, 2047)); k.loop = KitLoop(std::clamp(getOr(o, "loop", 0), 0, 2));
    k.samples.clear();
    if (auto* samples = o->getProperty("samples").getArray())
        for (const auto& sv : *samples) {
            if (k.samples.size() >= size_t(kMaxKitSamples)) break;
            auto* so = sv.getDynamicObject(); if (!so) continue;
            KitSample s; s.name = so->getProperty("name").toString().toStdString(); s.note = uint8_t(std::clamp(getOr(so, "note", 60), 0, 127)); s.loopPoint = uint32_t(std::max(0, getOr(so, "loopPoint", 0)));
            const int len = std::max(0, getOr(so, "length", 0));
            MemoryOutputStream mo; Base64::convertFromBase64(mo, so->getProperty("data").toString());
            const auto* b = static_cast<const uint8_t*>(mo.getData());
            s.data.resize(size_t(len));
            for (int i = 0; i < len; ++i) s.data[size_t(i)] = size_t(i / 2) < mo.getDataSize() ? uint8_t((i & 1) ? (b[i / 2] & 15) : (b[i / 2] >> 4)) : 8;
            k.samples.push_back(std::move(s));
        }
}

} // namespace

var instrumentToVar(const Instrument& i) { return instrumentToVarSlot(i, 0); }
bool instrumentFromVar(const var& v, Instrument& out)
{
    if (!v.getDynamicObject()) return false;
    instrumentFromVarImpl(v, out);
    return true;
}
String instrumentToJson(const Instrument& i) { return JSON::toString(instrumentToVar(i), true); }
bool instrumentFromJson(const String& text, Instrument& out)
{
    const var v = JSON::parse(text);
    return instrumentFromVar(v, out);
}

var bankToVar(const Bank& b)
{
    auto* o = new DynamicObject();
    o->setProperty("format", "chipboy-bank");
    o->setProperty("version", 1);
    Array<var> ins, tabs, waves, kits;
    for (int i = 0; i < kInstrumentSlots; ++i) if (b.instruments[size_t(i)].used) ins.add(instrumentToVarSlot(b.instruments[size_t(i)], i + 1));
    for (int i = 0; i < kTableSlots; ++i) if (b.tables[size_t(i)].used) tabs.add(tableToVar(b.tables[size_t(i)], i + 1));
    for (int i = 0; i < kWaveSlots; ++i) if (b.waves[size_t(i)].used) waves.add(waveToVar(b.waves[size_t(i)], i + 1));
    for (int i = 0; i < kKitSlots; ++i) if (b.kits[size_t(i)].used) kits.add(kitToVar(b.kits[size_t(i)], i + 1));
    o->setProperty("instruments", ins); o->setProperty("tables", tabs); o->setProperty("waves", waves); o->setProperty("kits", kits);
    return var(o);
}

bool bankFromVar(const var& v, Bank& out)
{
    auto* o = v.getDynamicObject(); if (!o) return false;
    if (o->getProperty("format").toString() != "chipboy-bank") return false;
    out = Bank::empty();
    auto each = [](const var& arr, int maxSlot, auto fn) { if (auto* a = arr.getArray()) for (const auto& e : *a) { const int slot = e.getDynamicObject() ? int(e.getDynamicObject()->getProperty("slot")) : 0; if (slot >= 1 && slot <= maxSlot) fn(e, slot); } };
    each(o->getProperty("instruments"), kInstrumentSlots, [&](const var& e, int slot) { instrumentFromVarImpl(e, out.instruments[size_t(slot - 1)]); });
    each(o->getProperty("tables"), kTableSlots, [&](const var& e, int slot) { tableFromVar(e, out.tables[size_t(slot - 1)]); });
    each(o->getProperty("waves"), kWaveSlots, [&](const var& e, int slot) { waveFromVar(e, out.waves[size_t(slot - 1)]); });
    each(o->getProperty("kits"), kKitSlots, [&](const var& e, int slot) { kitFromVar(e, out.kits[size_t(slot - 1)]); });
    return true;
}

var songToVar(const tracker::Song& s)
{
    auto* o = new DynamicObject();
    o->setProperty("format", "chipboy-song"); o->setProperty("version", 3); o->setProperty("stepsPerBar", int(s.stepsPerBar));
    // The song's own timeline (docs/COMMANDS_AND_TEMPO.md section 4).
    o->setProperty("tempoBpm", s.tempoBpm); o->setProperty("songStartSeconds", s.songStartSeconds); o->setProperty("beatsPerBar", s.beatsPerBar);
    Array<var> phrases;
    for (int i = 0; i < tracker::kPhraseSlots; ++i) {
        const auto& p = s.phrases[size_t(i)]; if (!p.used) continue;
        auto* po = new DynamicObject(); po->setProperty("slot", i + 1); po->setProperty("groove", int(p.groove));
        Array<var> steps;
        for (const auto& c : p.steps) {
            auto* co = new DynamicObject();
            if (c.note) co->setProperty("n", int(c.note));
            if (c.vel) co->setProperty("v", int(c.vel));
            if (c.inst) co->setProperty("i", int(c.inst));
            if (c.table) co->setProperty("t", int(c.table));
            if (c.cmd1.cmd != Cmd::None) co->setProperty("c1", cmdToVar(c.cmd1));
            if (c.cmd2.cmd != Cmd::None) co->setProperty("c2", cmdToVar(c.cmd2));
            steps.add(var(co));
        }
        po->setProperty("steps", steps); phrases.add(var(po));
    }
    o->setProperty("phrases", phrases);
    Array<var> chains; for (const auto& c : s.chain) { Array<var> a; for (auto p : c) a.add(int(p)); chains.add(a); }
    o->setProperty("chains", chains);
    Array<var> src; for (auto n : s.noteSource) src.add(int(n)); o->setProperty("noteSource", src);
    // A groove is sixteen tick counts (section 9.2); trailing unused entries
    // are left out, so the common two-entry swing still reads as [a, b].
    Array<var> gr;
    for (const auto& g : s.grooves) { Array<var> a; for (int k = 0; k < g.length(); ++k) a.add(int(g.ticks[size_t(k)])); gr.add(a); }
    o->setProperty("grooves", gr);
    return var(o);
}

bool songFromVar(const var& v, tracker::Song& out)
{
    auto* o = v.getDynamicObject(); if (!o) return false;
    if (o->getProperty("format").toString() != "chipboy-song") return false;
    out = tracker::Song{};
    out.stepsPerBar = getOr(o, "stepsPerBar", 16) <= 8 ? 8 : 16;      // 8 or 16 (section 9.1)
    out.tempoBpm = std::clamp(o->hasProperty("tempoBpm") ? double(o->getProperty("tempoBpm")) : 120.0, 40.0, 255.0);
    out.songStartSeconds = std::max(0.0, o->hasProperty("songStartSeconds") ? double(o->getProperty("songStartSeconds")) : 0.0);
    out.beatsPerBar = std::clamp(o->hasProperty("beatsPerBar") ? double(o->getProperty("beatsPerBar")) : 4.0, 0.25, 32.0);
    if (auto* ph = o->getProperty("phrases").getArray())
        for (const auto& pv : *ph) {
            auto* po = pv.getDynamicObject(); if (!po) continue;
            const int slot = getOr(po, "slot", 0); if (slot < 1 || slot > tracker::kPhraseSlots) continue;
            auto& p = out.phrases[size_t(slot - 1)]; p.used = true; p.groove = uint8_t(std::clamp(getOr(po, "groove", 0), 0, 16));
            if (auto* steps = po->getProperty("steps").getArray())
                for (int k = 0; k < std::min(16, steps->size()); ++k) {
                    auto* co = (*steps)[k].getDynamicObject(); if (!co) continue;
                    auto& c = p.steps[size_t(k)];
                    c.note = uint8_t(std::clamp(getOr(co, "n", 0), 0, 255)); c.vel = uint8_t(std::clamp(getOr(co, "v", 0), 0, 127));
                    c.inst = uint8_t(std::clamp(getOr(co, "i", 0), 0, 128)); c.table = uint8_t(std::clamp(getOr(co, "t", 0), 0, 64));
                    c.cmd1 = cmdFromVar(co->getProperty("c1")); c.cmd2 = cmdFromVar(co->getProperty("c2"));
                }
        }
    if (auto* chains = o->getProperty("chains").getArray())
        for (int ch = 0; ch < std::min(4, chains->size()); ++ch) if (auto* a = (*chains)[ch].getArray()) { out.chain[size_t(ch)].clear(); for (const auto& p : *a) out.chain[size_t(ch)].push_back(uint8_t(std::clamp(int(p), 0, 255))); }
    if (auto* src = o->getProperty("noteSource").getArray()) for (int ch = 0; ch < std::min(4, src->size()); ++ch) out.noteSource[size_t(ch)] = tracker::NoteSource(std::clamp(int((*src)[ch]), 0, 1));
    // Sixteen tick counts; the old two-entry form reads as the first two.
    if (auto* gr = o->getProperty("grooves").getArray())
        for (int k = 0; k < std::min(16, gr->size()); ++k)
            if (auto* a = (*gr)[k].getArray()) {
                auto& t = out.grooves[size_t(k)].ticks;
                t = {};
                for (int i = 0; i < std::min(int(t.size()), a->size()); ++i) t[size_t(i)] = uint8_t(std::clamp(int((*a)[i]), 0, 48));
                if (t[0] == 0) t[0] = 6;
            }
    return true;
}

String bankToJson(const Bank& b) { return JSON::toString(bankToVar(b), false); }
bool bankFromJson(const String& text, Bank& out) { const var v = JSON::parse(text); return bankFromVar(v, out); }
String songToJson(const tracker::Song& s) { return JSON::toString(songToVar(s), false); }
bool songFromJson(const String& text, tracker::Song& out) { const var v = JSON::parse(text); return songFromVar(v, out); }

} // namespace chipboy::plugin
