#!/usr/bin/env python3
"""lsdjref-measure -- turn the traces into the numbers docs/LSDJ_PARITY.md quotes.

The compare tool says whether the two streams agree; this says what LSDj
actually did, in units: the timer period, the note table, the vibrato swing
per speed and depth, the bend per update, the envelope step interval, the
sequences a level change is made of.

    lsdjref_measure.py --traces DIR [--case NAME] [--model dmg]

Everything printed is derived from the register writes in the trace CSVs.
Nothing is read from the ROM.
"""

import argparse
import csv
import os
import statistics
import sys

CPU_HZ = 4194304.0
TRIGGERS = {"NR14", "NR24", "NR34", "NR44"}
LEVEL = {"NR14": "NR12", "NR24": "NR22", "NR34": "NR32", "NR44": "NR42"}


def read(path):
    rows = []
    with open(path) as f:
        for r in csv.reader(f):
            if len(r) < 4 or not r[0].isdigit():
                continue
            rows.append((int(r[0]), r[2], int(r[3], 16), int(r[4]) if len(r) > 4 and r[4] else -1))
    return rows


def note_ons(rows, window=4096):
    """Indexes of real note-ons: a trigger whose level register was written
    just before it, which an interface beep's bare period write is not."""
    out = []
    for i, (cy, name, val, _v) in enumerate(rows):
        if name not in TRIGGERS or not val & 0x80:
            continue
        level = LEVEL[name]
        j = i - 1
        while j >= 0 and cy - rows[j][0] <= window:
            if rows[j][1] == level:
                out.append(i)
                break
            j -= 1
    return out


def periods(rows, lo_name, hi_name):
    """The period the channel is set to, sampled where the hardware latches it.

    A period write is NRx3 then NRx4, and only the second one puts the pair
    into effect. Reading the pair after the low byte alone would show a period
    that never sounded -- 0x706 dropping to 0x606 while the high bits catch
    up -- so the sample is taken at the high write, with the low byte in force.
    """
    lo = hi = 0
    out = []
    for cy, name, val, _v in rows:
        if name == lo_name:
            lo = val
        elif name == hi_name:
            hi = val & 7
            out.append((cy, (hi << 8) | lo))
    return out


def freq_of(period):
    return 131072.0 / (2048 - period) if period < 2048 else 0.0


def midi_of(freq):
    import math
    return 69 + 12 * math.log2(freq / 440.0) if freq > 0 else 0.0


def segments(rows, marks):
    """The rows between one note-on and the next."""
    for k, i in enumerate(marks):
        end = marks[k + 1] if k + 1 < len(marks) else len(rows)
        yield k, rows[i][0], rows[i:end]


def spacing(cycles):
    if len(cycles) < 2:
        return None
    d = [cycles[i + 1] - cycles[i] for i in range(len(cycles) - 1)]
    return (statistics.mean(d), min(d), max(d), len(d))


# --- the reports ------------------------------------------------------------

def report_timer(rows):
    tma = tac = None
    for _cy, name, val, _v in rows:
        if name == "TMA" and tma is None:
            tma = val
        if name == "TAC" and tac is None:
            tac = val
    if tma is None or tac is None:
        return
    div = {0: 1024, 1: 16, 2: 64, 3: 256}[tac & 3]
    period = (256 - tma) * div
    print("timer      TMA=%02X TAC=%02X -> %d cycles = %.3f Hz (enabled=%s)"
          % (tma, tac, period, CPU_HZ / period, bool(tac & 4)))


def report_note_on(rows, marks, n=3):
    print("note-on    the writes from the level register to the trigger:")
    for k, cy, seg in segments(rows, marks):
        if k >= n:
            break
        # walk back to the start of the group
        i = marks[k]
        j = i
        while j > 0 and rows[i][0] - rows[j - 1][0] <= 4096:
            j -= 1
        group = rows[j:i + 1]
        print("           #%d  %s" % (k, " ".join("%s=%02X" % (g[1], g[2]) for g in group)))


def report_notes(rows, marks):
    print("notes      the period each note-on set, and what that sounds:")
    per = dict(periods(rows, "NR13", "NR14"))
    keys = sorted(per)
    for k, cy, seg in segments(rows, marks):
        # the period in force at the trigger
        p = 0
        for c in keys:
            if c > cy:
                break
            p = per[c]
        f = freq_of(p)
        print("           #%-2d period %4d  %8.2f Hz  MIDI %6.2f" % (k, p, f, midi_of(f)))


