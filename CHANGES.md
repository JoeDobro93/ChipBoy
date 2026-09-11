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
| M4 — bank + driver | **done** 2026-09-07 — the bank with a factory set, the driver on its own tick, 11 driver tests. **2026-09-08**: the instrument carries the pitch (Pitch speed, vibrato shape/direction, Command rate, Table mode, Overlap); a note without an instrument only changes pitch (bare notes); four note-hang paths closed (all-notes-off, kill, event ordering, keyswitch-clear). Then: an instrument saves and loads on its own as a `.cbi` **preset** (`bank::collectPreset` / `bank::placePreset`), its tables, waves and kit carried along and renumbered into place. **2026-09-08 (third)**: **song file format 5** embeds the whole bank — instruments, tables, waves and kits with their samples — so a song file is complete on its own; a format-4 file still loads the song alone, against a copy of the active bank, with the name-difference report as before. **2026-09-09**: the driver is made **LSDj-exact** where the opt-in parity harness (`tools/lsdjref/`, `-DCHIPBOY_LSDJREF=ON`, `CHIPBOY_LSDJ_ROM` naming a ROM the user owns and that never enters the repository) measured a difference against a real LSDj 9.2.J ROM — the pitch clock (11712 cycles, free-running, global), the note table's period-unit interpolation, V/L/P/C's laws, level changes as LSDj's own zombie-mode NRx2 bytes (`09 11 18` down, `08` up), `E` and `K` never triggering, `R`'s signed-nibble volume and pitch-clock resync, and NRx4 written with every NRx3; `docs/COMMANDS_AND_TEMPO.md` §7 is rewritten from the measured numbers rather than the manual. The instrument gains a second envelope **mode**, **Shaped** (Attack/Peak/Decay/Sustain/Release, each with a curve), beside the original, now called **Chip**. Song, bank and `.cbsong` files move to **format 7**. `docs/LSDJ_PARITY.md` has the case-by-case verdicts and what is still different by design or unresolved; `docs/HARDWARE_DRIVER_AUDIT.md` maps every driver behaviour to the register writes and clock a real program would use. **2026-09-09 (second)**: the instrument gains a **Chord rate** of its own (§37), so a C can be slowed without slowing R. **2026-09-09 (third)**: the noise channel takes the table's transpose column (§45); a cell's TBL lasts until an instrument column (§46); a tick-stream jump no longer flushes and the step it lands in still fires (§47); the chain carries a **transpose** per row (§48); pulse instruments carry a **PU2 transpose** and F on PU2 sets it (§49). **2026-09-09 (fourth)**: LSDj 9's three-stage envelope measured (§51) and the shaped envelope given a **Start** level and a **Fade** stage to carry it |
| M5 — Voice plugin + link | **done** 2026-09-07 — region files, claims, one-block timing, push/pull; `chipboy_linktest` passes 16 checks |
| M6 — tracker, waves, frames, kits | **done** 2026-09-07 — tracker player on the host transport, record arm, kit import (resample + 4-bit dither), bank/song files. **2026-09-08**: grooves are sixteen tick counts, cells carry velocity, the Player flushes a channel with All notes off on stop/locate/source change, and the recorder follows §9.4; `chipboy_recordtest` (record/replay parity) and the tracker-shaped demo are done, next to the link test. Then: **steps per bar** is a number, 1–64, with a per-bar override (`Song::barSteps`) for any bar, tracked through a prefix table (`Song::barStartSteps`) so a locate lands on the right step; a cell's two commands fire **once**, at their step, instead of occupying a slot; per-channel **record arms** gate an armed channel's recording whatever its playback source; **song files** (`.cbsong`) and the plugin's **own transport** (`transportPlay`/`transportStop`/`setLoop`) round out the Standalone; `Demo/ChipBoy Demo.cbsong` is the recorded demo, checked byte for byte by `demo_song_matches`. **2026-09-08 (third)**: the Tracker tab becomes a **tab strip**, one tab per open song, each owning its own bank — only the active tab plays, records, and is shown in every other tab; undo steps carry the tab they belong to. **Hybrid** joins MIDI and Trkr as a third playback source: notes come from MIDI, everything else (instrument, table, commands) from the song's cells. The host's time signature no longer reaches the tracker — bar ticks are always the song's own beats per bar (§11 amended), in both tempo modes. `tools/demo/make_songs.py` adds six original songs under `Demo/songs` (a groove study, a meter study, a route theme, a platformer tune, a modern track, a wave-manipulation track), each checked by the CTest `demo_songs_load`; `Demo/ChipBoy Demo (hybrid).rpp` carries the plugin's saved state, checked by `demo_state_matches`, reproducing the recorded demo under Hybrid. **2026-09-09**: bars leave the model — a phrase carries its own **length** (`Phrase::steps`, 1–64) and a step is six ticks at the straight groove, always; the chain is rows, one after another per channel, and channels whose phrases differ in length drift apart by design (the chain highlights each channel's own playing row). `Song::stepsPerBar`, `barSteps` and `beatsPerBar` are gone, replaced by a per-channel prefix table (`Song::rowStartTicks`). The Tables tab lights a running table's row (`VoiceView::tableRow`/`tableRun`). The Waves tab gains a **synth** — a source and a chain of shapers morphed over a run of frames (`bank::Synth`, `bank::synthesize`) — core code, no JUCE, covered by `Tests/WaveSynthTests.cpp`. A `chipboy_fuzz` CTest plays random songs on the plugin's own transport looking for NaN/inf, a hang, or sound after all-notes-off; it caught a note held back by notes-on-tick surviving a panic, fixed in `Driver::allNotesOff` |
| M7 — interface | **done** 2026-09-07 — the window from the mockup: header, mixer with period-locked scopes, seven tabs, status bar, visualizer window, Voice window. **2026-09-08**: the Phrases tab gained a groove editor and a VEL column, the Instrument tab gained the pitch fields, and the window was resized to fit a 1080p screen with tempo moved to the header; then the Phrases tab became the **Tracker** tab with the transport, the song files and the chain rotated beside the lane, a **Grooves** tab took the groove editor, and the Instrument tab gained preset files. Then a quality-of-life round: every number typeable, the wheel scrolling only, command cells split into letter and values with right-click slot lists, and undo / redo over every hand edit. **2026-09-08 (third)**: the song tab strip sits above the lane; the header's tempo becomes a **readout** — host or song, in force — with the song's own master tempo typed in the Tracker head, now two rows of grouped tools (TRANSPORT · RECORD · SONG · FILE); the master strip gains **LCD Whine** as a third, independent switch and one **VOL** stepper for both sides; PLAYS gains **Hybrid**; and the channel scopes lock to an edge chosen by the waveform's shape rather than the last edge before the window, holding still on a wave channel under vibrato. **2026-09-09**: the Instrument tab becomes a **form** with the shaped envelope drawn from its fields; the tracker gains the note gestures, the phrase's **LEN** in the lane's head and in the chain, and a per-channel playing row; one selector convention (click types, right-click lists, double-click opens the item's tab) covers every slot field; the Tables tab lights a **running table's row**; the Waves tab gains the **synth**; and a command's values are two views of one byte, typed in an inline box. **2026-09-09 (second)**: **one editing grammar** for every value field (click selects, typing refuses past the limit and Backspace takes digits back, double-click is the box, right-click lists with *Open in its tab* first, Shift+arrows move by one and sixteen); the window **remembers** its tab, channel and selections across a close; the Waves tab lays frames eight to a row, gives each morph end its own **shape**, places the run by **From / To**, regroups the synth and gains a **Points** view. **2026-09-09 (third)**: a blank cell **fills itself** on Enter or a double click and a new note brings its instrument; every value cell takes a drag; grooves have **names**; the Waves tab **imports** a single-cycle file; the lane has channel dividers and beat bands; MIDI / Hybrid channels show their notes dimmed; a note-off sorts before a note-on at one sample (the FL Studio first-note report). **2026-09-09 (fourth)**: values **wrap** in the grids (D-UI-15), the Tracker head gains **Follow** (D-UI-16), the chain shows a **TSP** cell beside each phrase (D-UI-17), the Instrument tab a PU2 transpose row. **2026-09-09 (fifth)**: **Hex by default and counting like LSDj** — slots, rows and steps from 00, transposes as bytes (§52); scopes draw a mix-silenced channel as off (§53); the window is **1280** wide, the chain 264 with a gap after each TSP (D-UI-18–21). **2026-09-10**: **Import .sav…** brings LSDj songs in through a version-aware model (§54, D-UI-22) |
| M8 — CGB / RAW / hardware options | **done** 2026-09-07 — CGB chip variant, RAW bypass, headphone noise, LCD line, bass mod, de-click, soften master pops; 61 core tests. **2026-09-09**: the quiet-edge volume writes are gone (§26) — no program on the console can wait for a pulse's low half; the parameter stays as a no-op so the parameter table does not move. 158 core tests in all (up from 61) |

---

## Spec revisions

### 2026-09-11 — every stable LSDj release swept, and the two boundaries the format byte cannot see

