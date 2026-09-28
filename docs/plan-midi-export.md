# Plan -- MIDI export: the notes a song sounds, on the song's own time

*(The user's brief, 2026-09-28: each channel's notes and nothing else, the noise channel
optional, the time signatures from the chain, the project tempo with its `T` changes as tempo
events, `K` ending a note, `C` in a cell or a table writing the notes that actually sound, `H`
rendered to what it does on the timeline. `COMMANDS_AND_TEMPO.md` §224 carries the law,
`UI_DESIGN.md` D-UI-41 the menu.)*

## The shape

**Render the song through the real engine and write down what sounds.** The Clock, the Player
and the Driver already turn the song into what plays -- the play order with its `H` hops, the
grooves, the tables, the chords, the kills -- so the exporter runs them offline (the recipe of
`chipboy_recordtest --trace-song`) and asks the driver, after every tick, which note each
channel is sounding. A change of that note is a MIDI note-off and a note-on; silence is a
note-off. Nothing about a command is re-implemented, so whatever the driver does, the file says.

## The law of the sounding note (§224)

`Driver::soundingNote(ch)`: 0 when the voice is inactive, killed or releasing; else the base
note, the chain's and the song's transpose, the instrument's own (the PU2 transpose), the
channel parameter's, the chord's current step and the table's transpose column -- an integer.
Not the 1/256-semitone offsets of `P`, `L` and `V`: a vibrato or a slide is not a new note.
On the noise channel the note is the base note plus the chord step, as a number.

## Time

- **PPQ** is the first time signature's beat unit in ticks, as quarters: `ticksPerBeat * unit / 4`
  -- 24 for `4/4` at 24 and for `11/8` at 12, the two cases that matter; a song whose first
  signature makes another number gets that PPQ and the tempo scaled to keep real time, and a
  note in the report. Ticks map to MIDI ticks by `ppq / 24`.
- **Tempo**: the song's own tempo at tick 0 as microseconds per quarter from
  `tickSeconds(bpm, lsdjTempo) * ppq`, so the ROM's word rounding and the 2x/3x/6x tempi carry;
  then one tempo event per point of the song's tempo map (the `T` cells, §4).
- **Time signatures**: one meta event per signature at its tick -- beats, the unit as a power of
  two (a unit that is not one is rounded to the nearest and noted), 96 / unit MIDI clocks per
  click, eight 32nds per quarter.
- **Length**: one pass, tick 0 to `songTicks` (the longest chain, or the `H F F`); a note still
  sounding there ends there. A shorter chain comes round within it (§212) and is written as it
  plays; nothing after the song's end is.

## The file

Format 1, `ppq` ticks per quarter. Track 0: the name, the tempo events, the signatures. Then
one track per channel -- PU1, PU2, WAV and, unless the option drops it, NOI -- named, on MIDI
channels 1-4, note-on velocity 100, note-off as 0x80. Events at one tick are offs before ons.
Delta times as variable-length quantities; full status bytes, no running status.

## Code

`Source/core/Export/MidiExport.h`:

```c++
namespace chipboy::midi {
struct Options { bool noise = true; uint8_t velocity = 100; };
struct Report  { int notes[4] = { 0, 0, 0, 0 }; int64_t ticks = 0; int ppq = 24; std::vector<std::string> notes; };
/// The song rendered through the engine on the bank it plays through, as a
/// standard MIDI file's bytes. Empty when the song has no rows.
std::vector<uint8_t> exportSong(const tracker::Song& song, const bank::Bank& bank, const Options& o, Report* report);
}
```

`Driver::soundingNote(int ch) const` beside `noteOfVoice`. `chipboy_recordtest --export-midi
FILE OUT.mid [--no-noise]` for the command line. The plugin's FILE card: Export ▸ MIDI ▸ *All
four channels…* / *Without the noise channel…*, a chooser in the songs folder, the report's
counts on the status line.

## Edge cases

- The render's sub-blocks end at each tick so the view is read after that tick's work; the
  events of a block are re-based into their sub-block.
- A note whose pitch changes without a retrigger (a chord step, a table transpose row) is a
  note-off and a note-on at that tick; a retrigger of the same note (a plain note, `R`) is not
  a change and writes nothing -- the note simply continues.
- `H F F` stops the song: the file ends there.
- A song with no rows exports nothing and says so.
- The export never touches the live processor: it renders a copy of the song and reads the
  bank the tab plays through. On the copy every channel's note source is the tracker.
- What the test found (§224): a cell's note stayed on the driver's held stack, so an `OFF` after
  two notes returned to the first. Fixed in `Driver::noteOn`; `Tests/DriverTests.cpp` keeps it.

## Tests

`Tests/MidiExportTests.cpp` `[midi]`, a song built by hand and the file parsed back by a small
reader in the test: PPQ 24; the tempo at 0 and a `T` cell's event at its tick; two signatures'
events; PU1's note at tick 0 ending at its `OFF`; a `K 02` ending a note two ticks after it; a
`C 47` cell writing 60, 64, 67 as one-tick notes at the chord rate; an `H 1 0` row playing its
first steps twice; the noise track present and absent by the option.
