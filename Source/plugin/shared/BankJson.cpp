#include "plugin/shared/BankJson.h"

#include <array>
#include <memory>
#include <utility>

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

/// Format 7 (docs/COMMANDS_AND_TEMPO.md section 34): three letters changed
/// what their arguments *mean*, so a file written before it is converted as it
/// is read.
///
///  - **P** was the signed speed biased by 128 and is two's complement now, so
///    `x` becomes `x - 128` as a byte.
///  - **S** carried its direction as bit 7 of `x`; it is NR10's low nibble in
///    `y` now, so `y` becomes `(down << 3) | shift`.
///  - **H** in a table was the step to hop to, 1-16; it is LSDj's `times, row`
///    now, and the old instruction is "for ever, to that row".
///
/// V, L, R and the rest keep their numbers: what changed there is the law the
/// driver plays them by, not the encoding, and no conversion can put a song's
/// musical intent back (CHANGES.md says so).
void convertCommandToFormat7(Command& c)
{
    if (isRevert(c)) return;
    if (c.cmd == Cmd::P) c.a = int16_t((int(c.a) - 128) & 0xFF);
    else if (c.cmd == Cmd::S) { c.b = int16_t(((int(c.a) & 0x80) ? 8 : 0) | (int(c.b) & 7)); c.a = int16_t(int(c.a) & 7); }
    else if (c.cmd == Cmd::H) { c.b = int16_t(std::clamp(int(c.a) - 1, 0, 15)); c.a = 0; }
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
    o->setProperty("transpose", i.transpose); o->setProperty("noteOff", int(i.noteOff)); o->setProperty("overlap", int(i.overlap));
    o->setProperty("pitchSpeed", int(i.pitchSpeed)); o->setProperty("cmdRate", int(i.cmdRate)); o->setProperty("tableMode", int(i.tableMode));
    o->setProperty("vibShape", int(i.vib.shape)); o->setProperty("vibDir", int(i.vib.dir)); o->setProperty("vibSpeed", int(i.vib.speed)); o->setProperty("vibDepth", int(i.vib.depth)); o->setProperty("vibDelay", int(i.vib.delay));
    o->setProperty("duty", int(i.duty));
    { Array<var> seq; for (int k = 0; k < i.dutySeqLen; ++k) seq.add(int(i.dutySeq[size_t(k)])); o->setProperty("dutySeq", seq); }
    o->setProperty("envVol", int(i.envVol)); o->setProperty("envDir", int(i.envDir)); o->setProperty("envRate", int(i.envRate));
    // The envelope's mode and, when it is Shaped, its segments (section 27).
    // A Chip instrument writes the mode alone, so a factory bank reads the
    // same as it always did but for one property.
    o->setProperty("envMode", int(i.env.mode));
    if (i.env.mode != EnvMode::Chip) {
        o->setProperty("envAttack", int(i.env.attackTicks)); o->setProperty("envPeak", int(i.env.peak));
        o->setProperty("envDecay", int(i.env.decayTicks)); o->setProperty("envSustain", int(i.env.sustain));
        o->setProperty("envRelease", int(i.env.releaseTicks));
        o->setProperty("envAttackCurve", int(i.env.attackCurve)); o->setProperty("envDecayCurve", int(i.env.decayCurve));
        o->setProperty("envReleaseCurve", int(i.env.releaseCurve));
    }
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
    i.transpose = bool(o->getProperty("transpose")); i.noteOff = NoteOff(std::clamp(getOr(o, "noteOff", 0), 0, 2));
    // Overlap replaced the legato flag: a file written before it carries only
    // legato, and one with neither takes the type's own default.
    if (o->hasProperty("overlap")) i.overlap = Overlap(std::clamp(getOr(o, "overlap", 0), 0, 1));
    else if (o->hasProperty("legato")) i.overlap = bool(o->getProperty("legato")) ? Overlap::Legato : Overlap::Retrig;
    else i.overlap = Instrument::defaults(i.type).overlap;
    i.pitchSpeed = PitchSpeed(std::clamp(getOr(o, "pitchSpeed", 0), 0, 3));
    i.cmdRate = uint8_t(std::clamp(getOr(o, "cmdRate", 0), 0, 15));
    i.tableMode = TableMode(std::clamp(getOr(o, "tableMode", 0), 0, 1));
    // The vibrato's shape used to carry its direction (Triangle, Square,
    // SawUp, SawDown); it is a shape and a direction now.
    if (o->hasProperty("vibDir")) {
        i.vib.shape = VibShape(std::clamp(getOr(o, "vibShape", 0), 0, 2));
        i.vib.dir = VibDir(std::clamp(getOr(o, "vibDir", 0), 0, 1));
    } else {
        const int shape = std::clamp(getOr(o, "vibShape", 0), 0, 3);
        i.vib.shape = shape == 0 ? VibShape::Triangle : shape == 1 ? VibShape::Square : VibShape::Saw;
        i.vib.dir = shape == 2 ? VibDir::Up : VibDir::Down;
    }
    i.vib.speed = uint8_t(std::clamp(getOr(o, "vibSpeed", 8), 1, 15)); i.vib.depth = uint8_t(std::clamp(getOr(o, "vibDepth", 0), 0, 15)); i.vib.delay = uint8_t(std::clamp(getOr(o, "vibDelay", 0), 0, 255));
    i.duty = uint8_t(std::clamp(getOr(o, "duty", 2), 0, 3));
    i.dutySeqLen = 0;
    if (auto* seq = o->getProperty("dutySeq").getArray()) for (const auto& s : *seq) if (i.dutySeqLen < 16) i.dutySeq[i.dutySeqLen++] = uint8_t(std::clamp(int(s), 0, 3));
    i.envVol = uint8_t(std::clamp(getOr(o, "envVol", 15), 0, 15)); i.envDir = EnvDir(std::clamp(getOr(o, "envDir", 0), 0, 1)); i.envRate = uint8_t(std::clamp(getOr(o, "envRate", 0), 0, 7));
    // A file written before section 27 has no envelope mode: it is Chip.
    i.env = Envelope{};
    i.env.mode = EnvMode(std::clamp(getOr(o, "envMode", 0), 0, 1));
    i.env.attackTicks = uint8_t(std::clamp(getOr(o, "envAttack", 0), 0, 255));
    i.env.peak = uint8_t(std::clamp(getOr(o, "envPeak", 15), 0, 15));
    i.env.decayTicks = uint8_t(std::clamp(getOr(o, "envDecay", 0), 0, 255));
    i.env.sustain = uint8_t(std::clamp(getOr(o, "envSustain", 15), 0, 15));
    i.env.releaseTicks = uint8_t(std::clamp(getOr(o, "envRelease", 0), 0, 255));
    i.env.attackCurve = EnvCurve(std::clamp(getOr(o, "envAttackCurve", 0), 0, 2));
    i.env.decayCurve = EnvCurve(std::clamp(getOr(o, "envDecayCurve", 0), 0, 2));
    i.env.releaseCurve = EnvCurve(std::clamp(getOr(o, "envReleaseCurve", 0), 0, 2));
    i.sweepRate = uint8_t(std::clamp(getOr(o, "sweepRate", 0), 0, 7)); i.sweepDown = bool(o->getProperty("sweepDown")); i.sweepShift = uint8_t(std::clamp(getOr(o, "sweepShift", 0), 0, 7));
    i.wave = uint8_t(std::clamp(getOr(o, "wave", 1), 1, 64)); i.frameAdvance = uint8_t(std::clamp(getOr(o, "frameAdvance", 0), 0, 15)); i.frameLoop = FrameLoop(std::clamp(getOr(o, "frameLoop", 0), 0, 2)); i.waveLevel = uint8_t(std::clamp(getOr(o, "waveLevel", 3), 0, 3));
    i.kit = uint8_t(std::clamp(getOr(o, "kit", 1), 1, 32)); i.kitLoop = KitLoop(std::clamp(getOr(o, "kitLoop", 0), 0, 2));
    i.lfsr7 = bool(o->getProperty("lfsr7")); i.noiseManual = bool(o->getProperty("noiseManual")); i.noiseShift = uint8_t(std::clamp(getOr(o, "noiseShift", 5), 0, 13)); i.noiseDivisor = uint8_t(std::clamp(getOr(o, "noiseDivisor", 1), 0, 7)); i.noiseSweep = int8_t(std::clamp(getOr(o, "noiseSweep", 0), -7, 7));
}

