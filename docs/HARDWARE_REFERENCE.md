# ChipBoy — DMG APU Hardware Reference

The behaviour ChipBoy's core has to reproduce. Everything here is documented hardware
behaviour unless it appears in §12, which lists the values that are empirical fits and
must be replaced with measurements from real hardware (spec §16.6).

Primary sources: Pan Docs (gbdev.io/pandocs), the gbdev wiki "Game Boy Sound Hardware"
page, blargg's *Game Boy Sound Operation*, and SameBoy's implementation. Read for
understanding only — see `LICENSING.md` §4.2.

Target hardware for v1 is the **DMG** (original Game Boy, 1989) and the **CGB** (Game Boy
Color) — both are available to measure. Other models are post-v1 (§11).

---

## 1. Clocks and time bases

| Clock | Frequency | Derivation |
|---|---|---|
| Master / CPU cycle | **4 194 304 Hz** | 2²² Hz |
| Frame sequencer | 512 Hz | CPU ÷ 8192, driven off a DIV bit |
| Length counter | 256 Hz | FS steps 0, 2, 4, 6 |
| Sweep | 128 Hz | FS steps 2, 6 |
| Volume envelope | 64 Hz | FS step 7 |
| V-blank ("tick" in most drivers) | **59.727 500 Hz** | 4 194 304 ÷ 70 224 |
| LCD line rate | 9 198.0 Hz | 4 194 304 ÷ 456 |

The master clock is the emulation's time base. Every event in the core is stamped with
a 64-bit CPU cycle count; nothing is expressed in host samples until the renderer.

The last two rows matter for two different reasons: V-blank is the rate at which real
drivers write registers (spec §8), and the LCD line rate is the dominant component of
the analog noise floor (§10.3).

---

## 2. Register map

| Addr | Name | Bits | Meaning |
|---|---|---|---|
| FF10 | NR10 | `-PPP NSSS` | CH1 sweep: period `P` (0–7), negate `N`, shift `S` (0–7) |
| FF11 | NR11 | `DDLL LLLL` | CH1 duty `D` (0–3), length load `L` (0–63) |
| FF12 | NR12 | `VVVV DPPP` | CH1 envelope: start volume `V` (0–15), direction `D`, period `P` (0–7) |
| FF13 | NR13 | `FFFF FFFF` | CH1 frequency LSB |
| FF14 | NR14 | `TL-- -FFF` | CH1 trigger `T`, length enable `L`, frequency MSB |
| FF16 | NR21 | `DDLL LLLL` | CH2 duty, length load |
| FF17 | NR22 | `VVVV DPPP` | CH2 envelope |
| FF18 | NR23 | `FFFF FFFF` | CH2 frequency LSB |
| FF19 | NR24 | `TL-- -FFF` | CH2 trigger, length enable, frequency MSB |
| FF1A | NR30 | `E--- ----` | CH3 DAC power |
| FF1B | NR31 | `LLLL LLLL` | CH3 length load (0–255) |
| FF1C | NR32 | `-VV- ----` | CH3 output level (0–3) |
| FF1D | NR33 | `FFFF FFFF` | CH3 frequency LSB |
| FF1E | NR34 | `TL-- -FFF` | CH3 trigger, length enable, frequency MSB |
| FF20 | NR41 | `--LL LLLL` | CH4 length load (0–63) |
| FF21 | NR42 | `VVVV DPPP` | CH4 envelope |
| FF22 | NR43 | `SSSS WDDD` | CH4 clock shift `S` (0–15), LFSR width `W`, divisor code `D` (0–7) |
| FF23 | NR44 | `TL-- ----` | CH4 trigger, length enable |
| FF24 | NR50 | `ALLL BRRR` | Vin left `A`, master volume left (0–7), Vin right `B`, master volume right (0–7) |
| FF25 | NR51 | `4321 4321` | Panning matrix: high nibble left, low nibble right, one bit per channel |
| FF26 | NR52 | `P--- 4321` | APU power `P` (write), per-channel active flags (read-only) |
| FF30–FF3F | Wave RAM | | 32 × 4-bit samples, **high nibble of each byte first** |

Vin (`A`/`B` in NR50) mixes an analog input from the cartridge. Essentially no
commercial cartridge used it. ChipBoy models the bits as present and always zero.

Writing 0 to NR52 bit 7 powers the APU down: on DMG this clears every register (except
the length counters and wave RAM) and resets the pulse duty position.

---

## 3. Frame sequencer

512 Hz, eight steps, cycling:

| Step | Length (256 Hz) | Sweep (128 Hz) | Envelope (64 Hz) |
|---|:---:|:---:|:---:|
| 0 | ● | | |
| 1 | | | |
| 2 | ● | ● | |
| 3 | | | |
| 4 | ● | | |
| 5 | | | |
| 6 | ● | ● | |
| 7 | | | ● |

The sequencer is clocked by a falling edge of a DIV bit, not by an independent timer.
Consequence: writing to DIV can clock the sequencer early. ChipBoy has no CPU and no
DIV register in normal operation, so this only matters inside the test-ROM harness
(spec §16.1), which does have one.

On a DMG the bit is bit 12 of the 16-bit counter behind DIV (DIV itself is the top
eight bits, so DIV bit 4), which falls every 8192 cycles. Power-on (NR52 bit 7) resets
the sequencer so that its next step is 0.

**Power-on while bit 12 is already set.** The sequencer then skips its first tick
entirely — no length, sweep or envelope clock — and until that skipped tick has passed
it counts as being about to run a step that does not clock length, so enabling a length
counter in that window clocks it once (§10.1, extra length clocking). Verified by
SameSuite `div_write_trigger_10`; the model is SameBoy's.

---

## 4. Channels 1 and 2 — pulse

**Frequency.** The duty step advances every `(2048 − f) × 4` CPU cycles, and there are
8 steps per period:

```
f_out = 131072 / (2048 − f)        f is the 11-bit value in NR13/NR14
```

| f | f_out |
|---|---|
| 0 | 64 Hz (the floor) |
| 44 | 65.41 Hz — **C2, the lowest playable note** |
| 1750 | 439.8 Hz — A4 |
| 2017 | 4228 Hz — nearest to C8, **+17.3 cents sharp** |
| 2047 | 131 072 Hz |

Pitch is a *period*, not a frequency, so resolution collapses at the top of the range.
This is reproduced, not corrected (spec §2 C4). B1 and everything below it is
unreachable on these two channels.

**Duty.** Four patterns, one bit per duty step:

| NR11 bits 7–6 | Duty | Pattern |
|---|---|---|
| 0 | 12.5% | `0000 0001` |
| 1 | 25% | `1000 0001` |
| 2 | 50% | `1000 0111` |
| 3 | 75% | `0111 1110` |

75% is spectrally identical to 25% — it is the same wave inverted. The difference is
phase, which matters for the DC step at note boundaries and for how one note joins the
next, not for timbre.

**Digital output** is `duty_bit ? volume : 0`, where `volume` is the envelope's current
0–15. Note that at volume 0 the channel still outputs a *constant digital 0*, which the
DAC maps to a rail, not to silence — see §9.

**The duty position counter is not reset by a trigger.** It resets only on APU power-off.
Every note therefore begins at whatever phase the previous one left behind.

---

## 5. Channel 3 — wave

**Frequency.** One 4-bit sample is consumed every `(2048 − f) × 2` CPU cycles, 32 samples
per period:

```
f_out       = 65536  / (2048 − f)      one octave below the pulse channels
sample_rate = 2097152 / (2048 − f)
```

| f | f_out | sample rate |
|---|---|---|
| 0 | 32 Hz | 1 024 Hz |
| 44 | 32.70 Hz — **C1, the lowest playable note** | 1 046 Hz |
| 1865 | 358 Hz | **11 460 Hz** — the rate conventionally used for sample kits |
| 2047 | 65 536 Hz | 2 097 152 Hz |

**Volume.** Two bits only, applied as a right shift:

| NR32 bits 6–5 | Level | Shift |
|---|---|---|
| 0 | mute | — |
| 1 | 100% | 0 |
| 2 | 50% | 1 |
| 3 | 25% | 2 |

There is no envelope on this channel. Finer volume means rescaling the samples in wave
RAM, which spends bit depth to buy level.

**Wave RAM access.** On **DMG**, wave RAM cannot be read or written while the channel is
running: reads return `FF` and writes are ignored, except during the exact cycle the
channel is fetching a byte. Drivers therefore clear NR30 bit 7, write, and re-trigger —
which costs a DAC-off/DAC-on step and a position reset. **This is why changing the
waveform mid-note clicks on a DMG, and the click is not optional.**

"Exact cycle" is one 2 MHz APU cycle: an access on the CPU cycle of the fetch or the
one after it lands on the byte being fetched (at the position *after* the advance);
anything else sees `FF` or is dropped. Established by replaying blargg's tests 09 and
12 against their DMG checksums, which admit no wider window.

