# The LSDj parity test spec (docs/COMMANDS_AND_TEMPO.md 31).
#
# One `case` is one song: lsdjref_sav.py writes it into an LSDj save, the
# trace tool plays it on the ROM and logs the APU writes, and lsdjref-compare
# builds the same song for ChipBoy's driver and diffs the two streams.
#
# Lines, all inside a case:
#   desc TEXT            what the case is for
#   frames N             emulator frames to run (60 a second; playback starts
#                        at frame 180, so allow for that)
#   models dmg cgb       which consoles to trace
#   tempo BPM            the song tempo
#   groove SLOT T...     a groove's tick counts
#   inst SLOT KIND K=V   pulse | wave | noise, fields as lsdjref_sav.py names them
#   table SLOT           opens a table; `row` lines follow
#   phrase SLOT          opens a phrase; `row` lines follow
#   row STEP K=V         table: env= tsp= c1= c2=   phrase: n= i= c=
#   chain CH P P P       pu1 | pu2 | wav | noi, the phrases in playing order
#
# Notes are scientific pitch (C-4 is middle C, MIDI 60). Command values are
# LSDj's single byte, hex: `c=V:48` is V with x=4, y=8. A `-` is nothing.
# Every two-digit value in a command or an instrument field is hex, since that
# is how LSDj shows them; `frames`, `tempo` and `groove` are decimal.
#
# Instruments keep LSDj's own encodings, so what the spec says is what the
# save holds: `env` is the NRx2 byte (amplitude in the high nibble; 0 and 8
# hold, 1-7 fall at that rate, 9-F rise at rate-8) and `sweep` is NR10's
# complement, which is why FF means no sweep.

# --- (a) the baseline -----------------------------------------------------

case a_baseline
  desc Plain pulse notes every 8 steps: the note-on sequence, and the note column
  frames 1500
  models dmg cgb
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF pitch=fast
  phrase 0
    row 0 n=C-4 i=0
    row 8 n=C-4 i=0
  phrase 1
    row 0 n=C-2 i=0
    row 4 n=C-3 i=0
    row 8 n=C-5 i=0
    row 12 n=C-6 i=0
  phrase 2
    row 0 n=A-4 i=0
    row 4 n=A#4 i=0
    row 8 n=B-4 i=0
    row 12 n=G-7 i=0
  chain pu1 0 1 2

# --- (b) V vibrato, one case per PITCH speed ------------------------------
# Sixteen speed x depth pairs to a case, two to a phrase; every note carries
# the instrument column, so each pair starts from a freshly loaded instrument.

case b_vib_fast
  desc V vibrato at speeds 1 4 8 F and depths 0 3 8 F, PITCH = FAST
  frames 1400
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF pitch=fast
  phrase 0
    row 0 n=C-5 i=0 c=V:10
    row 8 n=C-5 i=0 c=V:13
  phrase 1
    row 0 n=C-5 i=0 c=V:18
    row 8 n=C-5 i=0 c=V:1F
  phrase 2
    row 0 n=C-5 i=0 c=V:40
    row 8 n=C-5 i=0 c=V:43
  phrase 3
    row 0 n=C-5 i=0 c=V:48
    row 8 n=C-5 i=0 c=V:4F
  phrase 4
    row 0 n=C-5 i=0 c=V:80
    row 8 n=C-5 i=0 c=V:83
  phrase 5
    row 0 n=C-5 i=0 c=V:88
    row 8 n=C-5 i=0 c=V:8F
  phrase 6
    row 0 n=C-5 i=0 c=V:F0
    row 8 n=C-5 i=0 c=V:F3
  phrase 7
    row 0 n=C-5 i=0 c=V:F8
    row 8 n=C-5 i=0 c=V:FF
  chain pu1 0 1 2 3 4 5 6 7

case b_vib_tick
  desc The same sixteen V pairs with PITCH = TICK
  frames 1400
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF pitch=tick
  phrase 0
    row 0 n=C-5 i=0 c=V:10
    row 8 n=C-5 i=0 c=V:13
  phrase 1
    row 0 n=C-5 i=0 c=V:18
    row 8 n=C-5 i=0 c=V:1F
  phrase 2
    row 0 n=C-5 i=0 c=V:40
    row 8 n=C-5 i=0 c=V:43
  phrase 3
    row 0 n=C-5 i=0 c=V:48
    row 8 n=C-5 i=0 c=V:4F
  phrase 4
    row 0 n=C-5 i=0 c=V:80
    row 8 n=C-5 i=0 c=V:83
  phrase 5
    row 0 n=C-5 i=0 c=V:88
    row 8 n=C-5 i=0 c=V:8F
  phrase 6
    row 0 n=C-5 i=0 c=V:F0
    row 8 n=C-5 i=0 c=V:F3
  phrase 7
    row 0 n=C-5 i=0 c=V:F8
    row 8 n=C-5 i=0 c=V:FF
  chain pu1 0 1 2 3 4 5 6 7

