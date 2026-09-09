# LSDj parity: what the real thing writes, and what ChipBoy writes

Every number here was measured, on 2026-09-09, by playing a test song on an
LSDj 9.2.J ROM inside SameBoy's core and logging the writes to FF10–FF3F with
a CPU-cycle stamp, then playing the same song through ChipBoy's driver with
its own write log and comparing the two streams. The harness is
`tools/lsdjref/`; §31 of `docs/COMMANDS_AND_TEMPO.md` asked for it.

**Nothing of the ROM is reproduced here.** These are observations of behaviour:
register addresses, values and cycle counts. The ROM is the user's own copy, it
lives outside the repository, and no test runs it in CI.

Cycles are the 4.194304 MHz master clock, the same unit `driver::RegWrite`
counts in. Everything below is DMG unless it says otherwise; the console
differences are in §12. The tempo is 120 BPM throughout, where a tracker tick
is 87381 cycles (48 a second) on both sides.

Reproduce with:

```
cmake -S . -B build-ref -DCHIPBOY_LSDJREF=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-ref --parallel 4
CHIPBOY_LSDJ_ROM=/path/to/lsdj.gb ./build-ref/lsdjref/lsdjref-run
python3 tools/lsdjref/lsdjref_measure.py --traces build-ref/lsdjref/trace
```

---

## Summary of verdicts

| # | Item | Verdict | Recommended change |
|---|---|---|---|
| 1 | Note → period table | **agrees** | none |
| 2 | Note-on register order | **agrees** | none |
| 3 | NRx2 at a note-on: `F8` vs `F0` | **differs** | write the hold nibble as 8, not 0 |
| 4 | The pitch clock: 11712 vs 11651 cycles | **differs (0.5 %)** | move the 360 Hz clock to 11712 |
| 5 | V vibrato rate | **differs by 12–22×** | one cycle is 64/(x+1) updates, not 720/x |
| 6 | V vibrato depth and shape | **agrees on depth, differs on centre** | the swing is ±depth about the note |
| 7 | L slide duration and domain | **differs** | x+1 updates, linear in semitones |
| 8 | P bend rate and domain | **differs by 10–40×** | a measured table, and Fast is logarithmic |
| 9 | E and a table's volume: zombie vs retrigger | **differs** | zombie-step, one level at a time |
| 10 | The envelope's own rate | **differs** | a measured table of timer periods |
| 11 | R retrigger interval | **differs** | y × (rate + 1) + 1 ticks; y = 0 is not "once" |
| 12 | K kill | **differs** | zombie-ramp to zero at the killing tick |
| 13 | C chord and its rate | **agrees** | none |
| 14 | A table's first row fires with the note | **agrees** (§31 confirmed) | none |
| 15 | A table volume row with a zero low nibble | **differs** | such a row writes nothing at all |
| 16 | Bare notes | **agrees** | none |
| 17 | NRx4 written with every NRx3 | **differs** | write both halves every update |
| 18 | CGB | **agrees** | none |

---

## 1. The clock, the tick and the note table

**LSDj.** The timer is set once, at boot, and never changed: `TMA = $49`,
`TAC = $06`. That is the 65536 Hz clock divided by 183, so the interrupt is
every **11712 cycles = 358.12 Hz**. It does not move with the tempo — a song
at 60, 120 or 240 BPM writes the same two bytes and nothing else. The tempo is
counted in software from that interrupt, and TIMA is re-seeded once a frame
(`TIMA = $B8` every 70224 cycles), which is why the measured mean interval
between pitch updates is 11679 rather than 11712.

The tracker tick therefore lands on a multiple of the timer: at 120 BPM
successive notes eight steps apart were 4189812 and 4201536 cycles apart in the
same song — 48.00 ticks on average, ±1 timer period of jitter.

**The note table.** LSDj rounds to the nearest period, and ChipBoy's
`periodForNote` produces the same numbers for every note tested:

| note | MIDI | LSDj byte | period | Hz | ChipBoy |
|---|---:|---:|---:|---:|---:|
| C-2 | 36 | 1 | 44 | 65.41 | 44 |
| C-3 | 48 | 13 | 1046 | 130.81 | 1046 |
| C-4 | 60 | 25 | 1547 | 261.62 | 1547 |
| A-4 | 69 | 34 | 1750 | 439.84 | 1750 |
| A#4 | 70 | 35 | 1767 | 466.45 | 1767 |
| B-4 | 71 | 36 | 1783 | 494.61 | 1783 |
| C-5 | 72 | 37 | 1798 | 524.29 | 1798 |
| C-6 | 84 | 49 | 1923 | 1048.58 | 1923 |
| G-7 | 103 | 68 | 2006 | 3120.76 | 2006 |

