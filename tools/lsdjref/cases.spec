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