var tableToVarImpl(const Table& t, int slot)
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
void tableFromVarImpl(const var& v, Table& t)
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

/// The synth behind a run (docs/COMMANDS_AND_TEMPO.md section 33). Written
/// only when the run was generated, so a hand-drawn wave reads as it always
/// did; the frames themselves are still what the exporter ships.
var synthStateToVar(const SynthState& st)
{
    auto* o = new DynamicObject();
    o->setProperty("width", int(st.width));
    Array<var> partials;
    for (auto v : st.partials) partials.add(int(v));
    o->setProperty("partials", partials);
    Array<var> amount, resonance;
    for (auto v : st.amount) amount.add(int(v));
    for (auto v : st.resonance) resonance.add(int(v));
    o->setProperty("amount", amount);
    o->setProperty("resonance", resonance);
    return var(o);
}
void synthStateFromVar(const var& v, SynthState& st)
{
    auto* o = v.getDynamicObject(); if (!o) return;
    st.width = uint8_t(std::clamp(getOr(o, "width", 16), 1, 31));
    if (auto* p = o->getProperty("partials").getArray())
        for (int k = 0; k < std::min(kSynthPartials, p->size()); ++k) st.partials[size_t(k)] = uint8_t(std::clamp(int((*p)[k]), 0, 15));
    if (auto* a = o->getProperty("amount").getArray())
        for (int k = 0; k < std::min(kSynthStages, a->size()); ++k) st.amount[size_t(k)] = int8_t(std::clamp(int((*a)[k]), -15, 15));
    if (auto* r = o->getProperty("resonance").getArray())
        for (int k = 0; k < std::min(kSynthStages, r->size()); ++k) st.resonance[size_t(k)] = uint8_t(std::clamp(int((*r)[k]), 0, 15));
}
var synthToVar(const Synth& sy)
{
    auto* o = new DynamicObject();
    o->setProperty("source", int(sy.source));
    Array<var> chain;
    for (auto c : sy.chain) chain.add(int(c));
    o->setProperty("chain", chain);
    o->setProperty("start", synthStateToVar(sy.start));
    o->setProperty("end", synthStateToVar(sy.end));
    o->setProperty("frames", int(sy.frames));
    o->setProperty("seed", int(sy.seed));
    return var(o);
}
void synthFromVar(const var& v, Synth& sy)
{
    auto* o = v.getDynamicObject(); if (!o) return;
    sy.used = true;
    sy.source = SynthSource(std::clamp(getOr(o, "source", 0), 0, kSynthSourceCount - 1));
    if (auto* c = o->getProperty("chain").getArray())
        for (int k = 0; k < std::min(kSynthStages, c->size()); ++k) sy.chain[size_t(k)] = SynthShaper(std::clamp(int((*c)[k]), 0, kSynthShaperCount - 1));
    synthStateFromVar(o->getProperty("start"), sy.start);
    synthStateFromVar(o->getProperty("end"), sy.end);
    sy.frames = uint8_t(std::clamp(getOr(o, "frames", 1), 1, kMaxFrames));
    sy.seed = uint8_t(std::clamp(getOr(o, "seed", 1), 0, 255));
}

