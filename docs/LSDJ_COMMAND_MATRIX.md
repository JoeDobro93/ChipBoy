# The LSDj command matrix: 9.3.9 as the base, and what ChipBoy does with each letter

This is the working reference for making ChipBoy reproduce an LSDj song. It exists so the
next person -- or the next chat -- can pick a command off the list, read what LSDj does with
it, read what ChipBoy currently does, and know whether the difference is the importer's to
fix or the engine's.

**Base version: LSDj 9.3.9 (song format 22).** Every other version is described as a
*difference from* 9.3.9. §7 is the recipe for probing another ROM and deciding whether it
shares 9.3.9's behaviour or needs its own entry in `Source/core/Import/LsdjModel.cpp`.

Read alongside:

- `docs/COMMANDS_AND_TEMPO.md` -- ChipBoy's own design log, numbered sections. The binding
  spec: a design change goes there **before** the code.
- `docs/LSDJ_PARITY.md` -- the register-level measurements, numbered sections.
- `Source/core/Driver/Driver.cpp` -- `applyCommand()` is the authority on what ChipBoy does.
- `Source/core/Import/LsdjSong.cpp` -- `Reader::command()` is the authority on what the
  importer translates.

---

## 1. How to read this, and how much to trust it

Every row carries a **provenance** mark. This matters more than anything else in the
document: LSDj's behaviour has been measured for some commands and assumed for others, and
the assumed ones have been wrong before.

| Mark | Means |
|---|---|
| **M9** | Measured on a 9.x ROM by logging register writes. The ROM version is named. |
| **M8** | Measured on 8.4.4 or 5.0.3 playing a real user song. Named where it appears. |
| **D** | From LSDj's own changelog or manual -- documented behaviour, not yet traced. |
| **A** | Assumed from a neighbouring version or from how the format is laid out. **Not evidence.** |
| **?** | Unknown. Nobody has looked. |

Two warnings, both learned the hard way and both recorded in `COMMANDS_AND_TEMPO.md`:

1. **A generated probe save can lie.** Saves built by `tools/lsdjref/lsdjref_sav.py` and never
   opened in LSDj's own editor report a table hop from the second command column that a real
   save does not, and drop a table's `G` (§58, §63). **Measure on a real save.** The way to
   probe one variable safely is `work.py` (§3), which patches a byte into a *real* save.
2. **A measurement is only true of the version it was made on.** `LSDJ_PARITY.md` §7 said
   "LSDj never lets the chip's envelope run" as a flat fact; it is true from 8.8.0 and false
   before it (§70). Anything in `LSDJ_PARITY.md` was measured on **9.2.J** unless it says
   otherwise, and may be version-specific in the same way.

### The mappable flag

| Flag | Means |
|---|---|
| **Yes** | The importer can produce identical audible results today, or with the translation named. |
| **Value** | Mappable, but the number has to change -- a formula is given. |
| **Engine** | Not mappable by the importer alone; ChipBoy's engine has to change. |
| **No** | ChipBoy has no way to express it. The importer must add a note (§8) so the user knows. |

---

## 2. Summary matrix

`x` and `y` are the command byte's high and low nibbles; `xy` is the whole byte.
"Phrase / table" says whether the letter means different things in the two places.

