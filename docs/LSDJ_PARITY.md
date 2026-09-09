# LSDj parity: what the real thing writes, and what ChipBoy writes

Every number here was measured, on 2026-09-09, by playing a test song on an
LSDj 9.2.J ROM inside SameBoy's core and logging the writes to FF10–FF3F with
a CPU-cycle stamp, then playing the same song through ChipBoy's driver with
its own write log and comparing the two streams. The harness is
`tools/lsdjref/`; §31 of `docs/COMMANDS_AND_TEMPO.md` asked for it.

**The driver has since been made to match.** The verdict column below says what
each finding came to: where it reads *done* the law is now ChipBoy's, and §7 of
`docs/COMMANDS_AND_TEMPO.md` is written from these numbers. §16 is the
comparison case by case on both consoles, and §17 is what is still different
and why.

**Nothing of the ROM is reproduced here.** These are observations of behaviour:
register addresses, values and cycle counts. The ROM is the user's own copy, it
lives outside the repository, and no test runs it in CI.

Cycles are the 4.194304 MHz master clock, the same unit `driver::RegWrite`
counts in. Everything below is DMG unless it says otherwise; the console
differences are in §12. The tempo is 120 BPM throughout, where a tracker tick
is 87381 cycles (48 a second) on ChipBoy's side and 87374 on LSDj's — see §16.

Reproduce with:

```
cmake -S . -B build-ref -DCHIPBOY_LSDJREF=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-ref --parallel 4
CHIPBOY_LSDJ_ROM=/path/to/lsdj.gb ./build-ref/lsdjref/lsdjref-run
CHIPBOY_LSDJ_ROM=/path/to/lsdj.gb ./build-ref/lsdjref/lsdjref-run --model cgb
python3 tools/lsdjref/lsdjref_measure.py --traces build-ref/lsdjref/trace
```

---

## Summary of verdicts

| # | Item | Verdict | What was done |
|---|---|---|---|
| 1 | Note → period table | **agrees** | nothing; the fraction between two entries is now interpolated in period units, which is measurable (§3) |
| 2 | Note-on register order | **agrees** | nothing |
| 3 | NRx2 at a note-on: `F8` vs `F0` | **differed** | **done** — every NRx2 goes out with the low nibble 8, and the driver runs the envelope itself on §7's table |
| 4 | The pitch clock: 11712 vs 11651 cycles | **differed (0.5 %)** | **done** — 11712 cycles, and **one clock for the driver, free-running**: a note-on no longer restarts it |
| 5 | V vibrato rate | **differed by 12–22×** | **done** — 64/(x + 1) updates a cycle in Fast/Step/Drum; in Tick it is a **table, not a law**: the measured tick counts 96, 72, 64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4½, 4, 3, whose pattern (the period halves every three speeds) has no formula behind it yet |
| 6 | V vibrato depth and shape | **agreed on depth, differed on centre** | **done** — a symmetric triangle about the note; the depth **table** (⅛ … 8 semitones) is confirmed for all sixteen values, and in Drum one semitone is a measured 19.1 period units, again a number and not a law |
| 7 | L slide duration and domain | **differed** | **done** — x + 1 updates, linear in semitones, the trigger at the pitch it came from |
| 8 | P bend rate and domain | **differed by 10–40×** | **done — table, not law**: the measured step table (all 127 values), Fast/Tick/Step on the note and Drum on the period register, wrapping at 2048. The closed form in §5 is a fit to that table, good to a part in a hundred; the table is the fact |
| 9 | E and a table's volume: zombie vs retrigger | **differed** | **done** — `09 11 18` down and `08` up, byte for byte, and E never triggers |
| 10 | The envelope's own rate | **differed** | **done — table, not law**: the measured pitch-clock periods 6, 11, 15, 20, 27, 36, 36 for rates 1–7, stepped in software. They are within 11 % of what the chip's own envelope would have given, and no tidier expression fits |
| 11 | R retrigger interval | **differed** | **done** — y × (rate + 1) + 1 ticks, y = 0 every tick, x = 8 the pitch-clock resync, the whole note-on written again |
| 12 | K kill | **differed** | **done** — a zombie ramp to zero at the killing tick, the DAC left on |
| 13 | C chord and its rate | **agreed** | the root now plays on the note's own tick and the chord steps from the one after (measured) |
| 14 | A table's first row fires with the note | **agrees** (§31 confirmed) | nothing |
| 15 | A table volume row with a zero nibble | **differed** | **done** — the harness's mapping makes such a row blank, which is what ChipBoy's own `vol = −1` already meant |
| 16 | Bare notes | **agrees** | nothing |
| 17 | NRx4 written with every NRx3 | **differed** | **done** — both halves, every update |
| 18 | CGB | **agrees** | nothing; every verdict in §16 is the same on both consoles |
| 19 | LSDj's tempo against ChipBoy's clock | **differs by 88 ppm** | **deliberate** — LSDj counts its tempo in timer interrupts, so its tick is 87374 cycles and jitters by one whole interrupt; ChipBoy's clock is exact. §16 says how the comparison handles it |

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

