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
| **939** | **Measured on 9.3.9**, on the rig in §3, by logging register writes. The standard this table is held to. |
| **92J** | Measured on 9.2.J -- the same format-22 model, so expected to hold, but not confirmed on 9.3.9. |
| **M8** | Measured on 8.4.4 or 5.0.3. Kept only where it says something about an older format. |
| **D** | From LSDj's own changelog -- documented, not traced. **Twice now the changelog has been wrong about the current build** (see `B`). |
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
| `A` | Run table `xy`; **`A20` stops it**, even when the instrument names a table | all | same | `Cmd::A`, slot `xy + 1`, 0 stops | Yes (slot + 1) | **939** |
| `B` | **Chance.** Phrase: two rolls, note sounds if either passes. Table: hop to row `y` with chance `x` | all | **different** | **absent** | **No** -- dropped with a note | **939** |
| `C` | Chord: note, note+`x`, note+`y`, one step a tick | PU1 PU2 WAV **and NOI** | same | `Cmd::C`; **ignored on noise** | Yes on PU/WAV; **Engine** on noise | **939** |
| `D` | Delay the note by exactly `xy` ticks | all | same | `Cmd::D`, read at the note-on | Yes | **939** |
| `E` | Level `x`, walked with zombie steps; `y` the rate, one level every `y / 64` s | PU1 PU2 NOI (WAV: level only) | same | `Cmd::E`, one measured table | Yes | **939** |
| `F` | **PU1**: `y`/32 semitone **down**, `x` ignored. **PU2**: `x` semitones + `y`/32 **up**. **WAV**: the frame | PU1 PU2 WAV | same | frame on WAV, whole byte as transpose on PU2, **dropped on PU1** | **Value** -- §6.6; PU1 maps onto `fineOffset` | **939** |
| `G` | Groove `xy`, walked | all | see §5 | `Cmd::G`, slot `xy + 1` | Yes (slot + 1); pre-9 differs (§63) | **939** |
| `H` | Phrase: hop out of it. Table: hop to row `y`, `x` times (`x = 0` always) | all | **different** | `Cmd::H` both forms | Yes, except phrase `HFF` (§56) | **939** (table); M8 (phrase `HFF`) |
| `K` | Kill the note after exactly `xy` ticks | all | same | `Cmd::K`, same | Yes | **939** |
| `L` | Slide to the note over exactly `xy + 1` pitch updates, linear in semitones | all | same | `Cmd::L`; §68, §71 | Yes | **939** |
| `M` | `NR50` directly: `x` left, `y` right | global | same | `Cmd::M`, both nibbles | Yes | **939** |
| `O` | 0 off, 1 left, 2 right, 3 both | all | same | `Cmd::O`, `Pan(xy & 3)` -- the enum is in this order | Yes | **939** |
| `P` | Pitch bend, `xy` two's complement, non-linear step table | all (NOI differs) | same | `Cmd::P`; `bendStep256` matches the ROM within 1-2 % | Yes | **939** |
| `R` | Retrigger every `y` ticks, `x` a volume step; `x=8` resyncs | all | same | `Cmd::R`, same | Yes | 92J |
| `S` | **PU1: `NR10 = ((-x) & 15) << 4 \| ((-y) & 15)`** -- each nibble negated. NOI: semitones through the map. PU2, WAV: inert | PU1 NOI | same | writes `(x & 7) << 4 \| (y & 15)` -- **wrong** | **Engine** -- §6.15 | **939** |
| `T` | Tempo: the byte in BPM | global | same | `Cmd::T`, the Clock owns it | Yes | **939** |
| `V` | Vibrato: one cycle every `64 / (x + 1)` pitch updates, `y` the depth | PU1 PU2 WAV **and NOI** | same | `Cmd::V`; **ignored on noise** | Yes on PU/WAV; **Engine** on noise | **939** |
| `W` | PU: duty from the **low nibble**, high ignored. WAV: no effect seen -- §6.18 | PU1 PU2 (WAV **?**) | same | `Cmd::W`: `xy & 3` duty, wave slot on WAV | Yes for duty; WAV **?** | **939** (pulses) |
| `Z` | Re-runs **the last command executed**, adding random `0..x` to the target byte's high nibble and `0..y` to its low | all | same | re-runs the **other slot/column**, adding to its `a`/`b` fields | **Engine** -- §6.19 | **939** |

