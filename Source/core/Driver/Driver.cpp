#include "core/Driver/Driver.h"

#include <algorithm>
#include <cmath>

namespace chipboy::driver {

using namespace chipboy::bank;

namespace {

constexpr uint32_t kBurstSpacing = 20;     ///< cycles between consecutive writes from one tick: ld a,n / ldh (n),a
constexpr uint32_t kCpuHz = 4194304u;
constexpr int      kMaxTicksPerBlock = 512;

/// The pitch clock (section 7). LSDj sets the Game Boy's timer once, at boot,
/// and never moves it: the interrupt is every **11712 cycles** -- 358.12 Hz,
/// measured (docs/LSDJ_PARITY.md section 1). It is one clock for the whole
/// driver and it free-runs: it is not restarted at a note-on, because a real
/// driver's timer does not know that a note began. Where in that period a note
/// falls is where the player pressed play, not a property of the driver.
constexpr uint64_t kPitchCycles = 11712;

/// The vibrato's phase is a six-bit counter, 0-63 to the cycle, and the speed
/// is the step: **one cycle is 64/(x+1) updates** (measured for every speed).
/// It is carried in ninths of a phase unit so that Tick mode's steps, which
/// are thirds and ninths of one, are exact.
constexpr uint32_t kVibPhase = 64;
constexpr uint32_t kVibNinths = kVibPhase * 9;

/// Tick mode's phase step per tracker tick, in ninths of a phase unit
/// (measured, docs/LSDJ_PARITY.md section 3): one cycle is 96, 72, 64, 48, 36,
/// 32, 24, 18, 16, 12, 9, 8, 6, 4.5, 4 or 3 **ticks** -- the period halves
/// every three speeds, so it is not 64/(x+1) as the pitch clock's is.
constexpr uint32_t kVibTickStep9[16] = { 6, 8, 9, 12, 16, 18, 24, 32, 36, 48, 64, 72, 96, 128, 144, 192 };

/// V's depth in 1/256 semitones: LSDj's own table, confirmed for every depth
/// against the ROM -- 0 is an eighth of a semitone and 15 is eight.
constexpr int kVibDepth256[16] = {
    32, 64, 96, 128, 192, 256, 384, 512, 640, 768, 896, 1024, 1280, 1536, 1792, 2048
};

/// P's step per pitch update, in 1/256 of a semitone, for a magnitude 0-127.
/// Measured over all 127 values: the step is the sum of a ramp that rises by
/// one every four, `sum(ceil(j/4), j = 1..m)`, which closes to the form below
/// and fits the whole sweep to a part in a hundred (docs/LSDJ_PARITY.md 5).
constexpr int bendStep256(int m)
{
    const int q = m / 4, r = m % 4;
    return (q + 1) * (2 * q + r);
}

/// Drum mode works in the period register, and one semitone of a pitch effect
/// is worth this many period units there whatever the note -- measured over
/// P's whole sweep of 127 values (19.110) and over V's sixteen depths (19.1).
/// It is the same constant for both, which is what says Drum is one domain and
/// not two (docs/LSDJ_PARITY.md sections 3 and 5).
constexpr double kDrumUnitsPerSemitone = 19.11;

/// The instrument envelope's own step interval, in pitch-clock periods, for
/// the ENV nibble's rates 1-7 (measured; 6 and 7 are the same interval).
/// LSDj never lets the chip's envelope run -- it writes NRx2 with the period
/// nibble at 8, a hold -- and steps the level itself on this table.
constexpr int kEnvStepPeriods[8] = { 0, 6, 11, 15, 20, 27, 36, 36 };

/// The spacing of the NRx2 writes a level change is made of (measured): the
/// three writes of one step down are sixteen cycles apart and successive steps
/// a hundred and twelve, one step up is a single write and they come
/// sixty-eight apart.
constexpr uint32_t kZombieInner = 16, kZombieDownStep = 112, kZombieUpStep = 68;

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

/* ------------------------------------------------------------ zombie mode
 *
 * Section 26: a level change on a running pulse or noise channel is made the
 * way a Game Boy driver can make it -- by writing NRx2 without a trigger and
 * living with what the chip does to the volume. What it does is this (the
 * APU's own rule, Apu::writeSquare case 2 and writeNoise case 2, reference
 * section 10.2), with the state *before* the write deciding:
 *
 *     if (period == 0 && running) volume += 1;
 *     else if (direction is down)  volume += 2;
 *     if (the direction bit changed) volume = 16 - volume;
 *     volume &= 15;
 *
 * So on a channel whose envelope is holding -- period 0, which is where every
 * software level lives -- a write with the same direction bit adds one and a
 * write that flips it lands on 15 - v. Both consoles the APU models take the
 * same rule; setModel() is read here so that a console with its own would get
 * its own sequence rather than this one (docs/HARDWARE_DRIVER_AUDIT.md).
 */

/// One NRx2 write's effect on the chip's volume.
uint8_t zombieVolume(uint8_t vol, uint8_t oldPeriod, bool oldUp, bool running, bool newUp)
{
    uint8_t v = vol;
    if (oldPeriod == 0 && running) v = uint8_t(v + 1);
    else if (!oldUp)               v = uint8_t(v + 2);
    if (oldUp != newUp)            v = uint8_t(16 - v);
    return uint8_t(v & 15);
}

/// The NRx2 byte a note-on writes: the level in the high nibble and the low
/// nibble **forced to 8** -- amplitude, direction up, envelope period zero.
/// LSDj writes that for every instrument, whatever the ENV byte says, and runs
/// the envelope itself (docs/LSDJ_PARITY.md section 2); the direction bit
/// matters because it is the state every later zombie write starts from, and
/// because it leaves the DAC on at level zero.
uint8_t nrx2Hold(int level) { return uint8_t((uint8_t(level & 15) << 4) | 0x08); }

/// The two zombie-mode steps, byte for byte as the ROM writes them: one step
/// **down** is `09 11 18` and one step **up** is `08` (measured on both
/// consoles). Under the APU's own rule, from a holding envelope, the triple
/// lands on v - 1 and the single on v + 1.
constexpr uint8_t kZombieDown[3] = { 0x09, 0x11, 0x18 };
constexpr uint8_t kZombieUp = 0x08;

/// What one NRx2 byte does to the chip's volume, given the register state
/// before it: this is `Apu::writeSquare` case 2, so the driver's model of the
/// volume and the chip's stay together.
uint8_t zombieAfter(uint8_t vol, uint8_t oldPeriod, bool oldUp, bool running, uint8_t value)
{
    return zombieVolume(vol, oldPeriod, oldUp, running, (value & 0x08) != 0);
}

} // namespace

/* ------------------------------------------------------------ pitch */

/// One entry of the note table: the period of a whole semitone, rounded, as
/// LSDj's own table holds it.
int Driver::periodOfSemitone(int note, bool waveChannel)
{
    const double p = 2048.0 - (waveChannel ? 65536.0 : 131072.0) / noteHz(double(note));
    if (p < 0.0) return -1;
    return std::min(2047, int(std::floor(p + 0.5)));
}

/// The period of a note and a fraction. The table is one entry a semitone and
/// the fraction is interpolated **in period units**, not in frequency: LSDj
/// does that, and it is measurable -- a vibrato half a semitone below C-5
/// lands on 1791, where the exponential curve gives 1790. Whole notes are the
/// same number either way (docs/LSDJ_PARITY.md section 3).
int Driver::periodForNote(double note, bool waveChannel)
{
    const double base = std::floor(note);
    const int lo = periodOfSemitone(int(base), waveChannel);
    if (lo < 0) return -1;
    const double frac = note - base;
    if (frac <= 0.0) return lo;
    const int hi = periodOfSemitone(int(base) + 1, waveChannel);
    if (hi < 0) return lo;
    return std::min(2047, int(std::floor(double(lo) + frac * double(hi - lo) + 0.5)));
}

/// The same, unrounded, for Drum mode, where the offsets are period units and
/// the rounding happens once at the end.
int Driver::bendStepFor(int magnitude) { return bendStep256(std::clamp(magnitude, 0, 127)); }

double Driver::periodRealForNote(double note, bool waveChannel)
{
    const double base = std::floor(note);
    const int lo = periodOfSemitone(int(base), waveChannel);
    if (lo < 0) return -1.0;
    const double frac = note - base;
    if (frac <= 0.0) return double(lo);
    const int hi = periodOfSemitone(int(base) + 1, waveChannel);
    if (hi < 0) return double(lo);
    return double(lo) + frac * double(hi - lo);
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
    pitchClockAt_ = 0; pitchClockValid_ = false; mixerInit_ = false;
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
    out_->push_back({ cycle_ + burst_, addr, v });
    burst_ += kBurstSpacing;
}

void Driver::emitAt(uint64_t cycle, uint16_t addr, uint8_t v)
{
    const size_t idx = size_t(addr - 0xFF10);
    if (idx < shadow_.size()) { shadow_[idx] = v; known_[idx] = true; }
    out_->push_back({ cycle, addr, v });
}

uint8_t Driver::levelFromVelocity(uint8_t vel) const { return uint8_t(std::min(15, (vel * 16) / 128)); }

/// A Hybrid channel's Instrument, Table and command slots are inert: the
/// song's cells choose the instrument and carry the commands, and the lane
/// would fight them (section 20). Level, pan, transpose and the velocity mode
/// still apply, so only four fields are cleared.
ChannelParams Driver::effective(int ch) const
{
    ChannelParams p = params_[size_t(ch & 3)];
    if (!hybrid(ch)) return p;
    p.instrument = 0; p.table = 0;
    p.cmd[0] = Command{}; p.cmd[1] = Command{};
    return p;
}

/* --------------------------------------------------------- resolution */

int Driver::resolveSlot(int ch, uint8_t vel) const
{
    const ChannelParams p = effective(ch);
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
    v.p = effective(ch);
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
    v.vibSpeed = uint8_t(std::clamp<int>(i.vib.speed, 0, 15));
    v.vibDepth = i.vib.depth;
    v.vibOn = i.vib.depth != 0;
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
    // A shaped instrument brings its envelope with it: the load triggers the
    // channel, so the shape starts again from its attack (sections 26, 27).
    v.shapedOn = core.env.mode == EnvMode::Shaped;
    v.shapedTaken = false; v.shapedRelease = false; v.shapedTick = 0; v.shapedFrom = 0;
    if (v.shapedOn) {
        const uint8_t level = shapedLevel(v);
        if (core.type == InstrumentType::Wave || core.type == InstrumentType::Kit) v.waveLevel = uint8_t(level / 4);
        else { v.envVol = level; v.envRate = 0; v.envDir = EnvDir::Up; }
    }
    beginTableRun(ch, v.tableOverride ? v.tableOverride : core.table);
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
    const ChannelParams p = effective(ch);
    const bool hy = hybrid(ch);
    // The command octave (section 13): notes 0-11 never sound and never join
    // the held stack. They fire the channel's slots on whatever it is playing,
    // without a trigger, so a held note can be shaped after its attack; with
    // nothing sounding the persistent letters still land in the running state.
    if (note < 12) {
        // On a Hybrid channel the command octave is inert: the cells carry the
        // commands, and the slots it would fire are empty (section 20).
        if (hy) return;
        syncSlots(ch);
        fireSlots(ch, /*live*/ true);
        if (cell) applyCellCommands(ch, cell->cmd1, cell->cmd2);
        return;
    }
    // Keyswitch octave: selects an instrument, never sounds.
    const Instrument* cur = local_[size_t(ch)] ? local_[size_t(ch)] : bank_ ? bank_->instrument(p.instrument) : nullptr;
    const InstrumentType t = cur ? cur->type : (ch == 2 ? InstrumentType::Wave : ch == 3 ? InstrumentType::Noise : InstrumentType::Pulse);
    if (p.keyswitch || hy) {
        // Inert on a Hybrid channel, whatever the parameter says: the cell's
        // instrument column chooses, so a note there does nothing at all.
        const int base = keyswitchBase(t);
        if (note >= base && note < base + 12) {
            if (!hy) { v.ksInstrument = uint8_t(note - base + 1); v.ksFromCell = false; }
            return;
        }
    }
    // Whether a note is still held under this one decides, with the
    // instrument's Overlap, if the new note is plain or bare (section 8).
    const bool over = v.active && v.haveInst && v.heldCount > 0;
    if (v.heldCount < v.held.size()) v.held[v.heldCount++] = note;
    // The parameters' slots are in force from here; the cell's own commands
    // fire once, after them, when the note starts (section 12).
    syncSlots(ch);
    v.noteCmd[0] = {}; v.noteCmd[1] = {};
    // Hybrid: the cell at this tick chose the instrument and left its commands
    // waiting; the note loads that instrument and then takes them, exactly as
    // a cell's own note would (section 20).
    if (hy) {
        if (v.heldCmdOn) {
            v.noteCmd[0] = v.heldCmd[0]; v.noteCmd[1] = v.heldCmd[1];
            v.heldCmdOn = false; v.heldDelay = 0; v.heldCmd[0] = {}; v.heldCmd[1] = {};
            v.hybridSlide = {};                     // this tick's cell says what happens
        } else if (v.hybridSlide.cmd != Cmd::None) {
            // An L from an earlier cell: the portamento it asked for is this
            // note's, from wherever the channel is (section 20).
            v.noteCmd[0] = v.hybridSlide;
            v.hybridSlide = {};
        }
    }
    if (cell) {
        if (cell->inst) { v.ksInstrument = cell->inst; v.ksFromCell = true; }   // a cell's instrument column names it exactly
        // The channel's table override, from this step on. An instrument
        // column with a blank TBL ends it (section 46): the note plays the
        // instrument's own table, or the Table parameter's.
        if (cell->table) v.tableOverride = cell->table;
        else if (cell->inst) v.tableOverride = v.tableParam;
        v.noteCmd[0] = cell->cmd1; v.noteCmd[1] = cell->cmd2;
        v.cellTranspose = cell->transpose;                    // the chain row's (section 48)
    } else v.cellTranspose = 0;
    // A tracker cell is plain when its instrument column is filled and bare
    // when it is blank; a MIDI note is bare only when it lands over a held
    // note, would load the instrument already sounding, and that instrument
    // overlaps legato.
    bool plain = true;
    if (cell) plain = cell->inst != 0;
    // A note over a held one is bare only when it *moves*: a MIDI note-on at
    // the pitch already sounding is a repeat -- a drum hit in succession -- and
    // is plain whatever Overlap says (section 31).
    else if (over && note != v.note && v.inst.overlap == Overlap::Legato && instrumentKey(ch, vel) == v.instKey) plain = false;
    // What this note is recorded as (section 9.4): the instrument it loads,
    // and whether it was plain. Decided here, so a note D holds back reports
    // the same thing as one that starts at once. A bare note keeps the slot
    // the note under it loaded, which is the one still sounding.
    v.notePlain = plain;
    if (plain) v.noteInst = uint8_t(std::clamp(resolveSlot(ch, vel), 0, kInstrumentSlots));
    // D postpones the start, whether it came from a cell or a slot. The
    // cell's commands wait with it and fire when the note does.
    // The cell's own D, whether it came with the note or was held for this
    // tick by a Hybrid cell; then a slot's.
    const int delay = delayFor(ch, &v.noteCmd[0], &v.noteCmd[1]);
    const uint8_t velRule = cell ? (cell->velSet ? 2 : 1) : 0;
    if (delay >= 0) {
        v.pendingOn = true; v.pendingNote = note; v.pendingVel = vel; v.pendingPlain = plain; v.pendingVelRule = velRule;
        v.delay = int16_t(delay);
        // The cell's commands wait with the note rather than in the slot the
        // next note would read (section 12).
        v.pendingCmd[0] = v.noteCmd[0]; v.pendingCmd[1] = v.noteCmd[1];
        v.noteCmd[0] = {}; v.noteCmd[1] = {};
        return;
    }
    v.velRule = velRule;
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
    v.pitchClockOn = false; v.retrigEvery = 0; v.retrigOn = false; v.retrigFast = false; v.bendSpeed = 0;
    // A shaped envelope has its own release: from the level the note-off found
    // to silence, one level per tick, over its own curve (section 27). A level
    // change that took the envelope over leaves the chip's release instead.
    if (v.shapedOn && !v.shapedTaken) {
        if (!v.dacOn || v.inst.env.releaseTicks == 0) { v.shapedOn = false; stopVoice(ch, true); return; }
        v.shapedRelease = true; v.shapedTick = 0;
        v.shapedFrom = (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit)
                       ? uint8_t(v.waveLevel * 5) : v.envVol;
        v.releasing = true;
        return;
    }
    if (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) {
        v.releasing = v.dacOn;              // the level steps down from here, a tick apart
        return;
    }
    // A held or rising envelope would never finish: a decrease at rate 1 makes
    // the note fade out. The level is where it is, so the rate reaches the
    // register through a zombie sequence rather than a rewrite that would move
    // the volume with it -- and never through a trigger (section 26).
    if (v.dacOn && (v.envRate == 0 || v.envDir == EnvDir::Up)) {
        v.envRate = 1; v.envDir = EnvDir::Down;
        v.envCount = 0;
        setLevel(ch);
    }
    // The driver runs the fade itself, off the pitch clock, and the note ends
    // when it reaches silence (sections 26 and 8).
    v.pulseReleasing = v.dacOn;
}

/// WAV and KIT have no envelope generator, so their release is four levels one
/// tick apart: 100, 50, 25, mute (section 8). A shaped envelope's release is
/// its own curve instead (section 27).
void Driver::stepRelease(int ch)
{
    Voice& v = v_[size_t(ch)];
    if (v.shapedOn && v.shapedRelease) { stepShaped(ch); return; }
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
    v.note = note; v.vel = vel; v.active = true; v.killed = false; v.releasing = false; v.pulseReleasing = false;
    // The chain row's transpose, under the instrument's Transpose flag like
    // the table's column (section 48), and the instrument's own offset on the
    // second pulse (section 49). The note itself stays what the cell said.
    v.noteTsp = core.transpose ? v.cellTranspose : int8_t(0);
    v.instTranspose = (ch == 1 && core.type == InstrumentType::Pulse) ? core.pu2Transpose : int8_t(0);
    v.instKey = instrumentKey(ch, vel);
    v.ticks = 0; v.vibPhase9 = 0; v.pitchCount = 0;
    v.fineOffset = 0; v.fineQueued = 0; v.drumOffset = 0.0; v.bendSpeed = 0; v.sliding = false; v.slideLeft = 0; v.slideOff256 = 0;
    v.chordN = 0; v.chordIdx = 0; v.chordCount = 0;
    v.dutyIdx = 0; v.kill = -1; v.retrigEvery = 0; v.retrigStep = 0; v.retrigCount = 0; v.retrigOn = false; v.retrigFast = false; v.envCount = 0; v.lastCmd = {}; v.frameIdx = 0;
    v.rng = v.rng * 1664525u + 1013904223u + note;
    restartPitchClock(ch);
    // volume from velocity: a MIDI note asks the Velocity mode, a cell's VEL is
    // a start volume in any instance and a blank VEL is the instrument's own
    const bool velToVolume = v.velRule == 2 || (v.velRule == 0 && v.p.velocityMode == 0);
    if (velToVolume && (core.type == InstrumentType::Pulse || core.type == InstrumentType::Noise)) v.envVol = levelFromVelocity(vel);
    applyLevelParam(ch);
    // A shaped envelope owns the level from here (section 27): the chip's own
    // envelope holds at period 0 and its direction bit is up, so a level of
    // zero keeps the DAC on and every step of the shape can be a zombie write.
    v.shapedOn = core.env.mode == EnvMode::Shaped;
    v.shapedTaken = false; v.shapedRelease = false; v.shapedTick = 0; v.shapedFrom = 0;
    if (v.shapedOn) {
        const uint8_t level = shapedLevel(v);
        if (core.type == InstrumentType::Wave || core.type == InstrumentType::Kit) v.waveLevel = uint8_t(level / 4);
        else { v.envVol = level; v.envRate = 0; v.envDir = EnvDir::Up; }
    }
    // table
    const uint8_t tbl = v.tableOverride ? v.tableOverride : core.table;
    v.tableSlot = tbl; v.tableOn = tbl && bank_ && bank_->table(tbl);
    v.tableWait = 0;
    if (v.tableOn) ++v.tableRun;                  // a note-on starts a run of its own (section 32)
    // A Step-mode table advances one row per trigger instead of restarting,
    // which is the whole point of it; a table that was not already running
    // starts at its first row (section 7).
    if (core.tableMode != TableMode::Step || hadTable != tbl) { v.tableStep = 0; v.tableRow = 0; v.tableGroove = 0; }
    if (core.dutySeqLen) v.duty = uint8_t(core.dutySeq[0] & 3);
    // The table's first row fires with the note-on, in the same event, never
    // at the next tick (section 31): a table that drops the pitch or the level
    // starts dropping at once, so the raw note is never heard -- which is what
    // it sounded like when a note fell just after a tick. Inside the note-on
    // the row only changes the running state, so the note's own writes carry
    // its transpose, its level and its commands; the rows after it step on the
    // ticks, and tableJustStarted keeps this tick from taking a second one.
    if (v.tableOn) {
        const bool wasIn = inNoteOn_; inNoteOn_ = true;
        stepTable(ch);
        inNoteOn_ = wasIn;
        v.tableJustStarted = true;
    }
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
            writeEnvelope(ch, false);   // the whole register; the trigger follows with the period
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
    // LSDj writes the pan at every note-on whether or not it moved (measured,
    // docs/LSDJ_PARITY.md section 2): a driver sets the mixer with the note.
    writeNr51(true);
}

/// K, and the end of a note: LSDj takes the level to zero with the same
/// zombie steps as any other level change -- fifteen down-triples, about
/// 1700 cycles -- and leaves the DAC on (docs/LSDJ_PARITY.md section 9).
/// A panic still clears the DAC: killDac() is unconditional silence.
void Driver::killLevel(int ch)
{
    Voice& v = v_[size_t(ch)];
    if (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) { killDac(ch); return; }
    if (!v.dacOn || !v.hwOn) { killDac(ch); return; }
    v.shapedTaken = true; v.envVol = 0; v.envRate = 0;
    setLevel(ch);
    v.killed = true;
}

void Driver::killDac(int ch)
{
    Voice& v = v_[size_t(ch)];
    if (!v.dacOn) return;
    // Clearing the DAC holds the level on this hardware, so it is silent
    // (reference section 9).
    if (ch == 2) emit(regAddr(2, 0), 0x00, true);
    else { emit(regAddr(ch, 2), 0x00, true); v.hwPeriod = 0; v.hwUp = false; v.hwInitial = 0; }
    v.dacOn = false; v.hwOn = false; v.killed = true;
}

void Driver::stopVoice(int ch, bool kill)
{
    Voice& v = v_[size_t(ch)];
    v.active = false; v.tableOn = false; v.sliding = false; v.chordN = 0; v.kitOn = false; v.streamActive = false; v.pendingOn = false;
    v.pitchClockOn = false; v.releasing = false; v.pulseReleasing = false;
    v.shapedOn = false; v.shapedRelease = false; v.tableJustStarted = false;
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
    // A note-on still waiting for its tick is a delayed start like any other
    // (section 8): it has to go, or the queue puts back the note this just
    // silenced -- which the fuzz found, as a channel still sounding after a
    // panic (section 28).
    size_t keep = 0;
    for (size_t i = 0; i < pendingCount_; ++i) {
        if ((pending_[i].channel & 3) == (ch & 3)) continue;
        if (keep != i) { pending_[keep] = pending_[i]; pendingFrom_[keep] = pendingFrom_[i]; }
        ++keep;
    }
    pendingCount_ = keep;
    v.delay = -1; v.kill = -1;
    v.heldCmdOn = false; v.heldDelay = 0; v.heldCmd[0] = {}; v.heldCmd[1] = {};
    v.hybridSlide = {};
    v.retrigEvery = 0; v.retrigCount = 0; v.retrigOn = false; v.retrigFast = false;
    v.bendSpeed = 0; v.slideLeft = 0; v.chordIdx = 0; v.chordCount = 0;
    stopVoice(ch, true);
}

/* ------------------------------------------------------------- pitch */

/// What is left of a slide, in 1/256 of a semitone. L walks the pitch to the
/// note by a fixed step, `(target - source) / (x + 1)` truncated toward zero,
/// and the offset is only exactly zero on the update *after* the last step --
/// which is what the ROM writes (docs/LSDJ_PARITY.md section 4).
int32_t Driver::slideResidual(const Voice& v) const
{
    return v.sliding ? v.slideOff256 : 0;
}

/// The vibrato's offset in 1/256 semitones, from the phase. A **symmetric
/// triangle about the note**: the phase is a six-bit counter, the offset is
/// the depth table times the triangle, and the direction bit only says which
/// half of the swing comes first. Saw and square keep the shape ChipBoy gave
/// them -- one-sided, from the note -- because the ROM's own saw and square
/// could not be read off the register log (docs/LSDJ_PARITY.md section 13).
int Driver::vibratoFine(const Voice& v) const
{
    if (!v.vibOn || v.ticks < v.vibDelay) return 0;
    const uint32_t ph = (v.vibPhase9 / 9u) % kVibPhase;         // 0-63
    const int depth = kVibDepth256[v.vibDepth & 15];
    double u = 0.0;                                             // -1 .. +1
    switch (v.vibShape) {
        case VibShape::Triangle:
            u = ph <= 16 ? double(ph) / 16.0
              : ph <= 48 ? 2.0 - double(ph) / 16.0
                         : double(ph) / 16.0 - 4.0;
            break;
        case VibShape::Saw:    u = double(ph) / double(kVibPhase); break;
        case VibShape::Square: u = ph < kVibPhase / 2 ? 0.0 : 1.0; break;
    }
    const double off = u * double(depth);
    return v.vibDir == VibDir::Up ? int(std::lround(off)) : -int(std::lround(off));
}

/// The vibrato in Drum mode, in period units: the same triangle and the same
/// depth table, but the swing is the period's, not the note's -- LSDj works in
/// the register there and one semitone is worth kDrumUnitsPerSemitone units.
double Driver::vibratoDrumUnits(const Voice& v) const
{
    if (!v.vibOn || v.ticks < v.vibDelay) return 0.0;
    return double(vibratoFine(v)) / 256.0 * kDrumUnitsPerSemitone;
}

/// The note the channel is at, in semitones and vibrato apart: the note, the
/// channel's transpose, the bend wheel, the chord, the table's transpose
/// column, and the 1/256-semitone offsets P and L work in.
double Driver::noteOfVoice(int ch) const
{
    const Voice& v = v_[size_t(ch)];
    double note = v.note + v.noteTsp + v.instTranspose + v.p.transpose + v.bend;
    if (v.chordN) note += v.chord[v.chordIdx % v.chordN];
    note += tableTransposeOf(v);
    const int32_t fine = v.fineOffset + slideResidual(v);
    return note + double(fine) / 256.0;
}

/// The table row's transpose column, when the table runs and the instrument
/// admits it (section 7); the noise channel takes it too (section 45).
int Driver::tableTransposeOf(const Voice& v) const
{
    if (!v.tableOn || !v.inst.transpose || !bank_) return 0;
    const Table* t = bank_->table(v.tableSlot);
    if (!t) return 0;
    const auto& st = t->steps[v.tableRow];
    return st.hasTranspose ? int(st.transpose) : 0;
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
        return std::clamp(int(std::lround(per)) + int(std::lround(v.drumOffset)), 0, 2047);
    }
    const bool wave = v.inst.type == InstrumentType::Wave;
    if (pitchSpeed(v) == PitchSpeed::Drum) {
        // Drum works in the period register: P moves it in a straight line and
        // it **wraps at 2048** rather than clamping, which is what a P kick
        // falling off the bottom really does (docs/LSDJ_PARITY.md section 5).
        const int base = periodForNote(noteOfVoice(ch), wave);
        if (base < 0) return -1;
        double per = double(base) + v.drumOffset - vibratoDrumUnits(v);
        per = std::fmod(per, 2048.0);
        if (per < 0.0) per += 2048.0;
        return std::clamp(int(std::floor(per + 0.5)), 0, 2047);
    }
    // period = periodOf(noteFine) (section 7): the note, the vibrato, P and a
    // slide are all semitones outside Drum, so the whole pitch is one number.
    const double note = noteOfVoice(ch) + double(vibratoFine(v)) / 256.0;
    const int per = periodForNote(note, wave);
    if (per < 0) return -1;
    return std::clamp(per, 0, 2047);
}

