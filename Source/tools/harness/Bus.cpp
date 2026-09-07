#include "tools/harness/Bus.h"

namespace chipboy::harness {

Bus::Bus(std::vector<uint8_t> rom, Apu& apu) : rom_(std::move(rom)), apu_(apu)
{
    if (rom_.size() < 0x8000) rom_.resize(0x8000, 0xFF);
    const uint8_t type = rom_[0x147];
    mbc1_ = type != 0;                       // anything banked: treat as MBC1
}

uint8_t Bus::timerBit() const
{
    static constexpr uint8_t kBit[4] = {9, 3, 5, 7};
    return kBit[tac_ & 3];
}

void Bus::tick(uint32_t cycles)
{
    const uint16_t oldDiv = apu_.divider();
    apu_.runTo(apu_.cycle() + cycles);
    timerTick(oldDiv, apu_.divider());
    lcdTick(cycles);
    if (serialCountdown_ >= 0) {
        serialCountdown_ -= int32_t(cycles);
        if (serialCountdown_ < 0) {
            sc_ &= 0x7F;
            if_ |= 0x08;
        }
    }
}

void Bus::timerTick(uint16_t oldDiv, uint16_t newDiv)
{
    if (!(tac_ & 0x04)) return;
    const uint8_t b = timerBit();
    const bool wasSet = (oldDiv >> b) & 1;
    const bool isSet  = (newDiv >> b) & 1;
    if (wasSet && !isSet) {
        if (++tima_ == 0) {
            tima_ = tma_;
            if_ |= 0x04;
        }
    }
}

void Bus::lcdTick(uint32_t cycles)
{
    if (!(lcdc_ & 0x80)) return;
    lineCycle_ += cycles;
    while (lineCycle_ >= 456) {
        lineCycle_ -= 456;
        if (++ly_ == 144) if_ |= 0x01;
        if (ly_ >= 154) ly_ = 0;
        const bool coincide = (ly_ == lyc_);
        stat_ = uint8_t((stat_ & ~0x04) | (coincide ? 0x04 : 0));
        if (coincide && (stat_ & 0x40)) if_ |= 0x02;
    }
}

uint8_t Bus::read(uint16_t a)
{
    if (a < 0x4000) {
        if (mbc1_ && mbcMode_) {
            const size_t bank = size_t(bankHi_) << 5;
            const size_t off = (bank * 0x4000 + a) % rom_.size();
            return rom_[off];
        }
        return rom_[a];
    }
    if (a < 0x8000) {
        size_t bank = 1;
        if (mbc1_) bank = size_t(bankHi_ << 5) | romBankLo_;
        const size_t off = (bank * 0x4000 + (a - 0x4000)) % rom_.size();
        return rom_[off];
    }
    if (a < 0xA000) return vram_[a - 0x8000];
    if (a < 0xC000) {
        if (!mbc1_ || ramEnabled_) {
            const size_t bank = (mbc1_ && mbcMode_) ? bankHi_ : 0;
            return cartRam_[(bank * 0x2000 + (a - 0xA000)) % cartRam_.size()];
        }
        return 0xFF;
    }
    if (a < 0xE000) return wram_[a - 0xC000];
    if (a < 0xFE00) return wram_[a - 0xE000];
    if (a < 0xFEA0) return oam_[a - 0xFE00];
    if (a < 0xFF00) return 0xFF;
    if (a >= 0xFF80 && a < 0xFFFF) return hram_[a - 0xFF80];
    if (a == 0xFFFF) return ie_;

    switch (a) {
        case 0xFF00: return joyp_ | 0xCF;
        case 0xFF01: return sb_;
        case 0xFF02: return sc_ | 0x7E;
        case 0xFF04: return uint8_t(apu_.divider() >> 8);
        case 0xFF05: return tima_;
        case 0xFF06: return tma_;
        case 0xFF07: return tac_ | 0xF8;
        case 0xFF0F: return if_ | 0xE0;
        case 0xFF40: return lcdc_;
        case 0xFF41: {
            uint8_t mode = 0;
            if (lcdc_ & 0x80) {
                if (ly_ >= 144)          mode = 1;
                else if (lineCycle_ < 80)  mode = 2;
                else if (lineCycle_ < 252) mode = 3;
            }
            return uint8_t((stat_ & 0xFC) | mode | 0x80);
        }
        case 0xFF42: return scy_;
        case 0xFF43: return scx_;
        case 0xFF44: return ly_;
        case 0xFF45: return lyc_;
        case 0xFF47: return bgp_;
        case 0xFF48: return obp0_;
        case 0xFF49: return obp1_;
        case 0xFF4A: return wy_;
        case 0xFF4B: return wx_;
        default: break;
    }
    if (a >= 0xFF10 && a <= 0xFF3F) return apu_.read(a);
    return 0xFF;
}

void Bus::write(uint16_t a, uint8_t v)
{
    if (a < 0x8000) {
        if (!mbc1_) return;
        if (a < 0x2000)      ramEnabled_ = (v & 0x0F) == 0x0A;
        else if (a < 0x4000) { romBankLo_ = v & 0x1F; if (romBankLo_ == 0) romBankLo_ = 1; }
        else if (a < 0x6000) bankHi_ = v & 0x03;
        else                 mbcMode_ = v & 0x01;
        return;
    }
    if (a < 0xA000) { vram_[a - 0x8000] = v; return; }
    if (a < 0xC000) {
        if (!mbc1_ || ramEnabled_) {
            const size_t bank = (mbc1_ && mbcMode_) ? bankHi_ : 0;
            cartRam_[(bank * 0x2000 + (a - 0xA000)) % cartRam_.size()] = v;
        }
        return;
    }
    if (a < 0xE000) { wram_[a - 0xC000] = v; return; }
    if (a < 0xFE00) { wram_[a - 0xE000] = v; return; }
    if (a < 0xFEA0) { oam_[a - 0xFE00] = v; return; }
    if (a < 0xFF00) return;
    if (a >= 0xFF80 && a < 0xFFFF) { hram_[a - 0xFF80] = v; return; }
    if (a == 0xFFFF) { ie_ = v; return; }

    switch (a) {
        case 0xFF00: joyp_ = v & 0x30; return;
        case 0xFF01: sb_ = v; return;
        case 0xFF02:
            sc_ = v;
            if ((v & 0x81) == 0x81) {              // start, internal clock
                serial_.push_back(char(sb_));
                serialCountdown_ = 8 * 512;
            }
            return;
        case 0xFF04: {
            // A DIV write clears the divider. That is a falling edge for any
            // bit that was set: the APU's frame sequencer and the timer both
            // see it (reference section 3).
            const uint16_t old = apu_.divider();
            apu_.divReset();
            if ((tac_ & 0x04) && ((old >> timerBit()) & 1)) {
                if (++tima_ == 0) { tima_ = tma_; if_ |= 0x04; }
            }
            return;
        }
        case 0xFF05: tima_ = v; return;
        case 0xFF06: tma_ = v; return;
        case 0xFF07: tac_ = v & 0x07; return;
        case 0xFF0F: if_ = v & 0x1F; return;
        case 0xFF40:
            if ((lcdc_ & 0x80) && !(v & 0x80)) { ly_ = 0; lineCycle_ = 0; }
            lcdc_ = v; return;
        case 0xFF41: stat_ = uint8_t((v & 0x78) | (stat_ & 0x07)); return;
        case 0xFF42: scy_ = v; return;
        case 0xFF43: scx_ = v; return;
        case 0xFF44: return;
        case 0xFF45: lyc_ = v; return;
        case 0xFF46: {                              // OAM DMA: copy 160 bytes
            const uint16_t src = uint16_t(v) << 8;
            for (uint16_t i = 0; i < 0xA0; ++i) oam_[i] = read(uint16_t(src + i));
            return;
        }
        case 0xFF47: bgp_ = v; return;
        case 0xFF48: obp0_ = v; return;
        case 0xFF49: obp1_ = v; return;
        case 0xFF4A: wy_ = v; return;
        case 0xFF4B: wx_ = v; return;
        default: break;
    }
    if (a >= 0xFF10 && a <= 0xFF3F) apu_.write(a, v);
}

} // namespace chipboy::harness
