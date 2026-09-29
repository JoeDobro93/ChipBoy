# ChipBoy Solo -- one channel as a plugin of its own

*Plan, 2026-09-29. The user's brief: standalone individual channel plugins derived from the
same engine, aesthetically like ChipBoy's channel strips, for DAW and live use; the main
window is the waveform, an instrument, a table and two commands ("a row on the tracker");
a commands tab with a list stored per instance; wave tables and kits when the wave channel
is chosen; instruments stored in 64 automatable slots, each "a row of settings that get
put into the main window upon selection"; no tracker, small window; reads ChipBoy presets;
notes outside the channel's range select saved sounds so that a simultaneous note sounds
with the new sound. The user also noted that with the MIDI map (§225) routing a track's
MIDI into ChipBoy, this plugin can replace ChipBoy Voice.*

## 1. What it is

**ChipBoy Solo** (`ChipBoySolo`, VST3 / AU / Standalone, plugin code `Chbs`): one channel of
the DMG APU with its analog stage, driven by the same driver, bank, tables, waves and kits as
ChipBoy, and nothing else -- no song, no chain, no link, no headphone noise, no LCD line. The
name: *Solo* says one voice on its own and reads well beside the machine; *Channel* is
clunky, *Voice* is taken by the remote (and the remote is the one to retire, §9).

The engine is shared by construction: `Source/core/` is compiled in unchanged, the four
editor panels (Instrument, Tables, Waves, Kits) are the main plugin's own files compiled
into the Solo target, and the channel's parameters are the same set `ChipBoy` publishes for
a strip (`addChannelParameters`, `ChannelParamCache`). A change to the driver, the bank or
an editor lands in both plugins at once.

## 2. The model

**The row** is the host's parameters -- what the main window shows and what automation
draws: Instrument (0-128), Table (0-64), Level, Pan, Transpose, CMD 1 (letter, x, y), CMD 2,
plus Velocity mode and Live follow, prefixed `s_` (one lane set, `ChannelKind::Any` so the
channel can change). Live follow defaults **on**: the brief's law is that a change is applied
to the sounding note at once, and that is what Live follow is (§3, `Driver::tick`): an
Instrument change reloads and re-attacks, a Table change starts the new table on the sounding
note, Level and Pan write at the next tick, a command slot fires at the next tick when its
value changes and reverts when it goes to none. The same instrument staying while something
else moves never retriggers -- the driver already reads it that way -- and a new MIDI note-on
is the only other retrigger.

**Sounds** (`SoloSound`, 64 slots, in the plugin state): `used, name, inst, table, cmd1, cmd2`
-- a row without its note, level, pan or transpose, which stay the performance fields. A
sound is *recalled* -- its four fields written into the row's parameters -- by a click in the
Sounds tab, by the **Sound** parameter (0 none, 1-64; automatable, so a lane steps through
sounds), or by a **mapped key** (§4). A sound is *stored* from the row by a button. The Sound
parameter follows a hand recall; editing the row afterwards leaves it where it is (the row
is what plays; the parameter says where it came from).

**The bank** is a `bank::Bank` as ChipBoy's -- 128 instruments, 64 tables, 16 waves, 32 kits
-- so files and presets interchange without translation; Solo plays the instruments of the
chosen channel's type (a wrong type is greyed and, if played, cross-read as the driver does).
The instrument editors are the real ones; the Instrument tab's *Load preset…* reads a `.cbi`
(§15) into the bank, as it does in ChipBoy, and a whole bank file (`.chipboy`, `BankFiles`)
loads too.

**The command library** (32 entries, in the state): `letter, x, y, name`, a list the user
keeps per instance and drops into CMD 1 or CMD 2 with one click, and beside it the letter
reference (`commandInfo`) for the channel. The brief's "list of commands stored for that
specific instance".

**The key map** (`keyMaps[4][128]` as built, one per channel; sound index per note, 0 none): a MIDI note outside the
channel's playable range selects a sound instead of sounding. Playable notes cannot be mapped.
The default layout fills the keys **below the floor counting down** with sounds 1, 2, 3 …
(the first key under the lowest playable note is sound 1) and the keys **above the ceiling
counting up** with the sounds after those, as many as fit; a *Default layout* button lays
it again; each key's sound can be typed. The floor and ceiling per channel come from the
driver's period tables: pulse notes 36-119, wave 24-107 (`Driver::lowestNote`, the highest
note with a period), noise 12-127 (no keys above). Off by a toggle.

