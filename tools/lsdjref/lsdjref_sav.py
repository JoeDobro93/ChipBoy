#!/usr/bin/env python3
"""lsdjref-sav -- author LSDj save files for the parity harness.

Reads the declarative test spec (tools/lsdjref/cases.spec) and writes one
`.sav` per case: a save whose working-memory song is the case's song, so
pressing START on LSDj's song screen plays exactly that test.

The save-RAM layout is LSDj's, documented in the manual's SRAM chapter
(github.com/jkotlinski/lsdj-doc, `sram.tex`) and cross-checked against
liblsdj's `song_offsets.h` (stijnfrishert/liblsdj, MIT). Neither is copied
here: these are offsets, a fact table, and the code below is this harness's.

Nothing from the ROM is read or written. The base save is one LSDj itself
formatted -- capture it with

    lsdjref-trace --rom ROM --frames 3000 --init-sav base.sav

which boots with no save at all, lets LSDj run its cartridge test and lay out
its own SRAM, and saves the battery. Patching a copy of that means the version
byte, the defaults and every reserved field are this exact ROM build's rather
than a guess.

    lsdjref_sav.py --spec cases.spec --base base.sav --out DIR [--case NAME]
"""

import argparse
import os
import sys

# --- the song block (0x8000 bytes; a .sav starts with one) -----------------

SONG_BYTES = 0x8000
PHRASE_NOTES = 0x0000
GROOVES = 0x1090
CHAIN_ASSIGNMENTS = 0x1290
TABLE_ENVELOPES = 0x1690
RB1 = 0x1E78
TABLE_ALLOC = 0x2020
INSTRUMENT_ALLOC = 0x2040
CHAIN_PHRASES = 0x2080
CHAIN_TRANSPOSE = 0x2880
INSTRUMENT_PARAMS = 0x3080
TABLE_TRANSPOSE = 0x3480
TABLE_CMD1 = 0x3680
TABLE_CMD1_VALUE = 0x3880
TABLE_CMD2 = 0x3A80
TABLE_CMD2_VALUE = 0x3C80
RB2 = 0x3E80
PHRASE_ALLOC = 0x3E82
CHAIN_ALLOC = 0x3EA2
TEMPO = 0x3FB4
TRANSPOSE = 0x3FB5
PHRASE_COMMANDS = 0x4000
PHRASE_COMMAND_VALUES = 0x4FF0
WAVES = 0x6000
PHRASE_INSTRUMENTS = 0x7000
RB3 = 0x7FF0
FORMAT_VERSION = 0x7FFF

PHRASE_LEN = 16
TABLE_LEN = 16
CHAIN_LEN = 16
SAV_HEADER = 0x8000
SAV_INIT = SAV_HEADER + 318          # the two bytes 'jk'
SAV_ACTIVE_PROJECT = SAV_HEADER + 320 - 1

# The byte a phrase's (or a table's) command column holds. Measured on this
# ROM by writing each byte in turn and watching which registers moved: it is
# the letter's position in LSDj's own order, A first, with no gap for B.
# liblsdj shifts everything up by one from format 8 on and puts B at 1; that
# is not what 9.2.J stores, and using its table makes every V a W.
COMMANDS = ["A", "C", "D", "E", "F", "G", "H", "K", "L", "M", "O", "P",
            "R", "S", "T", "V", "W", "Z"]
CMD_BYTE = {}
for _i, _c in enumerate(COMMANDS):
    CMD_BYTE[_c] = _i + 1

CHANNELS = {"pu1": 0, "pu2": 1, "wav": 2, "noi": 3}

# LSDj's note column, measured on this ROM from the periods LSDj writes and
# recorded in docs/LSDJ_PARITY.md: byte 0 is an empty step and byte 1 sounds
# 65.4 Hz, which is MIDI 36. LSDj's own display calls that "C 3"; the spec
# names notes in scientific pitch, where middle C is C-4 = MIDI 60 = byte 25,
# so a spec note and a ChipBoy cell mean the same sound.
NOTE_ZERO_MIDI = 35
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def midi_of(name):
    """'C-4' / 'C#4' / 'A#-1' -> a MIDI note number (C-4 = 60)."""
    s = name.strip().upper().replace("-", " ").replace("  ", " ")
    # Accept C-4, C#4, C 4; the octave is whatever trails.
    i = 1
    if len(s) > 1 and s[1] == "#":
        i = 2
    pitch = s[:i]
    octave = s[i:].strip()
    if pitch not in NOTE_NAMES:
        raise ValueError("bad note %r" % name)
    return (int(octave) + 1) * 12 + NOTE_NAMES.index(pitch)


def lsdj_note(name):
    n = midi_of(name) - NOTE_ZERO_MIDI
    if not 1 <= n <= 0x6F:
        raise ValueError("note %s (%d) is outside LSDj's column" % (name, n))
    return n