So **LSDj's note column byte is the MIDI note minus 35**: byte 1 sounds 65.41 Hz.
LSDj's own display calls that "C 3", one octave above scientific pitch.

**ChipBoy.** The pitch clock is 11651 cycles (360.0 Hz exactly, `§7`).
**Verdict: 0.52 % fast.** Over a two-second note that is one update in 190.

> **Recommended change.** Make the Fast/Step/Drum pitch clock **11712 cycles**
> and say in §7 that the "360 Hz" is really 358.12 Hz, because a real driver
> gets it from a timer whose reload is an integer.

## 2. The note-on

LSDj plays a plain pulse note as five writes, in this order, about 60 cycles
apart, and then the pan:

```
NR10 = 00      the instrument's SWEEP, complemented (see below)
NR11 = 80      duty in bits 7-6, length 0
NR12 = F8      the instrument's ENV amplitude, with the low nibble forced to 8
NR13 = 0B      period low
NR14 = 86      period high + trigger
NR51 = FF      ~380 cycles later
```

ChipBoy writes **the same five registers in the same order** and `NR51` after
them. Two differences:

- **`NR12` is `F8` on LSDj and `F0` on ChipBoy.** The instrument's ENV byte was
  `F0`; LSDj replaces the low nibble with `8` — amplitude 15, direction *up*,
  period 0. Both mean "the hardware envelope does not run", but the direction
  bit is not cosmetic: it is the state every later zombie write starts from
  (§9). Measured across every envelope tested: ENV `A3` → `NR12 = A8`, ENV `8B`
  → `88`, ENV `09` → `08`. **LSDj never lets the hardware envelope run at all.**
- **LSDj writes the period again 2532 cycles after the trigger**, without the
  trigger bit (`NR13 = 0B`, `NR14 = 06`), on every note, even with no vibrato,
  slide or bend. That is the first pitch update after the note-on landing
  inside the same tick. ChipBoy writes it once.

**The instrument's SWEEP field is NR10's complement.** With the field at `00`
LSDj wrote `NR10 = FF`; with `36` it wrote `C9`; with `FF` it wrote `00`. So
`NR10 = 255 − field`, and `FF` in the field is "no sweep". Worth knowing when
an instrument import is written.

## 3. V vibrato

The LFO is a **64-step triangle in pitch**, and the speed is the phase step:

- **One cycle is 64/(x + 1) pitch updates.** Measured cycle lengths at
  PITCH = FAST: x = 1 → 32 updates (11.2 Hz), x = 4 → 12.8 (28.1 Hz),
  x = 8 → 7.1 (50.5 Hz), x = 15 → 4.0 (89.8 Hz). At x = 15 the sequence is
  literally `1798 1650 1798 1890` repeating — four samples a cycle.
- **Linear in semitones, not in period units.** At x = 1, depth 15 the period
  went 1798 1783 1767 1750 1732 1714 1694 1673 1650: exactly one semitone per
  update, eight updates to the trough.
- **The depth is the amplitude either side of the note**, and it is the
  manual's table. Measured at C-5 (period 1798), PITCH = FAST:

  | depth | period swing | semitones | manual's table |
  |---:|---|---:|---:|
  | 0 | 1796 … 1800 | ±0.14 | ⅛ |
  | 3 | 1791 … 1805 | ±0.48 | ½ |
  | 8 | 1759 … 1831 | ±2.48 | 2½ |
  | 15 | 1650 … 1890 | ±8.00 | 8 |

- **The direction bit chooses which way the swing starts, not where it swings.**
  Shape triangle, direction down: `1798 1759 1714 1662 …` (down first, then up
  to 1890). Direction up: `1798 1831 1860 1886 …` (up first, then down to 1650).
  Both cover ±depth.
- **PITCH = TICK is the same LFO clocked by the tracker tick**: 49 updates in a
  one-second note at 120 BPM, spacing 87206 cycles.
- **PITCH = STEP and DRUM behave as FAST for V**: the same 359 updates a second
  and the same swings.