**Ordering law.** A mapped key and a playable note at the same instant: the key applies
first, so the note sounds with the new sound. The key's recall is done **on the audio
thread**, in front of the note: the row the driver reads for that block is the sound's, the
parameters are brought up to it by the message thread on the next timer (`setValueNotifyingHost`
with no undo step: a played key is not a hand edit), and the audio thread keeps overriding
until the parameters agree. The Sound parameter moving under automation takes the same path.

## 3. Data layouts

```cpp
struct SoloSound { bool used = false; std::string name; uint8_t inst = 0, table = 0; bank::Command cmd1, cmd2; };
struct SoloCommand { bool used = false; std::string name; bank::Command cmd; };
struct SoloState {                      // everything beside the parameters and the bank
    std::array<SoloSound, 64>   sounds;
    std::array<SoloCommand, 32> commands;
    std::array<uint8_t, 128>    keyMap{};   // sound 1-64 per note, 0 none
    bool keyMapOn = true;
};
```
JSON through `BankJson`-style writers: `soloStateToVar` / `soloStateFromVar` (sounds as
`{slot, name, inst, table, cmd1, cmd2}`, commands as `{slot, name, cmd}`, `keyMap` as an
array of 128 ints, written only when not all zero). The plugin state (`ChipBoySoloState`
ValueTree): `version`, the APVTS, `bank` (bankToJson), `solo` (the JSON above), `ui_view`.
A `.cbsolo` file is `{ "format": "chipboy-solo", "version": 1, "bank": …, "solo": …,
"channel": n, "row": {…} }` -- the instance without the DAW: *Save…* / *Load…* in the
Sounds tab, under Documents/ChipBoy/Solo.

