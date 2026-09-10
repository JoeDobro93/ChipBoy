# Plan: importing LSDj songs from a .sav

The converter that recreated an LSDj 9.3.9 song (`COMMANDS_AND_TEMPO.md` §45–§53) moves
into the plugin: *Import .sav…* in the Tracker head reads a save, lists the songs in it,
and opens the chosen ones in tabs of their own, each with the bank it needs. Everything
that reads bytes lives in `Source/core/Import/` (no JUCE, rule L1); the file chooser, the
ROM sniff and the dialog live in the plugin layer. No LSDj content enters the repository
(L3): the tests build their own saves in memory.

## 1. The .sav

128 KB (`0x20000`). The working song is the first 32 KB (`0x0000`–`0x7FFF`); byte `0x7FFF`
is the **song format version** (22 for 9.3.x, 3 for the older songs in the user's save).
The file table is the 512-byte block at `0x8000`: 32 names of 8 bytes at `0x8000`, 32
file-version bytes at `0x8100` (the save counter, not the format), the **active file** at
`0x8140` (`0xFF` none), and the block allocation table at `0x8141`: 191 bytes, entry *i*
naming the file that owns **block *i* + 1**, `0xFF` free. Blocks are 512 bytes at
`0x8000 + block × 0x200`; the file table is block 0.

A file is the concatenation of its blocks' streams, decompressed:

| bytes | meaning |
|---|---|
| `C0 C0` | a literal `C0` |
| `C0 v n` | `v` repeated `n` times |
| `E0 E0` | a literal `E0` |
| `E0 F0 n` | the default wave (`8E CD CC BB AA A9 99 88 87 76 66 55 54 43 32 31`) `n` times |
| `E0 F1 n` | the default instrument (`A8 00 00 FF 00 00 03 00 00 D0 00 00 00 F3 00 00`) `n` times |
| `E0 FF` | end of file |
| `E0 b` | continue at block `b` (absolute, from `0x8000`) |
| other | a literal byte |

Measured on the user's save: SUNRISE's file decompresses to 32768 bytes equal to the
working song but for the edit-state bytes at `0x3FC1`–`0x3FCA`; six older files use
`F0`/`F1` and read as format 3.

```cpp
namespace chipboy::lsdj {
struct SaveEntry { int file; std::string name; int formatVersion; bool active; int blocks; };
struct SaveIndex { std::vector<SaveEntry> files; int activeFile; int workingFormat; bool workingUsed; };
bool indexSave(const uint8_t* data, size_t size, SaveIndex& out, std::string& error);
bool decompressFile(const uint8_t* data, size_t size, int file, std::vector<uint8_t>& song, std::string& error);
bool workingSong(const uint8_t* data, size_t size, std::vector<uint8_t>& song);
std::string romVersion(const uint8_t* rom, size_t size);   // "9.3.9" from the title "LSDj-v9.3.9"
}
```

Edge cases: a name with no blocks is not a file; a block jump outside the save, a stream
that runs past its block, more than 191 blocks visited, or a file that does not reach
32768 bytes fail with a message; a working song with no allocated instrument and no chain
row is still offered (the user may have only phrases).

## 2. The song, 32 KB

Offsets shared by every format read so far (liblsdj's layout, confirmed on formats 3 and
22): phrase notes `0x0000` (255 × 16), grooves `0x1090`, song rows `0x1290` (4 chains a
row, `FF` empty, the song ends at an all-`FF` row), table envelopes `0x1690`, instrument
names `0x1E7A` (5 bytes), table allocation `0x2020` (32), instrument allocation `0x2040`
(64), chain phrases `0x2080` (128 × 16), chain transposes `0x2880`, instrument parameters
`0x3080` (64 × 16), table transposes `0x3480`, table commands `0x3680`/`0x3880` and
`0x3A80`/`0x3C80`, phrase allocation bitmap `0x3E82`, chain allocation bitmap `0x3EA2`,
tempo `0x3FB4`, phrase commands `0x4000` and their values `0x4FF0`, waves `0x6000`
(256 × 16), phrase instruments `0x7000`, format version `0x7FFF`.

## 3. The model: what a version of LSDj means by its bytes

```cpp
struct LsdjModel {
    const char*    name;             // "LSDj 9.3.9"
    int            formatVersion;    // the format this version writes
    int            formatMin, formatMax;   // the formats it is used to read
    const char*    commandLetters;   // command byte -> letter, index 0 none
    bool           stagedEnvelope;   // 9.x three-stage software envelope, else the NRx2 byte
    const uint8_t* envPeriods;       // 16: pitch-clock periods per level (§51)
    const uint8_t* noiseMap;         // 128: MIDI note -> NR43 byte, 0xFF unmeasured
    int            waveOctave;       // semitones added to a wave note (§45: -12)
    bool           pu2Transpose;     // instrument byte 2 is PU2 TSP (§49)
    bool           measured;         // traced on that ROM, or assumed from another model
};
```

Two models ship: **LSDj 9.3.9** (format 22, everything measured on the user's ROM) and
**legacy** (formats 0–19: the command table without `B`, the hardware envelope, the noise
map traced from the harness's version-0 saves, assumed for the rest). A song picks the
model whose range holds its format; a format no model knows takes the ROM found beside the
save, if its title names a version a model has, else the newest model. The dialog's
dropdown overrides all of that.

**Adding a version** (the user will supply ROMs): copy `lsdj_9_3_9` in `LsdjModel.cpp`,
give it the format range the ROM writes and reads, and re-measure with the harness what
may differ — the command byte table (`x_sunrise_probe`-style: one phrase per letter), the
noise map (`x_noise_map`: every note, read NR43), the envelope table (§51's probe: one
note, the speed patched through 1–F), the wave octave, PU2 TSP, and the tick-based letters
P and V (§7's tables). Keep the shared parser; only the table pointers change. Set
`measured` when the ROM traced it. `HANDOFF.md` carries the same instructions.

## 4. Interpretation (from the converter, `COMMANDS_AND_TEMPO.md` §45–§52)

- **Instruments** (allocated slots only, same slot numbers): pulse — duty `b7 >> 6`, sweep
  from `~b4`, PU2 transpose `b2`, envelope; wave — level code `(b1 >> 5) & 3` → NR32 order
  `{0:0, 1:3, 2:2, 3:1}`, wave slot per LSDj synth `b3 >> 4` (a wave of 16 frames); noise —
  15-bit unless every note it plays maps to a 7-bit NR43 in the model's map; kit — a note
  and skipped (samples live in the ROM). Table `b6 & 0x1F` when `b6 & 0x20`; Transpose flag
  `!(b5 & 0x20)`; table mode `b5 & 8`; pitch speed from `b5` bits 7/6/4; cmd rate `b8`.
- **Envelope**: staged model — `a1 s1 / a2 s2 / a3 s3` from bytes 1, 9, 10 into Shaped
  (Start `a1`, Attack to Peak `a2`, Decay to Sustain `a3`, Fade to 0) with
  `ticks = max(1, round(|Δ| × periods[s] × 2.7924 ms / tick ms))` at the song tempo, Chip
  hold when `s1 = 0`; legacy model — byte 1 is NRx2: volume, direction, rate, Chip.
- **Notes**: `byte + 35` is MIDI; wave notes take the model's octave; noise notes go
  through the model's map to an NR43 and back to the ChipBoy note with the same LFSR clock
  (nearest LSDj note on a tie).
- **Commands**: by the model's letter table. `A` → the cell's TBL column (`A20` stop is a
  note); `P` the byte; `H x,y` → `times, row`; `E` on a wave → the level; `W` low digit on
  pulses; `O` low two bits; `T` the byte; `F` frame on WAV, PU2 transpose on PU2, dropped
  on PU1; `B` dropped with a note; `C V Z M R S D K L G` as they are.
- **Tables**: volume column high nibble, transposes as bytes (noise rows converted through
  the map for the table's lowest noise note), both command columns, end Loop.
- **Chains**: one ChipBoy phrase per (LSDj phrase, channel); the chain carries the
  transposes (§48). A song row with an empty channel is a note.
- **Grooves**: sixteen slots, trailing zeros trimmed, `6 6` when empty. Tempo from `0x3FB4`.
- **Song**: every channel Trkr, arms off, groove names blank, `buildRowTables()`.

`importSong(const uint8_t* song, size_t size, const LsdjModel&, bank::Bank&, tracker::Song&, ImportSummary&, ImportNotes&)`
fills a blank bank and song; the notes are the converter's FINDINGS.

## 5. The plugin

- **Import .sav…** in the Tracker head's FILE group opens a chooser for `*.sav`.
- **The dialog** (`LsdjImportDialog`, UI_DESIGN D-UI-22): the file's name; a row per song
  with a checkbox, the name, its format and the model it will take; the working song
  first, named after the active file with *(working copy)*, or *Working song*; a
  **Version** dropdown — *Auto (by each song's format)* and every model, with what the
  folder's ROM said beside it; **Import** and **Cancel**. Import opens one tab per checked
  song (`ChipBoyProcessor::addTab`, bank name "LSDj · name"), activates the last, and puts
  the notes in a message box when there are any.
- The ROM sniff reads the first 0x150 bytes of each `*.gb` beside the save.

## 6. Tests (`Tests/LsdjImportTests.cpp`, tag `[lsdj]`)

A save built in memory: a working song image and two files compressed by a small writer
(literals, RLE, both escapes, both defaults, a block jump, EOF). Assertions: the index
(names, formats, active file, block counts); each file decompresses to its image;
`E0 F0`/`E0 F1` expand to the defaults; a jump outside the save fails cleanly; the model
lookup by format, by ROM title, by name and the newest default; the import of a synthetic
format-22 song — instrument fields, the staged envelope's ticks at 165 BPM (A5 → 11), a
wave's frames, a table's rows, a phrase's notes, `A` into TBL, a chain transpose, a noise
note landing on the same LFSR clock — and of the same bytes under the legacy model (the
hardware envelope, the shifted letters).