case b_vib_step
  desc The same sixteen V pairs with PITCH = STEP
  frames 1400
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF pitch=step
  phrase 0
    row 0 n=C-5 i=0 c=V:10
    row 8 n=C-5 i=0 c=V:13
  phrase 1
    row 0 n=C-5 i=0 c=V:18
    row 8 n=C-5 i=0 c=V:1F
  phrase 2
    row 0 n=C-5 i=0 c=V:40
    row 8 n=C-5 i=0 c=V:43
  phrase 3
    row 0 n=C-5 i=0 c=V:48
    row 8 n=C-5 i=0 c=V:4F
  phrase 4
    row 0 n=C-5 i=0 c=V:80
    row 8 n=C-5 i=0 c=V:83
  phrase 5
    row 0 n=C-5 i=0 c=V:88
    row 8 n=C-5 i=0 c=V:8F
  phrase 6
    row 0 n=C-5 i=0 c=V:F0
    row 8 n=C-5 i=0 c=V:F3
  phrase 7
    row 0 n=C-5 i=0 c=V:F8
    row 8 n=C-5 i=0 c=V:FF
  chain pu1 0 1 2 3 4 5 6 7

case b_vib_drum
  desc The same sixteen V pairs with PITCH = DRUM
  frames 1400
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF pitch=drum
  phrase 0
    row 0 n=C-5 i=0 c=V:10
    row 8 n=C-5 i=0 c=V:13
  phrase 1
    row 0 n=C-5 i=0 c=V:18
    row 8 n=C-5 i=0 c=V:1F
  phrase 2
    row 0 n=C-5 i=0 c=V:40
    row 8 n=C-5 i=0 c=V:43
  phrase 3
    row 0 n=C-5 i=0 c=V:48
    row 8 n=C-5 i=0 c=V:4F
  phrase 4
    row 0 n=C-5 i=0 c=V:80
    row 8 n=C-5 i=0 c=V:83
  phrase 5
    row 0 n=C-5 i=0 c=V:88
    row 8 n=C-5 i=0 c=V:8F
  phrase 6
    row 0 n=C-5 i=0 c=V:F0
    row 8 n=C-5 i=0 c=V:F3
  phrase 7
    row 0 n=C-5 i=0 c=V:F8
    row 8 n=C-5 i=0 c=V:FF
  chain pu1 0 1 2 3 4 5 6 7

# --- (c) L slides ---------------------------------------------------------

case c_slide_fast
  desc L slides of 00, 04 and 10 between C-4 and C-5, PITCH = FAST
  frames 1200
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF pitch=fast
  phrase 0
    row 0 n=C-4 i=0
    row 4 n=C-5 i=0 c=L:00
    row 8 n=C-4 i=0 c=L:00
  phrase 1
    row 0 n=C-4 i=0
    row 4 n=C-5 i=0 c=L:04
    row 8 n=C-4 i=0 c=L:04
  phrase 2
    row 0 n=C-4 i=0
    row 4 n=C-5 i=0 c=L:10
    row 8 n=C-4 i=0 c=L:10
  chain pu1 0 1 2

case c_slide_tick
  desc The same three L slides with PITCH = TICK
  frames 1200
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF pitch=tick
  phrase 0
    row 0 n=C-4 i=0
    row 4 n=C-5 i=0 c=L:00
    row 8 n=C-4 i=0 c=L:00
  phrase 1
    row 0 n=C-4 i=0
    row 4 n=C-5 i=0 c=L:04
    row 8 n=C-4 i=0 c=L:04
  phrase 2
    row 0 n=C-4 i=0
    row 4 n=C-5 i=0 c=L:10
    row 8 n=C-4 i=0 c=L:10
  chain pu1 0 1 2

# --- (d) P bends, one case per PITCH speed --------------------------------

case d_bend_fast
  desc P bends of +2 -2 +16 -16 +64 -64 with PITCH = FAST
  frames 1200
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF pitch=fast
  phrase 0
    row 0 n=C-5 i=0 c=P:02
    row 8 n=C-5 i=0 c=P:FE
  phrase 1
    row 0 n=C-5 i=0 c=P:10
    row 8 n=C-5 i=0 c=P:F0
  phrase 2
    row 0 n=C-5 i=0 c=P:40
    row 8 n=C-5 i=0 c=P:C0
  chain pu1 0 1 2

