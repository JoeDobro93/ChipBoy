# Plan: the third campaign — audit every command against the 9.3.9 ROM's own code

A fresh session's brief. Nothing in this repository is to be taken as correct until this round
has checked it **against the code in the ROM**, not against a trace that happened to agree and
not against what an earlier section of the design log says. The two campaigns before this one
worked from register traces and got a long way — `docs/LSDJ_COMMAND_MATRIX.md` §9 has their
score: of nineteen letters, twelve came back unchanged and **seven were wrong**, and that was a
trace-led pass re-checking a trace-led pass. The faults that survive both are the ones only the
code will settle: where two commands meet, where a table row and a phrase cell disagree, and
what happens on the tick a thing starts or stops.

Read this file, then `CLAUDE.md`, then §1 below. Do not read the whole design log first.

## 1. What this round is for

Three jobs, in this order.

1. **The code audit.** For every command letter, in a **phrase cell** and in a **table row** and
   in both of a table's two command columns, find the ROM code that handles it, read it, and
   write down what it does — including what it does to the *other* things in force (the note's
   own registers, a running table, the envelope, a slide, another command on the same row). Then
   check ChipBoy against it. The interactions are the point: single-command behaviour is largely
   right already, and the bugs this year have all been where two mechanisms meet.
2. **Test on 9.2.L.** The user's `.sav` and its songs are the working material
   (`/root/lsdj/samesong.sav`, eight songs, `SAMESONG` / `READROOM` / `CASTSHDW` the ones they
   listen to). 9.2.L and 9.3.9 should be functionally the same bar the changelog's `R` change —
   **verify that** rather than assuming it, because most of the per-song work so far was measured
   on 9.2.L and written into laws named for 9.3.9.
3. **Re-derive the version mapping.** `Source/core/Import/LsdjModel.*` keys what a save's bytes
   mean to an LSDj version. A lot has changed under it since it was written; the user reports the
   **8.4.4** project no longer imports well while **9.2.L is close**. Every stable ROM and a
   few others will be supplied. Formats 17-21 (LSDj 9.0-9.1) have **no model of their own** and
   fall through to 8.8.6's, which is known wrong in at least two ways (§93's synth byte moves at
   17, §95's free table `ENV` hop starts at 8.9.3).

Assume nothing. Where a law in the design log disagrees with the ROM's code, the code wins and
the section gets a correction with its own number — that is how §145 corrected §144 and §124
withdrew §113's claim.

## 2. The rules that bind this work

From `CLAUDE.md` and `docs/LICENSING.md`; L3 is the one that shapes this round.

- **L3.** No LSDj content is committed — no ROM, save, sample, kit or manual text. Behaviour
  **may be derived from the ROM**: disassemble it to settle what a command does, **record the
  address that answered it**, and reimplement so the result is identical. Never commit an
  instruction listing or a table lifted whole. A sentence of the form "bank 02:`$698C` is eight
  bytes, 0, 6, 11, 17, 22, 28, 34, 39, indexed by `y & 7`" is exactly right: it is the answer and
  its provenance, and it is ChipBoy's own code that ships.
- **L1.** `Source/core` links nothing and includes only `<std>` and `"core/..."`.
- Assets live at `/root/lsdj/`, outside the tree, and `*.gb` / `*.sav` are git-ignored.
- One change at a time; the full gate before a push; `docs/COMMANDS_AND_TEMPO.md` gets the
  numbered section **before** the code; `CHANGES.md` and `docs/HANDOFF.md` get the round.

## 3. The tools, with the exact invocations

Everything below exists and works. Build the reference harness first:

```
cmake -S . -B build-ref -DCHIPBOY_LSDJREF=ON && cmake --build build-ref -j
```

**RGBDS must be on `PATH` before `cmake` configures**, to assemble SameBoy's open boot ROMs, or
the harness silently reports "no boot ROMs" and every test skips. It is not packaged for this
image and building it from source is unnecessary: take the prebuilt Linux tarball from
`gbdev/rgbds`' releases (0.8.0 assembles them cleanly). This is the one step a fresh container
needs that nothing else will tell you.

### 3.1 Read the ROM

```
python3 tools/lsdjref/lsdjref_dis.py /root/lsdj/lsdj9_3_9.gb 2 478D 20
                                     ROM                     bank addr count
```

A minimal SM83 disassembler with the sound and timer registers named. Its own docstring carries
the map that the last campaign left: **the command jump table is bank 02:`$47A2`**, twenty
little-endian words indexed by the letter's code in `-ABCDEFGHKLMOPRSTVWZ`, dispatched from
`$478D`; `B`, `D`, `G`, `H` and `Z` have **no entry** — they are handled where the row is read,
not where a command is run, which is itself a fact about where to look for them.

