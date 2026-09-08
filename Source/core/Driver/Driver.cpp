#include "core/Driver/Driver.h"

#include <algorithm>
#include <cmath>

namespace chipboy::driver {

using namespace chipboy::bank;

namespace {

constexpr uint32_t kBurstSpacing = 20;     ///< cycles between consecutive writes from one tick: ld a,n / ldh (n),a
constexpr uint32_t kCpuHz = 4194304u;
constexpr int      kMaxTicksPerBlock = 512;

/// The pitch clock (section 7): 11651 CPU cycles is 360.0 Hz. Every voice has
/// its own, restarted at each plain note-on, so a note's vibrato and slide are
/// the same whatever sample the note started on.
constexpr uint64_t kPitchCycles = 11651;
constexpr uint32_t kVibCycle = 65536;      ///< one vibrato cycle, in phase units

/// V's depth, in 1/32 semitones: LSDj's table, 0 = 1/8 of a semitone, 15 = 8.
constexpr int kVibDepthFine[16] = { 4, 8, 12, 16, 24, 32, 48, 64, 80, 96, 112, 128, 160, 192, 224, 256 };

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

// NRx1's length bits from an instrument's length (0 = no length counter).
uint8_t lengthCode6(uint16_t length) { return length ? uint8_t(uint8_t(64 - std::min<int>(64, length)) & 0x3F) : 0; }

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
    tickCount_ = 0;
    pendingCount_ = 0;
    for (auto& g : tableGroove_) g.fill(0);
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

int Driver::resolveSlot(int ch, uint8_t vel) const
{
    const auto& p = params_[size_t(ch)];
    const Voice& v = v_[size_t(ch)];
    int slot = v.ksInstrument ? v.ksInstrument : p.instrument;
    // The velocity bank picks an instrument around the channel's own choice.
    // A cell's instrument column has already named one -- the recorder writes
    // the slot the note really loaded (section 9.4) -- so it is taken as it is
    // and the bank is not applied a second time.
    if (p.velocityMode == 1 && slot && !v.ksFromCell) slot += vel / 8;
    return slot;
}

const Instrument* Driver::resolveInstrument(int ch, uint8_t vel)
{
    if (local_[size_t(ch)] && !v_[size_t(ch)].ksInstrument) return local_[size_t(ch)];
    return bank_ ? bank_->instrument(resolveSlot(ch, vel)) : nullptr;
}

/// Which instrument a note-on would load, as one comparable number: a bank
/// slot, or a mark of its own for a Voice's local instrument. An overlapping
/// MIDI note is only bare while this does not change (section 8).
uint32_t Driver::instrumentKey(int ch, uint8_t vel) const
{
    if (local_[size_t(ch)] && !v_[size_t(ch)].ksInstrument) return 0x10000u;
    return uint32_t(std::max(0, resolveSlot(ch, vel)));
}

void Driver::setTableGroove(int ch, const uint8_t* ticks16)
{
    auto& g = tableGroove_[size_t(ch & 3)];
    if (!ticks16) { g.fill(0); return; }
    for (size_t i = 0; i < g.size(); ++i) g[i] = ticks16[i];
}

uint8_t Driver::tableGrooveSlot(int ch) const { return v_[size_t(ch & 3)].tableGroove; }

PitchSpeed Driver::pitchSpeed(const Voice& v) const
{
    // Noise has no pitch effects at all, and a kit has no Drum (section 7).
    if (v.inst.type == InstrumentType::Noise) return PitchSpeed::Fast;
    if (v.inst.type == InstrumentType::Kit && v.inst.pitchSpeed == PitchSpeed::Drum) return PitchSpeed::Fast;
    return v.inst.pitchSpeed;
}

void Driver::latch(int ch)
{
    // The running state starts as the instrument's; the level and pan
    // parameters sit on top, and the command slots on top of those.
    Voice& v = v_[size_t(ch)];
    v.p = params_[size_t(ch)];
    const auto& p = v.p;
    const auto& i = v.inst;
    v.envVol = i.envVol; v.envRate = i.envRate; v.envDir = i.envDir;
    v.duty = i.duty;
    v.sweepRate = i.sweepRate; v.sweepShift = i.sweepShift; v.sweepDown = i.sweepDown;
    v.lfsr7 = i.lfsr7;
    v.noiseShift = i.noiseShift; v.noiseDiv = i.noiseDivisor; v.noiseSweep = i.noiseSweep;
    v.pan = p.pan != 255 ? Pan(p.pan & 3) : i.pan;
    v.vibShape = i.vib.shape;
    v.vibDir = i.vib.dir;
    v.vibSpeed = uint8_t(std::clamp<int>(i.vib.speed, 1, 15));
    v.vibDepth = i.vib.depth;
    v.vibDelay = i.vib.delay;
    v.waveSlot = i.wave;
    v.waveLevel = i.waveLevel;
    v.lengthCode = i.length;
}

void Driver::applyLevelParam(int ch)
{
    Voice& v = v_[size_t(ch)];
    if (v.p.level == 255) return;                       // the instrument's own
    if (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) v.waveLevel = uint8_t(v.p.level & 3);
    else v.envVol = uint8_t(v.p.level & 15);
}

InstrumentType Driver::defaultType(int ch) { return ch == 2 ? InstrumentType::Wave : ch == 3 ? InstrumentType::Noise : InstrumentType::Pulse; }
bool Driver::typeFits(int ch, InstrumentType t)
{
    // A channel can only host its own kind (section 9.2).
    return (ch < 2 && t == InstrumentType::Pulse) || (ch == 2 && (t == InstrumentType::Wave || t == InstrumentType::Kit)) || (ch == 3 && t == InstrumentType::Noise);
}

/// Load the instrument on a sounding channel: the tracker's instrument column
/// and Live follow both do this, and the slots in force then apply on top.
void Driver::reloadInstrument(int ch)
{
    Voice& v = v_[size_t(ch)];
    const Instrument* inst = resolveInstrument(ch, v.vel);
    InstrumentCore core = inst ? *inst : Instrument::defaults(defaultType(ch));
    if (!typeFits(ch, core.type)) core = Instrument::defaults(defaultType(ch));
    v.inst = core; v.haveInst = true;
    latch(ch);
    applyLevelParam(ch);
    const uint8_t tbl = v.tableOverride ? v.tableOverride : core.table;
    v.tableSlot = tbl; v.tableStep = 0; v.tableRow = 0; v.tableWait = 0; v.tableGroove = 0;
    v.tableOn = tbl && bank_ && bank_->table(tbl);
    restartPitchClock(ch);                 // the new instrument may run its pitch elsewhere
    fireSlots(ch);
    if (v.active) {
        if (core.type == InstrumentType::Pulse) emit(regAddr(ch, 1), uint8_t((v.duty << 6) | (core.length ? uint8_t(64 - std::min<int>(64, core.length)) & 0x3F : 0)));
        writeEnvelope(ch, core.type == InstrumentType::Pulse || core.type == InstrumentType::Noise);
        writeNr51();
    }
}

/* -------------------------------------------------------------- notes */

namespace {
/// The letters a bare note fires again (section 8): the per-note ones. The
/// rest -- E, F, O, P, S, V, W, A -- are already in force and are left alone.
bool perNoteCmd(Cmd c) { return c == Cmd::C || c == Cmd::D || c == Cmd::K || c == Cmd::L || c == Cmd::R; }
} // namespace

void Driver::noteOn(int ch, uint8_t note, uint8_t vel, const NoteEvent* cell)
{
    Voice& v = v_[size_t(ch)];
    const auto& p = params_[size_t(ch)];
    // The command octave (section 13): notes 0-11 never sound and never join
    // the held stack. They fire the channel's slots on whatever it is playing,
    // without a trigger, so a held note can be shaped after its attack; with
    // nothing sounding the persistent letters still land in the running state.
    if (note < 12) {
        syncSlots(ch);
        fireSlots(ch, /*live*/ true);
        if (cell) applyCellCommands(ch, cell->cmd1, cell->cmd2);
        return;
    }
    // Keyswitch octave: selects an instrument, never sounds.
    const Instrument* cur = local_[size_t(ch)] ? local_[size_t(ch)] : bank_ ? bank_->instrument(p.instrument) : nullptr;
    const InstrumentType t = cur ? cur->type : (ch == 2 ? InstrumentType::Wave : ch == 3 ? InstrumentType::Noise : InstrumentType::Pulse);
    if (p.keyswitch) {
        const int base = keyswitchBase(t);
        if (note >= base && note < base + 12) { v.ksInstrument = uint8_t(note - base + 1); v.ksFromCell = false; return; }
    }
    // Whether a note is still held under this one decides, with the
    // instrument's Overlap, if the new note is plain or bare (section 8).
    const bool over = v.active && v.haveInst && v.heldCount > 0;
    if (v.heldCount < v.held.size()) v.held[v.heldCount++] = note;
    // The parameters' slots are in force from here; the cell's own commands
    // fire once, after them, when the note starts (section 12).
    syncSlots(ch);
    v.noteCmd[0] = {}; v.noteCmd[1] = {};
    if (cell) {
        if (cell->inst) { v.ksInstrument = cell->inst; v.ksFromCell = true; }   // a cell's instrument column names it exactly
        if (cell->table) v.tableOverride = cell->table;      // the channel's table override, from this step on
        v.noteCmd[0] = cell->cmd1; v.noteCmd[1] = cell->cmd2;
    }
    // A tracker cell is plain when its instrument column is filled and bare
    // when it is blank; a MIDI note is bare only when it lands over a held
    // note, would load the instrument already sounding, and that instrument
    // overlaps legato.
    bool plain = true;
    if (cell) plain = cell->inst != 0;
    else if (over && v.inst.overlap == Overlap::Legato && instrumentKey(ch, vel) == v.instKey) plain = false;
    // What this note is recorded as (section 9.4): the instrument it loads,
    // and whether it was plain. Decided here, so a note D holds back reports
    // the same thing as one that starts at once. A bare note keeps the slot
    // the note under it loaded, which is the one still sounding.
    v.notePlain = plain;
    if (plain) v.noteInst = uint8_t(std::clamp(resolveSlot(ch, vel), 0, kInstrumentSlots));
    // D postpones the start, whether it came from a cell or a slot. The
    // cell's commands wait with it and fire when the note does.
    const int delay = delayFor(ch, cell ? &cell->cmd1 : nullptr, cell ? &cell->cmd2 : nullptr);
    if (delay >= 0) {
        v.pendingOn = true; v.pendingNote = note; v.pendingVel = vel; v.pendingPlain = plain;
        v.delay = int16_t(delay);
        // The cell's commands wait with the note rather than in the slot the
        // next note would read (section 12).
        v.pendingCmd[0] = v.noteCmd[0]; v.pendingCmd[1] = v.noteCmd[1];
        v.noteCmd[0] = {}; v.noteCmd[1] = {};
        return;
    }
    startVoice(ch, note, vel, plain);
}

/// The delay a note about to start takes: its cell's own D column first --
/// a cell's commands are that step's, not the lane's -- then a slot's.
int Driver::delayFor(int ch, const Command* c1, const Command* c2) const
{
    for (const Command* c : { c1, c2 })
        if (c && c->cmd == Cmd::D && !isRevert(*c)) return std::clamp<int>(c->a, 0, 255);
    const Voice& v = v_[size_t(ch)];
    for (int i = 0; i < 2; ++i)
        if (v.slot[size_t(i)].cmd == Cmd::D) return std::clamp<int>(v.slot[size_t(i)].a, 0, 255);
    return -1;
}

void Driver::noteOff(int ch, uint8_t note)
{
    Voice& v = v_[size_t(ch)];
    // remove from the held stack
    for (uint8_t i = 0; i < v.heldCount; ++i)
        if (v.held[i] == note) { for (uint8_t k = i; k + 1 < v.heldCount; ++k) v.held[k] = v.held[k + 1]; --v.heldCount; break; }
    if (v.pendingOn && v.pendingNote == note) { v.pendingOn = false; return; }
    if (!v.active || v.note != note) return;
    if (v.heldCount > 0) {                 // back to the most recent held note, bare: no attack
        startVoice(ch, v.held[v.heldCount - 1], v.vel, false);
        return;
    }
    switch (v.inst.noteOff) {
        case NoteOff::Kill:    stopVoice(ch, true); break;
        case NoteOff::Release: beginRelease(ch); break;
        case NoteOff::Ignore:  break;
    }
}

/// Release: the note stops being played but is left to finish by itself.
void Driver::beginRelease(int ch)
{
    Voice& v = v_[size_t(ch)];
    v.active = false; v.tableOn = false; v.sliding = false; v.chordN = 0;
    v.pitchClockOn = false; v.retrigEvery = 0; v.retrigOnce = false; v.bendSpeed = 0;
    if (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) {
        v.releasing = v.dacOn;              // the level steps down from here, a tick apart
        return;
    }
    // A held or rising envelope would never finish: write a decrease at rate 1
    // so the note fades out. No trigger -- that would start it again.
    if (v.dacOn && (v.envRate == 0 || v.envDir == EnvDir::Up)) {
        v.envRate = 1; v.envDir = EnvDir::Down;
        emit(regAddr(ch, 2), uint8_t((v.envVol << 4) | 1), true);
        v.volume = v.envVol;
    }
}

/// WAV and KIT have no envelope generator, so their release is four levels one
/// tick apart: 100, 50, 25, mute (section 8).
void Driver::stepRelease(int ch)
{
    Voice& v = v_[size_t(ch)];
    if (v.waveLevel > 0) {
        v.waveLevel = uint8_t(v.waveLevel - 1);
        emit(regAddr(2, 2), nr32Code(v.waveLevel));
        if (v.waveLevel > 0) return;
    }
    v.releasing = false;
    stopVoice(ch, true);
}

void Driver::startVoice(int ch, uint8_t note, uint8_t vel, bool plain)
{
    Voice& v = v_[size_t(ch)];
    if (!plain && v.haveInst) {
        // A bare note (section 8): only the period moves. No trigger, no
        // instrument reload, no table restart, no state reset -- the envelope
        // runs on, the vibrato keeps its phase, the P offset stays, and a
        // slide in force starts from the pitch the channel is at.
        v.note = note; v.vel = vel; v.active = true;
        const bool was = inNoteOn_; inNoteOn_ = true;
        if (v.inst.tableMode == TableMode::Step && v.tableOn) stepTable(ch);   // a row per note, bare notes included
        for (int i = 0; i < 2; ++i) {
            const Command c = slotForNoteOn(ch, i);
            if (perNoteCmd(c.cmd) && c.cmd != Cmd::D) applyCommand(ch, c, false);
        }
        // The cell's own commands, once, at this step: a persistent letter
        // written on a bare note changes the running state and stays until a
        // plain note reloads the instrument (section 12).
        applyCellCommands(ch, v.noteCmd[0], v.noteCmd[1]);
        v.noteCmd[0] = {}; v.noteCmd[1] = {};
        inNoteOn_ = was;
        writePeriod(ch, false);
        writeNr51();
        return;
    }

    const Instrument* inst = resolveInstrument(ch, vel);
    InstrumentCore core;
    if (inst) core = *inst;
    else core = Instrument::defaults(defaultType(ch));
    if (!typeFits(ch, core.type)) core = Instrument::defaults(defaultType(ch));

    // A Step-mode table keeps its place across notes, note-offs included: the
    // slot it was on, not whether it happens to be running.
    const uint8_t hadTable = v.tableSlot;
    v.inst = core; v.haveInst = true;
    latch(ch);
    v.note = note; v.vel = vel; v.active = true; v.killed = false; v.releasing = false;
    v.instKey = instrumentKey(ch, vel);
    v.ticks = 0; v.vibPhase = 0; v.pitchCount = 0;
    v.pOffset = 0; v.fineOffset = 0; v.bendSpeed = 0; v.sliding = false; v.slideLeft = 0;
    v.chordN = 0; v.chordIdx = 0; v.chordCount = 0;
    v.dutyIdx = 0; v.kill = -1; v.retrigEvery = 0; v.retrigStep = 0; v.retrigCount = 0; v.retrigOnce = false; v.lastCmd = {}; v.frameIdx = 0;
    v.rng = v.rng * 1664525u + 1013904223u + note;
    restartPitchClock(ch);
    // volume from velocity
    if (v.p.velocityMode == 0 && (core.type == InstrumentType::Pulse || core.type == InstrumentType::Noise)) v.envVol = levelFromVelocity(vel);
    applyLevelParam(ch);
    // table
    const uint8_t tbl = v.tableOverride ? v.tableOverride : core.table;
    v.tableSlot = tbl; v.tableOn = tbl && bank_ && bank_->table(tbl);
    v.tableWait = 0;
    // A Step-mode table advances one row per trigger instead of restarting,
    // which is the whole point of it; a table that was not already running
    // starts at its first row (section 7).
    if (core.tableMode != TableMode::Step || hadTable != tbl) { v.tableStep = 0; v.tableRow = 0; v.tableGroove = 0; }
    if (core.tableMode == TableMode::Step && v.tableOn) {
        const bool was = inNoteOn_; inNoteOn_ = true;
        stepTable(ch);
        inNoteOn_ = was;
    }
    if (core.dutySeqLen) v.duty = uint8_t(core.dutySeq[0] & 3);
    // instrument, then its table, then CMD1 and CMD2: the slots in force apply
    // to every note in their span (section 3). Their registers go out with the
    // note's own writes below rather than twice.
    fireSlots(ch);
    // Then the cell's own two commands, once (section 12): they are this
    // step's, not the lane's, so they are not left in force behind the note.
    {
        const bool was = inNoteOn_; inNoteOn_ = true;
        applyCellCommands(ch, v.noteCmd[0], v.noteCmd[1]);
        v.noteCmd[0] = {}; v.noteCmd[1] = {};
        inNoteOn_ = was;
    }

    const int base = computePeriod(ch);
    if (base < 0 && core.type != InstrumentType::Noise && core.type != InstrumentType::Kit) {
        // Below the chip's range: does not sound (C4). The key is still held,
        // so the held stack stays as it is.
        v.basePeriod = 0; v.tableOn = false; v.sliding = false; v.chordN = 0; v.pitchClockOn = false;
        killDac(ch);
        v.active = true; view_[size_t(ch)].outOfRange = true;
        return;
    }
    view_[size_t(ch)].outOfRange = false;

    switch (core.type) {
        case InstrumentType::Pulse: {
            if (ch == 0) emit(regAddr(0, 0), uint8_t((v.sweepRate << 4) | (v.sweepDown ? 8 : 0) | v.sweepShift), true);
            const uint8_t len = core.length ? uint8_t(64 - std::min<int>(64, core.length)) : 0;
            emit(regAddr(ch, 1), uint8_t((v.duty << 6) | (len & 0x3F)), true);
            writeEnvelope(ch, false);   // sets dacOn; the previous state decides the quiet-edge marker
            writePeriod(ch, true);
            break;
        }
        case InstrumentType::Wave: {
            const Wave* w = bank_ ? bank_->wave(v.waveSlot) : nullptr;
            const Frame* f = w && !w->frames.empty() ? &w->frames[0] : nullptr;
            v.frameCount = 0; v.frameDir = 1; v.kitOn = false; v.streamActive = false;
            if (w && !w->frames.empty()) v.frameIdx = uint8_t(std::min<int>(v.frameIdx, int(w->frames.size()) - 1));
            else v.frameIdx = 0;
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

void Driver::killDac(int ch)
{
    Voice& v = v_[size_t(ch)];
    if (!v.dacOn) return;
    // Clearing the DAC holds the level on this hardware, so it is silent
    // (reference section 9).
    if (ch == 2) emit(regAddr(2, 0), 0x00, true);
    else emit(regAddr(ch, 2), 0x00, true);
    v.dacOn = false; v.killed = true;
}

void Driver::stopVoice(int ch, bool kill)
{
    Voice& v = v_[size_t(ch)];
    v.active = false; v.tableOn = false; v.sliding = false; v.chordN = 0; v.kitOn = false; v.streamActive = false; v.pendingOn = false;
    v.pitchClockOn = false; v.releasing = false;
    // A kill or a stop ends the phrase: a key released afterwards must not
    // bring a note back that nobody is playing (section 8).
    v.heldCount = 0;
    if (kill) killDac(ch);
}

/// Everything a channel is doing stops and it goes quiet, whatever the state
/// says: MIDI CC 120/123, and every flush the Player or the processor sends.
void Driver::allNotesOff(int ch)
{
    Voice& v = v_[size_t(ch)];
    v.delay = -1; v.kill = -1;
    v.retrigEvery = 0; v.retrigCount = 0; v.retrigOnce = false;
    v.bendSpeed = 0; v.slideLeft = 0; v.chordIdx = 0; v.chordCount = 0;
    stopVoice(ch, true);
}

/* ------------------------------------------------------------- pitch */

/// What is left of a slide, in the domain it started in: period units, or
/// 1/32 semitones in Drum. It walks to zero over the duration L asked for, so
/// the note arrives exactly, without accumulating rounding.
int32_t Driver::slideResidual(const Voice& v) const
{
    if (!v.sliding || v.slideTotal <= 0) return 0;
    return int32_t(int64_t(v.slideFrom) * int64_t(v.slideLeft) / int64_t(v.slideTotal));
}

/// The vibrato's offset in 1/32 semitones, from the phase: the shape over one
/// cycle scaled by LSDj's depth table, downward or upward from the note.
int Driver::vibratoFine(const Voice& v) const
{
    if (!v.vibDepth || !v.vibSpeed || v.ticks < v.vibDelay) return 0;
    const uint32_t ph = v.vibPhase % kVibCycle;
    const double half = double(kVibCycle / 2);
    double u = 0.0;                                   // 0 at the note, 1 at full depth
    switch (v.vibShape) {
        case VibShape::Triangle: u = ph < kVibCycle / 2 ? double(ph) / half : 2.0 - double(ph) / half; break;
        case VibShape::Saw:      u = double(ph) / double(kVibCycle); break;
        case VibShape::Square:   u = ph < kVibCycle / 2 ? 0.0 : 1.0; break;
    }
    const int off = int(std::lround(u * kVibDepthFine[v.vibDepth & 15]));
    return v.vibDir == VibDir::Up ? off : -off;
}

/// The note the channel is at, in semitones and vibrato apart: the note, the
/// channel's transpose, the bend wheel, the chord, the table's transpose
/// column, and the 1/32-semitone offsets Drum-mode P and L work in.
double Driver::noteOfVoice(int ch) const
{
    const Voice& v = v_[size_t(ch)];
    double note = v.note + v.p.transpose + v.bend;
    if (v.chordN) note += v.chord[v.chordIdx % v.chordN];
    if (v.tableOn && v.inst.transpose && bank_) {
        const Table* t = bank_->table(v.tableSlot);
        if (t) { const auto& st = t->steps[v.tableRow]; if (st.hasTranspose) note += st.transpose; }
    }
    int32_t fine = v.fineOffset;
    if (v.slideDrum) fine += slideResidual(v);
    return note + double(fine) / 32.0;
}

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
        return std::clamp(int(std::lround(per)) + v.pOffset + (v.slideDrum ? 0 : slideResidual(v)), 0, 2047);
    }
    // period = periodOf(noteFine) + periodOffset (section 7): the note and the
    // vibrato are semitones, P and a slide are register units unless the
    // instrument's pitch speed is Drum, where they are semitones too.
    const double note = noteOfVoice(ch) + double(vibratoFine(v)) / 32.0;
    int per = periodForNote(note, v.inst.type == InstrumentType::Wave);
    if (per < 0) return -1;
    per += v.pOffset;
    if (!v.slideDrum) per += slideResidual(v);
    return std::clamp(per, 0, 2047);
}

/// One pitch update: the vibrato phase, a slide and a P bend move on, and the
/// period goes out without a trigger. The 360 Hz clock calls this in Fast,
/// Step and Drum; the tracker tick calls it in Tick, where the instrument's
/// command rate slows P and V to one step every rate + 1 ticks.
void Driver::pitchStep(int ch, bool onTick)
{
    Voice& v = v_[size_t(ch)];
    if (!v.active) return;
    bool advance = true;
    if (onTick) {
        const int every = int(v.inst.cmdRate) + 1;
        if (every > 1) { if (++v.pitchCount < every) advance = false; else v.pitchCount = 0; }
    }
    if (advance) {
        // One cycle is 720 / speed updates at 360 Hz, or 96 / speed ticks.
        if (v.vibSpeed && v.vibDepth && v.ticks >= v.vibDelay)
            v.vibPhase += uint32_t((uint64_t(v.vibSpeed) * kVibCycle) / (onTick ? 96u : 720u));
        if (v.bendSpeed) {
            if (pitchSpeed(v) == PitchSpeed::Drum) v.fineOffset = std::clamp<int32_t>(v.fineOffset + v.bendSpeed * 2, -32000, 32000);
            else                                   v.pOffset = int16_t(std::clamp<int>(v.pOffset + v.bendSpeed, -2047, 2047));
        }
    }
    // A slide's duration is in updates of this clock; the command rate leaves
    // it alone.
    if (v.sliding) { if (v.slideLeft > 0) --v.slideLeft; if (v.slideLeft <= 0) { v.sliding = false; v.slideLeft = 0; } }
    writePeriod(ch, false);
}

void Driver::restartPitchClock(int ch)
{
    Voice& v = v_[size_t(ch)];
    v.pitchClock = cycle_ + kPitchCycles;
    // Noise has no pitch effects, and a kit's period is its sample rate, read
    // by the streaming timer: neither is bent between ticks.
    v.pitchClockOn = (v.inst.type == InstrumentType::Pulse || v.inst.type == InstrumentType::Wave)
                     && pitchSpeed(v) != PitchSpeed::Tick;
}

void Driver::writePeriod(int ch, bool trigger)
{
    Voice& v = v_[size_t(ch)];
    if (v.inst.type == InstrumentType::Noise) {
        uint8_t s, d;
        if (v.inst.noiseManual) { s = v.noiseShift; d = v.noiseDiv; }
        else { const int n = std::clamp(int(v.note) + v.p.transpose, 0, 127); s = uint8_t(noiseShiftMap_[size_t(n)]); d = uint8_t(noiseDivMap_[size_t(n)]); s = uint8_t(std::clamp(int(s) + int(v.noiseShift) - 5, 0, 13)); }
        v.noiseShift = s; v.noiseDiv = d;
        emit(regAddr(3, 3), uint8_t((s << 4) | (v.lfsr7 ? 8 : 0) | (d & 7)));
        if (trigger) emit(regAddr(3, 4), uint8_t(0x80 | (v.inst.length ? 0x40 : 0)), true);
        v.lastPeriod = int16_t((s << 4) | d);
        return;
    }
    int per = computePeriod(ch);
    if (per < 0) per = 0;
    v.basePeriod = int16_t(per);
    // Where the channel is now, for an L that fires later: the pitch without
    // the vibrato, in both domains, the slide it is in the middle of included.
    {
        const double note = noteOfVoice(ch);
        v.pitchNowFine = int32_t(std::lround(note * 32.0));
        const int base = periodForNote(note, v.inst.type == InstrumentType::Wave);
        v.pitchNowPeriod = (base < 0 ? 0 : base) + v.pOffset + (v.slideDrum ? 0 : slideResidual(v));
        v.pitchValid = true;
    }
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
        emit(regAddr(2, 2), nr32Code(v.waveLevel));
        return;
    }
    const uint8_t nr2 = uint8_t((v.envVol << 4) | (v.envDir == EnvDir::Up ? 8 : 0) | (v.envRate & 7));
    // Rewriting NRx2 on a running channel is zombie mode; a driver that wants
    // a clean level change writes the register and then retriggers, which
    // restarts the envelope but keeps the duty phase (reference section 4).
    // With "volume writes at edges" the plugin moves this burst to the low
    // half of the pulse cycle, where a level change is silent.
    if (global_.volumeAtEdges && v.active && v.dacOn && (ch == 0 || ch == 1) && v.inst.type == InstrumentType::Pulse) emit(kAlignToQuietEdge, uint8_t(ch), true);
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
    for (int ch = 0; ch < 4; ++ch) if (gateMask_ & (1u << ch)) bits |= panBitsFor(v_[size_t(ch)].pan, ch);
    emit(0xFF25, bits);
    gateDirty_ = false;
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
    if (!v.timerValid || !v.dacOn || v.fetchPeriod == 0) return;
    // The fetch model advances every block, streaming or not, so a frame
    // change that arrives later finds the read pointer where it really is.
    // (Walking only while streaming let the model go stale, and a CGB frame
    // change then spent its bytes on fetches that were already in the past.)
    if (v.nextFetch + uint64_t(v.fetchPeriod) * 64 < cycleStart) {
        const uint64_t k = (cycleStart - v.nextFetch) / v.fetchPeriod;
        v.nextFetch += k * v.fetchPeriod;
        v.fetchIndex = uint32_t((v.fetchIndex + k) & 31);
    }
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

namespace {

/// R's volume step per retrigger (section 7): 0 none, 1-7 up by that much,
/// 9-15 down by x - 8.
int16_t retrigVolStep(int x)
{
    const int n = std::clamp(x, 0, 15);
    return int16_t(n < 8 ? n : -(n - 8));
}

/// One side of M: 0-7 sets it, 8 leaves it, 9-11 raise it by 1-3 and 13-15
/// lower it by 1-3. 12 is a value LSDj does not document; it changes nothing.
int masterFromArg(int x, int cur)
{
    const int n = std::clamp(x, 0, 15);
    if (n < 8) return n;
    if (n >= 9 && n <= 11) return std::min(7, cur + (n - 8));
    if (n >= 13) return std::max(0, cur - (n - 12));
    return cur;
}

} // namespace

void Driver::applyCommand(int ch, const Command& cIn, bool fromTable)
{
    Voice& v = v_[size_t(ch)];
    const Command c = cIn;
    if (c.cmd == Cmd::None) return;
    // Z is resolved by whoever fires it -- a slot at a note-on, a table step --
    // because what it re-runs is the other slot or column.
    if (c.cmd == Cmd::Z) return;
    // The revert form of a letter takes the slot-going-to-none path, so a
    // cell and a slot revert through exactly the same code (section 3). It is
    // not what a later Z re-runs: un-setting a letter is not a value.
    if (isRevert(c)) { revertCommand(ch, c.cmd); return; }
    if (c.cmd != Cmd::H) v.lastCmd = c;            // what a later Z re-runs
    const bool pulse = v.inst.type == InstrumentType::Pulse;
    const bool wave = v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit;
    const bool noise = v.inst.type == InstrumentType::Noise;
    // Inside a note-on the commands only set the running state: the note's own
    // writes carry it out, so a slot does not cost a second burst or a pop.
    const bool live = v.active && !inNoteOn_;
    switch (c.cmd) {
        case Cmd::A:                                  // table select, 0 stops
            if (c.a <= 0) v.tableOn = false;
            else { v.tableSlot = uint8_t(std::clamp<int>(c.a, 1, kTableSlots)); v.tableStep = 0; v.tableRow = 0; v.tableWait = 0; v.tableGroove = 0; v.tableOn = bank_ && bank_->table(v.tableSlot); }
            break;
        case Cmd::C:                                  // 0, x, y one step per cmdRate + 1 ticks
            if (noise) break;
            v.chord[0] = 0; v.chord[1] = uint8_t(std::clamp<int>(c.a, 0, 60)); v.chord[2] = uint8_t(std::clamp<int>(c.b, 0, 60));
            v.chordN = c.b ? 3 : (c.a ? 2 : 0);
            v.chordIdx = 0; v.chordCount = 0;
            break;
        case Cmd::D: if (fromTable) v.delay = int16_t(std::clamp<int>(c.a, 0, 255)); break;   // a slot's D is read at the note-on
        case Cmd::E: {
            // Envelope: volume in x; y is the NRx2 encoding, 0 and 8 holding,
            // 1-7 decaying at that rate and 9-15 rising at y - 8.
            if (wave) { v.waveLevel = uint8_t(std::clamp<int>(c.a, 0, 3)); if (live) writeEnvelope(ch, false); }
            else {
                v.envVol = uint8_t(std::clamp<int>(c.a, 0, 15));
                v.envRate = uint8_t(c.b & 7);
                v.envDir = (c.b & 8) ? EnvDir::Up : EnvDir::Down;
                // A rewrite of NRx2 alone is zombie mode; the retrigger keeps
                // the level honest and the duty phase intact (reference 4).
                if (live) writeEnvelope(ch, pulse || noise);
            }
            break;
        }
        case Cmd::F: if (v.inst.type == InstrumentType::Wave) { const Wave* w = bank_ ? bank_->wave(v.waveSlot) : nullptr; if (w && !w->frames.empty()) { v.frameIdx = uint8_t(std::clamp<int>(c.a - 1, 0, int(w->frames.size()) - 1)); v.frameCount = 0; if (live) loadFrame(ch, w->frames[v.frameIdx], model_ == Console::DMG); } } break;
        case Cmd::G:
            // Inside a table G sets that run's row lengths: the driver keeps
            // the slot for the Player, which hands back the groove's ticks
            // through setTableGroove(). On the timeline the Player owns it.
            if (fromTable) { v.tableGroove = uint8_t(std::clamp<int>(c.a, 0, 16)); if (!v.tableGroove) tableGroove_[size_t(ch)].fill(0); }
            break;
        case Cmd::T: break;                           // timeline: the Player and the Clock own this
        case Cmd::H: if (fromTable) { if (c.a <= 0) v.tableOn = false; else v.tableStep = uint8_t(std::clamp<int>(c.a - 1, 0, 15)); } break;
        case Cmd::K: v.kill = int16_t(std::clamp<int>(c.a, 0, 255)); break;
        case Cmd::L: {
            // A slide is a residual that walks to zero over x updates: ticks in
            // Tick, 1/360 s otherwise, 0 instant. It starts wherever the
            // channel is, mid-slide included, and ends on the note of this
            // cell or note-on -- in period units, or semitones in Drum.
            if (noise) break;
            const bool drum = pitchSpeed(v) == PitchSpeed::Drum;
            const int32_t fromFine = v.pitchNowFine, fromPeriod = v.pitchNowPeriod;
            const bool have = v.pitchValid;
            v.sliding = false; v.slideLeft = 0; v.slideDrum = drum;
            const int dur = std::clamp<int>(c.a, 0, 32767);
            if (!have || dur <= 0) { if (live) writePeriod(ch, false); break; }
            int32_t from = 0;
            if (drum) from = fromFine - int32_t(std::lround(noteOfVoice(ch) * 32.0));
            else {
                const int b = periodForNote(noteOfVoice(ch), v.inst.type == InstrumentType::Wave);
                from = fromPeriod - ((b < 0 ? 0 : b) + v.pOffset);
            }
            if (from != 0) { v.slideFrom = from; v.slideTotal = dur; v.slideLeft = dur; v.sliding = true; }
            if (live) writePeriod(ch, false);
            break;
        }
        case Cmd::M: {
            const uint8_t cur = known_[0x14] ? shadow_[0x14] : uint8_t(((global_.masterL & 7) << 4) | (global_.masterR & 7));
            writeNr50(uint8_t(masterFromArg(c.a, (cur >> 4) & 7)), uint8_t(masterFromArg(c.b, cur & 7)));
            break;
        }
        case Cmd::O: v.pan = Pan(std::clamp<int>(c.a, 0, 3)); writeNr51(); break;
        case Cmd::P: {
            // The bend speed, signed around 128: period units per update, or
            // (x - 128)/16 semitones in Drum. Step has no bend, so it is an
            // immediate offset instead. P 128 stops a bend and keeps what it
            // reached; a plain note-on is what puts the offset back to zero.
            if (noise) break;
            const int speed = std::clamp<int>(c.a, 0, 255) - 128;
            if (pitchSpeed(v) == PitchSpeed::Step) { v.pOffset = int16_t(speed); v.bendSpeed = 0; }
            else v.bendSpeed = int16_t(speed);
            if (live) writePeriod(ch, false);
            break;
        }
        case Cmd::R:
            v.retrigEvery = uint8_t(std::clamp<int>(c.b, 0, 255));
            v.retrigOnce = v.retrigEvery == 0;        // y = 0 retriggers once
            v.retrigStep = retrigVolStep(c.a);
            v.retrigCount = 0;
            break;
        case Cmd::S: {
            // PU1's sweep. The direction is the instrument's unless x asks for
            // down; on WAV and NOI there is no sweep unit, so S is inert.
            if (ch == 0 && pulse) {
                v.sweepRate = uint8_t(c.a & 7);
                v.sweepDown = (c.a & 128) ? true : v.inst.sweepDown;
                v.sweepShift = uint8_t(c.b & 7);
                if (live) {
                    emit(0xFF10, uint8_t((v.sweepRate << 4) | (v.sweepDown ? 8 : 0) | v.sweepShift), true);
                    writePeriod(ch, true);             // the sweep unit reloads on the trigger
                }
            }
            break;
        }
        case Cmd::V:
            // Speed 1-15 is V's own: one cycle every 720 / x updates (x / 2 Hz
            // in Fast) or 96 / x ticks. x = 0 turns the vibrato off.
            if (noise) break;
            v.vibSpeed = uint8_t(std::clamp<int>(c.a, 0, 15));
            v.vibDepth = uint8_t(std::clamp<int>(c.b, 0, 15));
            v.vibDelay = 0;
            if (!v.vibSpeed) { v.vibDepth = 0; v.vibPhase = 0; }
            if (live) writePeriod(ch, false);
            break;
        case Cmd::W: {
            // Duty on the pulses, wave slot on WAV: one letter, the thing the
            // channel's waveform actually is.
            if (wave) {
                const Wave* w = bank_ ? bank_->wave(uint8_t(std::clamp<int>(c.a, 1, kWaveSlots))) : nullptr;
                if (w && !w->frames.empty()) { v.waveSlot = uint8_t(std::clamp<int>(c.a, 1, kWaveSlots)); v.frameIdx = 0; v.frameCount = 0; if (live) loadFrame(ch, w->frames[0], model_ == Console::DMG); }
            } else if (pulse) {
                v.duty = uint8_t(c.a & 3);
                if (live) emit(regAddr(ch, 1), uint8_t((v.duty << 6) | lengthCode6(v.inst.length)));
            }
            break;
        }
        default: break;
    }
}

/// A letter going back to where the instrument left it (section 3): the
/// instrument's value for E F O S V W, zero for P, the parameter's for M, a
/// stopped table for A. A slot going to none comes here, and so does a cell
/// holding the letter's revert form -- one path, one result. G and T are the
/// timeline's: the Player and the Clock revert those.
void Driver::revertCommand(int ch, Cmd cmd)
{
    Voice& v = v_[size_t(ch)];
    const auto& i = v.inst;
    const bool pulse = i.type == InstrumentType::Pulse;
    const bool wave = i.type == InstrumentType::Wave || i.type == InstrumentType::Kit;
    const bool live = v.active && !inNoteOn_;
    switch (cmd) {
        case Cmd::A: v.tableOn = false; break;                    // A none stops the table
        case Cmd::E:
            if (wave) { v.waveLevel = i.waveLevel; applyLevelParam(ch); if (live) writeEnvelope(ch, false); }
            else { v.envVol = i.envVol; v.envRate = i.envRate; v.envDir = i.envDir; applyLevelParam(ch); if (live) writeEnvelope(ch, pulse || i.type == InstrumentType::Noise); }
            break;
        case Cmd::F:
            if (i.type == InstrumentType::Wave) { const Wave* w = bank_ ? bank_->wave(v.waveSlot) : nullptr; v.frameIdx = 0; v.frameCount = 0; if (live && w && !w->frames.empty()) loadFrame(ch, w->frames[0], model_ == Console::DMG); }
            break;
        case Cmd::M: writeNr50(global_.masterL, global_.masterR); break;
        case Cmd::O: v.pan = v.p.pan != 255 ? Pan(v.p.pan & 3) : i.pan; writeNr51(); break;
        case Cmd::P: v.pOffset = 0; v.fineOffset = 0; v.bendSpeed = 0; if (live) writePeriod(ch, false); break;
        case Cmd::S:
            if (ch == 0 && pulse) {
                v.sweepRate = i.sweepRate; v.sweepDown = i.sweepDown; v.sweepShift = i.sweepShift;
                emit(0xFF10, uint8_t((v.sweepRate << 4) | (v.sweepDown ? 8 : 0) | v.sweepShift), true);
            }
            break;
        case Cmd::V:
            v.vibShape = i.vib.shape; v.vibDir = i.vib.dir;
            v.vibSpeed = uint8_t(std::clamp<int>(i.vib.speed, 1, 15)); v.vibDepth = i.vib.depth; v.vibDelay = i.vib.delay;
            break;
        case Cmd::W:
            if (wave) { const Wave* w = bank_ ? bank_->wave(i.wave) : nullptr; v.waveSlot = i.wave; v.frameIdx = 0; v.frameCount = 0; if (live && w && !w->frames.empty()) loadFrame(ch, w->frames[0], model_ == Console::DMG); }
            else if (pulse) { v.duty = i.duty; if (live) emit(regAddr(ch, 1), uint8_t((v.duty << 6) | lengthCode6(i.length))); }
            break;
        default: break;                                            // C D H K L R Z leave nothing behind; G and T are the timeline's
    }
}

/// A cell's two command columns (section 12). They are applied once, at
/// their step, and never stored in a slot: a persistent letter changes the
/// running state, which holds until a plain note reloads the instrument or a
/// later command moves it, and a per-note letter shapes that note alone. The
/// revert form puts the letter back where the instrument left it. D is read
/// at the note-on, before the note starts, so it is skipped here.
void Driver::applyCellCommands(int ch, const Command& c1, const Command& c2)
{
    const Command* in[2] = { &c1, &c2 };
    for (int i = 0; i < 2; ++i) {
        Command c = *in[i];
        if (c.cmd == Cmd::None || c.cmd == Cmd::D) continue;
        // Z re-runs the other column, as a slot's Z re-runs the other slot.
        if (c.cmd == Cmd::Z) c = resolveRandom(ch, c, *in[i ^ 1]);
        applyCommand(ch, c, false);
    }
}

int16_t Driver::randomArg(int ch, int max)
{
    Voice& v = v_[size_t(ch)];
    if (max <= 0) return 0;
    v.rng = v.rng * 1664525u + 1013904223u;
    return int16_t((v.rng >> 16) % uint32_t(max + 1));
}

/// Z re-runs the last command that is not Z or H -- the other slot or column
/// when that is set, else the last one the channel fired -- with a random
/// 0..x added to its x and 0..y to its y (section 7).
Command Driver::resolveRandom(int ch, const Command& z, const Command& other)
{
    Voice& v = v_[size_t(ch)];
    Command c = (other.cmd != Cmd::None && other.cmd != Cmd::Z && other.cmd != Cmd::H) ? other : v.lastCmd;
    if (c.cmd == Cmd::None || c.cmd == Cmd::Z || c.cmd == Cmd::H) return {};
    c.a = int16_t(c.a + randomArg(ch, z.a));
    c.b = int16_t(c.b + randomArg(ch, z.b));
    return c;
}

/// The slot as it applies to this note: itself, or what Z re-runs.
Command Driver::slotForNoteOn(int ch, int i)
{
    Voice& v = v_[size_t(ch)];
    const Command& c = v.slot[size_t(i)];
    if (c.cmd != Cmd::Z) return c;
    return resolveRandom(ch, c, v.slot[size_t(i ^ 1)]);
}

void Driver::fireSlots(int ch, bool live)
{
    const bool was = inNoteOn_;
    if (!live) inNoteOn_ = true;
    for (int i = 0; i < 2; ++i) {
        const Command c = slotForNoteOn(ch, i);
        if (c.cmd == Cmd::None || c.cmd == Cmd::D) continue;   // D was read before the note started
        applyCommand(ch, c, false);
    }
    inNoteOn_ = was;
}

/// A keyswitch or a cell's instrument column holds until the Instrument
/// parameter moves; moving it hands the channel back to the parameter, so the
/// lane is never dead (section 8).
void Driver::adoptInstrumentParam(int ch)
{
    Voice& v = v_[size_t(ch)];
    const int16_t p = int16_t(params_[size_t(ch)].instrument);
    if (v.instParam == p) return;
    if (v.instParam >= 0) { v.ksInstrument = 0; v.ksFromCell = false; }
    v.instParam = p;
}

/// Take the parameters' slots as the ones in force, without firing them: what
/// a note-on does, because fireSlots() is about to apply them in order.
void Driver::syncSlots(int ch)
{
    Voice& v = v_[size_t(ch)];
    const auto& p = params_[size_t(ch)];
    adoptInstrumentParam(ch);
    if (p.table != v.tableParam) { v.tableParam = p.table; v.tableOverride = p.table; }
    for (int i = 0; i < 2; ++i)
        if (!sameCmd(p.cmd[i], v.slotParam[size_t(i)])) { v.slotParam[size_t(i)] = p.cmd[i]; v.slot[size_t(i)] = p.cmd[i]; }
}

/// A slot fires at the next tick when its value changes, and reverts when it
/// goes to none. Tracker cells write the same slots, so they are one path.
void Driver::updateSlots(int ch)
{
    Voice& v = v_[size_t(ch)];
    const auto& p = params_[size_t(ch)];
    adoptInstrumentParam(ch);
    if (p.table != v.tableParam) { v.tableParam = p.table; v.tableOverride = p.table; }
    for (int i = 0; i < 2; ++i) {
        if (sameCmd(p.cmd[i], v.slotParam[size_t(i)])) continue;
        const Cmd was = v.slot[size_t(i)].cmd;
        v.slotParam[size_t(i)] = p.cmd[i];
        v.slot[size_t(i)] = p.cmd[i];
        if (p.cmd[i].cmd == Cmd::None) { if (was != Cmd::None) revertCommand(ch, was); continue; }
        applyCommand(ch, p.cmd[i], false);        // Z waits for the note-on
    }
}

/// The ticks a table row lasts: one, unless a G in the table asked for a
/// groove and the Player handed its counts over (setTableGroove).
uint16_t Driver::tableRowTicks(int ch, int row) const
{
    const Voice& v = v_[size_t(ch)];
    if (!v.tableGroove) return 1;
    const auto& g = tableGroove_[size_t(ch)];
    size_t len = 0;
    while (len < g.size() && g[len]) ++len;
    if (len == 0) return 1;
    const uint8_t n = g[size_t(row) % len];
    return uint16_t(n ? n : 1);
}

void Driver::stepTable(int ch)
{
    Voice& v = v_[size_t(ch)];
    if (!v.tableOn) return;
    const Table* t = bank_ ? bank_->table(v.tableSlot) : nullptr;
    if (!t) { v.tableOn = false; return; }
    if (v.delay > 0) { --v.delay; return; }
    // The row's transpose column and its commands take effect together.
    v.tableRow = v.tableStep;
    const TableStep& s = t->steps[v.tableRow];
    if (s.vol >= 0) {
        // Inside a note-on the level only changes the running state: the
        // note's own writes carry it, as a slot's would.
        const bool live = v.active && !inNoteOn_;
        if (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) { v.waveLevel = uint8_t(std::clamp<int>(s.vol / 4, 0, 3)); if (live) writeEnvelope(ch, false); }
        else { v.envVol = uint8_t(s.vol); if (live) writeEnvelope(ch, true); }
    }
    const Command c1 = s.cmd1.cmd == Cmd::Z ? resolveRandom(ch, s.cmd1, s.cmd2) : s.cmd1;
    const Command c2 = s.cmd2.cmd == Cmd::Z ? resolveRandom(ch, s.cmd2, s.cmd1) : s.cmd2;
    if (c1.cmd != Cmd::None) applyCommand(ch, c1, true);
    if (c2.cmd != Cmd::None) applyCommand(ch, c2, true);
    v.tableWait = tableRowTicks(ch, v.tableRow);
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
    // A slot whose value changed fires here, at the tick after the change.
    updateSlots(ch);
    if (params_[size_t(ch)].liveFollow && v.haveInst) {
        // Live follow: instrument, table, level, pan and transpose apply to the
        // sounding note instead of waiting for the next one (section 3).
        const ChannelParams before = v.p;
        v.p = params_[size_t(ch)];
        if (v.active) {
            if (before.instrument != v.p.instrument) { reloadInstrument(ch); return; }
            if (before.table != v.p.table || v.tableOverride != v.tableSlot) {
                const uint8_t tbl = v.tableOverride ? v.tableOverride : v.inst.table;
                if (tbl != v.tableSlot) { v.tableSlot = tbl; v.tableStep = 0; v.tableRow = 0; v.tableWait = 0; v.tableOn = tbl && bank_ && bank_->table(tbl); }
            }
            if (before.level != v.p.level) {
                applyLevelParam(ch);
                writeEnvelope(ch, v.inst.type == InstrumentType::Pulse || v.inst.type == InstrumentType::Noise);
            }
            if (before.pan != v.p.pan) { v.pan = v.p.pan != 255 ? Pan(v.p.pan & 3) : v.inst.pan; writeNr51(); }
        }
    }
    // pending delayed start
    if (v.pendingOn) {
        if (v.delay > 0) { --v.delay; }
        else {
            v.pendingOn = false;
            v.noteCmd[0] = v.pendingCmd[0]; v.noteCmd[1] = v.pendingCmd[1];
            v.pendingCmd[0] = {}; v.pendingCmd[1] = {};
            startVoice(ch, v.pendingNote, v.pendingVel, v.pendingPlain);
        }
    }
    // A released WAV or KIT walks its level down whether it is active or not.
    if (v.releasing) stepRelease(ch);
    if (!v.active) return;
    ++v.ticks;
    // kill countdown
    if (v.kill >= 0) { if (v.kill == 0) { stopVoice(ch, true); v.kill = -1; return; } --v.kill; }
    // The table: a row per tick, or per the row length a G inside it asked
    // for. A Step-mode table advances at notes instead (section 7).
    if (v.inst.tableMode == TableMode::Tick) {
        if (v.tableWait > 1) --v.tableWait;
        else stepTable(ch);
    }
    if (!v.active) return;
    // chord: one step every cmdRate + 1 ticks
    if (v.chordN && ++v.chordCount >= uint8_t(v.inst.cmdRate + 1)) { v.chordCount = 0; v.chordIdx = uint8_t((v.chordIdx + 1) % v.chordN); }
    // duty sequence
    if (v.inst.type == InstrumentType::Pulse && v.inst.dutySeqLen) {
        v.dutyIdx = uint8_t((v.dutyIdx + 1) % v.inst.dutySeqLen);
        const uint8_t d = uint8_t(v.inst.dutySeq[v.dutyIdx] & 3);
        if (d != v.duty) { v.duty = d; emit(regAddr(ch, 1), uint8_t((d << 6) | lengthCode6(v.inst.length))); }
    }
    // noise sweep
    if (v.inst.type == InstrumentType::Noise && v.noiseSweep) { v.noiseShift = uint8_t(std::clamp<int>(int(v.noiseShift) + v.noiseSweep, 0, 13)); v.inst.noiseManual = true; }
    // retrigger, every y ticks x (cmdRate + 1), or once when y is zero
    bool retrig = false;
    if (v.retrigOnce) { v.retrigOnce = false; retrig = true; }
    else if (v.retrigEvery) {
        const uint16_t every = uint16_t(uint16_t(v.retrigEvery) * uint16_t(v.inst.cmdRate + 1));
        if (++v.retrigCount >= every) { v.retrigCount = 0; retrig = true; }
    }
    if (retrig && v.retrigStep && v.inst.type != InstrumentType::Wave && v.inst.type != InstrumentType::Kit)
        v.envVol = uint8_t(std::clamp<int>(int(v.envVol) + v.retrigStep, 0, 15));
    // wave frames
    if (v.inst.type == InstrumentType::Wave && v.inst.frameAdvance) {
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
    // With the pitch speed at Tick this tick is the pitch update: the vibrato
    // phase, a slide and a P bend move here rather than at 360 Hz.
    if (!v.pitchClockOn && (v.inst.type == InstrumentType::Pulse || v.inst.type == InstrumentType::Wave)
        && pitchSpeed(v) == PitchSpeed::Tick) pitchStep(ch, true);
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
    if (gateDirty_) writeNr51();
    for (int ch = 0; ch < 4; ++ch) tick(ch);
    ++tickCount_;
}

/* ------------------------------------------------------------- events */

void Driver::handleEvent(NoteEvent& e)
{
    const int ch = e.channel & 3;
    Voice& v = v_[size_t(ch)];
    // Channels playing from the tracker ignore the piano roll and vice versa.
    // Only notes are gated: a flush silences a channel whatever its source is.
    if (song_) {
        const bool trackerCh = song_->noteSource[size_t(ch)] == tracker::NoteSource::Tracker;
        const bool note = e.kind == NoteEvent::NoteOn || e.kind == NoteEvent::NoteOff;
        if (note && e.source == NoteEvent::Tracker && !trackerCh) return;
        if (note && e.source == NoteEvent::Midi && trackerCh && !(recordMask_ & (1u << ch))) return;
    }
    switch (e.kind) {
        case NoteEvent::NoteOn:
            if (e.b == 0) { noteOff(ch, e.a); break; }
            noteOn(ch, e.a, e.b, e.source == NoteEvent::Tracker ? &e : nullptr);
            // Stamped on the event itself, so the recorder reads what *this*
            // note did rather than the channel's latest (section 9.4).
            e.plain = v.notePlain; e.loaded = v.noteInst;
            break;
        case NoteEvent::NoteOff:
            // A cell's OFF ends the note; its instrument, table and command
            // columns are still the cell's, and apply from this step on
            // (section 3: cells and slots are one code path).
            noteOff(ch, e.a);
            if (e.source == NoteEvent::Tracker) applyCellColumns(ch, e);
            break;
        case NoteEvent::PitchBend: v.bend = double(e.value) / 8192.0 * 2.0; if (v.active) writePeriod(ch, false); break;
        case NoteEvent::Control:
            if (e.a == 1) v.vibDepth = uint8_t(e.b / 8);       // the mod wheel is vibrato depth
            else if (e.a == 7 && v.active) { if (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) { v.waveLevel = uint8_t(e.b / 32); writeEnvelope(ch, false); } else { v.envVol = uint8_t(e.b / 8); writeEnvelope(ch, true); } }
            else if (e.a == 120 || e.a == 123) allNotesOff(ch);
            break;
        case NoteEvent::AllNotesOff: allNotesOff(ch); break;
        case NoteEvent::Command:
            // A tracker cell with no note: its columns are the slots from here
            // on, and its instrument column reloads the instrument first.
            applyCellColumns(ch, e);
            break;
    }
}

/// The columns of a cell that does not start a note: the table override, an
/// instrument column, which reloads and fires the slots itself, and the two
/// commands, applied once at this step (sections 3 and 12).
void Driver::applyCellColumns(int ch, const NoteEvent& e)
{
    Voice& v = v_[size_t(ch)];
    if (e.table) { v.tableOverride = e.table; if (v.active) { v.tableSlot = e.table; v.tableStep = 0; v.tableRow = 0; v.tableWait = 0; v.tableOn = bank_ && bank_->table(e.table); } }
    if (e.inst && e.inst != v.ksInstrument) {
        v.ksInstrument = e.inst; v.ksFromCell = true;
        // The instrument reloads and fires the slots; the cell's commands
        // follow it, as they would at a note (section 3's order).
        if (v.haveInst) { reloadInstrument(ch); applyCellCommands(ch, e.cmd1, e.cmd2); return; }
    }
    applyCellCommands(ch, e.cmd1, e.cmd2);
}

/* ------------------------------------------------------------ process */

void Driver::process(NoteEvent* events, size_t n, uint32_t numSamples, uint64_t frameAbs,
                     const TickPoint* ticks, size_t nTicks,
                     const std::function<uint64_t(uint64_t)>& cycleAt,
                     std::vector<RegWrite>& out)
{
    out_ = &out;
    const size_t logFrom = out.size();
    const uint64_t blockEnd = frameAbs + numSamples;

    // Events land where the host put them (sample accurate); the tick drives
    // what a driver runs from its interrupt: tables, frames, the command
    // slots. The per-voice pitch clock runs at 360 Hz between them, in cycle
    // order with both, so vibrato and slides are where they really are.
    // With notes-on-tick the note-ons and note-offs wait for the next tick as
    // a tracker's do -- bends and controllers never do, and tracker cells
    // already sit on ticks.
    size_t ei = 0;
    auto moveTo = [&](uint64_t c) { if (c != cycle_) { cycle_ = c; burst_ = 0; } };
    auto offOf = [&](uint32_t o) { return numSamples ? std::min<uint32_t>(o, numSamples - 1) : 0u; };
    auto pitchBefore = [&](uint64_t limit) {
        for (int ch = 0; ch < 4; ++ch) {
            // A jump in the timeline (a locate, a long gap) must not walk the
            // clock forward one update at a time.
            Voice& v = v_[size_t(ch)];
            if (v.pitchClockOn && v.pitchClock + kPitchCycles * 4096 < limit)
                v.pitchClock = limit - (limit - v.pitchClock) % kPitchCycles;
        }
        for (;;) {
            int best = -1; uint64_t at = 0;
            for (int ch = 0; ch < 4; ++ch) {
                const Voice& v = v_[size_t(ch)];
                if (!v.pitchClockOn || !v.active || v.pitchClock >= limit) continue;
                if (best < 0 || v.pitchClock < at) { best = ch; at = v.pitchClock; }
            }
            if (best < 0) break;
            // A tick's register writes go out as one burst, an instruction
            // pair apart. A 360 Hz update landing inside that burst cannot
            // interleave with it on real hardware, and must not overtake
            // writes that were computed before it, so it follows the burst.
            if (at > cycle_ + uint64_t(burst_) * kBurstSpacing) moveTo(at);
            pitchStep(best, false);
            v_[size_t(best)].pitchClock = at + kPitchCycles;
        }
    };
    auto runEvent = [&](NoteEvent& e) {
        const uint64_t at = cycleAt(frameAbs + offOf(e.offset));
        pitchBefore(at);
        moveTo(at);
        handleEvent(e);
    };
    auto waits = [this](const NoteEvent& e) {
        return notesOnTick_ && e.source == NoteEvent::Midi && (e.kind == NoteEvent::NoteOn || e.kind == NoteEvent::NoteOff);
    };
    // A note that has waited for a tick reports back to the event it came
    // from, so the recorder still sees what it did -- if that event is still
    // this block's. One held over a block boundary has none to report to.
    auto fire = [&](size_t i) {
        handleEvent(pending_[i]);
        if (pendingFrom_[i]) { pendingFrom_[i]->plain = pending_[i].plain; pendingFrom_[i]->loaded = pending_[i].loaded; }
    };
    auto hold = [&](NoteEvent& e) {
        if (pendingCount_ == pending_.size()) {
            // More waiting notes than the queue holds: the oldest one runs now
            // rather than the newest jumping the line, so a note-off can never
            // execute before its note-on (section 8).
            const uint64_t at = cycleAt(frameAbs + offOf(e.offset));
            pitchBefore(at);
            moveTo(at);
            fire(0);
            for (size_t i = 1; i < pendingCount_; ++i) { pending_[i - 1] = pending_[i]; pendingFrom_[i - 1] = pendingFrom_[i]; }
            --pendingCount_;
        }
        pendingFrom_[pendingCount_] = &e;
        pending_[pendingCount_++] = e;
    };

    for (size_t k = 0; k < nTicks; ++k) {
        const uint32_t off = std::min<uint32_t>(ticks[k].offset, numSamples ? numSamples - 1 : 0);
        while (ei < n && events[ei].offset <= off) { NoteEvent& e = events[ei++]; if (waits(e)) hold(e); else runEvent(e); }
        const uint64_t at = cycleAt(frameAbs + off);
        pitchBefore(at);
        moveTo(at);
        for (size_t i = 0; i < pendingCount_; ++i) fire(i);   // notes that were waiting for a tick
        pendingCount_ = 0;
        tickAll();
    }
    while (ei < n) { NoteEvent& e = events[ei++]; if (waits(e)) hold(e); else runEvent(e); }
    pitchBefore(cycleAt(blockEnd));
    // The caller's events go away with the block; a note still waiting has
    // nothing left to report to.
    for (size_t i = 0; i < pendingCount_; ++i) pendingFrom_[i] = nullptr;

    // --- wave RAM streaming, cycle domain ---------------------------------
    scheduleStreams(cycleAt(frameAbs), cycleAt(blockEnd));

    std::stable_sort(out.begin(), out.end(), [](const RegWrite& a, const RegWrite& b) { return a.cycle < b.cycle; });
    if (writeLog_) for (size_t i = logFrom; i < out.size(); ++i) writeLog_->push_back(out[i]);
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
    w.volume = (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) ? v.waveLevel : v.volume;
    w.duty = v.duty; w.frame = uint8_t(v.frameIdx + 1);
    w.tableSlot = v.tableOn ? v.tableSlot : 0; w.tableStep = v.tableStep;
    // The running state the strip prints under the two slots (section 3).
    w.envVol = v.envVol; w.envRate = v.envRate; w.envDir = v.envDir == EnvDir::Up ? 1 : 0;
    w.vibSpeed = v.vibSpeed; w.vibDepth = v.vibDepth;
    // What the pitch effects have added, in register units: P and a slide
    // give them directly, and a Drum-mode bend through the note it moves.
    int off = v.pOffset + (v.slideDrum ? 0 : slideResidual(v));
    const int32_t fine = v.fineOffset + (v.slideDrum ? slideResidual(v) : 0);
    if (fine && v.inst.type != InstrumentType::Noise && v.inst.type != InstrumentType::Kit) {
        const bool wave = v.inst.type == InstrumentType::Wave;
        const double n = noteOfVoice(ch);
        const int a = periodForNote(n, wave), b = periodForNote(n - double(fine) / 32.0, wave);
        if (a >= 0 && b >= 0) off += a - b;
    }
    w.pitchOffset = int16_t(std::clamp(off, -2047, 2047)); w.pan = uint8_t(v.pan);
    for (int r = 0; r < 5; ++r) w.regs[r] = known_[size_t(ch * 5 + r)] ? shadow_[size_t(ch * 5 + r)] : 0;
}

} // namespace chipboy::driver