case d_bend_tick
  desc The same six P bends with PITCH = TICK
  frames 1200
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF pitch=tick
  phrase 0
    row 0 n=C-5 i=0 c=P:02
    row 8 n=C-5 i=0 c=P:FE
  phrase 1
    row 0 n=C-5 i=0 c=P:10
    row 8 n=C-5 i=0 c=P:F0
  phrase 2
    row 0 n=C-5 i=0 c=P:40
    row 8 n=C-5 i=0 c=P:C0
  chain pu1 0 1 2

case d_bend_step
  desc The same six P bends with PITCH = STEP
  frames 1200
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF pitch=step
  phrase 0
    row 0 n=C-5 i=0 c=P:02
    row 8 n=C-5 i=0 c=P:FE
  phrase 1
    row 0 n=C-5 i=0 c=P:10
    row 8 n=C-5 i=0 c=P:F0
  phrase 2
    row 0 n=C-5 i=0 c=P:40
    row 8 n=C-5 i=0 c=P:C0
  chain pu1 0 1 2

case d_bend_drum
  desc The same six P bends with PITCH = DRUM
  frames 1200
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF pitch=drum
  phrase 0
    row 0 n=C-5 i=0 c=P:02
    row 8 n=C-5 i=0 c=P:FE
  phrase 1
    row 0 n=C-5 i=0 c=P:10
    row 8 n=C-5 i=0 c=P:F0
  phrase 2
    row 0 n=C-5 i=0 c=P:40
    row 8 n=C-5 i=0 c=P:C0
  chain pu1 0 1 2

# --- (e) E mid-note, and the instrument's own envelope rates ---------------

case e_env_change
  desc E on a sounding note: a level at the same rate, a different rate, and up
  frames 1200
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF
  phrase 0
    row 0 n=C-5 i=0
    row 4 c=E:80
    row 8 c=E:40
    row 12 c=E:C0
  phrase 1
    row 0 n=C-5 i=0
    row 4 c=E:83
    row 8 c=E:87
    row 12 c=E:F1
  phrase 2
    row 0 n=C-5 i=0
    row 4 c=E:09
    row 8 c=E:0F
    row 12 c=E:80
  chain pu1 0 1 2

case e_env_rates
  desc The instrument envelope's own rates 1 to 7 down, and 9 to F up
  note One note to a phrase, held for the whole sixteen steps, so each
  note envelope runs undisturbed and its step interval can be averaged.
  frames 2100
  tempo 120
  inst 0 pulse env=F1 duty=2 sweep=FF
  inst 1 pulse env=F2 duty=2 sweep=FF
  inst 2 pulse env=F3 duty=2 sweep=FF
  inst 3 pulse env=F4 duty=2 sweep=FF
  inst 4 pulse env=F5 duty=2 sweep=FF
  inst 5 pulse env=F6 duty=2 sweep=FF
  inst 6 pulse env=F7 duty=2 sweep=FF
  inst 7 pulse env=09 duty=2 sweep=FF
  inst 8 pulse env=0B duty=2 sweep=FF
  inst 9 pulse env=0F duty=2 sweep=FF
  phrase 0
    row 0 n=C-5 i=0
  phrase 1
    row 0 n=C-5 i=1
  phrase 2
    row 0 n=C-5 i=2
  phrase 3
    row 0 n=C-5 i=3
  phrase 4
    row 0 n=C-5 i=4
  phrase 5
    row 0 n=C-5 i=5
  phrase 6
    row 0 n=C-5 i=6
  phrase 7
    row 0 n=C-5 i=7
  phrase 8
    row 0 n=C-5 i=8
  phrase 9
    row 0 n=C-5 i=9
  chain pu1 0 1 2 3 4 5 6 7 8 9

# --- (f) a table's volume column ------------------------------------------

case f_table_volume
  desc A table stepping the volume 15 8 4 0 -- retrigger or zombie step?
  note A volume row needs a non-zero low nibble: measured, a row whose
  note envelope nibble is 0 writes nothing at all.
  frames 1200
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF table=0
  inst 1 pulse env=F0 duty=2 sweep=FF table=1
  table 0
    row 0 env=F1
    row 1 env=81
    row 2 env=41
    row 3 env=01
    row 4 env=F1
    row 5 env=81
    row 6 env=41
    row 7 env=01
  table 1
    row 0 env=F0
    row 1 env=80
    row 2 env=40
    row 3 env=00
  phrase 0
    row 0 n=C-5 i=0
    row 8 n=C-5 i=1
  chain pu1 0

# --- (g) a bare note ------------------------------------------------------

case g_bare_note
  desc A note with V, then a note with a blank instrument column
  frames 1200
  tempo 120
  inst 0 pulse env=F3 duty=2 sweep=FF
  phrase 0
    row 0 n=C-5 i=0 c=V:88
    row 4 n=E-5
    row 8 n=G-5
    row 12 n=C-5 i=0
  phrase 1
    row 0 n=C-5 i=0
    row 4 n=E-5
    row 8 n=G-5 i=0
    row 12 n=E-5
  chain pu1 0 1

