# Plan -- grooves inferred from what is played (recording)

*(The user's idea, 2026-09-28, after the `G` question: rather than setting a groove before
recording, derive one from the notes. When what comes in cannot sit on the ticks the current
grid has, make a groove that fits the notes -- still tick-quantised -- and fall back to the
default when things fit again. Keep it simple and keep the grooves few: a shuffled passage
should keep `8 4` across gaps rather than snap back to `6 6`; keep two-step sums to 12 ticks
where possible unless a lot of notes come very fast; think through fast melodies longer than a
bar, a fast partial phrase, and "lost" ticks. This is the design, not the code: nothing here
is built until the user says so.)*

## What the tracker can already express

Three facts of the model shape the whole design:

1. **A groove is up to sixteen tick counts, used cyclically** (§9.2): step `i` lasts
   `ticks[i mod length]`. A pattern that is not periodic within sixteen entries is not one
   groove. Sixteen straight steps then eight fast ones is *not* one groove of twenty-four
   entries; it is two grooves and a `G`.
2. **A `G` in a cell changes the groove from that step**, and the row is re-laid from there
   (§135): "a groove that changes mid-row re-lays the positions after it". The revert form
   `G=` puts the phrase's own groove back. A `G` stays in force across rows until another
   `G` or a `G=` (`Player::grooveCell_`). So a row can have several grids, each a segment
   that begins at a `G`.
3. **A row's length is its steps under its grooves** (§25), and each channel's rows lie end
   to end on its own time. The recorder must never change how long a row lasts: a row that
   came out 84 ticks instead of 96 would put that channel a beat ahead of the other three for
   the rest of the song.

The recorder today quantises an incoming note to the nearest step of the phrase's own grid
(`Player::quantise`, which walks the groove in force). So the machinery for *playing back* an
inferred groove exists in full; what is new is choosing it.

## The shape: segments, hypotheses, a decision at the row's end

**Live, provisional; final at the row's end.** A note is written the moment it arrives, on the
grid in force, so the lane shows it and it replays at once. When the play head leaves the row
(or recording stops), the row's onsets -- every note's tick, held exactly as it arrived -- are
fitted once more, the row's cells re-laid onto the chosen grids, and the `G`s written. Nothing
is lost by the re-lay: a note's tick never changes, only the step that names it.

**Onsets, not cells, are the truth during a row.** The recorder keeps, per channel and row, the
list of note-on ticks (and note-off ticks, which matter for the same reason) as they arrived.
The cells are a rendering of that list under the current grids.

**A row is one or more segments.** A segment is a run of steps under one groove; the first
starts at the row's first step and takes the phrase's own groove chip; each later one starts
at a `G`. Two segments a row is the cap: more is a sign the passage is not tracker-shaped, and
the fallback (below) takes over.

**Hypotheses, simplest first.** For a segment's onsets (relative to the segment's start), the
candidate grooves, in the order they are preferred:

| Order | Groove | What it is | Steps in 96 ticks |
|---|---|---|---|
| 0 | `6` | straight sixteenths | 16 |
| 1 | `7 5`, `8 4`, `9 3`, `10 2`, `11 1` and their mirrors | swung sixteenths, two-step sum 12 | 16 |
| 2 | `8` | eighth-note triplets | 12 |
| 3 | `4` | sixteenth triplets | 24 |
| 4 | `3` | thirty-seconds | 32 |
| 5 | `4 2`, `5 1` and mirrors | swung thirty-seconds, two-step sum 6 | 32 |
| 6 | `12`, `24` | eighths, quarters as steps (sparse passages) | 8, 4 |
| 7 | `2`, `1` | the finest grids (capped by the phrase's 64 cells) | 48, 96 |
| 8 | custom | the onsets' own gaps as the entries (see the fallback) | -- |

A hypothesis **fits** when every onset of the segment is within the **tolerance** of a step
start of that grid, and no two onsets share a step. The tolerance is the human-timing window:
**one tick** by default (21 ms at 120 BPM), a setting the user can widen to two. It is what
keeps a slightly late note from becoming a `7 5` groove of its own. It is also why swing is
detected by *position* and not by exactness: an offbeat at 8 ticks is two ticks from the
straight grid and lands on `8 4`'s step exactly; at 7 it is `7 5`; at 9, `9 3`. Human swing
wanders between those, so among the swing pairs the one whose grid the *median* offbeat lands
nearest wins, and every offbeat of the segment is then quantised onto it.

**Scoring.** Among the hypotheses that fit, the winner is the lowest of
`order + deviation + newSlot + change`, where `deviation` is the mean distance of the onsets
to their steps in ticks, `newSlot` is 1 when the groove is not yet in the song's 32 slots (a
new slot is a cost: "without creating too many"), and `change` is 1 when the groove differs
from the one the previous segment or row used. The last term is the stickiness the user asked
for: a row of downbeats fits straight and `8 4` equally, and under a shuffle it stays `8 4`.

