"""Read LSDj's 8x8 tile text off a screen dump, so a probe can check what the
editor *shows* and not only what it plays.

`lsdjref_trace --screen FILE.pgm` writes the LCD at the end of a run, and the
key script (`--keys F:KEY[:HOLD],...`, two keys at one frame for a chord) walks
LSDj to the screen worth reading. LSDj takes a while to boot: nothing is drawn
before about frame 150 here, so press no earlier than 200.

    lsdjref_trace --rom ROM --sav PROBE.sav --frames 400 \
        --keys 200:right:4,215:right:4,230:right:4,\
260:select:8,262:right:4,300:select:8,302:right:4 \
        --out /dev/null --screen shot.pgm
    python3 lsdjref_ocr.py shot.pgm learn

That key script is song screen -> three columns right (to NOI) -> SELECT+RIGHT
twice, which lands on the phrase screen of the noise channel's first phrase.

The glyphs are learned from the screen itself: a phrase screen's row labels are
0-F down the left column, which gives every hex digit, and `learn` uses them.
Anything else prints as `?`; name it by reading the tile out of `tiles()`.
Inverted tiles (the cursor, the headers) are matched against the same library.

This reads pixels the emulator drew. Nothing of the ROM's contents leaves the
tool: the output is what the screen showed, which is a measurement of behaviour
(docs/COMMANDS_AND_TEMPO.md section 31).
"""
import sys

def tiles(path):
    """(tx, ty) -> an 8-tuple of 8-character rows, '#' for a dark pixel."""
    d = open(path, 'rb').read(); i = d.index(b'255\n') + 4; px = d[i:]
    W = 160
    out = {}
    for ty in range(18):
        for tx in range(20):
            bits = []
            for y in range(8):
                bits.append(''.join('#' if px[(ty * 8 + y) * W + tx * 8 + x] < 128 else '.' for x in range(8)))
            out[(tx, ty)] = tuple(bits)
    return out

def inv(t):
    return tuple(''.join('.' if c == '#' else '#' for c in r) for r in t)

def read(path, learn=True):
    """The screen as 18 lines of 20 characters, unknown glyphs as '?'."""
    t = tiles(path)
    lib = {}
    if learn:
        for ty in range(2, 18):                       # a phrase screen's row labels
            lib[inv(t[(0, ty)])] = '0123456789ABCDEF'[ty - 2]
    lines = []
    for ty in range(18):
        row = []
        for tx in range(20):
            g = t[(tx, ty)]
            if all(set(r) <= {'.'} for r in g) or all(set(r) <= {'#'} for r in g):
                row.append(' '); continue
            row.append(lib.get(g) or lib.get(inv(g)) or '?')
        lines.append(''.join(row))
    return lines

if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit('usage: lsdjref_ocr.py SHOT.pgm [learn]')
    for n, line in enumerate(read(sys.argv[1], len(sys.argv) > 2 and sys.argv[2] == 'learn')):
        print('%2d |%s|' % (n, line))