# --- (h) R retrigger ------------------------------------------------------

case h_retrig
  desc R with y = 1 and 4, x = 0, 2 and A
  frames 1400
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF
  phrase 0
    row 0 n=C-5 i=0 c=R:01
    row 8 n=C-5 i=0 c=R:04
  phrase 1
    row 0 n=C-5 i=0 c=R:21
    row 8 n=C-5 i=0 c=R:24
  phrase 2
    row 0 n=C-5 i=0 c=R:A1
    row 8 n=C-5 i=0 c=R:A4
  phrase 3
    row 0 n=C-5 i=0 c=R:00
    row 8 n=C-5 i=0 c=R:80
  chain pu1 0 1 2 3

# --- (i) K, D and C -------------------------------------------------------

case i_kill_delay_chord
  desc K kills after so many ticks, D delays the note, C arpeggiates
  frames 1400
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF
  inst 1 pulse env=F0 duty=2 sweep=FF cmdrate=3
  phrase 0
    row 0 n=C-5 i=0 c=K:02
    row 4 n=C-5 i=0 c=K:06
    row 8 n=C-5 i=0 c=D:02
    row 12 n=C-5 i=0 c=D:06
  phrase 1
    row 0 n=C-5 i=0 c=C:37
    row 8 n=C-5 i=0 c=C:47
  phrase 2
    row 0 n=C-5 i=0 c=C:C0
    row 8 n=C-5 i=0 c=C:00
  phrase 3
    row 0 n=C-5 i=1 c=C:37
    row 8 n=C-5 i=1 c=R:04
  chain pu1 0 1 2 3

# --- (j) grooves and hops -------------------------------------------------

case j_groove_hop
  desc A G groove inside a table, and H hops in a table and a phrase
  frames 1400
  tempo 120
  groove 0 6 6
  groove 1 3 3
  groove 2 12 12
  inst 0 pulse env=F0 duty=2 sweep=FF table=0
  inst 1 pulse env=F0 duty=2 sweep=FF table=1
  table 0
    row 0 env=F1 c1=G:01
    row 1 env=81
    row 2 env=41
    row 3 env=F1 c1=H:00
  table 1
    row 0 env=F1 c1=G:02
    row 1 env=81
    row 2 env=41
    row 3 env=F1 c1=H:00
  phrase 0
    row 0 n=C-5 i=0
    row 8 n=C-5 i=1
  phrase 1
    row 0 n=C-5 i=0 c=G:01
    row 4 n=C-5 i=0
    row 8 n=C-5 i=0 c=G:00
    row 12 n=C-5 i=0
  chain pu1 0 1

# --- (k) a wave kick ------------------------------------------------------

case k_wave_kick
  desc A wave instrument with a frame run, and a P kick table on it
  frames 1200
  tempo 120
  inst 0 wave volume=3 wave=0 synth=0 pitch=drum table=0 speed=4 length=0F playtype=2
  inst 1 wave volume=3 wave=0 synth=0 pitch=fast speed=1 length=0F playtype=2
  table 0
    row 0 env=F0 c1=P:C0
    row 1 env=F0 tsp=80 c1=L:30
    row 2 env=F0
    row 3 env=F0 c1=H:00
  phrase 0
    row 0 n=C-6 i=0
    row 8 n=C-6 i=0
  phrase 1
    row 0 n=C-5 i=1
    row 8 n=C-6 i=1
  chain wav 0 1

# --- (l) noise ------------------------------------------------------------

case l_noise
  desc Noise with an S shape command, free and safe pitch
  frames 1200
  tempo 120
  inst 0 noise env=F0 safe=0
  inst 1 noise env=F0 safe=1
  phrase 0
    row 0 n=C-5 i=0
    row 4 n=C-5 i=0 c=S:12
    row 8 n=C-5 i=0 c=S:F0
    row 12 n=C-5 i=0 c=S:0F
  phrase 1
    row 0 n=C-5 i=1
    row 4 n=C-5 i=1 c=S:12
    row 8 n=C-4 i=1
    row 12 n=C-6 i=1
  chain noi 0 1

# --- the scaling cases: one command, many values ---------------------------

case d_bend_scale
  desc P from -1 to -127, from a high note so nothing wraps, in DRUM and FAST
  note DRUM moves the period in a straight line, which is the easiest place to
  note read the rate off; the FAST rows of the same case are in d_bend_scale_fast.
  frames 1500
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF pitch=drum
  phrase 0
    row 0 n=C-6 i=0 c=P:FF
    row 8 n=C-6 i=0 c=P:FE
  phrase 1
    row 0 n=C-6 i=0 c=P:FC
    row 8 n=C-6 i=0 c=P:F8
  phrase 2
    row 0 n=C-6 i=0 c=P:F0
    row 8 n=C-6 i=0 c=P:E0
  phrase 3
    row 0 n=C-6 i=0 c=P:C0
    row 8 n=C-6 i=0 c=P:81
  chain pu1 0 1 2 3