| Cmd | LSDj 9.3.9, in one line | Channels | Phrase vs table | ChipBoy now | Mappable | Prov. |
|---|---|---|---|---|---|---|
| `A` | Run table `xy`; `A20` stops the table | all | same | `Cmd::A`, slot `xy + 1`, 0 stops | Yes (slot + 1; a table *column* cannot say "stop") | D |
| `B` | **MayBe**: phrase = play-probability, table = probabilistic hop | all | **different** | **absent** | **No** -- dropped with a note | D |
| `C` | Chord: arpeggiate note, note+`x`, note+`y` | PU1 PU2 WAV | same | `Cmd::C`, same | Yes | M9 (9.2.J, §9) |
| `D` | Delay the note by `xy` ticks | all | same | `Cmd::D`, read at the note-on | Yes | M9 (§9) |
| `E` | Envelope: level `x`, `y` is the NRx2 low nibble | PU1 PU2 NOI (WAV: level only) | same | `Cmd::E`; runs the envelope in software | Yes, **but see §4** (rate table is version-dependent) | M9 (§6, §7), M8 (§67, §70) |
| `F` | Wave: frame. PU2: finetune/transpose. Elsewhere: finetune | WAV, PU2 | same | `Cmd::F`: frame on WAV, transpose on PU2, inert elsewhere | Partly -- PU1/NOI finetune is **No** | A |
| `G` | Groove `xy` | all | **different** -- see §5 | `Cmd::G`, slot `xy + 1` | Yes (slot + 1); pre-9 differs (§63) | M8 (§63) |
| `H` | Phrase: hop out of the phrase. Table: hop `x` times to row `y` | all | **different** | `Cmd::H` both forms | Yes, except phrase `HFF` (§56) | M8 (§34, §56) |
| `K` | Kill the note after `xy` ticks | all | same | `Cmd::K`, same | Yes | M9 (§9) |
| `L` | Slide to the note, over `xy + 1` pitch updates | all | same | `Cmd::L`; §68, §71 | Yes | M9 (§4), M8 (§68, §71) |
| `M` | Master volume, `x` left `y` right | global | same | `Cmd::M`, both nibbles | Yes | M9 (§34) |
| `O` | Output/pan, `xy` in 0-3 | all | same | `Cmd::O`, same | Yes | M9 |
| `P` | Pitch bend, `xy` two's complement | all (NOI differs) | same | `Cmd::P`; speed law per instrument | Yes; older formats need a **Value** map (§56) | M9 (§5), M8 (§66) |
| `R` | Retrigger every `y` ticks, `x` a volume step; `x=8` resyncs | all | same | `Cmd::R`, same | Yes | M9 (§8) |
| `S` | PU1: hardware sweep. NOI: shape/transpose. PU2, WAV: inert | PU1 NOI | same | `Cmd::S`; two noise domains (§66) | Yes | M9 (§34), M8 (§55, §66) |
| `T` | Tempo `xy` BPM | global | same | `Cmd::T`, the Clock owns it | Yes | M9 |
| `V` | Vibrato, `x` speed `y` depth | PU1 PU2 WAV | same | `Cmd::V`, same | Yes; format 0 needs a **Value** map (§56) | M9 (§3) |
| `W` | PU: duty. WAV: wave/synth | PU1 PU2 WAV | same | `Cmd::W`: duty or wave slot | Partly -- wave-instrument `W` (synth speed/length) is **No** | A |
| `Z` | Randomise: re-run the last command with `0..x`, `0..y` added | all | same | `Cmd::Z`, re-runs last non-Z/H | **Value** -- ChipBoy adds to fields, LSDj to nibbles (§6.19) | D |

---

## 3. The method: how any of this gets measured

Nothing here is guessed from audio. The technique is to log **every APU register write with
its CPU cycle** from the real ROM, do the same from ChipBoy, and diff the two streams. Two
streams that agree byte for byte sound the same; two that differ tell you exactly where.

Assets live **outside the repository** (rule L3): ROMs and saves at `/root/lsdj/` in this
container. Nothing of LSDj is committed.

### 3.1 Trace the ROM

```
build-ref/lsdjref/lsdjref_trace \
  --rom /root/lsdj/lsdj9_3_9.gb \
  --bootrom-dir build-ref/lsdjref/BootROMs \
  --model dmg --sav SONG.sav --frames 1500 --keys 180:start \
  --out lsdj.csv
```

Build it with `cmake -S . -B build-ref -DCHIPBOY_LSDJREF=ON && cmake --build build-ref`.
`--frames 1500` is about 25 s; the song starts a beat or so after the `start` key at frame
180. Columns are `cycle,addr,name,value`.

### 3.2 Trace ChipBoy on the same song

```
chipboy_recordtest --import-sav SONG.sav "SONG NAME" out.cbsong
chipboy_recordtest --trace-song out.cbsong chipboy.csv 23.8
```

Match the durations or the note counts are not comparable. The binary is at
`build-plugin/chipboy_recordtest_artefacts/Release/chipboy_recordtest`.

### 3.3 Compare

- `python3 /root/lsdj/notes.py lsdj.csv chipboy.csv` -- note-ons per channel, scored by
  longest common run. The coarse "how far off are we" number.
