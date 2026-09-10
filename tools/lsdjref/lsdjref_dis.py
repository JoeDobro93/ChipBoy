"""A minimal SM83 disassembler, for reading an LSDj ROM in place.

Rule L3 allows deriving behaviour from the ROM (docs/CHIPBOY_SPEC.md 3.3): what
ChipBoy ships is its own code producing the same result, and the address that
answered a question is worth recording. Nothing of the ROM is ever committed --
this tool reads one the user owns, from outside the tree.

    python3 tools/lsdjref/lsdjref_dis.py /root/lsdj/lsdj9_3_9.gb 2 4828 14
                                          ROM                  bank addr count

Bank 0 is $0000-$3FFF and every other bank is $4000-$7FFF, so a bank-2 address
of $4828 is file offset 2 * 0x4000 + (0x4828 - 0x4000). Register names are
filled in for the sound block and the timer, which is most of what matters
here.

**Where to start on a 9.x ROM.** The command jump table is at bank 02:$47A2 --
twenty little-endian words indexed by the letter's code in `-ABCDEFGHKLMOPRSTVWZ`
-- dispatched from $478D. `B`, `D`, `G`, `H` and `Z` have no entry: they are
handled where the row is read, not where a command is run.

    python3 tools/lsdjref/lsdjref_dis.py ROM 2 478D 20      # the dispatcher
    python3 tools/lsdjref/lsdjref_dis.py ROM 2 4812 12      # S, per channel
    python3 tools/lsdjref/lsdjref_dis.py ROM 2 46D8 20      # E, per channel

To find a handler you do not have an address for, use `lsdjref_pc.cpp` beside
this file: it is the trace tool plus the PC and ROM bank of every write, and
`--watch` extends it to a work-RAM address, which turns "what wrote that" into
one line of output.
"""
import sys
R8  = ['b','c','d','e','h','l','[hl]','a']
R16 = ['bc','de','hl','sp']
R16S= ['bc','de','hl','af']
CC  = ['nz','z','nc','c']
ALU = ['add a,','adc a,','sub ','sbc a,','and ','xor ','or ','cp ']
CB  = ['rlc','rrc','rl','rr','sla','sra','swap','srl']
IO  = {0x04:'DIV',0x05:'TIMA',0x06:'TMA',0x07:'TAC',0x10:'NR10',0x11:'NR11',0x12:'NR12',
       0x13:'NR13',0x14:'NR14',0x16:'NR21',0x17:'NR22',0x18:'NR23',0x19:'NR24',
       0x1A:'NR30',0x1B:'NR31',0x1C:'NR32',0x1D:'NR33',0x1E:'NR34',0x20:'NR41',
       0x21:'NR42',0x22:'NR43',0x23:'NR44',0x24:'NR50',0x25:'NR51',0x26:'NR52'}
def hio(n): return IO.get(n, '$FF%02X' % n)

def one(rom, o, pc):
    """-> (text, length)"""
    b = rom[o]
    u8  = rom[o+1] if o+1 < len(rom) else 0
    i8  = u8 - 256 if u8 > 127 else u8
    u16 = (rom[o+2] << 8 | rom[o+1]) if o+2 < len(rom) else 0
    x, y, z = b >> 6, (b >> 3) & 7, b & 7
    if b == 0x00: return 'nop', 1
    if b == 0x08: return 'ld [$%04X],sp' % u16, 3
    if b == 0x10: return 'stop', 2
    if b == 0x18: return 'jr $%04X' % ((pc + 2 + i8) & 0xFFFF), 2
    if x == 0 and z == 0 and y >= 4: return 'jr %s,$%04X' % (CC[y-4], (pc + 2 + i8) & 0xFFFF), 2
    if x == 0 and z == 1: return ('ld %s,$%04X' % (R16[y>>1], u16), 3) if not (y & 1) else ('add hl,%s' % R16[y>>1], 1)
    if x == 0 and z == 2:
        t = ['[bc]','[de]','[hl+]','[hl-]'][y>>1]
        return ('ld %s,a' % t, 1) if not (y & 1) else ('ld a,%s' % t, 1)
    if x == 0 and z == 3: return ('inc %s' if not (y & 1) else 'dec %s') % R16[y>>1], 1
    if x == 0 and z == 4: return 'inc %s' % R8[y], 1
    if x == 0 and z == 5: return 'dec %s' % R8[y], 1
    if x == 0 and z == 6: return 'ld %s,$%02X' % (R8[y], u8), 2
    if x == 0 and z == 7: return ['rlca','rrca','rla','rra','daa','cpl','scf','ccf'][y], 1
    if b == 0x76: return 'halt', 1
    if x == 1: return 'ld %s,%s' % (R8[y], R8[z]), 1
    if x == 2: return '%s%s' % (ALU[y], R8[z]), 1
    if x == 3:
        if z == 0:
            if y < 4: return 'ret %s' % CC[y], 1
            if y == 4: return 'ldh [%s],a' % hio(u8), 2
            if y == 5: return 'add sp,%d' % i8, 2
            if y == 6: return 'ldh a,[%s]' % hio(u8), 2
            return 'ld hl,sp%+d' % i8, 2
        if z == 1:
            if not (y & 1): return 'pop %s' % R16S[y>>1], 1
            return ['ret','reti','jp hl','ld sp,hl'][y>>1], 1
        if z == 2:
            if y < 4: return 'jp %s,$%04X' % (CC[y], u16), 3
            return ['ld [$FF00+c],a','ld [$%04X],a' % u16,'ld a,[$FF00+c]','ld a,[$%04X]' % u16][y-4], (3 if y in (5,7) else 1)
        if z == 3:
            if y == 0: return 'jp $%04X' % u16, 3
            if y == 1:
                c = rom[o+1]; return '%s %s' % (CB[(c>>3)&7] if c < 0x40 else
                    ['bit','res','set'][(c>>6)-1] + (' %d,' % ((c>>3)&7)), R8[c&7]), 2
            if y == 6: return 'di', 1
            if y == 7: return 'ei', 1
            return 'db $%02X' % b, 1
        if z == 4 and y < 4: return 'call %s,$%04X' % (CC[y], u16), 3
        if z == 5:
            if not (y & 1): return 'push %s' % R16S[y>>1], 1
            if y == 1: return 'call $%04X' % u16, 3
            return 'db $%02X' % b, 1
        if z == 6: return '%s$%02X' % (ALU[y], u8), 2
        if z == 7: return 'rst $%02X' % (y * 8), 1
    return 'db $%02X' % b, 1

def dis(rom, bank, addr, n=32, labels=None):
    o = bank * 0x4000 + (addr - 0x4000) if addr >= 0x4000 else addr
    pc = addr; out = []
    for _ in range(n):
        t, L = one(rom, o, pc)
        raw = ' '.join('%02X' % c for c in rom[o:o+L])
        out.append('  %02X:%04X  %-8s  %s' % (bank, pc, raw, t))
        o += L; pc += L
    return '\n'.join(out)

if __name__ == '__main__':
    rom = open(sys.argv[1], 'rb').read()
    print(dis(rom, int(sys.argv[2], 16), int(sys.argv[3], 16), int(sys.argv[4]) if len(sys.argv) > 4 else 32))
