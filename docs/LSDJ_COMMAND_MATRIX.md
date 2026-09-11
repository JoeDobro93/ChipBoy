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
| **939✓** | Measured on 9.3.9 **and re-measured independently** in the stage-1 verification pass, which also read the ROM's own code for the case. The strongest mark here. |
| **939✗** | The first 9.3.9 measurement was **wrong or incomplete**; the entry has been corrected. What it used to say is kept in the entry, because a wrong reading that survived one campaign can survive another. |
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
| `A` | Run table `xy`; **`A20` stops it**, even when the instrument names a table. `A00` runs table 00 | all | same | `Cmd::A`, slot `xy + 1`, 0 stops | Yes (slot + 1) | **939✓** |
| `B` | **Chance.** Phrase: two rolls at `n`/**15**, note sounds if either passes. Table: hop to row `y` with chance `x`/**16** -- a different law, so `BF0` misses one hop in sixteen | all | **different** | **absent** | **No** -- dropped with a note | **939✗** |
| `C` | Chord: note, note+`x`, note+`y`, one step a tick | PU1 PU2 WAV **and NOI** | same | `Cmd::C`; **ignored on noise** | Yes on PU/WAV; **Engine** on noise | **939** |
| `D` | Delay the note by exactly `xy` ticks | all | same | `Cmd::D`, read at the note-on | Yes | **939** |
| `E` | Level `x`, reached by zombie steps. `y` is **bit 3 = direction, bits 0-2 = rate** into an 8-entry ROM table; rate 0 holds. **WAV reads `y`, not `x`** | PU1 PU2 NOI (WAV: level only) | same | `Cmd::E`; `y` right on the pulses, **`x` wrong on WAV** | Yes on PU/NOI; **Engine** on WAV | **939✗** |
| `F` | **PU1**: `y`/32 semitone **down**, `x` ignored. **PU2**: `x` semitones + `y`/32 **up**. **WAV**: the frame | PU1 PU2 WAV | same | frame on WAV, whole byte as transpose on PU2, **dropped on PU1** | **Value** -- §6.6; PU1 maps onto `fineOffset` | **939** |
| `G` | Groove `xy`, walked | all | see §5 | `Cmd::G`, slot `xy + 1` | Yes (slot + 1); pre-9 differs (§63) | **939** |
| `H` | Table: hop to row `y`, `x` times (`x = 0` always). Phrase: **`x = 0` ends it** and the next phrase starts at step `y`; `x > 0` hops *within* it, `x` times | all | **different** | `Cmd::H` is the table form; a cell's is the phrase length | Yes for the table form; **Engine** for a counted phrase hop | **939✗** |
| `K` | Kill the note after exactly `xy` ticks | all | same | `Cmd::K`, same | Yes | **939** |
| `L` | Slide to the note over exactly `xy + 1` pitch updates, linear in semitones | all | same | `Cmd::L`; §68, §71 | Yes | **939** |
| `M` | Per side, through a lookup: **0-7 sets** that side's volume, **8-15 shifts** it by 0 +1 +2 +3 −4 −3 −2 −1, clamped | global | same | `Cmd::M` already relative, but its **down half is wrong** (0 −1 −2 −3) | **Engine** -- §6.11 | **939✗** |
| `O` | 0 off, 1 left, 2 right, 3 both | all | same | `Cmd::O`, `Pan(xy & 3)` -- the enum is in this order | Yes | **939** |
| `P` | Pitch bend, `xy` two's complement, non-linear step table | all (NOI differs) | same | `Cmd::P`; `bendStep256` matches the ROM within 1-2 % | Yes | **939** |
| `R` | Retrigger every `y` ticks, `x` a volume step; `x=8` resyncs | all | same | `Cmd::R`, same | Yes | 92J |
| `S` | **PU1: each nibble is *added* to the instrument's sweep byte and the result inverted** -- `NR10 = ((-x) & 15) << 4 \| ((-y) & 15)` only for the first `S` of a note on a sweep-00 instrument. NOI: semitones through the map, also accumulating. PU2, WAV: inert | PU1 NOI | same | writes `(x & 7) << 4 \| (y & 15)`, and absolute -- **wrong twice** | **Engine** -- §6.15 | **939✗** |
| `T` | Tempo: the byte in BPM for **40-255**; bytes **0-39 mean 256-295 BPM** | global | same | `Cmd::T`, the Clock owns it | Yes for 40-255; **Value** for 0-39 | **939✗** |
| `V` | Vibrato: one cycle every `64 / (x + 1)` pitch updates, `y` the depth | PU1 PU2 WAV **and NOI** | same | `Cmd::V`; **ignored on noise** | Yes on PU/WAV; **Engine** on noise | **939** |
| `W` | PU: duty from the **low two bits**, the rest ignored. WAV: `x` (if non-zero) and `y` (if non-zero) set two synth variables -- §6.18 | PU1 PU2 WAV | same | `Cmd::W`: `xy & 3` duty (right), wave slot on WAV | Yes for duty; **No** on WAV | **939✗** |
| `Z` | Re-runs the last command **in its own lane** -- a phrase `Z` the channel's last phrase command, a table `Z` that table column's last -- adding random `0..x` to the target byte's high nibble and `0..y` to its low | all | same | re-runs the **other slot/column**, adding to its `a`/`b` fields | **Engine** -- §6.19 | **939✗** |

One row is not fully settled on 9.3.9: phrase `HFF`, whose *destination* is measured here but
whose 8.4.4 "fifteen times then plays in full" reading (§56) is not reproduced. `R` and `V`,
previously carried over from 9.2.J, are now measured on 9.3.9.

---

## 3. The method: how any of this gets measured

Nothing here is guessed from audio. The technique is to log **every APU register write with
its CPU cycle** from the real ROM, do the same from ChipBoy, and diff the two streams. Two
streams that agree byte for byte sound the same; two that differ tell you exactly where.

Assets live **outside the repository** (rule L3): ROMs and saves at `/root/lsdj/` in this
container. No LSDj content is committed -- but what the ROM *does*, and the address that does
it, is exactly what this document is for.

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
(the software-envelope hold), one period write and one trigger; two notes eight rows apart at
128 BPM should be 0.9375 s apart; and `S23` on PU1 should write `NR10 = ED`. All three were
checked before any measurement here was taken, and again in the stage-1 verification pass (§9).

**Anchor on LSDj's playback reset, never on a frame count.** `run.playStart()` finds the
`NR52 = 00` → `NR52 = 80` power-cycle LSDj does when `START` starts the song; that is the only
`NR52 = 80` after the boot ROM's chime. Counting frames is not equivalent — a frame is not
70224 cycles while the LCD is off through LSDj's boot — and the version of `events()` that did
so anchored a whole phrase late. See §9.

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

**Disassembling the ROM is cleared and expected** (rule L3, `CHIPBOY_SPEC.md` §3.3: no LSDj
content is bundled, but behaviour may be derived from the ROM; what ships is ChipBoy's own code
producing the *same result*, and the address that answered a question is worth recording).
Register-stream diffing is still usually faster to reach for first. Reach for the ROM when a constant is *measurably* ambiguous -- the
standing example was `LSDJ_PARITY.md` §7, where envelope rates 6 and 7 measured as the same
interval; the ROM's eight-byte rate table (§6.5) ended that argument in one read.

The stage-1 verification pass leaned on it much harder, and it is worth saying why: **four of
the eight corrections in §9 were invisible to any reasonable sweep of values.** `S` looks
absolute until you run two of them; `M` looks like a plain `NR50` write until a nibble goes
above 7; `T` looks like BPM until the byte drops below 40; `Z` looks like "the last command"
until the two lanes disagree. In each case five instructions said plainly what a sweep of values
said ambiguously. Two tools make this cheap and **both are in the tree**:
`tools/lsdjref/lsdjref_dis.py`, a fifty-line SM83 disassembler, and `lsdjref_pc`, the trace tool
plus the PC and ROM bank of every write — `--watch` extends it to a work-RAM range, which turns
"what wrote that register" into a one-line answer and names the handler directly.
`tools/lsdjref/README.md` has the loop, and a table of where every handler on a 9.x ROM lives.

The command jump table is at bank 02:`$47A2`, twenty little-endian words indexed by the letter's
code (`-ABCDEFGHKLMOPRSTVWZ`), dispatched from `$478D`. `B`, `D`, `G`, `H` and `Z` have no entry:
they are handled where the row is read, not where a command is run.

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

Two letters mean different things in a phrase and in a table -- `H` used to be listed here as a
third and is not one (§6.8: the counted-hop rule is the same in both places; only the range of
the destination differs).

- **`B`** -- phrase: the probability the note plays, `n/15` per nibble. Table: a hop that only
  happens sometimes, `x/16`. **Different laws, not just different meanings** (§6.2).
- **`G`** -- phrase: sets the groove from that row on. Table: sets the row lengths of *that
  table run*; and before 9.x the row carrying the `G` takes the groove's **first step** as its
  own length rather than walking the groove (§63). ChipBoy's importer flattens such a `G` into
  a one-step groove in a spare slot, ranking free slots so it never overwrites a groove the
  song still names.
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
**Re-measured (939✓).** Confirmed exactly, and one thing added: **`A00` is not "stop"** -- it
selects table 00. With the instrument naming table 04 (row 1 = +12) and a table 00 whose row 1
is +7, `A00` walks +7 while `A20` holds still. ChipBoy's convention is the opposite way round
(slot 0 means "no table"), which the importer's `xy + 1` already absorbs.
**ChipBoy:** `Cmd::A` with slot `xy + 1` (ChipBoy's slots are 1-based, 0 = none).
**Mapping — Yes.** The importer prefers the cell's *Table column* when the `A` sits in a phrase
cell, since ChipBoy has a dedicated column for it. `A20` cannot be expressed in that column and
is dropped with a note; as a command it becomes `A 0`.

### 6.2 `B` -- chance *(not implemented)*

**LSDj 9.3.9 (939✗).** The direction of the effect was measured right and **the changelog's
examples are the wrong way round for this build** -- it describes `B00` as "always plays" and a
high value as "usually skips", where 9.3.9 does the opposite. But the *rate* was measured on too
small a sample, and the two forms turn out not to share a law.

*In a phrase* — the byte gates whether the note sounds. Each nibble is an independent roll and
the note sounds if **either** passes; the roll is **`n/15` exactly**. Re-measured over 2408
note-ons per value (a note on every phrase step at 255 BPM, groove `2 2`):

| `n` | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `B 0n` played | 0.000 | .065 | .133 | .198 | .264 | .339 | .403 | .472 | .535 | .602 | .672 | .735 | .806 | .863 | .927 | 1.000 |
| `n/15` | 0.000 | .067 | .133 | .200 | .267 | .333 | .400 | .467 | .533 | .600 | .667 | .733 | .800 | .867 | .933 | 1.000 |

and both nibbles obey it. The union holds to three figures: `B44` 0.4622 against a predicted
0.4622, `B88` 0.7799 against 0.7822, `BCC` 0.9635 against 0.9600 -- and `B84` and `B48` agree
with each other (0.6499, 0.6607), which "take the larger nibble" cannot explain.

**The ROM confirms it** (bank 02:`$5074`): the phrase roll takes a random byte and reduces it
by the general divide helper with a divisor of **15**, so the value it compares against a nibble
is uniform on 0-14 and a nibble `n` passes `n` times in 15. `B00` never plays; `B0F`, `BF0` and `BFF` always do.

*In a table* — a hop that only sometimes happens: **`x` is the chance and `y` the destination
row**, and here the chance is **`x/16`, not `x/15`**. With rows 0-3 stepping +0/+4/+8/+12,
rows 4-15 at +20 and the `B` on row 3, counting every arrival at row 3:

| `x` | 0 | 1 | 2 | 4 | 8 | 12 | 14 | **15** |
|---|---|---|---|---|---|---|---|---|
| hopped | 0.000 | .063 | .125 | .248 | .470 | .732 | .862 | **.928** |
| `x/16` | 0.000 | .063 | .125 | .250 | .500 | .750 | .875 | **.938** |
| `x/15` | 0.000 | .067 | .133 | .267 | .533 | .800 | .933 | **1.000** |

**The ROM confirms this too** (bank 02:`$732C`): the table hop compares an **unreduced** random
byte against `x << 4` and hops only when the random byte is the smaller -- a flat `x/16`. Two
different random paths, in the same letter.

> **Superseded.** This entry used to say the table roll was the same "about `n/15`" as the phrase
> roll and that "`BF0` gives a three-row cycle (it always hops to row 0)". `BF0` misses one hop
> in sixteen: 35 fall-throughs in 483 arrivals. Over the handful of rows the first campaign
> looked at, one miss in sixteen is invisible.

*The hop replaces the row.* An always-hop on row 3 gives a **three**-row cycle, not four: row 3's
transpose column never reaches the channel. The same is true of a table `H` (§6.8).

**ChipBoy:** nothing. The letter is not in `bank::Cmd`.
**Mapping — No.** The importer drops it with a note naming the cell. To close it: add `B` to
`Cmd`, to the parameter table and to the driver -- the table form is an `H` behind a
probability gate, the phrase form a gate on the note-on. Being random it cannot be flattened
into anything deterministic, so this is engine work, not a mapping.

### 6.3 `C` -- chord

**LSDj 9.3.9 (939✓):** arpeggiates the note with `note`, `note + x`, `note + y`, one step per
tick. Re-measured on PU1: `C37` from note `18` walks MIDI 59 → 62 → 66 → 59 …, one step every
19.5 ms = exactly one tick at 128 BPM. On noise, re-measured from note `1F` (`NR43 = 65`):
`C37` walks `65 → 63 → 53 → 65 …`.
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

**LSDj 9.3.9 (939✓):** delays the note by exactly `xy` ticks. Re-measured against the song's own
undelayed note-on rather than against the start key: `D01` `D02` `D03` `D06` `D0C` `D18` come
out at 1.00, 2.00, 3.00, 6.00, 12.00 and 24.00 ticks. Exact.
**ChipBoy:** `Cmd::D`; a slot's `D` is read at the note-on rather than applied live.
**Mapping — Yes.**

### 6.5 `E` -- envelope

**LSDj 9.3.9 (939✗).** `x` is the level. `y` is **not** a rate: **bit 3 is the direction and bits
0-2 are the rate**. **`E` never triggers** from 8.8.0 on; before that it re-attacks (§59).

- **The level** walks to `x` with zombie steps. Measured with the `E` on a row after the note,
  from the instrument's level 15: `EC0` issues 3 down-steps, `E80` 7, `E40` 11 and `E00` 15 --
  exactly `15 - x` each time. With the `E` on the note's own row there are no steps at all,
  because the note-on writes the level directly.
- **The rate table is confirmed, and is the ROM's own.** It sits at bank 02:`$698C`, eight bytes
  indexed by `y & 7`:

  | `y & 7` | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
  |---|---|---|---|---|---|---|---|---|
  | period | **0** | 6 | 11 | 17 | 22 | 28 | 34 | 39 |

  That is exactly the 6, 11, 17, 22, 28, 34, 39 measured in the first campaign, so rates 6 and 7
  really are distinct and `LSDJ_PARITY.md` §7 (15, 20, 27, 36, 36, with 6 and 7 equal) is
  superseded outright. See §70.
- **The unit is a fixed 2.788 ms**, one LSDj timer interrupt, *not* the `y / 64` s the entry used
  to claim. Measured directly: `E81` steps every 16.73 ms — 6 units — and `E83` every 47.7 ms —
  17 units. The unit does **not** scale with tempo: at 64, 128, 192 and 255 BPM the row length
  changes fourfold and the `E` step interval stays at 16.71-16.74 ms.
- **Rate 0 means no envelope at all.** `E80` and `E88` produce no continuing steps: the ROM skips
  the whole software envelope when `table[y & 7] == 0`.
- **Bit 3 of `y` is the direction**, 0 down and 1 up, and it is the *only* thing that says which
  way the level moves — the level `x` is not a target it walks toward. From an instrument at
  level 8: `E89` climbs 9, 10, 11 … 15 and stops; `E81` falls, in the `09 11 18` down-triples.
  `E01` from level 15 zombie-steps straight to 0 and then holds, because down from 0 is nowhere.

**Channels:** on WAV/KIT the level is `NR32`'s two bits — and it reads **`y`, not `x`**. The ROM
(bank 02:`$46E8`) takes `value & 3` and negates it into `NR32`'s bits 6-5, so `y & 3` of
0/1/2/3 gives mute / 25% / 50% / 100%. Measured: `E01`→`NR32` code 3, `E02`→2, `E03`→1,
`E0F`→1, and `E10`, `E20`, `E30`, `EF0` all→0 whatever `x` is; `E31` is code 3 and `E13` code 1.

> **Superseded.** This entry used to say "on WAV/KIT ... `x` is clamped to 0-3 and `y` is
> meaningless". It is the other way round. **ChipBoy has copied the wrong half** — `Cmd::E` does
> `v.waveLevel = clamp(c.a, 0, 3)` — so every imported `E` on a wave channel takes the wrong
> nibble. The entry also used to describe `y` as a plain 4-bit rate; ChipBoy's driver already
> splits it correctly (`envRate = c.b & 7`, `envDir = c.b & 8`), so here the **document** was
> wrong and the code right. Anyone "fixing" the code to match the old entry would have broken it.
**ChipBoy:** `Cmd::E` sets `envVol`, `envRate` (`c.b & 7`) and `envDir` (`c.b & 8`) and takes a
shaped envelope over (`shapedTaken`), then steps the level itself in software — on either table,
chosen by `Instrument::envChipTiming`. It emits §26's zombie writes (`08` up, `09 11 18` down),
so a `NRx2` value of `08` in a ChipBoy trace is an *increment*, not "volume 0". The ROM's own
up/down split matches: up is a single `08`-style write, down the `09 11 18` triple.
**Mapping — Yes on the pulses and noise**, with `envChipTiming` and `envRetrig` set from the
model's `EnvelopeLaw`. **Engine on WAV**: one character, `c.a` → `c.b`, in the `wave` branch.

### 6.6 `F` -- finetune / frame

**LSDj 9.3.9 (939✓).** Re-measured on every channel; every number below was reproduced
exactly, including the whole six-note table. It is three different things:

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

**LSDj 9.3.9 (939✓):** selects groove `xy`, zero-based, and **walks** it. Re-measured with groove
1 set to `3 3`: eight rows take 0.93759 s under `G00` (groove 0, `6 6`) and 0.46881 s under
`G01` — the same figures to five places. Before 9.x the row carrying a table's `G` instead takes the groove's **first step** as
its own length (§63, measured on 8.4.4).
**ChipBoy:** `Cmd::G` with slot `xy + 1`. Inside a table it sets that run's row lengths.
**Mapping — Yes.** For pre-9 saves the importer flattens each such `G` into a one-step groove
of that length in a spare slot, ranking free slots (LSDj-empty first, then the `6 6` default,
then a groove written but never named) and taking the high slots first, so a groove the song
still shows is never overwritten. The slots it takes are named for the length they hold.

### 6.8 `H` -- hop

**LSDj 9.3.9 (939✓), and the open question is closed: the two places behave the same.**

- *In a table* — **hop to row `y`, `x` times**. Re-measured with rows 0-3 transposing
  +0/+4/+8/+12 and the `H` on row 3: `H00` cycles three rows for ever (it always hops), `H10`
  hops once and then lets row 3 through, `H20` hops twice, and `H01` settles into a two-row
  cycle on rows 1 and 2. So `x = 0` is "always" and otherwise `x` is a count.
- *In a phrase* — **`x` decides which of two different things it is.** Measured with a chain of
  **two** phrases, A then B, A's steps transposing 0-15 and B's 24-39, and the `H` on A's step 4.
  A single-phrase chain cannot tell these apart, because "end the phrase" and "hop to step 0"
  produce the same notes when the phrase that follows is the same one.

  | `H` | what plays | reading |
  |---|---|---|
  | `H00` | A 0-3, **B 0-15**, A 0-3, B … | end the phrase; the next starts at step 0 |
  | `H04` | A 0-3, **B 4-15**, A 0-3, B 4-15 … | … at step 4 |
  | `H0A` | A 0-3, **B 10-15**, A 0-3, B 10-15 … | … at step 10 |
  | `H0F` | A 0-3, **B 15**, A 0-3, B 15 … | … at step 15 |
  | `H10` | A 0-3, **A 0-15**, B 0-15 … | hop *inside* A to step 0, once, then let it run |
  | `H1A` | A 0-3, **A 10-15**, B 0-15 … | … to step 10, once |
  | `H2A` | A 0-3, **A 10-15**, B 0-15, A 0-3, A 10-15 … | … twice, one hop a pass |
  | `H14` | A 0-15, B 0-15 … | a hop to the `H`'s own step is a no-op |
  | `HF0` | A 0-3, A 0-3, A 0-3 … | `x = 15`: it hops every pass |
  | `HF1` | A 0-3, A 1-3, A 1-3 … | … to step 1 |
  | `HFF` | A 0-3, then **silence** | special-cased; see below |

  So **`H 0 y` ends the phrase and the next phrase in the chain starts at step `y`** -- a chain
  hop, which is what §56 called it on 8.4.4 -- while **`H x y` with `x > 0` hops within the
  phrase to step `y`, `x` times**, one hop a pass, then lets the phrase run through. The two
  forms are not the same command, and only the second resembles a table's `H`.

  `HFF` remains the exception: `x = 15, y = 15` should hop to step 15 fifteen times, and `H1F`
  does exactly that once, but `HFF` triggers nothing at all after the hop.

> **Superseded twice.** This entry originally read the phrase form as "jump to step `xy`" with
> the high nibble untested. The stage-1 pass then claimed the high nibble was a plain repeat
> count, "the same rule" as a table's -- measured on a **one-phrase chain**, which cannot
> separate "end the phrase" from "hop to step 0". With two phrases in the chain the two forms
> come apart at once. The lesson is the same one §3.7 draws: a probe that cannot distinguish two
> hypotheses will happily confirm whichever you had in mind.

**ChipBoy:** `Cmd::H` handles the counted table form on the lane it fired in (§64: it moves
that lane's pointer only). The phrase form sets `hopStep` and **ignores `x`**.
**Mapping — Yes for the table form; Engine for a counted phrase hop**, which ChipBoy cannot
express: `hopStep` fires every pass. The phrase form with `x = 0` is a jump within the phrase,
which ChipBoy does express; `HFF` is the exception (§56) and the importer notes it.

### 6.9 `K` -- kill

**LSDj 9.3.9 (939✓):** kills the note after exactly `xy` ticks -- re-measured at 1.00, 2.00,
3.00, 6.00 and 12.00 ticks for `K01`, `K02`, `K03`, `K06` and `K0C`. It takes the level to zero
with the same zombie steps as any other level change (fifteen down-triples from level 15) and
leaves the DAC on. **`K00` kills at once**, on the note's own tick -- the ROM branches on a zero
byte before anything else (bank 02:`$65FB`).
**ChipBoy:** `Cmd::K`, same, via `killLevel()`.
**Mapping — Yes.**

### 6.10 `L` -- slide

**LSDj (939✓ on the update count; M8 §68 §71 for the two properties below):** slides to the note
over **`xy + 1` pitch updates**, linear in semitones, by a fixed step `(target - source) / (xy + 1)` in 1/256 semitones truncated toward
zero, landing on the note one update after the last step. `L00` is instant.

Two properties measured on 8.4.4's wave kick (§71) that are easy to get wrong:

- The **target is latched**. A table steps every tick, so a slide outlives the row that aimed
  it; a later row's transpose column must not drag the target.
- The **target is clamped to a note the channel can sound** *before* the step is divided out.
  The wave channel bottoms at note 24 (period 44); the pulses at note 36. A table transpose
  naming something lower is clamped, so the rate comes out right because the destination does.

Re-measured on 9.3.9, note `18` (period 1517) to note `24` (period 1783): `L00` is instant,
`L01` passes through 1 intermediate period, `L03` through 3, `L07` through 7 and `L0F` through
15 -- so `xy + 1` steps, landing on the note. Exact.
**Channels:** all. On noise ChipBoy ignores `L`.
**ChipBoy:** `Cmd::L`, both properties implemented (§71).
**Mapping — Yes** on 9.x. On formats 0-3 `L` is a *speed in register units per clock*, so the
importer converts: `updates = ceil(|period(target) - period(from)| / xy)`, emitted as
`L (updates - 1)`. It needs a note before it in the chain to measure from, and says so when
there is not.

### 6.11 `M` -- master volume

**LSDj 9.3.9 (939✗).** The byte does **not** go straight into `NR50`. Each nibble is looked up in
a 128-entry table indexed by *the volume that side already holds*, so half the values are
**relative**. The ROM (bank 02:`$6246`) does the left side as
`NR50 = swap(tbl[(NR50 & 0x70) + x])` and then the right as `NR50 += tbl[(right << 4) + y]`.

Measured, sweeping one nibble from two different starting volumes (`NR50 = 77` and `NR50 = 73`,
i.e. that side at 7 and at 3), the same map for both nibbles:

| nibble `n` | 0-7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|
| from 7 | 0-7 | 7 | 7 | 7 | 7 | 3 | 4 | 5 | 6 |
| from 3 | 0-7 | 3 | 4 | 5 | 6 | 0 | 0 | 1 | 2 |
| **means** | **set to `n`** | +0 | +1 | +2 | +3 | **−4** | **−3** | **−2** | **−1** |

clamped to 0-7 at both ends. So `M0F` from 7 gives `06`, not `0F`; `MF0` gives `60`; `MFF` gives
`66`; `M80` gives `70`.

> **Superseded.** This entry used to say the byte goes straight into `NR50` and mark the row
> **Yes**. The three values it was measured on -- `M40`, `M07`, `M73` -- have every nibble ≤ 7,
> which is exactly the half of the range where "straight into `NR50`" is true.

**ChipBoy:** `Cmd::M` is **already relative** -- `masterFromArg` in `Driver.cpp` -- so the entry
misdescribed ChipBoy too. But its down half is wrong: it maps 12-15 to 0, −1, −2, −3 where LSDj
maps them to −4, −3, −2, −1. Only `n = 14` agrees by accident.
**Mapping — Engine.** `masterFromArg`'s `n >= 13` branch becomes `n >= 12` returning
`max(0, cur - (16 - n))`. Three lines, and the 8-11 half is already right.

### 6.12 `O` -- output / pan

**LSDj 9.3.9 (939✓):** `xy` in 0-3 selects the channel's `NR51` bits: re-measured on PU1 against
a resting `NR51` of `FF`, `O00` gives `EE` (both bits clear), `O01` `FE` (left only), `O02` `EF`
(right only), `O03` `FF` (both) -- which is exactly ChipBoy's `Pan { Off, Left, Right, Both }`
order. `O04` repeats `O00` and `O07` repeats `O03`, so the byte really is masked to `& 3`.
**ChipBoy:** `Cmd::O`, `Pan(xy & 3)`.
**Mapping — Yes.**

### 6.13 `P` -- pitch bend

**LSDj (939✓ on the sign and the drift rate; M8 §66 for the noise split):** `xy` read as a
**two's-complement** signed byte. The step per update comes from a measured table. `P 0` stops a
bend and keeps what it reached; a plain note-on puts the offset back.

Re-measured on PU1 from note `18` (period 1517), period change per pitch clock: `P01` about
+0.2, `P02` +0.33, `P04` +0.5, `P08` +1.4, `P10` +4.7 -- and the negatives mirror them, `PFF`
−0.2, `PFE` −0.33, `PF0` −4.9. Two's complement confirmed, and the rate is strongly non-linear
in the byte, as `bendStep256` has it.
**Channels:** on noise, `P` walks either the note map or the `NR43` nibbles, depending on the
instrument's Sweep mode (§66).
**ChipBoy:** `Cmd::P`, with four speed laws — Fast and Tick bend the note; Step applies one
offset of `x/32` of a semitone and no continuous bend; Drum bends the **period register** and
wraps at 2048, which is what a `P` kick falling off the bottom really does (§5).
**Mapping — Yes** on 9.x; **Value** on formats 0-3, where `P` is register units per clock and
the importer picks the nearest Drum speed (§56).

### 6.14 `R` -- retrigger

**LSDj 9.3.9 (939✓):** retriggers every **`y` ticks**. Re-measured on PU1 with the instrument's
command rate at 0: `R01` gives 1.00 trigger-interval in ticks, `R02` 2.00, `R04` 4.00.
`y = 0` retriggers **once** and stops (two triggers in all, the note-on and one more) -- the
changelog dates that to v8.8.1, restoring 4.7.3's behaviour. `x = 8` runs the retrigger on a
faster clock: `R81` measures 0.29 ticks and `R84` 0.71. Other values of `x` leave the interval
alone -- `R11` and `R41` are indistinguishable from `R01` -- and are a signed volume step.
**ChipBoy:** `Cmd::R` with the interval `y * (cmdRate + 1) + 1` ticks, so at command rate 0 it
retriggers every `y + 1` ticks and treats `y = 0` as *every tick*.
**Mapping — Engine.** Both halves are off by one idea: the interval is one tick too long, and
`y = 0` should fire once rather than continuously. Small fix, audible on any drum table.

### 6.15 `S` -- sweep / shape

**LSDj 9.3.9 (939✗ — the formula is right, but it is not an assignment).**

- **PU1** — writes `NR10`, but **not** as the byte, and **not absolutely**. The handler
  (bank 02:`$4828`) is a dozen instructions and what they do is this. The channel keeps a sweep
  byte, held **inverted**, seeded at every note-on from the instrument's own sweep field — which
  the save also stores inverted, so a sweep of `00` seeds it as `FF`. `S xy` **adds `x` to that
  byte's high nibble and `y` to its low**, the low nibble masked to four bits so it never borrows
  into the high one, and then falls into the note refresh, which writes **`NR10 = ~byte`**
  (bank 02:`$604B`). Because a fresh note on a sweep-`00` instrument starts that byte at `FF`, the *first* `S`
  after such a note gives exactly the published formula:

  `NR10 = ((-x) & 15) << 4 | ((-y) & 15)`

  Re-measured on **sixteen** values, including the `y = 0` cases the first campaign never tried
  (they are the only ones that distinguish this from a whole-byte negation, and every one of
  them agrees with the per-nibble form):

  | `S` | `00` | `10` | `20` | `30` | `70` | `F0` | `01` | `0F` | `11` | `12` | `23` | `2B` | `34` | `71` | `88` | `FF` |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
  | `NR10` | `00` | `F0` | `E0` | `D0` | `90` | `10` | `0F` | `01` | `FF` | `FE` | `ED` | `E5` | `DC` | `9F` | `88` | `11` |

  **But two `S`s compound.** `S23` on four consecutive rows of one note gives
  `ED → CA → A7 → 84`, not `ED` four times. And **the instrument's own sweep byte is the
  starting point**: the same `S23` on an instrument whose sweep is `11` writes `FE`, not `ED`.
  So `S00` is not "sweep off" — it is "add nothing", which only *looks* like off because the
  usual starting point is `00`.
- **NOI** — the byte is **semitones** through the noise map, accumulating until the next
  note-on. Re-measured from note `1F` (`NR43 = 65`): `S01` gives `90`, `S02` `57`, `SFF` `73`,
  `SFE` `67` — map lookups, not arithmetic on the register — and four `S01`s in a row walk
  `90 → 90 → 57 → 63 → 55`, confirming the accumulation.
- **PU2 and WAV** — inert. Re-measured by diffing the whole APU write stream for 0.3 s with and
  without `S23`: **byte for byte identical** on both. (The ROM does `ret` immediately for PU2;
  for WAV it calls into the synth code, which touched no register in this probe.)

> **Superseded.** This entry used to present PU1's `S` as an assignment,
> "`NR10 = ((-x) & 15) << 4 | ((-y) & 15)`", and to say the fix was "one line in `applyCommand`".
> The formula holds for the first `S` of a note on a sweep-`00` instrument, which is what the
> eight probe values all were. It is an accumulate.

**ChipBoy:** `Cmd::S` sets `sweepRate = x & 7`, `sweepDown = y & 8`, `sweepShift = y & 7` —
`NR10 = 23` for `S23`, where the ROM writes `ED`. On noise it branches on
`Instrument::noiseDomain` for the semitones-vs-nibbles split, which is right, and it already
accumulates (`noiseTsp +=`).
**Mapping — Engine on PU1, and larger than it looked.** The channel needs a *running* sweep
byte, seeded from the instrument at every note-on, that `S` adds into nibble-wise (the low
nibble mod 16 with no borrow, the high nibble as a plain byte add) and whose complement goes to
`NR10`. Setting `sweepRate`/`sweepShift` from a single command cannot express it.

### 6.16 `T` -- tempo

**LSDj 9.3.9 (939✗ for the low bytes; the rest confirmed).** The byte is the tempo in BPM --
**for bytes 40 and above**. Below that it is not: bytes 0-39 mean **256-295 BPM**.

Re-measured over eight rows taken *clear of the row the `T` sits on* (notes on steps 4 and 12,
`T` on step 0 — measuring across the `T`'s own row mixes the two tempos and was what made the
first pass read `TC0` as 189 BPM):

| `T` | `01` | `10` | `20` | `27` | `28` | `30` | `40` | `80` | `C0` | `FF` |
|---|---|---|---|---|---|---|---|---|---|---|
| byte | 1 | 16 | 32 | 39 | 40 | 48 | 64 | 128 | 192 | 255 |
| BPM | 257.5 | 272.2 | 288.6 | 294.5 | 40.0 | 48.0 | 64.0 | 128.0 | 192.0 | 254.5 |
| = | 256+1 | 256+16 | 256+32 | 256+39 | 40 | 48 | 64 | 128 | 192 | 255 |

so 40 is the hinge, and `T27` (294.5) and `T28` (40.0) are adjacent bytes an octave and a half
apart in tempo.

> **Superseded.** This entry used to say "the byte is the tempo in BPM" with no lower bound. The
> three values it was measured on -- `T40`, `T80`, `TC0` -- are all above the hinge.

**ChipBoy:** `Cmd::T`; the Player and the Clock own it, so `applyCommand` does nothing.
**Mapping — Yes for bytes 40-255; Value for 0-39**, where the importer must emit `256 + xy`
BPM. ChipBoy's own tempo range has to reach 295 for that, or the importer notes the clamp.

### 6.17 `V` -- vibrato

**LSDj 9.3.9 (939✓, now measured on the pulses too):** `x` speed, `y` depth. One cycle is
`64 / (x + 1)` pitch updates, so `x = 0` is the **slowest**, not "off". Re-measured on PU1 at
all eight speeds, timing trough to trough:

| `x` | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| measured, pitch clocks | 64.07 | 32.03 | 21.37 | 16.02 | 12.83 | 10.67 | 9.16 | 8.01 |
| `64 / (x + 1)` | 64 | 32 | 21.33 | 16 | 12.80 | 10.67 | 9.14 | 8 |

The swing is symmetric about the note: from note `18` (period 1517), `V x 4` runs 1494-1540 at
every speed, i.e. ±23 either side. Depth scales it — `V02` gives 1505-1528, `V08` 1435-1589.
The direction bit only says which half comes first.
**Channels:** pulses and wave — **and noise, which works.** Re-measured: a noise note holding
`NR43 = 65` writes that one value and nothing else, and the same note with `V 4 8` moves `NR43`
continuously (`65 67 75 B0 93 87 A3 D0 A7 D1 B3 97 95 87 …`), so the vibrato drives the LFSR
clock through the map. ChipBoy ignores `V` on noise.
**ChipBoy:** `Cmd::V`, with the measured tick table for Tick mode, and `if (noise) break;`.
ChipBoy's saw and square vibrato shapes are its own — the ROM's could not be read off the
register log (`LSDJ_PARITY.md` §13).
**Mapping — Yes on the pulses and wave; Engine on noise**, the same gap as `C`.

### 6.18 `W` -- duty / wave

**LSDj 9.3.9 (939✗ on the pulses' mask; the wave side is now read off the ROM).**

On a pulse it is the **low two bits**, not the low nibble — the ROM (bank 02:`$47D2` for PU2,
`$47E5` for PU1) does `and $03`. Re-measured, reading `NR11`'s duty field:

| `W` | `00` | `01` | `02` | `03` | `04` | `07` | `0C` | `0F` | `F1` | `F3` |
|---|---|---|---|---|---|---|---|---|---|---|
| duty | 0 | 1 | 2 | 3 | **0** | **3** | **0** | **3** | 1 | 3 |

`W04` is `W00` and `W07` is `W03`, which "the low nibble" does not predict.

On the **wave** channel `W` is **not** a no-op, which is what the first campaign concluded from
seeing no register change. The handler (bank 02:`$47F8`) reads the two nibbles separately and
writes each to a synth parameter in work RAM: `x`, **if it is non-zero**, is stored as `x - 1`
(to two addresses, one of them the value the wave engine reads per tick); `y`, **if it is
non-zero**, is stored as itself somewhere else. Neither register is touched, and a zero nibble
leaves its parameter alone rather than setting it to zero. The three probe values
the first campaign used -- `W00`, `W02`, `W0F` -- all have `x = 0`, so only the second variable
ever moved, and a synth-0 instrument does not animate, so nothing reached wave RAM. Re-measured
across `W00 W01 W0F W10 W21 WF1 W1F` on synth-0 and synth-1 instruments: wave-RAM writes stay at
16 per 0.5 s in every case, so the two variables still have not been shown *doing* anything.
**What they are remains `?`** — it needs a save with real synth data.

> **Superseded.** "the **low nibble** is the duty" (it is the low two bits), and "on the wave
> channel **nothing was observed** ... consistent with `W` there setting a synth's speed or
> length": the guess was right, and the ROM now names two variables rather than one.

**ChipBoy:** `Cmd::W`: `duty = xy & 3` on the pulses -- **which is correct**, the entry was
wrong about ChipBoy as well -- and wave slot on WAV.
**Mapping — Yes for the duty**, and the importer should keep `xy & 3`, not the low digit. `W` on
a *wave instrument* is dropped with a note and stays dropped: it addresses a synth engine
ChipBoy does not have.

### 6.19 `Z` -- randomise

**LSDj 9.3.9 (939✗ on the source; 939✓ on the arithmetic).** Two things, and ChipBoy has both
wrong -- but the first is not wrong in the way this entry used to say.

1. **`Z` re-runs the last command in *its own lane*, not the last command executed.** LSDj keeps
   a "last command" record -- a letter at `$C371 + i` and a value at `$C3B5 + i` in work RAM --
   and the index
   `i` is the *lane*, not a single most-recent slot: `0-3` are the four channels' phrase
   commands, `4 + t` is table `t`'s column 1 and `36 + t` its column 2. `Z` copies that lane's
   letter and value into its own slot and lets the dispatcher run them (bank 02:`$73F3` for a
   phrase, `$7402` and `$7424` for the two table columns). Re-measured with `M 40` as the target,
   which shows up directly in `NR50`:

   | probe | `NR50` values seen | verdict |
   |---|---|---|
   | phrase: `M40` step 0, `Z0F` step 1 | `40 41 42 43 44 45 46 47` | re-runs `M` |
   | table: `M40` col 1 **and** `Z0F` col 2, same row | `40` only | does **not** |
   | table: `M40` col 1 row 0, `Z0F` col 1 row 1 | `40 41 … 47` | re-runs `M` |
   | table: `M40` col **2** row 0, `Z0F` col **1** row 1 | `40` only | does **not** |

   The last row is the one that settles it: the `M` ran a whole row *earlier in time*, and the
   `Z` still did not see it, because it was in the other column's lane.

   Two commands are never recorded and so are never what a `Z` re-runs: **`H`** (the ROM does
   `cp $08; ret z` before the record) and **`Z`** itself.

2. **Each digit adds a random `0..that digit` to the matching nibble of the target's byte** --
   confirmed, and now from the code as well. The ROM computes
   `result = lastValue + (rand(0..x) << 4) + rand(0..y)` as a **plain byte add**, so a carry out
   of the low nibble does reach the high one, and `rand(0..n)` is `rand8() mod (n + 1)`
   (bank 02:`$6479`). Measured against `M40`: `Z01` gives `40 41`, `Z10` gives `40 50`, `Z33`
   gives every combination of `40 50 60 70` with `0 1 2 3`, and `Z00` gives `40` alone. That is
   the changelog's own description -- `Z20` adds one of `0x00`, `0x10`, `0x20` -- and here the
   changelog and the ROM agree.

> **Superseded.** This entry used to say "`Z` re-runs the last command **executed**". It re-runs
> the last command *in the same lane*. The probe it was drawn from -- one column against the
> other on the same row -- cannot tell those apart, because on the same row the other column has
> not run yet either way.

**ChipBoy:** `Cmd::Z` re-runs the last command that is not `Z` or `H`, preferring **the other
slot or column** and falling back to the channel's last; it adds `rand(0..x)` to the target's
`a` field and `rand(0..y)` to its `b`.
**Mapping — Engine, on both counts.**

- The *source* is wrong: keep a last-command record **per lane** -- one per channel for phrase
  commands, one per (table, column) -- and re-run that lane's, never another's. ChipBoy already
  excludes `Z` and `H` from the record, which matches.
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
| `A` `H` | any | which row's values appear, and when; for `H`, the high nibble in a **phrase** as well as a table |
| `B` | `NRx4` trigger (phrase), `NRx3/4` (table) | the played fraction over ~2000 notes, against `n/15`; the hopped fraction against `x/16` |
| `C` | `NRx3`/`NRx4` | the note sequence and the tick spacing |
| `D` `K` | `NRx4` trigger, `NRx2` | how many ticks before the trigger / the kill |
| `E` | `NRx2` | **the low nibble**: 8 means software, anything else means the chip runs it (§70). Also the eight-byte rate table and whether bit 3 of `y` is still the direction |
| `F` | `FF30-FF3F` on WAV, `NRx3/4` on PU2 | which frame is loaded / the period offset |
| `G` `T` | any | the tick spacing of the rows, measured **clear of the row the command sits on**; for `T`, a byte below 40 as well as above (§6.16) |
| `L` `P` | `NRx3`/`NRx4` | the period per pitch clock; check whether it is geometric (semitones) or linear (register units) |
| `M` | `FF24` | the two nibbles at values **above 7**, from two different starting volumes -- that is where the relative half lives (§6.11) |
| `O` | `FF25` | the channel's two bits |
| `R` | `NRx4` trigger, `NRx2` | the retrigger interval and the volume step |
| `S` | `FF10` on PU1, `FF22` on NOI | the sweep byte / the `NR43` delta, nibble by nibble -- and **two `S`s in a row**, to see whether this version accumulates (§6.15) |
| `V` | `NRx3`/`NRx4` | the swing: symmetric about the note or one-sided below it, and the period |
| `W` | `NRx1` duty bits, `FF30-FF3F` | the duty at `04` and `07` as well as `00`-`03`, to see the mask / the wave loaded |
| `Z` | whichever the target command moves | whether the random lands on the byte's nibbles or on two fields, and whether the source is the **same lane** or something wider (§6.19) |

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

Every command has been traced on 9.3.9 twice: once in the first campaign, and once in an
independent **stage-1 verification pass** that rebuilt the rig from the ROM alone
(`docs/plan-lsdj-version-sweep.md`), re-derived each number without looking at the old one
first, and read the ROM's own code wherever the two disagreed. Of the nineteen entries,
**twelve came back unchanged and seven did not.** Three of the twelve gained something the
first campaign had not looked for, marked **+** below; one of those closes a standing open
question.

| Cmd | Verdict | What the re-measurement found |
|---|---|---|
| `A` | **confirmed** + | Table select and `A20` reproduced. Added: `A00` selects **table 00**, it is not a second "stop". |
| `B` | **corrected** | Phrase roll is `n/15` (2408 samples per value, and the ROM's `rand8() mod 15`). **Table hop is `x/16`** — a different law — so `BF0` misses one hop in sixteen, where the entry claimed it always hops. |
| `C` | **confirmed** | Chord cycle and tick spacing on PU1; `NR43` walking the map on noise. |
| `D` | **confirmed** | 1, 2, 3, 6, 12, 24 ticks for `D01`-`D18`, exact. |
| `E` | **corrected** | Rate table **confirmed from the ROM itself** (bank 02:`$698C` = 0, 6, 11, 17, 22, 28, 34, 39) — `LSDJ_PARITY.md` §7 stays superseded. But `y` is **bit 3 = direction, bits 0-2 = rate**, not a 4-bit rate; the unit is a fixed 2.788 ms, not `y/64` s, and does not scale with tempo; and **WAV reads `y`, not `x`**. |
| `F` | **confirmed** | Both nibble sweeps and the whole six-note `F0F` table reproduced exactly. |
| `G` | **confirmed** | 0.93759 s and 0.46881 s under grooves `6 6` and `3 3`. |
| `H` | **corrected** | Table form reproduced. The **phrase** form is two commands, not one: `H 0 y` ends the phrase and the next in the chain starts at step `y`; `H x y` with `x > 0` hops *within* the phrase to step `y`, `x` times. Measured with two phrases in the chain -- a one-phrase chain, which is what the stage-1 pass used, cannot separate them. |
| `K` | **confirmed** + | 1, 2, 3, 6, 12 ticks. Added: `K00` kills on the note's own tick. |
| `L` | **confirmed** | `xy + 1` pitch updates at four durations, exact. |
| `M` | **corrected** | **Not the byte into `NR50`.** Each nibble: 0-7 sets that side, 8-15 shifts it by 0 +1 +2 +3 −4 −3 −2 −1, clamped. The old entry's three probe values all had nibbles ≤ 7. ChipBoy's `masterFromArg` is already relative but its down half is wrong. |
| `O` | **confirmed** | All four values, plus `O04`/`O07` showing the `& 3`. |
| `P` | **confirmed** | Two's complement and the non-linear drift rate at eight values. |
| `R` | **confirmed** | Interval `y` ticks at three values, `y = 0` firing once, `x = 8`'s fast clock at 0.29 ticks. Was 92J; now 939. |
| `S` | **corrected** | The per-nibble formula is right — and now checked at the **`y = 0`** values that distinguish it from a whole-byte negation, which the first campaign never tried. But `S` **accumulates** onto the instrument's own sweep byte (ROM bank 02:`$4828`); `S23` four times gives `ED CA A7 84`, and on a sweep-`11` instrument it gives `FE`. Noise accumulates too (confirmed). PU2 and WAV inert, confirmed by a byte-for-byte stream diff. |
| `T` | **corrected** | The byte in BPM only for **40-255**; bytes **0-39 mean 256-295 BPM**. The old entry's three values were all above the hinge. Its `TC0 = 192` also only reads as 189 if you measure across the `T`'s own row. |
| `V` | **confirmed** | Cycle `64 / (x + 1)` pitch clocks at all eight speeds, symmetric swing, `NR43` moving on noise. Was 92J on the pulses; now 939. |
| `W` | **corrected** | Duty is the low **two bits**, not the low nibble (`W04` = `W00`, `W07` = `W03`). On WAV it is **not** a no-op: the ROM sets two synth variables from `x - 1` and `y`, each skipped when its nibble is zero — which is why three probes that all had `x = 0` saw nothing. |
| `Z` | **corrected** | The arithmetic is right. The **source** is not "the last command executed" but the last command **in the same lane** — per channel for phrase commands, per (table, column) for table ones. A command that ran a row earlier in the other column is not re-run. |

**Still open**, and both are named where they belong:

1. **What `W`'s two variables do on a wave instrument** (§6.18). The ROM says `W` writes
   `x - 1` and `y` to two synth parameters; nothing was made to move by them, because the
   bootstrapped host has no synth data worth animating. It needs a real save.
2. **`HFF`'s exact semantics** (§6.8). By the rule the rest of the range follows it should hop
   to step 15 fifteen times, and `H1F` does exactly that once — but `HFF` triggers nothing at
   all after the hop, on a two-phrase chain as on a one-phrase one. 8.4.4 was measured as ending
   the phrase fifteen times and then letting it play in full (§56). Not settled.

### What the verification pass changed about the rig

**The "START blip" does not exist.** The plan and this document both warned that LSDj plays a
short note about 1.8 s before the song's first note. It does not. Traced with the PC and ROM
bank of every write, the only triggers in a 200-frame run are two at 0.31 s and 0.40 s from
**`pc=00D1` in the boot ROM** — the Game Boy's own power-on chime, before LSDj runs at all — and
then the song's first note from LSDj's driver in bank 02. Between the `START` key and that first
note LSDj (bank 02) does exactly five things, none of them a note:

```
NR52 = 00      ; APU off -- every APU register cleared   (02:7C1F)
DIV  = 00      ; re-base the divider                     (02:5FDB)
NR52 = 80      ; APU on                                  (02:5FDF)
NR51 = 11, 33, 77, FF                                    (02:61F4, four times)
NR50 = 77                                                (02:6003)
```

The `NR51` ramp is one read-modify-write per channel from a four-iteration init loop, over
0.35 ms, with every DAC still off from the power-cycle — audible on hardware only as the click
of the `NR52` cycle itself. **The 1.8 s was an anchoring bug.** `run.py`'s `events()` skipped
`180 * 70224` cycles to get past the key press, but a frame is not 70224 cycles here (the LCD is
off through LSDj's boot), so the skip landed 36 ms *after* the key and past the song's own first
note — anchoring on the second pass of a looping phrase. The note one phrase-length earlier
(1.875 s at 128 BPM with a 16-row phrase — which is where the "about 1.8 s" came from) then
looked like a blip. `run.py` now anchors on LSDj's playback reset, the one `NR52 = 80` after the
boot chime, and `at_start()` gives times measured from it.

**Rig validation, run before any of the above was trusted.** All three checks pass on a host
save the ROM formatted itself (`--init-sav`, 3000 frames):

1. A plain note on PU1 is one `NR12 = F8`, one period write and one trigger — plus `NR10 = 00`,
   the duty, and a same-tick repeat of the period from the pitch-update path.
2. Two notes eight rows apart at 128 BPM with groove `6 6` are **0.937593 s** apart (want
   0.9375).
3. `S23` on PU1 writes `NR10 = ED`, preceded by the `NR10 = 00` the note-on clears it with.

The earlier check that the working-area path matches booting the save as a file (12549 of 12551
register writes in order) still stands.

## 10. What is still open in ChipBoy

**Most of this list is now done.** Sections 72-84 of `COMMANDS_AND_TEMPO.md` carry the design and
the code is in: `S` accumulates onto a running sweep byte, `B` exists in both its forms, `Z`
re-runs its own lane, `M`'s down half is right, `R`'s interval is `y` ticks with `y = 0` firing
once, `C` and `V` reach the noise channel, `F` is a finetune on PU1 and two nibbles on PU2, `E`
on WAV reads `y`, an imported noise instrument plays LSDj's own 120-entry table with its index
**wrapping** as the ROM's does, and a note-on triggers at the plain note with the table's
transpose following one update later. The importer converts `T`'s low bytes and masks `W` to two
bits.

What is left, in the order it costs a real song:

1. **The phase of a `P` bend at a note-on** (§6.13). The ROM's note-on writes a period already
   *half* a bend step in where ChipBoy writes the plain note; from the second update the two
   agree to within one period unit. It costs the first five milliseconds of every swept drum --
   27 of SUNRISE's 61 wave note-ons -- and nothing after that. Needs a measurement of its own,
   beside `LSDJ_PARITY.md` §5.
2. **A counted phrase `H`** (§6.8, §80). `H 0 y` -- end the phrase, next starts at step `y` -- is
   expressed for `y = 0` and noted otherwise. `H x y` with `x > 0` hops *within* the phrase, `x`
   passes running, and ChipBoy cannot: the Player lays a chain row's steps out ahead of the row,
   and a hop whose count survives across passes has no place in a schedule built once. Noted at
   import.
3. **Phrase `HFF`** (§6.8) triggers nothing after its hop on the ROM and is not understood.
4. **`W` on a wave instrument** addresses a synth engine ChipBoy does not have (§6.18); it stays
   dropped with a note.
5. **Kit `DIST` mixing** — narrowed to instrument byte 13's bit 6; needs a kit-stream decoder.
   61 of 69 kit instruments in the user's saves mix two kits.
6. **`LSDJ_PARITY.md` was measured on 9.2.J** with generated probe saves. §7 has been superseded
   twice over — by a real-save measurement (§70) and by the ROM's own rate table (§6.5) — and the
   rest has not been re-read for the same problem.

### Where the user's SUNRISE stands (LSDj 9.3.9, format 22)

The acceptance test for a format-22 import: the ROM playing the song, against ChipBoy playing
what the importer made of it, note-on for note-on over twenty-seven seconds, comparing the
period (or `NR43`) each trigger actually sounds at.

| | ROM | ChipBoy | longest common run | values differing |
|---|---|---|---|---|
| PU1 | 63 | 63 | **63** | **0** |
| PU2 | 53 | 53 | **53** | **0** |
| WAV | 61 | 61 | 7 | 28 |
| NOI | 70 | 70 | **70** | **0** |

**Three channels of four are exact** — every note-on, in order, at the byte or period the ROM
writes. The import prints one note, and it is about a PU2 transpose it kept rather than
anything it lost.

**What is left is the wave channel's swept drums**, and only their first update. The table's row
0 carries `P CF`, a downward bend; the ROM's note-on writes a period already part of the way
into it (1910 where the plain note is 1923) and ChipBoy's writes the plain note. From the
*second* update the two are identical to within one period unit, all the way down the sweep:

```
ROM   2016(trig)  1910  1899  1875  1851  1827  1802  1778  1753  1729
CB    1923(trig)        1899  1874  1850  1826  1802  1777  1753  1729
```

So 27 of 61 wave note-ons start about 1.3 semitones high for one pitch update -- roughly five
milliseconds at the attack of a drum -- and are right thereafter. The ROM's first value is
**half** a bend step below the plain note, not a whole one, so it is not simply "ChipBoy is one
update late": what sets the bend's phase at a note-on is not yet measured, and belongs with §6.13
and `LSDJ_PARITY.md` §5 rather than here.

### Where SPACE TI stands (the working song, LSDj 8.4.4)

Note-ons against the ROM over 23.8 s, longest common run: **PU1 67/103, PU2 104/153,
WAV 198/219, NOI 32/32.** PU1 and PU2 are the weak ones.
