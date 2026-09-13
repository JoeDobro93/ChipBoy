"""Run a probe-built save through ChipBoy: import it, trace it, return the CSV.
The ROM the save was built on is put beside it, so the importer picks the right
model and kits."""
import os, shutil, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
LSDJ = os.environ.get('CHIPBOY_LSDJ_DIR', '/root/lsdj')
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REC = os.path.join(REPO, 'build-plugin/chipboy_recordtest_artefacts/Release/chipboy_recordtest')
OUT = os.path.join(LSDJ, 'probe/cbv')

def trace_cb(sav, rom, tag, seconds=2.0):
    d = os.path.join(OUT, tag)
    os.makedirs(d, exist_ok=True)
    work = os.path.join(d, tag + '.sav')
    shutil.copyfile(sav, work)
    link = os.path.join(d, os.path.basename(rom))
    if not os.path.exists(link): os.symlink(os.path.abspath(rom), link)
    cb = os.path.join(d, tag + '.cbsong')
    r = subprocess.run([REC, '--import-sav', work, 'working', cb], capture_output=True, text=True, timeout=600)
    if r.returncode != 0: sys.stderr.write(r.stdout + r.stderr)
    csv = os.path.join(d, tag + '_cb.csv')
    subprocess.run([REC, '--trace-song', cb, csv, str(seconds)], check=True, capture_output=True, timeout=900)
    return csv