case d_bend_scale_fast
  desc The same eight P rates with PITCH = FAST
  frames 1500
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF pitch=fast
  phrase 0
    row 0 n=C-6 i=0 c=P:FF
    row 8 n=C-6 i=0 c=P:FE
  phrase 1
    row 0 n=C-6 i=0 c=P:FC
    row 8 n=C-6 i=0 c=P:F8
  phrase 2
    row 0 n=C-6 i=0 c=P:F0
    row 8 n=C-6 i=0 c=P:E0
  phrase 3
    row 0 n=C-6 i=0 c=P:C0
    row 8 n=C-6 i=0 c=P:81
  chain pu1 0 1 2 3

case b_vib_shapes
  desc The three vibrato shapes, down and up, at speed 4 depth F
  frames 1200
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF vib_shape=0 vib_dir=0
  inst 1 pulse env=F0 duty=2 sweep=FF vib_shape=0 vib_dir=1
  inst 2 pulse env=F0 duty=2 sweep=FF vib_shape=1 vib_dir=0
  inst 3 pulse env=F0 duty=2 sweep=FF vib_shape=1 vib_dir=1
  inst 4 pulse env=F0 duty=2 sweep=FF vib_shape=2 vib_dir=0
  inst 5 pulse env=F0 duty=2 sweep=FF vib_shape=2 vib_dir=1
  phrase 0
    row 0 n=C-5 i=0 c=V:4F
    row 8 n=C-5 i=1 c=V:4F
  phrase 1
    row 0 n=C-5 i=2 c=V:4F
    row 8 n=C-5 i=3 c=V:4F
  phrase 2
    row 0 n=C-5 i=4 c=V:4F
    row 8 n=C-5 i=5 c=V:4F
  chain pu1 0 1 2

# --- the fine cases: one law, many values ---------------------------------
# Added for the LSDj-exact round: the sparse cases above pin the shape of
# each law, these pin its numbers. They are read the same way and traced the
# same way; the tables they produced are in docs/LSDJ_PARITY.md.

