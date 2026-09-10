"""Trace a probe save on a ROM and print the writes that matter.

Paths point outside the tree on purpose (rule L3): ROMs and saves are the
user's own and are never committed.
"""
import csv, math, subprocess, sys, os
HZ = 4194304.0
_ROOT = os.environ.get('CHIPBOY_ROOT', os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
_LSDJ = os.environ.get('CHIPBOY_LSDJ_DIR', '/root/lsdj')
TRACE = os.environ.get('CHIPBOY_LSDJREF_TRACE', os.path.join(_ROOT, 'build-ref/lsdjref/lsdjref_trace'))
BOOT = os.environ.get('CHIPBOY_BOOTROMS', os.path.join(_ROOT, 'build-ref/lsdjref/BootROMs'))
# `rom` may be a key here, a bare filename inside CHIPBOY_LSDJ_DIR, or a path.
ROM = {'939': os.path.join(_LSDJ, 'lsdj9_3_9.gb'), '844': os.path.join(_LSDJ, 'lsdj8_4_4.gb'),
       '886': os.path.join(_LSDJ, 'lsdj8_8_6.gb'), '92J': os.path.join(_LSDJ, 'lsdj9_2_J.gb')}
DEFAULT_ROM = os.environ.get('CHIPBOY_LSDJ_ROM', '939')
NAMES = {0xFF10:'NR10',0xFF11:'NR11',0xFF12:'NR12',0xFF13:'NR13',0xFF14:'NR14',
         0xFF16:'NR21',0xFF17:'NR22',0xFF18:'NR23',0xFF19:'NR24',
         0xFF1A:'NR30',0xFF1B:'NR31',0xFF1C:'NR32',0xFF1D:'NR33',0xFF1E:'NR34',
         0xFF20:'NR41',0xFF21:'NR42',0xFF22:'NR43',0xFF23:'NR44',
         0xFF24:'NR50',0xFF25:'NR51',0xFF26:'NR52'}

def romPath(rom):
    if rom in ROM: return ROM[rom]
    return rom if os.path.sep in rom else os.path.join(_LSDJ, rom)

def trace(sav, out, rom=None, frames=420):
    subprocess.run([TRACE, '--rom', romPath(rom or DEFAULT_ROM), '--bootrom-dir', BOOT, '--model', 'dmg',
                    '--sav', sav, '--frames', str(frames), '--keys', '180:start',
                    '--out', out], check=True, capture_output=True)
    return out

def rows(path):
    for r in csv.DictReader((l for l in open(path) if not l.startswith('#'))):
        yield int(r['cycle']), int(r['addr'], 16), int(r['value'], 16)

def playStart(path):
    """The cycle at which LSDj starts the song.

    Pressing START makes LSDj power-cycle the APU -- `NR52 = 00` then
    `NR52 = 80` -- reset DIV, build NR51 up one channel at a time (11 33 77 FF)
    and set NR50; the song's first note follows on the next tick.  That
    `NR52 = 80` is the only one after the boot ROM's chime, so it is an exact
    anchor.  Counting frames is not: a frame is not 70224 cycles here (the LCD
    is off through LSDj's boot), so `180 * 70224` lands ~36 ms LATE and skips
    the song's own first note, anchoring on the second pass of a looping
    phrase instead -- which is what made a note one phrase earlier look like a
    "START blip".  There is no blip: nothing is triggered between the key and
    the first note.
    """
    last00 = None; cands = []
    for c, a, v in rows(path):
        if a != 0xFF26: continue
        if v == 0x00: last00 = c
        elif v & 0x80 and last00 is not None: cands.append(c)
    if not cands: raise SystemExit('%s: no playback reset (NR52 00 -> 80) found' % path)
    return cands[-1]

def events(path, ch, skip_key=None):
    """Writes for one channel from playback start, with t=0 at its first trigger."""
    base = {0: 0xFF10, 1: 0xFF15, 2: 0xFF1A, 3: 0xFF1F}[ch]
    regs = set(range(base, base + 5)) | ({0xFF24, 0xFF25} if ch < 0 else set())
    start = playStart(path) if skip_key is None else skip_key
    ev = [(c, a, v) for c, a, v in rows(path) if c >= start and a in regs]
    trg = base + 4
    t0 = next((c for c, a, v in ev if a == trg and v & 0x80), None)
    if t0 is None: return []
    return [((c - t0) / HZ, a, v) for c, a, v in ev]

def at_start(path, ch):
    """The same writes, but with t = 0 at **playback start** rather than at the
    channel's first trigger -- what a delay (`D`) or a table's first row needs."""
    base = {0: 0xFF10, 1: 0xFF15, 2: 0xFF1A, 3: 0xFF1F}[ch]
    regs = set(range(base, base + 5))
    z = playStart(path)
    return [((c - z) / HZ, a, v) for c, a, v in rows(path) if c >= z and a in regs]

def midi(per, wave=False):
    if per >= 2048 or per < 0: return None
    f = (65536.0 if wave else 131072.0) / (2048 - per)
    return 69 + 12 * math.log2(f / 440.0)

def show(path, ch, lo=-0.01, hi=1.2, limit=60, glob=False):
    lo3 = 0
    n = 0
    for t, a, v in events(path, ch):
        if not (lo <= t <= hi): continue
        nm = NAMES.get(a, '%04X' % a)
        extra = ''
        if nm.endswith('3'): lo3 = v
        if nm.endswith('4'):
            per = ((v & 7) << 8) | lo3
            m = midi(per, ch == 2)
            extra = '  period %4d%s%s' % (per, '  midi %6.2f' % m if m is not None else '',
                                          '  TRIGGER' if v & 0x80 else '')
        print('  %8.4f  %s %02X%s' % (t, nm, v, extra))
        n += 1
        if n >= limit: print('  ...'); break
