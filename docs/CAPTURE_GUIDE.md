# ChipBoy — Hardware Capture Guide

Exactly what to do to measure a DMG and a CGB well enough to fit the emulation's
analog stage. Spec §16.6 says why each measurement exists; this says how to take it.

Budget about **90 minutes** for the first session and 30 for repeats. The recording
itself is 2 minutes 36 seconds per console.

---

## What you need

| | |
|---|---|
| **DMG** | the original brick. A Pocket (MGB) is a different amplifier and is not a substitute. |
| **CGB** | captured separately — a CGB in DMG-compatibility mode still sounds like a CGB. |
| **Flash cart** | any 32 KB-capable cart. The ROM is plain ROM-only, no MBC. |
| **Focusrite 18i20** | at **192 kHz**. |
| **A 3.5 mm TRS → dual TS/XLR cable** | headphone jack to two **line** inputs. |
| **A second cable** | for the loopback: one interface output back to the input you will use. |
| **Fresh batteries or a clean PSU** | a sagging supply changes the amplifier's clip point. Batteries are cleaner than most wall adapters. |
| **A plain commercial cartridge** | for one noise-floor comparison (any game). |
| **Python 3** | `pip install numpy scipy soundfile` (add `matplotlib` if you want plots). |

---

## Step 0 — Build the ROM

```sh
cd tools/capture
./build.sh          # needs RGBDS: https://rgbds.gbdev.io
python verify_rom.py
```

`verify_rom.py` executes the ROM on a small SM83 interpreter and must print
**83 takes, ids 1..141, stack pointer FFFE**. It also writes `takes.json`, which the
analyser needs. If it fails, stop — do not record with a ROM that failed verification.

Copy `chipboy_probe.gb` to the flash cart.

---

## Step 1 — Interface setup (do this once, exactly)

Everything downstream assumes these settings are identical between the loopback and
both consoles. **Write them down.**

1. In the Focusrite control panel, set the sample rate to **192000 Hz**.
2. Use a **LINE** input. Not the instrument/Hi-Z input, and not a mic preamp with gain —
   a preamp's own colour is precisely what we are trying to measure out of the console.
3. Set input gain to **minimum**, and pad off.
4. Disable every processing option: no Air, no compressor, no high-pass filter, no
   loopback routing, no monitor mix into the record path.
5. In Reaper: **File → Project Settings → Media → Format: WAV, 32-bit floating point**.
   Not 24-bit integer — float means an overloaded take can still be salvaged.
6. Record-arm one stereo track. Set the track fader to 0.0 dB and remove all FX.

> **Why so fussy:** the numbers we are extracting are absolute voltages and time
> constants. A filter or a gain change between two recordings makes them
> incomparable, and the error looks like a property of the console.

---

## Step 2 — Loopback calibration (do this FIRST, every session)

The interface's inputs are AC-coupled too, so they add a high-pass on top of the
console's. The DMG's corner is around 28 Hz — close enough to the interface's own that
an uncorrected measurement reads high. This step measures the interface so it can be
subtracted.

```sh
python tools/capture/make_calibration_wav.py calibration.wav
```

1. Play `calibration.wav` out of an interface **output**.
2. Cable that output to the **same input, at the same gain** the console will use.
3. Record it. Save as `loopback_dmg.wav`.
4. Peak should land around **−6 dBFS**. If it clips, lower the playback level, not the
   input gain — the input gain must stay identical to the console recording.

Keep this file. Every console capture is analysed against it.

---

## Step 3 — Set the console level

1. Batteries in, flash cart in, **volume wheel at maximum**.
2. Headphone jack → line input.
3. Power on. The ROM waits 3 seconds, then starts. The screen alternates **black during
   sync markers** and **white during measurements** — that is your progress indicator.
4. Watch input meters through one full run without recording. **Peak should land between
   −12 and −6 dBFS.**
   - Too hot → clipping destroys the DAC-transfer and clipping measurements.
   - Too quiet → every amplitude measurement degrades.
   - Adjust with the interface gain, then **redo Step 2** so the loopback matches.
5. Power-cycle the console to restart the ROM.

---

## Step 4 — The DMG runs

Record each run start-to-finish, beginning **before** power-on and stopping after the
screen begins its slow end-of-run flash. Extra silence at either end is harmless; a
truncated run is not.

| # | File | Setup |
|---|---|---|
| 1 | `dmg_vol_max.wav` | volume wheel **maximum** |
| 2 | `dmg_vol_mid.wav` | volume wheel at **12 o'clock** |
| 3 | `dmg_noise_gamecart.wav` | swap the flash cart for a **commercial game cart**, record ~30 s of its silence or title screen |

Run 3 is not a probe run — it isolates flash-cart noise from console noise, so the noise
model in §6.4 describes the console rather than your cart.

**Do not touch the volume wheel during a run.** It is an analog control ahead of the
output and it changes the noise and distortion balance; captures at different positions
are not comparable, which is why runs 1 and 2 are separate files.