case d_bend_scale_all
  desc P at every value from -1 to -127, DRUM, two steps a note: the rate table
  note The period moves in a straight line in DRUM, so the rate reads off the
  note first differences. C-6 is high enough that the small values never wrap.
  frames 2200
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF pitch=drum
  phrase 0
    row 0 n=C-6 i=0 c=P:FF
    row 2 n=C-6 i=0 c=P:FE
    row 4 n=C-6 i=0 c=P:FD
    row 6 n=C-6 i=0 c=P:FC
    row 8 n=C-6 i=0 c=P:FB
    row 10 n=C-6 i=0 c=P:FA
    row 12 n=C-6 i=0 c=P:F9
    row 14 n=C-6 i=0 c=P:F8
  phrase 1
    row 0 n=C-6 i=0 c=P:F7
    row 2 n=C-6 i=0 c=P:F6
    row 4 n=C-6 i=0 c=P:F5
    row 6 n=C-6 i=0 c=P:F4
    row 8 n=C-6 i=0 c=P:F3
    row 10 n=C-6 i=0 c=P:F2
    row 12 n=C-6 i=0 c=P:F1
    row 14 n=C-6 i=0 c=P:F0
  phrase 2
    row 0 n=C-6 i=0 c=P:EF
    row 2 n=C-6 i=0 c=P:EE
    row 4 n=C-6 i=0 c=P:ED
    row 6 n=C-6 i=0 c=P:EC
    row 8 n=C-6 i=0 c=P:EB
    row 10 n=C-6 i=0 c=P:EA
    row 12 n=C-6 i=0 c=P:E9
    row 14 n=C-6 i=0 c=P:E8
  phrase 3
    row 0 n=C-6 i=0 c=P:E7
    row 2 n=C-6 i=0 c=P:E6
    row 4 n=C-6 i=0 c=P:E5
    row 6 n=C-6 i=0 c=P:E4
    row 8 n=C-6 i=0 c=P:E3
    row 10 n=C-6 i=0 c=P:E2
    row 12 n=C-6 i=0 c=P:E1
    row 14 n=C-6 i=0 c=P:E0
  phrase 4
    row 0 n=C-6 i=0 c=P:DF
    row 2 n=C-6 i=0 c=P:DE
    row 4 n=C-6 i=0 c=P:DD
    row 6 n=C-6 i=0 c=P:DC
    row 8 n=C-6 i=0 c=P:DB
    row 10 n=C-6 i=0 c=P:DA
    row 12 n=C-6 i=0 c=P:D9
    row 14 n=C-6 i=0 c=P:D8
  phrase 5
    row 0 n=C-6 i=0 c=P:D7
    row 2 n=C-6 i=0 c=P:D6
    row 4 n=C-6 i=0 c=P:D5
    row 6 n=C-6 i=0 c=P:D4
    row 8 n=C-6 i=0 c=P:D3
    row 10 n=C-6 i=0 c=P:D2
    row 12 n=C-6 i=0 c=P:D1
    row 14 n=C-6 i=0 c=P:D0
  phrase 6
    row 0 n=C-6 i=0 c=P:CF
    row 2 n=C-6 i=0 c=P:CE
    row 4 n=C-6 i=0 c=P:CD
    row 6 n=C-6 i=0 c=P:CC
    row 8 n=C-6 i=0 c=P:CB
    row 10 n=C-6 i=0 c=P:CA
    row 12 n=C-6 i=0 c=P:C9
    row 14 n=C-6 i=0 c=P:C8
  phrase 7
    row 0 n=C-6 i=0 c=P:C7
    row 2 n=C-6 i=0 c=P:C6
    row 4 n=C-6 i=0 c=P:C5
    row 6 n=C-6 i=0 c=P:C4
    row 8 n=C-6 i=0 c=P:C3
    row 10 n=C-6 i=0 c=P:C2
    row 12 n=C-6 i=0 c=P:C1
    row 14 n=C-6 i=0 c=P:C0
  phrase 8
    row 0 n=C-6 i=0 c=P:BF
    row 2 n=C-6 i=0 c=P:BE
    row 4 n=C-6 i=0 c=P:BD
    row 6 n=C-6 i=0 c=P:BC
    row 8 n=C-6 i=0 c=P:BB
    row 10 n=C-6 i=0 c=P:BA
    row 12 n=C-6 i=0 c=P:B9
    row 14 n=C-6 i=0 c=P:B8
  phrase 9
    row 0 n=C-6 i=0 c=P:B7
    row 2 n=C-6 i=0 c=P:B6
    row 4 n=C-6 i=0 c=P:B5
    row 6 n=C-6 i=0 c=P:B4
    row 8 n=C-6 i=0 c=P:B3
    row 10 n=C-6 i=0 c=P:B2
    row 12 n=C-6 i=0 c=P:B1
    row 14 n=C-6 i=0 c=P:B0
  phrase 10
    row 0 n=C-6 i=0 c=P:AF
    row 2 n=C-6 i=0 c=P:AE
    row 4 n=C-6 i=0 c=P:AD
    row 6 n=C-6 i=0 c=P:AC
    row 8 n=C-6 i=0 c=P:AB
    row 10 n=C-6 i=0 c=P:AA
    row 12 n=C-6 i=0 c=P:A9
    row 14 n=C-6 i=0 c=P:A8
  phrase 11
    row 0 n=C-6 i=0 c=P:A7
    row 2 n=C-6 i=0 c=P:A6
    row 4 n=C-6 i=0 c=P:A5
    row 6 n=C-6 i=0 c=P:A4
    row 8 n=C-6 i=0 c=P:A3
    row 10 n=C-6 i=0 c=P:A2
    row 12 n=C-6 i=0 c=P:A1
    row 14 n=C-6 i=0 c=P:A0
  phrase 12
    row 0 n=C-6 i=0 c=P:9F
    row 2 n=C-6 i=0 c=P:9E
    row 4 n=C-6 i=0 c=P:9D
    row 6 n=C-6 i=0 c=P:9C
    row 8 n=C-6 i=0 c=P:9B
    row 10 n=C-6 i=0 c=P:9A
    row 12 n=C-6 i=0 c=P:99
    row 14 n=C-6 i=0 c=P:98
  phrase 13
    row 0 n=C-6 i=0 c=P:97
    row 2 n=C-6 i=0 c=P:96
    row 4 n=C-6 i=0 c=P:95
    row 6 n=C-6 i=0 c=P:94
    row 8 n=C-6 i=0 c=P:93
    row 10 n=C-6 i=0 c=P:92
    row 12 n=C-6 i=0 c=P:91
    row 14 n=C-6 i=0 c=P:90
  phrase 14
    row 0 n=C-6 i=0 c=P:8F
    row 2 n=C-6 i=0 c=P:8E
    row 4 n=C-6 i=0 c=P:8D
    row 6 n=C-6 i=0 c=P:8C
    row 8 n=C-6 i=0 c=P:8B
    row 10 n=C-6 i=0 c=P:8A
    row 12 n=C-6 i=0 c=P:89
    row 14 n=C-6 i=0 c=P:88
  phrase 15
    row 0 n=C-6 i=0 c=P:87
    row 2 n=C-6 i=0 c=P:86
    row 4 n=C-6 i=0 c=P:85
    row 6 n=C-6 i=0 c=P:84
    row 8 n=C-6 i=0 c=P:83
    row 10 n=C-6 i=0 c=P:82
    row 12 n=C-6 i=0 c=P:81
  chain pu1 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15

