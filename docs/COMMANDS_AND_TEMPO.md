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
| H | hop | times 0–15, 0 = for ever | row 0–15 to hop to | tables only | | | – |
| K | kill | ticks after the tick it is read on (section 128) | – | yes | yes | yes | no (per note) |
| L | slide | x + 1 updates, linear in semitones: ticks in Tick pitch speed, pitch-clock updates otherwise | – | yes | yes | – | no (per note: portamento from the previous note) |
| M | master volume | left 0–15: 0–7 absolute, 8/12 no change, 9–11 up 1–3, 13–15 down 1–3 | right, the same 0–15 scheme | global | | | until cleared |
| O | pan | 0 off, 1 L, 2 R, 3 both | – | yes | yes | yes | until cleared or a new instrument loads |
| P | bend speed | two's complement −128…127; the measured step table per update — the note (Fast/Tick), one offset of x/32 of a semitone (Step), the period register wrapping at 2048 (Drum) | – | yes | yes | – | until cleared |
| R | retrigger | a signed nibble of volume: 0 none, 1–7 up, 9–15 down by 16 − x; 8 resyncs to the pitch clock | interval y × (rate + 1) + 1 ticks; 0 is every tick | yes | yes | yes | no (per note) |
| S | sweep / noise transpose | PU1: rate 0–7; NOI: the high digit of a two's-complement byte | PU1: NR10's low nibble, 0–7 sweep up at that shift, 8–15 down; NOI: the low digit | PU1 | – | yes (§55) | until cleared or a new instrument loads; on NOI until the next note-on |
| T | tempo | LSDj's byte: `28`–`FF` is 40–255 BPM, `00`–`27` is 256–295 | – | song tempo (Song source only) | | | until the next T |
| V | vibrato | speed 0–15: one cycle every 64/(x + 1) updates, 0 the slowest | depth 0–15: ⅛ to 8 semitones either side of the note | yes | yes | – | until cleared or a new instrument loads |
| W | wave | pulse: duty 0–3 (12.5/25/50/75 %); wave: wave slot 1–64 | – | duty | wave slot | – | until cleared or a new instrument loads |
| Z | random | re-runs the last non-Z/H command, adding 0…x to its x (nibble) | …and 0…y to its y (nibble) | yes | yes | yes | no (per note-on) |

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

**Every number in this section was measured against an LSDj 9.2.J ROM**, not taken from
the manual: the harness is `tools/lsdjref/` and the measurements are
[`docs/LSDJ_PARITY.md`](LSDJ_PARITY.md). Where a law could not be read off the register
log, this section says so.

| Field | Values | Meaning |
|---|---|---|
| **Pitch speed** | Fast (default), Tick, Step, Drum | how P, L and V move. *Fast*: on the pitch clock, 358 updates a second, tempo-independent. *Tick*: once per tracker tick (24 per beat), so the effect follows the tempo. *Step*: as Fast, except P is an immediate offset instead of a bend. *Drum*: as Fast, but P and V move the **period register** rather than the note — for P kicks. Not on noise; kits have no Drum. |
| **Vibrato shape** | Triangle, Saw, Square × Down, Up | the waveform of V and of the instrument's own vibrato. *Triangle* is a **symmetric swing about the note**, and Down or Up only chooses which half of it comes first. Saw and square stay one-sided — from the note to note ± depth — because the ROM's own saw and square could not be read off the register log (LSDJ_PARITY §13). |
| **Command rate** | 0–15, default 0 | slows R (an interval of rate + 1 ticks per retrigger step), and P and V when the pitch speed is Tick (they advance every rate + 1 ticks). Nothing else. *(C had this rate until §37 gave it its own.)* |
| **Chord rate** | 0–15, default 0 | how fast a C chord steps: one step every rate + 1 ticks. 0 is LSDj's one step a tick (§37). |
| **Table mode** | Tick (default), Step | *Tick*: the table runs one row per tick (or per its own G). *Step*: the table advances one row each time the instrument is triggered (a plain note), LSDj's old "automate". |

### The pitch clock

The **pitch update** in Fast, Step and Drum is a clock of **11712 CPU cycles — 358.12 Hz**.
LSDj sets the Game Boy's timer once, at boot (`TMA = $49`, `TAC = $06`), and never moves
it, so ChipBoy's is the same number and, like a timer interrupt, it is **one clock for
the whole driver and it free-runs**: it is *not* restarted at a note-on, because a real
driver's interrupt does not know that a note began. Where a note falls inside the period
is therefore where the player pressed play; it is not a property of the driver, and the
parity harness's timing tolerance is one whole period for that reason.

The tracker tick is the pitch update in Tick mode.

An update writes the period registers without the trigger bit — **both halves, NRx3 and
NRx4, every time**, as LSDj does. NRx4 carries no trigger there, so the extra write
changes nothing but the log, and the log is what a parity harness can line up. An update
writes the period **whenever something is moving it**, and once after a note-on whether
anything is moving or not: a plain note writes its period again one update after the
trigger and then goes quiet.

### The note table

The table is one entry a semitone, `round(2048 − 131072/f)` on the pulse channels and
half that numerator on the wave channel, and a **fraction is interpolated between two
entries in period units**, not in frequency. That is measurable: a vibrato half a
semitone below C-5 lands on 1791, where the exponential curve gives 1790. Whole notes
are the same number either way.

### V vibrato

`V x y`. The phase is a **six-bit counter, 64 to the cycle**, and `x` is the step, so
**one cycle is 64/(x + 1) updates** in Fast, Step and Drum — 5.6 Hz at x = 0, 11.2 at
x = 1, 89.8 at x = 15. `x` = 0 is the *slowest*, not off; the phase steps **after** the
write, so the first update of a note writes the note itself and the swing starts from the
one after it.

In **Tick** mode the cycle is a measured table of tick counts, not 64/(x + 1): one cycle
is **96, 72, 64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4½, 4 or 3 ticks** for x = 0…15 —
the period halves every three speeds — and the command rate slows it as it slows P.

`y` is the depth, the swing **either side of the note** in semitones, and it is LSDj's
own table, confirmed against the ROM for all sixteen values:

| y | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| semitones | ⅛ | ¼ | ⅜ | ½ | ¾ | 1 | 1½ | 2 | 2½ | 3 | 3½ | 4 | 5 | 6 | 7 | 8 |

So at C-5 (period 1798) depth 0 swings 1796…1800, depth 3 swings 1791…1805, depth 8
swings 1759…1831 and depth 15 swings 1650…1890 — the ROM's own numbers. A `V`'s depth 0
is an eighth of a semitone and not "off"; the *instrument's* own vibrato depth 0 is off
(the interface shows "off"), and the only way to stop a `V` is the letter's revert form or
a new instrument. In **Drum** the same triangle and the same depths move the period
register instead, where one semitone is worth about **19.1 period units** whatever the
note; the phase law is the same. The instrument's own Vibrato uses the same speed, depth
and delay (delay in ticks, ChipBoy's). The phase restarts at a plain note-on and
continues through bare notes.

### L slide

`L x`. The slide takes **x + 1 updates** — ticks in Tick mode, pitch updates otherwise —
and is **linear in semitones**. The note walks from the pitch the channel is at
(mid-slide included) to the note of the same cell by a fixed step of
`(target − source) / (x + 1)` in 1/256 semitones, truncated toward zero; because it is
truncated there is usually a little left after the last step, and one more update lands
the pitch exactly on the note. The note-on of a slide **triggers at the pitch the channel
was at**, which is what makes it a portamento: LSDj writes the old period with the
trigger and the slide moves from there. `L 00` is one step, the whole distance, so it
writes the period once and nothing is left over.

Measured: `L 04` from C-4 to C-5 writes 1612, 1668, 1718, 1760, 1798 and then 1798 again —
five equal steps in semitones covering an octave, which in period units are 65, 56, 50,
42, 38. `L 10` writes seventeen steps and lands on the note at the eighteenth.

It is a per-note letter: a slot with L in force gives every note a portamento from the
previous one. In a table, L in the first command column slides to the transpose column's
value relative to the base note, and the transpose and the slide add independently.

### P pitch

`P x`, **two's complement** (§34): the byte 0–255 read as −128…127. The step per update is
not proportional to the value — it is a measured table, and over the whole sweep of 127
values it closes to

> **step = S(|x|) / 256 of a semitone per update**, where
> `S(m) = sum(ceil(j/4), j = 1..m) = (q + 1)(2q + r)` for `q = m / 4`, `r = m mod 4`.

which fits every measured rate to about one part in a hundred. So |x| = 1 is 1/256 of a
semitone an update, 16 is 40/256, 64 is 544/256 and 127 is 2080/256 — eight semitones an
update. The domains:

- **Fast** bends the **note**: the pitch moves in a straight line in semitones and the
  period step grows as it falls.
- **Tick** is Fast clocked by the tracker tick, and one tick's step is **four** of the
  pitch clock's (measured), not the 7.46 that a tick is worth in updates. The command
  rate slows it.
- **Step** is no bend at all but one immediate offset of **x/32 of a semitone**, applied
  at the first pitch update after the note-on rather than in the note's own writes:
  LSDj's note-on writes the pitch the channel was at and the commands move it from there.
- **Drum** bends the **period register** in a straight line and **wraps at 2048** rather
  than sticking at the top — which is what a P kick falling off the bottom really does.
  One semitone of the table above is worth about 19.1 period units there.

`P 0` stops a bend and keeps what it reached; a plain note-on resets the offset to zero.
Not on noise (LSDj's noise P is a shape command and is out of scope).

### C, R, M, E and Z

**C chord** (`C x y`): the cycle is 0, x, y per step; if `y` = 0 the cycle is 0, x;
`C 0 0` stops. The note's own tick plays the root and the chord steps from the tick after
it, one step per **chord rate** + 1 ticks (§37; measured at rate 0: `C 3 7` wrote 1798,
then 1837, then 1881).

**R retrigger** (`R x y`): the interval is **y × (rate + 1) + 1 ticks**, so `y` = 0 is
every tick and not "once". A retrigger writes **the whole note-on sequence again** —
sweep, duty, level, period, trigger — not just the trigger. `x` is a **signed nibble** of
volume change: 0 none, 1–7 up by that much, 9–15 down by 16 − x (measured: `R A` steps the
level down by six). **`x` = 8 is LSDj's resync**: the retrigger runs on the **pitch
clock** instead of on ticks, and writes two registers, the level and the trigger.

**M master** (`M x y`): 0–7 absolute, 8 no change, 9–11 up by 1–3, 13–15 down by 1–3, per
side; both arguments are nibbles.

**E**: `x` is the level and `y` the envelope — 0 and 8 hold, 1–7 fall at that rate, 9–15
rise at `y` − 8. **E does not trigger** *unless the instrument's LENGTH counter is enabled*
(§138, the one exception): it walks the level to `x` by zombie-mode writes at
its own tick (§26) and sets the direction and rate of what happens next, which the driver
then runs itself. Measured: `E 8 0` on a channel sounding at 15 is seven down-triples and
nothing else.

**Z random** (`Z x y`): re-runs the last command that is not Z or H — the other
slot/column when it is set, else the last command fired on the channel — adding a random
0…x to its `x` and 0…y to its `y`, at every note-on; both arguments are nibbles.

### The instrument's own envelope, in software

LSDj **never lets the chip's envelope run**. Every NRx2 it writes has the low nibble
forced to **8** — amplitude, direction up, period zero, a hold — so an instrument's ENV
byte of `F0` goes out as `F8`, `A3` as `A8` and `09` as `08`. The direction bit is not
cosmetic: it is the state every later zombie write starts from, and it leaves the DAC on
at level zero. ChipBoy does the same, and runs the envelope itself off the pitch clock at
the measured rate:

| ENV's rate nibble | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| pitch-clock periods a step | 6 | 11 | 15 | 20 | 27 | 36 | 36 |
| cycles | 70224 | 128740 | 175550 | 234070 | 316010 | 421340 | 421340 |

Rates 6 and 7 measured the same interval over sixteen steps of a held note, which is
either true of the ROM or an artefact of the note; it is recorded as measured. Each step
is a level change and reaches the chip as §26's zombie writes. A note whose envelope
reaches silence ends there.

### Not changed

A, D, F, G, K, O, S, T and W keep what §2 says. LSDj's letters that mean something else
here (W on wave, F on pulse, S and P on noise) stay as ChipBoy defines them and the
documentation says so.

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
  blank instrument column is bare; OFF is a note-off. A filled VEL column is a start
  volume whatever the channel's Velocity mode; a blank one keeps the instrument's own
  volume — so a song file sounds the same in any instance. The recorder fills VEL only
  when the velocity shaped the volume (Velocity mode = start volume); under the bank
  mode the instrument column already carries what the velocity chose.
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
cell). A cell's G changes **how long the rows last** from the step that carries it, and
walks on into the channel's next rows until another G (§135); the slot, being live, only
re-lays the steps inside a row. G inside a table sets that table run's row lengths from
the groove (default one tick per row). Recording quantises a note to the nearest step start of the channel's own
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
the sequence of APU register writes (order and values exact, time within 140 samples -- one
358 Hz instant, since §171's sync wait can hold an instant's work by up to 2.8 ms) and
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
- **Step ticks** = bar ticks / steps per bar, where bar ticks = the **song's** beats per
  bar × 24 in both tempo modes. *(Amended in the third addendum: the host contributes
  the tempo only. Its time signature never reaches the tracker — a DAW change to 3/4
  moves the DAW's bar markers, not the song's bars, and a DAW that goes 4/4 4/4 3/4 5/4
  while the song stays in 4/4 lines back up at the end because the ticks are continuous.
  A song that wants a 3/4 bar sets its beats per bar, or a bar's step override.)* When that is not a whole
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
  the 1280 × 1020 window exactly, so it does not grow.
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

---

# Addendum, 2026-09-08 (third): songs own their sounds, hybrid playback, the head

Agreed after playing the third round. Binding.

## 18. Song tabs, and a song owns its sounds

- The Tracker tab's summary row becomes a **tab strip**: one tab per loaded song, a
  **+** tab that opens a new empty song, an × on each tab (asks before dropping unsaved
  work). A tab is `{Song, Bank, file, name}`. *As built:* `plugin::SongTab` is `{song,
  bank, file, name, bankName, dirty, id}`; `ui::SongTabStrip` stands where the summary
  line was, rebuilt from `tabCount` / `tabName` / `tabFile` / `tabDirty` on the panel's
  timer, at the cost of a comparison when nothing moved.
- **Only the active tab is live.** It is what plays and records; it is what the
  Instrument, Tables, Grooves, Waves and Kits tabs show and edit; the header's Bank group
  shows *its* bank (Load bank… replaces this song's bank, Save bank… writes it; the STOCK
  badge is per tab); the Voice plugin's link sees its bank. Switching tabs sends
  all-notes-off, publishes the tab's song and bank, and gives the Song tempo parameter
  the tab's master tempo. The plugin state saves every tab and which one is active.
  *As built:* the STOCK badge stayed the **window's**, not per tab — it reads the
  de-click and soften-pops departures, which are global and not per song, so a badge
  that could never differ between tabs would only imply that it could. `setActiveTab`
  sends all-notes-off through an atomic the audio thread drains ahead of the block's
  events, then swaps the two published pointers with nothing rebuilt — the tempo map and
  the bar table were already built for that song. `chipboy_uishot --tab-switch` marks
  one tab's bank and song and shoots the Instrument, Tables, Grooves, Waves and Kits
  panes on both sides of a switch, failing if any of them drew the same picture twice.
- A new tab starts with the factory bank. **Song file format 5 embeds the bank**
  (instruments, tables, waves, kits with their samples), so a song file is complete:
  loading one opens a tab with its own sounds. A format-4 file (song only) opens a tab
  with a copy of the active bank and the name-difference report as now. Saving writes
  format 5. Presets carry sounds between tabs. *As built:* `songFileText` writes the
  whole bank (`bankData`, through the bank writer, so kits carry their samples) beside
  the bank's name and every instrument name it already recorded; `loadSong` takes a
  `bank::Bank*` and fills it when the file carries one (`SongReport::hasBank`), against
  which the name-difference report then has nothing to differ. `Demo/ChipBoy
  Demo.cbsong` grew from 104 KB to 147 KB carrying the factory bank, and
  `demo_song_matches` still compares it byte for byte.
- Global, not per song: model, hardware options, master volume, noise and whine, link,
  tempo source, quantize, the channel Source/Level/Pan/Transpose/Velocity/Keyswitch lanes.
  *As built:* all of these are plugin parameters, and a parameter is not per tab by
  construction — nothing extra had to be written to keep them global.
- Undo steps record the tab they belong to and re-activate it. *As built:* `BankAction`
  and `SongAction` carry a **tab id** — stable across closes, unlike an index — and
  `restoreBank` / `restoreSong` activate that tab before restoring; a step whose tab has
  since closed does nothing rather than landing in the wrong song.
  `Source/plugin/ui/EditHistory.*` did not change at all: the actions live in the
  processor, which is where the tab is known.

## 19. Tempo, and the host's signature stays out

- The host contributes the **tempo** only: in Host mode ticks follow the host's beat
  position, continuously, whatever its time signature does. Bar ticks always come from
  the song's beats per bar (§11, amended), so the Beats field works in both modes and a
  DAW signature change never moves the song's bars. *As built:*
  `driver::Transport::beatsPerBar` is gone; `Clock::beatsPerBar()` reads
  `ClockConfig::beatsPerBar` — the song's — in both tempo sources, and the processor no
  longer calls `getTimeSignature()` at all. Considered keeping the host's signature in
  Host mode; rejected — it silently re-cut every song's step grid at the signature
  change, which is the bug this fixes.

- The header's tempo is a **readout**: the host's BPM in Host mode, the active song's
  tempo in force (its master tempo, or the T last passed) in Song mode. The Host/Song
  switch stays in the header. *As built:* a small well replaces the stepper, reading
  `effectiveTempo()` to one decimal with a *host* or *song* tag beside it, sized to the
  tag so neither word is ever cut short.
- Each song has a **master tempo** (`Song::tempoBpm`), edited in the Tracker tab beside
  Start and Beats (typed), mirrored into the automatable Song tempo parameter for the
  active tab; T commands override it from their tick, as before. *As built:*
  `setMasterTempo(bpm)` writes the song and the Song tempo parameter as one undo step,
  and a song snapshot restores both; a host moving the parameter instead writes back into
  the active song's `tempoBpm` on the timer, with no undo step, which is also where the
  tempo map is rebuilt. Activating a tab pushes its tempo into the parameter;
  `tempoInForce()` is now named `effectiveTempo()`.

## 20. Hybrid playback

PLAYS gains a third choice: **MIDI / Trkr / Hybrid**. *As built:* on screen the third
option reads **Hyb** (`tracker::NoteSource::Hybrid = 2`), each option with a sentence of
tooltip, and a Hybrid channel's strip carries a **HYBRID** tag beside its name.

- In **Hybrid**, notes come from MIDI — pitch, gate and velocity (through the channel's
  Velocity mode; the VEL column is ignored) — and *everything else* comes from the
  song's cells at their steps: an instrument column selects the instrument the next MIDI
  note-on loads (a note already sounding is not reloaded, exactly as a cell's instrument
  column is exact under the velocity bank); a table column selects the table the next
  note starts; commands fire once at their step (§12) on whatever sounds — the per-note
  letters act on the sounding note (K kills it, R retriggers it, C arpeggiates it, D delays
  the cell's commands) and **L** sets a portamento for the next note-on. Cell notes and
  OFFs are ignored. *As built:* the Player sends a Hybrid cell's non-note columns as a
  `Command` event marked `NoteEvent::hybrid`, dropping a cell that holds nothing but a
  note entirely; a missing phrase, a stop or a timeline jump no longer flushes a Hybrid
  channel — only the player's own note does that. In the driver, a Hybrid channel's
  `ChannelParams` are read through `effective(ch)`, which zeroes Instrument, Table and
  both command slots; a cell's instrument and table columns select through `ksFromCell`,
  so the velocity bank is not layered on top of what the cell chose.
- **Order at a step**: the cell's columns apply first; a plain MIDI note-on arriving in
  the same tick loads the instrument the cell selected and *then* takes the cell's
  commands, so a note on the step gets exactly what a recorded slot would have given it;
  if no note-on arrives in that tick, the commands land on the sounding note at the
  tick's end. *As built:* the processor sorts a block's events by (offset, kind) — a
  flush, then the song's cells, then MIDI — so a cell is always ahead of the note-on it
  shapes.
- On a Hybrid channel the strip's Instrument, Table, CMD1 and CMD2 lanes, the keyswitch
  octave and the command octave are inert; Level, Pan, Transpose and Velocity mode still
  apply. Recording on an armed Hybrid channel writes cells as usual. *As built:* the
  greyed Instrument, Table and both command slots read **from the cells** where the
  resolved value would be, all four sharing one tooltip — *the tracker's cells drive
  this channel*. Recording writes only what the channel really read: no commands from
  the inert slots, no cell for a note in the inert keyswitch or command octaves.
- **Demo**: `Demo/ChipBoy Demo (hybrid).rpp` — the MIDI item, only the model and
  De-click automation, and the plugin state embedded with the demo song loaded and all
  four channels in Hybrid. The state comes from `chipboy_recordtest --write-state`
  (committed under Demo/, checked by CTest like the song file), and a fourth record-test
  pass proves that MIDI plus the song in Hybrid reproduces pass 1's register stream.
  *As built:* the fourth `chipboy_recordtest` pass keeps only the two parameters not
  inert under Hybrid (`ch1_source`, `ch4_velocity`), and its register stream equals pass
  1's exactly; `--write-state` / `--check-state` build the same project and write
  `Demo/chipboy_demo_hybrid.state`, checked by the new CTest `demo_state_matches`, and
  the file is deterministic — a pinned instance UUID and name, the tab built from the
  song file's own contents rather than opened from a path, nothing timestamped.
  `vst_chunk_lines()` in `make_demo.py` carries the state bytes as base64 lines of 128
  characters, a multiple of four, with the header's size field holding their length;
  with no state the size is zero and the two older projects come out byte for byte as
  before.

## 21. The master section

- **Headphone Noise** is the hiss and the frame hum; **LCD Whine** is the display line,
  an independent switch; **De-click** stays. All three live in the master strip.
  *As built:* `Options::noise` is the hiss and frame hum, `Options::lcd` the display
  line and its harmonic; both phases advance whenever either switch is on, so flipping
  one never moves the other's. The `lcd` parameter's display name changed to **LCD
  Whine** (its id and range did not); the test harness's single `noise` flag still means
  the whole floor, so every existing render test measures what it always did.
- One **VOL** control sets both NR50 sides; the two parameters remain (the M command
  and existing automation address left and right), the control shows the left value
  and writes both. The Hardware tab keeps its measured facts, reworded to the split.
  *As built:* the stepper writes `master_l` and `master_r` as one undo step through an
  explicit `beginGesture` / `endGesture` pair; the readout shows the left value and,
  when something has moved them apart, both as `7·5` with the tooltip naming which is
  which. VOL L and VOL R are gone, and the three switches plus the one stepper take
  exactly the height the old two steppers and two switches did, so the master scope
  stayed the same size. The Hardware tab's whine row no longer greys when the hiss is
  off — the two are independent now.

## 22. The channel scopes

Two cycles of the channel's own frequency, phase-locked on a rising edge, still for a
steady tone and moving smoothly under vibrato. The code intends this already and the
picture flashes; find why (a window that rescales with the period every frame, a
trigger that fails when the ring holds less than two periods, a stale period) and make
it hold. Noise and kits keep a fixed window.

*As built:* measured, not guessed — two frames of a steady tone through the old code
differed by 3832 pixels on the wave scope and by 0 on both pulse scopes. The edge is now
the rise that goes **furthest**, ties broken by the level held **longest** before it,
then the **lowest** level it rises from — three keys that are properties of the shape,
so they name the same phase every frame and re-lock in one frame when the shape
changes. `paint()` stopped reading the `latestCycle` atomic live; the timer captures it
with the samples, so one snapshot draws one picture however often it repaints. A kit now
keeps a fixed window too (`setFixedWindow`, set from the instrument the driver is
actually playing) — a sample has no period to lock to. `chipboy_uishot --scope-check`
holds a note on each channel and compares two renderings a fifth of a second apart; all
four are pixel-identical now.

## 23. The Tracker head

Two rows with captions, grouped: TRANSPORT (Play Stop Loop, the readout) · RECORD (Rec)
· SONG (master tempo, start, beats, steps/bar) · FILE (Save song…, Load song…, Export
.gb); the song tab strip under them, above the lane. No new height.

*As built:* row one is TRANSPORT (*Play* 62, *Stop* 62, *Loop* 52, the LED, *playing*,
the position readout — 328 px) and RECORD (*Rec* 62); row two is SONG (*Tempo* 84,
*Start* 84, *Beats* 62, *Steps / bar* 80 — 562 px) and FILE (*Save song…* 104, *Load
song…* 104, *Export .gb* 96), each pair separated by a 16 px gap with a hairline down
the middle. The budget is unchanged and asserted in the source: 12 px caption + 26 px
controls, 4, the same again, 6, and the 26 px tab strip — 112 exactly — so the lane
keeps its 400 px and the tab still asks for no scrolling at 1280 × 1020. What a file did
still goes to the status bar; the head has no line of its own any more.

## 24. More demo songs

`tools/demo/make_songs.py` (standard-library Python, byte-reproducible) writes
`Demo/songs/*.cbsong` in format 5, each with its own bank — original compositions,
never copies:

1. a **groove** study (7/5 swing, a 4 4 4 triplet section, a G change mid-song);
2. a **time-signature** study (3/4 at 12 steps, a 7/8 bar by override, a 5/4 stretch);
3. a bright **early-handheld RPG** route theme (pulse melody, C arpeggio chords, wave bass,
   noise hats);
4. a bouncy **pink-puffball platformer** tune (duty changes, vibrato, staccato bass);
5. a **modern** track with punchy wave-channel kick and snare from tables and Drum-mode
   bends, a pulse lead;
6. a **wave-manipulation** track, half-time, F/W frame sweeps and P wobbles.

*As built:* `groove-study` (24 bars, 132 BPM) — its triplet section is groove **8 8 8**
at twelve steps, not `4 4 4`, since a groove only fills the bar if its ticks average
six; `meter-study` (20 bars, 126 BPM) — the 7/8 bar is fourteen steps by override and
bars 15–16 are **two** twenty-step 5/4 bars; `route-theme` (24 bars, 132 BPM);
`puffball-bounce` (24 bars, 150 BPM), with a six-bar bridge and PU1's own S sweep on the
way out; `neon-grid` (24 bars, 140 BPM); `wave-study` (24 bars, 100 BPM, half time).

Each is 16–32 bars, described in Demo/README.md, and a CTest `demo_songs_load` loads
every file and plays eight bars, checking it sounds and never hangs. *As built:* the
compiler (`tools/demo/make_songs.py`) is Python data with names instead of slot numbers
— `pulse()`, `wave()`, `noise()`, `table()`, `wav()` — and music as sections of bars,
each a groove and one bar of text per channel; it refuses at generation time a letter
that means nothing on its channel, an argument out of range, an instrument of the wrong
kind, a bank entry nothing plays, a bar under two channels, and a groove that would put
a written step past the bar's end. `chipboy_recordtest --play-song FILE [bars]` prints
the RMS of every bar of every channel, mix and solo, which is what caught a note below
the channel's lowest period going silently absent and a bar with no phrase leaving a
stacked note sounding instead of stopping the channel.

---

# Addendum, 2026-09-09: channels on their own time, honest volume, shaped envelopes

Agreed after playing the fifth round. Binding. §25 supersedes §11 and §18's bar model;
§26 changes how every level change reaches the chip; §27 adds to §7's instrument.

## 25. Every channel keeps its own time

LSDj's rule, adopted whole: **a phrase lasts as long as its groove makes it, and each
channel moves to its next phrase when its own phrase ends.** Bars leave the model.

- A phrase has its own **length**, `Phrase::steps` 1–64 (default 16), and its groove. A
  step is **6 ticks at the straight groove, always**; groove entries are ticks per step,
  as LSDj. Sixteen straight steps are 96 ticks = four beats; a 3/4 phrase is 12 steps; a
  1/32 grid is a `3 3` groove. *As built:* cells live in `Phrase::cells`; `Song::stepsPerBar`,
  `Song::barSteps`, `Song::beatsPerBar`, `Song::barTicks()`, `Song::steps()`,
  `stepsOfBar()`, the bar table and every `barStartTick`/`barAtTick`/`barLengthTicks` are
  gone, and so are `ClockConfig::beatsPerBar` and `Clock::barTicks()` — the clock counts
  ticks and nothing else now.
- The chain is per channel as now, one **row** after another. Row *r* of a channel starts
  at the sum of the durations of that channel's rows before it; an empty row lasts 96
  ticks with a note-off at its start. Channels whose phrases differ in length drift apart
  by design; the chain column highlights each channel's own playing row, so one channel
  can show a row ahead of another. H hops within the channel as before. *As built:* the
  chain's fifth column is the row's **LEN**, typed, where the bar override sat, and each
  channel's own playing row lights in its own column (`ui::ChainColumn`, renamed from
  `ChainStrip`). A row past a chain's end is 96 ticks with a note-off at its start, same
  as an empty one; `tools/demo/make_songs.py` writes a channel's rest as a phrase of the
  section's own width holding a note off rather than an empty row, so a resting channel
  stays in step with the others instead of drifting by the difference (it cost
  `meter-study` its alignment before this fix).
- Position ↔ (row, step) per channel goes through a prefix table over that channel's row
  durations, rebuilt on publish; locate is exact; T cells sit at their channel's ticks;
  the recorder quantises to the channel's own grid. *As built:* `Song::rowStartTicks[ch]`
  is the prefix table, built by `buildRowTables()` when a song is published, read by
  `rowStartTick()`, `rowAtTick()`, `rowTicks()`, `phraseTicks()` and `songTicks()`;
  `Player::Position` is `{row, step, phrase}`; `buildTempoMap()` gathers all four
  channels' T cells and sorts them, the lowest channel winning a tie on equal ticks; the
  record message's bar field (`RecordMessage::bar`) is now `::row`.
- `Song::stepsPerBar`, `barSteps` and `beatsPerBar` go. Song file **format 6** carries
  `steps` per phrase. Loading format 5 or older: every used phrase takes the file's steps
  per bar as its length, and a bar override becomes the length of the phrase in that
  bar (a phrase used with two different overrides is duplicated). *As built:*
  `lengthsFromBars()` in `BankJson.cpp` converts a format-5-or-older file, duplicating an
  overridden phrase into the first free slot and pointing that bar's chain entry at the
  copy; the LSDj-exactness round moved the format on again, to **7**, converting a
  format-6-or-older file's `P`, `S` and `H` arguments to their §34 encoding as it loads.
- The plugin's own transport loops the longest channel; the readout shows the song's
  time and, for the selected channel, its row·step. The host's bars stay a ruler (§19).
  *As built:* `setLoopRows(0, -1)` — row 1 to the last row of the longest chain
  (`loopFirstRow`, `loopLastRow`); the head's SONG group drops *Beats* and *Steps / bar*,
  which is what leaves the model with no bars at all.

## 26. Level changes are zombie-mode writes, never a retrigger

- On pulse and noise, a level change on a running channel — a table's volume column, an
  E that keeps the envelope's direction and rate, a shaped envelope's step (§27), CC7,
  the Level lane, a Release fade — is made the way the hardware allows without a trigger:
  **zombie-mode NRx2 writes** (DMG: a write with envelope period 0 adds one to the volume,
  flipping the direction bit maps the volume to 16 − v; the driver issues the shortest
  sequence to the target on the selected console, and the APU emulates each console's
  own behaviour). No NRx4 trigger, no phase reset. *As built:* `Driver::setLevel(ch)`
  first chose its sequence by a breadth-first search over (volume, direction, period) —
  right by construction, but caught carrying the target level in NRx2's high nibble,
  which the chip never does until the next trigger and which the parity harness showed
  LSDj never does either. LSDj's own two primitives replaced it: `09 11 18` for one step
  down and `08` for one step up, repeated to the target whichever way round is fewer
  writes, at LSDj's own measured spacing (sixteen cycles inside a down-triple, a hundred
  and twelve between triples, sixty-eight between ups) — a full ramp from 15 to 0 is
  about 1700 cycles.
- The **"volume writes at edges"** option and the driver's quiet-edge marker go: a
  program on the Game Boy cannot wait for a pulse's low half, so neither does ChipBoy.
  *As built:* `GlobalParams::volumeAtEdges`, `Driver::kAlignToQuietEdge` and the
  processor's re-sorting of a delayed burst are deleted; the `vol_edges` parameter stays
  as a no-op so the 76-entry parameter table does not move, and the Hardware tab's row
  now reads *no effect* and says why.
- A trigger happens only where a driver needs one: a plain note-on, R, and an E that
  changes the envelope's direction or rate. Wave levels stay NR32 writes. *As built:* the
  driver's own envelope model (`Voice::volume`, `hwPeriod`, `hwUp`, `hwRun`, `hwOn`,
  `hwInitial`) moves at every trigger (`markTrigger()`) and every NRx2 write
  (`emitNrx2()`), exactly as `Apu::writeSquare` moves the chip's, so the two agree; **E
  never triggers**, measured (`E 8 0` on a channel sounding at 15 is seven down-triples
  and nothing else).
- The demo song and state files are regenerated (their register streams change); the
  record test, the demo checks and the six songs stay green. *As built:* measured with
  `--play-song`: `neon-grid`'s stabs are where they were (PU2's bars 13–16 read
  0.09990/0.09895/0.09931/0.09864 before and 0.09996/0.09886/0.09932/0.09854 after, peak
  0.27010 → 0.26992) — the level changes sound the same and are now writes the hardware
  would really take.

## 27. Shaped envelopes

- The instrument's envelope has a **mode**: **Chip** — NRx2 initial volume, direction and
  rate, as today — or **Shaped**: Attack (ticks, from silence to Peak), Peak (0–15),
  Decay (ticks, to Sustain), Sustain (0–15, held while the note is), Release (ticks, from
  the level at note-off to silence), each segment with a **curve**: linear, exponential
  (fast start) or logarithmic (slow start). Wave instruments use the four NR32 levels.
  *As built:* `bank::Envelope`'s `envMode` is written to JSON only when it is Shaped, so
  a factory (Chip) instrument reads exactly as it always did but for one property; the
  curves are integer arithmetic in `bank::envSegmentLevel` — `t/n`, `1 − (1 − t/n)²` and
  `(t/n)²`, rounded half away from zero — so the per-tick level list is a constant of the
  song, not of the machine.
- A shaped envelope is rendered as one level per tick and reaches the chip through §26 —
  one write when the level changes, none while it holds — so a playback ROM can replay
  it from a per-tick list. A note-off on a Shaped instrument starts the Release when the
  Note-off mode is Release; Kill cuts as before. *As built:* a shaped note-on writes NRx2
  with the level, direction **up** and no rate, so a level of zero still leaves the DAC
  on and every later step is a plain §26 zombie write; the Instrument tab draws the list
  as a graph over the fields that produced it (`FormRow`/`FormGroup`, moved into
  `PanelCommon` so the Waves tab could reuse them).
- A table's volume column or an E that fires during a shaped envelope takes over: the
  remaining segments stop until the next plain note-on, and the table or E shapes the
  sound. *As built:* the velocity and the Level lane do **not** set a shaped note's start
  level — the envelope owns it — which is the one thing this section leaves open, and
  which `docs/HARDWARE_DRIVER_AUDIT.md` lists rather than resolves.

## 28. Hardware honesty, and the road to a ROM

- `docs/HARDWARE_DRIVER_AUDIT.md` lists every driver behaviour with the way a Game Boy
  driver realises it — register writes at ticks, the timer-driven 358 Hz pitch clock,
  zombie-mode levels, wave RAM rewrites with the channel off on DMG, kit streaming — and
  marks what is **plugin-only** (MIDI input and its sample-accurate notes, Hybrid's live
  notes, the Voice link). It ends with the data a playback ROM needs: song format 6 with
  its bank, as a compact binary. Anything emulator-only found on the way is fixed (§26)
  or listed with a reason. *As built:* the audit also keeps the approximations that stay,
  each with the reason it stays — a level change while the chip's own envelope is running
  (now unreachable, since the driver never writes a non-zero envelope period), the length
  counter's effect on the *enabled* flag, the 64 Hz envelope phase — and its playback-ROM
  sketch moved on to **format 7** once the driver-exactness round renumbered it.
- Stability: a `chipboy_fuzz` CTest plays random songs — random cells, letters with
  random arguments, grooves, phrase lengths, tempo and mode changes, locates — on every
  channel for 64 rows and fails on NaN/inf, a hang (bounded time per block), or sound
  after all-notes-off; each run prints its seed. *As built:* `tools/fuzz/main.cpp`;
  "audible" is the swing inside a block minus the drift across it, since a DAC switched
  off holds its last level and has no coupling to take that offset away. It found a real
  bug — a note waiting for its tick under notes-on-tick survived an all-notes-off, because
  the driver's pending queue was not part of "every internal flush" — fixed by dropping
  that channel's queued entries in `Driver::allNotesOff`, with a regression test; CTest
  runs eight fixed seeds, and a hundred and forty seeds have passed since.

## 29. The Instrument tab

- A compact revamp: a form with labels left and controls right, thin captions for the
  groups, no card chrome per knob; the envelope as a small graph drawn from the fields
  (Chip: the ramp; Shaped: the ADSR with its curves), with the fields beneath it.
  *As built:* Sound over Pitch & modulation on the left, Envelope over Table & note
  behaviour on the right; the knobs are gone for steppers with a readout — 24 px where a
  dial and its caption asked 70 — so the tallest type (a Shaped instrument) asks 468 px
  of the pane's 530, against 474 for the old four cards, and every instrument type now
  sits at the same height because the picture, not the knobs, is the tall thing.
- Hints under fields become tooltips; the panel shows labels and values only. *As built:*
  a value that means something else says so on the control itself ("4 Hz", "¾ st",
  "every 3", "15.6 ms").
- The Table field: right-click lists the tables; double-click opens the Tables tab on
  that table. *As built:* the same click/right-click/double-click convention as every
  other slot field (§30), carried by `EditorPanel::onOpenSlot` and `selectSlot`.

## 30. Tracker editing and navigation

- **Notes**: Shift+Up/Down a semitone, Shift+Left/Right an octave; a vertical click-drag
  on a note moves it a semitone per six pixels (Shift: octaves); double-click types with
  auto-correction — `a1`, `A 1`, `a#1`, `bb2` → `A-1`, `A#1`, `A#2`; `off` or `-` → OFF;
  Escape cancels. The other columns keep their typed entry. *As built:* an empty box
  blanks the cell; anything else typed is refused rather than guessed at; a whole drag,
  however many pixels it crosses, is one undo step.
- **One convention for every slot field** — the grid's INS and TBL, the groove chip, the
  strips' instrument and table steppers, the Instrument tab's Table field: single click
  selects and types; right-click lists; **double-click opens that item's settings** (the
  Instrument, Tables or Grooves tab with the item selected). *As built:* the cost is that
  a slot stepper's click now **focuses** its readout instead of opening the inline box —
  Enter still opens it — so the double-click can be seen at all; typing digits straight at
  it is unchanged. A delayed-open (double-click) timer was considered and rejected: it
  makes every click on every slot field feel slow to save one keystroke.
- The strip's running-state line goes; the strip's instrument name shows the instrument
  the driver last loaded, so it follows the tracker. *As built:* `VoiceView::instrument`
  follows a cell's `ins` column, an `A`, a keyswitch or a Hybrid channel, prefixing the
  slot number when it differs from the stepper; the strip is 332 px instead of 350, and
  the editor pane took the 18.
- The grid head shows the phrase's **LEN** (typed) beside the groove chip; the chain
  column highlights each channel's own playing row; the head readout shows the selected
  channel's row·step and the song's time. *As built:* the *PLAYS* caption moved into the
  switch's tooltip to make room for LEN; the chain's fifth column is the row's LEN where
  the bar override sat (`ui::ChainColumn`).
- Verbose descriptions across the window (Hardware rows, panel captions, tooltips that
  read as paragraphs) are cut to a label and a one-line tooltip. *As built:* the Hardware
  rows are a label and their measured fact with the explanation moved into the tooltip;
  the Grooves help column is three lines; the strips', the master volume's, the wave
  tools' and the groove stepper's tooltips are one line each.

## 31. Two rules from the P kick, and the LSDj reference

- **A table's first row fires with the note-on**, in the same event, never at the next
  tick: a wave kick whose table drops the pitch must start dropping at once, or the raw
  note is heard for up to a tick — sometimes, depending on where the note fell between
  ticks. Rows after the first step on the ticks as before. *As built:* `startVoice` runs
  row 0 inside the note's own event, after the instrument loads and before the trigger,
  for Tick-mode tables as well as Step-mode ones; `Voice::tableJustStarted` keeps the
  following tick from taking a second row. Firing row 0 *after* the trigger was measured
  too and rejected — it puts the raw period ahead of the drop in the stream, exactly what
  this rule forbids. The parity harness confirms the rule against the real thing: LSDj's
  own table row lands 2932 cycles after the trigger, a thirtieth of a tick.
- **A repeated pitch is never legato.** A MIDI note-on at the *same pitch* as the note
  sounding is plain even under Overlap = legato — legato is for moving between pitches,
  and a drum hit in succession is a retrigger. (§8 amended.) *As built:* the note-on
  order is unchanged and now tested: instrument → table row 0 → command slots → the
  cell's own commands → the register writes ending in the trigger; the pitch clock
  restarts at the note and its first update is one full period later, so a bend never
  doubles the note's own period write; a D delays all of it, row 0 included.
- **The LSDj reference.** An LSDj 9.2 ROM the user owns lives outside the repository
  (`/root/lsdj/`, never committed, never in CI: `*.gb`, `*.sav` are ignored). A parity
  harness under `tools/lsdjref/` authors test songs into an LSDj save, runs the ROM in an
  emulator core with a write log on the APU registers, plays the same songs through
  ChipBoy's driver, and diffs the two register streams per command. It observes
  behaviour only; no code or data from the ROM enters ChipBoy. Its tests skip when the
  ROM is absent. Findings are written to `docs/LSDJ_PARITY.md` and turned into driver
  changes with tests, one letter at a time. *As built:* `-DCHIPBOY_LSDJREF=ON` fetches
  SameBoy (MIT) into git-ignored `TestRoms/SameBoy` and builds `lsdjref_trace`, linked
  nowhere else; the ROM is named by `CHIPBOY_LSDJ_ROM`, and a test that needs it skips
  (exit 77) when it, the boot ROMs or Python are missing, so a plain checkout and Actions
  never see one. Twenty-three test songs (`tools/lsdjref/cases.spec`) produced eighteen
  verdicts — fourteen differences, closed by the driver-exactness round with
  `docs/COMMANDS_AND_TEMPO.md` §7 rewritten from the measured numbers; see
  `docs/LSDJ_PARITY.md` §16–17 for the case-by-case account and what is still different
  by design or left measured but unresolved.

## 32. A running table shows where it is

The Tables tab highlights the row a table is on while it runs, following it down the
steps and through hops and loops. The driver publishes, per channel, the table slot,
its current row and a **run serial** that increments at every table start; the panel
follows, for the table on view, the channel whose run started **last**, so two channels
running the same table show the newer run. When no channel runs it, nothing is lit.
*As built:* `VoiceView` carries `tableRow` (−1 when nothing runs) and the serial,
`tableRun`; `Driver::beginTableRun()` is the one place a run starts, and the processor
publishes both as `ScopeBuffers::tableRun` (`packTableRun()`) without touching
`packState2`, so the link region's layout is unchanged. The panel compares the serial as
a signed difference so it survives a wraparound, and follows its own 30 Hz timer rather
than reading the live value every repaint.

## 33. Wave shaping

The Waves tab gains a **synth**, in the spirit of LSDj's and beyond it, that writes
frames into a run of wave slots from parameters the bank keeps (`bank::Synth`: a source
and a chain of shapers, a start and an end state, and the number of frames to morph
between them), so a run can be regenerated after an edit and the exporter still only
ships frames. *As built:* the shapers' filters are **per-harmonic gains of the wave's own
32-point transform**, not a running filter — a wave is 32 samples and so sixteen
harmonics, so the transform is exact, perfectly cyclic and the same bytes on every
platform, where a running filter's output would depend on where its state started. An
amount of 0 is a no-op on every shaper and the sign is the direction where a shaper has
one; a synthesised sine, triangle, saw and square are the bank's own generators byte for
byte.

- **Sources**: sine, triangle, saw, square with a pulse width, harmonic additive (eight
  partial levels), noise, and the drawn wave. *As built:* each covered by its own case in
  `Tests/WaveSynthTests.cpp`.
- **Shapers**, in order and each with its amount: low-pass, high-pass, band-pass and
  all-pass filters with resonance; drive with clip, fold and wrap; phase rotate; vertical
  shift; invert; reverse; smooth; bit-crush (levels below the 16 the chip has); quantise
  to a step; normalise. LSDj's synth is a subset of this. *As built:* each shaper is
  tested on a known input, and the chain's order is tested too (`Tests/WaveSynthTests.cpp`).
- **Morph**: the run's frames interpolate the parameters from the start state to the end
  state (LSDj's start/end waves), so a frame run sweeps a filter or a pulse width.
  *As built:* tested that the run's ends are the start and end states exactly, and that
  the whole generator is deterministic over every source crossed with every shaper.
- The generator is core code (`bank::synthesize`, no JUCE), deterministic and tested; the
  panel edits the parameters, shows the start and end waves and the run, and offers
  drawing with the mouse on any frame as today. *As built:* **Generate** writes the run
  into the slot as one undo step; the parameters live in `Wave::synth` and travel in the
  bank's JSON, so a run can be made again after a hand edit and the exporter still only
  ships frames.

## 34. Command arguments: two values, one byte

A command's arguments stay `x` and `y` in the model; what changes is that every letter
declares its **shape**, and the window shows one of two views of the same byte.
*As built:* `commandInfo` carries the shape, and `commandByte()` / `setCommandByte()` are
the one encoding both views read — shared by the grid, the strips' `CommandSlot` and the
Voice window, so none of them can show a different byte for the same cell.

- **Nibbles** (V, C, R, M, Z, E, S on pulse, H in tables): `x` and `y` are 0–15 each.
  Decimal mode shows `x,y` (`V 4,6`); Hex mode shows the LSDj byte `xy` (`V46`).
- **Byte** (D, K, L, T, P): one value. Decimal shows the number — P signed, T in BPM
  40–295 — and Hex shows the byte LSDj would: P in two's complement (`FE` = −2), T as
  `28`–`FF` for 40–255 and `00`–`27` for 256–295.
- **Small** (A, G, W, F, O): one value with its own range, shown as a number or as two
  hex digits.

Ranges follow: Z's and M's arguments clamp to 0–15; S's `y` is NR10's low nibble (0–7
up, 8–15 down) instead of a flag on `x`; P is stored two's complement; H in a table takes
LSDj's `times, row` (0 times = forever). The hex byte is exactly what a playback ROM will
carry, so the exporter reads one encoding. Typing follows the view: in Hex mode a
two-digit entry sets the byte, in Decimal mode each value is typed on its own with Tab
between them. *As built:* `TypedEntry` and its parsing moved to `plugin/ui/InlineEntry.h`
with a Tab hook, so the grid and the strips' widgets share one box; a click opens it
holding the current value, Enter commits, Escape cancels, and anything outside the
letter's range is refused; digits typed straight at a cell still work, with a comma to
move on. The driver-exactness round closed what this section had left owed on the engine
side — `P` is read as two's complement (`Driver.cpp` no longer does `c.a - 128`), `S`'s
direction comes from `y`'s bit 3 (`c.b & 8`) rather than `x`'s bit 7, and a table's `H`
is read as `times, row` rather than a bare hop target — so the window and the driver now
agree on all three.

---

# Addendum, 2026-09-09 (fourth): one editing grammar, the window remembers, the Waves tab

Agreed after using the sixth round. Binding. Amends §30 (the one selector convention and
the note gestures), §33 (the synth's data and its panel) and UI_DESIGN §2.1.

## 35. One editing grammar for every value field

Every value field in the window — a lane cell, a table cell, a chain cell, the head's
LEN and groove chips, a stepper's readout — takes the same five gestures, so nothing has
to be learned twice and no field surprises:

| Gesture | What it does |
|---|---|
| **Click** | selects the field (the cursor lands on it) and nothing else |
| **Type** | edits the selected field in place, without a box (below) |
| **Double-click** | opens the inline box holding the value — Enter commits, Escape cancels. On a **slot field** (instrument, table, groove, wave, kit) it opens **that item's own tab** instead: a slot is typed at the selected field already, so the box would add nothing; an empty slot field opens the box |
| **Right-click** | lists the choices where the field has them: the bank's slots by *slot · name*, the command palette, the phrases, the grooves — and, at the top, **Open in its tab** for the slot the field names |
| **Shift + arrows** | moves the value: ←/→ by one, ↑/↓ by sixteen (a note: a semitone and an octave) |

- **Typing in place.** The first digit typed at a selected field **replaces** what was
  there and starts an entry; every further digit is appended. A digit that would push the
  value past the field's limit is **refused** and the value stays where it was — `5`, `56`
  are taken in a 0–127 field and a third digit is not. **Backspace** removes the digit
  typed last (`56` → `5`); once the entry is empty it blanks the cell, as it always did,
  and the next digit starts a new entry (`8`). A stepper has no blank, so an emptied
  entry there leaves the value at zero, or at the range's low end above zero. The digit
  that starts a fresh entry after a full one is the same rule — it replaces. This
  replaces the old behaviour where an overflowing digit silently started a new value.
  Hex display types hex digits, in the grids and at the steppers alike.
- **Double-click is the box** on every value field that has nothing else to open: a
  note, a velocity, a table's volume and transpose, a chain cell, LEN, a plain stepper's
  readout. The command cell's double-click opens the box on its values; on the letter, or
  when the cell holds no command, it opens the palette. Enter on a selected field still
  opens the box, as before; on a command cell, Shift+Enter still opens the palette.
- **Double-click on a slot field opens the item's own tab**, as §30 had it: the lane's
  INS and TBL, the groove chip, the strips' instrument and table steppers, the Instrument
  tab's Table, Wave and Kit fields. A slot is typed at the selected field already, so a
  box there would add nothing, and the tab is what the field points at. An empty slot
  field opens the box. The right-click list also gains the tab as its first entry — *Open
  instrument 05 · Bass in its tab* — so it is reachable both ways. What changed from §30
  is only the single click: a slot stepper's click selects like every other stepper's,
  since the double click can be told apart without it. *(First built as the box on slot
  fields too, with the tab only in the menu, and put back the same day: the tab was the
  gesture that had been asked for.)*
- **Shift with the arrows** was Shift+↑/↓ a semitone and Shift+←/→ an octave on a note;
  it is **swapped**: ↑/↓ move the octave, ←/→ the semitone, left lowering and right
  raising. Every other value field takes the same keys: ←/→ by one, ↑/↓ by sixteen — in
  Hex that is the byte's high digit; in Decimal the step stays sixteen for consistency,
  clamped to the range. A command cell moves the argument the cursor is on (the byte in
  Hex). The vertical drag on a note is unchanged.
- **The window remembers where it was.** Closing the editor and opening it again shows
  the tab, the channel, each tab's selected slot, the Waves tab's frame and morph end, and
  the Tracker's row that were showing, instead of the Instrument tab on slot 1. They are
  kept as `ui_view` in the plugin's state beside `ui_scale`, so a project reopens where
  it was left too.

## 36. The Waves tab, second pass

- The frame strip lays its thumbnails **eight to a row**, stretched to the strip's width,
  with `+` and `−` after the sixteenth; the row of a frame is then its half of the run.
- The synth's **Source** is renamed **Shape**: it is what the wave starts as. Each end of
  the morph has its **own shape** (`SynthState::source`): a run can go from a sine to a
  saw. The render makes both ends' shapes and crossfades them by the morph position
  before the chain, so where the two shapes are the same the bytes are exactly what they
  were; the width and the partials already belonged to the state. A synth file that
  carries the old single `source` reads it into both ends.
- The run is placed by **From** and **To** frame numbers rather than a count:
  `Synth::first` (0-based in the data, 1-based on the panel) and `Synth::frames` together.
  Generate writes the run into frames From…To of the slot, growing the wave to reach
  To when it is shorter and leaving the frames outside the range as they were, so a
  sixteen-frame wave can have its second half regenerated. `bank::synthWriteRun` is the
  one place that does the writing, tested.
- The synth section is regrouped so each control says what it does: **Shape** (the
  Start/End switch, the shape and its own fields — width, partials, the noise seed),
  **Chain** (four rows, each a shaper with its amount and, on the filters only, its
  resonance; a dim line under each row says what that shaper's amount means, from
  `synthShaperHelp`), **Run** (From, To, Generate) and **Preview** (the two ends and the
  run). Resonance is hidden rather than greyed where the shaper has none.
- The tools over the drawing grid are one row under it — **Draw** a sine / triangle /
  saw / pulse into the frame, **Interpolate** the frames between the first and the last,
  and the **view** switch — so the Interpolate button no longer collides with the shape
  selector in the top row.
- The drawing grid gains a second **view**, *Points*: each sample fills its box on a
  32×16 grid instead of standing as a bar. In both views the pointer's column and row are lit softly and a
  small caption in the grid's corner reads the sample number and its level, the way
  LSDj's wave screen shows the coordinates, without a tooltip window in the way.
- Considered and kept out: nothing else of LSDj's synth (its filters are these), and a
  per-frame source list — two ends are what a morph has.

## 37. The chord's own rate

LSDj's one **command rate** slows C, R and the Tick-speed P and V together. A chord at
one step a tick is the LSDj sound and too fast for much else, and slowing it with the
command rate slowed the retrigger with it. The instrument gains a **Chord rate**, 0–15,
beside the command rate: a C steps every *chord rate + 1* ticks and nothing else reads
it; the command rate keeps R, P and V and no longer touches C.

- **Default 0**, one step a tick, so every measured parity case and every existing song
  plays as it did. *As built:* `InstrumentCore::chordRate`; `Driver` reads it where it
  read `cmdRate` for the chord and nowhere else.
- **Files**: `chordRate` in the instrument's JSON. A file without one — every song and
  preset before this section — takes its **command rate** as the chord rate, so it plays
  exactly as before; the parity harness sets both from LSDj's one rate. The demo song
  and the hybrid project's state are regenerated for the new property.
- **Tab**: *Chord rate* under *Cmd rate* in the Instrument tab's Pitch & modulation
  group, reading "every tick", "every 2" ….
- **Test**: `C arpeggiates 0, x, y and the chord rate slows it` — the chord rate slows
  the chord, the command rate does not, and neither disturbs the other.

---

# Addendum, 2026-09-09 (fifth): filling cells, named grooves, wave import, the lane's look

Agreed after the fourth addendum. Binding. Amends §30, §35 and UI_DESIGN §7.

## 38. A blank cell fills itself, and a new note brings its instrument

- **Enter, or a double click, on a blank cell fills it** with the column's most recent
  value — in the lane per channel, in a table per table: the last non-blank value that
  column was given this session, else the nearest non-blank cell above it in the same
  column, else a sensible default: the note the octave setting puts at C (C-4 at the
  default), velocity 100, instrument 1, volume 15, transpose 0. A table slot or a
  command with nothing to copy stays blank: there is no sensible command to invent. On
  a cell that already holds something, Enter and the double click do what §35 says (the
  box, or the slot's tab).
- **A note typed into a blank cell brings the instrument with it**: the cell's INS takes
  the column's most recent instrument when the cell had none. A note off does not, and
  a note changed afterwards — a semitone up, a retyped pitch — never puts a deleted
  instrument back; only the step from blank to a note does. *As built:* `PhraseGrid`
  keeps a `Recent` per channel — note, velocity, instrument, table, both commands —
  refreshed from every cell it writes; `TableGrid` keeps volume, transpose and the two
  commands.
- **A vertical drag on any value cell moves it**, as a note's already did: one unit per
  six pixels, sixteen with Shift (a note: a semitone, an octave), the whole drag one
  undo. Velocity, instrument, table, a command's argument (the byte in Hex), a table's
  volume and transpose. A drag from a blank cell starts at zero.

## 39. Grooves have names

A groove is sixteen tick counts, but "7/5 swing" and "the hi-hat shuffle" are how a
groove is remembered. `tracker::Groove` gains a **name**, up to fifteen characters
(`std::array<char, 16>`, so the factory grooves stay `constexpr`), typed in the Grooves
list as every other list renames — a double click or Enter on the row — and shown
wherever a groove is picked: the Grooves list as *slot · name* with the ticks beside it,
the lane's groove chip menu, the chip's tooltip. The song's JSON carries it as
`grooveNames`; a file without one, which is every song before this section, reads
blank names and the song format stays 7.

## 40. Importing a wave shape

The Waves tab's tools row gains **Import…**: an audio file the platform can read — a
single-cycle waveform is what it is for — read as **one cycle**, mixed to mono, its mean
removed, box-filtered onto 32 samples, peak-normalised and quantised to the sixteen
levels (rounded, no dither: a wave is a shape, not a signal), and written into the frame
on show as one undo. A file longer than a cycle is still read as one: the whole of it
becomes the 32 samples, which is what a user who chose the file meant. *As built:*
`bank::frameFromCycle(const float*, size_t)` in core, tested — a sine of any length is
`frameSine()` to within a level; `plugin::importWaveCycle` does the file reading beside
`importKitSample`.

## 41. The lane reads as rows and channels

- A **2 px divider** stands between the channels' column groups where a hairline did.
- **Row bands**: every fourth row — the beat at the straight groove, rows 1, 5, 9, 13 —
  is tinted, and the rows between alternate a fainter tint, so the eye finds a row and a
  beat without counting. The step numbers keep their emphasis on the same rows.

## 42. MIDI and Hybrid channels show their notes, dimmed

The note column of a channel on MIDI or Hybrid used to show only the host's notes as they
arrived and hid the cell's own. It now shows the **cell's notes dimmed**, the column
washed, and refuses edits — the cells are not what plays. Where a cell is blank and the
host played a note at that step, that note shows fainter still, as before. Switching the
channel to Trkr lights the column and makes it editable again.

## 43. The first note of a loop

A user report from FL Studio: looping a bar of a four-on-the-floor beat, the note on
beat one is often dropped. When a note ends exactly where the loop ends and the next
starts exactly where it starts, the two land on the same sample, and FL delivers the
**note-off after the note-on**. The plugin kept the host's order at equal offsets, so
the new note was cut by the old one's off in the same sample. At one sample offset a
MIDI **note-off now goes before a note-on** (a velocity-0 on counts as an off): the
order is a flush, the song's cells, MIDI offs, then MIDI ons. A note-off cannot lose
anything by going first, and a note-on cannot be killed by the note it replaces.
*As built:* the rank in `ChipBoyProcessor::processBlock`'s sort. Not reproducible here
without the host; the user confirms.

## 44. LSDj's defaults, measured on 9.3.9: a table row is one tick, a chord step is one tick

The user asked what LSDj's default table and chord rates are, to set the defaults for new
instruments, and supplied an LSDj **9.3.9** ROM (outside the tree, as §31 requires). Two
harness cases against it:

- **`f_table_speed`** (new): a table whose transpose column steps 0, 2, 4, 6, 8, 10 —
  the period changes are unambiguous where §10's volume column was not. LSDj wrote a
  new period **every tick** (0.94 / 1.07 alternating, the frame jitter of a 59.7 Hz
  machine against a 20.83 ms tick). **A table row is one tick**, and the "two ticks a
  row" §10 and §17 read off the volume column was the envelope nibble's own timing, not
  the row's. ChipBoy's tables already run one row a tick: the open issue closes with no
  change. *(The trace loops after six rows where the case wrote eight; the last two and
  the zero row write nothing visible. Unexplained and beside the point; noted in
  `LSDJ_PARITY.md`.)*
- **`i_kill_delay_chord`**: `C 3 7` at CMD/RATE 0 stepped **once a tick** on 9.3.9 as it
  did on 9.2.J, and the note's own tick plays the root.

So the defaults stand: a new instrument's **Chord rate** is 0 (one step a tick, §37), and
a new table runs one row a tick with no rate field to add. LSDj has no separate table
speed — its tables take the groove a `G` inside them names, as ChipBoy's do (§32).

## 45. The noise channel takes the table's transpose column

Recreating an LSDj song showed the noise drums flat: the snare's table (−32, +7, then the
note) and the crash's (−3, −9, −9) did nothing, because the noise period was computed
from the note and the channel transpose alone — "noise has no pitch effects" (§7) had
swallowed the transpose column with the bends. The LSDj 9.3.9 trace of the song writes a
new NR43 on every transposed row, one row a tick, the first inside the note-on: the snare
is a three-value pitch sequence, not a hit.

**Now:** a noise note's NR43 is looked up for `note + channel transpose + the table row's
transpose`, on the note-on and again on every tick that changes it, through ChipBoy's own
map (§9.4). P, V and L stay inert on noise (§7). Two measured details are *not* modelled:
LSDj's map runs into 7-bit LFSR values above its A-6 and retriggers the channel when a
row lands there (the snare's +7 row); ChipBoy's map has no 7-bit region — 7-bit is the
instrument's flag — so there is nothing to retrigger for. A converter maps LSDj's rows by
LFSR clock; the curves differ, so a transposed row lands near, not on, LSDj's value. An
LSDj-shaped map as an instrument option is the open item (`HANDOFF.md`).

## 46. The TBL column's span ends where an instrument column begins

A cell's TBL set the channel's table override and nothing cleared it: every later note,
whatever instrument its cell named, played that table until the channel's Table parameter
moved. The command columns never stuck because both are re-read from every cell.

**Now:** a cell that names an **instrument** with a blank TBL puts the override back to
the Table parameter's value (0 when the parameter is off), so the note plays the
instrument's own table — LSDj's rule too, an `A` table lasts until the next instrument
load. A cell with a TBL keeps setting it; a bare cell (blank INS, blank TBL) leaves it
alone. The same holds for a Hybrid channel's cells (§20).
*As built:* `Driver::noteOn`'s cell handling and `Driver::hybridCell`.

## 47. A jump in the tick stream no longer kills, and the step it landed in still fires

Looping in a DAW dropped or clipped the first note of the loop. Two causes, one place:

- The Player treated any tick that was not the last one plus one as a locate and sent
  **All Notes Off** on every lane before playing the tick. The user's request: only a
  **stop or pause** silences the channels; a wrap or a locate does not. A tracker leaves a
  note ringing until the next cell on its channel anyway, so a note carried over a loop
  point is what a tracker does.
- The host wraps mid-block: the block starting at the loop start reports a position a
  fraction of a tick past it, the tick the first step sits on was in the block before,
  and the step never matched a tick exactly. **Now** the first tick after a jump — or
  after Play — fires the **latest step at or before it in its row**, so a step missed by
  a fraction of a tick plays a fraction late instead of not at all. Every later tick
  matches exactly, as before.

Stop, pause, the lane leaving (a source switch, a mute for recording) still send All Notes
Off (§9.1, §20). *As built:* `Player::process`.

## 48. The chain's transpose column

LSDj's chain screen has a transpose beside every phrase; ChipBoy's chain had the phrase
only, so a song imported from LSDj needed a copy of a phrase per transpose. **Now each
chain row carries a transpose per channel**, −128..127 semitones, 0 by default:

- The Player stamps it on every note-on it fires from that row (`NoteEvent::transpose`);
  MIDI notes carry none.
- The driver adds it at the note-on **when the instrument's Transpose is on** — the same
  flag that admits the table's transpose column (§7), which is LSDj's rule as well: a
  drum's TRANSPOSE OFF keeps it out of both. A bare note takes the flag of the instrument
  sounding. Noise notes go through the map transposed (§45).
- The recorded note is the untransposed one; the transpose is the row's, not the cell's.
- Song file: `"chainTransposes"`, four arrays beside `"chains"`; absent reads as 0, so
  every existing file opens unchanged. Rows past a channel's chain read as 0 and the
  arrays are trimmed to the chain when the song is published.

The window shows it as a second, narrower column beside each channel's phrase in the chain
(`UI_DESIGN.md` §7), signed in the display's base, blank at 0.

## 49. An instrument's PU2 transpose, and F on PU2

LSDj's pulse instruments carry **PU2 TSP**, a signed semitone offset that applies only
when the instrument plays on the second pulse — the detune trick behind its phasing
leads — and an `F` on PU2 sets it for the note in progress until the next note (measured
on 9.3.9). ChipBoy's `F` was the wave frame on WAV and inert on the pulses.

**Now** a pulse instrument has `pu2Transpose` (−128..127, 0 default): added to the note
on channel 2 at every note-on, on top of the channel and chain transposes, under no flag
(it is the instrument's own pitch, not a musical transpose). `F x` on PU2 sets the offset
to `x` read two's-complement, like P's argument (§34), for the note in progress and the
notes after it until a plain note-on puts the instrument's own back; the revert form
puts it back at once. F on PU1 and NOI stays inert, and on WAV stays the frame.
*As built:* `InstrumentCore::pu2Transpose`, `Voice::instTranspose`, `Driver::noteOfVoice`.

## 50. P's argument is normalised as it is read

The two's-complement byte P carries (§34) is what a file holds; a file written with the
signed value instead — a converter did — clamped to 0 in the driver and the bend was
gone. The JSON reader now folds a negative `a` on a P into its byte, so both spellings
play the same. Nothing written by ChipBoy changes.

## 51. LSDj 9's envelope, measured, and the shaped envelope's third stage

The snare of a recreated 9.3.9 song rang five times too long. §7's envelope speeds had been
measured on the harness's saves, which carry **format version 0**, so LSDj ran them as the
hardware envelope; a 9.x song's instrument carries a **three-stage software envelope**
instead. Traced on the ROM with a format-22 probe (one noise note, the envelope byte
patched through every speed, then the later stages):

- **Three stages, from the note-on**: amplitude `a1` moves to `a2` at speed `s1`, then to
  `a3` at speed `s2`, then to silence at speed `s3`. A speed of 0 **holds** the level
  reached, for ever. Bytes 1, 9 and 10 of the instrument hold `a1 s1`, `a2 s2`, `a3 s3`;
  byte 14 is not part of it. A stage may rise or fall.
- **The speed table**, one level per this many pitch-clock periods (11712 cycles,
  2.79 ms): `1 2 3 4 6 8 11 15 20 27 36 48 64 86 115` for speeds 1–F. Speed 5 is one
  level a frame; the chip's slowest rate (7, 109 ms) sits between B and C.
- Every level is a zombie-mode step (`09 11 18` down, `08` up, §26), never a trigger.

ChipBoy's **Chip** envelope is the hardware's — one NRx2 write: a level, a direction, a
rate of 15.6 ms × n — and cannot follow this: its fastest rate is speed 5–6 and it has one
stage. The **Shaped** envelope (§27) rendered one level a tick from Attack → Peak → Decay →
Sustain, held, then Release, and lacked two things. **Now:**

- a **Start** level (0–15, 0 by default): the attack begins there instead of at silence,
  so `a1 → a2` is Start `a1`, Attack to Peak `a2`, whichever way it goes;
- a **Fade** stage after the sustain: `Fade` ticks to `Fade to` (a level, 0 by default),
  then held — 0 ticks means no fade, which is the old envelope exactly. `a2 → a3` is the
  Decay to Sustain `a3`; `a3 → silence` is the Fade to 0.

Times are ticks at the song's tempo: `|Δlevel| × period(speed) × 2.79 ms`, so a 165 BPM
snare's `A → 0` at speed 5 (167 ms) is an 11-tick attack from A to 0. A stage faster than
a tick a level (speeds 1–4) is quantised to the tick; LSDj's envelope is tempo-free and
ChipBoy's Shaped one follows the tempo — the trade §27 made for a replayable list of
levels, kept. A converter reads the three stages into Shaped; an instrument with a single
falling stage at speeds 5–B may also be written as Chip at the nearest rate (5 → 1, 7 → 2,
8 → 3, 9 → 4, A → 5, B → 6). *As built:* `Envelope::start`, `fadeTicks`, `fadeTo`,
`fadeCurve`; `Driver::shapedLevel`; `envStart`, `envFade`, `envFadeTo`, `envFadeCurve` in
the JSON, absent reads as before.

## 52. Hex mode counts like LSDj

With the display in Hex, an LSDj user reads the same numbers as on the machine:

- **Slots count from 00**: instrument, table, phrase, groove, wave and kit numbers show
  slot 1 as `00`, and typing `00` selects slot 1; the row and step numbers of the lane, the
  chain, a table and the groove editor start at `00` too. In Decimal they stay 1-based:
  `1` is the first slot and the first row, as the window always said.
- **Transposes are two's-complement bytes**: a table row's transpose, the chain's TSP, an
  instrument's PU2 transpose show `E0` for −32 and `07` for +7, and are typed as the byte.
  Decimal keeps the signed form (`-32`, `+7`), typed with a sign. The table's transpose
  column takes the whole byte, −128..127, where it stopped at ±60.
- **Hex is the default** for a new instance; the Hex Display parameter is still saved
  with the state, so a project keeps whichever it had.

*As built:* `ValueFormat::slot`, `index`, `transpose`; `Stepper::setSlotNumbering` and
`setTransposeNumbering`; the grids' typed entry adds one to a typed slot in Hex.

## 53. A scope shows what is audible

The LSDj song silenced its wave channel between notes through NR51, as LSDj does, and the
scope kept drawing the wave RAM cycling: the trace was the DAC's, not the mix's. **Now** a
channel whose two NR51 bits are both clear draws as off — the dashed baseline — in the
mixer and the visualizer alike, from the mix word the processor already publishes. The
analog trace is clamped to the DAC's sixteen levels, so the coupling capacitor's overshoot
on a DMG step stays inside the scope's grid instead of running through its border.

## 54. Importing LSDj songs

*Import .sav…* in the Tracker head reads an LSDj save and opens the songs the user picks in
tabs of their own, each with the bank it needs (`docs/plan-lsdj-import.md` has the layouts
and the plan). The rules are the ones the recreation established, §45–§52, now in
`Source/core/Import/`:

- **The save** (`LsdjSave`): the working song is the first 32 KB, the file table the block
  at `0x8000`, each file a chain of 512-byte blocks in LSDj's run-length code with two
  escapes and two default codes (`E0 F0` the default wave, `E0 F1` the default instrument).
  Confirmed on the user's save: the SUNRISE file decompresses to the working song, byte for
  byte but for the edit state at `0x3FC1`; six older files read as **format 3**.
- **The model** (`LsdjModel`): a song's **format version** (byte `0x7FFF`) picks how its
  bytes are read — the command letter table, the envelope (three stages or the NRx2 byte),
  the noise map, the wave octave, PU2 TSP; the LSDj versions known to write a format are
  its label. Measured on the user's ROMs (`plan-lsdj-import.md` §3): format **22** is
  9.2.J to 9.4.2, identical on every table traced; **15** (8.8.6) already has the three-
  stage envelope and reads the noise column as a raw NR43, `FF − n`; **11** (8.4.x, 8.5.1)
  has the hardware envelope and the SHAPE noise rule; **0–7** (3.1.5 to 7.0.2) the same
  with the letter table without `B`, and before 5.7 the register-unit pitch laws — all of
  it measured on the 31 archived releases and converted as §56 says. A format no model
  knows takes the nearest below; an unknown one takes the ROM found beside the save when
  it names a version (the cartridge title from 4.3, the welcome string before), else the
  newest. `LsdjModel.h` says how to add a version when a ROM arrives; `HANDOFF.md`
  repeats it.
- **The song** (`LsdjSong`): instruments by slot with the envelope of §51, wave frames per
  LSDj synth, tables with the noise rows converted through the map (§45), one phrase per
  LSDj phrase and channel with the chain's transposes (§48), `A` into the TBL column (§46),
  the noise notes by LFSR clock, grooves, the tempo. **Kits** come from the ROM found
  beside the save (`plan-lsdj-import.md` §4a, measured on 9.2.L): its kit banks are read,
  and a kit instrument becomes a ChipBoy kit holding the samples its notes use, cut to the
  instrument's lengths, at the period its speed byte sets; a note playing both kits at once
  is summed and clipped, since LSDj's DIST modes are not decoded. Without a ROM a kit
  instrument is noted and skipped. What could not be carried over exactly is a **note**,
  one line each; the dialog shows them after the import.

The song file this produces is a plain `.cbsong` once saved; nothing of LSDj's stays in it.

## 55. S on the noise channel: a transpose that adds up

Measured on LSDj 9.3.9 and 9.4.2 (the probe of round 13, `/root/lsdj/archive`): `S xy` on
the noise channel moves the note **by the two's-complement byte `xy` in semitones** through
the noise map — `S01` one up, `SFF` one down, `S10` sixteen up, `S80` eight down (`x` is a
signed nibble, `y` unsigned: `x × 16 + y` is the byte) — and **each S adds to the last**:
`S01` on three rows in a row is +1, +2, +3. A note-on puts the transpose back to zero. LSDj
writes NR43 alone, no retrigger, so the LFSR keeps running.

ChipBoy does the same, and it is the first pitch effect the noise channel takes (§45 still
holds for P, V and L): `S xy` on NOI adds `int8(xy)` to the channel's noise transpose, the
note goes through the map again (`noisePairForNote`) and NR43 is rewritten without a
trigger; the revert form (a table's `S` with no argument, an instrument load) clears it. The
palette lets `x` run 0–15 now; PU1 keeps reading it as the rate 0–7 and the low digit as
NR10's low nibble, so no PU1 song changes.

An S in the note's own cell lands in the note-on's write: ChipBoy writes NR43 once, for the
transposed note, where LSDj writes the note and then the S a tenth of a pitch clock later.
The map continues **below the keyboard** for transposes: a table's column or an S may take
the effective note down to −72 (`Driver::kNoiseMapBelow`), where the deeper shifts live,
while a cell's own note stays 12–127. And the instrument's **Shift** (5 is none) moves the
whole map by octaves: LSDj's noise clocks run from 16 Hz up, ChipBoy's notes from about
2 kHz, so an imported noise instrument takes the Shift that puts the clocks its notes ask
for onto the keyboard (§56).

Before 9.0 the same letter did something else on noise, and the importer converts it (§56).

## 56. Importing the older LSDj formats

All 31 stable releases from the LSDj archive were booted with `lsdjref_trace --init-sav`
and probed with the same saves (round 13; the ROMs stay at `/root/lsdj/archive`, outside
the tree). The **format a version writes** and what changed, measured:

| format | LSDj versions | noise note | S on noise | envelope | P, L | V | letters |
|---|---|---|---|---|---|---|---|
| 0 | 3.1.5, 3.1.9, 3.4.4, 3.5.1 | shape | nibbles | NRx2 | register units | one-sided, register units | no B (3.1: no Z) |
| 2 | 3.6.8 – 4.3.0 | shape | nibbles | NRx2 | register units | 9.x's | no B |
| 3 | 4.4.0 – 5.0.3 | shape | nibbles | NRx2 | register units | 9.x's | no B |
| 4 | 5.7.8 – 6.0.1 | shape | nibbles | NRx2 | 9.x's, pitch modes | 9.x's | no B |
| 5 | 6.4.5 | shape | nibbles | NRx2 | 9.x's | 9.x's | no B |
| 7 | 6.8.2 – 7.0.2 | shape | nibbles | NRx2 | 9.x's | 9.x's | no B |
| 11 | 8.4.0, 8.4.4, 8.5.1 | shape | nibbles | three stages, chip-ramped (§58) | 9.x's | 9.x's | B |
| 15 | 8.8.6 | raw `FF − n` | nibbles | three stages | 9.x's | 9.x's | B |
| 22 | 9.2.J, 9.2.L, 9.3.9, 9.4.2 | the musical map | semitones (§55) | three stages | 9.x's | 9.x's | B |

Formats 1, 6, 8–10, 12–14 and 16–21 were never written by a stable release; a song that
carries one takes the nearest model below. The tick, the table row (one tick), the wave
octave, PU2's byte-2 transpose and the chain's transposes are the same everywhere.

**The noise note before 9** (formats 0–11): the instrument's fourth byte is its **SHAPE**
and the note's octave is all that counts of the note — `NR43 = ~SHAPE + 16 × (5 − octave)`,
saturating at `00` and `FF`, C-2 to B-2 being octave 2. The default SHAPE `FF` puts C-5 at
`00`. The importer reads each noise note with its instrument's shape and takes the ChipBoy
note whose LFSR clock is nearest, as it does for every other format (§45).

**S on noise before 9** (formats 0–15): `S xy` **subtracts `x` from NR43's high nibble and
`y` from its low nibble, each modulo 16 with no borrow between them**, once per command, and
they add up until the next note-on (`SF1` on three rows: `2C`, `3B`, `4A`, `59`). ChipBoy
has no register arithmetic on NR43, so the importer resolves each S to the note it lands
on: it walks the phrases in chain order keeping the channel's NR43, applies the nibble rule
at each S, and writes ChipBoy's S (§55) with the semitone difference between the notes
nearest the two clocks. Inside a table used by noise the rows are resolved from the lowest
note the table is used with and **folded into the transpose column** (the first pass of the
loop; the accumulation past it is not carried), the S itself dropped. A 7-bit toggle (bit 3)
along the way is lost, as §45 says. Format 22's S goes through unchanged: it is §55.

**P and L before 5.7 (formats 0–3)** work in the period register: `P xx` adds `xx` units
every pitch clock, whatever the instrument's byte 5 says (there were no pitch modes yet);
`L xx` slides to the next note at `xx` units per pitch clock. ChipBoy's **Drum** pitch mode
is the register domain on the same clock, so the importer puts every pulse and wave
instrument of these formats in Drum and converts: a `P v` takes the ChipBoy speed whose
measured step (`bendStep256`, 1/256 semitone at 19.11 units a semitone) is nearest `v`
units a clock; an `L v` becomes the **duration** ChipBoy's L wants — the register distance
between the note before and the note after, divided by `v`, less one — from the two notes
around it (a slide whose start is in another phrase takes the channel's last note in chain
order; with no note before it, it is dropped with a note). **V before 3.6 (format 0)** is a
triangle **below** the note, `8 × y` units a clock for `x + 1` clocks and back: it becomes
speed `round(32 / (x + 1)) − 1` and depth `round(8 y (x + 1) / 2 / 19.11)` semitones, centred
— the one-sidedness is lost, noted.

**Any instrument on any channel.** LSDj plays an instrument as the channel's kind — a
pulse on NOI is a noise instrument with the same bytes (its byte 4 the SHAPE), a wave on
PU1 a pulse whose envelope is the wave's byte 1 — and the songs do it often. ChipBoy's
instruments have a type, so such a use gets a **variant** of the channel's kind built from
the same bytes, in the slots above LSDj's 64 (named after the original with the channel).
A note before any cell names an instrument plays LSDj's instrument 00, as the machine does.

**Inside a table on noise before 9**, the transpose column is **subtracted from NR43
byte-wise** (a −2 is +2 on the register; measured on the format-3 songs) and an S row
follows the nibble rule from where the row before left the register; a hop keeps the
accumulation (an `H02` loop sweeps on for ever, measured on 4.x). The importer resolves the
rows for the lowest note the table runs with: the column into ChipBoy's transpose column,
each S into ChipBoy's S with the semitones from the row before. Tables are imported when
they hold anything, allocated or not: LSDj 9's allocation bytes miss tables its instruments
name. A `G` in a table names LSDj's groove 0-based; ChipBoy's slot is one more.

**`H` in a phrase is a chain hop**, not the table hop of §34: `H 0 y` ends the phrase there
and starts the **next phrase of the chain** at its row *y*, and `H x y` with *x* above zero
repeats rows inside the phrase *x* times. Traced on 3.6.8 and 9.3.9, which agree. `H 0 0`
is 296 of the 324 uses across the 43 songs looked at and is exactly ChipBoy's phrase
**length**, so the importer shortens the phrase to that many rows; the row the `H` sits on
does not play, as LSDj's does not. A *y* above zero still starts at row 0, and the
repeating form is dropped; both are noted.

**Left alone, with the reason:**

- **P on noise.** Before 9 it is the S rule every tick (`P01`: `88 87 86 … 80 8F …`); on 9.x it
  walks the noise map by `v / 4` entries a tick with wraps past its ends whose table is not
  measured. Neither is a semitone bend ChipBoy could carry; the command is noted and dropped.
- **Drum mode on 5.7 – 6.0** plays its own note table (C-4 is period 458); not modelled.
- **The kill after the hardware envelope** (a `08` trigger when NRx2's envelope has run
  out, 8.4.x) changes nothing audible.
- **8.4.x's table timing on pulse**: the note-on there does not carry row 0's transpose,
  and a `G` in row 0 already sets row 0's own length; ChipBoy keeps the 9.x rule (§31: row
  0 in the note-on, the groove from the next row). Heard as the arpeggio starting one row
  early. Not modelled until a version rule is worth its own switch.
- **A nibble that wraps** in a long S sweep (`7F` → `80` on the register) is a different
  sound from the semitone step ChipBoy adds; the sweep's first pass is exact, later passes
  of a hopping table drift from it.
- The instrument bytes that pick a pitch mode (byte 5) carry other meanings before 5.7; the
  importer ignores them there.

**.lsdprj / .lsdsng files** (`LsdjSave::decompressProject`) are one song each: an 8-byte
name, a version byte, then the same compressed blocks a save holds, in order — a block-jump
code means "the next block", whatever number it names. *Import .sav…* takes them beside
saves, several at once, and lists them in the same dialog; `--import-sav` on the command
line takes one too. Confirmed on the user's eight projects against the save they were
loaded into: byte-identical songs but for the kit numbers in the kit instruments, which
LSDj renumbers to its ROM's kit list on loading a project.

## 57. A `G` in a table row sets that row's own length

Measured on every archived release from 3.5.1 to 9.3.9 (round 13's `x_ttime` probe, and
GOAL ACH's arpeggio on 8.4.4): a `G` in a table row takes effect **at that row**, and the
groove's first step is the row's own length. LSDj's `G 0A` in row 0 of a table whose groove
is 7 4 gives rows of 7, 4, 7, 4 ticks from the note on.

ChipBoy applied it a row late. The Driver kept the slot for the Player, which handed the
groove's ticks back on the next block, so the row that carried the `G` had already taken the
default one tick — an arpeggio one tick short in its first step and then a step out of phase
against LSDj for as long as the note lasted. The Driver reads the song's groove itself now,
at the moment the command runs (`Driver::applyCommand`, `Cmd::G` with `fromTable`), and the
Player's hand-off, which carries the same ticks, is left as it was for the rows after it.
Row *n* of the run still takes step *n* of the groove, as §44 measured.

Nothing else moved: `G` outside a table still belongs to the timeline, and a `G 0` still
clears the run's groove back to one tick a row.

## 58. Format 11's envelope is three stages too, ramped by the chip

Measured on 8.4.4 and 8.5.1 (round 13's `x_env11` probes): a pulse or noise instrument of
song format 11 carries **three envelope stages in bytes 1, 9 and 10**, the same three §51
found in 8.8.6 and 9.x — but the ramp between them is the **chip's own envelope**, not the
software table.

- Byte 1 goes to NRx2 at the note on: amplitude `b >> 4`, direction bit 3, period `b & 7`.
  The hardware envelope then walks one level every `period / 64` of a second.
- When it reaches **byte 9's amplitude**, LSDj writes byte 9 to NRx2 and retriggers. When
  that ramp reaches **byte 10's amplitude**, it writes byte 10 and retriggers.
- A stage whose period is 0 holds; a stage whose direction cannot reach the next
  amplitude never hands over, and the note holds where it is. Confirmed over stage-2
  amplitudes 2, 4, 8, 9 and 12 and periods 1, 2, 3 and 7: the hand-over lands at
  `|a2 − a1| × period / 64` seconds after the note, within half a step of the free-running
  64 Hz envelope clock.

Formats 0 to 7 (3.1.5 to 7.0.2) ignore bytes 9 and 10 altogether: NRx2 is written once and
the chip is left to it. Traced on 5.0.3 and 7.0.2 with the same probe.

The importer reads all three laws through `LsdjModel::envelope` — `Chip`, `HardwareStages`,
`SoftwareStages` — and the two staged ones land on the same Shaped envelope (§51); only the
milliseconds a level costs differ: `period / 64` of a second on the chip, the measured
period table on the software stages. It matters: 28 of the 61 instruments in one of the
user's format-11 songs and 31 of 42 in another set bytes 9 or 10, and every one of them was
playing a flat hardware ramp to the rail before this.

*(A probe whose save has never been opened in the LSDj editor writes the stage levels of
instrument 00 whatever instrument plays, while the timing still follows the playing one —
a stale editor pointer, not a rule: real saves write each instrument's own bytes, checked
against two of them on the user's song.)*

## 59. `E` re-attacks the note before LSDj 8.8, and an instrument can say so

Measured on 3.6.8, 5.0.3, 7.0.2, 8.4.4, 8.8.6 and 9.3.9 with one probe (a note, then an
`E` two rows later, on a pulse and on noise): every release **through song format 11**
writes NRx2 and then **triggers the channel**, on both pulse and noise. From 8.8.6 on, `E`
walks the level in zombie mode (`09 11 18`) and never triggers, which is what §26 measured
and what ChipBoy does.

The same holds for a **table's volume column**, traced with the same probe: on 3.6.8 and
8.4.4 each row writes NRx2 and triggers; on 9.3.9 it is zombie mode and silent about it.

It is not a detail. The old drum vocabulary is built on it: a table of `E` rows or volume
rows on a noise instrument is a stutter of re-attacks, and a phrase that puts an `E` beside
a note re-hits it. Two of the user's LSDj 3.6 songs use it on nearly every noise cell, and
a format-11 song's pulse parts trigger eleven times a phrase where ChipBoy sounded once.

ChipBoy gains an instrument property, **E re-attacks** (`Instrument::envRetrig`, off by
default, in the Behaviour group beside Transpose and Overlap): with it on, a level
change — an `E` from a cell or a table row, **or a table's volume column**, which is how the
old drums stutter — sets the level as before and then retriggers the channel, the
whole note-on sequence as a retrigger already writes it (§28). **On the pulses and on noise
only**: the wave channel's level is NR32, which needs no trigger, and LSDj triggers none
there on any version (traced on 3.6.8, 5.0.3, 8.4.4 and 9.3.9). Off, `E` behaves exactly as
§26 says, so nothing that exists changes. The importer turns it on for every instrument of
a song whose envelope law is `Chip` or `HardwareStages`, which is formats 0 to 14, and
leaves it off from format 15 up.

This is the shape every version difference should take: a property of the **instrument**,
named for what it does to the sound, not a hidden "LSDj 3.6 mode" that forks the meaning of
several commands at once. A ChipBoy player who never opens an LSDj save can reach for it
because a re-attacking envelope is a sound they want.

## 60. The wave instrument's synth and frame moved to byte 3 at LSDj 9

Measured with one probe on 3.6.8, 5.0.3, 7.0.2, 8.4.4, 8.8.6 and 9.3.9 — an instrument
whose byte 2 names one synth and whose byte 3 names another, each synth holding a
distinctive frame, and the wave RAM read back:

- **Formats 0 to 15** (3.6.8 through 8.8.6): **byte 2** is the wave, `synth << 4 | frame`.
- **Format 22** (9.x): **byte 3**, the position the importer already knew.

The importer had read byte 3 on every format, so every wave instrument of every song older
than 9 collapsed onto synth 0 — LSDj's default ramp — and a song's whole wave voice came
out as one saw. `LsdjModel::waveByte` names the byte now.

The frame within the synth matters as much as the synth: nine of the ten wave instruments
in one of the user's format-11 songs carry a non-zero low nibble. **The reading of that nibble
here is wrong**: §65 measured it as LSDj's LOOP POS, the frame a run returns to, not the frame
a note starts on. `Instrument::waveFrame` and the **Start frame** control it added are
withdrawn there; only the byte's *position* -- 2 before 9, 3 from 9 -- stands.

## 61. What the instrument's Transpose flag gates, and the song's own transpose

§48 said the flag gates the chain's transpose "as it gates the table's column". Half of that
is wrong, and it is why an imported kick swept the wrong way. Measured on 8.4.4 and 9.3.9,
on a pulse and on noise, with the flag on and off in the same song:

| moved by | gated by the instrument's Transpose flag? |
|---|---|
| the **table's** transpose column | **no** — it always applies |
| the **chain** row's transpose (§48) | yes |
| the **song's** transpose | yes |

So `Driver::tableTransposeOf` no longer asks the instrument, and §45's noise column applies
whatever the flag says. The kick that reads `TSP C4` in its table with the flag off drops
sixty semitones, as it always did in LSDj; ChipBoy had been holding it still and letting the
`L` beside it slide up from the note before.

**The song's transpose** is byte `0x3FB5`, a two's-complement semitone offset LSDj's PROJECT
screen sets, and it moves every note of the song that an instrument's flag admits (traced
by setting it to 7 and watching every note rise, except an instrument with the flag off).
ChipBoy's song gains **`Song::transpose`**, shown as *Transpose* in the Tracker head's
TRANSPORT group beside the tempo, in the same two's-complement hex as the chain's column
(§52). The Player adds it to the chain row's transpose, so the instrument's flag gates both
at once, and the importer reads it from the save.


## 62. A table's two command columns run and loop independently

The user's format-11 song plays a six-note arpeggio where ChipBoy played two notes over and
over. The table behind it is the shape LSDj writes for an arpeggio with a swing:

```
table 1B   tsp  00 03 07 0c 07 03 00 ...
           cmd1 O03 O02 O03 O03 O03 O01 H00 ---
           cmd2 G06 H00 --- --- --- --- --- ---
```

Both command columns carry an `H00`. ChipBoy honoured both against its one table pointer, so
the table hopped at row 1 and never reached the rest of the arpeggio.

LSDj's own changelog says what it does, at **v1.3.0B**, 2001:

> the both table command columns now run & loop independently. example: when used in tables,
> the hop ("H") command will affect transpose + left command column if issued in the left
> command column. the command will affect the right command column if used in the right
> command column. both command columns still use the same groove, tho'.

So a table has **two row pointers**: the left column's, which the ENV and TSP columns follow,
and the right column's. An `H` moves only its own.

Measured to confirm it, by playing the user's song out of a save's *working* area (which lets
one byte change between two otherwise identical runs) on 8.4.4, and the same way on a 9.2.L
song whose table 01 has `H01` in both columns:

| what was cleared | 8.4.4 | 9.2.L |
|---|---|---|
| the `H` in **cmd1** | the table stops looping and runs on to row 15 | the same |
| the `H` in **cmd2** | the notes do not move; one `NR11` write a pass goes away | the same |

That leftover `NR11` write is the right column looping over its own two rows, exactly as the
changelog says. Nothing sounds different because the commands it replays (`G06`, then the hop)
only set a value that is already set.

**ChipBoy has one table pointer**, and giving it two would change the table for every ChipBoy
player, not just for imports. So ChipBoy's own columns stay symmetric -- a hop in either is a
hop, because someone who writes one there means it -- and the **importer drops an `H` found in
a table's second command column**, saying so in the notes. That is exact whenever the rows the
right column would replay only set a value (`G`, `O`, `W`, `V`, a `T`), which is what the
column is nearly always used for; where it would replay something that acts afresh each pass
(a `P`, an `R`), the replay is lost. The note names the table so it can be looked at.

## 63. Before LSDj 9 a table's `G` holds the groove's first step

§57 said a table's `G` gives row *n* of the run step *n* of the groove, "as §44 measured". That
holds on LSDj 9 and not before it. The measurement it rested on came from a generated probe
save, and those lie about a table's commands (see the note at the end of this section), so it
was made again on the user's own songs, played out of a save's *working* area so that one byte
could change between two otherwise identical runs:

| ROM (format) | groove `07 04` | groove `04 07` |
|---|---|---|
| 8.4.4 (11) | every row **7** ticks | every row **4** |
| 8.8.6 (15) | every row **7** | every row **4** |
| 9.2.L (22) | 7, 4, 7, 4 | 4, 7, 4, 7 |

Checked both ways round so a symmetric groove could not hide the difference, and on 8.4.4 with
the table reached both by the instrument's own `TBL` and by an `A` in the phrase, which rules
out the entry path. Grooves `03 08`, `09 0D`, `0C 06`, `02 02` and `01 0F` on 8.4.4 all gave
rows of the first step alone, so it is the first step and not, say, the mean.

ChipBoy keeps §57's rule -- it is LSDj 9's, and 9 is what ChipBoy is. The **importer** carries
the older one instead: for a model whose `tableGrooveWalks` is false, a table's `G` is pointed
at a **one step groove** holding that groove's first count, which under ChipBoy's own rule gives
every row of the table the same length. The slot comes from the groove slots the imported song
never names; if the song names all sixteen, the `G` is left as it stands and the notes say so.

Formats 0 to 7 (LSDj 3.1.5 to 7.0.2) are **not measured**: no save of that era in hand plays
under the harness, and the probe saves cannot be trusted here. They take the pre-9 rule, which
is what both formats either side of them do.

**The probe saves lie about a table's commands.** A save built by `tools/lsdjref/lsdjref_sav.py`
and never opened in LSDj's editor reports a hop from a table's *second* command column that a
real save does not (§62), and drops a table's `G` entirely. Rebuilding the same table inside a
real song's working area gives the real answer. Table work is measured that way from now on;
this is a second artifact of the same kind as §58's envelope stages.

## 64. A table's three lanes run on their own pointers

§62 found LSDj's two command columns looping independently and worked around ChipBoy's single
table pointer by dropping the second column's hop. That was the wrong way round: the
independence is a feature, not a quirk to be flattened. One column can walk the pan while the
other walks the duty at another rate, and a great deal of what an LSDj table sounds like comes
out of that. **ChipBoy gets the lanes**, and the import stops dropping anything.

There are **three**, not two. LSDj's changelog at v1.3.0 calls the ENV column "carillon-style
... the first digit sets amplitude, the second digit sets duration", and it runs on its own
clock too. Measured on 8.4.4 in the user's song, one table with the transpose column stepping
every tick and the ENV rows lasting four: the pitch moved every 6.5 pitch clocks while `NR22`
moved every 25.9.

| lane | columns | steps on | hops on |
|---|---|---|---|
| **volume** | VOL, LEN | its row's LEN, else the table's row length | its row's LEN set to `H` |
| **one** | TSP, CMD 1 | the table's row length (§57, §63) | an `H` in CMD 1 |
| **two** | CMD 2 | the table's row length | an `H` in CMD 2 |

The row *length* stays shared -- LSDj's changelog is explicit that "both command columns still
use the same groove" -- so §57's `G` and §63's older reading of it time all three; the volume
lane's LEN is what overrides it.

**The ENV byte, measured on 8.4.4** (one `NR22` write per lane step): the low digit is a
duration in ticks, `1` giving 6.5 pitch clocks and `E` giving 89.7, exactly *n* ticks. `0`
kills the lane -- nothing is written at all, so `A0` is a blank row and not "amplitude 10".
`F` is a **hop**: the lane jumps to the row the high digit names. The amplitude `0` is a real
level: `01` writes `NR22 = 00`.

**The volume lane ends at its first empty row.** Traced with rows 0 and 1 set, row 2 empty and
row 3 set: LSDj plays the first two and stops -- it never reaches row 3, and it does not loop
back to row 0 either. Only its own hop brings it round. So the lane is a little program of its
own, self-delimiting, where the other two walk the whole table and take its End. ChipBoy does
the same, which is a change for a song written before this round whose VOL column had a gap in
it (`CHANGES.md`).

ChipBoy's table gains a **Len** column beside Vol: blank, `1`-`15` ticks, or `H0`-`HF` to hop
the volume lane. `TableStep` carries `volTicks` (0 = as long as the table's row) and `volHop`
(-1 = none). A song written before this round has `volTicks = 0` on every row, so its volume
column still steps with the table's row; the one thing that changes for it is that an `H` in
CMD 1 no longer drags the volume column along, which is recorded in `CHANGES.md`.

The `H` a table run reaches on a lane counts its `times` on that lane, and a note-on resets all
three pointers together.

**The volume lane's hop is free**, and the row it lands on plays in the same tick. That is
LSDj from 8.9.3 ("table envelope hops ... now happen immediately"), confirmed on 9.2.L: two
rows of four ticks with a hop under them cycle every 27 pitch clocks a row, with no extra tick
anywhere. Before 8.9.3 the hop row costs a tick -- measured on 8.4.4, where three content rows
and a hop take four ticks a cycle -- and the import asks for that by giving the hop row a
**LEN of 1**, which the lane spends before it jumps. So a ChipBoy table hops like LSDj 9, and
an older save still sounds like itself, with no separate rule in the engine.

Formats 16 to 21 (LSDj 9.0 and 9.1) take the older model's flag because no ROM in hand writes
them; they are after 8.9.3 and would want the free hop.

## 65. The wave instrument's frame run: LENGTH, LOOP POS, SPEED and PLAY

§60 read the low nibble of the wave instrument's synth byte as *the frame a note starts on*.
It is not. Measured on 8.4.4 by decoding every wave RAM load against the song's own frames:
the run **always starts at frame 0**, and the nibble is LSDj's **LOOP POS**, the point a LOOP
or PING-PONG returns to. ChipBoy's `Instrument::waveFrame` is withdrawn, and the
instrument's **Start frame** control with it.

Sweeping all sixteen instrument bytes, three carry the run and nothing else does:

| byte | field | values |
|---|---|---|
| 9, bits 0-1 | **PLAY** | 0 manual (no advance), 1 once, 2 loop, 3 ping-pong |
| 10, low nibble `n` | **LENGTH** | the run visits `L = 16 - n` frames; `n = F` freezes on one |
| 11 | **SPEED** `s` | a frame every **`s + 4` ticks** (`s = 0` gave 25.9 pitch clocks = 4 ticks, `s = F` gave 122.8 = 19) |

LENGTH does not shorten the run to its first frames -- it **spreads** it across all sixteen:

```
frame(i) = min(15, (i * 16) / (L - 1))     i = 0 .. L-1, integer division; L = 1 is frame 0
```

which is exact on every length traced (`0 5 10 15` for L = 4; `0 2 4 6 9 11 13 15` for L = 8;
`0 15` for L = 2).

**LOOP POS is counted against the sixteen, not against the run**: the loop covers the last
`16 - LOOP POS` steps, clamped to the run. LENGTH 8 with LOOP POS 9 loops from run step 1;
LOOP POS 4 with the same length loops the whole run. PING-PONG bounces between that step and
the run's end (`0 5 10 15 10 5 0 ...`).

ChipBoy's wave instrument gains **Frames** (`frameLength`, 0 = every frame) and **Loop from**
(`frameLoopStep`, a *run* step, not a frame) beside **Frame advance**, which keeps its meaning
of ticks per frame. The importer does the `16 - LOOP POS` arithmetic once, so ChipBoy's field
means one plain thing, and sets `frameAdvance = SPEED + 4`. `F` still names the frame itself, whether
or not the run visits it -- traced with a run of eight on 8.4.4, `F 06` loaded frame 5, which
that run skips -- and the run's step goes to the nearest so a later advance carries on from
about there.

## 66. The noise channel's two sweep domains

§55 measured `S` on noise as semitones through the map, and §56 left `P` on noise dropped and
an older save's `S` "resolved for the loop's first pass". Both were the same missing piece:
**before LSDj 9 the noise commands work on the NR43 byte, not on a note**, and ChipBoy only
had the note. It has both now, chosen per instrument.

### Measured

On 8.4.4, one noise note and the command on a later row, reading `NR43`:

| command | `NR43` |
|---|---|
| `S 11` three times | `10` -> `0F` -> `FE` -> `ED` |
| `S 0F` twice | `10` -> `11` -> `12` |
| `P 01` | `10` -> `1F` -> `1E` -> `1D` -> ... one step a tick |
| `P 10` | `10` -> `00` -> `F0` -> `E0` -> ... |
| `P FF` | `10` -> `21` -> `32` -> `43` -> ... |

So both letters do the same arithmetic: **each nibble of `NR43` less the matching nibble of
the value, modulo sixteen, with no borrow between them**. `S` does it once; `P` does it every
tick and keeps going. The low nibble carries the LFSR width bit, so a sweep can flip the
channel from fifteen bits to seven mid-note, which is where a lot of LSDj's noise character
comes from.

On 9.2.L the same `P` walks the **map** instead -- `NR43` steps through LSDj's own noise
entries, `P 04` one entry a tick and `P 01` one every four -- so the speed is **`value / 4`
entries a tick**, upward in clock for a positive byte. `S` there is §55's semitones.

### What ChipBoy does

The noise instrument gains **Sweep**, one of

* **Notes** (the default, and LSDj 9's): `S` adds its two's-complement byte to the channel's
  noise transpose (§55) and `P` bends the note through the map at `value / 4` notes a tick.
* **Register**: `S` subtracts its byte from `NR43` nibble-wise, once, and `P` does it every
  tick. The delta accumulates -- nibble-wise sums compose, so one running byte holds it --
  and the note, the transposes and the instrument's Shift still choose the pair it is
  subtracted from. A note-on clears it, as LSDj's does.

The importer sets **Register** for every format before 22 and passes the bytes straight
through; the "resolved for the loop's first pass" and "P on noise is dropped" notes both go.
Nothing about ChipBoy's own noise changes unless an instrument asks for Register.

## 67. An `E` hands the level back, and the envelope it names has to run

The user's PU1 phrase 82 carries `E1f`, `E67`, `E1f`. Traced on 8.4.4, LSDj writes each one
straight into `NR12`: `1F` at the note, `67` six steps later, `1F` six after that, and nothing
in between. So `E xy` **is** the register byte -- volume `x`, then `y` as direction and rate --
and it takes the instrument's own envelope over for the rest of the note.

ChipBoy read the byte correctly and then dropped the rate. Its imported instrument runs a
**Shaped** envelope (§51, §58), so `Voice::shapedOn` is set; an `E` sets `shapedTaken`, which
stops the shaped envelope rendering, and sets `envVol`, `envRate` and `envDir` for the software
envelope to run (§26). But `stepSoftEnvelope` refused to run **whenever `shapedOn` was set**,
taken or not. Between the two the level jumped to `x` and then froze: over sixty writes of the
song's `NR12` and not one with a rate nibble in it.

The gate is now "a shaped envelope owns the level **until something takes it over**":

```cpp
if (v.shapedOn && !v.shapedTaken) return;      // was: if (v.shapedOn) return;
```

It is an engine fix, not an import one, and it applies to every version: `E` means the same
thing on all of them, and the same freeze hit a table's volume column and a velocity change,
which set `shapedTaken` the same way.

## 68. A table's `L` beside a transpose slides *to* it, from the plain note

The user's wave kick (instrument 10, table 01) is one row: `TSP C4` and `L20`. On 8.4.4 it
sounds the note and slides down from there -- traced, `60, 59, 58, 57 ...` a semitone a pitch
clock. ChipBoy started at 38 and went its own way: a laser, not a kick.

§31 has the table's first row fire *inside* the note-on, where its transpose is running state
and "the note's own writes carry it". That is right for a row that only transposes. With an
`L` on the same row the transpose is the slide's **destination**, so the note has to sound
plain and move to it -- and inside the note-on the channel has no pitch of its own yet, so
`Cmd::L` was sliding from whatever the last note left in `pitchNowFine`.

A table's `L` fired inside a note-on now starts from the note **without** the table's transpose
column:

```cpp
if (fromTable && inNoteOn_) fromFine = lround((noteOfVoice(ch) - tableTransposeOf(v)) * 256.0);
```

Only a table's: an `L` in the note's own cell is still a portamento from the note before it
(§20), which is a different thing with the same letter.

## 69. A `G`'s number is the groove's, as the Grooves tab counts them

Reading the imported song beside LSDj, the user followed table 1B's `G 0A` to groove `0A` and
found the wrong groove. Both numbers were right and they meant different things: a slot counts
from `00` in Hex (§52), so ChipBoy's tenth groove is the tab's `09`, while the command cell
showed the stored slot -- `10`, which is `0A`.

`commandByte` and `setCommandByte` treat a `G` as a slot now: the byte is the slot less one,
so the cell reads `09` for the tab's `09`, and typing `06` selects the seventh slot -- which is
LSDj's groove `06`, the same number in both programs. ChipBoy's extra "straight" state (slot 0)
has no byte, which is right: it is the absence of a `G`.

## 70. The envelope steps at the chip's rate, in every version

`docs/LSDJ_PARITY.md` §7 measured LSDj stepping a level itself off the 11712-cycle pitch
clock, on the table 6, 11, 15, 20, 27, 36, 36 for rates 1-7, and noted that rates 6 and 7 came
out equal -- "worth one more run before that is taken as certain". They are not equal, and the
table is not right.

**Measured on 9.3.9**, on the rig in `docs/LSDJ_COMMAND_MATRIX.md` §3.4, by holding a note and
reading the interval between the zombie steps of `E F y`:

| rate | 9.3.9, pitch clocks | §7's table (9.2.J) | the chip's own rate |
|---:|---:|---:|---:|
| 1 | 6 | 6 | 5.60 |
| 2 | 11 | 11 | 11.19 |
| 3 | **17** | 15 | 16.79 |
| 4 | **22** | 20 | 22.38 |
| 5 | 28 | 27 | 27.98 |
| 6 | **34** | 36 | 33.57 |
| 7 | **39** | 36 | 39.17 |

9.3.9's intervals are the **chip's own**, `rate * 65536` cycles, to within the rounding to a
whole clock. §7's numbers came off generated probe saves -- the §58 and §63 trap -- and its
rates 6 and 7 are a sixth apart in reality.

So there is one law, not two. **What changed in 8.8.0 is who does the stepping, not how fast**:
before it the chip's envelope generator runs, after it LSDj walks the level itself with §26's
zombie writes, and both give a level every `rate / 64` seconds. ChipBoy keeps walking the level
itself either way -- §27's list of levels is what lets a playback ROM replay a part -- and now
does it on the one measured table. The step counter counts in 256ths of a pitch clock and
subtracts the period rather than clearing, so a rate whose interval is not a whole number of
clocks keeps its average and the error never accumulates.

The `envChipTiming` flag this section first introduced is gone: it selected between two tables
that turn out to be one. `envRetrig` stays, because *that* difference across 8.8.0 is real -- an
`E` re-attacks the note before it and never triggers after (§59).

## 71. A slide holds its own aim: the table's column, and the bottom of the range

§68 got the wave kick's *start* right -- a table row carrying a transpose beside an `L`
sounds the plain note and slides to the transposed one -- and its sweep still came out as a
rising whine. Two things were wrong once the slide was running, and the register log of
LSDj 8.4.4 playing SPACE TI's WAV phrase 17 shows both.

The kick is instrument 10, table 01: row 0 is `TSP c4` -- **signed, sixty semitones down** --
beside `L20`, and rows 1 to 13 are empty. LSDj writes, from the note-on at C-5:

```
period  1923 1911 1900 1887 1873 1857 1840 1823 1803 1782 1758 1732 1705 1675 1642
        1607 1568 1526 1481 1431 1377 1318 1255 1184 1109 1027  937  840  735  620
         494  359  210   50   44          <- and there it stops, 92 ms after the note
```

**The table's column cannot move the target.** A table steps every tick, so a slide of any
length outlives the row that started it. ChipBoy kept the slide as a residual added to the
live pitch, and the live pitch reads the table's transpose column, so one tick in -- when the
table stepped from row 0 to the empty row 1 -- the base jumped sixty semitones and took the
sounding pitch from note 70 to note 132. That is the whine, and it is why it *rose*: the
residual was still walking down through a base that had leapt up. A slide now holds a copy of
that column for its whole run, chosen so the base sits exactly on the target, and the residual
falls to zero right where the slide is aimed however the table steps underneath.

**The aim itself is a note the channel can sound.** Sixty semitones below C-5 is note 12, and
the wave channel bottoms out at note 24 -- period 44, because below that the period would
have to pass 2048 and there is no register for it. LSDj divides the distance to the
*reachable* note by `x + 1`: 48 semitones over 33 updates, 1.4545 a piece, landing on 44 as
the last update falls due. ChipBoy divided 60 by 33 and got 1.818, a quarter too fast, and
then ran off the bottom into periods that wrapped. The target is clamped to
`lowestNote(channel)` before the step is worked out, so the rate is right *because* the
destination is.

With both, ChipBoy's sweep is LSDj's period for period, off by one unit twice where the
fixed-point step rounds the other way.

> The hold lasts the slide, not the note. Nothing measured says what a table row that sets a
> *new* transpose under a running slide should do -- the kick's rows are empty -- so the
> simple rule stands until a song shows otherwise.

> The command-by-command comparison against LSDj 9.3.9 -- what each letter does there, what
> ChipBoy does now, whether the importer can bridge the two, and how to probe another ROM
> version -- lives in `docs/LSDJ_COMMAND_MATRIX.md`.

## 72. `S` on PU1 is a running sweep byte, not an assignment

`docs/LSDJ_COMMAND_MATRIX.md` §6.15. LSDj does not write `NR10` from the command. The channel
keeps a **sweep byte, held inverted**, seeded at every note-on from the instrument's own sweep
field; `S xy` **adds** `x` to that byte's high nibble and `y` to its low, the low nibble masked
to four bits so it never borrows into the high one; and `NR10 = ~byte` goes out with the note's
other writes. A sweep of `00` seeds the byte at `FF`, which is why a single `S` on a fresh note
comes out as `NR10 = ((-x) & 15) << 4 | ((-y) & 15)` — the published formula, and only that case.

Measured: `S23` on four consecutive rows of one note gives `ED CA A7 84`; the same `S23` on an
instrument whose sweep is `11` gives `FE`, not `ED`.

**Carried by** `Voice::sweepByte`, one byte, inverted as LSDj holds it. `Instrument`'s
`sweepRate` / `sweepShift` / `sweepDown` stay as they are — they are what the *instrument*
carries and what the UI edits; the voice no longer keeps its own copy of the three, because a
running byte cannot be split back into them once a carry has crossed a nibble.

- **note-on / instrument load**: `sweepByte = ~((rate << 4) | (down ? 8 : 0) | shift)`.
- **`S x y`**: `sweepByte += x << 4` (a plain byte add, so a carry out of bit 7 is dropped);
  then `sweepByte = (sweepByte & 0xF0) | ((sweepByte + y) & 0x0F)`.
- **every `NR10` write**: `~sweepByte`.
- **the revert form** puts the instrument's byte back, as every other letter's does.

`S` on PU2 and WAV stays inert, and on noise stays the accumulating semitone/nibble transpose of
§55 and §66 — which was already right.

## 73. `B`: the chance command, and its two different laws

`docs/LSDJ_COMMAND_MATRIX.md` §6.2. `B` is new to `bank::Cmd`, appended after `Z` so no
existing enum value moves and no song file changes meaning. Two forms:

- **In a phrase or a cell** — the byte gates whether the note sounds. Each nibble is an
  independent roll that passes `n` times in 15, and the note sounds if **either** passes. `B00`
  never sounds; any nibble of 15 always does.
- **In a table** — a hop to row `y` taken with probability `x`/**16**. A *different* law: `BF0`
  hops fifteen times in sixteen, not always. Zero `x` never hops.

Both are confirmed against the ROM: the phrase roll reduces a random byte by 15, the table hop
compares an unreduced random byte against `x << 4`.

**Carried by** `Driver::chanceRoll(int ch, int n)` for the phrase form and a plain
`randomArg(ch, 15) < x` compare for the table one, both on the voice's own `rng` so a render is
reproducible from a seed. A phrase `B` that fails **suppresses the note-on entirely** — the
instrument is not loaded, the table does not start, and the channel keeps what it had — which is
what LSDj does. It does not suppress the row's *other* command.

## 74. `Z` re-runs its own lane

`docs/LSDJ_COMMAND_MATRIX.md` §6.19. Not "the last command executed": LSDj keeps a last-command
record per **lane** and `Z` re-runs its own lane's. The lanes are the four channels' phrase
commands, and every table's column 1 and column 2 separately. A command that ran a row earlier
in the *other* column of the same table is not what a `Z` re-runs.

**Carried by** `Voice::lastCellCmd` (the channel's phrase/cell lane) and
`Driver::zRec_[slot][column]`, one `bank::Command` per table slot and column. `H` and `Z` are
never recorded, as before. The randomisation is unchanged and was already right: `0..x` on the
target's high nibble and `0..y` on its low.

## 75. `M`'s two halves

`docs/LSDJ_COMMAND_MATRIX.md` §6.11. Each nibble independently: **0-7 sets** that side's volume,
**8-15 shifts** it by `0 +1 +2 +3 −4 −3 −2 −1`, clamped to 0-7. `masterFromArg` already had the
shape and the up half; its down half mapped 12-15 to `0 −1 −2 −3`. The offset is the low three
bits read as a signed 3-bit number: `off = ((n - 8) ^ 4) - 4`.

## 76. `R`'s interval is `y` ticks, and `y = 0` fires once

`docs/LSDJ_COMMAND_MATRIX.md` §6.14. Measured: `R01` retriggers once a tick, `R02` every two,
`R04` every four — the interval is `y` ticks flat, not `y × (rate + 1) + 1`. `y = 0` retriggers
**once** and stops rather than every tick. `x = 8` keeps the fast clock (about 0.29 ticks) and
any other `x` is the signed volume step, both unchanged.

`Voice::retrigEvery` keeps its meaning but is read as ticks directly; `Voice::retrigOnce` is new
and says the `y = 0` shot is still owed.

## 77. `C` and `V` reach the noise channel

`docs/LSDJ_COMMAND_MATRIX.md` §6.3 and §6.17. Both work on the ROM and both were dropped. The
noise channel already takes the note, the transposes and the table's column through the map in
`writePeriod`; the chord step and the vibrato are two more semitone offsets on the same note, so
the fix is to read them there rather than to branch. The vibrato is rounded to the nearest whole
semitone before the map lookup, because the map has no room between entries.

The `if (noise) break;` at the top of `Cmd::C` and `Cmd::V` goes; `Cmd::L` keeps its own, because
a slide walks fractional semitones a map cannot follow.

## 78. `F` on the pulses: PU1's fine offset, PU2's two nibbles

`docs/LSDJ_COMMAND_MATRIX.md` §6.6. Three different things by channel, and two of them were
wrong.

- **PU1** — a downward finetune of `y`/32 of a semitone; `x` does nothing. **Absolute, not
  cumulative**: three `F0F` in a row leave the note exactly half a semitone down, and a note-on
  puts it back. ChipBoy's `fineOffset` is in 1/256 semitones, so it is `fineOffset =
  instrument's finetune − 8 × y`.
- **PU2** — an upward transpose of `x` semitones **plus** `y`/32 of a semitone, also absolute and
  also cleared by a note-on. ChipBoy read the whole byte as a signed semitone count.
- **WAV** — the frame, unchanged (§65).
- **NOI** — inert.

## 79. `E` on the wave channel reads `y`

`docs/LSDJ_COMMAND_MATRIX.md` §6.5. On WAV/KIT the level is `NR32`'s two bits and LSDj takes
them from the command's **low** nibble: `y & 3` of 0/1/2/3 gives mute / 25% / 50% / 100%. ChipBoy
took `x`. One nibble, in the `wave` branch of `Cmd::E`.

## 80. A phrase `H` is two commands, and ChipBoy expresses one of them

`docs/LSDJ_COMMAND_MATRIX.md` §6.8, measured with **two** phrases in the chain -- a one-phrase
chain cannot separate "end the phrase" from "hop to step 0", and reading it on one is what made
the first two attempts at this entry wrong.

- **`H 0 y`** ends the phrase at that step, and the **next phrase in the chain starts at step
  `y`**. This is the chain hop §56 recorded on 8.4.4.
- **`H x y`, `x > 0`** hops **inside** the phrase to step `y`, one hop a pass, `x` passes, and
  then lets the phrase run through. A hop to the `H`'s own step is a no-op that still spends a
  pass. A hop to a step past the phrase's sixteen ends it, which is why `H 1 F` reads as a chain
  hop taken once.
- **`H F F`** is the exception and still unexplained: it **stops the channel**. Re-measured over
  thirty-five seconds -- two note-ons and then nothing at all, where `H 2 F` on the same phrase
  hops to step 15 twice and runs on, and `H F 0` loops happily sixteen times.

ChipBoy expresses the first form with `y = 0`: the importer sets the phrase's **length** to the
`H`'s step, so the phrase ends there and the chain moves on. `y > 0` -- where the *next* phrase
starts -- still has nowhere to go and is noted at import.

**The counted form is §102's now.** This entry said it was engine work "because the Player lays a
chain row's steps out ahead of the row rather than interpreting them one at a time -- a hop whose
count survives across passes has no place in a schedule that is built once". The count is fixed, so
the order is fixed, so it does: §102 is the schedule, and the importer hands the hop through as the
cell's own command.

## 81. An imported noise instrument plays LSDj's own note map

ChipBoy builds its noise map by picking, for each note, the `(shift, divisor)` pair whose LFSR
clock is nearest that note's frequency. LSDj has a **table** instead, measured per version and
already carried by `LsdjModel::noiseMap`. The two are close but not the same, and the importer
was crossing between them twice: LSDj note → LSDj's `NR43` → the nearest ChipBoy note → back to
a `(shift, divisor)` pair by ChipBoy's own rule. On SUNRISE that round trip turns the ROM's
`NR43 = 20` into `15` — two octaves and a bit of LFSR clock, which on a drum is not a nuance.

**The fix is to stop crossing.** The bank carries LSDj's map and a noise instrument can say it
wants it:

- `Bank::noiseMap`, 128 bytes, and `Bank::noiseMapSet`. One map per bank, because an import
  comes from one version; it is written by the importer and by nothing else.
- `Instrument::noiseLsdjMap`, false by default. When it is set and the bank has a map, the
  driver reads `NR43` straight out of `bank.noiseMap[note]` rather than from `noisePairForNote`.
  Everything that moves a noise note -- a chain or table transpose, `S`'s semitones, a chord, a
  vibrato -- still moves the *note*, so it lands on another entry of LSDj's own table exactly as
  it does on the ROM.
- The importer then writes the **LSDj note itself** into the cell (`note byte + 35`), fills
  `bank.noiseMap` from the model, sets the flag on every noise instrument, and skips
  `noteForNr43` altogether. `chooseNoiseOffsets`, which existed to squeeze LSDj's clocks onto
  ChipBoy's keyboard, is not needed for a mapped instrument.

An instrument the user builds by hand is unaffected: the flag is off and the map is ChipBoy's.
A song imported before this reads with the flag off too, and sounds as it did.

The instrument's own **Shift** offset still applies on top, as it did, so a kit-style noise
instrument that shifts the whole map keeps working.

## 82. The noise channel triggers when a note turns the 7-bit LFSR on

Measured on 9.3.9 playing the user's own SUNRISE, over every `NR43` write in twenty-seven
seconds, sorted by what the width bit (`NR43` bit 3) did and whether a trigger followed:

| width bit | trigger | no trigger |
|---|---|---|
| 0 → 1 | **15** | 0 |
| 1 → 0 | 0 | **15** |
| unchanged | 55 (the note-ons) | 18 |

So when a table's transpose, an `S` or any other move lands the note on a map entry that turns
the **7-bit** LFSR on where it was off, LSDj **triggers the channel**; going back to 15-bit does
not. The asymmetry is what a real driver would do: the short LFSR is a different sound rather
than a pitch, and it only takes effect on a trigger.

ChipBoy wrote the width bit and carried on, which cost SUNRISE fifteen of its seventy noise hits
-- every one of them a snare's click. `writePeriod`'s noise branch now triggers on the rising
edge, mid-note only: a note-on has its own trigger and must not get a second.

## 83. LSDj's noise table is 120 entries and its index **wraps**

§81 gave an imported noise instrument LSDj's own table; §82 found the trigger that comes with a
7-bit entry. Both were right and both were reading a **truncated** table.

Swept whole on 9.3.9 -- every note byte 1 to 120 played on a noise instrument, `NR43` read at
each trigger -- the table is **120 entries**: bytes 1-60 are the 15-bit half, from `D7` down to
`00`, and bytes 61-120 the 7-bit half, from `DF` down to `08`. Byte 121 and up read past the end
as junk, which is why a phrase cannot usefully name one.

**The index wraps modulo 120.** Measured with a table transpose on a note whose byte is 58
(`NR43 = 20`, index 57), against `table[(57 + tsp) mod 120]`:

| `TSP` | −10 | −58 | −60 | −70 | −120 | +20 | +62 | +80 | +100 |
|---|---|---|---|---|---|---|---|---|---|
| `NR43` | `50` | `08` | `28` | `58` | `20` | `AB` | `08` | `A3` | `53` |
| wrapped | `50` | `08` | `28` | `58` | `20` | `AB` | `08` | `A3` | `53` |
| clamped | `50` | `D7` | `D7` | `D7` | `D7` | `AB` | `08` | `08` | `08` |

Every one is the wrap, and none is the clamp. `SUNRISE`'s two unexplained bytes fall straight
out: a transpose of −58 from index 57 lands on index 119, which is `08`.

**What ChipBoy has to change.** A cell's note is 0-127, and LSDj's table spans MIDI 36-155, so
it cannot be carried as a MIDI note. It does not need to be: for a mapped noise instrument the
note is an **index into that table**, not a pitch, so the importer carries **LSDj's own note
byte** -- entry 0 at note 1, the whole 120-entry table at notes 1-120.

- `Bank::noiseMap` keeps its 128 bytes; `Bank::noiseMapLen` says how many are the table (120 on
  9.x) and `Bank::noiseMapNote0` which ChipBoy note entry 0 plays (1). A version whose table is
  a different length needs no new field.
- The driver wraps: `idx = ((note + transposes) - noiseMapNote0) mod noiseMapLen`, the modulo
  taken so a negative walks to the top. It **replaces** the clamp into `-kNoiseMapBelow..127`
  for a mapped instrument, because a clamp is exactly the wrong answer here.
- The importer emits the note byte unchanged and drops the octave folding, which only existed
  because the table it knew about had holes at both ends.

> **Superseded.** The first version of this section re-based the table to note 8 (`n + 7`), to
> leave room underneath for a transpose to walk. The wrap makes that room unnecessary, and the
> offset put ChipBoy's Note column seven out from LSDj's phrase screen, which is the one place a
> reader checks an import against the original. Note 1 makes the two read the same (§85). The
> cost is that the table's first eleven entries would fall in ChipBoy's command octave (notes
> 0-11, §13), so a bank that carries LSDj's map has **no command octave on the noise channel**:
> there a note is an entry number, not a pitch, and there is no octave below it to spare.

## 84. A note-on triggers at the plain note; the table's transpose follows one update later

Measured on 9.3.9 with a table whose row 0 transposes +12, note `18` (period 1517, +12 = 1783):

| channel | what the ROM writes |
|---|---|
| PU1, row 0 = +12 | `1517` **with the trigger**, then `1783` 1.2 ms later, then `1517` |
| PU1, row 1 = +12 | `1517` with the trigger, `1517`, then `1783` |
| WAV, row 0 = +12 | trigger at a stale `2016`, `1517` in the same burst, then `1783` 1.2 ms later |

So **row 0 of a table fires with the note-on** -- §31 stands, and its commands and its volume are
the note's -- but its **transpose column reaches the channel on the next pitch update**, not in
the note's own writes. ChipBoy folded it in, which puts the channel one table row ahead for the
first instant of every note that starts a transposing table.

It costs more than an instant when the table is a *sweep*: the wave kick's first period came out
two updates into the sweep rather than at its start, so the whole drum began lower than the ROM's.

**The change.** `writePeriod` takes the transpose column out of the period it writes **when that
write carries the trigger and the table started with this note** (`Voice::tableJustStarted`) --
narrow enough that a retrigger mid-table, where the column genuinely applies, is untouched. The
update that follows picks the column up, because a note-on already asks for one
(`Voice::pitchWrite`).

The noise channel has no pitch clock unless a vibrato is running (§77), so it would never take
that following update and row 0's transpose would be lost rather than late. It joins the clock
whenever a **table** is running as well, and `writePeriod` then skips a noise write whose `NR43`
is the one already out -- LSDj writes the register when the value changes, and a forced repeat
would be a write the ROM does not make.

## 85. The noise channel's note reads as a number, not a note name

§83 made an imported noise instrument's note an **index into LSDj's table** rather than a pitch:
the channel has a clock, not a frequency, and the map's entries are not a twelve-tone scale.
Drawing that index as `C#4` invites exactly the confusion it caused -- a note name that does not
name the note, beside a transpose column whose semitones are really table steps.

**On the noise channel the Note column shows the entry number**, in the same base the rest of the
grid uses (§52). LSDj's own phrase screen counts that number **from zero** -- a phrase holding
note bytes `01`..`10` prints `00`..`0F`, measured by walking 9.3.9 to the phrase screen and
reading the LCD -- so ChipBoy prints `note - 1`: a cell holding LSDj's note byte `3A` reads `39`,
exactly as it reads in LSDj. `OFF` and the blank stay as they are, because they mean the same
thing on every channel. The entry box takes the same number back, and still takes a note name,
so a keyboard-minded edit is not refused; on the other three channels nothing changes.

This is the display only. Nothing in the song file, the bank or the driver moves: a cell's note
is the same byte it always was -- and after §83's re-base to note 1, that byte **is** LSDj's.

## 86. The noise channel's `PITCH`: which pitch change restarts it

LSDj 9.2.H revived the old `S MODE` setting under the name `PITCH`, with two values. Measured on
9.3.9 by walking a table's transpose across the table's width boundary and counting `NR44`
triggers, and located by sweeping the noise instrument's bytes one at a time:

**Instrument byte 2 is `PITCH`: zero is `FREE`, anything else is `SAFE`.** (`SUNRISE`'s own kick
stores `04`, so the byte is not a flag LSDj keeps at 1.)

| | a pitch change that turns the 7-bit LFSR **on** | one that turns it **off** | any other |
|---|---|---|---|
| `FREE` | restarts | no | no |
| `SAFE` | restarts | restarts | restarts |

`FREE` is what §82 measured, and it is the default. `SAFE` is the setting that stops a DMG muting
itself on a noise pitch change, at the price of a retrigger on every one of them.

**A restart is not a note-on.** The ROM writes, in this order: the new `NR43`, then
`NRx2 = (current level << 4) | 8` -- a hold at the level the note has *reached*, not the level the
instrument starts at -- then `NR44 = BF`. Re-arming `NRx2` is what keeps the envelope going: a
trigger reloads the chip's volume from `NRx2`, so without it every restart would throw the note
back to full and the software envelope would claw its way down again. That was audible on an
imported `SUNRISE` as a noise part that kept jumping back up.

`Instrument::noisePitchSafe` carries the setting; the importer reads it from byte 2 for format
22 (`LsdjModel::noisePitchByte`), and leaves it clear for every older model until that version's
ROM is measured.

## 87. LSDj's `LENGTH` is latent: the note-on never enables the counter

Measured on 9.3.9 with a noise instrument whose byte 3 runs `00`, `01`, `20`, `3F`: the byte goes
straight into `NR41`, and the note-on always writes `NR44 = 80`. **Bit 6 -- the length enable --
is clear.** So `LENGTH` does nothing at all by itself; the value sits in the counter's reload
register until something turns the counter on, and the only thing that does is §86's pitch
restart (`NR44 = BF`). An instrument with `LENGTH = 3F` and `PITCH = SAFE` is therefore a click:
the first pitch change after the attack cuts it one 256th of a second later.

The pulse channel stores its `LENGTH` in byte 3 too, written into `NR11`'s low six bits, but LSDj
rewrites `NR11` with the duty alone an instruction later, so the counter ends up reloaded to 64
either way and ChipBoy's `0` reaches the same place. Only the noise channel keeps the value.

`InstrumentCore::lengthLatent` says an instrument carries its length this way: `NRx1` gets the
value, `NRx4`'s enable bit stays clear, and only a §86 restart turns the counter on. The importer
sets it with `length = 64 - byte 3` on every noise instrument whose byte 3 is not zero.

## 88. `P` before 5.7.8 moves the period register by whole units

§56 found that before format 4 `P xx` adds `xx` **period-register units** a pitch clock rather
than bending by semitones, and the importer squeezed that into the nearest of ChipBoy's Drum
speeds. Measured against 3.6.8 playing the user's `CLUCK`, that is not close enough:

```
ROM       060A 05FA 05EA 05DA 05CA 05BA 05AA 059A 058A     exactly -16 a clock
ChipBoy   060B 05FB 05EC 05DC 05CC 05BD 05AD 059D          -16, -15, -16, -16, -15 ...
```

The nearest Drum speed to sixteen units is about 15.7, so a slide drifts a unit every three
clocks and a long one ends a semitone away from where the ROM's ends. Every note after it
inherits the error.

**`InstrumentCore::pitchRegisterUnits`**: when set, `P`'s signed byte is the number of units a
clock, whole, and the driver adds it straight to `drumOffset` -- which is already a
period-register offset (§7), so nothing else in the pitch path changes. The importer sets it on
every pulse and wave instrument of a `PitchLaw::Register` model and passes `P`'s byte through
untouched, which also removes the conversion that produced the drift.

With it, ChipBoy's slide is `060B 05FB 05EB 05DB 05CB 05BB 05AB 059B 058B` -- the ROM's step for
step, one unit above it because the note-on period rounds the other way, which is a thirtieth of
a semitone.

## 89. A wave instrument has no frame run before format 7

Measured on every release: a wave instrument walks a **run** of its synth's frames while a note
sounds only from 6.8.2 (format 7). Before that it loads **frame 0** and holds it, whatever the
low nibble of its synth byte says, and bytes 9, 10 and 11 -- which 9.x reads as PLAY, LENGTH and
SPEED -- mean something else.

| | note byte `30` | `31` | `35` | `3F` |
|---|---|---|---|---|
| 3.6.8 - 6.4.5 (formats 0-5) | frame 0 | frame 0 | frame 0 | frame 0 |
| 6.8.2 - 7.0.2 (format 7) | frames 0 1 2 3 4 5 | 0 1 2 3 4 5 | 0 1 2 3 4 5 | 0 1 2 3 4 5 |

Reading 9.x's bytes on an older instrument gives it a run it never had, which is heard as the
wave channel retriggering two or three times a step. On `CLUCK` it was 978 wave note-ons against
the ROM's 303; with `LsdjModel::waveFrameRun` clear for formats 0-5 it is 270.

## 90. `R 8 y` is a fast retrigger every `y + 1` pitch clocks, and `R 8 F` stops one

§76 called `x = 8` "LSDj's resync" and left `y` out of it, so ChipBoy retriggered on **every**
pitch clock whatever `y` said. Measured on 9.3.9 and 9.2.L, a note held while `R 8 y` runs:

| `y` | 0 | 1 | 2 | 3 | 4 | 8 | E | F |
|---|---|---|---|---|---|---|---|---|
| gap | 2.8 ms | 5.6 | 8.4 | 11.2 | 14.0 | 25.1 | 41.9 | — |
| pitch clocks | 1 | 2 | 3 | 4 | 5 | 9 | 15 | none |

**The interval is `y + 1` pitch clocks**, exactly, for `y` = 0 to E. **`R 8 F` retriggers nothing
at all** -- and it *stops a retrigger that is already running*, which `R 0 F` and `R 0 0` do not:
a table with `R 8 1` on one row and `R 8 F` two rows later rolls and then stops, where without the
second row it rolls on. That is how the user's `SAMESONG` uses it, in both of its tables.

`x` is still a signed nibble of volume change in the tick domain (`R 1 1` walks the level up by
one a retrigger, `R 9 1` down by seven), and `x = 8` alone means the fast domain with no volume
change. The driver keeps `retrigFastCount` beside `retrigCount` and `R 8 F` clears `retrigOn`.

On `SAMESONG` this took the noise channel from 5191 retriggers against the ROM's 1678 to 1708.

## 91. The wave instrument's `SPEED` is a **signed** byte

The run advances every `speed + 4` ticks and `speed` is instrument byte 11 read as a **signed**
byte, measured on 9.3.9 by counting wave-RAM refreshes over three seconds:

| byte 11 | `00` | `01` | `02` | `03` | `04` | `FF` | `FE` | `FD` |
|---|---|---|---|---|---|---|---|---|
| signed | 0 | 1 | 2 | 3 | 4 | −1 | −2 | −3 |
| ticks a frame | 4.00 | 4.99 | 6.00 | 6.99 | 7.99 | 3.00 | 2.00 | 1.00 |

The importer read it unsigned, so `FD` asked for 257 ticks a frame rather than one and the run
stood still. **Every wave instrument in `SAMESONG` stores a negative speed**, which is why its
wave channel triggered 745 times against the ROM's 1596.

The run's *content* was already right: `LENGTH` picks N frames spread evenly over the synth's
sixteen -- `LENGTH = 4` is frames 0, 5, 10, 15 and `LENGTH = 8` is 0, 2, 4, 6, 9, 11, 13, 15 --
which is what `bank::waveRun` builds, and `PLAY` 0/1/2/3 are MANUAL, ONCE, LOOP and PINGPONG as
the importer already reads them.

## 92. `F` on the wave channel **advances** the frame; it does not name one

§65 had `F` naming the frame it loads, from a reading of 8.4.4 where "`F 06` loaded frame 5".
Re-measured on 8.4.4, 8.8.6, 9.2.L and 9.3.9 by tagging each of a synth's sixteen frames and
reading the wave RAM back, with the command on a table row so it runs every tick:

| `F 0 y` | `01` | `02` | `04` | `06` | `02` then `01` |
|---|---|---|---|---|---|
| frames loaded | 0 1 2 | 0 2 4 | 0 4 8 | 0 6 12 | 0 2 3 5 6 |

**The frame advances by the command's argument every time it runs**, not through the instrument's
run -- so `F 02` on a run of 0, 5, 10, 15 still reaches frame 2. Identical on every version
measured; the old reading was off by one and mistook a single step for an absolute index.

§100 corrects two things said here: the argument is the **whole byte**, not `y`, and the index
does not wrap at the synth's sixteen frames.

A table whose rows carry `F` is how LSDj walks a synth: the user's `SAMESONG` drives every one of
its wave instruments that way, and its `GUITR` is `PLAY = MANUAL` with nothing but `F` rows.

### 92.1 A table's commands are read for the instrument that runs it

The importer converted a **table's** commands with no instrument kind at all, so every one that
reads the kind fell through to the channel-less branch: on the wave channel `F` was **dropped**,
with a note about the noise channel that was not even true. `tables()` now takes the kind from the
instruments that name the table (`tableUse`), and passes it with a representative channel; a table
shared between kinds keeps the old behaviour, because it cannot have both readings.

On `SAMESONG` this is the difference between 47 wave note-ons in the first ten seconds and the
ROM's 197 -- which ChipBoy now matches exactly.

## 93. `REPEAT` is a byte of its own, and it is not the synth byte

§65 read the wave instrument's run loop point out of the **low nibble of the synth byte** -- byte 2
on the formats it was measured on, byte 3 from 9.x, whichever `waveByte` names. That is wrong on
both edges, because *both* bytes carry the synth number in their high nibble and LSDj keeps them in
step, so a wrong choice reads a plausible synth and a loop point of zero.

Measured on every ROM from 6.8.2 up, with the two bytes given different synths and their frames
tagged so the wave RAM names the frame that is loaded:

| formats | 7-8 | 9-15 | 17-22 |
|---|---|---|---|
| synth number | byte 2, high nibble | byte 2, high nibble | byte 3, high nibble |
| `REPEAT` | byte 3, low nibble | byte 2, low nibble | byte 2, low nibble |

The nibble's meaning is §65's and unchanged -- the loop covers the last `16 - REPEAT` steps of the
run -- and `REPEAT = F` is the case that matters: the loop is then the run's **last step alone**, so
the run plays through once and the frame never changes again. Measured on 9.2.L with `PLAY = LOOP`
and the frames tagged:

| `LENGTH` | `REPEAT` | frames loaded |
|---|---|---|
| 4 | 0 | 0 5 10 15 0 5 10 15 ... |
| 4 | E | 0 5 10 15 10 15 10 15 ... |
| 4 | F | 0 5 10 15 |
| 8 | C | 0 2 4 6 9 11 13 15 9 11 13 15 ... |
| 8 | F | 0 2 4 6 9 11 13 15 |

**Every wave instrument in the user's `SAMESONG` stores `REPEAT = F`**, so every wave run there is
a one-shot; read off byte 3 it came through as 0 and each run looped for as long as the note held.
With §91's one-tick `SPEED` that is a trigger a tick: 2866 wave note-ons against the ROM's 1596.

The split at format 9 means LSDj 6.8.2-7.2.3 and 7.5.4-8.0.0 no longer share a model: the format
byte tells them apart (7-8 against 9-10), so `kLsdj68` keeps formats 7-8 and `kLsdj75` takes 9-10.

## 94. The tick a note starts on belongs to the first frame

The frame run advances every `SPEED + 4` ticks (§91) counted from the note-on -- and the tick the
note-on happens in is the **first frame's own tick**, not a tick the counter has already spent.
Measured on 9.2.L with `SPEED = FD` (a tick a frame): the note-on loads frame 0 and the *next* tick
loads the second frame; with `SPEED = 00` the second frame comes four ticks later, not three.

ChipBoy fired the note-on inside the tick and then ran the tick's own counter, so a one-tick run
loaded two frames in the same tick -- two triggers a hundred microseconds apart, 268 of them over
`SAMESONG`. The voice now carries `frameFresh`, set by the note-on and spent by that tick.

## 95. A table's `H` costs no tick

`H` inside a table hops the lane (§34, §64), and ChipBoy spent a tick on the row that carried it:
the row hopped to played on the *next* tick. LSDj plays it **now**. Measured on 3.6.5, 8.4.4,
9.2.L and 9.3.9 with a four row arpeggio -- transposes `FD 00 05 09` on rows 0-3 and `H 00` on
row 4 -- reading the period register:

```
0.026:1798  0.045:1837  0.065:1890  0.084:1923  0.104:1798  0.123:1837 ...
```

Four values, 19.4 ms apart, and the cycle closes in **four** ticks on every version. ChipBoy's
took five, the extra one being row 4 itself, so every arpeggio in a real song ran a fifth slow and
against the beat -- on the user's `SAMESONG` the pulse channels agreed with the ROM 47% and 30% of
the time before this and 85% and 94% after.

The lane now re-reads at the row it lands on, inside the same tick, with the volume lane's guard
against a ring of hops that never reaches a row to play. A hop whose count is spent (`H x y` with
`x` hops taken) leaves the step alone and the lane moves on to the next row, which it did not
before.

## 96. A kit instrument has one `LENGTH`, in byte 11, and a `LOOP` bit in byte 5

The importer read byte 3 as kit A's length and byte 11 as kit B's. Measured on 9.2.L by counting
the wave RAM refills a kit note makes -- LSDj rewrites the sixteen bytes once a wave cycle, so the
count is the sample's length in 32 nibble frames -- with one kit pointed at a slot the ROM does not
have so only the other sounds:

| bytes set | frames played |
|---|---|
| nothing | 56 (the whole sample) |
| byte 3 = 04, either kit or both | 56 |
| byte 11 = 04 | 4 |
| byte 11 = 08 | 8 |
| byte 13 = 08 | 48 |

**Byte 11 is the instrument's one LENGTH** and cuts both kits; byte 3 does nothing a note can hear,
and byte 13 is an offset into the sample (already noted at import, still not mapped).

Byte 5 carries **LOOP** in bit 5: with `20` set, a four frame sample kept refilling for the whole
1.6 s traced rather than stopping after four. It is `KitLoop::Loop` now, which repeats the sample --
cut to LENGTH -- until the note ends.

The user's `CASTSHDW` is the case: its `DSAMP` stores `LENGTH = 02` with `LOOP` on, and ChipBoy
read the length off byte 3 as three frames without the loop, so every kit note stopped after a
tenth of the sound the ROM makes -- 2395 wave writes against the ROM's 4264.

## 97. A kit reads `PITCH` from byte 5, and its `P` works in period-register units

ChipBoy held that "a kit's period is its sample rate, read by the streaming timer, so it is never
bent between ticks", and gave a kit no `PITCH` setting of its own. Both are wrong. Measured on
9.2.L with a looping kit note and one `P` in the cell, reading the period register (base 1865):

| byte 5 | `P 02` | `P F0` |
|---|---|---|
| `00` FAST | 1867, 1869, 1871, 1873 ... (+2 a pitch clock) | 1849, 1833, 1817 ... (−16 a clock) |
| `40` DRUM | the same as FAST | the same as FAST |
| `10` TICK | 1867, 1869, 1871 ... (+2 a **tick**) | −16 a tick |
| `80` STEP | 1871, and no more | 1817, and no more |

So a kit takes `PITCH` out of byte 5 exactly as a pulse or wave instrument does (§34's
`pitchSpeedOf`), and `P`'s byte is a number of **period-register units**, not semitones: one unit a
pitch clock under FAST and DRUM, one a tick under TICK -- not §88's four -- and **three times the
byte, once**, under STEP.

The user's `CASTSHDW` drives its kit drums this way: `DSAMP` is `PITCH = STEP` and every phrase row
carries a different `P`, which is what tunes each hit. ChipBoy played all of them at the
instrument's `SPEED` and nothing else; with this the kit notes it does play land on the ROM's
period 77% of the time against 43%.

## 98. The ROM beside the save decides the model, and old ROMs have to be found first

§54 gives the importer a ROM beside the save so a song can be read the way the version that wrote
it read it -- §4 of `docs/LSDJ_VERSIONS.md` is two format bytes that mean two different things, and
only the ROM tells them apart. Three faults meant that never happened for anything older than 4.3:

1. **`autoModel` asked the format first.** `lsdjModelForFormat(formatVersion)` always answers for a
   known format, so `lsdjModelForRomVersion` was reached only for a format no model claims -- the
   version-keyed table was dead code on the path that matters. The ROM's own version now wins
   whenever its model reads the song's format, and the format's default is the fallback.
2. **A pre-4.3 ROM has no version in its header.** The cartridge title is `LSDJ` with no number;
   the version sits in the welcome line, `WELCOME TO LITTLE SOUND DJ V3.6.5!`, inside bank 0.
   `romVersion()` knows that, but the caller handed it the first **0x150 bytes** of the file, so
   every ROM before 4.3 read as "not LSDj": no version, no kits, and the format's default model.
   It reads a whole bank now.
3. **3.6.5 was in neither table.** It writes format 2 -- measured, and its `V` is already the
   centred vibrato of 3.6.8, not 3.5.1's one-sided one -- so it sat in the gap between the
   3.1.5-3.5.1 model and the 3.6.8 one and took the older. `kLsdj36` starts at 3.6.5 now.

Together these three read every *Computer Savvy* song (format 2, written in 3.6.5) under the
**4.0.4** model with no kit ROM at all: a blank instrument column silently dropped every note that
used one, and every kit instrument was skipped.

## 99. `L` and `P` replace one another, and in Drum a slide runs in period units

Two measured corrections, both from `SAMESONG`'s `WKICK` -- the kick the user heard machine-gunning.
Its table is `P A0` on row 0, `TSP 80` with `L 30` on row 1, and `K 00` on row 6.

**A slide replaces a running bend and a bend replaces a running slide.** LSDj has one pitch
mechanism and the later command owns it. Measured on 9.2.L, the period register after each command:

| table | first tick | after |
|---|---|---|
| `P A0` alone | −89.5 units a pitch clock | −89.5, for ever |
| `P A0` then `L 30` | −89.5 | the slide's own rate, the bend gone |
| `TSP 80` + `L 30` then `P A0` | the slide's −40 | −89.5, the slide gone |

ChipBoy ran both at once, so the kick's sweep kept the bend on top of the slide, fell off the
bottom of the register and **wrapped at 2048** -- a second kick a few milliseconds later, and
another, which is the machine gun. `Cmd::L` clears `bendSpeed` now; `Cmd::P` folds a running
slide's offset into the channel's so the pitch carries on from where it had reached, and drops it.

**In Drum the slide is linear in the period register, not in semitones.** Drum's whole pitch is the
register (§7, §88) and so is its slide. Measured on 9.2.L with a table transpose of −24 semitones,
94 register units down from period 2017:

| `L vv` | `04` | `08` | `10` | `20` | `30` | `40` | `60` |
|---|---|---|---|---|---|---|---|
| updates to land | 5 | 9 | 17 | 33 | 49 | 65 | 97 |
| register step | −18.8 | −10.4 | −5.5 | −2.85 | −1.92 | −1.45 | −0.97 |

`vv + 1` pitch updates, the register walking in a straight line with the fractional part carried,
landing exactly on the transposed note (or on the bottom of the range when the transpose names
something unreachable). The same table on a **Fast** pulse gives −56, −74, −97, −129: that one is
linear in **semitones**, which is what §71 measured and what ChipBoy already does. So the law is
per pitch mode, and only Drum changes.

`Voice::drumSlideStep` / `drumSlideLeft` carry it, on `drumOffset` beside `P`'s own bend, and
`drumSlideHold` keeps the table's transpose column out of the note once it has been folded into
that offset -- until the next note-on, because the column *is* the aim. A Drum instrument whose
table carries an `L` and then a transpose on a later row would lose that later one; none of the
user's songs has one, and it is noted here rather than guessed at. On the user's `SAMESONG` the kick's sweep is now the ROM's within a unit or two for
its whole length instead of wrapping round three times.

## 100. `F` on the wave channel takes the whole byte, and walks the **flat** wave table

§92 read the argument as `y` and said the index wrapped at the synth's sixteen frames. Both are
wrong. Measured on 9.2.L with synths 1, 2 and 3 tagged so the wave RAM says which synth a frame
came from, an instrument on synth 1 and `F` on every table row:

| `F` | frames loaded |
|---|---|
| `01` | s1/0 s1/1 … s1/15 **s2/0** |
| `04` | s1/0 s1/4 s1/8 s1/12 **s2/0** s2/4 s2/8 s2/12 **s3/0** … |
| `08` | s1/0 s1/8 **s2/0** s2/8 **s3/0** s3/8 … |
| `10` | s1/0 **s2/0** **s3/0** … |
| `11` | s1/0 **s2/1** **s3/2** … |
| `1E` | s1/0 **s2/14** … |

So the argument is `x * 16 + y`, and LSDj's wave RAM is one **256-frame table**: sixteen synths of
sixteen frames laid end to end, which `F` walks straight through.

That matters because of `Z`. `SAMESONG`'s `SLAPB` runs a table with `F 01` on one row and `Z 1E` on
the next, and §74's randomisation is per **nibble** -- `0..x` on the high one and `0..y` on the low
-- so the advance is a random 0 to 31 frames and lands in the next synth about half the time. The
importer kept only the low nibble, so ChipBoy's advance was 1 or 2 where the ROM's was 7 to 24.
Both nibbles go through now and the driver reads them as one number, which puts `Z`'s range right.

**The flat table is modelled in §103.** It was not when this section was written: a ChipBoy wave
was one to sixteen frames and the index wrapped inside its own slot, so an advance past the synth's
sixteen landed on a frame of the same synth where LSDj sounded the next synth's, and the import
said so per command. §103 makes the bank sixteen slots of sixteen frames with the jump on the flat
index, and the importer reads all sixteen synths in order, so the advance now lands where the
ROM's does and the warning is gone.

## 101. A blank instrument column is a bare note, not an empty cell

`docs/LSDJ_VERSIONS.md` §4 had "a note whose instrument column is blank sounds on 3.6.8-3.9.2 and
nothing at all from 4.0.4", and the importer dropped the whole cell for every format from 2 on --
the note **and** the command beside it. Measured again with a note sounding first, which the
original probe did not have:

| version | a cell with a note and a blank instrument column |
|---|---|
| 3.6.5, 4.0.4 (the row alone) | 3.6.5 **triggers** at the new note; 4.0.4 moves the period to it with **no trigger** |
| with `L 10` on the same row | both slide to that note; 3.6.5 triggers first, 4.0.4 does not |
| the row with no note at all | nothing on 9.2.L; on 3.6.5 and 4.0.4 the `L` slides in register units (§88) |

So from 4.0.4 the cell is exactly **ChipBoy's own bare note** -- it moves the channel's pitch and
does not trigger -- and before 4.0.4 it triggers with the channel's last instrument. The importer
keeps the note either way now: blank from 4.0.4, the column filled in before it.

The user found this in `SAMESONG`'s phrase 1C, where step 9 is `D#4` with no instrument and `L 10`
beside it: a bend up to D#4 that ChipBoy played as nothing at all, because both the note and the
`L` went with the dropped cell. Twenty cells in that song are of this kind.

## 102. A phrase's `H` loops inside the phrase, and the groove walks with it

§80 measured what `H x y` does and then said the counted form was engine work, "because the Player
lays a chain row's steps out ahead of the row rather than interpreting them one at a time -- a hop
whose count survives across passes has no place in a schedule that is built once". That was the
wrong conclusion: the count is **fixed**, so the order a phrase plays is fixed too, and a schedule
built once can hold it. This section is that schedule.

Measured on 9.2.L with a six-row phrase whose notes rise a semitone a row, so the order reads off
the pitch:

| the `H` | the steps that play |
|---|---|
| none | 0 1 2 3 4 5 |
| `H 1 0` on row 2 | 0 1 **0 1** 2 3 4 5 |
| `H 2 0` on row 2 | 0 1 **0 1 0 1** 2 3 4 5 |
| `H 1 2` on row 5 | 0 1 2 3 4 **2 3 4** 5 |
| `H 0 0` on row 2 | 0 1 0 1 0 1 ... (§80's chain hop; one phrase in the chain cannot tell it from a loop) |

Two things that were not in §80:

- **The step carrying the `H` does not play on a hopping pass.** `H 1 0` on row 2 gives `0 1 0 1 2`,
  not `0 1 2 0 1 2`: the hop is taken before the row sounds. It plays once the count is spent, which
  is why step 2 is there at the end.
- **The groove entry comes from the position in the play order, not from the step's own index.**
  With the 7/5 swing and `H 1 0` on row 3 the gaps are `137 98 137 98 137 98 137 98` ms; by the
  step's own index the fourth would be 137 again. The groove walks with the playing.

### 102.1 What ChipBoy does with it

A phrase now has a **play order**: the list of steps it plays, in the order it plays them,
`tracker::phrasePlayOrder()`. Without an `H` it is `0 .. length-1` and nothing changes anywhere. With
one it is the expansion above, capped at `kMaxPlaySteps` (256) so a hop that cannot terminate stops
being a hang. A cell's `H` with `x = 0` ends the order there, which is §80's chain hop and what the
importer already expressed as the phrase's length.

Everything downstream is indexed by **position in that order** rather than by step:
`stepStartTicks()` fills a start tick and a step number per position and returns how many there are;
`phraseTicks()` is the groove's total over the order, so the row lasts longer and the prefix table
`buildRowTables()` builds puts the following rows where they belong.

**That is what keeps the host's timeline honest.** A row is as long as its order makes it, and the
order is deterministic, so a tick still maps to exactly one (row, step) and back. A phrase of four
straight steps at 120 BPM whose row 2 carries `H 1 0` lasts six steps: the row still starts where it
did, 2.0 s into it is the second pass of step 0 rather than the next row, and the next row starts at
3.0 s. Scrubbing the DAW to 2.0 s puts the playhead on step 0 of that row, on its second pass, which
is what LSDj is doing there too.

The editor shows the phrase's cells as they are -- the order is not a thing to edit -- and the play
position lands on the step that is sounding, whichever pass it is on.

## 103. The wave bank is one flat table: sixteen slots of sixteen frames

§100 measured that LSDj's wave RAM is a single 256-frame table -- sixteen synths of sixteen
frames laid end to end -- and that `F` walks it straight through, so an advance past a synth's
last frame sounds the next synth's. It left the table unmodelled: a ChipBoy wave was one to
sixteen frames and the index wrapped inside its own slot, which is why `SAMESONG`'s accent notes
(`SLAPB`, `F 01` then `Z 1E`) came out duller than the ROM's. This closes it.

**A ChipBoy wave slot is sixteen frames, always, and there are sixteen slots.** `kWaveSlots` goes
from 64 to 16 and `Wave::frames` is `kMaxFrames` long whether or not anyone has drawn them, so the
bank is `kWaveFrames = 256` frames with no holes. A slot's frames are the flat indices
`(slot - 1) * 16 .. (slot - 1) * 16 + 15`, and the flat index is the thing a frame jump moves:

```
flat  = (slot - 1) * 16 + frame
flat' = (flat + step) mod 256          // step is F's whole byte, section 100
slot' = flat' / 16 + 1                 // past slot 16's frame 15, back to slot 1 frame 0
frame'= flat' mod 16
```

Only the **jump** walks flat. The instrument's own run -- LENGTH, LOOP POS, PLAY, SPEED (§65) --
stays inside whichever slot the voice is on, which is what §65 traced on 8.4.4: a run of eight
visits eight of *that synth's* sixteen and LOOP returns within them. So a wave voice carries two
things: the slot it is on, which `W` sets and `F` can move, and the run step inside it.

`Z` needs no rule of its own. §74's randomisation is per nibble and §100 reads both nibbles as one
number, so `Z 1E` after `F 01` is a random advance of 0 to 31 flat frames -- past the slot's
sixteen about half the time, into the next slot's, exactly as on the ROM.

**What the importer owes this.** The flat table is only faithful if the slot next door holds what
the ROM had there, so the importer no longer allocates slots lazily in the order synths are
referenced. It reads **all sixteen synths in order**, LSDj synth `k` into ChipBoy wave slot
`k + 1`, from the save's own wave RAM at `0x6000`. An unreferenced synth costs 512 bytes and buys
a frame jump that lands where the ROM's would.

**What this costs the editor.** A wave no longer has a frame count, so the frame strip's `+` and
`-` and the click-past-the-end that grew a wave are gone; the strip is the sixteen frames and the
list reads each slot's flat range (`0-15`, `16-31`, ...) instead of `n fr`. The synth keeps its
**From** and **To** (`Synth::first`, `Synth::frames`, §36) -- unlike LSDj, where the synth owns
the whole synth, ChipBoy generates into a chosen part of the sixteen and leaves the rest alone.

**Sixteen for now.** LSDj has sixteen synths and a save holds sixteen, so sixteen slots import
exactly. Further blocks of sixteen -- a second 256-frame table a song can reach -- are a later
change; nothing here assumes 16 except `kWaveSlots`, and the arithmetic above is written against
`kMaxFrames` and `kWaveFrames`.

A wave read from a song saved before this section had between one and sixteen frames. It is padded
to sixteen with its last frame, so a slot that was one frame stays one shape and a run over the
sixteen sounds as it did; a shorter morph is held at its end rather than stretched, because
stretching would change frames the song's `F` commands name by number.

## 104. LSDj's WAVE screen draws a frame upside down; the bytes are the same

The user reported that a frame in ChipBoy's wave grid looks vertically mirrored against the same
frame on LSDj's WAVE screen, with the last point wrapped to the front, and that it looks that way
for every frame. It is a drawing convention, not the data.

`READROOM`'s synth 2 frame 0 -- LSDj's `WAVE 20` -- is stored in the save at `0x6000 + 0x20 * 16`
as `8F FF FF FC FE A9 AA 88 75 46 41 32 00 00 00 00`, so its samples run
`8 F F F F F F C F E A 9 A A 8 8 7 5 4 6 4 1 3 2 0 0 0 0 0 0 0 0`: one middle sample, a plateau at
the **top**, a staircase down, a plateau at the **bottom**. That is ChipBoy's picture of it. LSDj
draws the mirror of that -- bottom plateau first, rising to a top plateau.

The ROM settles which is the wave. Over 80 s of `READROOM`, `lsdjref_trace` caught **850** distinct
sixteen-byte loads into `FF30-FF3F`. **35** of them are a stored synth frame, byte for byte, `WAVE
20`'s among them. **None** is the vertical inverse of one. (The other 815 are LSDj generating synth
frames as it plays, which is what a synth with live parameters does.) So the bytes the APU sounds
are the bytes in the save, and ChipBoy holds those bytes: the import is right and the sound is the
ROM's.

Why LSDj's screen is the other way up is measured in **§106**: the DMG's DACs invert, on every
channel, so LSDj draws the analog shape while ChipBoy draws the sample value. The one-sample shift
is real too, and §106 measures that as well -- a trigger sounds sample 1 first and sample 0 a whole
cycle later, on both sides. Neither is audible on its own: a vertical mirror is a polarity flip and
a one-sample shift a thirty-second of a cycle.

Nothing in ChipBoy changes for this. Whether the **grid** should draw LSDj's way, so the two
editors can be read side by side, is a UI decision and is open.

## 105. ~~Some songs' synths are generated as they play~~ -- withdrawn: those loads are kit samples

**This section was wrong, and the user said so.** It read the wave-RAM loads on `READROOM` -- 12776
of them, 841 distinct, only 3.7% matching a frame stored in the save -- as LSDj rendering its synths
from their parameters as it played. It is not. LSDj writes a synth's frames into the wave table the
moment a synth parameter changes and never at play time, which is what the user said, and what the
loads themselves say once they are split by rate:

| song | loads | median gap when the frame **is** stored | median gap when it is not |
|---|---|---|---|
| `READROOM` | 12776 | 45.9 ms | **2.79 ms** |
| `SAMESONG` | 1563 | 39.1 ms | **2.79 ms** |

2.79 ms is 32 samples at 11468 Hz. Those loads are a **kit** streaming its next thirty-two nibbles
through channel 3, not a synth. Classifying each load by nothing but its distance from the previous
one -- under 5 ms is a kit mid-stream -- and only then asking what it holds:

| song | loads not mid-stream | of those, a stored synth frame |
|---|---|---|
| `SAMESONG` | 1373 | **99.6%** |
| `READROOM` | 673 | **88.7%** |

`READROOM`'s remaining 11% are the first load of each kit note, which arrives after a gap like a
synth frame does. So every wave-instrument load on both songs is a frame the save holds, §103's
"read all sixteen synths out of the save" is the whole story, and importing LSDj's synth parameters
buys nothing at play time. What it would buy is an **editable** synth in ChipBoy's Waves tab rather
than sixteen drawn frames, which is a convenience, not parity.

The lesson is in the method: a 16-byte write burst into `FF30-FF3F` is not by itself a synth frame,
and counting distinct ones without asking what else uses channel 3 counted a drum kit as evidence.

## 106. The DMG's DACs invert, which is why LSDj's WAVE screen is upside down

§104 settled that ChipBoy holds the bytes the APU sounds and that LSDj's WAVE screen draws their
mirror, and guessed at the reason without measuring it. The user asked for the reason to be
measured: play a frame and see how the sound is actually produced. `lsdjref_trace --wave-probe`
does that now -- it renders SameBoy's audio and records, per output sample, which of the wave
channel's thirty-two nibbles the DAC is on, the byte that pair came from, and what came out.

**The polarity.** A ramp cannot answer this: a rising ramp through a DC blocker comes out falling
whichever way the DAC runs. A square can. With synth 2 frame 0 set to `00 x8, FF x8` -- samples
0-15 the nibble 0, samples 16-31 the nibble F -- averaged over 38 complete passes:

| the nibble | SameBoy's output |
|---|---|
| `0` | **+3772** |
| `F` | **-3772** |

The DAC is **inverting**: a larger wave-RAM nibble is a *lower* output. That is LSDj's screen: it
draws the analog shape, so it is the mirror of the sample values, and it is right to be.

It is not the wave channel's own quirk. The same probe reports PU1's duty position, and a 12.5%
duty note gives -4080 while the duty bit is 1 against +4080 while it is 0 -- **the pulse inverts
too**. So it is one convention across the chip, not a relationship between channels.

~~**ChipBoy is non-inverting, on every channel alike**~~ -- **wrong, and corrected in §107.** That
read `Apu::outWave`, which returns the *digital* level the DAC is fed, and stopped there. The DAC
is in the renderer, and it already inverts: `dacValue(level) = -(level - 7.5) / 7.5`, digital 0 the
positive rail and 15 the negative one. §107 measures the rendered output and finds ChipBoy's
polarity is the reference's on both the analog path and RAW. Nothing about the audio needed
changing; what did was the picture.

**The order.** §104 also repeated the user's reading that the frame is shifted by one with the last
point wrapped to the front -- and it is, in the sound. A trigger puts the wave position at 0 and
does **not** refill the sample buffer, so the first nibble the DAC reads is the one the first
advance lands on: **sample 1**. Sample 0 is heard a whole thirty-two-sample cycle later. Measured on
both sides with a frame whose only non-zero sample is sample 0: SameBoy's position counter goes
`0 1 2 ... 31 0 1`, with the byte still `00` at index 0 and the frame's first byte from index 1; and
ChipBoy's APU, at period 0 where a sample is 4096 cycles, puts the spike at cycle 131078 -- thirty-two
samples in -- and every 131072 cycles after. **The two agree.** `Tests/ApuTests.cpp` pins both.

So LSDj's WAVE screen draws what you hear, in the order you hear it, the way the DAC puts it out.
ChipBoy's grid draws the sample values as stored. Both are right about different things, and
whether the grid should switch conventions to sit beside LSDj's is still a UI decision for the user.

## 107. ChipBoy's DAC already inverts; it is the Waves grid that was upside down

§106 measured the DMG's DACs inverting and then said ChipBoy's did not. It does. The claim came
from reading `Apu::outWave()`, which hands the DAC a digital level 0-15 and is not the DAC; the DAC
is `dacValue()` in the renderer, and it has always been `-(level - 7.5) / 7.5` with a comment
saying so. Measured this time instead of read -- the same square frame as §106, rendered, sampled
at known points in the cycle:

| | sample 2 | sample 8 | sample 14 | sample 18 | sample 24 | sample 30 |
|---|---|---|---|---|---|---|
| the nibble | `0` | `0` | `0` | `F` | `F` | `F` |
| ChipBoy, analog | +0.739 | +0.297 | +0.120 | -1.415 | -0.568 | -0.229 |
| ChipBoy, **RAW** | +0.988 | +0.952 | +0.918 | -1.080 | -1.041 | -1.004 |

Nibble `0` positive, nibble `F` negative, on both paths -- SameBoy's +3772 / -3772. RAW keeps the
polarity and only loses the coupling that makes the analog path droop across a half cycle, which is
what RAW is for. `Tests/RenderTests.cpp` pins it, and `renderScript` takes a `bypassAnalog` flag so
RAW is testable at all.

**So the audio was already right and the display was not.** ChipBoy's Waves grid drew level 15 at
the top, which is the sample value, not the output. It now draws the way the DAC puts it out and the
way LSDj's WAVE screen draws it: **level 0 at the top, level 15 at the bottom.** The Points view's
cell, the Bars view's bar (it hangs from the top now), the pointer's row, the frame strip's
thumbnails and the synth preview all follow, and `cellAt` follows so a click still lands on the row
under the pointer. The corner readout is unchanged and still names the **stored** level, 0-15: the
bits are the bits, and only the drawing turned over.

The one-sample rotation of §106 is **not** applied to the grid. It is a trigger transient -- the
first cycle of a note starts at sample 1, every cycle after it runs 0 to 31 like any other -- so
drawing it would be a phase choice, not the output, and it would make column 0 edit sample 1. The
grid stays honest about which sample is which. If a pixel-exact LSDj view is wanted later it is one
index rotation in the same three places.

## 108. A table's volume column on the wave channel is the NR32 level, `amplitude & 3`

`SAMESONG`'s phrase 24 runs table `0F` on a wave note. The table's volume column is `13 23 31` --
amplitudes 1, 2, 3 -- and it swells on the ROM while ChipBoy plays it silent.

Measured on 9.2.L by putting every amplitude `0`-`F` on its own table row at one tick and reading
`NR32`:

| amplitude | `NR32` | bits 6-5 | level |
|---|---|---|---|
| 0, 4, 8, C | `00` | 00 | **mute** |
| 1, 5, 9, D | `E0` | 11 | **25 %** |
| 2, 6, A, E | `C0` | 10 | **50 %** |
| 3, 7, B, F | `A0` | 01 | **100 %** |

So the column's amplitude selects the wave level by `amplitude & 3`, and the order is ChipBoy's own
`Instrument::waveLevel` numbering -- 0 mute, 1 25 %, 2 50 %, 3 100 % -- not the raw `NR32` bits,
which run the other way. Table `0F`'s `1 2 3` is therefore 25 % → 50 % → 100 %, the swell the user
hears. (Bit 7 of the byte LSDj writes is set and means nothing.)

ChipBoy had `waveLevel = vol / 4`, which is 0 for every amplitude 1-3: the whole swell muted. It is
`vol & 3`. The four-level wrap is the ROM's and cannot be a clamp.

## 109. `E` on a killed channel is a new envelope, and it runs to zero

`SAMESONG`'s phrase 10 plays a hat with `K 03` and puts a bare `E 21` on the row after -- no note,
no instrument, just the command. On the ROM that is a soft ghost hit; in ChipBoy the channel sat at
one level and hissed.

Measured on 9.2.L, reading the channel's volume rather than the zombie bytes (the tracer records the
volume after each write, so a `09 11 18` triple reads as the level it leaves):

| what plays | the ROM's noise volume, every 20 ms |
|---|---|
| `E 6 7` on the note itself | `6 6 6 6 6 6 5 5 5 5 5 4 4 4 4 4 3 3 3 3 3 2 2 2 2 2 1 1` |
| note + `K 03`, then bare `E 4 1` | `4 3 2 1 0` |
| note + `K 03`, then bare `E 2 1` | `2 1 0` |
| note, then bare `E 2 1` two rows on | `2 1 0` |

So `E x y` is the plain `NRx2` byte and it is a whole envelope, not a level: the volume goes **to x**
and then runs **to zero** at rate y -- `1` about 16 ms a step, `7` about 109 ms, the hardware's
`rate / 64` seconds. It does this whether or not a `K` has been through, and it never triggers
(no `NR44` write goes with it). A killed channel answering an `E` is what makes the ghost hit: the
level comes back up to x and falls away again over a few tens of milliseconds.

ChipBoy did the first half and not the second. `Cmd::E` set `envVol`, `envRate` and `envDir`
correctly and `setLevel` walked the volume to x, but after a `K` the voice had been torn down --
`stopVoice` had cleared `active` -- so the per-tick envelope never ran again and the level stayed
where the walk left it. Without a `K` the same phrase decayed properly, which is why this only shows
up on the hats.

## 110. An `L` in a cell slides the bare note; a table's transpose column plays no part in it

`SAMESONG`'s phrase 21 on PU1 is a note with instrument `0B` and, on the row after, a bare note two
semitones up with `L 10`. Instrument `0B` runs table `07`, which has `TSP 0C` on row 2 and `H 01` on
row 4, so the table loops rows 1-3 and blips an octave up every third tick. ChipBoy put the whole
bend an octave up.

The `L` itself was never wrong. Traced on 9.2.L against ChipBoy, six cases of a note and a bare note
two semitones up -- plain, `L 00`, `L 10`, `L 40` -- agree register for register, and `L 00` is
instant (`1783`, `1783`, `1812`) while `L 10` walks `1783 → 1812` in twenty updates. With the table
running they come apart:

```
ROM      1783T 1783 1915 1783 1915 1915 1785 1787 1788 1790 ... 1807 1928 1929 1930
ChipBoy  1783T 1783 1915 1783 1915 1915 1916 1917 1918 1919 ... 1927 1928 1929
```

The ROM slides the **base** -- `1785 1787 … 1807`, the note without the column -- and shows no octave
blip for the whole run, then puts the column back the moment the slide ends (`1928 1929 1930`, and
`1812 / 1930` alternating after). ChipBoy slid from `1915`, the transposed pitch it happened to be
on, so the bend sat an octave up from beginning to end.

So: **the source and the target of a cell's `L` are the note's own pitch**, not the pitch the column
has just put it on. A table's `L` keeps §68's rule -- the note sounds plain and slides *to* the
transposed one, which is the wave kick's `TSP C4` beside `L 20` -- so the two cases differ and the
driver has to know which one it is in.

**Corrected.** This section first read the trace above as "the column is suppressed for the whole
run", because twenty updates of `SAMESONG`'s phrase 21 show no blip. Over a longer window that is
too strong: phrase 23, where the `L` rides on a note-on rather than a bare note, blips all the way
through --

```
1871T 1871 1959 1871 1959 1959T 1871 1872 … 1878 1963 1963 1963 1963 1963 1964 1964 1881 1882 …
                                                 ^ the column, on top of the sliding base
```

-- so the column clearly reaches a running slide there. What is settled, and is what §111 implements,
is the **source and the target**: both are the bare note, which is what put phrase 23's bend an
octave up and sliding the wrong way. How the column interleaves with a slide already running, and
why phrase 21's shorter bend shows none of it, is **not settled** and is in `docs/HANDOFF.md`.

## 111. A cell's `L` does not move the pitch on its own update

Measured on 9.2.L while fixing §110. The update an `L` is processed on keeps the pitch the channel
was already sitting on, column and all; the slide begins on the **next** pitch update.

| | the L's own update | then |
|---|---|---|
| phrase 21, bare note + `L 10` | ROM `1915` (the base 1783 under the table's octave) | `1785 1787 1788 …` |
| phrase 23, note-on + `L 27` | ROM `1959T` -- it *triggers* on that value | `1871 1872 1872 …` |

Both are the same rule and neither is the note's own pitch: `1915` is the old note plus the column,
`1959` the old note plus the column at a trigger. ChipBoy wrote the bare base on that update (`1783`,
`1871T`), one pitch update ahead of the ROM and, on the note-on, triggering the wrong period.

The column to hold is the one the **table actually had at the last pitch write**, which is neither
of the two obvious candidates. The live column is no use: inside a note-on the table has already
restarted on row 0, which transposes nothing. Nor is the transpose that was folded into the pitch,
because a slide already running has had that suppressed -- phrase 23's second `L`, arriving
mid-slide, still holds an octave. So the voice records the column at every pitch write
(`pitchNowColFine`) alongside how much of the pitch was transpose (`pitchNowTspFine`, which is what
§110's source subtraction needs), and a cell's `L` carries the column in `slideTspFine` for exactly
one update before the slide advance drops it.

With this, `SAMESONG`'s phrase 21 and phrase 23 agree with the ROM register for register from the
note through the whole bend.

## 112. The pulse instrument's finetune, byte 11

`SAMESONG` warned six times that a pulse instrument "has finetune NN: ChipBoy has no finetune".
Measured on 9.2.L by sweeping byte 11 of a pulse instrument and reading the period:

| byte | 00-08 | 0F | 10 | 20 | 30 | 40 | 60 | 80 | A0 | C0 | E0 | F0 | FF |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| units off the period, on PU1 | 0 | -1 | -1 | -1 | -2 | -3 | -4 | -6 | -7 | -9 | -10 | -11 | -12 |

At this note a semitone is about 11.8 register units, so `FF` is one semitone and every row above
is `round(-byte / 256)` of a semitone. It is **not** applied at the trigger -- the note sounds at its
plain period and the *first pitch update* moves it -- which is why it took a register stream rather
than a trigger reading to find at all.

**It goes down on PU1 and up on PU2.** Byte 11 = `80` gives `1831` on PU1 and `1843` on PU2, six
units either side of the plain `1837`: the two pulses detune against each other, which is what the
byte is for. That is the same split §78 measured for the `F` command, and an `F` on the cell
**replaces** it rather than adding to it -- `F 08` beside byte 11 = `80` gives -3, which is `F`'s own
`y/32` of a semitone and nothing of the instrument's.

ChipBoy's voice already carries `fineTune` in 1/256 semitones and `F` already writes it, so the
instrument gains **Finetune** (`Instrument::fineTune`, 0-255) and a note-on seeds `v.fineTune` from
it -- negated on PU1, positive on PU2 -- where it used to seed zero. A cell's `F` overwrites it for
the note in progress, as it always did, and the next note-on brings the instrument's back.

Byte 11 is the wave instrument's **SPEED** (§65) and means nothing on noise, so this is a pulse
field only.

## 113. A table a table starts takes effect at once *(corrected by §122)*

`SAMESONG`'s instrument 02 sounded dead flat in ChipBoy where the ROM has a vibrato on it. Its table
`11` has `A 02` in row 0's second command lane, and table `02` has `V F3` in its own row 0 -- a table
starting a table, with the vibrato in the second one.

A plain chain already worked. What broke it is the instrument's **table mode**: byte 5 bit 3 is
LSDj's STEP, one table row per trigger rather than one per tick, which a sweep confirms on both a
pulse and a wave instrument -- with the bit set, a table whose transpose column steps `0 4 8 12`
holds its first row and the note never moves. Instrument 02 is in STEP mode, so its table never
leaves row 0: the `A 02` there is the whole of what the table does, and everything after depends on
it firing.

ChipBoy called `beginTableRun()` for the `A` and left the new table's row 0 for the next tick. In
Tick mode that arrives and the vibrato starts a tick late; in **Step mode there is no next tick**, so
the second table never ran at all. Measured on 9.2.L, byte 5 = `08`:

```
ROM      2016 1837 1837 1837 1831 1837 1843 1837 1831 1837 1843 …
ChipBoy  1837 1837                                                 (flat: nothing ever fired)
```

So the table a table starts fires its **row 0 in the same step**, which is the rule a note-on already
follows (§31). Only from inside a table: a cell's `A` keeps starting at row 0 and firing it on the
next tick, which §32 pins. A depth guard stops a ring of `A`s running away.

> **§122 corrects this.** The rate was the real fault: a table an `A` starts runs one row a **tick**
> whatever the instrument's mode says, so the "next tick" this entry says never comes does come, and
> its row 0 belongs to it rather than to the `A`'s own step. The fire-at-once below is gone.

## 114. Every vibrato shape is centred, and shape 3 is off

Chasing §113 left instrument 02 vibrating half as far as the ROM -- `1849 / 1855` against
`1843 / 1855`. Sweeping byte 5's low three bits with `V F3` on 9.2.L:

| byte 5 & 7 | what the period does | shape | starts |
|---|---|---|---|
| `0` | `1837 1831 1837 1843 …` | triangle | down |
| `1` | `1837 1843 1837 1831 …` | triangle | up |
| `2` | `1843 1840 1837 1834 …` | saw, ramping down | down |
| `3` | `1831 1834 1837 1840 …` | saw, ramping up | up |
| `4` | `1831 1831 1843 1843 …` | square | down |
| `5` | `1843 1843 1831 1831 …` | square | up |
| `6`, `7` | `1837` throughout | **off** | -- |

Three things follow. **Every shape is centred on the note** -- the full depth either side, a span of
12 units for `V F3` -- where ChipBoy's saw and square ran `0 .. +1`, half the swing and all of it on
one side. **Bit 0 picks which half comes first**, not a one-sided direction; ChipBoy's `VibDir`
already flips the wave, which is exactly that once the shapes are centred. And **shape 3 is no
vibrato at all**, which the importer used to clamp to Square.

`VibShape` gains `Off`, the saw becomes `2 * ph / N - 1` and the square `+1` then `-1`, and the
importer stops clamping. Instrument 02 -- byte 5 `0D`, so square starting up -- now gives
`1855 1855 1843 1843` against the ROM's `1855 1855 1843 1843`.

## 115. `A 20` in a cell stops the table, and `W` on a wave instrument is the run

Two of `SAMESONG`'s import notes, both measured on 9.2.L.

**`A 20` is LSDj's table stop.** A table whose transpose column climbs, with `A 20` on the row after
the note:

```
no A20   1837 1837 1871 1899 1923 1943 1959 1974 1985 1837 1871 …   (it loops for ever)
A20      1837 1837 1871 1899 1923 1943 1959                          (it stops dead)
```

and the pitch stays on the row the table last reached rather than returning to the note. ChipBoy's
cell has a **TBL column**, which names a slot and cannot say "stop", so the importer dropped the
command -- thirteen times in this song. ChipBoy's own `A 0` already stops a run, so the importer
puts it in a **command slot** instead of the column.

**`W xy` on a wave instrument is the frame run**, not a wave slot. Sweeping both nibbles:

| `x` (high) | 1 | 2 | 3 | 4 | 8 | F |
|---|---|---|---|---|---|---|
| ms between frames | 19 | 39 | 58 | 77 | 161 | 393 |

which is **x ticks a frame** exactly (a tick is 19.5 ms at this tempo), and `x = 0` leaves the speed
alone. The low nibble is the run's **length**, `y + 1` frames spread across the sixteen the way §65's
LENGTH is, with `y = 0` meaning all sixteen: `y = 1` visits `0 15`, `y = 2` visits `0 7 15`, `y = 3`
visits `0 5 10 15`, `y = F` visits all of them.

ChipBoy's own `W` on the wave channel is the **wave slot** (§75), a different thing, and songs and
tests already use it that way. So the run gets its own letter, **`U`**: `x` ticks a frame, `y + 1`
frames. It is not added to the per-channel command *parameter* list, so the 76-parameter table does
not move -- `H` is already left out the same way.

**One frame still out of place.** The spread `waveRun()` computes matches the ROM at most lengths
(`0 5 10 15` for 4, `0 3 6 9 12 15` for 6, `0 2 4 6 9 11 13 15` for 8) but not all: at length 3 the
ROM visits `0 7 15` and ChipBoy `0 8 15`, and at length 7 the ROM visits `0 2 5 7 10 13 15` against
ChipBoy's `0 2 5 8 10 13 15`. Neither `i * 15 / (L - 1)` nor `i * 16 / (L - 1)` fits every length, so
the ROM is doing something else -- an accumulator with its own rounding. Recorded rather than
guessed at; it is one frame of sixteen, on two of the sixteen lengths.

## 116. The shaped envelope steps on the pitch clock, not on the tick

`SAMESONG` warned eight times that "an envelope stage faster than a tick a level is quantised to the
tick". It was not a rounding: whole levels were being skipped.

`CLAP` is the clearest. Its shaped envelope is start 12 → peak 8 over **2 ticks**, decay to 4 over
**2**, then a fade to 0 over **1** -- four levels in a single tick. Sampling the noise channel's
volume every 5 ms:

```
ROM       B BA 99 88 777 66 55 4 2 0        every level, 11 down to 0
ChipBoy   AAA 8888 6666 4444 0000           four levels, the rest skipped
```

LSDj steps the level on its own envelope clock -- the same pitch clock everything else in §7 runs on,
about 2.8 ms -- so a stage shorter than the levels it crosses still walks through every one of them.
ChipBoy rendered the shaped envelope **one level per tracker tick** (§27), so a four-level fade in one
tick became a single jump.

The stages stay whole ticks, which is what the instrument stores and what the importer converts to.
What changes is the reading: the position is now `shapedTick * 256 + sub`, where `sub` is how far
this tick's pitch clocks have got, and the level is re-read on every pitch clock as well as on the
tick. `envSegmentLevel` only cares about the ratio, so scaling both sides leaves the value at a tick
boundary exactly as it was and fills in the levels between. The driver measures `clocksPerTick_`
from the clocks it counts between ticks, so it follows the tempo with nothing to configure, and the
position is held monotonic -- the tick resets `sub` to zero, and without that the level would step
backwards at every tick boundary and the channel would hear it.

```
CLAP      ROM  B BA 99 88 777 66 55 4 2 0      ChipBoy  A 99 88 77 66 55 4 3 2 11 0
SNARE     ROM  AAA 999 8888 777 666 5555 444 3333 222 111
          CB   AA 999 888 7777 666 555 4444 333 2222 111
```

What is left is the stage *lengths*, which the importer rounds to whole ticks (`envTicks`), so a
stage can be a tick longer or shorter than the ROM's. The level sequence is right; the total is
within a tick.

## 117. A kit instrument's `DIST`: the four ways LSDj sums two samples

A kit note plays **two** samples at once -- its high digit picks one from the kit in instrument
byte 2, its low digit one from the kit in byte 9 (§96) -- and `DIST` says how the two are added.
ChipBoy sums the pair when it imports the note, so the mode has to be exact there.

**What the ROM does.** Measured on 9.2.L: the streamer copies the first kit's sixteen bytes into
the scratch buffer at `$FFA0` (bank 0, `$0420`), then calls a routine it *generates* in WRAM at
`$D480` which mixes the second kit in nibble by nibble. That routine is a lookup, not arithmetic:

```
a = the buffer byte (the high digit's sample)   ahi, alo
b = the second kit's byte (the low digit's)     bhi, blo
out = T[(bhi << 4) | ahi] << 4  |  T[(alo << 4) | blo]
```

`T` is one of four 256-byte tables LSDj copies out of ROM into `$D000`, `$D100`, `$D200`, `$D300`
at boot, and **instrument byte 10 holds that page** -- it is literally `D0`, `D1`, `D2` or `D3`
(bank 0, `$04BA`: `ld a,[$C4F9]; ld h,a; call $D480`, and `$C4F9` is byte 10 copied through
`$C0DA`). Any other value points the lookup at unrelated memory; the ROM then streams noise, which
nothing can reproduce, so ChipBoy reads such a byte as `HARD` and says so in the import notes.
`F0` behaves as `D0` only because echo RAM mirrors it.

**The four curves.** Every table is a function of `r + c` alone, so each is a curve over the sum
`s = a + b - 8` of the two nibbles (both 0-15, 8 being silence). Checked entry for entry against
all 47 ROMs in the archive:

| page | 9.2 and later | 9.1.C and earlier | what it does to `s` |
|------|---------------|-------------------|---------------------|
| `D0` | `HARD`  | `CLIP`  | `clamp(s, 0, 15)` |
| `D1` | `SOFT`  | `SHAPE` | 9.2: half slope outside a knee (below); before: the mirror |
| `D2` | `FOLD`  | `SHAP2` | 9.2: the mirror, `-s` below 0 and `30 - s` above 15; before: twice that slope |
| `D3` | `WRAP`  | `WRAP`  | `(s - 8) & 15` -- the sum wraps round |

So the meaning of the stored byte moved at 9.2: a save written by 9.1 that says `D1` means the
mirror, and the same byte in a 9.2 save means the soft clip. The model carries the pair (§56).

`SOFT` is symmetric about `s = 8`: with `k = |s - 8|`, the offset is `k` while `k <= 4`, then
`min(7, (k + 4) / 2)`, and 8 from `k = 13` up; the result is `8 +/- offset`, clamped.

`SHAP2` is the mirror with twice the slope outside the range: `-2s` below 0 and `15 - 2(s - 15)`
above 15, clamped. Its ROM table has **one** entry that the formula does not give -- `r = 12,
c = 15` reads 5 where the fold would give 7, and the transposed entry reads 7 -- so ChipBoy
reproduces that entry as well, which is why the nibble order above matters: the high nibble of each
byte indexes the second kit's sample by the row and the first kit's by the column, and the low
nibble the other way round.

**What ChipBoy does.** The importer builds one ChipBoy sample per note byte, so the mix happens
once, at import, with the model's mode: `kitMix` in `Source/core/Import/LsdjKitDist.h`. Nothing in
the engine changes -- a ChipBoy kit sample is still plain nibbles.

## 118. A ChipBoy kit note carries the pair, not the sum

§117 settled how LSDj **sums** a kit note's two samples. This is what ChipBoy does with the pair.

The importer used to sum them once and store the result as a single sample, one entry per distinct
note byte. The song played, but the second sample was gone: it could not be changed, removed or put
on another note, and a kit a song used eight pairs of filled eight of the slot's thirty-two entries
with near-duplicates of six sounds.

Now a ChipBoy kit keeps the **sources** and a cell names two of them:

- the **note** column picks the first, by nearest note, as it always did;
- the **VEL** column picks the second by **index + 1** -- `00` blank plays one sample, `01` is the
  kit's first, up to its last;
- the kit's **`Dist`** says how they are summed: `Clip`, `Soft`, `Fold`, `Fold2`, `Wrap`, which are
  §117's five curves under ChipBoy's own names.

The driver runs a second cursor beside the first and writes `kitMix(dist, i, a, b)` into the chunk.
The second sample never ends the note -- past its end it reads as silence, 8 -- so a note's length
is the first sample's, which is what the ROM does. A VEL past the end of the kit, which every
ordinary MIDI velocity is, plays one sample, so nothing that played before this change plays
differently.

The importer maps a note byte `hi lo` straight onto that: `hi` names a sample of the kit in
instrument byte 2 and becomes the cell's note, `lo` one of the kit in byte 9 and becomes its VEL. A
note with one digit gets a blank VEL. An instrument whose two kits are the same one shares the
entries, so `AIR`+`AIR` is one sample named twice rather than two copies.

The one thing VEL had to be kept clear of is the **keyswitch velocity mode**, which adds `vel / 8`
to the instrument slot: it now skips a channel whose instrument is a kit.

## 119. `V` on the noise channel is a tick thing, and eight times as deep

§77 let `V` reach the noise channel by reading the same vibrato the pulses read -- the offset in
1/256 semitones, rounded to a whole one before the map lookup. Two things about that were wrong,
and together they are what makes `SAMESONG`'s phrase 47 -- instrument 08, a *pulse* instrument,
played on `NOI` -- sound nothing like the ROM.

**It moves once a tick, not once a pitch clock.** Measured on 9.2.L with `V 2 F` on a noise note:
`NR43` steps by four map entries every tick (19.4 ms) and by nothing in between. ChipBoy ran the
phase on the 358 Hz clock, so it swung about seven times too fast.

**Its phase is the Tick table's, whatever the instrument's `PITCH`.** Sweeping `V x 8` over every
speed gives quarter periods of 24, 18, 16, 12, 9, 8, 6, 4.5, 4, 3, 2.25, 2, 1.5, 1.125, 1 and
0.75 **ticks** -- exactly `kVibTickStep9`, the table §7 measured for Tick mode, and not the pitch
clock's `64/(x + 1)`. The noise channel has no pitch-clock processing in the ROM, so its `V` lives
in the tick handler and takes the tick law with it.

**Its depth is in map entries, not semitones.** Sweeping `V 2 y` over every depth gives swings of

```
y     0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
map   1  2  3  4  6  8 12 16 20 24 28 32 40 48 56 64      entries
```

which is `kVibDepth256[y] / 32` exactly -- eight entries per semitone of the depth table the
pulses use. So the ROM adds the depth to the note **index** without the 1/256 scaling, and `V 1 F`
on noise is a swing of five octaves where on a pulse it is eight semitones. The index wraps
(§83), which is why a deep one walks off the end of the map and round again.

ChipBoy takes all three: `writePeriod` divides the vibrato by 32 instead of 256 for a noise voice,
the tick handler steps a noise vibrato's phase itself whatever the instrument's `PITCH`, and
`pitchClockOn` no longer turns the pitch clock on for a noise voice that has only a vibrato.

**What is left.** Swept over every speed and six depths, ChipBoy's `NR43` stream is the ROM's
byte for byte in 81 of the 96 settings. The fifteen that differ are all at speeds 3, 4, 9 and F,
and they differ by **one sample of the phase** at the ticks where the ninths accumulator lands on
a whole phase unit -- the ROM holds the old unit for that tick where ChipBoy takes the new one.
`V 3 F` fits `floor((12k - 1) / 9)` where ChipBoy computes `floor(12k / 9)`, but the same
correction is wrong at speed 4, so the ROM's accumulator is not simply one behind. Ordering inside
a tick is also not settled: with a table running as well, the ROM writes the transpose column and
then the vibrato, and ChipBoy writes them the other way round, so the value the tick ends on can
differ even when both move at the same rate.

**What this is not.** A mismatched instrument type on a channel needed no work at all: measured on
9.2.L, a *pulse* instrument played on `NOI` writes the same `NR41`-`NR44` as a noise instrument
with the same bytes, note for note and tick for tick -- the note goes through the noise map, the
envelope is byte 1 read as three stages, `NR42` starting at `(byte 1 & F0) | 8`. ChipBoy's import
already builds a `NOI` variant that does exactly that, and its register stream matches the ROM's
without a vibrato. LSDj simply does not consult an instrument's type when a channel plays it; the
type only says which bytes the editor shows.

## 120. `H F F` ends the channel's timeline (corrected by §214: it stops the song)

§80 measured it and left it: `H F F` **stops the channel** on the ROM -- two note-ons over
thirty-five seconds and then nothing at all, where `H 2 F` on the same phrase hops twice and runs
on. ChipBoy ended the phrase there and let the chain carry on, which is the one thing the ROM does
not do. `SAMESONG`'s phrase 89 step 0 is an `H F F`, so ChipBoy played a whole channel the ROM
had switched off.

ChipBoy has no "stop" command and does not need one. A channel's chain is a flat list of phrase
slots and the host's timeline is laid out from it, so the faithful expression of "the channel
stops here" is that **the channel's chain ends here**: the importer stops adding rows to that
channel at the chain step whose phrase holds the `H F F`. The phrase itself keeps the steps before
the `H` -- they play -- and the chain has nothing after it.

That is what the user asked for, and it falls out of the layout: a host playhead dropped anywhere
past the stop finds no row on that channel, so nothing plays, exactly as on a ROM that got there
by playing through. The other channels are untouched, which is also the ROM's behaviour -- `H F F`
stops the channel it is on and no other.

**The one thing it does not carry** is the loop. On the ROM the channel is dead until playback
stops, so a song that loops round comes back without it; in ChipBoy the rows before the stop are
still there and play again on the next pass. Expressing that would want a channel state that
survives the transport's loop, which is a bigger thing than this, and the import notes say so.

## 121. A shaped envelope's stages are not whole ticks either

§116 fixed the *reading* of the shaped envelope -- the position runs in 1/256 of a tick and the
level is re-read on every pitch clock, so a stage crossing four levels inside one tick walks
through all four. What it left was the stage's own **length**, which `envTicks` rounded to a whole
tick and clamped to at least one. On `SAMESONG`'s `CLAP`, at tempo 129:

```
stage        levels  per level    length      ChipBoy had
C3 -> 84      12->8   8.377 ms    33.5 ms = 1.729 ticks    2      +16%
84 -> 41       8->4  11.169 ms    44.7 ms = 2.305 ticks    2      -13%
41 -> 0        4->0   2.792 ms    11.2 ms = 0.576 ticks    1      +74%
```

The third stage is the worst: a clap's tail, 11 ms on the ROM, was held for 19. Four of
`SAMESONG`'s instruments warned about it.

**The fix is to carry the fraction.** Each stage of `bank::Envelope` gains a **fine** byte beside
its tick count -- `attackFine`, `decayFine`, `fadeFine`, `releaseFine`, each an extra 1/256 of a
tick -- and the driver, which already works in those units, reads `ticks * 256 + fine` as the
segment's length. A stage shorter than a tick is `0` ticks and a fine value, which `envTicks`
could not express at all. Nothing else in the driver changes.

The importer computes each stage's length as `levels x period` in milliseconds, divides by the
song's tick and splits the result; the warning goes with it. Tempo is baked in, as it already was:
LSDj's envelope runs on the fixed 2.79 ms pitch clock, ChipBoy's on ticks, so the conversion is
exact at the song's own tempo and drifts with a `T` that changes it. That is §51's model, not a
new departure.

**In the editor** the stage steppers stay whole ticks, and setting one clears its fine part -- the
number shown becomes the truth. An imported envelope that has never been edited keeps the ROM's
fraction; the Instrument tab's envelope readout prints it (`2.3 ticks`) so it is not invisible.

## 122. `STEP` is the instrument's own table only: an `A` runs one row a tick

An instrument's byte 5 bit 3 picks how its table advances -- one row a **tick**, or one row a
**trigger**, which §113 called STEP. ChipBoy applied that to whatever table the channel happened to
be running. It is narrower than that.

Measured on 9.2.L, a wave instrument with bit 3 set and a table whose sixteen rows each carry an
`E`, read off `NR32`:

```
instrument's own table (byte 6)   ROM  one row, then nothing      a row a trigger
a table a cell's `A` starts       ROM  0 1 2 3 1 2 3 ... 19 ms apart   a row a TICK
a table another table's `A` starts  ROM  the same, 19 ms apart        a row a TICK
```

ChipBoy froze all three after row 0 when the instrument said STEP. So **`STEP` governs only the
table the instrument names**; a table an `A` starts -- from a cell or from inside another table --
runs one row a tick whatever the instrument says.

**This is `SAMESONG`'s phrase 14.** Instrument 02 is a wave instrument in STEP mode whose table
`11` holds `F 00` and `A 02` on row 0 and `Z 10` with `H 00` on row 1. Table `02` is the one with
the shape in it: `V F3` on row 0, `E 02` on row 9, `E 03` on row 12, and two `H` rows that loop the
pair. On the ROM `NR32` walks 100% -> 50% -> 100% every eleven ticks or so -- the fade the user
heard while the volume column stayed blank. In ChipBoy table `02` started, fired its row 0 (so the
vibrato was there) and then stood still, so the fade never happened.

`Voice::tableTicks` carries it: set when `Cmd::A` starts the run, cleared when the instrument's own
table (or a cell's `TBL` column, which overrides that same table) does. The tick handler steps the
lanes when the instrument says Tick **or** `tableTicks` is set, and a note-on's one-row-per-trigger
advance is skipped for an `A`-started run.

**And it fires its row 0 one tick later, which corrects §113.** The same measurement, read row by
row: the instrument's own table plays row 0 *with* the note and rows 1, 2, 3 on the ticks after it,
while a table an `A` started plays row 0 on the **next tick** and the rest after that -- one tick
behind the row that started it, every time. §113 fired it at once because in STEP mode there was no
next tick to fire it on; with that fixed the immediate fire is wrong and goes.

Two things in ChipBoy were eating that row 0. The fire-at-once of §113 was one. The other is that
`stepTableLane` holds `step` as a **reference** into the voice, and an `A` on the row it is running
puts every lane back to row 0 -- so the `++step` that ends the outer row was incrementing the *new*
table's pointer and the new table began at row 1. `beginTableRun` already counts runs, so the lane
returns when `tableRun` moved rather than bookkeeping a table that has gone.

Measured end to end on `SAMESONG`'s phrase 14, `NR32` against the ROM's, the ROM's 25 ms of
playback lead-in taken off:

```
ROM  0  196  235  428  466  662  698  891  929  1123  1161 ms
CB   0  194  233  426  465  659  698  891  930  1124  1163 ms
```

## 123. `Z`: what it re-runs, and the record outliving the note

The ROM says it plainly (the command help, bank 01 `$7448`):

```
Z: RANDOMIZE; REDO LAST CMD WITH RANDOM VALUE N ADDED TO LAST CMD VALUE
```

§74 measured the lane rule -- `Z` re-runs its own lane's last command, the channel's cell lane or a
table's column 1 or 2, and `H` and `Z` are never recorded. Three things checked again here, because
`SAMESONG`'s phrase 14 leans on all of them (`W 20` then five `Z 3F`, and `F 00` then `Z 10` inside
table `11`).

**The random is per nibble, not on the byte.** On `WAV` an `E` reads only its **low** nibble (§79),
so `E 03` followed by `Z F0` -- which touches the high nibble alone -- must leave `NR32` where it is
if the random is per nibble and move it if it is added to the byte. Measured on 9.2.L: `NR32` stays
at 100% for every note under `Z F0` and walks under `Z 0F`. §74's rule stands.

**`Z 0 0` re-runs the command exactly**, which is the clean way to see what the record holds:

```
E 02, a note, Z 00, a note, Z 00        ROM NR32  2 0 2 0 2      (50%, the instrument's, 50%, ...)
E 02, O 03, Z 00                        ROM NR32  2 0 0          O is the last command now
```

**And the record outlives the note-on** -- it even outlives a different instrument:

```
E 02, a note on another instrument, Z 00, Z 00    ROM NR32  2 0 2 2
```

ChipBoy cleared `Voice::lastCellCmd` at every note-on, so a `Z` on a later row had nothing to
re-run and did nothing at all. That is phrase 14's `Z 3F` rows: on the ROM each of them re-runs the
`W 20` from step 2 with a fresh random speed and length, and in ChipBoy they were inert. The clear
goes; nothing inside a channel's playback resets the record.

## 124. A phrase command does **not** outlive its note -- the working copy is reloaded at every note-on

§122's round reported the opposite, from a trace read wrong: three `NR11` writes that all belonged
to the *first* note were counted as one each for the first three, which made the duty look as though
it held. It does not. Measured properly on 9.2.L -- row 0 a note with the command, row 1 a plain note
on the same instrument, row 2 a note on a **different** instrument, row 3 back to the first -- and
the register read once per note:

```
                    ROM              ChipBoy
E  pulse  NR12   18 F8 F8 F8      18 F8 F8 F8
W  pulse  NR11   C0 00 00 00      C0 00 00 00
O  pulse  NR51   FE FF FF FF      FE FF FF FF
S  pulse  NR10   9F 00 00 00      9F 00 00 00
V  pulse  NR14   07 07 07 07      07 07 07 07
E  wave   NR32   C0 03 03 03      40 00 -- --
```

Every letter goes back to the instrument's own value on the very next note, on both sides. LSDj
reloads the instrument at each note-on exactly as ChipBoy's `latch` does, so **there is no
working-copy model to adopt** and nothing here to change. The `NR32` row differs only in bits the
chip does not use -- `C0` and `40` both carry volume code 2, `03` and `00` both carry 0 -- and in the
ROM re-writing the register at every note where ChipBoy writes it when it changes.

**What is really different is the order inside the note**, and it is sub-millisecond:

```
W 03 on a note's own row    ROM  NR11=00, TRIGGER, NR11=C0 (+0.3 ms)
                            CB   NR11=C0, TRIGGER
S 71 on a note's own row    ROM  NR10=00, TRIGGER, NR10=9F, TRIGGER (+0.35 ms)
                            CB   NR10=9F, TRIGGER
```

The ROM triggers on the instrument's value and lets the row's command land a fraction of a
millisecond later; on `S` that second write costs a second trigger, because LSDj restarts the
channel to start the sweep. ChipBoy folds a cell's command into the note's own burst instead --
§3's deliberate choice, so a command on a note's row costs neither a second burst nor a pop. The
steady state is identical either way; what the ROM has and ChipBoy does not is that extra
`S`-on-a-note retrigger. On a **bare** row (no note) the two agree exactly, retrigger included.

## 125. A pitch effect below the note table clamps the register; it does not silence the note

`SAMESONG`'s phrase 6C on PU1 plays instrument `1A` (`EGUIT`) on note byte `02` -- MIDI 37, near the
bottom of the pulse range -- and its table `13` opens with `V F9`, a vibrato three semitones deep.
Three semitones below MIDI 37 is off the bottom of the note table. ChipBoy computed the note-on's
period *with* the vibrato at its trough, got "below the chip's range", and killed the voice: the
whole measure was the `O` commands moving `NR51` and nothing else. The user heard pops and clicks
where the ROM has a sound.

Measured on 9.2.L, the same note and vibrato, reading the period register:

```
note 02, V F9    157  157  0  157  457  157  0  157  457 ...
note 02, V FF    157  157  0  157  856  157  0  157  856 ...
```

157 is MIDI 37. `V F9` is three semitones, so the top of the swing is MIDI 40 = 457 and the bottom
would be MIDI 34 = **-200**, which the ROM writes as **0**: the register clamps at the bottom and the
note goes on sounding. `V FF` is eight semitones and the top is MIDI 45 = 856, the bottom 0 again.
Not a wrap -- -200 would wrap to 1848, and the ROM writes 0.

So a pitch effect that runs off the bottom of the table **clamps the period register at 0**. Only a
note whose *own* pitch is out of range does not sound, which is C4's rule and stays. `computePeriod`
now tests the plain note -- the cell's note with its transposes, chord and table column, without the
vibrato, `P` or a slide -- for that, and clamps the effects on top of it.

The high end needs no rule: the note table compresses towards 2047 (a `V F9` on note byte `60` only
moves the register between 2038 and 2041), and the register is clamped to 2047 as it always was.

## 126. `V 0 0` turns the vibrato off; `V x 0` is a vibrato of depth zero

Two more things phrase 6C's table settles, both measured on 9.2.L with the same low note (MIDI 37,
period 157) so a fraction of a semitone is tens of register units and easy to read.

**The trigger carries the plain note.** Instrument `1A`'s vibrato shape is **square**, which swings
the full depth from phase zero (§114), so at the note-on the offset is already three semitones down.
The ROM triggers at 157 anyway and the swing appears on the update after:

```
ROM   0 ms: 157 (TRIGGER)   2 ms: 0   5 ms: 0   8 ms: 457 ...
```

That is §84's rule -- the note-on writes the **plain** note -- widened from the table's transpose
column to the vibrato as well. `Driver::plainVib_` is scoped to every trigger, where `plainTrigger_`
is scoped to a trigger that started a table.

**And `V 0 0` is off.** Table `13` turns its vibrato off on row 1 with `V 00`, and the ROM's period
goes back to the plain note. A depth of zero on its own does not: `V 2 0` leaves a vibrato running
at the depth table's smallest swing, an eighth of a semitone.

```
table V F9 then V 00    ROM  ... 19: 457   22: 157   25: 157      the note itself
table V F9 then V 20    ROM  ... 19: 457   22: 170   25: 170      157 + 1/8 semitone
```

ChipBoy set `vibOn` for any `V` at all, so `V 00` left an eighth of a semitone on the note for ever.
The whole byte being zero is what turns it off -- speed zero alone is the *slowest* vibrato, not a
stopped one (§119's `V 0 F` sweeps a fifth of the noise map), and depth zero alone is the smallest.

Both rules re-measured unchanged on **9.3.9** and **9.4.2**: `157 (trigger), 0, 0, 457 ... 157` for
`V 00` and `... 170` for `V 20`, register for register.

## 127. An `S` on a note's own row writes `NR10` after the trigger, and retriggers

§124 measured the order of a cell's commands against its own note and found one behavioural
difference, this one. ChipBoy applies a cell's commands inside the note-on so the note's own burst
carries them -- §3's choice, so a command slot firing at every note costs neither a second burst nor
a pop. The ROM agrees for `E` (the envelope the trigger uses is the command's) but not for `S`:

```
S 71 on a note's own row   ROM  NR10=00, TRIGGER, NR10=9F, TRIGGER (+0.35 ms)
                           CB   NR10=9F, TRIGGER
```

The sweep unit reloads on a trigger, so LSDj writes `NR10` after the note and triggers again to
start the sweep from there. ChipBoy folded the write into the burst and never emitted that second
trigger, so the note's phase was not restarted where the ROM restarts it. On a **bare** row -- an
`S` with no note -- ChipBoy already did both, and matched.

So a cell's `S` on `PU1` now writes `NR10` and retriggers **after** the note's burst, as it does on a
bare row. Nothing else moves: `E`, `W`, `O` and `V` keep folding into the burst, which is what the
ROM's registers show for `E` and is inaudible for the rest (`W`'s duty differs for the 0.3 ms
between the trigger and the ROM's own write). A **slot**'s `S` is left alone: slots are ChipBoy's
automation with no LSDj counterpart, and one firing on every note would cost a retrigger on every
note.

## 128. A table's `K` counts from the row's own tick, not the tick after it

`K xx` kills the note `xx` ticks later, counted from the tick the command was **read** on. §3 made
that a countdown on the voice, stepped once at the top of every tick. A phrase cell is read before
the tick body, so a cell's `K` was exact; a table row is read inside it, after the step, so every
`K` in a table fired one tick late.

Measured on 9.2.L at tempo 129 (a tick is 19.4 ms), an instrument whose table holds one `K` on row
3 and nothing else, the levels read off `NR12`'s zombie triples:

```
K 00 at table row 3     ROM  kill at 59 ms  (tick 3)      CB  77 ms  (tick 4)
K 01                    ROM        78 ms   (tick 4)       CB  97 ms  (tick 5)
K 02                    ROM        98 ms   (tick 5)       CB 116 ms  (tick 6)
K 0F                    ROM       349 ms   (tick 18)      CB 368 ms  (tick 19)
K 10                    ROM       368 ms   (tick 19)      CB  never
K 00 in the phrase cell ROM         0 ms                  CB   0 ms
K 01 in the phrase cell ROM        19 ms                  CB  19 ms
K 02 in the phrase cell ROM        39 ms                  CB  39 ms
```

The whole byte is the count -- `K 10` is sixteen ticks, not one -- and it is the same on all four
channels: `PU1`, `PU2` and `NOI` were measured identically, and `WAV`'s `NR30` clears at the same
tick. In the ROM the only per-channel part is the volume walk itself (`02:7E75`), which picks a
shadow byte and a register off the channel index at `02:7E7A` and is otherwise one routine; the
silence that a kill asks for is `02:5F5F` on `PU1`, writing the shadow and calling the walk with a
target of zero.

That walk is worth naming, because it is what a `K` and an `E` share. `02:7E75` takes a target
volume in the high nibble and steps the channel's shadow byte towards it one level at a time,
`09/11/18` for a step down and `08` for a step up -- the zombie writes. Both `E` and `K` set a
target and let the walk run; `K`'s target is zero, and it stops the channel afterwards. So a row
carrying **both** is not ambiguous: the row's lanes are read, the last target wins, and one walk
runs. A table row with `E 40` and `K 00` on it walks 13 -> 0 on the ROM in a single burst; ChipBoy,
applying the lanes in order and then killing, writes 13 -> 4 and 4 -> 0 back to back -- the same
thirteen decrements, in the same tick, so the registers agree.

**A countdown cannot express this, and moving it does not help.** Stepping it after the table's
rows instead of before them fixes the common cases and breaks `K 10`: sixteen ticks in a sixteen
row table, so the row that armed it comes round on the very tick it is due, and re-arming it first
means it never fires at all. The ROM kills there, at tick 19 -- so the kill is due *before* the row
is read, while a `K 00` on a row is due *after* it. Neither order is the whole rule.

**What LSDj has is a moment, not a count.** So the voice keeps `killAt`, the absolute tick index a
`K` asked it to die on: reading `K n` sets `killAt = <this tick> + n`, and the voice dies on the
first tick that has reached it. Both places a `K` can be read agree on what "this tick" is --
`tickCount_` is the tick being processed whether the read came from a note event, which runs before
the tick body, or from a table row inside it -- so a phrase cell and a table row count from the
same place, which is what the measurements say. The due test runs twice in the tick, once at the
top and once after the table's rows, and each case picks the one that comes first: `K 10`'s
sixteenth tick is caught at the top, before its row can re-arm it, and a row's own `K 00` is caught
after the rows, because the top of that tick had nothing to look at yet. Table row 0, which fires
with the note-on rather than on a tick, lands on the note's own tick either way, which is where the
ROM puts it.

The late kill was audible. `SAMESONG`'s `EGUIT` (instrument `1A`) ends its table with `E 30` on row
4 and `K 00` on row 5, and its phrases are rows of sixteenth notes: the ROM drops each note to
silence about 16 ms before the next one starts, and ChipBoy's kill landed on the next note's own
tick and was swallowed by its trigger. Every note ran into the next -- legato where the ROM is
staccato. A looping table made it worse: a `K` whose count is the table's own length never reached
zero at all, because the row came round and re-armed it one tick before it fired.

## 129. `W` on a wave instrument: `y` is the run's length, and the command loads nothing *(`y = 0` corrected by §131)*

§115 read `W x y` on a wave instrument as "x ticks a frame and y + 1 frames of it, y = 0 being all
sixteen". The speed half is right. The rest is not, and the difference is what the user heard in
`SAMESONG`'s phrases `05` and `06`, where instrument `02`'s cells carry `W 20` and `Z 3F` -- a `W`
whose two digits are re-rolled at every note.

Measured on 9.2.L at tempo 129, instrument `02` (`SAMESONG`'s own, run length 4, speed 3 ticks)
sounding a note, then a bare row carrying one `W`. The frames are the wave's own sixteen, written
as the index within them:

```
no command      0 5 10 15, then still          the instrument's own run, once through
W 21            15                             1 step
W 22            7 15                           2
W 23            5 10 15                        3
W 24            3 7 11 15                      4
W 25            3 6 9 12 15                    5
W 28            1 3 5 7 9 11 13 15             8
W 2F            1 2 3 4 5 6 7 8 ... 15         15
W 20 / 30 / 40 / 50 / 00    nothing at all
```

Three separate corrections come out of that.

**`y` is the run's length, and the ladder ends exactly on the last frame.** A run of `L` steps
visits `15 * k / L` truncated, for `k = 0..L` -- `L + 1` frames, the first frame and the last always
among them. `bank::waveRun` spread its frames `i * 16 / (len - 1)` and clamped the overshoot, which
lands on the same ladder only when `len - 1` divides 16 evenly *and* the clamp does no work: the
instrument's own four-frame run (`0 5 10 15`) agreed, `W 24` did not (ChipBoy `0 4 8 12 15`, the ROM
`0 3 7 11 15`), and `W 28` did not (ChipBoy `0 2 4 6 8 10 12 14 15`, the ROM `0 1 3 5 7 9 11 13 15`).
The fix is one term: the frames are spread across `frames - 1`, not `frames`. The clamp then never
fires, which is the tell that the old spacing was a frame too wide.

**`y = 0` is a run of one step, not sixteen.** The ROM writes no frame at all and the run never
moves again; the speed `x` still lands, which `W 40` then `W 0F` shows -- the later command walks at
four ticks a frame, the instrument's own three without the `W 40` before it. ChipBoy read `y = 0` as
"every frame" and started a sixteen-frame walk, so a `W x 0` turned a held wave into a sweep. This
is the loud one: `Z 3F` re-rolls `y` over the whole nibble, so one note in sixteen carries a `W x 0`,
and in `SAMESONG` the literal `W 20` on the phrase rows carries it every time.

**The command loads no frame.** It resets the run to step 0 and leaves the wave sounding where it
is; the next speed period steps to the ladder's second frame. `W 2F` sent at 116 ms, with the run
sitting on frame 5, wrote nothing then and frame 1 at 163 ms -- the next of its two-tick periods.
ChipBoy called `setFrameStep(ch, 0, live)`, which wrote frame 0 immediately: an audible jump to the
bottom of the wave on every `W`, and the reason ChipBoy's frames in those phrases sat at the start
of the run where the ROM's were spread through it.

**Not settled here**, and closed by §132: the ladder's rounding at half steps for `L` of 10 and 12.
`W 2A` measured `1 3 4 6 7 9 11 12` where `15 * k / L` truncated gives `1 3 4 6 7 9 10 12`, and
`W 2C` measured `1 2 3 5 6 7 9 10` against `1 2 3 5 6 7 8 10` -- one index high, at one step, in
both. Every `L` up to 8 matched. §132 has the rule: the ladder is an 8.8 accumulator, not a
division.

**`U` keeps its own letter.** ChipBoy's `W` on the wave channel is the wave slot, which LSDj has no
command for, so the run came in as `U` and stays there: folding them together would need one letter
to mean two things by argument, and would change what `W` does to every song already written.

## 130. An `A` inside a table does not replace the run, and a `Z` does not remember what it rolled *(the lane reading is corrected by §131)*

`SAMESONG`'s wave instrument `02` runs table `11`, whose row 0 carries `F 00` in CMD 1 and `A 02`
in CMD 2, row 1 a `Z 10` in CMD 1, and row 2 an `H 01` that hops back to it. §122 had an `A` restart
**every** lane on the new table, which put ChipBoy's CMD 1 lane on table `02` -- where it found no
`F` to re-roll and no `Z` at all. The channel played the instrument's plain four-frame run on every
note and never moved off it; the ROM jumps a whole sixteen-frame group on half of them.

Measured on 9.2.L, phrase `05` on `WAV` with its cells stripped to notes, reading the wave-RAM
writes as an index into the bank's flat 256 frames, and the wave level off `NR32`:

```
table 11 as it stands   frames  20 | 20 30 35 3A 3F | 20 30 35 3A 3F | ...   NR32 also 20 C0 per note
the A 02 removed        frames  20 | 20 30 35 3A 3F | 20 30 35 3A 3F | ...   NR32 only 20
the Z 10 removed        frames  20 | 20 25 2A 2F    | 20 25 2A 2F    | ...   NR32 also 20 C0
```

Both lanes are running at once. Taking the `A` away leaves the frames alone and takes the second
`NR32` write with it -- so table `02`, which the `A` starts, is what writes the level; taking the
`Z` away leaves the level alone and takes the `+16` frame jumps with it -- so table `11`'s CMD 1
lane is still walking its own rows while CMD 2 walks the `A`'s. **A lane keeps the table it is on
until something in that lane moves it.** Each lane therefore carries its own table slot and its own
clock: a lane an `A` started runs a row a tick (§122), a lane still on the instrument's table
follows the instrument's mode, and a note steps only the lanes in `STEP`. A cell's `A` is unchanged
-- it restarts every lane, which is what §115's `A 20` rests on.

**And a `Z` plays what it rolled without remembering it.** ChipBoy wrote the resolved command back
into the lane's record, so a `Z 10` sitting on an `F 00` recorded `F 10`, and the next pass rolled
from *that*: `F 10` then `F 20`, a step of sixteen frames growing by sixteen every note. Over 45
notes the ROM's jump is `+16` on 24 of them and nothing on 21 -- an even coin on the high nibble's
`0..1`, which is §74's rule, and never `+32`. ChipBoy now matches: 28 of 48, and the first twelve
notes agree one for one before the two random streams part. The record keeps the last command
actually **written** in that lane.

`F` itself was re-checked and is as §92 and §103 have it: it **advances** the frame and does it
every time it runs -- a table of `F 01` rows on a tick-mode instrument walks `20 21 22 23 24 ...`
across the group boundary, `F 10` walks `20 30 40 50 ...` through the whole flat table. It does not
accumulate across notes because a plain note reloads the instrument's own frame 0 first. A **bare**
note does neither: measured with a bare row between two plain ones, the ROM writes no frame and does
not step the `STEP` table, which is what ChipBoy already did.

**Still out:** with the phrase's own `W 20` and `Z 3F` cells back in, ChipBoy's frames follow the
ROM's shape -- both reach the group above, on the same notes to begin with -- but drift apart later
in the phrase, ChipBoy reaching a group the ROM never does. That is the `U`-and-`F` pair under a
randomised `W`, and it wants its own round: the two commands share `frameIdx` and `waveSlot`, and
which of them owns the base an `F` counts from is not measured yet.

## 131. The table an `A` starts runs **beside** the one that started it; `W`'s `y = 0` keeps the length; a bare note steps nothing

Three corrections, all from the same evidence: the user's `SAMESONG` phrases `05` and `06` still did
not land on the ROM's wave frames, and §130's first fix made the channel worse to listen to.

**§130 read the lane wrong.** It had an `A` inside a table row start its table in *that lane only*,
leaving the others where they were. That explains the measurement -- `SAMESONG`'s instrument `02` has
`A 02` in CMD 2 of its table's row 0, and both the `A`'s table and the table that started it go on
having effects -- but it cannot be how the ROM does it, because the `A`'s table has its `E 02` and
`E 03` in **CMD 1**, and those reach `NR32`. A lane cannot be reading table `02`'s CMD 1 and table
`11`'s CMD 1 at once. **Both tables are running, whole.** An `A` inside a table starts a second,
**nested** run beside the first: its own slot, its own three lanes, a row a tick (§122). The table
that started it keeps its own pointers and its own clock. Taking the `A` away leaves the frames
alone and takes the second `NR32` write with it; taking the `Z` away leaves the level alone and
takes the frame jumps with it. Nothing else changes: a **cell**'s `A` still replaces the run
outright, which is what §115's `A 20` rests on, and a note-on clears the nest.

Lane-scoping it silenced the `E`s, which is what the user heard as the channel getting worse. That is
the cost of shipping a model that explains a measurement without being checked against the parts of
the measurement it does not touch.

**§129 read `y = 0` wrong.** It had a run of one step, which never moves. Every `W x 0` in §129's
table was sent to a run that had already reached its end, so "nothing happened" meant nothing was
left to do -- not that the command had stopped it. Sent to a **running** instrument the speed lands
and the run goes on walking:

```
W 20 at the note   ROM  frames 0 5 10 15, ~40 ms apart   the instrument's own four-step run at speed 2
W 50 at the note   ROM  frames 0 5 10 15, ~97 ms apart   the same run at speed 5
W 2F then W 50     ROM  +1 a frame, 40 ms, then +1 a frame at 97 ms
```

The last one settles it: `W 50` kept the sixteen-step run `W 2F` had set and changed only the speed.
So **`y = 0` leaves the length as it stands, exactly as `x = 0` leaves the speed.**

And the run's **loop keeps the frame it returned to, not its step number**. Every wave instrument in
the user's save stores its loop at the run's last step -- "hold the last frame". Lengthen the run
with a `W` and a stored step index points into the middle of the new ladder, which left ChipBoy
oscillating between the last two frames where the ROM holds. The loop step is now re-derived from
the frame it was on, so holding goes on holding.

**A bare note steps nothing.** ChipBoy advanced a `STEP` table on bare notes as well as plain ones.
Measured with a bare row between two plain ones and an `F 10` on the table's rows, the ROM writes no
frame at the bare note at all -- its run just keeps walking. Stepping there fired the `F` again from
wherever the wave had got to, and since a bare note reloads no instrument there was no frame 0 to
count from: the wave walked a whole group further away on every pass, which is the drift §130 left
behind. The spec's bare note (§8) already promised no trigger, no reload and no table restart; it
does not step the table either.

With the three in, `SAMESONG`'s phrase `05` keeps to the two frame groups the ROM keeps to, every
note starting from the instrument's own frame 0 and the run walking inside the group, where before it
climbed a group at a time. What is left between the two is the `Z`'s own dice and one write: the ROM
loads the instrument's frame and then the table's `F` reloads it a few hundred microseconds later,
where ChipBoy folds both into the note's one burst (§3).

## 132. A run that plays **once** goes quiet; an envelope level is held, not rounded; and the frame ladder is an accumulator

Four things, from the user's `READROOM` -- a simpler song than `SAMESONG` and easier to hear one
fault at a time.

### The wave run's `ONCE` writes silence

`READROOM`'s phrase `17` on `WAV` plays instrument `1D`, whose `PLAY` is **ONCE**. On the ROM the
sound stops at the end of the run; in ChipBoy it sustained. Sweeping `PLAY` on that instrument, all
four modes read off the wave-RAM loads:

```
PLAY 0  MANUAL     30                                  loads one frame and holds
PLAY 1  ONCE       30 31 33 34 36 37 39 3B 3C 3E 3F  FLAT
PLAY 2  LOOP       30 31 33 34 36 37 39 3B 3C 3E 3F  33 34 36 …   back to the loop step
PLAY 3  PING-PONG  30 31 33 34 36 37 39 3B 3C 3E 3F  3E 3C 3B …   and back down
```

§65's table of the modes is right. What was missing is the `FLAT`: one step past the end of a `ONCE`
run the ROM writes a wave of **sixteen `77` bytes** -- every nibble 7, a flat line at mid-scale, so
the channel is silent without the DAC going off or the level changing. ChipBoy clamped the run to its
last step and held a real waveform, which is the note that would not stop. It now writes the same
flat frame, once, when the run would step past its end.

### A shaped envelope holds each level for its whole step

`READROOM`'s phrase `05` on `PU1` plays instrument `09`, whose envelope walks `5` down to `0` at
speed 10 -- a level every 27 pitch clocks, 75.4 ms. The ROM steps at **75** and **150** ms after the
note; ChipBoy stepped at **36** and **114**.

The 36 is half a step. `envSegmentLevel` interpolated along the ramp and **rounded**, so a falling
segment reached the next level down when the ramp was half way to it. LSDj holds a level until the
ramp has travelled a whole one. The interpolation is now **truncated** toward the level it started
from, which makes the sampled ramp identical to the ROM's stepping. §27's rounding was ChipBoy's own
choice and had nothing measured behind it.

The second difference was the stage's length. §121 gave `bank::Envelope` a fine byte beside each
stage's tick count, and the driver reads it -- but the **importer never wrote one**, so every stage
was still rounded to a whole tick. Instrument `09`'s 376.9 ms attack became 25 ticks (383.4 ms) and
every level after the first slid late. The importer now carries the fraction, which is what §121 said
it would.

```
                      note   1st    2nd    3rd    4th    5th
ROM                     0     75    150    225    300    375
ChipBoy, before         0     36    114    192    270    348
ChipBoy, after          0     73    151    ...           within 2 ms throughout
```

### The frame ladder is an 8.8 accumulator

§129 left the run's ladder unresolved for `L` of 10 and 12, where `15 * k / L` truncated was one
frame low at one step. `READROOM`'s instrument `1D` is another `L = 10` and measures the same
`0 1 3 4 6 7 9 11 12 14 15`, so the disagreement is real and not a stray reading.

The rule is that LSDj does not divide per step. It divides **once** -- `(frames * 256 - 1) / L`,
truncated, an 8.8 increment -- and then truncates the running total as well, which is not the same
thing:

```
L = 10   increment (16 * 256 - 1) / 10 = 409      409 * k / 256 floored
         0  1  3  4  6  7  9  11  12  14  15      the ROM's, exactly
15 k / L 0  1  3  4  6  7  9  10  12  13  15      one low at the eighth and tenth
```

Every run length read off the ROM -- 2, 3, 4, 5, 8, 10, 12, 15 -- matches the accumulator exactly,
including the ones `15 k / L` already got right. With it, `READROOM`'s phrase `17` plays the ROM's
eleven frames and its flat frame, in order, on every note.

### A note on the two `LENGTH`s

LSDj's wave instrument screen calls the run's length `LENGTH`, stored as `15 - L` in byte 10's low
nibble -- instrument `1D`'s `05` is the `A` the editor shows. ChipBoy calls that **Frames** (it counts
frames, `L + 1`, so it reads 11) and keeps **Length** for the note's own length counter, `NRx1`,
which LSDj's wave instruments do not use. Setting ChipBoy's Length to `0A` therefore does nothing for
this instrument, which is what the user found; the run's `PLAY` is what stops the sound.

## 133. `R`'s volume step applies on the noise channel as well

`READROOM`'s phrase `1A` on `NOI` carries `R F0` on two rows and `R F3` on a third, and the user
heard no `R` at all in ChipBoy. The retriggers were there -- both sides trigger on the same ticks --
but the ROM's retrigger writes a **lower level** each time and ChipBoy's rewrote the same one:

```
row 2, R F0   ROM  NR42 68, trigger; NR42 58, trigger      volume 6 -> 5
              CB   NR42 68, trigger; NR42 68, trigger      no change
```

§76 already had the law -- `x` is a signed nibble, 1-7 up by that much and 9-15 down by sixteen
minus it -- and `retrigVolStep` computes it correctly. The driver simply refused to apply it to
noise: `if (v.retrigStep && !noise)`. Nothing in the design log justifies the `!noise`, and the ROM
contradicts it. Swept on 9.2.L, a noise instrument retriggered once by `R x 0`:

```
from volume 15   x 0-7  15 (clamped)   x 8  resync, no change
                 x 9 -> 8   A -> 9   B -> 10   C -> 11   D -> 12   E -> 13   F -> 14
from volume 4    x 1 -> 5   2 -> 6   3 -> 7    5 -> 9    7 -> 11
```

Both halves, exactly `retrigVolStep`. The guard is gone. `x = 8` is still the resync and still
changes no level, which is what makes `R 8 y` a plain roll.

## 134. `R` fires on its own tick, and a pulse instrument has a `LENGTH`

The user's `READROOM`: `R` "isn't working" on `PU1`'s phrase `3C` and on `NOI`'s `1A` -- "it should
sound like stuttering glitchy beats, but instead they are just normal hits". Two faults, and the
second is the one that makes it sound like nothing is happening at all.

### A pulse instrument's `LENGTH` was never imported

Phrase `3C` row 4 is `note 31 inst 13 R 03`. Instrument `13` is a pulse whose byte 3 is `7B`. The
ROM's note-on writes `NR11 = BB` and `NR14 = C7`; ChipBoy wrote `80` and `87`. The low six bits of
that byte are `NR11`'s length code -- `7B & 3F` = 59, a sound length of five, about 20 ms -- and
**bit 6 enables the length counter**, which is `NR14`'s bit 6. So every retrigger is a 20 ms blip
that cuts itself off: the stutter. ChipBoy played each one as a note that rings until the next row.

Swept on 9.2.L, a pulse instrument's byte 3 against the registers its note-on writes:

```
b3  00 -> NR11 80, NR14 87      3F -> NR11 BF, NR14 87      bit 6 clear: the code is written, nothing arms it
b3  40 -> NR11 80, NR14 C7      7B -> NR11 BB, NR14 C7      bit 6 set: the counter runs
b3  80 -> NR11 80, NR14 87      BB -> NR11 BB, NR14 87      bit 7 is ignored
b3  FF -> NR11 BF, NR14 C7
```

The importer read byte 3 only for **noise** instruments, and there it always marked the length
latent (§87). The same sweep on noise gives the same answer -- `NR41` takes the low six bits and
`NR44`'s bit 6 follows byte 3's -- so §87 saw only instruments with the bit clear, which is the
latent case and not the rule. Both types now read `length = 64 - (b3 & 63)` and
`lengthLatent = (b3 & 0x40) == 0`.

### `R` retriggers on the command's own tick

With the length in, the stutter appeared but a tick out of step. Measured on `PU1` at tempo 150,
the retriggers after an `R` row, in ticks from that row:

```
R 03 on a bare row    ROM  0.14  3.13  6.14  9.15  12.0     ChipBoy  2  5  8  11  14
R 03 beside a note    ROM  0.11(note) 0.17  3.13  6.14      ChipBoy  0(note) 2  5  8
R 00 beside a note    ROM  0.11(note) 0.16                  ChipBoy  0(note) 1
```

The ROM fires a retrigger **on the tick the command is read**, then every `y` ticks; ChipBoy counted
`y` ticks from the command and so missed the first one and ran a tick early ever after. `y = 0` is
not a special case at all -- it is the immediate retrigger with nothing to repeat, which is what §76
described as "retriggers once". The voice now keeps `retrigNext`, the absolute tick the next one is
due on, and the command fires one as it is read.

Beside a note the ROM emits **two** triggers a fraction of a millisecond apart -- the note's own
burst and then the `R`'s -- so a cell's `R` is flushed after the burst, exactly as §127's `S` is.

With both in, `READROOM`'s phrase `3C` and phrase `1A` match the ROM trigger for trigger.

## 135. A `G` puts a groove in force on its channel: it changes how long the rows last, and it walks

`READROOM`'s song row `04` falls apart in ChipBoy a few seconds in, and not because of `H`:
measured against the ROM, the hops are exact. The chains of that row, their phrases' play
orders and what ChipBoy made of them:

```
PU1 0D  3C 3E 3C 40 41 42 3C 44 3C 3E 3C 48      3C: 16 positions, G 00 at row 0
PU2 1A  45 45 46 46 45 27                        45/46: H 71 at row 5 -> 44 positions, G 03 at row 0
WAV 7F  FE FE FE FE FE FE                        FE: H 10 at row 6 -> 22 positions
NOI 1B  89 59 59 59 47 59                        89/59: H 00 at row B -> 11 positions, G 0C at row 0
```

`READROOM`'s grooves 1 to C are single entries of 1 to C ticks -- the composer's speed slots --
so `G 03` asks for rows of three ticks and `G 0C` for rows of twelve, against the straight six.
Taken from the same save, the first note of each `PU2` row:

```
ROM  20  64  112  156  204  248  296  ...   (46 ms apart: three ticks at tempo 163)
CB    0  46   92  138  184  230  276  ...   (46 ms apart)
```

ChipBoy lays the steps *inside* the row at the groove in force and agrees for the first seven --
and then the row's own length, 44 positions of **six** ticks, runs out of steps at half time and
the channel waits through the other half. `PU2` reaches its second chain row at 4049 ms where the
ROM reaches it at 2023 ms, and two seconds in the channels no longer line up. The noise channel
fails the other way: `G 0C` lays twelve-tick steps into a row of eleven six-tick positions, so
positions 6 to A fall past the row's end and never fire.

This is §9.2's "a G re-lays the steps inside the row and never moves the rows", and
`docs/HARDWARE_DRIVER_AUDIT.md`'s row of the same name. Both are wrong. Measured on the ROM at
tempo 150, a sixteen-step phrase `P` (chain row 0) followed by `Q` (chain row 1), each step's
duration in milliseconds, 100 ms being the straight six ticks:

```
no G                 100 100 100 100 100 100 100 100 100 ...        (through P and Q alike)
G 03 at P's step 8   100 100 100 100 100 100 100 100  50  50 ...    (50 ms to the end of Q and past it)
G 0C at P's step 8   100 100 100 100 100 100 100 100 201 201 ...
```

So the groove a `G` names is **in force on that channel** -- from the step that carries it, through
the rest of the phrase, into the next chain row, and on until another `G` -- and what it changes
is how long each step lasts, which is how long the row lasts. It is per channel, not per song: in
the same `READROOM` run `PU2` was running three-tick rows while `PU1` ran six-tick rows.

A groove of more than one entry shows where the groove's own index sits. With groove 7 set to
`2 4 8` (33, 67 and 133 ms):

```
G 07 at P's step 8   100 x8   33 67 134  33 67 134  ...
G 07 at P's step 0    33 67 134  33 67 134 ... 33 [64 134 33] ...
                                        P's step 15 ^  ^ Q's step 0
```

The step that carries the `G` takes the groove's **first** entry, so the `G` restarts the walk; and
`Q`'s first step takes the entry after `P`'s last, so the walk **continues across the row** rather
than starting again. A channel therefore walks one groove: a slot and an index into it, advanced
one entry per position, restarted only by a `G`.

### As built

`GrooveWalk { slot, index }` is that state, `slot` being `kGrooveNone` when no `G` has spoken.
`stepStartTicks()` takes it by reference, applies each position's `G` as it reaches it, and leaves
it as the row ends; `phraseTicks()` the same. The walk is **static** -- the play order is fixed
(§102) and the `G`s are in cells -- so `buildRowTables()` threads one walk down each channel's
chain, storing the row lengths as before and, beside them, `Song::rowWalk[ch]`, the walk as each
row begins. The Player starts a row's layout from that, so a locate into the middle of a song
lands on the same grid the table was built from, and `buildTempoMap()` places a `T` on it too.

Two rules keep every song written before this one sounding as it did:

- A phrase's **own** groove is not the walk: where no `G` is in force the row takes the phrase's
  groove from its first entry, as it always has. Only a groove a `G` put in force walks on.
- A row with no phrase is 96 ticks and leaves the walk alone, as §25 has it.

`G 0` -- ChipBoy's revert, which LSDj cannot write (its groove 00 imports as slot 1) -- clears the
walk back to the phrase's own and restarts the index.

## 136. A retrigger starts the instrument's envelope again, and `R`'s step accumulates from there

With §135 in, `READROOM`'s song row `04` agreed with the ROM on `PU1` and `PU2` trigger for
trigger, and the noise channel still did not. Rendering the row's noise channel from both register
streams through the same APU put the difference in one place: where the ROM has four hits at
1695-2095 ms in every chain pass, ChipBoy has one long silence. Those are phrase `89`'s rows 9 and
A, `R F4` and `R F6`.

The retrigger *times* were right. The volumes were not. Measured on the noise channel at tempo 163,
the volume each trigger sounds at, for an instrument whose envelope **fades** (`b1 = 91`: volume 9,
fade 1) and one that does not (`b1 = F0`: volume 15, fade 0):

```
             env 91 (vol 9, fade 1)            env F0 (vol 15, no fade)
R F4   ROM   9 8 7 6 5 4 3 2 1 0               15 14 13 12 11 10 9 8 7 6
       CB    9 8 0 0 0 0 0 0 0 0               15 14 13 12 11 10 9 8 7 6
R F6   ROM   9 8 7 6 5 4 3 2 1 0               15 14 13 12 11 10 9 8 7 6
       CB    9 8 0 0 0 0 0 0 0 0               15 14 13 12 11 10 9 8 7 6
R 04   ROM   9 9 9 9 9 9 9 9 9 9               15 15 15 15 15 15 15 15 15 15
       CB    9 9 0 0 0 0 0 0 0 0               15 15 15 15 15 15 15 15 15 15
```

The same on `PU1`. So a retrigger **starts the instrument's envelope again** — it is a note-on in
everything but the note, as `docs/HARDWARE_DRIVER_AUDIT.md` already says of an instrument reload —
and `R`'s `x` step then accumulates over the retriggers from that starting volume: `R F4` is
9, 8, 7, 6 and not 9, 8, 8, 8, while `R 04`, which steps by nothing, is 9 every time however long
the note has been fading. ChipBoy added the step to the level the software envelope had already
reached, so the two agreed exactly while nothing was fading (the `F0` column, which is what §133
and §134 were measured on) and every retrigger of a fading instrument was silent.

### As built

`Driver::retrigger(ch, full = true)` restarts the shaped envelope the way a note-on and an
instrument load do — `shapedTick`, `shapedClocks`, `shapedPosMax`, `shapedFrom` to zero,
`shapedTaken` and `shapedRelease` clear, the level read back out of `shapedLevel()` — and then
applies `retrigStep * retrigCount`, `retrigCount` being which retrigger this is since the `R`
was read. It was already a field, reset by the `R` and by a note-on and incremented nowhere.
`Voice::retrigBase` is the level the step counts from, so it is not added to itself: the
envelope's start where there is one, and otherwise the level the `R` was read at.

Re-measured with it in, every case but the fast roll agrees with the ROM to the volume step:

```
             env 91 (vol 9, fade 1)       env F0 (vol 15, no fade)
R F4   ROM   9 8 7 6 5 4 3 2 1 0          15 14 13 12 11 10 9 8 7 6
       CB    9 8 7 6 5 4 3 2 1 0          15 14 13 12 11 10 9 8 7 6
R 04   ROM   9 9 9 9 9 9 9 9 9 9          15 15 15 15 15 15 15 15 15 15
       CB    9 9 9 9 9 9 9 9 9 9          15 15 15 15 15 15 15 15 15 15
```

### Left measured but not settled

`R` with `x = 8`, the fast roll on the pitch clock (§90), does **not** fit this: on the fading
instrument the ROM gives 9, 5, 0, 0 at 14 ms apart where a restart would hold 9, and it emits one
burst at the command where the other `x` values emit two. `READROOM` uses `R F4`, `R F6`, `R D0`,
`R 04` and `R 06` and no `x = 8`, so the fast roll keeps §90's behaviour until it is measured
properly.

## 137. The groove in force before any `G` is LSDj's **groove 0**, not a hard straight six

§135 settled what a `G` does; this is what is in force before one. Probing `R` at tempo 163 with
LSDj's groove 0 set to a single entry of twelve ticks and no `G` anywhere, the phrase's rows came
184 ms apart on the ROM and 92 ms apart in ChipBoy -- the straight six. LSDj's groove 0 is an
ordinary editable groove and it is the one a phrase uses when nothing has changed it; ChipBoy's
slot 0 is a hard-coded straight six that is not editable (§9.2), and the importer left every
phrase pointing at it.

The importer already copies LSDj groove *g* into ChipBoy slot *g + 1* -- that is what makes
`G 00` import as `G 1` -- so the fix is for an imported phrase's own groove to be **slot 1**, the
same groove `G 00` names. `READROOM`'s groove 0 is `06 06`, so nothing there moves; a song whose
groove 0 is anything else played at the wrong speed from its first row.

### As built

`LsdjSong.cpp` sets an imported phrase's `groove` to **1** where it set 0. `Groove` slot 0 stays
ChipBoy's own straight six for a song written here, so nothing native moves; the importer's
`grooves()` already writes `6 6` into a slot LSDj left empty, so a song with no groove 0 at all
imports exactly as it did. §135's walk then starts where the ROM starts: the phrase's own groove,
which for an imported song is LSDj's groove 0, until the first `G`.

## 138. An `E` on a channel whose LENGTH counter is on **does** trigger

§136 left one trigger of `READROOM`'s noise channel unaccounted for: the ROM triggers twice at
phrase `89`'s row 2 (`note 34`, instrument `15`, `E 35`) and once at its row 3 (no note, `E 24`),
where ChipBoy triggers once and none. The first guess -- that `E` retriggers on the noise channel --
is wrong, and `probe/vs_Enoi.py` measured it wrong five ways: `E 35`, `E 24`, `E 30`, an `E` on the
note's own row, and the same on `PU1`, all one trigger on the ROM and in ChipBoy alike.

What those probes had in common is an instrument with **no LENGTH**. `READROOM`'s instrument `15`
carries `b3 = 6F`: length code `2F` with bit 6, the counter enable, **set** (§134). Sweeping byte 3
against the same `E` (`probe/vs_Elen.py`, `vs_Elen2.py`), triggers counted:

```
b3 = 00   no length at all          E two rows later     ROM 1
b3 = 6F   length 17, counter ON     E two rows later     ROM 2      <- the E triggers
b3 = 2F   the same code, counter off E two rows later    ROM 1
b3 = 6F   length 17, counter ON     no E at all          ROM 1
b3 = 6F   length 17, counter ON     E on the note's row  ROM 2, both at t = 0
b3 = 6F   on PU1                    E two rows later     ROM 2
b3 = 2F   on PU1                    E two rows later     ROM 1
```

So it is the **counter enable bit**, not the length value, and not whether the counter has expired:
an `E` on the note's own row triggers twice before the counter can have run out. It is both pulse
and noise. And it is the `E` **command**, not a level change in general -- with the counter on, a
table's volume column still does not trigger, while a table's `E` triggers on every row that
carries it:

```
b3 = 6F   a table's VOLUME column   ROM 1
b3 = 2F   a table's VOLUME column   ROM 1
b3 = 6F   a table's E command       ROM 6: 0, 62, 308, 553, 799, 1044 ms
```

§26 says a level change is a zombie-mode write and never a trigger, and that holds for everything
except this: **an `E`, from a cell or from a table, triggers the channel when the instrument's
LENGTH counter is enabled.** The reason is visible from the hardware: a length counter can have
switched the channel off at any moment, and the driver cannot read back that it has, so a zombie
write to such a channel may land on a dead channel and do nothing. Re-triggering always is the
cheap correct answer, and it is what LSDj does.

`docs/HARDWARE_DRIVER_AUDIT.md` listed "the length counter disabling a channel" as a thing that
stays because modelling it means modelling the 256 Hz frame step in the driver, for one silent case.
That was the wrong shape of the problem: nothing needs to know *whether* the counter has expired,
only that it is **enabled**, which is a bit of the instrument. And the case is not silent -- it is
four noise hits a bar in `READROOM`.

### As built

`Driver::retrigger(ch, full, restartEnv = true)` takes a third argument: §136's envelope restart is
what an `R`'s retrigger wants and not what this one wants, because the trigger here must carry the
level the `E` just set. Measured on the ROM, the burst's `NRx2` is the `E`'s own volume. So the `E`
case calls `retrigger(ch, true, false)` after `setLevel()`, under
`v.inst.length && !v.inst.lengthLatent` -- the same test `lengthBit()` makes -- on pulse and noise,
leaving the wave channel (whose level is `NR32` and needs no trigger on any version) alone. §59's
pre-8.8 `envRetrig` path keeps its own full retrigger. Beside a note it is flushed after the note's
own burst through `retrigPending`, which §134 added for `R` and which now says *which* kind of
retrigger is owed. Re-measured with it in, every case agrees with the ROM:

```
                                          ROM   CB
b3 = 00  no length         E two rows on    1     1
b3 = 6F  counter ON        E two rows on    2     2
b3 = 2F  counter off       E two rows on    1     1
b3 = 6F  counter ON        no E             1     1
b3 = 6F  counter ON        E on the note    2     2   (both at t = 0)
b3 = 6F  counter ON        E on two rows    3     3
```

## 139. A table row's `O` lands **after** the note's own pan, not folded into it

From the user, on `READROOM`'s phrase `1A`: "in LSDj the retrigger note on row C is centered, but in
ChipBoy it's falling on the left pan in the panning sequence." Their guess was the STEP table's
index. The index is right; the **order of two writes in one tick** is not.

Phrase `1A` plays instrument `0C`, a noise instrument in **STEP** mode (byte 5 bit 3) whose table is
a pan sequence: `O 01` (left), `O 02` (right), a blank row, then `H 00` hopping to row 0 for ever.
STEP advances it one row per trigger, so the hits walk left, right, the instrument's own pan, and
round again. Probed with the same table and five plain note-ons, the register order per note:

```
ROM   NR43=40  TRIG  NR51=FF          <- the note sounds at the instrument's own pan
      (+2 ms)  NR51=F7                <- then the table row's O pans it left
CB    NR51=FF  NR51=F7  NR43=40  TRIG <- the note already sounds panned left
```

Both writes are inside the same tick -- 2 ms apart, where a tick at tempo 163 is 15.3 ms -- so this
is not a tick's delay. LSDj's note pass writes the mixer and triggers; its table pass, right after,
writes the row's `O`. ChipBoy fires the table's first row *inside* the note-on (§31) and `O` wrote
`NR51` there and then, so the note's own pan write carried the **table's** pan instead of the
instrument's and the attack was already panned. On a noise hit a few milliseconds long the attack is
the whole sound, which is what the user heard.

With an `R` on the row it is plainer still, and it is exactly phrase `1A`'s row C:

```
ROM   note at 390 -> NR51=FF (centre),  the R's retrigger at 392 -> NR51=7F (right)
CB    note at 368 -> NR51=7F (right),   retrigger -> 7F
```

§31 already says what should happen: inside the note-on the row "only changes the running state, so
the note's own writes carry its transpose, its level and its commands". Transpose and level obey it;
`O` did not.

### As built

`Cmd::O` inside a note-on stores the pan in `Voice::panQueued` (0 for none, the pan plus one
otherwise) and writes nothing. `startVoice()` flushes it after its own `writeNr51(true)` and after
§134's owed retrigger -- the order the ROM writes them in, measured with `R` on the row. Re-measured
with it in, the pan each trigger sounds at, `R F0` on the second row:

```
ROM   0/C  368/L  370/C  737/R  1105/C
CB    0/C  368/L  368/C  736/R  1104/C
```

The note on the `R`'s row is `L` and its retrigger `C` on both sides now, where ChipBoy had `R` for
both. That is the user's "row C is centred in LSDj".

## 140. A STEP table's position belongs to the **instrument**, and starts at its first row

§139 put phrase `1A`'s pan on the right step of the sequence for a run of notes on one instrument.
The user then named the whole sequence: instrument `0C`'s table is `O 01`, `O 02`, a blank row and
`H 00` back to row 0, so the pan walks **left, right, the instrument's own centre**, and in
`READROOM`'s phrase `1A` "row 0 and 8 are left, 2 and A are right, 6 and C are centered" — with the
instrument `0D` note on row 4 not disturbing it. And: in LSDj the position resets when play is
pressed, so the first articulation is always the table's first row.

(Phrase `1A` carries `H 10` at row 8, so rows 0–7 play twice before rows 8–E; three `0C` notes a pass
against a three-row cycle keeps the phase, which is why rows 0 and 8 agree.)

Measured with a phrase of that shape — instrument A at rows 0, 2, 6, 8, A, C and B at row 4, A's
table the same `O 01` / `O 02` / blank / `H 00` — the pan each note settles at:

```
                              row0  row2  row4  row6  row8  rowA  rowC
ROM, with B on row 4           L     R     (B)   C     L     R     C
CB   "                         L     R     (B)   L     R     C     L
ROM and CB, with no B at all   L     R      -    C     L     R     C
```

So the sequence is LSDj's until another instrument plays on the channel, and ChipBoy restarted the
table there: it keeps one position per **channel** and reset it whenever the table slot changed.
Two instruments sharing one table settle it — alternating A B A B A B:

```
one table, shared       ROM  L L R R C C      each instrument walks its own
two identical tables    ROM  L L R R C C
CB, one table                L R C L R C      one counter per channel
CB, two tables               L L L L L L      reset on every table change
```

The ROM pairs the pans, so **the position is the instrument's, not the table's and not the
channel's**: A's first note is its table's row 0 and B's first note is row 0 too, then both take
row 1, then row 2. ChipBoy was wrong in both directions at once.

### As built

`Driver::stepState_[ch][key]` parks `tableStep`, `tableStep2`, `tableStepE`, `tableRow`,
`tableRow2`, `tableRowE` and the table slot they belong to, `key` being the voice's `instKey` — the
bank slot, or the mark a local instrument gets (§8). A note-on parks the position of whatever
instrument was playing and takes up this one's, starting at row 0 when that instrument has none
saved or when its saved position belongs to another table (a `tableOverride` can point the same
instrument at a different one). A non-STEP table still starts at its first row, as before.

It is kept per channel as well as per instrument, which nothing here measures: one instrument
sounding on two channels at once keeps a position on each rather than interleaving them, which is
the conservative reading.

`allNotesOff()` clears that channel's positions, so the transport stopping puts every STEP table
back to its first row — LSDj's reset on play, and what makes the first articulation of a song
deterministic.

## 141. How long a tick is comes from the tick boundaries, not from counting the clocks inside the last one

From the user: "the envelope doesn't scale the same way with tempo. If I set tempo to T51 (81bpm) and
match that in ChipBoy, the noise notes in this same phrase sound a lot different."

The ROM's envelope is **tempo-independent**. Measured on the noise channel, the channel volume every
4 ms from the note, for instrument `0C`'s envelope (`b1 = 62`) and instrument `15`'s (`71`):

```
                 env 62                      env 71
ROM  T163   6 5 4 4 3 2 1 1 0           6 4 3 2 0
ROM  T81    6 5 4 4 3 2 1 1 0           6 4 3 2 0      <- byte for byte the same
CB   T163   6 6 5 4 3 2 1               7 6 4 2
CB   T81    6 5 3 3 1 1 1 1             6 4 1          <- faster, and a different shape
```

The importer is not at fault: it converts the ROM's real-time stage (its period table times the
pitch clock) into ticks with the song's own tempo, and dumping what it produced gives the same real
duration at both tempos -- `env 62` is `2 + 47/256` ticks at T163 and `1 + 22/256` at T81, both
33.5 ms.

The fault is the driver's sub-tick interpolation. §116 puts the envelope's position in 1/256 of a
tick, reading the fraction from `shapedClocks * 256 / clocksPerTick_`, and `clocksPerTick_` was
**counted from the pitch clocks that fell inside the previous tick** -- starting from a hard-coded 7.
The true figure is about 5.5 clocks a tick at T163 and 11 at T81, so the first tick of a session
interpolates 27% fast at one tempo and 57% fast at the other, and `shapedPosMax` ratchets the error
in permanently. Playing the same phrase and looking at successive notes shows exactly that shape:

```
                 ROM                 CB
T163 note 1   6 5 4 4 3 2 1 1     6 6 5 4 3 2 1 0
T163 note 2   6 5 4 4 3 2 1 1     5 5 4 4 3 2 2 0     <- close, once the count has been made
T81  note 1   6 5 4 4 3 2 1 1     6 5 3 3 1 1 1 1     <- badly wrong
T81  note 2   6 5 4 4 3 2 1 1     6 6 4 4 3 2 2 1
```

A whole envelope of this shape is about two ticks long, so the first one is most of it.

There is a second error in the same line of arithmetic, and it is why fixing the first alone changes
nothing measurable: `clocksPerTick_` was an **int**. A tick is 5.52 pitch clocks at tempo 163 and a
whole number cannot be, so `shapedClocks * 256 / 5` reached 256 on the sixth clock of a tick that has
5.52 -- clamped, so every tick's interpolation ran about 9% fast even once the count was right. That
is the residue visible on the notes after the first.

### As built

Three things, and all three are needed:

- The tick's length is carried in **cycles**, `tickCycles_`, not in whole pitch clocks. The fraction
  is `shapedClocks * kPitchCycles * 256 / tickCycles_` in `subOfTick()`, so nothing is rounded but
  the final 1/256.
- It is measured from the tick **boundaries**, which the driver already has: the next one in the
  block when there is one -- the exact length of the tick about to run -- and otherwise the gap back
  to the boundary before it, kept across blocks in `lastTickCycle_` and `lastTickIndex_`. Both are
  exact where the old tally was quantised and a tick late, and both are known before the tick they
  describe rather than after. `clocksThisTick_` goes.
- The very first tick of a session has no boundary either side of it, and that is the tick a song's
  first note lands on -- which is why the boundary measurement alone left the numbers above
  unchanged. `Driver::setTickRate(ticksPerSecond)` takes it from the caller's clock instead
  (`bpm * kTicksPerBeat / 60`), called each block by the plugin and by `recordtest`'s trace. A caller
  that never says is measured from the boundaries from the second tick on, as before.

Re-measured with all three in:

```
                  ROM              before            after
env 62  T163   6 5 4 4 3 2 1 1   6 6 5 4 3 2 1     6 6 5 4 3 2 1 1 0
env 62  T81    6 5 4 4 3 2 1 1   6 5 3 3 1 1 1 1   6 6 5 4 3 3 2 1 0
env 71  T163   6 4 3 2 0         7 6 4 2           7 6 4 2 0
env 71  T81    6 4 3 2 0         6 4 1             7 6 4 3 0
```

ChipBoy is the same at both tempos now, which is what the report was about: `env 62` ran twice as
fast at T81 as at T163 and does not any more.

### Left measured, not settled

The shape still starts about one sample late against the ROM -- `6 6 5` where the ROM has `6 5 4`,
and `env 71` opens at 7 where the ROM opens at 6, so the ROM has taken its first step before
ChipBoy has. That is an offset at the envelope's **start**, of the order of one pitch clock, not a
rate: the decay's slope matches and both tempos now agree with each other. It wants its own
measurement of where the ROM starts counting from a note.

## 142. The envelope's position needs finer than 1/256 of a tick, or a step slips a whole pitch clock

§141 left the shape starting late. Timed exactly -- each zombie triple on `NRx2` is one level, so its
own write says when the level landed -- the envelope's steps from the note, on noise and on `PU1`
alike:

```
env 62 (volume 6, fade 2: a level every 2 pitch clocks, 5.58 ms)
ROM   5.2  10.8  16.3  21.9  27.5  33.1
CB    8.4  13.9  16.7  22.3  27.9  33.5
env 71 (volume 7, fade 1: every clock, 2.79 ms)
ROM   2.4   5.2   8.0  10.8  13.6  16.3  19.1
CB    5.6   8.4  11.2  13.9  14.0  16.7  19.5
env F4 (volume 15, fade 4: every 4 clocks, 11.2 ms)
ROM  10.8  21.9  33.1  44.2  55.4  66.6
CB   13.9  22.3  33.5  44.7  55.8  67.0
```

The **spacing** is right everywhere -- 5.6, 2.8, 11.2 -- so the rate is not in question. What is wrong
is that the first step lands one whole pitch clock late, and then one short step at the first tick
boundary pulls the rest back into line (`env 62`: 13.9 to 16.7 is 2.8 ms where every other gap is
5.6; `env 71` shows it as the pair 13.9 and 14.0).

It is a knife-edge in the arithmetic. `env 62`'s decay is stored as `2 + 47/256` ticks, 559 of §116's
1/256-tick units, over six levels -- so a level boundary is at 559/6 = **93.17** units. Two pitch
clocks are worth 93.2 units at tempo 163. `subOfTick()` returned an integer, so 93.2 became **93**,
a hair under the boundary, and the step had to wait for the third clock. Every stage whose level
boundary falls just above a whole number of these units loses its first step the same way; 1/256 of a
tick is 0.06 ms of resolution holding a decision worth 2.79 ms.

### As built

The **runtime** position goes to 1/65536 of a tick (`kShapedPos = kShapedFine * 256`) while the bank's
stages keep their tick-plus-1/256 form: `stagePos()` scales a stored stage up, `subOfTick()` returns
the finer unit, and `shapedTick`, `shapedPosMax` and `envSegmentLevel()`'s two arguments are all in it.
93.2 against 93.17 then decides the way the ROM does. Nothing about the stored envelope changes, so no
song moves but the one step.

Re-measured with it in, every step lands on the pitch clock the ROM's does and the catch-up step is
gone:

```
env 62   ROM  5.2  10.8  16.3  21.9  27.5  33.1       env F4   ROM  10.8  21.9  33.1  44.2
         CB   5.6  11.2  16.7  22.3  27.9  33.5                CB   11.2  22.3  33.5  44.7
env 71   ROM  2.4   5.2   8.0  10.8  13.6  16.3  19.1
         CB   2.8   5.6   8.4  11.2  13.9  16.7  19.5
```

### Left measured, not settled

Every step is still 0.4 ms later than the ROM's, evenly -- `env 62` at 5.6 against 5.2, 11.2 against
10.8. That is where the two count **from**, not how fast they count: LSDj's handler starts its
envelope when the interrupt begins and writes the note's registers a few hundred microseconds into
it, which is where this trace's `t = 0` sits. 0.4 ms on a 5.6 ms step, and it would take measuring the
ROM's interrupt entry rather than its first register write to confirm.

## 143. `R` with `x = 8` starts its roll on the pitch clock, with no retrigger as the command is read

§136 left this open: the fast roll (§90) did not fit "a retrigger starts the instrument's envelope
again". Measured across `y`, on a fading instrument (`env 91`) and a flat one (`env F0`), the
retrigger times from the note and the volume each sounds at:

```
R 80   ROM  0/9   2/9   5/8   8/7  11/6  14/5       every pitch clock
       CB   0/9   0/9   3/8   6/7   8/6  11/5
R 81   ROM  0/9   5/8  11/6  16/4  22/2  27/0       every two
       CB   0/9   0/9   6/7  11/5  17/3  22/1
R 84   ROM  0/9  14/5  27/0  41/0                   every five
       CB   0/9   0/9  14/4  28/0  42/0
R 88   ROM  0/9  25/1  50/0  75/0                   every nine
       CB   0/9   0/9  25/0  50/0  75/0
```

Two of the three things §136 wondered about are already right. The **interval** is `y + 1` pitch
clocks -- one clock at `y = 0`, nine at `y = 8` -- and ChipBoy matches it. The **level** is simply the
instrument's envelope where it has got to, not reset: `R 81` on `env 91` sounds 9, 8, 6, 4, 2, 0,
which is the fade running on underneath, and ChipBoy does that too because the fast roll goes through
`retrigger(ch, false)`, which §136's restart never touched. So there was never a conflict with §136 --
only this:

**ChipBoy fires one retrigger as the command is read and the ROM does not.** It is there in every
row above as the doubled `0/9`, and in the counts over a second: 360 against 351, 181 against 176,
121 against 117, 73 against 71, 41 against 39. §134 measured a cell's `R` firing on its own tick and
that holds for every other `x`; the fast roll starts on the pitch clock instead, `y + 1` clocks after
the command, with nothing at the command itself.

### As built

The `R` case fires its immediate retrigger only when the roll is not the fast one, and owes none
through `retrigPending` either. `retrigOn`, `retrigNext` and the pitch-clock counter are untouched.

Re-measured with it in, the doubled trigger is gone and the times line up:

```
R 88   ROM  0  25  50  75  100   (39 over a second)    CB  0  25  50  75  101   (40)
R 84   ROM  0  14  27  41   55   (71)                  CB  0  14  28  42   56   (72)
R 80   ROM 351 over a second                           CB  359
```

The few extra over a second are the pitch clock's own 0.07%: `kPitchCycles` is 11712 where the ROM's
music clock is 11704, which `docs/HANDOFF.md` has separately.

## 144. A nested table run's transpose column counts too, and adds to its parent's

`READROOM`'s noise channel was the last thing on song row `04` not matching, and the note about it --
"the table steps a tick early" -- was wrong about the cause. The cluster it disagrees on is table
`0x20`, which table `05`'s row F calls with an `A 20`, and `0x20` is nothing but a transpose column.
Probed with a table whose only content is an `A`, calling one whose rows carry transposes, counting
the retriggers a noise transpose makes:

```
the transposes in the instrument's own table   ROM 5, at ticks 0.0  0.1  16.1  32.1  48.1
                                              CB  5, at ticks 0.0  0.2  16.0  32.0  48.0
the same transposes in the table an `A` calls  ROM 5, at ticks 0.0  0.9  17.0  33.0  49.0
                                              CB  1, at tick  0.0
```

So a **nested run's transpose column never reaches the note**: `tableTransposeOf()` reads
`v.tableSlot` and `v.tableRow`, the parent's, and §131 gave the nested run pointers of its own that
nothing consults for this. The ROM's first nested row lands one tick after the row carrying the `A`,
which is where `READROOM`'s apparent one-tick offset came from -- ChipBoy was reaching a
similar-looking cluster by another route rather than stepping this one early.

What happens when both tables carry one, `NR43` on noise:

```
parent / nested      ROM                          CB
none / +12           14:DC  31:40  (every 16)     nothing
+4   / none           1:30                         3:30  15:40
+4   / +12            1:30  14:DA  31:30           3:30  15:40
+12  / +4             1:DC  14:DA  31:DC           3:DC  15:40
```

`+12` alone is `DC` and `+4` alone is `30`, while `+4` with `+12` and `+12` with `+4` are both `DA` --
the clock for +16. **They add**, and the nested one applies on the tick its own row is on.

> **§145 narrows this.** The sum is real but it is the **noise** channel's alone: on PU1, where a
> period cannot wrap the way this map does, the called table's row is in force by itself and the
> parent's column is not read. What stays added on noise is the parent column's value on the tick the
> `A` ran, not its live row. The tick this section left open is settled there too.


### As built

`tableTransposeOf()` adds the nested run's row to the parent's, reading `nestSlot` and `nestRow[1]`
the way it reads `tableSlot` and `tableRow` -- lane 1 being the one the row's own columns belong to,
as for the parent.

### Left measured, not settled -- both answered by §145

Where a transpose **reverts** differed: with the parent holding one on its row 0 and nothing after,
the ROM wrote `NR43` once and held it where ChipBoy wrote it and then put it back on the next row
(`3:30  15:40` against the ROM's `1:30` alone). It is not a rule about the row after a transpose --
the column reverts per row on both channels with no `A` in sight. It is the parent's column being
held on noise once an `A` has run (§145).

The nested run's **first** row is also still a tick early. With the transpose reaching the note, the
five retriggers land on ROM ticks `0.0 0.9 17.0 33.0 49.0` against ChipBoy's `0.0 0.2 16.0 32.0 48.0`:
the ROM holds the row carrying the `A` for a whole tick before the called table's row 0 has any effect,
where ChipBoy starts it a fifth of a tick in. Gating the transpose on `nestJustStarted`, which is what
holds the nested *lanes* back for that tick (§122), does **not** fix it -- measured, the first effect
moved to tick `0.0`, marginally worse -- so whatever delays the nested run's first row in the ROM is
not that flag, and it is written down rather than guessed at.

## 145. The transpose column belongs to the run that is live, and on noise the `A`'s row stays added

§144 added the nested run's transpose column to the parent's and called it "they add", on four
readings of `NR43` taken at the top of the noise map, where +12 from the probe's note lands in the
7-bit region (`DC`, `DA`) and two different sums can print the same byte. Re-measured on **PU1**,
where a period is a number and not a wrap, the general claim is wrong. The periods for the probe's
note: base 1943, +4 1964, +8 1982, +12 1995, +16 2006; ticks from the note.

First the control, a table with no `A` in it at all -- the column is the **row's own** and a row
without one puts the note back, which is what ChipBoy already did:

```
rows 0 +4               ROM 0.1:1964  1.0:1943  16.1:1964  17.0:1943
rows 0 +4, 4 +12        ROM 0.1:1964  1.0:1943  4.0:1995  5.0:1943
rows 0 +4, 1 +4         ROM 0.1:1964  2.0:1943          -- one write, held across both rows
```

Then the same table with an `A` on row 0:

```
parent 0 `A`+4, 4 +8 / called 0 +12    ROM 0.1:1964  1.0:1995  2.0:1943  17.0:1995  18.1:1943
the same `A` in CMD 2 instead of CMD 1 ROM 0.1:1964  1.0:1995  2.0:1943  17.0:1995  18.1:1943
parent 0 `A`+4 / called empty          ROM 0.1:1964  1.0:1943          -- and nothing more
parent 0 `A`+4 / called 0 +4           ROM 0.1:1964  2.0:1943  17.0:1964  18.1:1943
```

Three things at once. The called table's row 0 is **alone** in force at tick 1 -- 1995 is +12, not
the 2006 a sum would give. The parent's column is then not read at all: its +4 is gone by tick 2
(1943, the bare note) and its row 4's +8 never arrives, where §144's code would have re-applied row
0's +4 every sixteenth tick. And the column moves whichever CMD column the `A` sat in -- CMD 1 and
CMD 2 print the same trace -- so this is about which *run* the column is read from, not about lanes.
`parent +4 / called +4` is the tidiest of the four: one write at tick 0 and the next at tick 2, so
tick 1's +4 came from the called table and matched what the parent's row had already written.

Now the same four on **noise**, on a note low enough to keep the map monotonic (base `B0`, +4 `A0`,
+8 `90`, +12 `80`, +16 `70`):

```
plain 0 +4, 4 +12, 8 +16               ROM 0.1:A0  0.9:B0  4.0:80  4.9:B0  8.0:70  8.9:B0  16.0:A0
parent 0 `A`+4, 4 +8 / called 0 +12    ROM 0.1:A0  0.9:70  2.0:A0  17.0:70  18.0:A0
parent 0 `A`+4 / called empty          ROM 0.1:A0                  -- and nothing more
parent 0 `A`+4 / called 0 +4           ROM 0.1:A0  0.9:90  2.0:A0  17.0:90  18.0:A0
```

The control reverts exactly as the pulse channel's does, so nothing is sticky about the column
itself. But with an `A` the parent's +4 **stays**: `A0` at tick 2 where the pulse channel gave the
bare note, `70` (+16) at tick 1 where it gave +12, `90` (+8) where `+4 / +4` gave no write at all,
and `A0` held for six hundred milliseconds against an empty called table. So §144's sum is real and
it is noise's alone.

Two readings of that fit every byte here and cannot be told apart by these probes: the parent's
column is frozen on the row the `A` ran and goes on being added, or the noise **note** absorbs the
transpose in force at that moment (the noise map needs an absolute index where a period takes an
offset, which is a reason for the asymmetry to exist at all). ChipBoy takes the first, as the
smaller change.

The third thing, and what §144 left open: the called table's row 0 lands on the tick **after** the
`A`'s, not on it -- `0.9` against §144's `0.2`. §122 already had that for the nested *lanes*; the
column was read straight off `nestRow[1]`, which `beginNestedRun` had already set to 0, so it
applied a tick early and §144's guess at `nestJustStarted` could not help: that flag is cleared on
the `A`'s own tick, before the end-of-tick period write.

### As built

`tableTransposeOf()` does not read the parent's column at all while a nested run is live. Until that
run reaches its first row the value in force is `Voice::nestTspHeld`, the parent column's value on the
tick the `A` ran -- which is how the `A`'s own row transposes its own tick -- and after it
(`Voice::nestRowLive`, set when the nested lane 1 steps) it is the nested row's, plus `nestTspHeld`
again on noise and nothing on the pitched channels.

Nothing stops the parent's lanes: §131's two runs still walk side by side, and the parent's CMD 1 goes
on re-rolling its `F` beside a nested `A 02`; it is only the transpose column that has one owner at a
time, and it belongs to the nested run whichever CMD column started it.

One thing here is a choice and not a measurement: the parent's row comes round again -- every
sixteenth tick for a table that runs its length -- and reads the same `A` a second time.
`beginNestedRun` now leaves a run that is already on that slot alone, because a restart would put the
called table's row 0 on that tick where the ROM has nothing, and its own loop already lands on the
next one. A parent whose rows loop sooner than the called table's would tell a restart from this; no
song measured has one, and `READROOM`'s `A 20` sits on its table's last row, where the two coincide.

## 146. A bare note has no burst, so what a command owes the burst is owed at once

The user, on `SAMESONG`'s **`WAV` phrase 2F after 2E**: the string of `E` commands does not sound
right. 2F is eight rows of bare notes -- the instrument column blank, §101's pitch change with no
trigger -- each carrying `E 03` or `E 02`, which on the wave channel is 100 % and 50 % of `NR32`
(§108). Traced against the ROM the whole of 2E agrees tick for tick, kills included, and 2F's level
sits a step low wherever a row carries **both** a note and an `E`:

```
phrase 2F, rows 2, 4, 6 (a bare note and `E 03`)   ROM level 1 (100 %)   CB level 2 (50 %)
```

Probed on its own -- `E 02` alone on one row, then `E 03` and `E 02` each beside a bare note:

```
WAV, NR32                     ROM 12:C0 (50 %)  24:A0 (100 %)  36:C0 (50 %)
                              CB  12:40 (50 %)   -- and nothing after it
PU1, NR12 writes per tick     ROM t12: 33  t24: 8  t36: 12      -- 11 down-triples, 8 up, 4 triples
                              CB  t12: 33
PU1 with the LENGTH counter on, triggers (§138)
                              ROM 0  12  24  36
                              CB  0  12
```

So **nothing** a cell's command does to the level reaches the register on a bare note, on any channel,
and §138's trigger is lost with it. The cause is one line in `setLevel()`: it returns without writing
while `inNoteOn_` is set, because §3 folds a note's commands into the note's own burst, which carries
the level anyway. §138's trigger waits for the same burst, in `retrigPending`. A bare note's writes are
the period and `NR51` and nothing else, so both were simply dropped.

The ROM's order at such a row's own tick, read off the trace:

```
NR13 NR14          the bare note's period, no trigger bit  (section 101)
NR10 NR11 NR12 NR13 NR14   the whole note-on again, with the trigger  (section 138)
NR12 x 8           the level's zombie steps, after the trigger, which carries the old level
NR13 NR14          the period once more
```

### As built

The bare-note branch of `startVoice()` remembers the level before the cell's commands run and, once
`inNoteOn_` is back off and the period is written, pays what they owe: the pending retrigger first,
then the level if it changed, which is the ROM's order. `setLevel()` is unchanged -- it is right for a
plain note, where the burst carries the level.

## 147. The third campaign: 9.4.2's code, and the model the pitch commands share

The audit target moved from 9.3.9 to 9.4.2 (the current release; the user's project is written in
it). The command map, the work-RAM map, the order a phrase step and a table tick run their columns
in, and the five letters handled where a row is read are in `docs/LSDJ_COMMAND_MATRIX.md` §11,
read off the ROM with the address of every answer. Two things from it bind the sections below.

**The pitch of a pulse or wave note is three numbers added at every write** (0:`$1BA7`):
the held note, the transposes in force (chain, chord and the table's column, summed into
`$C174` whenever one of them changes), and one **offset** in 1/256 semitone (`$C337 + 2ch`)
that `P`, `L` and, separately, the vibrato move. The offset is what a slide slides. The sum goes
through the note table with linear interpolation (0:`$1B28`) and is written without a trigger.
So a transpose column landing under a slide moves the pitch at once, and a slide never holds a
copy of anything.

**The two-sided rig** (`tools/lsdjref/`, `/root/lsdj/probe/vs_matrix.py` in the container): one
script builds a probe song, traces the ROM and ChipBoy, groups each channel's register writes
into batches a millisecond apart, reduces each batch to the state it leaves (the trigger bit
counted, the length bits masked) and reports the first batch that differs. Nineteen letters in
a phrase cell, a command-only cell, a bare note's cell, a table's CMD 1, CMD 2 and a row after a
transpose, on PU1, WAV and NOI: 200 cases, of which the ones below differed. Differences of
the fold kind -- the ROM triggers on the instrument's values and lets a table's row 0 or a cell's
`R`/`S` land a millisecond later as its own writes, where ChipBoy folds them into the burst
(§3, §124) -- are left as they are: `S` retriggers on both sides, `E`'s zombie walk and the
double trigger are inside a millisecond of the note.

## 148. A bare note's `S` retriggers, and its `W` writes the duty

Measured (ROM `S 21` on a bare note two rows after a plain one, PU1): the bare note's period
goes out, then `NR10` with the new sweep, `NR11`, `NR12` at the level in force with the add bit
(`$606A`: `level << 4 | 8`), the period again and `NR14` with the trigger -- `$4828` accumulates
the byte and jumps to the refresh at `$6058`, which always triggers. `W 01` on a bare note
writes `NR11` with the new duty and nothing else (`$47E5`). ChipBoy wrote the period alone in
both cases: a bare note applies its cell's letters with the burst suppressed (§146), and `S` and
`W` only emit when live.

### As built

The bare-note branch of `startVoice()` notes whether the cell carried an `S` (PU1) or a `W`
(a pulse) and, after the period, pays them: `W` emits `NR11`; `S` emits `NR10` and a full
retrigger without restarting the envelope, which is the ROM's refresh.

## 149. A `C` that lands on a tick that is not the note's plays the root on that tick

Measured: `C 37` on a bare note (and on a table's row 1) gives the root on that tick and +3 on
the next; ChipBoy gave +3 at once. `$4F3A` reads the chord phase, adds the semitone it names,
then advances it -- and the handler at `$476C` only stores the value. ChipBoy advanced the phase
before the first read whenever the voice was past its first tick. Now a `C` set on a voice
already past its first tick marks the chord fresh, and the tick that finds it fresh plays the
root and clears the mark. A `C` beside a plain note is unchanged (the first tick already played
the root there).

## 150. A noise `P`'s first step lands on the tick after its row

`P 20` on a noise note: the ROM writes the note on its own tick and the first step (`S` by the
byte) on the next (`$6653` stores the byte and a flag, and the noise pitch tick reads them a tick
later); ChipBoy stepped on the note's own tick, which is `READROOM`'s row 04 being one step out
(HANDOFF's item 6). Now the step set by a `P` waits one tick.

## 151. `V 00` starts a vibrato when none is running

`$7DEB`: a zero byte stops a vibrato if one is running and otherwise starts one at speed 0 and
depth 0 (9.1.0's note). Measured: `V 00` on a plain note gives a slow swing of one period unit
either side (one cycle every 64 updates). ChipBoy's §126 had the whole-byte zero as "off"
outright; it is now a toggle.

## 152. A slide is the offset beside the transposes; the column stays live under it

Three measurements on 9.4.2 against §68, §71, §110 and §111:

```
table row 0 tsp F4 + L 08, rows 1-2 tsp +4        ROM: slides an octave down at 360 Hz and the +4
                                                  lands on it at tick 1 (65 -> 7B), the slide going on
cell L 30 under a table whose column blips +12    ROM: the blips land during the slide (A0 -> D0 D1 D2 -> A4)
table row 0 tsp +12, row 1 tsp -12 + L 08,        ROM: +12 on the note, then a nine-update slide down
rows 2-15 tsp +12 (the manual's own example)      to the plain note -- the +12 stays, the offset goes to -12
table row 0 tsp +4, row 1 tsp 0 + L 03            ROM: +4 at the note, no slide at all, plain from tick 2
```

The column is not suppressed while a slide runs (§110 said it was; its measurement was of
`SAMESONG` phrase 21 on 9.2.L and is not reproduced by the ROM's code: `$4FBE` writes the
column into `$C347`, sets the refresh flag when it changes, and the 360 Hz update at 0:`$0584`
calls the refresh that adds it), and a slide holds no copy of the column (§71). A **table's `L`**
(`$42CD`) aims the offset at its own row's transpose -- `target = held note + row tsp`, clamped to
the table's ends, and the difference to the held note is the offset the slide walks to -- while
that row's own column is **not** applied (`$5303`: the column is skipped on a tick whose CMD 1 is
`L`), so the transpose in force stays the previous row's. Speed is `x + 1` updates whatever the
place, at 360 Hz in FAST and DRUM and per tick in TICK (`$4276` divides the distance by `x + 1`,
`FF` by 255).

A **cell's `L`** aims the offset at `(new note − held note)`, the held note being the last plain
one (`$4B43` skips the update of `$C0E8` on a step with `L`), from wherever `P` and earlier slides
have left the offset; the transposes stay live on top. Its first step is the update after the
note's own tick (§111 stands).

### As built

`noteOfVoice()` always adds the table's column (Drum's hold apart), and the held-column fields
(`slideTspFine`, `slideTspHeld`, `slideTspDrop`, `pitchNowTspFine`, `pitchNowColFine`) are gone.
`tableTransposeOf()` returns the previous row's column for a row whose CMD 1 is `L` (walking back
past consecutive `L` rows). The `L` handler: a table `L` sets `fineOffset` to its row's transpose
times 256 and puts the distance from the pitch now to `note + column in force + that` into the
residual; a cell `L` sets `fineOffset` to zero and the residual to the distance from the pitch now
to the new note with the live column. Both slide the residual to zero in `x + 1` updates as
before, and `P` still takes the residual over into `fineOffset` (§99).

## 153. `P` past either end of the note table wraps by nine octaves

`P 20` on a plain note runs the period up to `$7FF` and then the ROM writes `$056`: 0:`$1B28`
takes a note index at or above `$6C` (108) down by `$6C` and one below zero up by it, so the pitch
comes round nine octaves lower and goes on climbing (9.1.0's "adjusted pitch wrap"). ChipBoy
stopped at the top (`§125` measured the bottom as a clamp on 9.2.L with a vibrato; a bend on 9.4.2
wraps). `periodForNote()` now wraps the semitone index into the table's range before the lookup
when a bend or slide carries it out, and clamps only a plain note.

## 154. An `A` to a table with nothing in it runs it, and `A 21`-`A FF` stop the table

`$4679` masks the byte with `$E0`: any value of `$20` and up is the stop, and `A 00`-`A 1F` start
that table whether or not the editor has allocated it -- an empty table is sixteen empty rows,
and `A 01` from a table whose row 0 transposes ends the transposing (measured: +4 on the note, the
plain note from the `A`'s tick, and nothing more). ChipBoy's importer skipped tables with no
content, so the `A` named a slot with no table and the driver ignored it, the parent looping on;
and it mapped `A 21`-`A FF` to tables. Now the importer keeps an empty table any `A` names, maps
the high values to the stop, and the driver starts a run on an empty slot.

## 155. A table `H` to its own row holds the row

`$4E28` sets the lane's position to the target row and `$5475` re-reads it next tick, where the
same `H` hops again (the once-per-tick guard `$C8F8` only stops a second hop in the *same* tick):
`H 02` on row 2 runs row 2 -- its transpose column included -- every tick until the note ends,
which is the idiom LSDj's own tables use to hold a transpose under a slide. ChipBoy's lane
treated a hop that landed on the row it was on as no hop and stepped on to the next row. Now a
taken hop to the same row returns without advancing, and the factory *Slide up* table is
written in that idiom: `-12` on row 0, `+12` beside the `L` on row 1, `-12` held by `H 02` on
row 2 (§152).

## 156. The noise map is the ROM's 120 entries, generated, and `P` and `S` walk it round

The 9.4.2 noise map is 120 bytes at bank 02:`$5EE4`, read at `$5F5C` by the note index (note byte
− 1, `$4988`). It is exactly this rule: every (shift, divisor) pair with shift 0-13 and divisor 0-7
(0 counting as a half), sorted by the clock they give from slowest to fastest, two pairs with the
same clock kept once as the one with the larger shift -- sixty entries for the 15-bit half -- and
the same sixty with the width bit set for the 7-bit half. `LsdjModel`'s measured `kNoise9` had
two entries too many near the bottom (`24 2F` after `A5`) and two more further up, so every note
from byte 13 up was one or two entries off; it is now generated from the rule and checked byte for
byte. A noise `P` walks the index by a quarter of the byte a tick (§66) and an `S` by the byte,
and the index wraps at 120 (`$4637`, §83) -- ChipBoy clamped the walk at ±256 entries, so a long
`P 20` (measured: 90 70 50 30 then the 7-bit `DC` with the restart, `D9`, `C8`, …, a tick apart,
for as long as the note lasts) stopped after thirty ticks.

## 157. An `A` inside a table replaces the table

§131 had the table an `A` starts running **beside** the one that started it, measured on 9.2.L from
`SAMESONG`'s wave instrument. The ROM keeps one table number per channel (`$C204 + ch`) and one set
of row positions, and the `A` handler at `$4679` overwrites them: measured on 9.4.2, a parent whose
row 1 carries `A 01` in CMD 2 never reaches its own row 2 (`W 03`) or row 4 (`W 02`) again, and only
the called table's `+4` (its row 3) comes back, on tick 5 and every sixteenth tick after; a parent
row 3 `E 38` after an `A` on row 1 never lands either. What §131 saw was an `A` naming its own table,
whose row 0 then re-rolls every tick.

What the `A`'s own tick still does: the row's transpose was applied before the dispatch, and the
row's CMD 2 is dispatched after an `A` in CMD 1 (`$5323` reads the code it already has). The new
table's row 0 runs on the next tick (§122). On noise the column in force is a delta already on the
note (`$4FBE` → `$4637`) and `$5655` clears the accumulator without undoing it, so the parent's
transpose stays under the called table (§145's noise law); on the pitched channels the new table's
own rows set the column from its row 0.

### As built

The nested run (`nestOn`, its lanes, `nestTspHeld`, `nestRowLive`, `beginNestedRun`) is gone. A
table `A` absorbs the column into the noise note, calls `beginTableRun()` and marks the run just
started so row 0 is the next tick's; the lane that ran the `A` applies the row's CMD 2 if the `A`
was in CMD 1, and the tick stops stepping the lanes of the row that is gone. `tableTransposeOf()`
is the one run's row, with §152's `L`-row rule.

## 158. The project tempo byte reads as a `T` byte, and `R`'s level nibble moves a shaped envelope's start

Two more from the song diffs of the 9.4.2 project.

**`REACTION` plays at 280 BPM**, not 40: its tempo byte is `$18`, and the ROM's tempo routine
reads the project byte exactly as it reads a `T` (`$179A` is one routine: 0-39 are 256-295 BPM,
the manual's `T 00`-`T 27`). The importer clamped the project byte to 40, so every row of the song
was seven times too long; it now goes through `tempoBpmOfByte()` on the formats whose `T` does.
(§160 read the byte from the ROM's own accumulator: the word it adds is byte 24's.)

**`READROOM`'s noise row 2** (`R F0` on a note whose instrument has an ADSR envelope, `62` / `36`):
the ROM triggers at 6, retriggers at 5 a millisecond later (the nibble's −1) and the envelope then
falls from 5 to 1 in four milliseconds, where the plain note falls from 6 to 1 in five. ChipBoy
retriggered at 5 and then, on the next tick, stepped back **up** to 6 -- the shaped envelope's own
curve from its own start -- before falling. The level nibble moves the start the shaped envelope
runs from: `shapedStartOffset` is what `R`'s step has taken off, applied to the first stage's
start level and cleared by the next note.

## 159. `Z` re-rolls the last command's byte

The ROM's `$73CA` adds `random(x) << 4 + random(y)` to the last command's **byte**, carry and all,
and the letter's handler then reads its own fields from the result. ChipBoy added the two draws to
the command's `a` and `b` fields, which for a one-value letter left the draw in a field the letter
never reads: `CASTSHDW`'s pulse table -- `W 00` on row 0, `Z 02` on row 1 -- re-rolls the duty on
the ROM (0, 1 or 2; the trace alternates `NR11 = 80` and `00`) and never changed it in ChipBoy.
`resolveRandom()` now works on the byte: the nibble letters (`V C R M E S B`) re-split it, `T` goes
through its byte encoding, `G` through its slot-from-zero byte, and the rest (`D K L P A W F O`)
take it as their one value, the letter's own clamp applying after.

## 160. The tick grid: the ROM's 358 Hz clock, its accumulator and its tempo word

Read from 9.4.2's code and checked on its accumulator (the `songdiff` "drift" of `DELIVERY`, and
the 292 BPM case of §158, were this).

**The clock.** The timer runs at 65536 Hz (`TAC = $06`) with `TMA = $49`: 183 counts, 11712 cycles
between overflows. The V-blank interrupt (`$0040` → `0:$183A`) writes `TIMA` from `$C578` (`$B7`
on DMG, `$6E` at CGB double speed: 73 counts either way), so the first timer interrupt of a video
frame lands 4672 cycles after V-blank and the next five 11712 apart; `$C8F4` is the budget -- V-blank
adds six (`$C579`), each interrupt takes one and does nothing when it is spent, and a V-blank that
finds some unspent requests one at once (`IF` bit 2) -- so there are exactly six a frame and the
sixth-to-first gap is 11664. The average is 70224 / 6 = **11704 cycles, 358.37 Hz**. ChipBoy's
pitch clock was a flat 11712 (`LSDJ_PARITY` §4, measured from the reload): 0.07 % slow, 41 ms a
minute.

**What an interrupt runs** (`0:$0391`): the kit mixer, then `0:$0584`'s pitch work (FAST and DRUM
`P`/`L`/`V`, the software envelope), then at `0:$064C` the tick accumulator `$C956/$C957`: the
high byte loses 8 (2048 from the word), and when that borrows the word `$C954/$C955` is added and
the tick runs (`0:$227C`). Play start (`2:$4EA1`) zeroes the accumulator, so the first tick is the
first interrupt after play, and tick *k* lands on interrupt **floor(k · word / 2048) + 1**: the
150 ticks of `REACTION`'s first 1.3 s sit on exactly those interrupts (3, 6, 9, 12, 15, 19, …
for its word 6553). So a tick is never between interrupts, and consecutive ticks are floor or
ceil of word / 2048 apart -- at 280 BPM three or four interrupts, 8.4 or 11.2 ms, averaging 8.93;
at 163 BPM five or six, 14.0 or 16.7 ms, averaging 15.34.

**The word** (`7:$5DD8`, from `T`'s `$6676` and the project tempo alike): index = (byte + `$D8`)
& `$FF` -- byte 40 is entry 0, 255 is 215, 0-39 are 216-255 -- doubled into the 256-word table at
`7:$5E49` (`7:$6049` when `$C402` is set). Every one of the 256 words is
**round(2048 × 2.5 × 4194304 / 11704 / BPM) = round(1834828.8 / BPM)**: 45871 at 40, 15290 at
120, 11257 at 163, 7195 at 255, 6220 at 295. So the tick averages word / 2048 interrupts, which is
60 / (24 · BPM) s up to the word's rounding: 11257 for 11256.7 makes 163 BPM's tick 0.008 % long,
8 ms per 100 s. (`$C52A` non-zero takes (4 − mode) × 2048 instead: one, two or three interrupts a
tick -- a sync mode, not ChipBoy's.)

**What is not modelled:** the interrupt's latency. The trace shows the accumulator's writes from
4096 to 23084 cycles apart inside one second (the ROM's own display and kit code holding the
interrupt off, and the V-blank catch-up), averaging 11704. ChipBoy runs the ideal grid.

**ChipBoy.**

1. The driver's pitch clock **is the grid**: instant *n* at cycle 70224 · (n div 6) + 11712 ·
   (n mod 6) from the timeline's cycle 0, run at the first frame at or after it
   (`driver::gridCycle`, `gridFrame`, `gridAfter` in `Clock.h`). The free-running phase state is
   gone; a jump re-anchors to the grid.
2. **The Clock places every tick on the grid**: a tick due at frame *f* -- the host's ppq k/24,
   the song map's integral, the free run -- fires at the frame of the first instant strictly after
   *f*'s cycle. One that falls past the block's end is carried into the next block (at most one: a
   tick is at least 6 ms on a 2.8 ms grid; a jump drops it). In Host mode the ticks lag the host's
   grid by 0-2.8 ms, as the ROM's lag its nominal tempo. `tickAtBlockStart()` stays the nominal
   tick in force.
3. **An imported song's Song source takes the ROM's period**: `Song::lsdjTempo` (the importer
   sets it; `"lsdjTempo"` in the JSON) makes a whole-number BPM in 40..295 a tick of
   round(1834828.8 / BPM) × 11704 / (2048 × 4194304) s (`driver::tickSeconds(bpm, true)`), so
   a trace lines up with the ROM's over minutes, not seconds. A song written here keeps the
   exact 60 / (24 · BPM) -- its Host and Song sources agree to the sample, which the record test
   holds -- as does any other tempo (the host's, a fraction).
4. **Order on a shared instant**: the ROM's pitch work precedes its tick in one interrupt, so the
   driver runs an instant that falls on a tick's frame before the tick (`pitchBefore` takes
   instants up to and including the tick's cycle).
5. The shaped envelope's cycles-per-tick (§141) comes from the tempo the caller sets
   (`setTickRate`), the block's measured spacing only where no caller set one: on the grid the
   spacing alternates floor and ceil, which is not a tempo.
6. The record test (§9.5) plays its MIDI with *Quantise MIDI notes to ticks* on in the passes
   that have MIDI (the record pass, the hybrid pass), so a live note lands on the same grid
   instant the replay pass plays its cell on; a note held for a tick in a later block is
   recorded when it fires (`NoteEvent::held`, `Driver::firedHeld()`), not from the event it
   arrived on, which had recorded it bare. The demo song and hybrid state are regenerated
   (thirty-two grooves, §162).

## 161. The tempo runs to 295

`T 00`-`T 27` are 256-295 BPM (§59's byte) and the ROM's project byte reads the same (§158), but
ChipBoy's playback stopped at 255: the **Song Tempo** parameter, `buildTempoMap`'s base, the
processor's `T` pick-up on a locate and `chipboy_recordtest`'s `--tempo` all clamped at 255, so
`REACTION`'s 280 played at 255 (a tick of 9.8 ms for 8.9). Every one of them is 40-295 now; the
parameter table and its demo cross-check say 295.

## 163. A pulse note's own writes carry the plain period; the finetune rides the refresh

§112 measured it on 9.2.L -- "not applied at the trigger; the first pitch update moves it" -- and
ChipBoy still folded the finetune into the trigger. Read in 9.4.2: the note-on stores the
**plain** note's period at `$C0F4 + 2·ch` (`2:$4A29`, the note plus the transposes in force,
straight from the period table, no fraction, no finetune) and triggers from it; the retrigger
(`2:$6073`, `R` and `S`) reads the same word; and the refresh `0:$1BA7` -- the note, `$C174`'s
transposes, the finetune `$C696` (byte 11, stored by the loader at `2:$5A2D`; `$C697` for PU2),
the vibrato word `$C31C` and the slide offset `$C337`, through `0:$1B28`'s interpolation --
writes `NR13`/`NR14` (and `NR11` when the length bit is clear). **Where the refresh runs:** the
tick's epilogue for the channel (`2:$53C1`-`$53D7`: when the retrigger flag `$C34E` was set this
tick it rebuilds `$C174` and calls `0:$1BA7` unless `$C3F9` is set), and `$C3F9` -- kept by
`0:$1CA8` as "a bend word `$C2D0` or the vibrato flag `$C315` is live and the channel is not
TICK-mode `$C35F`" -- hands it to the 358 Hz handler (`0:$0584`) instead, at the next instant.
Raw traces of a finetune `$40` note (`FT40_*` in `vs_matrix.py`): the trigger writes
`NR13 = 97`, the epilogue `95` 0.7 ms later; with `S 21` the second trigger 0.3 ms after the
first still carries `97`; with `R 01` the retrigger 1 ms after carries `97`; with a FAST `P 03`
the `95` waits for the next instant, 3 ms on; a bare note two rows on writes `A7` and its
epilogue `A6`. `REACTION`'s pulses (`16` then `14`, `63` then `64`) are this, 4 ms apart under
a busy tick.

ChipBoy: `fineTunePending` is set by every note on a pulse channel with a finetune (plain or
bare); `noteOfVoice()` leaves the finetune out while it is pending, so the trigger and any
`S`/`R` trigger in the same tick carry the plain note; `tickAll()`'s epilogue writes the period
and clears it unless a FAST bend, slide or vibrato is running, and then the next pitch-clock
instant does (`pitchWrite`, or a plain write for a TICK instrument).

## 162. Thirty-two grooves

LSDj has thirty-two groove slots (`G 00`-`G 1F`, 32 × 16 bytes at `$1090` of the song) and ChipBoy
held sixteen, so the importer folded `G 10`-`G 1F` onto slot 16 (`std::min(v + 1, 16)`) and
`REACTION`'s noise phrase -- `G 12` on its first row -- swung on the wrong groove: the ROM's rows
came 0.077, 0.030, 0.078 s apart and ChipBoy's every 0.053. `tracker::kGrooveSlots` is 32 now:
the song's array, the phrase's slot, the `G` byte's range in the parameter table, the Grooves
tab's list (it scrolls) and its editor's slot stepper, the driver's table `G`, the importer's read
of all thirty-two and its §63 packing (which ranks the free slots from 32 down). A saved song with
sixteen grooves reads back with the other sixteen straight.

## 164. The three-stage envelope is a countdown machine on the 358 Hz clock

§51 measured LSDj 9's envelope as three stages and §116/§121/§141/§142 refined the shaped
walk that plays it; the ROM's own machine is simpler, and where the two differ every note's
shape differs. Read in 9.4.2, PU1's copy (PU2 and NOI have their own variables; WAV has none):

- **State**: mode `$CBBF` (0 off, 1-3 the stage), the countdown `$CBC1` and its reload `$CBC0`
  (in timer interrupts, the 358 Hz instants of §160), the target level `$CBC2`, the software
  level `$C2D8`, and the instrument's bytes 9 and 10 at `$CBBD`/`$CBBE` (the loader, `2:$5A25`).
- **Note-on** (`2:$5735`, then `2:$6086` from the retrigger routine the note-on shares): mode 0,
  level = byte 1's high nibble (the trigger's `NRx2` is `level << 4 | 8`: no hardware envelope,
  direction up for the zombie writes), rate = byte 1's low nibble; if the rate is zero the
  machine stays off and the level holds; else mode 1, countdown = reload = **table[rate]**, target
  = byte 9's high nibble. The table at `0:$300C` is `0 1 2 3 4 6 8 11 15 20 27 36 48 64 86 115`
  instants a step (§51's `kEnvPeriods9`, measured, is the same sixteen).
- **Every instant** (`0:$0619`, after the pitch work and before the tick): the countdown loses
  one; at zero `0:$2FC9` steps the level one toward the target with a zombie write (`08` up;
  `09 11 18` down, `0:$2F97`, with a `DIV` wait before the step from F to E), and when the level
  is the target -- reached by that step, or already there, which is how a stage **holds for one
  countdown** -- the next stage begins (`0:$301C`): mode 2 runs byte 9's rate toward byte 10's
  level, mode 3 byte 10's rate toward zero, and mode 3's end is off. A rate of zero at a
  hand-over stops the machine where it stands. Otherwise the countdown reloads.
- **`R`** (`2:$6086`): mode 1 again from the retrigger's level. **`E`** (`2:$6A0D`): mode 0, the
  level walked to `x` at once, then `y`'s rate from the eight-entry table `0 6 11 17 22 28 34 39`
  toward F or 0 in mode 3. **A table's volume column** (`2:$5224`) walks the level and leaves the
  mode alone: the machine goes on from the new level.

Probed (`ENV_*` in `vs_matrix.py`, `NRx2` writes timed from the trigger): `F3 85 46` steps every
3 instants seven times, then every 6 four times, then every 8; `39 36 08` (a start equal to its
first target) is silent for 20 + 8 = 28 instants and then steps every 8 -- `CASTSHDW`'s hats,
78 ms of hold that ChipBoy played as 22; `42 C3 07` rises with `08` every 2 instants eight times
then falls every 3. ChipBoy's shaped walk had the right rates and the wrong phase: a level
half a period early, no hold on an equal stage, and a stage's first step a clock off.

ChipBoy: `Envelope::lsdj` with the three bytes, set by the importer for the software-stage
formats (15 and 22; format 11's chip-ramped stages stay §58's) and carried in the JSON as
`envLsdj`; the driver runs the machine on the pitch clock (`lsdjEnvStart`, `lsdjEnvStep`)
instead of the shaped walk for such an instrument, restarts it on `R`, stops it on `E`, and
lets a table's volume column move the level under it. The shaped fields are still filled, so the
Instrument tab shows the picture it did.

## 165. The ROM's ticks sit at the end of their period, and a `T` takes effect a tick late

§160's accumulator, read again with the `D` probes (`D01_env` … `D06_env`, `vs_matrix.py`,
watched on `$C956`/`$C957` with the triggers in the same run): the interrupt-driven ticks land
on sub-ticks `0 5 10 16 21 27 32 38` at 163 BPM -- floor(n · T) for T = word / 2048 -- and the
song's row 0 sounds at the **second** of them (the first, at play start, only steps into the
song). So song tick *s* is at floor((s + 1) · T): relative to tick 0 the delayed notes came at
+5, +11, +16 and +33 instants for `D 01`, `02`, `03`, `06` (one tick each, as the manual says),
where ChipBoy's "first instant strictly after s · T" gave +5, +10, +16, +32 -- the long gaps in
the wrong places, which is why `SAMESONG`'s envelope steps and pitch writes sat a tick's
fraction apart. The accumulator also adds the tick's word **before** the tick's own commands
run, so a `T` sets the period from the tick after its own.

ChipBoy, for a song on the ROM's tempo (`Song::lsdjTempo`, the Song source or the plugin's
own transport): a tick fires at the last instant at or before its nominal end
(`gridRomTickFrame`), one tick after where it was, and the tempo map's points sit one tick
after their cell. Host mode keeps §160's placement: the ROM's extra tick of latency after play
is not what a host's grid wants.

## 166. A STEP table's position is the instrument's across the channels

§140 measured that two instruments keep their own STEP positions; ChipBoy kept them per
channel as well, "which nothing measured requires". 9.4.2 keeps **one position per
instrument** (`$C250 + $C210[ch]`, §11.5), and it is what `EGOFLEX` does: instrument `1B` --
STEP, table `0F` whose row 0 is `A 10` -- plays on both pulses at the same tick, so PU1 takes
row 0 (the `A`) and PU2 row 1 (`O 01`), and neither sounds the slide ChipBoy gave both.
Probed (`STEP_2ch`, the same instrument on PU1 and PU2, notes at the same rows): the ROM's
transposes go `+12` on PU1, `+5` on PU2, `+8` on PU1, `+1` on PU2 -- one walk shared in note
order; offset by a row (`STEP_2ch_off`) it is the same walk. ChipBoy gave each channel `+12,
+5, +8`.

ChipBoy: `stepState_` is one array over the instrument keys; `clearSteps()` empties them all.

## 167. `R`'s level nibble moves the hardware, not the machine's level; `E` and a table's volume column walk relative to it

`REPTCOMP`'s noise instrument `16` (`74 71 48`: start 7, hold at 7 for 4 instants, then to 4 a step an
instant, then to 0 a step every 15) plays with `R B0` on the note: the hardware level goes to 2
(7 − 5) at once, and the machine then steps **down** from there. ChipBoy compared its target (7)
with the hardware level (2) and stepped up. Read in the ROM: the retrigger routine (`2:$6058`)
writes `NRx2` from the software level `$C2D8` plus the nibble's delta, and leaves `$C2D8` alone;
`0:$2FC9` steps `$C2D8` toward the target and issues one zombie step to the hardware for each,
from wherever the hardware is; `2:$7F3E` (an `E`'s `x`, a table's volume column) walks `$C2D8`
to the new level with the same one-zombie-step-per-level rule; the fast retrigger (`0:$05C1`)
writes `$C2D8` as it is. So the hardware level is the machine's level plus whatever `R` nibbles
have taken off, and every walk is relative. Probed (`RC_noi`, `SS_noi`, `DL_noi` in
`vs_matrix.py`, the songs' noise instruments byte for byte).

ChipBoy: `Voice::lsdjLevel` is the machine's level for an `Envelope::lsdj` instrument -- set by
the note-on, stepped by the machine and by `lsdjWalkLevel()` (an `E`'s `x`, a table's volume
column), never by `R`; each step moves the hardware one zombie step with the chip's wrap
(`lsdjZombieStep()`); `R`'s nibble writes the hardware from `lsdjLevel` and its count; an `E`'s
rate runs in the machine's third mode with the ROM's eight-entry table.

## 168. The interrupt's order: pitch, the fast retrigger, then the envelope; and the roll's trigger has no length bit

`0:$0391`'s order is the pitch work (`$0584`), the fast retrigger countdowns (`$05B3`), the
envelope countdowns (`$0619`) and then the tick. So a roll's trigger (`R 8y`, `y + 1` instants
apart, the count multiplied by instrument byte 8 + 1) writes the machine's level **before** the
step that lands in the same instant -- `SAMESONG`'s hats: `C8`, then the step to B -- and it
writes `NRx4 = $80 | period`, **without the length bit** (`0:$05CF`, `$0615`), where ChipBoy
kept the instrument's. ChipBoy's instant now runs the fast retrigger between the pitch step and
the envelope, and its trigger drops the length bit.

## 169. DRUM pitch is a linear period table with a per-note fraction, and its note-on drops the fraction

§99 and §88 modelled DRUM as "the period register moves in a straight line", one semitone of a
pitch effect worth 19.11 units wherever the note is. The ROM's machine (`$49C0`-`$4A33` at the
note lookup, `$4B18` at the note-on, 0:`$1B28` at every pitch write) is a table, and the table is
what makes the units come out at 19.1:

- **The table** 0:`$09B9` is 108 words, `round(k · 2044 / 107)`: 0, 19, 38, 57, 76, 96 … 2044.
  A DRUM instrument's note lookup (`$C330 + ch` set) points the channel's table pointer at it
  instead of the semitone table in RAM at `$CF28`.
- **The note** becomes an index into it plus a fraction: two tables built at boot in RAM,
  `$CD28` (the index, the largest `k` with `table[k] ≤ period`) and `$CD94` (the fraction,
  `floor((period − table[k]) · 256 / (table[k + 1] − table[k]))`), both indexed by the note.
  `$49EB` writes the index + 1 into the note variable; `$4B18` writes the fraction into the low
  byte of the offset word (`$C337 + 2ch`, high byte `$80`). So the note's pitch is exact from the
  first refresh: 106.94 of the table for note `55` is 2025 + 19 · 94 / 256 = 2032, the semitone
  table's own `$7F0`.
- **Every effect is in 1/256 of an entry**: `P`'s curve value (§11.9), `L`'s
  `(target − offset) / (v + 1)`, the vibrato's `$C31C` are added to the word as in FAST mode, and
  the transposes (`$C174`, the table's column and a chord's step) to the index. 0:`$1B28` then
  reads `table[d]` and adds `round(diff · e / 256)` (an 8-bit shift-add multiply, rounded on the
  low byte's top bit) with the nine-octave wrap of §153 at 108. One entry is 19.1 units, which is
  where §88's constant came from; the table's rounding is what it lost (`CASTSHDW`'s wave drum:
  the ROM's `$713` where ChipBoy's line gave `$712`).
- **The note-on writes `table[index]` without the fraction** (`$4A2C` stores the lookup, the
  trigger reads it): `$7E9` for note `55`, seven units flat; the tick's epilogue refresh writes
  the exact `$7F0` about a millisecond later, unless a FAST effect is live (a table's row-0 `P`),
  when the next instant's step is the first exact write. This is §6.13's "half a step in" that
  §11's matrix left open: it is not a step of the bend at all, but the fraction the note-on drops.

Probed (`Wv_cast10`, `Wv_cast10_notbl`, `Wv55_tblPA9` in `vs_matrix.py`; `CASTSHDW`'s
instrument `10` byte for byte): the note-on `$7E9`, the refresh `$7F0` at +1.2 ms, and with the
table's `P A9` the word `$805E` stepping by −990 an instant (`$C33B`/`$C33C` watched) through
`$7A6 $75C $713 $6C8 $67F $635`.

ChipBoy, for a DRUM instrument of a 9.x format (`pitchSpeed == Drum`, not a kit, not the old
formats' register-unit law of §88): `noteOfVoice()` returns the note's entry and fraction
(`drumPosOf()`) with the transposes and the 1/256 offsets on top, so `P`, `L` and `V` run on the
FAST machinery unchanged; `periodFor()` reads the generated table (`drumPeriod()`) with the ROM's
rounding and §153's wrap at entry 108; the note-on writes the entry alone and leaves the exact
period to the epilogue refresh through §163's `fineTunePending`. The period-unit slide of §99
(`drumOffset`, `drumSlide*`) stays for kits and the register-unit formats.

## 170. A wave instrument's `FINETUNE` is byte 12, 1/256 of a semitone, and it rides the refresh

The 9.x wave instrument screen has a `FINETUNE` ("detune the sound") beside the pulse one of
§112, and the importer never read it: `CASTSHDW`'s instrument `0E` carries `7F`, `REACTION`'s
`05` and `14` carry `10` and `08`, `DELIVERY`'s `0A` carries `0C`. Read in the ROM: the loader
(`2:$5C2B`) sign-extends byte 12 into the word `$C698`/`$C699`, and the wave refresh
(`0:$1C47`) adds that word to the note's 16-bit note.fraction before the vibrato and the slide
offset -- so the unit is **1/256 of a semitone, signed**, added (the pulse one is subtracted on
PU1, `fine · 8 / 256`). The note-on's own writes carry the plain period as §163's do, and the
finetune arrives with the tick's epilogue refresh (`REACTION`'s wave opens at `$416` and the
refresh writes `$41A`), or with the next instant's step under a FAST effect.

ChipBoy: `LsdjModel::waveFineTuneByte` (12 on format 22, unread elsewhere) fills
`Instrument::fineTune` for a wave instrument, which the driver reads as a signed byte with no
channel sign and hands to §163's `fineTunePending`.

## 171. The wave frame writer: the sync grid, the `$7E0` pre-trigger, the start frame, `RESYNC`

Every wave RAM write of the ROM -- a note-on, a frame of a synth run, the `0x77` frame that ends
a `ONCE` run (`2:$5FCB`) -- goes through one routine, `0:$0762`, and the tick never calls it
for a frame step. Read together with the interrupt's head (`0:$06A5`-`$075F`):

- **The sequence.** `NR30 = 00`, the sixteen bytes, `NR30 = 80`, `NR33 = E0`, `NR34 = 87` -- a
  trigger at period `$7E0`, sixty-four cycles a sample -- then `NR51`, then `NR33`/`NR34` with
  the channel's current period and no trigger bit, about fifty cycles after the trigger. A
  note-on writes `NR32` before it and the tick's epilogue `NR31 = 00` after. ChipBoy triggered
  at the real period and, on CGB, streamed a new frame behind the read pointer instead of
  retriggering (section 6.5): the ROM does neither, on either model.
- **The sync grid** ("silky wave", 7.x). The trigger reads `DIV` (`$C400`) and zeroes a phase
  word `$C363` in units of 64 cycles; every interrupt adds `4 · ΔDIV` to it and reduces it
  modulo `S = (2048 − period) · 2^k`, the smallest `k` with `S ≥ 256` -- `2^k` cycles of the
  wave, at least 16384 cycles. A frame the tick has made pending (`$C69C`, `2:$7028`) is written
  only at an interrupt where the remainder `S − phase` is under 184 units (`0:$0748`: one
  interrupt period), and then the handler **busy-waits** `remainder / 4` DIV ticks (256 cycles
  each) to the boundary before writing, delaying its own pitch work and the next interrupt
  behind it. So a frame lands where the wave's own cycle restarts, with the trigger's phase
  reset landing on the old phase; measured on `Fr_loop_0C_FD` (a step a tick at 163 BPM,
  period `$797`, `S` = 4 cycles = 26880): the writes fall 2 or 3 `S` apart, 264-520 cycles
  after a boundary, and the ROM's own remainders at the checks it took run 4-180, the ones it
  skipped 184-420. ChipBoy wrote the frame on the tick, wherever the wave was: the click that
  made the runs sound rougher than LSDj's.
- **The start frame.** The run's frame index `$C694` starts at **byte 3 whole** -- synth in
  the high nibble, frame in the low -- and the steps are added to it unwrapped (`2:$6204`
  reads `$A000 + index · 16`), so a start of 4 with eight steps plays frames 4 6 8 A D F 11 13,
  the last two from the next synth. In `MANUAL` mode byte 3 is the `WAVE` parameter and that
  frame is what plays (`DELIVERY`'s `0F`: frame `$33`, synth 3's frame 3). ChipBoy read the
  high nibble as the slot and always started at frame 0.
- **`LOOP POS` is byte 2's low nibble** (§93 already), the loop covering the last
  `15 − nibble` steps, clamped to the run (`2:$57A2`); byte 10's high nibble is unused. The
  run's frames are §132's ladder exactly (the ROM's table at `2:$7EC6` is that rule row for
  row), the step comes every `speed + 4` ticks with the note-on's tick counted (`$C525` =
  `speed + 4` at the note, decremented by the same tick's table pass), and `ONCE` ends by
  writing the `0x77` frame. Speed `FC` wraps: the counter starts at 0, so the first step is
  the note's own tick and the next 256 ticks later. Not modelled (no song has it).
- **`RESYNC`** (9.2.E, `PLAY` = 4): §11's importer took `byte 9 & 3` and read it as `MANUAL`.
  It runs `PINGPONG`'s program and writes each frame **at the tick**, at once, through the
  same routine (`2:$701B`, interrupts off), so every step retriggers the wave. `REACTION`'s
  `0C` and `READROOM`'s `02` use it.

Probed (`Fr_*` in `vs_matrix.py`, sixteen frames tagged so `W0` names the frame; `frtimes.py`
prints both sides' frame writes): every `PLAY`, length, speed and start combination above.

ChipBoy: `FrameLoop::Resync`; `Instrument::frameStart`; the run's frames resolve through the
flat wave table (`waveFlatOf`), so a run past the slot's end reads the next slot; a frame step
sets `Voice::framePending` and the instant loop runs the ROM's check (`waveSyncPeriod()`,
`waveSyncBase` = the last trigger's cycle) and writes at the boundary through
`writeWaveFrame()`, which is also the note-on's and `RESYNC`'s writer; the trigger is the
`$7E0` pre-trigger followed by the period. The CGB streaming path stays for kits only.

## 172. Kits as the ROM plays them: the bank, the two sides' bytes, a frame an interrupt, the raw pages

`UNMASKED`'s kits "are not processed correctly", and the reading of 9.4.2's kit machine (matrix
§11.8, `0:$0391`-`$0590`, `0:$15A0`-`$1700`, the loader `2:$5C77`-`$5DB9`) says why on every
count. What the ROM does, and what ChipBoy did:

- ~~**Kit number `k` is ROM bank `k + 8`** (`0:$1478`)~~ -- **wrong, see §193**: the number
  counts the ROM's kit banks in order and skips the empty ones, which is what ChipBoy did
  before this section changed it.
- **Each side has its own bytes.** Kit A: number `byte 2 & $3F`, `ATK` bit 7 of byte 2, half
  speed bit 6 of byte 2 (both sides), `LEN` byte 3, `OFFSET` byte 12, `LOOP` bit 6 of byte 5. Kit
  B: number `byte 9 & $3F`, `ATK` bit 7 of byte 9, `LEN` byte 11, `OFFSET` byte 13, `LOOP` bit 5
  of byte 5. `LEN` and `OFFSET` are in 16-byte frames (32 samples); the loader shifts them by
  four into a byte offset (`$C4EA`/`$C4EB`, `$C4EF`/`$C4F0`). §96 had one `LENGTH` for both
  sides and no offsets; §97 had no `ATK`.
- **What a side plays** (`0:$15C3`-`$165D`): from the bank's offset word for the digit's sample,
  plus `OFFSET`, to the sample's end or `OFFSET + LEN` when `LEN` is not `ALL` (0). `LOOP ON`
  restarts there at the end; `ATK` starts at the sample's own beginning and loops from `OFFSET`;
  `OFF` stops the side. A side with `LOOP` off takes the sample's own loop bit from the bank
  header (`$405C`/`$405D`, one bit a sample) -- `0:$1599` skips that read when the instrument's
  bit is set. `SPEED` half runs the mixer every other interrupt at period `$692 + FINETUNE`
  (`0:$03B0`; full speed is `$749 + FINETUNE`, byte 8 signed), and `VOLUME` is byte 1's `NR32`
  code like a wave instrument's, which ChipBoy set to 100 % for every kit.
- **A frame an interrupt.** The note-on writes `NR32` and `NR33` and nothing else; from the next
  interrupt on, before the pitch work, the mixer copies sixteen bytes from each live side --
  raw when one side plays, through the page table when both -- and writes them: `NR51` with the
  wave's bits cleared, `NR30 = 00`, the bytes, `NR30 = 80`, `NR33 = E0`, `NR34 = 87`, `NR51`
  restored, then `NR33`/`NR34` with the kit period. The side's position moves sixteen bytes a
  frame whatever the period, so a `P` on a kit repeats or skips samples rather than resampling.
  When both sides have ended the next interrupt writes `NR30 = 00`. ChipBoy streamed a frame
  behind the read pointer on CGB and refilled at the 32nd fetch on DMG, following the period.
  (The wave frame writer of §171 mutes `NR51` the same way, `0:$0787`.)
- **The page table.** Two live sides mix byte by byte: `out = swap(T[bh · 16 + ah]) +
  T[al · 16 + bl]`, T the 256 bytes at page `byte 10`. `$D0`-`$D3` are §117's curves. Any other
  page is memory as it stands (the manual's "A + (LEFT, LEFT) while HARD is selected"):
  `UNMASKED`'s `8E` is VRAM `$8E00`, the font tiles the ROM loads at boot from its own data
  (ROM `$7842A` on 9.4.2, the same bytes in every RAM dump of the run), and the 8-bit adds carry
  between the nibbles. Reading that page from the ROM beside the save reproduces the mix; what
  it cannot reproduce is that a read during the LCD's mode 3 returns `$FF` (the frame's `FE`
  bytes, about a third of them), which depends on where the interrupt falls in the scanline.

ChipBoy: `LsdjKit::bank`/`loopBits` and a lookup by number; `KitSample::loop` beside
`loopPoint` (`Kit::perSampleLoop` says the samples carry their own), `Kit::halfSpeed`,
`KitDist::Raw` with `Kit::distTable`; the importer reads both sides' bytes, cuts and offsets
each sample per side, reads the volume, and takes a raw page from the ROM's font block for the
9.x model; the driver writes kit frames from the instant loop (`kitFrame()`), a frame an
instant or every other one, through the ROM's sequence, and drops the fetch model and the CGB
stream (`scheduleStreams`, `updateWaveTimer`). The mode-3 `$FF` reads are not modelled.

## 173. A cell's `L` skips the note-on's lookup: the trigger carries the old period

§152 has the slide as an offset beside the transposes, aimed at the new note from where the
channel is. One more thing the step reader does (`2:$4A07`, matrix §11.3 item 4): with an `L`
on the step the period lookup is skipped, so the note-on's own trigger writes whatever the
channel wrote last -- `EGOFLEX`'s first wave note, `L 60` on the song's first row, triggers at
period `$000` and the epilogue refresh 3.5 ms later brings `$416`. ChipBoy triggered on the new
note. `Voice::slideOnTrigger`, set by a cell's `L` inside the note-on, makes the trigger (and a
wave's post-`$7E0` write) carry `lastPeriod`, 0 when there is none.

## 174. The vibrato is a 16-bit phase, a 64-step waveform and a multiplier ladder

§114 and §125 measured every shape as a full swing either side of the note, the direction bit
choosing which half comes first, and §119 the noise depth in map entries. The ROM's machine
(`2:$7DEB` at the `V`, `0:$19FE` every pitch update, the shape at `0:$1986`):

- **The phase** is a 16-bit word (`$C308 + 2ch`) that gains an increment every update, after
  the offset is computed from it: `1024 · (speed + 1)` in FAST/DRUM (`$7DAB`, 64 / (speed + 1)
  updates a cycle), or `round(65536 / n)` for n = 96 72 64 48 36 32 24 18 16 12 9 8 6 4.5 4 3
  ticks a cycle in TICK and always on noise (`$7DCB`; ChipBoy's ninths table was this in
  another unit). A `V` on a running vibrato only changes the increment; on a stopped one it
  starts the phase at `$0000` when the instrument's direction bit is set, else at `$8000`, or
  `$FC00` for the saw. The note-on stops it (`2:$5FA7` clears the increment); the instrument
  has no vibrato of its own.
- **The waveform** is 64 entries a shape at `0:$0200`, indexed by the phase's top six bits:
  triangle `2i` up to 32 at 16, down through 0 at 32 to -32 at 48 and back; saw `i − 32`;
  square `+32` / `−32`; the fourth quarter zero (shape 3, off). The negative entries are
  stored one's-complemented and negated after the multiply, so both halves are the same size.
- **The depth** is a jump into a ladder of `add` code at `0:$0300` -- the sixteen bytes at
  `$7D9B` are its entry offsets -- that multiplies the entry by 1 2 3 4 6 8 12 16 20 24 28 32
  40 48 56 64, so depth 0 swings ±32 (an eighth of a semitone in 1/256) and F ±2048, which is
  §125's measured table exactly. The result is added to the note word before the slide offset.

ChipBoy's triangle had the right depths and a symmetric 64-point shape; its saw and square ran
`ph / 64` and `ph < 32` on a phase in ninths, its direction bit flipped the sign instead of the
start, and its phase started at 0 at every V. `Voice::vibPhase` is the word, `vibratoFine()`
the waveform times the ladder, the direction picks the start phase, and the increments are the
ROM's.

## 175. `R`'s level nibble rewrites the envelope's three levels and restarts the machine

§167 read `R`'s nibble as a move of the hardware level alone. The ROM does more: every
retrigger an `R` fires -- the command's own and each periodic one (`2:$674B`, `$6770`, `$67AE`
call `2:$4000`) -- adds the nibble, as a signed high nibble, to **each of the three envelope
bytes** in the channel's copy (bytes 1, 9 and 10: the level, the first stage's target and the
second's), a zero byte left alone, an underflow clamped to level 0 and an overflow to F with the
rate nibble kept, and then re-runs the note-on's envelope init (`2:$5735`) on the rewritten
bytes. Watched on `REPTCOMP`'s hats (`$CBCA`-`$CBCC`): `74 71 48` with `R B0` becomes `24 21 08`
-- level 2, hold four instants at its own first target, two steps to 0, and the third stage
finds its target already reached, so the machine stops. §167's model kept the targets at 7 and
4 and walked on to 0 from there: six steps where the ROM makes two, and `READROOM`'s `R F0`
(`62 36 00` → `52 26 00`) five steps against ChipBoy's six. ChipBoy: `retrigger()` shifts the
voice's `lsdjByte1/9/10` by the nibble each time it fires and restarts the machine from the new
level; the cumulative `retrigBase + step · count` stays for the chip envelope.

## 176. `K` walks the machine's level to 0 with zombie steps, and the shadow level is the chip's

§128 named the walk a `K` shares with `E` (`2:$7E75`, target 0) and §164-§167 gave the
machine its own copy of the level (`$C2D8`/`$C2D9`/`$C2DB`). The kill's tick side is the
same routine chain on every channel (`2:$664C`/`$669C` → `2:$6584` → `2:$5F68`): the fast
retrigger, the `P` step and the vibrato increment are zeroed, the machine's mode is set to 0
and the walk (`2:$7F3E`, entered at `$7F4D` NOI, `$7F58` PU2, `$7F63` PU1) compares the
machine's copy of the level to 0 and issues one `09 11 18` triplet per level; WAV is
`$5FB1` then `NR30=00`, no walk. `REPTCOMP`'s PU2 (`E67` on the note, `K03` four rows on):
the ROM's machine has stepped 6 → 3 by the kill and the kill writes three triplets
(0.441 s); `REACTION`'s NOI `K00` writes six.

ChipBoy's kill wrote nothing on both, and the reason was a level kept twice. `emitNrx2`
already moves the chip model's volume through each zombie write (`zombieAfter`, the triplet
is one level down and `08` one up); `lsdjZombieStep` then moved `v.volume` again by hand, so
after three steps the model's level was 0 while the machine's copy said 3, and the kill's
walk to 0 found nothing to do. The hand step is gone: the chip model's level is whatever
the writes made it, and the machine's copy (`lsdjLevel`) is the ROM's shadow. `killLevel`
on an lsdj-machine pulse or noise voice now walks `lsdjLevel` to 0 (`lsdjWalkLevel(ch, 0)`)
-- the ROM's target and the ROM's counter -- and the voice stops; wave and kit keep
`NR30=00`. Non-lsdj instruments keep §128's walk of the chip level.

## 177. A bare cell takes its own chain row's transpose

The phrase reader adds the chain row's transpose to the note as it reads the cell (`2:$4A07`,
before the instrument column is looked at, under the instrument's TRANSPOSE flag), so the
note a cell with a blank instrument column plays is the transposed one, and its `L` aims
there. `EGOFLEX`'s pad: phrase `2B` (chain transpose 12) plays note 01 with instrument 29,
phrase `4D` (transpose 20) the same note bare with `L 60`; the ROM triggers on the old period
(§173) and slides 36 → 44 over 97 instants (`$416` → `$589`, four or five units an instant,
1.780 s on). ChipBoy's bare note kept the previous plain note's transpose -- `noteTsp` was
only set when an instrument was loaded -- so the target was the note it was already on and
nothing slid; the pad stayed a minor sixth low for the row and every frame after it landed on
the wrong sync grid. `startVoice`'s bare path now takes `cellTranspose` under the flag, as the
plain path does; a bare cell without an `L` moves to the transposed note at once (the period
write it already made).

## 178. The sync phase word accumulates: a slide moves the grid with the note

§171 derived the wave's sync grid from the trigger: the phase was `4 · ΔDIV` since the last
wave trigger, reduced modulo the sync period of the *current* note. The ROM's check
(`0:$06A5`) keeps a running word instead: every interrupt (when no kit is playing, note or
none) it adds `4 · (DIV − $C400)` -- DIV's 8-bit delta since the previous interrupt -- to
`$C363`, stores DIV in `$C400`, forms `S = (2048 − period) << k` from the period register
copy of *that* interrupt, and subtracts `S` until the word is below it. What an earlier
period left in the word stays; a slide moves the grid with the note rather than re-deriving
it from the trigger, and after a jump the boundaries sit at the last old-period boundary
plus multiples of the new period. The busy-wait (`0:$074D`) counts `rem / 4` DIV ticks with
`$C400` re-read each pass, so its length is what §171 said. Seen on `REACTION`'s sliding
wave: ChipBoy's from-scratch phase waited a whole instant longer than the ROM's at 0.045 s
and pushed every channel's pitch work behind it. ChipBoy: `wavePhase` and `waveDivLast` per
wave voice, fed every instant by `waveSyncStep()` (before the channels, §171's order), both
zeroed by the frame writer; the pending frame is written when the remainder is under 184
units, as before.

## 179. A STEP table's position advances a row a note whatever an `A` did in between

§122 settled what runs when: STEP governs the instrument's own table, one row a note-on, and a
table an `A` starts runs a row a tick from the next tick. What it left to ChipBoy's guess was
the note-on *after* such an `A`: ChipBoy skipped the STEP advance when the run in force was an
`A`'s and restarted the instrument's table at row 0, so the `A` fired again on every note and the
rows after it never played. Probed on 9.4.2 (`STEP_A2`, `STEP_A3`, `STEP_A_r1`, `STEP_H`,
`STEP_noA` in `vs_matrix.py`: a STEP pulse instrument, four notes two rows apart, `W` values
read off `NR11`):

```
table 0: A01 / W01 / W02   table 1: -- / L03+12 / W03 / W00
ROM  note 1: trigger, then table 1's rows on ticks 2, 3, 4 (slide, C0, 00)
     note 2: trigger, 40 (table 0's row 1)     note 3: nothing (row 2: W02 = the duty)
     note 4: nothing (row 3 empty)             table 1 does not tick on after note 2
table 0: W03 / A01 / W02 / W00   table 1: W01 / L03+12
ROM  note 1: C0    note 2: trigger, 40 on the next tick, the slide on the one after
table 0: W03 / H03 / W02 / W00 / W01
ROM  note 1: C0    note 2: 00 (the hop is free: row 3 plays on the same note)    note 3: 40
```

So the instrument's STEP position counts the instrument's own rows, one a note, and an `A` on
one of them changes only what the channel runs until the next note-on reloads the instrument's
table (§124). ChipBoy: the note-on takes the parked position whether or not the run in force was
an `A`'s (the position written through when the `A` fired, §166, is the row after it). `H` in a
STEP table was already right: its target row plays on the same note.

## 180. The fold undone: what the ROM's note-on writes before the trigger, and what after it

§147 named the fold and left it: ChipBoy applied a cell's commands and the table's row 0 inside
the note-on, so the trigger carried their result, where the ROM triggers on the instrument's own
values and lets them land as their own writes. Read in the phrase reader (`2:$4A04`-`$4AA4`),
watched with the pc of every write (`watch.py SAV C8C4-C8C4 --apu` on the `W01_ph_ch0`,
`E38_ph_ch0`, `E38_tbl_c1`, `R01_ph_ch0`, `S21_ph_ch0`, `STEP_noA` probes):

```
before the trigger   L  skips the period lookup (§173)         D  the delay
                     E  2:$7D15 rewrites the envelope copy, so the trigger's NRx2 carries it
                     F on WAV  $C694 += byte, so the trigger's frame carries it (and the
                               command byte is cleared: it is not dispatched again)
                     S on NOI  the reader's noise path (2:$4973) moves the map index and
                               restarts (NR43, NR42 at the machine's level, NR44 |= $80)
                               before the trigger, and clears the command byte (2:$499B)
the trigger          2:$6014: NR10 NR11 NR12 NR13 NR14 from the instrument (the wave writer)
0.3 ms in            O  2:$61B6 writes the pan; every other command goes through the
                     dispatcher 2:$46C7 now -- W writes NR11 (80 then 40 in `W01_ph_ch0`),
                     S writes NR10 and triggers again, E walks (and finds its level)
1.0 ms in            R  the retrigger phase: the second full burst of `R01_ph_ch0`
1.2 ms in            the table's row 0 (2:$5300, the command byte stored at 2:$5311 and
                     dispatched): `E38_tbl_c1` triggers on F8 and walks twelve triplets,
                     `STEP_noA` triggers on the duty and writes the row's W after
2 ms in              a TICK instrument's period, with the row's transpose (`Ltbl_tick`)
```

The one-channel probes give those costs; in a four-channel song the same phases run later
(the note-on tick reaches a wave's row 0 some 3.5 ms in, §171's measurement), which stays
unmodelled. ChipBoy's plain note-on now runs the cell's `E`, `F` (wave), `S` (noise), `L` and `D`
before the burst as before, the burst on the instrument's values, then the rest of the cell's commands live
at 0.3 ms (`kCellDispatchCycles`), a cell `R`'s retrigger at 1.0 ms (`kRetrigPhaseCycles`),
the table's row 0 live at 1.2 ms (`kTableRowCycles`) with its own writes -- the volume lane's
walk, a `W`'s NR11, an `F`'s frame -- and a TICK instrument's period at 2 ms
(`kTickPitchCycles`) when the row moved the note; a pitch-clock instrument takes it at the
next instant, as the ROM's pitch work does. The bare note's path is unchanged. Two things
the un-fold uncovered: a cell `P` wrote the (unchanged) period at its dispatch where the
ROM's handler stores the step and writes nothing, and a tick-side retrigger restarted the
machine at the level it had walked to where the ROM re-runs the note-on's init on the copy
(`ENV_R`: every `R 03` trigger carries F8); both fixed. The noise restart's NR44 is what the
ROM reads back with `$80` set -- `BF`, or `FF` with the length bit. The matrix went from 71
differing cases of 323 to the timing, random and unmodelled ones.

## 181. A ONCE run's end stops the pitch effects, and a note-on's noise `S` is judged against the note

Two more from the un-fold's probes. `CASTSHDW`'s kick (a DRUM wave instrument, PLAY = ONCE,
table `00`: `P A9` on row 0, `L 20` with a transpose of `80` on row 1) slides down at 47 units
an instant on the ROM and halts at `$3A6`, holding it until the flat frame; ChipBoy slid on to
the table's floor. Watched (`$C33B`/`$C33C`, the wave's offset word; `$C2D4`/`$C2D5`, its step):
the step word is zeroed at 0.537 s by `2:$5FBE`, inside the wave stop `2:$5FB1` that the ONCE
end (`2:$5FCB`) calls before it queues the `$77` frame -- the same stop a `K` runs (§176). It
zeroes the P/L step word, the vibrato increment (`$C319`), the fast retrigger (`$C14E`) and the
kit flags. ChipBoy: the ONCE end clears the slide, the bend, the queued offset, the vibrato and
the roll along with queueing the flat frame.

A cell `S` on a noise note (§180: the reader applies it before the trigger) restarts the channel
when the map index crosses 60 upward (`2:$4660`: `old < $3C` and `new >= $3C`, the ROM's table
splitting exactly there by NR43's width bit, so §86's rule stands) or always under PITCH = SAFE
(`$C692`). The reader stores the note's own index (`2:$4985`) before the `S` moves it, so at a
note-on the crossing is judged from the note, not from whatever the channel last wrote:
`S21_ph_ch3` restarts (NR43, NR42 at the machine's level 0 → `08`, NR44 `BF`) before the
trigger. ChipBoy set `lastPeriod` to the note's own NR43 (`noiseNr43()`) before the pre-trigger
`S`; the restart's NR44 is the register read back with `$80` set.

## 182. A roll replays the instrument's table; `Z` on a wave `F` rolls the byte; `F 00` writes nothing

Four things `UNMASKED` showed once §180's fold was gone, each probed both-sided.

**A tick's `R` retrigger starts the instrument's table over.** The handler (`2:$64AA`) arms the
countdown; the tick's retrigger goes through `2:$4000` into `jp $5735`, the tail the note-on
shares -- the instrument's copy is reloaded and its table pointer set to row 0 -- so every roll
plays row 0 again, a TICK table and a STEP one alike, and a table an `A` had moved the channel
to is gone with the reload (`Rtick_tbl`: a four-row `W` table under `R 03` writes rows 0, 1, 2,
0, 1, 2 …, never row 3; `Rtick_Astop`, `Rstep_Astop`: the `A`'d table restarts on each roll;
`Rstep_tbl`: the STEP position is left where the last note parked it, the next note plays its
own next row). Row 0 is **the retrigger tick's row**: the row the table was on is not played,
and the row after row 0 is the next tick's. Timings, one channel: the roll's trigger to its row-0
write 0.93 ms (`NR14` at 0.07271, `NR11` at 0.07360); a note-on's `R` 1.06 ms after its trigger
without a table (`R01_ph_ch0`, §180's 1.0 ms) and 1.34 ms with one (`Rtick_tbl`: the table's setup
runs first), the table's row 0 then 1.2 ms after the retrigger it ran (2.52 ms into the note).
`UNMASKED`'s chain `30` is this: instrument `15`'s table `0A` is `W00 A0B` and table `0B` puts a
`+C` on row 0 -- the octave blip -- and stops; the phrase's `R C0` and the `Z` rolls of it
retrigger the note, and the ROM blips on every roll where ChipBoy played the blip once.

**On the wave channel `R`'s nibble walks NR32.** The wave has no envelope machine (§164), so
the nibble the retrigger adds to the envelope bytes lands on the level code: `R F4` on a 100 %
note writes `NR32 = 40` at its immediate retrigger, `60` four ticks on, `00` after that, and the
rolls after stay at `00` (`RF4_ph_ch2`) -- a notch a retrigger, 100 % → 50 % → 25 % → mute, held
at the ends. ChipBoy stepped `envVol`, which the wave never writes; `waveLevel` walks now.

The cell `R`'s retrigger after the trigger, measured on the ROM: 1.06 ms on a pulse, 1.07 on the
wave, 1.10 on noise (0.89 with a nibble of `F`), 1.34 with a table on the instrument; ChipBoy's
constant is 1.06 and the table's extra, so the compare's 1 ms batches split the pair on noise
and wave as they do on the ROM's pulse -- a boundary, not a difference.

ChipBoy: the tick decides the periodic retrigger **before** the table's row (`retrigReplays`),
skips the row for that tick, runs the retrigger, then starts the instrument's own table over
(`tableOverride` or the instrument's, the lanes at row 0 with their hop counters clear, the STEP
position untouched) and plays row 0 `kRollRowCycles` on, with `tableJustStarted` clear so the
next tick plays the row after -- or an `A`'d table's row 0. A note-on with an `R` puts its
retrigger at `kRetrigPhaseCycles` (1.06 ms now), `kRetrigTableExtraCycles` later when the
instrument has a table, and its row 0 `kTableRowCycles` after that.

**`Z` on a wave `F` rolls the byte.** `F`'s argument is the whole byte (`x · 16 + y`, §100), and
§159's roll adds `random(x) << 4 + random(y)` to the byte; `resolveRandom()` treated `F` as a
one-field letter, so `Z 0F` on `F 00` became `F` with `x` = 0..15 -- whole slots of sixteen
frames, into synths the song never drew (`UNMASKED`'s phrase `10`, row D: `A 0F` into a table of
`F 00` then `Z 0F`, the ROM writing one of synth 3's frames 0-15 on the tick after the note,
ChipBoy a frame from an empty synth, which is the "quiet" note). `F` is on the byte list now
(`ZF_wave_F00`, `ZF_wave_cellA`: both sides write a frame on row 1). The roll itself is the ROM's
random and stays unreproducible: in the song each side lands on a different frame of the sixteen,
and a roll of 0 writes nothing on either.

**`F 00` writes no frame.** The handler adds the byte to `$C694` and writes the frame it lands on;
a step of 0 lands where it is and the ROM writes nothing (the same phrase, row 0 of the table;
`ZF_wave_F00`'s row 0). ChipBoy's `F` returned early on a step of 0 -- it wrote the current frame
again through the sync grid before.

**A bare note that starts a slide takes no finetune refresh.** §163's refresh writes the finetuned
period after a note's own writes; under an `L` the ROM's first write is the slide's first step
(`UN_c05_full`: `NR13 = 6D` in the note's batch, from `762` toward `783`, no `62` before it) and the
slide's steps and landing are on the plain periods. ChipBoy set `fineTunePending` on the bare note
whether or not an `L` came with it, so the note-on wrote the plain period and the refresh the
finetuned one before the slide began; not when sliding now.

## 183. A STEP table's position after a hop: the `A`'s own row plus one

`UNMASKED`'s chain `05` (instrument `07`, STEP table `10` = `A11 / A12 / H00`; tables `11` and `12`
blip an octave and pan left or right, then `A 20`): on the ROM every note of the chain blips and the
pans alternate L, R, L, R; ChipBoy panned twice and then played thirteen clean notes. §179 made the
position after a row's `A` "this note's row plus one", counted from the row the note **started** on;
the ROM's `H` handler (`2:$55F9`) stores the hop's target through the same routine the advance uses
(`2:$5475`, `$C250 + inst`), and the `A` on the row it lands on then stores that row plus one. So
the third note goes 2 → `H00` → 0 → `A11`, position 1, and the cycle is three rows long for ever
where ChipBoy's went to 3 and walked the twelve empty rows behind it (`UN_c05_full`: pans
L R L R L on both sides now; the earlier `UN_c05*` replicas had collapsed table `10` to its first row,
which is why none of them reproduced the song). The position is per lane (`$C250 + inst` for
CMD 1, `$C290 + inst` for CMD 2): a hop in CMD 2 alone moves CMD 2's position, and CMD 1's walks
on from the row it read (`STEP_hopA`: table `W03+A01 / W01 / Z00+H00 / W02`, notes play W03,
W01, W01 then the hop's A, W02, then the hop again). ChipBoy: `stepTableLane()` records the row
each lane read (`tableRowRead[]`, after any hop; an `A` in CMD 1 marks CMD 2 as having read the
same row), the note-on parks each lane at its row plus one, and a note-on's `stepTable()` stops
at the lane whose `A` replaced the table, as the tick does (§157: the new table's row 0 is the
next tick's).

Still different in those replicas, and left: a slide's step lands one period unit off at one of
its updates (`NR13 = 68` against `67` on the second of three steps from `783` to `758`: the
ROM's 1/256-entry offset and division round differently from ChipBoy's semitone fraction); and a
bare note's finetune refresh, 1.1 ms after its plain write on the ROM against 40 cycles in
ChipBoy, which the batch compare shows as one write against two.

## 184. A kit's raw page in video RAM: the LCD's mode-3 reads, modelled

§172 left it: `UNMASKED`'s kits mix through page `$8E`, the font tiles in video RAM, and a CPU read
of video RAM while the LCD is drawing a line (mode 3) returns `$FF`, so about a third of the mixed
bytes come out `FE` -- the "glitchy distorted" kit sound the song is written around (the user's
phrase `6E` report: the character was missing in ChipBoy). Read and measured now:

- **The mixer.** The routine the ROM generates at `$D480` (from the run's RAM dump) is sixteen
  unrolled blocks of the same twenty-three instructions, **140 cycles a block**: the buffer byte
  from HRAM, the second side's from the sample, the two nibble pairs assembled, the page read for
  the high nibble **100 cycles** into the block (`ld a,[hl]`), the swap, the page read for the
  low nibble at **120** (`add a,[hl]`), the byte stored, the pointers stepped. Both reads in mode 3
  give `swap($FF) + $FF = $FE`; one of them gives a nibble `F` on one side.
- **The LCD.** 456 cycles a line, 154 lines a frame (70224), mode 3 from cycle 80 of each of the
  144 drawn lines; the ten lines of VBlank read freely. A line is 3.26 bytes of mixing, so a
  frame of sixteen bytes crosses 4.9 lines and carries four to seven `FE`s, a line apart, in runs
  of one or two (mode 3's length against 140 a byte).
- **The fit.** Over `UNMASKED`'s first twenty-two kit frames (the trace's `$FF30`-`$FF3F` writes),
  a model with those constants and a free LCD phase reproduces the `FE` positions at **348 of
  352 bytes** with mode 3 at **176 cycles**; the four misses are a frame's first byte. The phase
  drifts: the instant is 11712 cycles and the frame 70224, six instants short by 48, so the
  pattern of one instant returns six instants later 48 cycles on -- a slow beat over 160 ms --
  and each instant's pattern is the last one's shifted by 312 cycles of line.
- **What cannot be had.** The console's LCD phase against the song is whatever it was when play
  was pressed; the emulator run had the phase `kLcdPhaseAtStart` (46684 cycles into the LCD
  frame) at its play start, and ChipBoy uses that from its own cycle 0. Even so the song trace
  lines up only in character (267 of 384 bytes over the first twenty-four frames): the ROM's
  frame writes alternate 11800 and 11620 cycles apart -- its handler's cost before the mixer
  swings ±90 cycles an instant, 0.6 of a byte -- where ChipBoy's instants are even, which is
  §160's unmodelled latency again. A listener hears the same character at another alignment.

ChipBoy: `Kit::distVram` (the importer sets it for a raw page in `$80`-`$9F`; the bank JSON
carries it as `distVram`), `kitMixRawLcd()` beside `kitMixRaw()`, and `kitFrame(ch, at)` deciding
each read of a video RAM page against the virtual LCD (`lcdMode3At()`, the constants above). A
page in work RAM (`$D0`-`$D3`, or a raw one elsewhere) is unchanged. The test builds a kit on a
flat page and counts the `FE`s a frame, their runs, and that a work RAM page has none.

## 185. 9.4.0's `R` resets a DRUM pitch; 9.4.2 is the base the other versions map to

`docs/LSDJ_VERSIONS.md` §11 has the 9.2.J-against-9.4.2 probe. One of its three differences is
the driver's: from 9.4.0 a retrigger on a DRUM instrument zeroes the pitch offset word, so the
pitch goes back to the note's entry **without** its fraction (§169's `$7E9` for note `55`, never
the refresh's `$7F0`) and a bend or slide runs on from there -- `X92_Wv_drumR` (`CASTSHDW`'s
kick under `R 03` with its table's `P A9`): `E9`, the R's trigger, then `9F 55 0C C1 …` on 9.4.2
where 9.2.J's `A6 5C 13 C8 …` kept the fraction and the offset. ChipBoy followed 9.2.J.
`retrigger()` now, for a DRUM wave: `fineOffset` becomes minus the fraction (the ROM's word is
relative to the entry), the queued fine, the drum offset and any slide are cleared, and the
fraction's refresh is cancelled. A 9.2 song that rolls a DRUM kick and wants it to keep falling
has no value to import to; that needs an instrument switch (`LsdjModel::retrigResetsDrumPitch`
carries the version's answer for when one exists).

Two more the same probe uncovered on 9.4.2 itself. **A ONCE run's end does not stop a tick
roll**: `X92_Wv_onceR8` (`CASTSHDW`'s kick under `R 08`) writes its flat frame at the run's end
and rolls on, each roll starting the run again; §181's wave stop zeroes the *fast* retrigger
(`$C14E`), and ChipBoy had cleared the tick roll with it. And **a roll's tick skips the frame
step**: under `R 03` the three-tick run's end would fall on every roll's tick and the ROM never
writes the flat frame (`X92_Wv_onceR`), the retrigger starting the run over first; the tick now
skips the frame advance when a roll is due. Left: the roll's own frame write carries one more bend
step than the ROM's (`1DA` against `223` for one instant, batch 21 of `X92_Wv_drumR`) -- the
ROM's writer runs before that interrupt's pitch work and ChipBoy's deferred write after it.

The other two map by value at import (§11 of the versions doc): a wave or kit `R x y` from before
9.3.4 drops its nibble, and a kit `V x y` from before 9.4.0 doubles its depth.

## 186. A kit's samples start over on a roll and on a kit `F`; the note outlives its samples

The `X92_*` probes were the first kit cases in the matrix, and three things showed against 9.4.2:

- **A roll restarts the kit.** `R 04` on a kit note (`X92_KIT_R04`): every four ticks the ROM's
  frames are the kit's first frames again (`68 BD EF FF …` at 0.144 s, 0.206 s …) for as long as
  the note lasts, 263 frames in 0.75 s where ChipBoy played the sample once (45). The retrigger is
  the note-on's kit start: both sides from their start, NR32 with the nibble's level
  (`X92_KIT_RF4`: `NR32 = 40` at the immediate retrigger), the first frame from the next
  instant's mixer.
- **`F xy` on a kit plays the samples again from frame `xy`** -- sixteen bytes a frame -- and does
  so on a bare cell after the samples have ended: `X92_KIT_F01`, a lone `F 01` two rows after the
  note, brings back `NR30 = 80` and the sample's second frame (`06 44 43 32 …`) at 0.206 s.
- **The note outlives its samples.** For the `F` above to work, and for a roll to keep restarting
  a short sample, the channel must still be the note's after the samples end: the ROM writes
  `NR30 = 00` and waits. ChipBoy ended the voice there; now it keeps it, DAC off, until a note-off,
  a kill or the next note -- as a ONCE wave run's end leaves its note (§185).

ChipBoy: `restartKit(ch, frame)` (both positions to `frame · 32` nibbles, the sides live while
inside their samples, `kitOn`, the voice active), called by `retrigger()` for a kit with
`writeEnvelope()` after it and by the `F` handler on a kit; `kitFrame()` no longer clears
`active` when both sides end. `R`'s nibble walks the kit's level as the wave's (§182).

## 187. A kit's vibrato: the depth table in period units, stepped on the instant and the tick

ChipBoy had no kit vibrato at all (`X92_KIT_V42`: the ROM's NR33 moves every frame, ChipBoy's
never). Read off the trace on 9.4.2 and 9.2.L (the halving of 9.4.0 gives the two together):

- **The swing is the depth table's entry, in period units.** `V 42` on a kit at period `$749`:
  9.4.2 walks NR33 by 15 an instant to 45 either side (`73A 72B 71C 725 743 …`), 9.2.L by 30 to
  90. §174's table gives depth 2 = 96 in 1/256 semitones; the kit adds the entry itself to the
  period register -- 96 × the triangle's 30/32 = 90 -- halved from 9.4.0 ("halving kit vibrato
  depths"). The swing goes below the period first, as the wave's goes below the note.
- **The phase moves on the tick as well as the instant.** The ROM's steps double once a tick
  (`-15 -15 -15 +9 +30 +15 …`: eleven instants a cycle where the instant's increment alone makes
  12.8), and not on the note's own tick.
- `V FF` is the changelog's "kit VFF went out of range": 9.4.2 alternates `749`/`349`, 9.2.L
  stays put. Not modelled.

ChipBoy: `kitVibratoUnits()` on the kit's period, the `V` handler starting the pitch clock for
a kit as for noise (and writing no period at dispatch: a kit's goes out with its frames), the
tick advancing the phase once more, and **`Instrument::vibDouble`** -- the instrument setting
the user asked for (the Instrument tab's *Kit vibrato 1x / 2x*, `vibDouble` in the bank JSON) --
keeping the pre-9.4.0 depth; the importer sets it on every kit instrument of a version before
9.4.0 and leaves the `V` bytes alone (§185's doubling of `y` is gone). A kit whose samples have
ended writes no period.

## 188. The pre-9.1 noise channel as an instrument mode: SHAPE, the nibble commands, S MODE

The version sweep's first stop below format 22 is 8.5.1 (format 11), whose noise channel is the
one every release from 3.1.5 to 8.8.6 had before 9.1.0 "rearranged noise notes by frequency".
ChipBoy imported it by finding the ChipBoy note with the nearest LFSR clock for each byte the
ROM would write, which loses the byte itself, and then `S`, `P`, `C` and the table's transpose
column -- all of which work on that byte -- landed elsewhere or nowhere (`NOI_S03`: the ROM's
`10 -> 1D`, ChipBoy `01 -> 01`; `NOI_P02`: the ROM walks `1E 1C 1A …`, ChipBoy holds). Probed on
the 8.5.1 ROM, every noise note and the commands around them:

- **The note.** `NR43 = hi << 4 | lo` with `lo = 15 - SHAPE.lo` and
  `hi = clamp(15 - SHAPE.hi + 3 - octave, 0, 15)`, `octave = (note - 1) / 12` (LSDj note 1 is
  C-3, so every note of C-3..B-3 is octave 0). Each nibble saturates on its own -- SHAPE `0F` at
  C-4 is `F0`, not `FF`; SHAPE `FF` from C-6 up is `00`. The chain row's, the channel's and the
  instrument's transposes move the note before the rule (`chain_tsp_noi`).
- **`S xy`** subtracts each nibble from the running byte's, modulo 16, no borrow (`10 - 03 = 1D`,
  `10 - F3 = 2D`), written at once; **`P xy`** does the same every tick (`P 02`: `1E 1C 1A 18 …`;
  `P FE`: `22 34 46 …`, the high nibble up one and the low up two each tick); **`C xy`** alternates
  the note and the note less the **whole byte**, nibble-wise, a tick each (`C 37`: `10 E9 10 E9`;
  `C 30`: `10 E0`). Two states, not 9.x's three.
- **The table's transpose column** is a **byte** subtraction from the note's NR43 (`tsp 03` on `00`
  is `FD`, `F4` is `0C`), written when the column's value changes from the row before -- and it
  drops the `S`/`P` delta with it (`NOI_S03_tsp`: `1D` then `0D` on row 0's `03`, `10` on row 1's
  `00`; `NOI_P02_tbl`: `P` carries on from the rewritten byte, `10 1E 1C …`). A table without a
  transpose leaves the delta alone (`NOI_S03_tblEmpty`).
- **S MODE** is the noise instrument's byte 2: nonzero is STABLE (every bit tested alone gives it),
  and under it the width bit (bit 3) of every `S`, `P` and `C` result is the note's own
  (`S 03` on `10` gives `15`, on `18` gives `1D`; `P 02` on `10` walks `16 14 12 10`). The table's
  transpose column is not masked (`FD` keeps its bit). 9.1.0 removed the setting.
- No restart on any change (9.2's PITCH does not exist), no `V` (9.0's), the envelope byte is
  NRx2 (§189).

ChipBoy gets a third noise pitch mode, **LSDj shape** (`Instrument::noiseShapeMode`, with
`noiseShape` and `noiseStable`), beside *Note map* and *Manual*: `noiseNr43()` applies the rule
above to the note with its transposes, the table's column as a byte, then the running
`noiseReg` delta (the Register domain's nibble sum of every `S` and `P`) and the chord's byte,
and forces the width bit under STABLE; the tick clears the delta when the table's transpose
column changes. The importer sets the mode on every noise instrument of a `NoiseRule::Shape`
format (3.1.5 - 8.5.1), keeps the LSDj note in the cell (MIDI = note + 35) and the transposes
and `S`/`P`/`C` bytes as they are, and no longer folds chain transposes into phrase copies or
hunts a Shift offset for those instruments. The ROM's own upgrade converts notes and not
transposes or commands (9.1.0's changelog), so a song imported this way plays as its own version
played it, which is closer than 9.4.2 itself gets.

## 189. The hardware envelope stages of 8.1.0 - 8.5.1: the byte, then the next byte with a retrigger

Before 8.8.0 the pulse and noise envelope is the chip's: the instrument's byte 1 is NRx2 and the
chip runs it (`PU_env62`: the ROM writes `62` once; ChipBoy wrote `60` and stepped the level
itself -- the same levels at the same rate, so nothing to map). 8.1.0 added two more bytes (9 and
10) as stages the ROM hands over between, and 8.4.0 - 8.5.1 (format 11) still do it on the
chip. Probed on 8.5.1 with seventeen byte triples:

- Stage 1 is written as it stands with the note. When byte 9 is not `00`, it is written -- with a
  **retrigger** (`NR14 = 87`) -- after `(2 · |vol1 - vol2| + 1) · rate1 / 128` s: the chip has
  stepped `|vol1 - vol2|` levels in its own direction and half a step more (`A3 -> 54` at 0.259 s,
  `83 -> F2` at 0.354, `63 -> 90` at 0.167, `F1 -> 08` at 0.245, `2B -> 81` at 0.306). The
  direction bits play no part in the wait; a stage whose rate is 0 holds for ever (`A0 -> 50`,
  `F8 -> 80`, `28 -> 81`: nothing). Byte 10 follows byte 9 by the same rule (`54 -> 20` at 0.220 s
  after), and a byte 10 without a byte 9 is ignored.
- The noise channel is the same (`NOI_adsr`).
- 7.0.2 and before write byte 1 and nothing else (bytes 9 and 10 mean nothing).

ChipBoy had converted the three bytes to a shaped envelope, which reached each stage's level
half a step early and dropped byte 10's own rate. Now a Chip-mode instrument carries
`envStage2` and `envStage3` (NRx2 bytes, 0 = none): the note writes byte 1 as before, and the
software step (`stepSoftEnvelope()`, which already walks the level at the chip's rate) counts
the stage down in the same units and, at its end, loads the next byte into the level, direction
and rate and writes it with a retrigger through `writeEnvelope(ch, true)`; a note-on or a
retrigger arms stage 1 again, an `E` disarms the stages. The importer's `HardwareStages` law
now maps to this, byte for byte; the software-stage formats (8.8.0 and up) keep §164's machine.

## 190. A kit's `P` steps once more on every tick (9.4.2)

`KIT_P04` on 9.4.2: the ROM's kit period rises 4 units a frame (2.79 ms) -- and every tick by 8
(`59 5D 65 69` at 163 BPM, the double step every 5.5 frames; every 9 frames at 100 BPM, every
3.5 at 255). The tick handler runs the pitch routine for a kit as the instant does, so a kit's
bend gets the tick's step on top of the instant's. ChipBoy stepped it on the instant alone and
fell behind by a step a tick (`5D 61 65`, `BD` where the ROM had `D1` at 0.1 s). `tick()` now
adds one bend step to a kit's `drumOffset` (the FAST/DRUM modes; TICK already steps there and
nowhere else). 8.5.1 does the same, so nothing to map.

## 191. The pulse FINETUNE nibble before 9.x: byte 7 bits 2-5, `8 ×` the 9.x byte

Before 9.x a pulse instrument's FINETUNE (the "PU TUNE" of the older screens) is a nibble in
**byte 7 bits 2-5** (pan below it, duty above); byte 11 is unread (`FTb11_pu1`: the ROM ignores
`40`). Probed on 8.5.1 at three notes: the period drops by `v / 32` of a semitone -- `F` is 3
units at note 34 (6.1 units a semitone), 23 at note 10 (48.7), 0 at note 50 (1.2) -- which is
exactly 9.4.2's `F 0v` on PU1 (`C696 = v · 8` in 1/256 semitone, §78). The importer reads the
nibble for formats 4 - 14 (5.7.8 - 8.5.1; `fineTuneNibble`) and stores `8 · v` in `fineTune`,
so the 9.x driver plays it. 3.6.8 - 5.0.3 read the nibble as period units (`F` is 15 units at
note 34), which is another law (docs/LSDJ_VERSION_MAP.md's list); 3.1.5 - 3.5.1 have none.

## 192. `DIST` pages by version, selectable in the Kits tab; `W` on a MANUAL wave instrument

Two reports against the 9.2.L import of `UNMASKED`:

- **The `?8E00` kit.** Its `DIST` byte names page `$8E`, video RAM during playback, which §184
  models (`FF`s in LCD mode 3, the font block otherwise). The page reader only knew the 9.4.2
  ROM: `lsdjRawPages()` returned the block for the version string `"9.4.2"` and nothing else, so
  the 9.2.L import clipped instead. Video RAM was dumped on every ROM of the archive
  (`tools/lsdjref`'s new `--dump F:ADDR:LEN:FILE`, `/root/lsdj/probe/vramdump.py`, at two frames of
  playback): pages `$82`-`$87` and `$9B`-`$9F` are zero everywhere; `$89`-`$8C`, `$8E`-`$8F`,
  `$91`-`$93` and `$95`-`$97` are tile blocks copied from the ROM at fixed distances from the
  font block, whose offset moves with the version (`$784A9` on 3.1.5, `$7845C` from 3.4.4,
  `$7845B` from 3.6.8, `$78446` from 3.8.7, `$7843C` from 4.5.4, `$78457` from 5.0.3, `$7843C`
  again on 8.5.1, `$7842A` on 9.2.J - 9.4.2); `$80`, `$81`, `$88`, `$8D`, `$90`, `$94` and
  `$98`-`$9A` are drawn at run time and stay unread. The reader now takes a version-keyed table
  of the font block's offset (the newest entry at or below the ROM's version) and the zero
  pages, for every version.
- **The Kits tab showed the page as *Wrap*** (the sixth `KitDist`, `Raw`, had no button, and the
  segmented control clamped) and choosing anything lost it. The tab has a *Page* choice now,
  enabled while the kit carries a page and named by it (`Kit::distPage`, `8E`); the table stays
  with the kit through the other choices, so the harsh tone is a click away again.
- **`W xy` on a wave instrument whose PLAY is MANUAL** starts no frame run on the ROM (`Wv_W12`
  on 9.4.2 and 8.5.1: one batch), where ChipBoy's `U` started one; the importer drops it there
  with a note. `Wv_W12_run` (PLAY set) is the same on both ROMs and on ChipBoy.

## 193. Kit numbers count the ROM's kit banks (a correction to §172)

`READROOM` from 9.2.L: chain `1C`'s hits landed on the wrong kits or on none. §172 had "kit
number `k` is ROM bank `k + 8`", read off `0:$1478`, and the importer followed it. Probed on
both ROMs with a kit instrument naming kit `13` where banks `1B`-`1F` are empty: the ROM
streams the first sample of `KK3-TH`, the **twentieth kit bank** (`20`), not bank `1B`; kit
`1F` on 9.2.L streams `AMEN2` (the thirty-second kit, bank `2C`), and kit `18` on 9.4.2 --
past its twenty-one kits -- streams nothing. So the number is the kit's **position in the
ROM's list of kit banks**, empty banks skipped, on 9.2.L and 9.4.2 alike; `lsdjKitByNumber()`
is that again. It is why the user's song plays different kits on their 9.4.2 ROM, whose list
is shorter: `AMEN2`'s `1F` is off its end.

## 194. Custom DIST: LSDj's raw page as a kit's own table

§192 made an import's raw page a *Page* choice that existed only when a page had been read.
The user's design: a **Custom** DIST that any kit can have, so the "bad" DIST values work for
every import and can be made from scratch. `KitDist::Raw` is that choice now (named *Custom* on
the Kits tab, `"raw"` in a file as before): picking it on a kit without a table fills the table
with the curve the kit had (`kitDistEntry` row by column), so nothing changes until the bytes
do; the table stays with the kit through the other choices and in the file whatever the choice.
The tab shows the 256 bytes as sixteen lines of hex (`HexPage`, every edit that parses to 256
bytes applied), with *Randomize*, *Zero* (LSDj's blank video RAM pages) and *Load…* (the first
256 bytes of any file), and the **LCD holes** switch, which is §184's mode-3 read (`distVram`)
made a property of the table rather than of the page's address. An import sets the table and
the switch for a page in video RAM, as before.

## 195. A roll leaves a DRUM pitch running: the instrument says which

§185 measured 9.4.0's reset and left the older behaviour unmapped by decision; the decision has
changed. `Instrument::retrigKeepsPitch` (the Instrument tab's *R on DRUM pitch*: Resets / Keeps)
makes `retrigger()` skip the offset reset, so the refresh's exact period arrives and the rolls
keep sliding, as 9.2.J - 9.3.9 and every earlier version played it. The importer sets it from
the model (`retrigResetsDrumPitch` false).

## 196. The pulse FINETUNE nibble before 5.7.8: period units, capped

§191 read the nibble as `v/32` of a semitone (5.7.8 - 8.5.1). On 3.6.8 - 5.0.3 the same nibble
takes `v` **period units** off the register whatever the note (`F` is 15 units at note 34), which
is a different amount of pitch per note; 3.1.5 - 3.5.1 have no finetune. The user's decision: map
as closely as possible and cap. One unit is about 42/256 of a semitone at the middle of the
keyboard, so the importer stores `min(255, round(42.2 · v))` (`fineTuneUnits` on the model): a
nibble of 6 or more is capped at a semitone.

§189 addendum: an `E` over running hardware stages ends them (`PU_adsr_E`, `_E2` on 8.5.1: the
`E`'s byte with a retrigger, then no stage byte follows), which is what ChipBoy does.

## 197. Any instrument on any channel, read as LSDj reads it

LSDj keeps an instrument as sixteen bytes and a channel reads whichever instrument a cell names
by its own layout: a WAV instrument on PU1 is a pulse whose envelope is byte 1 and whose duty is
byte 7, a pulse on NOI a noise instrument, a kit on PU2 a pulse (the `X942_*` probes, all six
identical to the ROM through the importer's per-channel copies). The user's decision
(docs/plan-any-channel.md): ChipBoy does this itself and the copies go.

- The importer's `buildInstrument` body for pulse, wave and noise, and the envelope laws with
  it, are `lsdj::decodeInstrumentBytes()` in `core/Import/LsdjInstrument.cpp`; the importer
  reads every instrument through it and keeps the sixteen bytes and the format on the
  instrument (`InstrumentCore::lsdjFormat`, `lsdjBytes`, in the file as a 32-digit string).
- The driver, where a cell's instrument is not the channel's kind (`typeFits`), reads it as
  that kind through `crossKind()`: the save's own bytes under the model of their format, or,
  for a ChipBoy-native instrument, `lsdj::encodeInstrumentBytes()`'s 9.4.2 layout (lossy where
  ChipBoy has more than LSDj -- a Shaped envelope encodes as its Chip fields). PU1/PU2 read a
  pulse, WAV a wave (a kit fits already), NOI a noise instrument; a kit's number is never
  needed, since a kit read on another channel is a pulse or a noise instrument.
- The bank's noise map is set up front for a Map-rule import, so a noise channel's reading of
  a pulse finds it. The variant slots, their names and their import notes are gone: a cell
  names the instrument itself.

## 198. The wave run's PLAY and REPEAT before 7.7.6 (6.8.2, 7.0.2)

The 7.0.2 sweep showed every wave case retriggering every four ticks where ChipBoy held one
frame: the probe's wave instrument has PLAY 0, which 9.x reads as MANUAL. Probed on 6.8.2 and
7.0.2 with sixteen tagged frames, LENGTH 4 (`10 = 0C`) and SPEED 0, every PLAY value and the
nibble in either byte (`WvPlay*`, `WvP*` in `vs_sweep.py`):

- **PLAY (byte 9) is its low two bits: 0 ONCE, 1 LOOP, 2 PINGPONG, 3 MANUAL** (4-6 read as
  0-2, 7 as 3). 9.x has MANUAL 0, ONCE 1, LOOP 2, PINGPONG 3, RESYNC 4 (8.5.1: 4 is one frame).
- **The loop nibble is byte 2's low nibble** on these two ROMs as on 9.x (byte 3's nibble
  moves nothing; §93's "byte 3 on formats 7-8" was wrong), and it **counts the loop's steps less
  one** from the run's end: 0 holds the last frame, 1 ping-pongs the last two (`AA FF AA FF`), 2
  the last three, 3 and up the whole four-step run. 9.x's LOOP POS is the other way round --
  the steps *before* the loop, so F holds the last frame there and 0 loops everything. 7.5.0
  "changed default wave repeat to F" (loop everything under this law) and 7.7.6 renamed the
  field to LOOP POS, which is where the law is assumed to have turned; no ROM between 7.0.2
  and 8.4.0 is in the archive, so 7.5.4 - 7.7.5 (format 9) take the old laws and 7.7.6 - 8.0.0
  (format 10) the new, unmeasured (`wavePlayOld`, `waveRepeatCount`; the 7.5 model is split
  in two at 7.7.6).
- The run itself is 9.x's: LENGTH frames spread across the sixteen (0 5 A F), SPEED + 4 ticks
  each, ONCE holding its last frame.

## 199. A ping-pong run's first pass

`WvP3_b2_0E` (9.4.2, 8.5.1: PLAY PINGPONG, LOOP POS E) and `WvP2_b2_01` (7.0.2, REPEAT 1): a
four-step run whose loop is its last two steps plays `0 1 2 3 2 3 2 3 …` -- the first pass walks
every step and the bounce is inside the loop afterwards. ChipBoy turned at the loop step on the
way up as well and played `0 3 2 3 …`: the turn now happens only on the way down.

## 200. Before the frame run, a wave instrument's PLAY 0 is a one-tick note

The 6.0.1 sweep turned the DAC off one tick after every wave note (`NR30 = 00` at +17 ms) where
ChipBoy held it, and `WVb9_03` alone kept sounding. Probed with byte 9 at 01, 02, 03, 04, 08, 10,
20, 40 and FF on 3.6.8, 5.0.3, 6.0.1 and 6.4.5: the low two bits are the PLAY mode on these
formats as well, and **0 is ONCE** -- the one frame plays a tick and the channel goes quiet (the
6.x ROMs after 17 ms, 3.6.8 and 5.0.3 within 2 ms); 1, 2 and 3 hold it. §89's "frame 0 held"
was measured with a nonzero byte. The decoder gives such an instrument a ONCE run of one frame
at one tick (`frameAdvance = 1`), which ends the way §171's ONCE does; the two milliseconds of
the older ROMs are a tick here (the gap list).

## 201. The wave run before format 7: PLAY, the loop's tail and W (5.7.8, 6.0.1, 6.4.5)

Sixteen tagged frames (`f * 11`), PLAY 1, 2 and 3 in byte 9 against the byte 2 nibble at 0, 1
and F, each under `W12`, and PLAY 1 under `W00`, `W10`, `W20`, `W1F` and `W32` (`W6_*`). The
same on 5.7.8, 6.0.1 and 6.4.5:

- PLAY 3 is MANUAL: no W moves the frame. PLAY 1 is LOOP, PLAY 2 PINGPONG, PLAY 0 ONCE (§200).
- The instrument has no LENGTH or SPEED of its own: the run is the one frame at a tick a step
  until a W gives it `x` ticks a frame (0 keeps) and `y + 1` frames, spread across the sixteen
  as 9.x's are (`W12`: frames 0, 7, F; `W1F`: all sixteen). A speed-only W on the one frame
  moves nothing.
- The loop is the byte 2 nibble plus one steps counted from the run's **end**, whatever a W
  makes the length: nibble 0 holds the last frame once the walk is done (`00 77 FF FF ...`),
  1 keeps the last two (`77 FF 77 FF`, LOOP and PINGPONG alike), F the whole run (PINGPONG
  `00 77 FF 77 00 ...`). That is §198's rule without the LENGTH byte.

ChipBoy: a `frameLoopTail` on the instrument -- 0 keeps the loop at `frameLoopStep` (9.x, where
§131's `U` keeps the loop's *frame*), n makes the loop the run's last n steps and a `U` that
changes the length puts it at the new end. The decoder sets it from the REPEAT nibble on every
model before 7.7.6 (the `waveRepeatCount` ones and the formats before the run), and before
format 7 reads PLAY 1, 2 and 3 as LOOP, PINGPONG and MANUAL over a one-frame run at one tick a
step. The importer's "W on MANUAL is dropped" check now reads MANUAL by the version's own PLAY
encoding (byte 9 & 3 == 3 before 7.7.6, byte 9 == 0 after); it read byte 9 == 0 on every
version, which on 7.x is ONCE, and dropped the W there. 5.0.3 writes the third frame of a
`W12` a tick late (`0.134` for 5.7.8's `0.118`); that is the format-3 sweep's.

## 202. A retrigger starts the instrument's table over a tick late before 8.3.4

Six transpose steps in the instrument's table under `R03` (`Rtbl6_R03`), on 9.4.2, 8.5.1,
7.0.2, 6.8.2, 6.4.5, 6.0.1, 5.7.8 and 5.0.3. From 8.5.1 the retrigger and the table's row 0
are one batch (`TRIG NR13=9D`, §182). On 7.0.2 and every older ROM the retrigger's tick runs
the row the table was on, the trigger comes alone, and row 0 is written on the tick **after**
(`0.126 NR13=AC`, `0.127 TRIG`, `0.141 NR13=9D`); the immediate fire at the note-on does the
same, so its row 1 comes two ticks after row 0 where 9.4.2's comes one. A table without an R
starts on the note-on's own tick on every version (`Rtbl6_plain`). The changelog's 8.3.4 entry
("R command restarted table one tick too late") places the change; 8.1.0 - 8.3.3 are taken as
late with it, there being no ROM.

ChipBoy: `retrigTableLate` on the instrument (the importer sets it on every model up to
8.0.0). The retrigger sets a flag, the table phase of the next tick starts the instrument's own
table from row 0 as §182's replay does on the retrigger's own tick, and that tick's table phase
runs its row as any other.

## 203. The vibrato's depth ladder by version: five ladders and two unit laws

`V 0d` for every depth at C3 (period `$416`, 56 units a semitone, speed 0 so the waveform's
peak entry is hit) on every archive ROM (`VLo_0*`), and `V 4d` at D#6 (`$797`, `V4*_pu`). The
swing's peaks, up and down, give the multiplier of §174's ±32 waveform (1/256 semitone) or,
before 5.7.8, the period units:

- **7.7.6 - 9.4.2** (8.5.1, 9.2.J, 9.4.2; the changelog's 7.8.1 "new vibrato depth table"):
  §174's ladder, `1 2 3 4 6 8 12 16 20 24 28 32 40 48 56 64` for depths 0-F; `V x0` is a
  vibrato of an eighth of a semitone. 8.5.1's fine steps round toward zero where 9.x's round
  to the nearest unit (`11` for 9.x's `10` at one step of `VLo_02`); a unit, left.
- **5.8.8 - 7.7.5** (5.8.8, 5.9.9, 6.0.1, 6.4.5, 6.8.2, 7.0.2; 7.6.5 "adjusted Vx7" is
  unmeasured): `1 2 3 4 6 8 11 15 19 24 29 35 42 49 56 64` -- 6-D between §174's and 5.7.8's.
  (`VLo_00` is `V00`, which starts a vibrato on 5.8.8 - 6.4.5 and from 9.1.0 (§151) and does
  nothing on 6.8.2 - 8.5.1 and before 5.8.8; it says nothing about depth 0, which `V40` shows
  on: ±1 at D#6 on 7.0.2.)
- **5.7.8**: `0 1 2 3 4 5 7 9 11 13 16 19 22 25 28 31` -- half the swing of everything after.
- **3.7.5 - 5.0.3** (3.7.5, 3.8.7, 3.8.9, 3.9.2, 4.3.0, 4.7.3, 4.8.0, 4.9.4, 5.0.3): **period
  units**, the same sixteen integers as 5.7.8's times the note's frequency divider `(2048 -
  period) >> 6` -- 15 a step at C3, 1 at D#6 -- and the downward half short by a
  thirty-second: `x - (x >> 5)` (`-451 / +465` at depth F, `-16 / +16` at D#6).
- **3.6.5 - 3.6.8**: the same, symmetric (`±465`).
- **3.1.5 - 3.5.1**: §'s one-sided law as before, and its depth depends on the note in a way
  this round did not settle (8 units a step at C3, 40 at D#6; the gap list).

Before 7.7.6 the waveform's downward half peaks at 31 where the upward peaks at 32 (5.7.8,
6.0.1, 7.0.2: `-81 / +76` at C3 for a multiplier of 11, `-58` for 8 where 8.5.1 and 9.x give
`-60`); the unit laws' thirty-second is the same entry.

ChipBoy: a `vibLadder` on the instrument (`Lsdj9`, `Lsdj58`, `Lsdj57`, `Units39`, `Units36`),
set by the model; `vibratoFine()` reads the depth through the ladder,
and the unit laws run in `vibratoDrumUnits()`, where every pre-5.7.8 instrument's pitch lives
(the register law's DRUM speed). The models split for it: 5.7.8 alone, 5.8.8 - 5.9.9 and 6.0.1
- 6.4.5 (§205), 3.6.5 - 3.7.4 and 3.7.5 - 3.9.2.

## 204. `F` on the pulses by version: nothing, period units, then a thirty-second of a semitone

`F12`, `F30`, `F0F` at C3 on PU1 (`F*_lo_ch0`): 9.4.2 and 5.7.8 move the period by y/32 of a
semitone down (`-4`, `0`, `-28`; 5.7.8 two milliseconds after the trigger); 5.0.3 by **y period
units** (`-2`, `0`, `-15`, and `F03` at D#6 `-3`); 4.9.4, 4.8.0, 4.7.3 and 3.6.8 not at all. So a
`fineCmdLaw` on the model -- none before 5.0.3 (4.9.5 - 5.0.2 unmeasured), units on 5.0.3
(its own model now, §205), a thirty-second from 5.7.8 -- and the importer drops the F with a
note where it did nothing; under the register law the driver's F on a pulse sets
`fineUnits`, a period offset the DRUM pitch carries, cleared by the note-on as the semitone
finetune is. PU2 was not probed (the rig's second-channel song is wrong) and takes the same.

## 205. `P` reaches the noise channel from 5.4.4; the wave loop nibble from 6.0.1; the models

- `P02` on noise (`NOI_P02`, `P02_ch3`, `PFE_ch3`): 5.7.8 walks NR43 a tick; 5.0.3, 4.7.3,
  4.3.0, 3.9.2 and 3.6.8 write nothing. The changelog dates it: 5.4.4 "enabled P command for
  noise channel" (5.4.3 the C, §'s `noiseChord`). `noiseP` on the model; the importer drops it
  with a note before.
- The wave instrument's loop nibble (§201) is read from 6.0.1: on 5.9.9, 5.8.8, 5.7.8 and 5.0.3
  a `W12` under nibble F holds the last frame as nibble 0 does (`W6_p1_rF_W12`). `waveRepeatNibble`
  on the model; the tail is 1 without it.
- New models, all from measured ROMs: 6.0.1 - 6.4.5 (formats 4-6, the one a format-4 or -5 save
  takes without a ROM), 5.8.8 - 5.9.9 (format 4), 5.7.8 (format 4), 5.0.3 (format 3, the
  one a format-3 save takes), 4.8.0 - 4.9.4 (format 3), 3.7.5 - 3.9.2 (format 2), 3.6.5 -
  3.7.4 (format 2).

## 206. Under the register law a note's `L` slides from the period the channel had

`L03` on a second note (`L03_second_ch0`, 5.0.3): the note-on triggers at the **old** period
(`TRIG` with NR13 unchanged at `97`) and the pitch walks three units an instant to the new
note (`9A 9D A0 ...`); the bare note's `L02` (`bare`) the same at two. ChipBoy's register-law
slide started from the new note's period plus the DRUM offset, which at a note-on is the new
note itself, so the trigger carried the target and nothing slid. Now the note-on's L takes
the last period written as its start and puts the difference in the DRUM offset, so the
trigger carries the old period and the offset walks to zero. A first note's `L` (no period
before it) still plays the note where 5.0.3 slides up from period 0 over seconds, 5.7.8 stays
at 0, and 5.8.8 - 6.4.5 chirp from it for four milliseconds (the gap list).

## 207. The 3.x noise channel: S crosses the width bit, the table's transposes add up nibble by nibble, the chain's do nothing

`S03` on a noise note whose NR43 is `10` (`NOI_S03_b2*`, byte 2 nonzero): 3.1.5, 3.5.1, 3.6.8,
3.9.2 and 4.0.4 write `1D` -- each nibble less the command's, the width bit (bit 3) crossed --
where 4.1.0, 4.3.0, 4.4.0, 4.5.4 and 4.7.3 write `15`, the width bit kept from the note; with
byte 2 zero they cross it as 3.x does (`NOI_S03*` on 4.3.0 and 4.7.3), so §188's S MODE holds
from 4.1.0 (the changelog's 4.0.5) and 4.0.4 has none. A table's transpose column (`tsp_tbl_noi`: 3, 7, C, F4 with an H): 4.0.4
and later write the note's byte less the row's, `FD F9 F4` (§188); the 3.x ROMs subtract each
row's value **from the running byte, nibble by nibble** -- `0D 06 0A 07`, the H row's own
column skipped as the hop lands on row 0 -- and `NOI_S03_tsp` shows the S's byte taking the
rows' subtractions on top (`1D 1A 17 14`). A chain transpose on the noise channel moves nothing
on 3.1.5 - 3.5.1 (`chain_tsp_noi`: `10` where 3.6.8 and later write `00`).

ChipBoy: `noiseStable` on the model is a rule now -- Free (3.1.5 - 4.0.4), Byte2 (4.1.0 and
later) -- so 4.0.4 is its own model and 4.1.0 - 4.3.0 the next; `noiseTspNibbles` on the instrument makes the table's column an
addition to the running byte when its row is read, no byte subtraction from the note and no
rewrite on change; `noiseChainTsp` off on the model drops a chain transpose on the noise channel
with a note.

## 208. Before 3.6.5 the pulse FINETUNE byte is not read at all

`FT_pu1` on 3.1.5 (byte 11 = `40`): the note plays at `97`, ChipBoy at `95` -- the decoder read
byte 11 as 9.x's finetune on every model without the nibble or unit laws. It reads it from
format 15 only (8.8.6, where §191's nibble is gone); 3.1.5 - 3.5.1 have no finetune.

## 209. The late table restart is not kept: every project takes 8.3.4's timing

§202 measured the tick-late restart and gave it a flag. The flag is gone: the user chose the
one timing over a toggle that only says which ROM a song came from, and the 8.3.4 fix is what
the command was always meant to do. A project from 8.0.0 or before plays its `R` tables one
tick earlier than its ROM did -- the row after a retrigger comes a tick sooner -- and the
difference is on the version map's gap list. Nothing in the file format carried it long
enough to need a reader.

## 210. One vibrato depth scale in place of the five ladders

§203's ladders are close to one another once the eye is off the register: 5.8.8 - 7.7.5's
integers sit within a step of 9.x's, 5.7.8's are 9.x's halved, and the 3.6.5 - 5.0.3 unit laws
land between 0.8 and 1.3 times 9.x's semitones across the keyboard (1.25 at C3, 0.8 at D#6 for
depth 6). What matters to a song is the rate, which every version shares, and the depth being
near; the one's-complement downward half (a unit) and the unit laws' key-dependence are not
worth a mechanism.

ChipBoy: `vibScale` on the instrument -- `One`, `Half`, `Double` -- read by `vibratoFine()`
(9.x's ladder, then the scale) and by the kit path, where 9.4.2's depth is `One` and §187's
older, unhalved depth is `Double`; `vibDouble` folds into it. The importer sets `Half` on the
5.7.8 model, `Double` on a kit from before 9.4.0, `One` everywhere else. The ladders, the
complement and the unit laws are gone from the driver; the version map keeps the measurements.
The control is "V depth" (½x / 1x / 2x) on every instrument type, in the vibrato group. A file
that carries `vibDouble` reads as `Double`, one whose `vibLadder` is 5.7.8's as `Half`.

## 211. Wave loop points anywhere: a loop's end, a loop counted from the run's end, runs past sixteen

§201's `frameLoopTail` said "the loop is the run's last n steps" as a version rule. It is a
loop point now, and the run has two of them:

- `frameLoopStep` is where Loop and Ping-pong come back to, as before; `frameLoopEnd` is the
  step they turn at -- 0 the run's last step, otherwise the loop's last step, one-based, so the
  first pass plays the run up to it and the loop is `[frameLoopStep, frameLoopEnd - 1]`.
  Steps past the loop's end play only in One-shot.
- `frameLoopFromEnd` counts `frameLoopStep` back from the run's last step instead: 0 is the
  last step, n the n steps before it, whatever the length is at the time. That is exactly
  what every LSDj before 7.7.6 did with its REPEAT nibble, and it survives a `U` that changes
  the length mid-note without a rule of its own: the loop is measured from the end when the
  step is looked up. With it on, the loop's end is the run's end and `frameLoopEnd` is not
  read.
- `frameLength` runs to 64. Up to the wave's own frame count the run is spread across them
  as §65 says; past it the run is consecutive frames from the instrument's start frame, so
  a run of 40 walks two and a half slots of the flat table (§171's unwrapped steps).

A `U` that changes the length keeps the loop's frame when the loop is counted from the start
(§131, measured on 9.x) and needs no help when it is counted from the end. The importer sets
`frameLoopFromEnd` with `frameLoopStep` = the REPEAT nibble on every model before 7.7.6 (0
where the nibble is not read, §205), and the 7.7.6 rule -- `frameLoopStep = length - (16 -
nibble)` from the start -- after. The controls: "Frames" 0-64, "Loop from" 0-63 (shown as
"end", "end-1" ... when counted from the end), "Loop to" (end, 1-64), "Loop counts" (from the
start / from the end). The run buffers grow to `kMaxRunSteps` = 64.

## 212. The song's end: each channel plays its chain round again, or stops

Probed on 9.4.2 and 6.0.1 (`songend.py`: `one_chain`, `two_vs_one`, `gap_row`, `two_rows`,
`late_start`, `start_later`; note-on times and NR13 per channel over forty seconds): a channel
that meets an empty song step goes back to **its own** song row 0 and plays its chains again,
on its own clock and whatever the other channels are doing -- two chains against one gives the
one twice as many passes -- a channel whose row 0 is empty never plays at all, and rows after
an empty step are never reached. Playback never stops; the song is four independent loops
that only line up when their lengths do.

ChipBoy laid rows past a chain's end as empty rows, end to end (§25), so a short channel fell
silent while the long one played on, and the own transport looped the longest chain. Now:

- `Song::chainEnd[4]` -- `Loop` (the ROM's way, the default, and what a file without the key
  reads as) or `Stop` (what ChipBoy did). `rowAtTick()` wraps a looping channel's tick by
  its chain's length -- the rows up to its last phrase; trailing empty rows do not count --
  and hands the pass number to the Player, which fires a row again on a new pass even when the
  chain is one row long. The tempo map repeats a looping channel's `T` cells over the passes
  the longest chain lasts. `rowStartTick()` and `songTicks()` are unchanged: the own transport
  still loops the longest chain, so a wrap of the transport starts every channel over
  together where the ROM lets them run apart.
- The importer leaves every channel on `Loop` -- a channel's chain ends at its first empty
  song step, which is where the ROM goes round; an `H F F` is the song's stop (§214), not a
  channel's.
- The chain view's head carries one toggle per channel, a loop or a stop glyph in the
  channel's colour; a click switches it.

An empty row *inside* a chain is still ChipBoy's rest of sixteen steps (§25) -- the ROM
cannot express it -- and a channel with no phrase at all is silence either way.

## 213. Every field the importer sets has a control

`noiseTspNibbles` (§207) gets "Table TSP" in the LSDj-shape noise controls -- "Resets" (each
row's transpose is taken from the note's byte) / "Adds up" (each row's is added to the running
byte, 3.x) -- enabled in shape mode. With §210's "V depth", §211's loop points and §212's
channel end, nothing an import sets is out of the user's reach any more; `retrigTableLate`
is gone (§209) rather than exposed.

## 214. `H F F` stops the song, every channel, at that step: §120 corrected

§80 and §120 measured `H F F` on a single-channel song and read "the channel stops". With two
channels (`songend_hff.py` on 9.4.2: `hff_noi`, `hff_pu1`, `hff_row1`) the other channel stops
at the same tick -- PU1's note on the very step NOI's `H F F` sits on is not played, and a
three-row PU1 chain against an `H F F` in NOI's second row gets three note-ons and no more.
The manual says it: "HFF: stop playing song (or channel, if in live mode)". §212's per-channel
loop made the misreading audible: a song whose one `H F F` sits on NOI would have had the
other three channels going round for ever where the ROM is silent (`EGOFLEX`, `CASTSHDW`,
`SAMESONG` in the user's save all end that way).

ChipBoy: the cell keeps its `H F F` -- it is the user's to see, move and delete -- and it
means the same thing here: **the song stops before this step's notes, on every channel**. The
play order treats `H F F` as a step of its own, not a counted hop (§102), so the step has a
tick. `buildRowTables()` finds the earliest `H F F` over the four channels and sets
`Song::stopTick` (not in the file; a channel's first pass, since the tick is absolute);
`songTicks()` ends there, so the own transport loops the song at its stop and a run without
loop stops there; the Player fires a note-off on every channel at that tick and nothing after
it until the timeline jumps back. The importer keeps the phrase to the `H F F`'s step (its
note dropped: the ROM never plays it), still ends that channel's chain there, and leaves
`chainEnd` alone -- §212's Stop is ChipBoy's own toggle, not an import.

## 215. The wave RAM write by version: plain, muted, pre-triggered

The user's 4.1.0 save (`Computer_Savvy`, kits on every song) came back "lo-fi and distorted"
against the ROM on an emulator. Its kits import and their samples match the ROM's frames byte
for byte (§214's session, `/root/lsdj/cs/`); what differs is how each frame is **written**.
`KIT_plain` on every archive ROM, one frame's writes:

- **3.1.5 - 4.6.9**: `NR30 = 00`, the sixteen bytes, `NR30 = 80`, `NR34 = 87` (the trigger,
  the period's high bits), `NR33 = 49`. Nothing else; the wave note-on's burst is the same.
- **4.7.3 - 8.5.1**: the same inside `NR51` with the wave's two bits cleared before and put
  back after (the changelog's "slightly reduced noise of kit and wave instruments").
- **9.2.J - 9.4.2** (8.8.6 unmeasured, taken as the muted form): §172's sequence -- the
  mute, `NR30 = 00`, the bytes, `NR30 = 80`, `NR33 = E0`, `NR34 = 87`, the pan back, then
  the real period without the trigger bit.

ChipBoy wrote every frame 9.x's way. The `NR51` mute is a step in the mix for the length of
the write, every frame, which is a buzz at the frame rate on a DMG and in ChipBoy's mix alike
-- what 4.7.3 added and 9.x kept, and what a 4.1.0 song never had; the `$7E0` pre-trigger
races the first samples of every frame. Now `waveWrite` on the instrument -- `PreTrigger`
(9.x, the default, and what a file without the key reads as), `Muted`, `Plain` -- decides
the burst in `waveRamBurst()`, which writes the period itself: the ROM's order (`NR34` with
the trigger, then `NR33`) under `Plain` and `Muted`, §171's pre-trigger then the period under
`PreTrigger`. The models carry it (4.4.0 - 4.7.3 splits at 4.7.3, a twentieth model); the
control is "RAM writes" on wave and kit instruments. Left: 4.x's note-on writes the trigger
twice (the burst's and the note-on's own, `NR33=83 NR34=87` after `NR32`), a write ChipBoy
does not make.

## 216. An instrument column without a note ends the pitch effects in force

`SUNSET` (the user's `Live_Set_Main.sav`, played on 9.1.C), PU2 phrase `2F`: step 0 a kick --
instrument `05`, whose table bends the pitch to the floor with a `P BB` and kills the note at
row 5 -- step 1 instrument `0B` (an arp, no table, `C 47`) with **no note**, step 2 a bare
`A-5` with `E 2E`, then the chord runs. On the ROM step 1 writes `0B`'s volume in zombie
mode (`NR22 = 09 11 18 ...`, no trigger) and from then on the channel is `0B`'s: step 2 sets
the period of the plain note without a trigger and the chord cycles `76B 78A 79D` on it,
clean. ChipBoy reloaded the instrument at step 1 (the same zombie writes) but left the kick's
bend running on the voice, so the bare note and every chord step after it fell to the floor
again -- the kick sound continuing under the arpeggio. The ROM's instrument load resets the
channel's pitch effects as a note-on does; only the pitch register stays where it is until
the next update.

ChipBoy: `reloadInstrument()` -- the cell's instrument column and Live follow -- clears what
a plain note-on clears of the pitch state: the `P` bend and offsets in every mode, a slide in
progress, `F`'s units, the noise bends. It writes no period; the next note or update does.

## 217. A transpose below the channel's floor comes round by octaves

`CLD GRND` (the user's `Cold_Grenade.sav`, 8.8.6), chain `28` on the wave channel: phrase `7B`
holds `F-2` and `7C` holds `C-3`, both instrument `15`, under the chain transposes `00 F4 FD FD
F9 FB F8 FE` and the project's TRANSPOSE `FE`. Steps 1, 4 and 6 (and 9, C, E) were silent in
ChipBoy and sound on the ROM; with the project transpose at 0, steps 4, 6, C and E. Those are
exactly the steps whose note index -- the phrase's note plus the chain row's transpose plus the
song's -- lands at 0 or below: `C-3 - 12 - 2`, `F-2 - 7 - 2`, `F-2 - 8 - 2`.

Measured with `probe/tsp_low.py` (a chain of the same note under one transpose a step) on
3.1.5, 4.1.0, 5.8.8, 8.8.6, 9.1.C and 9.4.2, pulse and wave alike -- the wave's note table is
the pulse's, `C-2` (index 1) is period `02C` on both:

| note | chain transpose | index asked | period written | note played |
|---|---|---|---|---|
| `C-3` (13) | `F4` (-12) | 1 | `02C` | `C-2` |
| `C-3` | `F3` (-13) | 0 | `3DA` | `B-2` (12) |
| `C-3` | `F0` (-16) | -3 | `312` | `G#2` (9) |
| `C-3` | `EC` (-20) | -7 | `1C9` | `E-2` (5) |
| `C-3` | `E8` (-24) | -11 | `02C` | `C-2` (1) |
| `C-3` | `E0` (-32) | -19 | `1C9` | `E-2` (5) |
| `C-3` | `D0` (-48) | -35 | `02C` | `C-2` (1) |
| `C-3` | `C0` (-64) | -51 | `312` | `G#2` (9) |
| `C-3` | `80` (-128) | -115 | `1C9` | `E-2` (5) |
| `C-4` (25) | `E0` (-32) | -7 | `1C9` | `E-2` (5) |
| `C-4` | `C0` (-64) | -39 | `312` | `G#2` (9) |

Every index at or under 0 comes up by twelve until it is 1..12: the note plays in the bottom
octave, on its own pitch class, never silent and never clamped to `C-2`. The rule is the
ROM's on the note index at the note-on, before the instrument is looked at, so a bare note
(no instrument column) gets it too, and the table's transpose column, a slide and a bend
work from the raised note afterwards. (The table's transpose column is another matter: a
value that takes the note off the table writes periods that are no note's -- `D0` on `C-3`
writes `7E1`, then `7F4` for `E0`, and a second pass of the same rows writes other values --
where ChipBoy clamps the column at the floor. Not changed here; open. Cold Grenade's table `03`
has such a row, `D0` with `L 4E`, which is the ±7-unit wobble the ROM shows on those notes.)

ChipBoy: at a note-on on a pulse or wave voice, when the cell's note is itself in range, the
chain row's transpose in force (`noteTsp`) grows by twelves until the note with its
transposes -- the row's, the channel's, PU2's own -- reaches the channel's lowest note. A
note whose own number is below the floor (a MIDI key, a tracker cell under C2) stays silent
as C4 says; only the transposes come round. The bend wheel and the pitch effects keep
section 125's floor. Both note-on paths get it: the instrument's and the bare cell's
(section 177).

## 218. Project files: an `.lsdsng` is the save's song, an `.lsdprj` brings its kits

The user's `.lsdsng` files (Computer Savvy, eight songs, LSDManager exports of the same save)
import byte for byte as the save's files do -- the eight `.cbsong` pairs differ in nothing but
the tab's name. The stream is the save's, one block after another (plan section 1a).

An `.lsdprj` (LSDPatcher's export; the user's eight 9.2.L songs) appends the kits the song
names after its last block: `usedKits()` in its `LSDSavFile` walks all 64 instrument slots,
takes bytes 2 and 9 of every slot whose type byte is 2, masked `& 3F`, into a sorted set, and
`writeKits()` writes each as the 16 KB bank `kit + 8`, plus 5 past bank 26 (the ROM's code
banks 27-31). Its import maps the same sorted set, in order, onto the banks it places. All
eight files fit the rule (the set's size is the count of trailing `60 40` banks). The bytes
keep the exporting ROM's numbers: SPECIAL DELIVERY's second kit is `1D` in the file and `17`
in the save the ROM beside it matches, both SNARES.

ChipBoy: `projectKits()` reads the trailing banks back from the file's end (at most as many as
the set has) into a list indexed by the song's own numbers, placeholders between, which
`lsdjKitByNumber()` skips; a project's kits come first, the ROM beside the file serves what a
project lacks (an `.lsdsng`, or an `.lsdprj` missing a bank). The dialog's row says "n kits
inside" or "n kits from the ROM", and the ROM line warns only when a chosen file needs one.
Imported from the `.lsdprj` files against the save with its ROM, the eight songs differ only in
the instruments' archived `lsdjBytes` (those numbers); every sample, name and cell is the same.

## 219. The high-speed tempi: 2x, 3x, 6x the screen rate, a byte of their own

`STELLAR` (the user's `BON-VOYAGE` save, 9.2.L) shows its tempo as `3x` and played on the
ROM half again as fast as ChipBoy's import at 295 BPM: the same notes, every interval 1.51 times
shorter, from the first phrase on. Its tempo byte is `27` (295), and the probe of that byte on
the same ROM (`probe/tempo_probe.py`, a note every step at groove 1/1) gives 295.2 BPM, as
`24` gives 292.6 and `00` 256.3 -- so the byte was not the answer.

Read on the ROM (`watch_rom.py` on `$C952`-`$C953`, then the disassembly): the tick word (§160)
is written at bank 7 `$5E04` by the routine at `$5D9A`. It reads `$C52B`: when it is 0 the word
comes from a 256-entry table at 7:`$5E0B` indexed by the tempo byte less 40 (`$600B` on a Super
Game Boy), whose last entries are `6262 6241 6220` -- 293, 294, 295 BPM; when it is 1, 2 or 3
the word is `$0800 × (4 - mode)`: **6144, 4096, 2048**, a tick every third, second and every
interrupt of the six a frame -- 2x, 3x and 6x the screen refresh, which the changelog names
299, 448 and 896 BPM (9.2.I: "new project tempo values"; 9.2.J: "renamed new tempi to
2x/3x/6x"). The loader at bank 1 `$7D2E`-`$7D35` fills `$C52A` from song byte `$3FB4` and
`$C52B` from song byte **`$3FCC`**. `STELLAR` holds `27` and `02`. The `T` command (2:`$6631`)
writes its byte to `$C52A` and zeroes `$C52B`: a `T` puts the song back on the byte's table,
and no `T` value reaches the three tempi.

ChipBoy: `LsdjModel::tempoModes` (the format-22 models: 9.2.J and later; 9.2.I is the first
ROM with the byte, and the model "9.2.J - 9.3.3" reads every 9.2.x); the import takes byte
`$3FCC` in 1..3 as 299, 448 or 896 BPM over the tempo byte, with a note. `tickSeconds()` gives
those three whole-number tempi their exact words -- 6144, 4096, 2048 -- rather than the
rounded quotient; the song tempo's ceiling is 896 (`driver::kMaxSongBpm`) in the parameter,
the tracker head, the song file and the tools, and the parameter's readout says `(2x)`,
`(3x)`, `(6x)` beside them. `T` stays 40-295: its byte has no room, as on the ROM.

## 220. A noise `P` moves at the instrument's command rate

`NOSTLGIA` (the user's `BON-VOYAGE` save, 9.2.L, at the 3x tempo of §219) opens with noise
sweeps -- a note, then `P FF` -- that "kept looping" in ChipBoy: the ROM writes `NR43` every
111 ms (`13 07 40 15 23 17 50 ...`, one map entry a write) and ChipBoy the same entries every
22 ms, five times as fast, so it ran off the map's end and came round (`D7` → `08` with a
`NR44 = BF` retrigger, which is the ROM's own wrap too: `probe/noisep_probe.py … wrap`). Not the
tempo's doing: the same instrument at 120 BPM steps every 20 ticks on the ROM as well.

Measured with `probe/noisep_probe.py` on 9.2.L (a noise note at step 0, the `P` at step 1,
groove 6/6, the instrument's byte 8 varied), ticks between `NR43` writes:

| byte 8 (CMD/RATE) | `P FF` | `P FE` | `P 08` |
|---|---|---|---|
| 0 | 4 | 2 | 1 (two entries a write) |
| 1 | 8 | 4 | 2 |
| 3 | 16 | 8 | 4 |
| 4 | 20 | 10 | 5 |
| 7 | 32 | 16 | 8 |

So §66's `value / 4` entries a tick is the rate-0 case of **`value / 4` entries every `rate + 1`
ticks**, the same at 120, 255, 295 BPM and the 2x, 3x, 6x tempi (19.3 - 20.0 ticks for rate 4 in
every case; the odd short interval is the probe chain looping). The phase: the walk moves on the
ticks that are multiples of `rate + 1` counted from the note-on, and a `P` is taken up at the
first such tick at or after its row and moves from the next one -- with the `P` at tick 6, the
first entry lands at tick 10 (rate 0), 14 (rate 1), 24 (rate 3), 30 (rate 4), 40 (rate 7), which
is §150's one-tick wait at rate 0. On 8.5.1 the Register domain's byte-step obeys the same
rate (rate 3: one step every four ticks); 5.8.8 and 7.0.2 do not move `NR43` for a `P` at all in
this probe (not pursued).

ChipBoy: the importer already read byte 8 into `cmdRate` for every instrument type; the driver
paced only a pulse's and a wave's Tick-mode `P`/`V` by it. `Driver::tick()` now keeps a
per-voice count from the note-on (`noiseRateCount`) and steps both noise domains only on its
expiries, the fresh flag of §150 cleared on an expiry rather than on the next tick. Rate 0 is
every tick, exactly as before: a song whose noise instruments keep CMD/RATE at 0 -- the default
-- plays as it did.