var waveToVarImpl(const Wave& w, int slot)
{
    auto* o = new DynamicObject();
    o->setProperty("slot", slot); o->setProperty("name", String(w.name));
    Array<var> frames;
    for (const auto& f : w.frames) { Array<var> s; for (auto v : f.s) s.add(int(v)); frames.add(s); }
    o->setProperty("frames", frames);
    if (w.synth.used) o->setProperty("synth", synthToVar(w.synth));
    return var(o);
}
void waveFromVarImpl(const var& v, Wave& w)
{
    auto* o = v.getDynamicObject(); if (!o) return;
    w.used = true; w.name = o->getProperty("name").toString().toStdString(); w.frames.clear();
    w.synth = Synth{};
    if (auto* frames = o->getProperty("frames").getArray())
        for (const auto& fv : *frames) {
            if (w.frames.size() >= size_t(kMaxFrames)) break;
            Frame f; if (auto* s = fv.getArray()) for (int k = 0; k < std::min(32, s->size()); ++k) f.s[size_t(k)] = uint8_t(std::clamp(int((*s)[k]), 0, 15));
            w.frames.push_back(f);
        }
    if (w.frames.empty()) w.frames.push_back(Frame{});
    if (o->hasProperty("synth")) synthFromVar(o->getProperty("synth"), w.synth);
}

