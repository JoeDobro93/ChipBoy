"""Probe helpers for any LSDj version: the song layout is the same in every
format (docs/LSDJ_VERSIONS.md), only the interpretation changes."""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe_fmt22 as PF
import run
from probe_h import playStart, ev, reg, trigs, levels

LSDJ = os.environ.get('CHIPBOY_LSDJ_DIR', '/root/lsdj')
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROMS = os.path.join(LSDJ, 'roms')
TRACE = os.path.join(REPO, 'build-ref/lsdjref/lsdjref_trace')
BOOT = os.path.join(REPO, 'build-ref/lsdjref/BootROMs')
DIR = os.path.join(LSDJ, 'probe/v')
os.makedirs(DIR, exist_ok=True)

VERSIONS = ['lsdj3_1_5', 'lsdj3_1_9', 'lsdj3_4_4', 'lsdj3_5_1', 'lsdj3_6_5', 'lsdj3_6_8', 'lsdj3_7_5', 'lsdj3_8_7', 'lsdj3_8_9', 'lsdj3_9_2', 'lsdj4_0_4', 'lsdj4_1_0', 'lsdj4_3_0', 'lsdj4_4_0', 'lsdj4_5_4', 'lsdj4_6_0', 'lsdj4_6_2', 'lsdj4_6_9', 'lsdj4_7_3', 'lsdj4_8_0', 'lsdj4_9_4', 'lsdj5_0_3', 'lsdj5_7_8', 'lsdj5_8_8', 'lsdj5_9_9', 'lsdj6_0_1', 'lsdj6_4_5', 'lsdj6_8_2', 'lsdj6_9_0', 'lsdj7_0_2', 'lsdj7_2_3', 'lsdj7_5_4', 'lsdj7_9_9', 'lsdj8_0_0', 'lsdj8_2_0', 'lsdj8_5_1', 'lsdj8_8_6', 'lsdj8_9_3', 'lsdj8_9_5', 'lsdj9_0_0', 'lsdj9_0_1', 'lsdj9_1_0', 'lsdj9_1_C', 'lsdj9_4_2']
# Versions whose ROM sits beside the saves rather than under `roms/`.
EXTRA = {v: os.path.join(LSDJ, v + '.gb') for v in ('lsdj8_4_4', 'lsdj9_2_L', 'lsdj9_3_9')}

def romPath(v):
    return EXTRA.get(v, os.path.join(ROMS, v + '.gb'))

def hostPath(v):
    p = os.path.join(ROMS, v + '.host.sav')
    if not os.path.isfile(p):
        subprocess.run([TRACE, '--rom', romPath(v), '--bootrom-dir', BOOT, '--model', 'dmg',
                        '--frames', '4000', '--init-sav', p, '--out', '/dev/null'],
                       check=True, capture_output=True, timeout=300)
    return p

def fmt(v):
    return open(hostPath(v), 'rb').read()[0x7FFF]

class VP(PF.Probe):
    """A probe on any version's own bootstrapped host."""
    def __init__(self, v):
        self.version = v
        super().__init__(host=hostPath(v), blank=True, want_format=None)

def trace(p, v, tag, frames=500, keys='180:start'):
    sav = os.path.join(DIR, '%s_%s.sav' % (v, tag))
    csv = os.path.join(DIR, '%s_%s.csv' % (v, tag))
    p.write(sav)
    subprocess.run([TRACE, '--rom', romPath(v), '--bootrom-dir', BOOT, '--model', 'dmg',
                    '--sav', sav, '--frames', str(frames), '--keys', keys, '--out', csv],
                   check=True, capture_output=True, timeout=300)
    return csv


ALL = VERSIONS + ['lsdj8_4_4', 'lsdj9_2_L', 'lsdj9_3_9']

def key(v):
    """Sort key: 9_2_L sits between 9_2 and 9_3, letters after numbers."""
    out = []
    for part in v[4:].split('_'):
        out.append((0, int(part), '') if part.isdigit() else (1, 0, part))
    return out

ORDER = sorted(ALL, key=key)
