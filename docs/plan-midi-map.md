# Plan -- the MIDI map: sixteen channels, velocity regions as tracker rows

*(The user's brief, 2026-09-28: MIDI comes in on up to sixteen channels a port; each is assigned
to one ChipBoy channel (1 and 5 can both play PU1); within each, velocity regions, each a tracker
row without the note -- an instrument, a table, two commands, the letters that mean nothing from
MIDI ignored. A: channel 1 to PU1 with one region holding instrument 01: any note at any velocity
plays PU1's 01 at that pitch. B: channel 2 to WAV, the top half instrument 02, the bottom half no
instrument and a vibrato: C at 75 % for a whole note, D at 25 % halfway -- the pitch moves to D,
still instrument 02, with the vibrato. C: channel 3 to PU2, the top half instrument 03, the
bottom half no instrument and an `E`: four quarters, the first at 75 %, the rest at 25 % -- the
first plays 03 as is, the rest are still 03 with the envelope, as a bare `E` row in the tracker
remembers the instrument. Note ends are `K` commands unless another note follows at once.
`COMMANDS_AND_TEMPO.md` §225 carries the law, `UI_DESIGN.md` D-UI-43 the tab.)*

## The shape

**A mapped note is a tracker cell that arrived by MIDI.** The map turns a note-on into the
event a `Player` would have fired for a cell -- the pitch, the region's instrument, table and
two commands -- and the driver plays it by the cell path it already has (§3, §8, §12): a region
with an instrument is a plain note, one without is a bare note that keeps what sounds and fires
its commands on it. Nothing new is invented for the sound; the map is a routing table and the
regions are rows.

## Data (`Source/core/Tracker/MidiMap.h`, part of the `Song`)

```c++
namespace chipboy::tracker {
constexpr int kMidiChannels = 16, kMaxRegions = 8;
struct MidiRegion {                    // a row without a note
    uint8_t from = 1;                  // the lowest velocity it takes; the next region's `from` ends it
    uint8_t inst = 0, table = 0;       // 0 keep, as a cell's columns
    bank::Command cmd1, cmd2;
};
struct MidiChannelMap {
    int8_t target = -1;                // the ChipBoy channel 0-3, -1 off
    std::vector<MidiRegion> regions{ MidiRegion{} };   // one blank region: pitch and gate alone
};
struct MidiMap {
    bool on = false;                   // off: the channels' Source parameters route, as before
    std::array<MidiChannelMap, kMidiChannels> channels;
};
void normalizeMidiMap(MidiMap&);       // regions sorted by `from`, the first at 1, no two alike, 1..kMaxRegions
const MidiRegion& regionFor(const MidiChannelMap&, uint8_t velocity);   // the last region whose `from` <= velocity
bool midiMapIsDefault(const MidiMap&);  // off and nothing assigned: the file leaves it out
bool midiCommandAllowed(bank::Cmd);     // false for H, G and T: a hop, a groove and a tempo are the timeline's
}
```

`Song::midiMap`. The song is copied whole for the tabs and the export, so a vector per channel
is fine; the audio thread reads the published song and never allocates.

## Routing (`ChipBoyProcessor::routeMidi`)

With `midiMap.on`: a channel message on MIDI channel `m` goes to `channels[m-1].target`, or
nowhere. A note-on takes `regionFor(velocity)`'s columns into the `NoteEvent` (`inst`, `table`,
`cmd1`, `cmd2`, the disallowed letters dropped) and is marked `mapped`; a note-off is marked
`mapped` too; bends, controllers and all-notes-off go to the target as they are. With the map
off, the old per-channel Source routing runs unchanged. The tracker gate is untouched: a Trkr
channel still ignores MIDI unless it is armed and recording.

## The driver

- `NoteEvent::mapped`. In `dispatch`, a mapped note-on reaches `noteOn(..., &e)` -- the cell path
  -- unless the channel is Hybrid (§20: the song's cells choose the columns there; the map only
  gives pitch and gate). So `plain = inst != 0`, the note takes the instrument's own volume (a
  cell's rule 1: the velocity chose the region and does nothing else), the region's commands
  fire once at the note (§12), a bare region moves the pitch and fires its commands on what sounds.
