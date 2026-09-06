#!/usr/bin/env python3
"""Dump one take's payload for inspection, when a measurement looks wrong.

  python tools/capture/dump_take.py captures/dmg_vol_max.wav 10

Prints where the take was found and what the step detector sees, and writes a
short excerpt WAV next to the capture so the raw waveform can be looked at
directly instead of guessed at.
"""
import argparse, json, os, sys
import numpy as np
import soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyse as A


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("capture")
    ap.add_argument("take", type=int)
    ap.add_argument("--seconds", type=float, default=0.6)
    a = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    man = json.load(open(os.path.join(here, "takes.json")))
    x, sr, ch = A.load_mono(a.capture)
    takes = A.find_takes(x, sr, man["marker"], [t["id"] for t in man["takes"]])
    hit = [t for t in takes if t[0] == a.take]
    if not hit:
        raise SystemExit(f"take {a.take} not found")
    tid, t0, t1 = hit[0]
    print(f"take {tid}: payload {t0:.3f}s .. {t1:.3f}s  ({t1-t0:.3f}s)")

    y = A.seg(x, sr, t0, t1, head_ms=-10.0)
    print(f"  payload samples {len(y)}   peak {np.abs(y).max():.5f}   "
          f"mean {np.mean(y):+.6f}")

    idx, sgn = A.step_edges(y, sr)
    print(f"  edges detected {len(idx)}   signs {list(sgn[:12].astype(int))}")
    w = max(1, int(sr * 0.005))
    blocks = np.abs(y[:len(y)//w*w]).reshape(-1, w).max(axis=1)
    noise = float(np.percentile(blocks, 5))
    print(f"  noise estimate {noise:.6f}")
    for n, i in enumerate(idx[:8]):
        amp, rate = A.measure_step(y, sr, i, noise)
        kind = "ON " if n % 2 == 0 else "off"
        pre = np.median(y[max(0, i-int(sr*0.001)):i])
        post = y[i+1:i+9]
        print(f"    edge {n:2d} {kind} @ {i/sr*1000:8.2f} ms  baseline {pre:+.5f}"
              f"  next8 {np.array2string(post, precision=4, suppress_small=False)}")
        print(f"             -> amp {amp if amp is None else round(amp,5)}"
              f"   tau {None if rate in (None,0) else round(1000/rate,3)} ms")

    n = int(sr * a.seconds)
    out = f"{os.path.splitext(a.capture)[0]}_take{tid}.wav"
    sf.write(out, y[:n].astype(np.float32), int(sr), subtype="FLOAT")
    print(f"\nwrote {out}  ({a.seconds:g}s, {n*4/1e6:.1f} MB) -- send this")


if __name__ == "__main__":
    main()
