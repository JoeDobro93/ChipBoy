# Plan: a table's three lanes, and the wave instrument's frame run

Round 15. Two features, both measured on the user's own LSDj saves by patching bytes into
a save's *working* area (`/root/lsdj/work.py`) so one ROM plays one song twice with a single
byte changed. Written before the code, per `CLAUDE.md`.

The design sections are `docs/COMMANDS_AND_TEMPO.md` §64 (the lanes) and §65 (the wave run);
this file is the layout, the signatures, the edge cases and the tests.

---

## 1. What LSDj does

### 1.1 A table has three lanes, each with its own row pointer

LSDj's changelog, v1.3.0B (2001):

> the both table command columns now run & loop independently. example: when used in tables,
> the hop ("H") command will affect transpose + left command column if issued in the left
> command column. the command will affect the right command column if used in the right
> command column. both command columns still use the same groove, tho'.

and v1.3.0 for the envelope column:

> implemented carillon-style envelope column in tables. (it's the leftmost column...) the
> first digit sets amplitude, the second digit sets duration.

So the lanes are

| lane | columns | steps on | hops on |
|---|---|---|---|
| **E** | ENV | its row's own duration | its row's `xF` |
| **1** | TSP + CMD1 | the table's row length (§57, §63) | an `H` in CMD1 |
| **2** | CMD2 | the table's row length | an `H` in CMD2 |

Measured on 8.4.4 with a table whose TSP walked every tick and whose ENV rows lasted four:
the pitch moved every ~6.5 pitch clocks while `NR22` moved every ~25.9. The two are
independent. §62 has the command columns' half.

### 1.2 The ENV byte

High digit = the amplitude 0-F, low digit = the duration. Measured on 8.4.4, one `NR22` write per lane step:

| low digit | what happens |
|---|---|
| `0` | the lane is dead: nothing is written at all |
| `1`-`E` | the amplitude holds that many **ticks** (`1` = 6.5 pitch clocks, `E` = 89.7, exactly n x one tick) |
| `F` | **hop**: the lane jumps to the row the *high* digit names |

A hop row costs one tick before 8.9.3 and nothing from 8.9.3 on ("table envelope hops done by
setting second digit to H now happen immediately"), both measured. ChipBoy hops for free, the
newer rule, and the importer gives an older save's hop row a LEN of 1 to buy the tick back.

Amplitude `0` is a real amplitude (silence), not a blank -- `01` writes `NR22 = 00`.

The lane **ends at its first empty row**: with rows 0 and 1 set, row 2 empty and row 3 set,
LSDj plays two and stops, never reaching row 3 and never looping back. Only its own hop brings
it round.

### 1.3 The wave instrument's frame run

Instrument bytes, measured by sweeping every one of the sixteen and watching wave RAM:

| byte | field | values |
|---|---|---|
| 2 (before 9) / 3 (from 9) | synth << 4 \| **LOOP POS** | §60 read the low nibble as a *start* frame; it is the loop point |
| 9, bits 0-1 | **PLAY** | 0 MANUAL (no advance), 1 ONCE, 2 LOOP, 3 PING-PONG |
| 10, low nibble | **LENGTH** `n` | the run visits `L = 16 - n` frames; `n = F` freezes on one |
| 11 | **SPEED** `s` | a frame every `s + 4` ticks |

Bits 2-7 of byte 9, byte 10's high nibble and bytes 3, 5, 12-15 move nothing.

**The run always starts at frame 0** and visits `L` frames spread across the synth's sixteen:

```
frame(i) = min(15, (i * 16) / (L - 1))        i = 0 .. L-1, integer division; L = 1 gives frame 0
```

Exact on every length measured (L = 16, 10, 8, 6, 4, 2, 1: `0 5 10 15` for L = 4,
`0 2 4 6 9 11 13 15` for L = 8).

**LOOP POS counts from the synth's sixteen, not from the run**: the loop covers the last
`16 - LOOP POS` steps of the run, clamped to the run.

| LENGTH | LOOP POS | loops from run step |
|---|---|---|
| 16 | 11 | 11 |
| 8 | 9 | 1 |
| 8 | 4 | 0 (the whole run) |
| 4 | 5 | 0 |

PING-PONG bounces between that step and the run's end (`0 5 10 15 10 5 0 ...`).

---

## 2. What ChipBoy gets

ChipBoy is not LSDj and may go past it; the rule is that anything LSDj can do, ChipBoy can.

### 2.1 `bank::TableStep` (Bank.h)

```cpp
struct TableStep {
    int8_t  vol = -1;          ///< -1 blank, else 0-15: the volume lane's level at this row
    uint8_t volTicks = 0;      ///< 0 = as long as the table's row, 1-15 = that many ticks
    int8_t  volHop = -1;       ///< -1 none, else 0-15: the row the volume lane hops to
    bool    hasTranspose = false;
    int8_t  transpose = 0;
    Command cmd1, cmd2;
};
```

`volTicks = 0` is what every song written before this round has, and it makes the volume lane
step with the table's row -- what ChipBoy did when there was one pointer. The lanes are still
separate, so an `H` in CMD1 no longer drags the volume column with it; that is the one
behaviour change to an existing song and it is recorded in `CHANGES.md`.

### 2.2 `bank::InstrumentCore`, the wave group (Bank.h)

```cpp
uint8_t  wave = 1;               ///< wave slot 1-64
uint8_t  frameLength = 0;        ///< 0 = every frame, else 1-16 spread across the run
uint8_t  frameLoopStep = 0;      ///< the run step Loop and PingPong return to
uint8_t  frameAdvance = 0;       ///< ticks per frame, 0 holds
FrameLoop frameLoop = FrameLoop::Loop;
```

`waveFrame` (§60's "start frame") **goes away**: it was a misreading of LSDj's LOOP POS and no
released song used it. `frameLoopStep` is a *run* step, not a frame -- the importer does the
`16 - LOOP POS` arithmetic once, so ChipBoy's own field means one plain thing.

`frameLength` picks its frames with the integer rule in §1.3, which is the run LSDj plays and
is a useful shaper in its own right (four frames of a sixteen frame morph, evenly spread).

### 2.3 `Driver::Voice` (Driver.h)

Three pointers where there was one, and a wait for each:

```cpp
uint8_t  tableStep = 0, tableRow = 0;        ///< lane 1: TSP and CMD1
uint8_t  tableStep2 = 0, tableRow2 = 0;      ///< lane 2: CMD2
uint8_t  tableStepE = 0, tableRowE = 0;      ///< lane E: VOL
uint16_t tableWait = 0, tableWait2 = 0, tableWaitE = 0;
uint8_t  hopLeft = 0,  hopFrom = 0xFF;       ///< lane 1's H counter
uint8_t  hopLeft2 = 0, hopFrom2 = 0xFF;      ///< lane 2's
```

`stepTable(ch)` splits into `stepTableLane(ch, lane)`; the tick decrements each wait and
steps the lane whose wait ran out. `applyCommand` takes the lane so an `H` moves the right
pointer: its `fromTable` argument becomes `int lane` (-1 not a table, 1 or 2 the column).

The table's row *length* stays shared, as LSDj says: `tableRowTicks(ch, row)` is asked for
each lane with that lane's own row, and lane E overrides it with `volTicks` when set.

### 2.4 Wave frames (Driver.cpp)

`frameRun(const Wave&, const InstrumentCore&)` gives the run: a `std::array<uint8_t, 16>` of
frame indices and its length. The voice walks *run steps*, so `frameIdx` becomes
`frameStep` and the frame loaded is `run[frameStep]`. `Loop` and `PingPong` turn at
`frameLoopStep` instead of 0. `F` (the frame command) names the frame itself, run or no run
-- measured -- and the run step goes to the nearest of them.

### 2.5 The importer (LsdjSong.cpp)

* ENV byte: `lo == 0` -> the row is blank; `lo == 15` -> `volHop = hi`; else `vol = hi`,
  `volTicks = lo`. The "fade speed per row is not mapped" note goes.
* Wave instrument: `frameLoopStep = max(0, L - (16 - loopPos))`, `frameLength = L`,
  `frameAdvance = speed + 4`, `frameLoop` from PLAY (MANUAL -> `frameAdvance = 0`). The
  "PLAY / SPEED / LENGTH frame animation is not mapped" note goes.
* §62's dropped `H` in CMD2 **comes back**: ChipBoy has the second pointer now, so the hop is
  carried. Every format ChipBoy reads (0 to 22, LSDj 3.1.5 on) is after v1.3.0B, so no format
  wants the columns locked together.

### 2.6 The UI

* `TableGrid` gains a **Len** column between Vol and Trans: blank, `1`-`15`, or `H0`-`HF`
  for the lane's hop. `Kind::VolLen`, beside `Kind::Vol` in `cellText`, `valueAt`,
  `setValue`, the typed entry, the nudge and the tooltip.
* `InstrumentPanel`'s Wave group: **Frames** (length, 0 = all) and **Loop from** (run step)
  replace **Start frame**; **Frame advance** keeps its name and stays ticks per frame.

---

## 3. Edge cases

* A wave with fewer than sixteen frames: `frameLength` clamps to `frames.size()`, and the
  spread rule uses `frames.size()` in place of 16.
* `frameLength == 1`: the run is one frame and never advances; `PingPong` must not divide by
  zero (`L - 1`).
* `frameLoopStep` past the run's end clamps to the last step.
* `volTicks` on a row the lane never reaches costs nothing.
* A hop to the row it is on (`volHop == its own row`, or an `H` naming its row) must not spin:
  the lane still spends the row's ticks before it hops again, which the wait already gives it.
* An empty table row (no vol, no transpose, no command) still costs a lane step, as now.
* A table run restarted by a note-on resets all three pointers and both hop counters.
* `Cmd::A` inside a table (switch table) resets all three lanes, as it resets one today.

## 4. Tests

`Tests/DriverTests.cpp`:
1. "a table's volume lane keeps its own time": TSP on every row, `volTicks` 4 -- the transpose
   moves four times per level.
2. "an H in the second command column loops that column alone": CMD1 walks to the end while
   CMD2 repeats its first two rows.
3. "the volume lane hops on its own": `volHop` on row 2 replays rows 1-2 while the transpose
   column runs on.
4. "a wave instrument's frame run takes LENGTH and loops from its own step": `frameLength` 4
   over a 16 frame wave gives frames 0, 5, 10, 15; `frameLoopStep` 2 loops the last two.
5. "PingPong turns at the loop step, not at frame 0".

`Tests/LsdjImportTests.cpp`:
6. "a table's ENV column carries its duration and its hop" (format 11 and 22).
7. "a wave instrument's PLAY, LENGTH, LOOP POS and SPEED are read" -- all four PLAY values.
8. The §62 case is rewritten: the `H` in CMD2 is **kept** now.

`tools/linktest/main.cpp`: a round trip of the new fields, and a song written before them
reading back with `volTicks = 0`, `volHop = -1`, `frameLength = 0`, `frameLoopStep = 0`.
