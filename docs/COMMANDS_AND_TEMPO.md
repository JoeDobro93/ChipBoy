# Commands, channel lanes and tempo

Design as agreed on 2026-09-07. This supersedes spec §8.2 (tick sampling of notes),
§9.6 (command letters), §12.3–12.4 (parameter sets) and UI_DESIGN §4 where they differ.
Nothing here is optional; the three implementation stages (core, plugin, interface)
build exactly this.

## 1. The problem it solves

Two control systems overlapped: instruments held the sound, and a second set of
per-channel host lanes silently overrode instrument fields, with nothing in the window
showing which lane was winning. The fix is LSDj's own model: **the channel is a
visible tracker row.** An instrument is loaded, a table may run, and up to two commands
modify the running state. Host automation drives those same three things.

## 2. Commands — LSDj's letters, ChipBoy's base-10 arguments

Arguments are `x` and `y`, each 0–255 in the data model; the meaning and the useful
range depend on the letter. The window shows the meaning ("vol 12 · down 3"), never a
packed byte.

| Letter | Name | x | y | Pulse | Wave | Noise | Persists after the note? |
|---|---|---|---|---|---|---|---|
| A | table | table slot 1–64, 0 stops | – | yes | yes | yes | until cleared or a new instrument loads |
| C | chord | semitones | semitones | yes | yes | – | no (per note) |
| D | delay | ticks | – | yes | yes | yes | no (per note) |
| E | envelope | volume 0–15 | 0–7 decay speed, 8–15 attack speed (rate = y & 7) | yes | wave level 0–3 in x | yes | until cleared or a new instrument loads |
| F | frame | frame 1–16 | – | – | yes | – | until cleared or a new instrument loads |
| G | groove | groove slot 1–16, 0 straight | – | tracker timing (Player) | | | until cleared |
| H | hop | step 1–16 (0 stops) | – | tables only | | | – |
| K | kill | ticks after note-on | – | yes | yes | yes | no (per note) |
| L | slide | rate 0–15 (ticks) | – | yes | yes | – | no (per note: portamento from the previous note) |
| M | master volume | left 0–7 | right 0–7 | global | | | until cleared |
| O | pan | 0 off, 1 L, 2 R, 3 both | – | yes | yes | yes | until cleared or a new instrument loads |
| P | pitch offset | 0–255 → signed x − 128 period units | – | yes | yes | – | until cleared |
| R | retrigger | every y ticks; x = volume step per retrigger (0 none) | ticks | yes | yes | yes | no (per note) |
| S | sweep | rate 0–7 | shift 0–7, direction from the instrument unless x ≥ 128 (down) | PU1 | – | – | until cleared or a new instrument loads |
| T | tempo | BPM 40–255 | – | song tempo (Song source only) | | | until the next T |
| V | vibrato | speed 1–15 | depth 0–15 | yes | yes | – | until cleared or a new instrument loads |
| W | wave | pulse: duty 0–3 (12.5/25/50/75 %); wave: wave slot 1–64 | – | duty | wave slot | – | until cleared or a new instrument loads |
| Z | random | max | – | re-runs the other slot with a random argument up to x each note-on | | | – |

`Cmd::A` used to mean envelope and there was no E; that is corrected here. `B` and the
ArduinoBoy letters (N X Q Y) are not implemented. Tables keep two commands per step
with the same letters. LSDj's `S` on the wave channel (synth shape) has no meaning
here and is ignored on WAV.

## 3. The channel

Host-visible parameters per channel, in this order (this is the automation lane set):

| Parameter | Range | Notes |
|---|---|---|
| Source | Omni / MIDI 1–16 / Off | main plugin only |
| Instrument | 0 none, 1–128 | latched at note-on, or live |
| Table | 0 = instrument's, 1–64 | override |
| Level | PU/NOI 0–15, 16 = instrument's; WAV mute/25/50/100/instrument's | |
| Pan | off / L / R / both / instrument's | |
| Transpose | −60…+60 semitones | |
| CMD1 type | none, A C D E F G K L M O P R S T V W Z | |
| CMD1 x, CMD1 y | 0–255 each | meaning per letter |
| CMD2 type, x, y | same | |
| Live follow | on/off | Instrument, Table, Level, Pan, Transpose apply now instead of at the next note |
| Velocity | start volume / instrument bank / ignored | |
| Keyswitches | on/off | octave 24–35 (pulse), 12–23 (wave, noise) selects slots 1–12 |