> **Done.** The pitch clock is **11712 cycles** and §7 says the "360 Hz" is
> really 358.12 Hz, because a real driver gets it from a timer whose reload is
> an integer. It is also **one clock for the whole driver and free-running**: a
> note-on does not restart it, because a timer interrupt does not know a note
> began. The consequence is that where a note falls inside the period is where
> the player pressed play, which is why the harness's timing tolerance is a
> whole period (§16).

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

> **Done**, with two corrections the finer cases (`b_vib_depth`,
> `b_vib_speed_fast`, `b_vib_speed_tick`) made:
>
>  - The phase is a **six-bit counter stepping by x + 1**, so one cycle is
>    64/(x + 1) updates and **x = 0 is the slowest, not off**. The phase steps
>    *after* the write, so a note's first update writes the note itself.
>  - In **Tick** mode the cycle is **not** 64/(x + 1) ticks but a measured table:
>    96, 72, 64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4½, 4, 3 ticks for
>    x = 0…15 — the period halves every three speeds.
>
> The depth table is the manual's, and `b_vib_depth` confirmed it for **all
> sixteen** values at C-5: ±1796…1800, 1794…1802, 1792…1803, 1791…1805,
> 1787…1809, 1783…1812, 1775…1819, 1767…1825, 1759…1831, 1750…1837, 1741…1843,
> 1732…1849, 1714…1860, 1694…1871, 1673…1881, 1650…1890. The fraction of a
> semitone is interpolated **in period units** between two table entries, not in
> frequency: that is what puts depth 3's trough on 1791 rather than 1790, and it
> is now what `Driver::periodForNote` does.
>
> In **Drum** the swing is the period register's, not the note's: the same
> triangle and depths, with one semitone worth about **19.1 period units**
> whatever the note (measured from P's sweep at 19.110 and from V's depths at
> 19.1). The ±1-unit residue at the larger depths is not resolved — see §17.

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

> **Done**, with the arithmetic the ROM's own numbers pin down: the step is
> `(target − source) / (x + 1)` in **1/256 of a semitone, truncated toward
> zero**, so a little is usually left after the last step and one more update
> lands the pitch exactly on the note. `L 10`'s seventeenth step is 1797 and the
> eighteenth is 1798, which is only true of a truncated step. `L 00`'s one step
> is the whole distance, so it writes the period once and there is nothing left
> over. The note-on of a slide **triggers at the pitch the channel was at** —
> that is what makes it a portamento. `c_slide_fast` and `c_slide_tick` are
> write-for-write **identical** now.

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

**The whole sweep, all 127 values.** `d_bend_scale_all` and `d_bend_scale_up`
play every value from −127 to −1 and +1 to +64, two steps a note, and the rate
is symmetric in the sign. Read as a rate per update the table closes to

> **step = S(|x|) / 256 of a semitone**, where
> `S(m) = sum(ceil(j/4), j = 1..m) = (q + 1)(2q + r)` for `q = m / 4`,
> `r = m mod 4`.

which fits the measured rate at every value to about **one part in a hundred**
(the worst is 1.5 % at |x| = 1, where a rate of 0.073 period units an update is
measured over six steps of the register). That is the fit, stated as a fit: the
table itself is what the traces hold.

> **Done.** The step comes from that table; Fast and Tick bend the note, **Step
> applies one offset of x/32 of a semitone** — measured: `P 02` moves 1798 to
> 1799, `P 40` to 1825, `P C0` to 1767, all exactly x/32 of a semitone — and
> **Drum bends the period register and wraps at 2048**. Step's offset lands at
> the first pitch update, not in the note-on's own writes, because LSDj's
> note-on writes the pitch the channel was at and its commands move it from
> there. In Tick mode one tick's step is **four** of the pitch clock's, measured,
> and not the 7.46 a tick is worth in updates. `d_bend_step` is **identical**;
> the others differ by the fit's one per cent — see §17.

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