**ChipBoy today.** §7 says one cycle is 720/x updates — 0.5 Hz at x = 1 and
4 Hz at x = 8. Measured in the same harness: at V x = 1 ChipBoy writes *no*
period at all inside a one-second note, and at x = 4 it writes 40 in a second
against LSDj's 359. §7 also says "Down moves between the note and note − depth",
which would halve the swing and move its centre.

> **Recommended change.** One vibrato cycle is **64/(x + 1) pitch updates**
> (Fast/Step/Drum) or **64/(x + 1) ticks** (Tick). The waveform is a triangle in
> the note's own 1/32-semitone units, amplitude = the depth table, **centred on
> the note**; the direction only sets the starting half. §7's "720/x" and its
> "note to note − depth" wording both go.

## 4. L slide

- **The slide takes x + 1 pitch updates.** L `04` → 5 updates, L `10` → 17,
  L `00` → one write, straight to the target.
- **It is linear in semitones.** L `04` from C-4 to C-5 wrote
  `1612 1668 1718 1760 1798` — 2.40 semitones each time, five equal steps
  covering an octave. In period units those steps are 65, 56, 50, 42, 38:
  visibly not equal.
- The unit is the pitch update in FAST (11694 cycles measured) and the tracker
  tick in TICK (85800–87210 cycles measured), as documented.

**ChipBoy today.** L `04` writes four steps of 63 period units, L `10`
sixteen steps of 16 — the right count minus one, linear in the wrong domain.

> **Recommended change.** `x + 1` updates, interpolating the note in semitones
> (`noteFine`), not the period. §7's "linear in period units, or in semitones in
> Drum mode" becomes "always in semitones".

## 5. P bend

This is the largest disagreement and the least tidy finding.

**The rate is not proportional to the value.** Measured from C-6 (period 1923)
with PITCH = DRUM, where the motion is a straight line in period units so the
rate reads directly off the trace:

| P | value | period units per update | semitones per update |
|---|---:|---:|---:|
| `FF` | −1 | 0.10 | 0.014 |
| `FE` | −2 | 0.15 | 0.021 |
| `FC` | −4 | 0.30 | 0.041 |
| `F8` | −8 | 0.90 | 0.116 |
| `F0` | −16 | 2.95 | 0.328 |
| `E0` | −32 | 10.70 | 0.818 |
| `C0` | −64 | 40.60 | 1.540 |
| `81` | −127 | 155.27 | 3.085 |

The rate is linear in the value up to about |x| = 4 and grows roughly with the
**square** of it after that: `rate ≈ x²/108 + x/26` period units per update fits
every row from x = 4 to x = 127 within a few percent. Whether that is the shape
of LSDj's own arithmetic or the visible part of something else, this harness
cannot say from outside; the table is the fact.

**The domain differs by pitch speed.**

- **DRUM moves the period in a straight line** and wraps at 2048: P `40` from
  C-5 wrote `1798 1838 1879 1919 1960 2001 2041 19 59 100 …`, a constant +40.5
  per update straight through the top of the register. That is *not* the
  logarithmic fall the manual describes.
- **FAST moves the pitch in a straight line**: P `C0` from C-5 wrote
  `1798 1765 1728 1686 1639 1585 …` — a constant −2.14 semitones per update,
  and the *period* step grows from 33 to 224 as it falls.
- **STEP is an immediate one-off offset**, exactly as documented: two period
  writes and nothing more. P `02` moved 1798 → 1799, P `40` moved 1798 → 1825,
  P `C0` moved 1798 → 1767. So STEP's offset also uses the table above, applied
  once.
- **TICK is FAST clocked by the tracker tick**: 49 updates a second, same shape.

**ChipBoy today.** P is `x − 128` period units per update in Fast and Tick, an
immediate offset of that in Step, and `(x − 128)/16` semitones in Drum. Measured
against LSDj: ChipBoy's P `02` moves +2 units per update where LSDj moves 0.05;
P `10` moves +16 where LSDj moves 1.3. **ChipBoy's P is between 12× and 40× too
fast**, and Fast/Drum have their domains the wrong way round.

> **Recommended change.** Take the rate from the measured table (interpolating
> for untested values, or measuring the remaining 120 with the same case), and
> swap the domains: **Fast, Tick and Step move the note in semitones; Drum moves
> the period in units and wraps.** §7's P paragraph and §2's P row both change.

## 6. E, table volumes, and the zombie question

**§26 is right, and this is what it looks like.** LSDj never triggers a channel
to change its level. It changes the volume one step at a time with NRx2 writes,
about 112 cycles apart:

- **down one step: three writes, `09` `11` `18`.** Measured on a channel at
  volume 15 the APU's volume goes 15 → 0 → 0 → 14; the two intermediate values
  last 32 cycles (8 µs) and never reach a sample.
- **up one step: one write, `08`.** Volume 15 → 16 → 0 is never seen; the
  measured sequence climbs 1, 2, 3, … one per write.

`E 8 0` on a channel sounding at 15 produced **seven** down-triples back to
back, 15 → 8, at the tick the command fired. `E 4 0` after it produced four
more, 8 → 4. `E 0 9` produced fifteen, 15 → 0, then the channel rose again at
rate 1. So:

> **E x y sets the amplitude to x by |current − x| zombie steps issued at the
> command's own tick, and y sets the direction and rate of what happens next.
> There is no trigger and no phase reset.**

**A table's volume column is the same byte and the same mechanism.** A row of
`81` on a channel at 15 produced seven down-triples; a row of `41` four more.

**A row whose low nibble is zero writes nothing at all.** A table of
`F0 80 40 00` — the obvious way to write "15, 8, 4, 0" — produced not one NRx2
write in a two-second note. The same table written `F1 81 41 01` produced the
full ramp. The volume column is the NRx2 byte, amplitude in the high nibble and
the envelope nibble in the low, and the low nibble is what makes the row exist.

**ChipBoy today.** For the same table ChipBoy emitted **255 note-ons** in a
twenty-second capture — it retriggers the channel at every volume row — and for
`E 8 0` it writes `NR12 = 80` once. The single write is itself a zombie write on
real hardware, but it does not land on 8: writing NRx2 with a non-zero period
while a channel runs does not load the amplitude.

> **Recommended change.** Exactly §26, with the sequences above as the driver's
> primitive: `09 11 18` for one step down and `08` for one step up on DMG,
> repeated to the target, at the tick, with no NRx4 write. §27's shaped
> envelopes reach the chip the same way.

## 7. The instrument's own envelope rate

LSDj runs the envelope in software off the same 11712-cycle timer. Measured
step intervals, one note held for sixteen steps per instrument:

| ENV low nibble | direction | timer periods | cycles | Hz | hardware would be |
|---:|---|---:|---:|---:|---:|
| 1 | down | 6 | 70132 | 59.8 | 65536 |
| 2 | down | 11 | 128684 | 32.6 | 131072 |
| 3 | down | 15 | 175480 | 23.9 | 196608 |
| 4 | down | 20 | 233976 | 17.9 | 262144 |
| 5 | down | 27 | 315964 | 13.3 | 327680 |
| 6 | down | 36 | 421240 | 10.0 | 393216 |
| 7 | down | 36 | 421224 | 10.0 | 458752 |
| 9 (up 1) | up | 6 | 70132 | 59.8 | 65536 |
| B (up 3) | up | 15 | 175488 | 23.9 | 196608 |
| F (up 7) | up | 36 | 421304 | 10.0 | 458752 |

So LSDj's software envelope is within about 11 % of the rate the hardware
envelope would have had, and rates 6 and 7 measured the same — worth one more
run before that is taken as certain.

> **Recommended change.** When ChipBoy stops using the hardware envelope (§26),
> step the level on this table rather than on r/64 s, so a ChipBoy song and an
> LSDj song with the same instrument decay together.

## 8. R retrigger

Measured with CMD/RATE 0, one note per window:

| R | triggers in a note | interval | ticks |
|---|---:|---:|---:|
| `01` | 26 | 167600 | 1.92 → **2** |
| `04` | 10 | 436489 | **5.00** |
| `21` | 26 | 167600 | 2 (x changes only the level) |
| `24` | 11 | 418574 | 4.79 → **5** |
| `A1` | 24 | 174869 | **2.00** |
| `00` | 49 | 87203 | **1.00** |
| `80` | 38 | 11703 | **0.134** (the pitch clock) |

and with CMD/RATE 3, R `04` retriggered every 1481232 cycles = 16.95 ticks.

> **The interval is `y × (rate + 1) + 1` ticks.** y = 0 is *not* "once": it
> retriggers every tick. x = 8 is LSDj's resync — the retrigger runs at the
> **pitch clock**, 11712 cycles, not at a tick.

`R A y` writes a new NRx2 with the trigger each time (`38`, `08`, `98` …), so
the volume modulation rides on the retrigger rather than being zombie-stepped.