Gone as parameters: duty, envelope volume/direction/rate, sweep rate/direction/shift,
wave, frame, vibrato speed/depth, arpeggio, detune, LFSR width. They live in the
instrument, or arrive through a command (W, E, S, W, F, V, C/A, P).

### Command slots are "in force"

- A slot **fires when its value changes** (type, x or y), at the next tick.
- While a slot is set it **fires again at every note-on**, after the instrument and
  its table have loaded, so every note in the span gets it.
- Setting a slot to **none reverts** the command's persistent effect to the instrument's
  value (E, F, O, S, V, W), to zero (P), to the parameter's value (M), stops the table
  (A), or restores the phrase's groove (G).
- Order at note-on: instrument → instrument table → CMD1 → CMD2. Z in one slot
  randomises the other slot's `x` each note-on.
- Tracker cells use the same code path: a cell's two commands are applied at the step
  as if the slot had changed to that value, and a cell's instrument column reloads the
  instrument before them.

### Running state (display only)

The driver publishes per channel, next to the registers: duty, envelope (vol, rate,
dir), vibrato (speed, depth), pitch offset, pan, table slot and step, groove. The strip
shows it as a muted line under the two command slots so an automation move is visible
as the slot changing and the running state following.

## 4. Tempo

One global parameter **Tempo source: Host / Song**, and one toggle **Quantise MIDI
notes to ticks** (default off). Ticks are always 24 per beat (6 per sixteenth at the
straight groove, as LSDj); the "ticks per beat", "V-blank" and "custom Hz" tick
sources are removed.

- **Host** (default). Tempo is the host's; ticks sit at multiples of 1/24 beat of the
  host's beat position; tracker steps sit on host beats. Scrubbing is exact. Tempo
  automation is the host's tempo track.
- **Song.** The song owns its tempo: a **Song tempo** parameter (40–255 BPM, automatable)
  plus **T commands** in cells. Tick rate = tempo × 24 / 60 Hz. Tick 0 sits at the
  song start, a host position in seconds held in the song (default 0). The tracker's
  position at any host time is the integral of the song's tempo map from the song start,
  computed from the song alone (base tempo + T cells at known ticks), so a jump to bar 9
  lands on the right step every time and a playback ROM could reproduce it. If the Song
  tempo *parameter* is automated from the host instead, the plugin integrates while
  playing and re-anchors on a locate with the value it currently sees: exact for a
  constant lane, approximate across a ramp. The documentation says so and points to T.
  In Song mode the host's bars are only a ruler; the tracker's bars are its own.
- **Quantise MIDI notes to ticks**: with it on, incoming MIDI note-ons and note-offs
  wait for the next tick (the tracker feel, and keyswitch-selected instruments land on
  the same grid as cells); off, they are sample-accurate. Tracker cells are always on
  ticks. Bends and controllers are never quantised.

Time-signature: beats per bar come from the host in Host mode and from the song
(`beatsPerBar`, default 4) in Song mode.

## 5. Recording

With the arm on, at each step of a Trk channel the recorder writes: the note
(or note-off), the instrument if it differs from the last cell written on that channel,
the table override if set, and the two command slots as in force at that step. A slot
that changed since the last step is written even without a note. T and G in slots are
written as commands too, so a recorded song carries its own tempo and groove.

## 6. Implementation notes

- Core: `bank::Cmd` gains E, G, T; A becomes table. `driver::ChannelParams` becomes
  `{instrument, table, level, pan, transpose, cmd[2], liveFollow, velocityMode,
  keyswitch}`. A `driver::Clock` (or equivalent) owns tick generation for both sources
  and the tracker's position mapping; the Player consumes ticks rather than ppq.
  The driver exposes the running state; `link::packState` gets a second word.
- Plugin: parameters as in §3 plus `tempo_source`, `song_tempo`, `notes_on_tick`;
  `tick_source`, `ticks_per_beat`, `tick_hz` and the removed lanes go away. Link
  region `kVersion` bumps. The Voice plugin carries the same channel set.
- Interface: the strip shows instrument, table, CMD1, CMD2 (type + x + y as steppers
  with per-letter labels), level, pan, transpose, the running-state line, M/S/KS. The
  header bar gets Tempo source, Song tempo and the quantize toggle, and the Phrases tab
  the song's start and beats per bar; the Hardware tab loses the tick controls; the
  status bar shows "tempo host 120" or "tempo song 150".
- Demo: regenerated with the new parameter table (W for duty, E for envelope, T in the
  Song-tempo variant).
- Old projects and banks are not migrated (agreed).
