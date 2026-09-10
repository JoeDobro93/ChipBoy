"""Build a controlled probe song inside a REAL format-22 LSDj save.

Section 58/63 of docs/COMMANDS_AND_TEMPO.md: a save built from nothing and never
opened in LSDj's editor misreports table commands.  So we start from a real
song written by the editor, keep every structure it built (allocation tables,
instruments, waves, kits) and overwrite only the phrase, chain, table and
instrument we are probing -- then drop the whole image into the save's working
area, which is the path LSDj takes for a song being edited.
"""
import os

LSDJ_DIR = os.environ.get('CHIPBOY_LSDJ_DIR', '/root/lsdj')

SONG = 0x1290; CHAIN = 0x2080; CHAINTSP = 0x2880
NOTE = 0x0000; CMD = 0x4000; CMDV = 0x4FF0; INS = 0x7000
TENV = 0x1690; TTSP = 0x3480; TC1 = 0x3680; TC1V = 0x3880; TC2 = 0x3A80; TC2V = 0x3C80
IP = 0x3080; TEMPO = 0x3FB4; STSP = 0x3FB5; GROOVE = 0x1090
PHRASE_ALLOC = 0x3E82; INST_ALLOC = 0x2040; TALLOC = 0x2020
LETTERS = "-ABCDEFGHKLMOPRSTVWZ"          # format >= 11 (B present)

# A real, editor-written save in the format being probed.  It must be one LSDj
# itself wrote: a save built from nothing misreports table commands (section 58
# and 63 of docs/COMMANDS_AND_TEMPO.md), and the ROM's own --init-sav output is
# blank, so there is no way to bootstrap this from the ROM alone.
HOST = os.environ.get('CHIPBOY_LSDJ_HOST_SAV', os.path.join(LSDJ_DIR, 'kits.sav'))
HOST_IDX = int(os.environ.get('CHIPBOY_LSDJ_HOST_IDX', '0'))
# Slots the host song already allocates, so the allocation bitmaps stay true --
# writing into a slot the editor never made is the section 58 trap.
INST = {0: 0x03, 1: 0x03, 2: 0x00, 3: 0x05}   # per channel: pulse, pulse, wave, noise
TABLE = 0x04
TALLOC = 0x2020

_WAVE = bytes([0x8E,0xCD,0xCC,0xBB,0xAA,0xA9,0x99,0x88,0x87,0x76,0x66,0x55,0x54,0x43,0x32,0x31])
_INST = bytes([0xA8,0x00,0x00,0xFF,0x00,0x00,0x03,0x00,0x00,0xD0,0x00,0x00,0x00,0xF3,0x00,0x00])
_ALLOC = 0x8141; _BLK0 = 0x8000; _BSZ = 0x200; _NBLK = 191

def decompress(sav, file_idx):
    """One song out of a .sav.  Block indices are 1-based against the alloc table."""
    blk = next((b + 1 for b in range(_NBLK) if sav[_ALLOC + b] == file_idx), None)
    if blk is None: raise SystemExit('no song %d in that save' % file_idx)
    out = bytearray(); i = _BLK0 + blk * _BSZ
    while True:
        c = sav[i]; i += 1
        if c == 0xC0:
            v = sav[i]; i += 1
            if v == 0xC0: out.append(0xC0)
            else: n = sav[i]; i += 1; out += bytes([v]) * n
        elif c == 0xE0:
            v = sav[i]; i += 1
            if v == 0xE0: out.append(0xE0)
            elif v in (0xF0, 0xF1): n = sav[i]; i += 1; out += (_WAVE if v == 0xF0 else _INST) * n
            elif v == 0xFF: break
            else: i = _BLK0 + v * _BSZ
        else: out.append(c)
    return bytes(out)

def songs(path):
    """(index, name, blocks) for every song in a save -- to pick a host."""
    sav = open(path, 'rb').read()
    out = []
    for i in range(32):
        name = ''.join(chr(c) for c in sav[0x8000 + i * 8:0x8000 + i * 8 + 8] if 32 <= c < 127).strip()
        n = sum(1 for b in range(_NBLK) if sav[_ALLOC + b] == i)
        if n: out.append((i, name, n))
    return out

def code(letter):
    """The command byte LSDj stores for a letter, in this format."""
    return LETTERS.index(letter)