/// One pitch update: the vibrato phase, a slide and a P bend move on, and the
/// period goes out without a trigger. The 358 Hz clock calls this in Fast,
/// Step and Drum; the tracker tick calls it in Tick, where the instrument's
/// command rate slows P and V to one step every rate + 1 ticks.
///
/// LSDj writes the period **whenever something is moving it**, and once after
/// a note-on whether anything is moving or not -- so a plain note writes the
/// period again one update after its trigger and then stops, and a slide
/// writes at each of its steps and at the one that lands it on the note.
void Driver::pitchStep(int ch, bool onTick)
{
    Voice& v = v_[size_t(ch)];
    if (!v.active) return;
    bool advance = true;
    if (onTick) {
        const int every = int(v.inst.cmdRate) + 1;
        if (every > 1) { if (++v.pitchCount < every) advance = false; else v.pitchCount = 0; }
    }
    bool moving = false;
    const bool vib = advance && v.vibOn && v.ticks >= v.vibDelay;
    if (vib) moving = true;
    if (v.fineQueued) { v.fineOffset += v.fineQueued; v.fineQueued = 0; moving = true; }
    if (advance) {
        if (v.bendSpeed) {
            // In Tick mode one tick's step is **four** of the pitch clock's
            // (measured), not the 7.46 that a tick is worth in updates.
            const int mag = bendStep256(std::abs(int(v.bendSpeed))) * (onTick ? 4 : 1);
            const int step = v.bendSpeed < 0 ? -mag : mag;
            if (pitchSpeed(v) == PitchSpeed::Drum)
                v.drumOffset += double(step) / 256.0 * kDrumUnitsPerSemitone;
            else
                v.fineOffset = std::clamp<int32_t>(v.fineOffset + step, -1 << 20, 1 << 20);
            moving = true;
        }
    }
    // A slide runs for x + 1 updates, each a fixed step; the step is truncated,
    // so after them a little is usually left, and one more update snaps the
    // pitch onto the note exactly. When the step divides the distance -- L 00,
    // whose one step is the whole of it -- there is nothing left and no extra
    // update, which is why L 00 writes the period once (measured).
    if (v.sliding) {
        if (v.slideLeft > 0) {
            v.slideOff256 += v.slideStep256;
            if (--v.slideLeft == 0 && v.slideOff256 == 0) v.sliding = false;
        } else { v.sliding = false; v.slideOff256 = 0; }
        moving = true;
    }
    if (moving || v.pitchWrite) writePeriod(ch, false);
    v.pitchWrite = false;
    // The phase steps **after** the write: the first update of a note writes
    // the note itself, at phase zero, and the swing starts from the one after
    // (measured). One cycle is 64/(x + 1) updates on the pitch clock, and in
    // Tick mode the measured table of tick counts.
    if (vib) {
        const uint32_t step = onTick ? kVibTickStep9[v.vibSpeed & 15] : 9u * uint32_t((v.vibSpeed & 15) + 1);
        v.vibPhase9 = (v.vibPhase9 + step) % kVibNinths;
    }
}