On **CGB** the access lands on the byte currently being fetched, so live updates work
but tear if they race the read pointer.

**Trigger** resets the position counter to 0, and the sample buffer holds its previous
value until the next timer expiry, so the first sample after a trigger is delayed. That
first expiry comes **6 CPU cycles later** than the period alone (blargg 09, 10 and 12
fix this jointly with the access window above). Triggering while the channel is about
to read — the next fetch due within 2 CPU cycles — corrupts wave RAM on DMG (§10.2).

---

## 6. Channel 4 — noise

A 15-bit LFSR. Each clock: `bit0 XOR bit1` is computed, the register shifts right, and
the result is written into bit 14. When NR43 bit 3 (width) is set, the result is *also*
written into bit 6, shortening the period to 127 and producing a pitched, metallic tone.
Channel output is `NOT bit0`, gated by the envelope volume exactly as the pulse channels
are.

**Clock rate.** The timer period is `divisor << shift`:

| Divisor code | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| Divisor | **8** | 16 | 32 | 48 | 64 | 80 | 96 | 112 |

```
f_lfsr = 4194304 / (divisor << shift)
```

Range 4.57 Hz (divisor 112, shift 13) to 524 288 Hz (divisor 8, shift 0). Shift values
14 and 15 are invalid — the channel stops receiving clocks.

The resulting pitch grid is coarse and **not monotonic** in the (shift, divisor) pair.
ChipBoy ships a curated note map rather than pretending the parameter is continuous
(spec §9.4).

---

## 7. The three modulation units

### Envelope (CH1, CH2, CH4)

Clocked at 64 Hz. Start volume 0–15, direction (up/down), period 0–7 where **0 disables
the envelope entirely**.

| Period | Step interval | Full 15 → 0 |
|---|---|---|
| 1 | 15.6 ms | 234 ms |
| 2 | 31.3 ms | 469 ms |
| 3 | 46.9 ms | 703 ms |
| 4 | 62.5 ms | 938 ms |
| 5 | 78.1 ms | 1.17 s |
| 6 | 93.8 ms | 1.41 s |
| 7 | 109.4 ms | 1.64 s |

**That is the entire hardware decay palette: seven rates and off.** Anything smoother is
a driver writing NRx2 repeatedly on its own tick (spec §8.3).

Writing NRx2 with the top five bits zero (volume 0, direction down) **disables the DAC**
and turns the channel off.

### Length counter

Clocked at 256 Hz. Counts down from `64 − t` (CH1/2/4) or `256 − t` (CH3); at zero the
channel is disabled. Maximum 250 ms on the pulse and noise channels, 1.0 s on wave.

### Sweep (CH1 only)

Clocked at 128 Hz. Period 0–7 (0 = off, but the internal timer reloads with 8), shift
0–7, and a direction bit.

```
f' = f ± (f >> shift)
```

Overflow past 2047 disables the channel. The frequency written back is the *new* value,
so sweep compounds. Note the negate-mode quirk in §10.1.

---

## 8. Mixer and master volume

Per channel, in order: the 4-bit digital value → that channel's DAC → NR51's two gate
bits decide whether the analog result reaches the left sum, the right sum, both, or
neither → the two sums pass through NR50's master volume.

```
gain = (v + 1) / 8        v = 0..7
```

**Master volume 0 is not silence — it is 1/8.** This is a common emulator error and it
is audible.

Panning has four states per channel: off, left, right, both. There is no pan law and no
intermediate position.

---

## 9. DACs — where the character starts

Each channel has its own 4-bit DAC. Two properties matter more than any other detail in
this document:

1. **Digital 0 is a rail, not silence.** The DAC maps 0–15 across its full output range;
   the conventional emulator formulation is `out = value / 7.5 − 1.0`, so 0 → −1.0 and
   15 → +1.0. Absolute polarity is irrelevant (all four DACs share it); the relationship
   is not.
2. **A disabled DAC holds its last output.** Measured (§12.7), and not what the
   conventional model says. Disabling a DAC does not move the output at all; the
   summing node simply keeps the level that channel was sitting at. Enabling it again
   steps from that held level to the new digital value — nothing if they are equal, a
   full two-rail swing if the channel comes back at the opposite end. At power-on the
   held level is analog zero, so the very first DAC-on of a channel is the familiar
   half-swing step.

So the *DAC-on* click exists and the *DAC-off* click does not. Together with the AC
coupling in §10, the DAC-on step produces the clicks, pops and thumps of Game Boy music,
including the wave-channel re-trigger tick: NR30 bit 7 is set a few cycles before NR34
triggers, and in those cycles the disabled channel's digital output is 0, so the DAC
leaps to the positive rail, then to the stale sample buffer, then to the new wave. A
channel decaying to volume 0 makes one step (to the rail); clearing its DAC afterwards
makes none.

