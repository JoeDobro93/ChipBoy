# ChipBoy — interface and feature design

Working document for the UI workshop. The spec (`CHIPBOY_SPEC.md` §11–§13) already fixes
the two-plugin architecture, the parameter rules and the bank; this document turns those
into screens, decides what the musician sees first, and records where the workshop
changes the spec. The interactive mockup is `docs/mockups/chipboy_mockup.html`.

Everything here follows one rule from the spec: **the chip is the instrument.** Every
control is a register or a driver choice, shown in the chip's own ranges, and the
interface's job is to make the constraints legible rather than to hide them.

---

## 1. Two plugins, one machine

**ChipBoy** is the machine: the APU, the bank, the analog stage, the stereo output. One
instance is one Game Boy — four mono voices that share a mixer, one coupling capacitor
per side, and the wave channel's rules (C1, C9).

**ChipBoy Voice** is a remote control for one of those four voices, living on its own
track so it gets its own piano roll, automation lanes, name and colour. It is an
instrument that outputs silence and says so (spec §11.6).

Each of the four hardware channels has a **source**:

| Source | What it means | When to use it |
|---|---|---|
| **MIDI channel 1–16** on the ChipBoy track | Notes on that MIDI channel play this voice | One track drives the whole machine: FL's per-pattern MIDI channel, Reaper's item channel, Logic's multi-timbral tracks |
| **Omni** | Every note reaching the ChipBoy track plays this voice | The first minute: drop it on a track and play |
| **Voice plugin** | A ChipBoy Voice on another track has claimed the channel | The DAW-native layout: one track per voice, automation where you expect it |
| **Off** | The channel only responds to the Phrases lane and tables | Sound design, or a channel used purely as a drum from the lane |

Defaults: PU1 = omni, PU2 = MIDI 2, WAV = MIDI 3, NOI = MIDI 4. So a fresh instance plays
the lead the moment you touch a key, and the other three wake up when you address them.
A Voice claim overrides the MIDI source for that channel and the strip says which track
now owns it.

This is the design you described, made concrete. The alternatives were weighed:

- *Four independent plugins, one per channel, each with its own APU.* Breaks the shared
  mixer and coupling that make four channels sound like one machine (C1, C9). Rejected.
- *One multi-timbral plugin only, no helper.* Works in every host but piles four
  channels' automation onto one track. That is exactly the tracker-to-DAW friction this
  product exists to remove. Kept as the MIDI-channel source, not as the only way.
- *A MIDI-effect helper instead of a silent instrument.* Hosts route MIDI effects
  inconsistently; every host can put an instrument on a track. Rejected (spec §11.6).

---

## 2. The main window

Fixed-size like a hardware unit — 1180 × 760 at 100%, with 125% and 150% scaling.
Reading order is top to bottom: *what am I emulating → what is each voice doing → edit
the thing I selected.*

1. **Header.** Wordmark; the **model switch** (DMG / CGB / RAW); the bank name with
   previous/next; a **STOCK / MODIFIED** badge (§5); the visualizer window button; settings.
2. **The mixer row.** Four channel strips and a master strip. Every strip has its scope on
   top, mixer-bridge style, so the row reads at a glance while playing.
3. **Editor tabs.** Instrument · Tables · Waves · Kits · Phrases · Link · Hardware. The
   editor always knows its context: *Editing PU2's instrument "Bass 07"*.
4. **Status line.** Model, sample rate, latency, tick source, link state.

### A channel strip

Top to bottom: name and colour (PU1 sky, PU2 amber, WAV mint, NOI rose), an activity LED,
the source badge (*MIDI 2* / *Voice: Bass* / *Omni*); the scope; the live register line
(`NR11 80 · NR12 A3 · NR13 C1 · NR14 C7`) — the thing that teaches the instrument, kept
on screen deliberately (spec §13.1); instrument and table selectors; pan as the hardware
has it (off / L / both / R); two or three quick controls that differ by channel type
(level and envelope rate for PU and NOI; the four-step volume and frame for WAV); mute
and solo, which are NR51 gates and therefore pop like the hardware.

### The master strip

The stereo mix scope in LCD green; **master volume L and R** as 0–7 steppers where 0
reads "1/8", not "mute"; **Headphone Noise**; the **output trim**, the one continuous
control in the product, drawn as a fader with a dB readout so nobody mistakes it for
part of the chip.

---

## 3. The scopes

The wave viewer is the feature people will recognise from chiptune videos, and ChipBoy
can do it better than a video tool because it does not have to guess the period: the
APU knows it. Each channel's scope is **period-locked from the chip's own frequency
register**, so a sustained note is a still picture and vibrato is visible as the shape
breathing rather than as the trace sliding.