void Driver::restartPitchClock(int ch)
{
    // The clock itself is global and free-running (kPitchCycles): a note-on
    // does not restart it, because a real driver's timer interrupt does not
    // know a note began. What a note-on *does* reset is the vibrato phase and
    // the "write the period once more" flag (docs/LSDJ_PARITY.md sections 1
    // and 3).
    Voice& v = v_[size_t(ch)];
    v.pitchWrite = true;
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
        else {
            // The note, the chain row's and the channel's transposes, and the
            // table row's column (section 45), through the map (section 9.4).
            const int n = std::clamp(int(v.note) + v.noteTsp + v.p.transpose + tableTransposeOf(v), 0, 127);
            s = uint8_t(noiseShiftMap_[size_t(n)]); d = uint8_t(noiseDivMap_[size_t(n)]); s = uint8_t(std::clamp(int(s) + int(v.noiseShift) - 5, 0, 13));
        }
        v.noiseShift = s; v.noiseDiv = d;
        emit(regAddr(3, 3), uint8_t((s << 4) | (v.lfsr7 ? 8 : 0) | (d & 7)), true);
        if (trigger) { emit(regAddr(3, 4), uint8_t(0x80 | (v.inst.length ? 0x40 : 0)), true); markTrigger(ch); }
        v.lastPeriod = int16_t((s << 4) | d);
        return;
    }
    int per = computePeriod(ch);
    if (per < 0) per = 0;
    v.basePeriod = int16_t(per);
    // Where the channel is now, for an L that fires later: the pitch without
    // the vibrato, in 1/256 semitones, the slide it is in the middle of
    // included.
    {
        v.pitchNowFine = int32_t(std::lround(noteOfVoice(ch) * 256.0));
        v.pitchValid = true;
    }
    const uint16_t f = uint16_t(per);
    // **Both halves, every time.** LSDj writes NRx4 with every NRx3 whether or
    // not the high bits moved (docs/LSDJ_PARITY.md section 11); there is no
    // trigger bit in it, so it changes nothing but the log -- and the log is
    // what a parity harness can line up.
    emit(regAddr(ch, 3), uint8_t(f & 0xFF), true);
    const uint8_t hi = uint8_t((f >> 8) | (trigger ? 0x80 : 0) | (v.inst.length ? 0x40 : 0));
    emit(regAddr(ch, 4), hi, true);
    if (trigger && v.inst.type == InstrumentType::Pulse) markTrigger(ch);
    if (ch == 2) updateWaveTimer(ch, f, trigger);
    v.lastPeriod = int16_t(f);
}

