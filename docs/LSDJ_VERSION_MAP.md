# The version map: what every LSDj version does differently, and what the importer does about it

LSDj 9.4.2 is ChipBoy's reference: the driver plays 9.4.2's way, measured register by register
(`docs/COMMANDS_AND_TEMPO.md` §147-§186). Every older version was probed on its own ROM against
9.4.2 and against ChipBoy's import of the same song with that ROM beside the save (§188-§208),
so a song saved by any version comes in playing the way that version played it, as far as the
rows below reach. The numbered section after each row is the measurement.

How the version is found: the save's format byte picks the model; a ROM beside the save refines
it by its version string (`lsdjModelForRomVersion`); a save alone takes the newest model that
reads its format. Where the sweep found two versions of one format reading a song differently,
the ROM is the only thing that tells them apart -- keep the ROM beside the save.

## The models

| model (versions) | format | measured on |
|---|---|---|
| 9.4.0 - 9.4.2 | 22 | 9.4.2 |
| 9.3.4 - 9.3.9 | 22 | 9.3.9 |
| 9.2.J - 9.3.3 | 22 | 9.2.J, 9.2.L |
| 8.8.6 | 15 | 8.8.6 (the second campaign) |
| 8.4.0 - 8.5.1 | 11 - 14 | 8.5.1 |
| 7.7.6 - 8.0.0 | 10 | none in the archive: 8.5.1's laws with 7.x's letters |
| 7.5.4 - 7.7.5 | 9 | none in the archive: 7.0.2's laws |
| 6.8.2 - 7.2.3 | 7 - 8 | 6.8.2, 7.0.2 |
| 6.0.1 - 6.4.5 | 4 - 6 | 6.0.1, 6.4.5 |
| 5.8.8 - 5.9.9 | 4 | 5.8.8, 5.9.9 |
| 5.7.8 | 4 | 5.7.8 |
| 5.0.3 - 5.7.7 | 3 | 5.0.3 |
| 4.8.0 - 4.9.4 | 3 | 4.8.0, 4.9.4 |
| 4.7.3 - 4.7.9 | 3 | 4.7.3 |
| 4.4.0 - 4.6.9 | 3 | 4.4.0, 4.5.4, 4.6.0, 4.6.2, 4.6.9 |
| 4.1.0 - 4.3.0 | 2 | 4.1.0, 4.3.0 |
| 4.0.4 | 2 | 4.0.4 |
| 3.7.5 - 3.9.2 | 2 | 3.7.5, 3.8.7, 3.8.9, 3.9.2 |
| 3.6.5 - 3.7.4 | 2 | 3.6.8 |
| 3.1.5 - 3.5.1 | 0 - 1 | 3.1.5, 3.1.9, 3.4.4, 3.5.1 |

A save of format 4 without a ROM reads as 6.0.1 - 6.4.5, of format 3 as 5.0.3, of format 2 as
4.1.0 - 4.3.0: the newest of each. The versions between the measured ones (5.1 - 5.7.7,
4.9.5 - 5.0.2, 7.7.6 - 8.4.3, 8.6 - 9.1) take the nearest model below and are marked where
that is a guess.

## Commands