The four DACs sum into one amplifier through one coupling capacitor. Each channel's DC
step therefore moves every other channel's baseline. This is why ChipBoy has no
per-channel outputs (spec §2 C9).

**Composite resolution.** Four channels of 0–15 sum to 0–60: 61 distinct levels, about
5.9 bits, unevenly spaced. Quantisation happens per channel *before* the sum. Master
volume is applied in the analog domain and does not re-quantise.

---

## 10. Quirks the core must implement

### 10.1 Required for v1

| Quirk | Behaviour |
|---|---|
| Duty phase preservation | Trigger reloads the frequency timer but does not reset the duty position (§4). |
| DAC disable via NRx2 | Writing volume 0 + direction down to NRx2 disables the DAC and the channel. |
| Trigger with DAC off | Triggering a channel whose DAC is off does not enable it. |
| Sweep negate quirk | Once a sweep calculation has been performed in negate mode, clearing the negate bit disables the channel. |
| Extra length clocking | Enabling the length counter during the first half of a length period clocks it an extra time. |
| Wave RAM access rules | DMG: reads return `FF` and writes are ignored while the channel runs, outside the fetch cycle (§5). |
| Wave trigger delay | The sample buffer holds its previous value until the first timer expiry after a trigger. |
| Master volume floor | NR50 level 0 = 1/8, not mute (§8). |
| APU power-off | Clears registers (except length counters and wave RAM) and resets duty position. |

### 10.2 Required, gated behind the harness or a driver choice

| Quirk | Notes |
|---|---|
| Zombie envelope | Writing NRx2 while a channel runs has model-specific behaviour that real drivers exploit for fine volume control. Must be implemented for test-ROM parity; the ChipBoy driver only uses it if §8.3 selects the software-envelope mode. |
| Wave RAM corruption on trigger | DMG-only. Triggering CH3 while it is reading corrupts wave RAM. Implemented for parity; unreachable through normal ChipBoy driver behaviour. |
| DIV-triggered frame sequencer stepping | Only observable with a CPU present, i.e. in the test-ROM harness. |
| Power-on sequencer skip | Switching the APU on while DIV bit 12 is set skips the first sequencer tick (§3). Harness-only in practice; implemented for SameSuite parity. |

---

## 11. Model differences

| Model | Ships | Notes |
|---|---|---|
| **DMG** | **v1** | Low-frequency AC coupling — the fat one. Wave RAM is not accessible while the channel runs (§5), so waveform changes cost a re-trigger. |
| **CGB** | **v1** | Much more aggressive AC coupling — audibly thin. Wave RAM *is* live-writable (§5), and the trigger-corruption bug is absent, so the wave channel behaves differently, not just sounds different. A CGB running a DMG game still sounds like a CGB, so it cannot stand in for a DMG when capturing. |
| **MGB** (Pocket) | post-v1 | Different amplifier; commonly described as cleaner and quieter. No hardware to measure. |
| **AGB** (GBA) | post-v1 | The PSG path is quieter and noisier, mixed alongside the DirectSound channels. No hardware to measure. |
| **"Pro Sound"** | post-v1 | Not a model but a common hardware modification: a tap taken ahead of the amplifier and volume pot. Cleaner, and worth modelling as an output-path option. |

---

## 12. Measured values

Captured 2026-09-07 from **one DMG (brick) and one CGB**, batteries, volume wheel at
maximum unless stated; Focusrite Scarlett 18i20 4th Gen at 192 kHz through line inputs,
interface pole removed via loopback. Method: `tools/capture/`. Raw analyser output:
`measurements/2026-09-07/`. Per-unit variation is real — a second console of each would
be worth measuring before any of this is treated as the family's rather than this unit's.

### 12.1 AC coupling — the headline constant

| | DMG | CGB |
|---|---|---|
| **per-CPU-cycle factor** | **0.999963** | **0.999494** |
| τ | 6.44 ms | 0.471 ms |
| f_c | 24.7 Hz | 338 Hz |
| Blargg's published fit | 0.999958 (28.0 Hz) | 0.998943 (706 Hz) |

The DMG agrees with the published fit to ~12%, independently, on different hardware —
strong confirmation of both. The CGB measures a corner at **half** the published value.
It is repeatable (338 Hz at max, 298 Hz at mid), so it is this unit, not noise; CGB
boards went through several revisions. Model this unit's number and say so.

