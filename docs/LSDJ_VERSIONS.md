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
| 4.0.4 – 4.3.0 | 2 | **a cell whose instrument column is blank now sounds nothing at all**; the noise table's transpose changes law (it stepped `NR43` in uneven jumps before, one step per row after) | `bareNoteSounds = false`: such a note is dropped with a note at import. **Ambiguous** — 3.9.2 and 4.0.4 write the same format byte |
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
| `SUNRISE` | 9.3.9 | **276 / 276** | **170 / 170** | 440 / 440 | **740 / 740** |
| `CASTSHDW` | 9.2.L | **399 / 399**, 90% | **177 / 176**, 90% | 4264 / 3075, 40% | 637 / 655, 92% |
| `DELIVERY` | 9.2.L | **99 / 99**, 67% | **89 / 89**, 75% | 5104 / 4209, 65% | 1067 / 1031, 90% |
| `SAMESONG` | 9.2.L | 225 / 224, 85% | 160 / 159, **94%** | 1596 / 1587, 73% | 1678 / 1708, **94%** |
| `READROOM` | 9.2.L | 577 / 487, 4% | 199 / 138, 5% | 12971 / 8650, 51% | 1032 / 3093, 5% |
| `CLUCK` | 3.6.5 | 277 / 279 | 139 / 135 | 303 / 270 | 288 / 286 |
| `BUS` | 3.6.5 | 67 / 49 | **307 / 307** | 22 / 22 | 476 / 404 |
| `DISPATCH` | 3.6.5 | 128 / 131 | 96 / 82 | 346 / 376 | 213 / 216 |
| `SPACE TI` | 8.4.4 | 359 / 299, 57% | 846 / 797, 39% | 1135 / 1136, 44% | 437 / 373, 81% |

`SAMESONG`, `CASTSHDW` and `DELIVERY` are the three that the rounds behind §93-§97 were fixed
against, and they agree with the ROM for most of their length now. What moved them:

- **`REPEAT` off the right byte** (§93). Every wave instrument in `SAMESONG` stores `REPEAT = F`,
  which makes its frame run a one-shot; read off the synth byte it came through as zero and every
  run looped for as long as the note held. 2866 wave note-ons became 1587 against the ROM's 1596.
- **A table's `H` costing no tick** (§95). Every arpeggio in every song ran a fifth slow before
  this. `SAMESONG`'s pulse channels went from 47% and 30% agreement to 85% and 94%.
- **The kit `LENGTH` byte and `LOOP` bit** (§96) and **`P` on a kit** (§97), which is
  `CASTSHDW`'s wave channel: it is kit drums from end to end, each hit tuned by a `P` in its cell.

**What is still wrong, in the order it is worth taking up:**

1. `CASTSHDW`'s **kit notes**: 1142 note-level hits on the ROM against 326, and the ones that do
   play land on the ROM's period 77% of the time (43% before §97). ChipBoy is missing whole notes,
   among them every one whose note byte names a sample one of the two kits does not have -- the
   ROM plays the other kit's, the importer gives up on both.
2. `DELIVERY` and `READROOM` **come apart part way through**: `DELIVERY` agrees on every channel
   until about 50 s and on none after, `READROOM` never agrees on three channels at all. Both are
   structure, not a command: something ends a phrase or a chain in the wrong place. `READROOM`'s
   noise channel triples its note count (1032 against 3093), which is the clearer thread to pull.
3. `SPACE TI` (8.4.4) starts at 20% and climbs to 98% by the end, which reads like a structural
   difference early rather than a wrong law.

(The wave column counts the ROM's note-ons once: LSDj triggers that channel twice, with a stale
period and then the real one.) The three format-2 songs are from the *Computer Savvy* source
files, written in LSDj 3.6.5 and read here on 3.6.8. `BUS`'s PU2 is 307 of 307 with a longest
common run of 304; the rest is close in count and diverges in value, for the two reasons in §4
and §5 -- the bend's phase at a note-on, and the noise table transpose before 4.0.4.

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
- **A note with no instrument column does not sound from 4.0.4.** A probe that relies on one
  measures the *next pass* of the phrase instead, which reads as a tempo that is half what it
  should be. Give every probe note an instrument column.
- **Counting triggers is not a comparison** on a channel that retriggers inside a note. A kit
  plays by rewriting wave RAM once a wave cycle and both sides trigger on every rewrite, so the
  count is a count of refills. `/root/lsdj/probe/agree.py` samples the pitch both sides are
  sounding every 2 ms instead and reports the share of the time they agree, in six second windows
  each with its own offset (the ROM's playback drifts a few ms against ChipBoy's over a song);
  `runs.py` groups the triggers into notes and compares those. Both are better rulers than
  `cmp.py`'s longest common run, which one row's difference in timing can halve.
