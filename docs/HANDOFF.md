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
- 172 core tests, 11 plugin checks, all green on Linux, warning-free; the link test
  passes under a 1 MB stack. CI on push runs only the L1 check.
- The driver follows LSDj's measured laws (`LSDJ_PARITY.md` §16–§17): 4 harness cases
  identical, 5 within tolerance, the rest listed with reasons.
- Round 6: channels on their own time, zombie-mode levels, shaped envelopes, Instrument
  tab, tracker note gestures, one selector convention, table playhead, wave synth,
  command argument shapes, the hardware audit and the fuzz tool.
- Round 7 (§35–§37): one editing grammar for every value field (click selects, typing
  refuses past the limit and Backspace takes digits back, double-click is the box,
  right-click lists with *Open in its tab* first, Shift+←/→ by one and Shift+↑/↓ by
  sixteen — a semitone and an octave on a note); the window remembers its tab, channel
  and each tab's selection (`ui_view` in the plugin state); the Waves tab lays frames
  eight to a row, each morph end has its own shape, the run is placed by From / To
  (`Synth::first`, `bank::synthWriteRun`), the synth is regrouped with a help line per
  chosen shaper, the tools sit under the grid, and the grid has a Points view with the
  pointer's coordinates; the instrument gains a **Chord rate** apart from the command
  rate (default 0 keeps parity; old files take the command rate).
- The LSDj ROM is the user's own, at `/root/lsdj/lsdj9_2_J.gb` on the build container
  only; `*.gb`/`*.sav` are git-ignored.

## Open issues

- Table rows: LSDj measured two ticks per row, ChipBoy runs one (tables have their own
  groove). Deliberate for now; the user decides.
- Not at parity (`LSDJ_PARITY.md` §17): P's last ~1 %, V in Drum rounding, V in Tick at
  speeds not multiples of three, R's resync after 38, envelope rates 6 and 7, bare notes
  ended by a dead envelope; saw/square vibrato, kits and speech unmeasured.
- The hybrid Reaper project's state decodes correctly but was never opened in Reaper.
- Visualizer scopes lack the kit fixed-window hint; the STOCK badge is global;
  `Z 255,255` clips in a lane's command column.
- Demo songs were re-expressed under the measured laws; worth a listen.
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
