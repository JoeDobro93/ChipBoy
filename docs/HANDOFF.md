# Handoff

The state of ChipBoy for a fresh session. Read `CLAUDE.md`, this file, then only the
design-log section the change touches. Update this file at the end of every change.

## Map

- `Source/core/`: `Apu` (the chip), `Render` (BLEP, coupling, noise floor, RAW), `Driver`
  (notes, tables, commands, the 358 Hz pitch clock, zombie-mode levels, envelopes, the
  Clock), `Tracker` (Song, Player, recorder), `Bank` (instruments, tables, waves, kits,
  grooves, presets, the wave synth), `Link` (the shared-memory region).
- `Source/plugin/`: `main` (ChipBoyProcessor, editor, panels), `voice`, `shared`
  (parameters, JSON, song and preset files), `ui` (widgets, grids, scopes, undo).
- `tools/`: `gate.sh`, `demo` (make_demo.py, make_songs.py), `recordtest`, `linktest`,
  `paramdump`, `uishot`, `fuzz`, `lsdjref` (parity harness, opt-in). `Tests/`: Catch2.
- `docs/`: `COMMANDS_AND_TEMPO.md` (design log §1–§37, binding), `CHIPBOY_SPEC.md`,
  `UI_DESIGN.md`, `HARDWARE_DRIVER_AUDIT.md`, `LSDJ_PARITY.md`, `LICENSING.md`.
  `CHANGES.md`: departures newest first, implementation status. `Demo/`: Reaper
  projects, `.cbsong` files, `PARAMETERS.md`.

## Done (2026-09-09)

- Song format 7 (embeds the bank; converts older), link region v4, 76 host parameters.
- 180 core tests, 11 plugin checks, all green on Linux, warning-free; the link test
  passes under a 1 MB stack. CI on push runs only the L1 check.
- The driver follows LSDj's measured laws (`LSDJ_PARITY.md` §16–§17): 4 harness cases
  identical, 5 within tolerance, the rest listed with reasons.
- Round 6: channels on their own time, zombie-mode levels, shaped envelopes, Instrument
  tab, tracker note gestures, one selector convention, table playhead, wave synth,
  command argument shapes, the hardware audit and the fuzz tool.
- Round 7 (§35–§37): one editing grammar for every value field (click selects, typing
  refuses past the limit and Backspace takes digits back, double-click is the box — the
  item's tab on a slot field — right-click lists with *Open in its tab* first, Shift+←/→ by one and Shift+↑/↓ by
  sixteen — a semitone and an octave on a note); the window remembers its tab, channel
  and each tab's selection (`ui_view` in the plugin state); the Waves tab lays frames
  eight to a row, each morph end has its own shape, the run is placed by From / To
  (`Synth::first`, `bank::synthWriteRun`), the synth is regrouped with a help line per
  chosen shaper, the tools sit under the grid, and the grid has a Points view with the
  pointer's coordinates; the instrument gains a **Chord rate** apart from the command
  rate (default 0 keeps parity; old files take the command rate).
- Round 7, second pass (§38–§44): Enter or a double click on a blank cell fills it from
  the column's memory and a new note brings its instrument; every value cell takes a
  vertical drag; grooves have names (`Groove::name`, `grooveNames` in the song JSON);
  the Waves tab imports a single-cycle file as a frame (`bank::frameFromCycle`,
  `plugin::importWaveCycle`); the lane has 2 px channel dividers and beat bands; MIDI
  and Hybrid channels show their notes dimmed; a MIDI note-off sorts before a note-on at
  one sample (the FL Studio first-note report, unverified here); LSDj 9.3.9 measured — a
  table row and a chord step are both one tick, so the defaults stand.
- Round 8 (§45–§50, D-UI-15–17), from recreating an LSDj 9.3.9 song: the noise channel
  takes the table's transpose column through its map; a cell's TBL lasts until a cell
  names an instrument; a tick-stream jump (locate, loop wrap) no longer sends All Notes
  Off and the step it lands in fires a fraction late instead of never (the DAW loop's
  dropped first note); the chain carries a **transpose** per row and channel
  (`Song::chainTranspose`, `NoteEvent::transpose`, `"chainTransposes"`), gated by the
  instrument's Transpose flag like the table's column; pulse instruments carry
  `pu2Transpose` and F on PU2 sets it; the JSON reader folds a negative P into its byte;
  grid values wrap on a nudge; the Tracker head has **Follow**; the chain is 236 px with a
  TSP cell beside each phrase. Tests: four driver cases, two tracker cases.
- Round 9 (§51–§53, D-UI-18–21): LSDj 9's three-stage envelope measured on the ROM (a
  format-22 probe under `tools/lsdjref`'s save writer, patched to version 0x16) and the
  shaped envelope given a **Start** level and a **Fade** stage (`Envelope::start`,
  `fadeTicks`, `fadeTo`, `fadeCurve`; `envStart`, `envFade`, `envFadeTo`, `envFadeCurve`
  in the JSON); Hex is the default and counts like LSDj (`ValueFormat::slot`, `index`,
  `transpose`; `Stepper::setSlotNumbering` / `setTransposeNumbering`; typed slots from 00,
  typed transposes as bytes); scopes take the NR51 mix word and draw a silenced channel as
  off, the analog trace clamped; the chain has an 8 px gap after each TSP (264 px) and the
  window is 1280 wide. Demo state, parameter table and screenshots regenerated.
- Round 10: **the LSDj importer** (`docs/plan-lsdj-import.md`, §54, D-UI-22).
  `Source/core/Import/`: `LsdjSave` (the file table, the block code, the working song, the
  ROM title), `LsdjModel` (what a version of LSDj means by the bytes: two models, 9.3.9
  measured and a legacy one assumed), `LsdjSong` (the interpreter, from the converter).
  `plugin/shared/LsdjImport` reads the file and sniffs the folder's ROM;
  `LsdjImportDialog` is the popup; *Import .sav…* sits in the Tracker head. Tests build
  their saves in memory (`[lsdj]`); `CHIPBOY_LSDJ_SAV=/path/to/a.sav` makes one case import
  every song of a real save (the user's, outside the tree, holds seven: SUNRISE in format
  22 and six in format 3). `chipboy_recordtest --import-sav SAV NAME|working OUT.cbsong`
  runs the same importer from the command line, so a conversion can be checked with
  `--play-song`; all seven convert and play. The scopes repaint while a silenced channel's last waveform
  slides out of the window, so the visualizer no longer holds a dead shape.
- Round 11: three more ROMs measured (8.4.0, 8.8.6, 9.2.J; all at `/root/lsdj/`, outside
  the tree). Each booted with `lsdjref_trace --init-sav` gives the format it writes:
  **11**, **15**, **22** (9.3.9 also 22). The models are keyed by format now with the LSDj
  versions as labels (`plan-lsdj-import.md` §3): 22 measured on both 9.x ROMs, 15 has the
  three-stage envelope and a raw noise column (`FF − n`), 11 the hardware envelope and an
  octave-only noise map, 0–10 assumed. Formats 12–14 and 16–21 take the nearest model
  below. ROMs that would settle the rest: 8.5–8.7 (12–14), 9.0–9.1 (16–21), and something
  before 8.4 (7.x, 6.x, 5.x, 4.x) to find where `B` and the raw noise column began and
  which version wrote the format-3 songs. The probe saves and traces live in the session's
  scratch (`env/cmp`), not the tree.
- Round 12: **kits import** (`plan-lsdj-import.md` §4a), measured on the user's 9.2.L ROM
  and a save with kit songs (`/root/lsdj/l/`, outside the tree): the kit bank layout, the
  kit instrument's two kits (byte 2 for the note's high digit, byte 9 for the low), their
  lengths (bytes 3, 11), the speed byte as a signed offset on period 1865. `LsdjKits`
  reads the banks; `importSong` takes them; the file side loads the ROM beside the save,
  preferring the one whose version reads the song's format. Not decoded then: offsets, loop
  and half-speed flags, and the DIST modes -- those are §117 now. All eight songs of the kit
  save convert and play.
