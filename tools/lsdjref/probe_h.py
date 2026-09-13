"""Shared probe helpers for the 9.3.9 verification pass."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from probe_fmt22 import Probe as _Probe, INST, TABLE
import run

LSDJ = os.environ.get('CHIPBOY_LSDJ_DIR', '/root/lsdj')
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HOST = os.path.join(LSDJ, 'host939.sav')
DIR  = os.path.join(LSDJ, 'probe')

def P(**kw):
    return _Probe(host=HOST, blank=True, **kw)

def go(p, name, frames=420, ch=0, rom=None):
    sav = os.path.join(DIR, name + '.sav')
    csv = os.path.join(DIR, name + '.csv')
    p.write(sav)
    run.trace(sav, csv, rom=rom, frames=frames)
    return csv

def raw(path, lo=0xFF10, hi=0xFF26, after=0):
    """Every APU write after `after` cycles, absolute time in seconds."""
    return [(c / run.HZ, a, v) for c, a, v in run.rows(path) if c > after and lo <= a <= hi]

# --- anchoring -------------------------------------------------------------
# run.events()'s skip_key=180*70224 assumes a frame is exactly 70224 cycles.
# On this host the song's first note lands at 2.978 s and the skip at 3.014 s,
# so events() silently anchors on the SECOND pass of a looping phrase.  Anchor
# on LSDj's own playback reset instead: it writes NR52=00 then NR52=80 when
# START starts the song, and that is the only NR52=80 after the boot chime.
def playStart(path):
    last00 = None; cands = []
    for c, a, v in run.rows(path):
        if a != 0xFF26: continue
        if v == 0x00: last00 = c
        elif v & 0x80 and last00 is not None: cands.append(c)
    if not cands: raise SystemExit('%s: no playback reset found' % path)
    return cands[0]      # 4.x and 5.x rewrite NR52 = 80 every frame

def ev(path, ch, t0=None):
    """(t, addr, value) for one channel, t=0 at playback start (or at t0)."""
    base = {0: 0xFF10, 1: 0xFF15, 2: 0xFF1A, 3: 0xFF1F}[ch]
    regs = set(range(base, base + 5))
    z = playStart(path) if t0 is None else t0
    return [((c - z) / run.HZ, a, v) for c, a, v in run.rows(path) if c >= z and a in regs]

def trigs(path, ch):
    """(t, period) for every trigger on a channel, t=0 at playback start."""
    base = {0: 0xFF10, 1: 0xFF15, 2: 0xFF1A, 3: 0xFF1F}[ch]
    lo = 0; out = []
    for t, a, v in ev(path, ch):
        if a == base + 3: lo = v
        if a == base + 4 and v & 0x80: out.append((t, ((v & 7) << 8) | lo))
    return out

def reg(path, addr, t0=None):
    z = playStart(path) if t0 is None else t0
    return [((c - z) / run.HZ, v) for c, a, v in run.rows(path) if c >= z and a == addr]

TICK = 1.0 / 51.2      # 128 BPM: LSDj ticks at BPM*24/60

def volrows(path):
    import csv as _csv
    for r in _csv.DictReader((l for l in open(path) if not l.startswith('#'))):
        yield int(r['cycle']), int(r['addr'], 16), int(r['value'], 16), int(r.get('vol', 0))

def levels(path, ch=0, t0=None):
    """(t, NRx2 value, APU volume after it) for one channel."""
    a2 = {0: 0xFF12, 1: 0xFF17, 2: 0xFF1C, 3: 0xFF21}[ch]
    z = playStart(path) if t0 is None else t0
    return [((c - z) / run.HZ, v, vol) for c, a, v, vol in volrows(path) if c >= z and a == a2]