Global parameters: `channel` (PU1 / PU2 / WAV / NOI), `model` (DMG / CGB / RAW),
`tempo_source` (Host / Own), `tempo` (40-295, the own tempo; ticks free-run at it when the
host is stopped or absent, the Clock's law), `notes_on_tick` (Quantize, off), `sound`
(0-64), `volume` (NR50 both sides, 0-7), `trim` (dB), `hex`, `keymap` (on). Plus the `s_`
channel set. About 25 in all; the 76-parameter table of the main plugin is untouched.

## 4. The audio path (`SoloProcessor`)

`Apu`, `render::Renderer`, `driver::Driver`, `driver::Clock`; no Player, no link. Per
block: model and options (noise off, lcd off, declick off, soften off), the gate mask has
only the chosen channel on (NR51: the other three are silent and never receive an event),
`GlobalParams` from `volume`, notes-on-tick, the clock from the host play head (Host) or
the own tempo (Own; `setOwnsTransport(true)` with the own transport stopped, so the clock
free-runs -- there is no song to play). Events: every MIDI note / bend / CC on any MIDI
channel goes to the chosen channel; a note-on whose number is a **mapped key** becomes a
sound recall (no event); a note-off on a mapped key is dropped. The row: `params.read(Any)`
with the WAV level folded to four steps as the main plugin does for a Voice; a pending or
still-unmatched recall overrides inst / table / cmd1 / cmd2. `driver_.setParams(ch, row)`,
`driver_.process`, the writes into the APU, `renderer_.render`, the trim. Scope taps as the
main plugin's (`ScopeBuffers`), so `ScopeView`, `RegisterLine` and the Tables tab's running
row work unchanged.

Channel change: flush (AllNotesOff on the old channel), the gate mask moves, the window
rebuilds its tabs (Waves and Kits only on WAV) and re-greys the command palettes
(`CommandSlot::setChannelKind`).

Threads: parameters are atomics; the bank and the Solo state are published by pointer swap
(`std::shared_ptr<const T>`) exactly as ChipBoy's bank; the recall queue from the audio
thread is a single atomic word (`pendingRecall_`: slot | serial). The message thread's
timer applies it to the parameters and clears the override once they match.

## 5. The window (`SoloEditor`, 760 x 500, fixed -- as built 560 x 552, one page at a time, the editors in a compact mode, §227)

Header (28 px): wordmark CHIPBOY SOLO, the channel as a four-way segmented control (its
colour is the window's accent), MODEL, the tempo readout (host BPM or the own tempo,
editable), Hex, undo / redo.

**The strip** (the main window of the brief, always visible, 150 px): the channel's scope
and register line on the left (200 px, the strip's own look), then the row: SOUND (stepper
and name, with *Store*), INST (stepper and name, the driver's loaded instrument as the
strip shows it, §30), TABLE, LEVEL (knob; WAV's four steps), PAN, TSP, CMD 1, CMD 2 with
their captions, and the note line (the last note, the mapped key that selected the sound).

**Tabs**: Sounds · Instrument · Tables · Waves · Kits · Commands · Setup. Waves and Kits
only while the channel is WAV. Sounds: the 64 slots as a `SlotList` (name, the instrument
it recalls), *Recall*, *Store here*, *Rename*, *Clear*, and the key map: two columns of the
keys below and above the range with a sound each, *Default layout*, the toggle. Instrument,
Tables, Waves, Kits: the main plugin's panels through `EditorHost` (§7), in a `ScrollBlock`
where they overflow. Commands: the library's 32 rows (letter, arguments, name; *→ CMD 1*,
*→ CMD 2*, *Store from 1 / 2*) and the letter reference for the channel. Setup: velocity
mode, live follow, quantize, tempo source and own tempo, key map on, trim, the `.cbsolo`
Save / Load and the bank file Load.

Status line: what the last file did, what a recall did ("Sound 5 Bass · key C1").

## 6. Edge cases

- A mapped key while a note is held: the sound recalls; with Live follow the held note
  reloads (an instrument change) or takes the new table / commands -- the brief's law.
- Sound 0 in the parameter: nothing recalled; the row stays.
- A stored sound naming an instrument slot that is later emptied: recalls the number; the
  strip's name reads "— empty —" as ChipBoy's does.
- Channel changed with a kit instrument in the row: the driver cross-reads (§197); the
  window greys the mismatch.
- The key map with a channel change: the range moves, so keys that were mappable may be
  playable now; those entries are ignored while playable, kept in the map.
- No play head (Standalone) or a stopped host: ticks free-run at the tempo in force, so
  tables and commands run while playing live.
- Two note-ons in one block, one mapped: stable sort by offset, recalls before notes at
  equal offsets.
- State load: a project restores the bank, the sounds, the key map, the library; the
  history is cleared (a restore is not an edit).
- The bank's 41 KB and the Solo state never sit on the message thread's stack (Windows'
  1 MB): heap-built as ChipBoy's are.

## 7. The `EditorHost` refactor

The four editor panels read the processor through eight members: `apvts`, `bank()`,
`song()` (one use, the uses count), `editBank()`, `history()`, `scopes()`,
`effectiveTempo()`, `previewKitSample()`, and `channelParamId()`. `plugin/shared/EditorHost.h`
declares them as an abstract class; `ChipBoyProcessor` and `SoloProcessor` implement it;
`EditorPanel` takes an `EditorHost&` (member `host`), the parameter helpers in
`PanelCommon` take an `EditorHost&`, and the main-only panels derive from `MainPanel`
(an `EditorPanel` that also keeps its `ChipBoyProcessor& processor`). The processor-bound
lookups (`trackerPosition`, `playingStepOf`, `channelSourceText`, `modelIndex`,
`analogCornerHz`) move to `main/panels/MainLookups.*`, so `PanelCommon.cpp` and the four
panels compile without the main processor. `EditorHost::channelCount()` (4 or 1) and
`hardwareChannel(lane)` let the Instrument tab's "used on" loop read one lane.

## 8. Tests and checks

- `chipboy_solotest` (console, in the plugin checks): builds a `SoloProcessor`, plays a
  note on each channel and asserts output; a mapped key followed by a note in the same
  block sounds with the recalled sound (the driver's loaded instrument reads the sound's);
  the Sound parameter recalls; a state save / load round-trips sounds, key map, library and
  bank; a `.cbsolo` round-trips; the channel change silences the old channel.
- `chipboy_uishot --solo DIR`: every Solo tab as a PNG, for `docs/screenshots/solo-*.png`.
- The main gate stays green through the refactor (nothing audible changes; the parameter
  table is untouched).

## 9. What this replaces, and what is left

ChipBoy Voice is a remote for one channel of a ChipBoy instance on another track; with
the MIDI map a track's MIDI reaches ChipBoy directly, and with Solo a channel on its own
track has its own sound. The user's word after the first build: Voice stays -- it is used
less than expected, not retired.

Left for a later round: window scaling (the main window's 125 / 150 %); MIDI learn for
the sound slots; a Solo instance reading the *same* bank as a ChipBoy instance live (a
`.cbsolo` or a bank file carries it across today).