`docs/LSDJ_VERSIONS.md` is new: all 31 stable releases in the user's archive plus 8.4.4, 9.2.L and
9.3.9, each probed on its own bootstrapped save, with what differs from 9.3.9, how the importer
remaps it, and what cannot be remapped.

**Found, and now in the model:** `R x y` retriggers every **y + 1** ticks before 9.2 and every y
after; `R x 0` fires once on formats 0–2 and on format 3 up to 4.7.3, every tick from 4.8.0 to
8.8.0, and once again from 8.8.1; `C` reaches the noise channel only from format 4 and `V` only
from format 22; `T` bytes 0–39 mean 256–295 BPM only from format 11; **no pitch change restarts
the noise channel before 9.2 at all**, so section 82's width-flip trigger had to become a third
state (`NoisePitch::Never`) rather than the default for every imported instrument; and a cell whose
instrument column is blank sounds on 3.6.8–3.9.2 but **nothing at all** from 4.0.4 — confirmed on
the user's own SUNRISE, which has two such cells and whose trigger counts match the ROM exactly
only once they are dropped.

**Two releases can write the same format byte and still read a song differently**, which the
importer could not express before. `lsdjModelForRomVersion` now walks a version-keyed table rather
than going through the format, so a supplied ROM settles it; the format's own default is named in
the document. The splits are format 2 at 4.0.4 and format 3 at 4.8.0.

**Confirmed unchanged across every release**, each of which was a candidate: the wave note table
(only 3.1.5 differs, and only past note `43` where its table wraps), the tempo law, `S` on the
pulses, `W`'s two-bit mask, the latent `LENGTH`, and a note-on sounding the plain note with the
table's transpose one update later.

**Flagged as unmappable** (section 5 of the document): the noise `S MODE` width clamp on formats
2–11, 5.7.8's slightly slower vibrato, `M` on 4.6.9 (which leaves the master at maximum on that
release alone), the noise table transpose before 4.0.4, `E` on the wave channel outside 0–3 on
6.4.5–8.5.1, and `R x F` on a version whose interval is `y + 1`.

**Also:** the PU2 transpose note is gone. Instrument byte 2 is a **signed byte of semitones**
measured across `01`, `02`, `0F`, `1F`, `FF` and `F1`, and ChipBoy's `pu2Transpose` is the same
signed byte, so `1F` really is +31 semitones and is carried exactly. The import now says nothing
unless the transpose puts a note the song actually plays past ChipBoy's top note. SUNRISE imports
with no notes at all.

### 2026-09-11 — the noise channel's `PITCH`, its latent `LENGTH`, and note numbers that read like LSDj's

Three findings on 9.3.9, all of them audible on an imported `SUNRISE` and all of them measured
rather than reasoned about. `docs/COMMANDS_AND_TEMPO.md` §85-§87 carry the design.

1. **Instrument byte 2 is the noise `PITCH`.** Found by sweeping every byte of a noise instrument
   one at a time and counting `NR44` triggers while a table walked the transpose: zero is `FREE`
   (the channel restarts only when a pitch change turns the 7-bit LFSR on, which is what §82
   measured) and anything else is `SAFE` (every pitch change restarts it). `SUNRISE`'s kick
   stores `04`, so it is not a flag LSDj keeps at 1. ChipBoy played every noise instrument as
   `FREE`.
2. **A pitch restart re-arms `NRx2` at the level the note has reached**, then writes
   `NR44 = BF`. ChipBoy triggered without the re-arm, so the chip reloaded its volume from the
   instrument's own level and the software envelope clawed back down — an imported noise part
   jumped back to full on every width change. This was the regression the previous round
   introduced along with §82, and it is what "the noise sounds way off" was.
3. **`LENGTH` is latent.** Byte 3 goes into `NR41` and the note-on writes `NR44 = 80` with bit 6
   clear, so the value does nothing until a pitch restart turns the counter on. Carried as
   `InstrumentCore::lengthLatent`; the pulse channel's own `LENGTH` is overwritten by LSDj an
   instruction later and needs nothing.

