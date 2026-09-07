// ChipBoy -- DMG APU core
//
// A cycle-exact model of the Game Boy's audio processing unit. Plain C++20,
// no dependencies, no floats: time is a 64-bit count of 4.194304 MHz cycles
// and the output is a stream of DAC-input change events, not samples.
//
// Behaviour is defined by docs/HARDWARE_REFERENCE.md. Acceptance is blargg's
// dmg_sound test ROMs, run through the harness in Source/tools/harness.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace chipboy {

constexpr uint32_t kCpuHz = 4194304u;

/// One change in one channel's DAC input. The APU's entire output is a stream
/// of these; the analog stage and renderer consume them (spec section 5.2).
struct ApuEvent {
    uint64_t cycle;
    uint8_t  channel;   ///< 0 PU1, 1 PU2, 2 WAV, 3 NOI
    uint8_t  level;     ///< digital DAC input, 0..15
    bool     dacOn;     ///< false: the DAC is disabled and outputs analog zero
};

class Apu {
public:
    Apu();

    /// State after the boot ROM has run: APU on, NR51 = $F3, NR50 = $77.
    void reset();

    /// Advance to an absolute cycle, emitting events as channel outputs change.
    /// Uses next-event scheduling: nothing is ticked per cycle.
    void runTo(uint64_t cycle);

    /// Register access at the current cycle. Callers advance with runTo() first
    /// so that wave-RAM access lands on the right cycle (reference section 5).
    void    write(uint16_t addr, uint8_t value);
    uint8_t read(uint16_t addr);

    uint64_t cycle() const { return cycle_; }

    // --- divider -----------------------------------------------------------
    // The frame sequencer is clocked from bit 12 of the same 16-bit counter
    // that backs the CPU's DIV register (reference section 3). The APU owns
    // the counter; the harness implements DIV on top of it.
    uint16_t divider() const { return divider_; }
    void     divReset();     ///< a write to DIV: the counter clears, and a
                             ///< falling edge on bit 12 clocks the sequencer

    // --- output ------------------------------------------------------------
    std::vector<ApuEvent>& events() { return events_; }

    // --- introspection, for tests -----------------------------------------
    bool    channelActive(int ch) const;
    bool    dacOn(int ch) const;
    uint8_t level(int ch) const;
    bool    powered() const { return powered_; }
    uint8_t frameStep() const { return frameStep_; }

private:
    struct Square {
        uint8_t  reg[5]{};
        bool     enabled = false, dac = false;
        uint16_t freq = 0;
        int32_t  timer = 0;
        uint8_t  duty = 0, dutyPos = 0;
        uint16_t length = 0;
        bool     lengthEnabled = false;
        uint8_t  envInitial = 0, envPeriod = 0, envTimer = 0, volume = 0;
        bool     envAdd = false, envRunning = false;
        uint8_t  sweepPeriod = 0, sweepShift = 0, sweepTimer = 0;
        bool     sweepNegate = false, sweepEnabled = false, sweepNegateUsed = false;
        uint16_t sweepShadow = 0;
    };
    struct Wave {
        uint8_t  reg[5]{};
        bool     enabled = false, dac = false;
        uint16_t freq = 0;
        int32_t  timer = 0;
        uint8_t  position = 0, sampleBuffer = 0, volumeCode = 0;
        uint16_t length = 0;
        bool     lengthEnabled = false;
        uint64_t lastFetchCycle = 0;   ///< for the DMG wave-RAM access window
        std::array<uint8_t, 16> ram{};
    };
    struct Noise {
        uint8_t  reg[5]{};
        bool     enabled = false, dac = false;
        int32_t  timer = 0;
        uint16_t lfsr = 0x7FFF;
        uint8_t  shift = 0, divisor = 0;
        bool     width7 = false;
        uint16_t length = 0;
        bool     lengthEnabled = false;
        uint8_t  envInitial = 0, envPeriod = 0, envTimer = 0, volume = 0;
        bool     envAdd = false, envRunning = false;
    };

    void step(uint32_t dt);
    void frameTick();
    void clockLengths();
    void clockSweep();
    void clockEnvelopes();

    void writeSquare(int i, int r, uint8_t v);
    void writeWave(int r, uint8_t v);
    void writeNoise(int r, uint8_t v);
    void triggerSquare(int i);
    void triggerWave();
    void triggerNoise();
    uint16_t sweepCalc(bool& overflow);
    void powerOff();
    void powerOn();

    uint8_t outSquare(const Square& s) const;
    uint8_t outWave() const;
    uint8_t outNoise() const;
    void    emit(int ch);
    /// Whether the sequencer's next tick clocks the length counters. False
    /// while a power-on skip is pending: that tick does nothing at all.
    bool    nextStepClocksLength() const { return !skipNextTick_ && (frameStep_ & 1) == 0; }

    static constexpr int32_t squarePeriod(uint16_t f) { return (2048 - f) * 4; }
    static constexpr int32_t wavePeriod(uint16_t f)   { return (2048 - f) * 2; }
    int32_t noisePeriod() const;

    Square   sq_[2];
    Wave     wave_;
    Noise    noise_;
    uint8_t  nr50_ = 0, nr51_ = 0;
    bool     powered_ = false;
    uint8_t  frameStep_ = 0;        ///< the NEXT step the sequencer will execute
    bool     skipNextTick_ = false; ///< power-on with divider bit 12 set: first tick is skipped
    uint16_t divider_ = 0;
    uint64_t cycle_ = 0;

    std::vector<ApuEvent> events_;
    uint8_t  lastLevel_[4]{};
    bool     lastDac_[4]{};
};

} // namespace chipboy
