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
| C | chord | semitones — the cycle's step 2 (0, x, y; 0, x if y = 0) | semitones — step 3; 0 0 stops; one step per rate + 1 ticks | yes | yes | – | no (per note) |
| D | delay | ticks | – | yes | yes | yes | no (per note) |
| E | envelope | volume 0–15 | 0 and 8 hold, 1–7 decay at that rate, 9–15 attack at y − 8 | yes | wave level 0–3 in x | yes | until cleared or a new instrument loads |
| F | frame | frame 1–16 | – | – | yes | – | until cleared or a new instrument loads |
| G | groove | groove slot 1–16, 0 straight | – | tracker timing (Player) | | | until cleared |
| H | hop | step 1–16 (0 stops) | – | tables only | | | – |
| K | kill | ticks after note-on | – | yes | yes | yes | no (per note) |
| L | slide | duration 0–255: ticks in Tick pitch speed, 1/360 s otherwise; 0 instant | – | yes | yes | – | no (per note: portamento from the previous note) |
| M | master volume | left 0–15: 0–7 absolute, 8/12 no change, 9–11 up 1–3, 13–15 down 1–3 | right, the same 0–15 scheme | global | | | until cleared |
| O | pan | 0 off, 1 L, 2 R, 3 both | – | yes | yes | yes | until cleared or a new instrument loads |
| P | bend speed | x − 128 per update: period units (Fast/Tick), an immediate offset (Step), or ÷16 semitones (Drum) | – | yes | yes | – | until cleared |
| R | retrigger | 0 none, 1–7 up, 9–15 down by x − 8 | ticks between retriggers × (rate + 1); 0 once | yes | yes | yes | no (per note) |
| S | sweep | rate 0–7 | shift 0–7, direction from the instrument unless x ≥ 128 (down) | PU1 | – | – | until cleared or a new instrument loads |
| T | tempo | BPM 40–255 | – | song tempo (Song source only) | | | until the next T |
| V | vibrato | speed 1–15 | depth 0–15 | yes | yes | – | until cleared or a new instrument loads |
| W | wave | pulse: duty 0–3 (12.5/25/50/75 %); wave: wave slot 1–64 | – | duty | wave slot | – | until cleared or a new instrument loads |
| Z | random | re-runs the last non-Z/H command, adding 0…x to its x | …and 0…y to its y | yes | yes | yes | no (per note-on) |

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

---

# Addendum, 2026-09-08: pitch, bare notes, the tracker, grooves