var kitToVarImpl(const Kit& k, int slot)
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
void kitFromVarImpl(const var& v, Kit& k)
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
var instrumentToVar(const Instrument& i, int slot) { return instrumentToVarSlot(i, slot); }
var tableToVar(const Table& t, int slot) { return tableToVarImpl(t, slot); }
bool tableFromVar(const var& v, Table& out) { if (!v.getDynamicObject()) return false; tableFromVarImpl(v, out); return true; }
var waveToVar(const Wave& w, int slot) { return waveToVarImpl(w, slot); }
bool waveFromVar(const var& v, Wave& out) { if (!v.getDynamicObject()) return false; waveFromVarImpl(v, out); return true; }
var kitToVar(const Kit& k, int slot) { return kitToVarImpl(k, slot); }
bool kitFromVar(const var& v, Kit& out) { if (!v.getDynamicObject()) return false; kitFromVarImpl(v, out); return true; }
int slotOfVar(const var& v) { auto* o = v.getDynamicObject(); return o ? int(o->getProperty("slot")) : 0; }
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
    // Version 7 is the command encodings of section 34; 1 is everything before
    // them, and its table commands are converted as they are read.
    o->setProperty("version", 7);
    Array<var> ins, tabs, waves, kits;
    for (int i = 0; i < kInstrumentSlots; ++i) if (b.instruments[size_t(i)].used) ins.add(instrumentToVarSlot(b.instruments[size_t(i)], i + 1));
    for (int i = 0; i < kTableSlots; ++i) if (b.tables[size_t(i)].used) tabs.add(tableToVarImpl(b.tables[size_t(i)], i + 1));
    for (int i = 0; i < kWaveSlots; ++i) if (b.waves[size_t(i)].used) waves.add(waveToVarImpl(b.waves[size_t(i)], i + 1));
    for (int i = 0; i < kKitSlots; ++i) if (b.kits[size_t(i)].used) kits.add(kitToVarImpl(b.kits[size_t(i)], i + 1));
    o->setProperty("instruments", ins); o->setProperty("tables", tabs); o->setProperty("waves", waves); o->setProperty("kits", kits);
    return var(o);
}

bool bankFromVar(const var& v, Bank& out)
{
    auto* o = v.getDynamicObject(); if (!o) return false;
    if (o->getProperty("format").toString() != "chipboy-bank") return false;
    const bool oldCommands = getOr(o, "version", 1) < 7;
    // A blank bank is 41 KB and a blank song 83 KB. These read files on the
    // message thread, which a Windows host gives a megabyte of stack, so the
    // blank is built on the heap and moved in rather than made a temporary.
    { const auto blank = std::make_unique<Bank>(); out = std::move(*blank); }
    auto each = [](const var& arr, int maxSlot, auto fn) { if (auto* a = arr.getArray()) for (const auto& e : *a) { const int slot = e.getDynamicObject() ? int(e.getDynamicObject()->getProperty("slot")) : 0; if (slot >= 1 && slot <= maxSlot) fn(e, slot); } };
    each(o->getProperty("instruments"), kInstrumentSlots, [&](const var& e, int slot) { instrumentFromVarImpl(e, out.instruments[size_t(slot - 1)]); });
    each(o->getProperty("tables"), kTableSlots, [&](const var& e, int slot) {
        auto& t = out.tables[size_t(slot - 1)];
        tableFromVarImpl(e, t);
        if (oldCommands) for (auto& st : t.steps) { convertCommandToFormat7(st.cmd1); convertCommandToFormat7(st.cmd2); }
    });
    each(o->getProperty("waves"), kWaveSlots, [&](const var& e, int slot) { waveFromVarImpl(e, out.waves[size_t(slot - 1)]); });
    each(o->getProperty("kits"), kKitSlots, [&](const var& e, int slot) { kitFromVarImpl(e, out.kits[size_t(slot - 1)]); });
    return true;
}

