# Plan: validate 9.3.9, then sweep every older LSDj version

Two stages, in order. Stage 1 is a **verification** pass over
`docs/LSDJ_COMMAND_MATRIX.md` on the 9.3.9 ROM. Stage 2 probes every older version against
9.3.9 and decides, per command, whether it shares 9.3.9's behaviour, needs a converted value on
import, or cannot map at all.

Filling ChipBoy's own gaps (`docs/LSDJ_COMMAND_MATRIX.md` §10) comes **after** stage 1 and can
run alongside stage 2.

---

## What has to be supplied: just the ROM

**A ROM is enough.** `lsdjref_trace --init-sav FILE` boots with no save at all, lets LSDj
format its own SRAM and writes the result out. Give it **~3000 frames** (about 50 s) — at 400
frames the save is still blank and at 1200 it is caught mid-format, both of which look like
failure. At 3000 the working song is a valid empty song in the ROM's own format.

`Probe(host=..., blank=True)` builds on that working song, allocating the phrase, instrument and
table slots it uses. **This was checked against a real editor-written save** on the three things
that matter, including the exact cases where a *synthetic* save had gone wrong before
(`COMMANDS_AND_TEMPO.md` §58, §63):

| probe | real host | bootstrapped host |
|---|---|---|
| `S23` / `S71` in a phrase | `NR10 = ED` / `9F` | same |
| `W03` in a phrase, and in table columns 1 **and** 2 | `NR11 = C0` | same |
| a table `H` in column 2 — the §58 failure | same period cycle | same |
| a table `G` — the §63 failure | same row lengths | same |

So the trap §58 recorded is specific to saves built from nothing by
`tools/lsdjref/lsdjref_sav.py`, not to bootstrapped ones. A **real save is still worth using
when one exists** — it is one less assumption — but it is not required, and stage 2 can probe a
version for which no save exists at all.

No LSDj content is ever committed (rule L3): ROMs, saves and the traces stay outside the tree.
Disassembling the ROM to settle what a command does is cleared, and the findings belong in the
matrix -- it is the fastest way through the cases a sweep of values cannot separate.

### Formats and the versions that write them

0 (3.1.5-3.5.1), 2 (3.6.8-4.3.0), 3 (4.4.0-5.0.3), 4 (5.7.8-6.0.1), 5 (6.4.5), 7 (6.8.2-7.0.2),
11 (8.4.0-8.5.1), 15 (8.8.6), 22 (9.2.J-9.4.2). Formats 16-21 have never been measured at all.

---

## Setting up in a fresh container

```
cmake -S . -B build-ref -DCHIPBOY_LSDJREF=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-ref --parallel 4
export CHIPBOY_LSDJ_DIR=/path/to/where/the/roms/live
export CHIPBOY_LSDJ_ROM=lsdj9_3_9.gb

# Bootstrap a host save from the ROM itself -- 3000 frames, not fewer:
build-ref/lsdjref/lsdjref_trace --rom $CHIPBOY_LSDJ_DIR/lsdj9_3_9.gb \
    --bootrom-dir build-ref/lsdjref/BootROMs --model dmg --frames 3000 \
    --init-sav host939.sav --out /dev/null
```

Then `Probe(host='host939.sav', blank=True)`. If a real save made in that version *is* to hand,
`export CHIPBOY_LSDJ_HOST_SAV=...` and use plain `Probe()` instead.

`tools/lsdjref/probe_fmt22.py` and `tools/lsdjref/run.py` read those. `probe_fmt22.songs(path)`
lists a save's songs so the right host can be picked; `Probe(want_format=N)` refuses a host of
the wrong format rather than producing quiet nonsense.

**Validate the rig before trusting a single measurement.** All three must hold:

1. A plain note on PU1 is one `NR12 = F8`, one period write, one trigger — nothing else.
2. Two notes eight rows apart at tempo 128 with groove `6 6` are **0.9375 s** apart.
3. `S23` on PU1 writes `NR10 = ED`. (This is the sharpest check: it is a value nobody would
   guess, and it exercises a phrase command end to end.)

If any of those fails, the host save or the ROM is wrong. Stop and say so. All three passed on
9.3.9 in the stage-1 pass, on a host bootstrapped from the ROM alone.

**There is no `START` blip** — this plan used to say LSDj plays a short note about 1.8 s before
the song's own first note. It does not. Traced with the PC of every write, the only triggers
before the song are the boot ROM's own power-on chime, and between the key press and the first
note LSDj only power-cycles the APU, resets `DIV`, ramps `NR51` and sets `NR50`
(`LSDJ_COMMAND_MATRIX.md` §9). The 1.8 s was an anchoring bug: `events()` skipped `180 * 70224`
cycles, but a frame is not 70224 cycles while the LCD is off through LSDj's boot, so the skip
landed past the song's real first note and anchored on the second pass of a looping phrase —
and 1.875 s is exactly one 16-row phrase at 128 BPM. **Anchor on `run.playStart()`**, the
`NR52 = 00` → `NR52 = 80` reset LSDj does when playback starts; it is the only `NR52 = 80` after
the boot chime. Never count frames.

---