> **Done.** `09 11 18` for one step down and `08` for one step up, **byte for
> byte**, repeated to the target at the tick, with no NRx4 write — the search
> that used to put the target in the high nibble is gone. The spacing is the
> ROM's too: sixteen cycles inside a down-triple, a hundred and twelve between
> triples, sixty-eight between single ups. **E never triggers**, whatever it does
> to the direction or the rate. §27's shaped envelopes reach the chip the same
> way, and so does K, which is a zombie ramp to zero with the DAC left on.

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

> **Done.** Every NRx2 the driver writes has the low nibble 8 — a hold — and the
> driver steps the level itself off the pitch clock on this table, through §26's
> zombie writes. A note whose envelope reaches silence ends there.

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

> **Done.** `y × (rate + 1) + 1` ticks; `y` = 0 is every tick; `x` = 8 runs the
> retrigger on the pitch clock and writes only the level and the trigger, while a
> tick-driven retrigger writes **the whole note-on sequence again**. `x` is a
> **signed nibble** of volume change, 9–15 being down by 16 − x: `R A` steps the
> level down by six, not by two, which is what the trace shows (`F8`, `98`, `38`,
> `08`). What LSDj does at the floor is not resolved — see §17.

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
  ChipBoy has since given the chord a rate of its own (`COMMANDS_AND_TEMPO.md` §37);
  the harness sets it from LSDj's one CMD/RATE, so the comparison stands. Re-run on a
  9.3.9 ROM (§44): the same one step a tick.

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
| a table's volume column | the NRx2 byte | a level 0–15, **blank unless both nibbles are non-zero**: measured, a row whose amplitude or whose envelope nibble is zero writes nothing at all |
| A G | slot 0–31 | `a` = slot + 1 (ChipBoy numbers from 1) |
| H | `times, row` | `a` = times, `b` = row (section 34) |
| P | signed byte | `a` = the byte itself: ChipBoy stores P two's complement now (section 34) |
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

- **`cases.spec`** — 29 test songs in a line-oriented format, one `case` each:
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
  and diffs the two streams. It repeats each chain for as long as the capture
  runs, as LSDj loops a song, and lines the streams up note-on by note-on (§16). It links `chipboy_core` and nothing else: the
  Driver is what is under test and it knows no JUCE, so the song is built in
  memory rather than written as a `.cbsong`, whose writer lives in the plugin
  shell.
- **`lsdjref_measure.py`** — turns the traces into the tables above.

Six **finer cases** were added for this round, where the sparse ones pinned the
shape of a law and not its numbers: `d_bend_scale_all` and `d_bend_scale_up`
sweep P over every value from −127 to +64, `b_vib_depth` plays all sixteen
depths, `b_vib_speed_fast` and `b_vib_speed_tick` all sixteen speeds in both
clocks, and `f_table_rows` puts the same amplitudes behind different envelope
nibbles to ask whether the nibble is a row's length (it is not).

The two streams are aligned at **the first note-on**, which is not the first
trigger: before the song starts LSDj beeps its interface with a bare period
write and a trigger, so a note-on is defined as a trigger whose channel had its
level register written within the previous 4096 cycles. From there the writes
are compared per channel, value for value, with a 4096-cycle tolerance on the
timing, and the report says *identical*, *same values different timing* or
*different values* with the first divergence.

---

## 16. The comparison, case by case

`lsdjref-compare` plays each case through ChipBoy's driver and diffs the two
streams. This is the table it writes, on both consoles, with the driver as it
stands; reproduce it with the commands at the top of this file.

**How the two are lined up.** Both streams are anchored at the first note-on and
then again at **every note-on**, because two things that are not the driver's
behaviour separate them and would otherwise swamp everything that is:

- LSDj counts its tempo in timer interrupts, so a tick is a whole number of them
  and jitters by one either way — 11712 cycles, which is three times the old
  tolerance — while ChipBoy's clock is exact. The two tick rates also differ by
  88 parts per million (87374 cycles against 87381), which over a twenty-second
  capture is nine thousand cycles of drift.
- The pitch clock free-runs on both sides, so where a note falls inside its
  period is where the player pressed play. That is why the **timing tolerance is
  two whole periods, 23424 cycles**: one for that phase, and one more for an
  update one side fitted in and the other did not, which shifts everything after
  it by a period. Where such a shift happens the tool resynchronises by skipping
  the one NRx3/NRx4 pair and says how many it skipped.