var songToVar(const tracker::Song& s)
{
    auto* o = new DynamicObject();
    // Format 7 (docs/COMMANDS_AND_TEMPO.md section 34): P, S and H changed
    // what their arguments mean, so the version says which encoding the file
    // holds and a format-6 file is converted as it is read. Format 6 (section
    // 25) is where every channel got its own time: a phrase carries its own
    // length -- `steps`, 1-64 -- and its cells are `cells`, only the ones that
    // hold something, each with its step index. In a song *file* the format
    // also embeds the bank (SongFiles.cpp).
    o->setProperty("format", "chipboy-song"); o->setProperty("version", 7);
    // The song's own timeline (docs/COMMANDS_AND_TEMPO.md section 4).
    o->setProperty("tempoBpm", s.tempoBpm); o->setProperty("songStartSeconds", s.songStartSeconds);
    Array<var> phrases;
    for (int i = 0; i < tracker::kPhraseSlots; ++i) {
        const auto& p = s.phrases[size_t(i)]; if (!p.used) continue;
        auto* po = new DynamicObject();
        po->setProperty("slot", i + 1); po->setProperty("groove", int(p.groove)); po->setProperty("steps", p.length());
        Array<var> cells;
        for (int k = 0; k < tracker::kMaxSteps; ++k) {
            const auto& c = p.cells[size_t(k)];
            if (tracker::emptyCell(c)) continue;
            auto* co = new DynamicObject();
            co->setProperty("s", k);
            if (c.note) co->setProperty("n", int(c.note));
            if (c.vel) co->setProperty("v", int(c.vel));
            if (c.inst) co->setProperty("i", int(c.inst));
            if (c.table) co->setProperty("t", int(c.table));
            if (c.cmd1.cmd != Cmd::None) co->setProperty("c1", cmdToVar(c.cmd1));
            if (c.cmd2.cmd != Cmd::None) co->setProperty("c2", cmdToVar(c.cmd2));
            cells.add(var(co));
        }
        po->setProperty("cells", cells); phrases.add(var(po));
    }
    o->setProperty("phrases", phrases);
    Array<var> chains; for (const auto& c : s.chain) { Array<var> a; for (auto p : c) a.add(int(p)); chains.add(a); }
    o->setProperty("chains", chains);
    Array<var> src; for (auto n : s.noteSource) src.add(int(n)); o->setProperty("noteSource", src);
    Array<var> arm; for (auto a : s.recordArm) arm.add(a); o->setProperty("recordArm", arm);
    // A groove is sixteen tick counts (section 9.2); trailing unused entries
    // are left out, so the common two-entry swing still reads as [a, b].
    Array<var> gr;
    for (const auto& g : s.grooves) { Array<var> a; for (int k = 0; k < g.length(); ++k) a.add(int(g.ticks[size_t(k)])); gr.add(a); }
    o->setProperty("grooves", gr);
    return var(o);
}

