# CHIPBOY — Game Boy DMG Sound Chip Instrument
## Build Spec v0.1 — planning

> This document is the **source of truth**. Where it and any other document, mockup or
> conversation disagree, this wins.
>
> `[DECIDE]` marks an unresolved item. All of them are collected in §18.
>
> Unlike a normal build spec, this one has an explicit post-v1 section. §17 marks what
> ships in the first release and what does not. Everything not marked post-v1 ships.

Companion documents:

- [`HARDWARE_REFERENCE.md`](HARDWARE_REFERENCE.md) — the DMG APU's registers, timing and
  analog behaviour. The emulation contract.
- [`LICENSING.md`](LICENSING.md) — third-party obligations, the three release paths, and
  the development rules that keep them all open.
- [`../CHANGES.md`](../CHANGES.md) — every departure from this document, with reasons.

---

## 0. How to use this document

### 0.1 Deviating is expected; deviating silently is not

This spec was written before any code existed. It will be wrong in places that only
become visible once the thing makes sound. Record every departure in `CHANGES.md`: what
changed, why, and what was considered.

### 0.2 Audit before building, and again once it plays

1. **What would a DAW user expect that is missing?** Compare against Plogue chipsynth
   MSX/SFC, Defective Records VST-GB, and the workflow of LSDj and hUGETracker. If all
   of them have something and ChipBoy does not, check that the omission is deliberate
   (§2 lists what is deliberately absent) rather than forgotten.
2. **What is exposed that should not be?** A control earns its place only if it
   corresponds to something the chip can actually do, or to a workflow decision the
   software genuinely cannot make. Emulation internals are not controls.
3. **What would feel bad?** Anything that makes the user configure something the
   software could work out is a defect. Anything that makes the restrictions feel like
   bugs rather than the instrument is a UI failure, not an engine failure.

### 0.3 Definitions

| Term | Meaning |
|---|---|
| **APU** | The DMG's audio processing unit: four channels, four DACs, one mixer. |
| **Channel** | One of the four hardware voices: **PU1**, **PU2** (pulse), **WAV** (wave), **NOI** (noise). Fixed capabilities; not interchangeable. |
| **Instance** | One loaded copy of the main plugin. One instance is exactly one APU. |
| **Main plugin** | *ChipBoy*. Hosts the APU, the bank, and the audio output. |
| **Voice plugin** | *ChipBoy Voice*. A control surface for one channel of one instance, on its own DAW track. Produces no audio. |
| **Link** | The connection between a Voice plugin and a channel of a main instance. |
| **Tick** | The driver's own clock, at which all parameter changes and table steps happen. Analogous to a Game Boy driver's V-blank interrupt. Not the same as the APU's frame sequencer. |
| **Bank** | The collection of instruments, tables, waves and kits held by a main instance. |
| **Instrument** | A named set of register settings and modulation for one channel type. |
| **Table** | A 16-step sequence of volume, transpose and commands, advanced one step per tick. |
| **Wave** | A named sequence of one or more **frames**. |
| **Frame** | 32 × 4-bit samples — one full load of wave RAM. |
| **Kit** | A set of one-shot samples for the wave channel, played by streaming wave RAM. |
| **Register-true** | A parameter whose range and quantisation come from the hardware register it drives. |

---

## 1. Product thesis

ChipBoy is a Game Boy sound chip, not a chiptune synth. Every sound it makes is one a
real DMG could make, produced by the same mechanism.

That means it is a *cycle-accurate emulation of the DMG APU driven by a music driver*,
with a model of the analog output stage on the end. Nothing bypasses the chip. A vibrato
knob does not modulate a pitch — it writes an 11-bit period register on a tick boundary,
and the resulting quantisation is why it sounds like a Game Boy.

**The audience** is DAW users who want to write in a piano roll with LSDj's sound-design
vocabulary, without booting a Game Boy or learning a tracker. That is the whole reason
the product is two plugins (§11): a single plugin would put four channels of automation
on one track, which is exactly the thing that makes chip plugins unpleasant to sequence.

**The competition's mistake**, and the thing to get right, is that most Game Boy plugins
emulate the *digital* chip and stop. A square wave from a PC emulator looks like a
square wave. A square wave recorded from a DMG does not: it droops across every flat
top, its edges are rounded and overshoot, its baseline shifts when another channel
starts, and its notes begin and end with clicks. All of that comes from four 4-bit DACs
summing into one amplifier through one capacitor (`HARDWARE_REFERENCE.md` §9–10), and
all of it is in scope.

---

## 2. Hard constraints

**The restrictions are the product.** These are not defaults or presets. They are the
contract, and code that violates one is broken regardless of how it sounds.

**C1 — One instance is one APU: four monophonic voices.**
PU1, PU2, WAV, NOI. No polyphony, no voice stacking, no doubling a part across two
channels automatically, no "unison". More voices means another instance.

**C2 — All amplitude control inside the chip is the chip's.**
PU1/PU2/NOI: 4-bit level (0–15) with seven envelope rates and off. WAV: two bits
(mute / 25% / 50% / 100%). Master: NR50's three bits per side, where 0 is 1/8 and not
silence. There is no continuous gain anywhere inside the model, and no interpolation
between steps.

**C3 — The one continuous gain is outside the chip.**
The main plugin has an output trim applied *after* the analog stage. It is a fader on
the plugin's output, presented as such. It is the escape hatch for fine level control,
and it is the only one.

**C4 — Pitch is an 11-bit period.**
Notes are quantised to what the period allows. High notes are audibly sharp or flat
(tens of cents near the top of the range). PU1/PU2 cannot play below C2; WAV cannot play
below C1. None of this is corrected.

**C5 — Panning is NR51.**
Off, left, right, or both, per channel. No pan law, no intermediate positions, no width.

**C6 — Everything happens on tick boundaries.**
Parameter changes, automation, table steps, vibrato, arpeggios: all sampled and applied
at the driver tick (§8), never per sample. The hardware's own modulation units run on
the APU's frame sequencer at their own fixed rates and are unaffected by the tick.

**C7 — The artefacts are permanent.**
DAC on/off steps, AC coupling and its droop, envelope stepping, wave-RAM reload clicks,
pitch quantisation, the noise channel's non-monotonic pitch grid. These are not
"character" controls, not amounts, not toggles.