- **Two traces, selectable per scope:** *Digital* is the 4-bit staircase going into the
  DAC, on a 16-level grid; *Analog* is what leaves the machine after the coupling
  capacitor, so a low wave-channel note visibly sags on a DMG and turns into spikes on a
  CGB, and DAC-on clicks show as the steps they are. Both can be shown together.
- **Zoom:** 1, 2, 4 or 8 periods. Noise uses a fixed time window.
- **The master scope** shows the actual output, with the clicks and the droop.
- **Visualizer window.** A separate, resizable, clean window — the four scopes stacked
  or tiled, plus the master, no chrome, adjustable line weight, black or LCD ground —
  made for screen capture. Same data, so it costs nothing extra to render.

---

## 4. Playing it from a DAW

Notes come from the piano roll. Everything the note carries maps to something the chip
can do, and nothing is smoothed (C6):

| MIDI | Effect | Range |
|---|---|---|
| Note on/off | Trigger / note-off behaviour of the instrument (kill, release, ignore) | pitch quantised to the 11-bit period (C4) |
| Velocity | Envelope start volume (default), or instrument bank select, or ignored — per channel | 0–15 |
| Pitch bend | Signed period offset, recomputed per tick | raw period units |
| Mod wheel (CC1) | Vibrato depth | 0–15 |
| Keyswitch octave (optional) | Instrument select | 12 slots per octave |
| Any CC | Learnable to any channel parameter | the parameter's own range |

**Latch on note-on** (spec §10.1) is what makes a piano roll behave like a tracker's note
columns: automate *Instrument* on the track, play notes against it, and each note takes
the instrument that was current when it started. *Live follow*, per channel, flips that
for sweeps meant to be heard.

**Per-channel parameters** (spec §12.4) appear as automation lanes on the Voice's track,
or on the ChipBoy track when the channel is MIDI-sourced. Every one is discrete and the
host draws them as steps. This is the whole list, and it is short on purpose: the
instrument bank holds the sound; automation holds the performance.

---

## 5. Models and the Hardware panel

The header switch chooses which real machine is emulated:

| | Chip | Output stage |
|---|---|---|
| **DMG** | DMG quirks: wave RAM locked while playing, trigger corruption, DAC hold | measured DMG coupling (25 Hz), noise floor, clip |
| **CGB** | CGB quirks: live wave RAM, no corruption | measured CGB coupling (338 Hz), its louder LCD whine |
| **RAW** | DMG chip | **none** — the clean digital mix most gaming emulators produce: DAC-off is silence, no coupling, no noise, no clicks from DC |

RAW is the "unemulated" mode you asked for. It is what a listener who learned these
sounds from an emulator expects, and it is deliberately the third position rather than a
pile of toggles.

The **Hardware** tab holds everything else, in two groups that the badge in the header
summarises:

**Hardware states** — legal on a real unit, so the badge stays STOCK:

| Control | Hardware fact |
|---|---|
| Headphone Noise on/off | The original switch: hiss, LCD line and frame hum together |
| LCD on/off | With the display off a DMG's 9198 Hz line drops 24 dB (measured). "Disable the whine" is this |
| CGB bass mod | The common capacitor swap. Corner scales with the capacitor: stock 338 Hz, ×10 → 34 Hz, ×47 → 7 Hz. Approximation, no modded unit measured |
| Volume writes at edges | A driver technique: NRx2 writes wait for the low half of the pulse cycle, where a level change is silent. "Don't pop when changing volumes", done the way a driver could |
| Pro Sound tap (later) | Output taken before the amplifier and volume pot |

**Departures** — not what any Game Boy does; switching one on lights **MODIFIED**:

| Control | What it does |
|---|---|
| Tame DAC clicks | Crossfades DAC-on steps over 0.5–5 ms instead of stepping |
| Soften master pops | Ramps NR50 changes instead of stepping the DC offset |

This revises constraint **C8** ("exactly one switch changes what you hear"). The spirit
survives: the defaults are a stock machine; every departure is opt-in, labelled as a
departure, and visible in the header of the instance that uses it.

**Display** options live in the same tab: decimal or hex values (the LSDj habit), scope
trace mode, periods shown.

---

## 6. LSDj parity

Everything LSDj can express about a sound, ChipBoy can express — with base-10 values,
the hardware's ranges, and none of the cartridge limits (C10).