| what | LSDj versions | what that LSDj does | what the import does | field, section |
|---|---|---|---|---|
| the command letters | before 8.4.0; 3.1 | no `B` (every letter from `C` is one code lower); 3.1 has no `Z` either | reads the letters with the version's table | `commandLetters` |
| `R x y`'s interval | before 9.2 | every `y + 1` ticks; `R x 0` retriggers every tick between 4.8.0 and 8.8.0 | `y + 1`; `R x 0` becomes `R x 1` where it ran every tick; sixteen cannot be carried and becomes fifteen with a note | `retrigPlus`, `retrigZeroOnce` |
| a roll restarting the instrument's table | 3.1.5 - 8.3.3 | the table starts over on the tick **after** the retrigger; the immediate fire at the note-on the same | measured and left (§209, by decision): every project takes 8.3.4's timing, the row after a roll a tick earlier than the old ROMs' | §202, §209 |
| `R` on a DRUM instrument | before 9.4.0 | the pitch word runs on through the roll: a rolled kick keeps falling | the instrument's `retrigKeepsPitch`, the Instrument tab's *R on DRUM pitch* (Keeps); the bytes stay | §185, §195 |
| `E` | before 8.8 | re-attacks the note (a trigger with the new envelope) | the instrument's `envRetrig` | §59 |
| `F` on the pulses | before 5.0.3 / 5.0.3 / from 5.7.8 | nothing / `y` period units down / `y`/32 of a semitone down | dropped with a note / `fineUnits` on the voice under the register law / ChipBoy's own F | `fineCmdLaw`, §204 |
| `P` and `L` | 3.1.5 - 5.0.3 | period-register units an instant | the instrument's `pitchRegisterUnits`: the driver moves that many units; a note's `L` slides from the period the channel had, the trigger carrying it | `pitchLaw`, §88, §206 |
| `P` on noise | before 5.4.4 | nothing | dropped with a note | `noiseP`, §205 |
| `C` on noise, `V` on noise | before 5.4.3 / before 9.0 | nothing | dropped with a note | `noiseChord`, `noiseVibrato` |
| `S` on noise | formats 0 - 15 | each nibble of NR43 less the command's, once | the byte as it stands; the driver's Register domain takes it off the byte | `noiseS`, §66 |
| the LFSR width bit through `S` | before 4.1.0 / from 4.1.0 | crossed like any bit / kept from the note when byte 2 (S MODE) is nonzero | the instrument's `noiseStable`, from the rule and the byte | `noiseStableRule`, §188, §207 |
| `T` bytes 0-39 | before format 11 | 40 BPM | the version's reading (256 - 295 after) | `tempoLowIsHigh` |
| `W` on a wave instrument | all | the run: `x` ticks a frame, `y + 1` frames spread across the synth | ChipBoy's `U`; dropped with a note on a MANUAL instrument, MANUAL read by the version's PLAY encoding | §115, §201 |
| `V`'s depth | 5.8.8 - 7.7.5 / 5.7.8 / 3.7.5 - 5.0.3 / 3.6.5 - 3.7.4 | a shallower ladder (`1 2 3 4 6 8 11 15 19 24 29 35 42 49 56 64`), the downward half a little short / half again (`0 1 2 3 4 5 7 9 11 13 16 19 22 25 28 31`) / period units: those integers times the note's divider in sixty-fourths, the downward half a thirty-second short / the same, symmetric | the instrument's `vibScale` (§210): *V depth* ½x on 5.7.8, 1x elsewhere -- the ladders are within a step of 9.x's and the unit laws within 0.8 - 1.3 of its semitones; the register-law swing is 9.x's semitones in the note's own units | §203, §210 |
| `V 00` | 5.8.8 - 6.4.5 and from 9.1.0 / elsewhere | starts the slowest vibrato when none runs / nothing | matched (§151) | -- |
| a cell with a blank instrument column | before 4.0.4 | sounds, with the channel's last instrument | the cell keeps its blank column and triggers | `bareNoteSounds`, §101 |
| a table's `G` | before 9 | holds the groove's first step | the version's reading | `tableGrooveWalks`, §63 |
| a table `ENV` hop | before 8.9.3 | costs a tick | the version's reading | `envHopCostsTick`, §64 |

## Instruments

| what | LSDj versions | what that LSDj does | what the import does | field, section |
|---|---|---|---|---|
| the envelope | formats 0 - 10 / 11 / 15 and up | the chip's own NRx2 / NRx2 plus two stages the chip hands over between, each written with a retrigger / stages ramped in software | ChipBoy's Chip envelope, stepped at the chip's rate with zombie writes / that plus `envStage2`, `envStage3` / the shaped envelope | `envelopeLaw`, §51, §58, §189 |
| the pulse FINETUNE | before 3.6.5 / 3.6.8 - 5.0.3 / 5.7.8 - 8.5.1 / from 8.8.6 | none / a nibble of period units / a nibble of `v`/32 semitone / byte 11 | none / `min(255, 42.2 v)` in 1/256 semitone (a semitone at most) / `8 v` / the byte | `fineTuneNibble`, `fineTuneUnits`, §191, §196, §208 |
| any instrument on any channel | all | a WAV instrument placed on PU1 plays as a pulse reading the same bytes | every imported instrument carries its format and sixteen bytes; a channel of another kind reads them as the ROM would | `lsdjFormat`, `lsdjBytes`, §197 |
| the PU2 transpose | all | instrument byte 2 | the instrument's `pu2Transpose` | §49 |
| a wave `E` outside 0 - 3 | 6.4.5 - 8.5.1 | a nonsense NR32 | clamped to the four levels, noted | -- |

## Noise

