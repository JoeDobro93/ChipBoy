// ChipBoy test-ROM harness -- SM83 CPU, M-cycle accurate.
//
// Every memory access is preceded by one bus tick, so accesses land on the
// M-cycle boundary the hardware puts them on. That is what makes wave-RAM
// timing tests meaningful. Full instruction set; not optimised.
#pragma once

#include <cstdint>

namespace chipboy::harness {

class Bus;

class Sm83 {
public:
    explicit Sm83(Bus& bus);

    void reset();          ///< post-boot register state, PC = $0100
    void step();           ///< one instruction, or one interrupt dispatch

    uint8_t  a = 0, f = 0, b = 0, c = 0, d = 0, e = 0, h = 0, l = 0;
    uint16_t sp = 0, pc = 0;
    bool     ime = false, imePending = false, halted = false;
    bool     breakpoint = false;   ///< set when `ld b,b` executes (SameSuite's "test done")
    uint64_t instructions = 0;

private:
    void    tick();
    uint8_t rd(uint16_t addr);
    void    wr(uint16_t addr, uint8_t v);
    uint8_t fetch();
    uint16_t fetch16();

    uint16_t bc() const { return uint16_t((b << 8) | c); }
    uint16_t de() const { return uint16_t((d << 8) | e); }
    uint16_t hl() const { return uint16_t((h << 8) | l); }
    uint16_t af() const { return uint16_t((a << 8) | (f & 0xF0)); }
    void bc(uint16_t v) { b = uint8_t(v >> 8); c = uint8_t(v); }
    void de(uint16_t v) { d = uint8_t(v >> 8); e = uint8_t(v); }
    void hl(uint16_t v) { h = uint8_t(v >> 8); l = uint8_t(v); }
    void af(uint16_t v) { a = uint8_t(v >> 8); f = uint8_t(v & 0xF0); }

    uint8_t  getR(int i);
    void     setR(int i, uint8_t v);
    uint16_t getRp(int i) const;
    void     setRp(int i, uint16_t v);
    uint16_t getRp2(int i) const;
    void     setRp2(int i, uint16_t v);
    bool     cond(int i) const;

    void alu(int op, uint8_t v);
    uint8_t rot(int op, uint8_t v);
    void push(uint16_t v);
    uint16_t pop();
    void execute(uint8_t op);
    void executeCB();
    bool serviceInterrupt();

    Bus& bus_;
};

} // namespace chipboy::harness
