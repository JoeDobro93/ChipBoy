# ChipBoy — Hardware Capture Guide

Exactly what to do to measure a DMG and a CGB well enough to fit the emulation's
analog stage. Spec §16.6 says why each measurement exists; this says how to take it.

Budget about **2 hours** for the first session and 30 for repeats. The recording
itself is 2 minutes 36 seconds per console.

---

## What you need

| | |
|---|---|
| **DMG** | the original brick. A Pocket (MGB) is a different amplifier and is not a substitute. |
| **CGB** | captured separately — a CGB in DMG-compatibility mode still sounds like a CGB. |
| **Flash cart** | any 32 KB-capable cart. The ROM is plain ROM-only, no MBC. |
| **Focusrite Scarlett 18i20 (4th Gen)** | at **192 kHz**. See the 4th Gen notes in Step 1 — several of its features must be switched off. |
| **A 3.5 mm TRS → dual TS/XLR cable** | headphone jack to two **line** inputs. |
| **A second cable** | for the loopback: one interface output back to the input you will use. |
| **Fresh batteries or a clean PSU** | a sagging supply changes the amplifier's clip point. Batteries are cleaner than most wall adapters. |
| **A plain commercial cartridge** | for one noise-floor comparison (any game). |
| **Python 3.11+** | `pip install -r requirements.txt` from the repository root. Using PyCharm? See [`tools/capture/README.md`](../tools/capture/README.md#running-these-from-pycharm-windows) for setup, including a hardware-free smoke test to run **before** the session. |

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

## Step 1 — Why 192 kHz is fine without the DSP mixer

Focusrite Control 2 disables the internal monitor mixer above 96 kHz. **Run at 192 kHz
anyway.** We never wanted that mixer, and losing it removes the only hardware path that
could ever feed back.

The mixer is a DSP block that routes *inputs* to *outputs* inside the interface. This
procedure needs neither:

- **Inputs → Reaper.** A direct hardware capture path, always available.
- **Reaper → outputs.** A direct DAW playback path, always available.

With the mixer gone, the interface *cannot* route an input to an output at all. The only
remaining way to build a loop is in Reaper, and Step 3 eliminates that by construction.

### 4th Gen: four things must be OFF

Every one of these is a 4th Gen feature that silently changes the signal, which is fatal
for absolute measurements. Set them per-channel on **inputs 7 and 8** in Focusrite
Control 2.

| Feature | Setting | Why |
|---|---|---|
| **Loopback** | **Do not use it** — see below | It is a *virtual* path and bypasses everything we are measuring |
| **Clip Safe** | **OFF** | It reduces gain automatically when it sees clipping. A gain that moves during a take destroys every amplitude measurement and looks like console compression |
| **Auto Gain** | **OFF / not engaged** | Same reason: the gain must be a fixed, known constant across the loopback and both consoles |
| **Air** (Presence, Presence & Harmonic Drive) | **OFF** | Presence is a filter; Harmonic Drive adds distortion. We are measuring the console's filtering and distortion, not the interface's |

> ### The 4th Gen Loopback trap
>
> 4th Gen Scarletts offer a **Loopback** input that routes DAW playback straight back
> into the DAW. It is genuinely useful, and it is exactly wrong here.
>
> That path never leaves the digital domain. It skips the DAC reconstruction filter, the
> output stage, the cable, the input stage, its AC coupling, and the ADC anti-alias
> filter — **which is the entire list of things Step 4 exists to measure.** Recording it
> gives a near-perfect digital copy, the analyser computes a correction of about zero,
> and the console's coupling constant then comes out uncorrected while *looking* like it
> was corrected.
>
> **Use a physical cable from outputs 5/6 into inputs 7/8.** If a loopback measurement
> comes back with a time constant of seconds, or an edge that rises in a single sample,
> that is the virtual path and the take must be redone with a cable.

### Pre-flight: confirm the channels exist at 192 kHz

USB bandwidth is finite, so some interfaces drop channels at 176.4/192 kHz — usually
ADAT/S-MUX rather than analog, but confirm before wiring anything.

1. Set the interface to **192000 Hz** in Focusrite Control.
2. In Reaper: **Options → Preferences → Audio → Device**, ASIO driver = Focusrite USB
   ASIO, **Request sample rate 192000**, and enable the full input and output range.
3. Confirm **inputs 7/8** and **outputs 5/6** both still appear in Reaper's channel lists.

If outputs 5/6 vanish at 192 kHz, use whatever output pair does survive — the pair number
does not matter, only that it is not routed anywhere near the record path.

---

## Step 2 — Physical connections

**Do not connect the loopback cable and the Game Boy at the same time.** They share
inputs 7/8, and only one is ever needed.

### For the loopback (Step 4)

| From | To |
|---|---|
| Interface **Output 5** | Interface **Input 7** |
| Interface **Output 6** | Interface **Input 8** |

Plain 1/4" cables. TRS or TS both work.

### For the console (Step 5)

| From | To |
|---|---|
| Game Boy **headphone jack** | Interface **Input 7** (left) and **Input 8** (right) |

A 3.5 mm TRS → dual 1/4" TS cable. Unbalanced into a balanced line input is normal and
correct here.

> The Game Boy's headphone amplifier will be driving a ~10 kΩ line input rather than
> 32 Ω headphones, so it is very lightly loaded. That is the right choice: it is how
> people actually record a DMG, and it is what the model should reproduce. It is not
> identical to what headphones in your ears see.

### Before you start

- **Turn monitor speakers off and take headphones off.** Nothing in this procedure needs
  to be heard, and you will be sending step trains at line level.
- Batteries in the console, volume wheel at maximum.

---

## Step 3 — Reaper setup, and the rule that makes feedback impossible

**The rule: the playback track goes to hardware outputs only, never to the Master; the
record track sends nowhere and monitors nothing.** Follow it and there is no signal path
from any input to any output, so a loop cannot exist regardless of what is plugged in.

### Project settings

**File → Project Settings**

- **Sample rate 192000**, tick **Force project sample rate**.
- **Media → Format: WAV, 32-bit floating point.** Not 24-bit integer — float means an
  overloaded take is still salvageable.

### Track 1 — "CAL PLAY"

1. Insert `calibration.wav` on the track.
2. Click the track's **Route** button.
3. **Untick "Master send".** ← this is the important one.
4. **Add new hardware output → Output 5 / Output 6.**
5. Leave the track fader at 0.0 dB for now; Step 4 sets it.
6. No FX. No input monitoring.

Track 1 can now only reach outputs 5/6. It cannot reach the Master, so it cannot reach
your speakers or any other output.

### Track 2 — "GB REC"

1. **Input: Stereo → Input 7 / Input 8.**
2. **Record arm ON.**
3. **Record monitoring OFF** (click the speaker icon until it is off — not "on", not
   "auto"). You will watch meters, not listen.
4. **Route → untick "Master send".**
5. No FX. Fader at 0.0 dB.

### Verify the Master is not cabled

Check the **Master track's** hardware output. It should be **Outputs 1/2**, or nothing.
If the Master is routed to outputs 5/6, unroute it — that is the one configuration that
could turn an accidental monitor-enable into a loop.

### 30-second safety check

With the loopback cable connected, **Track 1 stopped**, and Track 2 armed with monitoring
off: the input 7/8 meters should sit at the noise floor and stay there. If they show
anything rising or howling, stop and re-check the two "Master send" tickboxes before
playing anything.

---

## Step 4 — Loopback calibration (do this FIRST, every session)

The interface's inputs are AC-coupled too, so they add a high-pass on top of the
console's. The DMG's corner is around 28 Hz — close enough to the interface's own that an
uncorrected measurement reads high. This step measures the interface so it can be
subtracted.

```sh
python tools/capture/make_calibration_wav.py calibration.wav
```

1. Loopback cable connected (Step 2), Game Boy **not** connected.
2. Set the input 7/8 **gain to minimum**. Pad off if those inputs have one. Air, Clip
   Safe and Auto Gain all off (Step 1) — a preamp's own colour is exactly what we are
   trying to measure out of the console later.
3. Record-arm Track 2, press record, play Track 1 through to the end (about 9 seconds),
   stop.
4. Look at the recorded peak. **Aim for −12 to −6 dBFS.** Adjust it with the **Track 1
   fader**, not the input gain — the input gain must stay untouched from here until the
   console runs are finished.
5. Save the take as `loopback_dmg.wav`.

**`calibration.wav` is what you play. `loopback_dmg.wav` is what you record, and it is
what `--loopback` takes.** Passing the generated `calibration.wav` to `--loopback` measures
nothing, exactly like the virtual-Loopback trap above. The analyser detects both and warns,
but it is easier not to make the mistake.

> **Why the fader and not the gain knob:** the whole point of this step is to characterise
> the input path at the exact gain the console will see. Turning the input knob between
> the loopback and the console makes the correction meaningless. Turning the playback
> fader only changes the source level, which does not matter.

Keep this file. Every console capture is analysed against it.

---

## Step 5 — Set the console level

1. **Stop playback. Disconnect the loopback cable from inputs 7/8.** Leave the input gain
   knobs exactly where Step 4 left them.
2. Connect the Game Boy headphone jack to inputs 7/8.
3. Power on. The ROM waits 3 seconds, then starts. The screen alternates **black during
   sync markers** and **white during measurements** — that is your progress indicator.
4. Watch the input meters through one full run **without recording**. Peaks should land
   between **−12 and −6 dBFS**, matching the loopback.
   - Too hot → clipping destroys the DAC-transfer and clipping measurements.
   - Too quiet → every amplitude measurement degrades.
5. If the level is wrong, adjust the input gain — **and then redo Step 4 at the new
   gain**, so the loopback still matches. There is no way around this; the two recordings
   must share a gain setting.
6. Power-cycle the console to restart the ROM, and record for real.

---

## Step 6 — The DMG runs

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

## Step 7 — The CGB runs

Repeat Steps 2–6 with the Game Boy Color, into `cgb_vol_max.wav` and `cgb_vol_mid.wav`,
plus a fresh `loopback_cgb.wav` if you changed anything about the gain.

The CGB is a genuinely different instrument in v1, not a tone variant: its coupling
corner is ~25× higher and its wave RAM is writable while the channel runs, so the wave
channel *behaves* differently (spec §6.5).

---

## Step 8 — Analyse

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

**Loopback correction comes out near zero.** You almost certainly recorded the 4th Gen
*virtual* Loopback input rather than a cable from outputs 5/6 into inputs 7/8. Redo Step 4
with the cable (Step 1).

**Amplitudes drift within a single take.** Clip Safe or Auto Gain is active on input 7
or 8. Turn both off and re-record — including the loopback, since the gain has moved.

**Wave DAC zero crossing is nowhere near 7.5.** Suspect clipping, or a high-pass filter
left enabled on the input. Check Step 4.2.

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