**C8 — Exactly one switch changes what you hear: Headphone Noise.**
On/off, in the main plugin, defaulting to on. It controls the analog noise-floor model
(§6.4) and nothing else. Every other switch in the product is workflow or display, never
accuracy.

**C9 — Stereo output only. No per-channel outputs.**
The four DACs share one summing node and one coupling capacitor, and their interaction —
one channel's DC step moving another's baseline — is part of the sound. Separate outputs
would have to falsify it. A channel that needs its own bus goes in its own instance.

**C10 — Constraints come from the APU, not from LSDj and not from cartridges.**
Register widths, timings, quantisation and channel capabilities are inherited and
inviolable. Bank sizes, sample lengths, instrument counts and table counts are *ours* —
LSDj's limits there come from ROM budget, which ChipBoy does not have. Do not import a
constraint that has no hardware cause.

---

## 3. Targets, build, licensing

### 3.1 Platforms and formats

- **macOS 11+**, universal (arm64 + x86_64). **Windows 10+ x64.**
- **VST3**, **AU** (macOS), **Standalone**. **CLAP** `[DECIDE] D2` — recommended; see
  `LICENSING.md` §3.
- AAX is out of scope.

**Host targets.** **Reaper** and **FL Studio** on Windows are primary and are what
development is validated against; **Logic** on macOS is the third. Logic is AU-only, so
the AU build is not optional. Reaper supports CLAP natively and recent FL Studio versions
do too — confirm the target FL version before settling `[DECIDE] D2`.

Both plugins are instruments (VST3 `Instrument`, AU `aumu`). The Voice plugin is an
instrument that outputs silence; see §11.6 for why it is not a MIDI effect.

### 3.2 Build

JUCE 8 / C++20 / CMake ≥ 3.24, dependencies via `FetchContent`, tests in Catch2, CI on
GitHub Actions building all targets on both platforms and running `ctest`. This mirrors
the SecondTake project's setup deliberately — it is known to work on both platforms.

### 3.3 Licensing rules — hard

Full analysis in [`LICENSING.md`](LICENSING.md). The rules that bind development:

**L1** — `Source/core` links **no JUCE and no copyleft code**, ever. Plain C++20 and the
standard library. CI enforces it.
**L2** — **No code is copied** from any GPL/LGPL/MPL emulator. Read them to understand
the hardware; implement from documentation and measurement. Cite the reference that
resolved a question in a comment.
**L3** — **No LSDj-derived content** in the repository or in any binary. Formats may be
implemented; content may not be bundled.
**L4** — Every dependency is recorded in `LICENSING.md` §1 *before* it is added.
**L5** — Outside contributions require a CLA, or are not accepted.
**L6** — The repository stays private until `[DECIDE] D1` is resolved.

### 3.4 Repository layout

```
ChipBoy/
  CMakeLists.txt
  README.md  LICENSE.md  CHANGES.md
  .github/workflows/
  docs/
    CHIPBOY_SPEC.md            this document
    HARDWARE_REFERENCE.md
    LICENSING.md
  Source/
    core/                      no JUCE, no GUI, headless-testable (L1)
      Apu/                     cycle-exact DMG APU + event stream
      Analog/                  DACs, mixer, output stage, noise floor
      Render/                  BLEP, area sampling, decimation, reference path
      Driver/                  tick engine, instruments, tables, voices
      Bank/                    bank model, file format, kit import
      Link/                    shared-memory transport, discovery, framing
    plugin/
      shared/                  parameter IDs, state versioning, link glue
      main/                    ChipBoy
      voice/                   ChipBoy Voice
    ui/
    tools/                     test-ROM harness, kit importer CLI, capture analyser
  Tests/                       Catch2
```

`TestRoms/` is fetched at configure time and git-ignored (§16.1).

### 3.5 Threading

The audio thread owns the APU, driver, analog stage and renderer, and does no
allocation, no locking and no file I/O. Bank edits happen on the message thread against
a copy; the finished copy is published by a single atomic pointer swap and the old one
is released back on the message thread. Link I/O is lock-free single-producer /
single-consumer (§11.3).

---

## 4. System architecture

```
   ChipBoy Voice (track 2)      ChipBoy Voice (track 3)      ...
        │ notes + params              │ notes + params
        └──────────────┬──────────────┘
                       ▼   shared memory, one block of latency  (§11)
   ┌───────────────────────────────────────────────────────────┐
   │ ChipBoy (track 1)                                          │
   │                                                            │
   │  L2  Driver ── tick engine, instruments, tables, voices    │
   │       │  register writes, stamped in CPU cycles            │
   │       ▼                                                    │
   │  L1  APU core ── cycle-exact, 4.194304 MHz                 │
   │       │  {cycle, channel, level 0-15, dacOn} events        │
   │       ▼                                                    │
   │  L3  Analog ── DACs, NR51 gate, sum, NR50, amp, coupling   │
   │       ▼                                                    │
   │  L4  Render ── BLEP / area sampling → host rate            │
   │       ▼                                                    │
   │      output trim (the only continuous gain, C3)            │
   └───────────────────────────────────────────────────────────┘
                       ▼
                  stereo out
```

`L0` is the validation harness (§16.1): a minimal SM83 CPU that exists only so real Game
Boy test ROMs can be run against L1 in CI. It ships in no binary.

The direction of the arrows is the whole design. Musician-facing parameters do not
reach the audio; they reach the driver, which writes registers. There is no path from a
knob to the output that skips the chip.

---

## 5. The APU core (L1)

### 5.1 Contract

Behaviour is defined entirely by `HARDWARE_REFERENCE.md`. The core is **accepted when it
passes blargg's `dmg_sound` tests 01–11 and SameSuite's APU tests** under the L0 harness
(§16.1). That is the acceptance criterion; "sounds right" is not.

### 5.2 Shape

- Time base is a `uint64_t` CPU cycle counter at 4 194 304 Hz. No floats.
- The public surface is the hardware's: `write(addr, value, cycle)`, `read(addr, cycle)`,
  `runTo(cycle)`, `reset()`.