- **Instruments** (spec §9.2–9.3): Pulse, Wave, Kit, Noise, with every field LSDj has —
  envelope, duty and duty sequence, sweep, length, vibrato shape/speed/depth/delay,
  transpose, table, pan, note-off behaviour, retrigger/legato — plus the wave's frame
  advance and loop mode. The editor shows the field groups for the selected type and the
  register each field lands in.
- **Tables** (spec §9.5): 16 steps of volume, transpose, two commands; loop, hop, or
  stop at the end; one step per tick; shared by every instrument that references them.
- **Commands** (spec §9.6, LSDj lettering): `A` envelope, `C` chord, `D` delay, `F`
  frame, `H` hop, `K` kill, `L` slide, `O` pan, `P` pitch offset, `R` retrigger, `S`
  sweep/shift, `V` vibrato, `W` wave. Added by this workshop: `M` master volume L/R
  (NR50, hardware-legal) and `Z` random argument for the previous command. `T` tempo and
  `G` groove are not commands here — tempo is the host's, and groove is a property of
  the Phrases lane (§7).
- **Waves** (spec §9.7): 32 × 16 grid, up to 16 frames per wave, shape generators,
  interpolate between frames. The editor states the DMG cost of a frame change.
- **Kits** (spec §9.8): up to 32 one-shots, note map, playback rate quantised to the
  period register, one-shot / loop / loop-from-point, the 4-bit preview.
- **LSDj import** stays post-v1 (spec §15), but the data model is shaped so a `.sav`'s
  instruments, tables, waves and kits map one to one.

---

## 7. The Phrases lane — the tracker column, without the tracker

LSDj expresses most of its character through the effect column: a command on a step,
next to a note. A DAW has no such column. Three doors lead to the same driver:

1. **Automation and CC lanes** for anything continuous-ish (level, transpose, vibrato).
2. **Note-embedded** data (velocity, keyswitches, pitch bend, note-off).
3. **The Phrases lane**: a tracker-style step grid per channel that **follows the host
   transport**. Sixteen steps per phrase (a bar of sixteenths by default), columns for
   instrument, table and two commands, and a ghost column showing the notes the piano
   roll is sending at each step, so a command sits visibly next to the note it will hit.
   Phrases are chained along the timeline by bar, LSDj-style, with a groove (6/6, 7/5,
   8/4 ticks per step…) per phrase for swing.

A cell fires at its step's tick, latches like any other parameter, and applies to the
notes that follow — exactly the tracker behaviour, with the DAW as the sequencer.

**Why not a full built-in tracker with a note column?** Two sources of notes compete:
the piano roll's editing, quantise, humanise and MIDI recording against a grid that
cannot see them. The lane takes what the DAW is bad at (per-step commands) and leaves
notes where the DAW is good. The data model does not forbid a note column later; if the
lane is loved, it can grow one. **Decision needed — D-UI-1.**

---

## 8. The Voice plugin window

Small, because it lives beside a piano roll: 560 × 420.

- Link status at the top — *Linked · ChipBoy 1 · PU2*, or *waiting* / *channel busy* /
  *disconnected* in plain words — with instance and channel selectors.
- One line that answers the question everyone asks: **audio comes out of the ChipBoy 1
  track.**
- Its channel's scope, fed by the return path (display only, never audio — C9).
- Instrument selector with the **Linked / Local** source switch and explicit *push to
  slot* / *pull from slot* (spec §12.5).
- The channel's parameters as a compact grid — the same automation lanes the host shows.
- *Open editor* jumps the main window to this channel's instrument. Editing happens in
  one place, the bank, so a change is heard everywhere the instrument is used.

---

## 9. Standalone

The main plugin as an application: MIDI input selector in the header, everything else
identical. For jamming, for testing hardware captures against the model, and for people
who want a Game Boy on a keyboard without a DAW.

---

## 10. Decisions needed

| # | Question | Recommendation |
|---|---|---|
| D-UI-1 | Phrases lane with commands only, or a full note-capable tracker? | Commands only in v1 (§7) |
| D-UI-2 | RAW = DMG chip with clean output, or a separate chip selector for RAW? | DMG chip; add a chip selector only if asked |
| D-UI-3 | Ship the two departures (tame clicks, soften master pops) in v1? | Yes, behind the MODIFIED badge |
| D-UI-4 | Default routing: PU1 omni + MIDI 2/3/4, or all four on MIDI 1–4? | PU1 omni, so the first key press sounds |
| D-UI-5 | Keyswitches on by default? | Off; one click to enable, and the strip shows the reserved octave |
| D-UI-6 | Visualizer window in v1? | Yes — it is the same scope code in another window |

Once agreed, spec §12.3, §13 and C8 are revised to match, with the change recorded in
`CHANGES.md`.
