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
| 2 | 3.6.8, 3.7.5, 3.8.7, 3.8.9, 3.9.2, 4.0.4, 4.1.0, 4.3.0 |
| 3 | 4.4.0, 4.5.4, 4.6.0, 4.6.2, 4.6.9, 4.7.3, 4.8.0, 4.9.4, 5.0.3 |
| 4 | 5.7.8, 5.8.8, 5.9.9, 6.0.1 |
| 5 | 6.4.5 |
| 7 | 6.8.2, 6.9.0, 7.0.2 |
| 11 | 8.4.4, 8.5.1 |
| 22 | 9.2.L, 9.3.9, 9.4.2 |

Formats 1, 6, 8–10, 12–14 and 16–21 were never written by a stable release. Format 15 is 8.8.6,
which is not in the archive; its row is carried from an earlier round's measurement on the user's
own 8.8.6 ROM and is marked *assumed* where it could not be re-checked.

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
| 3.1.5 – 3.5.1 | 0 | command letters have no `B`; envelope is the chip's own `NRx2`; noise is `~SHAPE + 16 × (5 − octave)`, saturating; `P`/`L` are period-register units; `V` is a one-sided triangle **below** the note; `C` and `V` do nothing on noise; no pitch change ever restarts the noise channel; `R x y` retriggers every **y + 1** ticks and `R x 0` once; `T` below 40 clamps to 40 BPM; a cell with a blank instrument column still sounds | the letter table, `EnvelopeLaw::Chip`, `NoiseRule::Shape`, `PitchLaw::Register`, `VibratoLaw::RegisterOneSided`, `NoisePitch::Never`, `retrigPlus = 1`, `retrigZeroOnce`, `bareNoteSounds`. `C`/`V` on noise are dropped with a note |
| 3.6.8 – 3.9.2 | 2 | as above but `V` is already 9.x's centred vibrato | `VibratoLaw::Semitone`; the rest as format 0 |
| 4.0.4 – 4.3.0 | 2 | **a cell whose instrument column is blank now sounds nothing at all**; the noise table's transpose changes law (it stepped `NR43` in uneven jumps before, one step per row after) | `bareNoteSounds = false`: such a note is dropped with a note at import. **Ambiguous** — 3.9.2 and 4.0.4 write the same format byte |
| 4.4.0 – 4.7.3 | 3 | `P` and `L` still register units; everything else as 4.3.0 | the format-3 model with `retrigZeroOnce` |
| 4.8.0 – 5.0.3 | 3 | **`R x 0` retriggers every tick instead of once** (changed back in 8.8.1) | `retrigZeroOnce = false`, so `R x 0` imports as ChipBoy's `R x 1`. **Ambiguous** — 4.7.3 and 4.8.0 write the same format byte |
| 5.7.8 – 6.0.1 | 4 | `P` and `L` become the 9.x semitone laws; **`C` starts working on the noise channel**; 5.7.8's vibrato is a little slower than 5.8.8's | `PitchLaw::Semitone`, `noiseChord`. 5.7.8's vibrato is **not** mapped: it differs from its format-mates by about one period unit a clock |
| 6.4.5 | 5 | **an `E` on the wave channel with a value outside 0–3 writes a nonsense `NR32`** (`E10` → `FE`, `E20` → `FC`, `E30` → `FA`) instead of muting | ChipBoy clamps to the four levels; the difference is noted at import |
| 6.8.2 – 7.0.2 | 7 | as 6.4.5 | — |
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

## 6. Where the two test songs stand

The user's own saves, against their own ROMs, over eighty seconds, note-on for note-on:

| song | version | PU1 | PU2 | WAV | NOI |
|---|---|---|---|---|---|
| `SUNRISE` | 9.3.9 | **276 / 276** | **170 / 170** | 440 / 440 | **740 / 740** |
| `SPACE TI` | 8.4.4 | 359 / 299 | 846 / 797 | 1135 / 1151 | 437 / 373 |
| `SAMESONG` | 9.2.L | 225 / 224 | 160 / 159 | 1596 / 745 | 1678 / 5191 |

`SUNRISE` is exact on three channels of four (the wave channel's swept drums differ only in the
first update of each, matrix section 10.1). The other two are not, and they are the next round:
the trigger *counts* are close on `SPACE TI` but the values diverge within a few notes, and
`SAMESONG`'s wave and noise channels are out by a factor. Neither is explained by anything in this
document -- every rule here was measured against a controlled probe, not against these songs -- so
they want the same treatment `SUNRISE` had: diff the register streams and read the ROM where they
disagree. `SAMESONG` also uses instrument finetune (byte 11) on four pulse instruments, which the
importer drops with a note and the driver could carry (section 78 gives `F` the same law).

## 7. How this was measured

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