- Output is **not samples**. It is a stream of
  `{ uint64_t cycle; uint8_t channel; uint8_t level; bool dacOn; }` — one entry per
  change in any channel's DAC input — plus a second, sparse stream of mixer changes,
  `{ cycle, NR50, NR51, powered }`, since routing and master volume sit after the DACs
  and belong to the analog stage. The analog stage and renderer consume both.
- Advance by **next-event scheduling**, not per-cycle ticking. Between register writes,
  every timer's next expiry is known analytically.
- Every quirk in `HARDWARE_REFERENCE.md` §10.1 is implemented in v1. §10.2's are
  implemented for test-ROM parity even where the ChipBoy driver never reaches them.

### 5.3 What the core does not know about

Notes, instruments, ticks, MIDI, the host, or JUCE. It is a chip.

---

## 6. The analog output stage (L3)

Signal order, matching the hardware:

```
per channel:  digital 0-15 ──► 4-bit DAC ──► NR51 gate (L / R / both / off)
                              (DAC off → holds its last level)
                                     │
        ── sum of four, per side ────┤
                                     ▼
                          NR50 master volume, (v+1)/8
                                     ▼
                    amplifier: bandwidth limit + slew
                                     ▼
                          soft clip (asymmetric)
                                     ▼
                    AC coupling — one-pole high-pass
                                     ▼
                   noise floor  (Headphone Noise, C8)
                                     ▼
                        output trim  (outside the chip, C3)
```

### 6.1 Non-negotiables

- **Digital 0 maps to a rail; a disabled DAC holds its last level.** Every click in
  Game Boy music comes from this (`HARDWARE_REFERENCE.md` §9, measured §12.7). An
  implementation where "volume 0" means silence is wrong, and so is one where DAC-off
  returns to zero: on the hardware the DAC-*on* step is the click, and DAC-off is
  silent.
- **The high-pass is applied once, after the sum** — there is one capacitor, not four.
  This is what makes one channel's DC step move another's baseline, and it is why C9
  exists.
- Master volume is analog: it scales, it does not re-quantise.

### 6.2 Filters

One-pole high-pass with the DMG time constant (`HARDWARE_REFERENCE.md` §12), computed in
the high-rate domain so the coefficient is exact per CPU cycle rather than per host
sample. Amplifier bandwidth as a low-pass in the same domain — which conveniently means
the signal is genuinely band-limited before decimation, so anti-aliasing is mostly
physics rather than a bolted-on filter (§7).

### 6.3 Saturation

Asymmetric soft clip at the measured amplifier limits. Reachable in normal use: four
channels at volume 15 with master volume 7 clips on real hardware, and that is part of
the "loud DMG" sound. **Not yet measured** (`HARDWARE_REFERENCE.md` §12.6): until the
coherent-peak capture is done, the clip is a symmetric placeholder that only engages
above three and a half aligned channels.

### 6.4 Noise floor — the one switch

Modelled from measurement (§16.6): broadband hiss, the 9 198 Hz LCD line component and
its harmonics, and a 59.73 Hz frame component. Present whenever the emulated console is
powered, independent of whether any channel is playing — because that is how it behaves.

**Headphone Noise** (on/off, default on) is the only control over it, and per **C8** the
only control in the product that changes what you hear. Off gives the clean signal path.
There is no amount.

### 6.5 Model selection — DMG and CGB

v1 ships **two consoles**, because both are available to measure. Model selection is not
an accuracy switch (**C8**): it chooses *which real hardware* is emulated, and each
model's artefacts are then as fixed as **C7** requires.

The difference is not only tonal:

| | DMG | CGB |
|---|---|---|
| AC coupling | f_c ≈ 28 Hz — fat | f_c ≈ 706 Hz — thin |
| Wave RAM while running | not writable; a frame change costs a DAC-off, a write and a re-trigger | writable live; updates tear if they race the read pointer |
| Wave RAM corruption on trigger | present | absent |

So **the wave channel behaves differently between models**, not merely sounds different:
frame changes and kit streaming click on DMG and need not on CGB. Both are real hardware
behaviour, so both are fixed within their model. A patch records which model it was
designed for, and the UI says so when it is loaded under the other.

MGB and AGB remain post-v1 (§17) — no hardware to measure.

---

## 7. Rendering (L4)

The APU emits a piecewise-constant signal whose transitions land on any of 4.19 million
cycle boundaries per second. Sampling that naively at 48 kHz aliases badly — an artefact
of the emulator, not of the hardware, and therefore a defect.

**Primary path: BLEP.** Each level change is inserted at its exact fractional sample
position as a band-limited step residual. Alias-free, and cheap when transitions are
sparse.

**Fallback: exact area summation.** When a channel's event rate exceeds a threshold
(the noise channel at divisor 8 / shift 0 clocks at 524 kHz — roughly 12 events per
output sample at 48 kHz), integrate the staircase over each output sample instead. The
content is broadband there and BLEP buys nothing.

**Reference path: brute force.** Tick at 4.194304 MHz and decimate with a long
windowed-sinc. Used only in tests and offline bounce, as the thing the fast path is
proven against (§16.3).

The nonlinear part of §6 runs at 2–4× the output rate and is decimated after.

### 7.1 Determinism requirement

**For the same MIDI and automation input, output must be bit-identical regardless of
host block size.** This is testable (§16.4) and it forces the right implementation: tick
positions derive from absolute sample counts rather than per-block accumulation, and
BLEP residuals carry across block boundaries. Treat a failure here as a correctness bug.

Supported host rates 44.1–192 kHz. The internal cycle domain never changes; only the
renderer's output ratio does.

---

## 8. The driver (L2)

The driver is a virtual Game Boy music engine. It is the only thing that writes APU
registers, and it does so on its own clock, exactly as a real driver's interrupt handler
does. This is what makes ChipBoy sound like a Game Boy rather than like a synth with
Game Boy waveforms.

### 8.1 The tick

| Source | Rate | Notes |
|---|---|---|
| **Host-synced** | ticks per beat, 1–48, default 24 | Default. Tables, arpeggios and vibrato lock to the grid. |
| **V-blank** | 59.7275 Hz free-running | What a real driver almost always uses. |
| **Custom** | 1–240 Hz free-running | Real drivers do use the hardware timer at other rates; this is not a cheat. |