**The row-length invariant.** A segment's step count is whatever its groove needs to reach
the next segment's start, or the row's end. When the segment's length is not a multiple of the
groove's cycle -- 30 ticks left under `8` -- the last entry absorbs the remainder (`8 8 8 6`),
which is a custom groove of four entries. That is the one place a "compensating" step appears,
and it appears at the *end* of the segment, never earlier: earlier onsets are already fitted
and must not move. The user's alternative, keeping every phrase at 96 ticks and absorbing lost
ticks earlier in the bar, is the same invariant seen from the other side; this design meets it
by never letting a row's length change in the first place.

**Slots.** A groove is looked up in the song's 32 slots (§162): the same tick counts, reuse;
otherwise the first slot no phrase and no `G` references. With no slot free the best fitting
existing groove is used and the status line says so. Auto grooves are named (`auto 8 4`), so
the Grooves tab shows where they came from.

**Steps.** A phrase holds 64 cells. A segment under `2` or `1` that would need more ends the
row there: the phrase's STEPS is set, and the rest of the bar becomes the next row -- a new
phrase on that channel alone, which the model allows (§25). The user sees a shorter row in the
chain; the timeline draws it as long as it lasts.

**Tolerance, as built (§226 amendment).** None: the user's target is sequenced MIDI, so every
note must sit on a step of its own and the row's own gaps are always a candidate. What follows
about a one-tick tolerance and sloppy playing is the plan as first written.

**`G` placement.** The first segment's groove is the phrase's chip (no `G`). Each later
segment writes `G n` into its first cell's free command column (a straight segment names a
slot that is straight: `G 0` is the revert to the chip). If the row ends under a `G`, the
next row's first cell gets `G=` -- *always*, as built (§226): a `G` left to carry overrides
every later row's chip, which the first build found when a `4` chip four rows on was laid
as straight; and it is written at once, since until the next row is fitted the tables would
lay the rows after it on the `G`'s grid.

## The cases

- **Straight playing.** Every onset within a tick of a multiple of 6: order 0 wins, no slot,
  no `G`. The common case costs nothing.
- **A shuffled bar with rests** (onsets 0, 8, 12, 24, 32, 36, 48 ...). Straight fails (8 is two
  ticks off). `8 4` fits: order 1, deviation 0. The rests do not matter: fitting is over the
  onsets that exist. The next bar, all downbeats: straight and `8 4` both fit; `change` makes
  `8 4` win. Only a bar whose offbeats sit at 6 again breaks the shuffle -- which is right.
- **Swing that drifts** (offbeats at 7, 8, 8, 9). Straight fails; `7 5` fits within the
  tolerance for the 7 and the 8s, not the 9; `8 4` fits all four (7 and 9 are one tick off). `8
  4` wins on fit, the deviation is 0.5. The 7 and the 9 are written at the `8 4` step: quantised
  swing, which is what a tracker plays anyway.
- **Triplets.** Onsets 0, 8, 16, 24: straight fails at 8 and 16; `8` (order 2) fits exactly.
  Twelve steps to the bar. A bar of sixteenths after it: `8` fails at 6; straight fits; `change`
  costs 1 but nothing else fits, so the groove switches back and the phrase chip (or a `G=`)
  says so.