Inside a note the comparison is strict: register for register, value for value,
in order. The verdicts are **identical** (same values, same order, every write
inside the tolerance), **same values, timing within tolerance** (the same, with
a trailing pitch update put down to the clock's phase, or writes past where the
shorter capture stopped -- both counted and named in the detail), **same values,
timing outside tolerance** and **different values**.

| case | channel | LSDj writes | ChipBoy writes | DMG | CGB |
|---|---|---:|---:|---|---|
| `a_baseline` | PU1 | 234 | 255 | identical | identical |
| `a_baseline` | global | 34 | 37 | identical | identical |
| `b_vib_fast` | PU1 | 13591 | 14669 | same values, timing within tolerance | same values, timing within tolerance |
| `b_vib_fast` | global | 19 | 21 | identical | identical |
| `b_vib_step` | PU1 | 13591 | 14669 | same values, timing within tolerance | same values, timing within tolerance |
| `b_vib_step` | global | 19 | 21 | identical | identical |
| `b_vib_drum` | PU1 | 13591 | 14669 | different values | different values |
| `b_vib_drum` | global | 19 | 21 | identical | identical |
| `b_vib_tick` | PU1 | 1901 | 2055 | different values | different values |
| `b_vib_tick` | global | 19 | 21 | identical | identical |
| `b_vib_depth` | PU1 | 12386 | 13467 | same values, timing within tolerance | same values, timing within tolerance |
| `b_vib_depth` | global | 18 | 19 | identical | identical |
| `b_vib_speed_fast` | PU1 | 12386 | 13467 | same values, timing within tolerance | same values, timing within tolerance |
| `b_vib_speed_fast` | global | 18 | 19 | identical | identical |
| `b_vib_speed_tick` | PU1 | 1734 | 1885 | different values | different values |
| `b_vib_speed_tick` | global | 18 | 19 | identical | identical |
| `b_vib_shapes` | PU1 | 4498 | 12262 | different values | different values |
| `b_vib_shapes` | global | 16 | 18 | identical | identical |
| `c_slide_fast` | PU1 | 360 | 413 | identical | same values, timing within tolerance |
| `c_slide_fast` | global | 24 | 27 | identical | identical |
| `c_slide_tick` | PU1 | 360 | 415 | identical | same values, timing within tolerance |
| `c_slide_tick` | global | 24 | 27 | identical | identical |
| `d_bend_step` | PU1 | 108 | 120 | identical | identical |
| `d_bend_step` | global | 16 | 18 | identical | identical |
| `d_bend_fast` | PU1 | 11176 | 12262 | different values | different values |
| `d_bend_fast` | global | 16 | 18 | identical | identical |
| `d_bend_tick` | PU1 | 1564 | 1720 | different values | different values |
| `d_bend_tick` | global | 16 | 18 | identical | identical |
| `d_bend_drum` | PU1 | 11176 | 12262 | different values | different values |
| `d_bend_drum` | global | 16 | 18 | identical | identical |
| `d_bend_scale` | PU1 | 14801 | 15871 | different values | different values |
| `d_bend_scale` | global | 21 | 23 | identical | identical |
| `d_bend_scale_fast` | PU1 | 14801 | 15871 | different values | different values |
| `d_bend_scale_fast` | global | 21 | 23 | identical | identical |
| `d_bend_scale_all` | PU1 | 23734 | 24784 | different values | different values |
| `d_bend_scale_all` | global | 128 | 134 | identical | identical |
| `d_bend_scale_up` | PU1 | 12641 | 13747 | different values | different values |
| `d_bend_scale_up` | global | 69 | 75 | identical | identical |
| `e_env_change` | PU1 | 708 | 876 | same values, timing outside tolerance | same values, timing outside tolerance |
| `e_env_change` | global | 8 | 9 | identical | identical |
| `e_env_rates` | PU1 | 708 | 745 | same values, timing within tolerance | different values |
| `e_env_rates` | global | 16 | 17 | identical | same values, timing outside tolerance |
| `f_table_volume` | PU1 | 5036 | 2397 | different values | different values |
| `f_table_volume` | global | 16 | 18 | identical | identical |
| `f_table_rows` | PU1 | 4244 | 1711 | different values | different values |
| `f_table_rows` | global | 19 | 21 | identical | identical |
| `g_bare_note` | PU1 | 2677 | 5874 | different values | different values |
| `g_bare_note` | global | 31 | 17 | same values, timing outside tolerance | same values, timing outside tolerance |
| `h_retrig` | PU1 | 2056 | 4147 | different values | different values |
| `h_retrig` | global | 19 | 21 | identical | identical |
| `i_kill_delay_chord` | PU1 | 577 | 1311 | different values | different values |
| `i_kill_delay_chord` | global | 25 | 27 | identical | identical |
| `j_groove_hop` | PU1 | 6792 | 2918 | different values | different values |
| `j_groove_hop` | global | 32 | 31 | same values, timing outside tolerance | same values, timing outside tolerance |
| `k_wave_kick` | WAV | 9452 | 7379 | different values | different values |
| `k_wave_kick` | global | 287 | 18 | different values | different values |
| `l_noise` | NOI | 122 | 137 | different values | different values |
| `l_noise` | global | 26 | 35 | same values, timing outside tolerance | same values, timing outside tolerance |

