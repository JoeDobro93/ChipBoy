# The version map: what differs from LSDj 9.4.2, and what the importer does about it

LSDj 9.4.2 is ChipBoy's reference: the driver is 9.4.2's code read and measured
(`docs/COMMANDS_AND_TEMPO.md` §147-§186), and every other version maps **onto** it at import.
This table is the record of that mapping. One row per difference; the columns say what the
older version does, what 9.4.2 does, how a song's bytes are translated so 9.4.2's engine plays
the older sound, and what happens when no translation exists. `docs/LSDJ_VERSIONS.md` has the
measurements behind each row (its §11 for format 22, §3 for the formats before it), and the
model fields named here live in `Source/core/Import/LsdjModel.h`.

How a version is told apart: the save's format byte (`$7FFF` of the song) picks the model, and a
ROM beside the save refines it by its version string (`lsdjModelForRomVersion`); with several
ROMs beside a save the newest that reads the format wins. A save alone in format 22 reads as
9.4.2. Bug reports start on 9.4.2 so a difference is not the default version's own.

## Format 22: LSDj 9.2.J - 9.4.2

Three models: `LSDj 9.2.J - 9.3.3`, `LSDj 9.3.4 - 9.3.9`, `LSDj 9.4.0 - 9.4.2`. Probed ROM
against ROM on the 349-case matrix plus the changelog's candidates, for 9.2.J and 9.2.L alike
(the two differ in nothing the matrix sees). The eight songs' bytes are the same in the 9.2.L
save and the 9.4.2 save but for the editor's bookkeeping (`$3FB3`, `$3FB6`-`$3FB8`, `$3FC1`,
`$3FCB`: counters and cursor state the importer never reads).

