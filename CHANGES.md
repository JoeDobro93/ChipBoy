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
| M4 — bank + driver | **done** 2026-09-07 — the bank with a factory set, the driver on its own tick, 11 driver tests. **2026-09-08**: the instrument carries the pitch (Pitch speed, vibrato shape/direction, Command rate, Table mode, Overlap); a note without an instrument only changes pitch (bare notes); four note-hang paths closed (all-notes-off, kill, event ordering, keyswitch-clear). Then: an instrument saves and loads on its own as a `.cbi` **preset** (`bank::collectPreset` / `bank::placePreset`), its tables, waves and kit carried along and renumbered into place. **2026-09-08 (third)**: **song file format 5** embeds the whole bank — instruments, tables, waves and kits with their samples — so a song file is complete on its own; a format-4 file still loads the song alone, against a copy of the active bank, with the name-difference report as before |
| M5 — Voice plugin + link | **done** 2026-09-07 — region files, claims, one-block timing, push/pull; `chipboy_linktest` passes 16 checks |
| M6 — tracker, waves, frames, kits | **done** 2026-09-07 — tracker player on the host transport, record arm, kit import (resample + 4-bit dither), bank/song files. **2026-09-08**: grooves are sixteen tick counts, cells carry velocity, the Player flushes a channel with All notes off on stop/locate/source change, and the recorder follows §9.4; `chipboy_recordtest` (record/replay parity) and the tracker-shaped demo are done, next to the link test. Then: **steps per bar** is a number, 1–64, with a per-bar override (`Song::barSteps`) for any bar, tracked through a prefix table (`Song::barStartSteps`) so a locate lands on the right step; a cell's two commands fire **once**, at their step, instead of occupying a slot; per-channel **record arms** gate an armed channel's recording whatever its playback source; **song files** (`.cbsong`) and the plugin's **own transport** (`transportPlay`/`transportStop`/`setLoop`) round out the Standalone; `Demo/ChipBoy Demo.cbsong` is the recorded demo, checked byte for byte by `demo_song_matches`. **2026-09-08 (third)**: the Tracker tab becomes a **tab strip**, one tab per open song, each owning its own bank — only the active tab plays, records, and is shown in every other tab; undo steps carry the tab they belong to. **Hybrid** joins MIDI and Trkr as a third playback source: notes come from MIDI, everything else (instrument, table, commands) from the song's cells. The host's time signature no longer reaches the tracker — bar ticks are always the song's own beats per bar (§11 amended), in both tempo modes. `tools/demo/make_songs.py` adds six original songs under `Demo/songs` (a groove study, a meter study, a route theme, a platformer tune, a modern track, a wave-manipulation track), each checked by the CTest `demo_songs_load`; `Demo/ChipBoy Demo (hybrid).rpp` carries the plugin's saved state, checked by `demo_state_matches`, reproducing the recorded demo under Hybrid |
| M7 — interface | **done** 2026-09-07 — the window from the mockup: header, mixer with period-locked scopes, seven tabs, status bar, visualizer window, Voice window. **2026-09-08**: the Phrases tab gained a groove editor and a VEL column, the Instrument tab gained the pitch fields, and the window was resized to fit a 1080p screen with tempo moved to the header; then the Phrases tab became the **Tracker** tab with the transport, the song files and the chain rotated beside the lane, a **Grooves** tab took the groove editor, and the Instrument tab gained preset files. Then a quality-of-life round: every number typeable, the wheel scrolling only, command cells split into letter and values with right-click slot lists, and undo / redo over every hand edit. **2026-09-08 (third)**: the song tab strip sits above the lane; the header's tempo becomes a **readout** — host or song, in force — with the song's own master tempo typed in the Tracker head, now two rows of grouped tools (TRANSPORT · RECORD · SONG · FILE); the master strip gains **LCD Whine** as a third, independent switch and one **VOL** stepper for both sides; PLAYS gains **Hybrid**; and the channel scopes lock to an edge chosen by the waveform's shape rather than the last edge before the window, holding still on a wave channel under vibrato |
| M8 — CGB / RAW / hardware options | **done** 2026-09-07 — CGB chip variant, RAW bypass, headphone noise, LCD line, bass mod, de-click, soften master pops; 61 core tests. **2026-09-09**: the quiet-edge volume writes are gone (§26) — no program on the console can wait for a pulse's low half; the parameter stays as a no-op so the parameter table does not move |

---

## Spec revisions

### 2026-09-09 — channels on their own time, zombie-mode levels, shaped envelopes (engine)

