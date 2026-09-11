# What each LSDj version means by a song

Every stable LSDj release in the user's archive, measured against 9.3.9 on its own ROM: what
differs, how the importer remaps it, and what cannot be remapped at all. `docs/LSDJ_COMMAND_MATRIX.md`
is the reference for what 9.3.9 itself does; this file is only the differences.

The model each row lands on is in `Source/core/Import/LsdjModel.cpp`. Two releases that write the
same format byte can still read a song differently — those are marked **ambiguous** and are the
cases where the importer needs the ROM's own version string to get it right.

## 1. The format byte, per release

Byte `0x7FFF` of a song, read from the save each ROM wrote for itself
(`lsdjref_trace --init-sav`, 4000 frames):

| format | releases |
|---|---|
| 0 | 3.1.5, 3.1.9, 3.4.4, 3.5.1 |
| 2 | 3.6.5, 3.6.8, 3.7.5, 3.8.7, 3.8.9, 3.9.2, 4.0.4, 4.1.0, 4.3.0 |
| 3 | 4.4.0, 4.5.4, 4.6.0, 4.6.2, 4.6.9, 4.7.3, 4.8.0, 4.9.4, 5.0.3 |
| 4 | 5.7.8, 5.8.8, 5.9.9, 6.0.1 |
| 5 | 6.4.5 |
| 7 | 6.8.2, 6.9.0, 7.0.2 |
| 8 | 7.2.3 |
| 9 | 7.5.4 |
| 10 | 7.9.9, 8.0.0 |
| 11 | 8.2.0, 8.4.4, 8.5.1 |
| 15 | 8.8.6 |
| 17 | 8.9.3, 8.9.5 |
| 18 | 9.0.0, 9.0.1 |
| 19 | 9.1.0 |
| 21 | 9.1.C |
| 22 | 9.2.L, 9.3.9, 9.4.2 |

Formats 1, 6, 12, 13, 14, 16 and 20 have no release in hand; everything else is measured on a ROM.
The importer sends a format with no model of its own to the nearest below.

**The song block layout is identical in every format.** Phrase notes at `0x0000`, grooves
`0x1090`, song rows `0x1290`, table envelopes `0x1690`, chains `0x2080`, instruments `0x3080`,
table transposes `0x3480`, table commands `0x3680`/`0x3880`/`0x3A80`/`0x3C80`, tempo `0x3FB4`,
phrase commands `0x4000`/`0x4FF0`, waves `0x6000`, phrase instruments `0x7000`. Only the
*interpretation* changes, which is why one parser serves every version.

## 2. What is the same in every release

Measured on all 31 archive ROMs plus 8.4.4, 9.2.L and 9.3.9, and worth stating because each one
was a candidate for change:

- **The wave channel's note table.** Note bytes twelve apart are twelve semitones apart and the
  absolute periods agree to within one unit on every release (44, 1046, 1547, 1798, 1923, 1985,
  2017, 2032, 2040 for note bytes 01, 0D, 19, 25, 31, 3D, 49, 55, 60). Only 3.1.5 differs, and
  only past note `0x43`, where its table wraps to a low period instead of holding the top.
- **The tempo law.** A tempo byte of 40 and up is that many BPM, identically everywhere; a groove
  of `(3,9)` runs exactly as `(6,6)` does, so only the pair's sum counts.
- **`S` on the pulse channels.** `S23` twice from a fresh note gives `NR10 = 00 ED CA` on every
  release: the running sweep byte of section 72 is not a 9.x invention.
