// ChipBoy -- DMG APU core. See Apu.h and docs/HARDWARE_REFERENCE.md.
//
// Section references below are to HARDWARE_REFERENCE.md. Where behaviour is
// implemented from documentation rather than from a datasheet, the reference
// that resolved it is cited (spec rule L2).

#include "core/Apu/Apu.h"

#include <algorithm>
#include <cstring>

namespace chipboy {

namespace {

// Section 4: duty patterns, one bit per step, position 0 in bit 7.
constexpr uint8_t kDuty[4] = {0x01, 0x81, 0x87, 0x7E};

// Section 6: noise divisor table; code 0 is 8, not 0.
constexpr uint8_t kNoiseDiv[8] = {8, 16, 32, 48, 64, 80, 96, 112};

// Section 2: bits that read back as 1 regardless of contents, FF10..FF2F.
constexpr uint8_t kReadMask[0x20] = {
    0x80, 0x3F, 0x00, 0xFF, 0xBF,   // NR10 NR11 NR12 NR13 NR14
    0xFF, 0x3F, 0x00, 0xFF, 0xBF,   // ---- NR21 NR22 NR23 NR24
    0x7F, 0xFF, 0x9F, 0xFF, 0xBF,   // NR30 NR31 NR32 NR33 NR34
    0xFF, 0xFF, 0x00, 0x00, 0xBF,   // ---- NR41 NR42 NR43 NR44
    0x00, 0x00, 0x70,               // NR50 NR51 NR52
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Section 3: the frame sequencer runs at 512 Hz -- a falling edge of divider
// bit 12, i.e. every 8192 cycles.
constexpr uint32_t kFramePeriod = 8192;

// Section 5 (DMG wave RAM access): the channel fetches a byte during one
// 2 MHz APU cycle; a CPU access in that same cycle hits that byte and any
// other access sees $FF. Fetches land on even CPU cycles, so "same APU cycle"
// is the fetch cycle or the one after it. Verified against blargg's 09 and 12
// (their DMG CRCs admit no wider window).
constexpr uint64_t kWaveAccessWindow = 1;

// Section 10.2 (DMG wave RAM corruption): a trigger corrupts wave RAM when
// the next fetch is due on the very next APU cycle, i.e. within 2 CPU cycles.
// Verified against blargg's 10.
constexpr int32_t kWaveCorruptWindow = 2;

// Section 5 (wave trigger delay): cycles added to the first period after a
// trigger, during which the stale sample buffer is still what plays.
// Verified against blargg's 09, 10 and 12 together with the two above.
constexpr int32_t kWaveTriggerDelay = 6;

} // namespace

// ---------------------------------------------------------------------------

Apu::Apu() { reset(); }

void Apu::reset()
{
    sq_[0] = Square{};
    sq_[1] = Square{};
    wave_  = Wave{};
    noise_ = Noise{};
    nr50_ = nr51_ = 0;
    powered_   = false;
    frameStep_ = 0;
    skipNextTick_ = false;
    divider_   = 0;
    cycle_     = 0;
    events_.clear();
    for (int c = 0; c < 4; ++c) { lastLevel_[c] = 0; lastDac_[c] = false; }

    // Post-boot-ROM state, written through the normal path so every derived
    // field is consistent. The boot chime leaves CH1 with a 50% duty and an
    // envelope of $F3; nothing is triggered.
    powerOn();
    write(0xFF25, 0xF3);
    write(0xFF24, 0x77);
    write(0xFF11, 0x80);
    write(0xFF12, 0xF3);
    events_.clear();                      // exactly one initial event per channel
    mixEvents_.clear();
    emitMix();
    for (int c = 0; c < 4; ++c) {
        lastLevel_[c] = level(c);
        lastDac_[c]   = dacOn(c);
        events_.push_back({0, uint8_t(c), lastLevel_[c], lastDac_[c]});
    }
}

// ---------------------------------------------------------------------------
// time

int32_t Apu::noisePeriod() const
{
    return int32_t(kNoiseDiv[noise_.divisor]) << noise_.shift;
}

void Apu::runTo(uint64_t target)
{
    while (cycle_ < target) {
        const uint64_t rem = target - cycle_;
        uint32_t dt = rem > 0x40000000u ? 0x40000000u : uint32_t(rem);

        // Next frame-sequencer tick. The divider counts whether or not the
        // APU is powered; the tick only does anything when it is.
        const uint32_t toFrame = kFramePeriod - (divider_ & (kFramePeriod - 1));
        dt = std::min(dt, toFrame);

        if (powered_) {
            for (const auto& s : sq_)
                if (s.timer > 0) dt = std::min<uint32_t>(dt, uint32_t(s.timer));
            if (wave_.enabled && wave_.timer > 0)
                dt = std::min<uint32_t>(dt, uint32_t(wave_.timer));
            if (noise_.enabled && noise_.shift < 14 && noise_.timer > 0)
                dt = std::min<uint32_t>(dt, uint32_t(noise_.timer));
        }
        step(dt);
    }
}

void Apu::step(uint32_t dt)
{
    const uint16_t oldDiv = divider_;
    divider_ = uint16_t(divider_ + dt);
    cycle_  += dt;

    if (!powered_) return;

    if ((uint32_t(oldDiv & (kFramePeriod - 1)) + dt) >= kFramePeriod)
        frameTick();

    // Section 4: pulse timers run whether or not the channel is enabled, which
    // is how the duty phase survives across triggers.
    for (auto& s : sq_) {
        s.timer -= int32_t(dt);
        if (s.timer <= 0) {
            const int32_t p = squarePeriod(s.freq);
            const int32_t n = (-s.timer) / p + 1;
            s.timer += n * p;
            s.dutyPos = uint8_t((s.dutyPos + n) & 7);
        }
    }

    // Section 5: the wave channel fetches one byte per period into a buffer.
    if (wave_.enabled) {
        wave_.timer -= int32_t(dt);
        if (wave_.timer <= 0) {
            const int32_t p = wavePeriod(wave_.freq);
            const int32_t n = (-wave_.timer) / p + 1;
            wave_.lastFetchCycle = cycle_ + uint64_t(wave_.timer + (n - 1) * p);
            wave_.timer += n * p;
            wave_.position = uint8_t((wave_.position + n) & 31);
            wave_.sampleBuffer = wave_.ram[wave_.position >> 1];
        }
    }

    // Section 6: the LFSR must be clocked one step at a time.
    if (noise_.enabled && noise_.shift < 14) {
        noise_.timer -= int32_t(dt);
        while (noise_.timer <= 0) {
            noise_.timer += noisePeriod();
            const uint16_t bit = (noise_.lfsr ^ (noise_.lfsr >> 1)) & 1;
            noise_.lfsr = uint16_t((noise_.lfsr >> 1) | (bit << 14));
            if (noise_.width7)
                noise_.lfsr = uint16_t((noise_.lfsr & ~uint16_t(1 << 6)) | (bit << 6));
        }
    }

    for (int c = 0; c < 4; ++c) emit(c);
}

// ---------------------------------------------------------------------------
// frame sequencer (section 3)

void Apu::frameTick()
{
    if (skipNextTick_) { skipNextTick_ = false; return; }
    switch (frameStep_) {
        case 0: clockLengths(); break;
        case 2: clockLengths(); clockSweep(); break;
        case 4: clockLengths(); break;
        case 6: clockLengths(); clockSweep(); break;
        case 7: clockEnvelopes(); break;
        default: break;
    }
    frameStep_ = uint8_t((frameStep_ + 1) & 7);
}

void Apu::divReset()
{
    // Section 3: writing DIV clears the divider. If bit 12 was set that is a
    // falling edge, and the sequencer clocks early.
    const bool bit12 = divider_ & 0x1000;
    divider_ = 0;
    if (bit12 && powered_) frameTick();
}

void Apu::clockLengths()
{
    auto tick = [](uint16_t& len, bool enabled, bool& chEnabled) {
        if (enabled && len > 0 && --len == 0) chEnabled = false;
    };
    tick(sq_[0].length, sq_[0].lengthEnabled, sq_[0].enabled);
    tick(sq_[1].length, sq_[1].lengthEnabled, sq_[1].enabled);
    tick(wave_.length,  wave_.lengthEnabled,  wave_.enabled);
    tick(noise_.length, noise_.lengthEnabled, noise_.enabled);
}

void Apu::clockEnvelopes()
{
    // Section 7: 64 Hz; a period of 0 reloads the timer as 8 but never moves
    // the volume; the envelope stops once it can no longer move.
    auto tick = [](uint8_t period, uint8_t& timer, bool add, uint8_t& vol, bool& running) {
        if (timer > 0) --timer;
        if (timer != 0) return;
        timer = period ? period : 8;
        if (period == 0 || !running) return;
        if (add && vol < 15)       ++vol;
        else if (!add && vol > 0)  --vol;
        else                       running = false;
    };
    tick(sq_[0].envPeriod, sq_[0].envTimer, sq_[0].envAdd, sq_[0].volume, sq_[0].envRunning);
    tick(sq_[1].envPeriod, sq_[1].envTimer, sq_[1].envAdd, sq_[1].volume, sq_[1].envRunning);
    tick(noise_.envPeriod, noise_.envTimer, noise_.envAdd, noise_.volume, noise_.envRunning);
}

uint16_t Apu::sweepCalc(bool& overflow)
{
    Square& s = sq_[0];
    const uint16_t delta = s.sweepShadow >> s.sweepShift;
    uint16_t nf;
    if (s.sweepNegate) {
        nf = uint16_t(s.sweepShadow - delta);
        s.sweepNegateUsed = true;           // section 10.1, negate quirk
    } else {
        nf = uint16_t(s.sweepShadow + delta);
    }
    overflow = nf > 2047;
    if (overflow) s.enabled = false;
    return nf;
}

void Apu::clockSweep()
{
    // Section 7: 128 Hz. Period 0 reloads the timer as 8. On expiry, if the
    // sweep is enabled and the period is non-zero, a new frequency is
    // computed and overflow-checked; if it fits and the shift is non-zero it
    // is written back and the calculation is run AGAIN for overflow only.
    Square& s = sq_[0];
    if (s.sweepTimer > 0) --s.sweepTimer;
    if (s.sweepTimer != 0) return;
    s.sweepTimer = s.sweepPeriod ? s.sweepPeriod : 8;
    if (!s.sweepEnabled || s.sweepPeriod == 0) return;

    bool ov = false;
    const uint16_t nf = sweepCalc(ov);
    if (!ov && s.sweepShift != 0) {
        s.sweepShadow = nf;
        s.freq = nf;
        s.reg[3] = uint8_t(nf & 0xFF);
        s.reg[4] = uint8_t((s.reg[4] & 0xF8) | (nf >> 8));
        sweepCalc(ov);
    }
}

// ---------------------------------------------------------------------------
// triggers

void Apu::triggerSquare(int i)
{
    Square& s = sq_[i];
    s.enabled = true;
    // Section 7 / 10.1: length reload, one less if enabled in the first half
    // of a length period.
    if (s.length == 0) {
        s.length = 64;
        if (s.lengthEnabled && !nextStepClocksLength()) s.length = 63;
    }
    s.timer = squarePeriod(s.freq);       // duty position is NOT reset (section 4)
    s.envTimer   = s.envPeriod ? s.envPeriod : 8;
    s.volume     = s.envInitial;
    s.envRunning = true;
    if (i == 0) {
        s.sweepShadow     = s.freq;
        s.sweepTimer      = s.sweepPeriod ? s.sweepPeriod : 8;
        s.sweepEnabled    = s.sweepPeriod != 0 || s.sweepShift != 0;
        s.sweepNegateUsed = false;
        if (s.sweepShift != 0) { bool ov; sweepCalc(ov); }   // immediate overflow check
    }
    if (!s.dac) s.enabled = false;        // section 10.1: trigger with DAC off
}

void Apu::triggerWave()
{
    Wave& w = wave_;

    // Section 10.2: DMG wave-RAM corruption. If the channel is running and
    // this trigger lands on the cycle it fetches a byte, the first bytes of
    // wave RAM are overwritten from the byte about to be read.
    if (w.enabled && w.timer > 0 && w.timer <= kWaveCorruptWindow) {
        const unsigned offset = ((w.position + 1) & 31) >> 1;
        if (offset < 4) {
            w.ram[0] = w.ram[offset];
        } else {
            const unsigned base = offset & ~3u;
            for (unsigned k = 0; k < 4; ++k) w.ram[k] = w.ram[base + k];
        }
    }

    w.enabled = true;
    if (w.length == 0) {
        w.length = 256;
        if (w.lengthEnabled && !nextStepClocksLength()) w.length = 255;
    }
    w.position = 0;
    w.timer    = wavePeriod(w.freq) + kWaveTriggerDelay;   // buffer NOT refilled (section 5)
    if (!w.dac) w.enabled = false;
}

void Apu::triggerNoise()
{
    Noise& n = noise_;
    n.enabled = true;
    if (n.length == 0) {
        n.length = 64;
        if (n.lengthEnabled && !nextStepClocksLength()) n.length = 63;
    }
    n.timer      = noisePeriod();
    n.lfsr       = 0x7FFF;
    n.envTimer   = n.envPeriod ? n.envPeriod : 8;
    n.volume     = n.envInitial;
    n.envRunning = true;
    if (!n.dac) n.enabled = false;
}

// ---------------------------------------------------------------------------
// register writes

void Apu::writeSquare(int i, int r, uint8_t v)
{
    Square& s = sq_[i];
    switch (r) {
        case 0: {                                  // NR10 (CH1 only)
            if (i != 0) return;
            s.reg[0] = v;
            const bool neg = v & 0x08;
            if (s.sweepNegate && !neg && s.sweepNegateUsed) s.enabled = false;
            s.sweepPeriod = (v >> 4) & 7;
            s.sweepNegate = neg;
            s.sweepShift  = v & 7;
            break;
        }
        case 1:                                    // NRx1
            s.reg[1]  = v;
            s.duty    = v >> 6;
            s.length  = uint16_t(64 - (v & 63));
            break;
        case 2: {                                  // NRx2
            s.reg[2] = v;
            const bool newAdd = v & 0x08;
            if (s.enabled) {                       // section 10.2: zombie mode (DMG)
                if (s.envPeriod == 0 && s.envRunning) s.volume = uint8_t(s.volume + 1);
                else if (!s.envAdd)                 s.volume = uint8_t(s.volume + 2);
                if (s.envAdd != newAdd)             s.volume = uint8_t(16 - s.volume);
                s.volume &= 0x0F;
            }
            s.envInitial = v >> 4;
            s.envAdd     = newAdd;
            s.envPeriod  = v & 7;
            s.dac = (v & 0xF8) != 0;               // section 7: DAC disable
            if (!s.dac) s.enabled = false;
            break;
        }
        case 3:                                    // NRx3
            s.reg[3] = v;
            s.freq = uint16_t((s.freq & 0x700) | v);
            break;
        case 4: {                                  // NRx4
            s.reg[4] = v;
            const bool wasEnabled = s.lengthEnabled;
            s.lengthEnabled = v & 0x40;
            s.freq = uint16_t((s.freq & 0xFF) | ((v & 7) << 8));
            // Section 10.1: extra length clock when enabling in the first
            // half of a length period.
            if (!wasEnabled && s.lengthEnabled && !nextStepClocksLength() && s.length > 0) {
                if (--s.length == 0 && !(v & 0x80)) s.enabled = false;
            }
            if (v & 0x80) triggerSquare(i);
            break;
        }
        default: break;
    }
}

void Apu::writeWave(int r, uint8_t v)
{
    Wave& w = wave_;
    switch (r) {
        case 0:                                    // NR30
            w.reg[0] = v;
            w.dac = v & 0x80;
            if (!w.dac) w.enabled = false;
            break;
        case 1:                                    // NR31
            w.reg[1] = v;
            w.length = uint16_t(256 - v);
            break;
        case 2:                                    // NR32
            w.reg[2] = v;
            w.volumeCode = (v >> 5) & 3;
            break;
        case 3:                                    // NR33
            w.reg[3] = v;
            w.freq = uint16_t((w.freq & 0x700) | v);
            break;
        case 4: {                                  // NR34
            w.reg[4] = v;
            const bool wasEnabled = w.lengthEnabled;
            w.lengthEnabled = v & 0x40;
            w.freq = uint16_t((w.freq & 0xFF) | ((v & 7) << 8));
            if (!wasEnabled && w.lengthEnabled && !nextStepClocksLength() && w.length > 0) {
                if (--w.length == 0 && !(v & 0x80)) w.enabled = false;
            }
            if (v & 0x80) triggerWave();
            break;
        }
        default: break;
    }
}

void Apu::writeNoise(int r, uint8_t v)
{
    Noise& n = noise_;
    switch (r) {
        case 1:                                    // NR41
            n.reg[1] = v;
            n.length = uint16_t(64 - (v & 63));
            break;
        case 2: {                                  // NR42
            n.reg[2] = v;
            const bool newAdd = v & 0x08;
            if (n.enabled) {
                if (n.envPeriod == 0 && n.envRunning) n.volume = uint8_t(n.volume + 1);
                else if (!n.envAdd)                 n.volume = uint8_t(n.volume + 2);
                if (n.envAdd != newAdd)             n.volume = uint8_t(16 - n.volume);
                n.volume &= 0x0F;
            }
            n.envInitial = v >> 4;
            n.envAdd     = newAdd;
            n.envPeriod  = v & 7;
            n.dac = (v & 0xF8) != 0;
            if (!n.dac) n.enabled = false;
            break;
        }
        case 3:                                    // NR43
            n.reg[3] = v;
            n.shift   = v >> 4;
            n.width7  = v & 0x08;
            n.divisor = v & 7;
            break;
        case 4: {                                  // NR44
            n.reg[4] = v;
            const bool wasEnabled = n.lengthEnabled;
            n.lengthEnabled = v & 0x40;
            if (!wasEnabled && n.lengthEnabled && !nextStepClocksLength() && n.length > 0) {
                if (--n.length == 0 && !(v & 0x80)) n.enabled = false;
            }
            if (v & 0x80) triggerNoise();
            break;
        }
        default: break;
    }
}

void Apu::powerOff()
{
    // Section 2: every register NR10..NR51 is zeroed and the channels stop.
    // Section 10.1 (DMG): the length COUNTERS survive, and so does wave RAM.
    for (auto& s : sq_) {
        const uint16_t len = s.length;
        s = Square{};
        s.length = len;
    }
    {
        const uint16_t len = wave_.length;
        const auto ram = wave_.ram;
        wave_ = Wave{};
        wave_.length = len;
        wave_.ram = ram;
    }
    {
        const uint16_t len = noise_.length;
        noise_ = Noise{};
        noise_.length = len;
    }
    nr50_ = nr51_ = 0;
    powered_ = false;
    emitMix();
    skipNextTick_ = false;
    for (int c = 0; c < 4; ++c) emit(c);
}

void Apu::powerOn()
{
    powered_   = true;
    frameStep_ = 0;                       // next step will be 0
    // Section 3: powering on while divider bit 12 is already set makes the
    // sequencer skip its first tick (SameSuite div_write_trigger_10). Until
    // then the "next step" counts as one that does not clock length, which
    // is what the extra-length-clocking check sees.
    skipNextTick_ = (divider_ & 0x1000) != 0;
    for (auto& s : sq_) {
        s.dutyPos = 0;                    // section 2: power-on resets duty position
        s.timer   = squarePeriod(s.freq);
    }
    for (int c = 0; c < 4; ++c) emit(c);
    emitMix();
}

void Apu::write(uint16_t addr, uint8_t v)
{
    if (addr >= 0xFF30 && addr <= 0xFF3F) {
        // Section 5, DMG: wave RAM is only reachable while the channel is off,
        // or on the cycle of a fetch -- in which case the access hits the byte
        // being fetched, not the one addressed.
        if (!wave_.enabled) {
            wave_.ram[addr & 0x0F] = v;
        } else if (cycle_ - wave_.lastFetchCycle <= kWaveAccessWindow) {
            wave_.ram[wave_.position >> 1] = v;
        }
        return;
    }
    if (addr < 0xFF10 || addr > 0xFF26) return;
    const int idx = addr - 0xFF10;

    if (idx == 0x16) {                                     // NR52
        const bool on = v & 0x80;
        if (powered_ && !on)      powerOff();
        else if (!powered_ && on) powerOn();
        return;
    }
    if (!powered_) {
        // Section 10.1 (DMG): length loads still land while powered off.
        switch (idx) {
            case 0x01: sq_[0].length = uint16_t(64 - (v & 63)); break;
            case 0x06: sq_[1].length = uint16_t(64 - (v & 63)); break;
            case 0x0B: wave_.length  = uint16_t(256 - v);       break;
            case 0x10: noise_.length = uint16_t(64 - (v & 63)); break;
            default: break;
        }
        return;
    }

    if (idx <= 0x04)      writeSquare(0, idx, v);
    else if (idx <= 0x09) writeSquare(1, idx - 5, v);
    else if (idx <= 0x0E) writeWave(idx - 10, v);
    else if (idx <= 0x13) writeNoise(idx - 15, v);
    else if (idx == 0x14) { nr50_ = v; emitMix(); }
    else if (idx == 0x15) { nr51_ = v; emitMix(); }

    for (int c = 0; c < 4; ++c) emit(c);
}

uint8_t Apu::read(uint16_t addr)
{
    if (addr >= 0xFF30 && addr <= 0xFF3F) {
        if (!wave_.enabled) return wave_.ram[addr & 0x0F];
        if (cycle_ - wave_.lastFetchCycle <= kWaveAccessWindow)
            return wave_.ram[wave_.position >> 1];
        return 0xFF;
    }
    if (addr < 0xFF10 || addr > 0xFF2F) return 0xFF;
    const int idx = addr - 0xFF10;
    uint8_t v = 0;
    if (idx <= 0x04)      v = sq_[0].reg[idx];
    else if (idx == 0x05) v = 0;
    else if (idx <= 0x09) v = sq_[1].reg[idx - 5];
    else if (idx <= 0x0E) v = wave_.reg[idx - 10];
    else if (idx == 0x0F) v = 0;
    else if (idx <= 0x13) v = noise_.reg[idx - 15];
    else if (idx == 0x14) v = nr50_;
    else if (idx == 0x15) v = nr51_;
    else if (idx == 0x16)
        v = uint8_t((powered_ ? 0x80 : 0) | (sq_[0].enabled ? 1 : 0) | (sq_[1].enabled ? 2 : 0)
                    | (wave_.enabled ? 4 : 0) | (noise_.enabled ? 8 : 0));
    return uint8_t(v | kReadMask[idx]);
}

// ---------------------------------------------------------------------------
// output

uint8_t Apu::outSquare(const Square& s) const
{
    if (!s.enabled) return 0;
    return ((kDuty[s.duty] >> (7 - s.dutyPos)) & 1) ? s.volume : 0;
}

uint8_t Apu::outWave() const
{
    if (!wave_.enabled) return 0;
    const uint8_t nib = (wave_.position & 1) ? (wave_.sampleBuffer & 0x0F)
                                             : (wave_.sampleBuffer >> 4);
    switch (wave_.volumeCode) {
        case 1:  return nib;
        case 2:  return nib >> 1;
        case 3:  return nib >> 2;
        default: return 0;
    }
}

uint8_t Apu::outNoise() const
{
    if (!noise_.enabled) return 0;
    return (noise_.lfsr & 1) ? 0 : noise_.volume;   // output is NOT bit 0
}

uint8_t Apu::level(int ch) const
{
    switch (ch) {
        case 0: return outSquare(sq_[0]);
        case 1: return outSquare(sq_[1]);
        case 2: return outWave();
        default: return outNoise();
    }
}

bool Apu::dacOn(int ch) const
{
    if (!powered_) return false;
    switch (ch) {
        case 0: return sq_[0].dac;
        case 1: return sq_[1].dac;
        case 2: return wave_.dac;
        default: return noise_.dac;
    }
}

bool Apu::channelActive(int ch) const
{
    switch (ch) {
        case 0: return sq_[0].enabled;
        case 1: return sq_[1].enabled;
        case 2: return wave_.enabled;
        default: return noise_.enabled;
    }
}

void Apu::emitMix()
{
    mixEvents_.push_back({cycle_, nr50_, nr51_, powered_});
}

void Apu::emit(int ch)
{
    const uint8_t lv = level(ch);
    const bool    dc = dacOn(ch);
    if (lv == lastLevel_[ch] && dc == lastDac_[ch]) return;
    lastLevel_[ch] = lv;
    lastDac_[ch]   = dc;
    events_.push_back({cycle_, uint8_t(ch), lv, dc});
}

} // namespace chipboy
