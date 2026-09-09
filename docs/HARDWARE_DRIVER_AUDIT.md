# The driver against the hardware

*Written for docs/COMMANDS_AND_TEMPO.md §28, 2026-09-09.*

Every behaviour the driver has, and the way a Game Boy music driver would
realise it: which registers it writes, what clocks the writing, and which
hardware quirk it leans on. What a real program could not do is marked
**plugin-only** — those are the parts a playback ROM would leave behind, and
they are all outside the chip's own path. Nothing in this table is a guess
about the hardware: the register behaviour is `docs/HARDWARE_REFERENCE.md`,
which is the measured and test-ROM-verified account, and the driver is
`Source/core/Driver/Driver.cpp`.

The audit is the reason §26 exists: the one thing here that a program on the
console could **not** have done was the quiet-edge marker, and it is gone.

---

## 1. Time

| Behaviour | On the hardware |
|---|---|
| **The tick** | The driver's whole timeline is ticks, 24 to a beat. A ROM runs them from the timer interrupt (TAC/TIMA) or from the vertical blank with a fractional accumulator; the plugin gets them from the host's beat position or from its own clock. Everything below happens at a tick unless it says otherwise. |
| **The pitch clock, 360 Hz** | A second timer interrupt, 11651 CPU cycles apart, per voice, restarted at every plain note-on (§7). A ROM runs one timer and services whichever voices are due; the cycle count is exact, not a rounding of the tick. |
| **Tempo (T)** | The tick period changes. A ROM reloads TMA; the plugin integrates a tempo map. Same thing, different clock source. |
| **Grooves** | The number of ticks a step lasts, from a sixteen-entry table (§9.2). A ROM counts ticks down per step out of the same table. |
| **A phrase's length** | 1–64 steps, its own; the row lasts the groove's ticks over that length (§25). A ROM walks the chain one row at a time and counts the row's ticks — no bar structure exists anywhere. |
| **Every channel on its own time** | Four independent row counters. A ROM has four; the prefix tables the plugin builds are only there to make a *locate* exact, which a ROM never has to do. |
| **The song's own transport, loop, locate** | **Plugin-only** in this form: a ROM plays from the start and loops the longest chain. The prefix tables (`rowStartTicks`) exist for the host's timeline. |

## 2. Notes

| Behaviour | On the hardware |
|---|---|
| **A plain note** | Load the instrument's running state, run its table's row 0, fire the two command slots, then write NR10–NR14 (or NR21–NR24, NR30–NR34, NR41–NR44) ending with the trigger bit in NRx4 (§8, §31). |
| **A bare note** | NRx3 and NRx4's low three bits only, no trigger bit: the envelope, the duty phase and the sweep keep running (reference §4). |
| **Note-off: Kill** | NRx2 = `$00` (or NR30 = `$00` on the wave channel), which clears the DAC. The level is held, so there is no click on the way down (reference §9). |
| **Note-off: Release** | Pulse and noise: NRx2 with a decrease at rate 1, written through the zombie sequence so the level is kept (§26). Wave and kits: the four NR32 levels, one tick apart. Shaped envelopes use their own release curve (§27). |
| **Note-off: Ignore** | Nothing is written. |
| **The held-note stack** | A ROM keeps the same list. Returning to an older note writes the period without a trigger. |
| **Overlap = legato / retrig** | Whether an overlapping note writes the trigger bit. A note at the pitch already sounding always triggers (§31): a repeated drum hit is a hit. |
| **The keyswitch and command octaves** | A ROM's note table has no notes there either; they select an instrument or fire the slots. |
| **MIDI, velocity, the note stack from a keyboard** | **Plugin-only** in origin; what they *do* is the note-on above. A ROM's notes come from its chains. |

## 3. Levels — the zombie-mode rules (§26)

| Behaviour | On the hardware |
|---|---|
| **The chip's own envelope (Chip mode)** | NRx2: initial volume, direction, one of seven rates. Clocked at 64 Hz by the frame sequencer (reference §7). Written at the note-on with the trigger. |
| **A level change on a running channel** | NRx2 writes without a trigger, using the chip's response to a write while the channel runs: with the envelope period at zero a write with the same direction bit adds one to the volume, and a write that flips the bit maps it to 15 − v. The driver searches for the shortest sequence of writes that lands the chip's volume on the level it wants and leaves the envelope it wants in the register (`Driver::setLevel`, `zombieSequence`). This is the table volume column, an E that keeps the direction and rate, a shaped envelope's step, CC7, the Level lane and the release fade. |
| **What a sequence costs** | From a holding envelope: up one is one write, down one is three (flip, step, flip), and the worst case across all sixteen levels is nine. Every write is 20 cycles after the last, which is what `ld a,n` / `ldh (n),a` costs, so the longest sequence is 180 cycles — a fifth of a scanline. |
| **A trigger** | Only where a driver needs one: a plain note-on, R, and an E that changes the envelope's direction or rate. Nothing else writes NRx4 bit 7. |
| **The DAC** | A zombie write carries the target level in NRx2's high nibble, which the chip reads only at the next trigger, so it is free — but the top five bits must not be zero or the DAC goes off and the channel stops. A level of 0 with the direction bit down would be exactly that pattern, and is written as level 1 instead; the volume it lands on is the same. |
| **Wave levels** | NR32's two bits: 100 %, 50 %, 25 %, mute. One write, no zombie mode, no trigger — the wave channel has no envelope generator. |
| **Console differences** | The APU implements one NRx2-write rule for both consoles it models, so both take the same sequence. The search reads the model (`Driver::model_`) and is driven by the rule, not by a table of writes, so a console whose behaviour differs (CGB-D and AGB are described as differing; ChipBoy has measured neither) would get its own sequence from the same code once the APU models it. Reference §10.2 marks the quirk model-specific for exactly this reason. |

