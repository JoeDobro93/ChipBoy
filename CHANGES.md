# CHANGES

Every departure from [`docs/CHIPBOY_SPEC.md`](docs/CHIPBOY_SPEC.md): what changed, why,
and what was considered. Deviating is expected. Deviating silently is not.

Also the place to record implementation status, so the spec stays a description of the
intended product rather than a progress report.

---

## Implementation status

| Milestone | Status |
|---|---|
| M0 — repository, spec, decisions | spec written; §18 decisions open |
| M1 — APU core + test-ROM harness | **done** 2026-09-07 — blargg `dmg_sound` 01–12 and the four DMG-observable SameSuite APU tests pass; CI on Linux, macOS, Windows |
| M2 — analog stage + renderer | **done** 2026-09-07 — first sound; fast path nulls against the reference at −122 dB; bit-identical across block sizes |
| M3 — main plugin shell | **done** 2026-09-07 — parameters per §12.3/§12.4, per-channel MIDI routing, JSON state, Linux VST3 and Standalone build |
| M4 — bank + driver | **done** 2026-09-07 — the bank with a factory set, the driver on its own tick, 11 driver tests |
| M5 — Voice plugin + link | **done** 2026-09-07 — region files, claims, one-block timing, push/pull; `chipboy_linktest` passes 16 checks |
| M6 — tracker, waves, frames, kits | **done** 2026-09-07 — tracker player on the host transport, record arm, kit import (resample + 4-bit dither), bank/song files |
| M7 — interface | **done** 2026-09-07 — the window from the mockup: header, mixer with period-locked scopes, seven tabs, status bar, visualizer window, Voice window |
| M8 — CGB / RAW / hardware options | **done** 2026-09-07 — CGB chip variant, RAW bypass, headphone noise, LCD line, bass mod, quiet-edge volume writes, de-click, soften master pops; 61 core tests |

---

## Spec revisions

### 2026-09-08 — revert cells and the Song tempo parameter

