# Handoff

The state of ChipBoy for a fresh session. Read this and `CLAUDE.md`, then the section of
the design log the change touches. Keep this file current at the end of every change.

## What it is

ChipBoy is a JUCE 8 / C++20 VST3 / AU / Standalone plugin: a cycle-exact DMG / CGB Game
Boy APU with measured analog colouring, an LSDj-shaped bank (instruments, tables, waves,
kits, grooves), a tracker whose channels keep their own time, two plugins (ChipBoy and
ChipBoy Voice, linked through shared memory), and a set of demos. The next big feature is
an exporter that turns a song and its bank into a playable Game Boy ROM; the driver is
kept hardware-honest for it (`docs/HARDWARE_DRIVER_AUDIT.md`).

## Where things live

| Path | What |
|---|---|
| `Source/core/` | links nothing, includes only `<std>` and `"core/..."` (rule L1): `Apu` (the chip), `Render` (BLEP, coupling, noise floor, RAW), `Driver` (notes, tables, commands, the 358 Hz pitch clock, zombie-mode levels, envelopes, the Clock), `Tracker` (Song, Player, recorder), `Bank` (instruments, tables, waves, kits, grooves, presets, the wave synth), `Link` (the region layout) |
| `Source/plugin/` | `main` (ChipBoyProcessor, the editor, panels), `voice` (the Voice plugin), `shared` (parameters, JSON, song/preset files, link transport), `ui` (widgets, grids, scopes, the groove editor, undo history) |
| `tools/` | `demo` (make_demo.py, make_songs.py), `recordtest` (record → replay → song file → hybrid passes; `--play-song`, `--write-song/--check-song`, `--write-state/--check-state`), `linktest`, `paramdump`, `uishot` (Xvfb screenshots), `fuzz`, `lsdjref` (the LSDj parity harness, opt-in) |
| `Tests/` | Catch2 core tests (Apu, Blargg, SameSuite, Render, Driver, Tracker, Link, Hardware, Clock, WaveSynth) |
| `Demo/` | the Reaper projects (host tempo, song tempo, hybrid), the MIDI file, the automation JSON, `ChipBoy Demo.cbsong`, `chipboy_demo_hybrid.state`, `songs/*.cbsong`, `PARAMETERS.md` |
| `docs/` | `COMMANDS_AND_TEMPO.md` (the design log, §1–§34, binding), `CHIPBOY_SPEC.md` (the spec with revision notes), `UI_DESIGN.md`, `HARDWARE_REFERENCE.md`, `HARDWARE_DRIVER_AUDIT.md`, `LSDJ_PARITY.md`, `LICENSING.md`, `screenshots/` |
| `CHANGES.md` | every departure from the spec, newest first, with why and what was considered; the implementation status table |

## State (2026-09-09)

- Song file format 7 (embeds the bank; converts 6 and older); link region version 4;
  76 host parameters (the demo generator cross-checks the table, do not change it
  casually).
- Core tests 172, plugin checks 11 (`chipboy_linktest`, `chipboy_recordtest` with four
  passes, `demo_song_matches`, `demo_state_matches`, six `demo_songs_load`,
  `chipboy_fuzz`), all green on Linux; warning-free; the link test passes under a 1 MB
  stack (Windows' default).
- CI: a push runs only the L1 rule check; platform builds run from the Actions tab or a
  `v*` tag. The user builds Windows and macOS locally and reports errors.
- The driver follows LSDj's measured laws (`docs/LSDJ_PARITY.md` §16–§17): 4 harness
  cases identical, 5 within tolerance, the rest listed with reasons.
- The LSDj ROM is the user's own, at `/root/lsdj/lsdj9_2_J.gb` on this container only;
  `*.gb`/`*.sav` are git-ignored. The harness needs `-DCHIPBOY_LSDJREF=ON` and
  `CHIPBOY_LSDJ_ROM`; its tests skip without the ROM.

## Verify

The gate, from `CLAUDE.md` ("Verification runs here"): core tests, the plugin targets,
`ctest --test-dir build-plugin -C Release`, `make_demo.py --paramdump` identical, the
link test under `ulimit -s 1024`. Screenshots: `chipboy_uishot` under Xvfb
(`--song Demo/ChipBoy Demo.cbsong`, `--shaped`, `--hex`, `--scope-check`, `--tab-switch`).
Parity: configure `build-ref` with `-DCHIPBOY_LSDJREF=ON`, run the `lsdjref_*` tests, read
`build-ref/lsdjref/LSDJ_PARITY_REPORT.{dmg,cgb}.md`.

## Open questions and known gaps

- **Table row length**: LSDj measured two ticks per table row; ChipBoy runs one (its
  tables have their own groove). Deliberate for now; decide with the user.
- **Not yet at parity** (`LSDJ_PARITY.md` §17): P's last ~1 % (a swept table, fitted),
  V in Drum rounding, V in Tick at speeds that are not multiples of three, R's resync
  stopping after 38 on LSDj, envelope rates 6 and 7 measuring alike, bare notes where a
  dead envelope ends LSDj's note; saw and square vibrato shapes, kits and speech unmeasured.
- The hybrid Reaper project's embedded state decodes back correctly but has not been
  opened in Reaper here.
- The visualizer window's scopes lack the kit fixed-window hint; the STOCK badge is
  global (its inputs are hardware options); `Z 255,255` clips in a lane's command column.
- Demo songs were re-expressed under the measured laws (every V speed → 0, P values in
  two's complement); worth a listen.
- The exporter: `HARDWARE_DRIVER_AUDIT.md` ends with the compact binary layout a
  playback ROM needs; nothing is built yet.

## How a change goes

1. Read the design-log section it touches; if the design changes, append a numbered
   section to `docs/COMMANDS_AND_TEMPO.md` first.
2. Work in the main checkout, incremental builds; run the tests of the area while
   working and the full gate once at the end.
3. Regenerate what the change invalidates (`make_demo.py`, `make_songs.py`,
   `--write-song`, `--write-state`; screenshots), and write the CHANGES entry.
4. Push `main` once. Update this file's State and Open questions.
