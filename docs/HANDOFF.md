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
- `docs/`: `COMMANDS_AND_TEMPO.md` (design log §1–§34, binding), `CHIPBOY_SPEC.md`,
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

## Next steps

- The playback-ROM exporter: `HARDWARE_DRIVER_AUDIT.md` ends with the binary layout a
  playback ROM needs; nothing is built. Start with `docs/plan-exporter.md`.
- Windows and macOS builds are the user's; fix what they report.