**The map the last campaign left is `tools/lsdjref/README.md`'s "Where to start on a 9.x ROM"** --
read that table rather than trusting the one below, which is the short version of it plus the few
addresses recorded elsewhere. It also carries the loop that finds a handler you have no address
for, and the four corrections that no register sweep could have made (`S` looks absolute until you
run two; `M` looks like a plain `NR50` write until a nibble goes above 7; `T` looks like BPM until
the byte drops below 40; `Z` looks like "the last command" until two lanes disagree).

| What | Where (9.3.9, bank 02 unless said) |
|---|---|
| the jump table, the dispatcher | `$47A2` (twenty words, letter code order) / `$478D` -- `cp $13; ret nc`, so `Z` never reaches the table |
| the five letters with no entry | `B` `D` `G` `H` `Z`, handled where the **row** is read |
| `E` per channel / the rate table / the wave level | `$46D8` / `$698C` (8 bytes: 0 6 11 17 22 28 34 39) / `$46E8` |
| `S` per channel / PU1's accumulate / the note refresh that writes `NR10` | `$4812` / `$4828`, `$4832`, `$483C` / `$6051`, `$604B` |
| `M` through its 128-entry table in work RAM | `$6246`, table at `$D400` |
| `W` / the wave branch | `$47C8`, `$47D2` (PU2) / `$47F8` |
| `Z` phrase / the two table columns / common | `$73F3` / `$7402`, `$7424` / `$7365`; the random `$33F4` raw, `$6479` bounded |
| `B`'s phrase roll / its table hop | `$5074` / `$732C` |
| `T`'s hinge | `$65FB` |
| playback start | `$5FDA`: APU off, `DIV` reset, APU on, `NR51` ramped per channel, `NR50` |
| the kit `DIST` pages | bank 0 `$04BA` and `$0420`, tables copied to `$D000`-`$D300` |
| the command help text | bank 01 `$7448` |

No address is recorded for `L`, `P`, `V`, `O`, `A`, `C`, `F`, `K` or `R`: those laws were settled
from traces alone, which is most of why this round exists.

### 3.2 Find the code behind a write

```
build-ref/lsdjref/lsdjref_pc --rom ROM --bootrom-dir build-ref/lsdjref/BootROMs \
  --sav PROBE.sav --frames 400 --keys 180 [--watch C2E4[-C2E8]] --out pc.csv
```

The trace plus **the PC and ROM bank of every write**, and `--watch` extends it to a work-RAM
range. Columns `cycle,addr,value,pc,bank`. This is what turns "the register did something" into
"this code did it": trace, grep the write, read the address it names with the disassembler, and
if the value came from a variable, `--watch` that variable to find what wrote *it*. The worked
example in `tools/lsdjref/pc/main.cpp`'s header is how §72's `S` accumulate was settled in four
steps.

### 3.3 Trace the ROM, build a probe song

```
build-ref/lsdjref/lsdjref_trace --rom ROM --bootrom-dir build-ref/lsdjref/BootROMs \
  --model dmg --sav PROBE.sav --frames 500 --keys 180:start --out rom.csv
```

The probe rig builds the save. It is **in the repository now** (`tools/lsdjref/`): `probe_fmt22.py`
(the song writer: `Probe`, `only`, `tempo`, `phrase`, `table`, `pulse`, `wave`, `noise`),
`run.py`, and the three helpers this session moved in from the scratchpad —
`probe_h.py` (`playStart`, `ev`, `trigs`, `reg`, `levels`), `probe_vh.py` (`VP`, `trace`,
`romPath`, `fmt`, every version's bootstrapped host) and `probe_cb.py` (`trace_cb`: import a
probe save into ChipBoy and trace it). A two-sided probe is a dozen lines:

```python
import sys, os; sys.path.insert(0, 'tools/lsdjref')
from probe_vh import *
from probe_cb import trace_cb
import probe_fmt22 as PF, run as R
v = 'lsdj9_2_L'
p = VP(v); ph = p.only(0); p.tempo(163, (6, 6))
i = PF.INST[0]; p.pulse(i, env=0xF0, duty=2, pan=3)
p.phrase(ph, {0: (0x34, i, 'E', 0x02)})          # note, instrument, letter, value
csv = trace(p, v, 'tag', frames=260)
cb  = trace_cb(os.path.join(DIR, '%s_tag.sav' % v), romPath(v), 'tag', seconds=0.4)
for path, isrom in ((csv, True), (cb, False)):
    z = playStart(path) if isrom else 0
    ...                                           # R.rows(path) is (cycle, addr, value)
```