- **Note ends are `K`s.** A mapped note-off that ends the sounding note (not one already
  replaced by a later note, not one with older keys still held -- those keep §8's rules) sets
  `killAt = tickCount_`: the voice dies at the next tick boundary, as a `K 00` read between ticks
  would. A mapped note-on on that channel before the tick clears `killAt`, so a note that follows
  at once is a plain or bare row over a live voice -- example C's second quarter keeps 03 and
  gets its `E`. The instrument's Note-off mode is not read for a mapped note.
- The command octave (§13) keeps its law: a mapped note 0-11 fires the channel's slots and then
  the region's commands on what sounds, without a trigger.
- Recording (§9.4): a mapped note records the region's table and commands (what the channel
  really read), not the channel parameters', and a blank VEL.

## The tab (D-UI-43)

A **MIDI** tab after Tracker. Left, the sixteen MIDI channels as a list with each one's target
(`PU1`, `WAV`, `--`). Right: the map's switch, the selected channel's target as a segmented
control (Off / PU1 / PU2 / WAV / NOI), a velocity bar showing the regions across 1-127 (drag a
boundary to move it), and the **region grid** -- a row per region: VEL (the region's `from`;
the first is always 1), INS, TBL, CMD 1, CMD 2 -- typed, nudged and right-clicked as the lane's
cells are; `+` and `-` add a region above the top one and remove the last. The palette leaves
out H, G and T. The strip's source badge reads `MAP 1·5` while the map is on.

## Files

`Source/core/Tracker/MidiMap.h/.cpp`; `Song.h` (`midiMap`); `BankJson.cpp` (`"midiMap"`, written
only when not default); `Driver.h/.cpp` (`mapped`, the kill, the cancel); `LinkLayout.h` (`kVersion`
5: `NoteEvent` grew); `ChipBoyProcessor.cpp` (routing, recording); `Grids.cpp`/`Widgets.h`
(`RegionGrid`); `panels/MidiMapPanel.h/.cpp`; `ChipBoyEditor` (the tab); `PanelCommon.cpp` (the
badge); `tools/uishot` (the shot).

## Edge cases

- A MIDI channel whose target changes while a note sounds: the panel asks the processor to
  flush every channel after a target edit, so nothing rings on the old one.
- Two MIDI channels on one ChipBoy channel merge as two keyboards would: the held stack is one.
- Velocity 0 note-ons are note-offs, as before.
- A region's `D` delays the note as a cell's does; its `K n` kills after n ticks; a `B` gates it.
- Keyswitches (the channel parameter) still select; a region's INS overrides what they chose.
- The Voice plugin is untouched: its notes arrive already routed.

## Open: `G` as a record grid (the user's pushback)

A `G` in a region is dropped today. The user's point: while a groove is inaudible under MIDI
playback, recording could use it to decide which step a note lands on. The recorder already
quantises on the phrase's own groove (`Player::quantise`), so the gap is only *setting* that
groove from the MIDI side. Proposed: a per-MIDI-channel **record groove** in the map, used as
the quantise grid for that channel's notes and stamped on the phrase written into; not a
command, not a cell. Not built; see the reply of 2026-09-28 and HANDOFF.

## Tests

`Tests/MidiMapTests.cpp` `[midimap]`: normalise (order, the first at 1, duplicates, the cap);
`regionFor` at the boundaries; the disallowed letters; a mapped `NoteEvent` from a region.
`Tests/DriverTests.cpp` `[driver][midimap]`: example A (any velocity, plain); B (a bare region's
note moves the pitch, keeps the instrument, applies `V`); C (`E` on a bare region after a plain
one); a mapped note-off kills at the next tick; a note-on before that tick cancels it and the
new note continues; the off of a replaced note does nothing. The song file's round trip in
`chipboy_recordtest`'s record test (the recorded song carries a map).