- **A fast run longer than a bar** (thirty-seconds for two bars). Row 1: `3` fits (order 4),
  32 steps, one new slot. Row 2: `3` again, reused, `change` 0. Two 32-step phrases on that
  channel; the other channels keep their 16. Correct by §25, and the chain shows two rows of
  the usual height.
- **A fast partial phrase** (twelve straight steps, then eight thirty-seconds in the last
  beat). One groove cannot say it (fact 1). The fitter tries a split: segment 1 (ticks 0-71)
  straight, segment 2 (72-95) `3`; 12 + 8 = 20 steps, 96 ticks, a `G` at step 12 and a `G=` at
  the next row's start. The row's length is unchanged. The user's idea of a 16-step phrase
  with the lost ticks absorbed earlier would need `[6 ×12, 3 ×8]` as a twenty-entry groove,
  which the model has no room for; the split says the same thing in the tracker's own words.
- **A fast run that starts mid-bar and spills into the next bar.** Row 1 splits as above;
  row 2's first segment is `3` from its start (the phrase chip), then a `G=` or a `G 0`
  (straight) where the run ends. Two `G`s across two rows, both visible.
- **Sloppy playing** (a note four ticks late on an otherwise straight bar). Straight fails
  for that note; every other grid fails too or fits worse; the custom fallback (order 8) would
  make a groove of the bar's own gaps -- a new slot for one clumsy note. Rule: the fallback is
  taken only when at least three onsets of the segment need it; otherwise the row stays on the
  best simple grid and the stray note is quantised to its nearest step, with the deviation
  shown on the status line ("1 note moved 4 ticks"). This is the setting the user can push
  toward exactness (tolerance 0, fallback always) for chip music that *is* the odd tick.
- **The empty row**, or a row with one note: straight, no change, nothing written.
- **Note lengths.** A note-off is a cell too (OFF). Offs are fitted with the onsets: a grid
  that fits the onsets and not the offs is still taken, and the off goes to its nearest step
  -- an off is a musical release, not an attack, and moving it a tick is inaudible where
  moving an attack is not.
- **Two rows of different channels.** Each channel fits its own rows; a swung melody over
  straight drums gives PU1 `8 4` and NOI `6`, as a tracker author would write it.
- **Slots run out** (32 in use). The best existing groove is taken; the status line reports
  "no free groove slot: used 8 4". Never a silent fallback.
- **Live playback while recording.** Provisional placement uses the grid in force, so a
  swung note played over a straight row is heard straight until the row ends and re-lays. If
  that is disliked, the re-fit can also run every four onsets within the row; the design
  allows it, the cost is more re-lays on screen.

## What it needs in the code

- `Player`: per channel, the row's onset/off list in ticks (`RowTake`), kept while the row
  records; `fitRow(ch)` at the row's end: hypotheses, scoring, segments, the re-lay of the
  row's cells, the `G` writes, the slot lookup; a `RecordMessage` that replaces a row's cells
  (today's messages add one cell at a time).
- `Song`: nothing new. `docs/COMMANDS_AND_TEMPO.md` gets the numbered section with the
  candidate table and the scoring.
- The Tracker tab: **Groove: auto / phrase's** beside Rec (D-UI row), the tolerance in the
  Hardware tab's recording group (a 1-tick default, 0-3), and the status line's report.
- Tests: `[record][groove]` -- each case above as a list of onset ticks fed to `fitRow`,
  asserting the groove, the segments and the `G`s; and a round trip: the cells the fit
  writes, played back, fire at the onset ticks (within the tolerance).

## What I recommend

Build it as described, with the row's-end fit and two segments a row. It keeps the user's
intent -- grooves come from playing, not from a setting -- while staying inside what a phrase
and a `G` can say, so everything it writes is ordinary tracker content the user can read and
edit. The per-MIDI-channel record groove proposed earlier is not needed; the phrase's own chip
and the `G` do that job, and the fitter chooses them.