namespace {

/// A format-5 or older song has bars: one step count for the whole song and,
/// perhaps, an override on a bar. Section 25 converts that to phrase lengths:
/// every used phrase takes the file's steps per bar, and a bar override
/// becomes the length of the phrase in that bar -- so a phrase used under two
/// different overrides has to be two phrases, and the second is a copy in the
/// first free slot.
void lengthsFromBars(tracker::Song& out, int stepsPerBar, const std::vector<int>& barSteps)
{
    const int def = std::clamp(stepsPerBar, 1, tracker::kMaxSteps);
    for (auto& p : out.phrases) if (p.used) p.steps = uint8_t(def);
    std::array<int, tracker::kPhraseSlots + 1> assigned{};      // the length each slot has taken, 0 = none yet
    std::vector<std::pair<int, int>> copies;                    // (original slot, length) -> the copy's slot
    std::vector<int> copySlot;
    int bars = int(barSteps.size());
    for (const auto& c : out.chain) bars = std::max(bars, int(c.size()));
    for (int bar = 0; bar < bars; ++bar) {
        const int over = bar < int(barSteps.size()) ? barSteps[size_t(bar)] : 0;
        const int want = over > 0 ? std::clamp(over, 1, tracker::kMaxSteps) : def;
        for (auto& chain : out.chain) {
            if (size_t(bar) >= chain.size()) continue;
            const int slot = chain[size_t(bar)];
            if (slot < 1 || slot > tracker::kPhraseSlots || !out.phrases[size_t(slot - 1)].used) continue;
            if (assigned[size_t(slot)] == 0) { assigned[size_t(slot)] = want; out.phrases[size_t(slot - 1)].steps = uint8_t(want); continue; }
            if (assigned[size_t(slot)] == want) continue;
            size_t at = 0;
            for (; at < copies.size(); ++at) if (copies[at].first == slot && copies[at].second == want) break;
            if (at == copies.size()) {
                int free = 0;
                for (int i = 0; i < tracker::kPhraseSlots; ++i) if (!out.phrases[size_t(i)].used) { free = i + 1; break; }
                if (free == 0) continue;                        // the song is full: it keeps the length it has
                out.phrases[size_t(free - 1)] = out.phrases[size_t(slot - 1)];
                out.phrases[size_t(free - 1)].steps = uint8_t(want);
                assigned[size_t(free)] = want;
                copies.push_back({ slot, want });
                copySlot.push_back(free);
            }
            chain[size_t(bar)] = uint8_t(copySlot[at]);
        }
    }
}

} // namespace