**Changed, and a departure worth stating:** §83 re-based a mapped noise instrument's note to
ChipBoy note 8 (`n + 7`). It is now LSDj's own note byte (entry 0 at note 1), because the Note
column is the one place a reader checks an import against the original and it was seven out.
LSDj's phrase screen counts a noise note **from zero** — measured by scripting the joypad to
LSDj's phrase screen and reading the LCD, with note bytes `01`..`10` in the phrase printing
`00`..`0F` — so ChipBoy's noise Note column now prints `note - 1` and the two read the same.
The cost: the table's first eleven entries would land in ChipBoy's **command octave** (notes
0-11, §13), so **a bank carrying LSDj's noise map has no command octave on the noise channel**.
Considered instead: keeping the offset at 8 and printing `note - 8` (the display would have to
know a bank property, and four entries still fall in the command octave), and leaving the
command octave in place (eleven of the table's 120 entries would never sound).

**Result.** Over the whole of `SUNRISE` — 113 seconds against the ROM — ChipBoy now writes
`NR41`, `NR43` and `NR44` byte for byte in the ROM's own order: 582, 1074 and 740 writes, no
difference in any of them. `NR42` still differs in 5836 of 12917 writes, all of them the ramp
steps of the software envelope, which ChipBoy quantises to the tick while LSDj steps it on its
own tempo-independent clock. That one is listed as open in the matrix §10 rather than fixed
here: it is not specific to the noise channel and changing it moves every instrument.

### 2026-09-10 — L3 allows deriving behaviour from the LSDj ROM

**Spec §3.3, rule L3** said "**No LSDj-derived content** in the repository or in any binary.
Formats may be implemented; content may not be bundled." Read strictly, "derived" barred
recording what the ROM's code does, which is the only way some commands can be settled at all.

**Now**: no LSDj *content* is bundled — no ROM, sample, wave or kit data, manual text or save —
and **behaviour may be derived from the ROM**. Disassembling it to settle exactly what a command
does is expected, and the address that answered a question is worth recording. What ChipBoy
ships is its own code producing the **same result**, not a transcription: no instruction listing
and no table of bytes lifted whole.

**Why.** The stage-1 verification of `docs/LSDJ_COMMAND_MATRIX.md` turned up eight wrong entries
on 9.3.9, and **four of them were invisible to any sweep of register values**: `S` looks like an
assignment until you run two of them (it accumulates); `M` looks like a plain `NR50` write until
a nibble goes above 7 (nibbles 8-15 are relative); `T` looks like BPM until the byte drops below
40 (0-39 mean 256-295); `Z` looks like "the last command executed" until two lanes disagree. Each
was settled in minutes by reading the handler, and each had survived a full measurement campaign
without it. The same reading also confirmed `E`'s disputed rate table outright — it is eight
bytes in the ROM — and closed the "is there a START blip" question by showing there is no
trigger between the key press and the song's first note.

**Considered**: keeping the strict reading and inferring everything from register sweeps. Rejected
— it is what produced the eight wrong entries, and two of them (`E`'s `y` nibble, `W`'s duty mask)
had the document telling a future session to "fix" driver code that was already correct.

**The owner's decision**, recorded here because it is theirs to make: this is a personal project
and any release is to be cleared with LSDj's author first.

### 2026-09-10 — a table's G times its own row; format 11's envelope is three stages

[`COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md) §57–§58, from the same archive of releases:

- **§57**: a `G` in a table row sets that row's own length, on every release traced from 3.5.1 to
  9.3.9. ChipBoy applied it a row late — the Driver kept the slot and waited for the Player's next
  block to hand the groove's ticks over — so an arpeggio's first step was a tick short and then a
  step out of phase for the whole note. The Driver reads the song's groove at the command now.
- **§58**: song format 11 carries three envelope stages in bytes 1, 9 and 10, the same three §51
  found in 8.8.6, but the chip ramps between them: a level every `period / 64` of a second, each
  stage handing over when the ramp reaches the next amplitude. Spec §51 had said only 8.8+ was
  staged and format 11 was the bare NRx2 byte; it is not. `LsdjModel::stagedEnvelope` became
  `EnvelopeLaw` (Chip / HardwareStages / SoftwareStages). Formats 0–7 really do ignore bytes 9
  and 10. Considered and rejected: leaving format 11 on the plain chip envelope, which had 28 of
  61 instruments in one of the user's own songs ramping to the rail instead of shaping.

### 2026-09-10 — every stable LSDj release measured; the older formats import; S on noise; project files

[`COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md) §55–§56, [`plan-lsdj-import.md`](docs/plan-lsdj-import.md) §1a, §3, §4b:

- **S on the noise channel** (§55) is a transpose in semitones through the map, adding up until the
  next note-on — LSDj 9's rule, measured on 9.3.9 and 9.4.2. Spec §34 had S inert on NOI. The palette
  lets `x` run 0–15; PU1's sweep reads it as before. The noise map continues below the keyboard for
  transposes (to −72) and the instrument's Shift is read from the instrument, not from the last pair
  written (a driver bug: a second NR43 write compounded the offset).
- **The models** grew rules per format (noise Shape/Raw/Map, S in nibbles or semitones, P/L/V in
  register units or semitones), measured on all 31 archived releases; the importer converts the older
  laws (§56): SHAPE noise, nibble S resolved to semitones, register P/L/V into Drum, any instrument on
  any channel as a variant, notes before an instrument column as instrument 00, tables by content, a
  noise slot's Shift chosen to reach LSDj's low clocks. Left alone with the reason in §56: P on noise,
  8.4.x's table row-0 timing, 5.x–6.x drum tables.
- **Project files** (`.lsdprj`, `.lsdsng`) import beside saves, several at once (D-UI-22).
- `chipboy_recordtest --trace-song` writes a song's register trace for comparison with an LSDj trace.

### 2026-09-10 — kit instruments imported from the ROM beside the save

The kit instrument's byte layout was measured on the user's 9.2.L ROM by copying a real kit instrument into probe songs and matching the streamed wave RAM against the ROM's kit banks ([`plan-lsdj-import.md`](docs/plan-lsdj-import.md) §4a): the note's high digit picks a sample of the kit in byte 2, the low digit one of the kit in byte 9, bytes 3 and 11 cut them to 32-sample frames, and byte 8 is a signed offset on the period 1865. The importer reads the `*.gb` beside the `.sav` (the one that reads the song's format, else the newest), turns each kit instrument into a ChipBoy kit of the samples its notes use, and rewrites the cells' notes to those samples. A note that plays both kits is summed and clipped and noted — LSDj's DIST modes did not match any simple combination in the probes. Offsets, loop and half-speed flags are noted. Nothing of the ROM enters the repository; the samples land in the user's song file.

### 2026-09-10 — the LSDj models keyed by format, three more ROMs measured

Three ROMs the user supplied (8.4.0, 8.8.6, 9.2.J) were booted with `lsdjref_trace --init-sav` and probed with §45–§51's cases in their own formats ([`plan-lsdj-import.md`](docs/plan-lsdj-import.md) §3). The importer's models are keyed by **song format** with the LSDj versions as labels: format 22 (9.2.J, 9.3.9) identical on every table traced; format 15 (8.8.6) with the three-stage envelope and a raw noise column; format 11 (8.4.0) with the hardware envelope and an octave-only noise map; formats 0–10 assumed from the version-0 measurements. The earlier "legacy for 0–19" entry was wrong for 11–19 — every ROM from 8.4.0 carries the letter table with `B` — and is replaced. The noise maps carry a measured note range instead of a sentinel value, since `FF` is a real value on 8.x.

### 2026-09-10 — the LSDj importer, and a scope that lets go of a dead waveform

- **Import .sav…** ([`COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md) §54, [`UI_DESIGN.md`](docs/UI_DESIGN.md) D-UI-22, [`plan-lsdj-import.md`](docs/plan-lsdj-import.md)): the converter that recreated a 9.3.9 song is in the plugin. `Source/core/Import/` reads the save (the file table, LSDj's block code with its two escapes and two default codes, the working song), chooses a **model** by each song's format version — 9.3.9 measured, a legacy model for the formats before it, the folder's ROM or the newest model for an unknown format, a dropdown to override — and reads the song into a bank and a song by §45–§52's rules. The spec had no import; nothing of LSDj's stays in the file. Kits are skipped with a note (their samples are in the ROM). The model is the seam for the ROMs to come: each version's tables in one entry, the parser shared.
- **Scopes repaint while the last waveform leaves the window** (§53 amended). A scope repainted only when a new sample or a state change arrived, so a channel that fell silent kept the frame that still showed the tail of its waveform — the visualizer's held shapes. While the newest sample is within two windows of the present, every block is a repaint.

### 2026-09-09 — LSDj's three-stage envelope, Hex that counts like LSDj, scopes that show the mix, a wider window

From the same LSDj 9.3.9 recreation ([`COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md) §51–§53, [`UI_DESIGN.md`](docs/UI_DESIGN.md) D-UI-18–21):

- **The shaped envelope gains a Start level and a Fade stage** (§51). LSDj 9's ENV was measured on the ROM with a format-22 probe: three stages from the note-on (`a1 → a2 → a3 → 0`, each at a speed; speed 0 holds), one level per `1 2 3 4 6 8 11 15 20 27 36 48 64 86 115` pitch-clock periods for speeds 1–F, every level a zombie step. The earlier speed table in §7 had been measured on the harness's version-0 saves, which LSDj plays with the hardware envelope, and had mapped the snare's speed 5 (16.7 ms a level) to the chip's rate 5 (78 ms). Spec §27's Attack → Peak → Decay → Sustain → Release could not hold three stages, so the attack now begins at `Envelope::start` and a third stage, `fadeTicks` to `fadeTo`, follows the sustain and holds. Written to the file only when set. Chip stays the hardware envelope. Stage times are ticks at the song tempo, the trade §27 made for a replayable list.
- **Hex counts like LSDj** (§52, D-UI-20): in Hex, slots show and type from 00 (slot 1 is `00`), rows and steps from 00, and a transpose is its two's-complement byte (`E0` is −32) in the table column, the chain's TSP and the PU2 transpose. Decimal stays 1-based and signed. **Hex is the default** (the parameter's default moved; `Demo/chipboy_demo_hybrid.state` and `Demo/PARAMETERS.md` regenerated). The table's transpose column takes the whole byte, −128..127, where the spec had ±60. The song tempo stays decimal in both displays, as LSDj shows it.
- **Scopes show what is audible** (§53, D-UI-21): a channel whose two NR51 bits are clear draws the off baseline in the mixer and the visualizer — the LSDj song silences its wave channel that way between notes, and the DAC's staircase kept drawing. The analog trace is clamped to the sixteen levels.
- **The chain and the window** (D-UI-18, D-UI-19): an 8 px gap after every TSP cell so the pair reads as one channel (the chain is 264 px), and the window is **1280** wide instead of 1180 so the lane keeps its 980 px and its channel heads stop eliding. The Instrument tab's envelope graph is 20 px shorter so the Shaped form, three rows taller, still fits the pane.

### 2026-09-09 — noise tables, the TBL span, loops that keep their first note, the chain's transpose, wrapping values, Follow

A round driven by recreating an LSDj 9.3.9 song ([`COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md) §45–§50, [`UI_DESIGN.md`](docs/UI_DESIGN.md) D-UI-15–17):

- **The noise channel takes the table's transpose column** (§45). Spec §9.4 gave noise a map and §7 said "noise has no pitch effects"; the transpose column had gone with the bends, so a snare table did nothing. The column now moves the note before the map, on the note-on and on every tick that changes it. P, V and L stay inert on noise. LSDj's 7-bit region and its retrigger on a row that lands there are not modelled — ChipBoy's map has no 7-bit region.
- **A TBL column lasts until a cell names an instrument** (§46). The override was never cleared, so one TBL stuck to every later note on the channel. An instrument column with a blank TBL puts it back to the Table parameter's value, which is also LSDj's rule for `A`.
- **A jump in the tick stream no longer sends All Notes Off** (§47; spec §9.1 had the Player flush on stop, locate and source change). Stop, pause and the lane leaving still flush; a locate or a loop wrap leaves the note ringing until the channel's next cell, as a tracker does. And the first tick after a jump fires the latest step at or before it, because a host that wraps mid-block hands over a position a fraction of a tick past the loop start and the step on the boundary never matched exactly — the dropped first note of a DAW loop.
- **The chain carries a transpose per row and channel** (§48), `Song::chainTranspose`, `NoteEvent::transpose`, `"chainTransposes"` in the song file (absent reads as 0; written only when one is set, so every existing file is byte-identical). The instrument's Transpose flag gates it, as it gates the table's column and as LSDj's TRANSPOSE does. The chain widens from 164 to 236 px; the lane's columns are proportional and give up the difference.
- **An instrument's PU2 transpose, and F on PU2** (§49): `InstrumentCore::pu2Transpose`, semitones added on the second pulse only — LSDj's PU2 TSP — and `F x` on PU2 sets it for the note in progress, two's complement; the revert form puts the instrument's back. F on PU1 and NOI stays inert, on WAV it stays the frame. Written to the file only when set.
- **P's argument is normalised as it is read** (§50): a negative `a` folds into its byte, so a file that spelt the signed value plays the bend instead of clamping to P00.
- **Values wrap in the grids** (D-UI-15): Shift+arrows, +/− and a drag on a lane, table or chain cell go from the top back to the bottom and from 00 up to the top. Notes still stop at their range; typing never wraps; the Instrument tab's steppers keep their ends.
- **Follow** (D-UI-16): a toggle in the Tracker head's TRANSPORT group, on by default, kept in `ui_view`. Off, the row on show and the lane's scroll stay put while the song plays.

Considered and not done: an LSDj-shaped noise map as an instrument option (would let an import land every transposed noise row on LSDj's value; ChipBoy's own map stays the default), a per-row fade speed in the table's volume column (LSDj's ENV low digit), and removing the `A` letter now that the TBL column covers its use in a cell — `A` inside a *table* still switches tables and `A 0` stops one, which the column cannot say, so the letter stays until that is decided.

### 2026-09-09 — filling cells, named grooves, wave import, the lane's look, the first note of a loop

[`docs/COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md) §38–§44, amending §30, §35 and
[`docs/UI_DESIGN.md`](docs/UI_DESIGN.md) §7. The second pass of the seventh round.

**Changed:**

- **A blank cell fills itself (§38).** Enter or a double click on a blank cell puts the
  column's most recent value in it — per channel in the lane, per table in a table —
  else the nearest non-blank cell above, else a default (the octave's C, velocity 100,
  instrument 1, volume 15, transpose 0; a table or a command with nothing to copy stays
  blank). **A note typed into a blank cell brings the channel's most recent instrument
  with it**; a note off does not, and a note moved afterwards never puts a deleted
  instrument back. **Every value cell takes a vertical drag** as a note did: one unit per
  six pixels, sixteen with Shift, one undo. *As built:* `PhraseGrid::Impl::Recent` per
  channel and `TableGrid::Impl::Recent`, refreshed from every cell written.
- **Grooves have names (§39).** `tracker::Groove::name`, fifteen characters, typed in the
  Grooves list as every list renames; shown as *slot · name* with the ticks beside it in
  the list, the lane's groove chip menu and its tooltip. `grooveNames` in the song JSON;
  a file without it reads blank names and the format stays 7.
- **Import… in the Waves tab (§40)**: an audio file read as one cycle — mono, mean
  removed, box-filtered onto 32 samples, peak-normalised, rounded to sixteen levels, no
  dither — into the frame on show, one undo. `bank::frameFromCycle` in core, tested (a
  sine of any length is `frameSine()` within a level); `plugin::importWaveCycle` beside
  the kit importer.
- **The lane (§41)**: 2 px dividers between the channels, and row bands — every fourth
  row tinted for the beat at the straight groove, the rows between alternating a fainter
  tint.
- **MIDI and Hybrid channels show their notes dimmed (§42)** in a washed column and refuse
  edits, where they showed only the host's notes; a blank cell still shows the host's note
  at that step, fainter.
- **The first note of a loop (§43).** From FL Studio: looping a bar, the note on beat one
  was often dropped. The plugin kept the host's order at one sample, and FL delivers the
  off of the note ending at the loop's end after the on of the note starting at its start.
  A MIDI **note-off now sorts before a note-on** at the same sample (velocity 0 counts as
  an off): flush, cells, offs, ons. Reasoned, not reproduced here.
- **LSDj's defaults measured on a 9.3.9 ROM (§44).** A table row is **one tick** — the
  transpose column changes the period every tick, so §17's "two ticks a row" was the
  envelope nibble and is withdrawn — and a chord at CMD/RATE 0 steps once a tick. Both are
  ChipBoy's defaults already; `f_table_speed` joins the harness's cases.

**Tests:** `a cycle of audio becomes a frame` (`Tests/WaveSynthTests.cpp`); 175 core tests.

### 2026-09-09 — one editing grammar, the remembered window, the Waves tab's second pass, the chord's own rate

[`docs/COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md) §35–§37, amending §30, §33
and [`docs/UI_DESIGN.md`](docs/UI_DESIGN.md) §2.1 (D-UI-12 to D-UI-14). A round of use
of the sixth round's window: what was inconsistent between the fields, what the window
forgot, and what the Waves tab made hard to read.

**Changed:**

- **One grammar for every value field (§35).** A **click selects** and nothing else;
  **typing** edits in place — the first digit replaces, the rest append, a digit that
  would pass the field's limit is **refused** (`5`, `56`, and a third digit is not taken
  in a 0–127 field) and **Backspace** takes the last digit back before it blanks the
  cell; a **double click** (or Enter) opens the inline box; a **right click** lists the
  choices; **Shift+←/→** moves by one and **Shift+↑/↓** by sixteen. It covers the lane,
  the table grid (whose volume and transpose gained the box), the chain (which gained
  the box and Enter), the lane head's LEN and groove chips (which now take the cursor
  and type in place, where a click used to open the box straight away) and every
  stepper (whose click used to open the box, and which now types hex digits in Hex).
  *Considered:* keeping the click-opens-box on steppers and chips — rejected, since the
  user's complaint was exactly that the fields differed. The old typed entry, where a
  digit that overflowed silently started a new value, is gone: `typeDigit` refuses and
  `popDigit` is Backspace's.
- **A slot field's double click still opens the item's tab** (D-UI-9), and every slot
  list — the lane's INS and TBL, the groove chip, the strips' instrument and table
  steppers, the Instrument tab's Table, Wave and Kit — gained *Open … in its tab* as its
  first entry as well. An empty slot field opens the box. *Considered and built first:*
  the box on slot fields too, with the tab only in the menu — reverted the same day, since
  a slot is typed at the selected field already and the tab was the gesture asked for.
- **The Points view** of the wave grid fills each sample's grid box rather than drawing
  a dot, as LSDj's wave screen does.
- **Shift with the arrows on a note is swapped**: ←/→ a semitone, ↑/↓ an octave, so the
  vertical pair is the big step on a note as on every other field. The drag is unchanged.
- **The window remembers where it was (§35, D-UI-13).** Closing and reopening the editor
  used to show the Instrument tab on slot 1. The tab, the channel, each panel's slot,
  the Waves tab's frame and morph end and the Tracker's row are kept as `ui_view` in the
  plugin state (`EditorPanel::saveView` / `restoreView`), written on every tab change and
  on close, read on open — so a reopened project comes back where it was left too.
- **The Waves tab (§36).** Thumbnails **eight to a row**, stretched to the strip and
  numbered. The synth's *Source* is **Shape**, and **each end of the morph has its own**
  (`SynthState::source`; `Synth::source` is gone): the render makes both shapes and
  crossfades them by the morph position before the chain, byte-identical to before
  where the two agree; a file with the old single `source` reads it into both ends. The
  run is placed by **From** and **To** frame numbers (`Synth::first` and `frames`;
  `bank::synthWriteRun` writes it into the slot, growing the wave to reach To and
  leaving the frames outside the run) instead of a count that replaced the whole wave.
  The section is regrouped — **Shape**, **Chain** (a dim line under each chosen shaper
  says what its amount does; resonance shown only on the filters), **Run**, **Preview**
  — and the shape / interpolate tools moved to a **row under the grid** with the new
  **Bars / Points view** switch (each sample a filled grid box), so Interpolate no
  longer collided with the shape selector. The grid lights the pointer's column and row
  and reads the sample and level in a corner. *Considered and kept out:* a per-frame list of shapes — two ends are what
  a morph has.
- **The chord's own rate (§37).** `Instrument::chordRate`, 0–15, default 0: a C steps
  every *chord rate + 1* ticks and the **command rate** keeps R and the Tick-speed P and
  V. LSDj has the one rate for both; the chord at one step a tick is the LSDj sound and
  was too fast for the rest, and slowing it slowed the retrigger. A file without
  `chordRate` takes its command rate, so every existing song plays as it did; the
  parity harness sets both from LSDj's rate. The demo song and the hybrid project's
  state are regenerated for the new property.

**Tests:** `each end of the morph has its own shape`, `the run is written from its first
frame and the rest of the wave stays` (`Tests/WaveSynthTests.cpp`); the chord test in
`Tests/DriverTests.cpp` gained the two rates' independence.

### 2026-09-09 — the driver made LSDj-exact

[`docs/LSDJ_PARITY.md`](docs/LSDJ_PARITY.md) measured eighteen differences
between what an LSDj 9.2.J ROM writes to the APU and what ChipBoy's driver
writes. This round closes them: every pitch law, every level write and three
command encodings are now the ROM's own, measured, and
[`docs/COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md) §7 is rewritten from
the numbers rather than from the manual. The interface side of §34 — how a
command's two nibbles or one byte are shown and typed — is the stage beside this
one; what is here is the driver's reading of them.

**Changed:**

- **The pitch clock is 11712 cycles — 358.12 Hz — and global.** LSDj sets the
  Game Boy's timer once at boot and never moves it, so ChipBoy uses the same
  reload and, like a timer interrupt, **one clock for the whole driver that
  free-runs**: a note-on no longer restarts it. The consequence is that where a
  note falls inside the period is where the player pressed play — which is true
  of a ROM too, and is why the parity harness's timing tolerance is a whole
  period. Considered keeping a per-voice clock restarted at the note, which made
  a note's vibrato deterministic; rejected, because the first period write after
  a note-on then lands 11712 cycles late where the ROM's lands wherever the
  timer happens to be, and no case could be compared write for write.
- **The note table interpolates in period units.** `Driver::periodForNote` is
  one entry a semitone, as before, but a fraction between two entries is now
  interpolated in the *period* rather than in the frequency. That is measurable:
  a vibrato half a semitone below C-5 lands on 1791 where the exponential curve
  gives 1790, and the ROM writes 1791. Whole notes are unchanged.
- **V is a symmetric triangle of 64/(x + 1) updates.** The phase is a six-bit
  counter stepping by `x + 1`, so **x = 0 is the slowest and not "off"**, and it
  steps *after* the write, so a note's first update writes the note itself. The
  depth is LSDj's own table, confirmed against the ROM for all sixteen values
  (⅛ to 8 semitones), and the swing is **either side of the note** — the
  direction bit only chooses which half comes first. In **Tick** mode the cycle
  is a measured table of tick counts (96, 72, 64, 48, 36, 32, 24, 18, 16, 12, 9,
  8, 6, 4½, 4, 3), not 64/(x + 1) ticks, and in **Drum** the same triangle moves
  the period register at about 19.1 units a semitone. §7's old "720/x" and its
  "note to note − depth" are both gone. Saw and square keep ChipBoy's one-sided
  shapes: the ROM's own produce five to nine period writes a second against the
  triangle's 360, which is neither, so there is nothing yet to copy.
- **L takes x + 1 updates and is linear in semitones.** The step is
  `(target − source) / (x + 1)` in 1/256 of a semitone truncated toward zero, so
  a little is usually left after the last step and one more update lands the
  pitch exactly on the note — which is why the ROM's seventeenth step of `L 10`
  is 1797 and its eighteenth is 1798. `L 00` is one step, the whole distance. A
  slide's note-on **triggers at the pitch the channel was at**, which is what
  makes it a portamento.
- **P's step comes from the measured table.** All 127 values were swept
  (`d_bend_scale_all`, `d_bend_scale_up`); the rate closes to
  `S(|x|)/256` of a semitone an update with
  `S(m) = sum(ceil(j/4), j = 1..m) = (q + 1)(2q + r)`, `q = m/4`, `r = m mod 4`,
  which fits every measured value to about one part in a hundred — stated as a
  fit, because it is one. Fast and Tick bend the note, Tick at four of the pitch
  clock's steps a tick; **Step** is one immediate offset of x/32 of a semitone,
  applied at the first update rather than in the note's own writes; **Drum**
  bends the period register and **wraps at 2048**, which is what a P kick falling
  off the bottom really does. The argument is **two's complement** (§34).
- **Levels are LSDj's own bytes.** The breadth-first search that found the
  shortest zombie sequence is gone: the ROM's two primitives are `09 11 18` for
  one step down and `08` for one step up, and the driver writes exactly those,
  repeated to the target, whichever way round is fewer writes. The spacing is
  the ROM's too — sixteen cycles inside a down-triple, a hundred and twelve
  between triples, sixty-eight between ups. The search was right by construction
  and wrong by observation: it carried the target level in NRx2's high nibble,
  which the ROM never does, so no level change could be compared byte for byte.
- **The chip's envelope never runs.** Every NRx2 goes out with the low nibble
  forced to **8** — amplitude, direction up, period zero — whatever the
  instrument's ENV byte says (`F0` → `F8`, `A3` → `A8`, `09` → `08`), and the
  driver steps the level itself off the pitch clock on the measured table: 6,
  11, 15, 20, 27, 36, 36 periods for rates 1–7. The direction bit is not
  cosmetic — it is the state every later zombie write starts from, and it leaves
  the DAC on at level zero. A note whose envelope reaches silence ends there.
- **E never triggers** (measured: `E 8 0` on a channel at 15 is seven
  down-triples and nothing else), and **K is a zombie ramp to zero** with the
  DAC left on rather than a DAC clear. A panic still clears the DAC.
- **R is `y × (rate + 1) + 1` ticks**, so `y` = 0 is every tick and not "once"; a
  tick-driven retrigger writes **the whole note-on sequence again**, not just the
  trigger; `x` = 8 is LSDj's resync and runs the retrigger on the **pitch clock**,
  writing only the level and the trigger; and `x` is a **signed nibble** of
  volume change, 9–15 being down by 16 − x (measured: `R A` steps down by six,
  not by two).
- **NRx4 goes out with every NRx3.** There is no trigger bit in it, so it changes
  nothing but the register log — and the log is what a parity harness can line
  up. The pitch update writes the period **whenever something is moving it**, and
  once after a note-on whether anything is moving or not, which is exactly what
  the ROM does; a chord step or a table's transpose column writes it at its tick.
- **C's root plays on the note's own tick** and the chord steps from the one
  after it (measured: `C 3 7` wrote 1798, then 1837, then 1881).
- **§34's encodings, driver side.** P is two's complement; S's `y` is NR10's low
  nibble (0–7 up, 8–15 down) instead of a flag on `x`; T's argument is LSDj's
  byte, `28`–`FF` for 40–255 BPM and `00`–`27` for 256–295, with the tempo
  derived (`bank::tempoBpmOfByte`); H in a table is `times, row` with 0 times
  meaning for ever; and Z's and M's arguments clamp to nibbles.
- **Song format 7.** The song JSON, the bank JSON and a `.cbsong` all carry
  version 7, and **a file written before it is converted as it is read**: P's
  `x − 128` becomes two's complement, S's direction moves from bit 7 of `x` into
  `y`, and H's step becomes `0, step − 1` — the same instruction in the new
  encoding. V, L, R and T keep their numbers, because what changed there is the
  law the driver plays them by and no conversion can put a song's musical intent
  back; this entry is the notice. The factory bank's "Drum drop" table and the
  demo generators were re-expressed by hand for that reason.
- **The demo and the six songs, re-expressed.** `tools/demo/make_demo.py`'s P
  lane is −14 (32/256 of a semitone an update) where it was −2 period units, and
  its L stays 30 because x + 1 updates is the same 87 ms.
  `tools/demo/make_songs.py` re-expresses `neon-grid`'s kick drop as `P -38`
  (about fifteen period units an update in Drum), `wave-study`'s wobbles as
  `P ±27` and `P ±30`, and every V speed as 0 — the slowest the new law has,
  5.6 Hz, which is the rate the old speeds 6 to 12 asked for. `Demo/ChipBoy
  Demo.cbsong`, `Demo/chipboy_demo_hybrid.state`, the three Reaper projects, the
  automation JSON and three of the six songs are regenerated; the record test's
  four passes agree register for register again, `demo_song_matches`,
  `demo_state_matches`, the six `demo_songs_load` and `chipboy_fuzz` pass, and
  the paramdump table is identical (76 parameters).
- **The harness.** `tools/lsdjref/cases.spec` gains six finer cases — P swept
  over every value from −127 to +64, V at all sixteen depths and all sixteen
  speeds in both clocks, and a table whose rows hold the same amplitudes behind
  different envelope nibbles — and `lsdjref_compare` was rewritten to compare
  what a driver decides rather than what a tempo counter does: it repeats each
  chain for as long as the capture runs (as LSDj loops a song), lines the two
  streams up **at every note-on**, and allows two pitch-clock periods of timing
  because the clock's phase against a note is where the player pressed play and
  because one side occasionally fits an update in that the other does not. Its
  verdicts are *identical*, *same values, timing within tolerance*, *same values,
  timing outside tolerance* and *different values*.
- **Tables, not laws.** Where a formula is still uncertain the driver carries the
  measured numbers and says so: P's step table (the closed form in LSDJ_PARITY §5
  is a fit to it, good to a part in a hundred), V's sixteen depths, V's Tick-mode
  tick counts, Drum's 19.1 period units a semitone, and the software envelope's
  six rates. LSDJ_PARITY's verdict table marks each of them "table, not law".
- **What the comparison says now** is `docs/LSDJ_PARITY.md` §16, case by case on
  DMG and CGB, and §17 is what is still different and why: ChipBoy's table rows
  are one tick where LSDj's are two (a tracker law, not a driver one, and ChipBoy's
  tables have a groove of their own), its noise letters and its absolute F and
  64-slot A are its own by §2, and four things are measured but not resolved —
  the last one per cent of P's rate, the rounding of V in Drum, the odd speeds of
  V in Tick, and why LSDj's resync retrigger stops after 38 of them.
- **Tests.** 160 core tests (the pitch section rewritten around the measured
  laws: the note table's period-unit interpolation, V's triangle and depth table
  read off the write log, V's Tick table, L's five equal steps and its `L 00`,
  P's step table and its four domains with Drum's wrap, the pitch clock's 11712
  cycles, E never triggering, and the zombie sequence asserted byte for byte and
  cycle for cycle). `Driver::bendStepFor` and `Driver::periodOfSemitone` are
  public so a test can pin a table without a rig.

### 2026-09-09 — the Instrument tab, tracker editing, the table playhead, the wave synth, command views (interface)

The fourth addendum's interface side ([`docs/COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md)
§29–§30, §32–§34), on top of the engine round above. The window catches up with the model
the engine now has: bars are gone from the head and the chain, a phrase's LEN is typed
where the phrase is, the Instrument tab is a form with the shaped envelope drawn from its
own fields, a running table shows where it is, waves can be generated, and a command's
values are finally pleasant to type.

**Changed:**

- **The Instrument tab is a form (§29).** Labels down one column, controls down the
  other, a thin caption over each group, **no card chrome**: Sound over Pitch &
  modulation on the left, Envelope over Table & note behaviour on the right. The envelope
  is **a picture over its fields** — the chip's NRx2 ramp over a second, or the shaped
  ADSR rendered through `bank::envSegmentLevel`, so what is drawn is the level list the
  driver will really write — with a **Chip / Shaped** switch, Attack, Decay and Release
  in ticks each beside its **curve** (Lin / Exp / Log), and Peak and Sustain 0–15, or 0–3
  on wave and kit, which have only the four NR32 levels. Hints became tooltips; the panel
  shows labels and values, and the values that mean something else say so on the control
  ("4 Hz", "3/4 st", "every 3", "15.6 ms"). **The knobs went**, and that is what buys the
  room: a stepper with a readout is 24 px where a dial with its caption was 70. The
  tallest type is a Shaped one at **468 px** of the pane's 530 — Pulse 468, Wave 468,
  Kit 468, Noise 468, against 474 for the old four cards, and every type now sits at the
  same height because the picture is the tall thing rather than the fields. `FormRow` and
  `FormGroup` moved into `PanelCommon` so the Waves tab could use them.
- **Tracker editing (§30).** A note takes **Shift+↑/↓** for a semitone and **Shift+←/→**
  for an octave, a **vertical drag** for a semitone every six pixels (octaves with
  Shift, the whole drag one undo), and a **double click** that types it with
  auto-correction: `a1`, `A 1`, `a#1` and `bb2` are `A-1`, `A-1`, `A#1` and `A#2`, `off`
  or `-` is a note off, an empty box blanks the cell, Escape cancels and anything else is
  refused. The lane's head carries the phrase's **LEN**, typed 1–64, beside the groove
  chip; the *PLAYS* caption went to make room for it and lives in the switch's tooltip.
  The chain column's fifth cell is the row's **LEN** where the bar override was, and
  **each channel's own playing row is lit in its own column**, because the channels keep
  their own time. The head's SONG group loses *Beats* and *Steps / bar*, which left the
  model with the bars, and the readout is the song's time beside the selected channel's
  own row·step. Loop is `setLoopRows(0, -1)`: row 1 to the last row of the longest chain.
- **One convention for every slot field (§30).** A **click** selects it and types, a
  **right click** lists the slots by *slot · name*, a **double click** opens that item's
  own tab with it selected. It covers the grid's `ins` and `tbl`, the groove chip, the
  strips' instrument and table steppers, and the Instrument tab's Table, Wave and Kit
  fields; `EditorPanel::onOpenSlot` and `selectSlot` carry it to the panel that owns the
  item. The cost is that a slot stepper's click **focuses** the readout instead of
  opening its inline box — the box is Enter's — so the double click can be seen at all;
  typing digits straight at it is unchanged. Considered a delayed open (a double-click
  timer) and rejected: it makes every click on every slot field feel slow to save one
  keystroke.
- **The strips lost their running-state line (§30)** — it repeated the register line
  above it and the tracker beside it — and their instrument name is now **the instrument
  the driver last loaded** (`VoiceView::instrument`), so it follows a cell's `ins`
  column, an `A`, a keyswitch or a Hybrid channel, prefixing the slot number when that
  differs from the stepper. A strip is 332 px instead of 350 and the editor pane took
  the 18.
- **A running table shows where it is (§32).** The Tables tab lights the row the table on
  view is on, following the channel whose run started **last** through the serial the
  driver publishes (`ScopeBuffers::tableRun`, compared as a signed difference so the
  serial can wrap), and the line under the name says which channel it is; nothing is lit
  when no channel runs it. It follows the panel's own 30 Hz timer.
- **The wave synth (§33).** `bank::Synth` in core — a **source** (sine, triangle, saw,
  square with a width, eight additive partials, noise, the drawn wave), a chain of four
  **shapers** with their amounts (low-, high-, band- and all-pass with resonance, drive
  as clip, fold and wrap, rotate, shift, invert, reverse, smooth, bit-crush, quantise,
  normalise), a **start and an end state** and the frames to morph between them — with
  `bank::synthesize` rendering the run. The filters are **per-harmonic gains of the
  32-point transform of the cycle** rather than a running filter: a wave is 32 samples
  and therefore sixteen harmonics, so the transform is exact, perfectly cyclic and the
  same bytes on every platform, where a running filter's output would depend on where its
  state started. An amount of 0 is a no-op on every shaper and the sign is the direction
  where a shaper has one; a synthesised sine, triangle, saw and square are the bank's own
  generators byte for byte. The Waves tab edits it beside previews of both ends and of
  the whole run, with **Generate** writing the frames into the slot as one undo; the
  parameters live in `Wave::synth` and travel in the bank's JSON, so a run can be made
  again after an edit and the exporter still only ships frames.
  `Tests/WaveSynthTests.cpp` covers each source, each shaper on a known input, the order
  of the chain, the morph's ends being the start and end states exactly, and determinism
  over every source crossed with every shaper.
- **Command arguments are two views of one byte (§34).** `commandInfo` gains a **shape**
  — Nibbles, Byte or Small — and the ranges that go with it: C, R, S, Z and M are two
  nibbles, H in a table is LSDj's `times, row` (0 times = forever), T reaches 295 BPM
  with the byte wrapping through `00`–`27`, and P is stored two's complement. Decimal
  shows `x,y`, Hex shows the byte a playback ROM will carry, and `commandByte` /
  `setCommandByte` are the one encoding both read. **Typing follows the view**: a click on
  a value opens an inline box holding it, Enter commits, Tab moves to the next argument,
  Escape cancels, and anything outside the letter's range is refused; in Hex the box is
  the whole byte and two digits set it; P's box takes `-73`. The strips' `CommandSlot`
  and the Voice window show the same two views — two steppers in Decimal, one byte in
  Hex. Digits typed straight at a cell still work, with a comma to move on.
  `TypedEntry` and its parsing moved to `plugin/ui/InlineEntry.h`, with a Tab hook, so
  the grids and the widgets share one box.
- **The spare words are gone (§30).** The Hardware rows are a label and their measured
  fact with the explanation in the tooltip, and the tab stopped scrolling; the Grooves
  help column is three lines; the paragraph tooltips on the strips, the master volume,
  the wave tools and the groove stepper are one line each.
- **Screenshots and the tool.** `chipboy_uishot` gains `--shaped`, which gives the first
  instrument a shaped envelope so the Instrument tab is shot at its tallest, and it now
  selects a table a channel is really running before shooting the Tables tab. The docs
  carry `main-instrument.png`, `main-instrument-shaped.png`, `main-tables.png`,
  `main-waves.png`, `main-tracker.png`, `main-grooves.png`, `main-hardware.png` and
  `voice.png`, all regenerated; no tab scrolls at 1180 × 1020 but the bank lists, which
  are meant to.

**What the driver still owes §34.** The window now stores and shows three of the
letters the way §34 defines them, and the driver reads two of them the old way; these are
the three lines the engine side has to move, and until it does, those letters mean what
the driver says and not what the window shows:

- **P** is stored two's complement (`FE` = −2); `Driver.cpp` still reads it as offset
  binary (`c.a - 128`).
- **S**'s `y` is NR10's low nibble, direction in bit 3; the driver still takes the
  direction from `c.a & 128` and the shift from `c.b & 7`.
- **H** in a table is `times, row`; the driver still hops to `c.a` and counts nothing.
  A fresh H is `1, 1` so that it means the same under both readings -- once, to row 1
  under §34, and a hop to step 1 under the driver as it stands.
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

### 2026-09-10 — a table's second command column, its groove, and the song transpose (spec §62, §63)

**Changed:** three things the LSDj import got wrong on the user's format-11 save.

1. An `H` in a table's **second command column** is dropped at import. LSDj gives each
   command column its own row pointer (its changelog says so at v1.3.0B), so such a hop
   loops that column alone; ChipBoy has one pointer, and honouring the hop truncated every
   arpeggio built this way.
2. A table's `G` is pointed at a **one step groove** when the save is older than LSDj 9,
   because before 9 every row of the run takes the groove's *first* step rather than
   walking it. The slot comes from the grooves the song never names.
3. The song's own transpose is no longer written into every chain row. `songToVar` used
   `transposeAt()`, which already adds it, so a save-and-reload doubled it; the writer and
   the chain grid now read the row's own value through the new `Song::rowTranspose()`.

**Why:** the user's first saved song imported with its arpeggios stuck on two notes, its
tables swinging where LSDj held one length, and, once §61 added the song transpose, every
note a semitone sharp after a round trip.

**Considered:** giving ChipBoy's tables two row pointers, which would match LSDj exactly --
rejected because it changes the table for every ChipBoy player to serve the import, and the
rows a second column replays are nearly always ones that only set a value. Rewriting the
groove in place instead of allocating a slot -- rejected because the same groove is usually
on the timeline too, where both its steps matter. Keeping §57's walking rule everywhere --
rejected: it is measurably LSDj 9's rule alone.

**Not measured:** formats 0 to 7 for the groove rule. No save of that era in hand plays
under the harness, and the generated probe saves proved unreliable for table commands (they
report a hop from the second column that a real save does not, and drop a table's `G`).
Those formats take the pre-9 rule, which both formats either side of them follow.

### 2026-09-10 — a table's three lanes, and the wave instrument's frame run (spec §64, §65)

**Changed:** ChipBoy's tables and wave instruments gain what LSDj has, rather than the import
flattening it.

1. **A table's three lanes run on their own pointers.** VOL with its new LEN is one, TSP with
   CMD 1 the second, CMD 2 the third; an `H` hops only its own column. §62 had dropped the
   second column's hop because ChipBoy had one pointer -- the importer carries it now.
2. **The table's LEN column**: `TableStep::volTicks` (0 = as long as the table's row, else
   ticks) and `volHop` (the row the volume lane jumps to). The importer reads them from
   LSDj's ENV byte, whose low digit was being dropped with a note in about twenty tables of
   the user's first song.
3. **The wave instrument's run**: `frameLength` (frames visited, spread across the wave's own)
   and `frameLoopStep` (where Loop and Ping-pong return) beside `frameAdvance`. The importer
   reads LSDj's PLAY, LENGTH, LOOP POS and SPEED, all four of which it used to drop.
