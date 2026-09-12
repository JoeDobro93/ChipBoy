# Plan -- a kit note plays two samples, the way LSDj's does

*(§117 settled how LSDj **sums** the pair. This is about ChipBoy keeping the pair instead of
baking it, so the second sample is something the user can change.)*

## Why

An LSDj kit instrument names two kits. A note's **high digit** picks a sample from the first, its
**low digit** one from the second, and the phrase screen grows a second column to show it. The
importer flattened that: it summed the pair once and stored the result as a single ChipBoy sample,
one per distinct note byte. The song plays, but the second sample is gone -- it cannot be changed,
removed, or put on another note, and a kit that a song uses eight pairs of fills eight of the
slot's thirty-two entries with near-duplicates.

## The shape

A kit slot keeps the **source** samples. A cell names two of them:

- the **note** column picks the first, as now, by nearest note;
- the **VEL** column picks the second by **index + 1** into the same kit -- `00` blank means one
  sample only, `01` is the kit's first sample, up to `20` (32).
- the kit's **`Dist`** says how the two are summed: `Clip`, `Soft`, `Fold`, `Fold2`, `Wrap`
  (§117's curves, under ChipBoy's own names).

VEL carries no other meaning on a kit instrument: `velToVolume` already only reaches Pulse and
Noise, so the only thing to keep clear of is the global `Velocity mode = keyswitch`, which adds
`vel / 8` to the instrument slot. That stays off for a kit (see *Edge cases*).

## Data

`Source/core/Bank/KitDist.h` (new, core, `<std>` only) -- moved out of `core/Import` so the driver
can reach it without depending on the importer:

```c++
enum class KitDist : uint8_t { Clip, Soft, Fold, Fold2, Wrap };
int      kitDistEntry(KitDist, int row, int col);            // one table entry
uint8_t  kitMix(KitDist, int index, int a, int b);           // one mixed nibble
```

`core/Import/LsdjKitDist.h` keeps only what is LSDj's: `kKitDistFirstPage`, `kKitDistPages`, and
the version's list of four.

`bank::Kit` gains one field:

```c++
KitDist dist = KitDist::Clip;      ///< how a note's two samples are summed
```

`bank::KitSample` is unchanged. Bank JSON gains `"dist"` on a kit, defaulting to `clip` so every
bank written before this reads back as it did.

## The driver

`Voice` gains a second cursor beside the first (`kitIdx`, `kitPos`, `kitLen`, `kitLoopPoint`):

```c++
bool     kitPair = false;          // a second sample is playing
uint8_t  kitIdxB = 0;
uint32_t kitPosB = 0, kitLenB = 0, kitLoopPointB = 0;
bank::KitDist kitDist = bank::KitDist::Clip;
```

`noteOn` resolves it: for `InstrumentType::Kit`, a cell VEL of `1..samples.size()` sets `kitIdxB`
and `kitPair`; anything else leaves `kitPair` false. `kitNextChunk` reads a nibble from each
cursor -- the second one's silence is `8`, not the end of the note -- and writes
`kitMix(v.kitDist, i, a, b)`; `ended` still follows the **first** cursor alone, so a note's length
is the first sample's, as LSDj's is.

Nothing else in the streaming path changes: the chunk is still 16 bytes, still double-buffered on
CGB, still fetched the same way.

## The importer

`kitInstrument` sets `kit.dist` from byte 10 through the model's list (§117), and `kitNote` stops
mixing. Instead:

- the first time a sample of kit A is named, it is appended to the ChipBoy kit and remembered;
  the same for kit B, in its own map;
- a note byte `hi lo` gives the cell a **note** (kit A's sample, or kit B's when `hi` is 0) and a
  **VEL** (kit B's sample index + 1, or 0 when `lo` is 0 or `hi` is 0).

So the ChipBoy kit holds at most 15 + 15 samples, inside the slot's 32, and a song that used eight
pairs now shows the six samples they were made of.

`kitNote` currently returns only a note; it grows to return the pair, and the three call sites (a
phrase step, and the two table columns that can carry a kit note) pass the VEL through.

## The UI

- **Tracker**: the VEL column's help line and context line say "second sample" when the row's
  instrument is a kit, and the value prints as the kit's sample name. The edit domain is
  `0 .. kit.samples.size()`, not 0-127, on those rows.
- **Kits tab**: a `Dist` segmented control beside `Loop`, and a **Preview** button beside the
  waveform that plays the selected sample.
- `docs/UI_DESIGN.md` gains D-UI-27 (the VEL column on a kit row), D-UI-28 (`Dist`) and
  D-UI-29 (Preview).

## Preview

The processor gains a small auditioner, off the driver: `previewKit(slot, sample)` pushes a
request through a lock-free slot the audio thread reads once a block. It streams the sample's
nibbles at the kit's own rate through the same DAC curve and analog chain as channel 3, mixed in
after the driver, and stops at the end. It never touches the driver's state, so previewing while
the song plays does not disturb it.

## Edge cases

- **VEL out of range** (bigger than the kit's sample count): no second sample, as if blank. A
  bank whose kit shrank keeps playing.
- **Keyswitch velocity mode** (`Velocity mode = keyswitch`, `vel / 8` added to the slot): skipped
  when the channel's base instrument is a kit, so the VEL column means one thing at a time.
- **MIDI**: a note-on's velocity picks the second sample the same way, so a controller can play
  pairs. `kDefaultVelocity` (100) is past the end of every kit, so an ordinary MIDI note plays one
  sample -- which is what it did before.
- **An old `.cbsong`**: its kits hold baked samples and its cells have `vel = 0`, so it plays
  exactly as it did.
- **A kit note in a table**: the transpose column names the note; the table has no VEL, so a table
  plays one sample. LSDj's tables cannot name a kit's second digit either.
- **`Fold2`'s odd entry** (§117) needs the nibble's index, which `kitMix` takes.

## Tests

- `Tests/BankTests.cpp`: `KitDist` round-trips through the bank JSON, and an old bank without the
  field reads as `Clip`.
- `Tests/DriverTests.cpp`: a kit with two samples, a cell with VEL 2, and the streamed wave RAM is
  the mix of the two under each of the five curves; VEL past the end plays one sample; the note
  ends with the **first** sample even when the second is longer.
- `Tests/LsdjImportTests.cpp`: the kit import test gains the pair -- note `0x12` becomes note =
  kit A's sample 1 and VEL = kit B's sample 1 + 1, the kit holds the sources, and the mixed output
  the driver produces equals what the old baked sample held.
- `Tests/RenderTests.cpp`: `SAMESONG`'s phrase 42 renders the same as before the change.