Default `[DECIDE] D3` is host-synced, on the argument that a DAW user expects an arp to
line up with the bar and that the tick *rate* was always a driver choice — only its
existence and its quantising effect are hardware.

Tick boundaries are computed from the absolute sample position of the transport (or of a
free-running counter when the host is stopped), never accumulated per block — see §7.1.

### 8.2 What happens on a tick

In this order, for each of the four channels:

1. Sample the channel's automatable parameters (§12.1). A parameter that changed several
   times within the tick contributes only its value at the boundary.
2. Advance the active table by one step; apply its volume, transpose and commands.
3. Advance arpeggio, vibrato, slide and wave-frame counters.
4. Compute the resulting register values and emit writes to the APU, stamped with the
   cycle corresponding to this tick.

The APU's own units — envelope at 64 Hz, length at 256 Hz, sweep at 128 Hz — run on the
frame sequencer and are **not** affected by the tick. A driver tick that rewrites NRx2
interacts with a 64 Hz envelope that is already running; that interaction is real and is
reproduced.

Kit streaming is the exception: refilling wave RAM every 32 samples happens far faster
than any tick (§9.8) and is scheduled in the cycle domain against the channel's actual
consumption, the way a real driver uses a timer interrupt rather than V-blank.

### 8.3 Envelopes: two honest modes, per instrument

**Hardware envelope** (default). Start volume 0–15, direction, rate 0–7. One write to
NRx2 on note-on and then the chip runs it. Seven decay rates and off — that is the whole
palette (`HARDWARE_REFERENCE.md` §7).

**Software envelope.** An arbitrary volume table written on every tick, exactly as LSDj
does it. Smoother and more flexible, still quantised to 16 levels and to the tick.

Neither is a cheat and neither is "more accurate" — they are the two things real Game Boy
drivers do. What is forbidden is a third mode that interpolates between levels.

### 8.4 Vibrato

Applied as a signed offset to the 11-bit period register, recomputed on each tick.

| Field | Range | Notes |
|---|---|---|
| Shape | triangle / square / saw up / saw down | |
| Speed | 1–15 ticks per step | |
| Depth | 0–15 | **In raw period units, not cents.** |
| Delay | 0–255 ticks | |

Depth is deliberately in period units. The same depth is a wider interval at high pitch
than at low, because the period is not linear in frequency — that asymmetry is the sound.

### 8.5 Pitch, transpose and slide

All pitch handling operates on the 11-bit period. Note → period is computed once and
rounded to the nearest integer; every subsequent modification is integer arithmetic on
that value. A note above the reachable range is clamped and reported in the UI; a note
below it does not sound (C4).

Slide (`L`) steps the period toward the target by a fixed increment per tick, so slides
are faster at the top of the range than the bottom. Correct, and characteristic.

---

## 9. The bank

One bank per instance. It holds everything reusable, and it is the single source of
truth for instrument definitions (§12.5).

### 9.1 Slot counts

| | Slots |
|---|---|
| Instruments | 128 |
| Tables | 64 |
| Waves | 64 |
| Kits | 32 |

These are fixed so that automation parameters have stable value ranges (§12.1), **not**
for authenticity. Per **C10**, LSDj's counts come from cartridge budget and are not
inherited.

### 9.2 Instrument — common fields

| Field | Range | Register / effect |
|---|---|---|
| Name | 16 chars | — |
| Channel type | PULSE / WAVE / KIT / NOISE | Determines which channels can host it |
| Output | off / L / R / both | NR51 |
| Length | off, or 1–64 (1–256 for WAVE) | NR11/21/31/41 + length enable |
| Table | none, or 1–64 | §9.5 |
| Vibrato | §8.4 | period register |
| Transpose | on / off | Whether the table's transpose column applies |
| Note-off behaviour | kill / release / ignore | §10.4 |
| Retrigger | retrigger / legato | §10.5 |

A PULSE instrument may load on PU1 or PU2 (sweep fields are inert on PU2). WAVE and KIT
load only on WAV; NOISE only on NOI. The UI does not offer invalid combinations.

### 9.3 Instrument — by type

**PULSE**

| Field | Range | Register |
|---|---|---|
| Duty | 12.5 / 25 / 50 / 75 % | NR11/NR21 bits 7–6 |
| Duty sequence | up to 16 duty values, 1 per tick, looped | repeated writes |
| Envelope volume | 0–15 | NR12/NR22 |
| Envelope direction | down / up / off | NR12/NR22 |
| Envelope rate | 0–7 | NR12/NR22 |
| Sweep rate *(PU1 only)* | 0–7 | NR10 |
| Sweep direction *(PU1 only)* | up / down | NR10 |
| Sweep shift *(PU1 only)* | 0–7 | NR10 |

**WAVE**

| Field | Range | Notes |
|---|---|---|
| Wave | 1–64 | A wave is a sequence of frames (§9.7) |
| Frame advance | 0–15 ticks per frame, 0 = hold | |
| Frame loop | loop / one-shot / ping-pong | |
| Volume | mute / 25 / 50 / 100 % | NR32 — two bits, no envelope |

**Changing frames costs a click.** On DMG, wave RAM is not writable while the channel
runs, so a frame change is a DAC-off, a write, and a re-trigger
(`HARDWARE_REFERENCE.md` §5). On CGB it does not — wave RAM is writable live (§6.5).
Each is fixed behaviour under **C7** for its model; neither is an option within a model.

**KIT** — see §9.8.

**NOISE**

| Field | Range | Register |
|---|---|---|
| Envelope volume / direction / rate | as PULSE | NR42 |
| LFSR width | 15-bit (noise) / 7-bit (metallic) | NR43 bit 3 |
| Pitch mode | note map / manual | §9.4 |
| Clock shift *(manual)* | 0–13 | NR43 bits 7–4 |
| Divisor *(manual)* | 0–7 | NR43 bits 2–0 |
| Noise sweep | −7…+7 shift steps per tick | repeated NR43 writes |

### 9.4 The noise note map