The fourth addendum ([`docs/COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md) §25–§28)
and the two rules of §31 and §32, engine side. Bars leave the model; every level change
becomes a write a program on the console could really make; the instrument gains an
envelope of its own shape; and the driver is audited against the hardware in
[`docs/HARDWARE_DRIVER_AUDIT.md`](docs/HARDWARE_DRIVER_AUDIT.md). The interface — the
Instrument tab's form and graph, the grid's LEN, the chain column's own-row highlight —
is the stage after this one; the window here only keeps building and behaving.

**Changed:**

- **Every channel keeps its own time (§25).** `Phrase` carries its **length** —
  `Phrase::steps`, 1–64 — and its cells are `Phrase::cells`; a step is six ticks at the
  straight groove, always, so a phrase lasts exactly what its groove and its length make
  it. `Song::stepsPerBar`, `Song::barSteps`, `Song::beatsPerBar`, `Song::barTicks()`,
  `Song::steps()`, `stepsOfBar()`, the bar table and every `barStartTick` / `barAtTick` /
  `barLengthTicks` are gone, and so are `ClockConfig::beatsPerBar` and `Clock::barTicks()`
  — the clock counts ticks and nothing else now. In their place, per channel:
  `Song::rowStartTicks[ch]` (a prefix sum over that channel's row durations, built by
  `buildRowTables()` when a song is published), `rowStartTick(s, ch, row)`,
  `rowAtTick(s, ch, tick, row, inRow)`, `rowTicks()`, `phraseTicks()`, `songTicks()` and
  `longestChain()`. The Player fires each channel from its own row and step, and
  `Player::Position` is `{row, step, phrase}`; the processor publishes `channelRow(ch)`
  and `channelStep(ch)` beside `trackerTick()`. T cells sit at their **own channel's**
  ticks (`buildTempoMap` gathers all four and sorts them, the lowest channel winning a
  tie), the recorder quantises to the channel's own grid, `RecordMessage::bar` is
  `::row`, and the plugin's own transport loops the longest chain (`setLoopRows`,
  `loopFirstRow`, `loopLastRow`).
- **A row with no phrase is 96 ticks** with a note-off at its start, as §25 says, and so
  is every row past the end of a chain. That is the one place where the model can put two
  channels out of step by accident: in a song whose rows are not sixteen straight steps,
  a channel resting for a row drifts from the others by the difference. It cost
  `meter-study` its alignment when the six demo songs were regenerated (three 12-step
  channels against one empty 16-step row), so `make_songs.py` now writes a rest as a
  phrase of the section's own width holding a note off — the same sound, and the channel
  stays with the others. Considered making an empty row take the length of the row the
  other channels are playing; rejected — a channel would then have no length of its own,
  which is the whole of §25.
- **A groove never moves the rows.** The rows lie end to end on the table built when the
  song was published, so a G — a cell's or a slot's — re-lays the steps *inside* the row
  and the row keeps its phrase's own length. A step that would start at or past the row's
  end does not fire, and a groove that ends early leaves the last note sustaining, which
  is §9.2 counted in rows instead of bars. The alternative — a G that changes what the
  row is worth — would make a locate unrepresentable and a T cell's tick depend on what
  played before it.
- **Song file format 6 (§25).** A phrase writes its own `steps` (its length) and its
  cells as `cells`; `stepsPerBar`, `barSteps` and `beatsPerBar` are not written at all.
  Loading format 5 or older converts as §25 says: every used phrase takes the file's steps
  per bar, and a bar override becomes the length of the phrase in that bar — a phrase used
  under two different overrides is **duplicated** into the first free slot and that bar's
  chain entry points at the copy (`lengthsFromBars` in `BankJson.cpp`). A file whose bar
  ticks were not six per step (a 3/4 song at sixteen steps) keeps its step *count*, so its
  rows are the length those steps really are. The plugin state carries the same JSON, and
  `songFileText` writes format 6 with the bank beside it as before.
- **Level changes are zombie-mode writes (§26).** `Driver::setLevel(ch)` emits the
  shortest sequence of NRx2 writes that leaves the chip's volume at the level the driver
  wants and the envelope it wants in the register, computed by a breadth-first search over
  (volume, direction, period) using the APU's own write rule — the search is driven by the
  rule rather than by a table, so it is right by construction and would follow a console
  whose rule differed. It is used by the table volume column, an E that keeps the
  envelope's direction and rate, a shaped envelope's step, CC7, the Level lane and the
  release fade. A trigger is left only where a driver needs one: a plain note-on, R, and
  an E that moves the direction or the rate. The driver models the chip's envelope
  (`Voice::volume`, `hwPeriod`, `hwUp`, `hwRun`, `hwOn`, `hwInitial`), moved on by
  `emitNrx2()` exactly as `Apu::writeSquare` moves it, and by `markTrigger()` at every
  trigger. From a holding envelope a step up costs one write and a step down three; nine
  is the worst case over all sixteen levels. A zombie write carries the target in NRx2's
  high nibble — free, since the chip reads it only at the next trigger — except that the
  DAC-off pattern (level 0 with the direction down) is written as level 1 instead.
- **The quiet edge is gone (§26).** `GlobalParams::volumeAtEdges`,
  `Driver::kAlignToQuietEdge` and the processor's re-sorting of a delayed burst are
  deleted: no program on a Game Boy can wait for a pulse's low half, so neither does
  ChipBoy. The **parameter** `vol_edges` stays, and does nothing: removing it would change
  the host-visible parameter table, which the two Reaper projects and
  `make_demo.py --paramdump` are written against, for a switch nobody should use again.
  The Hardware tab's row now reads *no effect* and says why. The table is byte for byte
  the one it was (76 parameters, checked).
- **Shaped envelopes (§27).** `bank::Envelope` on every instrument: a **mode** (Chip or
  Shaped) and, for Shaped, Attack, Peak, Decay, Sustain and Release with a curve each —
  linear, exponential (fast at the start) or logarithmic (slow at it). The curves are
  integer arithmetic in `bank::envSegmentLevel` — `t/n`, `1 − (1 − t/n)²` and `(t/n)²`,
  rounded half away from zero — so the per-tick level list is a constant of the song and
  a playback ROM can hold it. The driver renders one level per tick and writes it only
  when it changes, always through §26: a shaped note-on writes NRx2 with the level, the
  direction bit **up** and no rate, so a level of zero still leaves the DAC on and every
  step that follows can be a zombie write. Wave and kit instruments take the four NR32
  levels (the shaped level over four, as a table's volume column already scales). A
  note-off starts the Release under Note-off = Release, and the note ends when the release
  does; a table's volume column, an E, CC7 or the Level lane **takes the envelope over**
  and the segments left stop until the next plain note-on. The velocity and the Level lane
  do not set a shaped note's start level — the envelope owns it — which is the one thing
  §27 leaves open and is listed in the audit. Factory instruments are all Chip, and JSON,
  presets and the plugin state carry the fields (`envMode` alone when the mode is Chip, so
  a factory bank reads as it always did but for one property).
- **A table's first row fires with the note-on (§31).** `startVoice` runs row 0 inside the
  note's own event, after the instrument has loaded and before the trigger, for Tick-mode
  tables as well as Step-mode ones — so a wave kick whose table drops the pitch starts
  dropping at once and the raw note is never heard on its own. `Voice::tableJustStarted`
  keeps the tick that follows from taking a second row, so the rows after the first still
  step on the ticks. Firing row 0 *after* the trigger was measured too: it puts the raw
  period in the stream ahead of the drop, which is exactly what §31 asks not to happen.
- **A repeated pitch is never legato (§31).** A MIDI note-on at the pitch already sounding
  is plain — it triggers — whatever the instrument's Overlap says; legato is for moving
  between pitches, and a drum hit in succession is a hit. **The note-on order** was
  audited with it and is unchanged and now tested: instrument → table row 0 → command
  slots → the cell's own commands → the register writes ending in the trigger; the 360 Hz
  pitch clock restarts at the note and its first update is one full period later, so a
  bend never doubles the note's own period write; a D delays the whole of that, row 0
  included; the DMG wave dance (NR30 off, sixteen bytes, NR30 on) precedes the first
  period write; and a Hybrid cell's commands still wait for the tick's note-on and land on
  the sounding voice when none comes.
- **The table run in the view (§32).** `VoiceView` gains `tableRow` (−1 when no table is
  running) and `tableRun`, a serial that counts the runs a channel has started — a plain
  note-on with a table, an A, a table override, an instrument reload — so a window showing
  one table can tell which channel's run started last. `Driver::beginTableRun()` is the
  one place a run starts. The processor publishes them as `ScopeBuffers::tableRun`, packed
  by `packTableRun()`; `packState2` is untouched, so the link region's layout is the same.
- **`chipboy_fuzz` (§28).** `tools/fuzz/main.cpp`: a seed builds a random song — phrases
  1–64 steps long with their own grooves, random cells on all four channels, every command
  letter with arguments right across the byte, random note sources — writes it through the
  song writer, opens it through the reader, and plays it on the plugin's own transport in
  blocks of random size with tempo changes, PLAYS changes, locates, loops and MIDI thrown
  in. A run fails on a NaN or an infinity, on a block that takes longer than 400 ms, or on
  anything still audible 100 ms after all notes off. The CTest runs eight fixed seeds; a
  failure prints the seed to replay. "Audible" is the swing inside a block minus the
  drift across it: a DAC switched off holds its last level (reference §9) and the RAW
  model has no coupling to take that offset away, so an absolute threshold would fail on
  a silence that is silent.
- **What the fuzz found.** A note-on waiting for its tick under notes-on-tick survived an
  all-notes-off: the driver's pending queue was not part of "every internal flush", so a
  panic could be followed by the note it had just silenced. §8 counts a waiting note as a
  delayed start, so `Driver::allNotesOff` now drops that channel's entries from the queue.
  A regression test holds a note back with notes-on-tick, silences the channel and checks
  that the ticks after it write no trigger. One hundred and forty seeds pass since.
- **The demo, the songs and the checks.** `Demo/ChipBoy Demo.cbsong` and
  `Demo/chipboy_demo_hybrid.state` are regenerated (format 6 and the new register stream),
  `tools/demo/make_songs.py` writes format 6 — a phrase's length is part of its identity
  now, so a bar of text at another width interns as another phrase, which is §25's
  duplication rule applied at generation — and the six songs are regenerated with it. The
  record test's four passes still agree register for register, `demo_song_matches`,
  `demo_state_matches` and the six `demo_songs_load` still pass, and the paramdump table
  is identical. Measured with `--play-song`, `neon-grid`'s stabs are where they were:
  PU2's bars 13–16 read 0.09990 / 0.09895 / 0.09931 / 0.09864 before and 0.09996 / 0.09886
  / 0.09932 / 0.09854 after, with its peak 0.27010 → 0.26992 — the level changes sound the
  same and are now writes the hardware would take.
- **The audit (§28).** [`docs/HARDWARE_DRIVER_AUDIT.md`](docs/HARDWARE_DRIVER_AUDIT.md)
  lists every driver behaviour against its register writes and its clock, marks what is
  plugin-only (MIDI, Hybrid's live notes, the Voice link, the record path, the analog
  model, the own transport and the prefix tables), keeps the approximations that stay with
  the reason each one stays — a level change while the chip's envelope is running, the
  length counter's effect on the enabled flag, the 64 Hz envelope phase — and ends with
  the binary layout a playback ROM needs for format 6 and its bank.
- **Tests.** 159 core tests (13 new: the zombie sequence against a real `Apu` on both
  consoles and on noise, the shortest-sequence counts, a table volume column without a
  retrigger, an E that moves the envelope against one that does not, the shaped per-tick
  level list and its curves, a shaped wave instrument on NR32, a table volume column
  taking a shaped envelope over, the table's first row at the note-on, the kick played
  twice, the same-pitch retrigger, the table run serial, and the all-notes-off regression;
  the tracker's bar tests became row tests, with two channels drifting apart by design,
  a locate exact per channel and an empty row's 96 ticks). `Apu::channelVolume()` is new,
  for tests to ask the chip what the volume really is. The link test checks format 6, two
  channels of different lengths, the row tables and the format-5 conversion including the
  duplicate case; `chipboy_fuzz` joins the CTest list.

### 2026-09-09 — the LSDj parity harness

`docs/COMMANDS_AND_TEMPO.md` §31 asked for a way to stop guessing at what LSDj
does, so `tools/lsdjref/` measures it. It authors test songs into an LSDj save,
plays them on an LSDj 9.2 ROM the user owns inside SameBoy's core with a log on
FF10–FF3F, plays the same songs through ChipBoy's driver with its own write log,
and diffs the two streams. The findings are `docs/LSDJ_PARITY.md`; the engine
changes they call for are a separate round.

- **Opt in and tool only.** `-DCHIPBOY_LSDJREF=ON` fetches SameBoy (MIT) and
  builds two console tools; without it nothing new is fetched, configured or
  compiled, and the default build is byte for byte what it was. SameBoy links
  into `lsdjref_trace` and nothing else — rule L1 stands. Recorded in
  `docs/LICENSING.md` §1 along with liblsdj, which was read for the save layout
  and from which no code is taken.
- **The ROM never enters the tree.** It is named by `CHIPBOY_LSDJ_ROM`; `*.gb`
  and `*.sav` stay ignored; every test skips (exit 77) when the ROM, the boot
  ROMs or Python are missing, so `ctest` on a plain checkout is clean and
  Actions never sees a ROM. Nothing of the ROM's contents is written down —
  the findings are register addresses, values and cycle counts.
- **Twenty-three test songs** in `tools/lsdjref/cases.spec`, one file read by
  both the save writer and the compare tool, covering the plain note-on, V at
  four speeds × four depths in each of the four PITCH modes, L, P, E, a table's
  volume column, bare notes, R, K, D, C, grooves and hops, a wave kick and
  noise.
- **Eighteen verdicts**, of which fourteen are differences. The consequential
  ones: LSDj's pitch clock is 11712 cycles and not 11651; one vibrato cycle is
  64/(x + 1) updates, not 720/x, so ChipBoy's vibrato is twelve to twenty-two
  times too slow; L takes x + 1 updates and interpolates in semitones; P is
  neither linear in its value nor in the domain §7 gives it; and every level
  change LSDj makes is a zombie-mode NRx2 sequence with no trigger — `09 11 18`
  down, `08` up — which is what §26 already decided and now has numbers behind
  it. §31's rule that a table's first row fires with the note-on is confirmed
  against the real thing: 2932 cycles after the trigger, a thirtieth of a tick.
- **Two CTest entries** appear with the option: `lsdjref_baseline` authors the
  baseline save, plays it and checks the first note-on is NR10, NR11, NR12,
  NR13, NR14 in that order; `lsdjref_compare` writes the comparison report as
  an artefact rather than a pass or a failure — a difference is a finding, not
  yet a bug.

---
### 2026-09-08 — six demo songs and the hybrid project (content)

The third addendum's §20 and §24 ([`docs/COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md)),
content side: the hybrid Reaper project, six original songs that each carry their own
bank, and the check that keeps them honest. Nothing under `Source/` changed.

