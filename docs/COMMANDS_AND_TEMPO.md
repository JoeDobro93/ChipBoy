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
| K | kill | ticks after note-on | – | yes | yes | yes | no (per note) |
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
rise at `y` − 8. **E never triggers**: it walks the level to `x` by zombie-mode writes at
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