Agreed after checking the LSDj 9.2.6 manual (the author's own manual source) and
auditing the driver, the Player and the recorder. Binding, like the sections above; it
changes §2 (E wording, L, P, Z, C, R, M), §3 (note-on order) and §5 (recording).

## 7. Pitch: the instrument's PITCH speed, vibrato shape and command rate

LSDj puts the speed of every pitch effect in the instrument, not in the command. ChipBoy
does the same. Pulse and wave instruments (and kits) gain three fields; noise gets the
vibrato shape only.

| Field | Values | Meaning |
|---|---|---|
| **Pitch speed** | Fast (default), Tick, Step, Drum | how P, L and V move. *Fast*: 360 updates a second, tempo-independent. *Tick*: once per tracker tick (24 per beat), so the effect follows the tempo. *Step*: as Fast, except P is an immediate offset instead of a bend. *Drum*: as Fast, but P and L move in semitones (logarithmic) instead of period units — for P kicks. Not on noise; kits have no Drum. |
| **Vibrato shape** | Triangle, Saw, Square × Down, Up | the waveform of V and of the instrument's own vibrato. *Down* moves between the note and note − depth, *Up* between the note and note + depth. Replaces the old Triangle/Square/SawUp/SawDown enum. |
| **Command rate** | 0–15, default 0 | slows C and R (an interval of rate + 1 ticks per chord/retrigger step), and P and V when the pitch speed is Tick (they advance every rate + 1 ticks). Nothing else. |
| **Table mode** | Tick (default), Step | *Tick*: the table runs one row per tick (or per its own G). *Step*: the table advances one row each time the instrument is triggered (a plain note), LSDj's old "automate". |

The **pitch update** in Fast/Step/Drum is a per-voice clock of 11651 CPU cycles (360.0 Hz)
restarted at every plain note-on, so a note's vibrato and slide are the same whatever
sample the note started on. In Tick mode the update is the tracker tick. Each update
writes the period registers without the trigger bit; nothing is per sample.

The pitch pipeline is `period = periodOf(noteFine) + periodOffset`:

- `noteFine` is the note in 1/32 semitones: note + transpose + the bend wheel + vibrato
  (± depth, from the table below) + Drum-mode P and L.
- `periodOffset` is in NRx3/NRx4 units: Fast/Tick/Step P and L. It is what the sweep
  unit and LSDj operate on, so a linear slide there is the Game Boy's "accelerating"
  slide.

**V vibrato** (`V x y`). `x` 1–15 is the speed, `x` = 0 turns vibrato off (LSDj's V00).
One cycle is 720 / x updates at 360 Hz (so x / 2 Hz: 4 Hz at x = 8) or 96 / x ticks
in Tick mode (x cycles per four beats; identical to Fast at 120 BPM, and it scales with
the tempo — LSDj's "synced to the music"). `y` is the depth in semitones from LSDj's
table: 0 = ⅛, 1 = ¼, 2 = ⅜, 3 = ½, 4 = ¾, 5 = 1, 6 = 1½, 7 = 2, 8 = 2½, 9 = 3,
10 = 3½, 11 = 4, 12 = 5, 13 = 6, 14 = 7, 15 = 8. The instrument's own Vibrato uses the
same speed, depth and delay (delay in ticks, ChipBoy's). The phase restarts at a plain
note-on and continues through bare notes. The two zeroes differ: the instrument's own
Vibrato depth 0 means no vibrato at all (the interface shows "off"), while a `V`
command's `y` = 0 is not off but the smallest step on LSDj's table, ⅛ semitone, as in
LSDj itself.

**L slide** (`L x`). Slides from the pitch the channel is at (mid-slide included) to
the note of the same cell or note-on, in `x` units: ticks in Tick mode, 1/360 s
otherwise; `x` = 0 is instant. Linear in period units, or in semitones in Drum mode.
It is a per-note letter: a slot with L in force gives every note a portamento from the
previous one. In a table, L in the first command column slides to the transpose column's
value relative to the base note, and the transpose and the slide add independently.

**P pitch** (`P x`, signed `x − 128`). Fast: `x − 128` period units per update. Tick:
`x − 128` period units per tick (every rate + 1 ticks). Step: an immediate offset of
`x − 128` units, no bend. Drum: `(x − 128) / 16` semitones per update, so a kick's fall
is exponential. `P 128` stops a bend and keeps the offset; a plain note-on resets the
offset to zero. Not on noise (LSDj's noise P is a shape command and is out of scope).

**C chord** (`C x y`): the cycle is 0, x, y per step; if `y` = 0 the cycle is 0, x;
`C 0 0` stops. One step per rate + 1 ticks. **R retrigger** (`R x y`): every `y` ticks
× (rate + 1), `y` = 0 retriggers once; `x` changes the volume at each retrigger: 0 none,
1–7 up by that much, 9–15 down by `x − 8`. **M master** (`M x y`): 0–7 absolute, 8 no
change, 9–11 up by 1–3, 13–15 down by 1–3, per side. **E**: `y` 0 and 8 hold, 1–7
decrease at that rate, 9–15 increase at `y − 8` (the NRx2 encoding; the wording in §2
was misleading, the behaviour was already this). **Z random** (`Z x y`): re-runs the
last command that is not Z or H — the other slot/column when it is set, else the last
command fired on the channel — adding a random 0…x to its `x` and 0…y to its `y`, at
every note-on. Not changed: A, D, F, G, H, K, O, S, T, W. LSDj's letters that mean
something else here (W on wave, F on pulse, S and P on noise) stay as ChipBoy defines
them and the documentation says so.

## 8. Notes: plain and bare

LSDj retriggers a note only when the instrument column is filled; a note without an
instrument changes the pitch and nothing else. ChipBoy adopts that as the one rule for
both the tracker and MIDI.

- A **plain note** loads the instrument (the whole running state: envelope, duty, wave,
  frame, pan, sweep, vibrato, table restart, pitch offset zero, vibrato phase zero,
  pitch clock restart), then fires the slots (CMD1, CMD2), then triggers the channel
  (NRx4 bit 7; the DMG wave dance on WAV).
- A **bare note** writes only the period (no trigger, no reload, no table restart —
  a Step-mode table does advance one row). The envelope keeps running, vibrato keeps
  its phase, the P offset stays, a slide in force starts from the current pitch. Only
  the per-note letters (C, D, K, L, R, Z) fire again; E, F, O, P, S, V, W, A are already
  in force and are left alone.
- **Tracker**: a cell with a note and an instrument is plain; a cell with a note and a
  blank instrument column is bare; OFF is a note-off. Velocity comes from the cell's
  VEL column (default 100).
- **MIDI**: a note-on is bare when another note is still held on the channel *and* it
  would load the instrument already sounding *and* the instrument's **Overlap** field
  says *legato*. Anything else is plain: no note held, a different instrument (a
  keyswitch since the last note, or the velocity bank picking another slot), or
  Overlap = *retrig*. Overlap replaces the old `legato` flag; factory defaults are
  legato for pulse and wave instruments and retrig for noise and kits (drums).
- **Note stack**: releasing the sounding note while older notes are still held returns
  to the most recent held one as a bare note (no attack). Releasing the last note applies
  the instrument's Note-off mode. Kill drops the DAC. Release lets the envelope finish;
  a hold or rising envelope gets a decrease at rate 1 written so it finishes, and WAV/kit
  step the level 100 → 50 → 25 → mute one tick apart. Ignore leaves it.
- **All notes off** (MIDI CC120/123, and every internal flush) silences a channel
  unconditionally: any voice with its DAC on is killed, the held stack, delayed starts,
  retriggers, slides and tables are cleared, whatever the `active` flag says. It is never
  filtered by the Trk/MIDI source gate.
- A keyswitch or cell instrument stays selected until the Instrument parameter changes
  or another keyswitch/cell instrument arrives; a parameter change clears it.
- A kill (K, or a stop) clears the held stack too, so a later key release cannot restart
  a note nobody is playing. The notes-on-tick queue never runs an event out of order:
  when it is full the oldest queued events run first.

## 9. The tracker

### 9.1 Cells, phrases, bars

`Cell {note, vel, inst, table, cmd1, cmd2}`: `note` 0 empty / 1–127 / 255 OFF, `vel`
1–127 (0 = default 100), `inst` 0 blank (bare) / 1–128, `table` 0 none / 1–64, and two
commands. Phrases are sixteen cells; chains are per channel, one entry per bar; steps
per bar is 8 or 16 (32 was offered and never worked; it goes). The grid shows a VEL
column.

A note sounds until the next note, an OFF, a K, or the end of its instrument's length
(LSDj). A phrase of blank cells therefore sustains the previous bar's note; a bar with
**no phrase** in the chain sends a note-off at its first tick (as now). The Player sends
**All notes off** to a channel when: the transport stops; the tick stream jumps (locate,
loop wrap — any tick that is not the previous tick + 1); the channel leaves Trk, is muted,
or its source changes; a Voice releases or takes the channel; record disarms. The
processor sends the same flush when a channel's Source parameter changes, when link mode
turns off (draining the delayed MIDI first, not dropping it), and when a Voice claim is
lost. The Phrases tab's position readout follows the transport and stands still when it
is stopped; the free-running tick that keeps tables and vibrato alive while stopped is not
shown.

### 9.2 Grooves

A groove is sixteen tick counts, LSDj's screen: `Groove { uint8 ticks[16] }`, each 1–48,
0 = unused; the groove's length is the number of leading non-zero entries (at least 1)
and step *i* of a phrase lasts `ticks[i mod length]` ticks. Groove 0 is straight (6 per
step, not editable); the song holds 16 editable grooves, factory 1 = 6 6, 2 = 7 5,
3 = 8 4, 4 = 5 7, 5 = 9 3, 6 = 4 4 4, the rest 6 6. At 8 steps per bar every entry is
doubled (the existing scaling). Steps whose start tick falls at or beyond the bar's ticks
do not fire; a groove that ends early leaves the last note sustaining to the next bar.
The editor shows the total against the bar's ticks (96 at 4/4) so a user sees when a
groove does not fill the bar.

The groove in force on a channel is: the slot's G if set, else the last G cell, else the
phrase's own, recomputed every tick (no stateful override that can disagree with a
cell). G inside a table sets that table run's row lengths from the groove (default one
tick per row). Recording quantises a note to the nearest step start of the channel's own
grid (its phrase, its groove in force) — not channel 0's.

### 9.3 Tempo map

T cells are placed with the song's bar ticks and the phrase's own groove; in Song mode
that is the grid the Player runs on. A G or T in an automation *slot* is approximate
across a locate, as §4 already says for T; cells are the exact form.

### 9.4 Recording

The recorder writes, per armed Trk channel, so that playing the song back with MIDI and
automation off reproduces what was heard:

- **Note-on** (not a keyswitch): the note, its velocity, and the instrument column
  filled with the instrument actually loaded (keyswitch, velocity bank or parameter) when
  the note was plain, **blank when it was bare** — so an overlap records as a bare cell
  and plays back bare. The table override when set.
- **Note-off**: OFF at its step; if that step already holds a note-on, nothing (the next
  note ends it); if it is the note's own step, OFF goes to the following step when that is
  empty (a note shorter than a step becomes one step long).
- **Slots**: read at the step's tick (not the block start). At every step, a slot whose
  in-force value differs from the last one written on that channel is written into the
  matching command column (CMD1 → cmd1, CMD2 → cmd2), with or without a note. A slot going
  to *none* is written as the letter's **revert form** — the same letter with nothing to
  say but "put this back where the instrument left it" (§3), a cell command whose internal
  `c` field is 1. The per-note letters, which leave nothing behind, write nothing. A plain
  note's cell also carries both in-force slots.
- Not recorded, documented: the Level, Pan and Transpose lanes (static parameters that
  still apply on playback), the bend wheel, controllers, model and hardware options.

A revert cell is the exact form, and the reason there is one: a concrete value —
`E 15 2`, `V 10 2`, `W 2` — would say what the letter reverted *to* at that step and then
stay in force at every note-on after it, overriding velocity accents for ever, losing the
instrument's vibrato delay (a `V` has no argument for it), and beating the instrument a
later keyswitch brings in. The revert form is applied once, through exactly the code a
slot going to none takes, and leaves the slot empty, so what follows is the instrument's
own. The driver's per-note re-fire rules are untouched: there is simply nothing in force
to re-fire.

### 9.5 The demo is tracker-shaped, and a test proves it

`tools/demo/make_demo.py` changes so everything it does is representable: no mod wheel
and no bend wheel; bars 5–8 ride the vibrato depth through the V slot at step resolution;
bars 13–16 use L (portamento) and P (Fast and Drum bends) slots instead of the wheel;
the master dip is an M slot on NOI instead of the master parameters; every envelope point
sits on a step. Model and De-click stay as they are (not tracker-level). The generator also
emits `Demo/chipboy_demo_automation.json` (static parameter values and per-lane
`(beat, value)` points).

`tools/recordtest` (`chipboy_recordtest`, CTest, CI) runs the demo through the
processor twice under a fake play head at 120 BPM, 48 kHz, 512-sample blocks. Pass 1:
the MIDI file and the automation JSON, all channels Trk, record armed; the song is
saved through the JSON writer. Pass 2: a fresh processor, the saved song loaded, no MIDI,
every automated lane held at its bar-1 value, transport from 0. It compares, per channel,
the sequence of APU register writes (order and values exact, time within 64 samples) and
fails on the first difference, printing bar, step, channel and both writes. Model and
De-click are not automated in either pass.

## 10. Interface

- **Instrument tab**: Pitch speed, Vibrato shape and direction, Command rate, Table mode
  and Overlap join the instrument cards (pulse, wave, kit; noise gets shape and Overlap).
  The window's minimum height does not grow: the fields take the space of the removed
  Legato toggle and the cards' spare rows. *As built:* the tallest card set (a pulse
  instrument, two cards to a row) asks 474 px of the pane's 512, against 510 before;
  kits show Pitch speed with **Drum greyed**, since the driver always plays a kit's Drum
  as Fast.
- **Phrases tab**: a **groove editor** — sixteen tick cells, a slot stepper (1–16), the
  total against the bar's ticks, a swing readout (`ticks[0] / (ticks[0] + ticks[1])`, LSDj's
  61 % for 8/5) and a ◀ ▶ nudge that moves one tick between the entries of every pair
  keeping the total. Preferred placement: a column to the right of the phrase grid, row for
  row with the sixteen steps (LSDj's groove screen), with the grid narrowed; if the grid
  cannot give up the width, the free 560 px of the tools' second row. No new height. The
  groove preset combo (which overwrote slots 1–3) goes; the phrase's groove is chosen in
  the grid's chip as now. Steps per bar offers 8 and 16. *As built:* the preferred
  placement won — a 164 px editor column stands right of a 980 px lane, a 12 px gap
  between them, row for row with the sixteen steps.
- The VEL column in the phrase grid; the Voice plugin needs nothing new (instrument
  content is bank content).

---

# Addendum, 2026-09-08 (second): bars, one-shot cells, arms, files, transport

Agreed after playing the round above. Binding. Changes §3 (cells are not slots),
§9.1 (bars), §9.4 (recording bare notes) and §10.

## 11. Bars and steps

- **Steps per bar** is a number, 1–64, typed in the Tracker tab (was 8 or 16). A phrase
  holds up to 64 cells (`kMaxSteps`); the grid shows the bar's step count.
- **Step ticks** = bar ticks / steps per bar, where bar ticks = beats per bar × 24 (the
  host's signature in Host mode, the song's in Song mode). When that is not a whole
  number, step *i* starts at ⌊*i* × bar ticks / steps⌋ (never more than a tick of
  jitter). Grooves keep meaning "ticks at sixteen steps in 4/4": step *i* lasts
  `groove.at(i)` × step ticks / 6, as before. At the usual sixteen, sixteen steps now
  divide whatever the bar's length is rather than being cut off once six-tick steps run
  out — a 3/4 bar (72 ticks) makes sixteen 4.5-tick steps; set *Steps / bar* to 12 there
  to keep the six-tick steps a 3/4 bar had before.
- **A bar override** (`barSteps[bar]`, 0 = the song's default) gives one bar another
  step count, for every channel — one chain row is one bar. Its length is its steps ×
  step ticks, so a bar of 8 steps in a 16-step song moves the whole song on half a bar
  early, and no H is needed to leave it. Bars lie end to end from the song start; the
  tracker owns its ruler. Without overrides its bars coincide with the host's bars
  exactly as today; with them, the host's bars are only a ruler, as in Song mode.
  Position ↔ (bar, step) goes through a prefix sum over the bar lengths, rebuilt when
  the song is published, so a locate lands on the right step. T cells and the recorder's
  quantise use the same table.
- Everything else in §9.1 stands: a missing phrase sends a note-off at its bar's first
  tick, blank cells sustain, all-notes-off on stop, jump, lane change and disarm.

## 12. A cell's commands fire once

A cell's two commands are applied **once, at their step**, and never occupy a slot:

- The persistent letters (A E F G M O P S T V W) change the running state, which then
  holds until the next plain note reloads the instrument or a later command changes it —
  LSDj's rule. So a V written on a bare note in bar 5 stays through bar 5's bare notes and
  ends at the next note that carries an instrument; looping back to bar 1's first note
  (instrument in the column) plays it clean.
- The per-note letters (C D K L R Z) shape the note in that cell only.
- The automation slots keep §3's behaviour: in force, firing at every plain note-on and
  when they change. The earlier rule "a cell applies as if the slot had changed" is
  withdrawn — it made a cell re-fire at every later note, so a vibrato written once stuck
  to every note after it.
- Recording (§9.4 amended): a plain note's cell carries both in-force slots; a **bare**
  note's cell carries the in-force per-note letters (C D K L R Z) only, since the
  persistent ones are already in the running state and re-writing E on a sounding pulse
  would restart its envelope. A slot change between notes is written at its step as now.

## 13. The command octave

MIDI notes 0–11 on any channel never sound: a note-on there fires the channel's slots
(CMD1 then CMD2) on whatever the channel is playing, without a trigger, so a held note
can be shaped after its attack — vibrato in, a slide, a kill. Velocity is ignored. It
needs no keyswitch setting. When recording, it is written as a slot-only cell at its
step (the two commands, no note), which replays the same way.

## 14. Record arms and the playback source

- Each channel has a **record arm** in the song (saved with the plugin state; not a host
  parameter). With the Tracker tab's master **Rec** on and the transport playing, an armed
  channel's incoming MIDI is written to its cells **whatever its playback source**; an
  unarmed channel never records.
- The lane's switch is now the **playback source**: **MIDI** (the channel plays incoming
  MIDI; was "Roll") or **Trkr** (the channel plays its cells; was "Trk"). An armed Trkr
  channel also lets incoming MIDI sound while recording, so an overdub is audible; an
  unarmed Trkr channel ignores MIDI, as now.

## 15. Song files and instrument presets

- A **song file** (`.cbsong`, JSON, song format 4) holds the song alone: chains, phrases
  (only used cells), grooves, steps per bar and the bar overrides, tempo, song start,
  beats per bar, playback sources and arms. It also records the bank's name and the name
  of every instrument slot the song uses, so a load can say where the bank differs
  ("slot 7 was Triangle bass; this bank has Organ"). Loading replaces the song; with the
  bank it was written with it sounds the same — the record test's guarantee. Save/Load
  live in the Tracker tab; the default folder sits beside the banks folder.
- An **instrument preset** (`.cbi`, JSON) is one instrument with every table, wave (the
  frame run's wave slots included) and kit it references, transitively (a table whose A
  starts another table brings that table). Loading into a slot: each dependency goes to
  the first free slot of its kind unless an identical one (same content) is already in
  the bank, and every reference — the instrument's and the tables' — is renumbered to
  match. The load reports what went where in the status bar. The walk and the placement
  are core code (`bank::collectPreset`, `bank::placePreset`), no JUCE; the file format
  and the chooser are plugin code. Save/Load presets live in the Instrument tab beside
  New and Dup; the bank file keeps saving everything as now.

## 16. The tracker's own transport

When the host offers no transport — the Standalone, or a host without a play head — the
Tracker tab's **Play / Stop / Loop** run the song from the song start on the plugin's
own clock at the Song tempo (the header's Tempo source reads Song and is fixed while the
plugin owns the transport). In a host the host's transport rules and the buttons mirror
it, disabled. The demo song file (`Demo/ChipBoy Demo.cbsong`, written by
`chipboy_recordtest --write-song` from its recorded pass so it *is* the recorded demo,
and checked by CTest against a fresh recording) plays in the Standalone this way with
the factory bank.

## 17. Interface

- The tab is called **Tracker**. The explanatory paragraph goes. *As built:* the panel is
  `TrackerPanel.*`, renamed from `PhrasesPanel.*`; the paragraph's space became part of
  the head row below.
- **Chain**, rotated: channels as columns (PU1 PU2 WAV NOI), bars as rows numbered
  1, 2, 3… (no word), lowest at the top, scrolling with the song; a narrow fifth column
  holds the bar's step override (blank = default), typed like a cell. It takes the column
  to the right of the grid where the groove editor sat. *As built:* 164 px — a 25 px
  gutter for the bar number and five 25 px cells 2 px apart — with 22 px rows keeping the
  lane's own rhythm; `ui::ChainStrip` became `ui::ChainColumn`.
- **Per channel**, in the lane head: the arm (a red dot) and a **PLAYS** caption with
  the MIDI / Trkr switch. *As built:* `setChannelArm` / `channelArm`; the switch's
  options read **MIDI** and **Trkr** where they read Roll and Trk (the enum itself is
  unchanged).
- **Steps / bar** is a typed Stepper (1–64). **Save song… / Load song…** and the transport
  buttons sit in the head row; the window's height does not change. *As built:* the
  typed entry is `Stepper::setTyped`, opt-in for this one control — every other stepper
  still only steps. The 112 px head is two 26 px tool rows — Play/Stop/Loop, the playing
  readout, Rec, Steps/bar; then Start, Beats, Save song…, Load song…, Export .gb — over a
  48 px line naming the song and what the last file action did; sixteen steps still fit
  the 1180 × 1020 window exactly, so it does not grow.
- A **Grooves tab** (after Tables) takes the groove editor: the sixteen slots on the left,
  the sixteen-cell editor with total, swing and nudge on the right — grooves serve tables
  as well as phrases. The grid's per-phrase groove chip stays. *As built:* the list reads
  as a bank list — the number, the ticks (`7 5`, `4 4 4`) and the swing the first pair
  makes, with a seventeenth row for groove 0 — beside a `GrooveEditor` now 520 px wide
  with 29 px rows (against 164 and 22 in the lane's old corner), so the bar that shows
  the swing is 426 px instead of 70.
- **Instrument tab**: Save preset… / Load preset…. *As built:* on the row under New and
  Dup; the status bar summarises what `bank::placePreset` did — "Pluck → slot 3; table
  5 → 9 (renumbered); wave 2 reused".