Noise pitch is a coarse, non-monotonic grid over (shift, divisor)
(`HARDWARE_REFERENCE.md` §6). ChipBoy ships a curated map from MIDI note to the pair that
comes closest, one for each LFSR width, and exposes the raw pair in manual mode. It does
not pretend the parameter is continuous, and it does not hide the duplicates.

### 9.5 Tables

16 steps. Each step:

| Column | Range |
|---|---|
| Volume | blank, or 0–15 |
| Transpose | blank, or −60…+60 semitones |
| Command 1 | none, or a command + argument |
| Command 2 | none, or a command + argument |

One step per tick. At the end: loop to step 1, hop to a given step, or stop and hold the
last values. A table is shared by every instrument that references it; editing it changes
all of them, which is the point.

### 9.6 Commands

Lettering is LSDj-familiar; **behaviour is defined here**, and arguments are base 10.

| Cmd | Argument | Effect |
|---|---|---|
| `A` | volume 0–15, rate 0–7, direction | Rewrite the envelope register (NRx2) |
| `C` | two offsets, 0–15 semitones each | Chord: cycle root → +a → +b, one per tick |
| `D` | 0–15 ticks | Delay the note |
| `F` | frame 1–16 | Select wave frame (WAV only) |
| `H` | step 1–16, or 0 to stop | Hop within the table |
| `K` | 0–15 ticks | Kill the note after N ticks (clears the DAC — this pops) |
| `L` | rate 0–15 | Slide the period toward the target note |
| `O` | off / L / R / both | Set NR51 for this channel |
| `P` | −128…+127 | Add a signed offset in raw period units |
| `R` | 1–15 ticks | Retrigger every N ticks |
| `S` | signed | PU1: set sweep. NOI: step the clock shift per tick. Inert elsewhere. |
| `V` | speed 1–15, depth 0–15 | Set vibrato |
| `W` | wave 1–64 | Select wave slot (WAV only) |

### 9.7 Waves and frames

A **frame** is 32 × 4-bit samples: one complete load of wave RAM. A **wave** is a named
sequence of 1–16 frames, so waveform-sequencing timbres are a single bank object rather
than an arrangement of separate ones.

The editor is a 32 × 16 grid. Frames may be copied, interpolated between, generated from
common shapes (sine, saw, triangle, pulse at any width), and drawn freehand. Generated
shapes are quantised to 4 bits on creation, not on playback — what is shown is what
plays.

### 9.8 Kits — one-shot samples

A kit is a set of one-shot samples played on WAV by streaming wave RAM.

**Import.** Any audio file the host can read → mono → resampled to the kit's playback
rate → quantised to 4 bits. Quantisation is rounding by default; dither and noise
shaping are offered and clearly labelled as *not* what a Game Boy does.

**Playback.** The wave channel consumes 32 samples per wave-RAM load, so the driver
refills every 32 samples — at the conventional 11 460 Hz rate, every 2.79 ms. On DMG that
refill requires the DAC-off / write / re-trigger cycle of §9.3, at every block boundary.
The resulting granularity is the sound of DMG sample playback, and it is fixed (**C7**).

**Pitch and rate are the same control.** The wave channel has one frequency register, so
transposing a kit sample changes its playback rate exactly as it changes its pitch. There
is no resampler in the chip and there is not one in ChipBoy. Playing a kit an octave up
plays it twice as fast.

**No length limit** beyond available memory. Per **C10**, LSDj's kit size limit is a ROM
constraint, not a chip constraint.

| Field | Range |
|---|---|
| Samples per kit | up to 32 |
| Note map | one sample per MIDI note, with a default chromatic layout `[DECIDE] D9` |
| Playback rate | 1 024 Hz – 44 100 Hz, quantised to what the period register allows |
| Loop | one-shot / loop / loop from point |

---

## 10. Notes

### 10.1 Latch on note-on

**A note captures the channel's parameter values at note-on and keeps them for its whole
life.** Changing the instrument parameter mid-note does not change the sounding note; it
changes the next one.

This is the mechanism that gives a piano roll the per-note assignment LSDj has in its
note columns. Automating instrument select on the Voice plugin's track and playing notes
against it is equivalent to typing a different instrument number on each row.

A per-channel **Live follow** switch (workflow, not accuracy — C8) makes parameters apply
to the sounding note instead, for sound design and for automation sweeps that are meant
to be heard.

### 10.2 Velocity

`[DECIDE] D4` — default is **velocity → envelope start volume**, quantised to 0–15. The
alternatives, selectable per channel: velocity → instrument select (banks of 16), or
velocity ignored.

Whatever the mapping, the result is one of 16 levels. There is no velocity curve that
produces anything else (**C2**).

### 10.3 Keyswitches

An optional keyswitch range below the playable range selects the instrument. Off by
default; when on, the switched-out notes are removed from the playable range and the UI
says so.

### 10.4 Note-off

Per instrument:

- **Kill** (default for PULSE, WAVE, NOISE) — clear the DAC. This produces a step to
  analog zero and therefore a click, which is what a Game Boy does.
- **Release** — stop rewriting the envelope and let the hardware envelope finish.
- **Ignore** (default for KIT one-shots) — the note rings until the next note or until a
  length counter or `K` command ends it.

### 10.5 Stealing and legato

One voice per channel, so every note steals. Last-note priority. Per instrument:

- **Retrigger** (default) — write the trigger bit: envelope restarts, length reloads,
  and on WAV the wave position resets. The pulse duty phase does *not* reset
  (`HARDWARE_REFERENCE.md` §4), so each note starts from wherever the last one left off.
- **Legato** — write only the period register. No trigger, no envelope restart, no click.

---

## 11. The two plugins and the link

### 11.1 Roles

**ChipBoy** (main) hosts the APU, the bank, the analog stage and the audio output. It
accepts MIDI on its own track, multi-timbrally: MIDI channel 1 → PU1, 2 → PU2, 3 → WAV,
4 → NOI. Loaded on its own, it is a complete instrument.

**ChipBoy Voice** claims one channel of one main instance and drives it from its own DAW
track: its own piano roll, its own automation lanes, its own instrument selection. It
produces silence.

**A channel claimed by a Voice plugin ignores the main plugin's direct MIDI.** One owner
per channel, always.

### 11.2 Why shared memory rather than in-process pointers

