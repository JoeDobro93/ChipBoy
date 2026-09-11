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
| `lsdjref_ocr.py` | reads LSDj's 8x8 tile text off a `--screen` dump |
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

## Reading the ROM: `lsdjref_dis.py` and `lsdjref_pc`

Rule L3 allows **deriving behaviour from the ROM** (`docs/CHIPBOY_SPEC.md` §3.3): what ChipBoy
ships is its own code producing the same result, and the address that answered a question is
worth recording. Nothing of the ROM is committed; both tools read one the user owns, from
outside the tree.

Reach for these when a sweep of register values cannot separate two hypotheses — which is more
often than it sounds. Four of the seven corrections the 9.3.9 verification pass made were
invisible to any probe: `S` looks absolute until you run two, `M` looks like a plain `NR50`
write until a nibble goes above 7, `T` looks like BPM until the byte drops below 40, and `Z`
looks like "the last command" until two lanes disagree.

**`lsdjref_pc`** is the trace tool plus the PC and ROM bank of every write, and `--watch` extends
it to a work-RAM range:

```
build-ref/lsdjref/lsdjref_pc --rom /root/lsdj/lsdj9_3_9.gb \
    --bootrom-dir build-ref/lsdjref/BootROMs --sav probe.sav \
    --frames 200 --keys 180 --watch C2E4 --out pc.csv
```

Columns are `cycle,addr,value,pc,bank`. `--keys 180` presses START at frame 180, as `run.py`
does.

**`lsdjref_dis.py`** disassembles at an address it names:

```
python3 tools/lsdjref/lsdjref_dis.py /root/lsdj/lsdj9_3_9.gb 2 4828 14
```

### Where to start on a 9.x ROM

| What | Where |
|---|---|
| the command jump table | bank 02:`$47A2`, twenty little-endian words indexed by the letter's code in `-ABCDEFGHKLMOPRSTVWZ` |
| the dispatcher | bank 02:`$478D` (`cp $13; ret nc`, so `Z` never reaches the table) |
| the letters with **no** entry | `B` `D` `G` `H` `Z` — handled where the row is read, not where a command is run |
| `S`, per channel | bank 02:`$4812`; PU1's accumulate at `$4828` |
| `E`, per channel | bank 02:`$46D8`; the eight-byte rate table at `$698C` |
| `M` | bank 02:`$6246`, through a 128-entry table in work RAM at `$D400` |
| `W` | bank 02:`$47C8`; the wave branch at `$47F8` |
| `Z` | bank 02:`$73F3` (phrase), `$7402` and `$7424` (the two table columns), `$7365` common |
| the random | bank 02:`$33F4` raw, `$6479` bounded |
| playback start | bank 02:`$5FDA` — APU off, `DIV` reset, APU on, `NR51` ramped per channel, `NR50` |

### The loop that works

1. Trace the probe and find the register write you care about; note its `pc` and `bank`.
2. Disassemble there. If it is a refresh routine rather than the handler, it will read a work-RAM
   address — watch that address instead and trace again.
3. Disassemble where the watch points. That is the handler.

## Reading the screen: `--screen` and `lsdjref_ocr.py`

Some questions are about what the **editor shows**, not what the ROM plays — what number LSDj
prints for a noise note, say. `lsdjref_trace --screen FILE.pgm` writes the LCD as it stands at the
end of the run, and `--keys F:KEY[:HOLD],...` walks LSDj there; two keys at the same frame are
held together, which is how `SELECT+RIGHT` is pressed. LSDj draws nothing before about frame 150
here, so press no earlier than 200.

```
lsdjref_trace --rom ROM --sav PROBE.sav --frames 400   --keys 200:right:4,215:right:4,230:right:4,260:select:8,262:right:4,300:select:8,302:right:4   --out /dev/null --screen shot.pgm
python3 lsdjref_ocr.py shot.pgm learn
```

That script is: song screen, three columns right to `NOI`, then `SELECT+RIGHT` twice — the
phrase screen of the noise channel's first phrase. `lsdjref_ocr.py` learns the hex digits from
that screen's own row labels and prints the rest as `?`.

This is how §85 of `docs/COMMANDS_AND_TEMPO.md` was settled: a phrase holding note bytes `01`
through `10` prints `00` through `0F`, so LSDj counts a noise note from zero.
