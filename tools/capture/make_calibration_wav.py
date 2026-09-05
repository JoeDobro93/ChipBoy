#!/usr/bin/env python3
"""Generate the loopback calibration signal.

  python make_calibration_wav.py calibration.wav

Play this out of the interface and record it back on the same input, at the
same gain, that the console will use. `analyse.py --loopback` then measures the
interface's own AC coupling and its own step response, so both can be separated
from the console's.

The step train deliberately matches the probe ROM's DC-step cadence so the same
analysis code measures both.
"""
import argparse
import numpy as np
import soundfile as sf


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--sr", type=float, default=192000.0)
    ap.add_argument("--level", type=float, default=0.5,
                    help="step amplitude, linear (default 0.5 = -6 dBFS)")
    a = ap.parse_args()
    sr = a.sr

    rng = np.random.default_rng(20260905)

    def step_block(hold_s, level, jitter=True):
        """One held level. Lengths are jittered by a fraction of a sample so
        successive edges land at DIFFERENT sub-sample phases.

        This matters: the interface's DAC and ADC run off the SAME clock, so a
        naively generated loopback puts every edge at an identical sub-sample
        phase and equivalent-time sampling cannot sharpen it. The console has no
        such problem -- its crystal is independent of the converter's."""
        n = int(sr * hold_s)
        blk = np.full(n, float(level))
        if jitter:
            f = rng.random()                    # fractional-sample shift
            blk[0] = level * (1.0 - f) + (0.0 if level else 0.0) * f
        return blk

    parts = [np.zeros(int(sr * 3.0))]                     # lead-in silence
    # 1) slow step train: the interface's coupling time constant.
    #    Same 120/120 ms cadence as probe takes 10..25.
    for _ in range(12):
        parts.append(step_block(0.120, a.level, jitter=False))
        parts.append(step_block(0.120, 0.0, jitter=False))
    parts.append(np.zeros(int(sr * 0.5)))
    # 2) fast step train: 250 edges for equivalent-time reconstruction of the
    #    interface's own edge. Same 4/4 ms cadence as probe take 140.
    for _ in range(250):
        parts.append(step_block(0.004, a.level))
        parts.append(step_block(0.004, 0.0))
    parts.append(np.zeros(int(sr * 1.0)))

    y = np.concatenate(parts)
    sf.write(a.out, y.astype(np.float32), int(sr), subtype="FLOAT")
    print(f"wrote {a.out}  ({len(y)/sr:.1f} s @ {sr:g} Hz, steps at {a.level:g})")
    print("Play this out of the interface, record it back on the SAME input at "
          "the SAME gain as the console, then pass it to analyse.py --loopback")


if __name__ == "__main__":
    main()