The obvious implementation — a process-global registry that both binaries see — fails in
exactly the hosts people use. Logic sandboxes AU plugins, Bitwig can isolate plugins per
process, Cubase sandboxes VST3, Reaper bridges on request, and two separate plugin
binaries do not share statics unless they also share a dynamic library.

**Named OS shared memory** works across all of it, and additionally lets a VST3 Voice
drive an AU main. The payload is a few dozen bytes per tick, so the cost is irrelevant.

### 11.3 Transport

- A small fixed-size **directory** region lists live main instances: UUID, display name,
  process ID, heartbeat counter. Entries whose heartbeat has stopped for a timeout are
  reaped.
- Each main instance owns one region containing a header (magic, version, sample rate,
  block size, heartbeat) and four **channel slots**. Each slot holds a lock-free SPSC
  ring of timestamped note events and a double-buffered parameter snapshot.
- Everything is plain-old-data with fixed offsets. Real-time safe: atomics only, no
  allocation, no locks, no syscalls in `processBlock`.
- A version field is checked on connect; mismatched versions refuse to link and say so
  rather than misinterpreting the layout.

### 11.4 Latency, and why it is one block

Hosts do not guarantee the order in which tracks are processed, and the order can change.
If the main plugin happens to run before a Voice plugin in a given block, that Voice's
events for the block have not been written yet.

The fix is to make order irrelevant: **the main plugin always renders one block behind**.
During block *N* it renders block *N−1*, by which time every Voice plugin has certainly
written block *N−1*'s events regardless of order. It reports
`setLatencySamples(maximumExpectedSamplesPerBlock)` and the host compensates. The main
plugin's own direct MIDI is delayed identically, so the two paths stay aligned.

**Link mode** (main plugin, default **off**) gates this. With it off, the instance has no
latency at all and works as a standalone instrument. Turning it on is what enables Voice
plugins to connect, and the UI states that the host will re-compensate. This keeps the
common case free.

### 11.5 Pairing, persistence and conflicts

- A main instance registers under a persistent UUID and an editable display name
  ("ChipBoy 1"), both saved in its state.
- A Voice plugin shows live instances and their four channel slots, marking which are
  taken. It saves the UUID and the channel it claimed.
- On session reload the Voice reconnects by UUID. If the main is not loaded yet it shows
  **waiting** and keeps its settings; if the main never appears it shows **disconnected**
  and still keeps them. It never silently rebinds to a different instance.
- If two Voice plugins claim the same channel, the first keeps it and the second shows
  **channel busy**. Silently stealing a channel would make a reloaded session sound
  different depending on load order.

### 11.6 The Voice plugin is an instrument, not a MIDI effect

It is a synth that outputs silence. This is deliberate: MIDI-effect plugin types are
inconsistently supported and inconsistently routable across hosts, whereas every host can
put an instrument on a track, give it a piano roll, and automate it. The cost is a track
of silent audio, which is nothing.

Its UI states plainly that audio comes from the linked ChipBoy instance's track. Users
will look for it there first.

### 11.7 Return path

The main instance publishes, per channel, a small decimated scope trace and the current
register values, for the Voice plugin to display. **Display only — never audio.** Routing
a channel's audio back would require splitting the shared analog stage, which **C9**
forbids.

### 11.8 Robustness

Shared memory is visible to any process running as the same user, and a crashed host
leaves regions behind. Therefore: every value read from a shared region is range-checked
before use; no offset, length or index from a region is ever trusted; a malformed region
disconnects and reports rather than faulting. Heartbeat reaping cleans up after crashes.
Tests cover a deliberately corrupted region (§16.5).

---

## 12. Parameters and automation

### 12.1 Rules

1. **Every parameter is discrete.** Integers or choices, with the hardware's range. No
   continuous parameters anywhere except the output trim (**C3**).
2. **No smoothing.** Ever. A parameter change is a register write.
3. **Sampled at the tick** (§8.2). A parameter automated in a smooth ramp produces a
   stepped result, because that is what a driver writing registers on an interrupt
   produces.
4. **Value ranges are fixed at compile time** — hence the fixed slot counts in §9.1.
   Selecting an empty slot is silent and shown as empty; it does not renumber anything.
5. Parameters are marked discrete to the host, so DAWs draw them as steps.

### 12.2 Base 10

Values are shown in decimal with the hardware's ranges: volume 0–15, envelope rate 0–7,
sweep shift 0–7, table steps 1–16, transpose in signed semitones. Where the hardware
packs several fields into one byte (the envelope register), ChipBoy shows the fields
separately rather than the byte.

A **hex display** preference is available for people coming from LSDj `[DECIDE] D7`. It
is a display format, not an accuracy switch, so it does not conflict with **C8**.

### 12.3 Main plugin parameters

| Parameter | Range |
|---|---|
| Master volume L / R | 0–7 each (NR50; 0 is 1/8, not mute) |
| Output trim | continuous dB — the one continuous control (**C3**) |
| Headphone noise | on / off (**C8**) |
| Tick source | host-synced / V-blank / custom |
| Ticks per beat *(host-synced)* | 1–48 |
| Tick rate *(custom)* | 1–240 Hz |
| Link mode | on / off (§11.4) |
| Per channel × 4 | the same set as §12.4, used when no Voice plugin has claimed the channel |

### 12.4 Voice plugin parameters

All automatable, all discrete, all latched at note-on unless Live follow is set (§10.1).

| Parameter | Range |
|---|---|
| Instrument | 1–128, or none |
| Table override | none, or 1–64 |
| Level | 0–15 (PU/NOI) or mute/25/50/100 (WAV) |
| Pan | off / L / R / both |
| Wave *(WAV)* | 1–64 |
| Frame *(WAV)* | 1–16 |
| Transpose | −60…+60 semitones |
| Detune | −128…+127 raw period units |
| Vibrato speed / depth | 1–15 / 0–15 |
| Arpeggio | none, or 1–64 (a table slot) |
| Envelope volume / direction / rate | 0–15 / down-up-off / 0–7 |
| Duty *(PULSE)* | 12.5 / 25 / 50 / 75 % |
| Sweep rate / direction / shift *(PU1)* | 0–7 / up-down / 0–7 |
| LFSR width *(NOI)* | 15-bit / 7-bit |
| Live follow | on / off |