`VP(version)` builds on a host save **the ROM formatted itself** (`--init-sav`), so no save from
anyone is needed; `Probe(host=..., blank=False)` overwrites one song of a real save instead. Two
things to know about the rig, both hard-won: the ROM's play moment is found with `playStart()`
(LSDj's own `NR52 = 00` then `80`), never by counting frames — the frame is not 70224 cycles
while the LCD is off, and the old skip landed past the song's first note; and a save that has
**never been opened in the LSDj editor** writes instrument 00's envelope stage levels whatever
plays, so probe envelopes on a save the editor has touched.

About 110 `vs_*.py` probes from this year's rounds sit in `/root/lsdj/probe/` **in this
container only** and will not survive into the new one. They are named in the design log where
each was used; rebuild the one you need from the snippet above rather than asking for them back.

### 3.4 ChipBoy's side

```
tools/gate.sh core|plugin|all            # build + every check; -t <catch2 tag> for one area
chipboy_recordtest --import-sav SAV NAME|working OUT.cbsong
chipboy_recordtest --trace-song OUT.cbsong cb.csv [seconds] [--tempo BPM]
chipboy_uishot ...                       # screenshots, under Xvfb
```

`--trace-song` runs clock, player and driver with no JUCE and writes the harness's CSV, so the
two streams diff directly. `--tempo` plays a song at another tempo, which is what a host does to
it. The binary is `build-plugin/chipboy_recordtest_artefacts/Release/chipboy_recordtest`.

The gate runs here; GitHub Actions is not a feedback loop. The user builds Windows and macOS.

## 4. What to read, in what order, and how much to trust it

| Document | What it is | Trust |
|---|---|---|
| `CLAUDE.md` | the working rules | binding |
| `docs/HANDOFF.md` | state, **open issues**, next steps — rounds newest first | current |
| `docs/COMMANDS_AND_TEMPO.md` | the design log, §1-§146, every law with the measurement behind it | **binding, and the newest sections win**: §145 corrects §144, §141-§143 correct §116 and §136, §124 withdraws §113 |
| `docs/LSDJ_COMMAND_MATRIX.md` | campaign 2: every letter, the ROM addresses, §9's verification table, §10 what is open | the best starting map, but it **predates §103-§146** — treat its verdicts as the last pass's, not as settled |
| `docs/LSDJ_PARITY.md` | campaign 1 and the harness cases | oldest; §7's envelope numbers are superseded by the ROM's own table |
| `docs/LSDJ_VERSIONS.md` | per-format layout and per-song scores | the version work's base |
| `docs/HARDWARE_DRIVER_AUDIT.md` | departures from the chip, struck through as settled | short, current |
| `docs/CHIPBOY_SPEC.md` | what ChipBoy is; 3.3 is the L3 rule | binding |
| `CHANGES.md` | every departure, newest first, with its measurement | the reason a thing is the way it is |
| `docs/plan-lsdj-import.md`, `plan-lsdj-version-sweep.md`, `plan-table-lanes-and-wave.md`, `plan-kit-pairs.md` | the earlier plans | history |

Read `HANDOFF.md`'s **Open issues** in full before planning. Read design-log sections on demand:
§3 (a cell's commands fold into the note's burst), §7 (pitch speeds), §26/§27 (levels and what
takes an envelope over), §31 (a table's row 0 on the note), §64 (the three lanes), §122/§131
(an `A` inside a table), §134/§136 (`R`), §140 (a STEP table's position), §145 (the transpose
column's owner), §146 (a bare note) are the ones the interactions live in.

## 5. What is already known to be wrong

`docs/HANDOFF.md`'s Open issues is the list; these are the ones this round should expect to own,
newest first, each with its numbers there:

1. **A wave DRUM sweep does not stop.** `CASTSHDW`'s intro: the ROM falls `1958 1470 1202 891`
   over three ticks and **holds 891** until the next note; ChipBoy falls on to 90 and wraps
   (§110). Both run the sweep in real time, so a host tempo change legitimately lands it
   elsewhere in the row — the runaway is the fault. Measured: DRUM alone does not sweep and the
   row's `M 55` does not either, so it is a `P` in force; what **stops** the ROM is unmeasured.
   The first thing to read code for.
2. **A kit's STEP bend.** The ROM steps the period three times in a six-tick row, at the same
   ticks at 132 BPM and at 200, so STEP is musical where DRUM and FAST are real time; ChipBoy
   applies it once (§97's "three times the byte once").
3. **The imported envelope follows the host tempo** and LSDj's does not (§141 measured the ROM
   identical byte for byte at T163 and T81). The importer converts real-time stages into **ticks**;
   the stage length wants a real unit, which is a song-format change and an Instrument-tab change.
   **Ask the user before doing it** — they were asked and have not answered yet.
4. **Formats 17-21 have no import model**, and 8.4.4 regressed.
5. `SAMESONG` phrase 2F row E: instrument `00`'s own pitch fall is 4 % short a step.
6. `READROOM`'s noise row 04 is one `P` step out: the ROM's first `P` step lands a tick after the
   row begins where ChipBoy's lands on the row's own tick. Measure whether that is `P`'s first
   step or a command-only cell (`note=00 inst=FF`) landing a tick late.
7. `DELIVERY` gains a tick about every ten seconds — one row in a hundred is a tick long.
8. The noise vibrato's phase at speeds 3, 4, 9 and F (§119), and ordering inside a tick.
9. `H F F` stops the channel outright where every other `H x F` is a plain hop (§120).
10. `W`'s two synth variables on a wave instrument, and `Z`'s source per lane — the two the last
    campaign left open (matrix §9).

## 6. The method, and the traps

What has worked, and what this year cost to learn.

- **Measure, change, measure again.** §141's first fix was a **no-op** — the trace came back
  byte for byte identical — and that was only caught by re-running the probe afterwards. Never
  report a fix you have not re-measured.
- **Do not ship a guess whose comment the measurement denies.** §144 added a plausible
  `nestJustStarted` guard; the ROM said it moved the event the wrong way, and it was reverted
  rather than shipped with a hopeful comment.
- **Read a value where it cannot lie.** §144's "the transposes add" was measured at the top of
  the noise map, where the index wraps into 7-bit values and two different sums print the same
  byte; on a pulse, where a period is a number, the law was different (§145). Prefer the pulse
  period for anything about pitch, and check a noise result against a second note.
- **A count is not a law.** Five triggers matching five triggers says nothing about *which*
  rows fired them.
- **Beware the factory bank in tests.** `Rig`'s bank is `Bank::factory()` and its table and
  instrument slots hold **presets**; a test that sets `used = true` and a couple of fields
  inherits a preset's commands, which in one case swept the period under the test until it was
  reset with `tables[k] = Table{}`.
- **Sequence probes and builds.** A probe that runs while the gate relinks `chipboy_recordtest`
  dies with `PermissionError`.
- Nothing in the scratchpad may shadow a stdlib module: a `dis.py` there broke `import numpy`
  for every probe in the directory.
- No background watchers, no `pgrep` waits keyed on a pattern that matches the waiter itself,
  builds in the foreground with a timeout.

## 7. What done looks like, per finding

1. A probe that shows the ROM's behaviour and ChipBoy's side by side, with the numbers.
2. The ROM **address** that settles it, where the code was read.
3. A numbered section in `docs/COMMANDS_AND_TEMPO.md` — the measurement, then **As built**, then
   **Left measured, not settled** if anything is — written **before** the code.
4. The code, then a Catch2 case that fails without it.
5. `tools/gate.sh all` green (266 core tests and 11 plugin checks as of this handoff).
6. `CHANGES.md` and `docs/HANDOFF.md`, then one push to `main`.

A correction to an earlier section is a new section that says so, not an edit of the old one.

## 8. Suggested order

1. Build the harness, put the assets in place, and re-run a handful of the design log's most
   recent probes to check the rig and the ROMs agree with what is written down (§145's four
   transpose cases are a good start: they are exact, so any drift is the rig).
2. **Read the dispatcher** (`$478D`, `$47A2`) and map every letter to its handler. Write the map
   into a new section of `docs/LSDJ_COMMAND_MATRIX.md` — addresses and what each does, including
   the five letters with no table entry.
3. Read the **row reader**: what a phrase step does in order (note, instrument, the two command
   columns), and what a table row does in order (transpose, volume/LEN, CMD 1, CMD 2), and on
   which tick each lands. That order is where §3, §31, §122, §139, §145 and §146 all live, and
   where the remaining per-song faults are.
4. Then the interactions, letter by letter, phrase and table: what each does to a note in
   progress, to a running table, to the envelope, to a slide, and to the other column's command
   on the same row.
5. `CASTSHDW`'s intro (finding 1) as the first song-level target, then the version models.

## 9. Assets the user supplies

Put them under `/root/lsdj/` (or set `CHIPBOY_LSDJ_DIR`):

```
/root/lsdj/lsdj9_3_9.gb        the audit target
/root/lsdj/lsdj9_2_L.gb        what the songs were written in
/root/lsdj/samesong.sav        the user's save: CASTSHDW EGOFLEX REACTION READROOM
                               REPTCOMP SAMESONG DELIVERY UNMASKED
/root/lsdj/lsdj8_4_4.gb+.sav   the project that regressed
/root/lsdj/roms/lsdjX_Y_Z.gb   every other version, named this way; `probe_vh.py`'s
                               VERSIONS list is the set it knows, and `*.host.sav`
                               beside each is generated on demand by --init-sav
```

`CHIPBOY_LSDJ_SAV=/path/to/a.sav` makes one `[lsdj]` test import every song of a real save.
`-DCHIPBOY_LSDJREF=ON` plus `CHIPBOY_LSDJ_ROM` turns on the parity tests; they skip without it.