def report_vibrato(rows, marks, labels=None):
    """Per note: how far the period swung, in units and in semitones, and how
    long one cycle of the swing took."""
    import math
    print("vibrato    per note: base, swing in period units, in cents, and the cycle")
    for k, cy, seg in segments(rows, marks):
        per = [(c, p) for c, p in periods(seg, "NR13", "NR14") if c >= cy]
        if len(per) < 4:
            continue
        base = per[0][1]
        vals = [p for _c, p in per]
        lo, hi = min(vals), max(vals)
        # A cycle: the mean gap between successive upward crossings of the mean.
        mid = (lo + hi) / 2.0
        cross = [per[i][0] for i in range(1, len(per))
                 if per[i - 1][1] <= mid < per[i][1]]
        sp = spacing(cross)
        cents = 1200 * math.log2(freq_of(hi) / freq_of(lo)) if lo < 2048 and hi < 2048 and freq_of(lo) > 0 else 0
        name = labels[k] if labels and k < len(labels) else "#%d" % k
        print("           %-6s base %4d  swing %4d..%4d (%+d/%+d, %d units, %.0f cents)  cycle %s"
              % (name, base, lo, hi, lo - base, hi - base, hi - lo, cents,
                 ("%8.0f cyc = %5.2f Hz over %d updates" % (sp[0], CPU_HZ / sp[0], sp[3] + 1)) if sp else "none"))


def report_pitch_stream(rows, marks, label):
    """For each note: how the period moved, how often, and by how much."""
    print("%-10s per note: updates, interval, period range and step" % label)
    for k, cy, seg in segments(rows, marks):
        per = periods(seg, "NR13", "NR14")
        per = [(c, p) for c, p in per if c > cy + 100]     # after the trigger
        if len(per) < 2:
            print("           #%-2d no pitch updates" % k)
            continue
        # one update is a NR13+NR14 pair; keep the value at each NR14
        vals, cys = [], []
        for c, p in per:
            if not cys or c - cys[-1] > 200:
                cys.append(c)
                vals.append(p)
            else:
                vals[-1] = p
        sp = spacing(cys)
        steps = [vals[i + 1] - vals[i] for i in range(len(vals) - 1)]
        steps = [s for s in steps if s]
        print("           #%-2d %4d updates  every %8.1f cyc (%.1f Hz)  period %4d..%4d  step %s"
              % (k, len(cys), sp[0] if sp else 0, CPU_HZ / sp[0] if sp and sp[0] else 0,
                 min(vals), max(vals),
                 "+-%d" % max(abs(s) for s in steps) if steps else "0"))


def report_level(rows, marks, level_name="NR12"):
    print("level      the NRx2 writes after each note-on, with the volume they left")
    for k, cy, seg in segments(rows, marks):
        w = [(c, v, vol) for c, n, v, vol in seg if n == level_name]
        if not w:
            continue
        first = w[0][1]
        after = w[1:]
        bursts = []
        for c, v, vol in after:
            if bursts and c - bursts[-1][-1][0] < 1000:
                bursts[-1].append((c, v, vol))
            else:
                bursts.append([(c, v, vol)])
        text = " | ".join(" ".join("%02X" % v for _c, v, _vol in b) + "->%d" % b[-1][2] for b in bursts[:6])
        sp = spacing([b[0][0] for b in bursts])
        print("           #%-2d note-on %02X  %d bursts%s  %s"
              % (k, first, len(bursts),
                 ("  every %.0f cyc" % sp[0]) if sp else "", text))


def report_retrigger(rows, marks):
    print("triggers   how often the channel is retriggered inside a note")
    for k, cy, seg in segments(rows, marks):
        t = [c for c, n, v, _ in seg if n in TRIGGERS and v & 0x80]
        sp = spacing(t)
        print("           #%-2d %d triggers%s" % (k, len(t), ("  every %.0f cyc = %.1f Hz" % (sp[0], CPU_HZ / sp[0])) if sp else ""))


def report_all(path, what):
    rows = read(path)
    marks = note_ons(rows)
    print("\n=== %s  (%d writes, %d note-ons) ===" % (os.path.basename(path), len(rows), len(marks)))
    report_timer(rows)
    if not marks:
        return
    if "noteon" in what:
        report_note_on(rows, marks)
    if "notes" in what:
        report_notes(rows, marks)
    if "pitch" in what:
        report_pitch_stream(rows, marks, "pitch")
    if "level" in what:
        report_level(rows, marks, "NR32" if "wave" in path else "NR12")
    if "vib" in what:
        report_vibrato(rows, marks)
    if "trig" in what:
        report_retrigger(rows, marks)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--traces", required=True)
    ap.add_argument("--model", default="dmg")
    ap.add_argument("--case", action="append")
    ap.add_argument("--what", default="noteon,notes,pitch,level,trig",
                    help="which reports: noteon notes pitch level trig")
    args = ap.parse_args()

    what = set(args.what.split(","))
    names = args.case or sorted(
        f.split(".")[0] for f in os.listdir(args.traces)
        if f.endswith("." + args.model + ".csv"))
    for n in names:
        p = os.path.join(args.traces, "%s.%s.csv" % (n, args.model))
        if os.path.exists(p):
            report_all(p, what)
    return 0


if __name__ == "__main__":
    sys.exit(main())