Every one of these also responds to a MIDI CC, for hosts and workflows where CC lanes
are easier to draw than automation lanes. Same values, same tick quantisation.

### 12.5 Instrument source: linked or local

Per Voice plugin:

- **Linked** (default) — the Instrument parameter indexes a slot in the main instance's
  bank. The bank is the single source of truth; editing an instrument changes every
  channel using it, as in a tracker.
- **Local** — the Voice plugin holds a complete instrument definition in its own state
  and pushes it to the main instance on connect. Useful for copying a track between
  projects without carrying the bank.

Two explicit actions convert between them: **push to slot** and **pull from slot**.
Neither happens implicitly, because implicit bank writes from a track's state would make
project load order audible.

---

## 13. Interface

### 13.1 Main plugin

- **Channel strip × 4.** Per channel: an oscilloscope drawn from the event stream (so it
  shows the actual staircase, and after the analog stage the actual droop and rounding),
  the instrument in use, level, pan, and the register values currently written. The
  register view is not a debug panel — it is the thing that teaches the instrument, and
  it stays.
- **Bank browser.** Instruments, tables, waves, kits. Rename, duplicate, reorder,
  import, export.
- **Wave editor.** 32 × 16 grid, frame strip along the bottom, shape generators,
  interpolate-between-frames.
- **Table editor.** 16 rows × 4 columns, keyboard-navigable, in the tracker idiom that
  the audience already reads.
- **Kit editor.** Import, trim, note map, playback rate, per-sample preview showing the
  4-bit result rather than the source.
- **Global.** Master volume L/R, output trim, headphone noise, tick source, link mode.

### 13.2 Voice plugin

Small, because it lives on a track next to a piano roll. Connection status and channel
claim at the top; instrument selection; the per-note parameters of §12.4; a compact
scope fed by §11.7. It says where the audio is.

### 13.3 Rules

- Ranges are always visible. A control that goes 0–15 shows 0–15, not 0–100%.
- Out-of-range notes are shown as out of range, in the piano roll's terms — the C2 floor
  on the pulse channels is a fact users need on screen, not a mystery silence.
- Nothing implies precision the chip does not have. No decimal places on integer fields,
  no dB readouts on 4-bit levels.

---

## 14. File formats and persistence

### 14.1 `.chipboy` — bank

Versioned container: a JSON header (instruments, tables, waves, kit metadata, note maps)
plus binary payloads for kit sample data. Forward-compatible: an unknown field is
preserved on load and written back on save, so a bank edited in an older build does not
lose data from a newer one.

Waves and tables are small enough to live in the JSON as integer arrays, which makes
banks diffable and hand-editable. That is worth more than the bytes it costs.

### 14.2 Plugin state

**Main:** UUID, display name, global parameters, and the bank — embedded by default so
sessions are portable, with an option to reference an external `.chipboy` file
`[DECIDE] D5`.

**Voice:** linked UUID, claimed channel, instrument source mode (§12.5), the local
instrument if in local mode, and all parameter values.

Both are versioned, and both load older versions.

---

## 15. LSDj interoperability — post-v1

Not built in v1. The point of this section is to record the decisions taken *now* that
keep it cheap later, and the rules that apply when it is built.

### 15.1 Decisions already taken to keep the door open

- **Values are stored as hardware-native integers**, never as normalised floats. A
  round-trip through ChipBoy cannot lose precision that the hardware has.
- **Every instrument field maps to a register**, so anything LSDj can express, ChipBoy
  can express. The reverse is not guaranteed — ChipBoy's tables and waves are larger
  (**C10**) — so any future export must be lossy with explicit warnings, never silently
  truncating.
- **Tables are 16 steps with two command slots**, matching the shape LSDj uses.
- **The kit importer is format-agnostic** (audio in, 4-bit out), so a `.kit` reader is an
  additional front end rather than a new pipeline.

### 15.2 Rules when it is built

- Import reads **the user's own files** — `.sav` for songs, instruments, tables and
  waves; `.kit` for sample kits.
- **Nothing LSDj-derived ships** (`LICENSING.md` §1). No factory waves, no factory kits,
  no presets, no ROM data.
- Formats are implemented from public documentation and observation. No code is lifted
  from `lsdpatch` or any other GPL-licensed LSDj tool (`LICENSING.md` §4.2).
- **Parity claims are tested against LSDj itself**, not against anyone's memory of it.
  Until that testing exists, ChipBoy's behaviour is defined by this document and the
  resemblance is described as familiarity, not compatibility.

---

## 16. Testing and validation

### 16.1 Test ROMs — the acceptance gate

A minimal SM83 CPU, memory map and timer (`Source/tools`, never shipped) exists solely to
run real Game Boy test ROMs against the APU core:

- **blargg `dmg_sound` 01–11**
- **SameSuite APU tests**

Both are fetched at configure time into a git-ignored `TestRoms/`, and skipped with a
clear message when offline. **Passing these is the definition of an accurate core.** They
should be running before the first sound comes out of the plugin, because every quirk in
`HARDWARE_REFERENCE.md` §10 is far cheaper to get right now than to retrofit.

### 16.2 Golden files

Fixed register scripts and fixed MIDI + automation scripts rendered to buffers and
compared against stored references. The APU's event stream is hashed exactly (integer
data); rendered audio is compared with a tolerance, because the kernel design goes
through the platform's libm. Catches unintended timing changes.

### 16.3 Fast path vs reference path

The BLEP/area-sampling renderer nulled against the brute-force 4.194304 MHz reference
(§7). Requirement: residual below −90 dB on band-limited material.

### 16.4 Block-size determinism

The same input rendered at block sizes 32, 64, 128, 512, 2048 and a deliberately varying
block size must produce **bit-identical** output (§7.1). A failure is a correctness bug,
not a tolerance to widen.

### 16.5 Link tests

Processing-order independence (main before voice and voice before main produce identical
output), reconnect after the main is removed and restored, stale-region reaping, version
mismatch refusal, and a deliberately corrupted region that must disconnect rather than
fault (§11.8).

