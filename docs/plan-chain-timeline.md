# Plan -- the chain drawn in time, a play head that goes anywhere

*(Settled with the user over four rounds of an interactive mockup built from READROOM's own
row tables, 2026-09-17. `COMMANDS_AND_TEMPO.md` §222 and §223 carry the laws; `UI_DESIGN.md`
D-UI-35 to D-UI-37 the decisions. This is the shape of the code.)*

## Why

The chain column drew row *N* of every channel on one line, but a row's start is the sum of
that channel's own row durations (§25), so once phrases differ in length, grooves or `H` hops,
row *N* of PU1 and row *N* of WAV are different moments. READROOM builds every 132-tick section
from one PU1 row, two WAV rows and one 11-step NOI row on a 12-tick groove: row 10 of each
channel is somewhere else in the song. The lane made it worse: it showed every channel's phrase
at the *selected* row and lit the other three channels' steps from the selected channel's row
and offset (`TrackerPanel::tick`), so a channel a row behind showed the wrong phrase with the
wrong step lit. And the plugin's own transport could only start at tick 0.

The engine already has the answer: `rowStartTicks` per channel, `rowAtTick`, a play order per
row, a clock that maps ticks to seconds both ways and a Player that survives a jump (§47). The
work is to draw and drive the tracker from a **tick**.

## The shape

- **The play head is a tick**, `TrackerPanel::cursor_`. Each channel's lane shows the phrase
  *that channel* is in at that tick (`rowAtTick`), with its own step lit from its own offset.
  With Follow on the transport moves it; off, it stays where the user put it.
- **The chain is a timeline**: vertical axis in ticks, one column per channel, a block per row
  as tall as the row lasts (`rowStartTick(r+1) - rowStartTick(r)`), labelled with the phrase
  and its transpose. The play head is one line across the four channels. Every gesture the
  cells had (type, Backspace, +/-, Shift+arrows, box, list) survives on the block under the
  cursor; the LEN column goes (D-UI-8 revised).
- **The own transport locates**: Play runs from the play head, Pause stops where it stands,
  Stop stops and returns to tick 0, the loop is a tick region (Shift-drag on the gutter) or
  the whole song.
- **Time signatures** number the gutter and the readout, never the driver: `{tick, beats per
  bar, beat unit, ticks per beat unit}`, the first at tick 0, saved with the song.