| what | LSDj versions | what that LSDj does | what the import does | field, section |
|---|---|---|---|---|
| the note | formats 0 - 14 / 15 / 22 | each nibble of SHAPE complemented, the high one raised by `3 - octave`, saturating / `FF - note` / the musical map | the instrument's *LSDj shape* pitch mode (`noiseShapeMode`, `noiseShape`) / `Raw` / the map | `noiseRule`, §188 |
| the table's transpose column | 3.x / 4.0.4 - 8.x / 9.x | subtracted from the running byte nibble by nibble as each row plays, adding up / a byte off the note's, rewritten when it changes, dropping the S and P delta / semitones through the map | the instrument's `noiseTspNibbles` (*Table TSP: Adds up*, §213) / the driver's shape mode / the map | §188, §207 |
| a chain transpose on noise | before 3.6.5 | nothing | dropped with a note | `noiseChainTsp`, §207 |
| `P` on noise | 5.4.4 - 8.x | each nibble less the byte's every tick (`P 02`: `1E 1C 1A ...`) | the Register domain steps it | §188 |
| `C` on noise | 5.4.3 - 8.x | two states, the note and the note less the whole byte nibble-wise | the shape mode alternates them | §188 |

## Wave

| what | LSDj versions | what that LSDj does | what the import does | field, section |
|---|---|---|---|---|
| the frame run | before format 7 | no LENGTH or SPEED: one frame at a tick a step; byte 9's low two bits are PLAY (ONCE 0, LOOP 1, PINGPONG 2, MANUAL 3); a W lengthens the run; ONCE plays the frame a tick and turns the DAC off | a one-frame run with the loop counted from its end (`frameLoopFromEnd`, *Loop counts: from end*, §211), PLAY read as above, ONCE as a one-tick note | `waveFrameRun`, §200, §201, §211 |
| the loop nibble | before 6.0.1 | not read: the loop is the last frame | tail 1 | `waveRepeatNibble`, §205 |
| PLAY and REPEAT | formats 7 - 9 | PLAY ONCE 0 / LOOP 1 / PINGPONG 2 / MANUAL 3; REPEAT the loop's steps less one from the run's end | read so; the loop stays at the run's end under a W | `wavePlayOld`, `waveRepeatCount`, §198 |
| the synth, REPEAT and FINETUNE bytes | by format | different bytes | read from where the version keeps them | `waveByte`, `waveRepeatByte`, `waveFineTuneByte`, §60, §93, §170 |

## Kits

| what | LSDj versions | what that LSDj does | what the import does | field, section |
|---|---|---|---|---|
| the `DIST` list | before 9.2 | `D1` the mirror, `D2` the steep mirror | the version's list | `kitDist`, §117 |
| a `DIST` page outside `D0` - `D3` | all | the byte names a memory page, so the kit's samples pass through whatever sits there (`UNMASKED`'s `8E`: the font tiles) | the page is read from the ROM at the version's offset into the kit's Custom table (only pages a song uses); the Kits tab edits, randomises and loads such a table, with the LCD holes | §192, §194 |
| kit numbers | all | the k-th kit bank in ROM order, empty banks skipped | the same (§172 had `k + 8`) | §193 |
| a kit's `P` | all | four units an instant and one more step a tick | the same | §190 |
| a kit's `V` | before 9.4.0 | twice 9.4.2's depth | the instrument's `vibScale` at 2x, the Instrument tab's *V depth* | `kitVibratoHalved`, §187, §210 |
| the wave RAM write (a kit's frames, a wave's) | 3.1.5 - 4.6.9 / 4.7.3 - 8.5.1 / 9.x | `NR30 = 00`, the bytes, `NR30 = 80`, `NR34` with the trigger, `NR33` / the same inside an `NR51` mute / the mute, a `$7E0` pre-trigger, the period after | the instrument's `waveWrite` (*RAM writes*: Plain / Muted / Pre-trigger); 8.8.6 taken as muted | `waveWrite`, §215 |

## Differences measured and left alone

Each is a millisecond or a period unit; the probe names are `vs_sweep.py`'s.

- Timing inside a tick: an `E` re-attack or an `R`'s immediate fire lands in the note-on's own
  millisecond (`E38_*`, `R03_*`); a finetune or a table's first column arrives one or two
  milliseconds after the trigger (`F03_*`, `FTb7_*`, `env_tbl`); a table row and the roll's
  trigger sit two milliseconds apart before 8.5.1 where 9.x writes them together (`Rtbl6_R03`,
  `Rtick_tbl`); a kit's NR32 and its trigger swap order (`KIT_*`); a slide's first step is one
  update ahead (`L03_second_ch0`); a noise table's wrap writes the transpose a millisecond
  before the P step (`NOI_P02_tbl`); the wave channel's immediate `R` (`R03_ch2`).
- The chip envelope of formats 0 - 11 is stepped in software at the chip's own rate: the same
  levels within two milliseconds (`PU_env62`, `PU_adsr_*`, `NOI_env`).