void Driver::emitNrx2(int ch, uint8_t value)
{
    // One NRx2 write, with the model of the chip's envelope moved on exactly
    // as the chip moves it: on a running channel the write is a zombie write
    // and changes the volume (section 26), and the top five bits decide the
    // DAC. What the driver *wants* (v.envVol and the rest) is not touched.
    Voice& v = v_[size_t(ch)];
    const bool newUp = (value & 0x08) != 0;
    if (v.hwOn && v.dacOn) v.volume = zombieAfter(v.volume, v.hwPeriod, v.hwUp, v.hwRun, value);
    v.hwUp = newUp;
    v.hwPeriod = uint8_t(value & 7);
    v.hwInitial = uint8_t(value >> 4);
    emit(regAddr(ch, 2), value, true);
    v.dacOn = (value & 0xF8) != 0;
    if (!v.dacOn) v.hwOn = false;                    // the DAC off disables the channel
}

void Driver::markTrigger(int ch)
{
    // A trigger reloads the volume from NRx2's initial volume and starts the
    // envelope again (Apu::triggerSquare).
    Voice& v = v_[size_t(ch)];
    v.volume = v.hwInitial;
    v.hwRun = true;
    v.hwOn = v.dacOn;
}

void Driver::writeEnvelope(int ch, bool trigger)
{
    Voice& v = v_[size_t(ch)];
    if (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) {
        emit(regAddr(2, 2), nr32Code(v.waveLevel));
        return;
    }
    // The whole register, as a note-on writes it -- always with the low nibble
    // at 8, a hold (docs/LSDJ_PARITY.md section 2). The instrument's own rate
    // and direction are the *driver's* envelope from here and are stepped by
    // stepSoftEnvelope(); the chip's never runs. Every level change that is not
    // a note-on, R or an E moving the envelope goes through setLevel() instead,
    // which never triggers (section 26).
    emitNrx2(ch, nrx2Hold(v.envVol));
    v.envCount = 0;
    if (trigger) {
        const uint16_t f = uint16_t(std::max<int16_t>(0, v.lastPeriod));
        if (v.inst.type == InstrumentType::Noise) emit(regAddr(3, 4), uint8_t(0x80 | (v.inst.length ? 0x40 : 0)), true);
        else emit(regAddr(ch, 4), uint8_t((f >> 8) | 0x80 | (v.inst.length ? 0x40 : 0)), true);
        markTrigger(ch);
    }
}