**Changed:**

- **`Demo/ChipBoy Demo (hybrid).rpp` (§20).** The same MIDI item as the main project and
  only two envelopes — the model and De-click, bars 14–16, as the main project has them —
  with everything else in the plugin's **saved state**, carried inside the VST chunk.
  `vst_chunk_lines()` in `tools/demo/make_demo.py` takes the state bytes now: the header's
  size field holds their length, the bytes follow as base64 lines of 128 characters (a
  multiple of four, so a line is whole bytes wherever Reaper joins them), and the
  terminator is the line it always was. With no state the size is zero and the two older
  projects come out byte for byte as before. The state is `Demo/chipboy_demo_hybrid.state`
  — `chipboy_recordtest --write-state`, checked by `demo_state_matches` — which holds one
  song tab: the demo song *and its bank*, all four channels on Hybrid, PU1's Source on
  MIDI 1 and NOI's Velocity on instrument bank. The automation JSON gains nothing from it:
  the hybrid project's whole story is "notes from MIDI, everything else from the song", so
  there is no lane left to describe. `Demo/README.md` says what the state carries and how
  to build the same thing by hand in another host.
- **`tools/demo/make_songs.py` and `Demo/songs/*.cbsong` (§24).** Six original
  compositions — written for this repository, in the style of the machine rather than of
  any record; no melody, bass line or drum pattern is taken from anywhere, and no LSDj
  content is involved. Each is a **format 5** song file, so it brings its own bank:
  `groove-study` (one four-bar tune under straight, 7 5 swing, 8 8 8 triplets and an 8 4
  shuffle put there by a G on every channel, then G's revert form), `meter-study` (3/4 at
  twelve steps, a fourteen-step 7/8 bar and two twenty-step 5/4 bars by override, one
  tempo throughout), `route-theme` (a bright route theme: pulse melody, a C-arpeggio chord
  track, a walking wave bass, noise drums), `puffball-bounce` (W duty changes by section,
  V on the long notes, a wave bass killed three ticks after every note, a bridge, an S
  sweep on PU1), `neon-grid` (the wave channel as a drum machine: a kick whose table falls
  in semitones under Drum pitch speed while E steps the level, a snare that is an F frame
  run through a ragged wave) and `wave-study` (half time: F frame walks under held bare
  notes, W switching the wave slot, P wobbles in Tick pitch speed, a drop).