- The vibrato's fine steps: the older ROMs' pitch rounding differs from 9.x's by one period unit
  on some updates -- 8.5.1 rounds toward zero, 5.8.8 - 7.0.2 land one unit lower at a few
  points (`VLo_*`, `V4*_pu`); the kit's `V` on 6.0.1 is two units shallower downward
  (`KIT_V42`).
- The pitch table before 5.7.8 has six notes one or two units lower (`PU_all*`: notes 02, 03,
  08, 0A, 19, 3E), three cents at most.
- 4.7.3's `P` takes its first step in the note's own trigger (`P02_ch0`).
- 5.0.3's wave note-on triggers twice inside a millisecond, its `W12`'s third frame comes a
  tick late, and its noise `R` has no immediate fire.
- `B` is random on both sides (`B08_*`); the probe songs' end: LSDj loops a one-chain song
  where ChipBoy's plays it once (`NOI_all*`, `PU_all*`; a question, below).

## Gaps: for a decision

1. **Silence by turning the DAC off.** `K` writes `NRx2 = 00` through 8.5.1 and a wave ONCE
   run ends with `NR30 = 00` through 7.x (`K02_*`, `K03_tbl`, `WvPlay0`); 8.8 and later walk the
   level to zero, as ChipBoy does. On the hardware the DAC-off step is a pop. Leave: the pop
   is the glitch the later versions removed; a per-instrument "kill by DAC off" would carry it
   if a song wants its pops.
2. **A table's ENV column retriggers the wave channel** on every step through 7.0.2
   (`env_tbl_wav`; 8.5.1 writes NR32 alone as ChipBoy does). A retrigger restarts the wave from
   its first sample, and on a DMG a retrigger while the channel reads can corrupt the wave RAM.
   Leave unless a 6.x/7.x song needs the clicks.
3. **`L` on a channel's first note**, with no period before it: 5.0.3 slides up from period 0
   over seconds, 5.7.8 stays at period 0 (a 64 Hz drone), 5.8.8 - 6.4.5 chirp up from it in four
   milliseconds, 9.4.2 plays the note (`L03_first_ch0`). ChipBoy plays the note. Leave.
4. **The one-sided vibrato of 3.1.5 - 3.5.1** depends on the note (8 units a depth step at C3,
   40 at D#6, `VLo_*` and `V4*_pu`); the import recomputes a centred vibrato with one depth
   (§56). Leave unless a 3.1 - 3.5 song needs it; §203's unit law is the shape it would take.
5. **The vibrato's rounding and the pitch table's six notes** (above): a unit, left.
6. **A kit `L`** runs the period away by `2AA` an instant, wrapping past `7FF` (`KIT_L05`, 9.4.2
   too); the note holds in ChipBoy. Left by decision.
7. **A kit `V FF`** is out of the depth table. Left by decision.
8. **A `DIST` page LSDj draws at run time** (`80`, `81`, `88`, `8D`, `90`, `94`, `98` - `9A`) or
   work RAM changes with what is on screen or playing; the import clips with a note, and the
   Kits tab's Custom table takes whatever a user puts in it (§194).
9. **The song's end** -- settled (§212): each channel plays its chain round again from its own
   row 0 when it meets an empty song step, on its own clock (`songend.py` on 9.4.2 and 6.0.1),
   and ChipBoy does the same (`chainEnd` per channel, a toggle in the chain view's head). An
   `H F F` stops the whole song at its step (§214, `songend_hff.py`; §120 had read it as the
   channel's), and the cell keeps it. What remains: the own transport loops the longest chain
   and starts every channel over together, where the ROM lets them run apart for ever.
10. **Unmeasured ranges.** 7.7.6 - 8.0.0 take 8.5.1's laws (the changelog's 7.8.1 ladder; the
    8.3.4 table fix falls inside it, moot since §209); 7.5.4 - 7.7.5 take 7.0.2's; 8.8.6's
    vibrato ladder and its finetune are taken as 9.x's; 5.1 - 5.7.7 as 5.0.3's; 4.9.5 - 5.0.2
    as 4.9.4's; PU2's `F` under the register law was not probed (the rig's second-channel
    song is wrong). A ROM of any of these settles it in an afternoon.

## Adding a version

Put its ROM in `/root/lsdj/roms/`, run `python3 /root/lsdj/probe/vs_sweep.py trace lsdjX_Y_Z`
(every sweep case on that ROM), `vs_sweep.py show A B TAG` between it and its neighbours, then
`vs_cb.py lsdjX_Y_Z --sweep` (ChipBoy's import with that ROM beside the save) and `--show` on a
case. A real difference gets a design-log section, a model field with a value in
`LsdjModel.cpp`, a translation in `LsdjSong.cpp` or `LsdjInstrument.cpp`, and a row here.