bool songFromVar(const var& v, tracker::Song& out)
{
    auto* o = v.getDynamicObject(); if (!o) return false;
    if (o->getProperty("format").toString() != "chipboy-song") return false;
    { const auto blank = std::make_unique<tracker::Song>(); out = std::move(*blank); }
    // Before format 7 P, S and H carried their arguments another way (34).
    const bool oldCommands = getOr(o, "version", 1) < 7;
    // Format 6 has no bars at all. Older files carry a step count for the
    // whole song (`steps`, a number since format 4; `stepsPerBar`, 8 or 16,
    // before it) and perhaps a bar override, which become phrase lengths
    // below (section 25).
    const int fileSteps = std::clamp(o->hasProperty("steps") ? getOr(o, "steps", 16) : getOr(o, "stepsPerBar", 16), 1, tracker::kMaxSteps);
    out.tempoBpm = std::clamp(o->hasProperty("tempoBpm") ? double(o->getProperty("tempoBpm")) : 120.0, 40.0, 255.0);
    out.songStartSeconds = std::max(0.0, o->hasProperty("songStartSeconds") ? double(o->getProperty("songStartSeconds")) : 0.0);
    bool haveLengths = false;
    if (auto* ph = o->getProperty("phrases").getArray())
        for (const auto& pv : *ph) {
            auto* po = pv.getDynamicObject(); if (!po) continue;
            const int slot = getOr(po, "slot", 0); if (slot < 1 || slot > tracker::kPhraseSlots) continue;
            auto& p = out.phrases[size_t(slot - 1)]; p.used = true; p.groove = uint8_t(std::clamp(getOr(po, "groove", 0), 0, 16));
            // Format 6: `steps` is the phrase's length and the cells are
            // `cells`. Before it, `steps` *was* the cells.
            const var stepsVar = po->getProperty("steps");
            const Array<var>* cells = po->getProperty("cells").getArray();
            if (!cells && stepsVar.isArray()) cells = stepsVar.getArray();
            else if (!stepsVar.isArray() && po->hasProperty("steps")) { p.steps = uint8_t(std::clamp(getOr(po, "steps", 16), 1, tracker::kMaxSteps)); haveLengths = true; }
            if (cells)
                for (int k = 0; k < cells->size(); ++k) {
                    auto* co = (*cells)[k].getDynamicObject(); if (!co) continue;
                    // Format 4 stamps each cell with its step; before it the
                    // cells were a dense list of sixteen.
                    const int at = co->hasProperty("s") ? getOr(co, "s", 0) : k;
                    if (at < 0 || at >= tracker::kMaxSteps) continue;
                    auto& c = p.cells[size_t(at)];
                    c.note = uint8_t(std::clamp(getOr(co, "n", 0), 0, 255)); c.vel = uint8_t(std::clamp(getOr(co, "v", 0), 0, 127));
                    c.inst = uint8_t(std::clamp(getOr(co, "i", 0), 0, 128)); c.table = uint8_t(std::clamp(getOr(co, "t", 0), 0, 64));
                    c.cmd1 = cmdFromVar(co->getProperty("c1")); c.cmd2 = cmdFromVar(co->getProperty("c2"));
                    if (oldCommands) { convertCommandToFormat7(c.cmd1); convertCommandToFormat7(c.cmd2); }
                }
        }
    if (auto* chains = o->getProperty("chains").getArray())
        for (int ch = 0; ch < std::min(4, chains->size()); ++ch) if (auto* a = (*chains)[ch].getArray()) { out.chain[size_t(ch)].clear(); for (const auto& p : *a) out.chain[size_t(ch)].push_back(uint8_t(std::clamp(int(p), 0, 255))); }
    if (!haveLengths) {
        std::vector<int> barSteps;
        if (auto* bs = o->getProperty("barSteps").getArray())
            for (const auto& v2 : *bs) barSteps.push_back(std::clamp(int(v2), 0, tracker::kMaxSteps));
        lengthsFromBars(out, fileSteps, barSteps);
    }
    // 0 MIDI, 1 Trkr, 2 Hybrid (section 20); a file older than format 5 has
    // only the first two.
    if (auto* src = o->getProperty("noteSource").getArray()) for (int ch = 0; ch < std::min(4, src->size()); ++ch) out.noteSource[size_t(ch)] = tracker::NoteSource(std::clamp(int((*src)[ch]), 0, 2));
    // The arms are on for a song written before they existed (section 14).
    if (auto* arm = o->getProperty("recordArm").getArray()) for (int ch = 0; ch < std::min(4, arm->size()); ++ch) out.recordArm[size_t(ch)] = bool((*arm)[ch]);
    // Sixteen tick counts; the old two-entry form reads as the first two.
    if (auto* gr = o->getProperty("grooves").getArray())
        for (int k = 0; k < std::min(tracker::kGrooveSteps, gr->size()); ++k)
            if (auto* a = (*gr)[k].getArray()) {
                auto& t = out.grooves[size_t(k)].ticks;
                t = {};
                for (int i = 0; i < std::min(int(t.size()), a->size()); ++i) t[size_t(i)] = uint8_t(std::clamp(int((*a)[i]), 0, 48));
                if (t[0] == 0) t[0] = 6;
            }
    tracker::buildRowTables(out);
    return true;
}

String bankToJson(const Bank& b) { return JSON::toString(bankToVar(b), false); }
bool bankFromJson(const String& text, Bank& out) { const var v = JSON::parse(text); return bankFromVar(v, out); }
String songToJson(const tracker::Song& s) { return JSON::toString(songToVar(s), false); }
bool songFromJson(const String& text, tracker::Song& out) { const var v = JSON::parse(text); return songFromVar(v, out); }

} // namespace chipboy::plugin