## 17. What is still different, and why

**Deliberate — ChipBoy's own design, documented as such in §2 and §7.**

- ~~**A table row is one tick in ChipBoy and two in LSDj.**~~ **Withdrawn**
  (`COMMANDS_AND_TEMPO.md` §44): `f_table_speed`, a table whose *transpose*
  column steps 0 2 4 6 8 10, wrote a new period **every tick** on an LSDj 9.3.9
  ROM (0.94 / 1.07 alternating, the frame jitter). The "two ticks" this
  section read off `F1 81 41 01` was the envelope nibble's timing, not the
  row's. A table row is one tick in both. `f_table_volume`, `f_table_rows` and
  `j_groove_hop` still differ, in how the volume column's envelope byte is
  read, not in the row length. (The `f_table_speed` trace loops after six rows
  where the case wrote eight, the zero row and the last two writing nothing
  visible — unexplained, and beside the row-length question.)
- **Noise.** LSDj maps the note column to NR43 by its own scheme and its `S` is
  a shape command; ChipBoy maps a note to a shift/divisor pair musically and
  gives `S` to PU1's sweep. §2 already says the noise letters are ChipBoy's own,
  so `l_noise` cannot be compared note for note.
- **F is absolute in ChipBoy** (a frame number) and **A reaches 64 slots**
  rather than LSDj's 32, so a case using either is a translation rather than a
  comparison. `k_wave_kick` differs partly for that and partly for the wave
  channel's own note-on order.
- **E's x on the wave channel** is ChipBoy's four NR32 levels, not a 16-level
  amplitude.

**Measured but not resolved — the harness can see the behaviour and not the
arithmetic behind it.**

- **P's last one per cent.** The step table fits every one of the 127 measured
  rates to about a part in a hundred (§5), and the six `d_bend_*` cases agree
  for hundreds to thousands of writes and then drift by one period unit. Fitting
  each value its own rate still leaves about a sixth of the writes off, so the
  residue is LSDj's own fixed-point arithmetic — or the interrupt it counts,
  which the ROM re-seeds once a frame — and not a rate this harness can read
  from outside.
- **V in Drum.** The swing is the period register's and about 19.1 units a
  semitone, which `b_vib_drum` reproduces in shape and amplitude, but the
  rounding at the larger depths leaves ±1 unit: the trace's swing is −153/+152
  about a note whose period is 1798, and no single base and rounding rule gives
  both.
- **V in Tick.** The measured table of tick counts (§3) is exact at the speeds
  that are multiples of three and one tick out at the others, where the cycle is
  a ninth of a tick long (72, 36, 18, 9, 4½ ticks); `b_vib_tick` and
  `b_vib_speed_tick` differ there.
- **The vibrato shapes saw and square** (§13): the ROM writes five to nine
  period writes in a second for them against the triangle's 360, which is
  neither a saw nor a square, so ChipBoy keeps its own one-sided shapes and
  `b_vib_shapes` differs by design until a case can read the shape off LSDj's
  own instrument screen.
- **R's resync** (`x` = 8) runs on the pitch clock as measured, but LSDj's stops
  after 38 of them — five ticks into a forty-eight-tick note — for a reason the
  register log does not show. ChipBoy's runs for the note, so `h_retrig`
  differs from that point on.
- **R's volume at the floor.** `R A` steps the level down by six, twice, and
  then the trace reads 0, 9, 3, 0 for a two-tick interval and 3, 9, 9, 9 for a
  five-tick one: two different behaviours at the floor from one rule. ChipBoy
  clamps at zero.
- **The software envelope's rate 6 and 7** measured the same interval (§7).
  Taken as measured; `e_env_rates` differs from rate 7 on.
- **The bare-note and kill cases** (`g_bare_note`, `i_kill_delay_chord`) agree
  through their first notes and part company later, where a note whose envelope
  has reached silence ends on LSDj and ChipBoy keeps the channel.