- Round 13: **every stable LSDj release measured, the older formats imported, S on noise, project
  files** (`COMMANDS_AND_TEMPO.md` §55–§56, `plan-lsdj-import.md` §1a, §3, §4b; CHANGES 2026-09-10).
  The user's archive of 31 stable releases (3.1.5 – 9.4.2) sits unpacked at `/root/lsdj/archive/`
  (container only; `roms/<version>/lsdj_<version>.gb`, the `--init-sav` saves in `init/`, the probe
  saves and traces in `cmp/`, `formats.json` = version → format). The 8.4.4 ROM and save the user
  sent are at `/root/lsdj/mup/` (SPACE TI, GOAL ACH, STARWAY), the eight `.lsdprj` files and their
  9.2.L save at `/root/lsdj/sly/`, decompressed song images at `/root/lsdj/songs/*.bin`, their
  LSDj traces `*.503.csv` / `*.844.csv` and ChipBoy's imports and traces under `/root/lsdj/songs/out/`.
  What was found and built:
  - **Formats**: 0 (3.1.5–3.5.1), 2 (3.6.8–4.3.0), 3 (4.4.0–5.0.3), 4 (5.7.8–6.0.1), 5 (6.4.5),
    7 (6.8.2–7.0.2), 11 (8.4.0–8.5.1), 15 (8.8.6), 22 (9.2.J–9.4.2). Six `LsdjModel`s with new
    fields `noiseRule` (Shape / Raw / Map), `noiseS` (Nibbles / Semitones), `pitchLaw`
    (Register / Semitone), `vibratoLaw`; `lsdjFormatForVersion` holds the measured table; the old
    ROMs' version comes from their welcome line (`romVersion`).
  - **Noise before 9**: NR43 = ~SHAPE (byte 4) + 16 × (5 − octave), saturating; **S on noise**
    subtracts its nibbles from NR43's, mod 16, adding up until the note-on; P on noise does that
    every tick (not mapped). **9.x S on noise** = signed semitones through the map, adding up —
    now ChipBoy's own S on NOI (§55, `Voice::noiseTsp`); the noise map continues to −72 for
    transposes (`Driver::kNoiseMapBelow`); the driver's noise Shift offset is read from the
    instrument (it compounded on a second write before). The importer resolves the old S to
    semitone deltas in chain order (`ChannelState`), folds noise chain transposes into the note
    before 9, and gives each noise slot the Shift that puts LSDj's clocks (16 Hz up) onto ChipBoy's
    keyboard (2 kHz up): `chooseNoiseOffsets`. Cells never go below note 12 (the command octave).
  - **Pitch before 5.7** (formats 0–3): P adds its byte to the period register every pitch clock,
    L is a speed in units a clock, format 0's V is a one-sided register triangle; the importer puts
    those instruments in Drum and converts (`drumSpeedFor`, `gbPeriod`).
  - **Any instrument on any channel** (LSDj plays it as the channel's kind): variants in slots
    65–128 (`usage()`, `slotFor`); notes before any instrument column play LSDj's 00; tables
    imported by content (9.x's alloc bytes miss named tables); a table's `G` is LSDj's groove + 1;
    a noise table's transpose column is subtracted from NR43 byte-wise before 9.
  - **Project files** (`decompressProject`, `readProject`, `readImportFiles`): the chooser takes
    `.sav`, `.lsdprj`, `.lsdsng`, several at once; the dialog lists projects as rows. The eight
    projects equal the save's songs but for LSDj's kit renumbering.
  - **Verification tooling**: `chipboy_recordtest --trace-song FILE OUT.csv [seconds]` writes
    ChipBoy's register writes in the harness CSV; `/root/lsdj/cmp.py LSDJ.csv CHIPBOY.csv [detail]`
    (container only) compares per channel — noise by LFSR clock and 7-bit flag with the note's own
    S merged into the note-on, pulses and wave by pitch sequence. On the nine old songs the noise
    channel now lands on LSDj's clocks (SPACE TI: the S sweeps match; GUUDE's pulse-on-noise plays);
    pulses and wave match where no table/vibrato timing differs (see open issues).
  **The procedure for the next measurement** (the same for any question about an LSDj version):
  1. Boot the ROM once: `build-ref/lsdjref/lsdjref_trace --rom R.gb --bootrom-dir build-ref/lsdjref/BootROMs
     --model dmg --frames 3000 --init-sav init.sav --out /dev/null` — byte `0x7FFF` of `init.sav`
     is the format it writes.
  2. Author a probe with `tools/lsdjref/lsdjref_sav.py --spec X.spec --base build-ref/lsdjref/base.sav
     --out DIR --case NAME` (notes, instruments, tables, `c=S:01`-style commands; it writes the
     letter table **without B**, so for a ROM from 8.4 up patch the command bytes: byte b → index
     of the same letter in `-ABCDEFGHKLMOPRSTVWZ`, in phrases at `0x4000` and tables at
     `0x3680`/`0x3A80`). Patch instrument bytes directly (`0x3080 + 16 i`).
  3. Copy the probe's first `0x7FFF` bytes over `init.sav` and keep `init.sav`'s `0x7FFF`; trace
     with `--sav`, `--keys 180:start`, 700–1500 frames; the CSV is `cycle,addr,name,value`; the
     pitch clock is 11712 cycles; a row at 120 BPM is 44.8 clocks, a tick 7.47.
  4. Read the registers at each NR44/NR14 trigger and between them (the scripts in this round's
     transcript are the pattern: group writes within 0.3 clocks, pair NR13/NR14 by burst).
  5. Put the rule in `LsdjModel` (a field the converter switches on), the conversion in
     `LsdjSong.cpp`, a case in `Tests/LsdjImportTests.cpp` on a synthetic song, the finding in
     §56 and `plan-lsdj-import.md` §3; then import a real song, `--trace-song` it and compare.
- Round 13 (continued): **a table's `G` times its own row, and format 11's envelope is three
  stages** (`COMMANDS_AND_TEMPO.md` §57–§58).
  - §57: a `G` in a table row takes effect at that row on every release traced (3.5.1 to
    9.3.9). ChipBoy applied it a row late, because the Driver kept only the slot and waited
    for the Player's next block to hand the ticks over. It reads `song_->grooves` itself now.
    GOAL ACH's arpeggio matches LSDj step for step and tick for tick after it.
  - §58: song format 11 (8.4.0 – 8.5.1) carries **three envelope stages in bytes 1, 9 and
    10**, like 8.8.6 and 9.x, but the ramp between them is the **chip's** envelope: a level
    every `period / 64` of a second, and each stage hands over when the ramp reaches the
    next stage's amplitude (confirmed over amplitudes 2, 4, 8, 9, 12 and periods 1, 2, 3, 7).
    A stage whose direction points away from the next amplitude never hands over. Formats 0
    to 7 ignore bytes 9 and 10 (traced on 5.0.3 and 7.0.2). `LsdjModel::stagedEnvelope`
    became `EnvelopeLaw` (Chip / HardwareStages / SoftwareStages) and both staged laws land
    on the Shaped envelope; only the milliseconds a level costs differ. This was the largest
    remaining timbre gap: 28 of GOAL ACH's 61 instruments and 31 of SPACE TI's 42 set those
    bytes and every one of them had been playing a flat ramp to the rail.
  - **Two earlier open issues were wrong and are closed**: the note-on carrying a table's
    row 0 is not a version difference (every release writes the plain note and lets row 0
    land 0.4 pitch clocks later, which is what ChipBoy's merge sounds like); and the extra
    noise triggers on the 4.x songs were two analysis scripts using different time origins,
    not a rule. A noise instrument's byte 3 is loaded into NR41 with the length bit clear
    in NR44, so it is inaudible and ignoring it is right.
  - **Verification tools** (container only): `/root/lsdj/notes.py LSDJ.csv CHIPBOY.csv
    [detail]` scores the note-on sequence per channel by semitone (pulse and wave) or LFSR
    clock (noise), in-step and as a longest common run — the honest measure, since vibrato
    and slides move the period between triggers and swamp a raw register diff.
    `/root/lsdj/cmp.py` is the raw-register one. Current state on the nine old songs:
    ASDFIOJA, ISORHYTM and AITU2 exact on all three pitched channels; ASTEROID, GUUDE and
    BIRDS exact within the shorter run; SPACE TI and STARWAY 100/102 and 99/102; GOAL ACH's
    pulses carry LSDj's envelope-stage retriggers, which are not notes, so it scores low and
    was checked by hand instead. All nine, and all eight project files, pass `--play-song`
    (ISORHYTM fails only on a PU1 that LSDj leaves silent too).
- Round 14: **the table columns, the table groove, and the doubled song transpose**
  (`COMMANDS_AND_TEMPO.md` §62–§63; CHANGES 2026-09-10), all from the user's `S_TN - Мир -
  MUP 8_4_4.sav`, first song SPACE TI.
  - §62: LSDj gives a table's **two command columns their own row pointers** — its own
    changelog says so at v1.3.0B, and the A/Bs confirm it on 8.4.4 and 9.2.L. An `H` in the
    second column loops that column alone. ChipBoy has one pointer, so honouring it stopped
    every arpeggio at the hop; the importer drops it now, with a note. PU2's six-note
    arpeggio in SPACE TI went from `55 58 55 58 …` to LSDj's `54 57 61 66 61 57 54`.
  - §63: before LSDj 9 a table's `G` gives **every** row the groove's *first* step; 9 walks
    the groove. Measured on 8.4.4 and 8.8.6 against 9.2.L, both ways round, with the table
    reached by the instrument's `TBL` and by an `A` so the entry path is ruled out. The
    model gained `tableGrooveWalks`; for the older ones the importer points a table's `G` at
    a **one step groove**, taken from a slot the song never names.
  - The song transpose (§61) was being written into every chain row: `songToVar` used
    `transposeAt()`, which already adds it, so a round trip doubled it. `Song::rowTranspose()`
    is the raw accessor now, and the writer and the chain grid use it.
  - **A real save's working area is the way to measure table behaviour.** `/root/lsdj/work.py
    SAV FILEIDX OUT.sav [addr=val …]` decompresses one song, patches bytes and writes it into
    the save's working area (byte `0x8140` = `0xFF`), so a ROM plays the user's own song with
    one byte changed. `/root/lsdj/dump.py` decompresses and prints a song's tables, grooves,
    phrases and instruments. **The generated probe saves lie about a table's commands**: they
    report a hop from the second column that no real save does, and drop a table's `G`
    entirely — a second artifact of the kind §58 records. 8.8.6 and 9.x will play a
    format-11 working song (they upgrade it in place); the pre-8 ROMs play none of the saves
    in hand, which is why formats 0–7 are unmeasured for §63.
  - The user's copy of LSDj's official **CHANGELOG** is at `/root/lsdj/CHANGELOG.txt`
    (container only; `littlesounddj.com` is blocked by the egress proxy here). It is the
    fastest way to date a behaviour: §62 came out of it in one grep.
- Round 15: **a table's three lanes and the wave instrument's frame run**
  (`COMMANDS_AND_TEMPO.md` §64–§65, `plan-table-lanes-and-wave.md`; CHANGES 2026-09-10).
  - §64: a table's **VOL+LEN, TSP+CMD 1 and CMD 2 each keep their own row pointer**, in
    ChipBoy as in LSDj (whose changelog says so at v1.3.0B). `Driver::stepTable` became
    `stepTableLane(ch, lane)` and the tick counts three waits; `applyCommand` takes the lane
    so an `H` hops only its own. §62's import workaround is gone -- the hop is carried.
  - The **ENV byte's low digit is a duration in ticks**, not a fade speed: `1` holds one tick
    and `E` fourteen, `0` blanks the row (so `A0` is *not* amplitude 10) and `F` hops the
    volume lane to the row the high digit names. `TableStep` gained `volTicks` and `volHop`,
    the table grid a **Len** column that types a number or `H` and a step.
  - §65: the wave instrument's run. **PLAY** is byte 9's low two bits (manual / once / loop /
    ping-pong), **LENGTH** is `16 - (byte 10 & 15)` frames *spread* across the wave's sixteen
    (`frame(i) = min(15, i * 16 / (L - 1))`, exact on every length traced), **SPEED** is byte
    11 and costs `s + 4` ticks a frame, and the synth byte's low nibble is **LOOP POS**, whose
    loop covers the last `16 - LOOP POS` steps of the run. `Instrument` gained `frameLength`
    and `frameLoopStep` and **lost `waveFrame`** -- §60 had read LOOP POS as a start frame,
    and the run always starts at its first. The Instrument tab shows **Frames** and
    **Loop from** where **Start frame** was.
  - Measured with `/root/lsdj/archive/probe/wframe.py`, which decodes each wave RAM load in a
    trace against the song's own frames, and `reg.py` / `wsum.py` beside it. Every one of the
    wave instrument's sixteen bytes was swept; only 9, 10, 11 and the synth byte move anything.
- Round 16: **the free volume hop, a counted phrase H, and the noise sweep domains**
  (`COMMANDS_AND_TEMPO.md` §66; CHANGES 2026-09-10 later).
  - The volume lane's **hop is free** now, LSDj's rule from 8.9.3 (measured on 9.2.L), and an
    older save's hop row gets a **LEN of 1** at import to buy back the tick 8.4.4 spends. The
    engine carries no version, and a 9.x import is exact.
  - A **counted `H` in a phrase** ends it, where it used to be dropped: `H x y` ends the phrase
    x times and then plays it whole (traced on 8.4.4), and ending every time is right in four
    passes of five. 255 of the 283 phrase `H`s in the user's saves are the plain `H00`.
  - §66: **the noise channel's two sweep domains.** Before LSDj 9 both `S` and `P` do the same
    arithmetic on `NR43` -- each nibble less the matching nibble of the value, modulo sixteen,
    no borrow -- `S` once, `P` every tick (all three measured on 8.4.4). On 9.2.L `P` walks the
    map at **value / 4** entries a tick instead. `Instrument::noiseDomain` picks **Notes** or
    **Register**, shown as *Sweep* in the Instrument tab; the importer sets Register for every
    format before 22 and passes the bytes through, so the old "resolved for the loop's first
    pass" and "P on noise is dropped" notes are both gone. The running delta is one byte,
    because nibble-wise sums compose, and it comes off the byte on its way out so it never
    compounds against the pair the note chose.
- Round 17: **SPACE TI, first pass against the real song** (`COMMANDS_AND_TEMPO.md` §67–§69;
  CHANGES 2026-09-10 SPACE TI). The user is reading one song beside LSDj 8.4.4 and reporting a
  few things at a time; this is the first three.
  - §67: **an `E` runs the envelope it names.** LSDj writes the command's byte into `NR12`
    (traced on PU1 phrase 82: `1F`, `67`, `1F`). ChipBoy set `envRate`/`envDir` and then never
    ran them, because `stepSoftEnvelope` returned whenever `shapedOn` was set -- and every
    imported format-11/15/22 instrument has a Shaped envelope. One gate changed to
    `shapedOn && !shapedTaken`. It was visible in the trace as sixty-odd `NR12` writes without
    a rate nibble among them.
  - §68: **a table's `L` beside a transpose slides to it.** The wave kick's `TSP C4` + `L20`
    was jumping to the transposed note and sliding from stale state. A table's `L` inside a
    note-on now starts from the note without the table's column.
  - §69: **a `G`'s byte is its slot** as the Grooves tab counts them (§52), so the numbers in
    the command cell, the tab and LSDj all agree.
  - §63's flattening no longer overwrites a groove the song can still see: free slots are
    ranked (LSDj-empty, then the `6 6` default, then a groove never named) and taken from the
    top. It had been clobbering SPACE TI's grooves 02, 03 and 05. The slots it takes are named
    `held 12` and so on.
  - **Still open on SPACE TI**: the table grid draws one playhead where the engine now runs
    three lanes (§64), so the columns look locked together even though they are not. That is
    the next thing the user will see.
- Round 18: **the chip ran the envelope before LSDj 8.8.0** (`COMMANDS_AND_TEMPO.md` §70;
  CHANGES 2026-09-10 SPACE TI). Chasing the rest of the user's item 1 -- the `E` on PU1
  phrase 82 -- turned up that `LSDJ_PARITY.md` §7's "LSDj never lets the chip's envelope run"
  holds only from 8.8.0 on. The changelog dates it (**v8.8.0**, the release that moved the pulse and
  noise channels to software amplitude envelopes) and three traces of real songs prove it: 8.4.4 writes `NR12` 108 times
  with **no** low nibble 8 and rate 7 fifty-five times; the same save under 9.3.9 writes it
  848 times with rates 0 and 1 only; 5.0.3 agrees with 8.4.4.
  - The instrument carries `envChipTiming`, set by the importer from the model's existing
    `EnvelopeLaw`, and the panel shows it as **Env rate** (Soft / Chip). ChipBoy still steps
    the level itself; only the interval changes.
  - The step counter counts 256ths of a pitch clock and **subtracts** the period, so a rate
    whose interval is not a whole number of clocks keeps its average. The software table is
    whole clocks, so it is unchanged to the cycle.
  - Measured after: SPACE TI's PU1 ramps a level every 108.9 ms against the chip's 109.375
    (0.4 % out); §7's table had it at 100.5 ms, 8 % fast, and could not tell rate 6 from 7.
  - **`docs/LSDJ_PARITY.md` §7 and §10 now describe 8.8.0-and-after only.** Anything else in
    that file measured on a 9.x ROM may be version-specific in the same way -- worth
    re-reading before it is trusted for an older import.
  - **Where SPACE TI stands** (note-ons against LSDj 8.4.4, 23.8 s): PU1 67/103, PU2 104/153,
    WAV 198/219, NOI 32/32. PU1 and PU2 are the weak ones.
- Round 19: **the wave kick's slide holds its own aim** (`COMMANDS_AND_TEMPO.md` §71; CHANGES
  2026-09-10 SPACE TI). The user reported the kick still a "high pitched laser" after §68 fixed
  where its slide starts. Two faults, both off the 8.4.4 register log for phrase 17:
  - A table steps every tick, so a slide outlives the row that aimed it. ChipBoy held a slide
    as a residual over the *live* pitch, which reads the table's transpose column, so stepping
    from row 0 (`TSP c4`, -60) to the empty row 1 threw the base up sixty semitones mid-sweep
    -- note 70 to note 132. A slide now holds its own copy of that column and rebases onto the
    note it reaches.
  - The aim has to be a note the channel can sound. `lowestNote()` is new: wave bottoms at note
    24 (period 44), pulse at 36. LSDj divides the distance to the *reachable* note by x + 1;
    ChipBoy divided by the unreachable one, ran a quarter fast, and wrapped into eleven bits
    (period 2040 is -8).
  - Verified in the real song: the kick is LSDj's sweep period for period over all 35 updates,
    six off by one unit from fixed-point rounding, resting on 44 like LSDj.
  - **Method note**: the user has cleared reading the ROM directly rather than only tracing it
    (their project, their call on L3). Register-stream diffing settled this one without it, but
    it is available for constants that resist measurement.
- **The version sweep is done** (`docs/LSDJ_VERSIONS.md`): all 31 stable releases in the user's
  archive plus 8.4.4, 9.2.L and 9.3.9, each probed on its own bootstrapped save. The format map,
  what differs from 9.3.9 per release, how the importer remaps it, what cannot map, and the two
  places where **two releases write the same format byte and still read a song differently**
  (format 2 at 4.0.4, format 3 at 4.8.0). `lsdjModelForRomVersion` now walks a version-keyed
  table so a supplied ROM settles those; `lsdjModelForFormat` gives the format's default.
  The probe scripts are at `/root/lsdj/probe/vs_*.py`. Two traps, both of which gave a confident
  wrong answer first: the wave channel is triggered with a **stale period** (read the pair written
  a few ms later), and a note with **no instrument column does not sound from 4.0.4**, so a probe
  that uses one measures the next pass of the phrase and reads as half the tempo.
- **`docs/plan-lsdj-version-sweep.md` is the next two stages**: validate the command matrix on
  the 9.3.9 ROM, then probe every older version against it. It says what to supply (a ROM is
  enough -- the rig bootstraps its own host save), how to set up in a fresh container, the three
  rig checks to run before trusting a measurement, and the three-way same / value / kind
  decision per command.
- **Every LSDj command is now measured on 9.3.9 twice** -- the first campaign, then an
  independent **stage-1 verification pass** that rebuilt the rig from the ROM alone, re-derived
  each number, and read the ROM's own code wherever the two disagreed. Results in
  `docs/LSDJ_COMMAND_MATRIX.md` §9, ChipBoy's gaps ranked in §10. Of nineteen entries, **twelve
  confirmed and seven corrected**: `B` (the phrase roll is `n/15`, the *table* hop `x/16` -- different laws, so
  `BF0` is not "always"), `E` (`y` is direction + 3-bit rate, the unit is a fixed 2.788 ms, and
  **WAV reads `y` not `x`**), `M` (not the byte into `NR50`: nibbles 8-15 are relative), `S` (the
  per-nibble formula is right but `S` **accumulates** onto the instrument's sweep byte), `T`
  (bytes 0-39 mean 256-295 BPM), `W` (duty is the low **two bits**; on WAV it sets two synth
  variables), and `Z` (the source is the last command **in the same lane**, not the last
  executed). `H` held up and closed a standing open question with it: a phrase `H`'s high nibble
  counts repeats, exactly as a table `H`'s does. Nothing in ChipBoy is fixed yet.
  - **Two entries were wrong about ChipBoy, not about LSDj**: `E`'s `y` split and `W`'s `xy & 3`
    duty mask are already right in the driver. Do not "fix" them.
  - Three new ChipBoy bugs fell out: `E` on WAV takes `c.a` where LSDj takes `y`; `masterFromArg`
    maps `M`'s nibbles 12-15 to 0/−1/−2/−3 where LSDj uses −4/−3/−2/−1; a counted phrase `H`
    cannot be expressed at all.
  - **Read the ROM early.** Four of the corrections were invisible to any sweep of values
    (`S` until you run two; `M` until a nibble exceeds 7; `T` until the byte drops below 40; `Z`
    until the two lanes disagree). **Both tools are now in the tree**:
    `tools/lsdjref/lsdjref_dis.py` disassembles SM83 at a bank and address, and `lsdjref_pc`
    is the trace tool plus the PC and ROM bank of every write, with `--watch ADDR` to follow a
    work-RAM address. `tools/lsdjref/README.md` has the loop that works and a table of where
    every handler on a 9.x ROM lives -- the command jump table at bank 02:`$47A2`, dispatched
    from `$478D`, with `B` `D` `G` `H` `Z` handled at the row reader instead.
  - **Sections 72-82 put the findings into the code.** `S` accumulates onto a running sweep
    byte; `B` exists in both its forms; `Z` re-runs its own lane; `M`'s down half, `R`'s
    interval and one-shot, `C` and `V` on noise, `F` on both pulses, `E` on WAV all match the
    ROM; the importer converts `T`'s low bytes and masks `W` to two bits; and an imported noise
    instrument plays **LSDj's own note map** off the bank (§81) instead of crossing into
    ChipBoy's nearest-clock one and back.
  - **The acceptance test is the user's SUNRISE** (`/root/lsdj/lsdj9_3_9.sav`, song 6, format
    22). ROM against ChipBoy over the whole song (113 s, `--frames 7000`), comparing the period
    or `NR43` each trigger sounds at: **PU1 276/276, PU2 170/170 and NOI 740/740 with zero values
    differing**; WAV 440/440 note-ons with the swept drums starting about 1.3 semitones high for
    one pitch update and identical thereafter. On noise the whole **register stream** matches,
    not only the triggers: `NR41`, `NR43` and `NR44` byte for byte in the ROM's own order.
    That last one is §10.1 of the matrix: the table's `P CF` bend, whose *phase at the note-on*
    is not measured -- the ROM's first period is half a bend step below the plain note.
    Rebuild the comparison with `chipboy_recordtest --import-sav` then `--trace-song`, and
    `lsdjref_trace` on a save whose working song is the one under test; `/root/lsdj/probe/cmp.py`
    lines the two note streams up. Compare the value **at** the trigger on noise and a few
    milliseconds after it on the pitched channels, because LSDj triggers the wave channel with a
    stale period and writes the real one immediately after.
  - **Section 85**: on the **noise channel** the Note column shows the entry number in the grid's
    own base, because §83 made that note an index into a clock map rather than a pitch. LSDj's
    phrase screen counts it **from zero** (measured by scripting the joypad to the phrase screen
    and reading the LCD -- `lsdjref_trace --screen FILE.pgm` and `/root/lsdj/probe/ocr.py`), so
    ChipBoy prints `note - 1` and the two read the same. The entry box takes that number back and
    still takes a note name.
  - **Sections 86 and 87 finished the noise channel** (round 21, and they are what the user heard
    as "the noise sounds way off"): the noise instrument's **byte 2 is `PITCH`** -- 0 is `FREE`
    (restart only when a pitch change turns the 7-bit LFSR on, which is what §82 measured) and
    anything else is `SAFE` (restart on every pitch change); a restart **re-arms `NRx2` at the
    level the note has reached** and writes `NR44 = BF`, so the envelope carries on instead of
    jumping back to the instrument's own level; and byte 3's **`LENGTH` is latent** -- it reaches
    `NR41` but the note-on never sets `NR44`'s enable bit, so only a restart makes it cut. Found
    by sweeping the instrument's bytes one at a time against a table that walked the transpose
    across the map's width boundary (`/root/lsdj/probe/pitchmode.py`, `pm3.py`, `noiselen.py`).
  - **Sections 83 and 84** finished the noise: LSDj's table is **120 entries** (note byte 1-120)
    and its index **wraps** modulo 120, measured in both directions, so a mapped noise
    instrument's cell carries **LSDj's own note byte** (entry 0 at note 1) and the driver
    wraps rather than clamps. Because entries 0-10 would then fall in ChipBoy's command octave, a
    bank carrying LSDj's map has **no command octave on the noise channel**. The table entry's own **width bit** is the note, not a property of
    the instrument, which is what had been turning the table's 7-bit half into its 15-bit one.
    And a note-on triggers at the **plain** note: the table's transpose column reaches the
    channel on the next pitch update, which for noise means the channel has to join the pitch
    clock whenever a table runs.
- **`tools/lsdjref/probe_fmt22.py` + `run.py`** build and trace a controlled probe song inside a
  format-22 save -- a real one, or one the ROM formatted itself (`--init-sav`, 3000 frames, then
  `Probe(host=..., blank=True)`), which is what the verification pass used. Start from it for any
  new LSDj question; run all three rig checks (a plain note is one `NR12 = F8`; two notes eight
  rows apart at 128 BPM are 0.9375 s apart; `S23` on PU1 writes `NR10 = ED`) before trusting a
  result.
  - **`run.py` had a real bug and it is fixed.** `events()` skipped `180 * 70224` cycles to get
    past the `START` key, but a frame is not 70224 cycles while the LCD is off through LSDj's
    boot: the skip landed 36 ms late, past the song's own first note, anchoring on the second
    pass of a looping phrase. `run.playStart()` now anchors on LSDj's playback reset (the one
    `NR52 = 00` → `NR52 = 80` after the boot chime) and `run.at_start()` gives times from it.
    **There is no "START blip"** -- that warning was this bug seen from the other end, the note
    one phrase-length (1.875 s at 128 BPM) earlier looking spurious. Between the key and the
    first note LSDj only power-cycles the APU, resets `DIV`, ramps `NR51` and sets `NR50`.
- **`docs/LSDJ_COMMAND_MATRIX.md`** is the working reference for LSDj parity: every command
  as LSDj 9.3.9 handles it, what differs per channel and between a phrase and a table, what
  ChipBoy does now, whether the importer can bridge the gap, how to probe another ROM version
  against 9.3.9, and how to flag what will not map. Start there before touching a command.
- **Adding an LSDj version** when the user supplies its ROM (the steps also head
  `Source/core/Import/LsdjModel.h`): put the ROM beside the others outside the tree
  (`/root/lsdj/` here), copy the 9.3.9 entry in `LsdjModel.cpp`, set the format it writes
  (load a song on it, read byte `0x7FFF` of the working song) and the range it reads, then
  trace with the harness (`tools/lsdjref`, `--rom` naming that ROM) the tables that may
  differ and point the entry at them: the command byte table (a phrase with every letter,
  which register each byte moves — 9.x inserted `B` at 2), the noise map (every note on a
  noise instrument, read NR43), the envelope (§51's probe: one note, the speed patched
  1–F, the NR42 step periods; and whether byte 1 is NRx2 or the first of three stages), the
  wave octave (§45), PU2 TSP (§49), and P's and V's tick tables (§7) if a version differs
  there — those still use the driver's tables for every model. Set `measured` when the ROM
  traced it. The harness's `lsdjref_sav.py` writes version byte 0: give it the version
  under test, or LSDj reads the probe with the legacy rules — the quickest way is to copy
  the probe's 32 KB over the ROM's own `--init-sav` save and keep that save's byte `0x7FFF`.
  Add a `[lsdj]` case that reads a synthetic song under the new model.
- The parity harness runs here now: the ROM sits at `/root/lsdj/lsdj9_3_9.gb` (container only),
  `build-ref/` holds the build. **RGBDS is not packaged for this image and building it from
  source is unnecessary** -- the prebuilt Linux tarball from `gbdev/rgbds`' releases works, and
  SameBoy's boot ROMs assemble cleanly under 0.8.0. Put it on `PATH` before `cmake` configures,
  or the harness silently reports "no boot ROMs" and every test skips.
  `f_table_speed` is a new case; only it and `i_kill_delay_chord` were traced on 9.3.9.
- The LSDj ROM is the user's own, at `/root/lsdj/lsdj9_2_J.gb` on the build container
  only; `*.gb`/`*.sav` are git-ignored.

- Round 35 (§121-§123, correcting §113): the four envelope warnings and the user's phrase 14.
  **§121**: a shaped envelope's stages carry a **fraction of a tick** (`attackFine` and friends, 1/256
  each) -- `CLAP`'s three stages are 1.729, 2.305 and 0.576 ticks, held for 2, 2 and 1 before -- and
  the note's own tick no longer advances the envelope, so the start level gets its period as the ROM
  gives it. **§122**: `STEP` governs only the table the instrument names; a table an `A` starts runs
  one row a **tick** and fires its row 0 on the tick after the row that started it. That is phrase
  14: instrument 02's table `11` starts table `02`, whose `E` rows walk `NR32` 50% <-> 100%, the fade
  the user heard -- now inside 3 ms of the ROM over a second. **§123**: `Z`'s last-command record
  outlives the note-on (and a different instrument), so phrase 14's `Z 3F` rows re-run its `W 20`
  instead of doing nothing; the random is per nibble, re-confirmed against the ROM. `SAMESONG`'s
  import notes: 9 to 2.
- Round 34 (§120): **`H F F` ends the channel's timeline**. The ROM stops the channel outright and
  ChipBoy let the chain carry on, so `SAMESONG` played a whole channel the ROM had switched off.
  The importer now stops adding rows to that channel at the chain step whose phrase holds the
  `H F F`; a host playhead past it finds no row and nothing plays. What it does not carry is the
  loop -- the ROM's channel stays off until playback stops, where ChipBoy's rows before the stop
  play again on the next pass, which the import note says.
- Round 33 (§119): **`V` on the noise channel**. A mismatched instrument type needed nothing --
  measured on 9.2.L, a pulse instrument on `NOI` writes exactly what a noise one with the same
  bytes writes, and ChipBoy's `NOI` variant already matched. What made `SAMESONG`'s phrase 47 wrong
  was the vibrato: it moves **once a tick** (ChipBoy ran it on the pitch clock, seven times too
  fast), its phase is the **Tick table's** whatever the instrument's `PITCH`, and its depth is in
  **map entries** -- `kVibDepth256[y] / 32`, eight per semitone -- floored, so depth 0 still moves
  the index by one. 81 of 96 swept settings now match the ROM byte for byte.
- Round 32 (§118, `plan-kit-pairs.md`): a kit note **keeps its pair**. The ChipBoy kit holds the
  source samples, a cell's **note** column names the first and its **VEL** column the second by
  index + 1, and the kit's new **`Dist`** (D-UI-28) says how the driver sums them -- the same five
  curves as §117, live rather than baked at import. The importer maps a note byte's two digits
  straight onto the pair, so `AIR`+`AIR` is one entry named twice. VEL means one thing at a time:
  the keyswitch velocity mode skips a kit. The Kits tab also gained **Audition** (D-UI-29), which
  plays the selected sample beside the song without touching the driver.
- Round 31 (§117): the kit **`DIST` modes**, read off the ROM rather than guessed. Byte 10 of a
  kit instrument is the **page of the mixing table** LSDj looks the two samples up in -- `D0` to
  `D3` -- and each of the four tables is a function of the two nibbles' sum, so each is a curve.
  All four are in `Source/core/Import/LsdjKitDist.h` in closed form, checked entry for entry
  against the 47 ROMs in the archive and end to end against the ROM's streamed wave RAM. The list
  moved at 9.2 (`CLIP/SHAPE/SHAP2/WRAP` became `HARD/SOFT/FOLD/WRAP`), so the model carries the
  pair. The old sum had no floor, so a quiet passage wrapped round into noise; that is gone.
- Round 30 (§116): **the shaped envelope steps on the pitch clock**, not once a tracker tick.
  `SAMESONG`'s `CLAP` fades four levels in one tick and ChipBoy emitted one jump; the ROM walks every
  level. The stages stay whole ticks and the position becomes `shapedTick * 256 + sub`, `sub` being
  how far this tick's pitch clocks have got, held monotonic so the level never steps back at a tick
  boundary. `clocksPerTick_` is measured, so it follows the tempo. Notes: 17 to 13. What is left is
  the stage *lengths*, which `envTicks` rounds to whole ticks.
- Round 29 (§115): **`A 20` stops the table** -- measured, and ChipBoy's cell TBL column cannot say
  it, so the importer emits `A 0` into a command slot instead of dropping thirteen of them. And
  **`W` on a wave instrument is the run**: x ticks a frame, y + 1 frames, y = 0 all sixteen, both
  swept. ChipBoy's `W` on that channel is the wave slot and songs already use it so, so the run has
  its own letter **`U`**, kept out of the command parameter list so the 76-parameter table stays put.
  `SAMESONG`'s notes: 51 to 17.
  Still open with numbers: `waveRun()`'s spread is one frame out at lengths 3 and 7 (the ROM visits
  `0 7 15` and `0 2 5 7 10 13 15`); neither `i*15/(L-1)` nor `i*16/(L-1)` fits every length.
- Round 28 (§113, §114): **a table a table starts fires its row 0 at once**, and **every vibrato
  shape is centred**. `SAMESONG`'s instrument 02 was flat: it runs a STEP-mode table (byte 5 bit 3 --
  one row per trigger, swept and confirmed) whose row 0 holds `A 02`, and ChipBoy left the new
  table's row 0 for a next tick that Step mode never brings. Then the vibrato it started swung half
  as far, because ChipBoy's saw and square ran `0 .. +1` where the ROM swings the full depth either
  side; byte 5 bit 0 picks which half comes first, not a direction, and shape 3 is off, which the
  importer had clamped to Square.
- Round 27 (§112, D-UI-26): **the pulse instrument gains Finetune.** LSDj's byte 11 is a detune of
  `byte / 256` of a semitone, **down on PU1 and up on PU2** -- the two pulses beat against each other
  -- applied on the first pitch update rather than at the trigger, which is why a trigger-only
  reading showed nothing and the byte looked inert. An `F` on the cell replaces it. `Instrument::
  fineTune` is new, a note-on seeds the voice from it, the Instrument tab has a stepper and the
  importer sets it instead of warning. `SAMESONG`'s import notes: 57 to 51.
- Round 26 (§111, correcting §110): **`L` on a note that carries its own instrument.** §110 had
  subtracted the *live* table column from a cell `L`'s source, which is zero at a note-on because the
  new instrument's table has already restarted on row 0 -- so `SAMESONG`'s phrase 23, whose `L`s ride
  on note-ons, still bent an octave high and downward. The voice now records how much of each pitch
  it writes is transpose (`pitchNowTspFine`) and the slide subtracts that. **§111**: the update an
  `L` fires on keeps the pitch the channel was already on, column and all, and the slide starts on
  the next one -- the ROM *triggers* on that value at a note-on. The column to hold is the one the
  table had at the last pitch write (`pitchNowColFine`), which is neither the live column nor the
  folded transpose. Phrases 21 and 23 now agree register for register from the note through the bend.
- **Open, and §110 is corrected for claiming otherwise:** how a table's transpose column interleaves
  with a slide **already running**. §110 read twenty updates of phrase 21, saw no blip, and wrote
  "suppressed for the whole run". A longer window of phrase 23 blips throughout its bend, so the
  column does reach a running slide there; phrase 21's shorter bend still shows none. The source and
  the target are settled and implemented; this is not. Measure a slide long enough to cross several
  table loops, on a bare note and on a note-on, before writing a rule.
- Round 25 (§108-§110), three faults the user heard in `SAMESONG` and all three measured against the
  ROM first: **a table's volume column on the wave channel** is the `NR32` level by `amplitude & 3`
  (0 mute, 1 25 %, 2 50 %, 3 100 %, wrapping every four), where ChipBoy had `vol / 4` and muted every
  amplitude 1-3 -- phrase 24's bass swell was silent. **`E` is a whole envelope, not a level**: it
  takes the volume to x and then to zero at rate y, and a channel a `K` has killed answers it the
  same, which is how phrase 10's hats get their ghost notes; ChipBoy stopped the software envelope
  with the voice, so the level sat where the walk left it. And **a cell's `L` slides the bare note**
  with the table's transpose column suppressed for the run, where a table's `L` still aims *through*
  the column (§68): phrase 21's bend had been sliding from the table's octave blip, an octave high.
  The `L` command itself was never wrong -- six isolated cases agree register for register.
- Round 24 (D-UI-23-25): the Waves grid gains a **centre line on each axis**, drawn over the trace so
  the Bars view cannot bury the level line, and the Bars view's bars grow **up** from the floor again
  (turning the axis over in §107 had left them filling from the top down, so they appeared to hang);
  the frame strip and the synth previews follow. The sample last clicked is a **selection** the
  arrows move -- left and right between samples, up and down on its level, up being a smaller number
  -- through the same `onChange` a click uses, so undo covers it. And the **per-channel scopes turn
  over**: they trace digital levels, not rendered audio, so they were drawing level 15 at the top
  where the DAC puts it at the bottom; the silenced baseline moves to level 7.5, the DAC's zero. The
  master scope is untouched, since it already plots the rendered output.
- Round 23 (§103-§107): **the wave bank is one flat table**, the user's call between the two shapes
  §100 left open -- wave slots laid out in synth order with the driver carrying between them,
  rather than one 256-frame wave. `kWaveSlots` is **16**, a slot is **always `kMaxFrames`**, and a
  frame jump moves the flat index `(slot - 1) * 16 + frame`, wrapping at `kWaveFrames` = 256: past
  slot 16's frame 15 it is back on slot 1 frame 0. Only the **jump** walks flat -- the
  instrument's own run (LENGTH, LOOP POS, PLAY, SPEED, §65) stays inside the slot the voice is on,
  which is what §65 traced. The importer reads **all sixteen synths in order**, synth `k` into slot
  `k + 1`, so the slot next door holds what the save's wave RAM held; it no longer allocates slots
  in the order instruments happen to reference them. The driver looks waves up with `waveAt`, not
  `wave`: every one of the sixteen sounds, drawn or not. The editor loses the frame `+`/`-` and the
  click-past-the-end that grew a wave, and the list reads each slot's flat range instead of `n fr`;
  the synth keeps **From**/**To** (§36), which is the part LSDj does not have. Further blocks of
  sixteen are a later change -- the arithmetic is written against `kMaxFrames`/`kWaveFrames`, not
  against 16. `SAMESONG` is unchanged on pitch (84/91/79/96 windowed) -- the metric scores pitch,
  not timbre -- and its wave channel now walks runs of up to **87 consecutive frames** identical to
  the ROM's, with 114 of the ROM's 205 frames among the ones it loads.
  Two measurements came out of checking it. §104: LSDj's WAVE screen draws a frame **upside down**
  against ChipBoy's grid, which the user asked about twice -- it is a drawing convention and not
  the data. Of 850 distinct wave-RAM loads the ROM made over `READROOM`, 35 are a stored synth
  frame byte for byte and **none** is the vertical inverse of one, so the bytes ChipBoy imports are
  the bytes the APU sounds. Whether ChipBoy's grid should draw LSDj's way is open, and is a UI
  decision for the user. §105 claimed **some songs' synths are generated as they play** and is
  **withdrawn** -- the user said LSDj never does that, and they were right. The 12776 wave-RAM loads
  on `READROOM` that did not match a stored frame arrive every **2.79 ms**, which is 32 samples at
  11468 Hz: a **kit** streaming through channel 3, not a synth. Split by rate rather than by
  content, 99.6% of `SAMESONG`'s non-streaming loads and 88.7% of `READROOM`'s are frames the save
  holds, so §103's "read all sixteen synths out of the save" is the whole story.
  §106, measured at the user's asking with a new `lsdjref_trace --wave-probe` that renders
  SameBoy's audio beside the wave channel's own position: **the DMG's DACs invert.** A square frame
  gives nibble `0` at **+3772** and nibble `F` at **-3772**, and a 12.5% duty pulse inverts the same
  way, so it is one convention across the chip. ChipBoy is non-inverting on every channel alike, so
  the two differ by one global sign -- inaudible, and left alone rather than inverting every golden
  file. The one-sample shift is real and both sides agree on it: a trigger sounds **sample 1** first
  and sample 0 a whole cycle later. `Tests/ApuTests.cpp` pins both.
  §107 corrects §106's second half. **ChipBoy's DAC already inverts** -- `dacValue()` in the
  renderer is `-(level - 7.5) / 7.5` and always has been; §106 read `Apu::outWave()`, which is the
  digital level the DAC is *fed*, and stopped there. Rendered and measured, ChipBoy gives nibble `0`
  positive and nibble `F` negative on the analog path and on **RAW** alike, which is the reference's
  polarity. So nothing about the audio changed; what changed is the **Waves grid**, which drew level
  15 at the top -- the sample value, not the output -- and now draws level 0 at the top like the DAC
  and like LSDj. The stored bits are untouched and the corner readout still names the stored level.
  §106's one-sample rotation is deliberately **not** applied to the grid: it is a trigger transient,
  so drawing it would be a phase choice and would make column 0 edit sample 1.
- Round 22 (§102): **a phrase's `H` loops**. `H x y` with x > 0 hops inside the phrase x times and
  the step carrying it stays silent on a hopping pass; the groove walks with the play order, not
  with the step number, both measured. §80 had called this engine work the Player could not do,
  "a hop whose count survives across passes has no place in a schedule that is built once" -- but
  the count is fixed, so the order is, so the schedule can hold it. A phrase now has a **play
  order** and everything downstream is indexed by position in it, which is what keeps a host's
  timeline honest: a row is as long as its order makes it and a tick still names one (row, step).
  `READROOM` went from 3-6% agreement on three channels to 66/52/64/58, its first thirty seconds
  89-99% throughout. `H F F` is the one exception and stays unexplained: re-measured over
  thirty-five seconds it stops the channel outright, where every other `H x F` is a plain hop.
- Round 21 (§98-§101): the **ROM beside the save decides the model** -- `autoModel` asked the
  format first, so the version-keyed table was dead code; a pre-4.3 ROM has its version in the
  welcome line inside bank 0 and the caller only handed `romVersion()` the first 0x150 bytes, so
  every old ROM read as "not LSDj"; and 3.6.5, which is what the *Computer Savvy* songs were
  written in, was in neither table. Together those read every format-2 song under the **4.0.4**
  model with no kit ROM. Then **`L` and `P` replace one another** and **in Drum a slide is linear
  in the period register**, which is `SAMESONG`'s wave kick: ChipBoy ran the bend under the slide,
  took the period off the bottom and wrapped it at 2048 -- the machine gun the user heard. And
  **`F` on the wave channel takes the whole byte** and walks LSDj's flat 256-frame wave table,
  which §92 had as the low nibble wrapping inside one synth. And **a cell with a blank instrument
  column keeps its note** (§101): it was dropped outright, taking the row's command with it, where
  from 4.0.4 it is ChipBoy's own bare note -- a pitch change with no trigger, and an `L`'s target.
  `SAMESONG`'s phrase 1C step 9 is that: a `D#4` with `L 10` beside it, now the ROM's period for
  period where ChipBoy had played nothing.
- Round 20 (§93-§97), from the three songs in the user's 9.2.L save: the wave run's **`REPEAT`**
  comes off its own byte (byte 3 on formats 7-8, byte 2 from 9, while the synth moves to byte 3 at
  17), so formats 9-10 get their own model `kLsdj75`; the tick a note starts on belongs to its
  **first frame**; a table's **`H` costs no tick**, which is what an LSDj arpeggio's timing rests
  on; a kit has **one `LENGTH`, in byte 11**, and a **`LOOP` bit** in byte 5; and a kit reads
  **`PITCH` from byte 5** with `P` in period-register units. `SAMESONG`'s pulse channels went from
  agreeing with the ROM 47% and 30% of the time to 85% and 94%. `docs/LSDJ_VERSIONS.md` §6 has
  every song's numbers and what is still wrong.

## Open issues

- ~~Table rows: LSDj measured two ticks per row.~~ Closed (§44): re-measured on a 9.3.9
  ROM with a transpose column, a row is **one tick** in LSDj too; the two ticks were the
  envelope nibble. Nothing to change.
- Not at parity (`LSDJ_PARITY.md` §17): P's last ~1 %, V in Drum rounding, V in Tick at
  speeds not multiples of three, R's resync after 38, bare notes ended by a dead envelope;
  saw/square vibrato, kits and speech unmeasured. (Envelope rates 6 and 7 are off this list:
  the ROM's table settles them, `LSDJ_COMMAND_MATRIX.md` §6.5.)
- The hybrid Reaper project's state decodes correctly but was never opened in Reaper.
- Visualizer scopes lack the kit fixed-window hint; the STOCK badge is global;
  `Z 255,255` clips in a lane's command column.
- Demo songs were re-expressed under the measured laws; worth a listen.
- The FL Studio first-note drop (§43) is fixed on reasoning — a note-off now sorts before a
  note-on at the same sample — but was not reproduced here; the user confirms in FL. The
  tracker's own dropped first note at a DAW loop (§47) is likewise fixed on reasoning: the
  Player fires the step a jump lands in; the user confirms in the DAW.
- §7's envelope-speed numbers (6, 11, 15, 20, 27 for speeds 1–5) were the hardware
  envelope's, measured on version-0 saves; §51 has the 9.x software table. `LSDJ_PARITY.md`
  should be re-read against it when the harness writes format 22 (below). **Settled for 9.3.9**:
  the table is the ROM's own eight bytes at bank 02:`$698C` -- 0, 6, 11, 17, 22, 28, 34, 39
  indexed by `y & 7` -- so rates 6 and 7 are distinct and §7 is superseded outright.
- From round 13, measured and left, with the reason: **drum mode's own note table on 5.7–6.0**
  (C-4 is period 458);
  the **nibble wrap** in long S sweeps (`7F`→`80`) that ChipBoy's semitone steps cannot follow; the harness's tables loop after six rows for a
  reason still unknown (`LSDJ_PARITY.md`); noise **chain transposes before 9** are folded into the
  note's octave, unmeasured whether LSDj subtracts them from NR43 like a table's column; wave
  instruments' PLAY/SPEED/LENGTH and the old formats' vibrato-shape bits; the **instrument LENGTH**
  (byte 3), which LSDj loads into NR41 but leaves disabled in every song looked at, so it is
  inaudible until one is found that enables it; and format 11's **third envelope stage's own
  ramp** (its level is byte 10 and its timing follows §58, but no song was found that uses a
  non-zero period there, so the fade is left holding at the third amplitude).
  A probe save that has never been opened in the LSDj editor writes the stage levels of
  instrument 00 whatever plays, while the timing follows the playing instrument: a stale editor
  pointer. Real saves are fine; keep it in mind when probing envelopes.
- A table **ENV hop** is free in ChipBoy, as in LSDj from 8.9.3 (measured on 9.2.L); an older
  save's hop row gets a LEN of 1 at import to buy back the tick it used to cost (8.4.4).
  Formats 16-21 (LSDj 9.0-9.1) take the older flag, unmeasured -- no ROM in hand writes them.
- From the LSDj recreation, open by decision: an **LSDj-shaped noise map** as an
  instrument option (its map runs into 7-bit values above A-6 and retriggers on such a
  row; ChipBoy's transposed noise rows land near LSDj's pitches, not on them); **removing `A`** in favour
  of the TBL column (needs a say for "stop" and for `A` inside a table); the harness
  `lsdjref_sav.py` writes **version byte 0**, so LSDj reads its songs with the legacy
  command table (no `B`) and the legacy noise map — its measurements stand, but a 9.x
  song needs 0x16 and the B-shifted letter table.
- **Two gaps §10.1 left open**, both narrow. `L` replacing `P` is unmeasured on formats 0-3,
  because that probe's `P F0` runs the period off the register before the `L` lands there; a
  gentler `P` settles it. And `L`'s **duration** is not the same on formats 4-7 as on 11 and up --
  `L 10` covers its distance in about five updates on 5.7.8 to 6.8.2 and seventeen on 8.4.4 --
  so a Drum instrument whose table slides, on a song of those formats, slides too slowly. ChipBoy
  uses the later law.
- **The kit instrument's byte 3** and **a wave instrument's byte 3 low nibble** hold values in
  real songs and change nothing a trace can hear (§93, §96). Neither is mapped, neither is noted.
- **The Waves tab draws a frame the other way up from LSDj's `WAVE` screen**, and rotated by one,
  the user reports. The **data is not**: the bytes ChipBoy writes to `FF30`-`FF3F` for every frame
  of `SAMESONG`'s phrase 0B are identical to the ROM's, measured in the register traces, so the
  sound is right and this is a drawing convention. `Grids.cpp` puts sample 0 at the left with 0 at
  the bottom; matching LSDj is a one-line change if the user wants the editors to agree.
- **A cell's `W` on a pulse: the duty's lifetime** (§122's measurements, not fixed). On the ROM the
  `W`'s own note triggers at the **instrument's** duty and the new one lands on the write after, and
  it then holds until a *different* instrument loads: `W 03`, a note, the same instrument, another
  instrument gives `0 3 3 0` on `NR11`. ChipBoy applies it before the trigger and re-latches the
  instrument's duty at the next note-on: `3 0 0`. The value is right (`byte & 3`, swept), the
  lifetime is not. The general question behind it is whether ChipBoy should keep LSDj's **working
  copy** model -- a phrase command writes the instrument's copy and holds until another instrument
  replaces it -- where ChipBoy re-latches from the instrument at every note-on. `V` and the rest want
  the same measurement before anything changes, so this is a round of its own.
- **The noise vibrato's phase, at speeds 3, 4, 9 and F** (§119): fifteen of the ninety-six swept
  `V x y` settings differ from the ROM by one sample of the phase, at the ticks where the ninths
  accumulator lands on a whole unit. `V 3 F` fits `floor((12k - 1) / 9)` where ChipBoy computes
  `floor(12k / 9)`, but that correction is wrong at speed 4, so the ROM's accumulator is not one
  behind; it wants reading off the ROM rather than fitting. Ordering inside a tick is open too --
  with a table running, the ROM writes the transpose column then the vibrato and ChipBoy the other
  way round, so the value a tick ends on can differ even at the same rate.
- The chord rate defaults to LSDj's one step a tick; the demo songs' arpeggios still run
  at that speed. Slowing them is a content decision (`make_songs.py` would need a
  `chordRate` field).
- The Instrument tab gained a row (Chord rate); `chipboy_uishot` reports whether the
  form still fits the pane at the default height — check the Windows and macOS builds
  show no scrollbar there.

## Next steps

- **Fourteen more ROMs are in `/root/lsdj/roms/`** and every format gap is filled: 7.2.3 (format
  8), 7.5.4 (9), 7.9.9 and 8.0.0 (10), 8.2.0 (11), 8.8.6 (15), 8.9.3 and 8.9.5 (17), 9.0.0 and
  9.0.1 (18), 9.1.0 (19), 9.1.C (21), 3.6.5. Formats 9 and 10 have their own model now
  (`kLsdj75`, §93). **The models still do not know formats 17, 18, 19 or 21**: each falls through
  to the 8.8.6 model, and two things are already known wrong there -- the synth number moves to
  byte 3 at format 17, a table ENV hop stops costing a tick at 8.9.3 -- while the changelog puts
  the whole noise overhaul at 9.0. A model for 17-21 wants the noise map measured on 9.0.0 first.
  Run the batteries in `/root/lsdj/probe/vs_*.py` on them -- they take a version name and need
  nothing else.
- **Importing LSDj's synth parameters** would give the Waves tab an editable synth instead of
  sixteen drawn frames. It buys **nothing at play time** -- §105 first claimed otherwise and was
  withdrawn: LSDj writes a synth's frames into the wave table when a parameter changes, never while
  playing, and 99.6% of `SAMESONG`'s wave-instrument loads (88.7% of `READROOM`'s) are frames the
  save holds once the kit's 2.79 ms streaming bursts are separated out. A convenience, not parity.
- **`CASTSHDW`'s kit notes** are the largest thing left on the 9.2.L songs: 1142 note-level hits
  on the ROM against ChipBoy's 326, though the ones that do play now land on the ROM's period 77%
  of the time (§97). Whole notes are missing, among them every one whose note byte names a sample
  one of its two kits does not have -- the ROM plays the other kit's, `kitNote()` gives up on both
  and returns silence. Start there; it is a few lines.
- **`DELIVERY` gains a tick about every ten seconds.** Its PU2 note-ons run +17, +16, +11, +13 ms
  later than the ROM's in steps, with long plateaus between -- each step is one tick at its tempo
  (16.78 ms), not a drift. The tempo itself is right: the ROM's tick for every tempo byte measured
  (85 to 190) is within 0.016% of `1 / (0.4 x bpm)`, and DELIVERY's grooves are all 6/6 with no `G`
  or `T` anywhere. So one row in about a hundred is taking a tick longer in ChipBoy. Find which.
- **`READROOM` comes apart at about thirty seconds.** Its first five windows are 89-99% on every
  channel since §102 gave a phrase's `H` its loop; before that it was 3-6% from the first bar. What
  goes wrong at 30 s has not been looked at.
- **`DELIVERY` and `READROOM` come apart part way through.** `DELIVERY` agrees with the ROM on
  every channel until about 50 s and on none after; `READROOM` never agrees on three channels at
  all and its noise channel triples the ROM's note count (1032 against 3093). Neither is a
  command's law -- something ends a phrase or a chain in the wrong place. `READROOM`'s noise is
  the clearer thread. Compare with `/root/lsdj/probe/agreew.py`, which shows the windows.
- **The bend's phase at a note-on** is the largest thing left on the old songs and on SUNRISE's
  wave channel alike (`docs/LSDJ_VERSIONS.md` section 4 item 3, matrix section 10.1): the ROM's
  note-on writes a period already part of the way into a running `P`, ChipBoy writes the plain
  note. One question, two symptoms. Then **the noise table transpose before 4.0.4**, whose law is
  not worked out.
- **`SPACE TI` (8.4.4)** starts at about 20% agreement and climbs to 98% by the end, which reads
  like a structural difference early rather than a wrong law. `SAMESONG` also uses instrument
  finetune (byte 11) on four pulse instruments, which the importer drops with a note and the
  driver could carry (section 78 gives `F` the same law).
- **Two rulers now**, both in `/root/lsdj/probe/`: `agree.py` / `agreew.py` sample the pitch both
  sides are sounding every 2 ms and report the share of the time they agree, in six second windows
  each with its own offset; `runs.py` groups triggers into notes first. Counting triggers alone
  misleads on any channel that retriggers inside a note -- a kit plays by rewriting wave RAM once
  a wave cycle and both sides trigger on every rewrite. `song.py SAV IDX ROM TAG [sec]` is the
  whole pipeline for one song: extract, trace the ROM, import, trace ChipBoy, compare.
- **Test saves now in the container**: `/root/lsdj/csavvy[123].sav` are the *Computer Savvy*
  source files (25 songs, all format 2, LSDj 3.6.5), plus `lsdj8_4_4.sav` and `lsdj9_2_L.sav`.
  `/root/lsdj/probe/cmpn.py` is the comparison that counts noise by its **clock** rather than its
  `NR43` byte -- before 9.x the importer crosses into ChipBoy's own clock map, which reaches the
  same clock through a different shift/divisor pair, so a byte comparison reads as total failure
  when nothing is wrong.
- The software envelope's ramp steps are quantised to the tick (matrix §10 item 7). Not specific
  to noise; fixing it moves every instrument, so it wants its own round.
- The playback-ROM exporter: `HARDWARE_DRIVER_AUDIT.md` ends with the binary layout a
  playback ROM needs; nothing is built. Start with `docs/plan-exporter.md`.
- Windows and macOS builds are the user's; fix what they report.
