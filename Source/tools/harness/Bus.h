// ChipBoy test-ROM harness -- memory map, MBC1, timer, serial, LCD timing.
// Exists only so real Game Boy test ROMs can drive the APU. Ships in nothing.
#pragma once

#include "core/Apu/Apu.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace chipboy::harness {

class Bus {
public:
    Bus(std::vector<uint8_t> rom, Apu& apu);

    uint8_t read(uint16_t addr);
    void    write(uint16_t addr, uint8_t v);

    /// Advance every clocked device by `cycles` (a multiple of 4 from the CPU).
    void tick(uint32_t cycles);

    uint8_t& interruptFlags() { return if_; }
    uint8_t  interruptEnable() const { return ie_; }

    const std::string& serial() const { return serial_; }
    const uint8_t* cartRam() const { return cartRam_.data(); }
    const uint8_t* wram() const { return wram_.data(); }
    const std::vector<uint8_t>& rom() const { return rom_; }
    uint64_t cycles() const { return apu_.cycle(); }
    Apu& apu() { return apu_; }

private:
    void timerTick(uint16_t oldDiv, uint16_t newDiv);
    void lcdTick(uint32_t cycles);
    uint8_t timerBit() const;

    std::vector<uint8_t> rom_;
    Apu& apu_;
    std::array<uint8_t, 0x8000> cartRam_{};
    std::array<uint8_t, 0x2000> vram_{};
    std::array<uint8_t, 0x2000> wram_{};
    std::array<uint8_t, 0xA0>   oam_{};
    std::array<uint8_t, 0x7F>   hram_{};

    bool     mbc1_ = false;
    bool     ramEnabled_ = false;
    uint8_t  romBankLo_ = 1, bankHi_ = 0;
    bool     mbcMode_ = false;

    uint8_t  tima_ = 0, tma_ = 0, tac_ = 0;
    uint8_t  if_ = 0xE1, ie_ = 0;
    uint8_t  joyp_ = 0xCF;
    uint8_t  sb_ = 0, sc_ = 0;
    int32_t  serialCountdown_ = -1;
    std::string serial_;

    uint8_t  lcdc_ = 0x91, stat_ = 0x85, scy_ = 0, scx_ = 0, ly_ = 0, lyc_ = 0;
    uint8_t  bgp_ = 0xFC, obp0_ = 0xFF, obp1_ = 0xFF, wy_ = 0, wx_ = 0;
    uint32_t lineCycle_ = 0;
};

} // namespace chipboy::harness