Automated tests cover the transport. They cannot cover the *host*, so the same scenarios
are run by hand in **Reaper** and **FL Studio** on Windows and **Logic** on macOS before
M5 is done. FL Studio is the interesting case: it bridges plugins into a separate process
under several conditions, which is exactly why the transport is shared memory rather than
in-process pointers (§11.2).

### 16.6 Hardware validation

The thing that separates this from a plausible emulation. The rig is a real **DMG** and a
real **CGB**, a flash cart, and a **Focusrite Scarlett 18i20** at 192 kHz, driven by an
RGBDS test ROM that steps register combinations with sync markers.

> **Built.** The probe ROM, its verifier, the loopback generator and the analyser live in
> `tools/capture/`; the procedure is [`CAPTURE_GUIDE.md`](CAPTURE_GUIDE.md). The ROM runs
> 83 takes in 2 min 36 s and is verified by executing it on a minimal SM83 interpreter
> before any recording session.

**Calibrate the chain before the console.** The interface's inputs are themselves
AC-coupled, so they add a high-pass on top of the console's — and the DMG's corner is
around 28 Hz, close enough to the interface's own that an uncorrected measurement reads
high. Run a loopback first, at the same gain staging, and characterise the interface's
amplitude and phase response. Deconvolve every later measurement against it.

**Fix and record the gain staging.** Line inputs at unity — not the instrument/Hi-Z input
and not the mic preamp, whose own colour is precisely what is being measured out of the
console. Record the console's volume pot position: it is an analog control ahead of the
output and it changes the noise and distortion balance, so captures at different positions
are not comparable. Take one at maximum and one at a mid position.

**Separate the cart from the console.** A flash cart draws from the same rails and some
models inject audible noise. Capture the noise floor with the flash cart *and* with a
commercial cart or none, so the model in §6.4 is the console's and not the cart's.

| Measurement | Method | Confidence at 192 kHz |
|---|---|---|
| Coupling time constant | Droop slope across a flat square top | High — a low-frequency effect |
| DAC step linearity | Staircase through all 16 levels, per channel | High |
| Envelope step timing | All seven rates, both directions | High |
| Click amplitude and shape | DAC on/off from a known preceding level | High |
| Clip point and symmetry | Four channels at level 15, master volume 7 | High |
| Noise floor spectrum | Silence, APU powered and powered down | High |
| Amplifier slew / edge shape | Rising edge | **Low — see below** |

**Edge shape is the one thing an audio interface cannot measure honestly.** Its
anti-alias filter rounds exactly the edges under measurement, and the console's amplifier
bandwidth may sit above the 96 kHz ceiling a 192 kHz capture affords. An oscilloscope is
the right instrument. Without one, fit the amplifier low-pass to the 192 kHz capture and
accept that the fit is valid for what is *audible* but not for what the oscilloscope view
in §13.1 draws — and label it that way in `HARDWARE_REFERENCE.md` §12 rather than
presenting it as measured.

**Capture both consoles.** A CGB in DMG-compatibility mode still sounds like a CGB
(`HARDWARE_REFERENCE.md` §11), so the CGB cannot stand in for the DMG and both must be
captured separately.

A null test against analog hardware will not null. The deliverable is the measured
parameter set, not a null.

### 16.7 CI

Build all targets on macOS and Windows, run `ctest`, and enforce **L1**: the core target
must link no JUCE module.

---

## 17. Roadmap

| | Milestone | Ships in v1 |
|---|---|---|
| **M0** | Repository, spec, decisions in §18 | — |
| **M1** | APU core + test-ROM harness. No audio output. Done when blargg and SameSuite pass. | ● |
| **M2** | Analog stage + renderer. First sound. Reference-path null test. | ● |
| **M3** | Main plugin shell: VST3/AU/Standalone, direct MIDI, no driver features. Validate against hardware captures here, while the signal path is still trivial to inspect. | ● |
| **M4** | Bank + driver: instruments, envelopes, vibrato, tables, arpeggios, sweep. | ● |
| **M5** | Voice plugin + link transport + pairing. | ● |
| **M6** | Waves, frames, kits, importer. | ● |
| **M7** | Interface: scopes, editors, bank browser. | ● |
| **M8** | Hardware validation pass (§16.6) for **both DMG and CGB**, model selection (§6.5), noise-floor model, release preparation. | ● |
| — | LSDj import/export (§15) | post-v1 |
| — | Further models: MGB, AGB, Pro Sound tap | post-v1 — no hardware to measure |
| — | `.vgm` / `.gbs` playback | post-v1 |

M1 before anything audible is deliberate. An APU that passes the test ROMs and then gets
an analog stage is a different project from an analog stage that gets an APU retrofitted
into it.

---

## 18. Open decisions

| | Decision | Recommendation |
|---|---|---|
| **D1** | Release licence: Path A (AGPLv3), B (commercial closed), or C (open core). `LICENSING.md` §2 | Keep C available regardless — it costs nothing and the architecture already requires it. Choose between A and B at release. |
| **D2** | Ship CLAP in v1? | Yes. MIT, no agreement, and Reaper supports it natively. Confirm the target FL Studio version supports it. |
| **D3** | Default tick source: host-synced or V-blank? | Host-synced. The tick rate was always a driver choice; only its quantising effect is hardware. |
| **D4** | Default velocity mapping. | → envelope start volume, quantised to 0–15. |
| **D5** | Bank embedded in plugin state, or referenced as a file? | Embedded by default, file reference as an option. Portable sessions matter more. |
| **D6** | Product names. "ChipBoy" and "ChipBoy Voice"? | Confirm before any public artefact carries them, and check for conflicts with existing plugins and with Nintendo trademarks — "Game Boy" cannot appear in a product name. |
| **D7** | Hex display option for LSDj users? | Yes. Display only, so **C8** is unaffected. |
| **D8** | ~~Post-v1 model order.~~ **Resolved.** | **DMG and CGB both ship in v1** (§6.5) — both consoles are available to measure, and CGB changes wave-channel *behaviour*, not only tone. MGB and AGB stay post-v1 for want of hardware. |
| **D9** | Default kit note map: chromatic or GM drum map? | Chromatic, since pitch and playback rate are the same control (§9.8) and a drum map implies a per-note pitch the chip cannot give. |
