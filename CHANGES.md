# CHANGES

Every departure from [`docs/CHIPBOY_SPEC.md`](docs/CHIPBOY_SPEC.md): what changed, why,
and what was considered. Deviating is expected. Deviating silently is not.

Also the place to record implementation status, so the spec stays a description of the
intended product rather than a progress report.

---

## Implementation status

| Milestone | Status |
|---|---|
| M0 — repository, spec, decisions | spec written; §18 decisions open |
| M1 — APU core + test-ROM harness | not started |
| M2 — analog stage + renderer | not started |
| M3 — main plugin shell | not started |
| M4 — bank + driver | not started |
| M5 — Voice plugin + link | not started |
| M6 — waves, frames, kits | not started |
| M7 — interface | not started |
| M8 — hardware validation | not started |

---

## Spec revisions

### 2026-09-05 — DMG *and* CGB ship in v1 (§6.5, §16.6, §17; `HARDWARE_REFERENCE.md` §11)

**Changed:** CGB moved from post-v1 to v1, resolving `[DECIDE] D8`. Added spec §6.5 on
model selection. Rewrote §16.6 around the actual capture rig. Named the host targets in
§3.1 and §16.5.

**Why:** both a DMG and a CGB are available to measure, which was the only reason CGB was
deferred. It is also the largest audible difference between models, and it is not only a
filter coefficient — CGB wave RAM is live-writable, so the wave channel *behaves*
differently: frame changes and kit streaming click on DMG and need not on CGB.

**Considered:** shipping DMG only and adding CGB later. Rejected — the capture session
costs nothing extra while the rig is set up, and retrofitting a second model after the
wave channel is built around DMG's re-trigger requirement would be more expensive than
designing for both now.

### 2026-09-05 — hardware capture tooling built (§16.6)

**Added:** `tools/capture/` — probe ROM, SM83 verifier, loopback generator, analyser,
and a synthetic-capture generator for testing the analyser; `docs/CAPTURE_GUIDE.md`.

**Notable during development**, all caught before any recording session:

- `WaveRamp` clobbered `DE`, the take interpreter's bytecode pointer, so the ROM ran
  off into garbage after take 110 and never terminated. Found by executing the ROM
  rather than trusting that it assembled.
- The original marker tones (3000/1500/750 Hz) sat exactly on harmonics of the probe
  tones (1000.6/500.3 Hz squares), so payload audio decoded as markers. Marker tones
  moved into the gaps between harmonics, and the analyser now matches candidates
  against the known take order.
- Takes 130–133 originally used a sustained DC level on the wave channel, which is
  invisible after the coupling capacitor. Rewritten as an AC ladder.
- The analyser derived DAC step sign from edge direction, which inverted every wave
  value below 7.5 — where the DAC output is genuinely negative. It now takes the sign
  from the data, and measures only DAC-ON edges.

**Known limitation:** equivalent-time sampling improves time resolution, not bandwidth.
If the amplifier's corner is above the converter's ~90 kHz limit the analyser reports a
lower bound and says so. This is audibly irrelevant and cosmetically relevant to the
oscilloscope display only.

### 2026-09-06 — capture guide: exact Reaper routing, 192 kHz, 4th Gen notes

**Changed:** `docs/CAPTURE_GUIDE.md` Steps 1-5 rewritten with the real rig.

- **192 kHz confirmed despite the DSP mixer being unavailable above 96 kHz.** The mixer
  is an input-to-output path we never needed, and losing it means the interface cannot
  route an input to an output at all — removing the only hardware feedback path.
- **Feedback is now prevented by construction, not by care:** the playback track routes
  to hardware outputs only with Master send unticked, and the record track sends nowhere
  with monitoring off. No input reaches any output regardless of what is plugged in.
- **4th Gen specifics:** Clip Safe, Auto Gain and Air must be off (all three change the
  signal during or between takes). The 4th Gen *virtual* Loopback input must NOT be used
  for calibration — it never leaves the digital domain and so skips every stage the
  calibration exists to measure, yielding a correction of ~zero that looks legitimate.

**Considered:** dropping to 96 kHz to keep the mixer. Rejected — the mixer has no role
here, and 96 kHz would halve the usable bandwidth for the edge-shape measurement.

### 2026-09-06 — analyser rejects a loopback that never went through analog

**Added:** `analyse.py` warns when a `--loopback` file shows no plausible AC coupling
(time constant over 1 s), and marks the corrected coupling figures as meaningless.