- Align the two streams on an unambiguous shared event before comparing times. A good anchor
  is the first distinctive `NRx2` write of the song's first phrase.
- Then read the raw writes for the bar in question. Convert a pulse/wave period to a note
  with `f = K / (2048 - period)`, `K = 65536` on wave and `131072` on the pulses, and
  `midi = 69 + 12 * log2(f / 440)`.

### 3.4 Isolate one variable inside a real save

`/root/lsdj/work.py SAV FILEIDX OUT.sav [addr=val ...]` decompresses one song out of a save,
patches the bytes named, writes it into the save's **working area** (0x0000-0x7FFF) and sets
`0x8140 = 0xFF` so LSDj boots straight into it. That plays the user's own song with a single
byte changed -- the only trustworthy way to probe a table command (§1, warning 1).

`/root/lsdj/dump.py SAV` lists the songs; `dump.py SAV IDX [TABLE...]` prints a song's header
and any table's four columns.

### 3.5 Isolate one variable inside ChipBoy

Core links nothing, so a scratch probe compiles directly:

```
g++ -std=c++20 -O1 -I Source probe.cpp \
    Source/core/Bank/*.cpp Source/core/Driver/*.cpp \
    Source/core/Render/*.cpp Source/core/Apu/*.cpp Source/core/Tracker/*.cpp -o probe
```

Copy the `Rig` from `Tests/DriverTests.cpp` for a driver you can drive a tick at a time and
read `drv.view(ch)` from. To prove a fix *is* the fix, build the same probe against the
previous commit's `Driver.cpp`:

```
git show <sha>:Source/core/Driver/Driver.cpp > old/Driver.cpp
```

and compile that instead. This is how §67 and §71 were confirmed.

### 3.6 Reading the ROM directly

