#include "tools/harness/Sm83.h"
#include "tools/harness/Bus.h"

namespace chipboy::harness {

namespace {
constexpr uint8_t FZ = 0x80, FN = 0x40, FH = 0x20, FC = 0x10;
}

Sm83::Sm83(Bus& bus) : bus_(bus) { reset(); }

void Sm83::reset()
{
    a = 0x01; f = 0xB0; b = 0x00; c = 0x13; d = 0x00; e = 0xD8; h = 0x01; l = 0x4D;
    sp = 0xFFFE; pc = 0x0100;
    ime = imePending = halted = false;
    breakpoint = false;
    instructions = 0;
}

void Sm83::tick() { bus_.tick(4); }

uint8_t Sm83::rd(uint16_t addr) { tick(); return bus_.read(addr); }
void    Sm83::wr(uint16_t addr, uint8_t v) { tick(); bus_.write(addr, v); }
uint8_t Sm83::fetch() { return rd(pc++); }
uint16_t Sm83::fetch16() { const uint8_t lo = fetch(); const uint8_t hi = fetch(); return uint16_t((hi << 8) | lo); }

uint8_t Sm83::getR(int i)
{
    switch (i) {
        case 0: return b; case 1: return c; case 2: return d; case 3: return e;
        case 4: return h; case 5: return l; case 6: return rd(hl()); default: return a;
    }
}
void Sm83::setR(int i, uint8_t v)
{
    switch (i) {
        case 0: b = v; break; case 1: c = v; break; case 2: d = v; break; case 3: e = v; break;
        case 4: h = v; break; case 5: l = v; break; case 6: wr(hl(), v); break; default: a = v; break;
    }
}
uint16_t Sm83::getRp(int i) const { switch (i) { case 0: return bc(); case 1: return de(); case 2: return hl(); default: return sp; } }
void     Sm83::setRp(int i, uint16_t v) { switch (i) { case 0: bc(v); break; case 1: de(v); break; case 2: hl(v); break; default: sp = v; break; } }
uint16_t Sm83::getRp2(int i) const { switch (i) { case 0: return bc(); case 1: return de(); case 2: return hl(); default: return af(); } }
void     Sm83::setRp2(int i, uint16_t v) { switch (i) { case 0: bc(v); break; case 1: de(v); break; case 2: hl(v); break; default: af(v); break; } }
bool     Sm83::cond(int i) const { switch (i) { case 0: return !(f & FZ); case 1: return f & FZ; case 2: return !(f & FC); default: return f & FC; } }

void Sm83::push(uint16_t v) { wr(--sp, uint8_t(v >> 8)); wr(--sp, uint8_t(v)); }
uint16_t Sm83::pop() { const uint8_t lo = rd(sp++); const uint8_t hi = rd(sp++); return uint16_t((hi << 8) | lo); }

void Sm83::alu(int op, uint8_t v)
{
    const uint8_t cy = (f & FC) ? 1 : 0;
    switch (op) {
        case 0: { const unsigned r = a + v; f = uint8_t(((r & 0xFF) == 0 ? FZ : 0) | (((a & 0xF) + (v & 0xF)) > 0xF ? FH : 0) | (r > 0xFF ? FC : 0)); a = uint8_t(r); break; }
        case 1: { const unsigned r = a + v + cy; f = uint8_t(((r & 0xFF) == 0 ? FZ : 0) | (((a & 0xF) + (v & 0xF) + cy) > 0xF ? FH : 0) | (r > 0xFF ? FC : 0)); a = uint8_t(r); break; }
        case 2: { const unsigned r = a - v; f = uint8_t(((r & 0xFF) == 0 ? FZ : 0) | FN | ((a & 0xF) < (v & 0xF) ? FH : 0) | (a < v ? FC : 0)); a = uint8_t(r); break; }
        case 3: { const int r = a - v - cy; f = uint8_t(((r & 0xFF) == 0 ? FZ : 0) | FN | (((a & 0xF) - (v & 0xF) - cy) < 0 ? FH : 0) | (r < 0 ? FC : 0)); a = uint8_t(r); break; }
        case 4: a &= v; f = uint8_t((a == 0 ? FZ : 0) | FH); break;
        case 5: a ^= v; f = uint8_t(a == 0 ? FZ : 0); break;
        case 6: a |= v; f = uint8_t(a == 0 ? FZ : 0); break;
        default: { const unsigned r = a - v; f = uint8_t(((r & 0xFF) == 0 ? FZ : 0) | FN | ((a & 0xF) < (v & 0xF) ? FH : 0) | (a < v ? FC : 0)); break; }
    }
}

uint8_t Sm83::rot(int op, uint8_t v)
{
    const uint8_t cy = (f & FC) ? 1 : 0;
    uint8_t r = 0, c2 = 0;
    switch (op) {
        case 0: c2 = v >> 7; r = uint8_t((v << 1) | c2); break;              // RLC
        case 1: c2 = v & 1;  r = uint8_t((v >> 1) | (c2 << 7)); break;       // RRC
        case 2: c2 = v >> 7; r = uint8_t((v << 1) | cy); break;              // RL
        case 3: c2 = v & 1;  r = uint8_t((v >> 1) | (cy << 7)); break;       // RR
        case 4: c2 = v >> 7; r = uint8_t(v << 1); break;                     // SLA
        case 5: c2 = v & 1;  r = uint8_t((v >> 1) | (v & 0x80)); break;      // SRA
        case 6: c2 = 0;      r = uint8_t((v << 4) | (v >> 4)); break;        // SWAP
        default: c2 = v & 1; r = uint8_t(v >> 1); break;                     // SRL
    }
    f = uint8_t((r == 0 ? FZ : 0) | (c2 ? FC : 0));
    return r;
}

bool Sm83::serviceInterrupt()
{
    const uint8_t pending = bus_.interruptFlags() & bus_.interruptEnable() & 0x1F;
    if (!pending) return false;
    halted = false;
    if (!ime) return false;
    ime = false;
    tick(); tick();
    push(pc);
    tick();
    for (int i = 0; i < 5; ++i) {
        if (pending & (1 << i)) {
            bus_.interruptFlags() &= uint8_t(~(1 << i));
            pc = uint16_t(0x40 + i * 8);
            break;
        }
    }
    return true;
}

void Sm83::step()
{
    if (serviceInterrupt()) return;
    if (halted) { tick(); return; }
    // EI takes effect after the instruction that follows it; a DI there
    // cancels it (it clears imePending).
    const bool enable = imePending;
    const uint8_t op = fetch();
    ++instructions;
    execute(op);
    if (enable && imePending) { ime = true; imePending = false; }
}

void Sm83::execute(uint8_t op)
{
    const int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;

    switch (x) {
    case 0:
        switch (z) {
        case 0:
            switch (y) {
                case 0: return;                                                  // NOP
                case 1: { const uint16_t nn = fetch16(); wr(nn, uint8_t(sp)); wr(uint16_t(nn + 1), uint8_t(sp >> 8)); return; } // LD (nn),SP
                case 2: fetch(); return;                                         // STOP (consume the pad byte)
                case 3: { const int8_t d8 = int8_t(fetch()); tick(); pc = uint16_t(pc + d8); return; }   // JR d
                default: { const int8_t d8 = int8_t(fetch()); if (cond(y - 4)) { tick(); pc = uint16_t(pc + d8); } return; } // JR cc,d
            }
        case 1:
            if (q == 0) { setRp(p, fetch16()); return; }                       // LD rp,nn
            { const uint32_t r = hl() + getRp(p); tick();                        // ADD HL,rp
              f = uint8_t((f & FZ) | (((hl() & 0xFFF) + (getRp(p) & 0xFFF)) > 0xFFF ? FH : 0) | (r > 0xFFFF ? FC : 0));
              hl(uint16_t(r)); return; }
        case 2:
            if (q == 0) {
                switch (p) {
                    case 0: wr(bc(), a); return;
                    case 1: wr(de(), a); return;
                    case 2: wr(hl(), a); hl(uint16_t(hl() + 1)); return;
                    default: wr(hl(), a); hl(uint16_t(hl() - 1)); return;
                }
            } else {
                switch (p) {
                    case 0: a = rd(bc()); return;
                    case 1: a = rd(de()); return;
                    case 2: a = rd(hl()); hl(uint16_t(hl() + 1)); return;
                    default: a = rd(hl()); hl(uint16_t(hl() - 1)); return;
                }
            }
        case 3: tick(); setRp(p, uint16_t(getRp(p) + (q == 0 ? 1 : -1))); return; // INC/DEC rp
        case 4: { const uint8_t v = getR(y); const uint8_t r = uint8_t(v + 1);      // INC r
                  f = uint8_t((f & FC) | (r == 0 ? FZ : 0) | ((v & 0xF) == 0xF ? FH : 0)); setR(y, r); return; }
        case 5: { const uint8_t v = getR(y); const uint8_t r = uint8_t(v - 1);      // DEC r
                  f = uint8_t((f & FC) | (r == 0 ? FZ : 0) | FN | ((v & 0xF) == 0 ? FH : 0)); setR(y, r); return; }
        case 6: setR(y, fetch()); return;                                       // LD r,n
        default:
            switch (y) {
                case 0: a = rot(0, a); f &= FC; return;                            // RLCA
                case 1: a = rot(1, a); f &= FC; return;                            // RRCA
                case 2: a = rot(2, a); f &= FC; return;                            // RLA
                case 3: a = rot(3, a); f &= FC; return;                            // RRA
                case 4: {                                                          // DAA
                    unsigned r = a;
                    if (!(f & FN)) {
                        if ((f & FH) || (r & 0xF) > 9) r += 0x06;
                        if ((f & FC) || r > 0x9F) r += 0x60;
                    } else {
                        if (f & FH) r = (r - 6) & 0xFF;
                        if (f & FC) r -= 0x60;
                    }
                    f &= uint8_t(FN | FC);
                    if (r & 0x100) f |= FC;
                    a = uint8_t(r);
                    if (a == 0) f |= FZ;
                    return;
                }
                case 5: a = uint8_t(~a); f |= FN | FH; return;                     // CPL
                case 6: f = uint8_t((f & FZ) | FC); return;                        // SCF
                default: f = uint8_t((f & FZ) | ((f & FC) ? 0 : FC)); return;      // CCF
            }
        }
    case 1:
        if (op == 0x76) {                                                       // HALT
            halted = true;
            return;
        }
        if (op == 0x40) breakpoint = true;                                      // LD B,B: debugger breakpoint
        setR(y, getR(z)); return;                                               // LD r,r'
    case 2:
        alu(y, getR(z)); return;                                                // alu r
    default:
        switch (z) {
        case 0:
            switch (y) {
                case 0: case 1: case 2: case 3: tick(); if (cond(y)) { pc = pop(); tick(); } return;   // RET cc
                case 4: { const uint8_t n = fetch(); wr(uint16_t(0xFF00 + n), a); return; }         // LDH (n),A
                case 5: { const int8_t d8 = int8_t(fetch()); tick(); tick();                          // ADD SP,d
                          const uint16_t r = uint16_t(sp + d8);
                          f = uint8_t((((sp & 0xF) + (uint8_t(d8) & 0xF)) > 0xF ? FH : 0) | (((sp & 0xFF) + uint8_t(d8)) > 0xFF ? FC : 0));
                          sp = r; return; }
                case 6: { const uint8_t n = fetch(); a = rd(uint16_t(0xFF00 + n)); return; }         // LDH A,(n)
                default: { const int8_t d8 = int8_t(fetch()); tick();                                 // LD HL,SP+d
                           const uint16_t r = uint16_t(sp + d8);
                           f = uint8_t((((sp & 0xF) + (uint8_t(d8) & 0xF)) > 0xF ? FH : 0) | (((sp & 0xFF) + uint8_t(d8)) > 0xFF ? FC : 0));
                           hl(r); return; }
            }
        case 1:
            if (q == 0) { setRp2(p, pop()); return; }                          // POP
            switch (p) {
                case 0: pc = pop(); tick(); return;                             // RET
                case 1: pc = pop(); tick(); ime = true; return;                 // RETI
                case 2: pc = hl(); return;                                      // JP HL
                default: tick(); sp = hl(); return;                             // LD SP,HL
            }
        case 2:
            switch (y) {
                case 0: case 1: case 2: case 3: { const uint16_t nn = fetch16(); if (cond(y)) { tick(); pc = nn; } return; } // JP cc,nn
                case 4: wr(uint16_t(0xFF00 + c), a); return;                    // LDH (C),A
                case 5: wr(fetch16(), a); return;                               // LD (nn),A
                case 6: a = rd(uint16_t(0xFF00 + c)); return;                   // LDH A,(C)
                default: a = rd(fetch16()); return;                             // LD A,(nn)
            }
        case 3:
            switch (y) {
                case 0: { const uint16_t nn = fetch16(); tick(); pc = nn; return; }  // JP nn
                case 1: executeCB(); return;
                case 6: ime = false; imePending = false; return;                // DI
                case 7: imePending = true; return;                              // EI
                default: return;                                                // illegal
            }
        case 4: { const uint16_t nn = fetch16(); if (y < 4 && cond(y)) { tick(); push(pc); pc = nn; } return; } // CALL cc,nn
        case 5:
            if (q == 0) { tick(); push(getRp2(p)); return; }                    // PUSH
            if (p == 0) { const uint16_t nn = fetch16(); tick(); push(pc); pc = nn; return; }  // CALL nn
            return;
        case 6: alu(y, fetch()); return;                                        // alu n
        default: tick(); push(pc); pc = uint16_t(y * 8); return;                // RST
        }
    }
}

void Sm83::executeCB()
{
    const uint8_t op = fetch();
    const int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    switch (x) {
        case 0: setR(z, rot(y, getR(z))); return;
        case 1: { const uint8_t v = getR(z); f = uint8_t((f & FC) | FH | ((v & (1 << y)) ? 0 : FZ)); return; }
        case 2: setR(z, uint8_t(getR(z) & ~(1 << y))); return;
        default: setR(z, uint8_t(getR(z) | (1 << y))); return;
    }
}

} // namespace chipboy::harness