void Driver::setLevel(int ch)
{
    Voice& v = v_[size_t(ch)];
    if (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) {
        // The wave channel has no envelope generator: its level is the two
        // bits of NR32, one write, no trigger and no zombie mode (section 26).
        if (v.active && !inNoteOn_) emit(regAddr(2, 2), nr32Code(v.waveLevel));
        return;
    }
    // A note-on in progress, or a channel the chip is not running: the level
    // is running state and the note's own writes carry it. A voice that has
    // been released is not `active` and still takes a level change -- that is
    // what its fade is (section 26).
    if (inNoteOn_ || !v.dacOn || !v.hwOn) return;
    const int target = std::clamp<int>(v.envVol, 0, 15);
    // The steps LSDj issues, one at a time, at the tick the change was asked
    // for: `09 11 18` to go down one and `08` to go up one, repeated to the
    // target, whichever way round is fewer writes. Sixteen levels, so at worst
    // fifteen steps; the direction that wraps is never taken, because the ROM
    // never takes it.
    uint64_t at = burst_;
    for (int guard = 0; guard < 16 && v.volume != target; ++guard) {
        if (v.volume > target) {
            for (int i = 0; i < 3; ++i) { burst_ = at + uint64_t(i) * kZombieInner; emitNrx2(ch, kZombieDown[size_t(i)]); }
            at += kZombieDownStep;
        } else {
            burst_ = at; emitNrx2(ch, kZombieUp);
            at += kZombieUpStep;
        }
    }
    burst_ = at;
}

/// The instrument's own envelope, run in software off the pitch clock. LSDj
/// never lets the chip's envelope run (the register always holds), so a decay
/// or an attack is a level change every kEnvStepPeriods[rate] pitch-clock
/// periods, made of the same zombie writes as any other (LSDJ_PARITY 7).
void Driver::stepSoftEnvelope(int ch)
{
    Voice& v = v_[size_t(ch)];
    if (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) return;
    if (v.shapedOn) return;                     // a shaped envelope owns the level, per tick
    const int rate = v.envRate & 7;
    if (rate == 0 || !v.dacOn || !v.hwOn) return;
    if (++v.envCount < uint32_t(kEnvStepPeriods[rate])) return;
    v.envCount = 0;
    const int next = int(v.envVol) + (v.envDir == EnvDir::Up ? 1 : -1);
    if (next < 0 || next > 15) {
        v.envRate = 0;                                        // the rail: it stops there
        if (v.pulseReleasing) { v.pulseReleasing = false; stopVoice(ch, true); }
        return;
    }
    v.envVol = uint8_t(next);
    setLevel(ch);
}

uint8_t Driver::shapedLevel(const Voice& v) const
{
    const Envelope& e = v.inst.env;
    auto clamp15 = [](int x) { return uint8_t(std::clamp(x, 0, 15)); };
    if (v.shapedRelease) return clamp15(envSegmentLevel(v.shapedFrom, 0, e.releaseTicks, int(v.shapedTick), e.releaseCurve));
    const int a = e.attackTicks, d = e.decayTicks, t = int(v.shapedTick);
    if (t < a) return clamp15(envSegmentLevel(0, e.peak, a, t, e.attackCurve));
    if (t < a + d) return clamp15(envSegmentLevel(e.peak, e.sustain, d, t - a, e.decayCurve));
    return clamp15(e.sustain);
}

void Driver::stepShaped(int ch)
{
    // One level per tick from the segments, written only when it changes and
    // always through section 26 -- no trigger, so a playback ROM can replay
    // the same list of levels (section 27).
    Voice& v = v_[size_t(ch)];
    if (!v.shapedOn || v.shapedTaken) return;
    ++v.shapedTick;
    if (v.shapedRelease && int(v.shapedTick) >= int(v.inst.env.releaseTicks)) {
        v.shapedOn = false; v.shapedRelease = false;
        stopVoice(ch, true);
        return;
    }
    const uint8_t level = shapedLevel(v);
    if (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) {
        const uint8_t code = uint8_t(level / 4);
        if (code != v.waveLevel) { v.waveLevel = code; setLevel(ch); }
    } else if (level != v.envVol) {
        v.envVol = level;
        setLevel(ch);
    }
}

