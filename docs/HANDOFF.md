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
- From the LSDj recreation, open by decision: an **LSDj-shaped noise map** as an
  instrument option (its map runs into 7-bit values above A-6 and retriggers on such a
  row; ChipBoy's transposed noise rows land near LSDj's pitches, not on them); the table
  volume column's **per-row fade speed** (LSDj's ENV low digit); **removing `A`** in favour
  of the TBL column (needs a say for "stop" and for `A` inside a table); the harness
  `lsdjref_sav.py` writes **version byte 0**, so LSDj reads its songs with the legacy
  command table (no `B`) and the legacy noise map — its measurements stand, but a 9.x
  song needs 0x16 and the B-shifted letter table.
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