class Song(object):
    """A 32 KB LSDj song block, built field by field."""

    def __init__(self):
        self.b = bytearray(SONG_BYTES)
        self.b[CHAIN_ASSIGNMENTS:CHAIN_ASSIGNMENTS + 0x400] = b"\xFF" * 0x400
        self.b[CHAIN_PHRASES:CHAIN_PHRASES + 0x800] = b"\xFF" * 0x800
        self.b[RB1:RB1 + 2] = b"rb"
        self.b[RB2:RB2 + 2] = b"rb"
        self.b[RB3:RB3 + 2] = b"rb"
        self.b[TEMPO] = 128
        self.b[TRANSPOSE] = 0
        self.set_groove(0, [6, 6])
        # Sixteen sensible waves so a wave instrument has something to play;
        # a plain ramp, which is what LSDj's own default wave is close to.
        for w in range(256):
            for i in range(16):
                self.b[WAVES + w * 16 + i] = 0x8E if i % 2 == 0 else 0xCA

    # --- global ---------------------------------------------------------
    def set_tempo(self, bpm):
        self.b[TEMPO] = bpm & 0xFF

    def set_groove(self, slot, ticks):
        base = GROOVES + slot * 16
        for i in range(16):
            self.b[base + i] = ticks[i] if i < len(ticks) else 0

    # --- phrases --------------------------------------------------------
    def alloc_phrase(self, p):
        self.b[PHRASE_ALLOC + p // 8] |= 1 << (p % 8)

    def set_note(self, p, step, note):
        self.b[PHRASE_NOTES + p * PHRASE_LEN + step] = note
        self.alloc_phrase(p)

    def set_instrument_col(self, p, step, inst):
        self.b[PHRASE_INSTRUMENTS + p * PHRASE_LEN + step] = inst

    def set_command(self, p, step, letter, value):
        i = p * PHRASE_LEN + step
        self.b[PHRASE_COMMANDS + i] = CMD_BYTE[letter]
        self.b[PHRASE_COMMAND_VALUES + i] = value & 0xFF
        self.alloc_phrase(p)

    # --- chains ---------------------------------------------------------
    def alloc_chain(self, c):
        self.b[CHAIN_ALLOC + c // 8] |= 1 << (c % 8)

    def set_chain_step(self, c, step, phrase, transpose=0):
        self.b[CHAIN_PHRASES + c * CHAIN_LEN + step] = phrase
        self.b[CHAIN_TRANSPOSE + c * CHAIN_LEN + step] = transpose & 0xFF
        self.alloc_chain(c)

    def set_song_row(self, row, channel, chain):
        self.b[CHAIN_ASSIGNMENTS + row * 4 + channel] = chain

    # --- instruments ----------------------------------------------------
    def set_instrument(self, slot, data):
        base = INSTRUMENT_PARAMS + slot * 16
        for i, v in enumerate(data):
            self.b[base + i] = v & 0xFF
        self.b[INSTRUMENT_ALLOC + slot] = 1

    # --- tables ---------------------------------------------------------
    def alloc_table(self, t):
        self.b[TABLE_ALLOC + t] = 1

    def set_table_row(self, t, row, env=0, transpose=0, c1=None, c2=None):
        i = t * TABLE_LEN + row
        self.b[TABLE_ENVELOPES + i] = env & 0xFF
        self.b[TABLE_TRANSPOSE + i] = transpose & 0xFF
        if c1:
            self.b[TABLE_CMD1 + i] = CMD_BYTE[c1[0]]
            self.b[TABLE_CMD1_VALUE + i] = c1[1] & 0xFF
        if c2:
            self.b[TABLE_CMD2 + i] = CMD_BYTE[c2[0]]
            self.b[TABLE_CMD2_VALUE + i] = c2[1] & 0xFF
        self.alloc_table(t)


# --- instruments the spec can name -----------------------------------------

def pulse_instrument(env=0xF0, duty=2, pitch="fast", table=None, cmdrate=0,
                     vib_shape=0, vib_dir=0, sweep=0x00, out=3, env2=0x00,
                     env3=0x00, table_step=False, transpose=True, pu2tsp=0,
                     finetune=0, length=None):
    """The sixteen bytes of a pulse instrument (manual, table 'Pulse Instrument
    SRAM Layout')."""
    b5 = 0
    if pitch == "step":
        b5 |= 0x80
    elif pitch == "drum":
        b5 |= 0x40
    elif pitch == "tick":
        b5 |= 0x10
    if not transpose:
        b5 |= 0x20
    if table_step:
        b5 |= 0x08
    b5 |= (vib_shape & 3) << 1
    b5 |= vib_dir & 1
    b6 = 0x20 | (table & 0x1F) if table is not None else 0x00
    b3 = 0x00 if length is None else (0x40 | (length & 0x3F))
    return [0x00, env, pu2tsp, b3, sweep, b5, b6,
            ((duty & 3) << 6) | (out & 3), cmdrate & 0x0F, env2, env3,
            finetune, 0, 0, 0, 0]


def wave_instrument(volume=3, wave=0, synth=0, pitch="fast", table=None,
                    cmdrate=0, vib_shape=0, vib_dir=0, out=3, playtype=2,
                    length=0x0F, speed=4, table_step=False, transpose=True,
                    loop_pos=0, finetune=0):
    """The sixteen bytes of a wave instrument."""
    b5 = 0
    if pitch == "step":
        b5 |= 0x80
    elif pitch == "drum":
        b5 |= 0x40
    elif pitch == "tick":
        b5 |= 0x10
    if not transpose:
        b5 |= 0x20
    if table_step:
        b5 |= 0x08
    b5 |= (vib_shape & 3) << 1
    b5 |= vib_dir & 1
    b6 = 0x20 | (table & 0x1F) if table is not None else 0x00
    return [0x01, (volume & 3) << 5, loop_pos & 0x0F,
            ((synth & 0x0F) << 4) | (wave & 0x0F), 0, b5, b6, out & 3,
            cmdrate & 0x0F, playtype & 3, length & 0x0F, speed & 0xFF,
            finetune, 0, 0, 0]


def noise_instrument(env=0xF0, safe=False, table=None, cmdrate=0, vib_shape=0,
                     vib_dir=0, out=3, env2=0x00, env3=0x00, table_step=False,
                     transpose=True, length=None):
    """The sixteen bytes of a noise instrument."""
    b5 = 0
    if not transpose:
        b5 |= 0x20
    if table_step:
        b5 |= 0x08
    b5 |= (vib_shape & 3) << 1
    b5 |= vib_dir & 1
    b6 = 0x20 | (table & 0x1F) if table is not None else 0x00
    b3 = 0x00 if length is None else (0x40 | (length & 0x3F))
    return [0x03, env, 0x01 if safe else 0x00, b3, 0, b5, b6, out & 3,
            cmdrate & 0x0F, env2, env3, 0, 0, 0, 0, 0]


# --- the spec ---------------------------------------------------------------

class Case(object):
    def __init__(self, name):
        self.name = name
        self.desc = ""
        self.frames = 900
        self.models = ["dmg"]
        self.tempo = 120
        self.instruments = {}     # slot -> (kind, kwargs)
        self.tables = {}          # slot -> list of row dicts
        self.grooves = {}         # slot -> ticks
        self.phrases = {}         # slot -> list of row dicts
        self.chains = {}          # channel index -> [phrase slots in order]
        self.notes = []           # free-text findings the report carries


def _kv(tokens):
    out = {}
    for t in tokens:
        if "=" not in t:
            raise ValueError("expected key=value, got %r" % t)
        k, v = t.split("=", 1)
        out[k] = v
    return out


def _int(v):
    v = v.strip()
    if v.lower().startswith("0x"):
        return int(v, 16)
    if len(v) == 2 and all(c in "0123456789abcdefABCDEF" for c in v):
        return int(v, 16)
    return int(v, 0)


def strip_comment(raw):
    """A `#` opens a comment only at the start of a word, so a sharp inside a
    note name (`A#4`) survives."""
    for i, ch in enumerate(raw):
        if ch == "#" and (i == 0 or raw[i - 1].isspace()):
            return raw[:i].strip()
    return raw.strip()


def parse_spec(path):
    """The spec is line oriented: a `case` line opens a case and every line
    after it belongs to it until the next `case`."""
    cases = []
    cur = None
    ctx = None          # ('table', slot) or ('phrase', slot)
    with open(path) as f:
        for lineno, raw in enumerate(f, 1):
            line = strip_comment(raw)
            if not line:
                continue
            tok = line.split()
            head = tok[0]
            try:
                if head == "case":
                    cur = Case(tok[1])
                    cases.append(cur)
                    ctx = None
                    continue
                if cur is None:
                    raise ValueError("%s outside a case" % head)
                if head == "desc":
                    cur.desc = " ".join(tok[1:])
                elif head == "note":
                    cur.notes.append(" ".join(tok[1:]))
                elif head == "frames":
                    cur.frames = int(tok[1])
                elif head == "models":
                    cur.models = tok[1:]
                elif head == "tempo":
                    cur.tempo = int(tok[1])
                elif head == "groove":
                    cur.grooves[int(tok[1])] = [int(x) for x in tok[2:]]
                elif head == "inst":
                    cur.instruments[int(tok[1])] = (tok[2], _kv(tok[3:]))
                elif head == "table":
                    slot = int(tok[1])
                    cur.tables[slot] = []
                    ctx = ("table", slot)
                elif head == "phrase":
                    slot = int(tok[1])
                    cur.phrases[slot] = []
                    ctx = ("phrase", slot)
                elif head == "row":
                    if ctx is None:
                        raise ValueError("row outside a table or phrase")
                    d = _kv(tok[2:])
                    d["step"] = int(tok[1])
                    (cur.tables if ctx[0] == "table" else cur.phrases)[ctx[1]].append(d)
                elif head == "chain":
                    cur.chains[CHANNELS[tok[1]]] = [int(x) for x in tok[2:]]
                else:
                    raise ValueError("unknown line %r" % head)
            except Exception as exc:
                raise SystemExit("%s:%d: %s" % (path, lineno, exc))
    return cases


def _cmd(field):
    """`V:48` -> ('V', 0x48); `-` -> None."""
    if not field or field == "-":
        return None
    letter, _, value = field.partition(":")
    letter = letter.upper()
    if letter not in CMD_BYTE:
        raise ValueError("unknown command %r" % letter)
    return (letter, _int(value) if value else 0)


def build(case):
    s = Song()
    s.set_tempo(case.tempo)
    for slot, ticks in case.grooves.items():
        s.set_groove(slot, ticks)

    for slot, (kind, kw) in case.instruments.items():
        args = {}
        for k, v in kw.items():
            if k in ("pitch",):
                args[k] = v
            elif k in ("table_step", "transpose", "safe"):
                args[k] = v.lower() in ("1", "on", "yes", "true")
            elif k == "table" and v == "-":
                args[k] = None
            else:
                args[k] = _int(v)
        if kind == "pulse":
            s.set_instrument(slot, pulse_instrument(**args))
        elif kind == "wave":
            s.set_instrument(slot, wave_instrument(**args))
        elif kind == "noise":
            s.set_instrument(slot, noise_instrument(**args))
        else:
            raise SystemExit("unknown instrument kind %r" % kind)

    for slot, rows in case.tables.items():
        s.alloc_table(slot)
        for r in rows:
            s.set_table_row(slot, r["step"],
                            env=_int(r.get("env", "0")),
                            transpose=_int(r.get("tsp", "0")),
                            c1=_cmd(r.get("c1")), c2=_cmd(r.get("c2")))

    for slot, rows in case.phrases.items():
        s.alloc_phrase(slot)
        for r in rows:
            step = r["step"]
            if "n" in r:
                s.set_note(slot, step, lsdj_note(r["n"]))
            if "i" in r:
                s.set_instrument_col(slot, step, _int(r["i"]))
            c = _cmd(r.get("c"))
            if c:
                s.set_command(slot, step, c[0], c[1])

    # Phrases go into chains of sixteen, chains into song rows, in order:
    # pressing START at the top of the song screen plays the case through.
    for ch, phrases in case.chains.items():
        for i, p in enumerate(phrases):
            chain = ch * 16 + i // CHAIN_LEN
            s.set_chain_step(chain, i % CHAIN_LEN, p)
            s.set_song_row(i // CHAIN_LEN, ch, chain)
    return s


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--spec", default=os.path.join(os.path.dirname(__file__), "cases.spec"))
    ap.add_argument("--base", help="a save LSDj itself formatted")
    ap.add_argument("--out", help="directory for the .sav files")
    ap.add_argument("--case", action="append", help="only these cases")
    ap.add_argument("--list", action="store_true", help="print the case names and stop")
    args = ap.parse_args()

    cases = parse_spec(args.spec)
    if args.case:
        wanted = set(args.case)
        cases = [c for c in cases if c.name in wanted]
        missing = wanted - {c.name for c in cases}
        if missing:
            raise SystemExit("no such case: %s" % ", ".join(sorted(missing)))
    if args.list:
        for c in cases:
            print("%-18s %s" % (c.name, c.desc))
        return 0
    if not args.base or not args.out:
        raise SystemExit("--base and --out are needed to write saves")

    with open(args.base, "rb") as f:
        base = bytearray(f.read())
    if len(base) < SONG_BYTES + 512:
        raise SystemExit("%s is not an LSDj save (%d bytes)" % (args.base, len(base)))
    if bytes(base[SAV_INIT:SAV_INIT + 2]) != b"jk":
        raise SystemExit("%s has no 'jk' marker: LSDj never formatted it" % args.base)

    os.makedirs(args.out, exist_ok=True)
    for c in cases:
        sav = bytearray(base)
        sav[0:SONG_BYTES] = build(c).b
        sav[SAV_ACTIVE_PROJECT] = 0xFF          # nothing loaded from a slot
        path = os.path.join(args.out, c.name + ".sav")
        with open(path, "wb") as f:
            f.write(sav)
        print("%-18s %s" % (c.name, path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