void Driver::writeNr51(bool force)
{
    uint8_t bits = 0;
    for (int ch = 0; ch < 4; ++ch) if (gateMask_ & (1u << ch)) bits |= panBitsFor(v_[size_t(ch)].pan, ch);
    emit(0xFF25, bits, force);
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

/// R's volume step per retrigger: `x` is a **signed nibble** -- 0 none, 1-7 up
/// by that much, 9-15 down by sixteen minus it (measured: `R A` steps the
/// level down by six, not by two). 8 is the resync and changes no level.
int16_t retrigVolStep(int x)
{
    const int n = std::clamp(x, 0, 15);
    return int16_t(n == 8 ? 0 : n < 8 ? n : n - 16);
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
            else beginTableRun(ch, uint8_t(std::clamp<int>(c.a, 1, kTableSlots)));
            break;
        case Cmd::C:                                  // 0, x, y one step per chordRate + 1 ticks
            if (noise) break;
            v.chord[0] = 0; v.chord[1] = uint8_t(std::clamp<int>(c.a, 0, 60)); v.chord[2] = uint8_t(std::clamp<int>(c.b, 0, 60));
            v.chordN = c.b ? 3 : (c.a ? 2 : 0);
            v.chordIdx = 0; v.chordCount = 0;
            break;
        case Cmd::D: if (fromTable) v.delay = int16_t(std::clamp<int>(c.a, 0, 255)); break;   // a slot's D is read at the note-on
        case Cmd::E: {
            // Envelope: volume in x; y is the NRx2 encoding, 0 and 8 holding,
            // 1-7 decaying at that rate and 9-15 rising at y - 8.
            // E takes a shaped envelope over, as a table's volume column does
            // (section 27).
            v.shapedTaken = true;
            if (wave) { v.waveLevel = uint8_t(std::clamp<int>(c.a, 0, 3)); setLevel(ch); }
            else {
                // **E never triggers.** It walks the level to x by zombie steps
                // at its own tick and sets the direction and rate of what
                // happens next -- the envelope the driver runs in software
                // (docs/LSDJ_PARITY.md section 6). Measured: `E 8 0` on a
                // channel at 15 is seven down-triples and nothing else.
                v.envVol = uint8_t(std::clamp<int>(c.a, 0, 15));
                v.envRate = uint8_t(c.b & 7);
                v.envDir = (c.b & 8) ? EnvDir::Up : EnvDir::Down;
                v.envCount = 0;
                setLevel(ch);
            }
            break;
        }
        case Cmd::F:
            if (v.inst.type == InstrumentType::Wave) { const Wave* w = bank_ ? bank_->wave(v.waveSlot) : nullptr; if (w && !w->frames.empty()) { v.frameIdx = uint8_t(std::clamp<int>(c.a - 1, 0, int(w->frames.size()) - 1)); v.frameCount = 0; if (live) loadFrame(ch, w->frames[v.frameIdx], model_ == Console::DMG); } }
            // On the second pulse F is the instrument's PU2 transpose for the
            // note in progress and the notes after it, two's complement like
            // P's argument (section 49); inert on PU1 and NOI.
            else if (pulse && ch == 1) { v.instTranspose = int8_t(uint8_t(std::clamp<int>(c.a, 0, 255))); if (live) writePeriod(ch, false); }
            break;
        case Cmd::G:
            // Inside a table G sets that run's row lengths: the driver keeps
            // the slot for the Player, which hands back the groove's ticks
            // through setTableGroove(). On the timeline the Player owns it.
            if (fromTable) { v.tableGroove = uint8_t(std::clamp<int>(c.a, 0, 16)); if (!v.tableGroove) tableGroove_[size_t(ch)].fill(0); }
            break;
        case Cmd::T: break;                           // timeline: the Player and the Clock own this
        case Cmd::H:
            // In a table H is LSDj's **`times, row`** (section 34): hop to row
            // `y`, `x` times; `x` = 0 hops for ever. The count is per table
            // run, so a new note starts it again.
            if (fromTable) {
                const int times = std::clamp<int>(c.a, 0, 15), row = std::clamp<int>(c.b, 0, 15);
                if (times == 0) { v.tableStep = uint8_t(row); v.hopLeft = 0; }
                else if (v.hopLeft == 0 && v.hopFrom != v.tableRow) { v.hopFrom = v.tableRow; v.hopLeft = uint8_t(times); v.tableStep = uint8_t(row); }
                else if (v.hopLeft > 0) { if (--v.hopLeft > 0) v.tableStep = uint8_t(row); else v.hopFrom = 0xFF; }
            }
            break;
        case Cmd::K: v.kill = int16_t(std::clamp<int>(c.a, 0, 255)); break;
        case Cmd::L: {
            // A slide takes **x + 1 pitch updates** and is **linear in
            // semitones**: the note walks from where the channel is to the
            // note of this cell by a fixed step, `(target - source) / (x + 1)`
            // in 1/256 semitones truncated toward zero, and lands exactly on
            // the note one update after the last step (docs/LSDJ_PARITY.md 4).
            // x = 0 is instant. It is the pitch update in Fast/Step/Drum and
            // the tracker tick in Tick.
            if (noise) break;
            const int32_t fromFine = v.pitchNowFine;
            const bool have = v.pitchValid;
            v.sliding = false; v.slideLeft = 0; v.slideOff256 = 0; v.slideStep256 = 0;
            const int dur = std::clamp<int>(c.a, 0, 255) + 1;
            if (!have) { if (live) writePeriod(ch, false); break; }
            const int32_t target = int32_t(std::lround(noteOfVoice(ch) * 256.0));
            const int32_t from = fromFine - target;
            if (from != 0) {
                v.slideOff256 = from;
                v.slideStep256 = int32_t(-from / dur);      // C++ truncates toward zero
                v.slideLeft = dur; v.slideTotal = dur; v.sliding = true;
            }
            if (live) writePeriod(ch, false);
            break;
        }
        case Cmd::M: {
            // Both arguments are nibbles (section 34).
            const uint8_t cur = known_[0x14] ? shadow_[0x14] : uint8_t(((global_.masterL & 7) << 4) | (global_.masterR & 7));
            writeNr50(uint8_t(masterFromArg(c.a & 15, (cur >> 4) & 7)), uint8_t(masterFromArg(c.b & 15, cur & 7)));
            break;
        }
        case Cmd::O: v.pan = Pan(std::clamp<int>(c.a, 0, 3)); writeNr51(); break;
        case Cmd::P: {
            // The argument is **two's complement** (section 34): the byte 0-255
            // read as a signed -128..127. The step per update comes from the
            // measured table, `bendStep256`, in 1/256 of a semitone; Fast and
            // Tick bend the note, Step applies one offset of x/32 of a semitone
            // and no bend, and Drum bends the **period register** and wraps at
            // 2048 (docs/LSDJ_PARITY.md section 5). `P 0` stops a bend and
            // keeps what it reached; a plain note-on puts the offset back.
            if (noise) break;
            const int speed = int(int8_t(uint8_t(std::clamp<int>(c.a, 0, 255))));
            if (pitchSpeed(v) == PitchSpeed::Step) {
                // The offset reaches the pitch at the next update, not in the
                // note-on's own writes: LSDj's note-on writes the pitch the
                // channel was at and the commands move it from there
                // (measured -- P 02 triggers on the note and the first update
                // is a semitone-thirty-second above it).
                v.fineQueued += int32_t(speed) * 8; v.bendSpeed = 0;
            }
            else v.bendSpeed = int16_t(speed);
            if (live) writePeriod(ch, false);
            break;
        }
        case Cmd::R:
            // `y` is the interval: y x (rate + 1) + 1 ticks, so y = 0 is every
            // tick. `x` = 8 is LSDj's resync -- the retrigger runs on the pitch
            // clock instead. `x` otherwise is a signed nibble of volume change.
            v.retrigEvery = uint8_t(std::clamp<int>(c.b, 0, 15));
            v.retrigOn = true;
            v.retrigFast = (c.a & 15) == 8;
            v.retrigStep = retrigVolStep(c.a);
            v.retrigCount = 0;
            break;
        case Cmd::S: {
            // PU1's sweep. The direction is the instrument's unless x asks for
            // down; on WAV and NOI there is no sweep unit, so S is inert.
            if (ch == 0 && pulse) {
                // `x` is the sweep rate and `y` is **NR10's low nibble**
                // (section 34): 0-7 sweep up at that shift, 8-15 sweep down.
                v.sweepRate = uint8_t(c.a & 7);
                v.sweepDown = (c.b & 8) != 0;
                v.sweepShift = uint8_t(c.b & 7);
                if (live) {
                    emit(0xFF10, uint8_t((v.sweepRate << 4) | (v.sweepDown ? 8 : 0) | v.sweepShift), true);
                    writePeriod(ch, true);             // the sweep unit reloads on the trigger
                }
            }
            break;
        }
        case Cmd::V:
            // One cycle is **64 / (x + 1) pitch updates** in Fast, Step and
            // Drum -- so x = 0 is the slowest, not "off" -- and the measured
            // table of tick counts in Tick. `y` is the depth, a symmetric
            // swing of that many semitones either side of the note.
            if (noise) break;
            v.vibSpeed = uint8_t(std::clamp<int>(c.a, 0, 15));
            v.vibDepth = uint8_t(std::clamp<int>(c.b, 0, 15));
            v.vibDelay = 0; v.vibOn = true;
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
        case Cmd::E: {
            // The instrument's envelope back. As with an E that names one: a
            // level change when the direction and the rate are already those,
            // a new envelope -- and a trigger -- when they are not (26).
            v.shapedTaken = true;
            if (wave) { v.waveLevel = i.waveLevel; applyLevelParam(ch); setLevel(ch); }
            else {
                v.envVol = i.envVol; v.envRate = i.envRate; v.envDir = i.envDir; v.envCount = 0;
                applyLevelParam(ch);
                setLevel(ch);
            }
            break;
        }
        case Cmd::F:
            if (i.type == InstrumentType::Wave) { const Wave* w = bank_ ? bank_->wave(v.waveSlot) : nullptr; v.frameIdx = 0; v.frameCount = 0; if (live && w && !w->frames.empty()) loadFrame(ch, w->frames[0], model_ == Console::DMG); }
            else if (pulse && ch == 1) { v.instTranspose = i.pu2Transpose; if (live) writePeriod(ch, false); }   // the instrument's own PU2 transpose back (section 49)
            break;
        case Cmd::M: writeNr50(global_.masterL, global_.masterR); break;
        case Cmd::O: v.pan = v.p.pan != 255 ? Pan(v.p.pan & 3) : i.pan; writeNr51(); break;
        case Cmd::P: v.fineOffset = 0; v.fineQueued = 0; v.drumOffset = 0.0; v.bendSpeed = 0; if (live) writePeriod(ch, false); break;
        case Cmd::S:
            if (ch == 0 && pulse) {
                v.sweepRate = i.sweepRate; v.sweepDown = i.sweepDown; v.sweepShift = i.sweepShift;
                emit(0xFF10, uint8_t((v.sweepRate << 4) | (v.sweepDown ? 8 : 0) | v.sweepShift), true);
            }
            break;
        case Cmd::V:
            v.vibShape = i.vib.shape; v.vibDir = i.vib.dir;
            v.vibSpeed = uint8_t(std::clamp<int>(i.vib.speed, 0, 15)); v.vibDepth = i.vib.depth; v.vibDelay = i.vib.delay;
            v.vibOn = i.vib.depth != 0;
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
    // Z's own arguments are nibbles (section 34).
    c.a = int16_t(c.a + randomArg(ch, z.a & 15));
    c.b = int16_t(c.b + randomArg(ch, z.b & 15));
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
    const int16_t p = int16_t(effective(ch).instrument);
    if (v.instParam == p) return;
    if (v.instParam >= 0) { v.ksInstrument = 0; v.ksFromCell = false; }
    v.instParam = p;
}

/// Take the parameters' slots as the ones in force, without firing them: what
/// a note-on does, because fireSlots() is about to apply them in order.
void Driver::syncSlots(int ch)
{
    Voice& v = v_[size_t(ch)];
    const ChannelParams p = effective(ch);
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
    const ChannelParams p = effective(ch);
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

void Driver::beginTableRun(int ch, uint8_t slot)
{
    Voice& v = v_[size_t(ch)];
    v.tableSlot = slot; v.tableStep = 0; v.hopLeft = 0; v.hopFrom = 0xFF; v.tableRow = 0; v.tableWait = 0; v.tableGroove = 0;
    v.tableOn = slot != 0 && bank_ && bank_->table(slot);
    if (v.tableOn) ++v.tableRun;
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
        // A level change, never a retrigger (section 26); inside a note-on it
        // only changes the running state and the note's own writes carry it.
        // It takes a shaped envelope over: the segments left stop until the
        // next plain note-on (section 27).
        v.shapedTaken = true;
        if (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) v.waveLevel = uint8_t(std::clamp<int>(s.vol / 4, 0, 3));
        else v.envVol = uint8_t(std::clamp<int>(s.vol, 0, 15));
        setLevel(ch);
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
    // A Hybrid cell's commands waited for a note-on through this tick; with
    // none they land on the sounding voice now, where a slot change would
    // have fired (section 20).
    if (v.heldCmdOn) {
        if (v.heldDelay > 0) --v.heldDelay;
        else {
            const Command c1 = v.heldCmd[0], c2 = v.heldCmd[1];
            v.heldCmdOn = false; v.heldCmd[0] = {}; v.heldCmd[1] = {};
            applyCellCommands(ch, c1, c2);
            // L is the one per-note letter with nothing under it yet: no note
            // arrived, so it is the next one's portamento (section 20).
            for (const Command* c : { &c1, &c2 })
                if (c->cmd == Cmd::L && !isRevert(*c)) v.hybridSlide = *c;
        }
    }
    if (params_[size_t(ch)].liveFollow && v.haveInst) {
        // Live follow: instrument, table, level, pan and transpose apply to the
        // sounding note instead of waiting for the next one (section 3).
        const ChannelParams before = v.p;
        v.p = effective(ch);
        if (v.active) {
            if (before.instrument != v.p.instrument) { reloadInstrument(ch); return; }
            if (before.table != v.p.table || v.tableOverride != v.tableSlot) {
                const uint8_t tbl = v.tableOverride ? v.tableOverride : v.inst.table;
                if (tbl != v.tableSlot) beginTableRun(ch, tbl);
            }
            if (before.level != v.p.level) {
                // The Level lane is a level change too, and takes a shaped
                // envelope over (sections 26 and 27).
                v.shapedTaken = true;
                applyLevelParam(ch);
                setLevel(ch);
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
            v.velRule = v.pendingVelRule;
            startVoice(ch, v.pendingNote, v.pendingVel, v.pendingPlain);
        }
    }
    // A released WAV or KIT walks its level down whether it is active or not.
    if (v.releasing) stepRelease(ch);
    if (!v.active) return;
    ++v.ticks;
    // What the *tick* does to the pitch -- a chord step, a table's transpose
    // column -- has to reach the registers; what the pitch clock does is its
    // own business. Comparing the note before and after the tick's work
    // separates the two, and the next pitch update carries the change.
    // ... so the comparison leaves the pitch effects' own offsets out of it.
    const auto tickNote = [&] { return noteOfVoice(ch) - double(v.fineOffset + slideResidual(v)) / 256.0; };
    const double noteBeforeTick = tickNote();
    // kill countdown
    if (v.kill >= 0) { if (v.kill == 0) { killLevel(ch); stopVoice(ch, false); v.kill = -1; return; } --v.kill; }
    // The table: a row per tick, or per the row length a G inside it asked
    // for. A Step-mode table advances at notes instead (section 7).
    if (v.tableJustStarted) v.tableJustStarted = false;      // row 0 fired with the note-on (section 31)
    else if (v.inst.tableMode == TableMode::Tick) {
        if (v.tableWait > 1) --v.tableWait;
        else stepTable(ch);
    }
    if (!v.active) return;
    // The shaped envelope's level for this tick, after the table, which may
    // just have taken it over (sections 26 and 27).
    stepShaped(ch);
    if (!v.active) return;
    // chord: one step every chordRate + 1 ticks -- the instrument's own rate
    // for C, apart from the command rate R and the Tick-speed P and V run on
    // (section 37). The note's own tick plays the root: the chord steps from
    // the tick after it (measured -- C 3 7 wrote 1798, then 1837, then 1881,
    // a tick apart).
    if (v.chordN && v.ticks > 1 && ++v.chordCount >= uint8_t(v.inst.chordRate + 1)) { v.chordCount = 0; v.chordIdx = uint8_t((v.chordIdx + 1) % v.chordN); }
    // duty sequence
    if (v.inst.type == InstrumentType::Pulse && v.inst.dutySeqLen) {
        v.dutyIdx = uint8_t((v.dutyIdx + 1) % v.inst.dutySeqLen);
        const uint8_t d = uint8_t(v.inst.dutySeq[v.dutyIdx] & 3);
        if (d != v.duty) { v.duty = d; emit(regAddr(ch, 1), uint8_t((d << 6) | lengthCode6(v.inst.length))); }
    }
    // noise sweep
    if (v.inst.type == InstrumentType::Noise && v.noiseSweep) { v.noiseShift = uint8_t(std::clamp<int>(int(v.noiseShift) + v.noiseSweep, 0, 13)); v.inst.noiseManual = true; }
    // R: the interval is **y x (rate + 1) + 1 ticks**, so y = 0 is every tick
    // and not "once" (docs/LSDJ_PARITY.md section 8). x = 8 resyncs instead:
    // the retrigger runs on the pitch clock, and pitchBefore() does it.
    bool retrig = false;
    if (v.retrigOn && !v.retrigFast) {
        const uint16_t every = uint16_t(uint16_t(v.retrigEvery) * uint16_t(v.inst.cmdRate + 1) + 1u);
        if (++v.retrigCount >= every) { v.retrigCount = 0; retrig = true; }
    }
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
    // phase, a slide and a P bend move here rather than on the pitch clock.
    if (!v.pitchClockOn && (v.inst.type == InstrumentType::Pulse || v.inst.type == InstrumentType::Wave)
        && pitchSpeed(v) == PitchSpeed::Tick) pitchStep(ch, true);
    // pitch for this tick. The pitch clock writes the period whenever a pitch
    // effect is moving it; what the *tick* moves -- a chord step, a table's
    // transpose column, the channel's own transpose -- goes out here, and only
    // when it really changed something.
    if (retrig) retrigger(ch, true);
    else if (v.inst.type == InstrumentType::Noise) { if (v.noiseSweep || tickNote() != noteBeforeTick) writePeriod(ch, false); }   // a table's transpose reaches NR43 (section 45)
    else if (tickNote() != noteBeforeTick) writePeriod(ch, false);
}

/// One retrigger. LSDj writes the whole note-on sequence again -- NR10, NR11,
/// NR12, NR13, NR14 with the trigger -- not just a trigger, so the register
/// log of a retrigger and of a note-on are the same five writes
/// (docs/LSDJ_PARITY.md section 8).
void Driver::retrigger(int ch, bool full)
{
    Voice& v = v_[size_t(ch)];
    if (!v.active) return;
    const bool pulse = v.inst.type == InstrumentType::Pulse;
    const bool noise = v.inst.type == InstrumentType::Noise;
    if (!full) {
        // R's resync (x = 8) runs on the pitch clock and writes two registers,
        // the level and the trigger, not the whole note-on (measured).
        if (pulse || noise) writeEnvelope(ch, true);
        else { const uint16_t f = uint16_t(std::max<int16_t>(0, v.lastPeriod));
               emit(regAddr(ch, 4), uint8_t((f >> 8) | 0x80 | (v.inst.length ? 0x40 : 0)), true); markTrigger(ch); }
        return;
    }
    // `x` is a signed nibble of volume change: 1-7 up by that much, 9-15 down
    // by sixteen minus it (measured: R A steps the level down by six).
    if (v.retrigStep && !noise) v.envVol = uint8_t(std::clamp<int>(int(v.envVol) + v.retrigStep, 0, 15));
    if (pulse) {
        if (ch == 0) emit(0xFF10, uint8_t((v.sweepRate << 4) | (v.sweepDown ? 8 : 0) | v.sweepShift), true);
        emit(regAddr(ch, 1), uint8_t((v.duty << 6) | lengthCode6(v.inst.length)), true);
    } else if (noise) {
        emit(regAddr(3, 1), lengthCode6(v.inst.length), true);
    }
    if (pulse || noise) writeEnvelope(ch, false);
    writePeriod(ch, true);
    if (noise) { }                       // writePeriod carries NR43/NR44 for noise
    else if (pulse || v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) { }
    v.pitchWrite = true;
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
        // Trkr only: a Hybrid channel's notes come from MIDI and its cells
        // never carry one, so both pass its gate (section 20).
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
            if (e.a == 1) { v.vibDepth = uint8_t(e.b / 8); v.vibOn = e.b != 0; }   // the mod wheel is vibrato depth
            // CC7 is a level change: zombie-mode writes, no trigger (26).
            else if (e.a == 7 && v.active) { v.shapedTaken = true; if (v.inst.type == InstrumentType::Wave || v.inst.type == InstrumentType::Kit) v.waveLevel = uint8_t(e.b / 32); else v.envVol = uint8_t(e.b / 8); setLevel(ch); }
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
    if (e.hybrid) { applyHybridCell(ch, e); return; }
    if (e.table) { v.tableOverride = e.table; if (v.active) beginTableRun(ch, e.table); }
    else if (e.inst) v.tableOverride = v.tableParam;         // an instrument column ends the TBL span (section 46)
    if (e.inst && e.inst != v.ksInstrument) {
        v.ksInstrument = e.inst; v.ksFromCell = true;
        // The instrument reloads and fires the slots; the cell's commands
        // follow it, as they would at a note (section 3's order).
        if (v.haveInst) { reloadInstrument(ch); applyCellCommands(ch, e.cmd1, e.cmd2); return; }
    }
    applyCellCommands(ch, e.cmd1, e.cmd2);
}

/// A Hybrid channel's cell (section 20). The instrument and table columns are
/// a selection for the next MIDI note-on -- what is sounding keeps its own,
/// exactly as a cell's instrument column is exact under the velocity bank --
/// and the two commands are held for the rest of this tick: a note-on inside
/// it takes them, otherwise the tick's end lands them on the sounding voice.
/// A D among them holds them that many ticks longer, as it delays a note.
void Driver::applyHybridCell(int ch, const NoteEvent& e)
{
    Voice& v = v_[size_t(ch)];
    if (e.inst) { v.ksInstrument = e.inst; v.ksFromCell = true; }
    if (e.table) { v.tableOverride = e.table; }
    else if (e.inst) v.tableOverride = v.tableParam;         // section 46
    v.heldCmd[0] = e.cmd1; v.heldCmd[1] = e.cmd2;
    v.heldCmdOn = e.cmd1.cmd != Cmd::None || e.cmd2.cmd != Cmd::None;
    v.heldDelay = 0;
    // The cell's own D, and only that: on a Hybrid channel the slots are inert.
    for (const Command* c : { &v.heldCmd[0], &v.heldCmd[1] })
        if (c->cmd == Cmd::D && !isRevert(*c)) { v.heldDelay = int16_t(std::clamp<int>(c->a, 0, 255)); break; }
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

    // A driver writes the mixer once, at its own initialisation, before it
    // plays anything: NR50 from the master volume and NR51 from the pans.
    // LSDj does it while its interface is still up, so the note-on that lines
    // the two streams up already has them behind it.
    if (!mixerInit_) {
        mixerInit_ = true;
        cycle_ = cycleAt(frameAbs); burst_ = 0;
        masterL_ = global_.masterL; masterR_ = global_.masterR;
        writeNr50(masterL_, masterR_);
        writeNr51(true);
    }

    // Events land where the host put them (sample accurate); the tick drives
    // what a driver runs from its interrupt: tables, frames, the command
    // slots. The driver's one pitch clock runs at 358 Hz between them, in cycle
    // order with both, so vibrato and slides are where they really are.
    // With notes-on-tick the note-ons and note-offs wait for the next tick as
    // a tracker's do -- bends and controllers never do, and tracker cells
    // already sit on ticks.
    size_t ei = 0;
    auto moveTo = [&](uint64_t c) { if (c != cycle_) { cycle_ = c; burst_ = 0; } };
    auto offOf = [&](uint32_t o) { return numSamples ? std::min<uint32_t>(o, numSamples - 1) : 0u; };
    auto pitchBefore = [&](uint64_t limit) {
        // One clock for the driver, free-running (docs/LSDJ_PARITY.md 1). A
        // jump in the timeline (a locate, a long gap) must not walk it forward
        // one update at a time.
        if (!pitchClockValid_) { pitchClockAt_ = cycle_ + kPitchCycles; pitchClockValid_ = true; }
        if (pitchClockAt_ + kPitchCycles * 4096 < limit)
            pitchClockAt_ = limit - (limit - pitchClockAt_) % kPitchCycles;
        while (pitchClockAt_ < limit) {
            const uint64_t at = pitchClockAt_;
            // A tick's register writes go out as one burst, an instruction
            // pair apart. A 358 Hz update landing inside that burst cannot
            // interleave with it on real hardware, and must not overtake
            // writes that were computed before it, so it follows the burst.
            if (at > cycle_ + burst_) moveTo(at);
            for (int ch = 0; ch < 4; ++ch) {
                Voice& v = v_[size_t(ch)];
                if (v.active && v.pitchClockOn) pitchStep(ch, false);
                // The instrument's own envelope and R's resync run on the same
                // clock, whatever the pitch speed is (sections 7 and 8).
                if (v.active || v.releasing || v.pulseReleasing) stepSoftEnvelope(ch);
                if (v.active && v.retrigFast) {
                    if (v.inst.type == InstrumentType::Pulse || v.inst.type == InstrumentType::Noise
                        || v.inst.type == InstrumentType::Wave) retrigger(ch, false);
                }
            }
            pitchClockAt_ = at + kPitchCycles;
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
    w.tableRow = v.tableOn ? int8_t(v.tableRow) : int8_t(-1);
    w.tableRun = v.tableRun;
    // The running state the strip prints under the two slots (section 3).
    w.envVol = v.envVol; w.envRate = v.envRate; w.envDir = v.envDir == EnvDir::Up ? 1 : 0;
    w.vibSpeed = v.vibSpeed; w.vibDepth = v.vibDepth;
    // What the pitch effects have added, in register units: P and a slide
    // give them directly, and a Drum-mode bend through the note it moves.
    int off = int(std::lround(v.drumOffset));
    const int32_t fine = v.fineOffset + slideResidual(v);
    if (fine && v.inst.type != InstrumentType::Noise && v.inst.type != InstrumentType::Kit) {
        const bool wave = v.inst.type == InstrumentType::Wave;
        const double n = noteOfVoice(ch);
        const int a = periodForNote(n, wave), b = periodForNote(n - double(fine) / 256.0, wave);
        if (a >= 0 && b >= 0) off += a - b;
    }
    w.pitchOffset = int16_t(std::clamp(off, -2047, 2047)); w.pan = uint8_t(v.pan);
    for (int r = 0; r < 5; ++r) w.regs[r] = known_[size_t(ch * 5 + r)] ? shadow_[size_t(ch * 5 + r)] : 0;
}

} // namespace chipboy::driver