- **The songs are Python data with a small compiler.** A bank is `pulse()`, `wave()`,
  `noise()`, `table()` and `wav()` calls with names instead of slot numbers; music is
  sections of bars, each a groove and one bar of text per channel, a step being
  `C-4@lead:96%table+V9,4` or `.` or `off`. The compiler resolves every name against the
  song's own bank, interns identical bars into phrases, and refuses at generation time
  what the machine would refuse silently: a letter that means nothing on its channel
  (§2), an argument out of range, an instrument of the wrong kind, a bank entry nothing
  plays, a bar under two channels, and a groove that would put a written step past the
  bar's end (§9.2).
- **`chipboy_recordtest --play-song FILE [bars]`**, and a CTest `demo_songs_load` per file
  in `Demo/songs`. The file opens in a tab, the plugin's own transport plays it at the
  song's tempo with no play head (§16), and the run is measured — once for the mix and
  once per soloed channel, with the headphone noise and the LCD line switched off so
  silence is silence. It fails on a NaN or an infinity, on a run that is silent
  throughout, and on a channel whose audio never changes at all, and prints the RMS of
  every bar of every channel either way. All six pass at eight bars in about two seconds
  each.

**Why:** §24 asks for songs that are pages of the command table played rather than
written down, and a song that nobody has heard is a guess. The RMS table is the listening:
it is what caught two arrangement bugs that sounded like nothing at all (below).

**Considered:** measuring per-channel level from the driver's write log or the scope
rings instead of playing the song five times. The rings are 4096 entries and a noise
channel overruns them inside a block, and a register log says what was written rather
than what came out — the solo runs cost two seconds and measure the thing itself.

**Three things the numbers found, and what the songs do about them.**

- **A note below its channel's lowest period does not sound at all.**
  `Driver::periodForNote` returns −1 under 64 Hz on a pulse channel (32 Hz on the wave
  channel) and the voice never starts. Two bass lines were written an octave too low and
  were simply absent — four bars of a column of zeroes in the RMS table. The compiler
  now knows each channel's floor (C-2 on the pulse channels, C-1 on the wave channel) and
  rejects a note under it.
- **A bar with no phrase does not stop a channel that has stacked notes.** Every cell
  note-on pushes onto the driver's held-note stack (§8, last-note priority) and only a
  note-off pops one, so after a dozen bars of tracker notes the note-off a missing phrase
  sends just falls back to the note under it. It is the documented behaviour and the
  right one for MIDI; for a song it means an ending needs **K**, which clears the stack,
  or the loop wrap and the transport stop, which flush everything. The songs that end use
  E to fade the pulse channels and K to stop the wave channel; `wave-study`'s drop kills
  its bass rather than trusting an empty bar.
- **A groove only fills the bar if its ticks average six.** `4 4 4` at sixteen steps
  covers two thirds of a 4/4 bar and leaves the last note sustaining, which is what §9.2
  says and not what a triplet section wants. `8 8 8` is the one that fits: twelve steps of
  eight ticks fill the bar exactly as eighth-note triplets and steps 12–15 never fire —
  checked directly, a note written on step 12 under that groove is silent and one on step
  11 sounds. `groove-study`'s triplet section is written to twelve steps for that reason.

### 2026-09-08 — song tabs, the head, the master section, scopes, Hybrid (interface)

