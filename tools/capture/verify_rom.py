#!/usr/bin/env python3
"""Execute chipboy_probe.gb on a minimal SM83 interpreter and verify that it
produces the intended APU register sequence.

This is not a Game Boy emulator. It implements exactly the instruction subset
the probe ROM uses, and raises on anything else, so "it ran" means "every
instruction was understood". It exists to catch stack imbalances, bad loops and
table errors before a recording session is spent on a broken ROM.

Emits takes.json: the manifest the analyser uses to know what it is looking at.
"""
import json, sys, os

CPU_HZ = 4194304
TONES = {}


class SM83:
    def __init__(self, rom):
        self.rom = rom
        self.ram = bytearray(0x10000)
        self.a = 0x01; self.f = 0xB0
        self.b = 0x00; self.c = 0x13
        self.d = 0x00; self.e = 0xD8
        self.h = 0x01; self.l = 0x4D
        self.sp = 0xFFFE; self.pc = 0x0100
        self.cycles = 0
        self.io_log = []          # (cycle, addr, value)
        self.ly = 0
        self.halted = False
        # Exact fast path for WaitMs.inner. That loop provably only decrements
        # BC at 28 T-cycles per iteration, so collapsing n-1 iterations into an
        # arithmetic step is cycle-identical to running them. Without it the
        # ~4 minute ROM needs ~150M interpreted instructions.
        self.delay_loop = None

    # --- flags ---
    def _zf(self): return (self.f >> 7) & 1
    def _cf(self): return (self.f >> 4) & 1
    def _setz(self, z): self.f = (self.f & 0x7F) | (0x80 if z else 0)
    def _setn(self, n): self.f = (self.f & 0xBF) | (0x40 if n else 0)
    def _seth(self, h): self.f = (self.f & 0xDF) | (0x20 if h else 0)
    def _setc(self, c): self.f = (self.f & 0xEF) | (0x10 if c else 0)

    def rb(self, a):
        a &= 0xFFFF
        if a == 0xFF44:                     # LY: free-running line counter
            return (self.cycles // 456) % 154
        if a < 0x8000:
            return self.rom[a]
        return self.ram[a]

    def wb(self, a, v):
        a &= 0xFFFF; v &= 0xFF
        if a < 0x8000:
            return                          # ROM write: ignored
        self.ram[a] = v
        if 0xFF10 <= a <= 0xFF3F or a == 0xFF40 or a == 0xFF47:
            self.io_log.append((self.cycles, a, v))

    def imm8(self):
        v = self.rb(self.pc); self.pc = (self.pc + 1) & 0xFFFF; return v

    def imm16(self):
        lo = self.imm8(); hi = self.imm8(); return (hi << 8) | lo

    def push(self, v):
        self.sp = (self.sp - 2) & 0xFFFF
        self.ram[self.sp] = v & 0xFF
        self.ram[(self.sp + 1) & 0xFFFF] = (v >> 8) & 0xFF

    def pop(self):
        v = self.ram[self.sp] | (self.ram[(self.sp + 1) & 0xFFFF] << 8)
        self.sp = (self.sp + 2) & 0xFFFF
        return v

    def get_r(self, i):
        return [self.b, self.c, self.d, self.e, self.h, self.l,
                self.rb((self.h << 8) | self.l), self.a][i]

    def set_r(self, i, v):
        v &= 0xFF
        if   i == 0: self.b = v
        elif i == 1: self.c = v
        elif i == 2: self.d = v
        elif i == 3: self.e = v
        elif i == 4: self.h = v
        elif i == 5: self.l = v
        elif i == 6: self.wb((self.h << 8) | self.l, v)
        else:        self.a = v

    def step(self):
        if self.pc == self.delay_loop:
            bc = (self.b << 8) | self.c
            if bc > 1:
                n = bc - 1
                self.cycles += 28 * n
                self.b, self.c = 0, 1
        op = self.imm8()

        # ld r, r'   (0x40..0x7F, excluding 0x76 = halt)
        if 0x40 <= op <= 0x7F and op != 0x76:
            self.set_r((op >> 3) & 7, self.get_r(op & 7))
            self.cycles += 8 if (op & 7) == 6 or ((op >> 3) & 7) == 6 else 4
            return True
        if op == 0x76:
            self.halted = True; return False

        # ld r, d8
        if op in (0x06, 0x0E, 0x16, 0x1E, 0x26, 0x2E, 0x36, 0x3E):
            self.set_r((op >> 3) & 7, self.imm8()); self.cycles += 8; return True

        # ld rr, d16
        if op == 0x01: v = self.imm16(); self.b, self.c = v >> 8, v & 0xFF; self.cycles += 12; return True
        if op == 0x11: v = self.imm16(); self.d, self.e = v >> 8, v & 0xFF; self.cycles += 12; return True
        if op == 0x21: v = self.imm16(); self.h, self.l = v >> 8, v & 0xFF; self.cycles += 12; return True
        if op == 0x31: self.sp = self.imm16(); self.cycles += 12; return True

        # inc/dec rr
        if op == 0x03: v = ((self.b << 8 | self.c) + 1) & 0xFFFF; self.b, self.c = v >> 8, v & 0xFF; self.cycles += 8; return True
        if op == 0x13: v = ((self.d << 8 | self.e) + 1) & 0xFFFF; self.d, self.e = v >> 8, v & 0xFF; self.cycles += 8; return True
        if op == 0x23: v = ((self.h << 8 | self.l) + 1) & 0xFFFF; self.h, self.l = v >> 8, v & 0xFF; self.cycles += 8; return True
        if op == 0x0B: v = ((self.b << 8 | self.c) - 1) & 0xFFFF; self.b, self.c = v >> 8, v & 0xFF; self.cycles += 8; return True
        if op == 0x1B: v = ((self.d << 8 | self.e) - 1) & 0xFFFF; self.d, self.e = v >> 8, v & 0xFF; self.cycles += 8; return True
        if op == 0x2B: v = ((self.h << 8 | self.l) - 1) & 0xFFFF; self.h, self.l = v >> 8, v & 0xFF; self.cycles += 8; return True

        # inc/dec r
        if op in (0x04, 0x0C, 0x14, 0x1C, 0x24, 0x2C, 0x34, 0x3C):
            i = (op >> 3) & 7; v = (self.get_r(i) + 1) & 0xFF
            self.set_r(i, v); self._setz(v == 0); self._setn(0); self._seth((v & 0x0F) == 0)
            self.cycles += 12 if i == 6 else 4; return True
        if op in (0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D):
            i = (op >> 3) & 7; v = (self.get_r(i) - 1) & 0xFF
            self.set_r(i, v); self._setz(v == 0); self._setn(1); self._seth((v & 0x0F) == 0x0F)
            self.cycles += 12 if i == 6 else 4; return True

        # ld [hl+], a  /  ld a, [hl+]
        if op == 0x22:
            hl = (self.h << 8) | self.l; self.wb(hl, self.a); hl = (hl + 1) & 0xFFFF
            self.h, self.l = hl >> 8, hl & 0xFF; self.cycles += 8; return True
        if op == 0x2A:
            hl = (self.h << 8) | self.l; self.a = self.rb(hl); hl = (hl + 1) & 0xFFFF
            self.h, self.l = hl >> 8, hl & 0xFF; self.cycles += 8; return True

        # ld a, [de]
        if op == 0x1A:
            self.a = self.rb((self.d << 8) | self.e); self.cycles += 8; return True

        # xor a / or r / and r / cp r
        if 0xA8 <= op <= 0xAF:
            self.a ^= self.get_r(op & 7); self.a &= 0xFF; self.f = 0x80 if self.a == 0 else 0
            self.cycles += 4; return True
        if 0xB0 <= op <= 0xB7:
            self.a |= self.get_r(op & 7); self.a &= 0xFF; self.f = 0x80 if self.a == 0 else 0
            self.cycles += 4; return True
        if 0xA0 <= op <= 0xA7:
            self.a &= self.get_r(op & 7); self.f = (0x80 if self.a == 0 else 0) | 0x20
            self.cycles += 4; return True
        if 0xB8 <= op <= 0xBF:
            r = self.get_r(op & 7); d = (self.a - r) & 0xFF
            self._setz(d == 0); self._setn(1); self._seth((self.a & 0xF) < (r & 0xF)); self._setc(self.a < r)
            self.cycles += 4; return True
        if op == 0xE6:
            self.a &= self.imm8(); self.f = (0x80 if self.a == 0 else 0) | 0x20; self.cycles += 8; return True
        if op == 0xF6:
            self.a |= self.imm8(); self.f = 0x80 if self.a == 0 else 0; self.cycles += 8; return True
        if op == 0xFE:
            r = self.imm8(); d = (self.a - r) & 0xFF
            self._setz(d == 0); self._setn(1); self._seth((self.a & 0xF) < (r & 0xF)); self._setc(self.a < r)
            self.cycles += 8; return True

        # jr / jr cc
        if op == 0x18:
            o = self.imm8(); o = o - 256 if o > 127 else o
            self.pc = (self.pc + o) & 0xFFFF; self.cycles += 12; return True
        if op in (0x20, 0x28, 0x30, 0x38):
            o = self.imm8(); o = o - 256 if o > 127 else o
            cond = {0x20: not self._zf(), 0x28: self._zf(),
                    0x30: not self._cf(), 0x38: self._cf()}[op]
            if cond: self.pc = (self.pc + o) & 0xFFFF; self.cycles += 12
            else:    self.cycles += 8
            return True

        # jp / call / ret
        if op == 0xC3: self.pc = self.imm16(); self.cycles += 16; return True
        if op == 0xCD: t = self.imm16(); self.push(self.pc); self.pc = t; self.cycles += 24; return True
        if op == 0xC9: self.pc = self.pop(); self.cycles += 16; return True
        if op == 0xC8:
            if self._zf(): self.pc = self.pop(); self.cycles += 20
            else: self.cycles += 8
            return True
        if op == 0xC0:
            if not self._zf(): self.pc = self.pop(); self.cycles += 20
            else: self.cycles += 8
            return True

        # push / pop
        if op == 0xC5: self.push((self.b << 8) | self.c); self.cycles += 16; return True
        if op == 0xD5: self.push((self.d << 8) | self.e); self.cycles += 16; return True
        if op == 0xE5: self.push((self.h << 8) | self.l); self.cycles += 16; return True
        if op == 0xF5: self.push((self.a << 8) | self.f); self.cycles += 16; return True
        if op == 0xC1: v = self.pop(); self.b, self.c = v >> 8, v & 0xFF; self.cycles += 12; return True
        if op == 0xD1: v = self.pop(); self.d, self.e = v >> 8, v & 0xFF; self.cycles += 12; return True
        if op == 0xE1: v = self.pop(); self.h, self.l = v >> 8, v & 0xFF; self.cycles += 12; return True
        if op == 0xF1: v = self.pop(); self.a, self.f = v >> 8, v & 0xF0; self.cycles += 12; return True

        # ldh
        if op == 0xE0: self.wb(0xFF00 + self.imm8(), self.a); self.cycles += 12; return True
        if op == 0xF0: self.a = self.rb(0xFF00 + self.imm8()); self.cycles += 12; return True
        if op == 0xE2: self.wb(0xFF00 + self.c, self.a); self.cycles += 8; return True
        if op == 0xF2: self.a = self.rb(0xFF00 + self.c); self.cycles += 8; return True
        if op == 0xEA: self.wb(self.imm16(), self.a); self.cycles += 16; return True
        if op == 0xFA: self.a = self.rb(self.imm16()); self.cycles += 16; return True

        # di / ei / nop
        if op in (0xF3, 0xFB, 0x00): self.cycles += 4; return True

        # CB prefix: swap r, sla r
        if op == 0xCB:
            cb = self.imm8(); i = cb & 7
            if 0x30 <= cb <= 0x37:
                v = self.get_r(i); v = ((v << 4) | (v >> 4)) & 0xFF
                self.set_r(i, v); self.f = 0x80 if v == 0 else 0
                self.cycles += 16 if i == 6 else 8; return True
            if 0x20 <= cb <= 0x27:
                v = self.get_r(i); c = (v >> 7) & 1; v = (v << 1) & 0xFF
                self.set_r(i, v); self.f = (0x80 if v == 0 else 0) | (0x10 if c else 0)
                self.cycles += 16 if i == 6 else 8; return True
            raise NotImplementedError(f"CB {cb:02X} at {self.pc-2:04X}")

        raise NotImplementedError(f"opcode {op:02X} at {(self.pc-1)&0xFFFF:04X}")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    rom = open(os.path.join(here, "chipboy_probe.gb"), "rb").read()
    cpu = SM83(rom)

    # Run until the ROM reaches its end-of-run flash loop. Detected by the
    # take table pointer running past the terminator, which the ROM signals by
    # reaching AllDone -- we spot it as "no new IO for a long stretch after the
    # last take", so instead bound by instruction count and stop on repeat.
    global TONES
    TONES = {}
    for line in open(os.path.join(here, "chipboy_probe.asm")):
        line = line.strip()
        if line.startswith("DEF TONE_"):
            name = line.split()[1]
            TONES[name] = int(line.split("EQU")[1].split(";")[0].strip())

    sym = {}
    for line in open(os.path.join(here, "chipboy_probe.sym")):
        line = line.split(";")[0].strip()
        if not line: continue
        bank_addr, name = line.split(None, 1)
        sym[name.strip()] = int(bank_addr.split(":")[1], 16)
    all_done = sym["AllDone"]
    cpu.delay_loop = sym["WaitMs.inner"]

    steps = 0
    while cpu.pc != all_done:
        cpu.step()
        steps += 1
        if steps > 40_000_000:
            print("FAIL: did not reach AllDone", file=sys.stderr); sys.exit(1)

    dur = cpu.cycles / CPU_HZ
    print(f"reached AllDone after {steps:,} instructions")
    print(f"emulated run time     {dur:8.2f} s  ({dur/60:.2f} min)")
    print(f"APU/LCDC/BGP writes   {len(cpu.io_log):,}")
    print(f"stack pointer         {cpu.sp:04X} (expect FFFE -- balanced)")
    if cpu.sp != 0xFFFE:
        print("FAIL: stack imbalance", file=sys.stderr); sys.exit(1)

    takes = segment(cpu)
    ids = [t["id"] for t in takes]
    if len(ids) != len(set(ids)):
        print("FAIL: duplicate take ids", file=sys.stderr); sys.exit(1)
    print(f"takes executed        {len(takes)}  ids {ids[0]}..{ids[-1]}")

    for t in takes:                       # keep the manifest small: the raw
        t.pop("writes", None)             # register writes are debug detail
        for k in ("marker_start_cycle", "payload_start_cycle", "payload_end_cycle"):
            t.pop(k, None)
    manifest = {
        "rom": "chipboy_probe.gb",
        "cpu_hz": CPU_HZ,
        "marker": {
            "preamble_hz": 131072 / (2048 - TONES["TONE_HI"]),
            "one_hz":      131072 / (2048 - TONES["TONE_ONE"]),
            "zero_hz":     131072 / (2048 - TONES["TONE_NIL"]),
            "preamble_reps": 2, "preamble_on_ms": 40, "preamble_off_ms": 40,
            "bits": 8, "bit_on_ms": 25, "bit_off_ms": 15, "trailer_ms": 120,
        },
        "lead_in_ms": 3000,
        "total_seconds": round(dur, 3),
        "takes": takes,
    }
    out = os.path.join(here, "takes.json")
    json.dump(manifest, open(out, "w"), indent=1)
    print(f"wrote {out}")
    return cpu, dur


def segment(cpu):
    """Split the executed IO log into takes using the BGP feedback writes:
    BGP=3 starts a marker, BGP=0 starts that take's payload."""
    takes, cur = [], None
    for cyc, addr, val in cpu.io_log:
        if addr != 0xFF47:
            if cur is not None and cur["payload_start_cycle"] is not None:
                cur["writes"].append([cyc, addr, val])
            continue
        if val == 3:                      # marker begins
            if cur is not None:
                cur["payload_end_cycle"] = cyc
                takes.append(cur)
            cur = {"id": None, "marker_start_cycle": cyc,
                   "payload_start_cycle": None, "payload_end_cycle": None,
                   "writes": []}
        elif val == 0 and cur is not None and cur["payload_start_cycle"] is None:
            cur["payload_start_cycle"] = cyc
    if cur is not None:
        cur["payload_end_cycle"] = cpu.io_log[-1][0]
        takes.append(cur)

    # Recover each take id from the marker's own tone sequence: after the two
    # preamble bursts, eight NR13 writes carry 1 or 0 by frequency.
    for t in takes:
        bits, seen = 0, 0
        for cyc, addr, val in cpu.io_log:
            if cyc < t["marker_start_cycle"]: continue
            if t["payload_start_cycle"] and cyc >= t["payload_start_cycle"]: break
            if addr == 0xFF13:
                if val == (TONES["TONE_HI"] & 0xFF): continue      # preamble tone
                bits = (bits << 1) | (1 if val == (TONES["TONE_ONE"] & 0xFF) else 0)
                seen += 1
        t["id"] = bits if seen == 8 else None
        t["marker_bits_seen"] = seen
        for k in ("marker_start_cycle", "payload_start_cycle", "payload_end_cycle"):
            t[k.replace("_cycle", "_s")] = round(t[k] / CPU_HZ, 6) if t[k] else None
        t["payload_seconds"] = (round((t["payload_end_cycle"] - t["payload_start_cycle"]) / CPU_HZ, 4)
                                if t["payload_start_cycle"] and t["payload_end_cycle"] else None)
    return takes


if __name__ == "__main__":
    main()