Only three rows are not fully settled on 9.3.9: `R` (still the 9.2.J measurement), phrase `H`
(the `HFF` special case, from 8.4.4), and `W` on a wave instrument (`?`).

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

### 3.4 The 9.3.9 command rig

`tools/lsdjref/probe_fmt22.py` and `tools/lsdjref/run.py` are the rig every **939** row in this
document was measured on. It builds a controlled song into a save's **working area**, the path
LSDj takes for a song being edited, on top of one of two hosts:

- a **real editor-written save** in that format (`Probe()`), overwriting only the song row,
  chain, phrase, table and instrument being probed, and only in slots the host already
  allocates; or
- a save the **ROM formatted itself** (`Probe(host=..., blank=True)`), from
  `lsdjref_trace --init-sav` given ~3000 frames to finish. This needs no save from anyone.

The two were compared on the cases where a *synthetic* save had gone wrong before -- a table
`H` in the second command column (§58) and a table `G` (§63) -- and they agree, so the
bootstrapped host is sound. See `docs/plan-lsdj-version-sweep.md`.

```python
import sys; sys.path.insert(0, 'tools/lsdjref')
from probe_fmt22 import Probe, INST, TABLE
import run

p = Probe()
ph = p.only(0)                                   # PU1 alone, song ends after row 0
p.tempo(128, (6, 6))
p.pulse(INST[0], env=0xF0, duty=2, pan=3)        # flat level 15, no table, no vibrato
p.phrase(ph, {0: (0x18, INST[0], 'F', 0x0F)})    # note, instrument, command
p.write('probe.sav')
run.trace('probe.sav', 'probe.csv')              # 9.3.9 by default
run.show('probe.csv', 0)                         # the channel's writes, periods decoded
```

`Probe.table(slot, rows)` fills a table; `Probe.wave()` and `Probe.noise()` build the other
instrument kinds; `run.events()` returns a channel's writes with `t = 0` at its first trigger.

**Validate the rig before trusting it.** A plain note on PU1 should come out as one `NR12 = F8`
(the software-envelope hold), one period write and one trigger, and two notes eight rows apart
at 128 BPM should be 0.9375 s apart. Both were checked before any measurement here was taken.

### 3.5 Isolate one variable inside a real save

`/root/lsdj/work.py SAV FILEIDX OUT.sav [addr=val ...]` decompresses one song out of a save,
patches the bytes named, writes it into the save's **working area** (0x0000-0x7FFF) and sets
`0x8140 = 0xFF` so LSDj boots straight into it. That plays the user's own song with a single
byte changed -- the only trustworthy way to probe a table command (§1, warning 1).

`/root/lsdj/dump.py SAV` lists the songs; `dump.py SAV IDX [TABLE...]` prints a song's header
and any table's four columns.

### 3.6 Isolate one variable inside ChipBoy

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

### 3.7 Reading the ROM directly

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