case d_bend_scale_up
  desc P at every value from +1 to +64, DRUM: the rate table on the other side
  frames 1300
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF pitch=drum
  phrase 0
    row 0 n=C-3 i=0 c=P:01
    row 2 n=C-3 i=0 c=P:02
    row 4 n=C-3 i=0 c=P:03
    row 6 n=C-3 i=0 c=P:04
    row 8 n=C-3 i=0 c=P:05
    row 10 n=C-3 i=0 c=P:06
    row 12 n=C-3 i=0 c=P:07
    row 14 n=C-3 i=0 c=P:08
  phrase 1
    row 0 n=C-3 i=0 c=P:09
    row 2 n=C-3 i=0 c=P:0A
    row 4 n=C-3 i=0 c=P:0B
    row 6 n=C-3 i=0 c=P:0C
    row 8 n=C-3 i=0 c=P:0D
    row 10 n=C-3 i=0 c=P:0E
    row 12 n=C-3 i=0 c=P:0F
    row 14 n=C-3 i=0 c=P:10
  phrase 2
    row 0 n=C-3 i=0 c=P:11
    row 2 n=C-3 i=0 c=P:12
    row 4 n=C-3 i=0 c=P:13
    row 6 n=C-3 i=0 c=P:14
    row 8 n=C-3 i=0 c=P:15
    row 10 n=C-3 i=0 c=P:16
    row 12 n=C-3 i=0 c=P:17
    row 14 n=C-3 i=0 c=P:18
  phrase 3
    row 0 n=C-3 i=0 c=P:19
    row 2 n=C-3 i=0 c=P:1A
    row 4 n=C-3 i=0 c=P:1B
    row 6 n=C-3 i=0 c=P:1C
    row 8 n=C-3 i=0 c=P:1D
    row 10 n=C-3 i=0 c=P:1E
    row 12 n=C-3 i=0 c=P:1F
    row 14 n=C-3 i=0 c=P:20
  phrase 4
    row 0 n=C-3 i=0 c=P:21
    row 2 n=C-3 i=0 c=P:22
    row 4 n=C-3 i=0 c=P:23
    row 6 n=C-3 i=0 c=P:24
    row 8 n=C-3 i=0 c=P:25
    row 10 n=C-3 i=0 c=P:26
    row 12 n=C-3 i=0 c=P:27
    row 14 n=C-3 i=0 c=P:28
  phrase 5
    row 0 n=C-3 i=0 c=P:29
    row 2 n=C-3 i=0 c=P:2A
    row 4 n=C-3 i=0 c=P:2B
    row 6 n=C-3 i=0 c=P:2C
    row 8 n=C-3 i=0 c=P:2D
    row 10 n=C-3 i=0 c=P:2E
    row 12 n=C-3 i=0 c=P:2F
    row 14 n=C-3 i=0 c=P:30
  phrase 6
    row 0 n=C-3 i=0 c=P:31
    row 2 n=C-3 i=0 c=P:32
    row 4 n=C-3 i=0 c=P:33
    row 6 n=C-3 i=0 c=P:34
    row 8 n=C-3 i=0 c=P:35
    row 10 n=C-3 i=0 c=P:36
    row 12 n=C-3 i=0 c=P:37
    row 14 n=C-3 i=0 c=P:38
  phrase 7
    row 0 n=C-3 i=0 c=P:39
    row 2 n=C-3 i=0 c=P:3A
    row 4 n=C-3 i=0 c=P:3B
    row 6 n=C-3 i=0 c=P:3C
    row 8 n=C-3 i=0 c=P:3D
    row 10 n=C-3 i=0 c=P:3E
    row 12 n=C-3 i=0 c=P:3F
    row 14 n=C-3 i=0 c=P:40
  chain pu1 0 1 2 3 4 5 6 7

