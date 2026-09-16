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
| kit vibrato depth (9.4.0 halved it) | `V 42` moves NR33 by `1E` an instant | by `0F` | a kit note's `V x y` imports as `V x 2y`; `y` above 7 does not fit and imports as `F` with an import note | `kitVibratoHalved` |
| `R` on a DRUM instrument (9.4.0) | the pitch word runs on through the roll: a rolled kick keeps falling | the roll zeroes the offset word, each roll's sweep starts from the entry (§185) | **not mapped**: ChipBoy plays 9.4.2's way. Left as is by decision; a per-instrument "R keeps the pitch" switch would carry it if wanted | `retrigResetsDrumPitch` (carried, unused) |
| the cell `R`'s immediate retrigger | folded into the note-on's burst: the nibble on the note's trigger, no second burst | a second burst 1.06 ms after the trigger | nothing to map: the same levels within a millisecond | -- |
| the wave note-on's `NR31` write, the refresh's `NR11` rewrite | none | written, the length bit off / the same duty | nothing to map: no effect on the sound | -- |
| tempo (the 9.3.9 "inaccurate sequencer tempo" fix) | 15.333 ms a tick at 163 BPM | 15.313 ms | nothing to map: both inside the interrupt jitter §160 leaves unmodelled, and the tempo words are byte for byte the same | -- |
| 9.4.2's "noise table transpose not reset on a new note", 9.2.K's "kit F reset the amplitude" | not seen on the probes (`X92_NOI_tsp2`-`tsp4`, `X92_KIT_F01`, `_half`) | -- | nothing | -- |

## Formats 0 - 15: LSDj 3.1.5 - 8.8.6

Measured in the second campaign on the ROMs of that time (`docs/LSDJ_VERSIONS.md` §3, §5); the
models stay in the code as the record. None was re-probed against 9.4.2 in the third campaign,
so a difference 9.4.2's driver has since corrected may hide behind one of these rows.

| what | older | 9.4.2 | mapped at import | model field |
|---|---|---|---|---|
| the command letters | no `B` before 8.4.0 (every letter from `C` up one code), no `Z` in 3.1 | the 9.x table | the letter table of the version | `commandLetters` |
| the envelope | the chip's own NRx2 (formats 0-10); three stages the chip ramps between (11); three stages ramped in software (15) | the 358 Hz countdown machine (§164) | `EnvelopeLaw::Chip` / `HardwareStages` / `SoftwareStages`; the stages become ChipBoy's shaped envelope | `envelopeLaw`, `envPeriods` |
| the noise note | `~SHAPE + 16 × (5 − octave)`, saturating (0-11); `FF − note` (15) | the musical map (§156) | `NoiseRule::Shape` / `Raw`; the note is the ChipBoy note with the same LFSR clock | `noiseRule`, `noiseMap`, `noiseLo/Hi` |
| `S` on noise | nibbles subtracted from NR43's, modulo 16 | semitones through the map | `NoiseS::Nibbles`: resolved to the note it lands on | `noiseS` |
| `P` and `L` | period-register units a clock (0-3) | semitones (§169) | `PitchLaw::Register`: the byte becomes register units (`pitchRegisterUnits`) | `pitchLaw` |
| `V` | a one-sided triangle **below** the note, register units (format 0) | centred, semitones | `VibratoLaw::RegisterOneSided`: speed and depth recomputed, with an import note | `vibratoLaw` |
| `C` and `V` on noise | ignored (`C` before 5.7.8, `V` before 9.0) | applied | dropped, with an import note | `noiseChord`, `noiseVibrato` |
| `R x y`'s interval | every **y + 1** ticks before 9.2; `R x 0` every tick between 4.8.0 and 8.8.0 | every `y` ticks, `R x 0` once | `y + 1`; `R x 0` becomes `R x 1` where it ran every tick; sixteen cannot be carried and becomes fifteen with a note | `retrigPlus`, `retrigZeroOnce` |
| `T` bytes 0-39 | 40 BPM (clamped) before format 11 | 256-295 BPM | the version's reading | `tempoLowIsHigh` |
| a cell with a blank instrument column | sounds, with the channel's last instrument (before 4.0.4) | moves the pitch without a trigger (a bare note) | `bareNoteSounds`: the cell keeps its blank column | `bareNoteSounds` |
| the wave frame run | none before 6.8.2: frame 0 held; bytes 9-11 mean other things | a run of the synth's frames | `waveFrameRun = false`; the synth and REPEAT bytes read from where the version keeps them | `waveFrameRun`, `waveByte`, `waveRepeatByte`, `waveFineTuneByte` |
| a table's `G` | holds the groove's first step before 9 | walks the groove | the version's reading | `tableGrooveWalks` |
| a table `ENV` hop | costs a tick before 8.9.3 | costs none | the version's reading | `envHopCostsTick` |
| noise `PITCH` (byte 2) | `S MODE` on formats 2-11: holds the LFSR width through an `S` | `PITCH` = FREE / SAFE (§86) | **not mapped**: ChipBoy has no width clamp on `S`; an import note names the instrument | `noisePitchByte` |
| the `DIST` pages | `D1` the mirror, `D2` the steep mirror (before 9.2) | `D1` the soft clip, `D2` the mirror | the version's list | `kitDist` |
| `E` on the wave channel outside 0-3 (6.4.5 - 8.5.1) | a nonsense NR32 | clamped to the four levels | clamped, noted | -- |
| the vibrato of 5.7.8; `M` on 4.6.9; the noise table transpose before 4.0.4 | each its own law | -- | **not mapped** (`docs/LSDJ_VERSIONS.md` §5) | -- |

## Adding a version

Put its ROM in `/root/lsdj/roms/`, run `/root/lsdj/probe/vs_versions.py lsdjX_Y_Z lsdj9_4_2`
(every matrix case on both ROMs), then `vv_all.py lsdjX_Y_Z lsdj9_4_2` for the list without
the length-register noise, then the changelog's candidates as cases; each real difference gets a
row here, a model field, and a value translation in `LsdjSong.cpp` where one exists.