- **`W`** keeps the low two bits of its byte, everywhere.
- **`M`**'s two halves, **`P`**'s and **`L`**'s arguments within a pitch law, **`plainTrig`**
  (section 84: a note-on sounds the plain note and the table's transpose lands one update later),
  and the latent **`LENGTH`** of section 87 (instrument byte 3 reaches `NR41`, and the note-on
  never sets `NR44`'s enable bit).

## 3. The differences, oldest first

| release | format | differs from 9.3.9 | how it maps |
|---|---|---|---|
| 3.1.5 – 3.5.1 | 0 | command letters have no `B`; envelope is the chip's own `NRx2`; noise is `~SHAPE + 16 × (5 − octave)`, saturating; **a wave instrument has no frame run** (§89); `P`/`L` are period-register units (§88); `V` is a one-sided triangle **below** the note; `C` and `V` do nothing on noise; no pitch change ever restarts the noise channel; `R x y` retriggers every **y + 1** ticks and `R x 0` once; `T` below 40 clamps to 40 BPM; a cell with a blank instrument column still sounds | the letter table, `EnvelopeLaw::Chip`, `NoiseRule::Shape`, `PitchLaw::Register`, `VibratoLaw::RegisterOneSided`, `NoisePitch::Never`, `retrigPlus = 1`, `retrigZeroOnce`, `bareNoteSounds`, `waveFrameRun = false`, and `pitchRegisterUnits` so `P`'s byte moves exactly that many units a clock. `C`/`V` on noise are dropped with a note |
| 3.6.8 – 3.9.2 | 2 | as above but `V` is already 9.x's centred vibrato | `VibratoLaw::Semitone`; the rest as format 0 |
| 4.0.4 – 4.3.0 | 2 | **a cell whose instrument column is blank stops triggering** -- it bends the channel to that note instead (§101); the noise table's transpose changes law (it stepped `NR43` in uneven jumps before, one step per row after) | `bareNoteSounds = false`: the cell keeps its blank column and becomes ChipBoy's own bare note. **Ambiguous** — 3.9.2 and 4.0.4 write the same format byte |
| 4.4.0 – 4.7.3 | 3 | `P` and `L` still register units; everything else as 4.3.0 | the format-3 model with `retrigZeroOnce` |
| 4.8.0 – 5.0.3 | 3 | **`R x 0` retriggers every tick instead of once** (changed back in 8.8.1) | `retrigZeroOnce = false`, so `R x 0` imports as ChipBoy's `R x 1`. **Ambiguous** — 4.7.3 and 4.8.0 write the same format byte |
| 5.7.8 – 6.0.1 | 4 | `P` and `L` become the 9.x semitone laws; **`C` starts working on the noise channel**; still no wave frame run; 5.7.8's vibrato is a little slower than 5.8.8's | `PitchLaw::Semitone`, `noiseChord`. 5.7.8's vibrato is **not** mapped: it differs from its format-mates by about one period unit a clock |
| 6.4.5 | 5 | **an `E` on the wave channel with a value outside 0–3 writes a nonsense `NR32`** (`E10` → `FE`, `E20` → `FC`, `E30` → `FA`) instead of muting; still no wave frame run | ChipBoy clamps to the four levels; the difference is noted at import |
| 6.8.2 – 7.0.2 | 7 | **the wave frame run begins** (§89): a wave instrument walks its synth's frames while a note sounds | `waveFrameRun` |
| 8.4.4 – 8.5.1 | 11 | `B` enters the command letter table (every letter from `C` on moves up one code); the envelope becomes three stages the **chip** ramps between; `T` bytes 0–39 start meaning 256–295 BPM; the `E`-outside-0–3 bug is still there | the letter table, `EnvelopeLaw::HardwareStages`, `tempoLowIsHigh` |
| 8.8.6 | 15 | noise is `FF − note byte`; the envelope's three stages are ramped in software; `R x 0` back to once (8.8.1) | `NoiseRule::Raw`, `EnvelopeLaw::SoftwareStages`, `retrigZeroOnce`. *Assumed*: `retrigPlus` is carried from 8.5.1, not measured |
| 9.2.L – 9.4.2 | 22 | the reference. The musical noise map, `S` on noise in semitones, the synth byte moves from instrument byte 2 to byte 3, noise `PITCH` (section 86), `V` reaches the noise channel, `R x y` retriggers every **y** ticks, and `E` on a pulse stops retriggering | — |

## 4. The two boundaries the format byte cannot see

These are the cases where two releases write the same format byte and read a song differently.
The importer takes the **ROM's own version string** when one is supplied
(`lsdjModelForRomVersion`), and otherwise the format's default model, named here:

1. **Format 2 splits at 4.0.4.** A note whose instrument column is blank sounds on 3.6.8–3.9.2 and
   is silent from 4.0.4, and the noise table transpose changes law at the same place. Default for
   an unknown format-2 song: **4.0.4 – 4.3.0**, the later reading.
2. **Format 3 splits at 4.8.0.** `R x 0` retriggers once on 4.4.0–4.7.3 and every tick on
   4.8.0–5.0.3. Default for an unknown format-3 song: **4.8.0 – 5.0.3**, because that reading
   then held until 8.8.1.

## 5. What cannot be mapped

- **The noise `S MODE` / `S CMD` setting, instrument byte 2, on formats 2 through 11.** From 4.1.0
  a non-zero byte 2 changes what an `S` command does to `NR43` — it holds the LFSR width through
  the command, where `FREE` lets `S` cross it. (From 9.2 the same byte becomes `PITCH` and applies
  to *every* pitch change, which ChipBoy does carry, section 86.) ChipBoy has no width clamp on
  `S`; an instrument that uses it gets a note at import.
- **The vibrato of 5.7.8.** About one period unit a clock slower than 5.8.8 – 6.0.1, which share
  its format byte. Not modelled.
- **`M` on 4.6.9.** Every `M` value leaves the master volume at maximum on that release alone;
  its format-mates set it normally. Treated as the 4.6.9 bug it looks like, and imported as its
  format-mates behave.
- **The noise table transpose before 4.0.4.** `NR43` moves in uneven jumps rather than one step a
  row. The law is not worked out; such a table imports under the 4.0.4 rule.
- **`E` on the wave channel outside 0–3 on 6.4.5 – 8.5.1.** The ROM writes a nonsense `NR32`;
  ChipBoy clamps to the nearest of the four levels.
- **`R x y` where the version's interval is fifteen or more.** Before 9.2 the interval is `y + 1`
  ticks, so `R x F` would need sixteen, past ChipBoy's nibble. Fifteen is used, with a note.

## 6. Where the test songs stand

The user's own saves, against their own ROMs, over eighty seconds. Two rulers, because one is not
enough: **note-ons**, ChipBoy's against the ROM's, and **agree**, the share of the time the two are
sounding the same semitone, sampled every 2 ms in six second windows each with its own offset
(`/root/lsdj/probe/agree.py`; §9 says why counting triggers alone misleads).

| song | version | PU1 | PU2 | WAV | NOI |
|---|---|---|---|---|---|
| `SUNRISE` | 9.3.9 | **213 / 213**, 99% | **130 / 130**, 97% | **309 / 309**, 65% | **498 / 498**, 99% |
| `CASTSHDW` | 9.2.L | **399 / 399**, 95% | 177 / 176, 90% | 4264 / 3075, 44% | 637 / 655, 94% |
| `SAMESONG` | 9.2.L | 225 / 224, 85% | 160 / 159, 91% | 1596 / 1586, 80% | 1678 / 1716, 96% |
| `DELIVERY` | 9.2.L | **99 / 99**, 90% | **89 / 89**, 98% | 5104 / 4209, 68% | 1067 / 1054, 97% |
| `READROOM` | 9.2.L | 577 / 561, 66% | 199 / 156, 52% | 12921 / 8641, 64% | 926 / 1852, 58% |
| `SPACE TI` | 8.4.4 | 359 / 299, 60% | 846 / 797, 72% | 1135 / 1136, 44% | 437 / 373, 81% |
| `BUS` | 3.6.5 | 162 / 137, 30% | **417 / 417**, 98% | 66 / 33, 99% | 752 / 640, 96% |
| `CLUCK` | 3.6.5 | 397 / 395, 75% | 196 / 192, 97% | 1465 / 1058, 53% | 439 / 427, 93% |
| `DISPATCH` | 3.6.5 | 187 / 183, 22% | 171 / 129, 98% | 1022 / 480, 32% | 292 / 288, 76% |

`SUNRISE` is the reference: every channel's note-on count exact, and three of the four sounding the
ROM's note 97-99% of the time. Its wave column is the metric's own limit rather than a fault -- the
channel is swept drums whose pitch moves every 2.8 ms, and a one-tick offset halves the score.

What the last rounds moved, in order of how much:

- **A table's `H` costing no tick** (§95). Every arpeggio in every song ran a fifth slow before it.
  `SAMESONG`'s pulse channels went from 47% and 30% to 85% and 94%.
- **The ROM beside the save deciding the model** (§98). Every *Computer Savvy* song was being read
  under the 4.0.4 model with no kit ROM at all.
- **A blank instrument column keeping its note** (§101). Twenty cells in `SAMESONG`, including the
  `L 10` bend in phrase 1C that ChipBoy played as silence and now matches the ROM period for period.
- **`REPEAT` off the right byte** (§93) and **`L` replacing `P`** (§99), which are the wave run and
  the wave kick: `SAMESONG`'s kick used to run off the bottom of the register and wrap round three
  times a note -- the machine gun the user heard -- and now follows the ROM within a unit or two.
- **The kit `LENGTH` byte, `LOOP` bit and `P` law** (§96, §97), which is `CASTSHDW`'s wave channel.

**What is still wrong, in the order it is worth taking up:**

1. `READROOM` **comes apart at about thirty seconds**. Its first five windows are 89-99% on every
   channel now that a phrase's `H` loops (§102) -- it used to be 3-6% from the first bar, because
   every phrase with a counted hop was cut short and the channel ran away from the ROM
   immediately. What happens at 30 s is a second thing and unlooked at.
2. **A tick appears from nowhere every ten seconds or so.** `DELIVERY`'s and `CASTSHDW`'s per-window
   offsets walk from +28 ms to -96 ms in steps of one tick with long plateaus between. The tempo is
   not the cause: the ROM's tick for every tempo byte measured (85 to 190) is within 0.016% of
   `1 / (0.4 x bpm)`, and both songs' grooves are 6/6 throughout with no `G` or `T`. One row in
   about a hundred is a tick longer in ChipBoy. `SAMESONG` and `SUNRISE` barely drift at all.
3. `CASTSHDW`'s **kit notes**: 1142 note-level hits on the ROM against 326. The ones that do play
   land on the ROM's period 77% of the time (43% before §97), so it is missing whole notes.
4. `DISPATCH` and `BUS` on **PU1**, and the three format-2 songs' **wave** channels.
5. **LSDj's flat 256-frame wave table** (§100), which ChipBoy's sixteen-frame wave cannot express.
   It is a bank-model decision, not a fix; `docs/HANDOFF.md` has the two ways out.

(The wave column counts the ROM's note-ons once: LSDj triggers that channel twice, with a stale
period and then the real one.) The three format-2 songs are from the *Computer Savvy* source files,
written in LSDj 3.6.5 and read on 3.6.5 now that the importer finds an old ROM at all.

## 7. Still to measure on the new ROMs

Fourteen releases arrived after the first sweep and fill every gap that mattered: **7.2.3**
(format 8), **7.5.4** (9), **7.9.9** and **8.0.0** (10), **8.2.0** (11), **8.8.6** (15),
**8.9.3** and **8.9.5** (17), **9.0.0** and **9.0.1** (18), **9.1.0** (19), **9.1.C** (21), and
**3.6.5**, which is the version the *Computer Savvy* songs were actually written in.

Formats 9 and 10 have their own model now (`kLsdj75`, LSDj 7.5.4 - 8.0.0): the wave
instrument's `REPEAT` moved from byte 3 to byte 2 at 7.5.4, measured on every ROM from 6.8.2 up
(COMMANDS_AND_TEMPO §93), and a model cannot straddle that. `kLsdj68` keeps formats 7 and 8.

The models still do not know about formats 17, 18, 19 and 21: each falls through to the 8.8.6
model. Two things are already known to be wrong there -- the synth number moves from byte 2 to
byte 3 at format 17 (§93's table), and a table `ENV` hop stops costing a tick at 8.9.3 -- and the
changelog puts the whole noise overhaul at 9.0: the musical map, `V` on the noise channel, `C`
behaving like the pulses', the removal of `S MODE`. So a model for 17-21 wants the noise map
measured on 9.0.0 first. That is the next sweep, and the battery to run it with is already
written.

## 8. What is still assumed rather than measured

Everything the previous round listed as a missing ROM has arrived, so only these remain:

- **Formats 1, 6, 12, 13, 14, 16 and 20** were never seen. A song claiming one takes the nearest
  model below, which is a guess, but no released version writes them.
- **`retrigPlus` on 8.8.6.** Carried from 8.5.1 on the reading that 8.8.1's "changed back R
  command to 4.7.3 behavior" reverted the whole of `R`. Now testable on the 8.8.6 ROM.
- **The noise table transpose before 4.0.4** (§5), and **the bend's phase at a note-on** (§4),
  which is the same question as the matrix's §10.1.
- **Byte 3 of a wave instrument's low nibble**, and **byte 3 of a kit instrument**: neither
  changes anything a trace can hear (§93, §96), and both hold a value in real songs.

## 9. How this was measured

`tools/lsdjref` with a probe song built into each ROM's **own** bootstrapped save
(`lsdjref_trace --init-sav`, then `Probe(blank=True)`), so a version can be probed with nothing
but its ROM. The scripts live outside the tree at `/root/lsdj/probe/` (rule L3 keeps ROMs and
saves out of the repository): `vs_all.py` for the structural fields, `vs_beh.py` and `vs_beh2.py`
for the per-command battery, `vs_wave.py`, `vs_T.py`, `vs_tick.py`, `vs_wr.py` for the ones that
needed care.

Two traps worth repeating, because both produced a confident wrong answer first:

- **The wave channel is triggered with a stale period.** Reading `NR33`/`NR34` *at* the trigger
  says every note has the same period. Take the last pair written within a few milliseconds of it.
- **A note with no instrument column does not *trigger* from 4.0.4** (§101). A probe that relies on
  one measures the *next pass* of the phrase instead, which reads as a tempo that is half what it
  should be. Give every probe note an instrument column. It does still move the channel's pitch,
  which the first probe of this missed because nothing was sounding when it ran -- a rule about a
  channel's running state has to be measured with the channel running.
- **Counting triggers is not a comparison** on a channel that retriggers inside a note. A kit
  plays by rewriting wave RAM once a wave cycle and both sides trigger on every rewrite, so the
  count is a count of refills. `/root/lsdj/probe/agree.py` samples the pitch both sides are
  sounding every 2 ms instead and reports the share of the time they agree, in six second windows
  each with its own offset (the ROM's playback drifts a few ms against ChipBoy's over a song);
  `runs.py` groups the triggers into notes and compares those. Both are better rulers than
  `cmp.py`'s longest common run, which one row's difference in timing can halve.

## 10. What has changed since a song was last verified

Every finding that changes what the importer or the driver produces, newest first, with the
formats it can move. A song measured before a row here has to be measured again before its numbers
mean anything -- that is what this list is for. `/root/lsdj/probe/song.py SAV IDX ROM TAG [sec]`
re-runs one song end to end; `agreew.py` and `runs.py` are the rulers (§9).

| § | what changed | formats it moves | measured on |
|---|---|---|---|
| 102 | a phrase's `H x y` with x > 0 **loops inside the phrase**, the step carrying it silent on a hopping pass, and the groove walks with the play order | **all** | 9.2.L |
| 101 | a cell with a blank instrument column keeps its note: a bare note from 4.0.4, a trigger before | 2 and up (it was dropped outright) | seventeen releases, §10.1 |
| 100 | `F` on the wave channel takes the **whole byte**, and LSDj walks a flat 256-frame wave table (ChipBoy's sixteen-frame wave wraps: noted at import, not modelled) | **all**, confirmed from 3.1.5 up | seventeen releases, §10.1 |
| 99 | `L` and `P` replace one another; in Drum a slide is linear in the **period register** | 4 and up (formats 0-3 unmeasured, §10.1) | seventeen releases, §10.1 |
| 98 | the ROM beside the save decides the model, a pre-4.3 ROM is recognised at all, and 3.6.5 reads as 3.6.8 | 0-3 above all (every old ROM was invisible) | 3.6.5 |
| 97 | a kit reads `PITCH` from byte 5; `P` on a kit is period-register units (1 a clock FAST/DRUM, 1 a tick TICK, 3x once STEP) | every format with kits | 9.2.L |
| 96 | a kit's one `LENGTH` is byte 11 (not byte 3); `LOOP` is byte 5 bit 5 | every format with kits | 9.2.L |
| 95 | a table's `H` costs no tick: the row it lands on plays in the same tick | **all** | 3.6.5, 8.4.4, 9.2.L, 9.3.9 |
| 94 | the tick a note starts on belongs to the first frame of the run | 7 and up | 9.2.L |
| 93 | the frame run's `REPEAT` is byte 3 on formats 7-8, byte 2 from 9; the synth number moves to byte 3 at 17 | 7 and up | every ROM from 6.8.2 |
| 92.1 | a table's commands are converted for the kind of instrument that runs the table | **all** | -- (a code fault, not a law) |
| 92 | `F` on the wave channel **advances** the frame by `y`; it does not name one | **all** | 8.4.4, 8.8.6, 9.2.L, 9.3.9 |
| 91 | the wave instrument's `SPEED` (byte 11) is a **signed** byte, `s + 4` ticks a frame | 7 and up | 9.3.9 |
| 90 | `R 8 y` is a fast retrigger every `y + 1` pitch clocks; `R 8 F` stops one | **all** | 9.2.L |
| 89 | a wave instrument has no frame run at all before format 7 | 0-5 | 6.4.5 and below |
| 88 | `P` (and `L`) move the period register by whole units before 5.7.8 | 0-3 | 4.x, 5.0.3 |
| 87 | the instrument's `LENGTH` is latent: it reaches `NR41` but only a pitch restart enables it | **all** | 9.3.9 |
| 86 | the noise instrument's `PITCH` byte (FREE / SAFE); before 9.2 the byte is `S MODE` instead | 22 (and 15 and below read as `S MODE`) | 9.2.L, 9.3.9, 8.8.6 |
| 85 | a mapped noise note reads and is entered as **LSDj's entry number** (`note - 1`) | 22 | 9.3.9 |
| 84 | a note-on triggers at the **plain** note; the table's transpose reaches the channel one update later | **all** | 9.3.9 |
| 83 | LSDj's noise table is 120 entries and its index **wraps**; a bank with that map has no command octave on noise | 22 | 9.3.9 |
| 82 | a pitch change that turns the 7-bit LFSR on retriggers the noise channel | 22 | 9.3.9, older ROMs |
| 81 | an imported noise instrument carries LSDj's own note map | 22 | 9.3.9 |
| 80 | a phrase `H` is two commands and ChipBoy expresses one | **all** | 9.3.9 |
| 79 | `E` on the wave channel reads `y` | **all** | 9.3.9 |
| 78 | `F` on PU1 is a fine offset, on PU2 a transpose plus a fine offset | **all** | 9.3.9 |
| 77 | `C` reaches noise from format 4, `V` from format 22 | 4 and up / 22 | the version sweep |
| 76 | `R`'s interval is `y` ticks from 9.2 and `y + 1` before; `R x 0` fires once or every tick by version | **all** | the version sweep |
| 75 | `M`'s two halves | **all** | 9.3.9 |
| 74 | `Z` re-runs its **own lane** | **all** | 9.3.9 |
| 73 | `B`'s two laws: the phrase roll is `n/15`, the table hop `x/16` | 11 and up | 9.3.9 |
| 72 | `S` on PU1 is a running sweep byte | **all** | 9.3.9 |

### 10.1 The new rules, asked of every release

§99, §100 and §101 were measured on 9.2.L alone. `/root/lsdj/probe/vs_new.py` asks them of a
version at each model boundary. What came back (the `L vs P` column is the period register's step
per pitch clock: the bend's own steps, then what follows the `L`):

| version | fmt | `L` vs `P` | Drum slide | `F` past frame 15 | blank instrument column |
|---|---|---|---|---|---|
| 3.1.5 – 3.9.2 | 0, 2 | not read (see below) | no slide | **flat table** | **triggers** |
| 4.0.4 – 4.8.0 | 2, 3 | not read | no slide | **flat table** | bends, no trigger |
| 5.7.8, 6.4.5 | 4, 5 | −1 −1 −1 −2 −1 −1 then **+1 +1 +1** | ≈ −27 a clock, constant | **flat table** | bends, no trigger |
| 6.8.2 | 7 | the same | ≈ −27 a clock, constant | **flat table** | bends, no trigger |
| 7.5.4 | 9 | not read | no slide | **`F` did not move it** | bends, no trigger |
| 8.4.4, 8.8.6, 8.9.3, 9.0.0 | 11-18 | the same | ≈ −5.5 a clock, constant | **flat table** | bends, no trigger |
| 9.2.L, 9.3.9 | 22 | the same | ≈ −5.5 a clock, constant | **flat table** | bends, no trigger |

- **§101's boundary is exactly 4.0.4**, which is where `bareNoteSounds` already puts it. Every
  release above it bends without triggering and every one below it triggers. Nothing to change.
- **§100 holds everywhere.** `F 10` walks sixteen frames on, out of the instrument's synth and into
  the next, on every release from 3.1.5 up. So the gap it leaves in ChipBoy -- a wave of sixteen
  frames that wraps -- is a gap in *every* format, not just 9.x. (7.5.4 is the one version where
  the probe's `F` moved nothing at all; its `MANUAL` play may read byte 9 differently. Unmeasured.)
- **§99's "a slide replaces a bend" holds from 5.7.8**, the release where `P` and `L` become the
  semitone laws (§3). Below that they work in register units and this probe's `P F0` runs the
  period off before the `L` lands, so it reads nothing: the rule is **unmeasured on formats 0-3**.
  A gentler `P` would settle it.
- **§99's register-unit Drum slide holds from 5.7.8 too** -- the step is constant to within a unit
  or two at every release. Its *speed* is not the same, though: `L 10` covers the same distance in
  about five updates on 5.7.8 to 6.8.2 and seventeen on 8.4.4 and up, so `L`'s duration law changed
  somewhere between. ChipBoy uses the later one. Unmeasured, and rare: it needs a Drum instrument
  whose table slides, on a format-4 to format-7 song.

Model fields added over the same rounds, each of which changes a format's reading:
`bareNoteSounds` (a note with a blank instrument column stops sounding at 4.0.4), `waveFrameRun`
(§89), `envHopCostsTick` (a table `ENV` hop stops costing a tick at 8.9.3), `tableGrooveWalks`,
`tempoLowIsHigh` (`T` bytes 0-39 mean 256-295 BPM from format 11), `retrigPlus` /
`retrigZeroOnce` (§76), `noiseChord` / `noiseVibrato` (§77), `noisePitchByte` (§86), `waveByte`
and `waveRepeatByte` (§93), `pitchLaw` (§88), `noiseS`, and the `NoiseRule` the map is read under.

**Where each song was last measured**, so it is clear what is stale:

| song | version | last run | carries |
|---|---|---|---|
| `SAMESONG` | 9.2.L | after §100 | everything |
| `CASTSHDW`, `DELIVERY`, `READROOM` | 9.2.L | after §97 | not §98-§100 |
| `SPACE TI` | 8.4.4 | after §97 | not §98-§100 |
| `SUNRISE` | 9.3.9 | before §93 | not §93-§100 |
| `CLUCK`, `BUS`, `DISPATCH` | 3.6.5 | after §98 | not §99-§100 |
| `GOAL ACH`, `STARWAY` | 8.4.4 | never | -- |
| the other 22 *Computer Savvy* songs | 3.6.5 | never | -- |

