#include "core/Driver/Driver.h"

#include <algorithm>
#include <cmath>

namespace chipboy::driver {

using namespace chipboy::bank;

namespace {

constexpr uint32_t kBurstSpacing = 20;     ///< cycles between consecutive writes from one tick: ld a,n / ldh (n),a
constexpr uint32_t kCpuHz = 4194304u;
constexpr int      kMaxTicksPerBlock = 512;

uint16_t regAddr(int ch, int r) { return uint16_t(0xFF10 + ch * 5 + r); }

double noteHz(double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }

int keyswitchBase(InstrumentType t) { return t == InstrumentType::Pulse ? 24 : 12; }   // one octave below the playable floor

uint8_t panBitsFor(Pan p, int ch)
{
    uint8_t b = 0;
    if (p == Pan::Left || p == Pan::Both)  b |= uint8_t(0x10 << ch);
    if (p == Pan::Right || p == Pan::Both) b |= uint8_t(0x01 << ch);
    return b;
}

// NR32 volume code from the 0-3 level (mute, 25, 50, 100).
uint8_t nr32Code(uint8_t level) { static const uint8_t c[4] = { 0, 3, 2, 1 }; return uint8_t(c[level & 3] << 5); }

} // namespace

/* ------------------------------------------------------------ pitch */

int Driver::periodForNote(double note, bool waveChannel)
{
    const double hz = noteHz(note);
    const double p = 2048.0 - (waveChannel ? 65536.0 : 131072.0) / hz;
    if (p < 0.0) return -1;
    return std::min(2047, int(std::lround(p)));
}

double Driver::noiseClockHz(uint8_t shift, uint8_t divisor)
{
    const double r = divisor == 0 ? 0.5 : double(divisor);
    return 524288.0 / r / double(1u << (shift + 1));
}

void Driver::noisePairForNote(int note, uint8_t& shift, uint8_t& divisor)
{
    // The curated map (section 9.4): the pair whose LFSR clock is nearest to
    // 127 x the note's frequency, so 7-bit noise plays in tune and 15-bit
    // noise brightens with pitch. Nearest in log frequency.
    const double target = std::log(127.0 * noteHz(note));
    double best = 1e9; uint8_t bs = 0, bd = 0;
    for (uint8_t s = 0; s <= 13; ++s)
        for (uint8_t d = 0; d <= 7; ++d) {
            const double e = std::fabs(std::log(noiseClockHz(s, d)) - target);
            if (e < best) { best = e; bs = s; bd = d; }
        }
    shift = bs; divisor = bd;
}

/* ----------------------------------------------------------- lifetime */

Driver::Driver()
{
    for (int n = 0; n < 128; ++n) { uint8_t s, d; noisePairForNote(n, s, d); noiseShiftMap_[size_t(n)] = int8_t(s); noiseDivMap_[size_t(n)] = int8_t(d); }
    reset();
}

void Driver::prepare(double sampleRate, const Bank* bank, const tracker::Song* song, Console model)
{
    sampleRate_ = sampleRate; bank_ = bank; song_ = song; model_ = model;
    reset();
}

void Driver::reset()
{
    for (auto& v : v_) v = Voice{};
    for (auto& s : shadow_) s = 0;
    for (auto& k : known_) k = false;     // the first write of anything lands
    masterL_ = masterR_ = 255;
    tickCount_ = 0; haveTick_ = false; lastTickFrame_ = 0;
    pendingCount_ = 0;
    for (auto& vw : view_) vw = VoiceView{};
}

/* ------------------------------------------------------------ writes */

void Driver::emit(uint16_t addr, uint8_t v, bool force)
{
    const size_t idx = size_t(addr - 0xFF10);
    if (idx < shadow_.size()) {
        if (!force && known_[idx] && shadow_[idx] == v) return;
        shadow_[idx] = v; known_[idx] = true;
    }
    out_->push_back({ cycle_ + uint64_t(burst_) * kBurstSpacing, addr, v });
    ++burst_;
}

void Driver::emitAt(uint64_t cycle, uint16_t addr, uint8_t v)
{
    const size_t idx = size_t(addr - 0xFF10);
    if (idx < shadow_.size()) { shadow_[idx] = v; known_[idx] = true; }
    out_->push_back({ cycle, addr, v });
}

uint8_t Driver::levelFromVelocity(uint8_t vel) const { return uint8_t(std::min(15, (vel * 16) / 128)); }

/* --------------------------------------------------------- resolution */

const Instrument* Driver::resolveInstrument(int ch, uint8_t vel)
{
    const auto& p = params_[size_t(ch)];
    if (local_[size_t(ch)] && !v_[size_t(ch)].ksInstrument) return local_[size_t(ch)];
    int slot = v_[size_t(ch)].ksInstrument ? v_[size_t(ch)].ksInstrument : p.instrument;
    if (p.velocityMode == 1 && slot) slot += vel / 8;           // velocity -> instrument bank of 16
    return bank_ ? bank_->instrument(slot) : nullptr;
}

void Driver::latch(int ch)
{
    Voice& v = v_[size_t(ch)];
    v.p = params_[size_t(ch)];
    const auto& p = v.p;
    const auto& i = v.inst;
    v.envVol = p.envVol != 255 ? p.envVol : i.envVol;
    v.envRate = p.envRate != 255 ? p.envRate : i.envRate;
    v.envDir = p.envDir != 255 ? EnvDir(p.envDir & 1) : i.envDir;
    v.duty = p.duty != 255 ? uint8_t(p.duty & 3) : i.duty;
    v.sweepRate = p.sweepRate != 255 ? p.sweepRate : i.sweepRate;
    v.sweepShift = p.sweepShift != 255 ? p.sweepShift : i.sweepShift;
    v.sweepDown = p.sweepDir != 255 ? (p.sweepDir != 0) : i.sweepDown;
    v.lfsr7 = p.lfsr != 255 ? (p.lfsr != 0) : i.lfsr7;
    v.noiseShift = i.noiseShift; v.noiseDiv = i.noiseDivisor; v.noiseSweep = i.noiseSweep;
    v.pan = p.pan != 255 ? Pan(p.pan & 3) : i.pan;
    v.vibShape = i.vib.shape;
    v.vibSpeed = p.vibSpeed != 255 ? std::max<uint8_t>(1, p.vibSpeed) : std::max<uint8_t>(1, i.vib.speed);
    v.vibDepth = p.vibDepth != 255 ? p.vibDepth : i.vib.depth;
    v.vibDelay = i.vib.delay;
    v.waveSlot = p.wave ? p.wave : i.wave;
    v.lengthCode = i.length;
}

/* -------------------------------------------------------------- notes */

void Driver::noteOn(int ch, uint8_t note, uint8_t vel, const NoteEvent* cell)
{
    Voice& v = v_[size_t(ch)];
    const auto& p = params_[size_t(ch)];
    // Keyswitch octave: selects an instrument, never sounds.
    const Instrument* cur = local_[size_t(ch)] ? local_[size_t(ch)] : bank_ ? bank_->instrument(p.instrument) : nullptr;
    const InstrumentType t = cur ? cur->type : (ch == 2 ? InstrumentType::Wave : ch == 3 ? InstrumentType::Noise : InstrumentType::Pulse);
    if (p.keyswitch) {
        const int base = keyswitchBase(t);
        if (note >= base && note < base + 12) { v.ksInstrument = uint8_t(note - base + 1); return; }
    }
    if (v.heldCount < v.held.size()) v.held[v.heldCount++] = note;
    if (cell) {
        if (cell->inst) v.ksInstrument = cell->inst;        // a cell's instrument column selects like a keyswitch
        // A delay command on the cell postpones the start.
        if (cell->cmd1.cmd == Cmd::D || cell->cmd2.cmd == Cmd::D) {
            const auto& d = cell->cmd1.cmd == Cmd::D ? cell->cmd1 : cell->cmd2;
            v.pendingOn = true; v.pendingNote = note; v.pendingVel = vel; v.delay = int16_t(std::max<int>(0, d.a));
            return;
        }
    }
    const bool legato = v.active && v.haveInst && v.inst.legato;
    startVoice(ch, note, vel, legato);
    if (cell) {
        if (cell->table) { v.tableSlot = cell->table; v.tableStep = 0; v.tableOn = bank_ && bank_->table(cell->table); }
        if (cell->cmd1.cmd != Cmd::None && cell->cmd1.cmd != Cmd::D) applyCommand(ch, cell->cmd1, false);
        if (cell->cmd2.cmd != Cmd::None && cell->cmd2.cmd != Cmd::D) applyCommand(ch, cell->cmd2, false);
    }
}

void Driver::noteOff(int ch, uint8_t note)
{
    Voice& v = v_[size_t(ch)];
    // remove from the held stack
    for (uint8_t i = 0; i < v.heldCount; ++i)
        if (v.held[i] == note) { for (uint8_t k = i; k + 1 < v.heldCount; ++k) v.held[k] = v.held[k + 1]; --v.heldCount; break; }
    if (v.pendingOn && v.pendingNote == note) { v.pendingOn = false; return; }
    if (!v.active || v.note != note) return;
    if (v.heldCount > 0) {                 // return to the most recent held note
        startVoice(ch, v.held[v.heldCount - 1], v.vel, v.inst.legato);
        return;
    }
    switch (v.inst.noteOff) {
        case NoteOff::Kill:    stopVoice(ch, true); break;
        case NoteOff::Release: v.active = false; v.tableOn = false; v.sliding = false; v.chordN = 0; break;
        case NoteOff::Ignore:  break;
    }
}

void Driver::startVoice(int ch, uint8_t note, uint8_t vel, bool legato)
{
    Voice& v = v_[size_t(ch)];
    const Instrument* inst = resolveInstrument(ch, vel);
    InstrumentCore core;
    if (inst) core = *inst;
    else core = Instrument::defaults(ch == 2 ? InstrumentType::Wave : ch == 3 ? InstrumentType::Noise : InstrumentType::Pulse);
    // A channel can only host its own kind (section 9.2).
    const bool typeOk = (ch < 2 && core.type == InstrumentType::Pulse) || (ch == 2 && (core.type == InstrumentType::Wave || core.type == InstrumentType::Kit)) || (ch == 3 && core.type == InstrumentType::Noise);
    if (!typeOk) core = Instrument::defaults(ch == 2 ? InstrumentType::Wave : ch == 3 ? InstrumentType::Noise : InstrumentType::Pulse);

    const bool wasActive = v.active;
    v.inst = core; v.haveInst = true;
    latch(ch);
    v.note = note; v.vel = vel; v.active = true; v.killed = false;
    v.ticks = 0; v.vibPos = 0; v.pOffset = 0; v.sliding = false; v.chordN = 0; v.chordIdx = 0;
    v.dutyIdx = 0; v.kill = -1; v.retrigEvery = 0; v.retrigCount = 0; v.lastCmd = {};
    v.rng = v.rng * 1664525u + 1013904223u + note;
    // volume from velocity
    if (v.p.velocityMode == 0 && (core.type == InstrumentType::Pulse || core.type == InstrumentType::Noise)) v.envVol = levelFromVelocity(vel);
    if (v.p.level != 255) { if (core.type == InstrumentType::Wave || core.type == InstrumentType::Kit) v.inst.waveLevel = uint8_t(v.p.level & 3); else v.envVol = uint8_t(v.p.level & 15); }
    // table
    const uint8_t tbl = v.p.table ? v.p.table : core.table;
    v.tableSlot = tbl; v.tableStep = 0; v.tableOn = tbl && bank_ && bank_->table(tbl);
    v.arpSlot = v.p.arp; v.arpStep = 0;
    if (core.dutySeqLen) v.duty = uint8_t(core.dutySeq[0] & 3);

    const int base = computePeriod(ch);
    if (base < 0 && core.type != InstrumentType::Noise && core.type != InstrumentType::Kit) {
        // Below the chip's range: does not sound (C4).
        v.basePeriod = 0; stopVoice(ch, true); v.active = true; view_[size_t(ch)].outOfRange = true;
        return;
    }
    view_[size_t(ch)].outOfRange = false;

    if (legato && wasActive && !v.killed && core.type != InstrumentType::Kit) {
        writePeriod(ch, false);
        writeNr51();
        return;
    }

    switch (core.type) {
        case InstrumentType::Pulse: {
            if (ch == 0) emit(regAddr(0, 0), uint8_t((v.sweepRate << 4) | (v.sweepDown ? 8 : 0) | v.sweepShift), true);
            const uint8_t len = core.length ? uint8_t(64 - std::min<int>(64, core.length)) : 0;
            emit(regAddr(ch, 1), uint8_t((v.duty << 6) | (len & 0x3F)), true);
            v.dacOn = true;
            writeEnvelope(ch, false);
            writePeriod(ch, true);
            break;
        }
        case InstrumentType::Wave: {
            const Wave* w = bank_ ? bank_->wave(v.waveSlot) : nullptr;
            const Frame* f = w && !w->frames.empty() ? &w->frames[0] : nullptr;
            v.frameIdx = 0; v.frameCount = 0; v.frameDir = 1; v.kitOn = false; v.streamActive = false;
            if (v.p.frame && w) v.frameIdx = uint8_t(std::min<int>(v.p.frame - 1, int(w->frames.size()) - 1));
            static const Frame silent{};
            if (w && !w->frames.empty()) f = &w->frames[v.frameIdx];
            loadFrame(ch, f ? *f : silent, true);
            break;
        }
        case InstrumentType::Kit: {
            const Kit* kit = bank_ ? bank_->kit(core.kit) : nullptr;
            v.kitOn = false; v.streamActive = false;
            if (!kit || kit->samples.empty()) { stopVoice(ch, true); break; }
            // the sample mapped to this note, else the nearest below
            int best = -1; int bestDist = 1000;
            for (size_t i = 0; i < kit->samples.size(); ++i) { const int d = std::abs(int(kit->samples[i].note) - int(note)); if (d < bestDist) { bestDist = d; best = int(i); } }
            v.kitIdx = uint8_t(best); v.kitPos = 0; v.kitLoopsStreamed = 0;
            v.kitLen = uint32_t(kit->samples[size_t(best)].data.size());
            v.kitLoopPoint = std::min(kit->samples[size_t(best)].loopPoint, v.kitLen);
            v.kitLoop = core.kitLoop;
            v.kitOn = v.kitLen > 0;
            std::array<uint8_t, 16> chunk{}; bool ended = false;
            kitNextChunk(ch, chunk, ended);
            Frame f; for (int i = 0; i < 16; ++i) { f.s[size_t(i * 2)] = uint8_t(chunk[size_t(i)] >> 4); f.s[size_t(i * 2 + 1)] = uint8_t(chunk[size_t(i)] & 15); }
            loadFrame(ch, f, true);
            if (model_ == Console::CGB && v.kitOn) {
                // CGB: the next chunk streams in behind the read pointer.
                std::array<uint8_t, 16> next{}; bool e2 = false;
                kitNextChunk(ch, next, e2);
                v.streamData = next; v.streamActive = true; v.streamByte = 0;
                if (e2 && v.kitLoop == KitLoop::Once) v.kitOn = false;
            }
            break;
        }
        case InstrumentType::Noise: {
            const uint8_t len = core.length ? uint8_t(64 - std::min<int>(64, core.length)) : 0;
            emit(regAddr(3, 1), uint8_t(len & 0x3F), true);
            v.dacOn = true;
            writeEnvelope(ch, false);
            writePeriod(ch, true);
            break;
        }
    }
    writeNr51();
}

void Driver::stopVoice(int ch, bool kill)
{
    Voice& v = v_[size_t(ch)];
    v.active = false; v.tableOn = false; v.sliding = false; v.chordN = 0; v.kitOn = false; v.streamActive = false; v.pendingOn = false;
    if (kill && v.dacOn) {
        // Kill clears the DAC: on this hardware that holds the level, so it
        // is silent (reference section 9).
        if (ch == 2) emit(regAddr(2, 0), 0x00, true);
        else emit(regAddr(ch, 2), 0x00, true);
        v.dacOn = false; v.killed = true;
    }
}

/* ------------------------------------------------------------- pitch */

int Driver::computePeriod(int ch)
{
    Voice& v = v_[size_t(ch)];
    const auto& p = v.p;
    if (v.inst.type == InstrumentType::Noise) return 0;
    if (v.inst.type == InstrumentType::Kit) {
        // Pitch and rate are one control: the kit's period, transposed.
        const Kit* kit = bank_ ? bank_->kit(v.inst.kit) : nullptr;
        const double rate = kit ? sampleRateForPeriod(kit->period) : 11468.0;
        const double semis = p.transpose + v.bend;
        const double r = rate * std::pow(2.0, semis / 12.0);
        const double per = 2048.0 - 2097152.0 / std::max(1024.0, r);
        return std::clamp(int(std::lround(per)) + p.detune + v.pOffset, 0, 2047);
    }
    double note = v.note + p.transpose + v.bend;
    if (v.chordN) note += v.chord[v.chordIdx % v.chordN];
    // table / arp transpose columns
    if (v.tableOn && v.inst.transpose) { const Table* t = bank_->table(v.tableSlot); if (t) { const auto& s = t->steps[v.tableStep]; if (s.hasTranspose) note += s.transpose; } }
    if (v.arpSlot) { const Table* a = bank_ ? bank_->table(v.arpSlot) : nullptr; if (a) { const auto& s = a->steps[v.arpStep]; if (s.hasTranspose) note += s.transpose; } }
    int per = periodForNote(note, v.inst.type == InstrumentType::Wave);
    if (per < 0) return -1;
    // vibrato: signed period offset, recomputed per tick (section 8.4)
    int vib = 0;
    if (v.vibDepth && v.ticks >= v.vibDelay) {
        const int step = v.vibSpeed;
        const int pos = v.vibPos;
        switch (v.vibShape) {
            case VibShape::Triangle: { const int cyc = step * 4; const int ph = pos % cyc; const double tri = ph < cyc / 2 ? (ph * 2.0 / cyc) : (2.0 - ph * 2.0 / cyc); vib = int(std::lround((tri * 2.0 - 1.0) * v.vibDepth)); break; }
            case VibShape::Square:   vib = ((pos / step) & 1) ? -v.vibDepth : v.vibDepth; break;
            case VibShape::SawUp:    { const int cyc = step * 2; vib = int(std::lround((double(pos % cyc) / cyc * 2.0 - 1.0) * v.vibDepth)); break; }
            case VibShape::SawDown:  { const int cyc = step * 2; vib = int(std::lround((1.0 - double(pos % cyc) / cyc * 2.0) * v.vibDepth)); break; }
        }
    }
    per += p.detune + v.pOffset + vib;
    if (v.sliding) per = v.basePeriod;    // slides own the period until they arrive
    return std::clamp(per, 0, 2047);
}

void Driver::writePeriod(int ch, bool trigger)
{
    Voice& v = v_[size_t(ch)];
    if (v.inst.type == InstrumentType::Noise) {
        uint8_t s, d;
        if (v.inst.noiseManual) { s = v.noiseShift; d = v.noiseDiv; }
        else { s = uint8_t(noiseShiftMap_[size_t(std::clamp(int(v.note) + v.p.transpose, 0, 127))]); d = uint8_t(noiseDivMap_[size_t(std::clamp(int(v.note) + v.p.transpose, 0, 127))]); s = uint8_t(std::clamp(int(s) + int(v.noiseShift) - 5 + v.pOffset, 0, 13)); }
        v.noiseShift = s; v.noiseDiv = d;
        emit(regAddr(3, 3), uint8_t((s << 4) | (v.lfsr7 ? 8 : 0) | (d & 7)));
        if (trigger) emit(regAddr(3, 4), uint8_t(0x80 | (v.inst.length ? 0x40 : 0)), true);
        v.lastPeriod = int16_t((s << 4) | d);
        return;
    }
    int per = computePeriod(ch);
    if (per < 0) per = 0;
    v.basePeriod = int16_t(per);
    const uint16_t f = uint16_t(per);
    const bool changed = v.lastPeriod != int16_t(f);
    if (changed || trigger) {
        emit(regAddr(ch, 3), uint8_t(f & 0xFF), trigger || ((v.lastPeriod & 0xFF) != (f & 0xFF)));
        const uint8_t hi = uint8_t((f >> 8) | (trigger ? 0x80 : 0) | (v.inst.length ? 0x40 : 0));
        emit(regAddr(ch, 4), hi, trigger || ((v.lastPeriod >> 8) != (f >> 8)));
        if (ch == 2) updateWaveTimer(ch, f, trigger);
    }
    v.lastPeriod = int16_t(f);
}

void Driver::writeEnvelope(int ch, bool trigger)
{
    Voice& v = v_[size_t(ch)];
    if (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) {
        emit(regAddr(2, 2), nr32Code(v.inst.waveLevel));
        return;
    }
    const uint8_t nr2 = uint8_t((v.envVol << 4) | (v.envDir == EnvDir::Up ? 8 : 0) | (v.envRate & 7));
    // Rewriting NRx2 on a running channel is zombie mode; a driver that wants
    // a clean level change writes the register and then retriggers, which
    // restarts the envelope but keeps the duty phase (reference section 4).
    emit(regAddr(ch, 2), nr2, true);
    v.dacOn = (nr2 & 0xF8) != 0;
    v.volume = v.envVol;
    if (trigger) {
        const uint16_t f = uint16_t(std::max<int16_t>(0, v.lastPeriod));
        if (v.inst.type == InstrumentType::Noise) emit(regAddr(3, 4), uint8_t(0x80 | (v.inst.length ? 0x40 : 0)), true);
        else emit(regAddr(ch, 4), uint8_t((f >> 8) | 0x80 | (v.inst.length ? 0x40 : 0)), true);
    }
}

void Driver::writeNr51()
{
    uint8_t bits = 0;
    for (int ch = 0; ch < 4; ++ch) bits |= panBitsFor(v_[size_t(ch)].pan, ch);
    emit(0xFF25, bits);
}

void Driver::writeNr50(uint8_t l, uint8_t r) { emit(0xFF24, uint8_t(((l & 7) << 4) | (r & 7))); }

/* --------------------------------------------------------- wave RAM */

void Driver::updateWaveTimer(int ch, uint16_t freq, bool trigger)
{
    Voice& v = v_[size_t(ch)];
    const uint32_t period = uint32_t(2048 - freq) * 2;
    if (trigger) {
        // The trigger's write is the last one emitted; its cycle is where the
        // channel restarts. First fetch one period plus the measured six
        // cycles later (reference section 5).
        const uint64_t at = out_->back().cycle;
        v.nextFetch = at + period + 6; v.fetchIndex = 1; v.fetchPeriod = period; v.timerValid = true;
    } else {
        v.fetchPeriod = period;   // the fetch in flight keeps its old time
    }
}

void Driver::loadFrame(int ch, const Frame& f, bool trigger)
{
    Voice& v = v_[size_t(ch)];
    std::array<uint8_t, 16> bytes{};
    for (int i = 0; i < 16; ++i) bytes[size_t(i)] = uint8_t((f.s[size_t(i * 2)] << 4) | (f.s[size_t(i * 2 + 1)] & 15));
    const bool same = v.ramValid && bytes == v.ram;
    if (model_ == Console::CGB && v.dacOn && !trigger && v.timerValid) {
        // CGB: writable live, so stream the new frame one loop behind the
        // read pointer -- no click, one waveform's delay (section 6.5).
        if (!same) { v.streamData = bytes; v.streamActive = true; v.streamByte = 0; v.ram = bytes; v.ramValid = true; }
        return;
    }
    if (!same || trigger) {
        // DMG: the channel must be off to reach wave RAM. This is the click.
        emit(regAddr(2, 0), 0x00, true);
        v.dacOn = false;
        if (!same) for (int i = 0; i < 16; ++i) emit(uint16_t(0xFF30 + i), bytes[size_t(i)], true);
        v.ram = bytes; v.ramValid = true;
    }
    const uint8_t len = v.inst.length ? uint8_t(256 - std::min<int>(256, v.inst.length)) : 0;
    emit(regAddr(2, 1), len);
    emit(regAddr(2, 0), 0x80, true);
    v.dacOn = true;
    writeEnvelope(ch, false);
    writePeriod(ch, true);
}

void Driver::kitNextChunk(int ch, std::array<uint8_t, 16>& chunk, bool& ended)
{
    Voice& v = v_[size_t(ch)];
    const Kit* kit = bank_ ? bank_->kit(v.inst.kit) : nullptr;
    ended = false;
    if (!kit || v.kitIdx >= kit->samples.size()) { chunk.fill(0x88); ended = true; return; }
    const auto& data = kit->samples[v.kitIdx].data;
    for (int i = 0; i < 32; ++i) {
        uint8_t s = 8;
        if (v.kitPos < v.kitLen) s = data[v.kitPos++];
        else if (v.kitLoop == KitLoop::Loop) { v.kitPos = 0; s = v.kitLen ? data[v.kitPos++] : 8; }
        else if (v.kitLoop == KitLoop::FromPoint && v.kitLoopPoint < v.kitLen) { v.kitPos = v.kitLoopPoint; s = data[v.kitPos++]; }
        else ended = true;
        if (i & 1) chunk[size_t(i >> 1)] = uint8_t(chunk[size_t(i >> 1)] | (s & 15));
        else chunk[size_t(i >> 1)] = uint8_t(s << 4);
    }
}

void Driver::scheduleStreams(uint64_t cycleStart, uint64_t cycleEnd)
{
    Voice& v = v_[2];
    if (!v.timerValid || !v.dacOn) return;
    if (!v.kitOn && !v.streamActive) return;
    // Walk the channel's fetches through this block.
    int guard = 0;
    while (v.nextFetch < cycleEnd && guard++ < 200000) {
        const uint64_t t = v.nextFetch;
        const uint32_t pos = v.fetchIndex & 31;          // position after this fetch
        if (model_ == Console::CGB) {
            // Trailing writes: during the second nibble of byte j, write byte j for the next loop.
            if ((pos & 1) == 1 && v.streamActive) {
                const uint8_t j = uint8_t(pos >> 1);
                if (j == v.streamByte) {
                    if (t + v.fetchPeriod / 2 >= cycleStart) emitAt(t + v.fetchPeriod / 2, uint16_t(0xFF30 + j), v.streamData[j]);
                    if (++v.streamByte >= 16) {
                        v.streamActive = false;
                        if (v.kitOn) { std::array<uint8_t, 16> chunk{}; bool ended = false; kitNextChunk(2, chunk, ended); if (ended && v.kitLoop == KitLoop::Once) { v.kitOn = false; v.streamData.fill(0x88); } else { v.streamData = chunk; } v.streamActive = true; v.streamByte = 0; }
                    }
                }
            }
        } else if (v.kitOn && pos == 31) {
            // DMG: refill before fetch 32 wraps the position. The channel is
            // switched off for the burst, which is the granularity of DMG
            // sample playback (section 9.8).
            std::array<uint8_t, 16> chunk{}; bool ended = false;
            kitNextChunk(2, chunk, ended);
            const uint64_t at = t + v.fetchPeriod - 20 * 19 - 8;
            uint64_t c = std::max(at, cycleStart);
            if (ended && v.kitLoop == KitLoop::Once) {
                emitAt(c, 0xFF1A, 0x00);   // done: DAC off, silence
                v.dacOn = false; v.kitOn = false; v.timerValid = false; v.active = false;
                return;
            }
            emitAt(c, 0xFF1A, 0x00); c += kBurstSpacing;
            for (int i = 0; i < 16; ++i) { emitAt(c, uint16_t(0xFF30 + i), chunk[size_t(i)]); c += kBurstSpacing; }
            v.ram = chunk;
            emitAt(c, 0xFF1A, 0x80); c += kBurstSpacing;
            const uint16_t f = uint16_t(std::max<int16_t>(0, v.lastPeriod));
            emitAt(c, 0xFF1E, uint8_t((f >> 8) | 0x80 | (v.inst.length ? 0x40 : 0)));
            v.nextFetch = c + v.fetchPeriod + 6; v.fetchIndex = 1;
            ++v.kitLoopsStreamed;
            continue;
        }
        v.nextFetch = t + v.fetchPeriod;
        v.fetchIndex = (v.fetchIndex + 1) & 31;
    }
}

/* ----------------------------------------------------------- commands */

void Driver::applyCommand(int ch, const Command& cIn, bool fromTable)
{
    Voice& v = v_[size_t(ch)];
    Command c = cIn;
    if (c.cmd == Cmd::Z) {
        // Randomise the previous command's argument by up to the given amount.
        if (v.lastCmd.cmd == Cmd::None) return;
        v.rng = v.rng * 1664525u + 1013904223u;
        c = v.lastCmd; c.a = int16_t(c.a + int((v.rng >> 16) % uint32_t(std::max<int>(1, cIn.a + 1))));
    }
    v.lastCmd = c;
    switch (c.cmd) {
        case Cmd::A: {
            v.envVol = uint8_t(std::clamp<int>(c.a, 0, 15)); v.envRate = uint8_t(std::clamp<int>(c.b, 0, 7)); v.envDir = c.c ? EnvDir::Up : EnvDir::Down;
            if (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) v.inst.waveLevel = uint8_t(std::clamp<int>(c.a / 4, 0, 3));
            if (v.active) writeEnvelope(ch, v.inst.type == InstrumentType::Pulse || v.inst.type == InstrumentType::Noise);
            break;
        }
        case Cmd::C: v.chord[0] = 0; v.chord[1] = uint8_t(std::clamp<int>(c.a, 0, 15)); v.chord[2] = uint8_t(std::clamp<int>(c.b, 0, 15)); v.chordN = c.b ? 3 : (c.a ? 2 : 0); v.chordIdx = 0; break;
        case Cmd::D: if (fromTable) v.delay = int16_t(std::max<int>(0, c.a)); break;
        case Cmd::F: if (v.inst.type == InstrumentType::Wave) { const Wave* w = bank_ ? bank_->wave(v.waveSlot) : nullptr; if (w && !w->frames.empty()) { v.frameIdx = uint8_t(std::clamp<int>(c.a - 1, 0, int(w->frames.size()) - 1)); v.frameCount = 0; if (v.active) loadFrame(ch, w->frames[v.frameIdx], model_ == Console::DMG); } } break;
        case Cmd::H: if (fromTable) { if (c.a <= 0) v.tableOn = false; else v.tableStep = uint8_t(std::clamp<int>(c.a - 1, 0, 15)); } break;
        case Cmd::K: v.kill = int16_t(std::max<int>(0, c.a)); break;
        case Cmd::L: v.slideRate = uint8_t(std::clamp<int>(c.a, 0, 15)); v.sliding = v.slideRate > 0; v.slideTarget = int16_t(std::clamp(periodForNote(v.note + v.p.transpose, v.inst.type == InstrumentType::Wave), 0, 2047)); break;
        case Cmd::M: writeNr50(uint8_t(std::clamp<int>(c.a, 0, 7)), uint8_t(std::clamp<int>(c.b, 0, 7))); break;
        case Cmd::O: v.pan = Pan(std::clamp<int>(c.a, 0, 3)); writeNr51(); break;
        case Cmd::P: v.pOffset = int16_t(std::clamp<int>(c.a, -128, 127)); if (v.active) writePeriod(ch, false); break;
        case Cmd::R: v.retrigEvery = uint8_t(std::clamp<int>(c.a, 0, 15)); v.retrigCount = 0; break;
        case Cmd::S: {
            if (ch == 0) {
                v.sweepRate = 1; v.sweepDown = c.a < 0; v.sweepShift = uint8_t(std::clamp<int>(std::abs(c.a), 0, 7));
                emit(0xFF10, uint8_t((v.sweepRate << 4) | (v.sweepDown ? 8 : 0) | v.sweepShift), true);
                if (v.active) writePeriod(ch, true);
            } else if (ch == 3) {
                v.noiseSweep = int8_t(std::clamp<int>(c.a, -7, 7));
            }
            break;
        }
        case Cmd::V: v.vibSpeed = uint8_t(std::clamp<int>(c.a, 1, 15)); v.vibDepth = uint8_t(std::clamp<int>(c.b, 0, 15)); v.vibDelay = 0; break;
        case Cmd::W: if (v.inst.type == InstrumentType::Wave) { const Wave* w = bank_ ? bank_->wave(uint8_t(c.a)) : nullptr; if (w && !w->frames.empty()) { v.waveSlot = uint8_t(c.a); v.frameIdx = 0; v.frameCount = 0; if (v.active) loadFrame(ch, w->frames[0], model_ == Console::DMG); } } break;
        default: break;
    }
}

void Driver::stepTable(int ch)
{
    Voice& v = v_[size_t(ch)];
    if (!v.tableOn) return;
    const Table* t = bank_ ? bank_->table(v.tableSlot) : nullptr;
    if (!t) { v.tableOn = false; return; }
    if (v.delay > 0) { --v.delay; return; }
    const TableStep& s = t->steps[v.tableStep];
    if (s.vol >= 0) {
        if (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) { v.inst.waveLevel = uint8_t(std::clamp<int>(s.vol / 4, 0, 3)); if (v.active) writeEnvelope(ch, false); }
        else { v.envVol = uint8_t(s.vol); if (v.active) writeEnvelope(ch, true); }
    }
    if (s.cmd1.cmd != Cmd::None) applyCommand(ch, s.cmd1, true);
    if (s.cmd2.cmd != Cmd::None) applyCommand(ch, s.cmd2, true);
    if (!v.tableOn) return;                       // H 0 stopped it
    if (s.cmd1.cmd == Cmd::H || s.cmd2.cmd == Cmd::H) return;   // hopped: step already set
    if (v.tableStep + 1 < kTableSteps) { ++v.tableStep; return; }
    switch (t->end) {
        case TableEnd::Loop: v.tableStep = 0; break;
        case TableEnd::Hop:  v.tableStep = uint8_t(std::clamp<int>(t->hopStep - 1, 0, 15)); break;
        case TableEnd::Stop: v.tableOn = false; break;
    }
}

/* --------------------------------------------------------------- tick */

void Driver::tick(int ch)
{
    Voice& v = v_[size_t(ch)];
    if (v.p.liveFollow && v.haveInst) {
        // Parameters apply to the sounding note: re-latch and push differences.
        const ChannelParams before = v.p;
        latch(ch);
        if (v.active) {
            if (before.level != v.p.level || before.envVol != v.p.envVol || before.envRate != v.p.envRate || before.envDir != v.p.envDir) {
                if (v.p.level != 255) { if (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) v.inst.waveLevel = uint8_t(v.p.level & 3); else v.envVol = uint8_t(v.p.level & 15); }
                writeEnvelope(ch, v.inst.type == InstrumentType::Pulse || v.inst.type == InstrumentType::Noise);
            }
            if (before.duty != v.p.duty && v.inst.type == InstrumentType::Pulse) emit(regAddr(ch, 1), uint8_t((v.duty << 6) | (v.inst.length ? (64 - std::min<int>(64, v.inst.length)) & 0x3F : 0)));
            if (before.pan != v.p.pan) writeNr51();
        }
    }
    // pending delayed start
    if (v.pendingOn) {
        if (v.delay > 0) { --v.delay; }
        else { v.pendingOn = false; startVoice(ch, v.pendingNote, v.pendingVel, false); }
    }
    if (!v.active) return;
    ++v.ticks;
    // kill countdown
    if (v.kill >= 0) { if (v.kill == 0) { stopVoice(ch, true); v.kill = -1; return; } --v.kill; }
    // table and arpeggio
    stepTable(ch);
    if (!v.active) return;
    if (v.arpSlot) { const Table* a = bank_ ? bank_->table(v.arpSlot) : nullptr; if (a) v.arpStep = uint8_t((v.arpStep + 1) & 15); else v.arpSlot = 0; }
    if (v.chordN) v.chordIdx = uint8_t((v.chordIdx + 1) % v.chordN);
    // duty sequence
    if (v.inst.type == InstrumentType::Pulse && v.inst.dutySeqLen) {
        v.dutyIdx = uint8_t((v.dutyIdx + 1) % v.inst.dutySeqLen);
        const uint8_t d = uint8_t(v.inst.dutySeq[v.dutyIdx] & 3);
        if (d != v.duty) { v.duty = d; emit(regAddr(ch, 1), uint8_t((d << 6) | (v.inst.length ? (64 - std::min<int>(64, v.inst.length)) & 0x3F : 0))); }
    }
    // vibrato phase
    if (v.ticks >= v.vibDelay) ++v.vibPos;
    // slide
    if (v.sliding) {
        const int cur = v.basePeriod, tgt = v.slideTarget, step = v.slideRate * 2;
        if (std::abs(tgt - cur) <= step) { v.basePeriod = int16_t(tgt); v.sliding = false; }
        else v.basePeriod = int16_t(cur + (tgt > cur ? step : -step));
    }
    // noise sweep
    if (v.inst.type == InstrumentType::Noise && v.noiseSweep) { v.noiseShift = uint8_t(std::clamp<int>(int(v.noiseShift) + v.noiseSweep, 0, 13)); v.inst.noiseManual = true; }
    // retrigger
    bool retrig = false;
    if (v.retrigEvery) { if (++v.retrigCount >= v.retrigEvery) { v.retrigCount = 0; retrig = true; } }
    // wave frames
    if (v.inst.type == InstrumentType::Wave && v.inst.frameAdvance && !v.p.frame) {
        if (++v.frameCount >= v.inst.frameAdvance) {
            v.frameCount = 0;
            const Wave* w = bank_ ? bank_->wave(v.waveSlot) : nullptr;
            if (w && w->frames.size() > 1) {
                const int n = int(w->frames.size());
                int next = v.frameIdx + v.frameDir;
                switch (v.inst.frameLoop) {
                    case FrameLoop::Loop:     next = (next + n) % n; break;
                    case FrameLoop::Once:     if (next >= n) next = n - 1; break;
                    case FrameLoop::PingPong: if (next >= n) { next = n - 2; v.frameDir = -1; } else if (next < 0) { next = 1; v.frameDir = 1; } break;
                }
                if (next != v.frameIdx) { v.frameIdx = uint8_t(next); loadFrame(ch, w->frames[size_t(next)], model_ == Console::DMG); }
            }
        }
    }
    // pitch for this tick
    if (v.inst.type == InstrumentType::Noise) { if (v.noiseSweep || v.pOffset) writePeriod(ch, retrig); else if (retrig) writePeriod(ch, true); }
    else if (v.inst.type == InstrumentType::Kit) { if (retrig) writePeriod(ch, true); }
    else writePeriod(ch, retrig);
    if (retrig && (v.inst.type == InstrumentType::Pulse || v.inst.type == InstrumentType::Noise)) writeEnvelope(ch, true);
}

void Driver::tickAll()
{
    // Master volume from the parameters, when they change (an M command holds until then).
    if (global_.masterL != masterL_ || global_.masterR != masterR_) { masterL_ = global_.masterL; masterR_ = global_.masterR; writeNr50(masterL_, masterR_); }
    for (int ch = 0; ch < 4; ++ch) tick(ch);
    ++tickCount_;
}

/* ------------------------------------------------------------- events */

void Driver::handleEvent(const NoteEvent& e)
{
    const int ch = e.channel & 3;
    Voice& v = v_[size_t(ch)];
    // Channels playing from the tracker ignore the piano roll and vice versa.
    if (song_) {
        const bool trackerCh = song_->noteSource[size_t(ch)] == tracker::NoteSource::Tracker;
        const bool note = e.kind == NoteEvent::NoteOn || e.kind == NoteEvent::NoteOff;
        if (note && e.source == NoteEvent::Tracker && !trackerCh) return;
        if (note && e.source == NoteEvent::Midi && trackerCh && !recording_) return;
    }
    switch (e.kind) {
        case NoteEvent::NoteOn:
            if (e.b == 0) { noteOff(ch, e.a); break; }
            noteOn(ch, e.a, e.b, e.source == NoteEvent::Tracker ? &e : nullptr);
            break;
        case NoteEvent::NoteOff: noteOff(ch, e.a); break;
        case NoteEvent::PitchBend: v.bend = double(e.value) / 8192.0 * 2.0; if (v.active) writePeriod(ch, false); break;
        case NoteEvent::Control:
            if (e.a == 1) { v.vibDepth = uint8_t(e.b / 8); if (!v.p.liveFollow) params_[size_t(ch)].vibDepth = v.vibDepth; }
            else if (e.a == 7 && v.active) { if (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) { v.inst.waveLevel = uint8_t(e.b / 32); writeEnvelope(ch, false); } else { v.envVol = uint8_t(e.b / 8); writeEnvelope(ch, true); } }
            else if (e.a == 120 || e.a == 123) { v.heldCount = 0; if (v.active) stopVoice(ch, true); }
            break;
        case NoteEvent::AllNotesOff: v.heldCount = 0; if (v.active) stopVoice(ch, true); break;
        case NoteEvent::Command:
            // A tracker cell with no note: its columns apply from here on.
            if (e.inst) v.ksInstrument = e.inst;
            if (e.table) { v.tableSlot = e.table; v.tableStep = 0; v.tableOn = v.active && bank_ && bank_->table(e.table); }
            if (e.cmd1.cmd != Cmd::None) applyCommand(ch, e.cmd1, false);
            if (e.cmd2.cmd != Cmd::None) applyCommand(ch, e.cmd2, false);
            break;
    }
}

/* ------------------------------------------------------------ process */

void Driver::process(const NoteEvent* events, size_t n, uint32_t numSamples, uint64_t frameAbs,
                     const Transport& t, const std::function<uint64_t(uint64_t)>& cycleAt,
                     std::vector<RegWrite>& out)
{
    out_ = &out;
    const uint64_t blockEnd = frameAbs + numSamples;

    // --- tick boundaries in this block, from absolute positions ---------
    std::array<uint32_t, kMaxTicksPerBlock> ticks{}; int nTicks = 0;
    if (global_.tick == TickSource::Host) {
        const double bpm = t.valid && t.bpm > 1.0 ? t.bpm : lastBpm_;
        lastBpm_ = bpm;
        const double tpb = std::max<int>(1, global_.ticksPerBeat);
        const double framesPerTick = sampleRate_ * 60.0 / (bpm * tpb);
        if (t.valid && t.playing) {
            // Tick k sits at ppq k/tpb; find those inside [ppqStart, ppqEnd).
            const double ppqPerFrame = bpm / 60.0 / sampleRate_;
            const double ppqStart = t.ppq, ppqEnd = t.ppq + numSamples * ppqPerFrame;
            double k = std::ceil(ppqStart * tpb - 1e-9);
            for (; nTicks < kMaxTicksPerBlock; k += 1.0) {
                const double ppq = k / tpb;
                if (ppq >= ppqEnd) break;
                const double f = (ppq - ppqStart) / ppqPerFrame;
                const uint32_t off = uint32_t(std::max(0.0, std::floor(f + 1e-6)));
                if (off >= numSamples) break;
                ticks[size_t(nTicks++)] = off;
            }
            haveTick_ = false;   // free-running phase restarts when the transport stops
        } else {
            // Stopped: free-run at the tempo from the absolute frame count.
            double k = haveTick_ ? std::floor((double(lastTickFrame_) + framesPerTick) / framesPerTick) : std::ceil(double(frameAbs) / framesPerTick);
            for (; nTicks < kMaxTicksPerBlock; k += 1.0) {
                const uint64_t f = uint64_t(std::llround(k * framesPerTick));
                if (f >= blockEnd) break;
                if (f >= frameAbs) { ticks[size_t(nTicks++)] = uint32_t(f - frameAbs); lastTickFrame_ = f; haveTick_ = true; }
            }
        }
    } else {
        const double hz = global_.tick == TickSource::VBlank ? 59.7275 : std::clamp(global_.customHz, 1.0, 240.0);
        const double framesPerTick = sampleRate_ / hz;
        double k = std::ceil(double(frameAbs) / framesPerTick - 1e-9);
        for (; nTicks < kMaxTicksPerBlock; k += 1.0) {
            const uint64_t f = uint64_t(std::llround(k * framesPerTick));
            if (f >= blockEnd) break;
            if (f >= frameAbs) ticks[size_t(nTicks++)] = uint32_t(f - frameAbs);
        }
    }

    // --- merge pending events (from the previous block) with this block's -
    size_t pi = 0, ei = 0;
    auto nextEvent = [&](uint32_t upTo, const NoteEvent*& e) -> bool {
        if (pi < pendingCount_) { e = &pending_[pi]; return true; }              // pending always first
        if (ei < n && events[ei].offset <= upTo) { e = &events[ei]; return true; }
        return false;
    };
    for (int k = 0; k < nTicks; ++k) {
        const uint32_t off = ticks[size_t(k)];
        cycle_ = cycleAt(frameAbs + off); burst_ = 0;
        const NoteEvent* e = nullptr;
        while (nextEvent(off, e)) { handleEvent(*e); if (pi < pendingCount_) ++pi; else ++ei; }
        tickAll();
    }
    // Events after the last tick wait for the next block's first tick.
    size_t keep = 0;
    for (size_t i = pi; i < pendingCount_ && keep < pending_.size(); ++i) pending_[keep++] = pending_[i];
    for (size_t i = ei; i < n && keep < pending_.size(); ++i) { pending_[keep] = events[i]; pending_[keep].offset = 0; ++keep; }
    pendingCount_ = keep;

    // --- wave RAM streaming, cycle domain ---------------------------------
    scheduleStreams(cycleAt(frameAbs), cycleAt(blockEnd));

    std::stable_sort(out.begin(), out.end(), [](const RegWrite& a, const RegWrite& b) { return a.cycle < b.cycle; });
    for (int ch = 0; ch < 4; ++ch) refreshView(ch);
    out_ = nullptr;
}

void Driver::refreshView(int ch)
{
    const Voice& v = v_[size_t(ch)];
    VoiceView& w = view_[size_t(ch)];
    w.active = v.active; w.dacOn = v.dacOn; w.note = v.note; w.velocity = v.vel;
    w.instrument = v.ksInstrument ? v.ksInstrument : v.p.instrument;
    w.period = uint16_t(std::max<int16_t>(0, v.lastPeriod));
    w.volume = (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) ? v.inst.waveLevel : v.volume;
    w.duty = v.duty; w.frame = uint8_t(v.frameIdx + 1);
    w.tableSlot = v.tableOn ? v.tableSlot : 0; w.tableStep = v.tableStep;
    for (int r = 0; r < 5; ++r) w.regs[r] = known_[size_t(ch * 5 + r)] ? shadow_[size_t(ch * 5 + r)] : 0;
}

} // namespace chipboy::driver