| what | older (9.2.J - 9.3.3) | 9.4.2 | mapped at import | model field |
|---|---|---|---|---|
| `R x y`'s volume nibble on the wave channel, kits included (9.3.4) | ignored: the rolls keep NR32 | walks NR32 a notch a roll, held at the ends (§182) | a WAV/KIT `R x y` with `x` not 0 or 8 imports as `R 0 y`; `8` (the resync) is kept | `waveRetrigNibble` |
| kit vibrato depth (9.4.0 halved it) | `V 42` moves NR33 by `1E` an instant | by `0F` (§187) | the kit instrument takes **`vibDouble`** (the Instrument tab's *Kit vibrato 2x*); the `V` bytes stay | `kitVibratoHalved` → `Instrument::vibDouble` |
| `R` on a DRUM instrument (9.4.0) | the pitch word runs on through the roll: a rolled kick keeps falling | the roll zeroes the offset word, each roll's sweep starts from the entry (§185) | **not mapped**: ChipBoy plays 9.4.2's way. Left as is by decision; a per-instrument "R keeps the pitch" switch would carry it if wanted | `retrigResetsDrumPitch` (carried, unused) |
| the cell `R`'s immediate retrigger | folded into the note-on's burst: the nibble on the note's trigger, no second burst | a second burst 1.06 ms after the trigger | nothing to map: the same levels within a millisecond | -- |
| the wave note-on's `NR31` write, the refresh's `NR11` rewrite | none | written, the length bit off / the same duty | nothing to map: no effect on the sound | -- |
| tempo (the 9.3.9 "inaccurate sequencer tempo" fix) | 15.333 ms a tick at 163 BPM | 15.313 ms | nothing to map: both inside the interrupt jitter §160 leaves unmodelled, and the tempo words are byte for byte the same | -- |
| 9.4.2's "noise table transpose not reset on a new note", 9.2.K's "kit F reset the amplitude" | not seen on the probes (`X92_NOI_tsp2`-`tsp4`, `X92_KIT_F01`, `_half`) | -- | nothing | -- |
| a kit `DIST` page outside `D0`-`D3` (`UNMASKED`'s `8E`, video RAM) | the same page at the same ROM offset (`$7842A`) | the same | the page reader (`lsdjRawPages`) takes a version table of the font block's offset and the zero pages, so 9.2.J/L bring the page too; the Kits tab has a *Page* choice (§192) | -- |
| kit numbers | the k-th kit bank in ROM order, empty banks skipped -- on 9.2.L and 9.4.2 alike (§193) | the same | ChipBoy had them as bank `k + 8` since §172 (wrong): `READROOM`'s `AMEN2` played `AMEN1` and `LCAMN` nothing. Fixed; not a version difference | -- |

## Formats 0 - 15: LSDj 3.1.5 - 8.8.6

Measured in the second campaign on the ROMs of that time (`docs/LSDJ_VERSIONS.md` §3, §5); the
models stay in the code as the record. None was re-probed against 9.4.2 in the third campaign,
so a difference 9.4.2's driver has since corrected may hide behind one of these rows.

| what | older | 9.4.2 | mapped at import | model field |
|---|---|---|---|---|
| the command letters | no `B` before 8.4.0 (every letter from `C` up one code), no `Z` in 3.1 | the 9.x table | the letter table of the version | `commandLetters` |
| the envelope | the chip's own NRx2 (formats 0-10); three stages the chip hands over between, each written with a retrigger `(2·|Δvol| + 1)·rate/128` s after the one before (11, §189); three stages ramped in software (15) | the 358 Hz countdown machine (§164) | `Chip`: the byte as ChipBoy's Chip envelope (the driver steps the level at the chip's rate); `HardwareStages`: Chip plus **`envStage2`/`envStage3`**, the bytes the driver writes with a retrigger on the ROM's timing (probed on seventeen triples); `SoftwareStages`: §164's machine | `envelopeLaw`, `envPeriods` |
| the noise note | each nibble of SHAPE complemented, the high one raised by `3 − octave` and saturating on its own (0-14, §188); `FF − note` (15) | the musical map (§156) | `NoiseRule::Shape`: the instrument's **LSDj shape** mode (`noiseShapeMode`, `noiseShape`; the Instrument tab's third noise *Pitch* choice) and the cell keeps LSDj's note -- the driver writes the ROM's byte; `Raw` (8.8.6): still the nearest-clock note, to be revisited when 8.8.6 is swept | `noiseRule`, `noiseMap`, `noiseLo/Hi` |
| `S` on noise | each nibble off NR43's, modulo 16, once | semitones through the map | the byte as it stands; in LSDj shape the driver's Register domain takes it off the byte | `noiseS` |
| `P` on noise | the same nibble subtraction every tick (`P 02`: `1E 1C 1A …`) | map entries a tick | the byte as it stands; the Register domain steps it | -- |
| `C` on noise | two states, the note and the note less the **whole byte** nibble-wise, a tick each (`C 37`: `10 E9 10 E9`) | note, +x, +y | the byte as it stands; the driver's shape mode alternates the two | `noiseChord` |
| the noise table transpose column | a **byte** off NR43 (`03` on `00` is `FD`), written when it changes, dropping the S/P delta | semitones through the map | the byte as it stands; the driver's shape mode subtracts it and drops the delta (a `P` keeps its step) | -- |
| noise `S MODE` (byte 2) | nonzero is STABLE: `S`, `P` and `C` keep the note's LFSR width bit | 9.2's `PITCH` byte | `noiseStable`, the Instrument tab's *S mode* | `noisePitchByte` |
| the pulse FINETUNE | a nibble in byte 7 bits 2-5, `v/32` of a semitone down (5.7.8 - 8.5.1, §191); period units (3.6.8 - 5.0.3); none (3.1.5 - 3.5.1) | byte 11, 1/256 semitone | `fineTuneNibble`: `8·v` into `fineTune`; the period-unit law is **not mapped** (the list below) | `fineTuneNibble` |
| `W xy` on a wave instrument whose PLAY is MANUAL | no frame run (9.4.2 and 8.5.1 alike, §192) | the same | dropped with a note, where ChipBoy's `U` would start one | -- |
| a kit's `P` | 4 units an instant and one more step a tick (§190) | the same | nothing to map: the driver does it now | -- |
| the vibrato | the same depth and rate, the swing rounded half an instant apart (`V 42`: `… 96 95 95 95 96 97 98 …` against 9.4.2's `… 96 97 97 98 …`) | -- | nothing to map (cosmetic) | -- |
| `P` and `L` | period-register units a clock (0-3) | semitones (§169) | `PitchLaw::Register`: the byte becomes register units (`pitchRegisterUnits`) | `pitchLaw` |
| `V` | a one-sided triangle **below** the note, register units (format 0) | centred, semitones | `VibratoLaw::RegisterOneSided`: speed and depth recomputed, with an import note | `vibratoLaw` |
| `C` and `V` on noise | ignored (`C` before 5.7.8, `V` before 9.0) | applied | dropped, with an import note | `noiseChord`, `noiseVibrato` |
| `R x y`'s interval | every **y + 1** ticks before 9.2; `R x 0` every tick between 4.8.0 and 8.8.0 | every `y` ticks, `R x 0` once | `y + 1`; `R x 0` becomes `R x 1` where it ran every tick; sixteen cannot be carried and becomes fifteen with a note | `retrigPlus`, `retrigZeroOnce` |
| `T` bytes 0-39 | 40 BPM (clamped) before format 11 | 256-295 BPM | the version's reading | `tempoLowIsHigh` |
| a cell with a blank instrument column | sounds, with the channel's last instrument (before 4.0.4) | moves the pitch without a trigger (a bare note) | `bareNoteSounds`: the cell keeps its blank column | `bareNoteSounds` |
| the wave frame run | none before 6.8.2: frame 0 held; bytes 9-11 mean other things | a run of the synth's frames | `waveFrameRun = false`; the synth and REPEAT bytes read from where the version keeps them | `waveFrameRun`, `waveByte`, `waveRepeatByte`, `waveFineTuneByte` |
| a table's `G` | holds the groove's first step before 9 | walks the groove | the version's reading | `tableGrooveWalks` |
| a table `ENV` hop | costs a tick before 8.9.3 | costs none | the version's reading | `envHopCostsTick` |
| the `DIST` pages | `D1` the mirror, `D2` the steep mirror (before 9.2) | `D1` the soft clip, `D2` the mirror | the version's list | `kitDist` |
| `E` on the wave channel outside 0-3 (6.4.5 - 8.5.1) | a nonsense NR32 | clamped to the four levels | clamped, noted | -- |
| the vibrato of 5.7.8; `M` on 4.6.9; the noise table transpose before 4.0.4 | each its own law | -- | **not mapped** (`docs/LSDJ_VERSIONS.md` §5) | -- |

## Gaps: what cannot be mapped yet, for a decision

Everything the sweep found that the import does not carry, with what the ROM does and what
ChipBoy does instead. The probes are in `/root/lsdj/probe/vs_sweep.py` by the name given.

| what | the ROM | ChipBoy | options |
|---|---|---|---|
| `R` on a DRUM instrument before 9.4.0 (§185) | the pitch word runs on through the roll | 9.4.2's reset | left by decision; a per-instrument "R keeps the pitch" switch would carry it |
| a "wrong" instrument type in ChipBoy's own editor | a WAV instrument on PU1 plays as a pulse with its bytes (imported songs already get a per-channel variant) | `typeFits` refuses the cell | let the editor place any instrument on any channel and play it as the channel's kind |
| a kit `V FF` (`X92_KIT_VFF`) | out of the depth table | not modelled | leave |
| the pulse FINETUNE before 5.7.8 (`FTb7_*` on 3.6.8 - 5.0.3) | `v` **period units** down, whatever the note (`F` is 15 units at note 34) | `8·v` in 1/256 semitone (the 5.7.8+ law) | a `fineTune` in period units under `pitchRegisterUnits`, or leave |
| a kit `L` (`KIT_L05`, 9.4.2 too) | the period runs away by `2AA` an instant, wrapping past `7FF` | the note holds | model the runaway, or leave |
| `B` (`B08_*`) | a random roll | ChipBoy's own random | nothing to compare; leave |
| the hardware envelope's rate-0 corner (`PU_adsr_A3_A0_20`) | stage 1's byte written as `A0` at the note and again 41 ms later | `A3` then nothing | leave (one triple of many) |
| `NOI_all*` past the phrase | the ROM loops the chain | the probe's ChipBoy song ends | a probe artefact, not a gap |
| the immediate `R` burst, the table `R` re-fired on the table's wrap, the note-on's column write, an `L` on a channel's first note | within 1-2 ms of ChipBoy's | -- | cosmetic; the batch compare splits them differently |
| the page-`8E` mix on 9.2.L | the LCD phase of that ROM's run | the phase fitted on 9.4.2 (§184): 551 of 640 `FE` positions match on 9.2.L, 348 of 352 on 9.4.2 | fit 9.2.L's phase too, or leave |
| a kit's `DIST` naming a video-RAM page the ROM draws at run time (`80`, `81`, `88`, `8D`, `90`, `94`, `98`-`9A`) or work RAM | whatever sits there | clips, with a note | leave |
| `E` on a pulse whose hardware stages are running | unprobed | the stages stop | probe when a song needs it |

## Adding a version

Put its ROM in `/root/lsdj/roms/`, run `/root/lsdj/probe/vs_versions.py lsdjX_Y_Z lsdj9_4_2`
(every matrix case on both ROMs), then `vv_all.py lsdjX_Y_Z lsdj9_4_2` for the list without
the length-register noise, then the changelog's candidates as cases; each real difference gets a
row here, a model field, and a value translation in `LsdjSong.cpp` where one exists.
