#!/usr/bin/env python3
"""Synthesise a fake probe-ROM capture from known constants, so analyse.py can
be tested end to end without hardware.

  python simulate_capture.py sim.wav [--sr 192000] [--noise -80]

The forward model here is deliberately simple and is NOT the plugin's model --
its only job is to emit something with the right structure and known-good
constants, so that `analyse.py sim.wav` can be checked against GROUND_TRUTH.
"""
import argparse, json, os
import numpy as np
import soundfile as sf

CPU_HZ = 4194304.0

GROUND_TRUTH = {
    "dac_slope": 1.0 / 7.5,          # dac(L) = L/7.5 - 1
    "dac_intercept": -1.0,
    "coupling_tau_ms": 5.677,        # blargg's DMG constant
    "amp_lpf_hz": 60000.0,
    "master_law": "(v+1)/8",
    "output_scale": 0.18,   # keep the 4-channel sum inside full scale
}


def dac(L):
    return GROUND_TRUTH["dac_slope"] * L + GROUND_TRUTH["dac_intercept"]


class Builder:
    def __init__(self, sr):
        self.sr = sr
        self.buf = []

    def add(self, y):
        self.buf.append(np.asarray(y, dtype=np.float64))

    def silence(self, ms):
        self.add(np.zeros(int(self.sr * ms / 1000)))

    def square(self, ms, hz, low, high, duty=0.5):
        n = int(self.sr * ms / 1000)
        t = np.arange(n) / self.sr
        ph = (t * hz) % 1.0
        self.add(np.where(ph < duty, high, low))

    def dc(self, ms, level):
        self.add(np.full(int(self.sr * ms / 1000), float(level)))

    def result(self):
        return np.concatenate(self.buf) if self.buf else np.zeros(0)


def marker(b, tid, mk):
    """Reproduce the ROM's marker exactly as EmitMarker emits it."""
    for _ in range(mk["preamble_reps"]):
        b.square(mk["preamble_on_ms"], mk["preamble_hz"], dac(0), dac(15))
        b.silence(mk["preamble_off_ms"])
    for i in range(mk["bits"]):
        bit = (tid >> (mk["bits"] - 1 - i)) & 1
        b.square(mk["bit_on_ms"], mk["one_hz"] if bit else mk["zero_hz"], dac(0), dac(15))
        b.silence(mk["bit_off_ms"])
    b.silence(mk["trailer_ms"])


# Real hardware overshoots when the DAC turns on, and the wave channel's sample
# buffer holds its previous value for (2048 - f) * 2 cycles after a trigger.
# Both are modelled here because both broke the analyser on the first real
# capture and neither was exercised by a simulation that omitted them.
OVERSHOOT = 0.11          # measured on a DMG: peak 11% above the settled level
STALE_LEVEL = 0           # the buffer reads 0 after an APU power-cycle


def dc_train(b, level, reps, on_ms, off_ms, master=7, scale=1.0, wave_f=2047):
    g = (master + 1) / 8.0 * scale
    delay_ms = (2048 - wave_f) * 2 / CPU_HZ * 1000.0
    for _ in range(reps):
        n_over = max(1, int(b.sr * 0.00002))
        if delay_ms > 0.05:
            b.add(np.full(n_over, dac(STALE_LEVEL) * g * (1 + OVERSHOOT)))
            b.dc(delay_ms - n_over / b.sr * 1000, dac(STALE_LEVEL) * g)
            b.add(np.full(n_over, dac(level) * g * (1 + OVERSHOOT)))
            b.dc(on_ms - delay_ms - n_over / b.sr * 1000, dac(level) * g)
        else:
            b.add(np.full(n_over, dac(level) * g * (1 + OVERSHOOT)))
            b.dc(on_ms - n_over / b.sr * 1000, dac(level) * g)
        b.dc(off_ms, 0.0)