The third addendum ([`docs/COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md) §18–§23),
interface side, on top of the engine stage below. Nothing here is new behaviour: it is
the window catching up with the tabs, the tempo readout, the noise split, the third
playback source and the scope that was meant to hold still.

**Changed:**

- **The song tab strip (§18).** `ui::SongTabStrip` stands where the Tracker tab's
  summary line was: one tab per loaded song with its name, a dot while it has unsaved
  work and an × that closes it — asking first when that dot is there — and a **+** that
  starts an empty song on the factory bank. Clicking a tab calls `setActiveTab`, and the
  strip is rebuilt from `tabCount` / `tabName` / `tabFile` / `tabDirty` on the panel's
  timer, which costs a comparison when nothing moved. *Load song…* now calls
  `openSongFileInTab`, so a song opens in a tab of its own — unless the active tab is a
  fresh untitled song with nothing to lose, where it lands in place through
  `loadSongFile`; *Save song…* writes the tab's own file, or opens a chooser when it has
  none, and the tab takes the file's name. The window keeps at least one song open.
- **Every other tab already followed the active one**, through `song()` and `bank()`;
  the editor's timer notices the two pointers change on a switch and sends
  `songChanged` / `bankChanged` everywhere. `chipboy_uishot --tab-switch` proves it
  rather than assuming it: it marks the *other* tab's bank and song — a renamed
  instrument, a table step, a wave sample, a kit, a groove — then shoots the Instrument,
  Tables, Grooves, Waves and Kits panes on either side of a switch and fails if any of
  them drew the same picture twice. (Without the marking the test is worthless: both
  tabs start on copies of the factory bank, and a new song's grooves *are* the factory
  grooves, so four of the five panes were identical for good reasons.)
- **The header's Bank group is the active song's bank (§18).** Its menu reads *Load bank
  into this song…*, *Save this song's bank…* and *Reset this song's bank to factory*,
  and the arrows say so too. The **STOCK / MODIFIED badge stays the window's**: it reads
  the de-click and soften-pops departures, which §18 lists among the things that are
  global and not per song, so there is nothing per tab for it to say. Considered making
  it per tab as the brief reads; rejected because its two inputs are global parameters
  and a badge that never differed between tabs would only imply that they could.
- **The header's tempo is a readout (§19).** The Song tempo stepper leaves the header; a
  small well takes its place with the tempo in force — `effectiveTempo()`, one decimal —
  and a *host* or *song* tag beside it, sized to the tag so neither word is ever cut
  short. The Host / Song switch stays, with the same rules about the plugin's own
  transport. The song's **master tempo** is typed in the Tracker head instead, through
  `setMasterTempo`, which is one undo step over the song and the parameter together.
- **The Tracker head is two rows of grouped tools (§23).** TRANSPORT (*Play* 62, *Stop*
  62, *Loop* 52, the LED, *playing*, the position readout) · RECORD (*Rec* 62) on the
  first row; SONG (*Tempo* 84, *Start* 84, *Beats* 62, *Steps / bar* 80) · FILE (*Save
  song…* 104, *Load song…* 104, *Export .gb* 96) on the second, each group under a
  `draw::caption` and separated by a 16 px gap with a hairline down the middle. The
  budget is unchanged and asserted in the source: 12 caption + 26 controls, 4, the same
  again, 6, and the 26 px tab strip is 112 exactly, so the lane keeps its 400 px and the
  tab still asks for no scrolling at 1180 × 1020. **Beats is live in both tempo modes**
  now (§11 as amended, §19); *Start* still greys in Host mode. What a file did goes to
  the status bar, which always had it — the head has no line of its own any more.
- **The master section (§21).** One **VOL** stepper, 0–7, writes `master_l` and
  `master_r` as one undo step (an explicit `beginGesture` / `endGesture` around the two
  `setParam` calls). Both parameters stay, so the `M` command and existing automation
  still address left and right; the readout shows the left value and, when something has
  moved them apart, both — `7·5` — with the tooltip naming which is which. VOL L and
  VOL R are gone. **LCD Whine** joins Headphone Noise and De-click as the third switch,
  and the three of them plus the one stepper take exactly the height the two steppers
  and two switches did, so the master scope is the same size. In the Hardware tab the
  whine row no longer greys when the hiss is off — they are independent now — and the
  two rows' measured facts were split along with them.
- **The scopes hold still (§22).** The window was always meant to be two periods from a
  rising edge; what it actually did was start on *the last rising edge before the
  window*, and the window's end moves with the audio thread. A pulse has one rising edge
  in a period, so it picked the same one every frame and was already still — a wave has
  up to sixteen, so which one was "last" was effectively random and the trace jumped by a
  fraction of a period every frame. Measured, not guessed: two frames of a steady tone
  through the old code differ by 3832 pixels on the wave scope and by 0 on both pulses.
  The edge is now chosen by what the waveform *is* — the furthest rise, then the level
  held longest before it, then the lowest level rise
  — three keys that are properties of the shape rather than of where the search began, so
  they name the same phase every frame, re-lock in one frame when the shape changes, and
  hold the same edge while vibrato moves the period. `paint()` also stopped reading the
  `latestCycle` atomic live: the timer captures it with the samples, so one snapshot
  draws one picture however often it is painted. With less history in the ring than the
  window asks for the search moves up to what there is and the window keeps its length.
  Noise keeps its fixed window, and **a kit now keeps one too** (`setFixedWindow`, set
  from the instrument the driver is actually playing): a kit is a sample, not a repeating
  wave, so there is no period to lock to. `chipboy_uishot --scope-check` holds a note on
  each channel and compares two renderings of every scope a fifth of a second apart; all
  four are pixel-identical now.
- **PLAYS is a three-way switch (§20).** MIDI / Trkr / **Hyb**, writing
  `tracker::NoteSource::Hybrid`, with a sentence of tooltip each. A Hybrid channel shows
  the roll's note greyed in the note column, as a MIDI channel does, since its cells'
  notes and OFFs are ignored. Its **strip** says so where it matters: a HYBRID tag beside
  the channel name, and the Instrument, Table and both command slots greyed — the slots
  reading *from the cells* where the resolved command would be — with the same tooltip on
  all four, *the tracker's cells drive this channel*. The third choice costs the head
  row about 30 px, which comes out of the phrase's groove chip: it now says as much as
  fits, the slot and its ticks where there is room, the ticks alone at the demo's width,
  the slot number when a narrower window leaves only a chip, and the whole of it stays in
  the tooltip and the menu.
- **`chipboy_uishot`** opens `--song` with `openSongFileInTab`, so a shot shows the strip
  with two tabs in it, and gained `--hybrid`, `--scope-check` and `--tab-switch`.
- Screenshots regenerated: the Tracker tab, the header and mixer (`main-instrument.png`),
  the Hardware tab, Grooves and the Voice window.

**Tests:** `chipboy_linktest`, `chipboy_recordtest`, `demo_song_matches` and
`demo_state_matches` pass unchanged — no engine file was touched. The two new
`chipboy_uishot` modes are the interface's own checks and are run by hand, since they
need a display.

**Departures from the brief:** the STOCK badge is not per tab, for the reason above.

### 2026-09-08 — song tabs, hybrid playback, the noise split (engine)

The third addendum ([`docs/COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md) §18–§21),
engine side, and §11/§19 as amended: the host's time signature stays out of the tracker.
The interface — the tab strip, the three-way PLAYS switch, the master section's one VOL
control and the Tracker head — is the stage after this one; the window here only keeps
building and behaving, with the PLAYS switch still a two-way control that shows Hybrid
as MIDI and never writes over it.

**Changed:**