case b_vib_depth
  desc V at speed 1 and every depth 0-F, PITCH = FAST: the depth table
  frames 1300
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF pitch=fast
  phrase 0
    row 0 n=C-5 i=0 c=V:10
    row 8 n=C-5 i=0 c=V:11
  phrase 1
    row 0 n=C-5 i=0 c=V:12
    row 8 n=C-5 i=0 c=V:13
  phrase 2
    row 0 n=C-5 i=0 c=V:14
    row 8 n=C-5 i=0 c=V:15
  phrase 3
    row 0 n=C-5 i=0 c=V:16
    row 8 n=C-5 i=0 c=V:17
  phrase 4
    row 0 n=C-5 i=0 c=V:18
    row 8 n=C-5 i=0 c=V:19
  phrase 5
    row 0 n=C-5 i=0 c=V:1A
    row 8 n=C-5 i=0 c=V:1B
  phrase 6
    row 0 n=C-5 i=0 c=V:1C
    row 8 n=C-5 i=0 c=V:1D
  phrase 7
    row 0 n=C-5 i=0 c=V:1E
    row 8 n=C-5 i=0 c=V:1F
  chain pu1 0 1 2 3 4 5 6 7

case b_vib_speed_fast
  desc V at every speed 0-F and depth F, PITCH = FAST: the phase step
  frames 1300
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF pitch=fast
  phrase 0
    row 0 n=C-5 i=0 c=V:0F
    row 8 n=C-5 i=0 c=V:1F
  phrase 1
    row 0 n=C-5 i=0 c=V:2F
    row 8 n=C-5 i=0 c=V:3F
  phrase 2
    row 0 n=C-5 i=0 c=V:4F
    row 8 n=C-5 i=0 c=V:5F
  phrase 3
    row 0 n=C-5 i=0 c=V:6F
    row 8 n=C-5 i=0 c=V:7F
  phrase 4
    row 0 n=C-5 i=0 c=V:8F
    row 8 n=C-5 i=0 c=V:9F
  phrase 5
    row 0 n=C-5 i=0 c=V:AF
    row 8 n=C-5 i=0 c=V:BF
  phrase 6
    row 0 n=C-5 i=0 c=V:CF
    row 8 n=C-5 i=0 c=V:DF
  phrase 7
    row 0 n=C-5 i=0 c=V:EF
    row 8 n=C-5 i=0 c=V:FF
  chain pu1 0 1 2 3 4 5 6 7

case b_vib_speed_tick
  desc V at every speed 0-F and depth F, PITCH = TICK: the phase step per tick
  frames 1300
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF pitch=tick
  phrase 0
    row 0 n=C-5 i=0 c=V:0F
    row 8 n=C-5 i=0 c=V:1F
  phrase 1
    row 0 n=C-5 i=0 c=V:2F
    row 8 n=C-5 i=0 c=V:3F
  phrase 2
    row 0 n=C-5 i=0 c=V:4F
    row 8 n=C-5 i=0 c=V:5F
  phrase 3
    row 0 n=C-5 i=0 c=V:6F
    row 8 n=C-5 i=0 c=V:7F
  phrase 4
    row 0 n=C-5 i=0 c=V:8F
    row 8 n=C-5 i=0 c=V:9F
  phrase 5
    row 0 n=C-5 i=0 c=V:AF
    row 8 n=C-5 i=0 c=V:BF
  phrase 6
    row 0 n=C-5 i=0 c=V:CF
    row 8 n=C-5 i=0 c=V:DF
  phrase 7
    row 0 n=C-5 i=0 c=V:EF
    row 8 n=C-5 i=0 c=V:FF
  chain pu1 0 1 2 3 4 5 6 7

case f_table_rows
  desc The same amplitudes with different envelope nibbles: is the nibble the row's length?
  note Three tables, all stepping 15 then 8: nibble 1, nibble 7, and a row
  note whose amplitude is zero between them.
  frames 1400
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF table=0
  inst 1 pulse env=F0 duty=2 sweep=FF table=1
  inst 2 pulse env=F0 duty=2 sweep=FF table=2
  table 0
    row 0 env=F1
    row 1 env=81
  table 1
    row 0 env=F7
    row 1 env=87
  table 2
    row 0 env=F1
    row 1 env=01
    row 2 env=81
  phrase 0
    row 0 n=C-5 i=0
    row 8 n=C-5 i=1
  phrase 1
    row 0 n=C-5 i=2
    row 8 n=C-5 i=0
  chain pu1 0 1

# --- (f) the table's row speed, read off a transpose column -----------------

case f_table_speed
  desc A table transposing 0 2 4 6 8 10 12 14: how many ticks is a row at the default speed?
  note The period changes are unambiguous where the volume rows were not, so
  note the spacing of the writes is the row length itself.
  frames 1200
  tempo 120
  inst 0 pulse env=F0 duty=2 sweep=FF table=0
  table 0
    row 0 tsp=00
    row 1 tsp=02
    row 2 tsp=04
    row 3 tsp=06
    row 4 tsp=08
    row 5 tsp=0A
    row 6 tsp=0C
    row 7 tsp=0E
  phrase 0
    row 0 n=C-5 i=0
  chain pu1 0