class Probe:
    def __init__(self, host=None, idx=None, want_format=22, blank=False):
        self.sav = bytearray(open(host or HOST, 'rb').read())
        if blank:
            # A save the ROM formatted itself (`lsdjref_trace --init-sav`, given
            # ~3000 frames to finish): its working song is empty but valid, so
            # the song row, chain, phrase and allocation bits are ours to set.
            self.s = bytearray(self.sav[0:0x8000])
            self.blank = True
        else:
            self.s = bytearray(decompress(self.sav, HOST_IDX if idx is None else idx))
            self.blank = False
        if want_format is not None and self.s[0x7FFF] != want_format:
            raise SystemExit('host song is format %d, expected %d -- point '
                             'CHIPBOY_LSDJ_HOST_SAV at a save written by this version'
                             % (self.s[0x7FFF], want_format))
        # The song row 0 chains, and the first phrase of each, read off the host
        # itself so any real save can serve.
        global HOST_CHAIN, HOST_PHRASE
        if self.blank:
            # One chain and one phrase per channel, allocated as we go.
            HOST_CHAIN = [0x00, 0x01, 0x02, 0x03]
            HOST_PHRASE = [0x00, 0x01, 0x02, 0x03]
            for c in range(4):
                self.allocPhrase(HOST_PHRASE[c])
        else:
            HOST_CHAIN = [self.s[SONG + c] for c in range(4)]
            HOST_PHRASE = [self.s[CHAIN + self.s[SONG + c] * 16] if self.s[SONG + c] != 0xFF else 0
                           for c in range(4)]

    def allocPhrase(self, p):
        self.s[PHRASE_ALLOC + p // 8] |= 1 << (p % 8)

    def allocInst(self, i):
        self.s[INST_ALLOC + i] = 1

    # -- song ---------------------------------------------------------------
    def only(self, ch):
        """Play one channel from song row 0, and end the song after that row."""
        for c in range(4):
            self.s[SONG + c] = HOST_CHAIN[c] if c == ch else 0xFF
        for r in range(1, 8):
            for c in range(4):
                self.s[SONG + r * 4 + c] = 0xFF
        chain = HOST_CHAIN[ch]
        for i in range(16):
            self.s[CHAIN + chain * 16 + i] = HOST_PHRASE[ch] if i == 0 else 0xFF
            self.s[CHAINTSP + chain * 16 + i] = 0
        return HOST_PHRASE[ch]

    def tempo(self, bpm=128, groove=(6, 6)):
        self.s[TEMPO] = bpm
        self.s[STSP] = 0
        for i in range(16):
            self.s[GROOVE + i] = groove[i] if i < len(groove) else 0

    # -- phrase -------------------------------------------------------------
    def phrase(self, p, rows):
        """rows: {step: (note, inst, 'L', value)}; note/inst None to leave blank."""
        o = p * 16
        for i in range(16):
            self.s[NOTE + o + i] = 0
            self.s[INS + o + i] = 0xFF
            self.s[CMD + o + i] = 0
            self.s[CMDV + o + i] = 0
        for step, (note, inst, letter, val) in rows.items():
            if note is not None: self.s[NOTE + o + step] = note
            if inst is not None: self.s[INS + o + step] = inst
            if letter: self.s[CMD + o + step] = code(letter); self.s[CMDV + o + step] = val

    # -- table --------------------------------------------------------------
    def table(self, t, rows):
        self.s[TALLOC + t] = self.s[TALLOC + t] or 1        # keep the slot allocated
        """rows: {step: dict(env=, tsp=, c1=('L',v), c2=('L',v))}"""
        o = t * 16
        for i in range(16):
            self.s[TENV + o + i] = 0; self.s[TTSP + o + i] = 0
            self.s[TC1 + o + i] = 0; self.s[TC1V + o + i] = 0
            self.s[TC2 + o + i] = 0; self.s[TC2V + o + i] = 0
        for step, r in rows.items():
            self.s[TENV + o + step] = r.get('env', 0)
            self.s[TTSP + o + step] = r.get('tsp', 0)
            for key, cc, vv in (('c1', TC1, TC1V), ('c2', TC2, TC2V)):
                if key in r:
                    letter, val = r[key]
                    self.s[cc + o + step] = code(letter); self.s[vv + o + step] = val

    # -- instrument ---------------------------------------------------------
    def pulse(self, i, env=0xF0, duty=2, pan=3, table=None, sweep=0x00, finetune=0):
        b = IP + i * 16
        for k in range(16): self.s[b + k] = 0
        self.allocInst(i)
        self.s[b + 0] = 0                                   # type: pulse
        self.s[b + 1] = env                                 # NRx2-shaped envelope
        self.s[b + 4] = (~sweep) & 0xFF                     # stored inverted
        self.s[b + 5] = 0x00                                # no vib, tick table, transpose on
        self.s[b + 6] = (0x20 | (table & 0x1F)) if table is not None else 0
        self.s[b + 7] = ((duty & 3) << 6) | (pan & 3)
        self.s[b + 11] = finetune
        return i

    def wave(self, i, env=0x03, pan=3, table=None, synth=0, loop=0):
        b = IP + i * 16
        for k in range(16): self.s[b + k] = 0
        self.allocInst(i)
        self.s[b + 0] = 1
        self.s[b + 1] = env
        self.s[b + 3] = ((synth & 15) << 4) | (loop & 15)   # 9.x: byte 3 (section 60)
        self.s[b + 5] = 0x00
        self.s[b + 6] = (0x20 | (table & 0x1F)) if table is not None else 0
        self.s[b + 7] = pan & 3
        return i

    def noise(self, i, env=0xF0, pan=3, table=None, shape=0xFF):
        b = IP + i * 16
        for k in range(16): self.s[b + k] = 0
        self.allocInst(i)
        self.s[b + 0] = 3
        self.s[b + 1] = env
        self.s[b + 4] = shape
        self.s[b + 5] = 0x00
        self.s[b + 6] = (0x20 | (table & 0x1F)) if table is not None else 0
        self.s[b + 7] = pan & 3
        return i

    def write(self, path):
        self.sav[0:0x8000] = self.s
        self.sav[0x8140] = 0xFF          # boot the working song
        open(path, 'wb').write(bytes(self.sav))
        return path