**ChipBoy today.** R `01` retriggers every 87381 cycles — one tick, not two —
and §2 says y = 0 retriggers once.

> **Recommended change.** `y × (rate + 1) + 1` ticks; `y = 0` is every tick;
> `x = 8` is the pitch clock. §2's R row and §7's R sentence both change.

## 9. K, D and C

- **K** `02` began its work 174968 cycles after the note-on — 2.00 ticks — and
  `06` at 526196 — 6.02 ticks. The kill is **a zombie ramp to zero**, fifteen
  down-triples 112 cycles apart, about 1700 cycles end to end. Not a DAC clear,
  not a trigger. ChipBoy kills by dropping the DAC.
- **D** delays the note by its value in ticks, as documented.
- **C** steps the chord **one tick per step** at CMD/RATE 0 and **every
  rate + 1 ticks** otherwise (measured 4 ticks at rate 3). `C 3 7` wrote periods
  1798, 1837, 1881 — 0, +3, +7 semitones exactly. `C C 0` wrote 1798, 1923 —
  a two-step cycle, 0 and +12. `C 0 0` stopped it. **This agrees with ChipBoy.**

## 10. Tables, bare notes and hops

- **A table's first row fires with the note-on.** A table whose row 0 sets
  amplitude 0 began its zombie ramp **2932 cycles after the trigger** — a
  thirtieth of a tick, in the same event. §31's first rule is confirmed against
  the real thing.
- With a volume column of `F1 81 41 01` the amplitude changes came 175960,
  351512 and 527192 cycles after the note-on: **two ticks apart**, not one.
  Whether that is the low nibble acting as the row's length or a table speed of
  two ticks is not settled by this run — see §13.
- **A bare note** (a note with a blank instrument column) writes the period and
  nothing else: no NRx0/1/2, no trigger. Measured over a phrase of
  plain-then-bare notes, the vibrato in force kept its phase across the bare
  notes and the envelope kept running. **ChipBoy agrees.**
- **H** in a table loops the run; **G** in a table sets its row lengths from the
  song's groove. Both fire, but neither writes a register on its own, so the
  harness only sees them through what they change.

## 11. Wave and noise

- **Wave.** LSDj's note-on writes the sixteen wave bytes with the DAC off, then
  `NR30 = 80`, then `NR33`/`NR34` with the trigger — the DMG wave dance, as
  ChipBoy does it. Its P kick table then wrote **4704 NR33 and 4704 NR34**
  writes in a twenty-second capture; ChipBoy wrote 78 and 38.
- **NR34 is written with every NR33.** LSDj writes both halves of the period at
  every update whether or not the high bits moved. ChipBoy writes NRx4 only when
  the high bits change — which is fewer writes for the same sound on an
  emulator, but not what a Game Boy driver does, and it makes the two streams
  impossible to line up write for write. Same on the pulse channels.
- **Noise.** LSDj writes `NR41`, `NR42`, `NR43`, `NR44` at every note-on and
  `S x y` writes `NR43` alone. The note column maps to `NR43` by LSDj's own
  scheme (the same note produced `DA`, `8B`, `40`, `8D` under different S
  values); ChipBoy maps a note musically to a shift/divisor pair and wrote `04`.
  §2 already says ChipBoy's noise letters are its own, so this is a documented
  divergence rather than a defect — but the two cannot be compared note for
  note.

## 12. DMG against CGB

The a_baseline, f_table_volume, e_env_change, b_vib_fast and h_retrig cases were
traced on both. **Every register value and every ordering is identical.** The
only difference is that LSDj puts the CGB in double speed, so the writes inside
one note-on are half as far apart in real time (the period rewrite after the
trigger is at +1266 cycles instead of +2532) while the musical timing — one
second between notes — is unchanged. Nothing in the findings above is
console-specific.

The CGB also needs a later START: LSDj is on the song screen about two seconds
after a DMG reset and about four after a CGB one, so the harness presses at
frame 180 and 300 respectively.

## 13. What could not be measured

- **The vibrato shapes saw and square.** Setting the instrument's shape bits to
  1 or 2 produced only five to nine period writes in a one-second note against
  the triangle's 360, which is neither a saw nor a square. Either LSDj writes
  the period only when the value changes for those shapes, or the shape bits in
  byte 5 of a 9.2.J pulse instrument are not the two bits the manual's SRAM
  chapter documents. The case (`b_vib_shapes`) is in the spec for a follow-up
  that reads the shape back off LSDj's own instrument screen.