def payload(b, tid, dur_s):
    ms = dur_s * 1000.0
    f1k = 131072.0 / (2048 - 1917)
    f500 = 131072.0 / (2048 - 1786)

    if tid in (1, 2, 3, 4):
        b.silence(ms)
    elif 10 <= tid <= 25:
        dc_train(b, tid - 10, 6, 120, 120)
    elif 30 <= tid <= 37:
        dc_train(b, 15, 4, 150, 150, master=tid - 30)
    elif 40 <= tid <= 43:
        n = tid - 40
        digital = 0 if n == 0 else (15 >> {1: 0, 2: 1, 3: 2}[n])
        dc_train(b, digital, 4, 150, 150)
    elif 50 <= tid <= 65:
        L = tid - 50
        if L == 0:
            b.silence(ms)                      # NR12=0 kills the DAC
        else:
            b.square(ms, f1k, dac(0), dac(L))
    elif 70 <= tid <= 73:
        b.square(ms, f500, dac(0), dac(15), duty=[0.125, 0.25, 0.5, 0.75][tid - 70])
    elif 80 <= tid <= 93:
        up = tid >= 87
        p = (tid - 86) if up else (tid - 79)
        step = p / 64.0
        lvl = 0 if up else 15
        left = ms
        while left > 0:
            seg = min(step * 1000, left)
            if lvl <= 0 and not up:
                b.silence(seg)
            else:
                b.square(seg, f1k, dac(0), dac(lvl))
            lvl += 1 if up else -1
            lvl = max(0, min(15, lvl))
            left -= seg
    elif tid in (100, 101, 102, 103):
        freg = {100: 0, 101: 1024, 102: 1750, 103: 2017}[tid]
        b.square(ms, 131072.0 / (2048 - freg), dac(0), dac(15))
    elif tid in (110, 111):
        freg = {110: 1750, 111: 2017}[tid]
        hz = 65536.0 / (2048 - freg)
        n = int(b.sr * ms / 1000)
        t = np.arange(n) / b.sr
        ph = (t * hz * 32) % 32
        tri = np.concatenate([np.arange(16), np.arange(15, -1, -1)])
        b.add(dac(tri[ph.astype(int)]))
    elif 120 <= tid <= 123:
        n = int(b.sr * ms / 1000)
        rng = np.random.default_rng(tid)
        b.add(np.where(rng.random(n) > 0.5, dac(15), dac(0)))
    elif 130 <= tid <= 133:
        n = int(b.sr * ms / 1000)
        t = np.arange(n) / b.sr
        y = np.where((t * f1k) % 1.0 < 0.75, dac(15), dac(0))          # PU1
        if tid >= 131:
            y = y + np.where((t * f500) % 1.0 < 0.75, dac(15), dac(0))  # PU2
        if tid >= 132:
            rng = np.random.default_rng(7)
            y = y + np.where(rng.random(n) > 0.5, dac(15), dac(0))      # noise
        if tid >= 133:
            tri = np.concatenate([np.arange(16), np.arange(15, -1, -1)])
            ph = (t * (65536.0 / (2048 - 1786)) * 32) % 32
            y = y + dac(tri[ph.astype(int)])                            # wave
        b.add(y)
    elif tid == 134:
        dc_train(b, 15, 4, 200, 200, master=0)
    elif tid in (140, 141):
        dc_train(b, 15, 250, 4, 4, master=7 if tid == 140 else 3)
    else:
        b.silence(ms)


def one_pole_hp(x, sr, tau_s):
    a = np.exp(-1.0 / (sr * tau_s))
    y = np.empty_like(x)
    prev_x = prev_y = 0.0
    for i in range(len(x)):
        prev_y = a * (prev_y + x[i] - prev_x)
        prev_x = x[i]
        y[i] = prev_y
    return y


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--sr", type=float, default=192000.0)
    ap.add_argument("--noise", type=float, default=-84.0, help="noise floor dBFS")
    ap.add_argument("--takes", default=None)
    a = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    man = json.load(open(a.takes or os.path.join(here, "takes.json")))
    mk = man["marker"]
    sr = a.sr

    b = Builder(sr)
    b.silence(man["lead_in_ms"])
    for t in man["takes"]:
        marker(b, t["id"], mk)
        payload(b, t["id"], t["payload_seconds"])
    y = b.result()

    # analog chain: amplifier low-pass, then the single shared coupling cap
    fc = GROUND_TRUTH["amp_lpf_hz"]
    if fc < sr / 2:
        alpha = 1 - np.exp(-2 * np.pi * fc / sr)
        lp = np.empty_like(y); acc = 0.0
        for i in range(len(y)):
            acc += alpha * (y[i] - acc)
            lp[i] = acc
        y = lp
    y = one_pole_hp(y, sr, GROUND_TRUTH["coupling_tau_ms"] / 1000.0)
    y *= GROUND_TRUTH["output_scale"]

    rng = np.random.default_rng(1)
    y += rng.normal(0, 10 ** (a.noise / 20), len(y))
    t = np.arange(len(y)) / sr
    y += 10 ** ((a.noise + 12) / 20) * np.sin(2 * np.pi * 9198.0 * t)   # LCD whine

    sf.write(a.out, y.astype(np.float32), int(sr), subtype="FLOAT")
    truth = dict(GROUND_TRUTH)
    truth["coupling_rate_1_s"] = 1000.0 / GROUND_TRUTH["coupling_tau_ms"]
    truth["per_cpu_cycle_factor"] = float(np.exp(-truth["coupling_rate_1_s"] / CPU_HZ))
    json.dump(truth, open(os.path.splitext(a.out)[0] + "_truth.json", "w"), indent=1)
    print(f"wrote {a.out}  ({len(y)/sr:.1f} s @ {sr:g} Hz)")
    print(f"ground truth -> {os.path.splitext(a.out)[0]}_truth.json")


if __name__ == "__main__":
    main()