**LSDj 9.3.9 (939):** `Axy` runs table `xy` on the channel; `A20` **stops** the table, and does
so even when the instrument itself names one. Measured with a table whose row 1 transposes
+12: with no `A` the period stays 1517; with `A04` it walks 1517 → 1783 → 1517; with the
instrument pointing at table 04 the result is identical; with `A20` it stays 1517 throughout.
**ChipBoy:** `Cmd::A` with slot `xy + 1` (ChipBoy's slots are 1-based, 0 = none).
**Mapping — Yes.** The importer prefers the cell's *Table column* when the `A` sits in a phrase
cell, since ChipBoy has a dedicated column for it. `A20` cannot be expressed in that column and
is dropped with a note; as a command it becomes `A 0`.

### 6.2 `B` -- chance *(not implemented)*

**LSDj 9.3.9 (939).** Measured, and **the changelog's examples are the wrong way round for this
build** -- it describes `B00` as "always plays" and a high value as "usually skips", where
9.3.9 does the opposite. Do not take the changelog's sense on trust here.

*In a phrase* — the byte gates whether the note sounds. Each nibble is an independent roll of
about `n/15`, and the note sounds if **either** passes. Over 102 notes:

| `B` | `00` | `02` | `04` | `08` | `0C` | `0F` | `20` | `40` | `80` | `F0` | `22` | `44` | `FF` |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| played | 0 | 17 | 30 | 61 | 84 | 102 | 13 | 23 | 53 | 102 | 28 | 48 | 102 |

`B44` at 48/102 is what two independent rolls predict -- `1 - (1 - 23/102)(1 - 30/102) = 45%` --
not the 30 that "take the larger nibble" would give, and `B22` (28) matches the same rule.
`B00` never plays; `B0F`, `BF0` and `BFF` always do.

*In a table* — a hop that only sometimes happens: **`x` is the chance and `y` the destination
row**. With rows 0-3 stepping +0/+12/+24/+36 and the `B` on row 3, `BF0` gives a three-row
cycle (it always hops to row 0), `BF1` a two-row cycle, `B80` hops about half the time, and
`B00`, `B08` and `B0F` never hop at all -- so a zero high nibble is "never", not "always".

**ChipBoy:** nothing. The letter is not in `bank::Cmd`.
**Mapping — No.** The importer drops it with a note naming the cell. To close it: add `B` to
`Cmd`, to the parameter table and to the driver -- the table form is an `H` behind a
probability gate, the phrase form a gate on the note-on. Being random it cannot be flattened
into anything deterministic, so this is engine work, not a mapping.

### 6.3 `C` -- chord

**LSDj 9.3.9 (939 on noise, 92J elsewhere):** arpeggiates the note with `note`, `note + x`,
`note + y`, one step per tick.
**Channels:** pulses and wave — **and noise, which works.** Measured: a noise note whose
`NR43` is `50` with `C 3 7` cycles `NR43` through `50 → 15 → 05 → 50 …`, walking the note map
the same way 9.x's `S` does. ChipBoy ignores `C` on noise entirely.
**ChipBoy:** `Cmd::C`, `chord[0] = 0`, `chord[1] = x`, `chord[2] = y`, one step every
`chordRate + 1` ticks; `if (noise) break;` at the top.
**Mapping — Yes on the pulses and wave; Engine on noise.** The importer can already carry the
byte; the driver has to stop discarding it and walk the noise map, which it can already do —
`writePeriod` maps a note to `NR43` for the noise channel, so `C` needs the same treatment `S`
got in §66.

### 6.4 `D` -- delay

**LSDj 9.3.9 (939):** delays the note by exactly `xy` ticks. Measured against the start key:
`D01` moves the note one tick later, and `D02` through `D18` step by exactly their difference.
**ChipBoy:** `Cmd::D`; a slot's `D` is read at the note-on rather than applied live.
**Mapping — Yes.**

### 6.5 `E` -- envelope

**LSDj 9.3.9 (939):** `x` is the level; `y` is the rate. **`E` never triggers** from 8.8.0 on;
before that it re-attacks (§59).

- **The level** walks to `x` with zombie steps. Measured with the `E` on a row after the note,
  from the instrument's level 15: `EC0` issues 3 down-steps, `E80` 7, `E40` 11 and `E00` 15 --
  exactly `15 - x` each time. With the `E` on the note's own row there are no steps at all,
  because the note-on writes the level directly.
- **The rate** is one level every `y / 64` s -- the chip's own interval, `y * 65536` cycles.
  Measured in pitch clocks: **6, 11, 17, 22, 28, 34, 39** for rates 1-7. That is *not*
  `LSDJ_PARITY.md` §7's table (15, 20, 27, 36, 36 for rates 3-7, with 6 and 7 equal), which
  came off generated probe saves on 9.2.J; rates 6 and 7 are a sixth apart. See §70.

**Channels:** on WAV/KIT the level is `NR32`'s two bits, so `x` is clamped to 0-3 and `y` is
meaningless.
**ChipBoy:** `Cmd::E` sets `envVol`, `envRate`, `envDir` and takes a shaped envelope over
(`shapedTaken`), then steps the level itself in software — on either table, chosen by
`Instrument::envChipTiming`. It emits §26's zombie writes (`08` up, `09 11 18` down), so a
`NRx2` value of `08` in a ChipBoy trace is an *increment*, not "volume 0".
**Mapping — Yes**, with `envChipTiming` and `envRetrig` set from the model's `EnvelopeLaw`.

### 6.6 `F` -- finetune / frame

**LSDj 9.3.9 (939).** Measured on every channel, and it is three different things:

- **PU1** — a **downward finetune**. The **low nibble only**; `x` does nothing (`FF0` and `F10`
  leave the period untouched). The shift is about `y/32` of a semitone, applied in period
  units, so it scales with where the note sits. `F0F` measured against the semitone width:

  | note | period | semitone width | `F0F` delta | as a fraction of a semitone |
  |---|---|---|---|---|
  | `10` | 1205 | 48 | −23 | 0.479 |
  | `18` | 1517 | 30 | −15 | 0.500 |
  | `20` | 1714 | 18 | −9 | 0.500 |
  | `28` | 1837 | 12 | −6 | 0.500 |
  | `30` | 1915 | 8 | −4 | 0.500 |
  | `38` | 1964 | 5 | −2 | 0.400 |

  So `F0F` is half a semitone down and `F01` is 1/32 of one. The last unit of rounding is not
  pinned — a couple of widths come out one period unit off a plain `round(y * width / 32)` —
  but the law is clear.
- **PU2** — an **upward transpose**: `x` **semitones** plus `y/32` of a semitone. Measured from
  note `18` (period 1517): `F10` gives 1547, which is exactly note `19`; `FF0` gives 1825,
  which is exactly fifteen semitones up; `F01` gives 1518 and `F0F` gives 1531.
- **WAV** — selects the frame. `F00`, `F03` and `F0F` load visibly different wave RAM.
- **NOI** — not measured (`?`).

**ChipBoy:** `Cmd::F` names the wave frame on WAV (§65), sets `instTranspose` from the **whole
byte** as two's complement on PU2, and is **dropped on PU1**.
**Mapping — Value, and both pulses need work.**

- PU1 is exactly expressible: ChipBoy's `fineOffset` is in 1/256 semitones, so `F y` is
  `fineOffset -= 8 * y`. Nothing new is needed in the engine, only in the importer and a
  driver case.
- PU2 is currently wrong: the whole byte is read as a signed semitone count where LSDj reads
  `x` semitones **and** `y/32`. It should be `instTranspose += x` and `fineOffset += 8 * y`.

### 6.7 `G` -- groove

**LSDj 9.3.9 (939):** selects groove `xy`, zero-based, and **walks** it. Measured with groove
1 set to `3 3`: eight rows take 0.9376 s under `G00` (groove 0, `6 6`) and 0.4688 s under
`G01`. Before 9.x the row carrying a table's `G` instead takes the groove's **first step** as
its own length (§63, measured on 8.4.4).
**ChipBoy:** `Cmd::G` with slot `xy + 1`. Inside a table it sets that run's row lengths.
**Mapping — Yes.** For pre-9 saves the importer flattens each such `G` into a one-step groove
of that length in a spare slot, ranking free slots (LSDj-empty first, then the `6 6` default,
then a groove written but never named) and taking the high slots first, so a groove the song
still shows is never overwritten. The slots it takes are named for the length they hold.

### 6.8 `H` -- hop

**LSDj 9.3.9 (939).**

- *In a table* — **hop to row `y`, `x` times**. Measured with rows 0-3 transposing +0/+4/+8/+12
  and the `H` on row 3: `H00` cycles three rows for ever (it always hops), `H10` hops once and
  then lets row 3 through, `H20` hops twice, and `H01` settles into a two-row cycle on rows 1
  and 2. So `x = 0` is "always" and otherwise `x` is a count.
- *In a phrase* — **jump to step `xy`**. Measured with a rising note on all sixteen steps and
  the `H` on step 4: `H00` gives a four-step cycle, `H02` a two-step cycle on steps 2 and 3,
  and `H0A` runs 0-4 then 10-15. `HFF` silences the channel outright.

**ChipBoy:** `Cmd::H` handles the counted table form on the lane it fired in (§64: it moves
that lane's pointer only). The phrase form sets `hopStep`.
**Mapping — Yes for the table form.** The phrase form is a jump within the phrase, which
ChipBoy expresses; `HFF` is the exception (§56) and the importer notes it.
**Still open:** whether the high nibble counts repeats in a *phrase* as it does in a table was
not tested -- every phrase probe here used `x = 0`.

### 6.9 `K` -- kill

**LSDj 9.3.9 (939):** kills the note after exactly `xy` ticks -- measured at 1, 3 and 6 ticks
for `K01`, `K03` and `K06`. It takes the level to zero with the same zombie steps as any other
level change (fifteen down-triples from level 15) and leaves the DAC on.
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

**LSDj 9.3.9 (939):** the byte goes straight into `NR50` -- `M40` writes `40`, `M07` writes
`07`, `M73` writes `73`. `x` is the left level and `y` the right.
**ChipBoy:** `Cmd::M`, both nibbles, through `masterFromArg`.
**Mapping — Yes.**

### 6.12 `O` -- output / pan

**LSDj 9.3.9 (939):** `xy` in 0-3 selects the channel's `NR51` bits: measured `O00` off,
`O01` left only, `O02` right only, `O03` both -- which is exactly ChipBoy's
`Pan { Off, Left, Right, Both }` order.
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

**LSDj 9.3.9 (939):** retriggers every **`y` ticks**. Measured on PU1 with the instrument's
command rate at 0: `R01` gives one trigger a tick, `R02` one every two, `R04` one every four.
`y = 0` retriggers **once** and stops -- the changelog dates that to v8.8.1, restoring 4.7.3's
behaviour. `x = 8` runs the retrigger on a faster clock (measured at about 0.29 ticks, roughly
two pitch clocks); other values of `x` are a signed volume step.
**ChipBoy:** `Cmd::R` with the interval `y * (cmdRate + 1) + 1` ticks, so at command rate 0 it
retriggers every `y + 1` ticks and treats `y = 0` as *every tick*.
**Mapping — Engine.** Both halves are off by one idea: the interval is one tick too long, and
`y = 0` should fire once rather than continuously. Small fix, audible on any drum table.

### 6.15 `S` -- sweep / shape

**LSDj 9.3.9 (939).**

- **PU1** — writes `NR10`, but **not** as the byte. Each nibble is negated:
  `NR10 = ((-x) & 15) << 4 | ((-y) & 15)`. Confirmed on eight values:

  | `S` | `00` | `11` | `12` | `23` | `2B` | `34` | `71` | `88` | `FF` |
  |---|---|---|---|---|---|---|---|---|---|
  | `NR10` | `00` | `FF` | `FE` | `ED` | `E5` | `DC` | `9F` | `88` | `11` |

  So `S00` leaves the sweep off and `S11` is the slowest sweep at the deepest shift.
- **NOI** — the byte is **semitones** through the noise map, accumulating until the next
  note-on. Measured: from a note whose `NR43` is `50`, `S01` gives `17`, `S02` `23`, `SFF` `25`,
  `SFE` `33` — map lookups, not arithmetic on the register.
- **PU2 and WAV** — inert. Measured: `S 23` changes no register on either.

**ChipBoy:** `Cmd::S` writes `NR10 = (x & 7) << 4 | (y & 8 ? 8 : 0) | (y & 7)` — `23` for `S23`,
where the ROM writes `ED`. On noise it branches on `Instrument::noiseDomain` for the
semitones-vs-nibbles split, which is right.
**Mapping — Engine on PU1.** Every imported PU1 sweep is currently wrong, in rate, direction
and shift at once. The fix is one line in `applyCommand` and one in the importer; the noise and
inert cases already agree.

### 6.16 `T` -- tempo

**LSDj 9.3.9 (939):** the byte is the tempo in BPM. Measured over eight rows: `T40` gives
64 BPM, `T80` 128 and `TC0` 192.
**ChipBoy:** `Cmd::T`; the Player and the Clock own it, so `applyCommand` does nothing.
**Mapping — Yes.**

### 6.17 `V` -- vibrato

**LSDj 9.3.9 (939 on noise, 92J elsewhere):** `x` speed, `y` depth. One cycle is `64 / (x + 1)`
pitch updates, so `x = 0` is the **slowest**, not "off". The swing is symmetric about the note;
the direction bit only says which half comes first.
**Channels:** pulses and wave — **and noise, which works.** Measured: a noise note holding
`NR43 = 50` takes `V 4 8` and starts moving `NR43` continuously (`DD CF DB DC DF 10 20 05 07
23 …`), so the vibrato drives the LFSR clock through the map. ChipBoy ignores `V` on noise.
**ChipBoy:** `Cmd::V`, with the measured tick table for Tick mode, and `if (noise) break;`.
ChipBoy's saw and square vibrato shapes are its own — the ROM's could not be read off the
register log (`LSDJ_PARITY.md` §13).
**Mapping — Yes on the pulses and wave; Engine on noise**, the same gap as `C`.

### 6.18 `W` -- duty / wave

**LSDj 9.3.9 (939 on the pulses).** On a pulse the **low nibble** is the duty and the high
nibble is ignored: `W00` writes `NR11 = 00`, `W03` writes `C0`, and `WF1` writes `40` — the
same as `W01`.
On the wave channel **nothing was observed**: `W00`, `W02` and `W0F` all loaded identical wave
RAM on an instrument with synth 0. That is consistent with `W` there setting a synth's speed or
length rather than choosing a wave, which this probe would not show — it needs an instrument
whose synth actually animates. **Still `?`.**
**ChipBoy:** `Cmd::W`: `duty = xy & 3` on the pulses, wave slot on WAV.
**Mapping — Yes for the duty.** The importer keeps only the low digit and notes that the high
digit has no register effect, which the measurement now backs. `W` on a *wave instrument* is
dropped with a note, and stays open until the wave side is measured.

### 6.19 `Z` -- randomise

**LSDj 9.3.9 (939).** Two things, and ChipBoy has both wrong.

1. **`Z` re-runs the last command *executed*, not the other column's.** Measured in a table
   with `W 00` in column 1 and `Z` in column 2 of the same row: the duty never varied. Move the
   `Z` to the **next row** of the same column and it varies at once. (The second column is not
   the problem: `cmd2 = W03` on its own drives the duty exactly as `cmd1 = W03` does, and with
   both columns set both fire.)
2. **Each digit adds a random `0..that digit` to the matching nibble of the target's byte.**
   With `W 00` on row 0 and `Z 03` or `Z 0F` on row 1, the duty takes every value 0-3; with
   `Z F0` it stays 0, because the random lands on the high nibble and `W` reads only the low
   one. That is the changelog's own description -- `Z20` adds one of `0x00`, `0x10`, `0x20` --
   and here the changelog and the ROM agree.

**ChipBoy:** `Cmd::Z` re-runs the last command that is not `Z` or `H`, preferring **the other
slot or column** and falling back to the channel's last; it adds `rand(0..x)` to the target's
`a` field and `rand(0..y)` to its `b`.
**Mapping — Engine, on both counts.**

- The *source* is wrong: prefer the previous command in time, not the other column.
- The *arithmetic* is wrong for any command whose argument is a whole byte rather than a nibble
  pair. For `C`, `E`, `M`, `R`, `S`, `V` the two agree, because `a` and `b` really are the
  nibbles. For `A`, `D`, `G`, `K`, `L`, `O`, `P`, `T` they do not: LSDj's `x` contributes
  `rand(0..x) * 16` to the byte, where ChipBoy's contributes `rand(0..x) * 1` and its `y` lands
  in a field the command does not read.

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

## 9. Verification status against 9.3.9

**Every command has now been traced on 9.3.9** with the rig in §3.4. What each probe read:

| Cmd | What was measured on 9.3.9 |
|---|---|
| `A` | table select against a transposing table; `A20` stops even an instrument's own table |
| `B` | phrase probability over 102 notes at 13 values; table hop chance and destination row |
| `C` | the chord cycle on PU1; `NR43` walking the map on noise |
| `D` | the delay in ticks, timed from the start key |
| `E` | the level's zombie-step count at five values; the rate at all seven, in pitch clocks |
| `F` | full nibble sweeps on PU1 and PU2 across six notes, plus wave RAM on WAV |
| `G` | row length under two grooves |
| `H` | table: count and destination; phrase: destination step, and `HFF` |
| `K` | the kill time in ticks at three values |
| `L` | the update count at four durations |
| `M` | `NR50` at three values |
| `O` | `NR51` at all four values |
| `P` | the drift rate in semitones at six values, against `bendStep256` |
| `R` | the retrigger interval at four values, and `y = 0` |
| `S` | `NR10` on PU1 at eight values; the map on noise; inert on PU2 and WAV |
| `T` | the tempo at three values |
| `V` | the vibrato cycle at five speeds on PU1; `NR43` moving on noise |
| `W` | the duty at three values on PU1; wave RAM on WAV |
| `Z` | the source command and the per-nibble arithmetic |

**Three things are still not settled**, and all three are named where they belong:

1. **`W` on a wave instrument** (§6.18) showed no effect on an instrument with synth 0. It needs
   one whose synth actually animates.
2. **Phrase `H` with a non-zero high nibble** (§6.8): every phrase probe used `x = 0`, so
   whether it counts repeats there as it does in a table is untested.
3. **`HFF`'s exact semantics** (§6.8): it silenced the channel here, where 8.4.4 was measured as
   ending the phrase fifteen times and then letting it play in full (§56).

**Rig validation, done before any of this was trusted.** The working-area path was checked
against booting the save as a file: 12549 of 12551 register writes match in order, the two
exceptions being an adjacent swap of unrelated registers. A plain note comes out as one
`NR12 = F8`, one period write and one trigger, and two notes eight rows apart at 128 BPM are
0.9375 s apart. The probe song is built on a real editor-written format-22 song and only writes
into slots that song already allocates, which is what keeps it clear of §1's first warning.

**One trap worth repeating**: LSDj plays a short blip when `START` is pressed, about 1.8 s
before the song's own first note. Anchoring on "the first trigger after the key" catches the
blip and every timing that follows is nonsense. `run.py` anchors past it; a new probe must too.

---

## 10. What is still open in ChipBoy

Ordered by how much it costs a real song. Every one of these is now backed by a 9.3.9
measurement, so they are ready to implement rather than to investigate.

1. **`S` on PU1 is wrong in every imported song** (§6.15). LSDj negates each nibble into
   `NR10`; ChipBoy writes the byte more or less as it stands. Rate, direction and shift are all
   off. One line in `applyCommand`.
2. **`B` (chance) is missing entirely** (§6.2). Engine work: a probability gate on the note-on
   and on a table hop. Fully characterised, ready to build.
3. **`Z` is wrong twice over** (§6.19): it re-runs the other column rather than the last command
   executed, and its random lands on the wrong digits for byte-argument commands.
4. **`R`'s interval is a tick too long, and `y = 0` should fire once** (§6.14) rather than every
   tick. Audible on any drum table.
5. **`C` and `V` are dropped on noise** (§6.3, §6.17) and both work on the ROM. The driver
   already maps a note to `NR43`, so this is the shape of fix `S` got in §66.
6. **`F` is dropped on PU1 and wrong on PU2** (§6.6). PU1 maps exactly onto `fineOffset` as
   `-8 * y`; PU2 needs `x` semitones plus `y/32`, not the whole byte as semitones.
7. **`W` on a wave instrument** is unmeasured and dropped (§6.18).
8. **Phrase `HFF`** cannot be expressed (§6.8); ChipBoy's phrases always start at row 0.
9. **Kit `DIST` mixing** — narrowed to instrument byte 13's bit 6; needs a kit-stream decoder.
   61 of 69 kit instruments in the user's saves mix two kits.
10. **`LSDJ_PARITY.md` was measured on 9.2.J** with generated probe saves. §7 has now been
    superseded outright by a real-save measurement (§70); the rest has not been re-read for the
    same problem.

### Where SPACE TI stands (the working song, LSDj 8.4.4)

Note-ons against the ROM over 23.8 s, longest common run: **PU1 67/103, PU2 104/153,
WAV 198/219, NOI 32/32.** PU1 and PU2 are the weak ones.
