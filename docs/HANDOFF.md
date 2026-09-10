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
  preferring the one whose version reads the song's format. Not decoded: the DIST modes
  for notes that play both kits (summed and clipped instead, noted), offsets, loop and
  half-speed flags. All eight songs of the kit save convert and play.
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
- The parity harness runs here now: RGBDS was built from source into `/usr/local`, the
  ROM sits at `/root/lsdj/lsdj9_3_9.gb` (container only), `build-ref/` holds the build.
  `f_table_speed` is a new case; only it and `i_kill_delay_chord` were traced on 9.3.9.
- The LSDj ROM is the user's own, at `/root/lsdj/lsdj9_2_J.gb` on the build container
  only; `*.gb`/`*.sav` are git-ignored.

## Open issues

- ~~Table rows: LSDj measured two ticks per row.~~ Closed (§44): re-measured on a 9.3.9
  ROM with a transpose column, a row is **one tick** in LSDj too; the two ticks were the
  envelope nibble. Nothing to change.
- Not at parity (`LSDJ_PARITY.md` §17): P's last ~1 %, V in Drum rounding, V in Tick at
  speeds not multiples of three, R's resync after 38, envelope rates 6 and 7, bare notes
  ended by a dead envelope; saw/square vibrato, kits and speech unmeasured.
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
  should be re-read against it when the harness writes format 22 (below).
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
- **The kit `DIST` modes** are the one gap left of the four. Narrowed to **byte 13's bit 6** of
  the kit instrument, which changes the mixed stream with its length unchanged, while bytes 4,
  5, 6, 7, 10, 12, 14, 15 and the top bits of 2 and 9 do not (5 and 6 are length, 10 an offset
  into the sample -- a high nibble of `b` starts past the end and plays silence). Reading the
  modes off wants a kit-stream decoder in `/root/lsdj/` that can line the mix up against each
  sample nibble by nibble; two samples at once are summed and clipped until then. 61 of the 69
  kit instruments in the user's saves play two kits at once, so it is worth a round.
- The chord rate defaults to LSDj's one step a tick; the demo songs' arpeggios still run
  at that speed. Slowing them is a content decision (`make_songs.py` would need a
  `chordRate` field).
- The Instrument tab gained a row (Chord rate); `chipboy_uishot` reports whether the
  form still fits the pane at the default height — check the Windows and macOS builds
  show no scrollbar there.

## Next steps

- The playback-ROM exporter: `HARDWARE_DRIVER_AUDIT.md` ends with the binary layout a
  playback ROM needs; nothing is built. Start with `docs/plan-exporter.md`.
- Windows and macOS builds are the user's; fix what they report.
