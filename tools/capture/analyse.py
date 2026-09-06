#!/usr/bin/env python3
"""Measure a ChipBoy probe-ROM capture.

  python analyse.py capture.wav [--loopback loopback.wav] [--out measured.json]
                                [--model dmg|cgb] [--plots]

Segments the recording using the ROM's own sync markers, then measures the
analog properties the emulation needs. Digital behaviour is NOT measured here --
blargg's dmg_sound and SameSuite do that far better than audio can.

Outputs measured.json (the numbers that replace HARDWARE_REFERENCE.md section 12)
and a readable report on stdout.
"""
import argparse, json, os, sys
import numpy as np
from scipy import signal as sig
import soundfile as sf

CPU_HZ = 4194304.0


# --------------------------------------------------------------------------
# loading and marker decoding
# --------------------------------------------------------------------------

def load_mono(path):
    x, sr = sf.read(path, always_2d=True)
    return x.mean(axis=1).astype(np.float64), float(sr), x.shape[1]


def tone_mags(x, sr, freqs, hop_ms=2.5, win_ms=10.0):
    """Magnitude at each of `freqs` per analysis frame (a direct DFT bin)."""
    hop, win = int(sr * hop_ms / 1000), int(sr * win_ms / 1000)
    n = 1 + max(0, (len(x) - win) // hop)
    w = np.hanning(win)
    idx = np.arange(win)
    basis = [np.exp(-2j * np.pi * f * idx / sr) * w for f in freqs]
    frames = np.lib.stride_tricks.as_strided(
        x, shape=(n, win), strides=(x.strides[0] * hop, x.strides[0]))
    mags = np.stack([np.abs(frames @ b) / win for b in basis], axis=1)
    times = np.arange(n) * hop / sr
    return times, mags


def runs_above(mask, times, min_ms, max_ms):
    """Contiguous True runs whose duration falls in [min_ms, max_ms]."""
    out, i, n = [], 0, len(mask)
    while i < n:
        if mask[i]:
            j = i
            while j < n and mask[j]:
                j += 1
            dur = (times[j - 1] - times[i]) * 1000
            if min_ms <= dur <= max_ms:
                out.append((times[i], times[j - 1]))
            i = j
        else:
            i += 1
    return out


def find_takes(x, sr, mk, expected_ids=None):
    """Locate every marker and return [(take_id, payload_start_s, payload_end_s)].

    Payload audio is harmonic-rich, so some candidates are always spurious. The
    decisive filter is that we know the take order in advance: walk the expected
    sequence and accept the next candidate that decodes to the id we are looking
    for. A genuinely missed marker drops one take rather than desynchronising."""
    f_hi, f_one, f_nil = mk["preamble_hz"], mk["one_hz"], mk["zero_hz"]
    times, mags = tone_mags(x, sr, [f_hi, f_one, f_nil])
    hi = mags[:, 0]
    thr = 0.25 * np.percentile(hi, 99.5)
    if thr <= 0:
        raise SystemExit("no signal found -- is the capture silent?")

    bursts = runs_above(hi > thr, times, mk["preamble_on_ms"] * 0.55,
                        mk["preamble_on_ms"] * 2.2)
    # pair consecutive bursts into preambles
    preambles, i = [], 0
    gap_max = (mk["preamble_off_ms"] * 2.5) / 1000
    while i < len(bursts) - 1:
        if bursts[i + 1][0] - bursts[i][1] <= gap_max:
            preambles.append((bursts[i][0], bursts[i + 1][1]))
            i += 2
        else:
            i += 1

    slot = (mk["bit_on_ms"] + mk["bit_off_ms"]) / 1000.0
    lead = mk["preamble_off_ms"] / 1000.0
    takes = []
    for p_start, p_end in preambles:
        bits = 0
        for b in range(mk["bits"]):
            t0 = p_end + lead + b * slot
            c0, c1 = t0 + 0.005, t0 + mk["bit_on_ms"] / 1000.0 - 0.005
            s = (times >= c0) & (times <= c1)
            if not s.any():
                bits = None
                break
            bits = (bits << 1) | int(mags[s, 1].mean() > mags[s, 2].mean())
        if bits is None:
            continue
        payload = p_end + lead + mk["bits"] * slot + mk["trailer_ms"] / 1000.0
        takes.append([bits, p_start, payload])

    if expected_ids:
        matched, ci = [], 0
        for want in expected_ids:
            j = ci
            while j < len(takes) and takes[j][0] != want:
                j += 1
            if j < len(takes):
                matched.append(takes[j])
                ci = j + 1
        takes = matched

    out = []
    for k, (tid, m_start, p_start) in enumerate(takes):
        p_end = takes[k + 1][1] if k + 1 < len(takes) else len(x) / sr
        out.append((tid, p_start, p_end))
    return out


def seg(x, sr, t0, t1, head_ms=25.0, tail_ms=25.0):
    """Slice a payload. The guards are asymmetric on purpose: a marker ends
    with a silent trailer, so nothing bleeds into the START of a payload and
    the head guard can be negative (start early) to be sure the payload's very
    first edge is included. The NEXT marker's preamble is what must be kept
    out, so the tail guard is always positive."""
    a = int((t0 + head_ms / 1000) * sr)
    b = int((t1 - tail_ms / 1000) * sr)
    return x[max(0, a):max(0, b)]


# --------------------------------------------------------------------------
# primitive measurements
# --------------------------------------------------------------------------

def step_edges(y, sr, min_gap_ms=40.0):
    """Indices of large level transitions, by peak-picking |d/dt|."""
    if y is None or len(y) < 64:
        return np.array([], int), np.array([], float)
    d = np.diff(y)
    if d.size == 0 or not np.isfinite(d).any() or np.abs(d).max() == 0:
        return np.array([], int), np.array([], float)
    thr = 0.35 * np.abs(d).max()
    cand = np.where(np.abs(d) > thr)[0]
    if len(cand) == 0:
        return np.array([], int), np.array([], float)
    keep, last = [], -10 ** 9
    gap = int(sr * min_gap_ms / 1000)
    for i in cand:
        if i - last > gap:
            keep.append(i)
            last = i
    keep = np.array(keep)
    return keep, np.sign(d[keep])


def measure_step(y, sr, i0, noise=0.0):
    """Signed step amplitude at edge i0, plus the local decay rate.

    Two things here are deliberate, and the previous version got both wrong:

    1. AMPLITUDE IS READ DIRECTLY AT THE EDGE, not extrapolated from an
       exponential fit. The console's coupling capacitor and the interface's
       input coupling are in SERIES, so the observed decay is two-pole. A
       single-exponential extrapolation back to t=0 is biased, and the bias
       depends on the time constants -- which differ by ~25x between a DMG
       (~5 ms) and a CGB (~0.2 ms).

    2. THE FIT WINDOW SCALES TO THE OBSERVED DECAY. A window fixed at 1-25 ms
       suits a DMG and completely misses a CGB, whose step has already decayed
       to ~1% before 1 ms. Fitting that window on a CGB measures the
       INTERFACE's tail and reports its time constant as the console's.
    """
    n = len(y)
    pre = max(1, int(sr * 0.001))
    if i0 - pre < 0 or i0 + 8 >= n:
        return None, None
    base = float(np.median(y[max(0, i0 - pre):i0]))
    dev = y[i0:] - base

    # Peak within a few samples: long enough to clear the converter's own
    # settling (the loopback edge takes ~2 samples), short enough that even a
    # 0.2 ms time constant has barely decayed.
    look = min(8, n - i0 - 1)
    k0 = int(np.argmax(np.abs(dev[:look])))
    a0 = float(dev[k0])
    if a0 == 0.0:
        return None, None

    # Decay rate, over a window derived from the signal itself.
    mag = np.abs(dev[k0:])
    floor = max(noise * 3.0, abs(a0) * 1e-3)
    below = np.where(mag < abs(a0) / np.e)[0]
    if len(below) == 0:
        return None, None
    n_1e = int(below[0])
    if n_1e < 2:
        return None, None
    lo = k0 + max(1, int(0.05 * n_1e))
    hi = k0 + min(int(0.5 * n_1e), len(mag) - 1)
    seg = np.abs(dev[lo:hi])
    t = np.arange(lo, hi) / sr
    good = seg > floor
    if good.sum() < 8:
        return None, None
    slope = np.polyfit(t[good], np.log(seg[good]), 1)[0]
    rate = float(-slope)
    if not np.isfinite(rate) or rate <= 0:
        return None, None

    # Undo the decay that happened in the k0 samples before the peak.
    amp = a0 * float(np.exp(rate * k0 / sr))
    return amp, rate


def dc_step_take(y, sr):
    """Signed step amplitude and coupling rate over a DC on/off train.

    The ROM always drives DAC-on then DAC-off, so edges alternate and the first
    edge in a payload is an ON edge. Only ON edges are measured: an OFF edge
    steps from an already-decayed level, not from the DAC level.

    The sign must come from the data, not from the edge direction. For a wave
    value below mid-scale the DAC output is on the other side of zero, so
    turning the DAC on is a step in the opposite direction -- taking |edge|
    would report the wrong sign for half the levels and hide the very thing
    being measured.
    """
    if y is None or len(y) < 64:
        return None
    idx, sgn = step_edges(y, sr)
    if len(idx) < 1:
        return None
    alternating = bool(len(sgn) < 2 or np.all(sgn[:-1] * sgn[1:] < 0))
    # Noise estimate from the quietest stretch, used as the fit's cutoff.
    w = max(1, int(sr * 0.005))
    blocks = np.abs(y[:len(y) // w * w]).reshape(-1, w).max(axis=1)
    noise = float(np.percentile(blocks, 5)) if len(blocks) else 0.0
    amps, rates = [], []
    for i in idx[0::2]:                       # ON edges only
        A, r = measure_step(y, sr, i, noise)
        if A is None or not np.isfinite(r) or r <= 0:
            continue
        amps.append(A)                        # already signed by measure_step
        rates.append(r)
    if not amps:
        return None
    return {"amplitude": float(np.median(amps)),
            "coupling_rate_1_s": float(np.median(rates)),
            "tau_ms": float(1000.0 / np.median(rates)),
            "fc_hz": float(np.median(rates) / (2 * np.pi)),
            "edges_used": len(amps), "edges_found": len(idx),
            "edges_alternate": alternating}


def undo_hp(y, sr, tau_s):
    """Exact inverse of a one-pole high-pass, vectorised.

    The console's coupling capacitor and the interface's are in SERIES, so the
    recorded step is a two-pole decay and neither pole can be read off it
    directly. Removing the interface's measured pole first leaves a genuine
    single-pole decay, which is what the fit assumes.

    Subtracting rates instead (rates add only at t=0) leaves a systematic
    error: on a DMG it under-reads the time constant by about 5%.
    """
    a = float(np.exp(-1.0 / (sr * tau_s)))
    d = y / a - np.concatenate(([0.0], y[:-1]))
    return np.cumsum(d)


def theil_sen(xs, ys):
    """Median-of-pairwise-slopes fit. Levels near the DAC's zero crossing give
    tiny, noisy steps; a least-squares fit lets those points drag the line."""
    xs, ys = np.asarray(xs, float), np.asarray(ys, float)
    sl = [(ys[j] - ys[i]) / (xs[j] - xs[i])
          for i in range(len(xs)) for j in range(i + 1, len(xs)) if xs[j] != xs[i]]
    if not sl:
        return None, None
    m = float(np.median(sl))
    return m, float(np.median(ys - m * xs))


def fundamental(y, sr, f0, span=0.15):
    """Amplitude of the component nearest f0 (immune to coupling droop)."""
    n = len(y)
    if n < 1024:
        return 0.0
    w = np.hanning(n)
    Y = np.abs(np.fft.rfft(y * w)) * 2 / w.sum()
    fr = np.fft.rfftfreq(n, 1 / sr)
    m = (fr > f0 * (1 - span)) & (fr < f0 * (1 + span))
    return float(Y[m].max()) if m.any() else 0.0


def peak_freq(y, sr, lo=40.0, hi=20000.0):
    n = len(y)
    w = np.hanning(n)
    Y = np.abs(np.fft.rfft(y * w))
    fr = np.fft.rfftfreq(n, 1 / sr)
    m = (fr >= lo) & (fr <= hi)
    k = np.argmax(Y[m])
    f = fr[m][k]
    # parabolic refinement
    i = np.where(fr == f)[0][0]
    if 0 < i < len(Y) - 1:
        a, b, c = Y[i - 1], Y[i], Y[i + 1]
        d = (a - c) / (2 * (a - 2 * b + c)) if (a - 2 * b + c) != 0 else 0.0
        f = f + d * (fr[1] - fr[0])
    return float(f)


def rms_envelope(y, sr, win_ms=4.0):
    w = int(sr * win_ms / 1000)
    n = len(y) // w
    e = np.sqrt((y[:n * w].reshape(n, w) ** 2).mean(axis=1))
    return np.arange(n) * w / sr, e


def ets_edge(y, sr, oversample=16, pre_ms=0.3, post_ms=1.2):
    """Equivalent-time reconstruction of a repeated rising edge.

    The console clock and the converter clock are independent, so successive
    edges land at random sub-sample phases. Estimating each edge's crossing to
    sub-sample precision and averaging on a common fine grid gives a much
    finer-grained view of the edge than the raw 1/sr grid.

    NOTE this improves TIME RESOLUTION, not BANDWIDTH: content above the
    converter's anti-alias corner is gone and no amount of averaging recovers
    it. Compare against the loopback edge to know which you measured.
    """
    idx, sgn = step_edges(y, sr, min_gap_ms=3.0)
    rise = idx[sgn > 0]
    if len(rise) < 8:
        return None
    npre, npost = int(sr * pre_ms / 1000), int(sr * post_ms / 1000)
    grid = np.arange(-npre, npost, 1.0 / oversample)
    acc = np.zeros(len(grid))
    cnt = 0
    for i in rise:
        if i - npre - 2 < 0 or i + npost + 2 >= len(y):
            continue
        w = y[i - npre - 2: i + npost + 2].astype(np.float64)
        lo, hi = np.median(w[:npre]), np.median(w[-npost // 3:])
        if hi - lo <= 0:
            continue
        half = (lo + hi) / 2
        loc = np.arange(len(w))
        k = np.argmax(w >= half)
        if k == 0:
            continue
        frac = (half - w[k - 1]) / (w[k] - w[k - 1] + 1e-30)
        cross = (k - 1) + frac
        norm = (w - lo) / (hi - lo)
        acc += np.interp(grid + cross, loc, norm)
        cnt += 1
    if cnt < 8:
        return None
    avg = acc / cnt
    t_us = grid / sr * 1e6
    try:
        t10 = np.interp(0.1, avg, t_us)
        t90 = np.interp(0.9, avg, t_us)
        rise_us = float(t90 - t10)
    except Exception:
        rise_us = float("nan")
    return {"edges_averaged": cnt, "rise_10_90_us": rise_us,
            "implied_bandwidth_hz": float(0.35 / (rise_us * 1e-6)) if rise_us > 0 else None,
            "t_us": t_us.tolist(), "shape": avg.tolist()}


def spectrum(y, sr, nfft=32768):
    f, p = sig.welch(y, sr, nperseg=min(nfft, len(y)))
    return f, 10 * np.log10(p + 1e-30)


# --------------------------------------------------------------------------
# per-group analysis
# --------------------------------------------------------------------------

def analyse(path, manifest, loopback=None, want_plots=False):
    x, sr, ch = load_mono(path)
    if sr < 96000:
        print(f"WARNING: capture is {sr:g} Hz. 192 kHz is expected; edge and "
              f"bandwidth results will be meaningless.", file=sys.stderr)
    expected = [t["id"] for t in manifest["takes"]]
    takes = find_takes(x, sr, manifest["marker"], expected)
    found = {t[0]: t for t in takes}
    missing = [i for i in expected if i not in found]

    # Headroom: a clipped capture silently ruins the DAC-transfer and clipping
    # measurements, and looks like console saturation. Check before anything else.
    peak = float(np.abs(x).max())
    at_fs = float((np.abs(x) >= 0.999).mean())
    R = {"capture": os.path.basename(path), "sample_rate": sr, "channels": ch,
         "takes_expected": len(expected), "takes_found": len(found),
         "takes_missing": missing,
         "headroom": {"peak": peak, "peak_dbfs": float(20 * np.log10(peak + 1e-30)),
                      "fraction_at_full_scale": at_fs,
                      "clipped": bool(at_fs > 1e-5),
                      "too_quiet": bool(peak < 0.05)}}
    if R["headroom"]["clipped"]:
        print(f"WARNING: {at_fs*100:.3f}% of samples are at full scale -- the capture "
              f"is CLIPPED. Lower the input gain and record again; DAC transfer and "
              f"clipping results from this file are not usable.", file=sys.stderr)
    if R["headroom"]["too_quiet"]:
        print(f"WARNING: peak is only {R['headroom']['peak_dbfs']:.1f} dBFS. Raise the "
              f"input gain; low levels degrade every amplitude measurement.",
              file=sys.stderr)
    if len(found) < len(expected) * 0.8:
        raise SystemExit(f"only {len(found)}/{len(expected)} markers decoded -- "
                         f"check levels and that the whole run was captured")

    def payload(tid, min_ms=50.0, head_ms=25.0):
        if tid not in found:
            return None
        _, a, b = found[tid]
        y = seg(x, sr, a, b, head_ms=head_ms)
        return y if len(y) >= sr * min_ms / 1000 else None

    # The interface's own coupling pole, measured from the loopback, is removed
    # from every DC-step payload before anything is fitted (see undo_hp).
    lb = analyse_loopback(loopback) if loopback else None
    tau_iface_s = (lb.get("tau_ms") / 1000.0) if (lb and lb.get("tau_ms")) else None

    def dc_payload(tid):
        """DC step trains must include their FIRST edge: it is the DAC-ON edge,
        and the sign of the whole measurement depends on starting there. The
        preceding 120 ms of marker trailer is silent, so starting early is safe."""
        y = payload(tid, head_ms=-10.0)
        if y is not None and tau_iface_s:
            y = undo_hp(y, sr, tau_iface_s)
        return y

    # --- noise floor -------------------------------------------------------
    nf = {}
    for tid, name in ((1, "apu_off_lcd_on"), (2, "apu_on_lcd_on"),
                      (3, "apu_off_lcd_off"), (4, "apu_on_lcd_off")):
        y = payload(tid)
        if y is None or len(y) < sr // 10:
            continue
        f, p = spectrum(y, sr)
        def at(hz, bw=60.0):
            """Prominence of a line above the surrounding noise floor, in dB."""
            inn = (f > hz - bw) & (f < hz + bw)
            out = ((f > hz - 12 * bw) & (f < hz - 2 * bw)) | \
                  ((f > hz + 2 * bw) & (f < hz + 12 * bw))
            if not inn.any() or not out.any():
                return None
            return float(p[inn].max() - np.median(p[out]))
        nf[name] = {"rms_dbfs": float(20 * np.log10(np.sqrt((y ** 2).mean()) + 1e-30)),
                    "line_9198_db": at(9198.0), "line_2x_db": at(2 * 9198.0),
                    "frame_59_7_db": at(59.7275, bw=8.0)}
    R["noise_floor"] = nf

    # --- wave DAC transfer + coupling (takes 10..25) -----------------------
    wave_dac, coupling = {}, []
    for lvl in range(16):
        y = dc_payload(10 + lvl)
        if y is None:
            continue
        m = dc_step_take(y, sr)
        if m is None:
            continue
        wave_dac[lvl] = m["amplitude"]
        if lvl >= 11:                      # best SNR levels only
            coupling.append(m["coupling_rate_1_s"])
    R["wave_dac_transfer"] = {str(k): v for k, v in wave_dac.items()}

    if len(wave_dac) >= 4:
        L = np.array(sorted(wave_dac)); A = np.array([wave_dac[k] for k in L])
        m, b = theil_sen(L, A)
        R["wave_dac_fit"] = {
            "slope_per_level": m, "intercept": b,
            "zero_crossing_level": float(-b / m) if m else None,
            "max_deviation_lsb": float(np.abs(A - (m * L + b)).max() / abs(m)) if m else None,
        }
    if coupling:
        r = float(np.median(coupling))
        R["coupling"] = {"rate_1_s": r, "tau_ms": 1000.0 / r,
                         "fc_hz": r / (2 * np.pi),
                         "per_cpu_cycle_factor": float(np.exp(-r / CPU_HZ)),
                         "interface_pole_removed": bool(tau_iface_s),
                         "interface_tau_ms": (tau_iface_s * 1000 if tau_iface_s else None)}

    # --- master volume (30..37) -------------------------------------------
    mv = {}
    for v in range(8):
        y = dc_payload(30 + v)
        if y is None: continue
        m = dc_step_take(y, sr)
        if m: mv[v] = m["amplitude"]
    R["master_volume"] = {str(k): v for k, v in mv.items()}
    if len(mv) >= 4:
        ref = mv.get(7)
        if ref:
            R["master_volume_vs_theory"] = {
                str(v): {"measured_ratio": mv[v] / ref, "theory_ratio": (v + 1) / 8.0}
                for v in sorted(mv)}

    # --- NR32 wave level (40..43) -----------------------------------------
    nr32 = {}
    for n in range(4):
        y = dc_payload(40 + n)
        if y is None: continue
        m = dc_step_take(y, sr)
        if m: nr32[n] = m["amplitude"]
    if nr32:
        # NR32 shifts the DIGITAL sample, and the step is measured against
        # DAC-off (analog zero). Because the DAC map is affine, halving the
        # digital value does NOT halve the analog step -- so compare against
        # the fitted DAC transfer at the shifted value, not a naive ratio.
        fit = R.get("wave_dac_fit")
        shift = {0: None, 1: 0, 2: 1, 3: 2}
        R["wave_output_level"] = {}
        for n in sorted(nr32):
            digital = 0 if n == 0 else (15 >> shift[n])
            pred = (fit["slope_per_level"] * digital + fit["intercept"]) if fit else None
            R["wave_output_level"][str(n)] = {
                "measured": nr32[n],
                "digital_value": digital,
                "predicted_from_dac_fit": pred,
                "error": (nr32[n] - pred) if pred is not None else None}

    # --- pulse DAC transfer (50..65) --------------------------------------
    f1k = 131072.0 / (2048 - 1917)
    pulse = {}
    for lvl in range(16):
        y = payload(50 + lvl)
        if y is None: continue
        # 50% square: fundamental amplitude = 2*Vpp/pi
        pulse[lvl] = float(np.pi * fundamental(y, sr, f1k) / 2.0)
    R["_pulse_probe_hz"] = f1k
    R["pulse_dac_transfer"] = {str(k): v for k, v in pulse.items()}
    if len(pulse) >= 8:
        L = np.array([k for k in sorted(pulse) if k > 0])
        A = np.array([pulse[k] for k in L])
        m, b = theil_sen(L, A)
        R["pulse_dac_fit"] = {"slope_per_level": m, "intercept": b,
                              "level0_residual": pulse.get(0)}

    # --- duty cycles (70..73) ---------------------------------------------
    duty = {}
    # The DAC's polarity, taken from the wave transfer: a negative slope means
    # digital 0 sits at the POSITIVE rail, so "above zero" in the recording is
    # the LOW half of the duty cycle.
    inverting = -1 if (R.get("wave_dac_fit") or {}).get("slope_per_level", 0) < 0 else 1
    R["dac_polarity"] = "inverting" if inverting == -1 else "non-inverting"
    f500 = 131072.0 / (2048 - 1786)
    for d in range(4):
        y = payload(70 + d)
        if y is None: continue
        b, a = sig.butter(2, [f500 * 0.4 / (sr / 2), min(0.99, f500 * 12 / (sr / 2))], "band")
        z = sig.filtfilt(b, a, y)
        frac = float((z > 0).mean())
        duty[d] = {"measured_high_fraction": frac,
                   "high_fraction_polarity_corrected": frac if inverting == 1 else 1.0 - frac,
                   "theory": [0.125, 0.25, 0.5, 0.75][d],
                   "fundamental": fundamental(y, sr, f500)}
    R["duty"] = {str(k): v for k, v in duty.items()}

    # --- envelope rates (80..93) ------------------------------------------
    env = {}
    for p_i in range(1, 8):
        for base, name in ((79, "down"), (86, "up")):
            y = payload(base + p_i)
            if y is None: continue
            t, e = rms_envelope(y, sr, win_ms=3.0)
            e = e / (e.max() + 1e-30)
            d = np.diff(e)
            thr = 0.25 * np.abs(d).max()
            steps = np.where(np.abs(d) > thr)[0]
            if len(steps) < 3: continue
            keep, last = [], -10 ** 9
            for i in steps:
                if i - last > 2: keep.append(i); last = i
            iv = np.diff(t[keep])
            iv = iv[(iv > 0.005) & (iv < 0.5)]
            if len(iv) < 2: continue
            env[f"{name}_{p_i}"] = {
                "measured_step_s": float(np.median(iv)),
                "theory_step_s": p_i / 64.0,
                "steps_detected": len(keep)}
    R["envelope"] = env

    # --- pitch cross-check (100..103) -------------------------------------
    pitch = {}
    for tid, freg in ((100, 0), (101, 1024), (102, 1750), (103, 2017)):
        y = payload(tid)
        if y is None: continue
        th = 131072.0 / (2048 - freg)
        got = peak_freq(y, sr, lo=th * 0.6, hi=th * 1.6)
        pitch[str(freg)] = {"theory_hz": th, "measured_hz": got,
                            "cents": float(1200 * np.log2(got / th)) if got > 0 else None}
    R["pitch"] = pitch

    # --- summing / clipping (130..134) ------------------------------------
    clip = {}
    for tid, name in ((130, "pu1"), (131, "pu1+pu2"), (132, "pu1+pu2+noise"),
                      (133, "all four")):
        y = payload(tid)
        if y is None: continue
        clip[name] = {"peak": float(np.abs(y).max()),
                      "p999": float(np.percentile(np.abs(y), 99.9)),
                      "rms": float(np.sqrt((y ** 2).mean()))}
    y = dc_payload(134)
    if y is not None:
        m = dc_step_take(y, sr)
        if m:
            ref = mv.get(7)
            clip["master_volume_0"] = {
                "amplitude": m["amplitude"],
                "ratio_to_master_7": (m["amplitude"] / ref) if ref else None,
                "theory_ratio": 1 / 8.0}
    R["summing"] = clip

    # --- edge shape (140, 141) --------------------------------------------
    edges = {}
    for tid, name in ((140, "master_7"), (141, "master_3")):
        y = dc_payload(tid)
        if y is None: continue
        e = ets_edge(y, sr)
        if e: edges[name] = e
    R["edge"] = edges

    # --- loopback correction ----------------------------------------------
    if lb:
        R["loopback"] = lb
        if lb.get("implausible"):
            R["loopback_trustworthy"] = False
        # The loopback contains the interface's OUTPUT and INPUT stages, but only
        # the input stage is in the console's path, so removing the whole loopback
        # pole slightly over-corrects. Quantify that rather than hiding it.
        if "coupling" in R and tau_iface_s:
            r = R["coupling"]["rate_1_s"]
            half = undo_hp_alternative_rate(r, tau_iface_s)
            R["coupling"]["bound_if_input_is_half_the_loopback"] = {
                "rate_1_s": half, "tau_ms": 1000.0 / half,
                "fc_hz": half / (2 * np.pi),
                "per_cpu_cycle_factor": float(np.exp(-half / CPU_HZ))}
        if edges and lb.get("edge"):
            for name, e in edges.items():
                lr = lb["edge"]["rise_10_90_us"]
                e["loopback_rise_10_90_us"] = lr
                e["limited_by_interface"] = bool(e["rise_10_90_us"] <= lr * 1.25)
                e["verdict_is_conservative"] = True

    if want_plots:
        write_plots(R, os.path.splitext(path)[0])
    return R


# A real analog loopback -- DAC, output stage, cable, input stage, ADC -- always
# has AC coupling somewhere, typically a corner between about 1 and 20 Hz. A time
# constant beyond this means the signal never went through the analog path.
LOOPBACK_MAX_PLAUSIBLE_TAU_MS = 1000.0


def undo_hp_alternative_rate(rate, tau_iface_s):
    """Console rate if only HALF the loopback's pole is in the console's path.
    Deconvolving by the full loopback removes a little too much; this is the
    other end of the bracket."""
    return max(rate - 0.5 / tau_iface_s, 1e-9)


def analyse_loopback(path):
    """Characterise the interface itself from a RECORDING of calibration.wav.

    Note "recording": this must be the file captured after playing calibration.wav
    out of the interface and back in through a cable. Passing the generated
    calibration.wav itself -- or a 4th Gen virtual Loopback capture, which never
    leaves the digital domain -- measures nothing and yields a correction of about
    zero that looks legitimate. Both mistakes are caught below.
    """
    y, sr, _ = load_mono(path)
    out = {"sample_rate": sr}
    m = dc_step_take(y, sr)
    if m:
        out.update({"coupling_rate_1_s": m["coupling_rate_1_s"],
                    "tau_ms": m["tau_ms"], "fc_hz": m["fc_hz"]})
        if m["tau_ms"] > LOOPBACK_MAX_PLAUSIBLE_TAU_MS:
            out["implausible"] = True
            print(
                f"\nWARNING: the loopback file shows essentially no AC coupling "
                f"(tau {m['tau_ms']/1000:.1f} s, fc {m['fc_hz']:.4f} Hz). No analog "
                f"path behaves like that.\n"
                f"         This is almost certainly NOT a recording. Either the "
                f"generated calibration.wav was passed directly, or a virtual/"
                f"internal loopback was recorded instead of a cable from the "
                f"outputs to the inputs.\n"
                f"         The correction computed from it will be ~zero, so the "
                f"console's coupling constant will come out UNCORRECTED while "
                f"appearing corrected. Re-record it through a physical cable.\n",
                file=sys.stderr)
    e = ets_edge(y, sr)
    if e:
        e.pop("shape", None); e.pop("t_us", None)
        out["edge"] = e
    return out


def write_plots(R, stem):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("(matplotlib not installed -- skipping plots; pip install matplotlib)",
              file=sys.stderr)
        return
    figs = 0
    if R.get("wave_dac_transfer"):
        d = R["wave_dac_transfer"]
        L = sorted(int(k) for k in d)
        plt.figure(figsize=(6, 4))
        plt.plot(L, [d[str(k)] for k in L], "o-")
        plt.axhline(0, lw=0.5, color="k")
        plt.xlabel("wave sample value"); plt.ylabel("step amplitude")
        plt.title("Wave DAC transfer"); plt.grid(alpha=.3); plt.tight_layout()
        plt.savefig(f"{stem}_wave_dac.png", dpi=120); plt.close(); figs += 1
    for name, e in (R.get("edge") or {}).items():
        plt.figure(figsize=(6, 4))
        plt.plot(e["t_us"], e["shape"])
        plt.xlabel("microseconds"); plt.ylabel("normalised")
        plt.title(f"Edge, equivalent-time ({name}, {e['edges_averaged']} edges)")
        plt.grid(alpha=.3); plt.tight_layout()
        plt.savefig(f"{stem}_edge_{name}.png", dpi=120); plt.close(); figs += 1
    print(f"(wrote {figs} plots next to the capture)", file=sys.stderr)


# --------------------------------------------------------------------------

def report(R):
    P = print
    P("=" * 72)
    P(f"ChipBoy capture analysis -- {R['capture']}  @ {R['sample_rate']:g} Hz")
    P("=" * 72)
    h = R["headroom"]
    P(f"peak level           {h['peak_dbfs']:.2f} dBFS"
      + ("   *** CLIPPED ***" if h["clipped"] else
         "   *** TOO QUIET ***" if h["too_quiet"] else "   (ok)"))
    P(f"takes decoded        {R['takes_found']}/{R['takes_expected']}")
    if R["takes_missing"]:
        P(f"  MISSING            {R['takes_missing']}")

    if R.get("coupling"):
        c = R["coupling"]
        P("\nAC COUPLING  (HARDWARE_REFERENCE.md 12)")
        if R.get("loopback_trustworthy") is False:
            P("  *** the loopback is not a real recording (see the warning above) --")
            P("      the interface pole was NOT removed and these figures are raw ***")
        tag = "interface pole removed" if c.get("interface_pole_removed") else "NO LOOPBACK -- includes the interface"
        P(f"  measured           tau {c['tau_ms']:.3f} ms   fc {c['fc_hz']:.2f} Hz"
          f"   per-cycle {c['per_cpu_cycle_factor']:.6f}   ({tag})")
        b = c.get("bound_if_input_is_half_the_loopback")
        if b:
            P(f"  other bound        tau {b['tau_ms']:.3f} ms   fc {b['fc_hz']:.2f} Hz"
              f"   per-cycle {b['per_cpu_cycle_factor']:.6f}")

    if R.get("wave_dac_fit"):
        f = R["wave_dac_fit"]
        P("\nWAVE DAC TRANSFER")
        P(f"  slope              {f['slope_per_level']:+.6g} per level")
        P(f"  zero crossing      level {f['zero_crossing_level']:.2f}"
          f"   (7.5 => digital 0 is a rail, not silence)")
        P(f"  max deviation      {f['max_deviation_lsb']:.3f} LSB from a straight line")

    if R.get("master_volume_vs_theory"):
        P("\nMASTER VOLUME (NR50)      measured / theory (v+1)/8")
        for v, d in sorted(R["master_volume_vs_theory"].items(), key=lambda kv: int(kv[0])):
            P(f"  {v}                  {d['measured_ratio']:.4f} / {d['theory_ratio']:.4f}")

    if R.get("wave_output_level"):
        P("\nWAVE OUTPUT LEVEL (NR32)  digital -> measured / predicted by DAC fit")
        for n, d in sorted(R["wave_output_level"].items(), key=lambda kv: int(kv[0])):
            pr = d["predicted_from_dac_fit"]
            P(f"  {n}   digital {d['digital_value']:2d}     {d['measured']:+.5f} /"
              f" {('%+.5f' % pr) if pr is not None else '   n/a  '}")

    if R.get("envelope"):
        P("\nENVELOPE RATES            measured / theory (n/64 s)")
        for k in sorted(R["envelope"], key=lambda s: (s.split('_')[0], int(s.split('_')[1]))):
            d = R["envelope"][k]
            err = 100 * (d["measured_step_s"] / d["theory_step_s"] - 1)
            P(f"  {k:9s}          {d['measured_step_s']*1000:7.2f} ms /"
              f" {d['theory_step_s']*1000:7.2f} ms  ({err:+.1f}%)")

    if R.get("pitch"):
        P("\nPITCH CROSS-CHECK         f_reg -> measured (error)")
        for k, d in sorted(R["pitch"].items(), key=lambda kv: int(kv[0])):
            P(f"  {k:>5s}              {d['theory_hz']:9.2f} Hz -> {d['measured_hz']:9.2f} Hz"
              f"  ({d['cents']:+.1f} cents)")

    if R.get("duty"):
        P(f"\nDUTY  (DAC is {R.get('dac_polarity','?')})   corrected / theory high fraction")
        for k, d in sorted(R["duty"].items(), key=lambda kv: int(kv[0])):
            P(f"  {k}                  {d['high_fraction_polarity_corrected']:.3f} / {d['theory']:.3f}"
              f"    (raw {d['measured_high_fraction']:.3f})")

    if R.get("summing"):
        P("\nSUMMING AND CLIPPING")
        for k, d in R["summing"].items():
            if "peak" in d:
                P(f"  {k:24s} peak {d['peak']:.5f}  rms {d['rms']:.5f}")
            else:
                P(f"  {k:24s} ratio {d['ratio_to_master_7']:.4f} / theory {d['theory_ratio']:.4f}")

    if R.get("edge"):
        P("\nEDGE SHAPE (equivalent-time)")
        for k, e in R["edge"].items():
            P(f"  {k:10s} {e['edges_averaged']:4d} edges  10-90% {e['rise_10_90_us']:.2f} us"
              f"  => bandwidth ~{(e['implied_bandwidth_hz'] or 0)/1000:.1f} kHz (0.35/t_r, first order)")
            if "limited_by_interface" in e:
                P("    " + ("LIMITED BY THE INTERFACE -- treat as a LOWER BOUND on amplifier bandwidth"
                            if e["limited_by_interface"] else
                            "faster than the interface's own edge -- this is the console"))

    if R.get("noise_floor"):
        P("\nNOISE FLOOR")
        for k, d in R["noise_floor"].items():
            P(f"  {k:18s} rms {d['rms_dbfs']:7.2f} dBFS   9198 Hz +{d['line_9198_db']:5.1f} dB"
              f"   59.7 Hz +{d['frame_59_7_db']:5.1f} dB   (prominence over local floor)")
    P("")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("capture")
    ap.add_argument("--loopback")
    ap.add_argument("--takes", default=None)
    ap.add_argument("--out", default=None)
    ap.add_argument("--model", default="dmg", choices=["dmg", "cgb"])
    ap.add_argument("--plots", action="store_true")
    a = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    manifest = json.load(open(a.takes or os.path.join(here, "takes.json")))
    R = analyse(a.capture, manifest, a.loopback, a.plots)
    R["model"] = a.model
    report(R)
    out = a.out or os.path.splitext(a.capture)[0] + "_measured.json"
    slim = dict(R)
    slim["edge"] = {k: {kk: vv for kk, vv in v.items() if kk not in ("t_us", "shape")}
                    for k, v in (R.get("edge") or {}).items()}
    json.dump(slim, open(out, "w"), indent=1)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