The project owner has cleared disassembling the ROM to settle a constant that resists
measurement (their call; the repository stays clean either way -- **no ROM-derived code or
text is committed**, behaviour is reimplemented in ChipBoy's own terms). Register-stream
diffing has settled every question so far and is usually faster. Reach for the ROM when a
constant is *measurably* ambiguous -- the standing example is `LSDJ_PARITY.md` §7, where
envelope rates 6 and 7 measured as the same interval and the document itself says that is
"worth one more run before that is taken as certain".

---

## 4. Where a command's meaning changes with the version

These are the known version boundaries. Each is a fact about LSDj, and each is already
carried by a field on `LsdjModel` or on the instrument.

| Boundary | What changes | Carried by | Prov. |
|---|---|---|---|
| **8.8.0** | Before it, the **chip** runs the envelope, so `E` and an instrument's own envelope step at `rate / 64` s. From it, LSDj steps the level in software on a measured table where rates 6 and 7 are equal. | `Instrument::envChipTiming`, from `EnvelopeLaw` | M8 (§70) |
| **8.8.0** | Before it, `E` re-attacks the note; after, it never triggers. | `Instrument::envRetrig` | M9/M8 (§59) |
| **8.9.3** | Before it, a table volume-column hop costs a tick; after, it is free. | `LsdjModel::envHopCostsTick` | M8 (§64) |
| **9.x** | A table's `G` walks the groove from 9.x; before, the row holds the groove's **first step** as its length. | `LsdjModel::tableGrooveWalks` | M8 (§63) |
| **format 11** | Command letters gain `B`, so every byte ≥ 2 shifts by one. | `LsdjModel::commandLetters` | M8 |
| **format 22** | `S` on noise becomes semitones through a musical map; before, nibble subtraction on NR43. | `LsdjModel::noiseS` | M8 (§55, §66) |
| **formats 0-3** | `P`, `L` and `V` work in **period register units**, not semitones. | `LsdjModel::pitchLaw`, `vibratoLaw` | M8 (§56) |
| **format 15** | Noise notes are `FF - note`; formats 0-11 use `~SHAPE`; format 22 uses the map. | `LsdjModel::noiseRule` | M8 (§56) |
| **9.x** | The instrument byte holding `synth << 4 \| frame` moves from byte 2 to byte 3. | `LsdjModel::waveByte` | M8 (§60) |

---

## 5. Where a command's meaning changes with the *place*

Three letters mean different things in a phrase and in a table.

- **`B`** -- phrase: the probability the note plays. Table: a hop that only happens sometimes.
- **`G`** -- phrase: sets the groove from that row on. Table: sets the row lengths of *that
  table run*; and before 9.x the row carrying the `G` takes the groove's **first step** as its
  own length rather than walking the groove (§63). ChipBoy's importer flattens such a `G` into
  a one-step groove in a spare slot, ranking free slots so it never overwrites a groove the
  song still names.
- **`H`** -- phrase: ends the phrase early / jumps. Table: `x` times to row `y` (§34).

And one letter means different things in a phrase and in a **command slot**:

- **`D`** -- a slot's `D` is read at the note-on, not applied live. `applyCommand` only takes
  `D` when `fromTable` is set.

Everything else applies identically in both, as far as anything has been measured.

### The three table lanes

ChipBoy runs a table as **three independent lanes** (§64), which is LSDj's behaviour:

| Lane | Columns | Ends how |
|---|---|---|
| 0 | volume (`VOL`) + `LEN` | at its first empty row |
| 1 | transpose (`TSP`) + command 1 | runs 16 rows and loops |
| 2 | command 2 | runs 16 rows and loops |

Each has its own pointer, its own wait counter and its own `H`. A `H` in column 1 does not
move column 2. The Tables tab draws a playhead per lane.

---

## 6. The commands, one at a time

Each entry: what LSDj 9.3.9 does, what differs by channel, what ChipBoy does, and the
mapping. `x`/`y` are the byte's nibbles.

### 6.1 `A` -- table

**LSDj (D):** starts table `xy` on the channel. `A20` stops the running table. In a table,
`A` switches to another table.
**ChipBoy:** `Cmd::A` with slot `xy + 1` (ChipBoy's slots are 1-based, 0 = none).
**Mapping — Yes.** The importer prefers the cell's *Table column* when the `A` is in a phrase
cell, since ChipBoy has a dedicated column. `A20` in a table column cannot be expressed there
and is dropped with a note; as a command it becomes `A 0`.

### 6.2 `B` -- MayBe *(not implemented)*

**LSDj (D, added v7.1.0, renamed "maybe" in v7.1.1):** the letter makes something conditional
on a die roll, and what it gates depends on where it sits.
- *In a phrase* — how likely the note is to sound at all. The high digit governs the left kit
  and the low digit the note and the right kit, with zero meaning certain: `B00` always plays,
  a high low-digit usually skips, and `B80` drops the left kit about half the time.
- *In a table* — a hop that only sometimes happens. The high digit is the probability and the
  low digit the destination row, so `B05` is an unconditional hop to row 5 while `B84` reaches
  row 4 about half the time.

**ChipBoy:** nothing. The letter does not exist in `bank::Cmd`.
**Mapping — No.** The importer drops it with a note naming the cell. To close this: `B` needs
adding to `Cmd`, to the parameter table, and to the driver — the table form is a `H` with a
probability gate, the phrase form a note-on gate. Being random, it cannot be flattened into a
deterministic equivalent, so this is genuinely an engine feature, not a mapping.

### 6.3 `C` -- chord

**LSDj (M9, §9):** arpeggiates the note with `note`, `note + x`, `note + y`, one step per tick
(per the instrument's chord/FX speed).
**Channels:** pulses and wave. On noise, LSDj's `C` changed at 7.0.4/7.0.6 (**?** — not
traced); ChipBoy ignores `C` on noise.
**ChipBoy:** `Cmd::C`, `chord[0] = 0`, `chord[1] = x`, `chord[2] = y`, stepping every
`chordRate + 1` ticks.
**Mapping — Yes.**

### 6.4 `D` -- delay

**LSDj (M9, §9):** delays the note by `xy` ticks.
**ChipBoy:** `Cmd::D`; a slot's `D` is read at the note-on rather than applied live.
**Mapping — Yes.**

### 6.5 `E` -- envelope

**LSDj (M9 §6, M8 §67 §70):** `x` is the level; `y` is written into the `NRx2` low nibble —
`0` and `8` hold, `1-7` decay at that rate, `9-15` rise at `y - 8`. **`E` never triggers**
from 8.8.0 on; before that it re-attacks (§59).

The *rate* is the version-dependent part (§70):

- **From 8.8.0**: LSDj steps the level itself off the 11712-cycle pitch clock, on the measured
  table `{6, 11, 15, 20, 27, 36, 36}` clocks for rates 1-7. Every `NRx2` goes out with low
  nibble 8, a hold.
- **Before 8.8.0**: the byte goes straight into `NRx2` and the **chip's** envelope runs it, one
  level every `rate / 64` s = `rate * 65536` cycles.

**Channels:** on WAV/KIT the level is `NR32`'s two bits, so `x` is clamped to 0-3 and `y` is
meaningless.
**ChipBoy:** `Cmd::E` sets `envVol`, `envRate`, `envDir` and takes a shaped envelope over
(`shapedTaken`), then steps the level itself in software — on either table, chosen by
`Instrument::envChipTiming`. It emits §26's zombie writes (`08` up, `09 11 18` down), so a
`NRx2` value of `08` in a ChipBoy trace is an *increment*, not "volume 0".
**Mapping — Yes**, with `envChipTiming` and `envRetrig` set from the model's `EnvelopeLaw`.

### 6.6 `F` -- frame / finetune

**LSDj (A):** on a wave instrument, selects the frame. On PU2, a transpose for the note
(§49). Elsewhere a finetune. **The general finetune behaviour is not traced (`?`).**
**ChipBoy:** `Cmd::F` names the wave frame outright (§65: `F 06` loads frame 5 whether or not
the run visits it, measured on 8.4.4) and on PU2 sets `instTranspose` two's complement.
**Mapping — partly.** WAV and PU2 are Yes. `F` on PU1 or NOI is dropped with a note; closing
it needs the finetune measured first.

### 6.7 `G` -- groove

**LSDj (M8, §63):** selects groove `xy`. In a table on 9.x the run walks the groove; before
9.x the row carrying the `G` takes the groove's **first step** as its own length.
**ChipBoy:** `Cmd::G` with slot `xy + 1`. Inside a table it sets that run's row lengths.
**Mapping — Yes.** For pre-9 saves the importer flattens each such `G` into a one-step groove
of that length in a spare slot, ranking free slots (LSDj-empty first, then the `6 6` default,
then a groove written but never named) and taking the high slots first, so a groove the song
still shows is never overwritten. The slots it takes are named for the length they hold.

### 6.8 `H` -- hop

**LSDj (M8, §34, §56):**
- *In a table* — `x` times to row `y`. `H00` is an unconditional hop to row 0.
- *In a phrase* — ends the phrase. `HFF` is the special case: it ends the phrase and starts
  the next one at row 15.

**ChipBoy:** `Cmd::H` handles the counted table form on the lane it fired in (§64: it moves
that lane's pointer only). The phrase form sets `hopStep`.
**Mapping — Yes except `HFF`.** ChipBoy's phrases always start at row 0, so `HFF`'s
"start the next phrase at row 15" cannot be expressed; the importer notes it (§56).

### 6.9 `K` -- kill

**LSDj (M9, §9):** kills the note after `xy` ticks. LSDj takes the level to zero with the same
zombie steps as any other level change — fifteen down-triples — and leaves the DAC on.
**ChipBoy:** `Cmd::K`, same, via `killLevel()`.
**Mapping — Yes.**

### 6.10 `L` -- slide

**LSDj (M9 §4, M8 §68 §71):** slides to the note over **`xy + 1` pitch updates**, linear in
semitones, by a fixed step `(target - source) / (xy + 1)` in 1/256 semitones truncated toward
zero, landing on the note one update after the last step. `L00` is instant.

Two properties measured on 8.4.4's wave kick (§71) that are easy to get wrong:

- The **target is latched**. A table steps every tick, so a slide outlives the row that aimed
  it; a later row's transpose column must not drag the target.
- The **target is clamped to a note the channel can sound** *before* the step is divided out.
  The wave channel bottoms at note 24 (period 44); the pulses at note 36. A table transpose
  naming something lower is clamped, so the rate comes out right because the destination does.

**Channels:** all. On noise ChipBoy ignores `L`.
**ChipBoy:** `Cmd::L`, both properties implemented (§71).
**Mapping — Yes** on 9.x. On formats 0-3 `L` is a *speed in register units per clock*, so the
importer converts: `updates = ceil(|period(target) - period(from)| / xy)`, emitted as
`L (updates - 1)`. It needs a note before it in the chain to measure from, and says so when
there is not.

### 6.11 `M` -- master volume

**LSDj (M9, §34):** `x` left, `y` right, into `NR50`.
**ChipBoy:** `Cmd::M`, both nibbles, through `masterFromArg`.
**Mapping — Yes.**

### 6.12 `O` -- output / pan

**LSDj (M9):** `xy` in 0-3 selects the `NR51` bits for the channel.
**ChipBoy:** `Cmd::O`, `Pan(xy & 3)`.
**Mapping — Yes.**

### 6.13 `P` -- pitch bend

**LSDj (M9 §5, M8 §66):** `xy` read as a **two's-complement** signed byte. The step per update
comes from a measured table. `P 0` stops a bend and keeps what it reached; a plain note-on
puts the offset back.
**Channels:** on noise, `P` walks either the note map or the `NR43` nibbles, depending on the
instrument's Sweep mode (§66).
**ChipBoy:** `Cmd::P`, with four speed laws — Fast and Tick bend the note; Step applies one
offset of `x/32` of a semitone and no continuous bend; Drum bends the **period register** and
wraps at 2048, which is what a `P` kick falling off the bottom really does (§5).
**Mapping — Yes** on 9.x; **Value** on formats 0-3, where `P` is register units per clock and
the importer picks the nearest Drum speed (§56).

### 6.14 `R` -- retrigger

**LSDj (M9, §8):** retriggers every `y * (rate + 1) + 1` ticks, so `y = 0` is every tick. `x`
is a signed nibble of volume change per retrigger; `x = 8` is LSDj's *resync*, which runs the
retrigger on the pitch clock instead.
**ChipBoy:** `Cmd::R`, same, via `retrigVolStep`.
**Mapping — Yes.**

### 6.15 `S` -- sweep / shape

**LSDj (M9 §34, M8 §55 §66):**
- *PU1* — the hardware sweep. `x` is the sweep rate; `y` is **`NR10`'s low nibble**: 0-7 sweep
  up at that shift, 8-15 sweep down.
- *NOI* — from format 22 the byte is **semitones** through the noise map, adding up until the
  next note-on. Before that it is a **nibble subtraction on `NR43`**: each nibble of `NR43`
  less the matching nibble of `xy`, mod 16, accumulating.
- *PU2, WAV* — no sweep unit; inert.

**ChipBoy:** `Cmd::S`; on noise it branches on `Instrument::noiseDomain`
(`Notes` / `Register`) for exactly this split. The running delta is one byte, applied on the
way out so it never compounds.
**Mapping — Yes**, with `noiseDomain` set from `LsdjModel::noiseS`.

### 6.16 `T` -- tempo

**LSDj (M9):** sets the tempo to `xy` BPM.
**ChipBoy:** `Cmd::T`; the Player and the Clock own it, so `applyCommand` does nothing.
**Mapping — Yes.**

### 6.17 `V` -- vibrato

**LSDj (M9, §3):** `x` speed, `y` depth. One cycle is `64 / (x + 1)` pitch updates, so `x = 0`
is the **slowest**, not "off". The swing is symmetric about the note; the direction bit only
says which half comes first.
**Channels:** pulses and wave. `V` was added for noise in a later version (**?** — not
traced); ChipBoy ignores it on noise.
**ChipBoy:** `Cmd::V`, same, with the measured tick table for Tick mode. ChipBoy's saw and
square vibrato shapes are its own — the ROM's could not be read off the register log (§13).
**Mapping — Yes** on 9.x; **Value** on format 0, whose vibrato is one-sided below the note in
register units. The importer converts:
`speed = clamp(round(32 / (x + 1)) - 1, 0, 15)`, `depth = clamp(round(8 * y * (x + 1) / 2 / 19.11), 1, 15)`,
with a note, because the shape genuinely differs (centred vs. one-sided).

### 6.18 `W` -- wave / duty

**LSDj (A):** on the pulses, the duty. On a wave instrument, the wave or synth.
**ChipBoy:** `Cmd::W`: `duty = xy & 3` on the pulses, wave slot on WAV.
**Mapping — partly.** The duty is Yes; the importer keeps only the low digit and notes that
the high digit has no register effect. `W` on a *wave instrument* (synth speed / length) is
**No** and dropped with a note.

### 6.19 `Z` -- randomise

**LSDj (D, changelog v6.x):** re-runs the last command with a random amount added. Each digit
is the **maximum possible random value** for that nibble, and the digits are added
independently: `Z02` adds one of 0, 1, 2; `Z20` adds one of `0x00`, `0x10`, `0x20`. Since
8.1.9 it affects every command but `H`. Since 6.1.3 the "last command" is remembered
per-table rather than per-column.
**ChipBoy:** `Cmd::Z` re-runs the last command that is not `Z` or `H` — the other slot or
column when set, else the channel's last — adding `rand(0..x)` to its `a` and `rand(0..y)` to
its `b`.
**Mapping — Value, and worth a look.** ChipBoy adds to the target's two *fields*; LSDj adds to
the target byte's two *nibbles*. For a command whose argument is a nibble pair (`C`, `E`, `M`,
`R`, `S`, `V`) these agree. For a command whose argument is a whole byte (`D`, `K`, `L`, `P`,
`T`, `A`, `G`, `O`) they do not: LSDj's `x` contributes `rand(0..x) * 16` to the byte, where
ChipBoy's contributes `rand(0..x) * 1` and its `y` lands in an unused field. **Not measured
(`D` only) — probe before changing anything.**

---

## 7. Probing another LSDj version against 9.3.9

The goal each time is a yes/no: **does this version behave as 9.3.9 does for this command?**
Yes means it shares 9.3.9's implementation and needs nothing. No means it needs its own field
on `LsdjModel` and a branch in the driver or the importer.

### 7.1 Set up

1. Put the ROM beside the others, outside the tree (`/root/lsdj/lsdj<v>.gb`).
2. Get a **real save** written by that version. If you only have a save from another version,
   open it in the target ROM once and let it convert, then use the converted save — a save
   the ROM has never opened is the untrustworthy case from §1.
3. Note the format the version writes (`s[0x7FFF]` from `dump.py`) and find the matching
   entry in `LsdjModel.cpp`, or copy the nearest one.

### 7.2 Probe one command

For each command you care about:

1. Find a phrase and a table in the real save that use it. `dump.py SAV IDX TABLE` prints a
   table's four columns; the phrase dumper is a few lines on the same offsets.
2. Trace the ROM playing that song (§3.1). Note the cycle and the registers the command moves.
3. Change **one byte** with `work.py` — the command's value, or the command letter — and trace
   again. The difference between the two traces is that byte's effect, with everything else
   held still.
4. Do the same pair on 9.3.9, converting the save if necessary.
5. Compare the two differences, not the two traces.

**What to look at per command**, so a "same" verdict means something:

| Command | The register that answers it | The thing to read |
|---|---|---|
| `A` `H` | any | which row's values appear, and when |
| `C` | `NRx3`/`NRx4` | the note sequence and the tick spacing |
| `D` `K` | `NRx4` trigger, `NRx2` | how many ticks before the trigger / the kill |
| `E` | `NRx2` | **the low nibble**: 8 means software, anything else means the chip runs it (§70) |
| `F` | `FF30-FF3F` on WAV, `NRx3/4` on PU2 | which frame is loaded / the period offset |
| `G` `T` | any | the tick spacing of the rows |
| `L` `P` | `NRx3`/`NRx4` | the period per pitch clock; check whether it is geometric (semitones) or linear (register units) |
| `M` | `FF24` | the two nibbles |
| `O` | `FF25` | the channel's two bits |
| `R` | `NRx4` trigger, `NRx2` | the retrigger interval and the volume step |
| `S` | `FF10` on PU1, `FF22` on NOI | the sweep byte / the `NR43` delta, nibble by nibble |
| `V` | `NRx3`/`NRx4` | the swing: symmetric about the note or one-sided below it, and the period |
| `W` | `NRx1` duty bits, `FF30-FF3F` | the duty / the wave loaded |
| `Z` | whichever the target command moves | whether the random lands on the byte's nibbles or on two fields |

### 7.3 Decide

- **Same** — the register writes have the same shape and the same numbers. Point the new
  model entry at 9.3.9's tables and rules. Nothing else to do.
- **Different in value only** — the shape is the same but a constant differs. Add the constant
  to `LsdjModel` (as `envPeriods` and `noiseMap` already are) and branch on it in the
  *importer*.
- **Different in kind** — the command does something 9.3.9's code cannot express with a
  different constant (the chip-vs-software envelope of §70; the register-vs-semitone pitch law
  of §56). Add an enum to `LsdjModel`, add the field the *instrument* carries so a bank can
  hold both behaviours at once, and branch in the **driver**. The `EnvelopeLaw` /
  `envChipTiming` pair is the pattern to copy.

Set `LsdjModel::measured = true` only for what that ROM actually traced. An entry assumed from
a neighbour keeps `measured = false`, and the import dialog says so.

Then add a case to `Tests/LsdjImportTests.cpp` that reads a synthetic song under the new model.
`docs/HANDOFF.md` repeats these steps, and the head of `Source/core/Import/LsdjModel.h` is the
checklist.

---

## 8. Flagging what does not map

A song written in an old version and opened in 9.3.9 is format-converted by LSDj, but
conversion does not always preserve the sound. ChipBoy has to be honest about that rather than
silently producing something different.

**The mechanism** is `notes.add(...)` in `Source/core/Import/LsdjSong.cpp`. Every note reaches
the user in the *Import .sav…* dialog and in `chipboy_recordtest --import-sav`'s summary, and
they are the first thing to read after an import.

**A note must say three things**: what was in the save, where it was, and what ChipBoy did
instead. The existing ones are the model:

```
"V" + hex2(v) + " at " + where + ": this format's vibrato swings below the note in register
 units; ChipBoy's is centred (speed " + ... + ", depth " + ... + ")"
"HFF at phrase 6D step 0 ends the phrase 15 times and then lets it play in full; ChipBoy ends
 it every time (section 56)"
"B" + hex2(v) + " (MayBe) at " + where + ": no ChipBoy equivalent; dropped"
```

**When to add one:**

| Case | Note it? |
|---|---|
| Mapped exactly, same audible result | No |
| Mapped with a converted value, same audible result | No — but say so in the design section |
| Mapped with a converted value, *approximately* the same | **Yes**, and give both numbers |
| Not expressible; dropped | **Yes**, naming the cell |
| Expressible only by changing another slot as a side effect | **Yes**, saying what was changed |
| A whole feature is missing (`B`) | **Yes**, once per occurrence with the location |

Prefer one note per distinct cause with the location in it, not one per row — an import that
prints two hundred lines is an import nobody reads. The importer already collapses repeats;
keep that.

**When conversion is the right answer instead of a note**: if a value can be translated so the
result is *identical*, translate it and stay quiet. The groove flattening of §63 is the model
— an old table `G` becomes a purpose-made one-step groove and sounds the same, so the note
only mentions that the numbers moved, not that anything was lost.

---

## 9. What is still open

Ordered by how much it costs a real song.

1. **`B` (MayBe) is missing entirely** (§6.2). Engine work: a probability gate on a note-on and
   on a table hop.
2. **`Z`'s random may land on the wrong digits** (§6.19). Documented, not measured. Cheap to
   probe, and it silently changes any song using `Z` on a byte-argument command.
3. **`F` finetune on PU1 and NOI** (§6.6) is dropped. Not traced.
4. **`W` on a wave instrument** (synth speed / length) is dropped (§6.18).
5. **Phrase `HFF`** cannot be expressed (§6.8); ChipBoy's phrases always start at row 0.
6. **Kit `DIST` mixing** — narrowed to instrument byte 13's bit 6; needs a kit-stream decoder
   to read the modes off. 61 of 69 kit instruments in the user's saves mix two kits.
7. **`C` and `V` on noise** — LSDj added both at some point; neither is traced, and ChipBoy
   ignores both there.
8. **`LSDJ_PARITY.md` was measured on 9.2.J.** §7 and §10 have been marked as 8.8.0-and-after;
   the rest has not been re-read for version sensitivity, and §70 is the standing proof that
   it can matter.
9. **Formats 16-21 are unmeasured** for §63's table `G` and the §64 envelope hop.

### Where SPACE TI stands (the working song, LSDj 8.4.4)

Note-ons against the ROM over 23.8 s, longest common run: **PU1 67/103, PU2 104/153,
WAV 198/219, NOI 32/32.** PU1 and PU2 are the weak ones.