## 4. Pitch

| Behaviour | On the hardware |
|---|---|
| **Period** | NRx3 and NRx4's low three bits, from the note table. Below the chip's range the note does not sound (a real driver's table has no entry either). |
| **Vibrato (V, and the instrument's own)** | The 360 Hz clock computes a phase and writes the period. A ROM does the same from a small table; nothing is per sample. |
| **Slide (L)** | A residual walked to zero over the duration, in period units — or in semitones in Drum mode, where the driver re-reads the note table each update. |
| **Bend (P)** | An offset added per update. `P 128` stops it and keeps the offset. |
| **Chord (C)** | The note changes every *rate + 1* ticks; the period is rewritten, no trigger. |
| **Sweep (S)** | NR10 on CH1 only, the chip's own sweep unit. The negate quirk (reference §10.1) is the chip's, and the driver does not work around it. |
| **Noise pitch** | NR43's shift and divisor from the curated map (spec §9.4) — a ROM ships the same 128-byte table. |
| **Kits** | NR33/NR34 hold the sample rate; the note transposes it. |

## 5. Tables and instruments

| Behaviour | On the hardware |
|---|---|
| **A table row** | One row per tick (or per the row length a G inside the table asks for): a transpose column, a volume column and two commands. A ROM reads the same rows out of ROM. |
| **Row 0** | Runs inside the note-on, before the trigger (§31), so the note's own register writes already carry the row's transpose, level and commands and the raw note is never heard. The rows after it step on the ticks. |
| **A table's own groove (G)** | The row lengths come from the song's groove table. |
| **Hop (H), Loop, Stop** | The row counter's next value. |
| **Duty sequences** | NRx1's top two bits, a step per tick. |
| **Wave frames (F, W, the frame run)** | See §6. |
| **Shaped envelopes (§27)** | One level per tick from the segment curves, written through §26. A ROM can hold the same per-tick list — the curves are integer arithmetic (`bank::envSegmentLevel`), so the list is a constant of the song, not of the machine. |

## 6. Wave RAM

| Behaviour | On the hardware |
|---|---|
| **Loading a frame on DMG** | NR30 = `$00`, sixteen writes to `$FF30`–`$FF3F`, NR30 = `$80`, then NR32 and a trigger. Wave RAM is unreachable while the channel runs (reference §5), so the channel really is switched off for the sixteen writes — that is the click, and it is on the hardware too. |
| **Loading a frame on CGB** | The channel stays on and the bytes are streamed a loop behind the read pointer, which is what the CGB's live access allows. |
| **Kit streaming** | Sixteen bytes every 32 samples of playback, scheduled from the channel's own timer. A ROM does this from the timer interrupt; it is the one thing here that is not on the tick. |
| **The trigger-corruption bug** | Implemented in the APU for parity, never reached by the driver: it never triggers CH3 while it is reading. |

## 7. Mixing

| Behaviour | On the hardware |
|---|---|
| **Pan (O, the instrument's pan)** | NR51's two bits per channel. |
| **Mute / solo** | The same NR51 bits — the driver has no other way to silence a channel, and it pops exactly as the hardware does. |
| **Master volume (M, the VOL control)** | NR50's two three-bit fields. Level 0 is 1/8, not mute (reference §8). |
| **The analog model, the coupling, the hiss, the LCD line, De-click, the output trim** | **Plugin-only**: they are the console's analog stage and the plugin's own conveniences, not register writes. A ROM has them for free by being a console. |

## 8. What is plugin-only, and why

* **MIDI in every form** — notes, velocity, controllers, the bend wheel, CC7, the keyswitch octave from a keyboard, notes-on-tick. What they do to the registers is a driver's business; where they come from is not.
* **Hybrid playback (§20)** — live notes over the song's columns. A ROM has no live notes.
* **The Voice plugin and the link region** — one channel of one machine on another track.
* **The record path** — quantising, cells written back, the undo history.
* **The scopes, meters and the window.**
* **The own transport, locate and loop** — a ROM plays from the start.
* **Prefix tables** (`rowStartTicks`) and the tempo map — they make a locate exact; a ROM walks its chains.

## 9. Approximations, and the ones left in

| Thing | Why it stays |
|---|---|
| **A level change while the chip's envelope is running** (a non-zero NRx2 period) | The driver's model of the volume is what it last wrote; the chip has been stepping it at 64 Hz since. A driver on the hardware has exactly the same problem — the volume cannot be read back — and a real one keeps the period at zero whenever it wants software levels, which is what a shaped envelope and every §26 path do. The sequence is exact whenever the period is zero. |
| **The length counter disabling a channel** | `Driver` does not model the length counter's effect on the chip's *enabled* flag, so a zombie write to a channel a length counter has stopped is computed as if it were running. Reaching it needs an instrument with a length, a level change after the length has expired, and nothing else in between; the note is over by then. Listed rather than fixed because modelling it means modelling the 256 Hz frame step in the driver, which is a second clock for one silent case. |
| **The 64 Hz envelope phase** | Not modelled in the driver (see above). It is not needed for §26 in its intended use. |
| **The 360 Hz pitch clock's first update** | One full period after the note, so a bend never doubles the note's own period write. A ROM's timer has some phase too; this one is deterministic, which a ROM's is not. |
| **An instrument reload on a sounding channel** (a cell's instrument column, Live follow) keeps its trigger | It is not a level change: it re-lays duty, length, envelope, pan and table, which is a note-on in everything but the note. |
| **A groove in force does not change a row's length** | The rows lie end to end on a table built when the song was published, so a G — a cell's or a slot's — re-lays the steps *inside* the row and never moves the rows. A step that would start at or past the row's end does not fire, and a groove that ends early leaves the last note sustaining (§9.2). A ROM counting ticks per row would do the same. |

## 10. What a playback ROM needs

The tracker is kept self-contained for this (spec §15.3). What follows is the
shape of the data, not the exporter: a sketch to check that nothing in the
model is unrepresentable.

**The song, format 6, as a compact binary.** Everything is little-endian; a
"slot" is a one-based index into the section that holds it.

```
header      "CBRM", u8 version = 1, u8 flags, u16 tempoBpm x 4 (fixed point)
grooves     16 x { u8 count, u8 ticks[count] }              1..48 each
phrases     u16 count, then per phrase:
              u8 steps (1..64), u8 groove (0 = straight, 1..16)
              u8 cellCount, then per cell:
                u8 step, u8 mask, then the fields the mask names:
                  bit 0  u8 note      (1..127, 255 = off)
                  bit 1  u8 velocity  (1..127)
                  bit 2  u8 instrument
                  bit 3  u8 table
                  bit 4  u8 cmd1 letter, i16 x, i16 y   (letter bit 7 = revert)
                  bit 5  u8 cmd2 letter, i16 x, i16 y
chains      4 x { u16 rows, u8 phrase[rows] }               0 = an empty row
```

A row's length in ticks is the groove's entries over the phrase's steps, so
the player needs no tables: it counts down. An empty row is 96 ticks with a
note-off at its start.

**The bank.** Instruments are fixed-size records; the rest is what they point
at.

```
instruments u8 count, then per instrument:
              u8 type, u8 pan, u16 length, u8 table, u8 flags
              u8 noteOff, u8 overlap, u8 pitchSpeed, u8 cmdRate, u8 tableMode
              u8 vibShape, u8 vibDir, u8 vibSpeed, u8 vibDepth, u8 vibDelay
              u8 duty, u8 dutySeqLen, u8 dutySeq[dutySeqLen]
              u8 envMode, u8 envVol, u8 envDir, u8 envRate           (Chip)
              u8 attack, u8 peak, u8 decay, u8 sustain, u8 release,
              u8 curves (2 bits each)                                (Shaped)
              u8 sweepRate, u8 sweepDown, u8 sweepShift
              u8 wave, u8 frameAdvance, u8 frameLoop, u8 waveLevel
              u8 kit, u8 kitLoop
              u8 lfsr7, u8 noiseManual, u8 noiseShift, u8 noiseDiv, i8 noiseSweep
tables      u8 count, then per table: u8 end, u8 hopStep,
              16 x { i8 vol (-1 blank), i8 transpose, u8 hasTranspose,
                     cmd1, cmd2 }
waves       u8 count, then per wave: u8 frames, then frames x 16 bytes
                                                  (two 4-bit samples a byte)
kits        u8 count, then per kit: u16 period, u8 loop, u8 samples,
              then per sample: u8 note, u32 length, u32 loopPoint,
              then (length + 1) / 2 bytes, packed as wave RAM is
```

A shaped envelope can be flattened further — one level per tick, as a byte
list, which is what the driver renders anyway — and that is the form to use if
the ROM's code budget matters more than its data.

**What the ROM's player has to do**, in order, once a tick: advance each
channel's row counter and fire the step that starts on this tick; run each
voice's table row; step the shaped envelope; write the pitch; and on its own
360 Hz timer, write the pitch again for vibrato, slides and bends. Levels go
out as the sequences in §3. Nothing in that list needs a table the exporter
does not already have.