**Why:** two different mistakes produce a loopback file that is a perfect digital copy —
passing the generated `calibration.wav` instead of a recording of it, and recording a
4th Gen *virtual* Loopback instead of cabling outputs to inputs. Both yield a correction
of about zero, so the console's coupling constant comes out uncorrected while appearing
corrected. That is worse than not calibrating, because it looks right.

**Detection:** an un-played `calibration.wav` measures a 38.6 s time constant (0.004 Hz);
a real analog path is 1-20 Hz. The threshold sits at 1 s, far from both.

**Also:** docs used `cal.wav` and `calibration.wav` for the same file in different places.
Unified to `calibration.wav`, and the smoke test now states explicitly that it misuses
`--loopback` on purpose and that the resulting warning is expected there and nowhere else.

### 2026-09-06 — step measurement rewritten after the first real capture

The first DMG and CGB captures came back with a broken wave-DAC zero crossing
(4.7 and 5.4 on the DMG, 669 and -2616 on the CGB) and a CGB coupling constant of
51 ms against an expected 0.23 ms. The recordings were fine; `analyse.py` was not.

**Three faults, all in the step measurement:**

1. **Fixed 1-25 ms fit window.** Suits a DMG (tau ~5 ms), useless on a CGB, whose
   step has decayed to ~1% before the window opens — so it fitted the INTERFACE's
   27.5 ms tail and reported it as the console's. Reproduced in simulation: 50.66 ms
   measured against 0.23 ms true, matching the observed 51.01 ms. The window now
   scales to the observed decay.
2. **Amplitude extrapolated from a single-exponential fit.** The console's coupling
   capacitor and the interface's are in SERIES, so the decay is two-pole and the
   extrapolation is biased. Amplitude is now read directly at the edge and corrected
   for the few samples of decay before the peak.
3. **Rates subtracted instead of the pole being removed.** Rates add only at t=0;
   subtracting them under-read the DMG by ~5%. `undo_hp` now deconvolves the
   interface's measured pole before fitting.

**Validated against a simulation carrying the real time constants, the real
interface pole and the measured noise floors:** DMG tau 5.674 ms against 5.680 true
(-0.1%), CGB 0.226 against 0.225 (+0.5%), zero crossings 7.498 and 7.523.

**Also:** duty is now reported polarity-corrected. The real DMG measured
0.84/0.73/0.50/0.27 against theory 0.125/0.25/0.5/0.75 — exactly 1 - theory,
because the DMG's DAC is inverting. That polarity is now detected from the wave
transfer's slope and recorded as `dac_polarity`.

**What the first capture already established**, all from FFT-based measurements
that were never affected: pitch within 2.4 cents on both consoles; all seven
envelope rates within 2%; a linear pulse DAC transfer with a zero intercept and
level 0 reading 4.6e-6 (DAC-off is true silence); and the 9198 Hz LCD line at
+26 dB prominence on the DMG, falling to +2 dB with the LCD off.

### 2026-09-06 — probe ROM: wave trigger delay invalidated the DC-step takes

**Found by looking at the raw capture**, after two wrong guesses from summary JSON.
Every DC-step take stepped to the SAME value regardless of wave level for the first
~1 ms, then the real level appeared. Cause: the takes set `NR33 = $00`, i.e. wave
frequency 0, so the first sample after a trigger arrives `(2048 - f) * 2 = 4096`
cycles = **976 us** late (`HARDWARE_REFERENCE.md` section 10.1, "wave trigger delay").
The DAC-on step therefore landed on the channel's STALE sample buffer — identical for
every take — and `DAC(L)` only appeared a millisecond afterwards.

The reasoning that produced the bug was "all 32 samples are equal, so the frequency
does not matter." The frequency does not change the OUTPUT, but it does change how
long the stale buffer persists after a trigger.

**Fixed:** those takes now drive f = 2047 (`NR33 = $FF`, trigger `NR34 = $87`),
putting the first sample 0.48 us after the trigger. Requires a re-record.

**Confirmed valid in the meantime:** the level-dependent step that appears after the
delay gives `DAC(15) - DAC(0) = -0.115` on the DMG, against `0.00722 x 15 = 0.108`
from the pulse-channel transfer measured in the same capture. Two independent
channels agreeing to 6% says the console and the analysis are both sound.

**Still open:** the DAC-off transitions produce no visible step at all, while every
DAC-on does. Possibly a high-impedance disabled-DAC state. Not needed for the
measurement — only ON edges are used — but it should be understood before section 6.1
of the spec claims what a disabled DAC does.

---

## Departures from the spec

*None yet — no code has been written.*

### Format

```
### YYYY-MM-DD — short title (spec §N)

**Changed:** what the code does instead.
**Why:** the reason.
**Considered:** the alternatives, and why they lost.
```