The three things the record test left open ([`docs/COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md)
§3, §4, §9.4). The demo no longer works around any of them.

**Changed:**

- **A cell can say "put this letter back".** A command's internal `c` field gains a
  meaning: 1 makes the command the **revert form** of its letter, which is exactly what a
  command slot going to none does — the instrument's own value for E, F, O, S, V and W,
  no offset and no bend for P, the master parameters for M, the song tempo for T, the
  phrase's groove for G, a stopped table for A. The driver takes the same function for
  both, so the two can never disagree, and a cell's revert form is applied once and
  **leaves the slot empty** — which is the whole point. The recorder writes that form
  instead of computing a value, and `SlotRevert`, `tracker::revertCommand` and the
  processor's `slotRevert` go away with it. In the phrase grid a revert reads as the
  letter and an equals sign, **`E =`**; `=` on a cell that already has a letter enters
  it, and typing a value clears it again ([`docs/UI_DESIGN.md`](docs/UI_DESIGN.md) §7).
  The cell's `c` round-trips through the song JSON, and a file written without it reads
  as a plain command.
- **The note report is per event.** `Driver::noteReport(ch)` described the block's *last*
  note-on on a channel, so two note-ons in one block — a fast tempo, a big buffer, a
  keyswitch between them — gave the first one the second one's instrument column. The
  driver stamps each note-on with what it did (`NoteEvent::plain` and `::loaded`) as it
  plays it, and the recorder reads the event; `noteReport` and `NoteReport` are gone. The
  event list `Driver::process` takes is no longer const.
- **The Song tempo parameter is the song's base tempo.** `buildTempoMap` seeded the map
  with `Song::tempoBpm` at tick 0 and the clock let that entry override its config, so the
  `song_tempo` parameter was dead the moment a song was published: `Demo/ChipBoy Demo
  (song tempo).rpp` played at 120 rather than 150. The map holds the T cells only now and
  takes its base from the caller, which is the parameter — automatable, and re-integrated
  from the current position while playing, as §4 says. `Song::tempoBpm` is the value the
  *file* carries: stamped from the parameter when a song is published, read back into the
  parameter when one arrives from a file, so a saved song opens at its tempo. A T cell at
  tick 0 is still a cell and owns the base from the song's start.
- **The demo reverts naturally.** PU2's **E** goes to none at bar 13 and the bass's
  velocity accents come back; PU1's **V** goes to none at bar 9 and the lead plays its own
  vibrato, ten-tick delay and all; WAV's **W** and **F** go to none at bar 13 and the
  instrument the keyswitch brings in plays its own wave from its own first frame. All
  three used to hold a concrete value instead, because a recorded revert stayed in force.
  `Demo/` is regenerated and `chipboy_recordtest` still passes.

**Why:** the record test's own report said a recorded revert was not a revert, and named
three demo lanes that had to lie to get past it. A letter's revert is a *kind* of value,
not a number, and the data model had a spare field for exactly this.

**Considered:** letting a revert stay in force and re-fire at every note-on, as a
concrete command does. It is what made the concrete form wrong: an E in force sets the
start volume of every note after it. A revert is a one-shot by definition — after it the
instrument's own value is what a note-on loads anyway.

**Checked, not registered as a test:** the demo played on the song's clock at a base of
150 against the same run at 120. The tempo in force is now the parameter's — 150 and 120,
where both used to be 120 — and the notes, which are sample-accurate against the host's
120, land in the same places: NOI, whose drums are note-driven, writes exactly the same
1022 registers in the same order. Everything the *tick* drives is a quarter faster and
so writes more: WAV 4198 registers against 3686, PU1 2471 against 2442. That is what
§9.5 and this file already say the song-tempo variant does, and why the record test runs
the host-tempo project.

**Skipped / uncertain:** a note held by *Quantise notes to ticks* across a block boundary
has no event left to report to, so its cell is written from the defaults; the driver
reports back through the pending queue for the notes that fire in the block they arrived
in. Rebuilding a song's tempo map when the parameter moves is a whole-song copy on the
message timer, taken only when the song has T cells at all — a song without them never
pays it, because its base is the clock's, live.

### 2026-09-08 — pitch speed and bare notes (engine)

The driver side of the addendum to [`docs/COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md)
(§7, §8): the speed of every pitch effect moves into the instrument, and a note without
an instrument stops retriggering.

**Changed:**

- **The instrument carries the pitch** — Pitch speed (Fast, Tick, Step, Drum), the
  vibrato's shape *and* direction (replacing the Triangle/Square/SawUp/SawDown enum),
  Command rate 0–15, Table mode (Tick, Step) and Overlap (legato, retrig) in place of
  the Legato flag. Vibrato depth is LSDj's semitone table rather than raw period units,
  so a lead's vibrato is the same interval at every pitch. A bank written before this
  still reads: the old `legato` flag becomes Overlap, the old four-value shape becomes a
  shape and a direction.
- **A pitch pipeline** of `periodOf(noteFine) + periodOffset`, driven by a per-voice
  360 Hz clock (11651 CPU cycles) restarted at every plain note-on and interleaved with
  the events and the ticks in cycle order, so a note sounds the same whatever sample it
  started on. V is a phase and a semitone depth (one cycle every 720/x updates, or 96/x
  ticks in Tick); L is a residual that walks to zero over the duration it is given; P is
  a bend speed rather than an absolute offset — `P 128` stops the bend and keeps what it
  reached, and a plain note-on is what puts the offset back to zero.
- **Plain and bare notes.** A tracker cell is plain when its instrument column is
  filled; a MIDI note is bare when it lands over a held note, would load the instrument
  already sounding, and that instrument overlaps legato. A bare note writes the period
  and nothing else — no trigger, no reload, no table restart — and fires only C, D, K, L,
  R and Z. `Driver::noteReport()` tells the recorder which it was and which slot was
  involved.
- **Four ways a channel could ring for ever are closed**: All notes off kills any voice
  whose DAC is on whatever `active` says and is never filtered by the source gate, a
  kill or a stop clears the held stack, the notes-on-tick queue runs its oldest event
  first when it is full rather than letting a note-off overtake its note-on, and moving
  the Instrument parameter clears a keyswitch.
- **Note-off Release finishes the sound**: a held or rising envelope gets a decrease at
  rate 1 written, without a trigger; WAV and KIT step the level 100 → 50 → 25 → mute one
  tick apart.
- **C, R and M follow §7**: C and R step every `cmdRate + 1` ticks, R's x is 0 none,
  1–7 up, 9–15 down by x − 8, and M's sides take 8 as "keep" and 9–15 as a relative
  move. Z adds a random 0…x to the re-run command's x and 0…y to its y instead of
  replacing the argument. E was already the NRx2 encoding the addendum describes; only
  the wording changed.
- **The factory bank** shows the new fields: the lead's vibrato is 5 Hz and three
  eighths of a semitone, the Pluck runs its pitch on the tick, table 5 "Slide up" starts
  an octave below and slides back over 60 updates, and instrument 17 "Pulse kick" with
  table 7 "Drum drop" is a Drum-speed P bend.

**Not done here**, and left to the tracker stage: `Driver::setTableGroove()` exists and
is documented but nothing calls it yet — a `G` inside a table records the slot it wants
in `Driver::tableGrooveSlot()` for the Player to answer with that groove's ticks. The
Instrument tab keeps the fields it had, renamed, plus the vibrato's direction; Pitch
speed, Command rate and Table mode reach the window in the interface stage.

### 2026-09-08 — groove editor and VEL column (interface)

The interface half of the addendum's §9.2 and §10 (`docs/COMMANDS_AND_TEMPO.md`), in the
Phrases tab. Nothing in the engine moved; this is what the tab shows and how it is
edited (`docs/UI_DESIGN.md` §7).

**Changed:**

- **A groove editor beside the lane**, sixteen cells row for row with the lane's steps
  (the preferred placement of §10; the grid gave up the width). Each row is the count
  that step lasts, a bar drawn against the longest entry, and the tick the step starts
  on — in the warn colour when that start is at or past the end of the bar and the step
  therefore never fires. The head carries the slot (0 straight and read-only, 1–16 the
  song's), the total against the bar's ticks (green when they match, warn either way
  round), the swing of the first pair, and a ◀ ▶ nudge that trades one tick inside every
  pair keeping its total. The editor follows the groove in force for the selected
  channel — a `G` in a slot or a cell, else the phrase's chip — until the stepper browses
  somewhere else. Edits go through `mutateSong`, the path the cells already take, so the
  Player picks them up with the next published song.
- **The lane has a VEL column**, 1–127 with blank meaning the default 100, editing like
  the instrument column. The cells always carried the velocity (the engine stage); it
  was not visible or editable.
- **The playing row is the row that is playing.** It was `inBar × steps / barTicks`, a
  sixteenth of the bar, which is the wrong row on every swung groove; it is now read
  from the channel's own step grid (`stepStartTicks` with the groove in force), and a
  step whose start falls past the bar's end never lights.
- The lane's columns were re-measured for the width the editor took: of 1156 px, 164 to
  the editor and 12 to the gap leave 980 — a 34 px step column and four groups of 236
  (note 39, vel 31, ins 31, tbl 29, two commands of 53). Command arguments of seven
  characters (`C 60,60`) clip a little sooner than they did; they clipped before too, at
  nine (`R 255,255`).
- *Steps / bar* already offered 8 and 16 only, and the groove preset combo was already
  gone, both from the engine stage. Nothing was left to remove.

**Why:** a sixteen-tick groove cannot be typed anywhere else — the lane's chip picks a
slot, it does not write one — and the total against the bar is the one number that says
whether a groove will drift, so it belongs beside the steps it lengthens.

**Considered:** the second row of tools, §10's fallback, which has ~560 px free. Sixteen
cells in a row there fit, but they lose what the column gives for nothing: step 3 of the
groove sits on step 3 of the lane, and the playing row crosses both.

**Note on the screenshots:** `chipboy_uishot` builds no song, so
`docs/screenshots/main-phrases.png` is the empty tab as the tool renders it.
`main-phrases-groove.png` is the same tab with a bar of cells and a groove typed into the
song by hand, since an empty tracker shows neither the velocities nor a groove worth
looking at.

### 2026-09-08 — the tracker audit (engine)

The engine half of the addendum's §9 (`docs/COMMANDS_AND_TEMPO.md`): the song model, the
Player, the recorder and the processor's flushes. The groove editor and the VEL column
in the grid are the interface stage; the phrase's groove is still chosen in the lane's
chip, and the groove preset combo — which quietly overwrote song grooves 1–3 — is gone.

**Changed:**

- **A groove is sixteen tick counts**, not a pair. `Groove { uint8 ticks[16] }`, each
  1–48 with 0 unused; the length is the leading non-zero entries and step *i* lasts
  `ticks[i mod length]`. The counts are absolute, not scaled to the bar: a groove that
  adds up to less than the bar leaves its last note sustaining, and a step that would
  start at or past the bar's ticks does not fire. At eight steps per bar every entry is
  doubled, as before. A new song holds 1 = 6 6, 2 = 7 5, 3 = 8 4, 4 = 5 7, 5 = 9 3,
  6 = 4 4 4 and the rest straight; slot 0 is straight and not editable.
- **Cells carry a velocity** (`vel`, 1–127, 0 meaning the default 100) and fire with it;
  the driver saw a hard-coded 100 before. Steps per bar is 8 or 16 — 32 was offered,
  never worked, and is read back as 16. The phrase's groove slot 16 no longer falls off
  on load (it was clamped to 0–15), and the song file carries the sixteen ticks; the old
  `[a, b]` form still reads, as the first two entries.
- **The groove in force is worked out at every tick**: the G in a command slot, else the
  last G cell that played on that channel, else the phrase's own. The stateful override
  the Player kept could disagree with a cell that had just played; there is no latched
  copy now. A G that arrives mid-bar re-lays the steps after it, and a step already
  played in that bar is not played twice.
- **The Player silences a channel with All notes off** — never a Tracker note-off, which
  the driver's source gate drops the moment the lane changes — when the transport stops,
  when the tick stream jumps (a locate or a loop wrap: any tick that is not the last one
  plus one), and when the channel leaves Trk or is muted. The processor sends the same
  flush when a channel's Source parameter changes, when the song's Trk/Roll choice flips,
  when a Voice lets a channel go, when record disarms, and when link mode turns off —
  where the block of MIDI held for the Voices is now drained into the block rather than
  dropped.
- **The tracker position the window shows follows the transport** and stands still while
  it is stopped. The clock still free-runs so tables and vibrato stay alive with the
  transport stopped; that tick is simply not the tracker's position.
- **T cells and the Player agree on the bar.** The tempo map is built with the song's bar
  ticks and the phrase's own groove, and in Song mode the Player now counts in the same
  bar ticks, so a T lands where the map says.
- **The recorder follows §9.4.** The instrument column is filled with what the note
  loaded when the note was plain and left blank when it was bare, so an overlap records
  as a bare cell and plays back bare (it used to be omitted whenever it repeated, which
  now means something else). Velocity is recorded. Keyswitch notes are never written as
  cells. A note-off goes to its own step, to the next one when that step is the note's
  own, and nowhere when the step already holds a note-on that ends it anyway. The
  command slots are read at each step's own tick on the channel's own grid — not once a
  block at the block start with values a block stale — and a slot going to none is
  written as the letter it reverts to: the instrument's own E F O S V W, `P 128`, `A 0`,
  `G 0`, `M` with the master parameters, `T` with the song tempo, nothing for the
  per-note letters. Quantising uses the recording channel's own phrase and groove, not
  channel 0's.

**Why:** the audit found the code and the model disagreeing where the ear would notice:
a groove that could only swing in pairs, a G slot latched into a copy the cells could not
see, notes left ringing whenever a lane changed hands, and a recorder that sampled the
command slots once a block from values a block old.

**Considered:** keeping a groove a share of the bar, so that any groove always fills it.
LSDj's grooves are tick counts and the bar is whatever they add up to; scaling them would
make one groove mean different lengths in 3/4 and 4/4, and the total-against-the-bar the
editor is to show would have nothing to say. Also considered letting a mid-bar G re-lay
the bar with no guard: the grid is counted from the bar's start so that a locate is
exact, and without the guard a step whose new start falls after the tick it already
played on would play twice.

**Not done here, on purpose:** the groove editor, the VEL column in the phrase grid and
the instrument's pitch fields (the interface and driver stages). A G inside a table
still runs at one tick per row; the processor marks where the driver's setter
for it goes.

### 2026-09-08 — the demo records and plays back (recordtest)

The addendum's §9.5 (`docs/COMMANDS_AND_TEMPO.md`): everything the demo does is now
something a tracker cell can hold, and `chipboy_recordtest` proves it by recording the
demo and playing the recording back, comparing the APU register writes.

**Changed:**

- **The demo is tracker-shaped.** No mod wheel and no pitch wheel anywhere: the vibrato
  ride of bars 5–8 is the V slot's depth moving a notch a beat on step boundaries, bars
  13–14 give PU1 an **L 30** slide (the lead's pitch speed is Fast, so 30/360 s ≈ 83 ms of
  portamento into every note) and bars 15–16 a **P 126** Fast bend that leans every note
  down. The master dip in bar 8 is an **M** slot on NOI (7 → 5 → 3 → 7) instead of the
  master parameters, and PU2 takes factory slot 17 "Pulse kick" — Drum pitch speed, its
  table doing the drop — for a kick pattern in bars 15–16. Every note starts on a step and
  ends one step before the next note on its channel, and every automation point sits on a
  step where its own channel starts no note; the generator asserts both.
- **`Demo/chipboy_demo_automation.json`**, written from the same Python tables as the
  Reaper envelopes and the MIDI file: `static` for the parameters the demo sets once,
  `lanes` for the ones it automates as `(beat, value)` points in plugin units, `hardware`
  for De-click and the model, which are the analog stage rather than anything a cell can
  hold. One source, three files.
- **`tools/recordtest` (`chipboy_recordtest`)**, a console tool beside `chipboy_linktest`,
  registered with CTest and run in CI. Pass 1 plays the MIDI file and the automation into
  the processor at 120 BPM, 48 kHz, 512-sample blocks with all four channels on Trk and
  record armed, and saves the recorded song through the JSON writer next to a listing of
  its cells. Pass 2 loads that song into a fresh processor, holds every lane at its bar-1
  value, sends no MIDI and plays the same seventeen bars. The two runs are compared per
  channel — the same writes, in the same order, with the same values, within 64 samples —
  with NR50 and NR51 as a fifth stream, because M and O are letters. `Driver` gained an
  optional write log for it: a vector pointer, null in normal builds, filled where the
  block's writes are handed back.
- **An OFF cell carries its columns.** A cell that ends a note dropped its instrument,
  table and command columns on the floor; they are applied now, after the note-off, as
  §3 says cells and slots are one code path. The recorder writes exactly such cells — a
  slot change at a step that also holds an OFF — so a recorded song could not play back
  without this.
- **A cell's instrument column is exact.** With Velocity = *instrument bank* the driver
  added `velocity / 8` to whatever slot was selected, cell columns included, so a recorded
  drum (the slot the note really loaded, §9.4) was banked a second time on playback and
  came out as a different drum. The bank now applies to the channel's own choice only.
- **A pitch update inside a tick's burst follows it.** A tick's writes go out an
  instruction pair apart; a 360 Hz update landing inside that burst was sorted in front of
  writes that had been computed before it, so a period computed at the tick could be
  written over the fresher one the update produced. The update now follows the burst, as
  it would on hardware, where a timer cannot interleave with an interrupt's register
  writes.
- **The recorder reads the note report after the note has played.** The instrument column
  and the plain/bare flag came from the driver's report while the driver had not yet
  played the block, so they described the *previous* note: a keyswitch or an Instrument
  lane move was recorded a note late. The note events are recorded after `Driver::process`
  now; the report is the block's last note-on on that channel, which is the cell that
  survives, a block being far shorter than a step.

**Why:** a demo that cannot be recorded cannot show that the recorder works, and §9.5
asks for the proof rather than the claim. Running it found four real defects — three in
the driver, one in the processor — that no unit test had reached, because each needs a
whole performance to show up.

**Considered:** keeping the mod and pitch wheels as a second, unrecordable layer. They
would have made the test meaningless, and the point of the channel model is that what you
hear is what a cell holds.

**A limit the test found, and the demo works around.** A slot going back to `none` is
recorded as its letter carrying the instrument's own value (§9.4), and that letter then
stays in force at every note-on — which is not what `none` does. Three cases in the demo:
**E** reverting would keep setting the start volume, so the velocity accents could never
come back; **V** reverting would restore the lead's speed and depth but not its ten-tick
vibrato delay, which a V command has no argument for; and a **W** revert on WAV would
override the instrument the keyswitch brings in a step later. The demo therefore reverts
to `none` only where it is faithful — PU1's W, whose instrument never changes under it —
and elsewhere moves the lane to the instrument's own value, which sounds the same and
records exactly. Making a cell able to say "clear this letter" would need a new value in
the data model, which §9.1 does not have.

### 2026-09-08 — the window fits a screen and stretches, tempo moves to the header (UI_DESIGN §2, §6, §7)

The main window was 1180 x 760 and fixed, which the Instrument tab never fitted: it
always opened scrolled, because one instrument's four cards were stacked one under
the other down a full-width column.

**Changed:**

- **The window is 1180 x 1020 at 100 %, and 1020 is now its minimum** — small enough
  for a 1080p screen with the host's own chrome around it. It is header 54 + mixer row
  370 + tab bar 34 + a 536 editor pane (512 of content and its 2 x 12 padding) + status
  line 26, and 512 is what the two fullest tabs ask for: the Instrument tab's cards
  need 510, the Phrases lane's sixteen steps 512 under its head. Neither shows a
  scrollbar at the default size. The Hardware tab (654) still scrolls until the window
  is stretched, as the bank lists always will.
- **The Instrument tab is two cards to a row**: Sound beside Envelope, Modulation
  beside Table & note behaviour. The left column is wide enough for three field
  columns and the right for two, and every field stays — 510 px where the single
  column wanted 802. The envelope preview is 90 px of drawing, which is all a
  one-second envelope needs, so the Result sits beside the Rate knob instead of taking
  a row of its own.
- **The mixer strip is 350 rather than 358**: the scope is 60 px instead of 66, and the
  two command slots and the running-state line read as one block on a 4 px gap.
- **The height stretches, the width does not.** The constrainer pins the width to the
  scaled 1180 and the minimum height to the scaled 1020, so the corner resizer only
  moves vertically. Header, mixer row and tab bar keep their heights at the top and
  the status line stays at the bottom: every extra pixel is the editor pane's. The
  chosen height is remembered in `apvts.state` as `ui_height`, unscaled, the way
  `ui_scale` already was, and the 100 / 125 / 150 % menu multiplies both.
- **Tempo moved from the Phrases tab to the header bar** (COMMANDS_AND_TEMPO §4):
  the source (Host / Song), the song's BPM — greyed in Host mode — and the *Quantize*
  toggle, so what the ticks follow is in force wherever you are working. The Phrases
  tab keeps *Start* and *Beats*, which are song data. Room came from stacking the
  wordmark's second line under it and letting the bank name field take what is left
  (87 px at 1180); nothing overflows.
- **"Quantize", not "quantise", in every user-visible string** — the parameter's own
  name included. Code identifiers and the prose in these documents keep the British
  spelling.

**Not changed:** the width, the scale steps, every control and field the panels had,
and the status line's "tempo host 120 / song 150".

### 2026-09-08 — the Instrument tab's pitch fields (interface)

The window half of [`docs/COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md) §7 and §10:
Pitch speed, Command rate and Table mode reach the instrument cards, the vibrato reads as
one group with its depth in semitones, and the cards re-flow so the tallest instrument
still fits the pane the window already has. `Theme.h`'s sizes do not change.

**Changed:**

- **Three fields join the cards.** *Pitch speed* (Fast / Tick / Step / Drum) on pulse,
  wave and kit — not on noise, where P and L do not apply — and with Drum greyed on kits
  because the driver plays a kit's Drum as Fast; *Command rate* 0–15 and *Table mode*
  (Tick / Step) on every type. Overlap and the vibrato's direction, which the engine
  stage put there to keep the tab compiling, get the captions and hints they were owed.
- **The vibrato is one group again.** Shape and direction share a single cell — two
  segmented controls side by side, the way the mockup pairs values — with speed, depth
  and delay following it, and the card is *Pitch & modulation* rather than *Modulation*.
- **A field says what its value is worth, and keeps saying it as the value moves.** The
  vibrato speed reads "4 Hz" in Fast, Step and Drum and "8 cycles/4 beats" in Tick; the
  depth reads LSDj's semitone table, "3/4 st", and "off" at 0, which is what the driver
  makes of it; the pitch speed reads "360 Hz", "per tick", "P jumps" or "semitones"; the
  command rate "every 3 ticks"; the table mode "row per tick" or "row per note"; overlap
  "only the pitch" or "starts it again". The sentence behind each short hint is the
  control's tooltip, per option where the options differ.
- **The cards re-flow instead of the window growing.** The vibrato's delay is a stepper
  rather than a knob (40 px where a knob asks 86, and the same control the Length field
  uses for a range this wide), and pan moves to the Sound card, where it sits with the
  other registers — it is NR51 — and where there was a row to spare. The tab now asks
  474 px of the 512 the pane has for a pulse instrument, 470 for wave and kit and 468
  for noise, against 510 / 468 / 468 / 510 before; nothing scrolls at the minimum
  height. The kit's Loop field takes two field columns so "Loop from point" is not cut.
- **The strip's running-state line says what the vibrato depth is worth**: "vib 8/¾"
  rather than "vib 8/4", the depth now being a semitone table rather than an amount.
  The line has barely 200 px for everything the driver is doing, so it uses the
  one-glyph fractions — the same width the raw index took — and its tooltip names the
  unit; the command slot above it keeps the roomier "3/4 st".

**Not changed:** the window's minimum height and the editor pane, the cards' order and
their two-to-a-row layout, the Assign / double-click behaviour, and type switching, which
still keeps every field so switching back finds them as they were.

### 2026-09-07 — the demo and the documents follow the new model (§8.1, §8.2, §12.3, §12.4)

Stage 3 of the revision above: nothing in the code changed, the demo project and every
document that described the old parameter set did.

**Changed:**

- **`tools/demo/make_demo.py` regenerated for the new parameter set.** The embedded
  table is the built one — 16 globals and 15 lanes per channel, 76 parameters — and
  `--paramdump` reports it identical to what `chipboy_paramdump` prints. `Demo/PARAMETERS.md`
  now also carries the command letters and what `x` and `y` mean for each.
- **The tune is the same sixteen bars at 120 BPM** (`Demo/chipboy_demo.mid` is unchanged,
  byte for byte); what it automates is the channel model. PU1's duty is CMD1 = `W`, one
  duty per bar over bars 9–12; PU1's vibrato in bars 5–8 is CMD2 = `V` with speed and
  depth in `x` and `y`, the mod wheel still riding the depth between notes; PU2's
  envelope is CMD1 = `E` alternating pluck and long by the bar; WAV's wave slot and frame
  are `W` and `F` over the keyswitched instruments; NOI keeps its keyswitches and
  velocity-selected drums. Every letter goes back to *none* by bar 13, so the revert to
  the instrument's own value is audible. The model switch DMG → CGB → RAW and the
  de-click toggle over the last bars are where they were.
- **A second Reaper project, `Demo/ChipBoy Demo (song tempo).rpp`:** the same track with
  *Tempo source* = **Song** and *Song tempo* = **150**, so the tracker's ticks run at
  60 Hz against the host's 48, and a `T` in PU1's second slot dropping the song to 100 for
  bars 9–12. One slot carries two letters in turn — `V` for bars 5–8, `T` for bars 9–12 —
  which is the clearest demonstration that a slot is a lane, not a fixed control. The
  GUIDs of the two projects differ; the derivation (uuid5 of a fixed namespace) is
  unchanged, and both files are still byte-identical between runs.
- **Documents.** `Demo/README.md` has a new bar-by-bar table, a section on what to listen
  for in the song-tempo project, and FL Studio notes that say which lanes to draw;
  `README.md`'s "Playing it" describes the tracker row, the two slots and the tempo model;
  `docs/UI_DESIGN.md` §4 lists the lane set and §7 says the clock lives in the Phrases
  tab; spec §12.3 and §12.4 carry the built parameter sets, and §8.1/§8.2 say the tick is
  24 to the beat from the tempo source.

**Why:** the demo is the only executable description of the parameter set, and it was
describing lanes that no longer exist. The second project exists because the tempo model
is the half of the revision that a screenshot cannot show: two projects side by side, one
on each clock, make the difference audible in a minute.

**Considered:** regenerating the tune as well (the notes were not the problem, and a
byte-identical `.mid` keeps the diff readable); one project with the tempo source
automated mid-song (it would demonstrate the re-anchoring caveat instead of the model).

**Not done:** an `.flp` for FL Studio — the format is binary and undocumented, so the
README explains the lanes to draw by hand instead.

### 2026-09-07 — commands, channel lanes and tempo (§8.1, §8.2, §9.6, §12.3, §12.4)

The design is [`docs/COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md), agreed on the
same day; this entry records what the first stage (the core and the plugin layer)
changed. The window's strips and panels follow in stage 2, the demo in stage 3.

**Changed:**

- **Commands are `A C D E F G H K L M O P R S T V W Z`** with two arguments `x` and `y`,
  0–255 each. `A` is the table select (it was the envelope), `E` is the envelope, `G`
  the groove, `T` the tempo, and `W` is duty on the pulses and the wave slot on WAV.
  `S` is PU1's sweep only — the noise channel's clock-shift step is the instrument's
  `noiseSweep` field, and the factory bank's kick uses it.
- **A channel is a visible tracker row.** `driver::ChannelParams` is now
  `{instrument, table, level, pan, transpose, cmd[2], liveFollow, velocityMode,
  keyswitch}`. The silent override lanes — duty, envelope volume/direction/rate, sweep,
  wave, frame, vibrato, arpeggio, detune, LFSR — are gone as parameters; they live in
  the instrument or arrive through a command. A slot fires at the next tick when its
  value changes, again at every note-on after the instrument and its table, and reverts
  when it goes to none. Tracker cells write the same two slots, so the lanes and the
  tracker are one code path, and the recorder writes what is in force.
- **`driver::Clock`** owns tick generation and the tracker's position for both tempo
  sources. Ticks are always 24 per beat: `tick_source`, `ticks_per_beat` and `tick_hz`
  are gone, and so are the V-blank and custom tick rates. Host mode puts a tick at every
  multiple of 1/24 beat of the host's ppq; Song mode integrates the song's tempo map
  (a base tempo plus the `T` cells) from a song start held in seconds, so a locate lands
  on the step playing through would have reached. With the transport stopped both
  free-run at the current tempo, as the driver did before.
- **The Player counts in ticks**, not ppq: a bar is `beatsPerBar × 24` ticks, a straight
  step is its share, grooves scale alternate steps. `Song` gained `tempoBpm`,
  `songStartSeconds`, `beatsPerBar` and a tempo map built from its `T` cells when it is
  published. A `G` slot or a `G` cell sets a channel's groove.
- **New parameters:** per channel `cmd1_type/x/y` and `cmd2_type/x/y`; globally
  `tempo_source`, `song_tempo` and `notes_on_tick` (MIDI notes wait for the next tick;
  bends and controllers never do, tracker cells always sit on ticks).
- **The driver publishes the running state** — duty, envelope, vibrato, pitch offset,
  pan, table slot and step, groove — next to the registers. The link region carries it
  in a second 64-bit word, so `link::kVersion` is 2.

**Why:** two control systems overlapped. An instrument held the sound and a second set
of per-channel host lanes silently overrode its fields, with nothing in the window
saying which was winning; and the tick rate was a global choice that the tracker, the
tables and the host transport each read differently. One row per channel and one clock
for both tempo sources removes both ambiguities, and it is the model the audience
already reads.

**Considered:** keeping the override lanes alongside the commands (the ambiguity stays,
and the strip has to explain it); making Song tempo a host-tempo-map import (a host
cannot always be automated from a plugin, and the song would stop being self-contained).

**Not migrated:** old projects, banks and parameter sets. Agreed with the owner: the
parameter set changed shape, and a half-translated project is worse than a fresh one.

### 2026-09-07 — notes are sample-accurate, the tick drives the rest (§8.2)

**Changed:**

- **Note-ons, note-offs, bends and controllers are applied at their own sample offset**
  inside the block, not sampled at the next driver tick. The tick keeps everything a
  real driver runs from its interrupt: tables, vibrato, wave frames and the phrase
  commands.

**Why:** the spec's tick is 60 Hz (§8.2) but the notes reaching it come from a host,
not from a phrase grid. Quantising every note-on to the tick made a played or
DAW-sequenced note up to one tick (16.7 ms) late, and shortened or lengthened it by as
much again — audible as flam on anything tight, and it moved with the block size.
Nothing on the hardware needs a note to start on the tick: the tick exists so tables
and vibrato step at a fixed rate, and they still do.

**Considered:** keeping the tick quantisation and shortening the tick (changes every
table and vibrato rate); interpolating the note's start inside the tick (the same
latency, hidden).

### 2026-09-07 — M3–M8: the plugin as built (§8, §9, §11, §12, §13, §14)

**Changed:**

- **The link is a memory-mapped file per instance**, `<temp>/chipboy-link/<uuid>.cbl`,
  not a named shared-memory object, and there is no directory region: a Voice lists the
  folder (§11.3). Voice events carry the host's sample position; the main applies an
  event from host block *N* in its block *N+1* and holds events that arrive early
  (Reaper's anticipative rendering can be many blocks ahead). When the host reports no
  sample position, or the transport is stopped so time is frozen, events apply at once
  with one block of jitter. A duplicated instance (copied state) gets a fresh UUID so two
  mains never publish one region; a duplicated Voice likewise. Push-to-slot and
  pull-from-slot are explicit requests answered on the main's message thread (§12.5).
- **Volume writes at edges** (§12.3) is a marker in the driver's write list: the plugin
  moves the marked burst to the next cycle where the pulse output is low
  (`Apu::cyclesUntilPulseLow`), re-sorting the moved writes.
- **Mute and solo** on the mixer strips are NR51 gates on the driver's next tick
  (UI_DESIGN §2); they pop like the hardware.
- **Level changes are NRx2 + retrigger**, not a bare NRx2 write, which would enter
  zombie mode (reference §4); the duty phase survives the retrigger.
- **RAW** keeps a 1 Hz DC blocker so a stuck DAC does not leave an offset; nothing a
  note can hear.
- **The Voice's parameters use one union set** (`ChannelKind::Any`); when a Voice drives
  the wave channel its 0–15 level maps onto the four hardware steps.
- **Recording** writes the instrument, table override and V / P / O / A commands derived
  from the parameters in force; while the arm is on, incoming MIDI plays on tracker
  channels and their lane is muted.
- **The scopes' analog trace is an approximation**: a one-pole high-pass at the model's
  corner over the channel's own digital staircase, not a per-channel render through the
  shared stage (C9 forbids splitting it).

**Why:** each is the simplest mechanism that keeps the spec's promise; the file-backed
region in particular works across every host process of the same user without
platform-specific names.

**Considered:** named POSIX/Win32 shared memory (sandboxed hosts need per-platform
names either way; deferred), a per-channel analog render for the scopes (audio cost,
and C9), NR51 gating from the plugin rather than the driver (would bypass the tick).

### 2026-09-07 — the link's known limit (§11.2)

A host whose sandbox gives it a private temp folder cannot see other processes'
regions. Everything inside one host process works; a VST3 Voice in one host driving an
AU main in another only works when both see the same temp folder. Platform
shared-memory names are the fix, deferred.

## Deferred to after v1

| Item | When |
|---|---|
| Playback ROM export (`.gb`) from the tracker (§15.3) | after v1; the song model is self-contained for it |
| LSDj `.sav` import / export (§15) | after v1; the data model maps one to one |
| MGB / AGB models | when a unit is measured |
| CLAP format | when the licence decision (D1) is taken |
| Pro Sound tap | after v1; needs a measured pro-sound unit |
| SameSuite CGB timing gates | when the CGB core is validated beyond wave RAM |
| pluginval in CI | next CI pass |
| MIDI CC learn beyond CC1 / CC7 / CC120 / CC123 | v1.1 |
| Platform shared-memory names for sandboxed hosts | v1.1 |
| A local-instrument editor inside the Voice window | v1.1 (v1: pull a slot, edit it in the ChipBoy window, push it back) |
| Windows and macOS DAW testing of the plugin builds | first user session |

### 2026-09-07 — UI workshop: decisions taken (C8, §6.5, §9.6, §12.3, §13, §15.3, §17, §18)

**Changed:** `docs/UI_DESIGN.md` and the interactive mockup `docs/mockups/chipboy_mockup.html`
are now the interface specification; §13 points at them and everything in them ships in
v1. The six workshop decisions (D10–D15 in §18):

- The Phrases lane is a **full tracker** — note column, record arm, per-channel choice of
  piano-roll or tracker notes — so a song can later be exported as a playback ROM. §15.3
  records the two rules that keep that door open.
- **RAW** joins the model switch: the DMG chip with the analog stage bypassed.
- **C8 is revised.** The one-switch rule becomes "defaults are a stock machine; every
  audible departure is opt-in, labelled, and lights MODIFIED". The departures (de-click,
  softened master pops) ship in v1; de-click is also on the master strip.
- `M` (master volume) and `Z` (random argument) join the command set.
- Default routing PU1 omni, PU2 / WAV / NOI on MIDI 2–4. Keyswitches off by default.
  Visualizer window in v1.

**Why:** the owner's answers in the workshop. **Considered:** keeping commands-only in
the lane to avoid two note sources; rejected because the ROM-export goal needs the
tracker to hold the whole song, and recording makes the two sources one.

### 2026-09-07 — M2: analog stage and renderer (§5.2, §6, §7, §16.2–16.4)

**Built:** `Source/core/Analog/AnalogModel.h` — the constants per console, each marked
measured or estimated. `Source/core/Render` — the band-limited step kernel (Kaiser
windowed sinc at twice the host rate with the amplifier's low-pass folded in), the fast
renderer, and the brute-force reference. `chipboy_demo` renders a built-in tune to WAV;
`chipboy_runrom --wav` renders any ROM's audio. Both take `--cgb`, which selects the
CGB's analog constants only — the core is still the DMG core until model selection (M8). Tests: block-size determinism (32, 64,
128, 2048, a varying size, and a block larger than the renderer's own buffer — all
bit-identical), the fast/reference null (−122 dB against a −90 dB requirement), the
DAC-hold behaviour, a golden render, and a golden hash of the APU's event stream.

**Spec changed — DAC-off (§6, §6.1; reference §9, §12.7).** The open question from the
capture is settled by the DC-step recordings themselves: a disabled DAC holds its last
level. Six repetitions of on/off at wave value 0 show one step, on the first enable
only; at value 15 every re-enable shows a 12-cycle two-rail blip and no step; no
repetition shows any movement at DAC-off. So the DAC-off click does not exist, the
DAC-on click steps from the held level, and the renderer models exactly that. This is
a real departure from the conventional emulator model, which returns to zero on
DAC-off and therefore clicks twice where the hardware clicks once.

**Spec changed — event stream (§5.2).** A second, sparse stream carries NR50/NR51 and
the power state, because routing and master volume are the analog stage's business.

**Read into the spec, not changed:**

- §7's area-summation fallback for dense event rates is not built. The step path renders
  the noise channel at its fastest clock (524 kHz) as part of a full four-channel tune
  at 30× realtime on one core, so there is no case for it yet. It stays in the spec as
  the remedy if one appears.
- §16.2 asks for hashed golden audio; the render golden compares with a tolerance
  (2e-5) because the kernel design goes through libm. The APU event-stream golden is an
  exact 64-bit hash.
- §6.3's soft clip is a symmetric placeholder that engages only above 3.5 aligned
  channels: the clip point is unmeasured (reference §12.6). §6.2's amplifier low-pass is
  an 80 kHz estimate above the measured ≥ 39 kHz bound, chosen so that a wrong guess
  cannot dull the audible band.
- §6.4's noise model splits the measured total RMS between a white floor and the three
  measured lines (9198 Hz, its second harmonic, 59.7 Hz) at their measured prominences
  — the CGB's line dominates its floor, so treating the floor as white would have
  overshot the total. Rendered and re-measured with the analyser's method: DMG +32.7,
  +26.6, +27.1 dB and CGB +49.6, +30.3, +41.0 dB, each within the difference in FFT
  resolution of the capture's +26/+20/+20 and +43/+24/+34.

**For M3:** the renderer's contract is `cycleForFrame()` — run the APU to that cycle,
apply register writes at their cycles on the way, then `render()`. Latency is 42 frames
at 48 kHz (a 4-frame kernel head and a 38-frame linear-phase decimator), reported to
the host. Output is in rail units, ±4 at most; the output trim (C3) applies after.

### 2026-09-07 — M1: APU core and test-ROM harness (§5, §16.1, §16.7)

**Built:** `Source/core/Apu` — the chip as §5 describes it: `uint64_t` cycle time,
`write`/`read`/`runTo`/`reset`, next-event scheduling, and a stream of
`{cycle, channel, level, dacOn}` events as the only output. Every §10.1 and §10.2 quirk.
`Source/tools/harness` — an M-cycle-accurate SM83, MBC1 memory map, timer, LCD line
counter and serial port, enough to run real test ROMs and nothing more. `Tests/` — unit
tests for the behaviours the plugin leans on (masks, timing, chunk-independence of
`runTo`), one Catch2 case per blargg ROM, one per SameSuite ROM. `chipboy_runrom` for
reading a failing ROM's own output.

**Result:** blargg `dmg_sound` 01–12 pass. SameSuite `div_write_trigger`,
`div_write_trigger_10`, `channel_3_wave_ram_dac_on_rw` and
`channel_3_wave_ram_locked_write` pass.

**Read into the spec, not changed:**

- §5.1 and §16.1 say "SameSuite's APU tests". Of its 78 APU tests, only those four are
  observable on a DMG; the other 74 read PCM12/PCM34, registers that exist only on a
  CGB (SameSuite's own README says as much: "Pre-CGB devices … other tests fail because
  they rely on the CGB-only PCM registers"). So the DMG core's gate is those four plus
  blargg, and the full list becomes the acceptance gate for the CGB model (§6.5) when
  it is built. Their expected values come from a CGB-E, and several are revision-specific.
- §16.1 says blargg 01–11. There are twelve; 12 (`wave write while on`) is DMG-only and
  passes, so it is in the gate.
- SameSuite ships as source, not ROMs. It is assembled at build time with RGBDS when
  that is on the `PATH`, otherwise its tests are skipped with a message. RGBDS is
  therefore an optional build-time tool (`LICENSING.md` §1).

**Established while building it**, all now in `HARDWARE_REFERENCE.md`:

- The DMG wave-RAM access window is one 2 MHz cycle — the fetch cycle or the CPU cycle
  after it — with the sample buffer refilled 6 CPU cycles later than the period after a
  trigger, and trigger corruption when the next fetch is due within 2 cycles. Found by
  replaying blargg's tests 09 and 12 in Python against their DMG checksums: the first
  guess (a 2-cycle window) reproduced the ROM's printed CRC exactly but not the DMG's,
  and the checksum admits no window wider than one APU cycle. §3, §5.
- Powering the APU on while DIV bit 12 is set skips the first frame-sequencer tick
  (SameSuite `div_write_trigger_10`). §3, §10.2.
- The harness CPU initially made `EI` take effect before the following instruction,
  which is wrong (`EI; DI` must not open a window). Fixed before it could matter.
- `ld b,b` is honoured as a software breakpoint, SameSuite's "test finished" signal.

**Considered:** rendering SameSuite's screen and comparing to a reference image, the
way SameBoy's tester does. Unnecessary: every test bakes its expected table into the
ROM, compares in software, and reports over the serial port, so the harness needs no
PPU. Also considered vendoring the four SameSuite ROMs to avoid the RGBDS dependency;
rejected because §16.1's rule is that test ROMs are fetched, and building from source
keeps them current.

### 2026-09-05 — DMG *and* CGB ship in v1 (§6.5, §16.6, §17; `HARDWARE_REFERENCE.md` §11)

**Changed:** CGB moved from post-v1 to v1, resolving `[DECIDE] D8`. Added spec §6.5 on
model selection. Rewrote §16.6 around the actual capture rig. Named the host targets in
§3.1 and §16.5.

**Why:** both a DMG and a CGB are available to measure, which was the only reason CGB was
deferred. It is also the largest audible difference between models, and it is not only a
filter coefficient — CGB wave RAM is live-writable, so the wave channel *behaves*
differently: frame changes and kit streaming click on DMG and need not on CGB.

**Considered:** shipping DMG only and adding CGB later. Rejected — the capture session
costs nothing extra while the rig is set up, and retrofitting a second model after the
wave channel is built around DMG's re-trigger requirement would be more expensive than
designing for both now.

### 2026-09-05 — hardware capture tooling built (§16.6)

**Added:** `tools/capture/` — probe ROM, SM83 verifier, loopback generator, analyser,
and a synthetic-capture generator for testing the analyser; `docs/CAPTURE_GUIDE.md`.

**Notable during development**, all caught before any recording session:

- `WaveRamp` clobbered `DE`, the take interpreter's bytecode pointer, so the ROM ran
  off into garbage after take 110 and never terminated. Found by executing the ROM
  rather than trusting that it assembled.
- The original marker tones (3000/1500/750 Hz) sat exactly on harmonics of the probe
  tones (1000.6/500.3 Hz squares), so payload audio decoded as markers. Marker tones
  moved into the gaps between harmonics, and the analyser now matches candidates
  against the known take order.
- Takes 130–133 originally used a sustained DC level on the wave channel, which is
  invisible after the coupling capacitor. Rewritten as an AC ladder.
- The analyser derived DAC step sign from edge direction, which inverted every wave
  value below 7.5 — where the DAC output is genuinely negative. It now takes the sign
  from the data, and measures only DAC-ON edges.

**Known limitation:** equivalent-time sampling improves time resolution, not bandwidth.
If the amplifier's corner is above the converter's ~90 kHz limit the analyser reports a
lower bound and says so. This is audibly irrelevant and cosmetically relevant to the
oscilloscope display only.

### 2026-09-06 — capture guide: exact Reaper routing, 192 kHz, 4th Gen notes

**Changed:** `docs/CAPTURE_GUIDE.md` Steps 1-5 rewritten with the real rig.

- **192 kHz confirmed despite the DSP mixer being unavailable above 96 kHz.** The mixer
  is an input-to-output path we never needed, and losing it means the interface cannot
  route an input to an output at all — removing the only hardware feedback path.
- **Feedback is now prevented by construction, not by care:** the playback track routes
  to hardware outputs only with Master send unticked, and the record track sends nowhere
  with monitoring off. No input reaches any output regardless of what is plugged in.
- **4th Gen specifics:** Clip Safe, Auto Gain and Air must be off (all three change the
  signal during or between takes). The 4th Gen *virtual* Loopback input must NOT be used
  for calibration — it never leaves the digital domain and so skips every stage the
  calibration exists to measure, yielding a correction of ~zero that looks legitimate.

**Considered:** dropping to 96 kHz to keep the mixer. Rejected — the mixer has no role
here, and 96 kHz would halve the usable bandwidth for the edge-shape measurement.

### 2026-09-06 — analyser rejects a loopback that never went through analog

**Added:** `analyse.py` warns when a `--loopback` file shows no plausible AC coupling
(time constant over 1 s), and marks the corrected coupling figures as meaningless.

**Why:** two different mistakes produce a loopback file that is a perfect digital copy —
passing the generated `calibration.wav` instead of a recording of it, and recording a
4th Gen *virtual* Loopback instead of cabling outputs to inputs. Both yield a correction
of about zero, so the console's coupling constant comes out uncorrected while appearing
corrected. That is worse than not calibrating, because it looks right.

**Detection:** an un-played `calibration.wav` measures a 38.6 s time constant (0.004 Hz);
a real analog path is 1-20 Hz. The threshold sits at 1 s, far from both.

**Also:** docs used `cal.wav` and `calibration.wav` for the same file in different places.
Unified to `calibration.wav`, and the smoke test now states explicitly that it misuses
`--loopback` on purpose and that the resulting warning is expected there and nowhere else.

### 2026-09-06 — step measurement rewritten after the first real capture

The first DMG and CGB captures came back with a broken wave-DAC zero crossing
(4.7 and 5.4 on the DMG, 669 and -2616 on the CGB) and a CGB coupling constant of
51 ms against an expected 0.23 ms. The recordings were fine; `analyse.py` was not.

**Three faults, all in the step measurement:**

1. **Fixed 1-25 ms fit window.** Suits a DMG (tau ~5 ms), useless on a CGB, whose
   step has decayed to ~1% before the window opens — so it fitted the INTERFACE's
   27.5 ms tail and reported it as the console's. Reproduced in simulation: 50.66 ms
   measured against 0.23 ms true, matching the observed 51.01 ms. The window now
   scales to the observed decay.
2. **Amplitude extrapolated from a single-exponential fit.** The console's coupling
   capacitor and the interface's are in SERIES, so the decay is two-pole and the
   extrapolation is biased. Amplitude is now read directly at the edge and corrected
   for the few samples of decay before the peak.
3. **Rates subtracted instead of the pole being removed.** Rates add only at t=0;
   subtracting them under-read the DMG by ~5%. `undo_hp` now deconvolves the
   interface's measured pole before fitting.

**Validated against a simulation carrying the real time constants, the real
interface pole and the measured noise floors:** DMG tau 5.674 ms against 5.680 true
(-0.1%), CGB 0.226 against 0.225 (+0.5%), zero crossings 7.498 and 7.523.

**Also:** duty is now reported polarity-corrected. The real DMG measured
0.84/0.73/0.50/0.27 against theory 0.125/0.25/0.5/0.75 — exactly 1 - theory,
because the DMG's DAC is inverting. That polarity is now detected from the wave
transfer's slope and recorded as `dac_polarity`.

**What the first capture already established**, all from FFT-based measurements
that were never affected: pitch within 2.4 cents on both consoles; all seven
envelope rates within 2%; a linear pulse DAC transfer with a zero intercept and
level 0 reading 4.6e-6 (DAC-off is true silence); and the 9198 Hz LCD line at
+26 dB prominence on the DMG, falling to +2 dB with the LCD off.

### 2026-09-06 — probe ROM: wave trigger delay invalidated the DC-step takes

**Found by looking at the raw capture**, after two wrong guesses from summary JSON.
Every DC-step take stepped to the SAME value regardless of wave level for the first
~1 ms, then the real level appeared. Cause: the takes set `NR33 = $00`, i.e. wave
frequency 0, so the first sample after a trigger arrives `(2048 - f) * 2 = 4096`
cycles = **976 us** late (`HARDWARE_REFERENCE.md` section 10.1, "wave trigger delay").
The DAC-on step therefore landed on the channel's STALE sample buffer — identical for
every take — and `DAC(L)` only appeared a millisecond afterwards.

The reasoning that produced the bug was "all 32 samples are equal, so the frequency
does not matter." The frequency does not change the OUTPUT, but it does change how
long the stale buffer persists after a trigger.

**Fixed:** those takes now drive f = 2047 (`NR33 = $FF`, trigger `NR34 = $87`),
putting the first sample 0.48 us after the trigger. Requires a re-record.

**Confirmed valid in the meantime:** the level-dependent step that appears after the
delay gives `DAC(15) - DAC(0) = -0.115` on the DMG, against `0.00722 x 15 = 0.108`
from the pulse-channel transfer measured in the same capture. Two independent
channels agreeing to 6% says the console and the analysis are both sound.

**Still open:** the DAC-off transitions produce no visible step at all, while every
DAC-on does. Possibly a high-impedance disabled-DAC state. Not needed for the
measurement — only ON edges are used — but it should be understood before section 6.1
of the spec claims what a disabled DAC does.

### 2026-09-06 — settled-level amplitude, and partial captures now merge

**Fixed:** step amplitude was read from the PEAK within a few samples of the edge.
Real hardware overshoots on DAC turn-on — measured at ~11% on a DMG (peak 0.0631
where the settled value is 0.0572). That alone put the wave-DAC zero crossing at
8.37; measuring the settled level 20-100 us after the edge puts it at **7.504**,
recovered from the FIRST capture set.

**The simulator now models both the turn-on overshoot and the wave trigger delay.**
Neither was exercised before, which is exactly why a simulation that passed
perfectly did not predict what real hardware did. A simulator that omits the
hardware's awkward behaviour only tests the analyser against itself.

**Added:** `analyse.py` accepts several captures and pools takes across them, since
each take carries its own id in its marker. Verified by splitting a known-good
capture into two overlapping halves: each alone decodes 46/83 and fails; merged
they give 83/83 and identical constants. A console that cuts out mid-run — a real
hazard on a DMG whose cells shift — now costs a second pass, not a session.

---

### 2026-09-06 — settled-level amplitude, and partial captures now merge

**Fixed:** step amplitude was read from the PEAK within a few samples of the edge.
Real hardware overshoots on DAC turn-on — measured at ~11% on a DMG (peak 0.0631
where the settled value is 0.0572). That alone put the wave-DAC zero crossing at
8.37; measuring the settled level 20-100 us after the edge puts it at **7.504**,
recovered from the FIRST capture set.

**The simulator now models both the turn-on overshoot and the wave trigger delay.**
Neither was exercised before, which is exactly why a simulation that passed
perfectly did not predict what real hardware did. A simulator that omits the
hardware's awkward behaviour only tests the analyser against itself.

**Added:** `analyse.py` accepts several captures and pools takes across them, since
each take carries its own id in its marker. Verified by splitting a known-good
capture into two overlapping halves: each alone decodes 46/83 and fails; merged
they give 83/83 and identical constants. A console that cuts out mid-run — a real
hazard on a DMG whose cells shift — now costs a second pass, not a session.

---

### 2026-09-07 — hardware measured: DMG and CGB constants replace the estimates

`HARDWARE_REFERENCE.md` §12 is now measured values from one DMG and one CGB, second
capture set, 83/83 takes on all four runs, no clipping. Analyser output tracked under
`measurements/2026-09-07/`.

**Headline:** DMG coupling **0.999963** per cycle (24.7 Hz) — within 12% of Blargg's
published 0.999958, measured independently on different hardware. CGB **0.999494**
(338 Hz), half the published corner, repeatable across both volume settings; treated as
this unit's number.

**Confirmed by measurement, not assertion:** the DAC is inverting; digital 0 and 15 sit
symmetrically about DAC-off (zero crossing 7.41–7.59 across four runs, max and mid
agreeing per console); the DAC is linear; NR50 = 0 is ~1/8, not mute; wave and pulse
channels agree to 3% on the DMG by two independent methods; the 9198 Hz LCD line sits
at +26 dB on the DMG and drops 24 dB with the display off.

**Two findings that change the model:**

1. The CGB's 9198 Hz line is 17 dB stronger than the DMG's and does **not** stop when
   the display is turned off. Model it as always-on for CGB.
2. DAC-**off** produces no step on either console. Spec §6.1's "disabled DAC is analog
   zero" is contradicted; a high-impedance disabled state fits. Flagged in the spec —
   the click model in M2 depends on which it is.

**Still estimated:** amplifier bandwidth (≥ 39 kHz, interface-limited) and the clip point
(ladder sources never peak coherently). Neither is audible; both are logged.

---

## Departures from the spec

Recorded above under the milestone entries (2026-09-07, M3–M8).

### Format

```
### YYYY-MM-DD — short title (spec §N)

**Changed:** what the code does instead.
**Why:** the reason.
**Considered:** the alternatives, and why they lost.
```