## Stage 1: validate the matrix on 9.3.9

`docs/LSDJ_COMMAND_MATRIX.md` §9 lists what each row's measurement was. Re-run them and compare.
This is a check, not a rewrite: the point is to catch anything the first campaign got wrong.

Work command by command in §6's order. For each:

1. Read the entry's claim and its formula.
2. Build the probe the entry describes (§3.4 has the shape; §9 says what was read).
3. Compare the numbers. **Report every difference, however small**, rather than editing the
   document to match — a disagreement is either a rig problem or a real finding, and both matter.

Pay particular attention to the four entries that contradict what ChipBoy or the changelog says,
because they are the ones most worth re-confirming:

- **§6.15 `S` on PU1** — `NR10 = ((-x) & 15) << 4 | ((-y) & 15)`. Eight values were measured.
- **§6.2 `B`** — the changelog's examples are inverted from the ROM: `B00` never plays.
- **§6.19 `Z`** — re-runs the *last command executed*, not the other column's.
- **§6.5 `E`'s rate** — 6, 11, 17, 22, 28, 34, 39 pitch clocks, with rates 6 and 7 **distinct**.
  `LSDJ_PARITY.md` §7 says otherwise and is superseded.

Three things are already known to be unsettled; settling any of them is a bonus, not a blocker:
`W` on a wave instrument, whether a phrase `H` counts repeats, and `HFF`'s exact semantics.
(Of these the phrase `H` is now settled — it counts, exactly as a table `H` does — and `W`'s two
wave-side variables are named from the ROM but still not seen doing anything.)

**Done when**: every §6 entry has been re-measured on 9.3.9 and either confirmed or corrected,
and §9's status table says so.

**Stage 1 is done.** Of nineteen entries, twelve confirmed and **seven corrected** --
`B` `E` `M` `S` `T` `W` `Z` -- and one open question closed (a phrase `H`'s high nibble counts
repeats, as a table's does). What each correction was is in `LSDJ_COMMAND_MATRIX.md` §9, and
§10 lists what they cost ChipBoy. Of the four entries this plan singled out, three held up —
`S`'s per-nibble formula, `B00` never playing, and `E`'s rate table, now read straight out of the
ROM at bank 02:`$698C` — and `Z`'s source rule did not. Two things to carry into stage 2: **read
the ROM early** (four of the eight corrections were invisible to a sweep of values), and **there
is no `START` blip**.

---

## Stage 2: sweep the older versions against 9.3.9

For each version, the question per command is one of three answers.

| Answer | What it means | Where it goes |
|---|---|---|
| **Same** | The register writes have the same shape and the same numbers as 9.3.9. | Point the model entry at 9.3.9's rules. Nothing else. |
| **Value** | The shape is the same, a constant differs. | A constant on `LsdjModel`, branched on in the **importer**. |
| **Kind** | The command does something 9.3.9's code cannot express with a different constant. | An enum on `LsdjModel`, a field the **instrument** carries so one bank can hold both, and a branch in the **driver**. |

`EnvelopeLaw` / `NoiseRule` / `NoiseS` / `PitchLaw` / `VibratoLaw` are the existing examples of
the third kind; `envPeriods` and `noiseMap` of the second.

### The order to do it in

Newest first — 9.2.J, 8.8.6, 8.5.1, 8.4.0, 7.x, 6.x, 5.x, 4.x, 3.x. Each older version is more
likely to differ, and doing them in order means each answer is "same as the one above" more
often than not, which is cheaper to record and cheaper to read.

### Per version

1. Confirm the format the ROM writes (`s[0x7FFF]`) and find or copy the `LsdjModel` entry.
2. Run the three rig checks above with that version's ROM and a save made in it.
3. Probe each command as §7 of the matrix describes, with the register to read per command in
   its table.
4. Record the answer in a per-version column, and set `LsdjModel::measured = true` only for what
   that ROM actually traced.
5. Add a case to `Tests/LsdjImportTests.cpp` reading a synthetic song under the new model.

### Known boundaries to check first

These are already established and are the fastest way to tell whether a version behaves as its
neighbours do (`docs/LSDJ_COMMAND_MATRIX.md` §4):

- **8.8.0** — before it the chip runs the envelope and an `E` re-attacks; after, LSDj steps the
  level itself and `E` never triggers. The *rate* is the same either way (§70).
- **8.9.3** — a table volume-column hop costs a tick before it, nothing after.
- **9.x** — a table's `G` walks the groove; before, the row holds the groove's first step.
- **format 11** — the command letters gain `B`, so every byte ≥ 2 shifts by one.
- **format 22** — `S` on noise becomes semitones through a map; before, nibble subtraction.
- **formats 0-3** — `P`, `L` and `V` work in period register units, not semitones.

### Flagging what cannot map

`notes.add(...)` in `Source/core/Import/LsdjSong.cpp`, per §8 of the matrix. A note names what
was in the save, where it was, and what ChipBoy did instead. Prefer converting silently when the
result is *identical*; note it when the result is only close; always note a drop.

**Done when**: every version in the archive has a `LsdjModel` entry whose `measured` flag is
honest, a per-version column in the matrix, and an import test.