- **Song tabs (§18).** `plugin::SongTab` is `{song, bank, file, name, bankName, dirty,
  id}` and the processor holds a list of them with an active index. `song()`, `bank()`
  and `bankName()` are the *active* tab's, and every publish, mutate, edit, restore,
  preset, arm and link path already went through those, so they follow the tab without
  knowing about it. `setActiveTab(i)` sends all notes off (an atomic the audio thread
  drains in front of the block's events), swaps the two published pointers without
  rebuilding anything — the tempo map and the bar table were built when that song was
  published — and hands the tab's master tempo to the Song tempo parameter. `newTab()`
  starts an empty song on the factory bank; `closeTab(i)` refuses the last one;
  `addTab(song, bank, name, bankName)` builds one from a song and a bank already in
  hand. Global things stay global: model, hardware, master, noise and whine, link, the
  tempo source, quantize and the channel Source/Level/Pan/Transpose/Velocity/Keyswitch
  lanes are parameters, and parameters are not per tab.
- **Undo knows which tab it belongs to.** `BankAction` and `SongAction` carry a **tab
  id** — stable across closes, unlike an index — and `restoreBank`/`restoreSong` take
  it, activate that tab and put the snapshot back; a step whose tab has been closed does
  nothing rather than landing in the wrong song. `Source/plugin/ui/EditHistory.*` did
  not change at all: the actions live in the processor, which is where the tab is known.
- **Plugin state version 2.** A `tabs` child with one `tab` per song (name, file path,
  bank name, bank JSON, song JSON) and `activeTab` beside it. A version-1 state — one
  bank and one song at the root — loads as one tab.
- **Song file format 5 (§18).** `songFileText` writes the whole bank into the file
  (`bankData`, through the bank writer, so kits carry their samples), beside the bank's
  name and the instrument names it already recorded. `loadSong` takes a `bank::Bank*`
  and fills it when the file carries one, saying so in `SongReport::hasBank`; the name
  report is then against *that* bank, which has nothing to differ from. A format-4 file
  still loads the song alone. `openSongFileInTab()` opens a new tab — with the file's
  own bank, or a copy of the active one for an older file and the difference report as
  before — and `loadSongFile()` replaces the active tab's song, and its bank when the
  file brought one, as one undo step. `Demo/ChipBoy Demo.cbsong` is regenerated: the
  same recording, now with the factory bank inside it (104 KB → 147 KB), and
  `demo_song_matches` still compares it byte for byte.
- **The master tempo, both ways (§19).** `Song::tempoBpm` is the tab's master tempo.
  `setMasterTempo(bpm)` writes the song and the Song tempo parameter as one undo step,
  and a song snapshot restores both, so undoing a tempo edit moves the lane back with
  it. A host moving the parameter is written back into the active song's `tempoBpm` on
  the timer, in memory and with no undo step, which is also where the tempo map is
  rebuilt on the new base. Activating a tab pushes its tempo into the parameter.
  `tempoInForce()` is now `effectiveTempo()` — the name the header reads it by: the
  host's BPM in Host mode, the song's tempo in force (its master tempo, or the T last
  passed) in Song mode, straight from the clock, which already knows the map.
- **The host contributes the tempo, never the signature (§11 amended, §19).**
  `driver::Transport::beatsPerBar` is gone; `Clock::beatsPerBar()` is
  `ClockConfig::beatsPerBar` — the song's — in both sources, and the processor no longer
  reads `getTimeSignature()` at all. The Player's bar ticks, the loop's bar arithmetic
  and the position readout all take `Song::barTicks()`. A DAW going 4/4 → 3/4 → 5/4
  under a 4/4 song now moves its own bar markers and nothing else; the ticks run on, so
  the two line back up when the DAW's bars add up to the song's. Considered: keeping the
  host's signature in Host mode. Rejected — it silently re-cut every song's step grid at
  the signature change, which is the bug this fixes, and the *Beats* field is the place
  to say a song is in 3/4.
- **Hybrid playback (§20).** `tracker::NoteSource::Hybrid = 2`, through the JSON
  (song format 5), the plugin state and the song file. The Player fires a Hybrid
  channel's cells but drops their note and OFF columns, sending the rest as a `Command`
  event marked `NoteEvent::hybrid`; a cell holding nothing but a note is not sent at
  all, and a missing phrase, a stop and a timeline jump no longer flush a Hybrid channel
  — the note sounding on it is the player's, not the song's. In the driver a Hybrid
  channel's `ChannelParams` are read through `effective(ch)`, which zeroes Instrument,
  Table and both command slots, and its keyswitch and command octaves are ignored
  whatever the parameters say; Level, Pan, Transpose and the Velocity mode still apply.
  A hybrid cell's instrument and table columns are a *selection* for the next note-on
  (`ksFromCell`, so the velocity bank is not applied on top) and what is sounding is not
  reloaded; its commands are **held for the tick**: a note-on inside it takes them after
  `fireSlots` — which is empty there — exactly as a cell's own commands, and with none
  they land on the sounding voice at the tick's end, where a slot change would have
  fired. A `D` among them holds them that many ticks longer; an `L` that lands with no
  note under it waits and becomes the next note-on's portamento. The processor sorts a
  block's events by (offset, kind): a flush, then the song's cells, then MIDI, so a cell
  is always in front of the note-on it shapes.
- **Recording on a Hybrid channel** writes cells as on any channel, through the
  parameters the channel really read: no commands from the inert slots, no cell for a
  note in the inert keyswitch or command octaves.
- **Headphone Noise and LCD Whine are two switches (§21).** In the renderer the hiss
  and the frame hum are `Options::noise` and the display's line and its harmonic are
  `Options::lcd`; the phases advance whenever either is on, so switching one does not
  move the other's, and the per-console levels are untouched. The `lcd` parameter's
  display name is **LCD Whine** (its id and range are unchanged) and the two tooltips
  say what each switch now covers. The test harness's one `noise` flag still means the
  whole floor, so every existing render test measures what it did.
- **Master volume**: no engine change. `master_l` and `master_r`, the `M` command and
  their existing tests stand as they were; one control driving both is the interface
  stage's.

**Tests:**

- Core: a MIDI note under Hybrid takes the cell's instrument and commands in the same
  tick; a cell with no note lands on the sounding one at the tick; cell notes and OFFs
  are ignored; the slots, the keyswitch octave and the command octave are inert; `L` is
  the next note's portamento; a cell's `D` holds its commands back. The Player's Hybrid
  cells keep everything but the note, and a missing phrase leaves the note alone. The
  bar is the song's beats per bar in both tempo sources. Headphone Noise and LCD Whine
  are independent: the line is there with the hiss off, at its measured level, and the
  hiss is there with the whine off.
- `chipboy_recordtest` grew a **fourth pass**: a fresh processor with the demo song in a
  tab, all four channels Hybrid, the MIDI file, no automation lanes and only the two
  static parameters that are not inert under Hybrid (`ch1_source`, `ch4_velocity`). Its
  per-channel register stream equals pass 1's exactly, so MIDI plus the song in Hybrid
  reproduces the recorded demo. `--write-state` / `--check-state` build that same
  project and write the processor's state to `Demo/chipboy_demo_hybrid.state`, checked
  by the new CTest `demo_state_matches`. The file is deterministic: the tool pins the
  instance UUID and name, the tab is built from the song file's contents rather than
  opened from it so no absolute path is stored, and nothing in the state carries a
  timestamp.
- `chipboy_linktest` gained the tabs (new, switch, close, undo across tabs, two tabs and
  a version-1 state through `getStateInformation`), the format-5 round trip with its
  bank, a format-4 file opening in a tab with a copy of the active bank and its
  difference report, the master tempo mirroring both ways and per tab, the header's
  readout in both tempo modes, Hybrid through the song file and the state, and a play
  head that changes its time signature mid-song while the song's bars stand still.

**Departures from the brief:** one line of `tools/demo/make_demo.py` (the embedded
parameter table's name for `lcd`) and the one row it generates in `Demo/PARAMETERS.md`
were changed with it, so `make_demo.py --paramdump` still reports *identical*. Nothing
else in `tools/demo` or `Demo/*.rpp` was touched — regenerating the two `.rpp` files and
the MIDI produces the same bytes.

### 2026-09-08 — typed fields, no wheel edits, command cells, undo (interface)

A quality-of-life round on both windows. Nothing in `Source/core` or
`Source/plugin/shared` changed; the spec's ranges and the command table
(`plugin::commandInfo`) are what the new validation checks against.
[`docs/UI_DESIGN.md`](docs/UI_DESIGN.md) §2.1 is the new "Editing conventions"
subsection these rules live in.

**Changed:**

- **Every number is typeable.** `ui::Stepper` opens an inline text box on a click (or
  Enter) in its readout; `ui::Knob` and `ui::Fader` open one on a double click. Enter
  commits, Escape cancels, focus loss commits. Digits, a leading minus where the range
  allows and hex while *Hex* is on are taken; anything else is refused and the field
  keeps what it had; a number outside the range is clamped to it. `Stepper::setTyped`
  is on by default now — the Song tempo, the strips' slots, the master volumes, the
  tracker's *Steps / bar* and the groove editor's slot all take typed values — and
  `setEntryFormat` lets a field whose readout is not a plain number (the song start, in
  tenths of a second) say how a typed value reads and parses. Segmented rows and
  switches stay click-only: a choice is not a number.
- **The wheel never edits.** The `mouseWheelMove` overrides are gone from `Knob`,
  `Stepper`, `TableGrid`, `PhraseGrid` and `GrooveEditor`, so JUCE's default forwards
  the event to whatever scrolls; `setScrollWheelEnabled(false)` covers the trim fader's
  slider, the command slots' letter box and the Voice's four combo boxes. `ChainColumn`
  keeps its wheel, which scrolls its bars rather than editing a cell. Considered:
  keeping the wheel with a modifier. Rejected — a modifier is not discoverable and the
  accident it prevents (scrolling a tab and retuning an instrument on the way past) is
  silent.
- **A command cell is two parts.** The letter is drawn in the accent colour against a
  hairline, then its values; a click on the letter opens a palette of the letters that
  channel can carry (`plugin::commandAppliesTo`), each with its name and argument
  description, plus *none* and the letter's revert form. Typing a letter still sets it,
  and now **keeps the values, clamped into the new letter's ranges** rather than
  replacing them with its defaults — an empty cell still takes the defaults, so one
  keystroke writes a command that does something. Typed arguments are validated against
  `commandInfo`: a digit the letter cannot hold is refused and the cell shows what it
  had (this tightened `typeDigit` for every grid column, not only commands). Command
  arguments stay base 10 in hex display, as spec §9.6 fixes them.
- **Right-click lists.** A right click on an **INS** cell lists the bank's used
  instruments by slot and name, the ones the channel plays first and the rest marked
  with their type; on a **TBL** cell, the bank's tables; on a chain cell, the phrases
  the song uses with how many bars play each. The left click still selects for typing.
  `PhraseGrid::setBank` is how the lane reads the names.
- **Undo and redo for every hand edit** (`Source/plugin/ui/EditHistory.*`, new). A
  `juce::UndoManager` per processor, capped with `setMaxNumberOfStoredUnits` at 64 MB
  with a floor of eight transactions — a Song snapshot is ~300 KB and a Bank ~40 KB plus
  its kit samples, so the cap is counted in bytes. Three kinds of action: a parameter
  (id, old and new normalised values, applied with `setValueNotifyingHost` so the host
  sees an undo as it saw the gesture), a bank snapshot and a song snapshot — the
  before/after pointers the copy-on-write path already makes, so an edit costs no extra
  copy and only an undo does. `ChipBoyProcessor::editBank` / `editSong` are the undoable
  entry points; `mutateBank` / `mutateSong` stay as the plain path the timer and the
  link use. The controls find the history by walking up to their window
  (`ui::EditHistoryHost` on both editors), so two instances never share one.
  Surface: **↶ ↷** in the header right of the bank, Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y
  (never while a text box has the keys), tooltips naming the action ("Undo: PU1 Level
  inst → 11") and the status line saying what was undone. One gesture is one undo: a
  knob drag opens a transaction on `mouseDown` and the actions coalesce; a run of edits
  under the same name — the digits of one typed cell — joins the transaction still open.
  **Not** on the history, and never opening a transaction: host automation, a state
  restore (`setStateInformation` clears it), the Voice's edits, which are the Voice's
  own history, and anything the audio thread does.
- **The header keeps its height.** The two arrows cost 47 px, found by trimming the
  right cluster and by fixing the visualizer button's width — it said "Visualizer
  (open)" and grew by 32 px, which shoved the bank along beside it and could overlap the
  badge. It says the same thing in its colour now.
- `juce::ButtonParameterAttachment`, `juce::SliderParameterAttachment` and
  `juce::ComboBoxParameterAttachment` write straight to the parameter and cannot be
  undone, so the four places that used them (Quantize, Hex, Keyswitches, the trim, the
  Voice's velocity and instrument boxes) now use a `ParameterAttachment` with the click
  routed through the history. `plugin::ToggleParam` is the shared piece.

**Considered and rejected:** hooking undo to a global parameter listener instead of the
UI write path — it cannot tell a host's automation from a hand on a knob, which is the
one distinction that matters here. And making `mutateBank` / `mutateSong` themselves
undoable — the timer and the link call them, and a Voice pushing an instrument is not
the musician's edit.

**Known limit:** a command with two three-digit arguments (`Z 255,255`) is wider than
the 53 px the lane can give a command column and is clipped; every other letter fits.

---

### 2026-09-08 — the Tracker tab, the Grooves tab and presets (interface)

The window side of the second addendum ([`docs/COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md)
§17), on top of the engine stage above. Every API it uses was there already; nothing in
`Source/core` or `Source/plugin/shared` moved.

**Changed:**

- **The Phrases tab is the Tracker tab** (`Source/plugin/main/panels/TrackerPanel.*`,
  renamed from `PhrasesPanel.*`). The explanatory paragraph is gone; the 112 px head is
  now two 26 px tool rows — **Play / Stop / Loop**, the playing readout, **Rec**,
  **Steps / bar**; then *Start*, *Beats*, **Save song…**, **Load song…** and *Export
  .gb* — over a 48 px line that says what the song is and what the last file did. The
  transport buttons are live only while `ownsTransport()`; in a host they mirror it and
  are disabled, and *Loop* is `setLoopBars(0, -1)` plus `setLoop`, the whole song.
- **The chain is rotated into the column right of the lane**, 164 px where the groove
  editor sat: one row per bar, numbered 1, 2, 3… with the lowest at the top, the four
  channels' phrase cells and a fifth **STP** cell for `Song::barSteps` — blank is the
  song's *Steps / bar*, typed and blanked exactly as a phrase cell is. Its 22 px rows
  keep the lane's rhythm, it follows the playing bar, and typing in the empty row under
  the last bar grows the song. `ui::ChainStrip` became `ui::ChainColumn`.
- **The lane shows the bar's own steps**, `Song::stepsOfBar(bar)`, one to sixty-four:
  the grid is as tall as they ask and scrolls inside the tab's pane past sixteen —
  following the typing cursor and the row the selected channel is playing — which is the
  tab's only scrollbar. Sixteen steps still fit the 1180 × 1020 window exactly.
- **Each lane head carries its channel's record arm** — a red dot, `setChannelArm` /
  `channelArm`, saved in the song — and a **PLAYS** caption over the switch, whose
  options read **MIDI** and **Trkr** where they read Roll and Trk (the enum is
  unchanged). The tooltips are §14's wording: an armed channel records whatever it
  plays; an unarmed one never does.
- **A Grooves tab, after Tables.** The sixteen slots on the left as a bank list — the
  number, the ticks (*7 5*, *4 4 4*) and the swing the first pair makes, with a
  seventeenth row for groove 0 — and the existing `GrooveEditor` on the right, which now
  takes whatever width and row height the tab gives it: 520 px wide and 29 px rows
  against 164 and 22 in the lane's corner, so the bars that show the swing are 426 px
  instead of 70. The list and the editor's own stepper are one selection, and the editor
  still follows the groove in force for the selected channel until one of them browses
  elsewhere. The lane keeps the per-phrase groove chip.
- **Save preset… / Load preset…** join New and Dup in the Instrument tab, on the row
  under them: `bank::collectPreset` → `savePreset` to a `.cbi`, and `loadPreset` →
  `bank::placePreset` into the selected slot through the panel's own bank-edit path. The
  `PlaceReport` is summarised in the status bar — "Pluck → slot 3; table 5 → 9
  (renumbered); wave 2 reused".
- **The header's Tempo source is fixed on Song while the plugin owns the transport**
  (§16), disabled, with a tooltip saying why; automation cannot move it either. The
  **status bar** gained *transport own* / *transport host* beside the tempo, drawn only
  when the groups before it leave room, and a message line on the right — what a song
  load or a preset did — which replaces the tagline for twenty seconds. Panels post to
  it through `EditorPanel::onMessage`.
- **`chipboy_uishot --song <file>`** loads a `.cbsong` before the editor opens and
  leaves the processor without a play head, so the plugin owns the transport and the
  Tracker tab's buttons render live. `docs/screenshots/main-tracker.png` is
  `Demo/ChipBoy Demo.cbsong` with the factory bank, playing; `main-grooves.png` is the
  new tab. They replace `main-phrases.png` and `main-phrases-groove.png`.

**Why:** §17, and the step count is what forced the chain to turn: a bar is now a row
with a length of its own, and lengths are read down a column. Turning it also frees the
340 px the chain strip took across the head, which is where the transport and the file
buttons went.

**Considered:** keeping the groove editor beside the lane as well as in its own tab.
Rejected — two copies of one editor disagree the moment the window is narrow, and the
column it stood in is the only place the rotated chain can go without shrinking the
lane's four channel groups below the 236 px their six columns need. Also considered
letting the chain scroll with the lane inside the same pane: it must not, because the
lane scrolls in *steps* and the chain counts *bars*.

**Skipped / uncertain:** the Grooves tab opens on the lowest slot that is not straight,
because slot 1 of a fresh song is 6 6 and an editor full of equal bars teaches nothing;
after that it follows the groove in force for the selected channel, as it did beside the
lane. The Tracker tab shows the song's file name only when the file was opened from this
tab — the processor does not remember where a song came from, so a song
restored with the plugin state reads "17 bars · 16 steps a bar · 68 phrases" with no
name. `Stepper` grew opt-in typed entry (`setTyped`) for *Steps / bar*; every other
stepper still only steps, so nothing else changed under the keyboard.

### 2026-09-08 — bars, one-shot cells, arms, song files, presets, transport (engine)

The second addendum ([`docs/COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md) §11–§16),
engine side. The interface — the Tracker tab, the Grooves tab, the preset and transport
buttons — is the stage after this one; the window here only keeps building and behaving.

**Changed:**

- **Steps per bar is a number, 1–64, and a bar may have its own.** A phrase holds
  `tracker::kMaxSteps` = 64 cells; `Song::stepsPerBar` is clamped 1–64 and
  `Song::barSteps` gives one bar another count (0 = the song's). Step *i* starts at
  `groove ticks so far × bar ticks / (6 × steps per bar)`, which is the addendum's
  ⌊*i* × bar ticks / steps⌋ at the straight groove and, at sixteen steps in a 4/4 bar,
  exactly the running sum of the groove's entries that every song had before — so the
  demo is unchanged to the register. A bar's length is its steps × the song's step
  ticks, and the bars lie end to end from tick 0 through a **prefix table**
  (`Song::barStartSteps`), built in `buildBarTable()` when the song is published and
  read by `barAtTick()`/`barStartTick()` with a binary search, so nothing walks the
  overrides on the audio thread. The Player, the recorder's quantise, `stepAt` and the
  T cells all go through it; the chain is still one row per bar.
- **A cell's commands fire once.** `Driver::setSlotFromCell` is gone;
  `applyCellCommands()` applies a cell's two columns at their step and never writes a
  slot, so `v.slot[]` is the automation lanes' alone (§12). A persistent letter changes
  the running state and holds until the next plain note reloads the instrument; a
  per-note letter shapes that note; the revert form puts the letter back, once. They are
  applied inside the note-on, right after the slots fire, so a cell costs no second
  burst of register writes. A cell's `D` is read at the note-on and its commands wait
  with the note it delays.
- **The recorder follows.** A plain note's cell carries both slots in force, a bare
  note's carries the in-force per-note letters (C D K L R Z) and any slot that moved,
  and a slot change with no note is written as before — `Player::slotCells` took a
  mode (`Changed`, `Plain`, `Bare`, `All`) instead of a flag.
- **The command octave.** MIDI notes 0–11 never sound and never join the held stack:
  a note-on there fires CMD1 then CMD2 on whatever the channel is playing, without a
  trigger, and lands in the running state when nothing sounds. The recorder writes it
  as a slot-only cell at its step (`recordSlots(..., force)`).
- **Record arms.** `Song::recordArm[4]`, saved with the song and so with the plugin
  state, **on for a new song** so a song written before them records exactly as it used
  to. The processor records an armed channel's MIDI whatever its playback source; an
  unarmed channel never records; an armed Tracker channel lets incoming MIDI through and
  its lane goes quiet meanwhile, an unarmed one drops it. `Driver::setRecording(bool)`
  became `setRecordMask(uint32_t)`, a bit per channel.
- **Song format 4.** `steps` as a number, `barSteps`, `recordArm`, phrases of 64 cells
  written sparsely (each cell carries its step index, and empty cells are left out).
  Format 3 and older still load: sixteen dense cells, `stepsPerBar` 8 or 16, arms on.
- **Song files.** `Source/plugin/shared/SongFiles.h/.cpp`: `saveSong` / `loadSong` for
  `.cbsong`, the song JSON plus the bank's name and the name of every instrument slot
  the song uses; the report names the slots this bank has renamed or left empty. The
  processor has `saveSongFile()` and `loadSongFile()`, which publishes through the
  existing path. `Documents/ChipBoy/Songs` sits beside the banks folder, and the old
  unused `.chipboysong` dialogs in `BankFiles` are gone.
- **Instrument presets.** `bank::collectPreset` / `bank::placePreset`
  (`Source/core/Bank/Preset.h/.cpp`, no JUCE) walk an instrument's table, the tables its
  A commands start, the wave it plays and the waves its tables' W commands select, and
  its kit; placing reuses an identical table, wave or kit, otherwise takes the first free
  slot, renumbers every reference in the copies, and fails without touching the bank when
  a kind is full. `Source/plugin/shared/Presets.h/.cpp` is the `.cbi` file, written with
  the bank file's own writers.
- **The tracker's own transport.** With no play head, or one that reports no position,
  the `Clock` makes the transport itself: playing, its own seconds, the Song tempo (the
  source is forced to Song while it owns it), and a loop between two ticks. The processor
  has `transportPlay()`, `transportStop()`, `setLoop()`, `setLoopBars()`,
  `ownsTransport()` and `transportPlaying()`; the buttons ask on the message thread and
  the audio thread does it, so the clock keeps one owner. The Standalone plays this way.
- **`Demo/ChipBoy Demo.cbsong`** is the recorded demo, written by
  `chipboy_recordtest --write-song` through `saveSong`. It carries no timestamp, so
  `--check-song` compares a fresh recording against it byte for byte — the CTest
  `demo_song_matches`. The record test gained a third pass: the file loaded into a fresh
  processor, the factory bank, no MIDI and no play head, played on the plugin's own
  transport, drives the chip exactly as the recording did (2442 / 755 / 3686 / 1022 / 5
  register writes, the same as passes 1 and 2).
- **A host that gives a beat position but no seconds** now gets a time base derived from
  its ppq and tempo, so the Song timeline works there instead of silently free-running.

**Why:** §11–§16 as agreed. The one-shot cell is the load-bearing change: a cell that
wrote a slot re-fired at every note after it, so a vibrato written once stuck to the
whole song and a loop back to bar 1 did not play clean.

**Considered:** a prefix table of bar start *ticks*, as the addendum sketches. It holds
**steps** instead: the same table then serves whatever bar ticks the host reports, so a
time-signature change does not need the song republished, and the tick is one multiply
away. Also considered handing the Clock the loop in bars — it holds ticks, because the
song is what knows where its bars are.

**Consequence, worth knowing:** sixteen steps now divide the bar *whatever its length*,
where they used to be six ticks each and a short bar simply cut the grid off. In 3/4 a
step is four and a half ticks and all sixteen play; at 4/4 nothing moves at all. That is
what §11 asks for, and the tempo-map test was updated to it.

**Skipped / uncertain:** a `W` inside a table counts as a wave reference only when the
instrument that owns the table plays waves — on a pulse the same letter is the duty and
names nothing. Content equality for preset reuse includes the **name**, so a renamed
table takes a slot of its own rather than being silently adopted. The GrooveEditor and
the phrase grid still show sixteen rows: the grid that shows a bar's own step count is
the interface stage's.

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