---

## Step 5 — The CGB runs

Repeat Steps 2–4 with the Game Boy Color, into `cgb_vol_max.wav` and `cgb_vol_mid.wav`,
plus a fresh `loopback_cgb.wav` if you changed anything about the gain.

The CGB is a genuinely different instrument in v1, not a tone variant: its coupling
corner is ~25× higher and its wave RAM is writable while the channel runs, so the wave
channel *behaves* differently (spec §6.5).

---

## Step 6 — Analyse

```sh
python tools/capture/analyse.py dmg_vol_max.wav \
       --loopback loopback_dmg.wav --model dmg --plots
```

Read the top of the report first:

- **peak level** must say `(ok)`. `*** CLIPPED ***` means redo the run at lower gain —
  results from a clipped file are not usable.
- **takes decoded** must be `83/83`. Fewer means markers were missed; see below.

Then the measurements. What matters, and what "right" looks like:

| Section | What it feeds | Sanity check |
|---|---|---|
| **AC coupling** | `HARDWARE_REFERENCE.md` §12, the single most audible constant | DMG per-cycle factor near 0.99996; CGB near 0.9989 |
| **Wave DAC transfer** | the DAC map | zero crossing near **level 7.5** — that is the "digital 0 is a rail, not silence" claim, measured |
| **Master volume** | NR50 law | ratios track (v+1)/8; level 0 is ~1/8, **not** mute |
| **Wave output level** | NR32 | measured should match "predicted from DAC fit" |
| **Envelope rates** | cross-check only | within a few % of n/64 s |
| **Pitch** | cross-check only | within a few cents of 131072/(2048−f) |
| **Summing and clipping** | amplifier saturation | peak growth per added channel; sub-linear growth is clipping |
| **Edge shape** | amplifier bandwidth | see the caveat below |
| **Noise floor** | §6.4, and the Headphone Noise switch | 9198 Hz prominence should be visible with the LCD on |

The run writes `dmg_vol_max_measured.json`. That file is the deliverable — its numbers
replace the estimates in `HARDWARE_REFERENCE.md` §12.

---

## About edge shape — read this before trusting the number

The report prints a 10–90% rise time and an implied bandwidth. Under it, one of:

- **"faster than the interface's own edge — this is the console"** → a real measurement.
- **"LIMITED BY THE INTERFACE — treat as a LOWER BOUND"** → the interface's anti-alias
  filter is rounding the edge, not the console's amplifier. The true bandwidth is *at
  least* the printed figure, and possibly far higher.

The analyser uses **equivalent-time sampling**: the console's crystal and the converter's
are independent, so 250 repeated edges land at random sub-sample phases and can be
combined into a much finer-grained picture than the 192 kHz grid alone. That improves
*time resolution*, not *bandwidth* — content above the converter's ~90 kHz corner is gone
and no amount of averaging brings it back.

**This is fine for the plugin.** Anything the capture cannot see is above 90 kHz, and the
plugin's output is band-limited far below that. Fitting the amplifier low-pass to the
192 kHz capture is correct for everything audible. The only thing that suffers is the
cosmetic fidelity of the per-channel oscilloscope display. If a "lower bound" verdict
appears, record it in §12 as **estimated**, not measured.

(The loopback verdict errs toward saying "interface limited": the interface's DAC and ADC
share a clock, so equivalent-time sampling cannot sharpen the loopback edge and its
measured rise is an upper bound. Erring that way is deliberate — it reports a bound
rather than overclaiming.)

---

## Troubleshooting

**Fewer than 83 takes decoded.** Almost always level. Check the peak reading; re-run
between −12 and −6 dBFS. Also confirm the recording covers the whole run — it starts
3 s after power-on and lasts 2:36.

**Analyser reports clipping.** Lower interface gain, redo the loopback at the new gain,
re-record.

**Coupling constant looks far off.** Confirm you passed `--loopback`. Without it the
figure includes the interface's own coupling and will read high.

**Wave DAC zero crossing is nowhere near 7.5.** Suspect clipping, or a high-pass filter
left enabled on the input. Check Step 1.4.

**Noise floor identical with the LCD on and off.** Check that runs were not normalised or
gated anywhere in the chain, and that the console is on batteries — a switching PSU can
swamp the console's own noise.

**Nothing decodes at all.** Confirm the ROM runs: the screen should alternate black and
white. If it stays uniform, the flash cart did not take the ROM.

---

## What to do with the results

1. Copy the measured values into `HARDWARE_REFERENCE.md` §12, replacing the estimates,
   and mark anything the analyser flagged as a bound as **estimated**.
2. Note the console's board revision if you can read it, alongside the numbers. Units
   vary, and a later capture from another DMG will want something to compare against.
3. Commit the `*_measured.json` files somewhere — they are the evidence behind every
   constant in the model. Recordings themselves are git-ignored; keep them elsewhere.