- **The lane's head** reads the row's PHRASE, its TSP (typed), STEPS (typed, was LEN) and
  TICKS (calculated: the groove and the `H` hops, the block's height) on a chip row of its
  own; cells an `H` never reaches are dimmed but stay editable.
- **The transport** moves out of the head's left into the chain's own column of the head:
  icon buttons (play/pause, stop, loop, follow), the LED, a `bar·beat·tick` readout, and the
  zoom slider under them. RECORD, SONG and FILE keep the left.

## Data

`Source/core/Tracker/Song.h`:

```c++
/// Section 222: a time signature is a numbering the timeline reads, never
/// the driver. Ticks per beat unit is what makes 11/8 at 12 ticks a 132-tick
/// bar and 4/4 at 24 a 96-tick one.
struct TimeSignature {
    int64_t tick = 0;            ///< where it takes effect
    uint8_t beats = 4;           ///< beats per bar, 1-64
    uint8_t unit = 4;            ///< the beat unit's name: 4 a quarter, 8 an eighth, 1-64
    uint8_t ticksPerBeat = 24;   ///< ticks per beat unit, 1-192
    int barTicks() const;        ///< beats * ticksPerBeat
};
struct Song {
    ...
    /// Section 222: sorted by tick, the first always at tick 0. Not read by playback.
    std::vector<TimeSignature> signatures{ TimeSignature{} };
};
struct BarPosition { int bar = 1; int beat = 1; int tick = 0; int signature = 0; };   // bar and beat 1-based

void        normalizeSignatures(Song& s);                 // sort, one per tick, tick 0 present, fields clamped
int         signatureAt(const Song& s, int64_t tick);     // index of the one in force
BarPosition barPositionAt(const Song& s, int64_t tick);   // bars counted through every signature, each spanning ceil(span / barTicks)
```

`Source/plugin/shared/BankJson.cpp`: a `"signatures"` array of `{tick, beats, unit, ticks}`,
written only when the list is not the single default, read as the default when absent. No
format bump: an older reader ignores the key, the demo song's bytes do not move.

`Source/core/Driver/Clock.h`:

```c++
void    ownLocate(int64_t tick);   ///< the position, playing or stopped; the next block starts there
int64_t ownTick() const;           ///< where the own transport stands
```

`ownPlay()` starts from where it stands, not from the loop start or tick 0 (§16 revised; a
position outside an active loop region starts at the region's start).

`Source/plugin/main/ChipBoyProcessor.h`:

```c++
void transportPlay();              // from the play head
void transportPause();             // stop where it stands
void transportStop();              // stop, and locate tick 0
void transportLocate(int64_t t);   // message thread asks, audio thread does (a pending tick + a flag)
void setLoopTicks(int64_t a, int64_t b);   // b < 0: the song's end; replaces setLoopRows
```

The tracker tick published while the own transport is stopped becomes `clock_.ownTick()` so
a locate shows at once.

`Source/plugin/ui/Widgets.h`:

```c++
class ChainColumn {
    void setSong(std::shared_ptr<const tracker::Song>, int64_t cursorTick);
    void setTransport(bool playing, int64_t tick);      // the moving line while it plays
    void setZoom(double t);  double zoom() const;        // 0..1, the slider; px per tick is derived
    std::function<void(int64_t)> onSelectTick;           // click, drag, keys
    std::function<void(int64_t a, int64_t b)> onLoopRegion;   // Shift-drag; b < 0 clears
    std::function<void(std::vector<tracker::TimeSignature>)> onSignaturesChange;
    std::function<void()> onPlayPause;                   // Space
    // kept: onEntryEnd, onChainChange, onChainTransposeChange, onChainEndChange
    static constexpr int kHeaderHeight = 48, kWidth = 264;
};
class PhraseGrid {
    void setSong(std::shared_ptr<const tracker::Song>, const int rows[4]);   // each channel's own row
    std::function<void(int ch, int semis)> onTransposeChange;               // the TSP chip
    static constexpr int kHeaderHeight = 68;   // name row 26, chip row 20, captions 22
};
class IconButton : juce::Button { enum class Icon { Play, Pause, Stop, Loop, Follow }; };
```

## The timeline's geometry

- 264 px wide as today: 4 px pad, a **54 px gutter** (bar numbers left-aligned, beat labels
  right-aligned), four **50 px** channel columns 2 px apart. A block is `[start, start+ticks)`
  in px at `pxPerTick`, 1 px gap below; the phrase slot at the top left (`ValueFormat::slot`),
  the transpose at the top right (`ValueFormat::transpose`, blank at 0), the tick count at the
  bottom right when the block is 30 px or taller.
- **Zoom**: the slider is 0..1, log-mapped to `pxPerTick` between `22 / (4 bars of the first
  signature)` and 22. The **grid division** is the finest of the candidates `{divisors of
  ticksPerBeat, 2x 3x 4x 8x ticksPerBeat below a bar, 1 2 4 bars}` of the *first* signature
  whose spacing is at least 22 px. The play head snaps to it on a click or drag in the gutter;
  PgUp / PgDn move by it.
- **Labels**: a grid line at a bar of the signature in force shows the bar number (left); any
  other grid line shows its beat, `+ticks` when off the beat (right, dim). A bar that falls
  between grid lines gets its own line and number. Bar numbers are pruned to every 2^n-th when
  bars come closer than 12 px; beats of the signature in force are 6 px marks in the gutter.
- **Blocks**: a base fill, then the channel's colour at 30 % over stretches of steps played
  for the first time and 10 % over stretches an `H` replays (walk the play order, a set of
  seen steps), a 1 px border in the channel's colour at 55 %, a 2 px left edge in full. The
  block the cursor is in has the accent outline. A dashed `+` block stands after each
  channel's last row; a looping channel shorter than the song repeats its blocks dimmed.
- **Signatures**: a warn-colour tag in the gutter above the bar line, `11/8·12`. Double-click
  the tag to edit, double-click empty gutter to add one at the snapped tick: a CallOutBox with
  three steppers (beats 1-64, unit 1-64, ticks per beat 1-192), Delete (not at tick 0), OK.
- **Scrolling**: `scrollPx_`, the wheel and a drag on the blocks; Follow keeps the play head
  in view. Only visible blocks are painted (binary search on `rowStartTicks`).
- **Keys** (the chain focused): `↑ ↓` one tick, `← →` the previous or next step boundary of
  any of the four rows under the play head, `PgUp PgDn` a grid division, `Home` tick 0, `End`
  the song's end, `Tab` / `Shift+Tab` the cell cursor across the eight cells, Space
  play/pause, everything else the cell grammar of §35 on the block under the play head.

## Edge cases

- **A locate in a host** does nothing to the clock (the host rules); the play head still
  moves so the lanes can show another moment while Follow is off.
- **A looping channel** (§212): `rowAtTick` wraps, so the block under the play head on a
  second pass is the same row; edits go to it. The `+` block is that channel's end, reached
  by a click only (its tick would wrap), held as an explicit "editing past the end" state
  until the play head moves.
- **A play head past the song's end** is clamped to `songTicks`; an empty song is one empty
  row per channel, 96 ticks.
- **A signature at a tick past the song** is kept (it numbers nothing until the song grows).
  Two signatures typed at one tick: the later one wins. Deleting the tick 0 signature is
  refused; editing it is allowed.
- **Zoom under an odd signature**: the grid comes from the *first* signature, so under a
  change to 4/4 the grid may not divide the new bars; the bar layer still draws every bar in
  force with its number. Changing the tick 0 signature re-derives the grid.
- **Follow off while playing**: the red line is the transport, the cursor a thin dashed line
  at the accent colour; with Follow on they coincide.
- **`ui_view`**: `tick` (as a string, int64), `follow`, `zoom`; an old state's `row` is taken
  as PU1's row start.
- **Stop in a host** is disabled as before; Pause and Stop share one button's meaning with
  the host's own transport.
- **The head's budget**: the lane head grows 48 -> 68 for the chip row; the tool rows' gaps
  shrink 4 -> 2 and 6 -> 4 so the 112 px head is 108 and sixteen 22 px rows still fit at the
  minimum height (422 - 68 - 352 = 2 px over).

## Tests

- `[tracker]` signatures: normalize sorts, dedupes by tick and inserts the default at 0;
  `barPositionAt` under `11/8·12` at 0 and `4/4·24` at 4752: tick 131 is bar 1 beat 11 tick
  11, tick 132 bar 2 beat 1, tick 4752 bar 37 beat 1, tick 4752 + 25 bar 37 beat 2 tick 1.
- `[clock]` the own transport: `ownLocate` while stopped then `ownPlay` starts there; a
  locate while playing continues from there on the next block; `ownStop` keeps the position
  and `ownTick` reports it; a locate outside an active loop region starts the region.
- The plugin gate: the console checks as they are; `chipboy_uishot --song` shots the new
  Tracker tab and reports that the lane still fits.