The DMG's time constant lengthened at mid volume (8.9 ms vs 6.4 ms). The coupling
capacitor sees the volume pot's output impedance, which rises off maximum. The plugin
models maximum, which is also how a DMG is conventionally recorded.

### 12.2 DAC transfer

| | DMG | CGB |
|---|---|---|
| Polarity | **inverting** | **inverting** |
| Linearity, max deviation from a line | 0.16 LSB (mid run) · 1.3 LSB (max run) | 1.8 LSB |
| Wave zero crossing (want 7.5) | **7.42 / 7.41** | **7.47 / 7.59** |
| Wave slope vs pulse slope, same capture | agree to **3%** | agree to 12% |
| Pulse level 0 (DAC disabled) | 7 × 10⁻⁶ — silence | 9 × 10⁻⁶ — silence |

Digital 0 and digital 15 sit symmetrically either side of the DAC-off level, on both
consoles, at both volumes: **"digital 0 is a rail, not silence" is measured, not
asserted.** Treat the DAC as linear. The wave and pulse channels were measured by
different methods (a DC step and an FFT) and agree, which is the cross-check that
matters.

### 12.3 Master volume (NR50)

DMG, both volumes: follows `(v+1)/8` within 4% for v = 1..7. **v = 0 measures
0.12–0.17 of v = 7, not zero** — the "level 0 is 1/8, not mute" claim, measured. The
CGB's master-volume takes were too noisy to read (its fast coupling erodes the
settled-level window); same register, same APU, same law.

### 12.4 Noise floor — LCD on, volume max

| | DMG | CGB |
|---|---|---|
| Broadband RMS relative to the four-channel full-scale peak | **−58 dB** | **−60 dB** |
| 9198 Hz LCD line, prominence over the local floor | **+26 dB** | **+43 dB** |
| 59.7 Hz frame component | +20 dB | +34 dB |
| 9198 Hz with LCDC bit 7 cleared | +2 dB — gone | **+41 dB — persists** |

The DMG's line is unambiguously the LCD: it drops 24 dB when the display stops. The
CGB's is 17 dB stronger and **does not stop** when LCDC bit 7 is cleared — either that
unit's PPU keeps its line clock running with the display off, or the component has
another source at the same frequency. For the plugin it makes no difference: a playing
console has its display on. Model the CGB line as always present.

### 12.5 Confirmed to specification

Both consoles: pitch within 2.6 cents at f = 0, 1024, 1750, 2017; all seven envelope
rates, both directions, within 2% of n/64 s; duty patterns correct once the inverting
polarity is accounted for.

### 12.6 Not measured — still estimates

| Quantity | Status |
|---|---|
| Amplifier bandwidth / edge shape | The analyser's edge window is dominated by the coupling decay, and the interface's own edge (9 µs rise, ~39 kHz) bounds the method regardless. **Estimated: ≥ 39 kHz.** Audibly irrelevant; cosmetically relevant to the scope display. |
| Clip point and symmetry | The summing ladder's sources sit at different frequencies, so their peaks never align. No clipping was observed at four channels full, NR50 = 7. Needs a coherent-peak test. |

### 12.7 DAC-off — the disabled DAC holds its level

From the DMG wave DC-step takes (six repetitions each of NR30 = $80 + trigger for
120 ms, then NR30 = $00 for 120 ms), read at 50 µs resolution:

| Take | Wave value | DAC-on step, rep 1 | DAC-on step, reps 2 and 3 | DAC-off, every rep |
|---|---|---|---|---|
| 10 | 0 | +0.057 (to the positive rail) | **none** (< 0.002) | none |
| 25 | 15 | +0.058 then −0.063 (rail to rail) | a single 50 µs blip of +0.016, then **nothing** | none |

Rep 1 starts from analog zero because the APU was power-cycled between takes. From rep
2 on, the DAC comes back at the level it was holding: for value 0 that is the level it
left, so nothing happens; for value 15 the enable lands the disabled channel's digital
0 on the positive rail for the 12 cycles until the trigger — a two-rail excursion that
averages to +0.016 over 50 µs — and then returns to the held negative rail. No
repetition shows any movement at DAC-off, fast or slow, to within 0.002 of a 0.057
step, over the full 120 ms. The model in §9 follows from this directly. What happens
over seconds or minutes (leakage) was not measured; the plugin holds indefinitely.
Whether APU power-off resets the held levels to analog zero at once or slowly was not
measured either; the plugin resets at once, and never powers the APU off in normal use.