4. **`Instrument::waveFrame` is withdrawn.** §60 read the low nibble of the wave instrument's
   synth byte as a start frame; §65 measured it as LSDj's LOOP POS. The run always starts at
   its first frame, so there was nothing for the field to mean.

**Why:** the user asked for the independent columns as a ChipBoy feature -- one column walking
the pan while the other walks the duty at another rate is most of what an LSDj table sounds
like -- and for the wave channel to be able to express anything an LSDj wave instrument can.

**Considered:** keeping the lanes locked and handling the difference in the importer, which is
what §62 did -- rejected on the user's instruction and on the merits: the independence is
useful in its own right, and every LSDj format ChipBoy reads is after v1.3.0B, where the
columns became independent, so no import wants them locked. Copying LSDj's `xF` packing into
ChipBoy's VOL cell -- rejected for a LEN cell that says `H` and a step instead.

**Two behaviour changes to a song written before this round:** an `H` in CMD 1 no longer drags
the volume column with it, because the volume column has its own pointer; and the volume lane
now ends at its first empty row, as LSDj's does, where it used to walk the whole table and
loop. Rows keep their timing (`volTicks` reads back as 0, which is "as long as the table's
row"). The second is what stops an imported drum re-applying its whole envelope every sixteen
rows; a ChipBoy table whose VOL column has a gap in it loses what came after the gap, and
wants the levels moved up or a hop put in.

**The volume lane's hop is free**, which is LSDj from 8.9.3 and so what a 9.x import wants.
An older save's hop row is given a LEN of 1 at import, which the lane spends before it jumps,
so it keeps the tick it had. Confirmed on 9.2.L (a two row cycle keeps its length with a hop
under it) and on 8.4.4 (three rows and a hop take four ticks).

### 2026-09-10 (later) — the volume lane's hop, a counted phrase H, and the noise domains (spec §56, §64, §66)

**Changed:** four things, all measured on the user's saves through a save's working area.

1. **A table's volume-lane hop is free**, which is LSDj from 8.9.3 and so what a 9.x import
   wants. Confirmed on 9.2.L: two rows of four ticks with a hop under them cycle every 27
   pitch clocks a row, no extra tick anywhere. An older save's hop row is given a **LEN of 1**
   at import, which the lane spends before it jumps, so it keeps the tick it had on 8.4.4.
   No second rule in the engine, and a 9.x import is exact.
2. **A counted `H` in a phrase ends it too.** `H x y` ends the phrase x times and then lets it
   play in full (four short passes and one long, traced on 8.4.4 with `H 4 0`); ChipBoy has no
   count on a phrase, so it ends every time -- right in four passes of five rather than wrong
   in all of them, which is what dropping it did. 28 of the 283 phrase `H`s in the user's
   saves carry a count.
3. **The noise channel gets a Register sweep domain** (§66). `S` and `P` on noise before LSDj
   9 do the same arithmetic on `NR43` -- each nibble less the matching nibble of the value,
   modulo sixteen, no borrow -- `S` once and `P` every tick. The instrument picks **Notes**
   (§55, LSDj 9's) or **Register**, the importer sets Register for every format before 22, and
   the bytes go through as written.
4. **`P` on noise is mapped**, where it was dropped. In Notes it bends the note through the
   map at **value / 4** entries a tick, which is what 9.2.L does; in Register it is the nibble
   subtraction every tick.

**Why:** the user asked for 9.x imports to be exact and for the four listed gaps to be closed.

**Considered:** keeping the engine's hop at a tick and shifting the target row for 9.x --
rejected, it makes the engine carry a version; resolving an older save's `S` to semitones for
the first pass of a table loop (what §56 did) -- rejected now that the register domain exists,
since the loop's later passes went their own way.

**Still open: the kit `DIST` modes.** Narrowed this round to **byte 13's bit 6** of the kit
instrument, which changes the mixed stream with its length unchanged; bytes 4, 5, 6, 7, 10,
12, 14, 15 and the top bits of 2 and 9 do not (5, 6 and 10 are length and offset). Reading the
modes off needs a kit-stream decoder to compare the mix against each sample nibble by nibble,
which is a round of its own. Two samples at once are still summed and clipped, with the note.

### 2026-09-10 (SPACE TI, first pass) — E, a table's L, and a G's number (spec §67, §68, §69)

**Changed:** three things the user found reading SPACE TI beside LSDj 8.4.4.

1. **An `E` now runs the envelope it names.** LSDj writes the command's byte straight into
   `NR12` -- traced on PU1 phrase 82, `1F`, then `67`, then `1F` -- so the rate and direction
   are the register's. ChipBoy read the byte right and then dropped the rate: an imported
   instrument runs a **Shaped** envelope, and `stepSoftEnvelope` refused to run whenever
   `shapedOn` was set, even after an `E` had taken the level over. The level jumped to `x` and
   froze. The gate is now `shapedOn && !shapedTaken`. **Engine fix, every version**, and it
   frees a table's volume column and a velocity change the same way.
2. **A table's `L` beside a transpose slides to it, from the plain note.** The wave kick
   (instrument 10, table 01) is `TSP C4` with `L20`: LSDj sounds the note and slides down a
   semitone a pitch clock. ChipBoy jumped to the transposed note and then slid from whatever
   the last note had left behind -- a laser rather than a kick. A table's `L` fired inside a
   note-on now starts from the note without the table's transpose column. Only a table's: a
   cell's `L` is still a portamento from the note before it.
3. **A `G`'s byte is its slot as the Grooves tab counts them.** Following table 1B's `G 0A` led
   to the wrong groove, because a slot counts from `00` in Hex (§52) while the command cell
   showed the stored 1-based slot. `commandByte` and `setCommandByte` take a `G` as a slot now,
   so the cell and the tab read the same number -- and it is LSDj's number too.

**Also:** §63's groove flattening no longer overwrites a groove the song can still see. It
ranks the free slots -- LSDj-empty first, then the `6 6` default, then a groove the user wrote
but never names -- and takes the high slots before the low ones. On SPACE TI it had been
clobbering LSDj grooves 02, 03 and 05. The slots it does take are named `held 12` and so on,
so a song read beside LSDj says where the number went.

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

### 2026-09-10 (SPACE TI) — the chip ran the envelope before LSDj 8.8.0 (spec §70)

**Changed:** `docs/LSDJ_PARITY.md` §7 measured that LSDj steps the level itself off the pitch
clock, on the table 6, 11, 15, 20, 27, 36, 36, and that "every NRx2 goes out with the low
nibble 8". That is right, and it is right only from **8.8.0** on -- the changelog dates it:
**v8.8.0**, the release that moved the pulse and noise channels to software amplitude
envelopes. Traced on the user's own
songs, 8.4.4 playing SPACE TI writes `NR12` **108 times with not one low nibble 8** and rate
7 fifty-five times; the same save under 9.3.9 writes it 848 times with rates 0 and 1 only.
5.0.3 playing BIRDS agrees with 8.4.4. Before 8.8.0 LSDj hands the envelope to the chip.

The model already carried the distinction -- `EnvelopeLaw::HardwareStages` for format 11 says
"a level every (period / 64) s" -- but the driver ran §7's table for every instrument
whatever the song came from, so an import from before 8.8 ramped up to 11 % fast, and could
not tell rate 6 from rate 7 at all (§7's table gives both 36 clocks; the chip separates them
by a sixth). SPACE TI's PU1 is rate 7 fifty-five times and rate 4 twenty-nine times.

ChipBoy still steps the level itself either way -- §27's list of levels is what lets a
playback ROM replay a part. What is new is `envChipTiming` on the instrument: the levels come
at the chip's own rate, `rate x 65536` cycles. The step counter now counts in 256ths of a
pitch clock and subtracts the period rather than clearing, so a rate whose interval is not a
whole number of clocks keeps its average and the error does not accumulate; the software
table's entries are whole clocks, so its timing is unchanged to the cycle. The importer sets
the flag from the model's existing `EnvelopeLaw`, and the instrument panel has **Env rate**
(Soft / Chip), because the chip's rates are a real thing for a ChipBoy instrument to want.

**Measured after:** SPACE TI's PU1 ramp steps every 108.9 ms against the chip's 109.375 --
0.4 % out, where the software table had it at 100.5 ms, 8 % fast.

**Considered and not done:** emitting the real `NRx2` rate and letting ChipBoy's own APU run
the envelope. It would be exact rather than 0.4 % out, but it breaks §26's invariant that
every `NRx2` the driver writes is a hold, which the zombie-step model and its tests rest on,
and it would take away the level list §27 exists for.

### 2026-09-10 (SPACE TI) — the wave kick's slide holds its own aim (spec §71)

**Changed:** the user reported the WAV kick still sounding like "a high pitched laser" after
§68 fixed where its slide *starts*. Two things were wrong once the slide was running, both
read off LSDj 8.4.4's register log for phrase 17 (instrument 10, table 01: row 0 is `TSP c4`
-- signed, sixty semitones down -- beside `L20`, rows 1-13 empty, row 14 `K00`).

1. **A table's transpose column was dragging the target.** ChipBoy kept a slide as a residual
   added to the *live* pitch, and the live pitch reads the table's transpose column. A table
   steps every tick, so one tick in -- row 0 to the empty row 1 -- the base jumped sixty
   semitones and took the sounding note from 70 to 132, with the residual still walking down
   through it. That is the whine, and it is why it rose. A slide now holds a copy of that
   column for its whole run, chosen so the base sits exactly on the target; when it lands the
   channel keeps the note it reached and the column applies on top again.
2. **The aim was a note the channel cannot sound.** Sixty semitones below C-5 is note 12; the
   wave channel bottoms out at note 24, period 44, because below that the period would have
   to pass 2048. LSDj divides the distance to the *reachable* note by `x + 1` -- 48 semitones
   over 33 updates, 1.4545 apiece -- and lands on 44 as the last update falls due. ChipBoy
   divided 60 by 33, ran a quarter too fast, and then off the bottom into periods that wrapped
   into eleven bits (2040 is -8). The target is clamped to the new `lowestNote(channel)`
   before the step is worked out, so the rate comes out right because the destination does.

**Measured after:** the imported song's kick is LSDj's sweep period for period across all
thirty-five updates -- `1923 1911 1900 1887 ... 210 49 44` against `... 210 50 44` -- six of
them off by a single period unit where the fixed-point step rounds the other way, and it
comes to rest on 44 exactly as LSDj does.

**Left open:** the hold lasts the slide, not the note. Nothing measured says what a table row
setting a *new* transpose under a running slide should do -- the kick's rows are empty -- so
the simple rule stands until a song shows otherwise.

### 2026-09-10 — the table grid follows each lane, and the LSDj command matrix

**Changed:** §64 gave a table three pointers -- the volume column, the transpose-and-first-
command column, and the second command column -- and the Tables tab still drew one playhead
across the whole row, so the columns looked locked together when the point is that they are
not. `VoiceView` now carries all three rows, `packTableLanes()` publishes the two new ones,
and the grid fills each lane's own columns at that lane's row. A lane that has ended -- the
volume column stops at its first empty row -- reports -1 and draws nothing.

**Added:** `docs/LSDJ_COMMAND_MATRIX.md`, the working reference for LSDj parity. Every command
as LSDj 9.3.9 handles it (with the formula where there is one), what differs per channel and
between a phrase and a table, what ChipBoy does today, and a mappable flag saying whether the
importer can bridge the gap and how. Plus the measurement method, the version boundaries found
so far, a recipe for probing another ROM against 9.3.9 and deciding whether it shares the
implementation, and the rules for flagging what will not map on import. Every row carries a
provenance mark, because the two ways this work has gone wrong are trusting a generated probe
save (§58, §63) and trusting a measurement made on one version as though it held for all of
them (§70).

**Note:** the verbatim changelog line quoted in the §70 entries has been paraphrased. L3 keeps
LSDj's own text out of the repository; behaviour may be checked and described, which is what
these entries do.

### 2026-09-10 — the command matrix measured on 9.3.9

**Changed:** the matrix's rows were a mix of 9.2.J measurements, 8.4.4 measurements and
changelog reading. A rig was built for 9.3.9 (`tools/lsdjref/probe_fmt22.py`, `run.py`) and
every row that was not already a 9.x register measurement has been traced on that ROM.

Three of them came back different from what the changelog says or what ChipBoy assumes:

- **`B`'s sense is inverted from its changelog examples.** On 9.3.9 `B00` never plays the note
  and `B0F` always does; the changelog describes the opposite. Each nibble is an independent
  roll of about `n/15` and the note sounds if either passes -- `B44` at 48/102 matches
  `1 - (1 - 23/102)(1 - 30/102)`, not the larger nibble. In a table, `x` is the hop chance and
  `y` the destination row, and a zero `x` never hops.
- **`Z` re-runs the last command executed, not the other column's**, and its digits add to the
  target byte's nibbles. ChipBoy prefers the other slot or column and adds to the `a`/`b`
  fields, which diverges for every command whose argument is a whole byte.
- **`C` and `V` both work on the noise channel**, walking the note map; ChipBoy discards both.

`F` was also decoded properly for the first time: on PU1 it is a downward finetune of `y/32` of
a semitone with `x` ignored, on PU2 it is `x` semitones plus `y/32` upward, and on WAV it picks
the frame. ChipBoy drops it on PU1 and reads the whole byte as semitones on PU2; both are
wrong, and PU1 maps exactly onto `fineOffset` as `-8 * y`.

**Rig validation, before any of it was trusted:** the working-area path was checked against
booting the save as a file and 12549 of 12551 writes matched in order; the probe song is built
on a real editor-written format-22 song and only writes into slots that song already allocates,
which is what keeps it clear of the section 58 trap.

**Not done:** twelve letters still carry a 9.2.J measurement rather than a 9.3.9 one. They are
the same format-22 model and the same ROM code path, so they are expected to hold, and the
document says plainly which ones they are.

### 2026-09-10 — the whole command table measured on 9.3.9, and the envelope rate corrected

**Changed:** every remaining row of the command matrix was traced on 9.3.9 rather than carried
over from 9.2.J or from 8.4.4. Most confirmed what was already there -- `D` delays by exactly
`xy` ticks, `K` kills after exactly `xy`, `L` slides over exactly `xy + 1` pitch updates, `V`
cycles in exactly `64 / (x + 1)` updates, `T` is the byte in BPM, `G` selects the groove and
walks it, `H` in a table hops to row `y` exactly `x` times, `M` is `NR50` directly, `O` is
off/left/right/both, `S` is inert on PU2 and WAV and walks the map on noise, `E`'s level walks
to `x` with the right count of zombie steps, and `P` matches ChipBoy's own `bendStep256`
formula to within 1-2 per cent at every value tested.

Two did not.

1. **The envelope rate table was wrong** (spec §70, rewritten). `LSDJ_PARITY.md` §7 gives 6,
   11, 15, 20, 27, 36, 36 pitch clocks for rates 1-7, with 6 and 7 equal -- and flagged that
   equality as "worth one more run before that is taken as certain". 9.3.9 measures 6, 11,
   **17**, **22**, 28, **34**, **39**, which is the chip's own rate, `rate * 65536` cycles, and
   rates 6 and 7 are a sixth apart. §7 was measured on 9.2.J with generated probe saves, the
   trap §58 and §63 already recorded. There is one law and not two: what 8.8.0 changed is who
   steps the level, not how fast. The `envChipTiming` flag added in the previous round is
   removed -- it selected between two tables that turn out to be one -- and `envRetrig` stays,
   because that difference across 8.8.0 is real.
2. **`S` on PU1 negates each nibble.** `S x y` writes `NR10 = ((-x) & 15) << 4 | ((-y) & 15)`,
   confirmed on eight values: `S23` gives `ED`, `S71` gives `9F`, `S11` gives `FF`, `S88` gives
   `88`, `SFF` gives `11`. ChipBoy writes `(x & 7) << 4 | (y & 15)` — `23` for `S23` — so every
   imported sweep is wrong. Recorded in the matrix; not yet fixed.

**Also measured for the first time:** `H` in a table (`x` times to row `y`, `x = 0` always),
`C` on the pulses (note, +`x`, +`y`, one step a tick), `S` on noise (semitones through the map),
and `O`'s mapping onto ChipBoy's `Pan` enum.

### 2026-09-10 — the probe rig bootstraps its own host save

**Changed:** the rig hard-coded a path to one of the user's saves. It now takes
`CHIPBOY_LSDJ_DIR`, `CHIPBOY_LSDJ_HOST_SAV`, `CHIPBOY_LSDJ_ROM` and friends from the
environment, reads the host's chains and phrases off the save rather than assuming them, carries
its own decompressor instead of importing a script from outside the tree, and refuses a host of
the wrong format instead of producing quiet nonsense.

**Added:** `Probe(blank=True)` builds on a save the **ROM formatted itself**
(`lsdjref_trace --init-sav`, given ~3000 frames — at 400 it is still blank and at 1200 it is
caught mid-format), allocating the phrase, instrument and table slots it uses. So a version can
be probed with **nothing but its ROM**, which matters for old versions nobody has a save for.

Checked against a real editor-written save on the cases where a synthetic save had gone wrong
before: `S23`/`S71` in a phrase, `W03` in a phrase and in *both* table command columns, a table
`H` in column 2 (the §58 failure) and a table `G` (the §63 failure). All agree. The §58 trap is
specific to saves built from nothing by `lsdjref_sav.py`, not to bootstrapped ones.

**Added:** `docs/plan-lsdj-version-sweep.md` — the plan for the next two stages: validate the
command matrix on the 9.3.9 ROM, then sweep every older version against it, with the three-way
same / value / kind decision per command and where each answer lands in the code.