- **The exact law behind P's rate.** The table in §5 is solid; the formula
  behind it is a fit. Running `d_bend_scale` over all 127 values would give a
  lookup table and settle it without needing the formula.
- **The table row length.** §10 measured two ticks a row with a volume column
  whose low nibble was 1. Separating "the low nibble is the row's length" from
  "a table row is two ticks" needs one case with the same amplitudes and
  different low nibbles.
- **Envelope rates 6 and 7** measured the same interval, which is either true or
  an artefact of a note that ended before the envelope did.
- **Anything needing LSDj's screen.** The harness scripts the joypad and reads
  registers; it does not read LSDj's interface, so a case that depends on what a
  screen shows (a groove's swing display, an instrument's field names) is out of
  reach. `lsdjref-trace --screen` dumps the framebuffer as a PGM, which is how
  the key sequence was found, but nothing is parsed from it.
- **Kits and the speech instrument**, which need sample data in the save that
  this harness does not author.

## 14. The mapping the compare tool uses

So that a difference is a real difference and not a translation error, this is
how a test case's LSDj value becomes a ChipBoy one. It is `commandOf()` in
`tools/lsdjref/compare/main.cpp`.

| letter | LSDj | ChipBoy |
|---|---|---|
| C E M R S V Z | one byte, x in the high nibble | `a` = high nibble, `b` = low |
| A G | slot 0–31 | `a` = slot + 1 (ChipBoy numbers from 1) |
| H | step 0–15 | `a` = step + 1 |
| P | signed byte | `a` = (byte + 128) & 255 |
| D K L T W | the byte | `a` = the byte |

A phrase's command column holds the letter's position in LSDj's own order with
no gap for B: A = 1, C = 2, D = 3, E = 4, F = 5, G = 6, H = 7, K = 8, L = 9,
M = 10, O = 11, P = 12, R = 13, S = 14, T = 15, V = 16, W = 17, Z = 18. This was
measured by writing each byte in turn and watching which registers moved.
liblsdj shifts everything up by one from save format 8 on and puts B at 1; that
is not what 9.2.J stores, and using its table turns every V into a W.

Instruments map as: the ENV byte's high nibble is the volume and the low nibble
is 0/8 → hold, 1–7 → down at that rate, 9–15 → up at rate − 8; the SWEEP field
is NR10's complement; `out` bit 0 is the right channel and bit 1 the left; the
pitch bits are byte 5 bit 7 (step), bit 6 (drum), bit 4 (tick).

## 15. The harness

`tools/lsdjref/` holds four pieces and a spec.

- **`cases.spec`** — 23 test songs in a line-oriented format, one `case` each:
  instruments, tables, phrases and a chain per channel. It is read by both the
  save writer and the compare tool, so there is one description of each test.
- **`lsdjref_sav.py`** — writes one `.sav` per case. The working-memory song is
  the first 32 KB of an LSDj save and is not compressed, so a case is written
  into a copy of a **base save that LSDj itself formatted**: boot the ROM with
  no save at all, let it run its cartridge test and lay out its own SRAM
  (about fifty seconds of emulated time), and save the battery. Every reserved
  field, the save format version byte and every default are then this exact ROM
  build's rather than a guess.
- **`lsdjref_trace`** — SameBoy's core with `GB_set_write_memory_callback` on
  FF04–FF07 and FF10–FF3F, a cycle stamp from the core's own counter, a scripted
  joypad and a CSV out. `--init-sav` captures the base save; `--screen` dumps the
  framebuffer, which is how the START key and its timing were found.
- **`lsdjref_compare`** — builds each case as a ChipBoy `Song` and `Bank`,
  plays it through the Clock, the Player and the Driver with the write log on,
  and diffs the two streams. It links `chipboy_core` and nothing else: the
  Driver is what is under test and it knows no JUCE, so the song is built in
  memory rather than written as a `.cbsong`, whose writer lives in the plugin
  shell.
- **`lsdjref_measure.py`** — turns the traces into the tables above.

The two streams are aligned at **the first note-on**, which is not the first
trigger: before the song starts LSDj beeps its interface with a bare period
write and a trigger, so a note-on is defined as a trigger whose channel had its
level register written within the previous 4096 cycles. From there the writes
are compared per channel, value for value, with a 4096-cycle tolerance on the
timing, and the report says *identical*, *same values different timing* or
*different values* with the first divergence.
