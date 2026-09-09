# tools/lsdjref — the LSDj parity harness

Measures how the real LSDj drives the Game Boy's APU, plays the same songs
through ChipBoy's driver, and diffs the two register streams. What it found is
`docs/LSDJ_PARITY.md`; why it exists is `docs/COMMANDS_AND_TEMPO.md` §31.

It is off by default. Nothing here is fetched, configured or compiled unless
you ask for it, and nothing here links into `chipboy_core` or a plugin.

## Running it

```
cmake -S . -B build-ref -DCHIPBOY_LSDJREF=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-ref --parallel 4
CHIPBOY_LSDJ_ROM=/path/to/lsdj.gb ./build-ref/lsdjref/lsdjref-run            # DMG
CHIPBOY_LSDJ_ROM=/path/to/lsdj.gb ./build-ref/lsdjref/lsdjref-run --model cgb
python3 tools/lsdjref/lsdjref_measure.py --traces build-ref/lsdjref/trace
```

The first run spends about fifty seconds of emulated time letting LSDj format
its own save; that file is kept and reused. Traces land in
`build-ref/lsdjref/trace/`, the comparison in
`build-ref/lsdjref/LSDJ_PARITY_REPORT.dmg.md`.

`ctest -R lsdjref` runs the same thing as two tests. Both **skip** — they exit
77 — when the ROM, the boot ROMs or Python are missing, so a checkout without a
ROM still runs the suite clean.

## What you need

- **An LSDj ROM you own**, named by `CHIPBOY_LSDJ_ROM` or `--rom`. It never
  enters the repository (`*.gb` and `*.sav` are ignored) and no CI job has one.
- **RGBDS** on the PATH, to assemble SameBoy's own open boot ROMs at configure
  time. Or point `-DCHIPBOY_LSDJREF_BOOTROM_DIR=` at a directory holding
  `dmg_boot.bin` and `cgb_boot.bin`.
- **Python 3**, standard library only.

## The pieces

| | |
|---|---|
| `cases.spec` | the 29 test songs, one `case` each, read by both tools |
| `lsdjref_sav.py` | writes one `.sav` per case into a copy of a base save |
| `trace/` | `lsdjref_trace`: the ROM in SameBoy's core, with a write log |
| `compare/` | `lsdjref_compare`: the same songs through ChipBoy's driver, diffed |
| `lsdjref_measure.py` | turns traces into the tables `docs/LSDJ_PARITY.md` quotes |
| `run.sh.in` | configured into the build tree as `lsdjref-run` |
| `cmake/SameBoy.cmake` | fetches SameBoy and assembles its boot ROMs |

## Adding a case

Add a `case` block to `cases.spec` and run `lsdjref-run --case yourname`. The
grammar is in the file's own header; notes are scientific pitch, command values
are LSDj's single byte in hex, and instrument fields keep LSDj's own encodings
(`env` is the NRx2 byte, `sweep` is NR10's complement) so what the spec says is
what the save holds.

The last six cases are the **fine** ones: where the cases above them pin the
shape of a law, these pin its numbers -- P swept over every value from -127 to
+64, V at all sixteen depths and all sixteen speeds in both clocks, and a table
whose rows hold the same amplitudes behind different envelope nibbles.

Two traps worth knowing, both measured:

- A table's volume row with a **zero low nibble** writes nothing at all. Write
  `81`, not `80`, to mean "amplitude 8".
- The phrase command column stores the letter's position in LSDj's own order
  with **no gap for B** — V is 16 and W is 17. liblsdj's shifted table turns
  every V into a W.

## What it does not do

It scripts the joypad and reads registers; it does not read LSDj's screen, so a
question that depends on what a screen shows is out of reach.
`lsdjref_trace --screen FILE.pgm` dumps the framebuffer, which is how the START
key and its timing were found, but nothing is parsed from it. Kits and the
speech instrument need sample data the save writer does not author.
